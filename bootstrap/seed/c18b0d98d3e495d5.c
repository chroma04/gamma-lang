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
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
struct gm_fr_check_link_glakqpq {
    gm_frame h;
    uint64_t l3; /* depth */
    gm_tl572d7579hns ret;
    gm_b0_1 w0;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_for_body_0h3rgr6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_6l4llq8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_hrv26t6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_member_names_17el54k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
    gm_b0_1 w0;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
gm_b0_1 gm_dtu50j4i95ke_0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0h3rgr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_3400t3p(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_4g8ig10(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6l4llq8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_fe8qj55(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hrv26t6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vetgq3s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
GM_NOINLINE bool gm_pf1m4b8q4e78(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_c01amogfnq1[3] __attribute__((aligned(1)));
extern uint8_t gm_c163provqtj[15] __attribute__((aligned(1)));
extern uint8_t gm_c1qv4qjgsc9[7] __attribute__((aligned(1)));
extern uint8_t gm_c2uvga3a33u[9] __attribute__((aligned(1)));
extern uint8_t gm_c3512j75mfr[9] __attribute__((aligned(1)));
extern uint8_t gm_c36pbvga8vj[10] __attribute__((aligned(1)));
extern uint8_t gm_c37f79jqcno[3] __attribute__((aligned(1)));
extern uint8_t gm_c4h4g2jdbns[11] __attribute__((aligned(1)));
extern uint8_t gm_c4j9utibsmh[5] __attribute__((aligned(1)));
extern uint8_t gm_c56m9jg65kg[3] __attribute__((aligned(1)));
extern uint8_t gm_c5dv10uirua[16] __attribute__((aligned(8)));
extern uint8_t gm_c5p3be4v7rl[12] __attribute__((aligned(1)));
extern uint8_t gm_c644mb9o5bq[16] __attribute__((aligned(8)));
extern uint8_t gm_c6bqo8ii8vq[7] __attribute__((aligned(1)));
extern uint8_t gm_c6cn111uula[6] __attribute__((aligned(1)));
extern uint8_t gm_c71qftp734m[17] __attribute__((aligned(1)));
extern uint8_t gm_c7ri5vg1pkt[11] __attribute__((aligned(1)));
extern uint8_t gm_c83hsvhq98b[8] __attribute__((aligned(8)));
extern uint8_t gm_c9eimjm1h83[6] __attribute__((aligned(1)));
extern uint8_t gm_c9h85it1mgi[5] __attribute__((aligned(1)));
extern uint8_t gm_cahcs2deqne[14] __attribute__((aligned(1)));
extern uint8_t gm_ccl8cvpv516[12] __attribute__((aligned(1)));
extern uint8_t gm_ccoccdcv92t[13] __attribute__((aligned(1)));
extern uint8_t gm_ccqi23qab2q[13] __attribute__((aligned(1)));
extern uint8_t gm_cdhuk7ib5ef[272] __attribute__((aligned(8)));
extern uint8_t gm_cdt6le1o3ns[23] __attribute__((aligned(1)));
extern uint8_t gm_cetnk59fqdb[4] __attribute__((aligned(1)));
extern uint8_t gm_cf39qh9kr14[12] __attribute__((aligned(1)));
extern uint8_t gm_cf61b9g709r[1] __attribute__((aligned(1)));
extern uint8_t gm_cg5b54h69cm[3] __attribute__((aligned(1)));
extern uint8_t gm_cg68ocegidi[44] __attribute__((aligned(1)));
extern uint8_t gm_cgofrj6eqlu[4] __attribute__((aligned(4)));
extern uint8_t gm_cjcj297i683[1] __attribute__((aligned(1)));
extern uint8_t gm_cjia9g10m37[41] __attribute__((aligned(1)));
extern uint8_t gm_cjr4mp0de0q[13] __attribute__((aligned(1)));
extern uint8_t gm_cka88d5t8lu_2[16] __attribute__((aligned(8)));
extern uint8_t gm_ckdcil0k80m[5] __attribute__((aligned(1)));
extern uint8_t gm_ckifm27qmcp[8] __attribute__((aligned(1)));
extern uint8_t gm_ckji37gt5og[9] __attribute__((aligned(1)));
extern uint8_t gm_cl4mi9no7ck[102] __attribute__((aligned(1)));
extern uint8_t gm_cl9mf80irg3[3] __attribute__((aligned(1)));
extern uint8_t gm_clslitrb510[16] __attribute__((aligned(8)));
extern uint8_t gm_cm18055k83v[13] __attribute__((aligned(1)));
extern uint8_t gm_cnl0lngt3i5[10] __attribute__((aligned(1)));
extern uint8_t gm_cpkiil8fc4e[6] __attribute__((aligned(1)));
extern uint8_t gm_cqcbpnidn0k[6] __attribute__((aligned(1)));
extern uint8_t gm_cqqto88k0f1[7] __attribute__((aligned(1)));
extern uint8_t gm_cri7ilp8buh[8] __attribute__((aligned(1)));
extern uint8_t gm_crui1aejv1g[11] __attribute__((aligned(1)));
extern uint8_t gm_cspj8kugbce[11] __attribute__((aligned(1)));
extern uint8_t gm_ctac819ec20[25] __attribute__((aligned(1)));
extern uint8_t gm_ctk0j7slrnl[10] __attribute__((aligned(1)));
extern uint8_t gm_cucf5kd1i0t[4] __attribute__((aligned(1)));
extern uint8_t gm_cuqbj62gqhf[16] __attribute__((aligned(8)));
extern uint8_t gm_cvjtf68avci[12] __attribute__((aligned(1)));
extern void *const gm_hdu50j4i95ke[];
extern void *const gm_hl5iavnkj4ht[];
extern void *const gm_hl9hta9cqrqu[];
extern void *const gm_hlak2jvvbqg4[];
extern void *const gm_hlhqfdrl9vfc[];
extern void *const gm_hlkic73ff07n[];
extern void *const gm_hlob66lsuf8h[];
extern void *const gm_hlu84m640fm9[];
extern uint8_t gm_k22qkkqdt5e[40] __attribute__((aligned(8)));
extern uint8_t gm_k2ok1rai48t[40] __attribute__((aligned(8)));
extern uint8_t gm_k3mmr0p0mh8[40] __attribute__((aligned(8)));
extern uint8_t gm_k61ski6fvbu[40] __attribute__((aligned(8)));
extern uint8_t gm_k647t05rm86[40] __attribute__((aligned(8)));
extern uint8_t gm_k68ol137ncd[40] __attribute__((aligned(8)));
extern uint8_t gm_k7tijt756kp[40] __attribute__((aligned(8)));
extern uint8_t gm_k8gk4puesad[40] __attribute__((aligned(8)));
extern uint8_t gm_kba69021uoh[40] __attribute__((aligned(8)));
extern uint8_t gm_ke30i57o09o[40] __attribute__((aligned(8)));
extern uint8_t gm_kec2ujc0693[40] __attribute__((aligned(8)));
extern uint8_t gm_kei7njaa9je[40] __attribute__((aligned(8)));
extern uint8_t gm_kg2a0b5p4sm[40] __attribute__((aligned(8)));
extern uint8_t gm_khorl1vfvs4[40] __attribute__((aligned(8)));
extern uint8_t gm_kklngajk7ov[40] __attribute__((aligned(8)));
extern uint8_t gm_klhvim4pn5v[40] __attribute__((aligned(8)));
extern uint8_t gm_koalr8ndd0a[40] __attribute__((aligned(8)));
extern uint8_t gm_koedfqu36hd[40] __attribute__((aligned(8)));
extern uint8_t gm_kr3abq56gec[40] __attribute__((aligned(8)));
extern uint8_t gm_krsnqpdcjf4[40] __attribute__((aligned(8)));
extern uint8_t gm_krt7450irj2[40] __attribute__((aligned(8)));
extern uint8_t gm_kssv18460i9[40] __attribute__((aligned(8)));
extern uint8_t gm_ku7k9fgdnj6[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0021gvebcdc4[9];
extern const uint8_t gm_s0147vgtvilnq[19];
extern const uint8_t gm_s039kgf77ln8v[41];
extern const uint8_t gm_s049rlfhk3i2d[6];
extern const uint8_t gm_s05qp4ie7rju8[84];
extern const uint8_t gm_s09fhtm8l4rep[4];
extern const uint8_t gm_s09r5jslo2reb[13];
extern const uint8_t gm_s0br9o46sotd5[41];
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s0jhhkkbgc6ks[18];
extern const uint8_t gm_s0kajv5j53ub7[38];
extern const uint8_t gm_s0l40gug6qpuq[17];
extern const uint8_t gm_s0lh54534p3vd[10];
extern const uint8_t gm_s0lpuem1an6p4[7];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0mtf4em37at6[29];
extern const uint8_t gm_s0n03nrf296lf[33];
extern const uint8_t gm_s0njd7khs134c[31];
extern const uint8_t gm_s0o0qjspcfsmh[5];
extern const uint8_t gm_s0orlismakhop[16];
extern const uint8_t gm_s0pf2spartj4v[12];
extern const uint8_t gm_s0pm20lshv21u[21];
extern const uint8_t gm_s0rhh4q7cpm4k[20];
extern const uint8_t gm_s0ri8hi6r25m8[37];
extern const uint8_t gm_s0s0aq2lhn254[22];
extern const uint8_t gm_s0u0sleiibm5v[3];
extern const uint8_t gm_s142hf0mc48om[41];
extern const uint8_t gm_s14frn0dagfs0[25];
extern const uint8_t gm_s16vnj1m5rn6t[9];
extern const uint8_t gm_s17uhrtdo7u30[2];
extern const uint8_t gm_s1a10cqdk6nil[19];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1br3p106uhjq[10];
extern const uint8_t gm_s1cbu3jc19u4t[36];
extern const uint8_t gm_s1dc9boe5unhg[32];
extern const uint8_t gm_s1g69u68g0ra5[3];
extern const uint8_t gm_s1gjt02j7um1i[37];
extern const uint8_t gm_s1isj7vodtbvg[37];
extern const uint8_t gm_s1js4dn5e65ja[11];
extern const uint8_t gm_s1o010pu82atd[17];
extern const uint8_t gm_s1r2hqmnh3o3j[16];
extern const uint8_t gm_s1rmhdquo3v18[29];
extern const uint8_t gm_s1rqgg2ibsis6[10];
extern const uint8_t gm_s1sbi8ubhu50k[51];
extern const uint8_t gm_s1t08grtrn18m[9];
extern const uint8_t gm_s1t9ao79ferut[22];
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s22lkrbtp01c5[23];
extern const uint8_t gm_s23t712hkqlmp[5];
extern const uint8_t gm_s24mnh1a25k0f[31];
extern const uint8_t gm_s25jkl4c6n8uo[4];
extern const uint8_t gm_s263r2tflkl4v[25];
extern const uint8_t gm_s29885s5blpal[13];
extern const uint8_t gm_s2aq94adogrjl[35];
extern const uint8_t gm_s2bnc7q68giqi[17];
extern const uint8_t gm_s2btotlgsaibr[41];
extern const uint8_t gm_s2cd81u5542io[10];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2io5nh7o36cj[15];
extern const uint8_t gm_s2iv492pjqjc4[12];
extern const uint8_t gm_s2kl2d59c04nf[19];
extern const uint8_t gm_s2nj060fs0sf4[6];
extern const uint8_t gm_s2o3od9vb6lnu[12];
extern const uint8_t gm_s2red66k9r4fi[10];
extern const uint8_t gm_s2s2f3gkf4c98[13];
extern const uint8_t gm_s2t2dldl583td[24];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s2umangg4hn1g[15];
extern const uint8_t gm_s3154a2sfmm32[18];
extern const uint8_t gm_s3347hvco05di[6];
extern const uint8_t gm_s34n6pusofh71[8];
extern const uint8_t gm_s38n4b5q0dbil[45];
extern const uint8_t gm_s39bdeo03pq18[9];
extern const uint8_t gm_s3avlcjhdh976[10];
extern const uint8_t gm_s3bsv9idfdodt[7];
extern const uint8_t gm_s3dhhc4laniae[22];
extern const uint8_t gm_s3e38enju83br[209];
extern const uint8_t gm_s3gqldvcud6t6[17];
extern const uint8_t gm_s3hloevtmnfuc[8];
extern const uint8_t gm_s3ilkivmnrfk4[28];
extern const uint8_t gm_s3lho0ls7g9c5[40];
extern const uint8_t gm_s3lq4s2q8ds4c[32];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3plkb3in5251[9];
extern const uint8_t gm_s3q858hngo0ho[39];
extern const uint8_t gm_s3tgka429lf13[60];
extern const uint8_t gm_s3v0kr2fdpfld[5];
extern const uint8_t gm_s420tm9ua7ggg[4];
extern const uint8_t gm_s44ek1gecbajs[7];
extern const uint8_t gm_s47llp68ptjgu[8];
extern const uint8_t gm_s49bn4rn2il7d[26];
extern const uint8_t gm_s4a980fahtblo[3];
extern const uint8_t gm_s4fips0vo4jt3[8];
extern const uint8_t gm_s4g2k7ups3ui6[6];
extern const uint8_t gm_s4g3cajt15dmi[3];
extern const uint8_t gm_s4h0qjq5qavbn[10];
extern const uint8_t gm_s4iasgp71ld87[41];
extern const uint8_t gm_s4jd1uj5sjs8a[41];
extern const uint8_t gm_s4k8r5f4ncaqi[10];
extern const uint8_t gm_s4ld5r1oi34jh[18];
extern const uint8_t gm_s4lpfn3di4rp9[66];
extern const uint8_t gm_s4oil3epcv041[3];
extern const uint8_t gm_s4s2kvgh5crk6[63];
extern const uint8_t gm_s4vd6e3mkiiso[3];
extern const uint8_t gm_s4vnlpe2ovpj8[6];
extern const uint8_t gm_s542rn9shphg8[16];
extern const uint8_t gm_s54psrmgl6ti7[9];
extern const uint8_t gm_s54vpa0pj59v6[8];
extern const uint8_t gm_s554r118mfcao[34];
extern const uint8_t gm_s58f7dj21gtf9[18];
extern const uint8_t gm_s5aqsh8ujnh9q[38];
extern const uint8_t gm_s5c1hnl87iu5m[9];
extern const uint8_t gm_s5c5548ll8taj[10];
extern const uint8_t gm_s5dgs745ff5ko[39];
extern const uint8_t gm_s5dmjeoiprfiv[72];
extern const uint8_t gm_s5e0seq0he7ou[10];
extern const uint8_t gm_s5i2d5hm88htd[4];
extern const uint8_t gm_s5kue8bfcvqfr[27];
extern const uint8_t gm_s5l2i6ke0df90[21];
extern const uint8_t gm_s5llvatkc5h5i[14];
extern const uint8_t gm_s5lr3gdic3o7e[15];
extern const uint8_t gm_s5m8hf43qu2i7[27];
extern const uint8_t gm_s5p0i898s3u4j[11];
extern const uint8_t gm_s5ppm345dr9s1[15];
extern const uint8_t gm_s5qoh8o6sn63e[24];
extern const uint8_t gm_s5ruqkgqqrd34[17];
extern const uint8_t gm_s5s758m9u7gle[26];
extern const uint8_t gm_s5u5a7gd2hl9p[6];
extern const uint8_t gm_s60bni5maq4df[12];
extern const uint8_t gm_s66b184memqnd[4];
extern const uint8_t gm_s66ph0gfa95db[14];
extern const uint8_t gm_s677r76pa1oks[7];
extern const uint8_t gm_s67i6nrmepi0v[7];
extern const uint8_t gm_s68manpbqruhq[61];
extern const uint8_t gm_s6b88abo6eubp[43];
extern const uint8_t gm_s6bsma1542aam[45];
extern const uint8_t gm_s6cchf3tcs9e4[21];
extern const uint8_t gm_s6cs6skh2j334[17];
extern const uint8_t gm_s6ecg17hs903q[9];
extern const uint8_t gm_s6f1t1a0knpo4[82];
extern const uint8_t gm_s6g1iihs2bped[46];
extern const uint8_t gm_s6h791v5191sh[11];
extern const uint8_t gm_s6mdrbgb4t376[12];
extern const uint8_t gm_s6nsj8sc6uhgg[17];
extern const uint8_t gm_s6o30d8d984s9[8];
extern const uint8_t gm_s6of1cedjphs7[3];
extern const uint8_t gm_s6p9ub6gu603m[7];
extern const uint8_t gm_s6qodcf63v3t5[16];
extern const uint8_t gm_s6qvm08vrap69[4];
extern const uint8_t gm_s6rsd5hkg3dmo[6];
extern const uint8_t gm_s6t4r831mukdr[3];
extern const uint8_t gm_s6tv3tkgkodqm[12];
extern const uint8_t gm_s6ug9r8dlfd2a[44];
extern const uint8_t gm_s6unleea2pm63[35];
extern const uint8_t gm_s71712nhgrg2u[15];
extern const uint8_t gm_s72hk5u5ngq4i[11];
extern const uint8_t gm_s72phirci8af3[25];
extern const uint8_t gm_s75kntcasmsv8[14];
extern const uint8_t gm_s75s8qhd4m46e[6];
extern const uint8_t gm_s77fi68iu5q6n[5];
extern const uint8_t gm_s7826b3jmrqb0[10];
extern const uint8_t gm_s79a8ngj68lbf[5];
extern const uint8_t gm_s79oh9p16aa8u[3];
extern const uint8_t gm_s7c9tkfjfc169[26];
extern const uint8_t gm_s7d1996rqhrdg[9];
extern const uint8_t gm_s7euumoo24h31[3];
extern const uint8_t gm_s7f68g16j7a3o[6];
extern const uint8_t gm_s7jb41578vpai[24];
extern const uint8_t gm_s7m5v91ik4pn5[52];
extern const uint8_t gm_s7md88e2dla9a[9];
extern const uint8_t gm_s7mfvroqmll1f[3];
extern const uint8_t gm_s7o0qkitvahn6[41];
extern const uint8_t gm_s7qmtc95nc11i[24];
extern const uint8_t gm_s7ucknluds45h[5];
extern const uint8_t gm_s7ui29rhp3vg3[24];
extern const uint8_t gm_s80l47nhae4el[23];
extern const uint8_t gm_s83ukg5uj91s8[41];
extern const uint8_t gm_s87h4fof1n9ou[11];
extern const uint8_t gm_s882hbgnf10vn[21];
extern const uint8_t gm_s8847rbf0kqmg[12];
extern const uint8_t gm_s88n2j7klshfh[11];
extern const uint8_t gm_s8abg3vemofr6[4];
extern const uint8_t gm_s8bgt7glif1c9[34];
extern const uint8_t gm_s8lp1qs73nh4k[17];
extern const uint8_t gm_s8m45dpj5ugr4[6];
extern const uint8_t gm_s8n5rdam5o1sm[69];
extern const uint8_t gm_s8s91tiu5r15q[45];
extern const uint8_t gm_s8tadn8f0l2a8[21];
extern const uint8_t gm_s90saa0unq8e7[35];
extern const uint8_t gm_s927d308r5uo5[6];
extern const uint8_t gm_s92h6r7qs6q4f[41];
extern const uint8_t gm_s92lujmr0a2c3[35];
extern const uint8_t gm_s93i7ga7kbfpu[34];
extern const uint8_t gm_s93qviqoij24b[31];
extern const uint8_t gm_s98peh781o3et[51];
extern const uint8_t gm_s99ou2clapmm5[300];
extern const uint8_t gm_s9agk8q084c5k[8];
extern const uint8_t gm_s9b72a1n5tssp[78];
extern const uint8_t gm_s9c2oj94op2h6[10];
extern const uint8_t gm_s9d2hbf54sh7s[22];
extern const uint8_t gm_s9dgf2prvg715[6];
extern const uint8_t gm_s9gu0s8i5nn2u[9];
extern const uint8_t gm_s9ijlf0rj4app[21];
extern const uint8_t gm_s9kou5ia9uri9[6];
extern const uint8_t gm_s9lvs84vghure[12];
extern const uint8_t gm_s9nc0lk9srfom[8];
extern const uint8_t gm_s9ob1jv3b8md9[10];
extern const uint8_t gm_s9pljh6s0n7in[17];
extern const uint8_t gm_s9r71kmnlqpv8[17];
extern const uint8_t gm_s9rujqsrrjcho[110];
extern const uint8_t gm_s9sev457i3j7n[5];
extern const uint8_t gm_s9tth144tddms[13];
extern const uint8_t gm_s9v13eck7gubi[4];
extern const uint8_t gm_sa3obh41s8f7p[70];
extern const uint8_t gm_sa65ola9dm4t8[8];
extern const uint8_t gm_sa8k1i3b5phk8[24];
extern const uint8_t gm_sa8vf0vlkso00[30];
extern const uint8_t gm_sabu5uehm65n2[22];
extern const uint8_t gm_sadog1f5187ms[94];
extern const uint8_t gm_sadscuj628cl7[8];
extern const uint8_t gm_saempncqkf91r[3];
extern const uint8_t gm_saf1c3tfhshf0[10];
extern const uint8_t gm_samqd3pmlgqmg[14];
extern const uint8_t gm_sao4mumvadoct[4];
extern const uint8_t gm_sape9vso1m5fa[2];
extern const uint8_t gm_saqbkgqthogiq[30];
extern const uint8_t gm_saqgvkkehjsjb[33];
extern const uint8_t gm_saqhctf8022h0[33];
extern const uint8_t gm_sas0hkqll4io0[10];
extern const uint8_t gm_sasdd0129vf8p[20];
extern const uint8_t gm_sashkcsm7eqk7[11];
extern const uint8_t gm_sau1b8ivpr0us[8];
extern const uint8_t gm_sb33sthvnbnb9[23];
extern const uint8_t gm_sb423ab672pgh[8];
extern const uint8_t gm_sb5it581p5nll[5];
extern const uint8_t gm_sb6brpqpm5qjh[21];
extern const uint8_t gm_sb7hastj3mkf0[36];
extern const uint8_t gm_sb86a9k7pfmnh[22];
extern const uint8_t gm_sb8829ojmkbv6[6];
extern const uint8_t gm_sb9k4ip91d7oa[6];
extern const uint8_t gm_sbjsogn1fh76q[41];
extern const uint8_t gm_sbkmg1rlq4fa7[27];
extern const uint8_t gm_sbmedt8vmt5ve[5];
extern const uint8_t gm_sbmlclkb2d9o9[74];
extern const uint8_t gm_sboedjgihdapp[81];
extern const uint8_t gm_sbogo0q916fbh[19];
extern const uint8_t gm_sbpcdsg6ifd91[5];
extern const uint8_t gm_sbqbp3u228b97[30];
extern const uint8_t gm_sbqjqj9ajcalm[80];
extern const uint8_t gm_sbtq9nj91tefv[10];
extern const uint8_t gm_sbui4p06qp6ck[29];
extern const uint8_t gm_sbvdm2nk31pvh[9];
extern const uint8_t gm_sc0vbtqan26pc[18];
extern const uint8_t gm_sc19lloml9nb7[17];
extern const uint8_t gm_sc22qkqa9q462[16];
extern const uint8_t gm_sc2e7c2ag4q0u[12];
extern const uint8_t gm_sc2tqolbo0u3i[32];
extern const uint8_t gm_sc49g68j2cekh[7];
extern const uint8_t gm_sc51jgguf848t[70];
extern const uint8_t gm_sc8obklajb0e7[40];
extern const uint8_t gm_scaaj480d4i26[9];
extern const uint8_t gm_scaduriptfv6s[34];
extern const uint8_t gm_scd25kom6sdt8[34];
extern const uint8_t gm_scd34uli7vuuj[20];
extern const uint8_t gm_scedkbh0j6d78[6];
extern const uint8_t gm_scf54410fgl4j[39];
extern const uint8_t gm_scf7jg05jni41[5];
extern const uint8_t gm_scgm14ju6d0p8[15];
extern const uint8_t gm_schd55rs2s02f[11];
extern const uint8_t gm_scj9op1emegq8[13];
extern const uint8_t gm_sckv4ch46j06c[36];
extern const uint8_t gm_scmbb8a9enevr[16];
extern const uint8_t gm_scmc155kidjub[41];
extern const uint8_t gm_scs8vqh2gmmhd[51];
extern const uint8_t gm_scvhv26j3m3dm[18];
extern const uint8_t gm_sd2s2op6i577j[36];
extern const uint8_t gm_sd48v0fh5kl37[24];
extern const uint8_t gm_sd6vqnf9glo8m[51];
extern const uint8_t gm_sd9ri1vmgds82[9];
extern const uint8_t gm_sdavnmflj0bfl[4];
extern const uint8_t gm_sdccotu1l20mb[7];
extern const uint8_t gm_sdhjq72t0cbpl[9];
extern const uint8_t gm_sdkah956ljhi9[19];
extern const uint8_t gm_sdmtl8vq6k6qg[13];
extern const uint8_t gm_sdo8qcbf74mp0[7];
extern const uint8_t gm_sdpf7uf4ias00[24];
extern const uint8_t gm_sdpkm2aceiv8l[47];
extern const uint8_t gm_sdqgviehbfoad[6];
extern const uint8_t gm_sdsg5rjte31pl[15];
extern const uint8_t gm_sdtnu56jsuhl2[8];
extern const uint8_t gm_sdv6tt97thssp[22];
extern const uint8_t gm_se0oi28sapu01[11];
extern const uint8_t gm_se1krm9tn2oaa[3];
extern const uint8_t gm_se20eu14i6umf[9];
extern const uint8_t gm_se287i5v97ugv[8];
extern const uint8_t gm_se8us8vie9a8a[37];
extern const uint8_t gm_sead5oitv3opu[39];
extern const uint8_t gm_sed3elknidrs3[11];
extern const uint8_t gm_sedrsudco1u16[10];
extern const uint8_t gm_see48nut40kq2[13];
extern const uint8_t gm_seg00g6ievv09[41];
extern const uint8_t gm_seggohi50ng09[4];
extern const uint8_t gm_seleu2adn5qnd[18];
extern const uint8_t gm_sem5pak34704h[35];
extern const uint8_t gm_ser8ip7vtbv6c[9];
extern const uint8_t gm_ses7tdrdt8a6u[6];
extern const uint8_t gm_ses9h0rg965i4[54];
extern const uint8_t gm_set3pi1jhetbi[7];
extern const uint8_t gm_set8ntrenb1qd[18];
extern const uint8_t gm_sevnjih7nf5ja[41];
extern const uint8_t gm_sf022jqpe0964[11];
extern const uint8_t gm_sf0p9o7g7fr6l[7];
extern const uint8_t gm_sf2lr85hprd3e[40];
extern const uint8_t gm_sf9difklo6omg[17];
extern const uint8_t gm_sfbjh3dq66kkg[17];
extern const uint8_t gm_sfctoi611a7ft[64];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfi2ha42r1nr2[2];
extern const uint8_t gm_sfjs7cccbf3qf[56];
extern const uint8_t gm_sfm11uv2bn7us[29];
extern const uint8_t gm_sfm8ps2e26dh1[3];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sfqk3rro4g7t0[2];
extern const uint8_t gm_sfrrqniqpcb8o[13];
extern const uint8_t gm_sfubbk3hjkq2r[41];
extern const uint8_t gm_sg1s2lngtt92k[13];
extern const uint8_t gm_sg398rgmck67b[4];
extern const uint8_t gm_sg4edpe0eroav[8];
extern const uint8_t gm_sg4nlara1m052[14];
extern const uint8_t gm_sg4pgef1hupjq[45];
extern const uint8_t gm_sg50kn7lo9hgn[86];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg9gm9epbpvie[22];
extern const uint8_t gm_sg9l06oo908p3[45];
extern const uint8_t gm_sgb522dnrltds[37];
extern const uint8_t gm_sgboe8ukotqh2[51];
extern const uint8_t gm_sge745dj3cnrb[73];
extern const uint8_t gm_sgfqei77bsia8[4];
extern const uint8_t gm_sggg4cm5q63i6[5];
extern const uint8_t gm_sgj7bt7nru3s1[27];
extern const uint8_t gm_sgks95jf6fs73[21];
extern const uint8_t gm_sgqejiat83g1e[47];
extern const uint8_t gm_sgs65cm0mtgm4[14];
extern const uint8_t gm_sgsd94uuisj8t[73];
extern const uint8_t gm_sgt05f0dtajpv[18];
extern const uint8_t gm_sgtu622jht4g2[51];
extern const uint8_t gm_sgukm1evmrjal[4];
extern const uint8_t gm_sgvhjfacilshn[9];
extern const uint8_t gm_sh1v4dof73kmv[7];
extern const uint8_t gm_sh3h2k0f6ktgl[13];
extern const uint8_t gm_sh40na9qbga3l[2];
extern const uint8_t gm_sh5qb2l9hubh3[9];
extern const uint8_t gm_sh75rh8dk51gf[12];
extern const uint8_t gm_sh7grf0rgudkg[49];
extern const uint8_t gm_sh89s9kbi478a[10];
extern const uint8_t gm_sh8houjk34evm[15];
extern const uint8_t gm_sheobcjeiocck[16];
extern const uint8_t gm_shjbutqe8bgfp[49];
extern const uint8_t gm_shn6qvu5196go[10];
extern const uint8_t gm_shnco6rh33tl5[9];
extern const uint8_t gm_shohh3180gu26[7];
extern const uint8_t gm_shol80fqem8c6[48];
extern const uint8_t gm_shpjiqltl04un[33];
extern const uint8_t gm_shrmh6t9a5b3g[15];
extern const uint8_t gm_shsno1f4se8ej[15];
extern const uint8_t gm_shu2irk2mdah6[14];
extern const uint8_t gm_shv4snn6f8hcr[29];
extern const uint8_t gm_si1m7q6ia0hto[7];
extern const uint8_t gm_si2ih874n1gou[10];
extern const uint8_t gm_si34mhnsv5dtr[8];
extern const uint8_t gm_si3r4kcu7q6os[8];
extern const uint8_t gm_si416c0mdacrd[5];
extern const uint8_t gm_si4l87dl4tpkq[8];
extern const uint8_t gm_si5ao9la9pflk[19];
extern const uint8_t gm_si5gf6931vagv[11];
extern const uint8_t gm_si702r0b37nml[6];
extern const uint8_t gm_si9mfda4b719e[63];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sic09fl7nt2ch[27];
extern const uint8_t gm_sidairr6d8hcq[45];
extern const uint8_t gm_sighahslvsc1i[11];
extern const uint8_t gm_sigmhs8muccg1[28];
extern const uint8_t gm_siisa3jb2ksa3[4];
extern const uint8_t gm_sijc9cvc14bto[9];
extern const uint8_t gm_sildigrhtv45e[18];
extern const uint8_t gm_simmbi8o1lo4k[46];
extern const uint8_t gm_siqfg68f4ud6j[16];
extern const uint8_t gm_sir2n5292lks8[8];
extern const uint8_t gm_sirn08c749sdd[28];
extern const uint8_t gm_sirqj5pqb9rph[58];
extern const uint8_t gm_siv3d9he09v3r[44];
extern const uint8_t gm_sivdpq4bdf3h5[17];
extern const uint8_t gm_sj0ur1o7a5584[16];
extern const uint8_t gm_sj1d6tstgco64[89];
extern const uint8_t gm_sj1k4t2de5144[6];
extern const uint8_t gm_sj2if4k4r1jer[8];
extern const uint8_t gm_sj2tf1ift8sga[21];
extern const uint8_t gm_sj62oio2kg58e[20];
extern const uint8_t gm_sj64h79nh4kvi[57];
extern const uint8_t gm_sj66n4fl4pp5d[18];
extern const uint8_t gm_sjcm18p35o9rq[4];
extern const uint8_t gm_sjcsd1e8rp9jn[43];
extern const uint8_t gm_sjdenof9qo6nf[12];
extern const uint8_t gm_sjfpr08jc3ep1[23];
extern const uint8_t gm_sjgpba6e9i17v[20];
extern const uint8_t gm_sjid6lq0hib0h[3];
extern const uint8_t gm_sjjavpa3r1ahk[41];
extern const uint8_t gm_sjk23rfp9022d[14];
extern const uint8_t gm_sjlci8b0qvbpc[3];
extern const uint8_t gm_sjlnsaoa4hibc[8];
extern const uint8_t gm_sjn4odi0ubper[4];
extern const uint8_t gm_sjoaauk0ndamd[16];
extern const uint8_t gm_sjqp7nokhg4lk[5];
extern const uint8_t gm_sjs17a3d2ohel[35];
extern const uint8_t gm_sk0nmb4pv74eb[96];
extern const uint8_t gm_sk0r84vdcvmnk[13];
extern const uint8_t gm_sk16sqfpds4ga[18];
extern const uint8_t gm_sk2b59ovo43qg[6];
extern const uint8_t gm_sk3630dksnfoh[24];
extern const uint8_t gm_sk754mj3p1el6[2];
extern const uint8_t gm_sk96ie971jidi[4];
extern const uint8_t gm_sk9jo4s16ifdk[9];
extern const uint8_t gm_skarrfopl04v8[23];
extern const uint8_t gm_skau91fmi0eei[42];
extern const uint8_t gm_skb9m8a8jekvg[41];
extern const uint8_t gm_skd5c72k4pcr6[23];
extern const uint8_t gm_skfff1ladr223[23];
extern const uint8_t gm_ski6lj2hn0gg4[35];
extern const uint8_t gm_skiv4voi8bqij[20];
extern const uint8_t gm_skmt2kt2p7v0u[3];
extern const uint8_t gm_skn25h7mjcuvp[84];
extern const uint8_t gm_skocj074p3r97[9];
extern const uint8_t gm_sksrclikobn5n[37];
extern const uint8_t gm_skub5dfrnu5tr[18];
extern const uint8_t gm_sl0eq2rejro5l[18];
extern const uint8_t gm_sl1f0va0l61bh[23];
extern const uint8_t gm_sl1f8ibfoc1i1[13];
extern const uint8_t gm_sl325guoc37g3[23];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl815v6oegv7n[14];
extern const uint8_t gm_sl83jfd1fgvi2[7];
extern const uint8_t gm_slbs27elv52b5[30];
extern const uint8_t gm_slgjpcgk90nuf[5];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slhuk1lrfcjqc[32];
extern const uint8_t gm_sliea8310b9mm[11];
extern const uint8_t gm_slkqcsgfdkn5k[41];
extern const uint8_t gm_sll95m1eva5e1[24];
extern const uint8_t gm_slllafvm0pbeb[6];
extern const uint8_t gm_slmeqnp27emg7[9];
extern const uint8_t gm_slntkh69kcfmm[8];
extern const uint8_t gm_slpp558cs8j3v[36];
extern const uint8_t gm_slsi5c1p764pd[8];
extern const uint8_t gm_slvu7caka1qb7[10];
extern const uint8_t gm_sm3vri4hmq0fl[13];
extern const uint8_t gm_sm6j3q0d4n2jp[3];
extern const uint8_t gm_sm6jfloe4be09[27];
extern const uint8_t gm_sm884rqp4fp7u[68];
extern const uint8_t gm_sm922ui8k0oca[9];
extern const uint8_t gm_smadkict0htlf[38];
extern const uint8_t gm_smb8nsuj9ie93[15];
extern const uint8_t gm_smbbaf3n1gml2[8];
extern const uint8_t gm_smdiuajagl2mn[5];
extern const uint8_t gm_smiidqh71uo72[10];
extern const uint8_t gm_smilh4u3um90d[13];
extern const uint8_t gm_smjeoupcicact[10];
extern const uint8_t gm_smk40q3nf1t3o[32];
extern const uint8_t gm_smkkcjkpmt4r9[11];
extern const uint8_t gm_smlfucs8bng1c[38];
extern const uint8_t gm_smn7jqhcc3v5b[10];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smovt8m5cpa13[7];
extern const uint8_t gm_smuiobhjc3kll[30];
extern const uint8_t gm_sn0ocj5vv0ci3[114];
extern const uint8_t gm_sn0ostovrrp1d[15];
extern const uint8_t gm_sn12gub8tjj5h[6];
extern const uint8_t gm_sn5naisar83li[16];
extern const uint8_t gm_snaenr23tbgmb[23];
extern const uint8_t gm_snak43jlv01k6[89];
extern const uint8_t gm_snb1pvju20aqn[7];
extern const uint8_t gm_snbrjpgiqf592[6];
extern const uint8_t gm_snesmv2gbtsbb[14];
extern const uint8_t gm_snetlri6r5cbl[54];
extern const uint8_t gm_snh0sl2cdagdn[13];
extern const uint8_t gm_snhb827gevj6u[26];
extern const uint8_t gm_sniofsn8cqk74[10];
extern const uint8_t gm_snjonc4tmu56e[21];
extern const uint8_t gm_snp3hu7kun7ag[23];
extern const uint8_t gm_snq87gidv4aub[9];
extern const uint8_t gm_snqq3153le4es[105];
extern const uint8_t gm_snrrubbi4s05g[16];
extern const uint8_t gm_snsbh43f5eo46[5];
extern const uint8_t gm_snsq190kog2e4[60];
extern const uint8_t gm_snsqui8o89u28[15];
extern const uint8_t gm_snst06bp2i1vp[23];
extern const uint8_t gm_snupav63sairp[12];
extern const uint8_t gm_snv7mc8ntub24[48];
extern const uint8_t gm_snvncjka7ph6m[7];
extern const uint8_t gm_so01usvt6ukbi[59];
extern const uint8_t gm_so0h0lkl6bp4l[39];
extern const uint8_t gm_so28toh6qcgon[11];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so7svtiv1a8q1[5];
extern const uint8_t gm_so81pe3qfh0hc[15];
extern const uint8_t gm_soa7cbv241ih1[11];
extern const uint8_t gm_sobb0dbnbb0kc[56];
extern const uint8_t gm_sodq6lf7v3uc2[11];
extern const uint8_t gm_sodqgrksuhvj9[10];
extern const uint8_t gm_sogkbe8nre26a[17];
extern const uint8_t gm_sogrqv5el59r8[14];
extern const uint8_t gm_sohcj0rggg07e[51];
extern const uint8_t gm_sokap7vn66boo[53];
extern const uint8_t gm_sol7l432pmrlq[8];
extern const uint8_t gm_solaeibjdukdj[24];
extern const uint8_t gm_solj59tk4ri3k[9];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_sonq8sf0vvvvr[127];
extern const uint8_t gm_sooqtob4aubd5[41];
extern const uint8_t gm_sopcc4aih66es[18];
extern const uint8_t gm_sopiqj93d6nu4[7];
extern const uint8_t gm_soq66vtlbhee2[56];
extern const uint8_t gm_soqlsukui24da[7];
extern const uint8_t gm_sos3b0mjqh560[23];
extern const uint8_t gm_sosa5l0k6d34c[15];
extern const uint8_t gm_soscrlodichin[67];
extern const uint8_t gm_sovm9i16n4uut[40];
extern const uint8_t gm_sp02qqgucmpt7[3];
extern const uint8_t gm_sp0qjak52qqef[158];
extern const uint8_t gm_sp13ju5l53585[24];
extern const uint8_t gm_sp1u9v27v4c4d[24];
extern const uint8_t gm_sp2du31gjaojf[8];
extern const uint8_t gm_sp2r5n25uphg2[20];
extern const uint8_t gm_sp2smaof0i4j2[10];
extern const uint8_t gm_sp7r202318t40[57];
extern const uint8_t gm_sp8bmcd3m68n6[7];
extern const uint8_t gm_spbhhmdbp335u[4];
extern const uint8_t gm_spc0i9srvnmq5[6];
extern const uint8_t gm_spc0s79b28n7m[9];
extern const uint8_t gm_spcl9lgniu1he[18];
extern const uint8_t gm_spdcqn4vu2cam[45];
extern const uint8_t gm_spgf846a0vg5g[21];
extern const uint8_t gm_spgfbquq93iqp[43];
extern const uint8_t gm_spgshrus7teti[24];
extern const uint8_t gm_spiad2tkfoc9s[4];
extern const uint8_t gm_spjohm68l6glq[22];
extern const uint8_t gm_splvrmi68lqi3[111];
extern const uint8_t gm_spn9416h484so[23];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_sppcm1qavsp1k[80];
extern const uint8_t gm_spqalnn72r2cn[34];
extern const uint8_t gm_spv9savsu9h0b[4];
extern const uint8_t gm_sq355po16rvrc[77];
extern const uint8_t gm_sq4sf3euttrc8[5];
extern const uint8_t gm_sq5stv2m4p0h1[37];
extern const uint8_t gm_sq6v0u0m2nfv8[23];
extern const uint8_t gm_sq7k2sk4t9rbi[11];
extern const uint8_t gm_sqc7sit9ajc3t[3];
extern const uint8_t gm_sqdfr4k3vea5p[4];
extern const uint8_t gm_sqebj59le8thb[9];
extern const uint8_t gm_sqes10usq4q5e[4];
extern const uint8_t gm_sqg1k42secsds[29];
extern const uint8_t gm_sqh82hv8jvv9p[18];
extern const uint8_t gm_sqhh38dkd5qvg[43];
extern const uint8_t gm_sqhhm0h7b3asd[6];
extern const uint8_t gm_sqje4v30nhs9k[41];
extern const uint8_t gm_sqk8phuvunvgk[56];
extern const uint8_t gm_sql0in48r61nd[5];
extern const uint8_t gm_sql9r2b8fvivt[10];
extern const uint8_t gm_sqm2dbl8kpvcq[4];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sqqi4njr3i0t4[46];
extern const uint8_t gm_sqqnbdm19fh9u[17];
extern const uint8_t gm_sqr1rjjq3dpuh[17];
extern const uint8_t gm_sqrl3kgomorsv[39];
extern const uint8_t gm_sqrv3igf39742[6];
extern const uint8_t gm_sqse1e7lhga3v[41];
extern const uint8_t gm_sqtrdhljiapb8[17];
extern const uint8_t gm_squ4ohvqdqaap[79];
extern const uint8_t gm_squ5nrnqo0b0l[6];
extern const uint8_t gm_squl6da1v7qvs[12];
extern const uint8_t gm_sqvb94amh2nu2[66];
extern const uint8_t gm_sr244s00qj01d[3];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr88r273kmptc[21];
extern const uint8_t gm_sr98hl2rjopi2[8];
extern const uint8_t gm_sr9q8kbf253ia[45];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4mnsbede6fos[];
extern const char gm_src4pc97o88riqc[];
extern const char gm_src5329n2jhsntp[];
extern const char gm_src7u9cfdjh7s0n[];
extern const char gm_srcbq9k9kvdokbf[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srccp2mbkjs1vn4[];
extern const char gm_srcgp1oqfehgsjb[];
extern const char gm_srch6h8rdicu443[];
extern const char gm_srckfibkpt681aa[];
extern const uint8_t gm_srcl5693p5ru5[17];
extern const char gm_srclr3r1dpiijpc[];
extern const uint8_t gm_srcpb16u2hru6[15];
extern const uint8_t gm_srefpimq1sas7[4];
extern const uint8_t gm_srfobl8913gvl[3];
extern const uint8_t gm_srjkl5qvdutgq[5];
extern const uint8_t gm_srl8r23tls393[15];
extern const uint8_t gm_srmrjv19jgvks[11];
extern const uint8_t gm_srnhu66btobke[9];
extern const uint8_t gm_srod2911mer3j[28];
extern const uint8_t gm_sroh2go4g3kcu[107];
extern const uint8_t gm_srqmunh3ba86d[20];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_srs4ljjaijhd2[24];
extern const uint8_t gm_srt8qrm10gni2[7];
extern const uint8_t gm_srv0b9jht81lt[18];
extern const uint8_t gm_ss27a20sarlvg[7];
extern const uint8_t gm_ss41pl5vtg7un[72];
extern const uint8_t gm_ss42hvrprenkk[26];
extern const uint8_t gm_ss46ng6b0cghl[41];
extern const uint8_t gm_ss49sa2bohvbp[28];
extern const uint8_t gm_ss57t1l5iltlq[14];
extern const uint8_t gm_ss75kiikq7m3h[28];
extern const uint8_t gm_ss7dlauv50bt8[81];
extern const uint8_t gm_ss8vfi8kj31rc[6];
extern const uint8_t gm_ss9ss6njjac9i[45];
extern const uint8_t gm_ssa1hve63tj2e[21];
extern const uint8_t gm_ssbkvpakdcg1k[7];
extern const uint8_t gm_sscsdlec4a8vt[18];
extern const uint8_t gm_ssevq7ii4bscf[10];
extern const uint8_t gm_ssfg7m66qbmvk[65];
extern const uint8_t gm_ssh7fkfdqsqve[41];
extern const uint8_t gm_ssi95ri2h4d02[28];
extern const uint8_t gm_ssipp3o8q1jun[11];
extern const uint8_t gm_ssjhh9bdn23jf[17];
extern const uint8_t gm_ssk4a01soguqj[7];
extern const uint8_t gm_sskduiq2hlj37[3];
extern const uint8_t gm_sskv7tavvjk2l[23];
extern const uint8_t gm_sslqgc2m7ch00[27];
extern const uint8_t gm_ssnts74n1eeq5[40];
extern const uint8_t gm_ssounvcga4tq8[15];
extern const uint8_t gm_ssqifgr2icd6s[3];
extern const uint8_t gm_ssr91slt3ah1c[11];
extern const uint8_t gm_ssv8t6tsqrjjf[97];
extern const uint8_t gm_st2a9sgu5aria[11];
extern const uint8_t gm_st2ertk6tjva7[14];
extern const uint8_t gm_st3m5olipe6ov[17];
extern const uint8_t gm_st45ica2g07t9[53];
extern const uint8_t gm_st6hj8v0h80gt[6];
extern const uint8_t gm_st8h6qvqu7ddb[30];
extern const uint8_t gm_sta33ql7gg6dk[46];
extern const uint8_t gm_stbjrsoqc456r[12];
extern const uint8_t gm_stbmo3g75s0lg[41];
extern const uint8_t gm_stgav1sk1eq6u[59];
extern const uint8_t gm_stlm46rulfhsg[39];
extern const uint8_t gm_stmsadvf54k73[3];
extern const uint8_t gm_stobu0djed6ck[10];
extern const uint8_t gm_stou3amhn9368[7];
extern const uint8_t gm_stt0il17c6uj9[17];
extern const uint8_t gm_sttkmg61kqc3k[65];
extern const uint8_t gm_stuj0mv5nl55t[4];
extern const uint8_t gm_stuutd7nfq21h[3];
extern const uint8_t gm_su01gdacg3ppp[9];
extern const uint8_t gm_su06jr9k1rhdo[15];
extern const uint8_t gm_su0hvb2ne02u0[17];
extern const uint8_t gm_su137hja0ebcd[6];
extern const uint8_t gm_su2eceicte7s7[6];
extern const uint8_t gm_su30pqbigkn5m[6];
extern const uint8_t gm_su50gais1htu0[38];
extern const uint8_t gm_su6ol10q7la3d[7];
extern const uint8_t gm_su823o267lacu[103];
extern const uint8_t gm_su8f8u3vs6aep[110];
extern const uint8_t gm_su99evi25asg4[6];
extern const uint8_t gm_sua99rsdk6d1r[51];
extern const uint8_t gm_suass5sid4b6e[5];
extern const uint8_t gm_sub9opb9g034o[14];
extern const uint8_t gm_sufif7dqc1pm3[17];
extern const uint8_t gm_sugavfshit856[10];
extern const uint8_t gm_suggn3kp9dfmg[9];
extern const uint8_t gm_suiab49hlgv7g[25];
extern const uint8_t gm_suio2i7004ufh[8];
extern const uint8_t gm_sujipgkhb2fih[8];
extern const uint8_t gm_sujn2evt64i1c[5];
extern const uint8_t gm_sujn6vi16u4un[15];
extern const uint8_t gm_sul4bd43gk4bq[10];
extern const uint8_t gm_suo9n1e1rnjb6[20];
extern const uint8_t gm_suoecsf0i038a[7];
extern const uint8_t gm_suonmnnb7u0t1[55];
extern const uint8_t gm_suqko6d4btmqa[10];
extern const uint8_t gm_suqqp23fdh2ff[6];
extern const uint8_t gm_susgjrlk3c5f0[8];
extern const uint8_t gm_susjn9psmiv0c[41];
extern const uint8_t gm_sutvcnnben4aj[59];
extern const uint8_t gm_suu5u3potp0p3[7];
extern const uint8_t gm_suup20cg9lo2d[8];
extern const uint8_t gm_suuv0qpi4v4e4[8];
extern const uint8_t gm_suv11ob37fisg[18];
extern const uint8_t gm_sv150rl18b4qa[4];
extern const uint8_t gm_sv1gci5lo7scg[5];
extern const uint8_t gm_sv3gqbo7rudsj[13];
extern const uint8_t gm_sv52eu837ea4c[5];
extern const uint8_t gm_sv5vs7371a92i[18];
extern const uint8_t gm_sv8lqdk5nsv8i[5];
extern const uint8_t gm_svacokl3cteo0[26];
extern const uint8_t gm_svbftsm94r0bv[7];
extern const uint8_t gm_svbhcvlahd4n2[3];
extern const uint8_t gm_svc1hcla4tqfb[6];
extern const uint8_t gm_svcmcgb47rki4[41];
extern const uint8_t gm_svdapq3j66qso[60];
extern const uint8_t gm_svdl0vjc2ha7u[50];
extern const uint8_t gm_sveol5dvm5sd7[20];
extern const uint8_t gm_svgqk0opgf2oj[60];
extern const uint8_t gm_svhcjkdo3os0j[62];
extern const uint8_t gm_svhe917rela4j[20];
extern const uint8_t gm_svjsvemd189fa[40];
extern const uint8_t gm_svld8cc4ptomt[16];
extern const uint8_t gm_svm0rfaqk3b2q[4];
extern const uint8_t gm_svnpflkpvd513[5];
extern const uint8_t gm_svokmu75u5n0c[14];
extern const uint8_t gm_svov1cn84ftlm[66];
extern const uint8_t gm_svqapkbdfenmd[54];
extern const uint8_t gm_svrjvsqtthedc[19];
extern const uint8_t gm_svsm5u96riots[18];
extern const uint8_t gm_svvbeos735pa5[84];
extern const gm_tmpl_piece gm_tpl00cra9g0k45s_p[];
extern const uint32_t gm_tpl01fkj9ukhaa3_i[];
extern const gm_spec *const gm_tpl05e80h89jqvd_s[];
extern const gm_spec *const gm_tpl083ga8282tol_s[];
extern const gm_tmpl_piece gm_tpl086m1ajg247c_p[];
extern const gm_spec *const gm_tpl09kglfpefe8i_s[];
extern const gm_spec *const gm_tpl0ej2jrvho6u0_s[];
extern const uint32_t gm_tpl0euitr9hdia5_i[];
extern const gm_tmpl_piece gm_tpl0fhs0t9qdf1o_p[];
extern const gm_tmpl_piece gm_tpl0gp8c85b2lf5_p[];
extern const gm_spec *const gm_tpl0h84r2u2heg0_s[];
extern const gm_tmpl_piece gm_tpl0j5bgvjcbn0u_p[];
extern const gm_tmpl_piece gm_tpl0ke3r8fiia4r_p[];
extern const uint32_t gm_tpl0n7kmejjk8it_i[];
extern const gm_tmpl gm_tpl0o9c14r51hfo;
extern const uint32_t gm_tpl0o9c14r51hfo_i[];
extern const gm_tmpl_piece gm_tpl0o9c14r51hfo_p[];
extern const gm_spec *const gm_tpl0o9c14r51hfo_s[];
extern const uint32_t gm_tpl0pd502nvjf7a_i[];
extern const gm_tmpl gm_tpl0rlavm9oqi24;
extern const uint32_t gm_tpl0rlavm9oqi24_i[];
extern const gm_tmpl_piece gm_tpl0rlavm9oqi24_p[];
extern const gm_spec *const gm_tpl0rlavm9oqi24_s[];
extern const gm_spec *const gm_tpl12e8u6hvkejb_s[];
extern const gm_tmpl gm_tpl13591hp4g6qh;
extern const uint32_t gm_tpl13591hp4g6qh_i[];
extern const gm_tmpl_piece gm_tpl13591hp4g6qh_p[];
extern const gm_spec *const gm_tpl13591hp4g6qh_s[];
extern const gm_spec *const gm_tpl14b6qietv6bm_s[];
extern const gm_tmpl_piece gm_tpl16petnrgdcd0_p[];
extern const gm_spec *const gm_tpl17t0pmu1kh1u_s[];
extern const gm_tmpl_piece gm_tpl1avjsgn54rqb_p[];
extern const gm_spec *const gm_tpl1avjsgn54rqb_s[];
extern const uint32_t gm_tpl1bno78mc58dl_i[];
extern const gm_tmpl_piece gm_tpl1c171lb88cdd_p[];
extern const gm_tmpl gm_tpl1dirc8ldrsao;
extern const uint32_t gm_tpl1dirc8ldrsao_i[];
extern const gm_tmpl_piece gm_tpl1dirc8ldrsao_p[];
extern const gm_spec *const gm_tpl1dirc8ldrsao_s[];
extern const gm_tmpl_piece gm_tpl1g1lh0kp24oa_p[];
extern const gm_spec *const gm_tpl1iblbr9u646o_s[];
extern const uint32_t gm_tpl1irt5kietk82_i[];
extern const gm_tmpl gm_tpl1o2hk14gjjj0;
extern const uint32_t gm_tpl1o2hk14gjjj0_i[];
extern const gm_tmpl_piece gm_tpl1o2hk14gjjj0_p[];
extern const gm_spec *const gm_tpl1o2hk14gjjj0_s[];
extern const gm_tmpl gm_tpl1os40ii1iecn;
extern const uint32_t gm_tpl1os40ii1iecn_i[];
extern const gm_tmpl_piece gm_tpl1os40ii1iecn_p[];
extern const gm_spec *const gm_tpl1os40ii1iecn_s[];
extern const uint32_t gm_tpl1r51q3hoe535_i[];
extern const gm_tmpl gm_tpl1rl5q0rqbd7p;
extern const uint32_t gm_tpl1rl5q0rqbd7p_i[];
extern const gm_tmpl_piece gm_tpl1rl5q0rqbd7p_p[];
extern const gm_spec *const gm_tpl1rl5q0rqbd7p_s[];
extern const gm_tmpl gm_tpl20a4acojo21c;
extern const uint32_t gm_tpl20a4acojo21c_i[];
extern const gm_tmpl_piece gm_tpl20a4acojo21c_p[];
extern const gm_spec *const gm_tpl20a4acojo21c_s[];
extern const gm_spec *const gm_tpl20t94jlvdndv_s[];
extern const gm_tmpl_piece gm_tpl21i1s6vvb3m4_p[];
extern const gm_tmpl gm_tpl233lm3m83499;
extern const uint32_t gm_tpl233lm3m83499_i[];
extern const gm_tmpl_piece gm_tpl233lm3m83499_p[];
extern const gm_spec *const gm_tpl233lm3m83499_s[];
extern const gm_tmpl gm_tpl29spsi1atllj;
extern const uint32_t gm_tpl29spsi1atllj_i[];
extern const gm_tmpl_piece gm_tpl29spsi1atllj_p[];
extern const gm_spec *const gm_tpl29spsi1atllj_s[];
extern const gm_tmpl gm_tpl2arnv1ofugei;
extern const uint32_t gm_tpl2arnv1ofugei_i[];
extern const gm_tmpl_piece gm_tpl2arnv1ofugei_p[];
extern const gm_spec *const gm_tpl2arnv1ofugei_s[];
extern const gm_tmpl gm_tpl2b904kc90480;
extern const uint32_t gm_tpl2b904kc90480_i[];
extern const gm_tmpl_piece gm_tpl2b904kc90480_p[];
extern const gm_spec *const gm_tpl2b904kc90480_s[];
extern const gm_spec *const gm_tpl2bm4b6itkmm6_s[];
extern const gm_tmpl gm_tpl2cgrvj4ke42d;
extern const uint32_t gm_tpl2cgrvj4ke42d_i[];
extern const gm_tmpl_piece gm_tpl2cgrvj4ke42d_p[];
extern const gm_spec *const gm_tpl2cgrvj4ke42d_s[];
extern const uint32_t gm_tpl2egb012obd94_i[];
extern const uint32_t gm_tpl2g2o9ile1980_i[];
extern const gm_tmpl_piece gm_tpl2qrjfa8j0fl5_p[];
extern const uint32_t gm_tpl2r84u6vnc58k_i[];
extern const uint32_t gm_tpl2s7fpgand9gc_i[];
extern const uint32_t gm_tpl2u97uspakqd9_i[];
extern const gm_spec *const gm_tpl313mu5t4735i_s[];
extern const gm_spec *const gm_tpl34tavcs9635o_s[];
extern const gm_tmpl_piece gm_tpl355ags9581ob_p[];
extern const uint32_t gm_tpl3bnq2anocte9_i[];
extern const gm_tmpl_piece gm_tpl3bnq2anocte9_p[];
extern const gm_spec *const gm_tpl3ergg6mce23h_s[];
extern const gm_tmpl gm_tpl3gvm2qp5ptd2;
extern const uint32_t gm_tpl3gvm2qp5ptd2_i[];
extern const gm_tmpl_piece gm_tpl3gvm2qp5ptd2_p[];
extern const gm_spec *const gm_tpl3gvm2qp5ptd2_s[];
extern const uint32_t gm_tpl40m4ah8dc3ae_i[];
extern const gm_tmpl gm_tpl4476c5bksft1;
extern const uint32_t gm_tpl4476c5bksft1_i[];
extern const gm_tmpl_piece gm_tpl4476c5bksft1_p[];
extern const gm_spec *const gm_tpl4476c5bksft1_s[];
extern const gm_tmpl gm_tpl4524ekle00ud;
extern const uint32_t gm_tpl4524ekle00ud_i[];
extern const gm_tmpl_piece gm_tpl4524ekle00ud_p[];
extern const gm_spec *const gm_tpl4524ekle00ud_s[];
extern const gm_spec *const gm_tpl4561ar79q0c0_s[];
extern const gm_tmpl_piece gm_tpl46q9qc00shi4_p[];
extern const uint32_t gm_tpl470f2c4pe1su_i[];
extern const uint32_t gm_tpl4auv8p2j34k5_i[];
extern const gm_spec *const gm_tpl4bpbmadpmu1i_s[];
extern const gm_tmpl_piece gm_tpl4cbbis3gd1k3_p[];
extern const gm_tmpl_piece gm_tpl4dq73nmettt5_p[];
extern const gm_spec *const gm_tpl4esqdh06rpvt_s[];
extern const gm_spec *const gm_tpl4iu2nfn1okgb_s[];
extern const uint32_t gm_tpl4ivrecg9ncdu_i[];
extern const gm_tmpl_piece gm_tpl4ivrecg9ncdu_p[];
extern const gm_tmpl gm_tpl4j60geuv9mvj;
extern const uint32_t gm_tpl4j60geuv9mvj_i[];
extern const gm_tmpl_piece gm_tpl4j60geuv9mvj_p[];
extern const gm_spec *const gm_tpl4j60geuv9mvj_s[];
extern const gm_spec *const gm_tpl4kmab8oiki17_s[];
extern const gm_tmpl_piece gm_tpl4r3nb3bvll23_p[];
extern const gm_tmpl gm_tpl4t11fkqh738f;
extern const uint32_t gm_tpl4t11fkqh738f_i[];
extern const gm_tmpl_piece gm_tpl4t11fkqh738f_p[];
extern const gm_spec *const gm_tpl4t11fkqh738f_s[];
extern const uint32_t gm_tpl4upfck3qu2er_i[];
extern const uint32_t gm_tpl53ihvkdedd31_i[];
extern const gm_tmpl_piece gm_tpl56vmcuv96cg9_p[];
extern const gm_tmpl gm_tpl598lk1ciiqvp;
extern const uint32_t gm_tpl598lk1ciiqvp_i[];
extern const gm_tmpl_piece gm_tpl598lk1ciiqvp_p[];
extern const gm_spec *const gm_tpl598lk1ciiqvp_s[];
extern const gm_tmpl gm_tpl59s4m8f706c9;
extern const uint32_t gm_tpl59s4m8f706c9_i[];
extern const gm_tmpl_piece gm_tpl59s4m8f706c9_p[];
extern const gm_spec *const gm_tpl59s4m8f706c9_s[];
extern const gm_tmpl gm_tpl5a8og2hcltt3;
extern const uint32_t gm_tpl5a8og2hcltt3_i[];
extern const gm_tmpl_piece gm_tpl5a8og2hcltt3_p[];
extern const gm_spec *const gm_tpl5a8og2hcltt3_s[];
extern const gm_tmpl_piece gm_tpl5g9nntg83p8b_p[];
extern const gm_spec *const gm_tpl5gt902348obb_s[];
extern const uint32_t gm_tpl5hatm6q530pn_i[];
extern const gm_spec *const gm_tpl5oph1bqhjo1e_s[];
extern const gm_tmpl gm_tpl5prdhjdrg194;
extern const uint32_t gm_tpl5prdhjdrg194_i[];
extern const gm_tmpl_piece gm_tpl5prdhjdrg194_p[];
extern const gm_spec *const gm_tpl5prdhjdrg194_s[];
extern const gm_spec *const gm_tpl5st0uuf0quab_s[];
extern const gm_tmpl gm_tpl5uko75p49g56;
extern const uint32_t gm_tpl5uko75p49g56_i[];
extern const gm_tmpl_piece gm_tpl5uko75p49g56_p[];
extern const gm_spec *const gm_tpl5uko75p49g56_s[];
extern const uint32_t gm_tpl606t7k5n88ss_i[];
extern const uint32_t gm_tpl60na9lt7qpkj_i[];
extern const gm_tmpl_piece gm_tpl6104ee3qfshr_p[];
extern const uint32_t gm_tpl61n2hscg0et2_i[];
extern const uint32_t gm_tpl63djfct0mcad_i[];
extern const gm_spec *const gm_tpl64ltc7suc5qg_s[];
extern const gm_spec *const gm_tpl64qrh372lslh_s[];
extern const gm_spec *const gm_tpl6a59mrfvoo09_s[];
extern const gm_spec *const gm_tpl6cs6ch4uvrua_s[];
extern const gm_tmpl gm_tpl6gmot6d7lh6n;
extern const uint32_t gm_tpl6gmot6d7lh6n_i[];
extern const gm_tmpl_piece gm_tpl6gmot6d7lh6n_p[];
extern const gm_spec *const gm_tpl6gmot6d7lh6n_s[];
extern const gm_tmpl gm_tpl6itong91gv3f;
extern const uint32_t gm_tpl6itong91gv3f_i[];
extern const gm_tmpl_piece gm_tpl6itong91gv3f_p[];
extern const gm_spec *const gm_tpl6itong91gv3f_s[];
extern const uint32_t gm_tpl6jo4mmkdq2l1_i[];
extern const gm_spec *const gm_tpl6lgf07g31gim_s[];
extern const gm_tmpl gm_tpl6mra39d4fo2s;
extern const uint32_t gm_tpl6mra39d4fo2s_i[];
extern const gm_tmpl_piece gm_tpl6mra39d4fo2s_p[];
extern const gm_spec *const gm_tpl6mra39d4fo2s_s[];
extern const uint32_t gm_tpl6n3s5f3ittsi_i[];
extern const uint32_t gm_tpl6ou7bec1fkae_i[];
extern const uint32_t gm_tpl6r9reg6pnruq_i[];
extern const gm_spec *const gm_tpl6salfcvi63n2_s[];
extern const gm_spec *const gm_tpl6vbod7vja73l_s[];
extern const gm_spec *const gm_tpl726l5jbs5bmh_s[];
extern const gm_tmpl gm_tpl78lk9ugsmc0b;
extern const uint32_t gm_tpl78lk9ugsmc0b_i[];
extern const gm_tmpl_piece gm_tpl78lk9ugsmc0b_p[];
extern const gm_spec *const gm_tpl78lk9ugsmc0b_s[];
extern const gm_tmpl gm_tpl7da8g7lhjmr1;
extern const uint32_t gm_tpl7da8g7lhjmr1_i[];
extern const gm_tmpl_piece gm_tpl7da8g7lhjmr1_p[];
extern const gm_spec *const gm_tpl7da8g7lhjmr1_s[];
extern const gm_tmpl gm_tpl7eehu7c1eqka;
extern const uint32_t gm_tpl7eehu7c1eqka_i[];
extern const gm_tmpl_piece gm_tpl7eehu7c1eqka_p[];
extern const gm_spec *const gm_tpl7eehu7c1eqka_s[];
extern const gm_tmpl_piece gm_tpl7ilj5n9objtq_p[];
extern const gm_spec *const gm_tpl7ll3166bkl17_s[];
extern const gm_tmpl_piece gm_tpl7m3t7r6fqpf7_p[];
extern const gm_tmpl gm_tpl7medmjmlvos1;
extern const uint32_t gm_tpl7medmjmlvos1_i[];
extern const gm_tmpl_piece gm_tpl7medmjmlvos1_p[];
extern const gm_spec *const gm_tpl7medmjmlvos1_s[];
extern const uint32_t gm_tpl7o4acs2tt6pp_i[];
extern const uint32_t gm_tpl7qfg883s3b97_i[];
extern const uint32_t gm_tpl7t589bv3t4us_i[];
extern const gm_spec *const gm_tpl7t589bv3t4us_s[];
extern const gm_tmpl_piece gm_tpl7ufg84gs1lc7_p[];
extern const gm_tmpl gm_tpl7umtk166uvmr;
extern const uint32_t gm_tpl7umtk166uvmr_i[];
extern const gm_tmpl_piece gm_tpl7umtk166uvmr_p[];
extern const gm_spec *const gm_tpl7umtk166uvmr_s[];
extern const gm_tmpl gm_tpl7vtkjsp2pa8b;
extern const uint32_t gm_tpl7vtkjsp2pa8b_i[];
extern const gm_tmpl_piece gm_tpl7vtkjsp2pa8b_p[];
extern const gm_spec *const gm_tpl7vtkjsp2pa8b_s[];
extern const uint32_t gm_tpl80avf3gokjno_i[];
extern const gm_tmpl_piece gm_tpl80egqk8smgev_p[];
extern const gm_tmpl gm_tpl81nunf5qvo6u;
extern const uint32_t gm_tpl81nunf5qvo6u_i[];
extern const gm_tmpl_piece gm_tpl81nunf5qvo6u_p[];
extern const gm_spec *const gm_tpl81nunf5qvo6u_s[];
extern const gm_spec *const gm_tpl84co505h34ud_s[];
extern const uint32_t gm_tpl85lm1gq83hlb_i[];
extern const gm_spec *const gm_tpl86i0s811rrlp_s[];
extern const gm_tmpl_piece gm_tpl8in37qe3omcj_p[];
extern const uint32_t gm_tpl8mdj3nc4lpjv_i[];
extern const gm_tmpl gm_tpl8qng452btr2a;
extern const uint32_t gm_tpl8qng452btr2a_i[];
extern const gm_tmpl_piece gm_tpl8qng452btr2a_p[];
extern const gm_spec *const gm_tpl8qng452btr2a_s[];
extern const gm_tmpl gm_tpl8ro9atn5p92f;
extern const uint32_t gm_tpl8ro9atn5p92f_i[];
extern const gm_tmpl_piece gm_tpl8ro9atn5p92f_p[];
extern const gm_spec *const gm_tpl8ro9atn5p92f_s[];
extern const gm_tmpl gm_tpl8vbes2v6184u;
extern const uint32_t gm_tpl8vbes2v6184u_i[];
extern const gm_tmpl_piece gm_tpl8vbes2v6184u_p[];
extern const gm_spec *const gm_tpl8vbes2v6184u_s[];
extern const gm_tmpl gm_tpl91a2oiuc6ot7;
extern const uint32_t gm_tpl91a2oiuc6ot7_i[];
extern const gm_tmpl_piece gm_tpl91a2oiuc6ot7_p[];
extern const gm_spec *const gm_tpl91a2oiuc6ot7_s[];
extern const gm_tmpl gm_tpl91b7sjgnmine;
extern const uint32_t gm_tpl91b7sjgnmine_i[];
extern const gm_tmpl_piece gm_tpl91b7sjgnmine_p[];
extern const gm_spec *const gm_tpl91b7sjgnmine_s[];
extern const gm_tmpl gm_tpl97n87h3pcva1;
extern const uint32_t gm_tpl97n87h3pcva1_i[];
extern const gm_tmpl_piece gm_tpl97n87h3pcva1_p[];
extern const gm_spec *const gm_tpl97n87h3pcva1_s[];
extern const gm_tmpl_piece gm_tpl9ag92r71r0vm_p[];
extern const uint32_t gm_tpl9cnobsjbh0r2_i[];
extern const uint32_t gm_tpl9d1siq0c9pir_i[];
extern const uint32_t gm_tpl9d5e93nnc8t1_i[];
extern const gm_tmpl_piece gm_tpl9fn8oj7nnnsq_p[];
extern const gm_tmpl_piece gm_tpl9fvss98clvrp_p[];
extern const gm_spec *const gm_tpl9iaqf5bbsv8o_s[];
extern const gm_spec *const gm_tpl9l4fqsojrjsa_s[];
extern const uint32_t gm_tpl9ne0ajfec16n_i[];
extern const gm_spec *const gm_tpl9ne0ajfec16n_s[];
extern const gm_tmpl_piece gm_tpl9q5j6bisf2fa_p[];
extern const uint32_t gm_tpl9qchhbq9qlbm_i[];
extern const gm_tmpl_piece gm_tpl9ufa4j1b7kal_p[];
extern const gm_tmpl_piece gm_tpl9vu8tqcv1d9s_p[];
extern const gm_tmpl_piece gm_tpla0ci4vs242iu_p[];
extern const gm_tmpl gm_tpla9stdteeiicv;
extern const uint32_t gm_tpla9stdteeiicv_i[];
extern const gm_tmpl_piece gm_tpla9stdteeiicv_p[];
extern const gm_spec *const gm_tpla9stdteeiicv_s[];
extern const gm_tmpl_piece gm_tplabinq26b6unj_p[];
extern const gm_spec *const gm_tplabinq26b6unj_s[];
extern const gm_spec *const gm_tplaka4u9f9dfvk_s[];
extern const gm_tmpl gm_tplamm5t71udd8k;
extern const uint32_t gm_tplamm5t71udd8k_i[];
extern const gm_tmpl_piece gm_tplamm5t71udd8k_p[];
extern const gm_spec *const gm_tplamm5t71udd8k_s[];
extern const gm_spec *const gm_tplamv2e4uailq7_s[];
extern const gm_tmpl gm_tplanhmvcr622qo;
extern const uint32_t gm_tplanhmvcr622qo_i[];
extern const gm_tmpl_piece gm_tplanhmvcr622qo_p[];
extern const gm_spec *const gm_tplanhmvcr622qo_s[];
extern const uint32_t gm_tplanqv6irfvtut_i[];
extern const gm_spec *const gm_tplaqje7ooa5hcl_s[];
extern const uint32_t gm_tplaqps986okebj_i[];
extern const gm_tmpl gm_tplav4294nbj9c7;
extern const uint32_t gm_tplav4294nbj9c7_i[];
extern const gm_tmpl_piece gm_tplav4294nbj9c7_p[];
extern const gm_spec *const gm_tplav4294nbj9c7_s[];
extern const gm_tmpl_piece gm_tplb5ldvki5nntc_p[];
extern const gm_tmpl gm_tplb9i7jfun8rvg;
extern const uint32_t gm_tplb9i7jfun8rvg_i[];
extern const gm_tmpl_piece gm_tplb9i7jfun8rvg_p[];
extern const gm_spec *const gm_tplb9i7jfun8rvg_s[];
extern const gm_tmpl_piece gm_tplbaqni0b3abqt_p[];
extern const gm_tmpl_piece gm_tplbbsc15j8h2ga_p[];
extern const gm_spec *const gm_tplbcublle8ivup_s[];
extern const uint32_t gm_tplbdrnrpalngql_i[];
extern const gm_tmpl_piece gm_tplbdrnrpalngql_p[];
extern const gm_tmpl_piece gm_tplbeuo81m3df3c_p[];
extern const gm_tmpl_piece gm_tplbj4npuroda3f_p[];
extern const uint32_t gm_tplbjh9dr2llnkb_i[];
extern const uint32_t gm_tplbk03g4ml40ad_i[];
extern const gm_tmpl gm_tplbnt69qpq91i8;
extern const uint32_t gm_tplbnt69qpq91i8_i[];
extern const gm_tmpl_piece gm_tplbnt69qpq91i8_p[];
extern const gm_spec *const gm_tplbnt69qpq91i8_s[];
extern const uint32_t gm_tplbntllbe37f81_i[];
extern const uint32_t gm_tplbp75knfmjbql_i[];
extern const gm_tmpl_piece gm_tplbppmjhpd8ol0_p[];
extern const gm_spec *const gm_tplbqqnfekph537_s[];
extern const gm_tmpl gm_tplbtbg1u9fmcqq;
extern const uint32_t gm_tplbtbg1u9fmcqq_i[];
extern const gm_tmpl_piece gm_tplbtbg1u9fmcqq_p[];
extern const gm_spec *const gm_tplbtbg1u9fmcqq_s[];
extern const gm_tmpl_piece gm_tplc0d2186db2jt_p[];
extern const uint32_t gm_tplc5prn5jeukj6_i[];
extern const uint32_t gm_tplc6hhdd7vcmvk_i[];
extern const uint32_t gm_tplc90dj04km0n5_i[];
extern const gm_spec *const gm_tplc9tn3pkjdpf4_s[];
extern const gm_tmpl gm_tplcb1495nujsb7;
extern const uint32_t gm_tplcb1495nujsb7_i[];
extern const gm_tmpl_piece gm_tplcb1495nujsb7_p[];
extern const gm_spec *const gm_tplcb1495nujsb7_s[];
extern const uint32_t gm_tplcds5gqa95rss_i[];
extern const gm_spec *const gm_tplcds5gqa95rss_s[];
extern const gm_tmpl_piece gm_tplce7sc9hodc13_p[];
extern const gm_tmpl_piece gm_tplcef5l6cvp5lt_p[];
extern const gm_spec *const gm_tplceihv99elj2t_s[];
extern const uint32_t gm_tplcein5d5pq2cs_i[];
extern const gm_tmpl gm_tplchb0p7o483ge;
extern const uint32_t gm_tplchb0p7o483ge_i[];
extern const gm_tmpl_piece gm_tplchb0p7o483ge_p[];
extern const gm_spec *const gm_tplchb0p7o483ge_s[];
extern const gm_tmpl gm_tplcouqld11oeil;
extern const uint32_t gm_tplcouqld11oeil_i[];
extern const gm_tmpl_piece gm_tplcouqld11oeil_p[];
extern const gm_spec *const gm_tplcouqld11oeil_s[];
extern const gm_tmpl gm_tplcpa735d0vjnu;
extern const uint32_t gm_tplcpa735d0vjnu_i[];
extern const gm_tmpl_piece gm_tplcpa735d0vjnu_p[];
extern const gm_spec *const gm_tplcpa735d0vjnu_s[];
extern const uint32_t gm_tplcte0qqjaa0q0_i[];
extern const uint32_t gm_tpld3o5iqghffqq_i[];
extern const gm_tmpl gm_tpld47p9te8ar36;
extern const uint32_t gm_tpld47p9te8ar36_i[];
extern const gm_tmpl_piece gm_tpld47p9te8ar36_p[];
extern const gm_spec *const gm_tpld47p9te8ar36_s[];
extern const gm_tmpl_piece gm_tpld9ts015fint3_p[];
extern const gm_tmpl_piece gm_tplda38mm1025ge_p[];
extern const gm_spec *const gm_tpldfm52le63mtn_s[];
extern const gm_tmpl_piece gm_tpldftt3jdrahlt_p[];
extern const gm_tmpl_piece gm_tpldhmlf1n8qcdi_p[];
extern const uint32_t gm_tpldofkl5m8hqug_i[];
extern const gm_spec *const gm_tpldq0sm30o8mph_s[];
extern const gm_spec *const gm_tpldrfjfgoqe179_s[];
extern const gm_tmpl_piece gm_tpldueenbh2sgd4_p[];
extern const gm_tmpl gm_tpldumo4kdlpj8p;
extern const uint32_t gm_tpldumo4kdlpj8p_i[];
extern const gm_tmpl_piece gm_tpldumo4kdlpj8p_p[];
extern const gm_spec *const gm_tpldumo4kdlpj8p_s[];
extern const gm_tmpl gm_tplduutdqd8m5r5;
extern const uint32_t gm_tplduutdqd8m5r5_i[];
extern const gm_tmpl_piece gm_tplduutdqd8m5r5_p[];
extern const gm_spec *const gm_tplduutdqd8m5r5_s[];
extern const gm_tmpl_piece gm_tpldvhfe50cnb69_p[];
extern const gm_tmpl gm_tple0332mlih7si;
extern const uint32_t gm_tple0332mlih7si_i[];
extern const gm_tmpl_piece gm_tple0332mlih7si_p[];
extern const gm_spec *const gm_tple0332mlih7si_s[];
extern const uint32_t gm_tple23ed5kepfqi_i[];
extern const gm_spec *const gm_tple2rp5rj93mkj_s[];
extern const uint32_t gm_tple34dj6br49e3_i[];
extern const gm_tmpl_piece gm_tple68gu1adcjq9_p[];
extern const gm_tmpl gm_tple6jpca094i2j;
extern const uint32_t gm_tple6jpca094i2j_i[];
extern const gm_tmpl_piece gm_tple6jpca094i2j_p[];
extern const gm_spec *const gm_tple6jpca094i2j_s[];
extern const gm_tmpl_piece gm_tplecdkophfbiam_p[];
extern const gm_tmpl_piece gm_tpleeuqgcr11ngn_p[];
extern const gm_spec *const gm_tplei8v65ors6jc_s[];
extern const gm_tmpl_piece gm_tplei9v853i04sp_p[];
extern const uint32_t gm_tplekdseqnnupsj_i[];
extern const uint32_t gm_tplem02qig8laoi_i[];
extern const uint32_t gm_tplengfdkso35ua_i[];
extern const gm_tmpl_piece gm_tplenpf051q0m4n_p[];
extern const gm_tmpl_piece gm_tpleoop2177apvc_p[];
extern const gm_tmpl gm_tplesi2enviniss;
extern const uint32_t gm_tplesi2enviniss_i[];
extern const gm_tmpl_piece gm_tplesi2enviniss_p[];
extern const gm_spec *const gm_tplesi2enviniss_s[];
extern const gm_tmpl gm_tpleth0e0pd50mr;
extern const uint32_t gm_tpleth0e0pd50mr_i[];
extern const gm_tmpl_piece gm_tpleth0e0pd50mr_p[];
extern const gm_spec *const gm_tpleth0e0pd50mr_s[];
extern const uint32_t gm_tpleugrd3m4ci89_i[];
extern const gm_tmpl_piece gm_tplf0gekbh2mtku_p[];
extern const gm_spec *const gm_tplf1r4lr6ofdql_s[];
extern const gm_tmpl gm_tplf2fho9tvid0p;
extern const uint32_t gm_tplf2fho9tvid0p_i[];
extern const gm_tmpl_piece gm_tplf2fho9tvid0p_p[];
extern const gm_spec *const gm_tplf2fho9tvid0p_s[];
extern const uint32_t gm_tplf3e41f3siie3_i[];
extern const gm_spec *const gm_tplf3en286s8fa5_s[];
extern const gm_spec *const gm_tplf9jp9gfh0eik_s[];
extern const gm_tmpl_piece gm_tplfbtlvu2isf2h_p[];
extern const gm_tmpl gm_tplfg7j0v7js1f7;
extern const uint32_t gm_tplfg7j0v7js1f7_i[];
extern const gm_tmpl_piece gm_tplfg7j0v7js1f7_p[];
extern const gm_spec *const gm_tplfg7j0v7js1f7_s[];
extern const gm_tmpl_piece gm_tplfm527r729efa_p[];
extern const gm_spec *const gm_tplfoosl93e8k5t_s[];
extern const gm_spec *const gm_tplfqju53pb0vaj_s[];
extern const gm_tmpl gm_tplfr6nl2m52h19;
extern const uint32_t gm_tplfr6nl2m52h19_i[];
extern const gm_tmpl_piece gm_tplfr6nl2m52h19_p[];
extern const gm_spec *const gm_tplfr6nl2m52h19_s[];
extern const gm_spec *const gm_tplfuin6hfnfo1c_s[];
extern const gm_tmpl_piece gm_tplful0p0r5pifs_p[];
extern const gm_tmpl_piece gm_tplg05c24rh8peq_p[];
extern const uint32_t gm_tplg0rms0t7302m_i[];
extern const uint32_t gm_tplg21rd4as3ph6_i[];
extern const gm_tmpl gm_tplg3qra7vq6h3a;
extern const uint32_t gm_tplg3qra7vq6h3a_i[];
extern const gm_tmpl_piece gm_tplg3qra7vq6h3a_p[];
extern const gm_spec *const gm_tplg3qra7vq6h3a_s[];
extern const uint32_t gm_tplg45jfs21ci2c_i[];
extern const gm_tmpl_piece gm_tplg45jfs21ci2c_p[];
extern const gm_tmpl_piece gm_tplg500ggupdf4i_p[];
extern const gm_tmpl gm_tplg7bh9jeid72i;
extern const uint32_t gm_tplg7bh9jeid72i_i[];
extern const gm_tmpl_piece gm_tplg7bh9jeid72i_p[];
extern const gm_spec *const gm_tplg7bh9jeid72i_s[];
extern const uint32_t gm_tplg91jq41nbbkt_i[];
extern const gm_tmpl gm_tplgan4klju39em;
extern const uint32_t gm_tplgan4klju39em_i[];
extern const gm_tmpl_piece gm_tplgan4klju39em_p[];
extern const gm_spec *const gm_tplgan4klju39em_s[];
extern const gm_spec *const gm_tplgavllsav46n0_s[];
extern const uint32_t gm_tplgfhhtcka09o4_i[];
extern const gm_tmpl gm_tplgih8dfq5hun7;
extern const uint32_t gm_tplgih8dfq5hun7_i[];
extern const gm_tmpl_piece gm_tplgih8dfq5hun7_p[];
extern const gm_spec *const gm_tplgih8dfq5hun7_s[];
extern const uint32_t gm_tplgj9d3igk4i62_i[];
extern const uint32_t gm_tplgjqa4ejl8op1_i[];
extern const gm_tmpl_piece gm_tplgnobkmf75b9i_p[];
extern const gm_tmpl_piece gm_tplgp15dehsbtkk_p[];
extern const gm_tmpl_piece gm_tplgq3upqu7s79r_p[];
extern const uint32_t gm_tplgr8ep9alj0ph_i[];
extern const gm_tmpl gm_tplgt6vkfjdh8cd;
extern const uint32_t gm_tplgt6vkfjdh8cd_i[];
extern const gm_tmpl_piece gm_tplgt6vkfjdh8cd_p[];
extern const gm_spec *const gm_tplgt6vkfjdh8cd_s[];
extern const gm_spec *const gm_tplgv9va17kt7ll_s[];
extern const gm_spec *const gm_tplh1blptpemhkl_s[];
extern const gm_tmpl gm_tplh6c513o0038j;
extern const uint32_t gm_tplh6c513o0038j_i[];
extern const gm_tmpl_piece gm_tplh6c513o0038j_p[];
extern const gm_spec *const gm_tplh6c513o0038j_s[];
extern const gm_tmpl_piece gm_tplh78gsuiavvas_p[];
extern const gm_tmpl gm_tplha5n8a69m774;
extern const uint32_t gm_tplha5n8a69m774_i[];
extern const gm_tmpl_piece gm_tplha5n8a69m774_p[];
extern const gm_spec *const gm_tplha5n8a69m774_s[];
extern const uint32_t gm_tplhfdokrq767sh_i[];
extern const uint32_t gm_tplhlt4sff0mrem_i[];
extern const gm_tmpl gm_tplhmairn7oibac;
extern const uint32_t gm_tplhmairn7oibac_i[];
extern const gm_tmpl_piece gm_tplhmairn7oibac_p[];
extern const gm_spec *const gm_tplhmairn7oibac_s[];
extern const gm_tmpl_piece gm_tplhn9hprqs819d_p[];
extern const gm_spec *const gm_tplhon5g1o0vuqi_s[];
extern const gm_spec *const gm_tplhu5cefj26aau_s[];
extern const gm_tmpl gm_tplhu7i2tvvfh15;
extern const uint32_t gm_tplhu7i2tvvfh15_i[];
extern const gm_tmpl_piece gm_tplhu7i2tvvfh15_p[];
extern const gm_spec *const gm_tplhu7i2tvvfh15_s[];
extern const gm_tmpl gm_tpli4rfoi75onhj;
extern const uint32_t gm_tpli4rfoi75onhj_i[];
extern const gm_tmpl_piece gm_tpli4rfoi75onhj_p[];
extern const gm_spec *const gm_tpli4rfoi75onhj_s[];
extern const gm_spec *const gm_tpli7hldrrhg7sr_s[];
extern const gm_tmpl_piece gm_tpli7ohq5o5sj5n_p[];
extern const gm_tmpl gm_tpli824iv2hf4o6;
extern const uint32_t gm_tpli824iv2hf4o6_i[];
extern const gm_tmpl_piece gm_tpli824iv2hf4o6_p[];
extern const gm_spec *const gm_tpli824iv2hf4o6_s[];
extern const uint32_t gm_tpli8aunvo88ks7_i[];
extern const uint32_t gm_tplibksf6f0e00q_i[];
extern const uint32_t gm_tplicmhpq9e6sbc_i[];
extern const uint32_t gm_tpligtpf80arr7h_i[];
extern const uint32_t gm_tplilne6f8eq90i_i[];
extern const gm_tmpl_piece gm_tpliqpvgtpo05sn_p[];
extern const gm_spec *const gm_tpliucrr7pqi5es_s[];
extern const uint32_t gm_tpliuif0nifmui1_i[];
extern const gm_tmpl_piece gm_tplj16n1o03849r_p[];
extern const gm_tmpl gm_tplj2qf541fmi6f;
extern const uint32_t gm_tplj2qf541fmi6f_i[];
extern const gm_tmpl_piece gm_tplj2qf541fmi6f_p[];
extern const gm_spec *const gm_tplj2qf541fmi6f_s[];
extern const gm_spec *const gm_tplj3vfuklvht3e_s[];
extern const gm_spec *const gm_tplj57b11lvidge_s[];
extern const gm_spec *const gm_tplj78420ela1cg_s[];
extern const gm_tmpl_piece gm_tplja7aur1oiv70_p[];
extern const uint32_t gm_tpljd9codq72bc9_i[];
extern const uint32_t gm_tpljgv7fkm5ld25_i[];
extern const gm_spec *const gm_tpljhar6jmbtsst_s[];
extern const gm_tmpl_piece gm_tpljicvo0ni8aba_p[];
extern const gm_spec *const gm_tplk25d3iiaamil_s[];
extern const uint32_t gm_tplk3nkr2eh6don_i[];
extern const gm_tmpl gm_tplka0n288r3ccr;
extern const uint32_t gm_tplka0n288r3ccr_i[];
extern const gm_tmpl_piece gm_tplka0n288r3ccr_p[];
extern const gm_spec *const gm_tplka0n288r3ccr_s[];
extern const gm_spec *const gm_tplka8uag807gql_s[];
extern const gm_tmpl gm_tplkik9smkp1eb6;
extern const uint32_t gm_tplkik9smkp1eb6_i[];
extern const gm_tmpl_piece gm_tplkik9smkp1eb6_p[];
extern const gm_spec *const gm_tplkik9smkp1eb6_s[];
extern const uint32_t gm_tplkjc5mpc5i80h_i[];
extern const gm_tmpl gm_tplkkvn546crm4n;
extern const uint32_t gm_tplkkvn546crm4n_i[];
extern const gm_tmpl_piece gm_tplkkvn546crm4n_p[];
extern const gm_spec *const gm_tplkkvn546crm4n_s[];
extern const gm_tmpl_piece gm_tplkv6ui0mqvq2g_p[];
extern const gm_spec *const gm_tpll060ppa56f8b_s[];
extern const uint32_t gm_tpll4f2ihchoe9c_i[];
extern const gm_tmpl gm_tpll66vdkdoc48m;
extern const uint32_t gm_tpll66vdkdoc48m_i[];
extern const gm_tmpl_piece gm_tpll66vdkdoc48m_p[];
extern const gm_spec *const gm_tpll66vdkdoc48m_s[];
extern const gm_spec *const gm_tpll6pilv9krmhr_s[];
extern const gm_tmpl_piece gm_tpll9g0ell7ocpk_p[];
extern const gm_tmpl_piece gm_tpllbgi055uoiek_p[];
extern const uint32_t gm_tpllfgb42aufjqd_i[];
extern const gm_tmpl_piece gm_tpllhk8pmjfso1j_p[];
extern const gm_spec *const gm_tpllir4kiseuk2b_s[];
extern const gm_spec *const gm_tplllolcurrktr6_s[];
extern const uint32_t gm_tpllp752obmvr55_i[];
extern const gm_spec *const gm_tplm0bsttjtt8b0_s[];
extern const gm_spec *const gm_tplm1fuloaqnfv4_s[];
extern const gm_tmpl_piece gm_tplm6g8a9odsasi_p[];
extern const gm_spec *const gm_tplm6qp3tlbq28j_s[];
extern const uint32_t gm_tplmaqqdmni75bf_i[];
extern const gm_tmpl_piece gm_tplme3pul5hs3bf_p[];
extern const gm_spec *const gm_tplme3pul5hs3bf_s[];
extern const uint32_t gm_tplmjku4vg3eupf_i[];
extern const gm_tmpl gm_tplmm5mgnjalbfr;
extern const uint32_t gm_tplmm5mgnjalbfr_i[];
extern const gm_tmpl_piece gm_tplmm5mgnjalbfr_p[];
extern const gm_spec *const gm_tplmm5mgnjalbfr_s[];
extern const gm_spec *const gm_tplmo1fik79j6ea_s[];
extern const gm_spec *const gm_tplmpsv5qagthkk_s[];
extern const uint32_t gm_tplmr0a02ecbaom_i[];
extern const gm_tmpl_piece gm_tplmueh9mdvaqub_p[];
extern const gm_spec *const gm_tplmv1ukn138isf_s[];
extern const gm_tmpl gm_tpln1d9amhhlm2o;
extern const uint32_t gm_tpln1d9amhhlm2o_i[];
extern const gm_tmpl_piece gm_tpln1d9amhhlm2o_p[];
extern const gm_spec *const gm_tpln1d9amhhlm2o_s[];
extern const uint32_t gm_tpln38mjgenps81_i[];
extern const gm_tmpl gm_tpln4hlddpv31vi;
extern const uint32_t gm_tpln4hlddpv31vi_i[];
extern const gm_tmpl_piece gm_tpln4hlddpv31vi_p[];
extern const gm_spec *const gm_tpln4hlddpv31vi_s[];
extern const gm_tmpl_piece gm_tplna8gfl3ukgs3_p[];
extern const gm_spec *const gm_tplnb9cl2s2ukch_s[];
extern const gm_tmpl_piece gm_tplnbbj7t6ap7ht_p[];
extern const gm_tmpl gm_tplnbsaptme3355;
extern const uint32_t gm_tplnbsaptme3355_i[];
extern const gm_tmpl_piece gm_tplnbsaptme3355_p[];
extern const gm_spec *const gm_tplnbsaptme3355_s[];
extern const gm_tmpl gm_tplncn0njcasr6v;
extern const uint32_t gm_tplncn0njcasr6v_i[];
extern const gm_tmpl_piece gm_tplncn0njcasr6v_p[];
extern const gm_spec *const gm_tplncn0njcasr6v_s[];
extern const gm_spec *const gm_tplngekt6vfvnsv_s[];
extern const gm_tmpl gm_tplngsd5kcuu6fv;
extern const uint32_t gm_tplngsd5kcuu6fv_i[];
extern const gm_tmpl_piece gm_tplngsd5kcuu6fv_p[];
extern const gm_spec *const gm_tplngsd5kcuu6fv_s[];
extern const gm_tmpl_piece gm_tplnitaca1dp5li_p[];
extern const gm_tmpl gm_tplnm352386bmep;
extern const uint32_t gm_tplnm352386bmep_i[];
extern const gm_tmpl_piece gm_tplnm352386bmep_p[];
extern const gm_spec *const gm_tplnm352386bmep_s[];
extern const gm_tmpl_piece gm_tplnndmi09f8nur_p[];
extern const uint32_t gm_tplnqad0r40bfej_i[];
extern const uint32_t gm_tplnrfprs01gfmj_i[];
extern const gm_tmpl_piece gm_tplnrs6u0h2oc92_p[];
extern const gm_tmpl_piece gm_tplo12k9ajl5nes_p[];
extern const uint32_t gm_tplo5614u5649ka_i[];
extern const gm_tmpl gm_tplo8bto3ii8iq9;
extern const uint32_t gm_tplo8bto3ii8iq9_i[];
extern const gm_tmpl_piece gm_tplo8bto3ii8iq9_p[];
extern const gm_spec *const gm_tplo8bto3ii8iq9_s[];
extern const gm_spec *const gm_tplo8vr0jo68llo_s[];
extern const uint32_t gm_tploa81ch7hbsd0_i[];
extern const gm_tmpl_piece gm_tploa995k06npbj_p[];
extern const gm_spec *const gm_tplob0n62u6gqqu_s[];
extern const gm_tmpl_piece gm_tplob6co4svk3k5_p[];
extern const gm_spec *const gm_tploc41q7b5gurn_s[];
extern const gm_tmpl_piece gm_tplodlbfsp8tfu3_p[];
extern const gm_tmpl_piece gm_tplolnkad3lslng_p[];
extern const gm_tmpl_piece gm_tplp0abp7005sqm_p[];
extern const gm_tmpl_piece gm_tplp5mnidu5m01d_p[];
extern const gm_tmpl gm_tplpav1vs8t9nu9;
extern const uint32_t gm_tplpav1vs8t9nu9_i[];
extern const gm_tmpl_piece gm_tplpav1vs8t9nu9_p[];
extern const gm_spec *const gm_tplpav1vs8t9nu9_s[];
extern const uint32_t gm_tplpd6kdal33co2_i[];
extern const gm_spec *const gm_tplpdnopu1p8235_s[];
extern const gm_spec *const gm_tplpf3jtoa0oclt_s[];
extern const gm_spec *const gm_tplpkvu14cg936d_s[];
extern const gm_tmpl_piece gm_tplpntcuu1r3a58_p[];
extern const gm_spec *const gm_tplpoe8f3eln4m2_s[];
extern const gm_spec *const gm_tplptu6sgqf50o0_s[];
extern const gm_tmpl gm_tplpv6nab1nov2l;
extern const uint32_t gm_tplpv6nab1nov2l_i[];
extern const gm_tmpl_piece gm_tplpv6nab1nov2l_p[];
extern const gm_spec *const gm_tplpv6nab1nov2l_s[];
extern const uint32_t gm_tplq3ra7ik9ej59_i[];
extern const gm_spec *const gm_tplq3ra7ik9ej59_s[];
extern const gm_tmpl gm_tplq73iq4gr17op;
extern const uint32_t gm_tplq73iq4gr17op_i[];
extern const gm_tmpl_piece gm_tplq73iq4gr17op_p[];
extern const gm_spec *const gm_tplq73iq4gr17op_s[];
extern const gm_spec *const gm_tplq7kai16vns6p_s[];
extern const gm_tmpl_piece gm_tplq7vstpm0lgda_p[];
extern const gm_spec *const gm_tplqbbl1qig0e0s_s[];
extern const gm_tmpl_piece gm_tplqbglsd979t5c_p[];
extern const uint32_t gm_tplqbtvqd3a4g25_i[];
extern const gm_tmpl_piece gm_tplqc1ev2l2fda9_p[];
extern const gm_tmpl_piece gm_tplqd1jqet3gtu1_p[];
extern const gm_tmpl gm_tplqgt00aqumuje;
extern const uint32_t gm_tplqgt00aqumuje_i[];
extern const gm_tmpl_piece gm_tplqgt00aqumuje_p[];
extern const gm_spec *const gm_tplqgt00aqumuje_s[];
extern const uint32_t gm_tplqh3e2aojp71u_i[];
extern const gm_tmpl_piece gm_tplqhugo4i82obv_p[];
extern const gm_tmpl_piece gm_tplqipe054or34v_p[];
extern const uint32_t gm_tplqj931pe0994k_i[];
extern const gm_spec *const gm_tplqjfk6t5rn06e_s[];
extern const uint32_t gm_tplqk3ejs6j0ne5_i[];
extern const gm_tmpl_piece gm_tplqk3ejs6j0ne5_p[];
extern const uint32_t gm_tplqnml5avaeikk_i[];
extern const gm_spec *const gm_tplqnufra7h2p1s_s[];
extern const gm_spec *const gm_tplqt9e8rihgogv_s[];
extern const gm_spec *const gm_tplqug1td3hpetg_s[];
extern const uint32_t gm_tplr1if0cs3oolt_i[];
extern const gm_tmpl_piece gm_tplr3ni3ai315ji_p[];
extern const gm_tmpl gm_tplr56tn8lpufgs;
extern const uint32_t gm_tplr56tn8lpufgs_i[];
extern const gm_tmpl_piece gm_tplr56tn8lpufgs_p[];
extern const gm_spec *const gm_tplr56tn8lpufgs_s[];
extern const gm_tmpl gm_tplr7712ujh33fl;
extern const uint32_t gm_tplr7712ujh33fl_i[];
extern const gm_tmpl_piece gm_tplr7712ujh33fl_p[];
extern const gm_spec *const gm_tplr7712ujh33fl_s[];
extern const gm_tmpl gm_tplr8vkqgn7raqo;
extern const uint32_t gm_tplr8vkqgn7raqo_i[];
extern const gm_tmpl_piece gm_tplr8vkqgn7raqo_p[];
extern const gm_spec *const gm_tplr8vkqgn7raqo_s[];
extern const gm_spec *const gm_tplrcmh11f2ocju_s[];
extern const uint32_t gm_tplre5cfbnr7ltv_i[];
extern const gm_tmpl_piece gm_tplrnntb2r7ns40_p[];
extern const gm_tmpl_piece gm_tplrof4shrhlnf8_p[];
extern const gm_tmpl_piece gm_tpls3fe84n8lo7i_p[];
extern const uint32_t gm_tpls77qni9k09vc_i[];
extern const uint32_t gm_tpls7kn7htisa6n_i[];
extern const gm_tmpl gm_tplsb492k540eai;
extern const uint32_t gm_tplsb492k540eai_i[];
extern const gm_tmpl_piece gm_tplsb492k540eai_p[];
extern const gm_spec *const gm_tplsb492k540eai_s[];
extern const gm_spec *const gm_tplscq8dbtjfamh_s[];
extern const gm_tmpl gm_tplsfkokuc5r1td;
extern const uint32_t gm_tplsfkokuc5r1td_i[];
extern const gm_tmpl_piece gm_tplsfkokuc5r1td_p[];
extern const gm_spec *const gm_tplsfkokuc5r1td_s[];
extern const gm_spec *const gm_tplsfrupsaoln3v_s[];
extern const gm_tmpl gm_tplsh3mbm625h0g;
extern const uint32_t gm_tplsh3mbm625h0g_i[];
extern const gm_tmpl_piece gm_tplsh3mbm625h0g_p[];
extern const gm_spec *const gm_tplsh3mbm625h0g_s[];
extern const uint32_t gm_tplshk2l4vr6fa3_i[];
extern const gm_tmpl gm_tplsnlr78cvfrao;
extern const uint32_t gm_tplsnlr78cvfrao_i[];
extern const gm_tmpl_piece gm_tplsnlr78cvfrao_p[];
extern const gm_spec *const gm_tplsnlr78cvfrao_s[];
extern const gm_spec *const gm_tplso01g2paq6o6_s[];
extern const gm_spec *const gm_tplspkvoi1o9of6_s[];
extern const uint32_t gm_tplsqibbtt8789o_i[];
extern const uint32_t gm_tplsrr2cfplv9ff_i[];
extern const gm_tmpl gm_tplsthb0beh35ts;
extern const uint32_t gm_tplsthb0beh35ts_i[];
extern const gm_tmpl_piece gm_tplsthb0beh35ts_p[];
extern const gm_spec *const gm_tplsthb0beh35ts_s[];
extern const gm_spec *const gm_tplt0hjq4nb3jer_s[];
extern const gm_spec *const gm_tplt1si3io9mdpe_s[];
extern const gm_tmpl gm_tplt3d4sltsd44m;
extern const uint32_t gm_tplt3d4sltsd44m_i[];
extern const gm_tmpl_piece gm_tplt3d4sltsd44m_p[];
extern const gm_spec *const gm_tplt3d4sltsd44m_s[];
extern const gm_spec *const gm_tplt56hco5v1mvf_s[];
extern const gm_tmpl_piece gm_tplt5jbb3iqgb9i_p[];
extern const gm_spec *const gm_tplt6nq68u4co32_s[];
extern const gm_spec *const gm_tplt8ovlno67b55_s[];
extern const uint32_t gm_tpltb9h1t5060dp_i[];
extern const gm_tmpl_piece gm_tpltev7cklsieaq_p[];
extern const gm_spec *const gm_tpltev7cklsieaq_s[];
extern const gm_tmpl gm_tpltfirffnkp263;
extern const uint32_t gm_tpltfirffnkp263_i[];
extern const gm_tmpl_piece gm_tpltfirffnkp263_p[];
extern const gm_spec *const gm_tpltfirffnkp263_s[];
extern const gm_tmpl gm_tplthb0oue219bq;
extern const uint32_t gm_tplthb0oue219bq_i[];
extern const gm_tmpl_piece gm_tplthb0oue219bq_p[];
extern const gm_spec *const gm_tplthb0oue219bq_s[];
extern const gm_spec *const gm_tpltrvmjk8jhmh7_s[];
extern const gm_tmpl gm_tplts0sr46c58jl;
extern const uint32_t gm_tplts0sr46c58jl_i[];
extern const gm_tmpl_piece gm_tplts0sr46c58jl_p[];
extern const gm_spec *const gm_tplts0sr46c58jl_s[];
extern const gm_tmpl_piece gm_tpltt9gd20e1v7c_p[];
extern const gm_spec *const gm_tpltt9gd20e1v7c_s[];
extern const uint32_t gm_tpltue438lnncbq_i[];
extern const gm_tmpl gm_tplu2kaori4llai;
extern const uint32_t gm_tplu2kaori4llai_i[];
extern const gm_tmpl_piece gm_tplu2kaori4llai_p[];
extern const gm_spec *const gm_tplu2kaori4llai_s[];
extern const gm_spec *const gm_tplu3lqf0bugia3_s[];
extern const gm_tmpl_piece gm_tplu7gq2cm8aanh_p[];
extern const gm_tmpl_piece gm_tpludt9aee4n14r_p[];
extern const gm_tmpl gm_tpluf8termqcruk;
extern const uint32_t gm_tpluf8termqcruk_i[];
extern const gm_tmpl_piece gm_tpluf8termqcruk_p[];
extern const gm_spec *const gm_tpluf8termqcruk_s[];
extern const gm_tmpl gm_tplugmq0iksatjg;
extern const uint32_t gm_tplugmq0iksatjg_i[];
extern const gm_tmpl_piece gm_tplugmq0iksatjg_p[];
extern const gm_spec *const gm_tplugmq0iksatjg_s[];
extern const gm_spec *const gm_tplut5h6m1atjoq_s[];
extern const uint32_t gm_tpluue9jh13g9df_i[];
extern const gm_tmpl gm_tplv05t19d54qoj;
extern const uint32_t gm_tplv05t19d54qoj_i[];
extern const gm_tmpl_piece gm_tplv05t19d54qoj_p[];
extern const gm_spec *const gm_tplv05t19d54qoj_s[];
extern const gm_spec *const gm_tplv0flrfdqimb1_s[];
extern const gm_tmpl gm_tplv1i7e9gejv6a;
extern const uint32_t gm_tplv1i7e9gejv6a_i[];
extern const gm_tmpl_piece gm_tplv1i7e9gejv6a_p[];
extern const gm_spec *const gm_tplv1i7e9gejv6a_s[];
extern const gm_spec *const gm_tplv3jh8gpmh6o5_s[];
extern const gm_tmpl gm_tplv8uh6lopnmo9;
extern const uint32_t gm_tplv8uh6lopnmo9_i[];
extern const gm_tmpl_piece gm_tplv8uh6lopnmo9_p[];
extern const gm_spec *const gm_tplv8uh6lopnmo9_s[];
extern const gm_tmpl gm_tplv9sqjp27m76e;
extern const uint32_t gm_tplv9sqjp27m76e_i[];
extern const gm_tmpl_piece gm_tplv9sqjp27m76e_p[];
extern const gm_spec *const gm_tplv9sqjp27m76e_s[];
extern const gm_spec *const gm_tplvajlmotjd2qr_s[];
extern const gm_tmpl gm_tplvbotf15ps5s5;
extern const uint32_t gm_tplvbotf15ps5s5_i[];
extern const gm_tmpl_piece gm_tplvbotf15ps5s5_p[];
extern const gm_spec *const gm_tplvbotf15ps5s5_s[];
extern const uint32_t gm_tplvdkp4jmav7jr_i[];
extern const gm_tmpl gm_tplvflfleui9qjm;
extern const uint32_t gm_tplvflfleui9qjm_i[];
extern const gm_tmpl_piece gm_tplvflfleui9qjm_p[];
extern const gm_spec *const gm_tplvflfleui9qjm_s[];
extern const gm_tmpl gm_tplvfsvhglok6jj;
extern const uint32_t gm_tplvfsvhglok6jj_i[];
extern const gm_tmpl_piece gm_tplvfsvhglok6jj_p[];
extern const gm_spec *const gm_tplvfsvhglok6jj_s[];
extern const gm_spec *const gm_tplviqo6u6r0sr3_s[];
extern const gm_tmpl gm_tplvkf7arb107q2;
extern const uint32_t gm_tplvkf7arb107q2_i[];
extern const gm_tmpl_piece gm_tplvkf7arb107q2_p[];
extern const gm_spec *const gm_tplvkf7arb107q2_s[];
extern const uint32_t gm_tplvklr3jhlo8m3_i[];
extern const uint32_t gm_tplvl1jqbb33ag2_i[];
extern const gm_tmpl_piece gm_tplvpehcchgo971_p[];
extern const uint32_t gm_tplvqa9kv7qt4oc_i[];
extern const gm_tmpl_piece gm_tplvqa9kv7qt4oc_p[];
extern const gm_tmpl gm_tplvqpq1chrhvno;
extern const uint32_t gm_tplvqpq1chrhvno_i[];
extern const gm_tmpl_piece gm_tplvqpq1chrhvno_p[];
extern const gm_spec *const gm_tplvqpq1chrhvno_s[];
extern const gm_spec *const gm_tplvttcl46jpcs3_s[];
extern const gm_type gm_tyjmccqdb90f;
extern const gm_type gm_tym9e81djunc;
extern const gm_type gm_tyo86ks9qkvt;
extern const gm_type gm_tyqa6gfa8omk;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0h3rgr6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_0h3rgr6 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l2;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l1 = t4;
            void * t5 = (void *)((uint8_t *)&((struct gm_fr_member_names_17el54k *)((&F.h)->up))->l1);
            gm_b16_8 t6 = gm_l1;
            gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
            gm_l2 = t7;
            uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
            if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srckfibkpt681aa, 456, 37);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srckfibkpt681aa, 456, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_6l4llq8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_6l4llq8 F;
    gm_b16_8 gm_l0; /* <item> */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_check_link_glakqpq *)((&F.h)->up))->l3));
            uint64_t r5; bool o6 = __builtin_add_overflow(t4, ((uint64_t)1ULL), &r5);
            if (o6) gm_trap("integer overflow in `+`", gm_srcbq9k9kvdokbf, 274, 36);
            uint64_t t7 = r5;
            GM_ST(uint64_t, ((uint8_t *)&((struct gm_fr_check_link_glakqpq *)((&F.h)->up))->l3), t7);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcbq9k9kvdokbf, 274, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_hrv26t6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_hrv26t6 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l1 = t4;
            gm_b16_8 t5 = gm_l1;
            if (gm_pf1m4b8q4e78(cx, gm_som0q53jqvjh3, 4, gm_src4mnsbede6fos, 57, 36)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t5) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t5)), GM_LD(uintptr_t, ((uint8_t *)&t5) + 8), gm_src4mnsbede6fos, 57, 36))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_src4mnsbede6fos, 57, 36)) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_src4mnsbede6fos, 57, 5);
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
const char gm_src4pc97o88riqc[] = "compiler/cgen/arith.gm";
const char gm_src5329n2jhsntp[] = "compiler/lint/copy.gm";
const char gm_src7u9cfdjh7s0n[] = "compiler/check/vector.gm";
const char gm_srcc2qutubarto1[] = "compiler/load/loader.gm";
const char gm_srccp2mbkjs1vn4[] = "compiler/interp/heap.gm";
const char gm_srcgp1oqfehgsjb[] = "compiler/lsp/json.gm";
const char gm_srch6h8rdicu443[] = "compiler/check/call.gm";
const char gm_srclr3r1dpiijpc[] = "compiler/testing/exe.gm";
const gm_spec *const gm_tpl05e80h89jqvd_s[] = {NULL};
const gm_spec *const gm_tpl083ga8282tol_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl09kglfpefe8i_s[] = {NULL};
const gm_spec *const gm_tpl0ej2jrvho6u0_s[] = {NULL};
const gm_spec *const gm_tpl0h84r2u2heg0_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl12e8u6hvkejb_s[] = {NULL, NULL};
const gm_spec *const gm_tpl13591hp4g6qh_s[] = {NULL};
const gm_spec *const gm_tpl14b6qietv6bm_s[] = {NULL, NULL};
const gm_spec *const gm_tpl17t0pmu1kh1u_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl1avjsgn54rqb_s[] = {NULL};
const gm_spec *const gm_tpl1iblbr9u646o_s[] = {NULL};
const gm_spec *const gm_tpl20t94jlvdndv_s[] = {NULL};
const gm_spec *const gm_tpl2bm4b6itkmm6_s[] = {NULL};
const gm_spec *const gm_tpl2cgrvj4ke42d_s[] = {NULL, NULL};
const gm_spec *const gm_tpl313mu5t4735i_s[] = {NULL};
const gm_spec *const gm_tpl34tavcs9635o_s[] = {NULL, NULL};
const gm_spec *const gm_tpl3ergg6mce23h_s[] = {NULL, NULL};
const gm_spec *const gm_tpl4561ar79q0c0_s[] = {NULL, NULL};
const gm_spec *const gm_tpl4bpbmadpmu1i_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl4esqdh06rpvt_s[] = {NULL, NULL};
const gm_spec *const gm_tpl4iu2nfn1okgb_s[] = {NULL, NULL};
const gm_spec *const gm_tpl4kmab8oiki17_s[] = {NULL};
const gm_spec *const gm_tpl5gt902348obb_s[] = {NULL};
const gm_spec *const gm_tpl5oph1bqhjo1e_s[] = {NULL};
const gm_spec *const gm_tpl5st0uuf0quab_s[] = {NULL};
const gm_spec *const gm_tpl64ltc7suc5qg_s[] = {NULL, NULL};
const gm_spec *const gm_tpl64qrh372lslh_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl6a59mrfvoo09_s[] = {NULL};
const gm_spec *const gm_tpl6cs6ch4uvrua_s[] = {NULL};
const gm_spec *const gm_tpl6lgf07g31gim_s[] = {NULL, NULL};
const gm_spec *const gm_tpl6salfcvi63n2_s[] = {NULL, NULL};
const gm_spec *const gm_tpl6vbod7vja73l_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl726l5jbs5bmh_s[] = {NULL};
const gm_spec *const gm_tpl7ll3166bkl17_s[] = {NULL, NULL};
const gm_spec *const gm_tpl7t589bv3t4us_s[] = {NULL};
const gm_spec *const gm_tpl84co505h34ud_s[] = {NULL};
const gm_spec *const gm_tpl86i0s811rrlp_s[] = {NULL};
const gm_spec *const gm_tpl8ro9atn5p92f_s[] = {NULL, NULL};
const gm_spec *const gm_tpl9iaqf5bbsv8o_s[] = {NULL};
const gm_spec *const gm_tpl9l4fqsojrjsa_s[] = {NULL};
const gm_spec *const gm_tpl9ne0ajfec16n_s[] = {NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplabinq26b6unj_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplaka4u9f9dfvk_s[] = {NULL};
const gm_spec *const gm_tplamv2e4uailq7_s[] = {NULL, NULL};
const gm_spec *const gm_tplaqje7ooa5hcl_s[] = {NULL};
const gm_spec *const gm_tplbcublle8ivup_s[] = {NULL};
const gm_spec *const gm_tplbqqnfekph537_s[] = {NULL, NULL};
const gm_spec *const gm_tplc9tn3pkjdpf4_s[] = {NULL, NULL};
const gm_spec *const gm_tplcds5gqa95rss_s[] = {NULL, NULL};
const gm_spec *const gm_tplceihv99elj2t_s[] = {NULL, NULL};
const gm_spec *const gm_tpldfm52le63mtn_s[] = {NULL, NULL};
const gm_spec *const gm_tpldq0sm30o8mph_s[] = {NULL, NULL};
const gm_spec *const gm_tpldrfjfgoqe179_s[] = {NULL};
const gm_spec *const gm_tplduutdqd8m5r5_s[] = {NULL};
const gm_spec *const gm_tple2rp5rj93mkj_s[] = {NULL};
const gm_spec *const gm_tplei8v65ors6jc_s[] = {NULL, NULL};
const gm_spec *const gm_tplf1r4lr6ofdql_s[] = {NULL, NULL};
const gm_spec *const gm_tplf3en286s8fa5_s[] = {NULL};
const gm_spec *const gm_tplf9jp9gfh0eik_s[] = {NULL};
const gm_spec *const gm_tplfg7j0v7js1f7_s[] = {NULL, NULL};
const gm_spec *const gm_tplfoosl93e8k5t_s[] = {&(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1}};
const gm_spec *const gm_tplfqju53pb0vaj_s[] = {NULL};
const gm_spec *const gm_tplfr6nl2m52h19_s[] = {NULL};
const gm_spec *const gm_tplfuin6hfnfo1c_s[] = {NULL};
const gm_spec *const gm_tplgavllsav46n0_s[] = {NULL};
const gm_spec *const gm_tplgv9va17kt7ll_s[] = {NULL};
const gm_spec *const gm_tplh1blptpemhkl_s[] = {NULL};
const gm_spec *const gm_tplhon5g1o0vuqi_s[] = {NULL, NULL};
const gm_spec *const gm_tplhu5cefj26aau_s[] = {NULL};
const gm_spec *const gm_tpli7hldrrhg7sr_s[] = {NULL};
const gm_spec *const gm_tpli824iv2hf4o6_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpliucrr7pqi5es_s[] = {NULL};
const gm_spec *const gm_tplj3vfuklvht3e_s[] = {NULL, NULL};
const gm_spec *const gm_tplj57b11lvidge_s[] = {NULL, NULL};
const gm_spec *const gm_tplj78420ela1cg_s[] = {NULL};
const gm_spec *const gm_tpljhar6jmbtsst_s[] = {NULL};
const gm_spec *const gm_tplk25d3iiaamil_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplka8uag807gql_s[] = {NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpll060ppa56f8b_s[] = {NULL};
const gm_spec *const gm_tpll6pilv9krmhr_s[] = {NULL};
const gm_spec *const gm_tpllir4kiseuk2b_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplllolcurrktr6_s[] = {NULL};
const gm_spec *const gm_tplm0bsttjtt8b0_s[] = {NULL, NULL};
const gm_spec *const gm_tplm1fuloaqnfv4_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplm6qp3tlbq28j_s[] = {NULL};
const gm_spec *const gm_tplme3pul5hs3bf_s[] = {NULL};
const gm_spec *const gm_tplmo1fik79j6ea_s[] = {NULL};
const gm_spec *const gm_tplmpsv5qagthkk_s[] = {NULL};
const gm_spec *const gm_tplmv1ukn138isf_s[] = {NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpln1d9amhhlm2o_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplnb9cl2s2ukch_s[] = {NULL};
const gm_spec *const gm_tplngekt6vfvnsv_s[] = {NULL, NULL};
const gm_spec *const gm_tplo8vr0jo68llo_s[] = {NULL};
const gm_spec *const gm_tplob0n62u6gqqu_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tploc41q7b5gurn_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplpdnopu1p8235_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplpf3jtoa0oclt_s[] = {NULL, NULL};
const gm_spec *const gm_tplpkvu14cg936d_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplpoe8f3eln4m2_s[] = {NULL};
const gm_spec *const gm_tplptu6sgqf50o0_s[] = {NULL};
const gm_spec *const gm_tplq3ra7ik9ej59_s[] = {NULL};
const gm_spec *const gm_tplq7kai16vns6p_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplqbbl1qig0e0s_s[] = {NULL, NULL};
const gm_spec *const gm_tplqjfk6t5rn06e_s[] = {NULL, NULL};
const gm_spec *const gm_tplqnufra7h2p1s_s[] = {NULL};
const gm_spec *const gm_tplqt9e8rihgogv_s[] = {NULL, NULL};
const gm_spec *const gm_tplqug1td3hpetg_s[] = {NULL, NULL};
const gm_spec *const gm_tplrcmh11f2ocju_s[] = {NULL};
const gm_spec *const gm_tplsb492k540eai_s[] = {NULL};
const gm_spec *const gm_tplscq8dbtjfamh_s[] = {NULL};
const gm_spec *const gm_tplsfrupsaoln3v_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplso01g2paq6o6_s[] = {NULL, NULL};
const gm_spec *const gm_tplspkvoi1o9of6_s[] = {NULL, NULL};
const gm_spec *const gm_tplsthb0beh35ts_s[] = {NULL, NULL};
const gm_spec *const gm_tplt0hjq4nb3jer_s[] = {NULL};
const gm_spec *const gm_tplt1si3io9mdpe_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplt56hco5v1mvf_s[] = {NULL};
const gm_spec *const gm_tplt6nq68u4co32_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplt8ovlno67b55_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpltev7cklsieaq_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpltrvmjk8jhmh7_s[] = {NULL};
const gm_spec *const gm_tpltt9gd20e1v7c_s[] = {NULL};
const gm_spec *const gm_tplu3lqf0bugia3_s[] = {NULL};
const gm_spec *const gm_tplugmq0iksatjg_s[] = {NULL};
const gm_spec *const gm_tplut5h6m1atjoq_s[] = {NULL};
const gm_spec *const gm_tplv0flrfdqimb1_s[] = {NULL};
const gm_spec *const gm_tplv3jh8gpmh6o5_s[] = {NULL};
const gm_spec *const gm_tplv8uh6lopnmo9_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplvajlmotjd2qr_s[] = {NULL, NULL};
const gm_spec *const gm_tplviqo6u6r0sr3_s[] = {NULL, NULL};
const gm_spec *const gm_tplvttcl46jpcs3_s[] = {NULL, NULL, NULL, NULL};
const gm_tmpl gm_tpl0o9c14r51hfo = {2u, gm_tpl0o9c14r51hfo_p, gm_tpl0o9c14r51hfo_i, gm_tpl0o9c14r51hfo_s};
const gm_tmpl gm_tpl0rlavm9oqi24 = {3u, gm_tpl0rlavm9oqi24_p, gm_tpl0rlavm9oqi24_i, gm_tpl0rlavm9oqi24_s};
const gm_tmpl gm_tpl13591hp4g6qh = {1u, gm_tpl13591hp4g6qh_p, gm_tpl13591hp4g6qh_i, gm_tpl13591hp4g6qh_s};
const gm_tmpl gm_tpl1dirc8ldrsao = {3u, gm_tpl1dirc8ldrsao_p, gm_tpl1dirc8ldrsao_i, gm_tpl1dirc8ldrsao_s};
const gm_tmpl gm_tpl1o2hk14gjjj0 = {1u, gm_tpl1o2hk14gjjj0_p, gm_tpl1o2hk14gjjj0_i, gm_tpl1o2hk14gjjj0_s};
const gm_tmpl gm_tpl1os40ii1iecn = {1u, gm_tpl1os40ii1iecn_p, gm_tpl1os40ii1iecn_i, gm_tpl1os40ii1iecn_s};
const gm_tmpl gm_tpl1rl5q0rqbd7p = {1u, gm_tpl1rl5q0rqbd7p_p, gm_tpl1rl5q0rqbd7p_i, gm_tpl1rl5q0rqbd7p_s};
const gm_tmpl gm_tpl20a4acojo21c = {4u, gm_tpl20a4acojo21c_p, gm_tpl20a4acojo21c_i, gm_tpl20a4acojo21c_s};
const gm_tmpl gm_tpl233lm3m83499 = {1u, gm_tpl233lm3m83499_p, gm_tpl233lm3m83499_i, gm_tpl233lm3m83499_s};
const gm_tmpl gm_tpl29spsi1atllj = {3u, gm_tpl29spsi1atllj_p, gm_tpl29spsi1atllj_i, gm_tpl29spsi1atllj_s};
const gm_tmpl gm_tpl2arnv1ofugei = {1u, gm_tpl2arnv1ofugei_p, gm_tpl2arnv1ofugei_i, gm_tpl2arnv1ofugei_s};
const gm_tmpl gm_tpl2b904kc90480 = {1u, gm_tpl2b904kc90480_p, gm_tpl2b904kc90480_i, gm_tpl2b904kc90480_s};
const gm_tmpl gm_tpl2cgrvj4ke42d = {2u, gm_tpl2cgrvj4ke42d_p, gm_tpl2cgrvj4ke42d_i, gm_tpl2cgrvj4ke42d_s};
const gm_tmpl gm_tpl3gvm2qp5ptd2 = {2u, gm_tpl3gvm2qp5ptd2_p, gm_tpl3gvm2qp5ptd2_i, gm_tpl3gvm2qp5ptd2_s};
const gm_tmpl gm_tpl4476c5bksft1 = {2u, gm_tpl4476c5bksft1_p, gm_tpl4476c5bksft1_i, gm_tpl4476c5bksft1_s};
const gm_tmpl gm_tpl4524ekle00ud = {4u, gm_tpl4524ekle00ud_p, gm_tpl4524ekle00ud_i, gm_tpl4524ekle00ud_s};
const gm_tmpl gm_tpl4j60geuv9mvj = {1u, gm_tpl4j60geuv9mvj_p, gm_tpl4j60geuv9mvj_i, gm_tpl4j60geuv9mvj_s};
const gm_tmpl gm_tpl4t11fkqh738f = {2u, gm_tpl4t11fkqh738f_p, gm_tpl4t11fkqh738f_i, gm_tpl4t11fkqh738f_s};
const gm_tmpl gm_tpl598lk1ciiqvp = {1u, gm_tpl598lk1ciiqvp_p, gm_tpl598lk1ciiqvp_i, gm_tpl598lk1ciiqvp_s};
const gm_tmpl gm_tpl59s4m8f706c9 = {2u, gm_tpl59s4m8f706c9_p, gm_tpl59s4m8f706c9_i, gm_tpl59s4m8f706c9_s};
const gm_tmpl gm_tpl5a8og2hcltt3 = {3u, gm_tpl5a8og2hcltt3_p, gm_tpl5a8og2hcltt3_i, gm_tpl5a8og2hcltt3_s};
const gm_tmpl gm_tpl5prdhjdrg194 = {2u, gm_tpl5prdhjdrg194_p, gm_tpl5prdhjdrg194_i, gm_tpl5prdhjdrg194_s};
const gm_tmpl gm_tpl5uko75p49g56 = {2u, gm_tpl5uko75p49g56_p, gm_tpl5uko75p49g56_i, gm_tpl5uko75p49g56_s};
const gm_tmpl gm_tpl6gmot6d7lh6n = {3u, gm_tpl6gmot6d7lh6n_p, gm_tpl6gmot6d7lh6n_i, gm_tpl6gmot6d7lh6n_s};
const gm_tmpl gm_tpl6itong91gv3f = {2u, gm_tpl6itong91gv3f_p, gm_tpl6itong91gv3f_i, gm_tpl6itong91gv3f_s};
const gm_tmpl gm_tpl6mra39d4fo2s = {3u, gm_tpl6mra39d4fo2s_p, gm_tpl6mra39d4fo2s_i, gm_tpl6mra39d4fo2s_s};
const gm_tmpl gm_tpl78lk9ugsmc0b = {1u, gm_tpl78lk9ugsmc0b_p, gm_tpl78lk9ugsmc0b_i, gm_tpl78lk9ugsmc0b_s};
const gm_tmpl gm_tpl7da8g7lhjmr1 = {1u, gm_tpl7da8g7lhjmr1_p, gm_tpl7da8g7lhjmr1_i, gm_tpl7da8g7lhjmr1_s};
const gm_tmpl gm_tpl7eehu7c1eqka = {1u, gm_tpl7eehu7c1eqka_p, gm_tpl7eehu7c1eqka_i, gm_tpl7eehu7c1eqka_s};
const gm_tmpl gm_tpl7medmjmlvos1 = {6u, gm_tpl7medmjmlvos1_p, gm_tpl7medmjmlvos1_i, gm_tpl7medmjmlvos1_s};
const gm_tmpl gm_tpl7umtk166uvmr = {1u, gm_tpl7umtk166uvmr_p, gm_tpl7umtk166uvmr_i, gm_tpl7umtk166uvmr_s};
const gm_tmpl gm_tpl7vtkjsp2pa8b = {4u, gm_tpl7vtkjsp2pa8b_p, gm_tpl7vtkjsp2pa8b_i, gm_tpl7vtkjsp2pa8b_s};
const gm_tmpl gm_tpl81nunf5qvo6u = {1u, gm_tpl81nunf5qvo6u_p, gm_tpl81nunf5qvo6u_i, gm_tpl81nunf5qvo6u_s};
const gm_tmpl gm_tpl8qng452btr2a = {2u, gm_tpl8qng452btr2a_p, gm_tpl8qng452btr2a_i, gm_tpl8qng452btr2a_s};
const gm_tmpl gm_tpl8ro9atn5p92f = {2u, gm_tpl8ro9atn5p92f_p, gm_tpl8ro9atn5p92f_i, gm_tpl8ro9atn5p92f_s};
const gm_tmpl gm_tpl8vbes2v6184u = {1u, gm_tpl8vbes2v6184u_p, gm_tpl8vbes2v6184u_i, gm_tpl8vbes2v6184u_s};
const gm_tmpl gm_tpl91a2oiuc6ot7 = {3u, gm_tpl91a2oiuc6ot7_p, gm_tpl91a2oiuc6ot7_i, gm_tpl91a2oiuc6ot7_s};
const gm_tmpl gm_tpl91b7sjgnmine = {3u, gm_tpl91b7sjgnmine_p, gm_tpl91b7sjgnmine_i, gm_tpl91b7sjgnmine_s};
const gm_tmpl gm_tpl97n87h3pcva1 = {1u, gm_tpl97n87h3pcva1_p, gm_tpl97n87h3pcva1_i, gm_tpl97n87h3pcva1_s};
const gm_tmpl gm_tpla9stdteeiicv = {1u, gm_tpla9stdteeiicv_p, gm_tpla9stdteeiicv_i, gm_tpla9stdteeiicv_s};
const gm_tmpl gm_tplamm5t71udd8k = {2u, gm_tplamm5t71udd8k_p, gm_tplamm5t71udd8k_i, gm_tplamm5t71udd8k_s};
const gm_tmpl gm_tplanhmvcr622qo = {1u, gm_tplanhmvcr622qo_p, gm_tplanhmvcr622qo_i, gm_tplanhmvcr622qo_s};
const gm_tmpl gm_tplav4294nbj9c7 = {1u, gm_tplav4294nbj9c7_p, gm_tplav4294nbj9c7_i, gm_tplav4294nbj9c7_s};
const gm_tmpl gm_tplb9i7jfun8rvg = {1u, gm_tplb9i7jfun8rvg_p, gm_tplb9i7jfun8rvg_i, gm_tplb9i7jfun8rvg_s};
const gm_tmpl gm_tplbnt69qpq91i8 = {1u, gm_tplbnt69qpq91i8_p, gm_tplbnt69qpq91i8_i, gm_tplbnt69qpq91i8_s};
const gm_tmpl gm_tplbtbg1u9fmcqq = {4u, gm_tplbtbg1u9fmcqq_p, gm_tplbtbg1u9fmcqq_i, gm_tplbtbg1u9fmcqq_s};
const gm_tmpl gm_tplcb1495nujsb7 = {7u, gm_tplcb1495nujsb7_p, gm_tplcb1495nujsb7_i, gm_tplcb1495nujsb7_s};
const gm_tmpl gm_tplchb0p7o483ge = {2u, gm_tplchb0p7o483ge_p, gm_tplchb0p7o483ge_i, gm_tplchb0p7o483ge_s};
const gm_tmpl gm_tplcouqld11oeil = {1u, gm_tplcouqld11oeil_p, gm_tplcouqld11oeil_i, gm_tplcouqld11oeil_s};
const gm_tmpl gm_tplcpa735d0vjnu = {1u, gm_tplcpa735d0vjnu_p, gm_tplcpa735d0vjnu_i, gm_tplcpa735d0vjnu_s};
const gm_tmpl gm_tpld47p9te8ar36 = {1u, gm_tpld47p9te8ar36_p, gm_tpld47p9te8ar36_i, gm_tpld47p9te8ar36_s};
const gm_tmpl gm_tpldumo4kdlpj8p = {2u, gm_tpldumo4kdlpj8p_p, gm_tpldumo4kdlpj8p_i, gm_tpldumo4kdlpj8p_s};
const gm_tmpl gm_tplduutdqd8m5r5 = {1u, gm_tplduutdqd8m5r5_p, gm_tplduutdqd8m5r5_i, gm_tplduutdqd8m5r5_s};
const gm_tmpl gm_tple0332mlih7si = {1u, gm_tple0332mlih7si_p, gm_tple0332mlih7si_i, gm_tple0332mlih7si_s};
const gm_tmpl gm_tple6jpca094i2j = {2u, gm_tple6jpca094i2j_p, gm_tple6jpca094i2j_i, gm_tple6jpca094i2j_s};
const gm_tmpl gm_tplesi2enviniss = {4u, gm_tplesi2enviniss_p, gm_tplesi2enviniss_i, gm_tplesi2enviniss_s};
const gm_tmpl gm_tpleth0e0pd50mr = {1u, gm_tpleth0e0pd50mr_p, gm_tpleth0e0pd50mr_i, gm_tpleth0e0pd50mr_s};
const gm_tmpl gm_tplf2fho9tvid0p = {1u, gm_tplf2fho9tvid0p_p, gm_tplf2fho9tvid0p_i, gm_tplf2fho9tvid0p_s};
const gm_tmpl gm_tplfg7j0v7js1f7 = {2u, gm_tplfg7j0v7js1f7_p, gm_tplfg7j0v7js1f7_i, gm_tplfg7j0v7js1f7_s};
const gm_tmpl gm_tplfr6nl2m52h19 = {1u, gm_tplfr6nl2m52h19_p, gm_tplfr6nl2m52h19_i, gm_tplfr6nl2m52h19_s};
const gm_tmpl gm_tplg3qra7vq6h3a = {1u, gm_tplg3qra7vq6h3a_p, gm_tplg3qra7vq6h3a_i, gm_tplg3qra7vq6h3a_s};
const gm_tmpl gm_tplg7bh9jeid72i = {1u, gm_tplg7bh9jeid72i_p, gm_tplg7bh9jeid72i_i, gm_tplg7bh9jeid72i_s};
const gm_tmpl gm_tplgan4klju39em = {1u, gm_tplgan4klju39em_p, gm_tplgan4klju39em_i, gm_tplgan4klju39em_s};
const gm_tmpl gm_tplgih8dfq5hun7 = {2u, gm_tplgih8dfq5hun7_p, gm_tplgih8dfq5hun7_i, gm_tplgih8dfq5hun7_s};
const gm_tmpl gm_tplgt6vkfjdh8cd = {1u, gm_tplgt6vkfjdh8cd_p, gm_tplgt6vkfjdh8cd_i, gm_tplgt6vkfjdh8cd_s};
const gm_tmpl gm_tplh6c513o0038j = {1u, gm_tplh6c513o0038j_p, gm_tplh6c513o0038j_i, gm_tplh6c513o0038j_s};
const gm_tmpl gm_tplha5n8a69m774 = {4u, gm_tplha5n8a69m774_p, gm_tplha5n8a69m774_i, gm_tplha5n8a69m774_s};
const gm_tmpl gm_tplhmairn7oibac = {17u, gm_tplhmairn7oibac_p, gm_tplhmairn7oibac_i, gm_tplhmairn7oibac_s};
const gm_tmpl gm_tplhu7i2tvvfh15 = {3u, gm_tplhu7i2tvvfh15_p, gm_tplhu7i2tvvfh15_i, gm_tplhu7i2tvvfh15_s};
const gm_tmpl gm_tpli4rfoi75onhj = {2u, gm_tpli4rfoi75onhj_p, gm_tpli4rfoi75onhj_i, gm_tpli4rfoi75onhj_s};
const gm_tmpl gm_tpli824iv2hf4o6 = {5u, gm_tpli824iv2hf4o6_p, gm_tpli824iv2hf4o6_i, gm_tpli824iv2hf4o6_s};
const gm_tmpl gm_tplj2qf541fmi6f = {1u, gm_tplj2qf541fmi6f_p, gm_tplj2qf541fmi6f_i, gm_tplj2qf541fmi6f_s};
const gm_tmpl gm_tplka0n288r3ccr = {1u, gm_tplka0n288r3ccr_p, gm_tplka0n288r3ccr_i, gm_tplka0n288r3ccr_s};
const gm_tmpl gm_tplkik9smkp1eb6 = {1u, gm_tplkik9smkp1eb6_p, gm_tplkik9smkp1eb6_i, gm_tplkik9smkp1eb6_s};
const gm_tmpl gm_tplkkvn546crm4n = {4u, gm_tplkkvn546crm4n_p, gm_tplkkvn546crm4n_i, gm_tplkkvn546crm4n_s};
const gm_tmpl gm_tpll66vdkdoc48m = {4u, gm_tpll66vdkdoc48m_p, gm_tpll66vdkdoc48m_i, gm_tpll66vdkdoc48m_s};
const gm_tmpl gm_tplmm5mgnjalbfr = {1u, gm_tplmm5mgnjalbfr_p, gm_tplmm5mgnjalbfr_i, gm_tplmm5mgnjalbfr_s};
const gm_tmpl gm_tpln1d9amhhlm2o = {10u, gm_tpln1d9amhhlm2o_p, gm_tpln1d9amhhlm2o_i, gm_tpln1d9amhhlm2o_s};
const gm_tmpl gm_tpln4hlddpv31vi = {3u, gm_tpln4hlddpv31vi_p, gm_tpln4hlddpv31vi_i, gm_tpln4hlddpv31vi_s};
const gm_tmpl gm_tplnbsaptme3355 = {1u, gm_tplnbsaptme3355_p, gm_tplnbsaptme3355_i, gm_tplnbsaptme3355_s};
const gm_tmpl gm_tplncn0njcasr6v = {3u, gm_tplncn0njcasr6v_p, gm_tplncn0njcasr6v_i, gm_tplncn0njcasr6v_s};
const gm_tmpl gm_tplngsd5kcuu6fv = {1u, gm_tplngsd5kcuu6fv_p, gm_tplngsd5kcuu6fv_i, gm_tplngsd5kcuu6fv_s};
const gm_tmpl gm_tplnm352386bmep = {2u, gm_tplnm352386bmep_p, gm_tplnm352386bmep_i, gm_tplnm352386bmep_s};
const gm_tmpl gm_tplo8bto3ii8iq9 = {2u, gm_tplo8bto3ii8iq9_p, gm_tplo8bto3ii8iq9_i, gm_tplo8bto3ii8iq9_s};
const gm_tmpl gm_tplpav1vs8t9nu9 = {1u, gm_tplpav1vs8t9nu9_p, gm_tplpav1vs8t9nu9_i, gm_tplpav1vs8t9nu9_s};
const gm_tmpl gm_tplpv6nab1nov2l = {1u, gm_tplpv6nab1nov2l_p, gm_tplpv6nab1nov2l_i, gm_tplpv6nab1nov2l_s};
const gm_tmpl gm_tplq73iq4gr17op = {1u, gm_tplq73iq4gr17op_p, gm_tplq73iq4gr17op_i, gm_tplq73iq4gr17op_s};
const gm_tmpl gm_tplqgt00aqumuje = {2u, gm_tplqgt00aqumuje_p, gm_tplqgt00aqumuje_i, gm_tplqgt00aqumuje_s};
const gm_tmpl gm_tplr56tn8lpufgs = {2u, gm_tplr56tn8lpufgs_p, gm_tplr56tn8lpufgs_i, gm_tplr56tn8lpufgs_s};
const gm_tmpl gm_tplr7712ujh33fl = {2u, gm_tplr7712ujh33fl_p, gm_tplr7712ujh33fl_i, gm_tplr7712ujh33fl_s};
const gm_tmpl gm_tplr8vkqgn7raqo = {2u, gm_tplr8vkqgn7raqo_p, gm_tplr8vkqgn7raqo_i, gm_tplr8vkqgn7raqo_s};
const gm_tmpl gm_tplsb492k540eai = {1u, gm_tplsb492k540eai_p, gm_tplsb492k540eai_i, gm_tplsb492k540eai_s};
const gm_tmpl gm_tplsfkokuc5r1td = {1u, gm_tplsfkokuc5r1td_p, gm_tplsfkokuc5r1td_i, gm_tplsfkokuc5r1td_s};
const gm_tmpl gm_tplsh3mbm625h0g = {2u, gm_tplsh3mbm625h0g_p, gm_tplsh3mbm625h0g_i, gm_tplsh3mbm625h0g_s};
const gm_tmpl gm_tplsnlr78cvfrao = {1u, gm_tplsnlr78cvfrao_p, gm_tplsnlr78cvfrao_i, gm_tplsnlr78cvfrao_s};
const gm_tmpl gm_tplsthb0beh35ts = {2u, gm_tplsthb0beh35ts_p, gm_tplsthb0beh35ts_i, gm_tplsthb0beh35ts_s};
const gm_tmpl gm_tplt3d4sltsd44m = {2u, gm_tplt3d4sltsd44m_p, gm_tplt3d4sltsd44m_i, gm_tplt3d4sltsd44m_s};
const gm_tmpl gm_tpltfirffnkp263 = {2u, gm_tpltfirffnkp263_p, gm_tpltfirffnkp263_i, gm_tpltfirffnkp263_s};
const gm_tmpl gm_tplthb0oue219bq = {2u, gm_tplthb0oue219bq_p, gm_tplthb0oue219bq_i, gm_tplthb0oue219bq_s};
const gm_tmpl gm_tplts0sr46c58jl = {1u, gm_tplts0sr46c58jl_p, gm_tplts0sr46c58jl_i, gm_tplts0sr46c58jl_s};
const gm_tmpl gm_tplu2kaori4llai = {2u, gm_tplu2kaori4llai_p, gm_tplu2kaori4llai_i, gm_tplu2kaori4llai_s};
const gm_tmpl gm_tpluf8termqcruk = {1u, gm_tpluf8termqcruk_p, gm_tpluf8termqcruk_i, gm_tpluf8termqcruk_s};
const gm_tmpl gm_tplugmq0iksatjg = {1u, gm_tplugmq0iksatjg_p, gm_tplugmq0iksatjg_i, gm_tplugmq0iksatjg_s};
const gm_tmpl gm_tplv05t19d54qoj = {2u, gm_tplv05t19d54qoj_p, gm_tplv05t19d54qoj_i, gm_tplv05t19d54qoj_s};
const gm_tmpl gm_tplv1i7e9gejv6a = {4u, gm_tplv1i7e9gejv6a_p, gm_tplv1i7e9gejv6a_i, gm_tplv1i7e9gejv6a_s};
const gm_tmpl gm_tplv8uh6lopnmo9 = {3u, gm_tplv8uh6lopnmo9_p, gm_tplv8uh6lopnmo9_i, gm_tplv8uh6lopnmo9_s};
const gm_tmpl gm_tplv9sqjp27m76e = {2u, gm_tplv9sqjp27m76e_p, gm_tplv9sqjp27m76e_i, gm_tplv9sqjp27m76e_s};
const gm_tmpl gm_tplvbotf15ps5s5 = {1u, gm_tplvbotf15ps5s5_p, gm_tplvbotf15ps5s5_i, gm_tplvbotf15ps5s5_s};
const gm_tmpl gm_tplvflfleui9qjm = {1u, gm_tplvflfleui9qjm_p, gm_tplvflfleui9qjm_i, gm_tplvflfleui9qjm_s};
const gm_tmpl gm_tplvfsvhglok6jj = {1u, gm_tplvfsvhglok6jj_p, gm_tplvfsvhglok6jj_i, gm_tplvfsvhglok6jj_s};
const gm_tmpl gm_tplvkf7arb107q2 = {1u, gm_tplvkf7arb107q2_p, gm_tplvkf7arb107q2_i, gm_tplvkf7arb107q2_s};
const gm_tmpl gm_tplvqpq1chrhvno = {1u, gm_tplvqpq1chrhvno_p, gm_tplvqpq1chrhvno_i, gm_tplvqpq1chrhvno_s};
const gm_tmpl_piece gm_tpl00cra9g0k45s_p[] = {{gm_sabu5uehm65n2, 21}, {gm_s9sev457i3j7n, 4}, {gm_sg398rgmck67b, 3}};
const gm_tmpl_piece gm_tpl086m1ajg247c_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_susgjrlk3c5f0, 7}, {gm_sugavfshit856, 9}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tpl0fhs0t9qdf1o_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}, {gm_so514oj5nbhcu, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl0gp8c85b2lf5_p[] = {{gm_snv7mc8ntub24, 47}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl0j5bgvjcbn0u_p[] = {{gm_ssi95ri2h4d02, 27}, {gm_srs1vrn2uaams, 1}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl0ke3r8fiia4r_p[] = {{gm_sbui4p06qp6ck, 28}, {gm_ssbkvpakdcg1k, 6}};
const gm_tmpl_piece gm_tpl0rlavm9oqi24_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sgks95jf6fs73, 20}, {gm_smooaouqmm8dj, 1}, {gm_shv4snn6f8hcr, 28}};
const gm_tmpl_piece gm_tpl16petnrgdcd0_p[] = {{gm_smb8nsuj9ie93, 14}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl1avjsgn54rqb_p[] = {{gm_s34n6pusofh71, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl1c171lb88cdd_p[] = {{gm_sildigrhtv45e, 17}, {gm_sf0p9o7g7fr6l, 6}, {gm_saempncqkf91r, 2}};
const gm_tmpl_piece gm_tpl1g1lh0kp24oa_p[] = {{gm_sql9r2b8fvivt, 9}, {gm_spjohm68l6glq, 21}, {gm_svbftsm94r0bv, 6}};
const gm_tmpl_piece gm_tpl1os40ii1iecn_p[] = {{gm_s2umangg4hn1g, 14}, {gm_s542rn9shphg8, 15}};
const gm_tmpl_piece gm_tpl21i1s6vvb3m4_p[] = {{gm_sas0hkqll4io0, 9}, {gm_s2053hhvf16fm, 1}};
const gm_tmpl_piece gm_tpl233lm3m83499_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sdqgviehbfoad, 5}};
const gm_tmpl_piece gm_tpl2cgrvj4ke42d_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sfi2ha42r1nr2, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl2qrjfa8j0fl5_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_svm0rfaqk3b2q, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl355ags9581ob_p[] = {{gm_scs8vqh2gmmhd, 50}, {gm_s60bni5maq4df, 11}, {gm_sr244s00qj01d, 2}};
const gm_tmpl_piece gm_tpl3bnq2anocte9_p[] = {{gm_s7826b3jmrqb0, 9}, {gm_se1krm9tn2oaa, 2}};
const gm_tmpl_piece gm_tpl3gvm2qp5ptd2_p[] = {{gm_s8s91tiu5r15q, 44}, {gm_sfm9elkq6bn2e, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl46q9qc00shi4_p[] = {{gm_scgm14ju6d0p8, 14}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl4cbbis3gd1k3_p[] = {{gm_sic09fl7nt2ch, 26}, {gm_sm6j3q0d4n2jp, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl4dq73nmettt5_p[] = {{gm_so7svtiv1a8q1, 4}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tpl4ivrecg9ncdu_p[] = {{gm_ss41pl5vtg7un, 71}, {gm_sg5gdnrith2i6, 2}, {gm_s0rhh4q7cpm4k, 19}};
const gm_tmpl_piece gm_tpl4r3nb3bvll23_p[] = {{gm_sf2lr85hprd3e, 39}, {gm_siv3d9he09v3r, 43}};
const gm_tmpl_piece gm_tpl56vmcuv96cg9_p[] = {{gm_sp13ju5l53585, 23}, {gm_sr4qbrk6tlgaa, 0}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl598lk1ciiqvp_p[] = {{gm_s66ph0gfa95db, 13}, {gm_seleu2adn5qnd, 17}};
const gm_tmpl_piece gm_tpl5g9nntg83p8b_p[] = {{gm_stmsadvf54k73, 2}, {gm_sfubbk3hjkq2r, 40}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl5uko75p49g56_p[] = {{gm_s049rlfhk3i2d, 5}, {gm_sfdbfrv46lihd, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl6104ee3qfshr_p[] = {{gm_s1sbi8ubhu50k, 50}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl7ilj5n9objtq_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_saqgvkkehjsjb, 32}};
const gm_tmpl_piece gm_tpl7m3t7r6fqpf7_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_snupav63sairp, 11}};
const gm_tmpl_piece gm_tpl7ufg84gs1lc7_p[] = {{gm_ss57t1l5iltlq, 13}, {gm_sgfqei77bsia8, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl80egqk8smgev_p[] = {{gm_s17uhrtdo7u30, 1}, {gm_s17uhrtdo7u30, 1}};
const gm_tmpl_piece gm_tpl8in37qe3omcj_p[] = {{gm_scf54410fgl4j, 38}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl9ag92r71r0vm_p[] = {{gm_s1dc9boe5unhg, 31}, {gm_saqhctf8022h0, 32}, {gm_sighahslvsc1i, 10}};
const gm_tmpl_piece gm_tpl9fn8oj7nnnsq_p[] = {{gm_sj2if4k4r1jer, 7}, {gm_s9rujqsrrjcho, 109}};
const gm_tmpl_piece gm_tpl9fvss98clvrp_p[] = {{gm_s6nsj8sc6uhgg, 16}, {gm_sl325guoc37g3, 22}};
const gm_tmpl_piece gm_tpl9q5j6bisf2fa_p[] = {{gm_smovt8m5cpa13, 6}, {gm_sosa5l0k6d34c, 14}, {gm_s6qvm08vrap69, 3}, {gm_s1asl1e1tvb0k, 1}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpl9ufa4j1b7kal_p[] = {{gm_s5s758m9u7gle, 25}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpl9vu8tqcv1d9s_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sqr1rjjq3dpuh, 16}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpla0ci4vs242iu_p[] = {{gm_s1a10cqdk6nil, 18}, {gm_se8us8vie9a8a, 36}, {gm_si2ih874n1gou, 9}};
const gm_tmpl_piece gm_tplabinq26b6unj_p[] = {{gm_s7ucknluds45h, 4}, {gm_sfdbfrv46lihd, 1}, {gm_sfdbfrv46lihd, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplb5ldvki5nntc_p[] = {{gm_smovt8m5cpa13, 6}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplbaqni0b3abqt_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s5c1hnl87iu5m, 8}, {gm_s2053hhvf16fm, 1}};
const gm_tmpl_piece gm_tplbbsc15j8h2ga_p[] = {{gm_su0hvb2ne02u0, 16}, {gm_s7euumoo24h31, 2}};
const gm_tmpl_piece gm_tplbdrnrpalngql_p[] = {{gm_sau1b8ivpr0us, 7}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplbeuo81m3df3c_p[] = {{gm_sabu5uehm65n2, 21}, {gm_s6t4r831mukdr, 2}};
const gm_tmpl_piece gm_tplbj4npuroda3f_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_skocj074p3r97, 8}, {gm_sp02qqgucmpt7, 2}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplbppmjhpd8ol0_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_skau91fmi0eei, 41}};
const gm_tmpl_piece gm_tplc0d2186db2jt_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_svld8cc4ptomt, 15}};
const gm_tmpl_piece gm_tplce7sc9hodc13_p[] = {{gm_sd9ri1vmgds82, 8}, {gm_sheobcjeiocck, 15}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplcef5l6cvp5lt_p[] = {{gm_s4oil3epcv041, 2}, {gm_sbqbp3u228b97, 29}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpld9ts015fint3_p[] = {{gm_smooaouqmm8dj, 1}, {gm_s5u5a7gd2hl9p, 5}};
const gm_tmpl_piece gm_tplda38mm1025ge_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sao4mumvadoct, 3}, {gm_sql0in48r61nd, 4}, {gm_sgfqei77bsia8, 3}, {gm_sql0in48r61nd, 4}, {gm_s66b184memqnd, 3}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tpldftt3jdrahlt_p[] = {{gm_ssr91slt3ah1c, 10}, {gm_sr4qbrk6tlgaa, 0}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpldhmlf1n8qcdi_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s9r71kmnlqpv8, 16}, {gm_sj0ur1o7a5584, 15}, {gm_sdo8qcbf74mp0, 6}};
const gm_tmpl_piece gm_tpldueenbh2sgd4_p[] = {{gm_snp3hu7kun7ag, 22}, {gm_su01gdacg3ppp, 8}, {gm_sg398rgmck67b, 3}};
const gm_tmpl_piece gm_tpldvhfe50cnb69_p[] = {{gm_s9c2oj94op2h6, 9}, {gm_st6hj8v0h80gt, 5}, {gm_s8abg3vemofr6, 3}, {gm_su823o267lacu, 102}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tple68gu1adcjq9_p[] = {{gm_sn12gub8tjj5h, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplecdkophfbiam_p[] = {{gm_s24mnh1a25k0f, 30}, {gm_st2ertk6tjva7, 13}};
const gm_tmpl_piece gm_tpleeuqgcr11ngn_p[] = {{gm_svacokl3cteo0, 25}, {gm_se0oi28sapu01, 10}, {gm_s6tv3tkgkodqm, 11}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplei9v853i04sp_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_snrrubbi4s05g, 15}, {gm_stgav1sk1eq6u, 58}};
const gm_tmpl_piece gm_tplenpf051q0m4n_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpleoop2177apvc_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s2u5posfkbgnq, 1}, {gm_s2u5posfkbgnq, 1}};
const gm_tmpl_piece gm_tplf0gekbh2mtku_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_su50gais1htu0, 37}};
const gm_tmpl_piece gm_tplfbtlvu2isf2h_p[] = {{gm_spv9savsu9h0b, 3}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplfm527r729efa_p[] = {{gm_sape9vso1m5fa, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplful0p0r5pifs_p[] = {{gm_st3m5olipe6ov, 16}, {gm_st6hj8v0h80gt, 5}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplg05c24rh8peq_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_ssqifgr2icd6s, 2}};
const gm_tmpl_piece gm_tplg45jfs21ci2c_p[] = {{gm_s1g69u68g0ra5, 2}, {gm_s5aqsh8ujnh9q, 37}};
const gm_tmpl_piece gm_tplg500ggupdf4i_p[] = {{gm_snh0sl2cdagdn, 12}, {gm_s38n4b5q0dbil, 44}};
const gm_tmpl_piece gm_tplgnobkmf75b9i_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tplgp15dehsbtkk_p[] = {{gm_s39bdeo03pq18, 8}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplgq3upqu7s79r_p[] = {{gm_sbmedt8vmt5ve, 4}, {gm_sfdbfrv46lihd, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplh78gsuiavvas_p[] = {{gm_sohcj0rggg07e, 50}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplhn9hprqs819d_p[] = {{gm_sgtu622jht4g2, 50}, {gm_svhe917rela4j, 19}};
const gm_tmpl_piece gm_tpli7ohq5o5sj5n_p[] = {{gm_sk754mj3p1el6, 1}, {gm_ssqifgr2icd6s, 2}, {gm_s2053hhvf16fm, 1}};
const gm_tmpl_piece gm_tpliqpvgtpo05sn_p[] = {{gm_sj2tf1ift8sga, 20}, {gm_s5m8hf43qu2i7, 26}, {gm_sqdfr4k3vea5p, 3}, {gm_sbkmg1rlq4fa7, 26}};
const gm_tmpl_piece gm_tplj16n1o03849r_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_su2eceicte7s7, 5}, {gm_s8m45dpj5ugr4, 5}, {gm_s09fhtm8l4rep, 3}};
const gm_tmpl_piece gm_tplja7aur1oiv70_p[] = {{gm_s677r76pa1oks, 6}, {gm_sfi2ha42r1nr2, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpljicvo0ni8aba_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_spcl9lgniu1he, 17}};
const gm_tmpl_piece gm_tplkv6ui0mqvq2g_p[] = {{gm_smovt8m5cpa13, 6}, {gm_sosa5l0k6d34c, 14}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpll9g0ell7ocpk_p[] = {{gm_slmeqnp27emg7, 8}, {gm_ss42hvrprenkk, 25}, {gm_sg5gdnrith2i6, 2}, {gm_sk0r84vdcvmnk, 12}, {gm_suup20cg9lo2d, 7}, {gm_s6ecg17hs903q, 8}, {gm_stuutd7nfq21h, 2}, {gm_si416c0mdacrd, 4}, {gm_s9kou5ia9uri9, 5}};
const gm_tmpl_piece gm_tpllbgi055uoiek_p[] = {{gm_svsm5u96riots, 17}, {gm_sgqejiat83g1e, 46}, {gm_set3pi1jhetbi, 6}};
const gm_tmpl_piece gm_tpllhk8pmjfso1j_p[] = {{gm_sadscuj628cl7, 7}, {gm_snsbh43f5eo46, 4}, {gm_srs4ljjaijhd2, 23}, {gm_stobu0djed6ck, 9}};
const gm_tmpl_piece gm_tplm6g8a9odsasi_p[] = {{gm_s9agk8q084c5k, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplme3pul5hs3bf_p[] = {{gm_sb5it581p5nll, 4}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplmueh9mdvaqub_p[] = {{gm_sm922ui8k0oca, 8}, {gm_sh75rh8dk51gf, 11}};
const gm_tmpl_piece gm_tplna8gfl3ukgs3_p[] = {{gm_s44ek1gecbajs, 6}, {gm_s5qoh8o6sn63e, 23}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplnbbj7t6ap7ht_p[] = {{gm_s5kue8bfcvqfr, 26}, {gm_sujipgkhb2fih, 7}, {gm_s3p0rbs9hc3lg, 1}, {gm_sgsd94uuisj8t, 72}};
const gm_tmpl_piece gm_tplnitaca1dp5li_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s0147vgtvilnq, 18}, {gm_snsqui8o89u28, 14}};
const gm_tmpl_piece gm_tplnndmi09f8nur_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_su99evi25asg4, 5}};
const gm_tmpl_piece gm_tplnrs6u0h2oc92_p[] = {{gm_scvhv26j3m3dm, 17}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplo12k9ajl5nes_p[] = {{gm_sdv6tt97thssp, 21}, {gm_sdhjq72t0cbpl, 8}, {gm_srv0b9jht81lt, 17}};
const gm_tmpl_piece gm_tploa995k06npbj_p[] = {{gm_s4ld5r1oi34jh, 17}, {gm_s3ilkivmnrfk4, 27}, {gm_sj1d6tstgco64, 88}};
const gm_tmpl_piece gm_tplob6co4svk3k5_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_svc1hcla4tqfb, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplodlbfsp8tfu3_p[] = {{gm_s6cs6skh2j334, 16}, {gm_smjeoupcicact, 9}, {gm_spc0s79b28n7m, 8}, {gm_s6o30d8d984s9, 7}, {gm_sgvhjfacilshn, 8}, {gm_sniofsn8cqk74, 9}, {gm_sjk23rfp9022d, 13}};
const gm_tmpl_piece gm_tplolnkad3lslng_p[] = {{gm_ski6lj2hn0gg4, 34}, {gm_sr9q8kbf253ia, 44}};
const gm_tmpl_piece gm_tplp0abp7005sqm_p[] = {{gm_siamquc19uikr, 1}, {gm_s9tth144tddms, 12}, {gm_sl3q52ep6aa53, 1}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplp5mnidu5m01d_p[] = {{gm_squ4ohvqdqaap, 78}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplpntcuu1r3a58_p[] = {{gm_s3lq4s2q8ds4c, 31}, {gm_skarrfopl04v8, 22}, {gm_sg1s2lngtt92k, 12}, {gm_s0l40gug6qpuq, 16}, {gm_sgukm1evmrjal, 3}, {gm_svgqk0opgf2oj, 59}, {gm_sb9k4ip91d7oa, 5}, {gm_si9mfda4b719e, 62}, {gm_sjqp7nokhg4lk, 4}};
const gm_tmpl_piece gm_tplq7vstpm0lgda_p[] = {{gm_s3plkb3in5251, 8}, {gm_sjcm18p35o9rq, 3}, {gm_sbtq9nj91tefv, 9}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplqbglsd979t5c_p[] = {{gm_s72phirci8af3, 24}, {gm_sg5gdnrith2i6, 2}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplqc1ev2l2fda9_p[] = {{gm_sirn08c749sdd, 27}, {gm_s0m0qaokk51g0, 3}, {gm_sl815v6oegv7n, 13}, {gm_s0m0qaokk51g0, 3}, {gm_sj62oio2kg58e, 19}};
const gm_tmpl_piece gm_tplqd1jqet3gtu1_p[] = {{gm_sh3h2k0f6ktgl, 12}, {gm_s6cchf3tcs9e4, 20}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplqhugo4i82obv_p[] = {{gm_sscsdlec4a8vt, 17}, {gm_s2red66k9r4fi, 9}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplqipe054or34v_p[] = {{gm_sogkbe8nre26a, 16}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplqk3ejs6j0ne5_p[] = {{gm_s8lp1qs73nh4k, 16}, {gm_srfobl8913gvl, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplr3ni3ai315ji_p[] = {{gm_si5gf6931vagv, 10}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplr56tn8lpufgs_p[] = {{gm_som0q53jqvjh3, 4}, {gm_scedkbh0j6d78, 5}, {gm_smdiuajagl2mn, 4}};
const gm_tmpl_piece gm_tplrnntb2r7ns40_p[] = {{gm_sh8houjk34evm, 14}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplrof4shrhlnf8_p[] = {{gm_samqd3pmlgqmg, 13}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpls3fe84n8lo7i_p[] = {{gm_s4oil3epcv041, 2}, {gm_sk3630dksnfoh, 23}, {gm_sa3obh41s8f7p, 69}};
const gm_tmpl_piece gm_tplt5jbb3iqgb9i_p[] = {{gm_svov1cn84ftlm, 65}, {gm_sjfpr08jc3ep1, 22}, {gm_s72hk5u5ngq4i, 10}};
const gm_tmpl_piece gm_tpltev7cklsieaq_p[] = {{gm_s7mfvroqmll1f, 2}, {gm_siqfg68f4ud6j, 15}, {gm_smbbaf3n1gml2, 7}, {gm_spbhhmdbp335u, 3}};
const gm_tmpl_piece gm_tpltt9gd20e1v7c_p[] = {{gm_su6ol10q7la3d, 6}, {gm_sh5qb2l9hubh3, 8}};
const gm_tmpl_piece gm_tplu7gq2cm8aanh_p[] = {{gm_s0kajv5j53ub7, 37}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpludt9aee4n14r_p[] = {{gm_snaenr23tbgmb, 22}, {gm_s3347hvco05di, 5}, {gm_svokmu75u5n0c, 13}, {gm_s3347hvco05di, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplvpehcchgo971_p[] = {{gm_slgmqgkioa4u6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplvqa9kv7qt4oc_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_smooaouqmm8dj, 1}, {gm_si4l87dl4tpkq, 7}, {gm_ssk4a01soguqj, 6}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s1asl1e1tvb0k, 1}};
const gm_type gm_tyjmccqdb90f = {.size = 1, .align = 1, .kind = GM_K_INT, .bits = 8, .is_signed = false};
const gm_type gm_tyjmccqdb90f;
const gm_type gm_tym9e81djunc = {.size = 4, .align = 4, .kind = GM_K_FLOAT, .bits = 32};
const gm_type gm_tym9e81djunc;
const gm_type gm_tyqa6gfa8omk = {.size = 4, .align = 4, .kind = GM_K_INT, .bits = 32, .is_signed = true};
const gm_type gm_tyqa6gfa8omk;
const uint32_t gm_tpl01fkj9ukhaa3_i[] = {0u};
const uint32_t gm_tpl0euitr9hdia5_i[] = {0u};
const uint32_t gm_tpl0n7kmejjk8it_i[] = {0u, 1u};
const uint32_t gm_tpl0pd502nvjf7a_i[] = {0u};
const uint32_t gm_tpl1bno78mc58dl_i[] = {0u};
const uint32_t gm_tpl1irt5kietk82_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tpl1r51q3hoe535_i[] = {0u, 1u};
const uint32_t gm_tpl2b904kc90480_i[] = {0u};
const uint32_t gm_tpl2egb012obd94_i[] = {0u};
const uint32_t gm_tpl2g2o9ile1980_i[] = {0u, 1u};
const uint32_t gm_tpl2r84u6vnc58k_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tpl2s7fpgand9gc_i[] = {0u};
const uint32_t gm_tpl2u97uspakqd9_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl3bnq2anocte9_i[] = {0u};
const uint32_t gm_tpl40m4ah8dc3ae_i[] = {0u};
const uint32_t gm_tpl470f2c4pe1su_i[] = {0u};
const uint32_t gm_tpl4auv8p2j34k5_i[] = {0u, 1u};
const uint32_t gm_tpl4ivrecg9ncdu_i[] = {0u, 1u};
const uint32_t gm_tpl4upfck3qu2er_i[] = {0u, 1u};
const uint32_t gm_tpl53ihvkdedd31_i[] = {0u};
const uint32_t gm_tpl598lk1ciiqvp_i[] = {0u};
const uint32_t gm_tpl5hatm6q530pn_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl606t7k5n88ss_i[] = {0u, 1u};
const uint32_t gm_tpl60na9lt7qpkj_i[] = {0u, 1u};
const uint32_t gm_tpl61n2hscg0et2_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl63djfct0mcad_i[] = {0u};
const uint32_t gm_tpl6jo4mmkdq2l1_i[] = {0u};
const uint32_t gm_tpl6n3s5f3ittsi_i[] = {0u, 1u};
const uint32_t gm_tpl6ou7bec1fkae_i[] = {0u, 1u};
const uint32_t gm_tpl6r9reg6pnruq_i[] = {0u};
const uint32_t gm_tpl7o4acs2tt6pp_i[] = {0u};
const uint32_t gm_tpl7qfg883s3b97_i[] = {0u};
const uint32_t gm_tpl7t589bv3t4us_i[] = {0u};
const uint32_t gm_tpl80avf3gokjno_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl85lm1gq83hlb_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpl8mdj3nc4lpjv_i[] = {0u};
const uint32_t gm_tpl9cnobsjbh0r2_i[] = {0u};
const uint32_t gm_tpl9d1siq0c9pir_i[] = {0u};
const uint32_t gm_tpl9d5e93nnc8t1_i[] = {0u};
const uint32_t gm_tpl9ne0ajfec16n_i[] = {0u, 1u, 2u, 3u, 4u, 5u};
const uint32_t gm_tpl9qchhbq9qlbm_i[] = {0u, 1u};
const uint32_t gm_tplanqv6irfvtut_i[] = {0u, 1u, 2u};
const uint32_t gm_tplaqps986okebj_i[] = {0u, 1u};
const uint32_t gm_tplbdrnrpalngql_i[] = {0u};
const uint32_t gm_tplbjh9dr2llnkb_i[] = {0u, 1u};
const uint32_t gm_tplbk03g4ml40ad_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplbnt69qpq91i8_i[] = {0u};
const uint32_t gm_tplbntllbe37f81_i[] = {0u};
const uint32_t gm_tplbp75knfmjbql_i[] = {0u};
const uint32_t gm_tplc5prn5jeukj6_i[] = {0u, 1u};
const uint32_t gm_tplc6hhdd7vcmvk_i[] = {0u};
const uint32_t gm_tplc90dj04km0n5_i[] = {0u, 1u};
const uint32_t gm_tplcds5gqa95rss_i[] = {0u, 1u};
const uint32_t gm_tplcein5d5pq2cs_i[] = {0u, 1u};
const uint32_t gm_tplcte0qqjaa0q0_i[] = {0u};
const uint32_t gm_tpld3o5iqghffqq_i[] = {0u, 1u};
const uint32_t gm_tpldofkl5m8hqug_i[] = {0u};
const uint32_t gm_tpldumo4kdlpj8p_i[] = {0u, 1u};
const uint32_t gm_tple23ed5kepfqi_i[] = {0u};
const uint32_t gm_tple34dj6br49e3_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplekdseqnnupsj_i[] = {0u, 1u};
const uint32_t gm_tplem02qig8laoi_i[] = {0u};
const uint32_t gm_tplengfdkso35ua_i[] = {0u, 1u};
const uint32_t gm_tpleugrd3m4ci89_i[] = {0u};
const uint32_t gm_tplf3e41f3siie3_i[] = {0u, 1u, 2u};
const uint32_t gm_tplg0rms0t7302m_i[] = {0u};
const uint32_t gm_tplg21rd4as3ph6_i[] = {0u, 1u, 2u};
const uint32_t gm_tplg45jfs21ci2c_i[] = {0u};
const uint32_t gm_tplg91jq41nbbkt_i[] = {0u, 1u};
const uint32_t gm_tplgfhhtcka09o4_i[] = {0u, 1u, 2u};
const uint32_t gm_tplgj9d3igk4i62_i[] = {0u};
const uint32_t gm_tplgjqa4ejl8op1_i[] = {0u};
const uint32_t gm_tplgr8ep9alj0ph_i[] = {0u, 1u};
const uint32_t gm_tplhfdokrq767sh_i[] = {0u};
const uint32_t gm_tplhlt4sff0mrem_i[] = {0u, 1u};
const uint32_t gm_tpli8aunvo88ks7_i[] = {0u};
const uint32_t gm_tplibksf6f0e00q_i[] = {0u};
const uint32_t gm_tplicmhpq9e6sbc_i[] = {0u, 1u};
const uint32_t gm_tpligtpf80arr7h_i[] = {0u, 1u, 2u};
const uint32_t gm_tplilne6f8eq90i_i[] = {0u};
const uint32_t gm_tpliuif0nifmui1_i[] = {0u, 1u};
const uint32_t gm_tpljd9codq72bc9_i[] = {0u};
const uint32_t gm_tpljgv7fkm5ld25_i[] = {0u, 1u, 2u};
const uint32_t gm_tplk3nkr2eh6don_i[] = {0u};
const uint32_t gm_tplkjc5mpc5i80h_i[] = {0u, 1u};
const uint32_t gm_tpll4f2ihchoe9c_i[] = {0u, 1u};
const uint32_t gm_tpllfgb42aufjqd_i[] = {0u};
const uint32_t gm_tpllp752obmvr55_i[] = {0u};
const uint32_t gm_tplmaqqdmni75bf_i[] = {0u, 1u};
const uint32_t gm_tplmjku4vg3eupf_i[] = {0u, 1u};
const uint32_t gm_tplmr0a02ecbaom_i[] = {0u};
const uint32_t gm_tpln38mjgenps81_i[] = {0u, 1u};
const uint32_t gm_tplnqad0r40bfej_i[] = {0u};
const uint32_t gm_tplnrfprs01gfmj_i[] = {0u};
const uint32_t gm_tplo5614u5649ka_i[] = {0u, 1u, 2u};
const uint32_t gm_tploa81ch7hbsd0_i[] = {0u};
const uint32_t gm_tplpd6kdal33co2_i[] = {0u, 1u};
const uint32_t gm_tplq3ra7ik9ej59_i[] = {0u};
const uint32_t gm_tplqbtvqd3a4g25_i[] = {0u};
const uint32_t gm_tplqh3e2aojp71u_i[] = {0u};
const uint32_t gm_tplqj931pe0994k_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplqk3ejs6j0ne5_i[] = {0u, 1u};
const uint32_t gm_tplqnml5avaeikk_i[] = {0u};
const uint32_t gm_tplr1if0cs3oolt_i[] = {0u};
const uint32_t gm_tplre5cfbnr7ltv_i[] = {0u, 1u};
const uint32_t gm_tpls77qni9k09vc_i[] = {0u};
const uint32_t gm_tpls7kn7htisa6n_i[] = {0u};
const uint32_t gm_tplshk2l4vr6fa3_i[] = {0u, 1u};
const uint32_t gm_tplsqibbtt8789o_i[] = {0u};
const uint32_t gm_tplsrr2cfplv9ff_i[] = {0u};
const uint32_t gm_tpltb9h1t5060dp_i[] = {0u, 1u};
const uint32_t gm_tpltue438lnncbq_i[] = {0u};
const uint32_t gm_tpluue9jh13g9df_i[] = {0u, 1u, 2u};
const uint32_t gm_tplv8uh6lopnmo9_i[] = {0u, 1u, 2u};
const uint32_t gm_tplvbotf15ps5s5_i[] = {0u};
const uint32_t gm_tplvdkp4jmav7jr_i[] = {0u, 1u};
const uint32_t gm_tplvklr3jhlo8m3_i[] = {0u};
const uint32_t gm_tplvl1jqbb33ag2_i[] = {0u};
const uint32_t gm_tplvqa9kv7qt4oc_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
const uint8_t gm_s0021gvebcdc4[9] = "F.ret = ";
const uint8_t gm_s039kgf77ln8v[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000g\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_s05qp4ie7rju8[84] = "` uses local variables of the enclosing function, so it cannot call itself (\302\24711.8)";
const uint8_t gm_s09r5jslo2reb[13] = "nvariants = ";
const uint8_t gm_s0br9o46sotd5[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\205\000\000\0001\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_s0i3i86nphpsh[6] = "isize";
const uint8_t gm_s0jhhkkbgc6ks[18] = "stack overflow: `";
const uint8_t gm_s0lh54534p3vd[10] = "max-value";
const uint8_t gm_s0lpuem1an6p4[7] = "gm_l{}";
const uint8_t gm_s0mtf4em37at6[29] = "static const gm_spec *const ";
const uint8_t gm_s0n03nrf296lf[33] = "` inside a `with` that handles `";
const uint8_t gm_s0njd7khs134c[31] = "` (it would round to infinity)";
const uint8_t gm_s0o0qjspcfsmh[5] = "HOME";
const uint8_t gm_s0orlismakhop[16] = "-fif-conversion";
const uint8_t gm_s0pf2spartj4v[12] = "upvar {} {}";
const uint8_t gm_s0pm20lshv21u[21] = "the operands of `is`";
const uint8_t gm_s0ri8hi6r25m8[37] = "invalid version requirement `{}`: {}";
const uint8_t gm_s0s0aq2lhn254[22] = "compiler/load/core.gm";
const uint8_t gm_s0u0sleiibm5v[3] = "ol";
const uint8_t gm_s142hf0mc48om[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000F\001\000\000&\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_s14frn0dagfs0[25] = "this constant expression";
const uint8_t gm_s16vnj1m5rn6t[9] = "position";
const uint8_t gm_s1br3p106uhjq[10] = "partition";
const uint8_t gm_s1cbu3jc19u4t[36] = "missing argument for parameter `{}`";
const uint8_t gm_s1gjt02j7um1i[37] = "internal error: not a memory builtin";
const uint8_t gm_s1isj7vodtbvg[37] = "` does not apply to a foreign member";
const uint8_t gm_s1js4dn5e65ja[11] = "NetConnect";
const uint8_t gm_s1o010pu82atd[17] = "an input must be";
const uint8_t gm_s1r2hqmnh3o3j[16] = "no module given";
const uint8_t gm_s1rmhdquo3v18[29] = "    memcpy(gm_g{}, {}, {});\012";
const uint8_t gm_s1rqgg2ibsis6[10] = "[^]const ";
const uint8_t gm_s1t08grtrn18m[9] = "lanes_gt";
const uint8_t gm_s1t9ao79ferut[22] = " has an unknown key `";
const uint8_t gm_s22lkrbtp01c5[23] = "(({} < 0) == ({} < 0))";
const uint8_t gm_s23t712hkqlmp[5] = "`: `";
const uint8_t gm_s25jkl4c6n8uo[4] = "`x.";
const uint8_t gm_s263r2tflkl4v[25] = "constant `@asm` clobbers";
const uint8_t gm_s29885s5blpal[13] = "store {}, {}";
const uint8_t gm_s2aq94adogrjl[35] = "operator `{}` is not defined on {}";
const uint8_t gm_s2bnc7q68giqi[17] = " && ((__int128)(";
const uint8_t gm_s2btotlgsaibr[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000!\001\000\000\012\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_s2cd81u5542io[10] = "otherwise";
const uint8_t gm_s2cuflhqkkuaq[2] = "}";
const uint8_t gm_s2io5nh7o36cj[15] = "this operation";
const uint8_t gm_s2iv492pjqjc4[12] = "gm_filc_ctx";
const uint8_t gm_s2kl2d59c04nf[19] = "(NULL, cx, NULL);\012";
const uint8_t gm_s2nj060fs0sf4[6] = "{}-{}";
const uint8_t gm_s2o3od9vb6lnu[12] = "gm_proc_get";
const uint8_t gm_s2s2f3gkf4c98[13] = "cached build";
const uint8_t gm_s2t2dldl583td[24] = "` is inferred as both `";
const uint8_t gm_s3154a2sfmm32[18] = "<h1>Module <code>";
const uint8_t gm_s3avlcjhdh976[10] = "global {}";
const uint8_t gm_s3bsv9idfdodt[7] = "fb->cx";
const uint8_t gm_s3dhhc4laniae[22] = "compiler/repl/repl.gm";
const uint8_t gm_s3e38enju83br[209] = "\000\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_s3gqldvcud6t6[17] = "if ((uintptr_t)(";
const uint8_t gm_s3hloevtmnfuc[8] = "--check";
const uint8_t gm_s3lho0ls7g9c5[40] = "    {} m;\012    memset(&m, 0, sizeof m);\012";
const uint8_t gm_s3q858hngo0ho[39] = "` cannot be inferred here; annotate it";
const uint8_t gm_s3tgka429lf13[60] = "` cannot be part of another type; use a function pointer `^";
const uint8_t gm_s3v0kr2fdpfld[5] = "none";
const uint8_t gm_s420tm9ua7ggg[4] = "tag";
const uint8_t gm_s47llp68ptjgu[8] = "cstring";
const uint8_t gm_s49bn4rn2il7d[26] = "`is` is not defined for `";
const uint8_t gm_s4fips0vo4jt3[8] = "h = {};";
const uint8_t gm_s4g2k7ups3ui6[6] = "yield";
const uint8_t gm_s4g3cajt15dmi[3] = "<=";
const uint8_t gm_s4h0qjq5qavbn[10] = "DA:{},{}\012";
const uint8_t gm_s4iasgp71ld87[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\216\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_s4jd1uj5sjs8a[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\260\000\000\000\035\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_s4k8r5f4ncaqi[10] = "imaginary";
const uint8_t gm_s4lpfn3di4rp9[66] = "internal error: a lane comparison of a value that is not a vector";
const uint8_t gm_s4s2kvgh5crk6[63] = "`#soa` stores the fields of a struct, and `{}` is not a struct";
const uint8_t gm_s4vd6e3mkiiso[3] = "-c";
const uint8_t gm_s4vnlpe2ovpj8[6] = "unit-";
const uint8_t gm_s54psrmgl6ti7[9] = "sortText";
const uint8_t gm_s54vpa0pj59v6[8] = "## {}\012\012";
const uint8_t gm_s554r118mfcao[34] = "`: it is part of the core library";
const uint8_t gm_s58f7dj21gtf9[18] = "kind = GM_K_UNION";
const uint8_t gm_s5aqsh8ujnh9q[38] = "` applies only to a function (\302\24720.2)";
const uint8_t gm_s5c5548ll8taj[10] = "CopyElems";
const uint8_t gm_s5dgs745ff5ko[39] = "the target (default: the C compiler's)";
const uint8_t gm_s5dmjeoiprfiv[72] = "__builtin_isnan({}) \077 ({})0 : ({} < {} \077 {} : ({} >= {} \077 {} : ({}){}))";
const uint8_t gm_s5e0seq0he7ou[10] = "NetListen";
const uint8_t gm_s5i2d5hm88htd[4] = "^{}";
const uint8_t gm_s5l2i6ke0df90[21] = "was ended by signal ";
const uint8_t gm_s5llvatkc5h5i[14] = "tag_size = {}";
const uint8_t gm_s5lr3gdic3o7e[15] = "__BYTE_ORDER__";
const uint8_t gm_s5p0i898s3u4j[11] = "{}, {}, {}";
const uint8_t gm_s5ppm345dr9s1[15] = "` is a keyword";
const uint8_t gm_s5ruqkgqqrd34[17] = "\000\000\000\000\000\000\000\0000\000\000\000\000\000\000\000";
const uint8_t gm_s67i6nrmepi0v[7] = "ret {}";
const uint8_t gm_s68manpbqruhq[61] = "run only the tests whose name or module path\012contains <text>";
const uint8_t gm_s6b88abo6eubp[43] = "` is given (in an instantiation used here)";
const uint8_t gm_s6bsma1542aam[45] = "a math vector has 2, 3 or 4 elements, not {}";
const uint8_t gm_s6f1t1a0knpo4[82] = "dependency `{}` combines `{}` and `{}`; use only one of version, tag, branch, rev";
const uint8_t gm_s6g1iihs2bped[46] = "`#bits` needs an unsigned integer type, not `";
const uint8_t gm_s6h791v5191sh[11] = "heap_alloc";
const uint8_t gm_s6mdrbgb4t376[12] = "->n) break;";
const uint8_t gm_s6of1cedjphs7[3] = "}}";
const uint8_t gm_s6p9ub6gu603m[7] = "WrapOk";
const uint8_t gm_s6qodcf63v3t5[16] = "gm_find_program";
const uint8_t gm_s6rsd5hkg3dmo[6] = "check";
const uint8_t gm_s6ug9r8dlfd2a[44] = "    if (!{}(a + {}, b + {})) return false;\012";
const uint8_t gm_s6unleea2pm63[35] = "static const uint8_t {}[{}] = {};\012";
const uint8_t gm_s71712nhgrg2u[15] = "cannot remove ";
const uint8_t gm_s75kntcasmsv8[14] = "gm_cover_path";
const uint8_t gm_s75s8qhd4m46e[6] = "{} |\012";
const uint8_t gm_s77fi68iu5q6n[5] = " };\012";
const uint8_t gm_s79a8ngj68lbf[5] = "bang";
const uint8_t gm_s79oh9p16aa8u[3] = "ns";
const uint8_t gm_s7c9tkfjfc169[26] = " has no parameter named `";
const uint8_t gm_s7d1996rqhrdg[9] = "targets ";
const uint8_t gm_s7f68g16j7a3o[6] = "trunc";
const uint8_t gm_s7jb41578vpai[24] = "cannot read LDFLAGS: {}";
const uint8_t gm_s7m5v91ik4pn5[52] = "internal error: a discarded fiber finished normally";
const uint8_t gm_s7md88e2dla9a[9] = "({} ^ 1)";
const uint8_t gm_s7o0qkitvahn6[41] = "\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000M\002\000\000Q\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000";
const uint8_t gm_s7qmtc95nc11i[24] = "#define GM_TRAP_HOOK 1\012";
const uint8_t gm_s7ui29rhp3vg3[24] = "unknown format spec `{:";
const uint8_t gm_s80l47nhae4el[23] = "Content-Length: {}\015\012\015\012";
const uint8_t gm_s83ukg5uj91s8[41] = "\000\000\000\000\000\000\000\000\034\000\000\000\000\000\000\0001\000\000\000\077\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_s87h4fof1n9ou[11] = "` has none";
const uint8_t gm_s882hbgnf10vn[21] = "`: `resume k(value)`";
const uint8_t gm_s8847rbf0kqmg[12] = "gm_set_args";
const uint8_t gm_s88n2j7klshfh[11] = "<constant>";
const uint8_t gm_s8bgt7glif1c9[34] = "invalid `git` URL of dependency `";
const uint8_t gm_s8n5rdam5o1sm[69] = "`#allow` takes the names of lint rules: `#allow(\"unused-parameter\")`";
const uint8_t gm_s8tadn8f0l2a8[21] = "` is not defined on ";
const uint8_t gm_s90saa0unq8e7[35] = "jumps out of a constant expression";
const uint8_t gm_s927d308r5uo5[6] = ", h);";
const uint8_t gm_s92h6r7qs6q4f[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000`\001\000\000 \000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_s92lujmr0a2c3[35] = "{} (in an instantiation used here)";
const uint8_t gm_s93i7ga7kbfpu[34] = "a `string` or byte slice, for `q`";
const uint8_t gm_s93qviqoij24b[31] = "gamma: {} has no `.gm` files{}";
const uint8_t gm_s98peh781o3et[51] = "`assert` takes a condition and an optional message";
const uint8_t gm_s99ou2clapmm5[300] = "`; they have `reduce_sum`, `reduce_min`, `reduce_max`, `min`, `max`, `abs`, `sqrt`, `to`, `trunc`, `round`, `floor`, `ceil`, `sat`, `shuffle`, `store` and the comparisons `lanes_eq`, `lanes_ne`, `lanes_lt`, `lanes_le`, `lanes_gt` and `lanes_ge`, and masks `all`, `any`, `count` and `select` (\302\24721.3)";
const uint8_t gm_s9b72a1n5tssp[78] = "parentheses around a whole condition, initializer, assigned or returned value";
const uint8_t gm_s9d2hbf54sh7s[22] = "untyped-complex {} {}";
const uint8_t gm_s9dgf2prvg715[6] = "Round";
const uint8_t gm_s9gu0s8i5nn2u[9] = "<length>";
const uint8_t gm_s9ijlf0rj4app[21] = "%{} = bits-get {}.{}";
const uint8_t gm_s9lvs84vghure[12] = "` is a copy";
const uint8_t gm_s9nc0lk9srfom[8] = " :: ...";
const uint8_t gm_s9ob1jv3b8md9[10] = "{}{} f{};";
const uint8_t gm_s9pljh6s0n7in[17] = ", (void *)&F.h, ";
const uint8_t gm_s9v13eck7gubi[4] = " hi";
const uint8_t gm_sa65ola9dm4t8[8] = "gm_lb{}";
const uint8_t gm_sa8k1i3b5phk8[24] = "invalid memory access: ";
const uint8_t gm_sa8vf0vlkso00[30] = "        h = gm_mix64(h, {});\012";
const uint8_t gm_sadog1f5187ms[94] = "for (uintptr_t i = 0; i < (uintptr_t)({}); i++) if ({}({} + i * {}, {})) {{ {} = 1; break; }}";
const uint8_t gm_saempncqkf91r[3] = "};";
const uint8_t gm_saf1c3tfhshf0[10] = "{} pieces";
const uint8_t gm_sape9vso1m5fa[2] = "l";
const uint8_t gm_saqbkgqthogiq[30] = "more than one directory given";
const uint8_t gm_saqhctf8022h0[33] = "`, but the `with` has the type `";
const uint8_t gm_sasdd0129vf8p[20] = "duplicate variant `";
const uint8_t gm_sashkcsm7eqk7[11] = "gm_wb{}_{}";
const uint8_t gm_sb33sthvnbnb9[23] = "static const uint32_t ";
const uint8_t gm_sb423ab672pgh[8] = "--prune";
const uint8_t gm_sb6brpqpm5qjh[21] = "dependency cycle: {}";
const uint8_t gm_sb7hastj3mkf0[36] = "; only space and tab are whitespace";
const uint8_t gm_sb86a9k7pfmnh[22] = "+HEAD:refs/gamma/head";
const uint8_t gm_sb8829ojmkbv6[6] = "s390x";
const uint8_t gm_sbjsogn1fh76q[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000 \001\000\000<\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_sbmlclkb2d9o9[74] = "it is private to its module: mark it `pub` if other modules are to use it";
const uint8_t gm_sboedjgihdapp[81] = "` is an iterator, not a value: call it as the source of a `for` loop (`for x in ";
const uint8_t gm_sbogo0q916fbh[19] = "    uint8_t e = v[";
const uint8_t gm_sbpcdsg6ifd91[5] = "HEAD";
const uint8_t gm_sbqjqj9ajcalm[80] = "gamma: `--coverage` needs the C back end: the interpreter does not count lines\012";
const uint8_t gm_sbvdm2nk31pvh[9] = "GM_ZERO(";
const uint8_t gm_sc0vbtqan26pc[18] = "kind = GM_K_TUPLE";
const uint8_t gm_sc19lloml9nb7[17] = "__builtin_floor(";
const uint8_t gm_sc22qkqa9q462[16] = "gm_popcount({})";
const uint8_t gm_sc2e7c2ag4q0u[12] = "gm_load_ptr";
const uint8_t gm_sc2tqolbo0u3i[32] = ", .env = (void *)&F.h, .hctx = ";
const uint8_t gm_sc49g68j2cekh[7] = "struct";
const uint8_t gm_sc51jgguf848t[70] = "a clause of a `fn` operation can only produce a value; it cannot `{}`";
const uint8_t gm_sc8obklajb0e7[40] = "]))(pn->env, pn->hctx, pn, a);\012    }\012}\012";
const uint8_t gm_scaaj480d4i26[9] = "gm_fdone";
const uint8_t gm_scaduriptfv6s[34] = "complex constants are not ordered";
const uint8_t gm_scd25kom6sdt8[34] = "this `break` gives the `with` a `";
const uint8_t gm_scd34uli7vuuj[20] = "pat-variant {} : {}";
const uint8_t gm_scf7jg05jni41[5] = "{:e}";
const uint8_t gm_schd55rs2s02f[11] = "\000\001\002\003\004\005\006\007\010\011";
const uint8_t gm_scj9op1emegq8[13] = "without_call";
const uint8_t gm_sckv4ch46j06c[36] = "cannot assign to an immutable place";
const uint8_t gm_scmbb8a9enevr[16] = "a foreign block";
const uint8_t gm_scmc155kidjub[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\371\002\000\000I\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_sd2s2op6i577j[36] = "compile-time arguments of this type";
const uint8_t gm_sd48v0fh5kl37[24] = "to begin the match arms";
const uint8_t gm_sd6vqnf9glo8m[51] = "`: gamma.toml defines no `[profile.<name>]` tables";
const uint8_t gm_sdavnmflj0bfl[4] = "+%=";
const uint8_t gm_sdccotu1l20mb[7] = "%{} = ";
const uint8_t gm_sdkah956ljhi9[19] = "` cannot be called";
const uint8_t gm_sdmtl8vq6k6qg[13] = "pat-const : ";
const uint8_t gm_sdpf7uf4ias00[24] = "gm_bt B = { gm_bt_top, ";
const uint8_t gm_sdpkm2aceiv8l[47] = "`{}` needs exactly {} elements, but 1 is given";
const uint8_t gm_sdsg5rjte31pl[15] = "  at {}:{}:{}\012";
const uint8_t gm_sdtnu56jsuhl2[8] = " bytes)";
const uint8_t gm_se20eu14i6umf[9] = "uint{}_t";
const uint8_t gm_se287i5v97ugv[8] = "**/*.gm";
const uint8_t gm_sead5oitv3opu[39] = "union __attribute__((packed)) {{{} }};";
const uint8_t gm_sed3elknidrs3[11] = "futex_wake";
const uint8_t gm_sedrsudco1u16[10] = "slice-ptr";
const uint8_t gm_see48nut40kq2[13] = "finish_const";
const uint8_t gm_seg00g6ievv09[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000d\002\000\000\015\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_seggohi50ng09[4] = "Int";
const uint8_t gm_sem5pak34704h[35] = "this range pattern matches nothing";
const uint8_t gm_ser8ip7vtbv6c[9] = "errdefer";
const uint8_t gm_ses7tdrdt8a6u[6] = "Yield";
const uint8_t gm_ses9h0rg965i4[54] = "this `continue` starts the next iteration of the loop";
const uint8_t gm_set8ntrenb1qd[18] = "<tr><td><a href=\"";
const uint8_t gm_sevnjih7nf5ja[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\026\001\000\000H\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000";
const uint8_t gm_sf022jqpe0964[11] = "lift_temps";
const uint8_t gm_sf9difklo6omg[17] = "untyped-float {}";
const uint8_t gm_sfbjh3dq66kkg[17] = "net_set_timeouts";
const uint8_t gm_sfctoi611a7ft[64] = "this continuation needs a value of type `{}`: `resume k(value)`";
const uint8_t gm_sfjs7cccbf3qf[56] = "a thread started from an invalid function pointer {:#x}";
const uint8_t gm_sfm11uv2bn7us[29] = "uint32_t j{} = {}->arg[k{}];";
const uint8_t gm_sfm8ps2e26dh1[3] = "];";
const uint8_t gm_sfqk3rro4g7t0[2] = "\077";
const uint8_t gm_sfrrqniqpcb8o[13] = "rotate_right";
const uint8_t gm_sg4edpe0eroav[8] = "aarch64";
const uint8_t gm_sg4nlara1m052[14] = "`: the path `";
const uint8_t gm_sg4pgef1hupjq[45] = "the builtin function `{}` can only be called";
const uint8_t gm_sg50kn7lo9hgn[86] = "the elements of a tuple iterated by `for` must have one type, but found `{}` and `{}`";
const uint8_t gm_sg9gm9epbpvie[22] = "GIT_TERMINAL_PROMPT=0";
const uint8_t gm_sg9l06oo908p3[45] = "invalid {} name `_`: it is not a usable name";
const uint8_t gm_sgb522dnrltds[37] = "`{}` takes {} positional argument(s)";
const uint8_t gm_sgboe8ukotqh2[51] = "targets a {}-endian machine, but `{}` is {}-endian";
const uint8_t gm_sge745dj3cnrb[73] = "internal error: pointer arithmetic on a value that is not a many-pointer";
const uint8_t gm_sggg4cm5q63i6[5] = "json";
const uint8_t gm_sgj7bt7nru3s1[27] = "; a pointer is not a slice";
const uint8_t gm_sgs65cm0mtgm4[14] = "--proto-redir";
const uint8_t gm_sgt05f0dtajpv[18] = "GM_CONV({}, gm_r)";
const uint8_t gm_sh1v4dof73kmv[7] = "switch";
const uint8_t gm_sh40na9qbga3l[2] = "{";
const uint8_t gm_sh7grf0rgudkg[49] = "compile-time evaluation exceeded its step budget";
const uint8_t gm_sh89s9kbi478a[10] = "copy_expr";
const uint8_t gm_shjbutqe8bgfp[49] = "if ({}) gm_trap(\"integer overflow in `{}`\", {});";
const uint8_t gm_shn6qvu5196go[10] = "effect {}";
const uint8_t gm_shnco6rh33tl5[9] = "gm_procs";
const uint8_t gm_shohh3180gu26[7] = "BitNot";
const uint8_t gm_shol80fqem8c6[48] = "` is not a C-compatible parameter type (\302\24719.1)";
const uint8_t gm_shpjiqltl04un[33] = "a lane index is a `usize`, not `";
const uint8_t gm_shrmh6t9a5b3g[15] = "gm_wasi_get_sp";
const uint8_t gm_shsno1f4se8ej[15] = "write_deferred";
const uint8_t gm_shu2irk2mdah6[14] = "--diag-format";
const uint8_t gm_si1m7q6ia0hto[7] = "cancel";
const uint8_t gm_si34mhnsv5dtr[8] = "newline";
const uint8_t gm_si3r4kcu7q6os[8] = "; else ";
const uint8_t gm_si5ao9la9pflk[19] = "(!{} && vs{}[j{}])";
const uint8_t gm_si702r0b37nml[6] = "fixes";
const uint8_t gm_sidairr6d8hcq[45] = "`copy_from` is missing from the core library";
const uint8_t gm_sigmhs8muccg1[28] = "cannot be checked against `";
const uint8_t gm_siisa3jb2ksa3[4] = "## ";
const uint8_t gm_sijc9cvc14bto[9] = "aligned(";
const uint8_t gm_simmbi8o1lo4k[46] = " is not a directory (`gamma new` creates one)";
const uint8_t gm_sir2n5292lks8[8] = "{} v{};";
const uint8_t gm_sirn08c749sdd[28] = "    { void *p = (uint8_t *)";
const uint8_t gm_sirqj5pqb9rph[58] = "the variable `{}` is never assigned: declare it with `{}`";
const uint8_t gm_sivdpq4bdf3h5[17] = " (a literal at `";
const uint8_t gm_sj1k4t2de5144[6] = "align";
const uint8_t gm_sj64h79nh4kvi[57] = "[package]\012name = \"{}\"\012version = \"0.1.0\"\012\012[dependencies]\012";
const uint8_t gm_sj66n4fl4pp5d[18] = "the column `a.{}`";
const uint8_t gm_sjcsd1e8rp9jn[43] = "a bounds check against a length of type {}";
const uint8_t gm_sjdenof9qo6nf[12] = "untyped {}\012";
const uint8_t gm_sjgpba6e9i17v[20] = "gm_wasi_fiber_entry";
const uint8_t gm_sjid6lq0hib0h[3] = "gw";
const uint8_t gm_sjjavpa3r1ahk[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000x\001\000\0001\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_sjlci8b0qvbpc[3] = "F.";
const uint8_t gm_sjlnsaoa4hibc[8] = "{} @ {}";
const uint8_t gm_sjn4odi0ubper[4] = "-O1";
const uint8_t gm_sjoaauk0ndamd[16] = "Asm { outputs: ";
const uint8_t gm_sjs17a3d2ohel[35] = "GM_STACK_CHECK(&F, GM_TEMP_BYTES);";
const uint8_t gm_sk0nmb4pv74eb[96] = "`, found a type; a type parameter that the arguments determine is written by name, if at all: `";
const uint8_t gm_sk16sqfpds4ga[18] = " is written twice";
const uint8_t gm_sk2b59ovo43qg[6] = "Trunc";
const uint8_t gm_sk96ie971jidi[4] = ":;\012";
const uint8_t gm_sk9jo4s16ifdk[9] = "->hctx, ";
const uint8_t gm_skb9m8a8jekvg[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000}\000\000\000\035\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_skd5c72k4pcr6[23] = "` has an unknown key `";
const uint8_t gm_skfff1ladr223[23] = "`{}` has lanes 0 to {}";
const uint8_t gm_skiv4voi8bqij[20] = " {\012    gm_frame h;\012";
const uint8_t gm_skmt2kt2p7v0u[3] = "\015\012";
const uint8_t gm_skn25h7mjcuvp[84] = "(\342\200\246)`; to pass its address, give it a function pointer type first: `p: ^fn(\342\200\246) = ";
const uint8_t gm_sksrclikobn5n[37] = " (`fill:` must be the only argument)";
const uint8_t gm_skub5dfrnu5tr[18] = "local {} {}: {}{}";
const uint8_t gm_sl0eq2rejro5l[18] = "the element for `";
const uint8_t gm_sl1f0va0l61bh[23] = "coverage (lines run):\012";
const uint8_t gm_sl1f8ibfoc1i1[13] = "` clause in ";
const uint8_t gm_sl83jfd1fgvi2[7] = "--mode";
const uint8_t gm_slbs27elv52b5[30] = "    gm_set_args(argc, argv);\012";
const uint8_t gm_slgjpcgk90nuf[5] = "prog";
const uint8_t gm_slhuk1lrfcjqc[32] = "the variable `{}` is never used";
const uint8_t gm_sliea8310b9mm[11] = "gm_env_var";
const uint8_t gm_slkqcsgfdkn5k[41] = "\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000\350\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_sll95m1eva5e1[24] = "__repl_input :: fn() {\012";
const uint8_t gm_slllafvm0pbeb[6] = "build";
const uint8_t gm_slntkh69kcfmm[8] = "\\{:03o}";
const uint8_t gm_slpp558cs8j3v[36] = "an alignment must be a power of two";
const uint8_t gm_slsi5c1p764pd[8] = "gm_node";
const uint8_t gm_slvu7caka1qb7[10] = " argument";
const uint8_t gm_sm3vri4hmq0fl[13] = "    {} l{};\012";
const uint8_t gm_sm6jfloe4be09[27] = "`{} --asyncify` failed:\012{}";
const uint8_t gm_sm884rqp4fp7u[68] = "memory allocated at {} is freed at {} through a different allocator";
const uint8_t gm_smadkict0htlf[38] = "expectation failed: the values differ";
const uint8_t gm_smiidqh71uo72[10] = "__APPLE__";
const uint8_t gm_smilh4u3um90d[13] = "gm_stat_path";
const uint8_t gm_smk40q3nf1t3o[32] = "__builtin_convertvector({}, {})";
const uint8_t gm_smkkcjkpmt4r9[11] = "hoist_args";
const uint8_t gm_smlfucs8bng1c[38] = "/main\012/main.exe\012/{}\012/{}.exe\012/.gamma/\012";
const uint8_t gm_smn7jqhcc3v5b[10] = "ends_with";
const uint8_t gm_smuiobhjc3kll[30] = "a fiber stack needs at least ";
const uint8_t gm_sn0ocj5vv0ci3[114] = "gamma.lock does not satisfy dependency `{}` ({}), and --locked was given; run `gamma fetch` and commit gamma.lock";
const uint8_t gm_sn0ostovrrp1d[15] = "stable_sort_by";
const uint8_t gm_sn5naisar83li[16] = "codeActionKinds";
const uint8_t gm_snak43jlv01k6[89] = "the archive of `{}` ({}) is not in the cache, and --offline was given; run `gamma fetch`";
const uint8_t gm_snb1pvju20aqn[7] = "with @";
const uint8_t gm_snbrjpgiqf592[6] = "&r->a";
const uint8_t gm_snesmv2gbtsbb[14] = "gm_cstr_array";
const uint8_t gm_snetlri6r5cbl[54] = "compile via C (the default) or run in the interpreter";
const uint8_t gm_snhb827gevj6u[26] = "compiler/check/codegen.gm";
const uint8_t gm_snjonc4tmu56e[21] = "` does not declare `";
const uint8_t gm_snq87gidv4aub[9] = "fn-value";
const uint8_t gm_snqq3153le4es[105] = "`--backend=interp` is supported by `run`, `test` and `bench` only: the other commands use the C back end";
const uint8_t gm_snsbh43f5eo46[5] = " of ";
const uint8_t gm_snsq190kog2e4[60] = "this `break` gives the `with` a `{}`, but its value is `{}`";
const uint8_t gm_snst06bp2i1vp[23] = "    if (e) {} else {}\012";
const uint8_t gm_snvncjka7ph6m[7] = ", but ";
const uint8_t gm_so01usvt6ukbi[59] = "a loop or `with` label that no `break` or `continue` names";
const uint8_t gm_so0h0lkl6bp4l[39] = "__attribute__((always_inline)) inline ";
const uint8_t gm_so28toh6qcgon[11] = "atomic_sub";
const uint8_t gm_so81pe3qfh0hc[15] = "unknown type `";
const uint8_t gm_soa7cbv241ih1[11] = "refs/tags/";
const uint8_t gm_sobb0dbnbb0kc[56] = "`{}` is not an integer, so it cannot be an array length";
const uint8_t gm_sodq6lf7v3uc2[11] = " = coerce ";
const uint8_t gm_sodqgrksuhvj9[10] = "this call";
const uint8_t gm_sogrqv5el59r8[14] = "gm_trap_shift";
const uint8_t gm_sokap7vn66boo[53] = "`.abs` needs signed integer or float elements, not `";
const uint8_t gm_sol7l432pmrlq[8] = "seconds";
const uint8_t gm_solaeibjdukdj[24] = "textDocument/formatting";
const uint8_t gm_solj59tk4ri3k[9] = "Popcount";
const uint8_t gm_som0q53jqvjh3[5] = "    ";
const uint8_t gm_sonq8sf0vvvvr[127] = "expected a value for `{}`, found a type; a type parameter that the arguments determine is written by name, if at all: `{}: {}`";
const uint8_t gm_sooqtob4aubd5[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\334\001\000\000.\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_sopcc4aih66es[18] = ")` or `x.checked(";
const uint8_t gm_sopiqj93d6nu4[7] = "Fields";
const uint8_t gm_soq66vtlbhee2[56] = "` for an imaginary literal; expected j, j16, j32 or j64";
const uint8_t gm_soqlsukui24da[7] = "endian";
const uint8_t gm_sos3b0mjqh560[23] = "operating-system error";
const uint8_t gm_soscrlodichin[67] = "` is already declared in this function; locals may not be shadowed";
const uint8_t gm_sovm9i16n4uut[40] = "if (!{}) gm_trap(\"unwrap of null\", {});";
const uint8_t gm_sp0qjak52qqef[158] = "the copy shares its elements with the original but not its length, so what is added through it is lost; to change the original, take its address: `{} := &{}`";
const uint8_t gm_sp1u9v27v4c4d[24] = "%{} = bits-set {}.{} {}";
const uint8_t gm_sp2du31gjaojf[8] = "{}{} {}";
const uint8_t gm_sp2r5n25uphg2[20] = "gm_overflow_install";
const uint8_t gm_sp2smaof0i4j2[10] = "min-value";
const uint8_t gm_sp7r202318t40[57] = "internal error: a continuation consumed outside a clause";
const uint8_t gm_sp8bmcd3m68n6[7] = "fs_cwd";
const uint8_t gm_spc0i9srvnmq5[6] = "u+rwX";
const uint8_t gm_spdcqn4vu2cam[45] = "expected an integer array length, found a {}";
const uint8_t gm_spgf846a0vg5g[21] = ")` needs `where Int(";
const uint8_t gm_spgfbquq93iqp[43] = "cannot infer the type parameter `{}`; {}{}";
const uint8_t gm_spgshrus7teti[24] = "textDocument/definition";
const uint8_t gm_spiad2tkfoc9s[4] = "asm";
const uint8_t gm_splvrmi68lqi3[111] = "`, which is not inside the package; paths in a dependency's [build] must be relative and stay in its directory";
const uint8_t gm_spn9416h484so[23] = "`, which is not a file";
const uint8_t gm_sppcm1qavsp1k[80] = "a template numbers all its placeholders or none: `{}` and `{0}` cannot be mixed";
const uint8_t gm_spqalnn72r2cn[34] = "`{}` is not a valid C symbol name";
const uint8_t gm_sq355po16rvrc[77] = "` is constructed while the default of one of its fields is evaluated (\302\2473.4)";
const uint8_t gm_sq4sf3euttrc8[5] = "cold";
const uint8_t gm_sq5stv2m4p0h1[37] = "extra comma in array, expected value";
const uint8_t gm_sq6v0u0m2nfv8[23] = ")(((unsigned __int128)";
const uint8_t gm_sq7k2sk4t9rbi[11] = "lex_number";
const uint8_t gm_sqc7sit9ajc3t[3] = "\\u";
const uint8_t gm_sqebj59le8thb[9] = ",\"file\":";
const uint8_t gm_sqes10usq4q5e[4] = "soa";
const uint8_t gm_sqg1k42secsds[29] = "gm_mix64(h, {}GM_LD({}, {}))";
const uint8_t gm_sqh82hv8jvv9p[18] = "  ... more calls\012";
const uint8_t gm_sqhh38dkd5qvg[43] = "; `{}` does not convert to `{}` losslessly";
const uint8_t gm_sqhhm0h7b3asd[6] = "Bswap";
const uint8_t gm_sqje4v30nhs9k[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\356\001\000\000\011\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_sqk8phuvunvgk[56] = "`main` may only perform ambient effects, but performs `";
const uint8_t gm_sqm2dbl8kpvcq[4] = "{}(";
const uint8_t gm_sqofr58g480ri[39] = "copy_from needs slices of equal length";
const uint8_t gm_sqqi4njr3i0t4[46] = "a placeholder's value number is at most 65536";
const uint8_t gm_sqqnbdm19fh9u[17] = "gm_errdefer_runs";
const uint8_t gm_sqrl3kgomorsv[39] = "a compile-time pointer into this value";
const uint8_t gm_sqrv3igf39742[6] = "++) {";
const uint8_t gm_sqse1e7lhga3v[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\027\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sqtrdhljiapb8[17] = "lib/std/io/io.gm";
const uint8_t gm_squ5nrnqo0b0l[6] = "c-str";
const uint8_t gm_squl6da1v7qvs[12] = "gm_hb_index";
const uint8_t gm_sqvb94amh2nu2[66] = "the archive unpacks to more than {} entries, which is not allowed";
const uint8_t gm_sr4qbrk6tlgaa[1] = "";
const uint8_t gm_sr88r273kmptc[21] = "`--{}` needs a value";
const uint8_t gm_sr98hl2rjopi2[8] = "gm_rt.c";
const uint8_t gm_srcl5693p5ru5[17] = "values of type `";
const uint8_t gm_srcpb16u2hru6[15] = "`\\` at the end";
const uint8_t gm_srefpimq1sas7[4] = " { ";
const uint8_t gm_srjkl5qvdutgq[5] = "-gcc";
const uint8_t gm_srl8r23tls393[15] = "gamma.toml: {}";
const uint8_t gm_srmrjv19jgvks[11] = "FnPtrWiden";
const uint8_t gm_srnhu66btobke[9] = "{}{}{}{}";
const uint8_t gm_srod2911mer3j[28] = "textDocument/documentSymbol";
const uint8_t gm_sroh2go4g3kcu[107] = "invalid memory access: {} byte(s) at address {:#x} are out of bounds of the allocation at {:#x} ({} bytes)";
const uint8_t gm_srqmunh3ba86d[20] = "\" is declared twice";
const uint8_t gm_srt8qrm10gni2[7] = "signed";
const uint8_t gm_ss27a20sarlvg[7] = "LaneGt";
const uint8_t gm_ss46ng6b0cghl[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000/\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_ss49sa2bohvbp[28] = "the {} `{}` is `{}`, but {}";
const uint8_t gm_ss75kiikq7m3h[28] = "the evaluation stopped here";
const uint8_t gm_ss7dlauv50bt8[81] = "`{}` refers to itself, so it needs a declared return type: write `-> R` (\302\24711.1)";
const uint8_t gm_ss8vfi8kj31rc[6] = "ULL))";
const uint8_t gm_ss9ss6njjac9i[45] = "(gm_spec){{{}, {}, {}, {}, {}, {}, {}u, {}}}";
const uint8_t gm_ssa1hve63tj2e[21] = "cannot complement {}";
const uint8_t gm_ssbkvpakdcg1k[7] = ") - 1)";
const uint8_t gm_ssevq7ii4bscf[10] = "__linux__";
const uint8_t gm_ssfg7m66qbmvk[65] = "cannot infer the type of `null` here; annotate the optional type";
const uint8_t gm_ssh7fkfdqsqve[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000}\000\000\000\"\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_ssipp3o8q1jun[11] = "TN:\012SF:{}\012";
const uint8_t gm_ssjhh9bdn23jf[17] = "` is given twice";
const uint8_t gm_sskduiq2hlj37[3] = "op";
const uint8_t gm_sskv7tavvjk2l[23] = "missing argument: the ";
const uint8_t gm_sslqgc2m7ch00[27] = " out of bounds for length ";
const uint8_t gm_ssnts74n1eeq5[40] = "` from a function whose error type is `";
const uint8_t gm_ssounvcga4tq8[15] = "gm_cancel_cont";
const uint8_t gm_ssv8t6tsqrjjf[97] = "the second-class type `{}` cannot be part of another type; use a function pointer `^{}` (\302\24711.6)";
const uint8_t gm_st2a9sgu5aria[11] = "\012    \342\200\246\012}";
const uint8_t gm_st45ica2g07t9[53] = "an `@asm` operand is a pair: `(constraint, operand)`";
const uint8_t gm_st8h6qvqu7ddb[30] = ") < 0 || (unsigned __int128)(";
const uint8_t gm_sta33ql7gg6dk[46] = "the type of this value was already fixed as `";
const uint8_t gm_stbjrsoqc456r[12] = ", pb + i * ";
const uint8_t gm_stbmo3g75s0lg[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\223\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_stlm46rulfhsg[39] = "core module `{}` does not declare `{}`";
const uint8_t gm_stou3amhn9368[7] = "extern";
const uint8_t gm_stt0il17c6uj9[17] = "`v` and `w` of `";
const uint8_t gm_sttkmg61kqc3k[65] = "`.bits` needs equal sizes, but `{}` has {} bytes and `{}` has {}";
const uint8_t gm_stuj0mv5nl55t[4] = "&{}";
const uint8_t gm_su06jr9k1rhdo[15] = "gm_bitreverse(";
const uint8_t gm_su137hja0ebcd[6] = "label";
const uint8_t gm_su30pqbigkn5m[6] = "octal";
const uint8_t gm_su8f8u3vs6aep[110] = "a deferred statement may not perform `{}`, whose operations can exit or suspend, unless it handles it (\302\2478.8)";
const uint8_t gm_sua99rsdk6d1r[51] = "a compile-time parameter cannot have the type `{}`";
const uint8_t gm_suass5sid4b6e[5] = "init";
const uint8_t gm_sub9opb9g034o[14] = "perform {}.{}";
const uint8_t gm_sufif7dqc1pm3[17] = "cannot negate {}";
const uint8_t gm_suggn3kp9dfmg[9] = "uint16_t";
const uint8_t gm_suiab49hlgv7g[25] = "compiler/parse/parser.gm";
const uint8_t gm_suio2i7004ufh[8] = "TN:\012SF:";
const uint8_t gm_sujn2evt64i1c[5] = "kill";
const uint8_t gm_sujn6vi16u4un[15] = "] != 0) != (b[";
const uint8_t gm_sul4bd43gk4bq[10] = "+[{} *{}]";
const uint8_t gm_suo9n1e1rnjb6[20] = "{} values are given";
const uint8_t gm_suoecsf0i038a[7] = "typeof";
const uint8_t gm_suonmnnb7u0t1[55] = "extra assignment between key-value pairs, expected `,`";
const uint8_t gm_suqko6d4btmqa[10] = "min_value";
const uint8_t gm_suqqp23fdh2ff[6] = "gm_{}";
const uint8_t gm_susgjrlk3c5f0[8] = " gm_r; ";
const uint8_t gm_susjn9psmiv0c[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000R\002\000\000\031\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_sutvcnnben4aj[59] = "attribute `#{}` does not apply to a local binding (\302\24720.2)";
const uint8_t gm_suu5u3potp0p3[7] = "arm64e";
const uint8_t gm_suuv0qpi4v4e4[8] = "c_union";
const uint8_t gm_suv11ob37fisg[18] = "gm_with_extension";
const uint8_t gm_sv150rl18b4qa[4] = "\012}\012";
const uint8_t gm_sv1gci5lo7scg[5] = "lint";
const uint8_t gm_sv3gqbo7rudsj[13] = "` here but `";
const uint8_t gm_sv52eu837ea4c[5] = "Sys(";
const uint8_t gm_sv5vs7371a92i[18] = "/main\012/main.exe\012/";
const uint8_t gm_sv8lqdk5nsv8i[5] = "Sqrt";
const uint8_t gm_svbhcvlahd4n2[3] = "in";
const uint8_t gm_svcmcgb47rki4[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000+\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_svdapq3j66qso[60] = "void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;";
const uint8_t gm_svdl0vjc2ha7u[50] = "`: the fields of a `#bits` struct have no address";
const uint8_t gm_sveol5dvm5sd7[20] = " is not a temporary";
const uint8_t gm_svhcjkdo3os0j[62] = "` is indexed by its enum, so it is sliced only whole: `a[..]`";
const uint8_t gm_svjsvemd189fa[40] = "the module `{}` has no declaration `{}`";
const uint8_t gm_svnpflkpvd513[5] = " == ";
const uint8_t gm_svqapkbdfenmd[54] = "a vector has a power of two lanes, at least 2, not {}";
const uint8_t gm_svrjvsqtthedc[19] = "textDocument/hover";
const uint8_t gm_svvbeos735pa5[84] = "more than {} postfix operations in a row, the limit of this implementation (\302\24723.5)";
uint8_t gm_c01amogfnq1[3] __attribute__((aligned(1)));
uint8_t gm_c163provqtj[15] __attribute__((aligned(1)));
uint8_t gm_c1qv4qjgsc9[7] __attribute__((aligned(1)));
uint8_t gm_c2uvga3a33u[9] __attribute__((aligned(1)));
uint8_t gm_c3512j75mfr[9] __attribute__((aligned(1)));
uint8_t gm_c36pbvga8vj[10] __attribute__((aligned(1)));
uint8_t gm_c37f79jqcno[3] __attribute__((aligned(1)));
uint8_t gm_c4h4g2jdbns[11] __attribute__((aligned(1)));
uint8_t gm_c4j9utibsmh[5] __attribute__((aligned(1)));
uint8_t gm_c56m9jg65kg[3] __attribute__((aligned(1)));
uint8_t gm_c5dv10uirua[16] __attribute__((aligned(8)));
uint8_t gm_c5p3be4v7rl[12] __attribute__((aligned(1)));
uint8_t gm_c644mb9o5bq[16] __attribute__((aligned(8)));
uint8_t gm_c6bqo8ii8vq[7] __attribute__((aligned(1)));
uint8_t gm_c6cn111uula[6] __attribute__((aligned(1)));
uint8_t gm_c71qftp734m[17] __attribute__((aligned(1)));
uint8_t gm_c7ri5vg1pkt[11] __attribute__((aligned(1)));
uint8_t gm_c83hsvhq98b[8] __attribute__((aligned(8)));
uint8_t gm_c9eimjm1h83[6] __attribute__((aligned(1)));
uint8_t gm_c9h85it1mgi[5] __attribute__((aligned(1)));
uint8_t gm_cahcs2deqne[14] __attribute__((aligned(1)));
uint8_t gm_ccl8cvpv516[12] __attribute__((aligned(1)));
uint8_t gm_ccoccdcv92t[13] __attribute__((aligned(1)));
uint8_t gm_ccqi23qab2q[13] __attribute__((aligned(1)));
uint8_t gm_cdhuk7ib5ef[272] __attribute__((aligned(8)));
uint8_t gm_cdt6le1o3ns[23] __attribute__((aligned(1)));
uint8_t gm_cetnk59fqdb[4] __attribute__((aligned(1)));
uint8_t gm_cf39qh9kr14[12] __attribute__((aligned(1)));
uint8_t gm_cf61b9g709r[1] __attribute__((aligned(1)));
uint8_t gm_cg5b54h69cm[3] __attribute__((aligned(1)));
uint8_t gm_cg68ocegidi[44] __attribute__((aligned(1)));
uint8_t gm_cgofrj6eqlu[4] __attribute__((aligned(4)));
uint8_t gm_cjcj297i683[1] __attribute__((aligned(1)));
uint8_t gm_cjia9g10m37[41] __attribute__((aligned(1)));
uint8_t gm_cjr4mp0de0q[13] __attribute__((aligned(1)));
uint8_t gm_cka88d5t8lu_2[16] __attribute__((aligned(8)));
uint8_t gm_ckdcil0k80m[5] __attribute__((aligned(1)));
uint8_t gm_ckifm27qmcp[8] __attribute__((aligned(1)));
uint8_t gm_ckji37gt5og[9] __attribute__((aligned(1)));
uint8_t gm_cl4mi9no7ck[102] __attribute__((aligned(1)));
uint8_t gm_cl9mf80irg3[3] __attribute__((aligned(1)));
uint8_t gm_clslitrb510[16] __attribute__((aligned(8)));
uint8_t gm_cm18055k83v[13] __attribute__((aligned(1)));
uint8_t gm_cnl0lngt3i5[10] __attribute__((aligned(1)));
uint8_t gm_cpkiil8fc4e[6] __attribute__((aligned(1)));
uint8_t gm_cqcbpnidn0k[6] __attribute__((aligned(1)));
uint8_t gm_cqqto88k0f1[7] __attribute__((aligned(1)));
uint8_t gm_cri7ilp8buh[8] __attribute__((aligned(1)));
uint8_t gm_crui1aejv1g[11] __attribute__((aligned(1)));
uint8_t gm_cspj8kugbce[11] __attribute__((aligned(1)));
uint8_t gm_ctac819ec20[25] __attribute__((aligned(1)));
uint8_t gm_ctk0j7slrnl[10] __attribute__((aligned(1)));
uint8_t gm_cucf5kd1i0t[4] __attribute__((aligned(1)));
uint8_t gm_cuqbj62gqhf[16] __attribute__((aligned(8)));
uint8_t gm_cvjtf68avci[12] __attribute__((aligned(1)));
uint8_t gm_k22qkkqdt5e[40] __attribute__((aligned(8)));
uint8_t gm_k2ok1rai48t[40] __attribute__((aligned(8)));
uint8_t gm_k3mmr0p0mh8[40] __attribute__((aligned(8)));
uint8_t gm_k61ski6fvbu[40] __attribute__((aligned(8)));
uint8_t gm_k647t05rm86[40] __attribute__((aligned(8)));
uint8_t gm_k68ol137ncd[40] __attribute__((aligned(8)));
uint8_t gm_k7tijt756kp[40] __attribute__((aligned(8)));
uint8_t gm_k8gk4puesad[40] __attribute__((aligned(8)));
uint8_t gm_kba69021uoh[40] __attribute__((aligned(8)));
uint8_t gm_ke30i57o09o[40] __attribute__((aligned(8)));
uint8_t gm_kec2ujc0693[40] __attribute__((aligned(8)));
uint8_t gm_kei7njaa9je[40] __attribute__((aligned(8)));
uint8_t gm_kg2a0b5p4sm[40] __attribute__((aligned(8)));
uint8_t gm_khorl1vfvs4[40] __attribute__((aligned(8)));
uint8_t gm_kklngajk7ov[40] __attribute__((aligned(8)));
uint8_t gm_klhvim4pn5v[40] __attribute__((aligned(8)));
uint8_t gm_koalr8ndd0a[40] __attribute__((aligned(8)));
uint8_t gm_koedfqu36hd[40] __attribute__((aligned(8)));
uint8_t gm_kr3abq56gec[40] __attribute__((aligned(8)));
uint8_t gm_krsnqpdcjf4[40] __attribute__((aligned(8)));
uint8_t gm_krt7450irj2[40] __attribute__((aligned(8)));
uint8_t gm_kssv18460i9[40] __attribute__((aligned(8)));
uint8_t gm_ku7k9fgdnj6[40] __attribute__((aligned(8)));
void *const gm_hdu50j4i95ke[] = {(void *)gm_dtu50j4i95ke_0};
void *const gm_hl5iavnkj4ht[] = {(void *)gm_f_for_body_3400t3p};
void *const gm_hl9hta9cqrqu[] = {(void *)gm_f_for_body_vetgq3s};
void *const gm_hlak2jvvbqg4[] = {(void *)gm_f_for_body_0h3rgr6};
void *const gm_hlhqfdrl9vfc[] = {(void *)gm_f_for_body_6l4llq8};
void *const gm_hlkic73ff07n[] = {(void *)gm_f_for_body_4g8ig10};
void *const gm_hlob66lsuf8h[] = {(void *)gm_f_for_body_hrv26t6};
void *const gm_hlu84m640fm9[] = {(void *)gm_f_for_body_fe8qj55};

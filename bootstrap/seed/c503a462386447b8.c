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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct gm_csia7oevr2u0fk { _Bool f0; unsigned __int128 f1; } gm_csia7oevr2u0fk;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t a[16]; } gm_t51b2en4qp02s;
typedef struct { uint8_t f0; uint8_t p1[15]; unsigned __int128 f1; } gm_t86h6hhm6d76n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { __int128 f0; } gm_tpl3q6e8s9qo1;
typedef struct { unsigned __int128 f0; } gm_tsn2omssg6cpf;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_csia7oevr2u0fk) == 32 && _Alignof(gm_csia7oevr2u0fk) == 16 && offsetof(gm_csia7oevr2u0fk, f0) == 0 && offsetof(gm_csia7oevr2u0fk, f1) == 16, "layout of `NumVal`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; gm_b16_8 f3; } gm_t2jeg3bg7sfqa;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t51b2en4qp02s) == 16 && _Alignof(gm_t51b2en4qp02s) == 1 && offsetof(gm_t51b2en4qp02s, a) == 0, "layout of `[16]u8`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t86h6hhm6d76n) == 32 && _Alignof(gm_t86h6hhm6d76n) == 16 && offsetof(gm_t86h6hhm6d76n, f0) == 0 && offsetof(gm_t86h6hhm6d76n, f1) == 16, "layout of `NumVal`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_tpl3q6e8s9qo1) == 16 && _Alignof(gm_tpl3q6e8s9qo1) == 16 && offsetof(gm_tpl3q6e8s9qo1, f0) == 0, "layout of `(i128,)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsn2omssg6cpf) == 16 && _Alignof(gm_tsn2omssg6cpf) == 16 && offsetof(gm_tsn2omssg6cpf, f0) == 0, "layout of `(u128,)`");
typedef struct { gm_t86h6hhm6d76n f0; uint8_t f1; uint8_t pe[15]; } gm_tsp9ul70rnkg6;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_t2jeg3bg7sfqa) == 40 && _Alignof(gm_t2jeg3bg7sfqa) == 8 && offsetof(gm_t2jeg3bg7sfqa, f0) == 0 && offsetof(gm_t2jeg3bg7sfqa, f1) == 8 && offsetof(gm_t2jeg3bg7sfqa, f2) == 16 && offsetof(gm_t2jeg3bg7sfqa, f3) == 24, "layout of `Seq`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
typedef struct { gm_t8588ma2cr9d7 f0; } gm_t6bs2bvvcgdnj;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, bits: u64)`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_t2jeg3bg7sfqa v1; } f1; } gm_tmc91gv7n9c0i;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsp9ul70rnkg6) == 48 && _Alignof(gm_tsp9ul70rnkg6) == 16 && offsetof(gm_tsp9ul70rnkg6, f0) == 0 && offsetof(gm_tsp9ul70rnkg6, f1) == 32, "layout of `?NumVal`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t6bs2bvvcgdnj) == 12 && _Alignof(gm_t6bs2bvvcgdnj) == 4 && offsetof(gm_t6bs2bvvcgdnj, f0) == 0, "layout of `(Builtin,)`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tmc91gv7n9c0i) == 48 && _Alignof(gm_tmc91gv7n9c0i) == 8 && offsetof(gm_tmc91gv7n9c0i, f0) == 0 && offsetof(gm_tmc91gv7n9c0i, f1) == 8, "layout of `Flow!Seq`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_apply_builtin_41dffti {
    gm_frame h;
    gm_tckdobunaulee l32; /* d */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_array_ptr_to_slice_7dg3m1v {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_coerce_bytes_9kf3u24 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_convert_is6an2k {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_46el73v {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8k5eh95 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_985clkf {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9qvncot {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fv5tfj6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h8e7ps1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kj15nqp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l7c15sv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pe0ujmq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ra7pont {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ven7kt5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_hash_bytes_2qnf0cp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_int_26mf5j0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_u64_32bripn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hash_val_nr2e99r {
    gm_frame h;
    gm_t2jeg3bg7sfqa l17; /* s */
    gm_tpautr4br7h3l l31; /* def */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_int_text_vrjnuu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mark_kpppm1j {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_num_bits_onl28vp {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_num_f16_to_f64_h70suv4 {
    gm_frame h;
    double ret;
};
struct gm_fr_num_f64_to_f16_3g33m05 {
    gm_frame h;
    uint16_t ret;
};
struct gm_fr_num_is_nan_b0ma7mo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_num_round_2pna7m3 {
    gm_frame h;
    double ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_opt_inner_q4ga84u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_payload_offset_la0c8ft {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_align_lavuopj {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
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
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_adt_of_8k5vqud(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_apply_builtin_41dffti(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t8588ma2cr9d7 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
__attribute__((aligned(4))) gm_b0_1 gm_f_array_ptr_to_slice_7dg3m1v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_checked_overflow_ma1bm1g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_coerce_bytes_9kf3u24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tmn7n0n9raou5 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_convert_is6an2k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t8588ma2cr9d7 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint16_t gm_f_f16_bits_2us9irg(double a0);
__attribute__((aligned(4))) double gm_f_f16_value_42vkfhl(uint16_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_field_bytes_bhivr4s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3);
__attribute__((aligned(4))) gm_tfbnciphf3261 gm_f_field_layout_h34salb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8k5eh95(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_985clkf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t6bs2bvvcgdnj a0);
inline __attribute__((aligned(4))) void gm_f_format_9qvncot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tpl3q6e8s9qo1 a0);
__attribute__((aligned(4))) void gm_f_format_b3uc2q7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tpl3q6e8s9qo1 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fv5tfj6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hmhrs8a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t6bs2bvvcgdnj a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ig8f8pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_kj15nqp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l7c15sv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_mnuav4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_pe0ujmq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ven7kt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) double gm_f_get_float_grmhlm4(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_hash_bytes_2qnf0cp(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_hash_int_26mf5j0(void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_hash_int_26mf5j0_c(void * a0, gm_csia7oevr2u0fk a1, uint32_t a2);
inline __attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_hash_val_nr2e99r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) int32_t gm_f_int_cmp_kb3sm6u(uint8_t a0, unsigned __int128 a1, unsigned __int128 a2);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_text_vrjnuu2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_invalid_tag_1gif354(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_res_085nq6f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint8_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_variant_bn3d74n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint32_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp(const void * a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_ext_348kmbu(uint8_t a0, uint32_t a1, unsigned __int128 a2);
inline __attribute__((aligned(4))) double gm_f_num_f16_to_f64_h70suv4(uint16_t a0);
inline __attribute__((aligned(4))) uint16_t gm_f_num_f64_to_f16_3g33m05(double a0);
__attribute__((aligned(4))) uint8_t gm_f_num_fits_1n7pcsh(const void * a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_num_float_int_ba5kc1t(void *gm_out, double a0, gm_t8588ma2cr9d7 a1);
__attribute__((aligned(4))) double gm_f_num_fmax_0qau2jp(double a0, double a1);
__attribute__((aligned(4))) double gm_f_num_fmin_1k5uqp0(double a0, double a1);
__attribute__((aligned(4))) double gm_f_num_int_to_float_4hrvis6(const void * a0, uint16_t a1);
__attribute__((aligned(4))) uint8_t gm_f_num_integral_qbts4a6(double a0);
inline __attribute__((aligned(4))) uint8_t gm_f_num_is_nan_b0ma7mo(double a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_num_mask_do3naah(uint32_t a0);
inline __attribute__((aligned(4))) double gm_f_num_round_2pna7m3(double a0, uint16_t a1);
__attribute__((aligned(4))) void gm_f_num_sat_66dqcpl(void *gm_out, const void * a0, uint8_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_num_val_i3upeur(void *gm_out, uint8_t a0, unsigned __int128 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_payload_1pbnobb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_opt_present_lb4i2ip(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_pow_7je2uvi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) uint64_t gm_f_ptr_align_lavuopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_put_float_fiad72d(void * a0, gm_csguankko4lmhd a1, double a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) uint8_t gm_f_res_is_err_8jbgndp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_payload_pb2hcmh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tgglga8ro3isc gm_f_res_types_pg7rvck(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_seq_of_ifahtjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_variant_index_s2p6fs4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_vector_builtin_3fn933h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t8588ma2cr9d7 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7);
extern const uint8_t gm_s064htvgs8b59[69];
extern const uint8_t gm_s26c80h2pm6a7[26];
extern const uint8_t gm_s2jl146j9vpnj[20];
extern const uint8_t gm_s3ergh2il1tjf[43];
extern const uint8_t gm_s3gnqv0up712m[61];
extern const uint8_t gm_s4bovq0eevpad[23];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s9h8lp3gkedth[29];
extern const uint8_t gm_s9o2c36nosv54[41];
extern const uint8_t gm_sartclcojin4f[52];
extern const uint8_t gm_sb9352gc5i144[21];
extern const uint8_t gm_sbel6qookv0gl[54];
extern const uint8_t gm_sbjj41cb5lph3[60];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_sc6st5mdtp49u[16];
extern const uint8_t gm_sciu02cqahje3[52];
extern const uint8_t gm_sd4ibvv64si9n[45];
extern const uint8_t gm_sge745dj3cnrb[73];
extern const uint8_t gm_sge7ihldgqi49[20];
extern const uint8_t gm_sh01rb1u2jpfs[19];
extern const uint8_t gm_sl9mgk5ho4kmr[29];
extern const uint8_t gm_slnkfiltk5b43[57];
extern const uint8_t gm_spqf88ftq0gqk[29];
extern const uint8_t gm_sq87o4fbigdsl[17];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcl8pn2pjtmj6k[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sti4tkgslrkgo[26];
extern const uint8_t gm_stk678hojgu6q[24];
extern const uint8_t gm_su88ia0hdam4d[66];
extern const gm_tmpl gm_tpl15v8rvh46nek;
extern const gm_tmpl gm_tpl1lj3ah7qeq42;
extern const gm_tmpl gm_tplav8bdvvt8rem;
extern const gm_tmpl gm_tplcktokqd7n6fo;
extern const gm_tmpl gm_tpljicvo0ni8aba;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplmm8hld32929h;
extern const gm_tmpl gm_tplmsb1klg4n6ap;
extern const gm_tmpl gm_tplt1lu36t604pk;
extern const gm_tmpl gm_tplvl1jqbb33ag2;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq9oqmmiu8qc(a + 2, b + 2)) return false;
    return true;
}
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_15fjgq1 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 16);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint64_t t5 = gm_f_align_of_k6eqqrn((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_of_k6eqqrn F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
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
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fast_23em7sv F;
    void * gm_l0; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 22u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t2) + 88));
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    uint8_t t5 = (uint8_t)((t4 == ((uint8_t)3ULL)));
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)3ULL));
    F.ret = t5;
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
inline __attribute__((aligned(4))) void gm_f_format_8k5eh95(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8k5eh95 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2jl146j9vpnj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpljicvo0ni8aba), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_985clkf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t6bs2bvvcgdnj a0) {
    struct gm_fr_format_985clkf F;
    gm_t6bs2bvvcgdnj gm_l0; /* args */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t6bs2bvvcgdnj t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sq87o4fbigdsl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_hmhrs8a((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplt1lu36t604pk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9qvncot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tpl3q6e8s9qo1 a0) {
    struct gm_fr_format_9qvncot F;
    gm_tpl3q6e8s9qo1 gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tpl3q6e8s9qo1 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_b3uc2q7((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fv5tfj6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fv5tfj6 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl9mgk5ho4kmr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1lj3ah7qeq42), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_h8e7ps1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h8e7ps1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc6st5mdtp49u));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvl1jqbb33ag2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_kj15nqp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_kj15nqp F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sge7ihldgqi49));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmsb1klg4n6ap), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l7c15sv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_l7c15sv F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su88ia0hdam4d));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(65));
    gm_f_format_ig8f8pl((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmm8hld32929h), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_pe0ujmq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_pe0ujmq F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4bovq0eevpad));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplcktokqd7n6fo), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ra7pont(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tsn2omssg6cpf a0) {
    struct gm_fr_format_ra7pont F;
    gm_tsn2omssg6cpf gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tsn2omssg6cpf t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_mnuav4d((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ven7kt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_ven7kt5 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sciu02cqahje3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl15v8rvh46nek), t3);
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
inline __attribute__((aligned(4))) void gm_f_hash_int_26mf5j0(void * a0, const void * a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_int_26mf5j0 F;
    void * gm_l0; /* h */
    gm_t86h6hhm6d76n gm_l1; /* x */
    uint32_t gm_l2; /* bits */
    unsigned __int128 gm_l3; /* low */
    GM_STACK_CHECK_SHALLOW(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    unsigned __int128 t2 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)a1)));
    gm_l3 = t2;
    void * t3 = gm_l0;
    unsigned __int128 t4 = gm_l3;
    uint64_t t5 = (uint64_t)t4;
    gm_f_hash_u64_32bripn(t3, t5);
    uint32_t t6 = gm_l2;
    uint8_t t7 = (t6 > ((uint32_t)64ULL));
    if (t7) {
        void * t8 = gm_l0;
        unsigned __int128 t9 = gm_l3;
        unsigned __int128 n10 = (unsigned __int128)((uint64_t)64ULL);
        if (n10 >= 128) gm_trap_shift((uint64_t)n10, "u128", gm_srcl8pn2pjtmj6k, 427, 33);
        unsigned __int128 t11 = (unsigned __int128)(t9 >> (int)n10);
        uint64_t t12 = (uint64_t)t11;
        gm_f_hash_u64_32bripn(t8, t12);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_u64_32bripn F;
    void * gm_l0; /* h */
    uint64_t gm_l1; /* x */
    unsigned __int128 gm_l2; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)t2));
    uint64_t t4 = gm_l1;
    uint64_t t5 = (uint64_t)(t3 ^ t4);
    unsigned __int128 t6 = (unsigned __int128)t5;
    unsigned __int128 r7; bool o8 = __builtin_mul_overflow(t6, ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 11400714819323198485ULL)), &r7);
    unsigned __int128 t9 = r7;
    gm_l2 = t9;
    void * t10 = gm_l0;
    unsigned __int128 t11 = gm_l2;
    uint64_t t12 = (uint64_t)t11;
    unsigned __int128 t13 = gm_l2;
    unsigned __int128 n14 = (unsigned __int128)((uint64_t)64ULL);
    if (n14 >= 128) gm_trap_shift((uint64_t)n14, "u128", gm_srcl8pn2pjtmj6k, 407, 26);
    unsigned __int128 t15 = (unsigned __int128)(t13 >> (int)n14);
    uint64_t t16 = (uint64_t)t15;
    uint64_t t17 = (uint64_t)(t12 ^ t16);
    GM_ST(uint64_t, ((uint8_t *)t10), t17);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nullable_pointer_7qfmn6o F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint8_t t4 = gm_f_is_nullable_pointer_nkie06s(t2, GM_CONV(gm_csguankko4lmhd, t3));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_num_bits_onl28vp(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_bits_onl28vp F;
    gm_t86h6hhm6d76n gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    unsigned __int128 gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    if (t2) {
        unsigned __int128 t3 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
        unsigned __int128 r4; bool o5 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)), t3, &r4);
        unsigned __int128 t6 = r4;
        gm_l1 = t6;
    } else {
        unsigned __int128 t7 = GM_LD(unsigned __int128, (((uint8_t *)a0) + 16));
        gm_l1 = t7;
    }
    unsigned __int128 t8 = gm_l1;
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_num_f16_to_f64_h70suv4(uint16_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_f16_to_f64_h70suv4 F;
    uint16_t gm_l0; /* h */
    GM_STACK_CHECK_SHALLOW(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint16_t t2 = gm_l0;
    double t3 = gm_f_f16_value_42vkfhl(t2);
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint16_t gm_f_num_f64_to_f16_3g33m05(double a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_f64_to_f16_3g33m05 F;
    double gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 22u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    double t2 = gm_l0;
    uint16_t t3 = gm_f_f16_bits_2us9irg(t2);
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_num_is_nan_b0ma7mo(double a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_is_nan_b0ma7mo F;
    double gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    double t2 = gm_l0;
    double t3 = gm_l0;
    uint8_t t4 = (!(t2 == t3));
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_num_round_2pna7m3(double a0, uint16_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_num_round_2pna7m3 F;
    double gm_l0; /* x */
    uint16_t gm_l1; /* width */
    GM_STACK_CHECK_SHALLOW(&F, 78u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint16_t t2 = gm_l1;
    uint8_t t3 = ((t2 == ((uint16_t)32ULL)));
    if (t3) {
        double t4 = gm_l0;
        float t5 = (float)((double)t4);
        double t6 = (double)t5;
        F.ret = t6;
        goto gm_fret;
    }
    uint16_t t7 = gm_l1;
    uint8_t t8 = ((t7 == ((uint16_t)16ULL)));
    if (t8) {
        double t9 = gm_l0;
        uint16_t t10 = gm_f_num_f64_to_f16_3g33m05(t9);
        double t11 = gm_f_num_f16_to_f64_h70suv4(t10);
        F.ret = t11;
        goto gm_fret;
    }
    double t12 = gm_l0;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_qpt7c30 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 16);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_of_e6hfj1r((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 16));
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
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_opt_inner_q4ga84u F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* inner */
    GM_STACK_CHECK_SHALLOW(&F, 106u);
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
            gm_f_kind_htv6093((void *)&gm_l3, t4, GM_CONV(gm_csguankko4lmhd, t5));
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)13ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 8));
            gm_l2 = t8;
            gm_t88rcucg1b7uk t9 = gm_l2;
            F.ret = t9;
            goto gm_fret;
        }
gm_lb2:;
        gm_t88rcucg1b7uk t10 = gm_l1;
        F.ret = t10;
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
gm_fprop:
    return GM_CONV(gm_csguankko4lmhd, F.ret);
}
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_payload_offset_la0c8ft F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* offs */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l3;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t4;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t6 = (t5 > ((uint64_t)1ULL));
    if (t6) {
        void * t7 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + (uintptr_t)((uint64_t)1ULL) * 8));
        gm_l3 = t9;
    } else {
        gm_l3 = ((uint64_t)0ULL);
    }
    uint64_t t10 = gm_l3;
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
__attribute__((aligned(4))) void gm_f_apply_builtin_41dffti(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t8588ma2cr9d7 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6, gm_b16_8 a7) {
    struct gm_fr_apply_builtin_41dffti F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t8588ma2cr9d7 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* res_ty */
    gm_t88rcucg1b7uk gm_l4; /* t0 */
    gm_t88rcucg1b7uk gm_l5; /* t1 */
    gm_b16_8 gm_l6; /* v */
    gm_b16_8 gm_l7; /* dst */
    uint64_t gm_l8; /* n */
    uint64_t gm_l9; /* addr */
    uint64_t gm_l10; /* al */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    __int128 gm_l12; /* stride */
    __int128 gm_l13; /* p */
    uint64_t gm_l14; /* pbits */
    uint64_t gm_l15; /* mask */
    int64_t gm_l16; /* d */
    int64_t gm_l17; /* q */
    gm_ttovhfee8mf28 gm_l18; /* info */
    unsigned __int128 gm_l19; /* raw */
    __int128 gm_l20; /* n */
    __int128 gm_l21; /* off */
    __int128 gm_l22; /* r */
    gm_tcmkompbl09il gm_l23; /* u */
    uint64_t gm_l24; /* n */
    gm_t88rcucg1b7uk gm_l25; /* u */
    gm_tcmkompbl09il gm_l26; /* def */
    gm_ttovhfee8mf28 gm_l27; /* info */
    gm_t86h6hhm6d76n gm_l28; /* x */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* i */
    unsigned __int128 gm_l33; /* tb */
    unsigned __int128 gm_l34; /* mag */
    gm_t2lqu8jsj511d gm_l35; /* mk */
    gm_b16_8 gm_l36; /* vb */
    double gm_l37; /* x */
    double gm_l38; /* y */
    double gm_l39; /* r */
    gm_ttovhfee8mf28 gm_l40; /* info */
    unsigned __int128 gm_l41; /* x */
    unsigned __int128 gm_l42; /* y */
    uint64_t gm_l43; /* pick */
    unsigned __int128 gm_l44; /* hi */
    gm_ttovhfee8mf28 gm_l45; /* info */
    unsigned __int128 gm_l46; /* x */
    unsigned __int128 gm_l47; /* r */
    gm_ttovhfee8mf28 gm_l48; /* info */
    unsigned __int128 gm_l49; /* x */
    unsigned __int128 gm_l50; /* y */
    unsigned __int128 gm_l51; /* d */
    gm_ttovhfee8mf28 gm_l52; /* r */
    gm_t9qkh0rsooc8d gm_l53; /* fl */
    double gm_l54; /* x */
    uint64_t gm_l55; /* raw */
    uint64_t gm_l56; /* exp */
    uint16_t gm_l57; /* w */
    double gm_l58; /* x */
    double gm_l59; /* y */
    double gm_l60; /* z */
    gm_ttovhfee8mf28 gm_l61; /* info */
    gm_ttovhfee8mf28 gm_l62; /* ainfo */
    uint32_t gm_l63; /* n */
    unsigned __int128 gm_l64; /* x */
    unsigned __int128 gm_l65; /* araw */
    uint32_t gm_l66; /* k */
    unsigned __int128 gm_l67; /* r */
    gm_ttovhfee8mf28 gm_l68; /* info */
    uint32_t gm_l69; /* n */
    unsigned __int128 gm_l70; /* x */
    unsigned __int128 gm_l71; /* r */
    uint32_t gm_l72; /* <tmp> */
    uint32_t gm_l73; /* <tmp> */
    uint32_t gm_l74; /* i */
    uint32_t gm_l75; /* <tmp> */
    uint32_t gm_l76; /* <tmp> */
    uint32_t gm_l77; /* i */
    gm_ttovhfee8mf28 gm_l78; /* info */
    gm_t86h6hhm6d76n gm_l79; /* seed */
    uint64_t gm_l80; /* h */
    GM_STACK_CHECK(&F, 9932u);
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
    gm_tabb710d9mq2e gm_l81;
    gm_t53at80pfdus3 gm_l82;
    gm_tg5lql6itufk7 gm_l83;
    gm_t57euh612ligr gm_l84;
    gm_t57euh612ligr gm_l85;
    gm_tg5lql6itufk7 gm_l86;
    gm_t3l8idb2m6c1r gm_l87;
    uint8_t gm_l88;
    gm_t53at80pfdus3 gm_l89;
    gm_tg5lql6itufk7 gm_l90;
    gm_tabb710d9mq2e gm_l91;
    uint64_t gm_l92;
    uint64_t gm_l93;
    gm_t53at80pfdus3 gm_l94;
    gm_tg5lql6itufk7 gm_l95;
    uint8_t gm_l96;
    gm_tcfa6l7reoosn gm_l97;
    gm_tg5lql6itufk7 gm_l98;
    gm_tg5lql6itufk7 gm_l99;
    uint8_t gm_l100;
    gm_t53at80pfdus3 gm_l101;
    uint8_t gm_l102;
    uint8_t gm_l103;
    uint8_t gm_l104;
    uint8_t gm_l105;
    gm_t53at80pfdus3 gm_l106;
    gm_tg5lql6itufk7 gm_l107;
    gm_tg5lql6itufk7 gm_l108;
    uint8_t gm_l109;
    gm_tg5lql6itufk7 gm_l110;
    unsigned __int128 gm_l111;
    gm_tabb710d9mq2e gm_l112;
    gm_tg5lql6itufk7 gm_l113;
    gm_tcfa6l7reoosn gm_l114;
    gm_tabb710d9mq2e gm_l115;
    {
        {
            {
                {
                    {
                        {
                            uint8_t t135 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t136 = (uint8_t)(t135 == ((uint8_t)0ULL));
                            if (!t136) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t138 = (uint8_t)(t137 == ((uint8_t)1ULL));
                            if (!t138) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t140 = (uint8_t)(t139 == ((uint8_t)2ULL));
                            if (!t140) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        {
                            uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t142 = (uint8_t)(t141 == ((uint8_t)3ULL));
                            if (!t142) {
                                goto gm_lb7;
                            }
                            goto gm_lb3;
                        }
gm_lb7:;
                        {
                            uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t144 = (uint8_t)(t143 == ((uint8_t)4ULL));
                            if (!t144) {
                                goto gm_lb8;
                            }
                            goto gm_lb3;
                        }
gm_lb8:;
                        {
                            uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t146 = (uint8_t)(t145 == ((uint8_t)5ULL));
                            if (!t146) {
                                goto gm_lb9;
                            }
                            goto gm_lb3;
                        }
gm_lb9:;
                        {
                            uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t148 = (uint8_t)(t147 == ((uint8_t)6ULL));
                            if (!t148) {
                                goto gm_lb10;
                            }
                            goto gm_lb3;
                        }
gm_lb10:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    void * t149 = gm_l0;
                    gm_tl874t2atnlh3 t150 = gm_l1;
                    gm_t8588ma2cr9d7 t151 = gm_l2;
                    gm_t88rcucg1b7uk t152 = gm_l4;
                    gm_t88rcucg1b7uk t153 = gm_l3;
                    void * t154 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t155)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t155, gm_srcl8pn2pjtmj6k, 192, 107);
                    gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)t154) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                    gm_b16_8 t158 = gm_l7;
                    gm_f_convert_is6an2k((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t149, t150, t151, t152, t153, t157, t158);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb2:;
                {
                    uint8_t t159 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t160 = (uint8_t)(t159 == ((uint8_t)7ULL));
                    if (!t160) {
                        goto gm_lb11;
                    }
                    gm_b16_8 t161 = gm_l7;
                    gm_f_fill_abmf5g5(t161, ((uint8_t)0ULL));
                    uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    void * t163 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t164)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t164, gm_srcl8pn2pjtmj6k, 195, 30);
                    uint64_t t165 = GM_LD(uint64_t, ((((uint8_t *)t163) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                    uint64_t t166 = (t162 < t165 ? t162 : t165);
                    gm_l8 = t166;
                    void * t167 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t169 = gm_l8;
                    uint64_t t170 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t171 = (uintptr_t)t169;
                    if (t170 > t171 || t171 > (uintptr_t)t168) gm_trap_slice(t170, t171, t168, gm_srcl8pn2pjtmj6k, 196, 13);
                    uint64_t t172 = ((uintptr_t)t169) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t173;
                    GM_ST(void *, (uint8_t *)&t173, (void *)(((uint8_t *)t167 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(t172));
                    void * t174 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t175)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t175, gm_srcl8pn2pjtmj6k, 196, 33);
                    void * t176 = GM_LD(void *, (((uint8_t *)t174) + (uintptr_t)((uint64_t)0ULL) * 16));
                    uint64_t t177 = GM_LD(uint64_t, ((((uint8_t *)t174) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                    uint64_t t178 = gm_l8;
                    uint64_t t179 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t180 = (uintptr_t)t178;
                    if (t179 > t180 || t180 > (uintptr_t)t177) gm_trap_slice(t179, t180, t177, gm_srcl8pn2pjtmj6k, 196, 33);
                    uint64_t t181 = ((uintptr_t)t178) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t182;
                    GM_ST(void *, (uint8_t *)&t182, (void *)(((uint8_t *)t176 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t182 + 8, (uintptr_t)(t181));
                    gm_b16_8 t183 = GM_LD(gm_b16_8, ((uint8_t *)&t182));
                    gm_f_copy_from_f7j6p05(t173, t183);
                    goto gm_lb1;
                }
gm_lb11:;
            }
            {
                {
                    uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t185 = (uint8_t)(t184 == ((uint8_t)8ULL));
                    if (!t185) {
                        goto gm_lb12;
                    }
                    void * t186 = gm_l0;
                    void * t187 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t188 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t188)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t188, gm_srcl8pn2pjtmj6k, 199, 30);
                    gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)t187) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)&t189));
                    uint64_t t191 = gm_f_ptr_of_b77b4c9(t186, t190);
                    gm_l9 = t191;
                    void * t192 = gm_l0;
                    gm_t88rcucg1b7uk t193 = gm_l3;
                    uint64_t t194 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t192, t193);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t194;
                    {
                        {
                            uint64_t t195 = gm_l9;
                            uint8_t t196 = (!(t195 == ((uint64_t)0ULL)));
                            if (!t196) {
                                goto gm_lb14;
                            }
                            uint64_t t197 = gm_l10;
                            uint8_t t198 = (t197 > ((uint64_t)1ULL));
                            if (!t198) {
                                goto gm_lb14;
                            }
                            uint64_t t199 = gm_l9;
                            uint64_t t200 = gm_l10;
                            if (t200 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 201, 45);
                            uint64_t t201 = (uint64_t)(t199 % t200);
                            uint8_t t202 = (!(t201 == ((uint64_t)0ULL)));
                            if (!t202) {
                                goto gm_lb14;
                            }
                            void * t203 = gm_l0;
                            gm_t88rcucg1b7uk t204 = gm_l4;
                            uint64_t t205 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t203, t204);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t206 = gm_l10;
                            uint8_t t207 = (t205 < t206);
                            if (!t207) {
                                goto gm_lb14;
                            }
                            void * t208 = gm_l0;
                            uint8_t t209 = gm_f_fast_23em7sv(t208);
                            uint8_t t210 = (uint8_t)!t209;
                            if (!t210) {
                                goto gm_lb14;
                            }
                            {
                                void * t211 = gm_l0;
                                gm_tl874t2atnlh3 t212 = gm_l1;
                                uint64_t t213 = gm_l9;
                                uint64_t t214 = gm_l10;
                                gm_t2lqu8jsj511d t215;
                                GM_ST(uint64_t, ((uint8_t *)&t215 + 0), t213);
                                GM_ST(uint64_t, ((uint8_t *)&t215 + 8), t214);
                                gm_f_format_l7c15sv((void *)&gm_l81, (void *)NULL, cx, NULL, t215);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t216 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                                if (!t216) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l81) + 8, gm_srcl8pn2pjtmj6k, 202, 38);
                                gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l81) + 8));
                                gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&t217));
                                gm_t76123gfet73g t219;
                                gm_f_trap_qh84l9d((void *)&t219, (void *)NULL, cx, NULL, t211, t212, t218);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t219);
                                goto gm_fdone;
                            }
                        }
gm_lb14:;
                    }
gm_lb13:;
                    gm_b16_8 t220 = gm_l7;
                    void * t221 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t222)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t222, gm_srcl8pn2pjtmj6k, 204, 27);
                    gm_b16_8 t223 = GM_LD(gm_b16_8, (((uint8_t *)t221) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)&t223));
                    gm_f_copy_from_f7j6p05(t220, t224);
                    goto gm_lb1;
                }
gm_lb12:;
                {
                    {
                        {
                            uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t226 = (uint8_t)(t225 == ((uint8_t)9ULL));
                            if (!t226) {
                                goto gm_lb17;
                            }
                            goto gm_lb16;
                        }
gm_lb17:;
                        {
                            uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t228 = (uint8_t)(t227 == ((uint8_t)13ULL));
                            if (!t228) {
                                goto gm_lb18;
                            }
                            goto gm_lb16;
                        }
gm_lb18:;
                        {
                            uint8_t t229 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t230 = (uint8_t)(t229 == ((uint8_t)35ULL));
                            if (!t230) {
                                goto gm_lb19;
                            }
                            goto gm_lb16;
                        }
gm_lb19:;
                        goto gm_lb15;
                    }
gm_lb16:;
                    gm_b16_8 t231 = gm_l7;
                    void * t232 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t233 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t233)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t233, gm_srcl8pn2pjtmj6k, 206, 51);
                    gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)t232) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t235 = GM_LD(gm_b16_8, ((uint8_t *)&t234));
                    gm_f_copy_from_f7j6p05(t231, t235);
                    goto gm_lb1;
                }
gm_lb15:;
            }
            {
                {
                    {
                        {
                            uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t237 = (uint8_t)(t236 == ((uint8_t)10ULL));
                            if (!t237) {
                                goto gm_lb22;
                            }
                            goto gm_lb21;
                        }
gm_lb22:;
                        {
                            uint8_t t238 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t239 = (uint8_t)(t238 == ((uint8_t)11ULL));
                            if (!t239) {
                                goto gm_lb23;
                            }
                            goto gm_lb21;
                        }
gm_lb23:;
                        {
                            uint8_t t240 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t241 = (uint8_t)(t240 == ((uint8_t)12ULL));
                            if (!t241) {
                                goto gm_lb24;
                            }
                            goto gm_lb21;
                        }
gm_lb24:;
                        goto gm_lb20;
                    }
gm_lb21:;
                    {
                        {
                            void * t242 = gm_l0;
                            gm_t88rcucg1b7uk t243 = gm_l4;
                            gm_f_kind_htv6093((void *)&gm_l82, t242, GM_CONV(gm_csguankko4lmhd, t243));
                            uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)&gm_l82) + 0));
                            uint8_t t245 = (uint8_t)(t244 == ((uint8_t)9ULL));
                            if (!t245) {
                                goto gm_lb26;
                            }
                            gm_t88rcucg1b7uk t246 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l82) + 8) + 4));
                            gm_l11 = t246;
                            void * t247 = gm_l0;
                            gm_t88rcucg1b7uk t248 = gm_l11;
                            uint64_t t249 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t247, t248);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            __int128 t250 = (__int128)t249;
                            gm_l12 = t250;
                            void * t251 = gm_l0;
                            void * t252 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t253 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t253)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t253, gm_srcl8pn2pjtmj6k, 210, 32);
                            gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)t252) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&t254));
                            uint64_t t256 = gm_f_ptr_of_b77b4c9(t251, t255);
                            __int128 t257 = (__int128)t256;
                            gm_l13 = t257;
                            void * t258 = gm_l0;
                            uint64_t t259 = GM_LD(uint64_t, (((uint8_t *)t258) + 176));
                            uint64_t r260; bool o261 = __builtin_mul_overflow(t259, ((uint64_t)8ULL), &r260);
                            if (o261) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 211, 22);
                            uint64_t t262 = r260;
                            gm_l14 = t262;
                            uint64_t t263 = gm_l14;
                            uint8_t t264 = (t263 >= ((uint64_t)64ULL));
                            if (t264) {
                                uint64_t t265 = (uint64_t)~((uint64_t)0ULL);
                                gm_l15 = t265;
                            } else {
                                uint64_t t266 = gm_l14;
                                unsigned __int128 n267 = (unsigned __int128)t266;
                                if (n267 >= 64) gm_trap_shift((uint64_t)n267, "u64", gm_srcl8pn2pjtmj6k, 212, 60);
                                uint64_t r268 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n267);
                                bool o269 = (uint64_t)(r268 >> (int)n267) != ((uint64_t)1ULL);
                                if (o269) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 212, 60);
                                uint64_t t270 = r268;
                                uint64_t r271; bool o272 = __builtin_sub_overflow(t270, ((uint64_t)1ULL), &r271);
                                if (o272) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 212, 59);
                                uint64_t t273 = r271;
                                gm_l15 = t273;
                            }
                            gm_t8588ma2cr9d7 t274 = gm_l2;
                            uint8_t t275 = GM_LD(uint8_t, (((uint8_t *)&t274) + 0));
                            uint8_t t276 = (uint8_t)((t275 == ((uint8_t)12ULL)));
                            gm_t8588ma2cr9d7 t277;
                            GM_ST(uint8_t, (uint8_t *)&t277 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t277 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t277 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t277 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t277 + 0), ((uint8_t)12ULL));
                            if (t276) {
                                __int128 t278 = gm_l13;
                                void * t279 = gm_l0;
                                void * t280 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t281 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t281)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t281, gm_srcl8pn2pjtmj6k, 214, 41);
                                gm_b16_8 t282 = GM_LD(gm_b16_8, (((uint8_t *)t280) + (uintptr_t)((uint64_t)1ULL) * 16));
                                gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&t282));
                                uint64_t t284 = gm_f_ptr_of_b77b4c9(t279, t283);
                                __int128 t285 = (__int128)t284;
                                __int128 r286; bool o287 = __builtin_sub_overflow(t278, t285, &r286);
                                if (o287) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 214, 23);
                                __int128 t288 = r286;
                                int64_t t289 = (int64_t)t288;
                                gm_l16 = t289;
                                __int128 t290 = gm_l12;
                                uint8_t t291 = ((t290 == ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
                                if (t291) {
                                    gm_l17 = ((int64_t)0LL);
                                } else {
                                    int64_t t292 = gm_l16;
                                    __int128 t293 = gm_l12;
                                    int64_t t294 = (int64_t)t293;
                                    if (t294 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 215, 54);
                                    if (t292 == ((int64_t)(-9223372036854775807LL - 1)) && t294 == (int64_t)-1) gm_trap("integer overflow in `/`", gm_srcl8pn2pjtmj6k, 215, 54);
                                    int64_t t295 = (int64_t)(t292 / t294);
                                    gm_l17 = t295;
                                }
                                void * t296 = gm_l0;
                                gm_b16_8 t297 = gm_l7;
                                int64_t t298 = gm_l17;
                                unsigned __int128 t299 = (unsigned __int128)t298;
                                gm_f_put_uint_cddin99(t296, t297, t299);
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
                            void * t300 = gm_l0;
                            gm_t88rcucg1b7uk t301 = gm_l5;
                            gm_tg5lql6itufk7 t302 = gm_f_int_info_ulcevdv(t300, GM_CONV(gm_csguankko4lmhd, t301));
                            gm_l83 = t302;
                            uint8_t t303 = (uint8_t)((((uint8_t *)&gm_l83))[8] != 0);
                            if (t303) {
                                gm_ttovhfee8mf28 t304 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l83));
                                gm_l18 = t304;
                            } else {
                                void * t305 = gm_l0;
                                gm_tl874t2atnlh3 t306 = gm_l1;
                                gm_b16_8 t307;
                                GM_ST(void *, (uint8_t *)&t307, (void *)(gm_s064htvgs8b59));
                                GM_ST(uintptr_t, (uint8_t *)&t307 + 8, (uintptr_t)(68));
                                gm_t76123gfet73g t308;
                                gm_f_trap_qh84l9d((void *)&t308, (void *)NULL, cx, NULL, t305, t306, t307);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t308);
                                goto gm_fdone;
                            }
                            void * t309 = gm_l0;
                            gm_t88rcucg1b7uk t310 = gm_l5;
                            void * t311 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t312 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t312)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t312, gm_srcl8pn2pjtmj6k, 220, 34);
                            gm_b16_8 t313 = GM_LD(gm_b16_8, (((uint8_t *)t311) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_b16_8 t314 = GM_LD(gm_b16_8, ((uint8_t *)&t313));
                            unsigned __int128 t315 = gm_f_get_int_pg7p4gc(t309, GM_CONV(gm_csguankko4lmhd, t310), t314);
                            gm_l19 = t315;
                            gm_l20 = ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                            uint8_t t316 = GM_LD(uint8_t, ((uint8_t *)&gm_l18));
                            if (t316) {
                                unsigned __int128 t317 = gm_l19;
                                __int128 t318;
                                memset(&t318, 0, sizeof t318); memcpy(&t318, ((uint8_t *)&t317), sizeof t318 < 16 ? sizeof t318 : 16);
                                gm_l20 = t318;
                            } else {
                                unsigned __int128 t319 = gm_l19;
                                unsigned __int128 t320 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                                unsigned __int128 n321 = (unsigned __int128)((uint64_t)1ULL);
                                if (n321 >= 128) gm_trap_shift((uint64_t)n321, "u128", gm_srcl8pn2pjtmj6k, 224, 31);
                                unsigned __int128 t322 = (unsigned __int128)(t320 >> (int)n321);
                                uint8_t t323 = (t319 <= t322);
                                if (t323) {
                                    unsigned __int128 t324 = gm_l19;
                                    __int128 t325;
                                    memset(&t325, 0, sizeof t325); memcpy(&t325, ((uint8_t *)&t324), sizeof t325 < 16 ? sizeof t325 : 16);
                                    gm_l20 = t325;
                                }
                            }
                            __int128 t326 = gm_l20;
                            __int128 t327 = gm_l12;
                            __int128 r328; bool o329 = gm_mul_ovf_i128(t326, t327, &r328);
                            __int128 t330 = r328;
                            gm_l21 = t330;
                            gm_t8588ma2cr9d7 t331 = gm_l2;
                            uint8_t t332 = GM_LD(uint8_t, (((uint8_t *)&t331) + 0));
                            uint8_t t333 = (uint8_t)((t332 == ((uint8_t)10ULL)));
                            gm_t8588ma2cr9d7 t334;
                            GM_ST(uint8_t, (uint8_t *)&t334 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t334 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t334 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t334 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t334 + 0), ((uint8_t)10ULL));
                            if (t333) {
                                __int128 t335 = gm_l13;
                                __int128 t336 = gm_l21;
                                __int128 r337; bool o338 = __builtin_add_overflow(t335, t336, &r337);
                                __int128 t339 = r337;
                                gm_l22 = t339;
                            } else {
                                __int128 t340 = gm_l13;
                                __int128 t341 = gm_l21;
                                __int128 r342; bool o343 = __builtin_sub_overflow(t340, t341, &r342);
                                __int128 t344 = r342;
                                gm_l22 = t344;
                            }
                            void * t345 = gm_l0;
                            gm_b16_8 t346 = gm_l7;
                            __int128 t347 = gm_l22;
                            uint64_t t348 = (uint64_t)t347;
                            uint64_t t349 = gm_l15;
                            uint64_t t350 = (uint64_t)(t348 & t349);
                            unsigned __int128 t351 = (unsigned __int128)t350;
                            gm_f_put_uint_cddin99(t345, t346, t351);
                            goto gm_lb25;
                        }
gm_lb26:;
                        {
                            void * t352 = gm_l0;
                            gm_tl874t2atnlh3 t353 = gm_l1;
                            gm_b16_8 t354;
                            GM_ST(void *, (uint8_t *)&t354, (void *)(gm_sge745dj3cnrb));
                            GM_ST(uintptr_t, (uint8_t *)&t354 + 8, (uintptr_t)(72));
                            gm_t76123gfet73g t355;
                            gm_f_trap_qh84l9d((void *)&t355, (void *)NULL, cx, NULL, t352, t353, t354);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t355);
                            goto gm_fdone;
                        }
                    }
gm_lb25:;
                    goto gm_lb1;
                }
gm_lb20:;
            }
            {
                {
                    uint8_t t356 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t357 = (uint8_t)(t356 == ((uint8_t)14ULL));
                    if (!t357) {
                        goto gm_lb27;
                    }
                    void * t358 = gm_l0;
                    gm_t88rcucg1b7uk t359 = gm_l4;
                    gm_f_union_def_h8mvf4t((void *)&gm_l84, t358, GM_CONV(gm_csguankko4lmhd, t359));
                    uint8_t t360 = (uint8_t)((((uint8_t *)&gm_l84))[24] != 0);
                    if (t360) {
                        memmove(((uint8_t *)&gm_l23), ((uint8_t *)&gm_l84), 24);
                    } else {
                        void * t361 = gm_l0;
                        gm_tl874t2atnlh3 t362 = gm_l1;
                        gm_b16_8 t363;
                        GM_ST(void *, (uint8_t *)&t363, (void *)(gm_sb9352gc5i144));
                        GM_ST(uintptr_t, (uint8_t *)&t363 + 8, (uintptr_t)(20));
                        gm_t76123gfet73g t364;
                        gm_f_trap_qh84l9d((void *)&t364, (void *)NULL, cx, NULL, t361, t362, t363);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t364);
                        goto gm_fdone;
                    }
                    void * t365 = gm_l0;
                    gm_t88rcucg1b7uk t366 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l23));
                    uint64_t t367 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t365, t366);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t368 = gm_f_us_cuv01fp(t367);
                    gm_l24 = t368;
                    gm_b16_8 t369 = gm_l7;
                    gm_f_fill_abmf5g5(t369, ((uint8_t)0ULL));
                    void * t370 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t371 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t372 = gm_l24;
                    uint64_t t373 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t374 = (uintptr_t)t372;
                    if (t373 > t374 || t374 > (uintptr_t)t371) gm_trap_slice(t373, t374, t371, gm_srcl8pn2pjtmj6k, 235, 13);
                    uint64_t t375 = ((uintptr_t)t372) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t376;
                    GM_ST(void *, (uint8_t *)&t376, (void *)(((uint8_t *)t370 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t376 + 8, (uintptr_t)(t375));
                    void * t377 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t378 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t378)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t378, gm_srcl8pn2pjtmj6k, 235, 33);
                    void * t379 = GM_LD(void *, (((uint8_t *)t377) + (uintptr_t)((uint64_t)0ULL) * 16));
                    uint64_t t380 = GM_LD(uint64_t, ((((uint8_t *)t377) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                    uint64_t t381 = gm_l24;
                    uint64_t t382 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t383 = (uintptr_t)t381;
                    if (t382 > t383 || t383 > (uintptr_t)t380) gm_trap_slice(t382, t383, t380, gm_srcl8pn2pjtmj6k, 235, 33);
                    uint64_t t384 = ((uintptr_t)t381) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t385;
                    GM_ST(void *, (uint8_t *)&t385, (void *)(((uint8_t *)t379 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t385 + 8, (uintptr_t)(t384));
                    gm_b16_8 t386 = GM_LD(gm_b16_8, ((uint8_t *)&t385));
                    gm_f_copy_from_f7j6p05(t376, t386);
                    goto gm_lb1;
                }
gm_lb27:;
                {
                    uint8_t t387 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t388 = (uint8_t)(t387 == ((uint8_t)15ULL));
                    if (!t388) {
                        goto gm_lb28;
                    }
                    void * t389 = gm_l0;
                    gm_t88rcucg1b7uk t390 = gm_l3;
                    gm_t88rcucg1b7uk t391 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t389, GM_CONV(gm_csguankko4lmhd, t390)));
                    gm_l25 = t391;
                    {
                        void * t392 = gm_l0;
                        gm_t88rcucg1b7uk t393 = gm_l25;
                        gm_f_union_def_h8mvf4t((void *)&gm_l85, t392, GM_CONV(gm_csguankko4lmhd, t393));
                    }
                    uint8_t t394 = (uint8_t)((((uint8_t *)&gm_l85))[24] != 0);
                    if (t394) {
                        memmove(((uint8_t *)&gm_l26), ((uint8_t *)&gm_l85), 24);
                    } else {
                        void * t395 = gm_l0;
                        gm_tl874t2atnlh3 t396 = gm_l1;
                        gm_b16_8 t397;
                        GM_ST(void *, (uint8_t *)&t397, (void *)(gm_stk678hojgu6q));
                        GM_ST(uintptr_t, (uint8_t *)&t397 + 8, (uintptr_t)(23));
                        gm_t76123gfet73g t398;
                        gm_f_trap_qh84l9d((void *)&t398, (void *)NULL, cx, NULL, t395, t396, t397);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t398);
                        goto gm_fdone;
                    }
                    void * t399 = gm_l0;
                    gm_t88rcucg1b7uk t400 = gm_l4;
                    gm_tg5lql6itufk7 t401 = gm_f_int_info_ulcevdv(t399, GM_CONV(gm_csguankko4lmhd, t400));
                    gm_l86 = t401;
                    uint8_t t402 = (uint8_t)((((uint8_t *)&gm_l86))[8] != 0);
                    if (t402) {
                        gm_ttovhfee8mf28 t403 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l86));
                        gm_l27 = t403;
                    } else {
                        void * t404 = gm_l0;
                        gm_tl874t2atnlh3 t405 = gm_l1;
                        gm_b16_8 t406;
                        GM_ST(void *, (uint8_t *)&t406, (void *)(gm_s3ergh2il1tjf));
                        GM_ST(uintptr_t, (uint8_t *)&t406 + 8, (uintptr_t)(42));
                        gm_t76123gfet73g t407;
                        gm_f_trap_qh84l9d((void *)&t407, (void *)NULL, cx, NULL, t404, t405, t406);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t407);
                        goto gm_fdone;
                    }
                    {
                        uint8_t t408 = GM_LD(uint8_t, ((uint8_t *)&gm_l27));
                        void * t409 = gm_l0;
                        gm_t88rcucg1b7uk t410 = gm_l4;
                        void * t411 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t412 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t412)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t412, gm_srcl8pn2pjtmj6k, 241, 53);
                        gm_b16_8 t413 = GM_LD(gm_b16_8, (((uint8_t *)t411) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t414 = GM_LD(gm_b16_8, ((uint8_t *)&t413));
                        unsigned __int128 t415 = gm_f_get_int_pg7p4gc(t409, GM_CONV(gm_csguankko4lmhd, t410), t414);
                        gm_f_num_val_i3upeur((void *)&gm_l28, t408, t415);
                    }
                    gm_b16_8 t416 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                    gm_b16_8 t417 = GM_LD(gm_b16_8, ((uint8_t *)&t416));
                    gm_l29 = t417;
                    gm_l30 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t418 = gm_l30;
                            uint64_t t419 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                            uint8_t t420 = (t418 < t419);
                            if (!t420) {
                                goto gm_lb29;
                            }
                            {
                                {
                                    uint64_t t421 = gm_l30;
                                    void * t422 = GM_LD(void *, ((uint8_t *)&gm_l29));
                                    uint64_t t423 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                                    uint64_t t424 = gm_l30;
                                    gm_tckdobunaulee t425 = GM_LD(gm_tckdobunaulee, (((uint8_t *)t422) + (uintptr_t)t424 * 72));
                                    GM_ST(uint64_t, ((uint8_t *)&gm_l87 + 0), t421);
                                    GM_ST(gm_tckdobunaulee, ((uint8_t *)&gm_l87 + 8), t425);
                                }
                                {
                                    {
                                        uint64_t t426 = GM_LD(uint64_t, ((uint8_t *)&gm_l87));
                                        gm_l31 = t426;
                                        memmove(((uint8_t *)&F.l32), (((uint8_t *)&gm_l87) + 8), 72);
                                        {
                                            {
                                                {
                                                    {
                                                        uint8_t t427 = (uint8_t)(((((uint8_t *)&F.l32) + 16))[4] != 0);
                                                        if (t427) {
                                                            goto gm_lb37;
                                                        }
                                                        gm_l88 = ((uint8_t)1);
                                                        goto gm_lb36;
                                                    }
gm_lb37:;
                                                    {
                                                        gm_l88 = ((uint8_t)0);
                                                        goto gm_lb36;
                                                    }
gm_lb38:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 243, 20);
                                                }
gm_lb36:;
                                                uint8_t t428 = gm_l88;
                                                if (!t428) {
                                                    goto gm_lb35;
                                                }
                                                uint8_t t429 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 24));
                                                uint8_t t430 = GM_LD(uint8_t, ((uint8_t *)&gm_l28));
                                                uint8_t t431 = ((t429 == t430));
                                                if (!t431) {
                                                    goto gm_lb35;
                                                }
                                                unsigned __int128 t432 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l32) + 24)));
                                                gm_l33 = t432;
                                                uint8_t t433 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 24));
                                                if (t433) {
                                                    unsigned __int128 t434 = gm_l33;
                                                    unsigned __int128 r435; bool o436 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)), t434, &r435);
                                                    unsigned __int128 t437 = r435;
                                                    gm_l34 = t437;
                                                } else {
                                                    unsigned __int128 t438 = gm_l33;
                                                    gm_l34 = t438;
                                                }
                                                unsigned __int128 t439 = gm_l34;
                                                unsigned __int128 t440 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l28) + 16));
                                                uint8_t t441 = ((t439 == t440));
                                                if (t441) {
                                                    void * t442 = gm_l0;
                                                    gm_t2lqu8jsj511d t443 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t442));
                                                    gm_l35 = t443;
                                                    void * t444 = gm_l0;
                                                    void * t445 = gm_l0;
                                                    gm_t88rcucg1b7uk t446 = gm_l25;
                                                    uint64_t t447 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t445, t446);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_b16_8 t448 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t444, t447);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l36 = t448;
                                                    void * t449 = gm_l0;
                                                    gm_t88rcucg1b7uk t450 = gm_l25;
                                                    gm_b16_8 t451 = gm_l36;
                                                    uint64_t t452 = gm_l31;
                                                    uint32_t t453 = (uint32_t)t452;
                                                    gm_b16_8 t454 = gm_f_empty_slice_ijtp9lo();
                                                    gm_b16_8 t455 = GM_LD(gm_b16_8, ((uint8_t *)&t454));
                                                    gm_b0_1 t456 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t449, t450, t451, t453, t455);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t457 = gm_l0;
                                                    gm_t88rcucg1b7uk t458 = gm_l3;
                                                    gm_b16_8 t459 = gm_l7;
                                                    gm_b16_8 t460 = gm_l36;
                                                    gm_b16_8 t461 = GM_LD(gm_b16_8, ((uint8_t *)&t460));
                                                    gm_b0_1 t462 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t457, t458, t459, t461);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t463 = gm_l0;
                                                    gm_t2lqu8jsj511d t464 = gm_l35;
                                                    gm_f_release_jn5kqdi(t463, GM_CONV(gm_csc7lc6jlkqkuq, t464));
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
                                                goto gm_lb34;
                                            }
gm_lb35:;
                                        }
gm_lb34:;
                                        goto gm_lb32;
                                    }
gm_lb33:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 242, 13);
                                }
gm_lb32:;
                            }
gm_lb31:;
                            uint64_t t465 = gm_l30;
                            uint64_t r466; bool o467 = __builtin_add_overflow(t465, ((uint64_t)1ULL), &r466);
                            uint64_t t468 = r466;
                            gm_l30 = t468;
                        }
                    }
gm_lb29:;
                    gm_b16_8 t469 = gm_l7;
                    gm_f_fill_abmf5g5(t469, ((uint8_t)0ULL));
                    goto gm_lb1;
                }
gm_lb28:;
            }
            {
                {
                    {
                        {
                            uint8_t t470 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t471 = (uint8_t)(t470 == ((uint8_t)16ULL));
                            if (!t471) {
                                goto gm_lb41;
                            }
                            goto gm_lb40;
                        }
gm_lb41:;
                        {
                            uint8_t t472 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t473 = (uint8_t)(t472 == ((uint8_t)17ULL));
                            if (!t473) {
                                goto gm_lb42;
                            }
                            goto gm_lb40;
                        }
gm_lb42:;
                        {
                            uint8_t t474 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t475 = (uint8_t)(t474 == ((uint8_t)18ULL));
                            if (!t475) {
                                goto gm_lb43;
                            }
                            goto gm_lb40;
                        }
gm_lb43:;
                        goto gm_lb39;
                    }
gm_lb40:;
                    {
                        {
                            {
                                void * t476 = gm_l0;
                                gm_t88rcucg1b7uk t477 = gm_l4;
                                gm_f_kind_htv6093((void *)&gm_l89, t476, GM_CONV(gm_csguankko4lmhd, t477));
                            }
                            uint8_t t478 = GM_LD(uint8_t, (((uint8_t *)&gm_l89) + 0));
                            uint8_t t479 = (uint8_t)(t478 == ((uint8_t)3ULL));
                            if (!t479) {
                                goto gm_lb45;
                            }
                            void * t480 = gm_l0;
                            gm_t88rcucg1b7uk t481 = gm_l4;
                            void * t482 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t483 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t483)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t483, gm_srcl8pn2pjtmj6k, 260, 38);
                            gm_b16_8 t484 = GM_LD(gm_b16_8, (((uint8_t *)t482) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t485 = GM_LD(gm_b16_8, ((uint8_t *)&t484));
                            double t486 = gm_f_get_float_grmhlm4(t480, GM_CONV(gm_csguankko4lmhd, t481), t485);
                            gm_l37 = t486;
                            void * t487 = gm_l0;
                            gm_t88rcucg1b7uk t488 = gm_l4;
                            void * t489 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t490 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t490)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t490, gm_srcl8pn2pjtmj6k, 261, 38);
                            gm_b16_8 t491 = GM_LD(gm_b16_8, (((uint8_t *)t489) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_b16_8 t492 = GM_LD(gm_b16_8, ((uint8_t *)&t491));
                            double t493 = gm_f_get_float_grmhlm4(t487, GM_CONV(gm_csguankko4lmhd, t488), t492);
                            gm_l38 = t493;
                            {
                                {
                                    uint8_t t494 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    uint8_t t495 = (uint8_t)(t494 == ((uint8_t)16ULL));
                                    if (!t495) {
                                        goto gm_lb47;
                                    }
                                    double t496 = gm_l37;
                                    double t497 = gm_l38;
                                    double t498 = gm_f_num_fmin_1k5uqp0(t496, t497);
                                    gm_l39 = t498;
                                    goto gm_lb46;
                                }
gm_lb47:;
                                {
                                    uint8_t t499 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                    uint8_t t500 = (uint8_t)(t499 == ((uint8_t)17ULL));
                                    if (!t500) {
                                        goto gm_lb48;
                                    }
                                    double t501 = gm_l37;
                                    double t502 = gm_l38;
                                    double t503 = gm_f_num_fmax_0qau2jp(t501, t502);
                                    gm_l39 = t503;
                                    goto gm_lb46;
                                }
gm_lb48:;
                                {
                                    double t504 = gm_l37;
                                    double t505 = gm_l38;
                                    double t506 = gm_f_num_fmax_0qau2jp(t504, t505);
                                    void * t507 = gm_l0;
                                    gm_t88rcucg1b7uk t508 = gm_l4;
                                    void * t509 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t510 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t510)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t510, gm_srcl8pn2pjtmj6k, 265, 67);
                                    gm_b16_8 t511 = GM_LD(gm_b16_8, (((uint8_t *)t509) + (uintptr_t)((uint64_t)2ULL) * 16));
                                    gm_b16_8 t512 = GM_LD(gm_b16_8, ((uint8_t *)&t511));
                                    double t513 = gm_f_get_float_grmhlm4(t507, GM_CONV(gm_csguankko4lmhd, t508), t512);
                                    double t514 = gm_f_num_fmin_1k5uqp0(t506, t513);
                                    gm_l39 = t514;
                                    goto gm_lb46;
                                }
gm_lb49:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 262, 22);
                            }
gm_lb46:;
                            void * t515 = gm_l0;
                            gm_t88rcucg1b7uk t516 = gm_l4;
                            double t517 = gm_l39;
                            gm_b16_8 t518 = gm_l7;
                            gm_f_put_float_fiad72d(t515, GM_CONV(gm_csguankko4lmhd, t516), t517, t518);
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
                        }
gm_lb45:;
                    }
gm_lb44:;
                    void * t519 = gm_l0;
                    gm_t88rcucg1b7uk t520 = gm_l4;
                    gm_tg5lql6itufk7 t521 = gm_f_int_info_ulcevdv(t519, GM_CONV(gm_csguankko4lmhd, t520));
                    gm_l90 = t521;
                    uint8_t t522 = (uint8_t)((((uint8_t *)&gm_l90))[8] != 0);
                    if (t522) {
                        gm_ttovhfee8mf28 t523 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l90));
                        gm_l40 = t523;
                    } else {
                        void * t524 = gm_l0;
                        gm_tl874t2atnlh3 t525 = gm_l1;
                        void * t526 = gm_l0;
                        void * t527 = GM_LD(void *, (((uint8_t *)t526) + 8));
                        gm_t88rcucg1b7uk t528 = gm_l4;
                        gm_b16_8 t529 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t527, t528);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t530;
                        GM_ST(gm_b16_8, ((uint8_t *)&t530 + 0), t529);
                        gm_f_format_kj15nqp((void *)&gm_l91, (void *)NULL, cx, NULL, t530);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t531 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                        if (!t531) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 8, gm_srcl8pn2pjtmj6k, 270, 62);
                        gm_b16_8 t532 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l91) + 8));
                        gm_b16_8 t533 = GM_LD(gm_b16_8, ((uint8_t *)&t532));
                        gm_t76123gfet73g t534;
                        gm_f_trap_qh84l9d((void *)&t534, (void *)NULL, cx, NULL, t524, t525, t533);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t534);
                        goto gm_fdone;
                    }
                    void * t535 = gm_l0;
                    gm_t88rcucg1b7uk t536 = gm_l4;
                    void * t537 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t538 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t538)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t538, gm_srcl8pn2pjtmj6k, 271, 32);
                    gm_b16_8 t539 = GM_LD(gm_b16_8, (((uint8_t *)t537) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t540 = GM_LD(gm_b16_8, ((uint8_t *)&t539));
                    unsigned __int128 t541 = gm_f_get_int_pg7p4gc(t535, GM_CONV(gm_csguankko4lmhd, t536), t540);
                    gm_l41 = t541;
                    void * t542 = gm_l0;
                    gm_t88rcucg1b7uk t543 = gm_l4;
                    void * t544 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t545 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t545)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t545, gm_srcl8pn2pjtmj6k, 272, 32);
                    gm_b16_8 t546 = GM_LD(gm_b16_8, (((uint8_t *)t544) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t547 = GM_LD(gm_b16_8, ((uint8_t *)&t546));
                    unsigned __int128 t548 = gm_f_get_int_pg7p4gc(t542, GM_CONV(gm_csguankko4lmhd, t543), t547);
                    gm_l42 = t548;
                    gm_l43 = ((uint64_t)0ULL);
                    {
                        {
                            uint8_t t549 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t550 = (uint8_t)(t549 == ((uint8_t)16ULL));
                            if (!t550) {
                                goto gm_lb51;
                            }
                            uint8_t t551 = GM_LD(uint8_t, ((uint8_t *)&gm_l40));
                            unsigned __int128 t552 = gm_l41;
                            unsigned __int128 t553 = gm_l42;
                            int32_t t554 = gm_f_int_cmp_kb3sm6u(t551, t552, t553);
                            uint8_t t555 = (t554 <= ((int32_t)0LL));
                            if (t555) {
                                gm_l92 = ((uint64_t)0ULL);
                            } else {
                                gm_l92 = ((uint64_t)1ULL);
                            }
                            uint64_t t556 = gm_l92;
                            gm_l43 = t556;
                            goto gm_lb50;
                        }
gm_lb51:;
                        {
                            uint8_t t557 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t558 = (uint8_t)(t557 == ((uint8_t)17ULL));
                            if (!t558) {
                                goto gm_lb52;
                            }
                            uint8_t t559 = GM_LD(uint8_t, ((uint8_t *)&gm_l40));
                            unsigned __int128 t560 = gm_l41;
                            unsigned __int128 t561 = gm_l42;
                            int32_t t562 = gm_f_int_cmp_kb3sm6u(t559, t560, t561);
                            uint8_t t563 = (t562 >= ((int32_t)0LL));
                            if (t563) {
                                gm_l93 = ((uint64_t)0ULL);
                            } else {
                                gm_l93 = ((uint64_t)1ULL);
                            }
                            uint64_t t564 = gm_l93;
                            gm_l43 = t564;
                            goto gm_lb50;
                        }
gm_lb52:;
                        {
                            void * t565 = gm_l0;
                            gm_t88rcucg1b7uk t566 = gm_l4;
                            void * t567 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t568 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t568)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t568, gm_srcl8pn2pjtmj6k, 278, 41);
                            gm_b16_8 t569 = GM_LD(gm_b16_8, (((uint8_t *)t567) + (uintptr_t)((uint64_t)2ULL) * 16));
                            gm_b16_8 t570 = GM_LD(gm_b16_8, ((uint8_t *)&t569));
                            unsigned __int128 t571 = gm_f_get_int_pg7p4gc(t565, GM_CONV(gm_csguankko4lmhd, t566), t570);
                            gm_l44 = t571;
                            uint8_t t572 = GM_LD(uint8_t, ((uint8_t *)&gm_l40));
                            unsigned __int128 t573 = gm_l41;
                            unsigned __int128 t574 = gm_l42;
                            int32_t t575 = gm_f_int_cmp_kb3sm6u(t572, t573, t574);
                            uint8_t t576 = (t575 < ((int32_t)0LL));
                            if (t576) {
                                gm_l43 = ((uint64_t)1ULL);
                            } else {
                                uint8_t t577 = GM_LD(uint8_t, ((uint8_t *)&gm_l40));
                                unsigned __int128 t578 = gm_l41;
                                unsigned __int128 t579 = gm_l44;
                                int32_t t580 = gm_f_int_cmp_kb3sm6u(t577, t578, t579);
                                uint8_t t581 = (t580 > ((int32_t)0LL));
                                if (t581) {
                                    gm_l43 = ((uint64_t)2ULL);
                                }
                            }
                            goto gm_lb50;
                        }
gm_lb53:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 274, 13);
                    }
gm_lb50:;
                    gm_b16_8 t582 = gm_l7;
                    void * t583 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t584 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint64_t t585 = gm_l43;
                    if ((uintptr_t)t585 >= (uintptr_t)(t584)) gm_trap_index((uintptr_t)t585, t584, gm_srcl8pn2pjtmj6k, 286, 27);
                    gm_b16_8 t586 = GM_LD(gm_b16_8, (((uint8_t *)t583) + (uintptr_t)t585 * 16));
                    gm_b16_8 t587 = GM_LD(gm_b16_8, ((uint8_t *)&t586));
                    gm_f_copy_from_f7j6p05(t582, t587);
                    goto gm_lb1;
                }
gm_lb39:;
            }
            {
                {
                    uint8_t t588 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t589 = (uint8_t)(t588 == ((uint8_t)19ULL));
                    if (!t589) {
                        goto gm_lb54;
                    }
                    {
                        {
                            {
                                void * t590 = gm_l0;
                                gm_t88rcucg1b7uk t591 = gm_l4;
                                gm_f_kind_htv6093((void *)&gm_l94, t590, GM_CONV(gm_csguankko4lmhd, t591));
                            }
                            uint8_t t592 = GM_LD(uint8_t, (((uint8_t *)&gm_l94) + 0));
                            uint8_t t593 = (uint8_t)(t592 == ((uint8_t)3ULL));
                            if (!t593) {
                                goto gm_lb56;
                            }
                            void * t594 = gm_l0;
                            gm_t88rcucg1b7uk t595 = gm_l4;
                            void * t596 = gm_l0;
                            gm_t88rcucg1b7uk t597 = gm_l4;
                            void * t598 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t599 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t599)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t599, gm_srcl8pn2pjtmj6k, 290, 50);
                            gm_b16_8 t600 = GM_LD(gm_b16_8, (((uint8_t *)t598) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t601 = GM_LD(gm_b16_8, ((uint8_t *)&t600));
                            double t602 = gm_f_get_float_grmhlm4(t596, GM_CONV(gm_csguankko4lmhd, t597), t601);
                            uint64_t t603;
                            memset(&t603, 0, sizeof t603); memcpy(&t603, ((uint8_t *)&t602), sizeof t603 < 8 ? sizeof t603 : 8);
                            uint64_t t604 = (uint64_t)(t603 & ((uint64_t)9223372036854775807ULL));
                            double t605;
                            memset(&t605, 0, sizeof t605); memcpy(&t605, ((uint8_t *)&t604), sizeof t605 < 8 ? sizeof t605 : 8);
                            gm_b16_8 t606 = gm_l7;
                            gm_f_put_float_fiad72d(t594, GM_CONV(gm_csguankko4lmhd, t595), t605, t606);
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
                        }
gm_lb56:;
                    }
gm_lb55:;
                    void * t607 = gm_l0;
                    gm_t88rcucg1b7uk t608 = gm_l4;
                    gm_tg5lql6itufk7 t609 = gm_f_int_info_ulcevdv(t607, GM_CONV(gm_csguankko4lmhd, t608));
                    gm_l95 = t609;
                    uint8_t t610 = (uint8_t)((((uint8_t *)&gm_l95))[8] != 0);
                    if (t610) {
                        gm_ttovhfee8mf28 t611 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l95));
                        gm_l45 = t611;
                    } else {
                        void * t612 = gm_l0;
                        gm_tl874t2atnlh3 t613 = gm_l1;
                        gm_b16_8 t614;
                        GM_ST(void *, (uint8_t *)&t614, (void *)(gm_sbel6qookv0gl));
                        GM_ST(uintptr_t, (uint8_t *)&t614 + 8, (uintptr_t)(53));
                        gm_t76123gfet73g t615;
                        gm_f_trap_qh84l9d((void *)&t615, (void *)NULL, cx, NULL, t612, t613, t614);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t615);
                        goto gm_fdone;
                    }
                    void * t616 = gm_l0;
                    gm_t88rcucg1b7uk t617 = gm_l4;
                    void * t618 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t619 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t619)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t619, gm_srcl8pn2pjtmj6k, 294, 32);
                    gm_b16_8 t620 = GM_LD(gm_b16_8, (((uint8_t *)t618) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t621 = GM_LD(gm_b16_8, ((uint8_t *)&t620));
                    unsigned __int128 t622 = gm_f_get_int_pg7p4gc(t616, GM_CONV(gm_csguankko4lmhd, t617), t621);
                    gm_l46 = t622;
                    uint8_t t623 = GM_LD(uint8_t, ((uint8_t *)&gm_l45));
                    uint8_t t624 = (uint8_t)!t623;
                    if (t624) {
                        gm_l96 = ((uint8_t)1);
                    } else {
                        unsigned __int128 t625 = gm_l46;
                        unsigned __int128 n626 = (unsigned __int128)((uint64_t)127ULL);
                        if (n626 >= 128) gm_trap_shift((uint64_t)n626, "u128", gm_srcl8pn2pjtmj6k, 295, 36);
                        unsigned __int128 t627 = (unsigned __int128)(t625 >> (int)n626);
                        uint8_t t628 = ((t627 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
                        gm_l96 = t628;
                    }
                    uint8_t t629 = gm_l96;
                    if (t629) {
                        void * t630 = gm_l0;
                        gm_b16_8 t631 = gm_l7;
                        unsigned __int128 t632 = gm_l46;
                        gm_f_put_uint_cddin99(t630, t631, t632);
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
                    unsigned __int128 t633 = gm_l46;
                    unsigned __int128 r634; bool o635 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)), t633, &r634);
                    unsigned __int128 t636 = r634;
                    gm_l47 = t636;
                    unsigned __int128 t637 = gm_l47;
                    uint32_t t638 = GM_LD(uint32_t, (((uint8_t *)&gm_l45) + 4));
                    uint32_t r639; bool o640 = __builtin_sub_overflow(t638, ((uint32_t)1ULL), &r639);
                    if (o640) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 300, 34);
                    uint32_t t641 = r639;
                    unsigned __int128 n642 = (unsigned __int128)t641;
                    if (n642 >= 128) gm_trap_shift((uint64_t)n642, "u128", gm_srcl8pn2pjtmj6k, 300, 22);
                    unsigned __int128 r643 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n642);
                    bool o644 = (unsigned __int128)(r643 >> (int)n642) != ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
                    if (o644) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 300, 22);
                    unsigned __int128 t645 = r643;
                    uint8_t t646 = ((t637 == t645));
                    if (t646) {
                        {
                            void * t647 = gm_l0;
                            gm_tl874t2atnlh3 t648 = gm_l1;
                            gm_b16_8 t649;
                            GM_ST(void *, (uint8_t *)&t649, (void *)(gm_sbl3pbs9195ho));
                            GM_ST(uintptr_t, (uint8_t *)&t649 + 8, (uintptr_t)(3));
                            gm_f_checked_overflow_ma1bm1g((void *)&gm_l97, (void *)NULL, cx, NULL, t647, t648, t649);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t650 = (uint8_t)!((((uint8_t *)&gm_l97))[0] != 0);
                        if (t650) {
                        } else {
                            gm_t76123gfet73g t651 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l97) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t651);
                            goto gm_fdone;
                        }
                        void * t652 = gm_l0;
                        gm_b16_8 t653 = gm_l7;
                        unsigned __int128 t654 = gm_l46;
                        gm_f_put_uint_cddin99(t652, t653, t654);
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
                    }
                    void * t655 = gm_l0;
                    gm_b16_8 t656 = gm_l7;
                    unsigned __int128 t657 = gm_l47;
                    gm_f_put_uint_cddin99(t655, t656, t657);
                    goto gm_lb1;
                }
gm_lb54:;
            }
            {
                {
                    uint8_t t658 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t659 = (uint8_t)(t658 == ((uint8_t)20ULL));
                    if (!t659) {
                        goto gm_lb57;
                    }
                    void * t660 = gm_l0;
                    gm_t88rcucg1b7uk t661 = gm_l4;
                    gm_tg5lql6itufk7 t662 = gm_f_int_info_ulcevdv(t660, GM_CONV(gm_csguankko4lmhd, t661));
                    gm_l98 = t662;
                    uint8_t t663 = (uint8_t)((((uint8_t *)&gm_l98))[8] != 0);
                    if (t663) {
                        gm_ttovhfee8mf28 t664 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l98));
                        gm_l48 = t664;
                    } else {
                        void * t665 = gm_l0;
                        gm_tl874t2atnlh3 t666 = gm_l1;
                        gm_b16_8 t667;
                        GM_ST(void *, (uint8_t *)&t667, (void *)(gm_s3gnqv0up712m));
                        GM_ST(uintptr_t, (uint8_t *)&t667 + 8, (uintptr_t)(60));
                        gm_t76123gfet73g t668;
                        gm_f_trap_qh84l9d((void *)&t668, (void *)NULL, cx, NULL, t665, t666, t667);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t668);
                        goto gm_fdone;
                    }
                    void * t669 = gm_l0;
                    gm_t88rcucg1b7uk t670 = gm_l4;
                    void * t671 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t672 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t672)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t672, gm_srcl8pn2pjtmj6k, 309, 32);
                    gm_b16_8 t673 = GM_LD(gm_b16_8, (((uint8_t *)t671) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t674 = GM_LD(gm_b16_8, ((uint8_t *)&t673));
                    unsigned __int128 t675 = gm_f_get_int_pg7p4gc(t669, GM_CONV(gm_csguankko4lmhd, t670), t674);
                    gm_l49 = t675;
                    void * t676 = gm_l0;
                    gm_t88rcucg1b7uk t677 = gm_l5;
                    void * t678 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t679 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t679)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t679, gm_srcl8pn2pjtmj6k, 310, 32);
                    gm_b16_8 t680 = GM_LD(gm_b16_8, (((uint8_t *)t678) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t681 = GM_LD(gm_b16_8, ((uint8_t *)&t680));
                    unsigned __int128 t682 = gm_f_get_int_pg7p4gc(t676, GM_CONV(gm_csguankko4lmhd, t677), t681);
                    gm_l50 = t682;
                    gm_l51 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                    uint8_t t683 = GM_LD(uint8_t, ((uint8_t *)&gm_l48));
                    unsigned __int128 t684 = gm_l49;
                    unsigned __int128 t685 = gm_l50;
                    int32_t t686 = gm_f_int_cmp_kb3sm6u(t683, t684, t685);
                    uint8_t t687 = (t686 >= ((int32_t)0LL));
                    if (t687) {
                        unsigned __int128 t688 = gm_l49;
                        unsigned __int128 t689 = gm_l50;
                        unsigned __int128 r690; bool o691 = __builtin_sub_overflow(t688, t689, &r690);
                        unsigned __int128 t692 = r690;
                        gm_l51 = t692;
                    } else {
                        unsigned __int128 t693 = gm_l50;
                        unsigned __int128 t694 = gm_l49;
                        unsigned __int128 r695; bool o696 = __builtin_sub_overflow(t693, t694, &r695);
                        unsigned __int128 t697 = r695;
                        gm_l51 = t697;
                    }
                    {
                        {
                            void * t698 = gm_l0;
                            gm_t88rcucg1b7uk t699 = gm_l3;
                            gm_tg5lql6itufk7 t700 = gm_f_int_info_ulcevdv(t698, GM_CONV(gm_csguankko4lmhd, t699));
                            gm_l99 = t700;
                            uint8_t t701 = (uint8_t)((((uint8_t *)&gm_l99))[8] != 0);
                            if (!t701) {
                                goto gm_lb59;
                            }
                            gm_ttovhfee8mf28 t702 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l99));
                            gm_l52 = t702;
                            uint8_t t703 = GM_LD(uint8_t, ((uint8_t *)&gm_l52));
                            uint32_t t704 = GM_LD(uint32_t, (((uint8_t *)&gm_l52) + 4));
                            unsigned __int128 t705 = gm_l51;
                            unsigned __int128 t706 = gm_f_num_ext_348kmbu(t703, t704, t705);
                            gm_l51 = t706;
                            goto gm_lb58;
                        }
gm_lb59:;
                    }
gm_lb58:;
                    void * t707 = gm_l0;
                    gm_b16_8 t708 = gm_l7;
                    unsigned __int128 t709 = gm_l51;
                    gm_f_put_uint_cddin99(t707, t708, t709);
                    goto gm_lb1;
                }
gm_lb57:;
            }
            {
                {
                    uint8_t t710 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t711 = (uint8_t)(t710 == ((uint8_t)21ULL));
                    if (!t711) {
                        goto gm_lb60;
                    }
                    gm_t9qkh0rsooc8d t712 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
                    gm_l53 = t712;
                    void * t713 = gm_l0;
                    gm_tl874t2atnlh3 t714 = gm_l1;
                    gm_t9qkh0rsooc8d t715 = gm_l53;
                    gm_t88rcucg1b7uk t716 = gm_l4;
                    gm_t88rcucg1b7uk t717 = gm_l3;
                    gm_t88rcucg1b7uk t718 = gm_l5;
                    void * t719 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t720 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t720)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t720, gm_srcl8pn2pjtmj6k, 320, 60);
                    gm_b16_8 t721 = GM_LD(gm_b16_8, (((uint8_t *)t719) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t722 = GM_LD(gm_b16_8, ((uint8_t *)&t721));
                    void * t723 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t724 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t724)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t724, gm_srcl8pn2pjtmj6k, 320, 66);
                    gm_b16_8 t725 = GM_LD(gm_b16_8, (((uint8_t *)t723) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t726 = GM_LD(gm_b16_8, ((uint8_t *)&t725));
                    gm_b16_8 t727 = gm_l7;
                    gm_f_pow_7je2uvi((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t713, t714, t715, t716, t717, t718, t722, t726, t727);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb60:;
                {
                    {
                        {
                            uint8_t t728 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t729 = (uint8_t)(t728 == ((uint8_t)22ULL));
                            if (!t729) {
                                goto gm_lb63;
                            }
                            goto gm_lb62;
                        }
gm_lb63:;
                        {
                            uint8_t t730 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t731 = (uint8_t)(t730 == ((uint8_t)23ULL));
                            if (!t731) {
                                goto gm_lb64;
                            }
                            goto gm_lb62;
                        }
gm_lb64:;
                        {
                            uint8_t t732 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t733 = (uint8_t)(t732 == ((uint8_t)24ULL));
                            if (!t733) {
                                goto gm_lb65;
                            }
                            goto gm_lb62;
                        }
gm_lb65:;
                        {
                            uint8_t t734 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t735 = (uint8_t)(t734 == ((uint8_t)25ULL));
                            if (!t735) {
                                goto gm_lb66;
                            }
                            goto gm_lb62;
                        }
gm_lb66:;
                        {
                            uint8_t t736 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t737 = (uint8_t)(t736 == ((uint8_t)26ULL));
                            if (!t737) {
                                goto gm_lb67;
                            }
                            goto gm_lb62;
                        }
gm_lb67:;
                        goto gm_lb61;
                    }
gm_lb62:;
                    void * t738 = gm_l0;
                    gm_t88rcucg1b7uk t739 = gm_l4;
                    gm_f_kind_htv6093((void *)&gm_l101, t738, GM_CONV(gm_csguankko4lmhd, t739));
                    {
                        {
                            uint8_t t740 = GM_LD(uint8_t, (((uint8_t *)&gm_l101) + 0));
                            uint8_t t741 = (uint8_t)(t740 == ((uint8_t)3ULL));
                            if (!t741) {
                                goto gm_lb69;
                            }
                            gm_l100 = ((uint8_t)1);
                            goto gm_lb68;
                        }
gm_lb69:;
                        {
                            gm_l100 = ((uint8_t)0);
                            goto gm_lb68;
                        }
gm_lb70:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 322, 21);
                    }
gm_lb68:;
                    uint8_t t742 = gm_l100;
                    uint8_t t743 = (uint8_t)!t742;
                    if (t743) {
                        void * t744 = gm_l0;
                        gm_tl874t2atnlh3 t745 = gm_l1;
                        gm_b16_8 t746;
                        GM_ST(void *, (uint8_t *)&t746, (void *)(gm_s9h8lp3gkedth));
                        GM_ST(uintptr_t, (uint8_t *)&t746 + 8, (uintptr_t)(28));
                        gm_t76123gfet73g t747;
                        gm_f_trap_qh84l9d((void *)&t747, (void *)NULL, cx, NULL, t744, t745, t746);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t747);
                        goto gm_fdone;
                    }
                    void * t748 = gm_l0;
                    gm_t88rcucg1b7uk t749 = gm_l4;
                    void * t750 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t751 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t751)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t751, gm_srcl8pn2pjtmj6k, 323, 34);
                    gm_b16_8 t752 = GM_LD(gm_b16_8, (((uint8_t *)t750) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t753 = GM_LD(gm_b16_8, ((uint8_t *)&t752));
                    double t754 = gm_f_get_float_grmhlm4(t748, GM_CONV(gm_csguankko4lmhd, t749), t753);
                    gm_l54 = t754;
                    double t755 = gm_l54;
                    uint64_t t756;
                    memset(&t756, 0, sizeof t756); memcpy(&t756, ((uint8_t *)&t755), sizeof t756 < 8 ? sizeof t756 : 8);
                    gm_l55 = t756;
                    uint64_t t757 = gm_l55;
                    unsigned __int128 n758 = (unsigned __int128)((uint64_t)52ULL);
                    if (n758 >= 64) gm_trap_shift((uint64_t)n758, "u64", gm_srcl8pn2pjtmj6k, 325, 21);
                    uint64_t t759 = (uint64_t)(t757 >> (int)n758);
                    uint64_t t760 = (uint64_t)(t759 & ((uint64_t)2047ULL));
                    gm_l56 = t760;
                    {
                        {
                            uint8_t t761 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t762 = (uint8_t)(t761 == ((uint8_t)22ULL));
                            if (!t762) {
                                goto gm_lb72;
                            }
                            void * t763 = GM_LD(void *, ((uint8_t *)&gm_l7));
                            uint64_t t764 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t764)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t764, gm_srcl8pn2pjtmj6k, 327, 29);
                            {
                                {
                                    uint64_t t765 = gm_l56;
                                    uint8_t t766 = ((t765 == ((uint64_t)2047ULL)));
                                    if (!t766) {
                                        goto gm_lb74;
                                    }
                                    uint64_t t767 = gm_l55;
                                    uint64_t t768 = (uint64_t)(t767 & ((uint64_t)4503599627370495ULL));
                                    uint8_t t769 = (!(t768 == ((uint64_t)0ULL)));
                                    if (!t769) {
                                        goto gm_lb74;
                                    }
                                    gm_l102 = ((uint8_t)1ULL);
                                    goto gm_lb73;
                                }
gm_lb74:;
                                gm_l102 = ((uint8_t)0ULL);
                            }
gm_lb73:;
                            uint8_t t770 = gm_l102;
                            GM_ST(uint8_t, (((uint8_t *)t763) + (uintptr_t)((uint64_t)0ULL) * 1), t770);
                            goto gm_lb71;
                        }
gm_lb72:;
                        {
                            uint8_t t771 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t772 = (uint8_t)(t771 == ((uint8_t)23ULL));
                            if (!t772) {
                                goto gm_lb75;
                            }
                            void * t773 = GM_LD(void *, ((uint8_t *)&gm_l7));
                            uint64_t t774 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t774)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t774, gm_srcl8pn2pjtmj6k, 328, 29);
                            {
                                {
                                    uint64_t t775 = gm_l56;
                                    uint8_t t776 = ((t775 == ((uint64_t)2047ULL)));
                                    if (!t776) {
                                        goto gm_lb77;
                                    }
                                    uint64_t t777 = gm_l55;
                                    uint64_t t778 = (uint64_t)(t777 & ((uint64_t)4503599627370495ULL));
                                    uint8_t t779 = ((t778 == ((uint64_t)0ULL)));
                                    if (!t779) {
                                        goto gm_lb77;
                                    }
                                    gm_l103 = ((uint8_t)1ULL);
                                    goto gm_lb76;
                                }
gm_lb77:;
                                gm_l103 = ((uint8_t)0ULL);
                            }
gm_lb76:;
                            uint8_t t780 = gm_l103;
                            GM_ST(uint8_t, (((uint8_t *)t773) + (uintptr_t)((uint64_t)0ULL) * 1), t780);
                            goto gm_lb71;
                        }
gm_lb75:;
                        {
                            uint8_t t781 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t782 = (uint8_t)(t781 == ((uint8_t)24ULL));
                            if (!t782) {
                                goto gm_lb78;
                            }
                            void * t783 = GM_LD(void *, ((uint8_t *)&gm_l7));
                            uint64_t t784 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t784)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t784, gm_srcl8pn2pjtmj6k, 329, 32);
                            uint64_t t785 = gm_l56;
                            uint8_t t786 = ((t785 == ((uint64_t)2047ULL)));
                            if (t786) {
                                gm_l104 = ((uint8_t)0ULL);
                            } else {
                                gm_l104 = ((uint8_t)1ULL);
                            }
                            uint8_t t787 = gm_l104;
                            GM_ST(uint8_t, (((uint8_t *)t783) + (uintptr_t)((uint64_t)0ULL) * 1), t787);
                            goto gm_lb71;
                        }
gm_lb78:;
                        {
                            uint8_t t788 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t789 = (uint8_t)(t788 == ((uint8_t)25ULL));
                            if (!t789) {
                                goto gm_lb79;
                            }
                            void * t790 = GM_LD(void *, ((uint8_t *)&gm_l7));
                            uint64_t t791 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t791)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t791, gm_srcl8pn2pjtmj6k, 330, 31);
                            uint64_t t792 = gm_l55;
                            unsigned __int128 n793 = (unsigned __int128)((uint64_t)63ULL);
                            if (n793 >= 64) gm_trap_shift((uint64_t)n793, "u64", gm_srcl8pn2pjtmj6k, 330, 44);
                            uint64_t t794 = (uint64_t)(t792 >> (int)n793);
                            uint8_t t795 = ((t794 == ((uint64_t)1ULL)));
                            if (t795) {
                                gm_l105 = ((uint8_t)1ULL);
                            } else {
                                gm_l105 = ((uint8_t)0ULL);
                            }
                            uint8_t t796 = gm_l105;
                            GM_ST(uint8_t, (((uint8_t *)t790) + (uintptr_t)((uint64_t)0ULL) * 1), t796);
                            goto gm_lb71;
                        }
gm_lb79:;
                        {
                            void * t797 = gm_l0;
                            gm_t88rcucg1b7uk t798 = gm_l4;
                            double t799 = gm_l54;
                            double t800 = (double)(__builtin_sqrt(t799));
                            gm_b16_8 t801 = gm_l7;
                            gm_f_put_float_fiad72d(t797, GM_CONV(gm_csguankko4lmhd, t798), t800, t801);
                            goto gm_lb71;
                        }
gm_lb80:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 326, 13);
                    }
gm_lb71:;
                    goto gm_lb1;
                }
gm_lb61:;
            }
            {
                {
                    uint8_t t802 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t803 = (uint8_t)(t802 == ((uint8_t)27ULL));
                    if (!t803) {
                        goto gm_lb81;
                    }
                    {
                        {
                            void * t804 = gm_l0;
                            gm_t88rcucg1b7uk t805 = gm_l4;
                            gm_f_kind_htv6093((void *)&gm_l106, t804, GM_CONV(gm_csguankko4lmhd, t805));
                            uint8_t t806 = GM_LD(uint8_t, (((uint8_t *)&gm_l106) + 0));
                            uint8_t t807 = (uint8_t)(t806 == ((uint8_t)3ULL));
                            if (!t807) {
                                goto gm_lb83;
                            }
                            uint16_t t808 = GM_LD(uint16_t, (((uint8_t *)&gm_l106) + 8));
                            gm_l57 = t808;
                            void * t809 = gm_l0;
                            gm_t88rcucg1b7uk t810 = gm_l4;
                            void * t811 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t812 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t812)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t812, gm_srcl8pn2pjtmj6k, 336, 34);
                            gm_b16_8 t813 = GM_LD(gm_b16_8, (((uint8_t *)t811) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t814 = GM_LD(gm_b16_8, ((uint8_t *)&t813));
                            double t815 = gm_f_get_float_grmhlm4(t809, GM_CONV(gm_csguankko4lmhd, t810), t814);
                            gm_l58 = t815;
                            void * t816 = gm_l0;
                            gm_t88rcucg1b7uk t817 = gm_l4;
                            void * t818 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t819 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t819)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t819, gm_srcl8pn2pjtmj6k, 337, 34);
                            gm_b16_8 t820 = GM_LD(gm_b16_8, (((uint8_t *)t818) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_b16_8 t821 = GM_LD(gm_b16_8, ((uint8_t *)&t820));
                            double t822 = gm_f_get_float_grmhlm4(t816, GM_CONV(gm_csguankko4lmhd, t817), t821);
                            gm_l59 = t822;
                            void * t823 = gm_l0;
                            gm_t88rcucg1b7uk t824 = gm_l4;
                            void * t825 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t826 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t826)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t826, gm_srcl8pn2pjtmj6k, 338, 34);
                            gm_b16_8 t827 = GM_LD(gm_b16_8, (((uint8_t *)t825) + (uintptr_t)((uint64_t)2ULL) * 16));
                            gm_b16_8 t828 = GM_LD(gm_b16_8, ((uint8_t *)&t827));
                            double t829 = gm_f_get_float_grmhlm4(t823, GM_CONV(gm_csguankko4lmhd, t824), t828);
                            gm_l60 = t829;
                            uint16_t t830 = gm_l57;
                            uint8_t t831 = ((t830 == ((uint16_t)32ULL)));
                            if (t831) {
                                void * t832 = gm_l0;
                                gm_t88rcucg1b7uk t833 = gm_l4;
                                double t834 = gm_l58;
                                float t835 = (float)((double)t834);
                                double t836 = gm_l59;
                                float t837 = (float)((double)t836);
                                double t838 = gm_l60;
                                float t839 = (float)((double)t838);
                                float t840 = (float)(__builtin_fmaf(t835, t837, t839));
                                double t841 = (double)t840;
                                gm_b16_8 t842 = gm_l7;
                                gm_f_put_float_fiad72d(t832, GM_CONV(gm_csguankko4lmhd, t833), t841, t842);
                            } else {
                                void * t843 = gm_l0;
                                gm_t88rcucg1b7uk t844 = gm_l4;
                                double t845 = gm_l58;
                                double t846 = gm_l59;
                                double t847 = gm_l60;
                                double t848 = (double)(__builtin_fma(t845, t846, t847));
                                gm_b16_8 t849 = gm_l7;
                                gm_f_put_float_fiad72d(t843, GM_CONV(gm_csguankko4lmhd, t844), t848, t849);
                            }
                            goto gm_lb82;
                        }
gm_lb83:;
                        {
                            void * t850 = gm_l0;
                            gm_tl874t2atnlh3 t851 = gm_l1;
                            gm_b16_8 t852;
                            GM_ST(void *, (uint8_t *)&t852, (void *)(gm_sh01rb1u2jpfs));
                            GM_ST(uintptr_t, (uint8_t *)&t852 + 8, (uintptr_t)(18));
                            gm_t76123gfet73g t853;
                            gm_f_trap_qh84l9d((void *)&t853, (void *)NULL, cx, NULL, t850, t851, t852);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t853);
                            goto gm_fdone;
                        }
                    }
gm_lb82:;
                    goto gm_lb1;
                }
gm_lb81:;
            }
            {
                {
                    {
                        {
                            uint8_t t854 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t855 = (uint8_t)(t854 == ((uint8_t)32ULL));
                            if (!t855) {
                                goto gm_lb86;
                            }
                            goto gm_lb85;
                        }
gm_lb86:;
                        {
                            uint8_t t856 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t857 = (uint8_t)(t856 == ((uint8_t)33ULL));
                            if (!t857) {
                                goto gm_lb87;
                            }
                            goto gm_lb85;
                        }
gm_lb87:;
                        goto gm_lb84;
                    }
gm_lb85:;
                    void * t858 = gm_l0;
                    gm_t88rcucg1b7uk t859 = gm_l4;
                    gm_tg5lql6itufk7 t860 = gm_f_int_info_ulcevdv(t858, GM_CONV(gm_csguankko4lmhd, t859));
                    gm_l107 = t860;
                    uint8_t t861 = (uint8_t)((((uint8_t *)&gm_l107))[8] != 0);
                    if (t861) {
                        gm_ttovhfee8mf28 t862 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l107));
                        gm_l61 = t862;
                    } else {
                        void * t863 = gm_l0;
                        gm_tl874t2atnlh3 t864 = gm_l1;
                        gm_b16_8 t865;
                        GM_ST(void *, (uint8_t *)&t865, (void *)(gm_s26c80h2pm6a7));
                        GM_ST(uintptr_t, (uint8_t *)&t865 + 8, (uintptr_t)(25));
                        gm_t76123gfet73g t866;
                        gm_f_trap_qh84l9d((void *)&t866, (void *)NULL, cx, NULL, t863, t864, t865);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t866);
                        goto gm_fdone;
                    }
                    void * t867 = gm_l0;
                    gm_t88rcucg1b7uk t868 = gm_l5;
                    gm_tg5lql6itufk7 t869 = gm_f_int_info_ulcevdv(t867, GM_CONV(gm_csguankko4lmhd, t868));
                    gm_l108 = t869;
                    uint8_t t870 = (uint8_t)((((uint8_t *)&gm_l108))[8] != 0);
                    if (t870) {
                        gm_ttovhfee8mf28 t871 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l108));
                        gm_l62 = t871;
                    } else {
                        void * t872 = gm_l0;
                        gm_tl874t2atnlh3 t873 = gm_l1;
                        gm_b16_8 t874;
                        GM_ST(void *, (uint8_t *)&t874, (void *)(gm_sti4tkgslrkgo));
                        GM_ST(uintptr_t, (uint8_t *)&t874 + 8, (uintptr_t)(25));
                        gm_t76123gfet73g t875;
                        gm_f_trap_qh84l9d((void *)&t875, (void *)NULL, cx, NULL, t872, t873, t874);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t875);
                        goto gm_fdone;
                    }
                    uint32_t t876 = GM_LD(uint32_t, (((uint8_t *)&gm_l61) + 4));
                    gm_l63 = t876;
                    void * t877 = gm_l0;
                    void * t878 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t879 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t879)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t879, gm_srcl8pn2pjtmj6k, 349, 29);
                    void * t880 = GM_LD(void *, (((uint8_t *)t878) + (uintptr_t)((uint64_t)0ULL) * 16));
                    uint64_t t881 = GM_LD(uint64_t, ((((uint8_t *)t878) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                    void * t882 = gm_l0;
                    gm_t88rcucg1b7uk t883 = gm_l4;
                    uint64_t t884 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t882, t883);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t885 = gm_f_us_cuv01fp(t884);
                    uint64_t t886 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t887 = (uintptr_t)t885;
                    if (t886 > t887 || t887 > (uintptr_t)t881) gm_trap_slice(t886, t887, t881, gm_srcl8pn2pjtmj6k, 349, 29);
                    uint64_t t888 = ((uintptr_t)t885) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t889;
                    GM_ST(void *, (uint8_t *)&t889, (void *)(((uint8_t *)t880 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t889 + 8, (uintptr_t)(t888));
                    gm_b16_8 t890 = GM_LD(gm_b16_8, ((uint8_t *)&t889));
                    unsigned __int128 t891 = gm_f_get_uint_bq8c4b7(t877, t890);
                    uint32_t t892 = gm_l63;
                    unsigned __int128 t893 = gm_f_num_mask_do3naah(t892);
                    unsigned __int128 t894 = (unsigned __int128)(t891 & t893);
                    gm_l64 = t894;
                    void * t895 = gm_l0;
                    gm_t88rcucg1b7uk t896 = gm_l5;
                    void * t897 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t898 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t898)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t898, gm_srcl8pn2pjtmj6k, 350, 35);
                    gm_b16_8 t899 = GM_LD(gm_b16_8, (((uint8_t *)t897) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t900 = GM_LD(gm_b16_8, ((uint8_t *)&t899));
                    unsigned __int128 t901 = gm_f_get_int_pg7p4gc(t895, GM_CONV(gm_csguankko4lmhd, t896), t900);
                    gm_l65 = t901;
                    gm_l66 = ((uint32_t)0ULL);
                    uint8_t t902 = GM_LD(uint8_t, ((uint8_t *)&gm_l62));
                    if (t902) {
                        unsigned __int128 t903 = gm_l65;
                        unsigned __int128 n904 = (unsigned __int128)((uint64_t)127ULL);
                        if (n904 >= 128) gm_trap_shift((uint64_t)n904, "u128", gm_srcl8pn2pjtmj6k, 352, 39);
                        unsigned __int128 t905 = (unsigned __int128)(t903 >> (int)n904);
                        uint8_t t906 = ((t905 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL))));
                        gm_l109 = t906;
                    } else {
                        gm_l109 = ((uint8_t)0);
                    }
                    uint8_t t907 = gm_l109;
                    uint8_t t908 = (uint8_t)!t907;
                    if (t908) {
                        unsigned __int128 t909 = gm_l65;
                        uint32_t t910 = gm_l63;
                        unsigned __int128 t911 = (unsigned __int128)t910;
                        if (t911 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 352, 65);
                        unsigned __int128 t912 = (unsigned __int128)(t909 % t911);
                        uint32_t t913 = (uint32_t)t912;
                        gm_l66 = t913;
                    }
                    gm_t8588ma2cr9d7 t914 = gm_l2;
                    uint8_t t915 = GM_LD(uint8_t, (((uint8_t *)&t914) + 0));
                    uint8_t t916 = (uint8_t)((t915 == ((uint8_t)33ULL)));
                    gm_t8588ma2cr9d7 t917;
                    GM_ST(uint8_t, (uint8_t *)&t917 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t917 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t917 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t917 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t917 + 0), ((uint8_t)33ULL));
                    if (t916) {
                        uint32_t t918 = gm_l63;
                        uint32_t t919 = gm_l66;
                        uint32_t r920; bool o921 = __builtin_sub_overflow(t918, t919, &r920);
                        if (o921) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 353, 35);
                        uint32_t t922 = r920;
                        uint32_t t923 = gm_l63;
                        if (t923 == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 353, 34);
                        uint32_t t924 = (uint32_t)(t922 % t923);
                        gm_l66 = t924;
                    }
                    uint32_t t925 = gm_l66;
                    uint8_t t926 = ((t925 == ((uint32_t)0ULL)));
                    if (t926) {
                        unsigned __int128 t927 = gm_l64;
                        gm_l67 = t927;
                    } else {
                        unsigned __int128 t928 = gm_l64;
                        uint32_t t929 = gm_l66;
                        unsigned __int128 t930 = (unsigned __int128)t929;
                        unsigned __int128 n931 = (unsigned __int128)t930;
                        if (n931 >= 128) gm_trap_shift((uint64_t)n931, "u128", gm_srcl8pn2pjtmj6k, 354, 43);
                        unsigned __int128 r932 = (unsigned __int128)((unsigned __int128)t928 << (int)n931);
                        bool o933 = (unsigned __int128)(r932 >> (int)n931) != t928;
                        unsigned __int128 t934 = r932;
                        unsigned __int128 t935 = gm_l64;
                        uint32_t t936 = gm_l63;
                        uint32_t t937 = gm_l66;
                        uint32_t r938; bool o939 = __builtin_sub_overflow(t936, t937, &r938);
                        if (o939) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 354, 71);
                        uint32_t t940 = r938;
                        unsigned __int128 t941 = (unsigned __int128)t940;
                        unsigned __int128 n942 = (unsigned __int128)t941;
                        if (n942 >= 128) gm_trap_shift((uint64_t)n942, "u128", gm_srcl8pn2pjtmj6k, 354, 61);
                        unsigned __int128 t943 = (unsigned __int128)(t935 >> (int)n942);
                        unsigned __int128 t944 = (unsigned __int128)(t934 | t943);
                        uint32_t t945 = gm_l63;
                        unsigned __int128 t946 = gm_f_num_mask_do3naah(t945);
                        unsigned __int128 t947 = (unsigned __int128)(t944 & t946);
                        gm_l67 = t947;
                    }
                    void * t948 = gm_l0;
                    gm_b16_8 t949 = gm_l7;
                    uint8_t t950 = GM_LD(uint8_t, ((uint8_t *)&gm_l61));
                    uint32_t t951 = gm_l63;
                    unsigned __int128 t952 = gm_l67;
                    unsigned __int128 t953 = gm_f_num_ext_348kmbu(t950, t951, t952);
                    gm_f_put_uint_cddin99(t948, t949, t953);
                    goto gm_lb1;
                }
gm_lb84:;
            }
            {
                {
                    {
                        {
                            uint8_t t954 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t955 = (uint8_t)(t954 == ((uint8_t)28ULL));
                            if (!t955) {
                                goto gm_lb90;
                            }
                            goto gm_lb89;
                        }
gm_lb90:;
                        {
                            uint8_t t956 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t957 = (uint8_t)(t956 == ((uint8_t)29ULL));
                            if (!t957) {
                                goto gm_lb91;
                            }
                            goto gm_lb89;
                        }
gm_lb91:;
                        {
                            uint8_t t958 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t959 = (uint8_t)(t958 == ((uint8_t)30ULL));
                            if (!t959) {
                                goto gm_lb92;
                            }
                            goto gm_lb89;
                        }
gm_lb92:;
                        {
                            uint8_t t960 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t961 = (uint8_t)(t960 == ((uint8_t)31ULL));
                            if (!t961) {
                                goto gm_lb93;
                            }
                            goto gm_lb89;
                        }
gm_lb93:;
                        {
                            uint8_t t962 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t963 = (uint8_t)(t962 == ((uint8_t)34ULL));
                            if (!t963) {
                                goto gm_lb94;
                            }
                            goto gm_lb89;
                        }
gm_lb94:;
                        goto gm_lb88;
                    }
gm_lb89:;
                    void * t964 = gm_l0;
                    gm_t88rcucg1b7uk t965 = gm_l4;
                    gm_tg5lql6itufk7 t966 = gm_f_int_info_ulcevdv(t964, GM_CONV(gm_csguankko4lmhd, t965));
                    gm_l110 = t966;
                    uint8_t t967 = (uint8_t)((((uint8_t *)&gm_l110))[8] != 0);
                    if (t967) {
                        gm_ttovhfee8mf28 t968 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l110));
                        gm_l68 = t968;
                    } else {
                        void * t969 = gm_l0;
                        gm_tl874t2atnlh3 t970 = gm_l1;
                        gm_b16_8 t971;
                        GM_ST(void *, (uint8_t *)&t971, (void *)(gm_spqf88ftq0gqk));
                        GM_ST(uintptr_t, (uint8_t *)&t971 + 8, (uintptr_t)(28));
                        gm_t76123gfet73g t972;
                        gm_f_trap_qh84l9d((void *)&t972, (void *)NULL, cx, NULL, t969, t970, t971);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t972);
                        goto gm_fdone;
                    }
                    uint32_t t973 = GM_LD(uint32_t, (((uint8_t *)&gm_l68) + 4));
                    gm_l69 = t973;
                    void * t974 = gm_l0;
                    void * t975 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t976 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t976)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t976, gm_srcl8pn2pjtmj6k, 360, 29);
                    void * t977 = GM_LD(void *, (((uint8_t *)t975) + (uintptr_t)((uint64_t)0ULL) * 16));
                    uint64_t t978 = GM_LD(uint64_t, ((((uint8_t *)t975) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                    void * t979 = gm_l0;
                    gm_t88rcucg1b7uk t980 = gm_l4;
                    uint64_t t981 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t979, t980);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t982 = gm_f_us_cuv01fp(t981);
                    uint64_t t983 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t984 = (uintptr_t)t982;
                    if (t983 > t984 || t984 > (uintptr_t)t978) gm_trap_slice(t983, t984, t978, gm_srcl8pn2pjtmj6k, 360, 29);
                    uint64_t t985 = ((uintptr_t)t982) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t986;
                    GM_ST(void *, (uint8_t *)&t986, (void *)(((uint8_t *)t977 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t986 + 8, (uintptr_t)(t985));
                    gm_b16_8 t987 = GM_LD(gm_b16_8, ((uint8_t *)&t986));
                    unsigned __int128 t988 = gm_f_get_uint_bq8c4b7(t974, t987);
                    uint32_t t989 = gm_l69;
                    unsigned __int128 t990 = gm_f_num_mask_do3naah(t989);
                    unsigned __int128 t991 = (unsigned __int128)(t988 & t990);
                    gm_l70 = t991;
                    gm_l71 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                    {
                        {
                            uint8_t t992 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t993 = (uint8_t)(t992 == ((uint8_t)28ULL));
                            if (!t993) {
                                goto gm_lb96;
                            }
                            void * t994 = gm_l0;
                            gm_b16_8 t995 = gm_l7;
                            unsigned __int128 t996 = gm_l70;
                            uint32_t t997 = (uint32_t)gm_clz((unsigned __int128)(unsigned __int128)(t996), 128);
                            uint32_t t998 = gm_l69;
                            uint32_t r999; bool o1000 = __builtin_sub_overflow(((uint32_t)128ULL), t998, &r999);
                            if (o1000) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 364, 53);
                            uint32_t t1001 = r999;
                            uint32_t r1002; bool o1003 = __builtin_sub_overflow(t997, t1001, &r1002);
                            if (o1003) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 364, 42);
                            uint32_t t1004 = r1002;
                            unsigned __int128 t1005 = (unsigned __int128)t1004;
                            gm_f_put_uint_cddin99(t994, t995, t1005);
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
gm_lb96:;
                        {
                            uint8_t t1006 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1007 = (uint8_t)(t1006 == ((uint8_t)29ULL));
                            if (!t1007) {
                                goto gm_lb97;
                            }
                            void * t1008 = gm_l0;
                            gm_b16_8 t1009 = gm_l7;
                            unsigned __int128 t1010 = gm_l70;
                            uint8_t t1011 = ((t1010 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL))));
                            if (t1011) {
                                uint32_t t1012 = gm_l69;
                                unsigned __int128 t1013 = (unsigned __int128)t1012;
                                gm_l111 = t1013;
                            } else {
                                unsigned __int128 t1014 = gm_l70;
                                uint32_t t1015 = (uint32_t)gm_ctz((unsigned __int128)(unsigned __int128)(t1014), 128);
                                unsigned __int128 t1016 = (unsigned __int128)t1015;
                                gm_l111 = t1016;
                            }
                            unsigned __int128 t1017 = gm_l111;
                            gm_f_put_uint_cddin99(t1008, t1009, t1017);
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
gm_lb97:;
                        {
                            uint8_t t1018 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1019 = (uint8_t)(t1018 == ((uint8_t)30ULL));
                            if (!t1019) {
                                goto gm_lb98;
                            }
                            void * t1020 = gm_l0;
                            gm_b16_8 t1021 = gm_l7;
                            unsigned __int128 t1022 = gm_l70;
                            uint32_t t1023 = (uint32_t)gm_popcount((unsigned __int128)(unsigned __int128)(t1022));
                            unsigned __int128 t1024 = (unsigned __int128)t1023;
                            gm_f_put_uint_cddin99(t1020, t1021, t1024);
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
gm_lb98:;
                        {
                            uint8_t t1025 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1026 = (uint8_t)(t1025 == ((uint8_t)31ULL));
                            if (!t1026) {
                                goto gm_lb99;
                            }
                            uint32_t t1027 = gm_l69;
                            if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 376, 24);
                            uint32_t t1028 = (uint32_t)(t1027 % ((uint32_t)8ULL));
                            uint8_t t1029 = (!(t1028 == ((uint32_t)0ULL)));
                            if (t1029) {
                                void * t1030 = gm_l0;
                                gm_tl874t2atnlh3 t1031 = gm_l1;
                                uint32_t t1032 = gm_l69;
                                gm_t88rcucg1b7uk t1033;
                                GM_ST(uint32_t, ((uint8_t *)&t1033 + 0), t1032);
                                gm_f_format_ven7kt5((void *)&gm_l112, (void *)NULL, cx, NULL, t1033);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t1034 = (uint8_t)!((((uint8_t *)&gm_l112))[0] != 0);
                                if (!t1034) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l112) + 8, gm_srcl8pn2pjtmj6k, 376, 62);
                                gm_b16_8 t1035 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l112) + 8));
                                gm_b16_8 t1036 = GM_LD(gm_b16_8, ((uint8_t *)&t1035));
                                gm_t76123gfet73g t1037;
                                gm_f_trap_qh84l9d((void *)&t1037, (void *)NULL, cx, NULL, t1030, t1031, t1036);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t1037);
                                goto gm_fdone;
                            }
                            gm_l73 = ((uint32_t)0ULL);
                            uint32_t t1038 = gm_l69;
                            if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 377, 33);
                            uint32_t t1039 = (uint32_t)(t1038 / ((uint32_t)8ULL));
                            gm_l72 = t1039;
                            {
                                for (;;) {
                                    uint32_t t1040 = gm_l73;
                                    uint32_t t1041 = gm_l72;
                                    uint8_t t1042 = (t1040 < t1041);
                                    if (!t1042) {
                                        goto gm_lb100;
                                    }
                                    {
                                        {
                                            {
                                                uint32_t t1043 = gm_l73;
                                                gm_l74 = t1043;
                                                unsigned __int128 t1044 = gm_l71;
                                                unsigned __int128 t1045 = gm_l70;
                                                uint32_t t1046 = gm_l74;
                                                uint32_t r1047; bool o1048 = __builtin_mul_overflow(t1046, ((uint32_t)8ULL), &r1047);
                                                if (o1048) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 377, 58);
                                                uint32_t t1049 = r1047;
                                                unsigned __int128 t1050 = (unsigned __int128)t1049;
                                                unsigned __int128 n1051 = (unsigned __int128)t1050;
                                                if (n1051 >= 128) gm_trap_shift((uint64_t)n1051, "u128", gm_srcl8pn2pjtmj6k, 377, 48);
                                                unsigned __int128 t1052 = (unsigned __int128)(t1045 >> (int)n1051);
                                                unsigned __int128 t1053 = (unsigned __int128)(t1052 & ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 255ULL)));
                                                uint32_t t1054 = gm_l69;
                                                if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcl8pn2pjtmj6k, 377, 83);
                                                uint32_t t1055 = (uint32_t)(t1054 / ((uint32_t)8ULL));
                                                uint32_t r1056; bool o1057 = __builtin_sub_overflow(t1055, ((uint32_t)1ULL), &r1056);
                                                if (o1057) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 377, 83);
                                                uint32_t t1058 = r1056;
                                                uint32_t t1059 = gm_l74;
                                                uint32_t r1060; bool o1061 = __builtin_sub_overflow(t1058, t1059, &r1060);
                                                if (o1061) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 377, 83);
                                                uint32_t t1062 = r1060;
                                                uint32_t r1063; bool o1064 = __builtin_mul_overflow(t1062, ((uint32_t)8ULL), &r1063);
                                                if (o1064) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 377, 82);
                                                uint32_t t1065 = r1063;
                                                unsigned __int128 t1066 = (unsigned __int128)t1065;
                                                unsigned __int128 n1067 = (unsigned __int128)t1066;
                                                if (n1067 >= 128) gm_trap_shift((uint64_t)n1067, "u128", gm_srcl8pn2pjtmj6k, 377, 46);
                                                unsigned __int128 r1068 = (unsigned __int128)((unsigned __int128)t1053 << (int)n1067);
                                                bool o1069 = (unsigned __int128)(r1068 >> (int)n1067) != t1053;
                                                if (o1069) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 377, 46);
                                                unsigned __int128 t1070 = r1068;
                                                unsigned __int128 t1071 = (unsigned __int128)(t1044 | t1070);
                                                gm_l71 = t1071;
                                                goto gm_lb103;
                                            }
gm_lb104:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 377, 21);
                                        }
gm_lb103:;
                                    }
gm_lb102:;
                                    uint32_t t1072 = gm_l73;
                                    uint32_t r1073; bool o1074 = __builtin_add_overflow(t1072, ((uint32_t)1ULL), &r1073);
                                    uint32_t t1075 = r1073;
                                    gm_l73 = t1075;
                                }
                            }
gm_lb100:;
                            goto gm_lb95;
                        }
gm_lb99:;
                        {
                            gm_l76 = ((uint32_t)0ULL);
                            uint32_t t1076 = gm_l69;
                            gm_l75 = t1076;
                            {
                                for (;;) {
                                    uint32_t t1077 = gm_l76;
                                    uint32_t t1078 = gm_l75;
                                    uint8_t t1079 = (t1077 < t1078);
                                    if (!t1079) {
                                        goto gm_lb106;
                                    }
                                    {
                                        {
                                            {
                                                uint32_t t1080 = gm_l76;
                                                gm_l77 = t1080;
                                                unsigned __int128 t1081 = gm_l70;
                                                uint32_t t1082 = gm_l77;
                                                unsigned __int128 t1083 = (unsigned __int128)t1082;
                                                unsigned __int128 n1084 = (unsigned __int128)t1083;
                                                if (n1084 >= 128) gm_trap_shift((uint64_t)n1084, "u128", gm_srcl8pn2pjtmj6k, 381, 29);
                                                unsigned __int128 t1085 = (unsigned __int128)(t1081 >> (int)n1084);
                                                unsigned __int128 t1086 = (unsigned __int128)(t1085 & ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)));
                                                uint8_t t1087 = ((t1086 == ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL))));
                                                if (t1087) {
                                                    unsigned __int128 t1088 = gm_l71;
                                                    uint32_t t1089 = gm_l69;
                                                    uint32_t r1090; bool o1091 = __builtin_sub_overflow(t1089, ((uint32_t)1ULL), &r1090);
                                                    if (o1091) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 381, 75);
                                                    uint32_t t1092 = r1090;
                                                    uint32_t t1093 = gm_l77;
                                                    uint32_t r1094; bool o1095 = __builtin_sub_overflow(t1092, t1093, &r1094);
                                                    if (o1095) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 381, 75);
                                                    uint32_t t1096 = r1094;
                                                    unsigned __int128 t1097 = (unsigned __int128)t1096;
                                                    unsigned __int128 n1098 = (unsigned __int128)t1097;
                                                    if (n1098 >= 128) gm_trap_shift((uint64_t)n1098, "u128", gm_srcl8pn2pjtmj6k, 381, 59);
                                                    unsigned __int128 r1099 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n1098);
                                                    bool o1100 = (unsigned __int128)(r1099 >> (int)n1098) != ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
                                                    if (o1100) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 381, 59);
                                                    unsigned __int128 t1101 = r1099;
                                                    unsigned __int128 t1102 = (unsigned __int128)(t1088 | t1101);
                                                    gm_l71 = t1102;
                                                }
                                                goto gm_lb109;
                                            }
gm_lb110:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 380, 21);
                                        }
gm_lb109:;
                                    }
gm_lb108:;
                                    uint32_t t1103 = gm_l76;
                                    uint32_t r1104; bool o1105 = __builtin_add_overflow(t1103, ((uint32_t)1ULL), &r1104);
                                    uint32_t t1106 = r1104;
                                    gm_l76 = t1106;
                                }
                            }
gm_lb106:;
                            goto gm_lb95;
                        }
gm_lb105:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 362, 13);
                    }
gm_lb95:;
                    void * t1107 = gm_l0;
                    gm_b16_8 t1108 = gm_l7;
                    uint8_t t1109 = GM_LD(uint8_t, ((uint8_t *)&gm_l68));
                    uint32_t t1110 = gm_l69;
                    unsigned __int128 t1111 = gm_l71;
                    unsigned __int128 t1112 = gm_f_num_ext_348kmbu(t1109, t1110, t1111);
                    gm_f_put_uint_cddin99(t1107, t1108, t1112);
                    goto gm_lb1;
                }
gm_lb88:;
            }
            {
                {
                    uint8_t t1113 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t1114 = (uint8_t)(t1113 == ((uint8_t)63ULL));
                    if (!t1114) {
                        goto gm_lb111;
                    }
                    void * t1115 = gm_l0;
                    gm_t88rcucg1b7uk t1116 = gm_l5;
                    gm_tg5lql6itufk7 t1117 = gm_f_int_info_ulcevdv(t1115, GM_CONV(gm_csguankko4lmhd, t1116));
                    gm_l113 = t1117;
                    uint8_t t1118 = (uint8_t)((((uint8_t *)&gm_l113))[8] != 0);
                    if (t1118) {
                        gm_ttovhfee8mf28 t1119 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l113));
                        gm_l78 = t1119;
                    } else {
                        void * t1120 = gm_l0;
                        gm_tl874t2atnlh3 t1121 = gm_l1;
                        gm_b16_8 t1122;
                        GM_ST(void *, (uint8_t *)&t1122, (void *)(gm_sartclcojin4f));
                        GM_ST(uintptr_t, (uint8_t *)&t1122 + 8, (uintptr_t)(51));
                        gm_t76123gfet73g t1123;
                        gm_f_trap_qh84l9d((void *)&t1123, (void *)NULL, cx, NULL, t1120, t1121, t1122);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t1123);
                        goto gm_fdone;
                    }
                    {
                        uint8_t t1124 = GM_LD(uint8_t, ((uint8_t *)&gm_l78));
                        void * t1125 = gm_l0;
                        gm_t88rcucg1b7uk t1126 = gm_l5;
                        void * t1127 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t1128 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t1128)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t1128, gm_srcl8pn2pjtmj6k, 389, 56);
                        gm_b16_8 t1129 = GM_LD(gm_b16_8, (((uint8_t *)t1127) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t1130 = GM_LD(gm_b16_8, ((uint8_t *)&t1129));
                        unsigned __int128 t1131 = gm_f_get_int_pg7p4gc(t1125, GM_CONV(gm_csguankko4lmhd, t1126), t1130);
                        gm_f_num_val_i3upeur((void *)&gm_l79, t1124, t1131);
                    }
                    unsigned __int128 t1132 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l79) + 16));
                    uint64_t t1133 = (uint64_t)t1132;
                    uint64_t t1134 = (uint64_t)(((uint64_t)14695981039346656037ULL) ^ t1133);
                    gm_l80 = t1134;
                    {
                        void * t1135 = gm_l0;
                        gm_tl874t2atnlh3 t1136 = gm_l1;
                        gm_t88rcucg1b7uk t1137 = gm_l4;
                        void * t1138 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t1139 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t1139)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t1139, gm_srcl8pn2pjtmj6k, 391, 38);
                        gm_b16_8 t1140 = GM_LD(gm_b16_8, (((uint8_t *)t1138) + (uintptr_t)((uint64_t)0ULL) * 16));
                        void * t1141 = (void *)((uint8_t *)&gm_l80);
                        gm_f_hash_val_nr2e99r((void *)&gm_l114, (void *)NULL, cx, NULL, t1135, t1136, t1137, t1140, t1141);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t1142 = (uint8_t)!((((uint8_t *)&gm_l114))[0] != 0);
                    if (t1142) {
                    } else {
                        gm_t76123gfet73g t1143 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l114) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t1143);
                        goto gm_fdone;
                    }
                    void * t1144 = gm_l0;
                    gm_b16_8 t1145 = gm_l7;
                    uint64_t t1146 = gm_l80;
                    unsigned __int128 t1147 = (unsigned __int128)t1146;
                    gm_f_put_uint_cddin99(t1144, t1145, t1147);
                    goto gm_lb1;
                }
gm_lb111:;
                {
                    {
                        {
                            uint8_t t1148 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1149 = (uint8_t)(t1148 == ((uint8_t)40ULL));
                            if (!t1149) {
                                goto gm_lb114;
                            }
                            goto gm_lb113;
                        }
gm_lb114:;
                        {
                            uint8_t t1150 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1151 = (uint8_t)(t1150 == ((uint8_t)41ULL));
                            if (!t1151) {
                                goto gm_lb115;
                            }
                            goto gm_lb113;
                        }
gm_lb115:;
                        {
                            uint8_t t1152 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1153 = (uint8_t)(t1152 == ((uint8_t)42ULL));
                            if (!t1153) {
                                goto gm_lb116;
                            }
                            goto gm_lb113;
                        }
gm_lb116:;
                        {
                            uint8_t t1154 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1155 = (uint8_t)(t1154 == ((uint8_t)43ULL));
                            if (!t1155) {
                                goto gm_lb117;
                            }
                            goto gm_lb113;
                        }
gm_lb117:;
                        {
                            uint8_t t1156 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1157 = (uint8_t)(t1156 == ((uint8_t)44ULL));
                            if (!t1157) {
                                goto gm_lb118;
                            }
                            goto gm_lb113;
                        }
gm_lb118:;
                        {
                            uint8_t t1158 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1159 = (uint8_t)(t1158 == ((uint8_t)45ULL));
                            if (!t1159) {
                                goto gm_lb119;
                            }
                            goto gm_lb113;
                        }
gm_lb119:;
                        {
                            uint8_t t1160 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1161 = (uint8_t)(t1160 == ((uint8_t)46ULL));
                            if (!t1161) {
                                goto gm_lb120;
                            }
                            goto gm_lb113;
                        }
gm_lb120:;
                        {
                            uint8_t t1162 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1163 = (uint8_t)(t1162 == ((uint8_t)47ULL));
                            if (!t1163) {
                                goto gm_lb121;
                            }
                            goto gm_lb113;
                        }
gm_lb121:;
                        {
                            uint8_t t1164 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1165 = (uint8_t)(t1164 == ((uint8_t)48ULL));
                            if (!t1165) {
                                goto gm_lb122;
                            }
                            goto gm_lb113;
                        }
gm_lb122:;
                        {
                            uint8_t t1166 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1167 = (uint8_t)(t1166 == ((uint8_t)49ULL));
                            if (!t1167) {
                                goto gm_lb123;
                            }
                            goto gm_lb113;
                        }
gm_lb123:;
                        {
                            uint8_t t1168 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1169 = (uint8_t)(t1168 == ((uint8_t)50ULL));
                            if (!t1169) {
                                goto gm_lb124;
                            }
                            goto gm_lb113;
                        }
gm_lb124:;
                        {
                            uint8_t t1170 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1171 = (uint8_t)(t1170 == ((uint8_t)51ULL));
                            if (!t1171) {
                                goto gm_lb125;
                            }
                            goto gm_lb113;
                        }
gm_lb125:;
                        {
                            uint8_t t1172 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1173 = (uint8_t)(t1172 == ((uint8_t)52ULL));
                            if (!t1173) {
                                goto gm_lb126;
                            }
                            goto gm_lb113;
                        }
gm_lb126:;
                        {
                            uint8_t t1174 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1175 = (uint8_t)(t1174 == ((uint8_t)53ULL));
                            if (!t1175) {
                                goto gm_lb127;
                            }
                            goto gm_lb113;
                        }
gm_lb127:;
                        {
                            uint8_t t1176 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1177 = (uint8_t)(t1176 == ((uint8_t)54ULL));
                            if (!t1177) {
                                goto gm_lb128;
                            }
                            goto gm_lb113;
                        }
gm_lb128:;
                        {
                            uint8_t t1178 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1179 = (uint8_t)(t1178 == ((uint8_t)55ULL));
                            if (!t1179) {
                                goto gm_lb129;
                            }
                            goto gm_lb113;
                        }
gm_lb129:;
                        {
                            uint8_t t1180 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1181 = (uint8_t)(t1180 == ((uint8_t)56ULL));
                            if (!t1181) {
                                goto gm_lb130;
                            }
                            goto gm_lb113;
                        }
gm_lb130:;
                        {
                            uint8_t t1182 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1183 = (uint8_t)(t1182 == ((uint8_t)57ULL));
                            if (!t1183) {
                                goto gm_lb131;
                            }
                            goto gm_lb113;
                        }
gm_lb131:;
                        {
                            uint8_t t1184 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t1185 = (uint8_t)(t1184 == ((uint8_t)58ULL));
                            if (!t1185) {
                                goto gm_lb132;
                            }
                            goto gm_lb113;
                        }
gm_lb132:;
                        goto gm_lb112;
                    }
gm_lb113:;
                    void * t1186 = gm_l0;
                    gm_tl874t2atnlh3 t1187 = gm_l1;
                    gm_t8588ma2cr9d7 t1188 = gm_l2;
                    gm_t88rcucg1b7uk t1189 = gm_l3;
                    gm_t88rcucg1b7uk t1190 = gm_l4;
                    gm_t88rcucg1b7uk t1191 = gm_l5;
                    gm_b16_8 t1192 = gm_l6;
                    gm_b16_8 t1193 = gm_l7;
                    gm_f_vector_builtin_3fn933h((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t1186, t1187, t1188, t1189, t1190, t1191, t1192, t1193);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb112:;
            }
            {
                {
                    void * t1194 = gm_l0;
                    gm_tl874t2atnlh3 t1195 = gm_l1;
                    gm_t8588ma2cr9d7 t1196 = gm_l2;
                    gm_t6bs2bvvcgdnj t1197;
                    GM_ST(gm_t8588ma2cr9d7, ((uint8_t *)&t1197 + 0), t1196);
                    gm_f_format_985clkf((void *)&gm_l115, (void *)NULL, cx, NULL, t1197);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t1198 = (uint8_t)!((((uint8_t *)&gm_l115))[0] != 0);
                    if (!t1198) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l115) + 8, gm_srcl8pn2pjtmj6k, 395, 42);
                    gm_b16_8 t1199 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l115) + 8));
                    gm_b16_8 t1200 = GM_LD(gm_b16_8, ((uint8_t *)&t1199));
                    gm_t76123gfet73g t1201;
                    gm_f_unsupported_70vvk1e((void *)&t1201, (void *)NULL, cx, NULL, t1194, t1195, t1200);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t1201);
                    goto gm_fdone;
                }
gm_lb133:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 191, 5);
            }
        }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_array_ptr_to_slice_7dg3m1v(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_array_ptr_to_slice_7dg3m1v F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* ptr_ty */
    gm_b16_8 gm_l2; /* b */
    gm_b16_8 gm_l3; /* dst */
    uint64_t gm_l4; /* len */
    gm_t88rcucg1b7uk gm_l5; /* pointee */
    uint64_t gm_l6; /* n */
    GM_STACK_CHECK(&F, 288u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l7;
    gm_t53at80pfdus3 gm_l8;
    gm_l4 = ((uint64_t)0ULL);
    {
        {
            {
                void * t4 = gm_l0;
                gm_t88rcucg1b7uk t5 = gm_l1;
                gm_f_kind_htv6093((void *)&gm_l7, t4, GM_CONV(gm_csguankko4lmhd, t5));
            }
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)8ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l7) + 8) + 4));
            gm_l5 = t8;
            {
                void * t9 = gm_l0;
                gm_t88rcucg1b7uk t10 = gm_l5;
                gm_f_kind_htv6093((void *)&gm_l8, t9, GM_CONV(gm_csguankko4lmhd, t10));
            }
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l8) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)11ULL));
            if (!t12) {
                goto gm_lb2;
            }
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            gm_l6 = t13;
            uint64_t t14 = gm_l6;
            gm_l4 = t14;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t15 = gm_l0;
    gm_b16_8 t16 = gm_l3;
    void * t17 = gm_l0;
    gm_b16_8 t18 = gm_l2;
    uint64_t t19 = gm_f_ptr_of_b77b4c9(t17, t18);
    uint64_t t20 = gm_l4;
    gm_f_put_slice_ko14loi(t15, t16, t19, t20);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_coerce_bytes_9kf3u24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tmn7n0n9raou5 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_coerce_bytes_9kf3u24 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tmn7n0n9raou5 gm_l2; /* kind */
    gm_t88rcucg1b7uk gm_l3; /* from */
    gm_t88rcucg1b7uk gm_l4; /* to */
    gm_b16_8 gm_l5; /* b */
    gm_b16_8 gm_l6; /* dst */
    gm_ttovhfee8mf28 gm_l7; /* info */
    uint32_t gm_l8; /* variant */
    gm_t88rcucg1b7uk gm_l9; /* derr */
    gm_b16_8 gm_l10; /* lifted */
    gm_t88rcucg1b7uk gm_l11; /* src_in */
    gm_t88rcucg1b7uk gm_l12; /* dst_in */
    gm_b16_8 gm_l13; /* payload */
    gm_t88rcucg1b7uk gm_l14; /* pointee */
    gm_b16_8 gm_l15; /* sl */
    GM_STACK_CHECK(&F, 1925u);
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
    gm_tg5lql6itufk7 gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    gm_t53at80pfdus3 gm_l19;
    uint8_t gm_l20;
    gm_t53at80pfdus3 gm_l21;
    gm_tgglga8ro3isc gm_l22;
    gm_t53at80pfdus3 gm_l23;
    uint8_t gm_l24;
    gm_t53at80pfdus3 gm_l25;
    uint8_t gm_l26;
    gm_t53at80pfdus3 gm_l27;
    {
        {
            {
                {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)0ULL));
                    if (!t37) {
                        goto gm_lb2;
                    }
                    {
                        {
                            void * t38 = gm_l0;
                            gm_t88rcucg1b7uk t39 = gm_l4;
                            gm_tg5lql6itufk7 t40 = gm_f_int_info_ulcevdv(t38, GM_CONV(gm_csguankko4lmhd, t39));
                            gm_l16 = t40;
                            uint8_t t41 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
                            if (!t41) {
                                goto gm_lb4;
                            }
                            gm_ttovhfee8mf28 t42 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l16));
                            gm_l7 = t42;
                            void * t43 = gm_l0;
                            gm_b16_8 t44 = gm_l6;
                            uint8_t t45 = GM_LD(uint8_t, ((uint8_t *)&gm_l7));
                            uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                            void * t47 = gm_l0;
                            gm_t88rcucg1b7uk t48 = gm_l3;
                            gm_b16_8 t49 = gm_l5;
                            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
                            unsigned __int128 t51 = gm_f_get_int_pg7p4gc(t47, GM_CONV(gm_csguankko4lmhd, t48), t50);
                            unsigned __int128 t52 = gm_f_num_ext_348kmbu(t45, t46, t51);
                            gm_f_put_uint_cddin99(t43, t44, t52);
                            goto gm_lb3;
                        }
gm_lb4:;
                        void * t53 = gm_l0;
                        gm_b16_8 t54 = gm_l6;
                        void * t55 = gm_l0;
                        gm_t88rcucg1b7uk t56 = gm_l3;
                        gm_b16_8 t57 = gm_l5;
                        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                        unsigned __int128 t59 = gm_f_get_int_pg7p4gc(t55, GM_CONV(gm_csguankko4lmhd, t56), t58);
                        gm_f_put_uint_cddin99(t53, t54, t59);
                    }
gm_lb3:;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t61 = (uint8_t)(t60 == ((uint8_t)1ULL));
                    if (!t61) {
                        goto gm_lb5;
                    }
                    void * t62 = gm_l0;
                    gm_t88rcucg1b7uk t63 = gm_l3;
                    gm_f_kind_htv6093((void *)&gm_l19, t62, GM_CONV(gm_csguankko4lmhd, t63));
                    {
                        {
                            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)3ULL));
                            if (!t65) {
                                goto gm_lb7;
                            }
                            gm_l18 = ((uint8_t)1);
                            goto gm_lb6;
                        }
gm_lb7:;
                        {
                            gm_l18 = ((uint8_t)0);
                            goto gm_lb6;
                        }
gm_lb8:;
                        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 18, 21);
                    }
gm_lb6:;
                    uint8_t t66 = gm_l18;
                    uint8_t t67 = (uint8_t)!t66;
                    if (t67) {
                        gm_l17 = ((uint8_t)1);
                    } else {
                        void * t68 = gm_l0;
                        gm_t88rcucg1b7uk t69 = gm_l4;
                        gm_f_kind_htv6093((void *)&gm_l21, t68, GM_CONV(gm_csguankko4lmhd, t69));
                        {
                            {
                                uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                uint8_t t71 = (uint8_t)(t70 == ((uint8_t)3ULL));
                                if (!t71) {
                                    goto gm_lb10;
                                }
                                gm_l20 = ((uint8_t)1);
                                goto gm_lb9;
                            }
gm_lb10:;
                            {
                                gm_l20 = ((uint8_t)0);
                                goto gm_lb9;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 18, 56);
                        }
gm_lb9:;
                        uint8_t t72 = gm_l20;
                        uint8_t t73 = (uint8_t)!t72;
                        gm_l17 = t73;
                    }
                    uint8_t t74 = gm_l17;
                    if (t74) {
                        void * t75 = gm_l0;
                        gm_tl874t2atnlh3 t76 = gm_l1;
                        gm_b16_8 t77;
                        GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sd4ibvv64si9n));
                        GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(44));
                        gm_t76123gfet73g t78;
                        gm_f_trap_qh84l9d((void *)&t78, (void *)NULL, cx, NULL, t75, t76, t77);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t78);
                        goto gm_fdone;
                    }
                    void * t79 = gm_l0;
                    gm_t88rcucg1b7uk t80 = gm_l4;
                    void * t81 = gm_l0;
                    gm_t88rcucg1b7uk t82 = gm_l3;
                    gm_b16_8 t83 = gm_l5;
                    gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                    double t85 = gm_f_get_float_grmhlm4(t81, GM_CONV(gm_csguankko4lmhd, t82), t84);
                    gm_b16_8 t86 = gm_l6;
                    gm_f_put_float_fiad72d(t79, GM_CONV(gm_csguankko4lmhd, t80), t85, t86);
                    goto gm_lb1;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t87 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t88 = (uint8_t)(t87 == ((uint8_t)2ULL));
                    if (!t88) {
                        goto gm_lb12;
                    }
                    void * t89 = gm_l0;
                    gm_t88rcucg1b7uk t90 = gm_l4;
                    gm_b16_8 t91 = gm_l6;
                    gm_b16_8 t92 = gm_l5;
                    gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                    gm_b0_1 t94 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t89, t90, t91, t93);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb12:;
                {
                    uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t96 = (uint8_t)(t95 == ((uint8_t)3ULL));
                    if (!t96) {
                        goto gm_lb13;
                    }
                    void * t97 = gm_l0;
                    gm_t88rcucg1b7uk t98 = gm_l4;
                    gm_b16_8 t99 = gm_l6;
                    gm_b16_8 t100 = gm_l5;
                    gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)&t100));
                    gm_b0_1 t102 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t97, t98, t99, ((uint8_t)0), t101);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb13:;
                {
                    uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t104 = (uint8_t)(t103 == ((uint8_t)4ULL));
                    if (!t104) {
                        goto gm_lb14;
                    }
                    uint32_t t105 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
                    gm_l8 = t105;
                    void * t106 = gm_l0;
                    gm_t88rcucg1b7uk t107 = gm_l3;
                    gm_b16_8 t108 = gm_l5;
                    gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
                    uint8_t t110 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t106, t107, t109);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    if (t110) {
                        void * t111 = gm_l0;
                        gm_t88rcucg1b7uk t112 = gm_l4;
                        gm_tgglga8ro3isc t113 = gm_f_res_types_pg7rvck(t111, GM_CONV(gm_csguankko4lmhd, t112));
                        gm_l22 = t113;
                        gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l22));
                        gm_l9 = t114;
                        void * t115 = gm_l0;
                        void * t116 = gm_l0;
                        gm_t88rcucg1b7uk t117 = gm_l9;
                        uint64_t t118 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t116, t117);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t119 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t115, t118);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l10 = t119;
                        void * t120 = gm_l0;
                        gm_t88rcucg1b7uk t121 = gm_l9;
                        gm_b16_8 t122 = gm_l10;
                        uint32_t t123 = gm_l8;
                        void * t124 = gm_l0;
                        gm_t88rcucg1b7uk t125 = gm_l3;
                        gm_b16_8 t126 = gm_l5;
                        gm_b16_8 t127 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t124, t125, t126);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
                        gm_b0_1 t129 = gm_f_make_variant_bn3d74n((void *)NULL, cx, NULL, t120, t121, t122, t123, t128);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t130 = gm_l0;
                        gm_t88rcucg1b7uk t131 = gm_l4;
                        gm_b16_8 t132 = gm_l6;
                        gm_b16_8 t133 = gm_l10;
                        gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                        gm_b0_1 t135 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t130, t131, t132, ((uint8_t)1), t134);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    } else {
                        void * t136 = gm_l0;
                        gm_t88rcucg1b7uk t137 = gm_l4;
                        gm_b16_8 t138 = gm_l6;
                        void * t139 = gm_l0;
                        gm_t88rcucg1b7uk t140 = gm_l3;
                        gm_b16_8 t141 = gm_l5;
                        gm_b16_8 t142 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t139, t140, t141);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
                        gm_b0_1 t144 = gm_f_make_res_085nq6f((void *)NULL, cx, NULL, t136, t137, t138, ((uint8_t)0), t143);
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
                    uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t146 = (uint8_t)(t145 == ((uint8_t)7ULL));
                    if (!t146) {
                        goto gm_lb15;
                    }
                    void * t147 = gm_l0;
                    gm_t88rcucg1b7uk t148 = gm_l3;
                    gm_b16_8 t149 = gm_l5;
                    gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&t149));
                    gm_b16_8 t151 = gm_l6;
                    gm_b0_1 t152 = gm_f_array_ptr_to_slice_7dg3m1v((void *)NULL, cx, NULL, t147, t148, t150, t151);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t154 = (uint8_t)(t153 == ((uint8_t)11ULL));
                    if (!t154) {
                        goto gm_lb16;
                    }
                    void * t155 = gm_l0;
                    gm_t88rcucg1b7uk t156 = gm_l3;
                    gm_b16_8 t157 = gm_l5;
                    gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
                    uint8_t t159 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t155, t156, t158);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t160 = (uint8_t)!t159;
                    if (t160) {
                        gm_b16_8 t161 = gm_l6;
                        gm_f_fill_abmf5g5(t161, ((uint8_t)0ULL));
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
                    void * t162 = gm_l0;
                    gm_t88rcucg1b7uk t163 = gm_l3;
                    gm_t88rcucg1b7uk t164 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t162, GM_CONV(gm_csguankko4lmhd, t163)));
                    gm_l11 = t164;
                    void * t165 = gm_l0;
                    gm_t88rcucg1b7uk t166 = gm_l4;
                    gm_t88rcucg1b7uk t167 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t165, GM_CONV(gm_csguankko4lmhd, t166)));
                    gm_l12 = t167;
                    void * t168 = gm_l0;
                    gm_t88rcucg1b7uk t169 = gm_l3;
                    gm_b16_8 t170 = gm_l5;
                    gm_b16_8 t171 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t168, t169, t170);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t171;
                    {
                        {
                            {
                                void * t172 = gm_l0;
                                gm_t88rcucg1b7uk t173 = gm_l11;
                                gm_f_kind_htv6093((void *)&gm_l23, t172, GM_CONV(gm_csguankko4lmhd, t173));
                            }
                            uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                            uint8_t t175 = (uint8_t)(t174 == ((uint8_t)8ULL));
                            if (!t175) {
                                goto gm_lb18;
                            }
                            gm_t88rcucg1b7uk t176 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l23) + 8) + 4));
                            gm_l14 = t176;
                            {
                                void * t177 = gm_l0;
                                gm_t88rcucg1b7uk t178 = gm_l12;
                                gm_f_kind_htv6093((void *)&gm_l25, t177, GM_CONV(gm_csguankko4lmhd, t178));
                            }
                            {
                                {
                                    uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
                                    uint8_t t180 = (uint8_t)(t179 == ((uint8_t)10ULL));
                                    if (!t180) {
                                        goto gm_lb20;
                                    }
                                    gm_l24 = ((uint8_t)1);
                                    goto gm_lb19;
                                }
gm_lb20:;
                                {
                                    gm_l24 = ((uint8_t)0);
                                    goto gm_lb19;
                                }
gm_lb21:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 42, 55);
                            }
gm_lb19:;
                            uint8_t t181 = gm_l24;
                            if (!t181) {
                                goto gm_lb18;
                            }
                            {
                                void * t182 = gm_l0;
                                gm_t88rcucg1b7uk t183 = gm_l14;
                                gm_f_kind_htv6093((void *)&gm_l27, t182, GM_CONV(gm_csguankko4lmhd, t183));
                            }
                            {
                                {
                                    uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                                    uint8_t t185 = (uint8_t)(t184 == ((uint8_t)11ULL));
                                    if (!t185) {
                                        goto gm_lb23;
                                    }
                                    gm_l26 = ((uint8_t)1);
                                    goto gm_lb22;
                                }
gm_lb23:;
                                {
                                    gm_l26 = ((uint8_t)0);
                                    goto gm_lb22;
                                }
gm_lb24:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 42, 90);
                            }
gm_lb22:;
                            uint8_t t186 = gm_l26;
                            if (!t186) {
                                goto gm_lb18;
                            }
                            void * t187 = gm_l0;
                            void * t188 = gm_l0;
                            gm_t88rcucg1b7uk t189 = gm_l12;
                            uint64_t t190 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t188, t189);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t191 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t187, t190);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l15 = t191;
                            void * t192 = gm_l0;
                            gm_t88rcucg1b7uk t193 = gm_l11;
                            gm_b16_8 t194 = gm_l13;
                            gm_b16_8 t195 = GM_LD(gm_b16_8, ((uint8_t *)&t194));
                            gm_b16_8 t196 = gm_l15;
                            gm_b0_1 t197 = gm_f_array_ptr_to_slice_7dg3m1v((void *)NULL, cx, NULL, t192, t193, t195, t196);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t198 = gm_l0;
                            gm_t88rcucg1b7uk t199 = gm_l4;
                            gm_b16_8 t200 = gm_l6;
                            gm_b16_8 t201 = gm_l15;
                            gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&t201));
                            gm_b0_1 t203 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t198, t199, t200, t202);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb17;
                        }
gm_lb18:;
                        void * t204 = gm_l0;
                        gm_t88rcucg1b7uk t205 = gm_l4;
                        gm_b16_8 t206 = gm_l6;
                        gm_b16_8 t207 = gm_l13;
                        gm_b16_8 t208 = GM_LD(gm_b16_8, ((uint8_t *)&t207));
                        gm_b0_1 t209 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t204, t205, t206, t208);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
gm_lb17:;
                    goto gm_lb1;
                }
gm_lb16:;
            }
            {
                {
                    {
                        {
                            uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t211 = (uint8_t)(t210 == ((uint8_t)6ULL));
                            if (!t211) {
                                goto gm_lb27;
                            }
                            goto gm_lb26;
                        }
gm_lb27:;
                        {
                            uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t213 = (uint8_t)(t212 == ((uint8_t)8ULL));
                            if (!t213) {
                                goto gm_lb28;
                            }
                            goto gm_lb26;
                        }
gm_lb28:;
                        {
                            uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t215 = (uint8_t)(t214 == ((uint8_t)10ULL));
                            if (!t215) {
                                goto gm_lb29;
                            }
                            goto gm_lb26;
                        }
gm_lb29:;
                        goto gm_lb25;
                    }
gm_lb26:;
                    gm_b16_8 t216 = gm_l6;
                    gm_b16_8 t217 = gm_l5;
                    gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&t217));
                    gm_f_copy_from_f7j6p05(t216, t218);
                    goto gm_lb1;
                }
gm_lb25:;
                {
                    {
                        {
                            uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t220 = (uint8_t)(t219 == ((uint8_t)5ULL));
                            if (!t220) {
                                goto gm_lb32;
                            }
                            goto gm_lb31;
                        }
gm_lb32:;
                        {
                            uint8_t t221 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t222 = (uint8_t)(t221 == ((uint8_t)9ULL));
                            if (!t222) {
                                goto gm_lb33;
                            }
                            goto gm_lb31;
                        }
gm_lb33:;
                        {
                            uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t224 = (uint8_t)(t223 == ((uint8_t)12ULL));
                            if (!t224) {
                                goto gm_lb34;
                            }
                            goto gm_lb31;
                        }
gm_lb34:;
                        goto gm_lb30;
                    }
gm_lb31:;
                    {
                        void * t225 = gm_l0;
                        gm_tl874t2atnlh3 t226 = gm_l1;
                        gm_b16_8 t227;
                        GM_ST(void *, (uint8_t *)&t227, (void *)(gm_sbjj41cb5lph3));
                        GM_ST(uintptr_t, (uint8_t *)&t227 + 8, (uintptr_t)(59));
                        gm_t76123gfet73g t228;
                        gm_f_trap_qh84l9d((void *)&t228, (void *)NULL, cx, NULL, t225, t226, t227);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t228);
                        goto gm_fdone;
                    }
                }
gm_lb30:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 9, 5);
            }
        }
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
__attribute__((aligned(4))) void gm_f_convert_is6an2k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t8588ma2cr9d7 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_convert_is6an2k F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t8588ma2cr9d7 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* src_ty */
    gm_t88rcucg1b7uk gm_l4; /* dst_ty */
    gm_b16_8 gm_l5; /* b */
    gm_b16_8 gm_l6; /* dst */
    gm_t88rcucg1b7uk gm_l7; /* target */
    uint8_t gm_l8; /* is_float */
    gm_t86h6hhm6d76n gm_l9; /* sv */
    double gm_l10; /* fv */
    gm_ttovhfee8mf28 gm_l11; /* info */
    gm_tcmkompbl09il gm_l12; /* u */
    gm_ttovhfee8mf28 gm_l13; /* info */
    gm_t88rcucg1b7uk gm_l14; /* u */
    gm_ttovhfee8mf28 gm_l15; /* info */
    gm_ttovhfee8mf28 gm_l16; /* tinfo */
    uint8_t gm_l17; /* signed */
    uint32_t gm_l18; /* bits */
    gm_t51b2en4qp02s gm_l19; /* pb */
    uint64_t gm_l20; /* n */
    uint8_t gm_l21; /* finite */
    gm_t86h6hhm6d76n gm_l22; /* n */
    gm_t51b2en4qp02s gm_l23; /* pb */
    uint64_t gm_l24; /* sz */
    uint8_t gm_l25; /* neg */
    gm_t86h6hhm6d76n gm_l26; /* n */
    gm_t86h6hhm6d76n gm_l27; /* n */
    uint16_t gm_l28; /* w */
    double gm_l29; /* x */
    gm_t88rcucg1b7uk gm_l30; /* u */
    gm_ttovhfee8mf28 gm_l31; /* info */
    GM_STACK_CHECK(&F, 3228u);
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
    gm_t53at80pfdus3 gm_l32;
    gm_tg5lql6itufk7 gm_l33;
    unsigned __int128 gm_l34;
    gm_t57euh612ligr gm_l35;
    gm_tg5lql6itufk7 gm_l36;
    gm_tkn42u7na3t81 gm_l37;
    gm_tg5lql6itufk7 gm_l38;
    gm_tabb710d9mq2e gm_l39;
    gm_tabb710d9mq2e gm_l40;
    gm_tg5lql6itufk7 gm_l41;
    uint8_t gm_l42;
    gm_tsp9ul70rnkg6 gm_l43;
    gm_t86h6hhm6d76n gm_l44;
    gm_tsp9ul70rnkg6 gm_l45;
    gm_tsp9ul70rnkg6 gm_l46;
    gm_t53at80pfdus3 gm_l47;
    gm_tkn42u7na3t81 gm_l48;
    gm_tg5lql6itufk7 gm_l49;
    gm_tabb710d9mq2e gm_l50;
    {
        gm_t8588ma2cr9d7 t24 = gm_l2;
        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
        uint8_t t26 = (uint8_t)((t25 == ((uint8_t)5ULL)));
        gm_t8588ma2cr9d7 t27;
        GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t27 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)5ULL));
        if (t26) {
            void * t28 = gm_l0;
            gm_t88rcucg1b7uk t29 = gm_l4;
            gm_t88rcucg1b7uk t30 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t28, GM_CONV(gm_csguankko4lmhd, t29)));
            gm_l7 = t30;
        } else {
            gm_t88rcucg1b7uk t31 = gm_l4;
            gm_l7 = t31;
        }
        gm_l8 = ((uint8_t)0);
        GM_ST(uint8_t, (uint8_t *)&gm_l9 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l9 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l9 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&gm_l9 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&gm_l9 + 0), ((uint8_t)0));
        GM_ST(unsigned __int128, ((uint8_t *)&gm_l9 + 16), ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
        gm_l10 = ((double)0e0);
        void * t32 = gm_l0;
        gm_t88rcucg1b7uk t33 = gm_l3;
        gm_f_kind_htv6093((void *)&gm_l32, t32, GM_CONV(gm_csguankko4lmhd, t33));
        {
            {
                {
                    {
                        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 0));
                        uint8_t t35 = (uint8_t)(t34 == ((uint8_t)0ULL));
                        if (!t35) {
                            goto gm_lb4;
                        }
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 0));
                        uint8_t t37 = (uint8_t)(t36 == ((uint8_t)1ULL));
                        if (!t37) {
                            goto gm_lb5;
                        }
                        goto gm_lb3;
                    }
gm_lb5:;
                    {
                        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 0));
                        uint8_t t39 = (uint8_t)(t38 == ((uint8_t)2ULL));
                        if (!t39) {
                            goto gm_lb6;
                        }
                        goto gm_lb3;
                    }
gm_lb6:;
                    goto gm_lb2;
                }
gm_lb3:;
                void * t40 = gm_l0;
                gm_t88rcucg1b7uk t41 = gm_l3;
                gm_tg5lql6itufk7 t42 = gm_f_int_info_ulcevdv(t40, GM_CONV(gm_csguankko4lmhd, t41));
                gm_l33 = t42;
                uint8_t t43 = (uint8_t)((((uint8_t *)&gm_l33))[8] != 0);
                if (!t43) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 75, 21);
                gm_ttovhfee8mf28 t44 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l33));
                gm_l11 = t44;
                uint8_t t45 = GM_LD(uint8_t, ((uint8_t *)&gm_l11));
                void * t46 = gm_l0;
                gm_t88rcucg1b7uk t47 = gm_l3;
                gm_b16_8 t48 = gm_l5;
                unsigned __int128 t49 = gm_f_get_int_pg7p4gc(t46, GM_CONV(gm_csguankko4lmhd, t47), t48);
                gm_f_num_val_i3upeur((void *)&gm_l9, t45, t49);
                goto gm_lb1;
            }
gm_lb2:;
            {
                uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 0));
                uint8_t t51 = (uint8_t)(t50 == ((uint8_t)3ULL));
                if (!t51) {
                    goto gm_lb7;
                }
                gm_l8 = ((uint8_t)1);
                void * t52 = gm_l0;
                gm_t88rcucg1b7uk t53 = gm_l3;
                gm_b16_8 t54 = gm_l5;
                double t55 = gm_f_get_float_grmhlm4(t52, GM_CONV(gm_csguankko4lmhd, t53), t54);
                gm_l10 = t55;
                goto gm_lb1;
            }
gm_lb7:;
            {
                uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 0));
                uint8_t t57 = (uint8_t)(t56 == ((uint8_t)4ULL));
                if (!t57) {
                    goto gm_lb8;
                }
                void * t58 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t59)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t59, gm_srcl8pn2pjtmj6k, 82, 32);
                uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)t58) + (uintptr_t)((uint64_t)0ULL) * 1));
                uint8_t t61 = ((t60 == ((uint8_t)0ULL)));
                if (t61) {
                    gm_l34 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                } else {
                    gm_l34 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
                }
                unsigned __int128 t62 = gm_l34;
                GM_ST(unsigned __int128, (((uint8_t *)&gm_l9) + 16), t62);
                goto gm_lb1;
            }
gm_lb8:;
            {
                uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 0));
                uint8_t t64 = (uint8_t)(t63 == ((uint8_t)17ULL));
                if (!t64) {
                    goto gm_lb9;
                }
                {
                    {
                        {
                            void * t65 = gm_l0;
                            gm_t88rcucg1b7uk t66 = gm_l3;
                            gm_f_union_def_h8mvf4t((void *)&gm_l35, t65, GM_CONV(gm_csguankko4lmhd, t66));
                        }
                        uint8_t t67 = (uint8_t)((((uint8_t *)&gm_l35))[24] != 0);
                        if (!t67) {
                            goto gm_lb11;
                        }
                        {
                            memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l35), 24);
                        }
                        void * t68 = gm_l0;
                        gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l12));
                        gm_tg5lql6itufk7 t70 = gm_f_int_info_ulcevdv(t68, GM_CONV(gm_csguankko4lmhd, t69));
                        gm_l36 = t70;
                        uint8_t t71 = (uint8_t)((((uint8_t *)&gm_l36))[8] != 0);
                        if (!t71) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 85, 25);
                        gm_ttovhfee8mf28 t72 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l36));
                        gm_l13 = t72;
                        {
                            uint8_t t73 = GM_LD(uint8_t, ((uint8_t *)&gm_l13));
                            void * t74 = gm_l0;
                            gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l12));
                            void * t76 = GM_LD(void *, ((uint8_t *)&gm_l5));
                            uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                            void * t78 = gm_l0;
                            gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l12));
                            uint64_t t80 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t78, t79);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t81 = gm_f_us_cuv01fp(t80);
                            uint64_t t82 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t83 = (uintptr_t)t81;
                            if (t82 > t83 || t83 > (uintptr_t)t77) gm_trap_slice(t82, t83, t77, gm_srcl8pn2pjtmj6k, 86, 60);
                            uint64_t t84 = ((uintptr_t)t81) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t85;
                            GM_ST(void *, (uint8_t *)&t85, (void *)(((uint8_t *)t76 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(t84));
                            unsigned __int128 t86 = gm_f_get_int_pg7p4gc(t74, GM_CONV(gm_csguankko4lmhd, t75), t85);
                            gm_f_num_val_i3upeur((void *)&gm_l9, t73, t86);
                        }
                        goto gm_lb10;
                    }
gm_lb11:;
                    {
                        {
                            void * t87 = gm_l0;
                            gm_t88rcucg1b7uk t88 = gm_l3;
                            gm_tkn42u7na3t81 t89 = gm_f_bits_struct_ty_22ue20s(t87, GM_CONV(gm_csguankko4lmhd, t88));
                            gm_l37 = t89;
                            uint8_t t90 = (uint8_t)((((uint8_t *)&gm_l37))[4] != 0);
                            if (!t90) {
                                goto gm_lb13;
                            }
                            gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l37));
                            gm_l14 = t91;
                            void * t92 = gm_l0;
                            gm_t88rcucg1b7uk t93 = gm_l14;
                            gm_tg5lql6itufk7 t94 = gm_f_int_info_ulcevdv(t92, GM_CONV(gm_csguankko4lmhd, t93));
                            gm_l38 = t94;
                            uint8_t t95 = (uint8_t)((((uint8_t *)&gm_l38))[8] != 0);
                            if (!t95) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 88, 25);
                            gm_ttovhfee8mf28 t96 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l38));
                            gm_l15 = t96;
                            void * t97 = gm_l0;
                            void * t98 = GM_LD(void *, ((uint8_t *)&gm_l5));
                            uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                            void * t100 = gm_l0;
                            gm_t88rcucg1b7uk t101 = gm_l14;
                            uint64_t t102 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t100, t101);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t103 = gm_f_us_cuv01fp(t102);
                            uint64_t t104 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t105 = (uintptr_t)t103;
                            if (t104 > t105 || t105 > (uintptr_t)t99) gm_trap_slice(t104, t105, t99, gm_srcl8pn2pjtmj6k, 89, 57);
                            uint64_t t106 = ((uintptr_t)t103) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t107;
                            GM_ST(void *, (uint8_t *)&t107, (void *)(((uint8_t *)t98 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(t106));
                            unsigned __int128 t108 = gm_f_get_uint_bq8c4b7(t97, t107);
                            uint32_t t109 = GM_LD(uint32_t, (((uint8_t *)&gm_l15) + 4));
                            unsigned __int128 t110 = gm_f_num_mask_do3naah(t109);
                            unsigned __int128 t111 = (unsigned __int128)(t108 & t110);
                            GM_ST(uint8_t, (uint8_t *)&gm_l9 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&gm_l9 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&gm_l9 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&gm_l9 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&gm_l9 + 0), ((uint8_t)0));
                            GM_ST(unsigned __int128, ((uint8_t *)&gm_l9 + 16), t111);
                            goto gm_lb12;
                        }
gm_lb13:;
                        {
                            void * t112 = gm_l0;
                            gm_tl874t2atnlh3 t113 = gm_l1;
                            void * t114 = gm_l0;
                            void * t115 = GM_LD(void *, (((uint8_t *)t114) + 8));
                            gm_t88rcucg1b7uk t116 = gm_l3;
                            gm_b16_8 t117 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t115, t116);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t118;
                            GM_ST(gm_b16_8, ((uint8_t *)&t118 + 0), t117);
                            gm_f_format_pe0ujmq((void *)&gm_l39, (void *)NULL, cx, NULL, t118);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                            if (!t119) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_srcl8pn2pjtmj6k, 91, 38);
                            gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
                            gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                            gm_t76123gfet73g t122;
                            gm_f_trap_qh84l9d((void *)&t122, (void *)NULL, cx, NULL, t112, t113, t121);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t122);
                            goto gm_fdone;
                        }
                    }
gm_lb12:;
                }
gm_lb10:;
                goto gm_lb1;
            }
gm_lb9:;
            {
                void * t123 = gm_l0;
                gm_tl874t2atnlh3 t124 = gm_l1;
                void * t125 = gm_l0;
                void * t126 = GM_LD(void *, (((uint8_t *)t125) + 8));
                gm_t88rcucg1b7uk t127 = gm_l3;
                gm_b16_8 t128 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t126, t127);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t129;
                GM_ST(gm_b16_8, ((uint8_t *)&t129 + 0), t128);
                gm_f_format_pe0ujmq((void *)&gm_l40, (void *)NULL, cx, NULL, t129);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t130 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                if (!t130) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 8, gm_srcl8pn2pjtmj6k, 94, 35);
                gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l40) + 8));
                gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
                gm_t76123gfet73g t133;
                gm_f_trap_qh84l9d((void *)&t133, (void *)NULL, cx, NULL, t123, t124, t132);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t133);
                goto gm_fdone;
            }
gm_lb14:;
            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 73, 5);
        }
gm_lb1:;
    }
    {
        {
            {
                void * t134 = gm_l0;
                gm_t88rcucg1b7uk t135 = gm_l7;
                gm_tg5lql6itufk7 t136 = gm_f_int_info_ulcevdv(t134, GM_CONV(gm_csguankko4lmhd, t135));
                gm_l41 = t136;
                uint8_t t137 = (uint8_t)((((uint8_t *)&gm_l41))[8] != 0);
                if (!t137) {
                    goto gm_lb16;
                }
                gm_ttovhfee8mf28 t138 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l41));
                gm_l16 = t138;
                uint8_t t139 = GM_LD(uint8_t, ((uint8_t *)&gm_l16));
                gm_l17 = t139;
                uint32_t t140 = GM_LD(uint32_t, (((uint8_t *)&gm_l16) + 4));
                gm_l18 = t140;
                uint8_t t141 = gm_l8;
                uint8_t t142 = (uint8_t)!t141;
                if (t142) {
                    gm_t8588ma2cr9d7 t143 = gm_l2;
                    uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&t143) + 0));
                    uint8_t t145 = (uint8_t)((t144 == ((uint8_t)5ULL)));
                    gm_t8588ma2cr9d7 t146;
                    GM_ST(uint8_t, (uint8_t *)&t146 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t146 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t146 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t146 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t146 + 0), ((uint8_t)5ULL));
                    if (t145) {
                        uint8_t t147 = gm_l17;
                        uint32_t t148 = gm_l18;
                        uint8_t t149 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t147, t148);
                        if (t149) {
                            void * t150 = gm_l0;
                            gm_t88rcucg1b7uk t151 = gm_l7;
                            uint64_t t152 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t150, t151);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t153 = gm_f_us_cuv01fp(t152);
                            gm_l20 = t153;
                            void * t154 = gm_l0;
                            void * t155 = (void *)((uint8_t *)&gm_l19);
                            uint64_t t156 = gm_l20;
                            uint64_t t157 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t158 = (uintptr_t)t156;
                            if (t157 > t158 || t158 > (uintptr_t)((uint64_t)16ULL)) gm_trap_slice(t157, t158, ((uint64_t)16ULL), gm_srcl8pn2pjtmj6k, 104, 32);
                            uint64_t t159 = ((uintptr_t)t156) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t160;
                            GM_ST(void *, (uint8_t *)&t160, (void *)(((uint8_t *)t155 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(t159));
                            uint8_t t161 = gm_l17;
                            uint32_t t162 = gm_l18;
                            unsigned __int128 t163 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                            unsigned __int128 t164 = gm_f_num_ext_348kmbu(t161, t162, t163);
                            gm_f_put_uint_cddin99(t154, t160, t164);
                            void * t165 = gm_l0;
                            gm_t88rcucg1b7uk t166 = gm_l4;
                            gm_b16_8 t167 = gm_l6;
                            void * t168 = (void *)((uint8_t *)&gm_l19);
                            uint64_t t169 = gm_l20;
                            uint64_t t170 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t171 = (uintptr_t)t169;
                            if (t170 > t171 || t171 > (uintptr_t)((uint64_t)16ULL)) gm_trap_slice(t170, t171, ((uint64_t)16ULL), gm_srcl8pn2pjtmj6k, 105, 46);
                            uint64_t t172 = ((uintptr_t)t169) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t173;
                            GM_ST(void *, (uint8_t *)&t173, (void *)(((uint8_t *)t168 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(t172));
                            gm_b16_8 t174 = GM_LD(gm_b16_8, ((uint8_t *)&t173));
                            gm_b0_1 t175 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t165, t166, t167, t174);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        } else {
                            gm_b16_8 t176 = gm_l6;
                            gm_f_fill_abmf5g5(t176, ((uint8_t)0ULL));
                        }
                    } else {
                        gm_t8588ma2cr9d7 t177 = gm_l2;
                        uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&t177) + 0));
                        uint8_t t179 = (uint8_t)((t178 == ((uint8_t)6ULL)));
                        gm_t8588ma2cr9d7 t180;
                        GM_ST(uint8_t, (uint8_t *)&t180 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t180 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t180 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t180 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t180 + 0), ((uint8_t)6ULL));
                        if (t179) {
                            void * t181 = gm_l0;
                            gm_b16_8 t182 = gm_l6;
                            uint8_t t183 = gm_l17;
                            uint32_t t184 = gm_l18;
                            uint8_t t185 = gm_l17;
                            uint32_t t186 = gm_l18;
                            gm_t86h6hhm6d76n t187;
                            gm_f_num_sat_66dqcpl((void *)&t187, &(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t185, t186);
                            unsigned __int128 t188 = gm_f_num_bits_onl28vp(&t187);
                            unsigned __int128 t189 = gm_f_num_ext_348kmbu(t183, t184, t188);
                            gm_f_put_uint_cddin99(t181, t182, t189);
                        } else {
                            void * t190 = gm_l0;
                            gm_b16_8 t191 = gm_l6;
                            uint8_t t192 = gm_l17;
                            uint32_t t193 = gm_l18;
                            unsigned __int128 t194 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                            unsigned __int128 t195 = gm_f_num_ext_348kmbu(t192, t193, t194);
                            gm_f_put_uint_cddin99(t190, t191, t195);
                        }
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
                double t196 = gm_l10;
                double t197 = gm_l10;
                double t198 = (t196 - t197);
                uint8_t t199 = ((t198 == ((double)0e0)));
                gm_l21 = t199;
                gm_t8588ma2cr9d7 t200 = gm_l2;
                uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&t200) + 0));
                uint8_t t202 = (uint8_t)((t201 == ((uint8_t)5ULL)));
                gm_t8588ma2cr9d7 t203;
                GM_ST(uint8_t, (uint8_t *)&t203 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t203 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t203 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t203 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t203 + 0), ((uint8_t)5ULL));
                if (t202) {
                    gm_b16_8 t204 = gm_l6;
                    gm_f_fill_abmf5g5(t204, ((uint8_t)0ULL));
                    uint8_t t205 = gm_l21;
                    uint8_t t206 = (uint8_t)!t205;
                    if (t206) {
                        gm_l42 = ((uint8_t)1);
                    } else {
                        double t207 = gm_l10;
                        uint8_t t208 = gm_f_num_integral_qbts4a6(t207);
                        uint8_t t209 = (uint8_t)!t208;
                        gm_l42 = t209;
                    }
                    uint8_t t210 = gm_l42;
                    if (t210) {
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
                    {
                        double t211 = gm_l10;
                        gm_t8588ma2cr9d7 t212;
                        GM_ST(uint8_t, (uint8_t *)&t212 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t212 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t212 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t212 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t212 + 0), ((uint8_t)1ULL));
                        gm_f_num_float_int_ba5kc1t((void *)&gm_l43, t211, t212);
                    }
                    uint8_t t213 = (uint8_t)((((uint8_t *)&gm_l43))[32] != 0);
                    if (t213) {
                        memmove(((uint8_t *)&gm_l22), ((uint8_t *)&gm_l43), 32);
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
                    {
                        uint8_t t214 = gm_l17;
                        uint32_t t215 = gm_l18;
                        uint8_t t216 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l22)), t214, t215);
                        uint8_t t217 = (uint8_t)!t216;
                        if (t217) {
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
                    }
                    void * t218 = gm_l0;
                    gm_t88rcucg1b7uk t219 = gm_l7;
                    uint64_t t220 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t218, t219);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t221 = gm_f_us_cuv01fp(t220);
                    gm_l24 = t221;
                    {
                        void * t222 = gm_l0;
                        void * t223 = (void *)((uint8_t *)&gm_l23);
                        uint64_t t224 = gm_l24;
                        uint64_t t225 = (uintptr_t)((uint64_t)0ULL);
                        uint64_t t226 = (uintptr_t)t224;
                        if (t225 > t226 || t226 > (uintptr_t)((uint64_t)16ULL)) gm_trap_slice(t225, t226, ((uint64_t)16ULL), gm_srcl8pn2pjtmj6k, 124, 24);
                        uint64_t t227 = ((uintptr_t)t224) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t228;
                        GM_ST(void *, (uint8_t *)&t228, (void *)(((uint8_t *)t223 + (uintptr_t)((uint64_t)0ULL) * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t228 + 8, (uintptr_t)(t227));
                        uint8_t t229 = gm_l17;
                        uint32_t t230 = gm_l18;
                        unsigned __int128 t231 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l22)));
                        unsigned __int128 t232 = gm_f_num_ext_348kmbu(t229, t230, t231);
                        gm_f_put_uint_cddin99(t222, t228, t232);
                    }
                    void * t233 = gm_l0;
                    gm_t88rcucg1b7uk t234 = gm_l4;
                    gm_b16_8 t235 = gm_l6;
                    void * t236 = (void *)((uint8_t *)&gm_l23);
                    uint64_t t237 = gm_l24;
                    uint64_t t238 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t239 = (uintptr_t)t237;
                    if (t238 > t239 || t239 > (uintptr_t)((uint64_t)16ULL)) gm_trap_slice(t238, t239, ((uint64_t)16ULL), gm_srcl8pn2pjtmj6k, 125, 38);
                    uint64_t t240 = ((uintptr_t)t237) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t241;
                    GM_ST(void *, (uint8_t *)&t241, (void *)(((uint8_t *)t236 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(t240));
                    gm_b16_8 t242 = GM_LD(gm_b16_8, ((uint8_t *)&t241));
                    gm_b0_1 t243 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t233, t234, t235, t242);
                    if (gm_unw.kind) {
                        goto gm_lad;
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
                }
                gm_t8588ma2cr9d7 t244 = gm_l2;
                uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&t244) + 0));
                uint8_t t246 = (uint8_t)((t245 == ((uint8_t)6ULL)));
                gm_t8588ma2cr9d7 t247;
                GM_ST(uint8_t, (uint8_t *)&t247 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t247 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t247 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t247 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t247 + 0), ((uint8_t)6ULL));
                if (t246) {
                    double t248 = gm_l10;
                    uint8_t t249 = gm_f_num_is_nan_b0ma7mo(t248);
                    if (t249) {
                        gm_b16_8 t250 = gm_l6;
                        gm_f_fill_abmf5g5(t250, ((uint8_t)0ULL));
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
                    double t251 = gm_l10;
                    uint64_t t252;
                    memset(&t252, 0, sizeof t252); memcpy(&t252, ((uint8_t *)&t251), sizeof t252 < 8 ? sizeof t252 : 8);
                    unsigned __int128 n253 = (unsigned __int128)((uint64_t)63ULL);
                    if (n253 >= 64) gm_trap_shift((uint64_t)n253, "u64", gm_srcl8pn2pjtmj6k, 133, 21);
                    uint64_t t254 = (uint64_t)(t252 >> (int)n253);
                    uint8_t t255 = ((t254 == ((uint64_t)1ULL)));
                    gm_l25 = t255;
                    {
                        uint8_t t256 = gm_l25;
                        unsigned __int128 t257 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
                        GM_ST(uint8_t, (uint8_t *)&gm_l26 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l26 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l26 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&gm_l26 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&gm_l26 + 0), t256);
                        GM_ST(unsigned __int128, ((uint8_t *)&gm_l26 + 16), t257);
                    }
                    uint8_t t258 = gm_l21;
                    if (t258) {
                        {
                            double t259 = gm_l10;
                            gm_t8588ma2cr9d7 t260;
                            GM_ST(uint8_t, (uint8_t *)&t260 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t260 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t260 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t260 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t260 + 0), ((uint8_t)1ULL));
                            gm_f_num_float_int_ba5kc1t((void *)&gm_l45, t259, t260);
                        }
                        uint8_t t261 = (uint8_t)((((uint8_t *)&gm_l45))[32] != 0);
                        if (t261) {
                            memmove(((uint8_t *)&gm_l44), ((uint8_t *)&gm_l45), 32);
                        } else {
                            memmove(((uint8_t *)&gm_l44), ((uint8_t *)&gm_l26), 32);
                        }
                        {
                            memmove(((uint8_t *)&gm_l26), ((uint8_t *)&gm_l44), 32);
                        }
                    }
                    {
                        void * t262 = gm_l0;
                        gm_b16_8 t263 = gm_l6;
                        uint8_t t264 = gm_l17;
                        uint32_t t265 = gm_l18;
                        uint8_t t266 = gm_l17;
                        uint32_t t267 = gm_l18;
                        gm_t86h6hhm6d76n t268;
                        gm_f_num_sat_66dqcpl((void *)&t268, &(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l26)), t266, t267);
                        unsigned __int128 t269 = gm_f_num_bits_onl28vp(&t268);
                        unsigned __int128 t270 = gm_f_num_ext_348kmbu(t264, t265, t269);
                        gm_f_put_uint_cddin99(t262, t263, t270);
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
                }
                uint8_t t271 = gm_l21;
                if (t271) {
                    {
                        {
                            {
                                double t272 = gm_l10;
                                gm_t8588ma2cr9d7 t273 = gm_l2;
                                gm_f_num_float_int_ba5kc1t((void *)&gm_l46, t272, t273);
                            }
                            uint8_t t274 = (uint8_t)((((uint8_t *)&gm_l46))[32] != 0);
                            if (!t274) {
                                goto gm_lb18;
                            }
                            {
                                memmove(((uint8_t *)&gm_l27), ((uint8_t *)&gm_l46), 32);
                            }
                            {
                                uint8_t t275 = gm_l17;
                                uint32_t t276 = gm_l18;
                                uint8_t t277 = gm_f_num_fits_1n7pcsh(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l27)), t275, t276);
                                if (t277) {
                                    {
                                        void * t278 = gm_l0;
                                        gm_b16_8 t279 = gm_l6;
                                        uint8_t t280 = gm_l17;
                                        uint32_t t281 = gm_l18;
                                        unsigned __int128 t282 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l27)));
                                        unsigned __int128 t283 = gm_f_num_ext_348kmbu(t280, t281, t282);
                                        gm_f_put_uint_cddin99(t278, t279, t283);
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
                                }
                            }
                            goto gm_lb17;
                        }
gm_lb18:;
                    }
gm_lb17:;
                }
                void * t284 = gm_l0;
                uint8_t t285 = gm_f_fast_23em7sv(t284);
                if (t285) {
                    gm_b16_8 t286 = gm_l6;
                    gm_f_fill_abmf5g5(t286, ((uint8_t)0ULL));
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
                {
                    void * t287 = gm_l0;
                    gm_tl874t2atnlh3 t288 = gm_l1;
                    gm_b16_8 t289;
                    GM_ST(void *, (uint8_t *)&t289, (void *)(gm_s9o2c36nosv54));
                    GM_ST(uintptr_t, (uint8_t *)&t289 + 8, (uintptr_t)(40));
                    gm_t76123gfet73g t290;
                    gm_f_trap_qh84l9d((void *)&t290, (void *)NULL, cx, NULL, t287, t288, t289);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t290);
                    goto gm_fdone;
                }
            }
gm_lb16:;
        }
gm_lb15:;
    }
    {
        {
            {
                {
                    void * t291 = gm_l0;
                    gm_t88rcucg1b7uk t292 = gm_l7;
                    gm_f_kind_htv6093((void *)&gm_l47, t291, GM_CONV(gm_csguankko4lmhd, t292));
                }
                uint8_t t293 = GM_LD(uint8_t, (((uint8_t *)&gm_l47) + 0));
                uint8_t t294 = (uint8_t)(t293 == ((uint8_t)3ULL));
                if (!t294) {
                    goto gm_lb20;
                }
                uint16_t t295 = GM_LD(uint16_t, (((uint8_t *)&gm_l47) + 8));
                gm_l28 = t295;
                uint8_t t296 = gm_l8;
                if (t296) {
                    double t297 = gm_l10;
                    uint16_t t298 = gm_l28;
                    double t299 = gm_f_num_round_2pna7m3(t297, t298);
                    gm_l29 = t299;
                } else {
                    uint16_t t300 = gm_l28;
                    double t301 = gm_f_num_int_to_float_4hrvis6(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)), t300);
                    gm_l29 = t301;
                }
                void * t302 = gm_l0;
                gm_t88rcucg1b7uk t303 = gm_l7;
                double t304 = gm_l29;
                gm_b16_8 t305 = gm_l6;
                gm_f_put_float_fiad72d(t302, GM_CONV(gm_csguankko4lmhd, t303), t304, t305);
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
            }
gm_lb20:;
        }
gm_lb19:;
        {
            {
                void * t306 = gm_l0;
                gm_t88rcucg1b7uk t307 = gm_l7;
                gm_tkn42u7na3t81 t308 = gm_f_bits_struct_ty_22ue20s(t306, GM_CONV(gm_csguankko4lmhd, t307));
                gm_l48 = t308;
                uint8_t t309 = (uint8_t)((((uint8_t *)&gm_l48))[4] != 0);
                if (!t309) {
                    goto gm_lb22;
                }
                gm_t88rcucg1b7uk t310 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l48));
                gm_l30 = t310;
                uint8_t t311 = gm_l8;
                uint8_t t312 = (uint8_t)!t311;
                if (t312) {
                    void * t313 = gm_l0;
                    gm_t88rcucg1b7uk t314 = gm_l30;
                    gm_tg5lql6itufk7 t315 = gm_f_int_info_ulcevdv(t313, GM_CONV(gm_csguankko4lmhd, t314));
                    gm_l49 = t315;
                    uint8_t t316 = (uint8_t)((((uint8_t *)&gm_l49))[8] != 0);
                    if (!t316) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 160, 21);
                    gm_ttovhfee8mf28 t317 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l49));
                    gm_l31 = t317;
                    {
                        void * t318 = gm_l0;
                        gm_b16_8 t319 = gm_l6;
                        uint32_t t320 = GM_LD(uint32_t, (((uint8_t *)&gm_l31) + 4));
                        unsigned __int128 t321 = gm_f_num_bits_onl28vp(&(*(gm_t86h6hhm6d76n *)((uint8_t *)&gm_l9)));
                        unsigned __int128 t322 = gm_f_num_ext_348kmbu(((uint8_t)0), t320, t321);
                        gm_f_put_uint_cddin99(t318, t319, t322);
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
                }
                goto gm_lb21;
            }
gm_lb22:;
        }
gm_lb21:;
    }
    {
        void * t323 = gm_l0;
        gm_tl874t2atnlh3 t324 = gm_l1;
        void * t325 = gm_l0;
        void * t326 = GM_LD(void *, (((uint8_t *)t325) + 8));
        gm_t88rcucg1b7uk t327 = gm_l7;
        gm_b16_8 t328 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t326, t327);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t329;
        GM_ST(gm_b16_8, ((uint8_t *)&t329 + 0), t328);
        gm_f_format_fv5tfj6((void *)&gm_l50, (void *)NULL, cx, NULL, t329);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t330 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
        if (!t330) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 8, gm_srcl8pn2pjtmj6k, 165, 26);
        gm_b16_8 t331 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 8));
        gm_b16_8 t332 = GM_LD(gm_b16_8, ((uint8_t *)&t331));
        gm_t76123gfet73g t333;
        gm_f_trap_qh84l9d((void *)&t333, (void *)NULL, cx, NULL, t323, t324, t332);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t333);
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
inline __attribute__((aligned(4))) void gm_f_hash_bytes_2qnf0cp(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hash_bytes_2qnf0cp F;
    void * gm_l0; /* h */
    gm_b16_8 gm_l1; /* bytes */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* w */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 283u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l2;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                gm_l3 = ((uint64_t)0ULL);
                gm_l5 = ((uint64_t)0ULL);
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t14 = gm_l2;
                uint64_t r15; bool o16 = __builtin_sub_overflow(t13, t14, &r15);
                if (o16) gm_trap("integer overflow in `-`", gm_srcl8pn2pjtmj6k, 416, 22);
                uint64_t t17 = r15;
                uint64_t t18 = (t17 < ((uint64_t)8ULL) ? t17 : ((uint64_t)8ULL));
                gm_l4 = t18;
                {
                    for (;;) {
                        uint64_t t19 = gm_l5;
                        uint64_t t20 = gm_l4;
                        uint8_t t21 = (t19 < t20);
                        if (!t21) {
                            goto gm_lb4;
                        }
                        {
                            {
                                {
                                    uint64_t t22 = gm_l5;
                                    gm_l6 = t22;
                                    uint64_t t23 = gm_l3;
                                    void * t24 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                    uint64_t t26 = gm_l2;
                                    uint64_t t27 = gm_l6;
                                    uint64_t r28; bool o29 = __builtin_add_overflow(t26, t27, &r28);
                                    if (o29) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 416, 61);
                                    uint64_t t30 = r28;
                                    if ((uintptr_t)t30 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t30, t25, gm_srcl8pn2pjtmj6k, 416, 55);
                                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t24) + (uintptr_t)t30 * 1));
                                    uint64_t t32 = (uint64_t)t31;
                                    uint64_t t33 = gm_l6;
                                    uint64_t r34; bool o35 = __builtin_mul_overflow(t33, ((uint64_t)8ULL), &r34);
                                    if (o35) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 416, 76);
                                    uint64_t t36 = r34;
                                    uint64_t t37 = (uint64_t)t36;
                                    unsigned __int128 n38 = (unsigned __int128)t37;
                                    if (n38 >= 64) gm_trap_shift((uint64_t)n38, "u64", gm_srcl8pn2pjtmj6k, 416, 51);
                                    uint64_t r39 = (uint64_t)((uint64_t)t32 << (int)n38);
                                    bool o40 = (uint64_t)(r39 >> (int)n38) != t32;
                                    if (o40) gm_trap("integer overflow in `<<`", gm_srcl8pn2pjtmj6k, 416, 51);
                                    uint64_t t41 = r39;
                                    uint64_t t42 = (uint64_t)(t23 | t41);
                                    gm_l3 = t42;
                                    goto gm_lb7;
                                }
gm_lb8:;
                                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 416, 9);
                            }
gm_lb7:;
                        }
gm_lb6:;
                        uint64_t t43 = gm_l5;
                        uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                        uint64_t t46 = r44;
                        gm_l5 = t46;
                    }
                }
gm_lb4:;
                void * t47 = gm_l0;
                uint64_t t48 = gm_l3;
                gm_f_hash_u64_32bripn(t47, t48);
                uint64_t t49 = gm_l2;
                uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)8ULL), &r50);
                if (o51) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 418, 9);
                uint64_t t52 = r50;
                gm_l2 = t52;
            }
gm_lb3:;
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
extern __attribute__((aligned(4))) void gm_f_hash_int_26mf5j0(void * a0, const void * a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_hash_int_26mf5j0_c(void * a0, gm_csia7oevr2u0fk a1, uint32_t a2) { gm_f_hash_int_26mf5j0(a0, &a1, a2); }
extern __attribute__((aligned(4))) void gm_f_hash_u64_32bripn(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_hash_val_nr2e99r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_hash_val_nr2e99r F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    void * gm_l4; /* h */
    gm_ttovhfee8mf28 gm_l5; /* info */
    double gm_l6; /* x */
    gm_b16_8 gm_l7; /* elems */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint64_t gm_l11; /* n */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    uint64_t gm_l13; /* esz */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t88rcucg1b7uk gm_l21; /* inner */
    gm_t88rcucg1b7uk gm_l22; /* err */
    gm_t88rcucg1b7uk gm_l23; /* ok */
    uint8_t gm_l24; /* is_err */
    gm_t88rcucg1b7uk gm_l25; /* u */
    gm_tcmkompbl09il gm_l26; /* u */
    uint32_t gm_l27; /* vi */
    gm_t88rcucg1b7uk gm_l28; /* pt */
    uint64_t gm_l29; /* off */
    void * gm_l30; /* a */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l34; /* i */
    gm_tfbnciphf3261 gm_l35; /* fl */
    uint64_t gm_l36; /* off */
    GM_STACK_CHECK(&F, 4020u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t53at80pfdus3 gm_l37;
    gm_tg5lql6itufk7 gm_l38;
    uint64_t gm_l39;
    gm_tcfa6l7reoosn gm_l40;
    gm_tcfa6l7reoosn gm_l41;
    gm_tmc91gv7n9c0i gm_l42;
    gm_tcfa6l7reoosn gm_l43;
    gm_tcfa6l7reoosn gm_l44;
    uint64_t gm_l45;
    gm_t88rcucg1b7uk gm_l46;
    gm_tcfa6l7reoosn gm_l47;
    gm_tkn42u7na3t81 gm_l48;
    gm_tcfa6l7reoosn gm_l49;
    gm_t57euh612ligr gm_l50;
    gm_t9805qtonbsag gm_l51;
    gm_tcfa6l7reoosn gm_l52;
    void * gm_l53;
    gm_tabb710d9mq2e gm_l54;
    gm_tcfa6l7reoosn gm_l55;
    gm_tabb710d9mq2e gm_l56;
    gm_tabb710d9mq2e gm_l57;
    {
        void * t52 = gm_l0;
        gm_t88rcucg1b7uk t53 = gm_l2;
        gm_f_kind_htv6093((void *)&gm_l37, t52, GM_CONV(gm_csguankko4lmhd, t53));
        {
            {
                {
                    {
                        {
                            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)0ULL));
                            if (!t55) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)1ULL));
                            if (!t57) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t59 = (uint8_t)(t58 == ((uint8_t)2ULL));
                            if (!t59) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    void * t60 = gm_l0;
                    gm_t88rcucg1b7uk t61 = gm_l2;
                    gm_tg5lql6itufk7 t62 = gm_f_int_info_ulcevdv(t60, GM_CONV(gm_csguankko4lmhd, t61));
                    gm_l38 = t62;
                    uint8_t t63 = (uint8_t)((((uint8_t *)&gm_l38))[8] != 0);
                    if (!t63) gm_trap("unwrap of null", gm_srcl8pn2pjtmj6k, 434, 21);
                    gm_ttovhfee8mf28 t64 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l38));
                    gm_l5 = t64;
                    void * t65 = gm_l4;
                    uint8_t t66 = GM_LD(uint8_t, ((uint8_t *)&gm_l5));
                    void * t67 = gm_l0;
                    gm_t88rcucg1b7uk t68 = gm_l2;
                    gm_b16_8 t69 = gm_l3;
                    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                    unsigned __int128 t71 = gm_f_get_int_pg7p4gc(t67, GM_CONV(gm_csguankko4lmhd, t68), t70);
                    gm_t86h6hhm6d76n t72;
                    gm_f_num_val_i3upeur((void *)&t72, t66, t71);
                    uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                    gm_f_hash_int_26mf5j0(t65, &t72, t73);
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t75 = (uint8_t)(t74 == ((uint8_t)3ULL));
                    if (!t75) {
                        goto gm_lb7;
                    }
                    void * t76 = gm_l0;
                    gm_t88rcucg1b7uk t77 = gm_l2;
                    gm_b16_8 t78 = gm_l3;
                    gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                    double t80 = gm_f_get_float_grmhlm4(t76, GM_CONV(gm_csguankko4lmhd, t77), t79);
                    gm_l6 = t80;
                    double t81 = gm_l6;
                    uint8_t t82 = ((t81 == ((double)0e0)));
                    if (t82) {
                        gm_l6 = ((double)0e0);
                    }
                    void * t83 = gm_l4;
                    double t84 = gm_l6;
                    uint64_t t85;
                    memset(&t85, 0, sizeof t85); memcpy(&t85, ((uint8_t *)&t84), sizeof t85 < 8 ? sizeof t85 : 8);
                    gm_f_hash_u64_32bripn(t83, t85);
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t87 = (uint8_t)(t86 == ((uint8_t)4ULL));
                    if (!t87) {
                        goto gm_lb8;
                    }
                    void * t88 = gm_l4;
                    void * t89 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t90)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t90, gm_srcl8pn2pjtmj6k, 442, 33);
                    uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)t89) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t92 = ((t91 == ((uint8_t)0ULL)));
                    if (t92) {
                        gm_l39 = ((uint64_t)0ULL);
                    } else {
                        gm_l39 = ((uint64_t)1ULL);
                    }
                    uint64_t t93 = gm_l39;
                    gm_f_hash_u64_32bripn(t88, t93);
                    goto gm_lb1;
                }
gm_lb8:;
            }
            {
                {
                    {
                        {
                            uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t95 = (uint8_t)(t94 == ((uint8_t)8ULL));
                            if (!t95) {
                                goto gm_lb11;
                            }
                            goto gm_lb10;
                        }
gm_lb11:;
                        {
                            uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t97 = (uint8_t)(t96 == ((uint8_t)9ULL));
                            if (!t97) {
                                goto gm_lb12;
                            }
                            goto gm_lb10;
                        }
gm_lb12:;
                        {
                            uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t99 = (uint8_t)(t98 == ((uint8_t)7ULL));
                            if (!t99) {
                                goto gm_lb13;
                            }
                            goto gm_lb10;
                        }
gm_lb13:;
                        {
                            uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                            uint8_t t101 = (uint8_t)(t100 == ((uint8_t)16ULL));
                            if (!t101) {
                                goto gm_lb14;
                            }
                            goto gm_lb10;
                        }
gm_lb14:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    void * t102 = gm_l4;
                    void * t103 = gm_l0;
                    gm_b16_8 t104 = gm_l3;
                    gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                    uint64_t t106 = gm_f_ptr_of_b77b4c9(t103, t105);
                    gm_f_hash_u64_32bripn(t102, t106);
                    goto gm_lb1;
                }
gm_lb9:;
                {
                    uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t108 = (uint8_t)(t107 == ((uint8_t)5ULL));
                    if (!t108) {
                        goto gm_lb15;
                    }
                    goto gm_lb1;
                }
gm_lb15:;
                {
                    uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t110 = (uint8_t)(t109 == ((uint8_t)12ULL));
                    if (!t110) {
                        goto gm_lb16;
                    }
                    gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                    gm_l7 = t111;
                    gm_l9 = ((uint64_t)0ULL);
                    uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    gm_l8 = t112;
                    {
                        for (;;) {
                            uint64_t t113 = gm_l9;
                            uint64_t t114 = gm_l8;
                            uint8_t t115 = (t113 < t114);
                            if (!t115) {
                                goto gm_lb17;
                            }
                            {
                                {
                                    {
                                        uint64_t t116 = gm_l9;
                                        gm_l10 = t116;
                                        void * t117 = gm_l0;
                                        gm_tl874t2atnlh3 t118 = gm_l1;
                                        void * t119 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                        uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                        uint64_t t121 = gm_l10;
                                        gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t119) + (uintptr_t)t121 * 4));
                                        void * t123 = gm_l0;
                                        gm_t88rcucg1b7uk t124 = gm_l2;
                                        gm_b16_8 t125 = gm_l3;
                                        uint64_t t126 = gm_l10;
                                        uint32_t t127 = (uint32_t)t126;
                                        gm_b16_8 t128 = gm_f_field_bytes_bhivr4s((void *)NULL, cx, NULL, t123, t124, t125, t127);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t129 = gm_l4;
                                        gm_f_hash_val_nr2e99r((void *)&gm_l40, (void *)NULL, cx, NULL, t117, t118, t122, t128, t129);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t130 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                        if (t130) {
                                        } else {
                                            gm_t76123gfet73g t131 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l40) + 8));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t131);
                                            goto gm_fdone;
                                        }
                                        goto gm_lb20;
                                    }
gm_lb21:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 446, 13);
                                }
gm_lb20:;
                            }
gm_lb19:;
                            uint64_t t132 = gm_l9;
                            uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                            uint64_t t135 = r133;
                            gm_l9 = t135;
                        }
                    }
gm_lb17:;
                    goto gm_lb1;
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t137 = (uint8_t)(t136 == ((uint8_t)11ULL));
                    if (!t137) {
                        goto gm_lb22;
                    }
                    uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                    gm_l11 = t138;
                    gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l37) + 8) + 8));
                    gm_l12 = t139;
                    void * t140 = gm_l0;
                    gm_t88rcucg1b7uk t141 = gm_l12;
                    uint64_t t142 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t140, t141);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t143 = gm_f_us_cuv01fp(t142);
                    gm_l13 = t143;
                    {
                        void * t144 = gm_l0;
                        gm_t88rcucg1b7uk t145 = gm_l12;
                        gm_t53at80pfdus3 t146;
                        gm_f_kind_htv6093((void *)&t146, t144, GM_CONV(gm_csguankko4lmhd, t145));
                        gm_tjcj4j2gbh6c8 t147;
                        GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)0));
                        GM_ST(uint16_t, ((uint8_t *)&t147 + 2), ((uint16_t)8ULL));
                        uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)&t146) + 0));
                        uint8_t t149 = (uint8_t)(t148 == ((uint8_t)0ULL));
                        if (t149) {
                            gm_tjcj4j2gbh6c8 t150 = GM_LD(gm_tjcj4j2gbh6c8, (((uint8_t *)&t146) + 8));
                            t149 = (uint8_t)gm_eqo3v9599nus(((uint8_t *)&t150), ((uint8_t *)&t147));
                        }
                        uint8_t t151 = (uint8_t)(t149);
                        gm_t53at80pfdus3 t152;
                        memset(&t152, 0, sizeof t152);
                        GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t152 + 8), t147);
                        if (t151) {
                            void * t153 = gm_l4;
                            void * t154 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t156 = gm_l11;
                            uint64_t t157 = gm_f_us_cuv01fp(t156);
                            uint64_t t158 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t159 = (uintptr_t)t157;
                            if (t158 > t159 || t159 > (uintptr_t)t155) gm_trap_slice(t158, t159, t155, gm_srcl8pn2pjtmj6k, 451, 31);
                            uint64_t t160 = ((uintptr_t)t157) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t161;
                            GM_ST(void *, (uint8_t *)&t161, (void *)(((uint8_t *)t154 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(t160));
                            gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                            gm_f_hash_bytes_2qnf0cp(t153, t162);
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
                    }
                    gm_l15 = ((uint64_t)0ULL);
                    uint64_t t163 = gm_l11;
                    uint64_t t164 = gm_f_us_cuv01fp(t163);
                    gm_l14 = t164;
                    {
                        for (;;) {
                            uint64_t t165 = gm_l15;
                            uint64_t t166 = gm_l14;
                            uint8_t t167 = (t165 < t166);
                            if (!t167) {
                                goto gm_lb23;
                            }
                            {
                                {
                                    {
                                        uint64_t t168 = gm_l15;
                                        gm_l16 = t168;
                                        void * t169 = gm_l0;
                                        gm_tl874t2atnlh3 t170 = gm_l1;
                                        gm_t88rcucg1b7uk t171 = gm_l12;
                                        void * t172 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                        uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        uint64_t t174 = gm_l16;
                                        uint64_t t175 = gm_l13;
                                        uint64_t r176; bool o177 = __builtin_mul_overflow(t174, t175, &r176);
                                        if (o177) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 454, 62);
                                        uint64_t t178 = r176;
                                        uint64_t t179 = gm_l16;
                                        uint64_t r180; bool o181 = __builtin_add_overflow(t179, ((uint64_t)1ULL), &r180);
                                        if (o181) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 454, 72);
                                        uint64_t t182 = r180;
                                        uint64_t t183 = gm_l13;
                                        uint64_t r184; bool o185 = __builtin_mul_overflow(t182, t183, &r184);
                                        if (o185) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 454, 71);
                                        uint64_t t186 = r184;
                                        uint64_t t187 = (uintptr_t)t178;
                                        uint64_t t188 = (uintptr_t)t186;
                                        if (t187 > t188 || t188 > (uintptr_t)t173) gm_trap_slice(t187, t188, t173, gm_srcl8pn2pjtmj6k, 454, 60);
                                        uint64_t t189 = ((uintptr_t)t186) - (uintptr_t)t178;
                                        gm_b16_8 t190;
                                        GM_ST(void *, (uint8_t *)&t190, (void *)(((uint8_t *)t172 + (uintptr_t)t178 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(t189));
                                        void * t191 = gm_l4;
                                        gm_f_hash_val_nr2e99r((void *)&gm_l41, (void *)NULL, cx, NULL, t169, t170, t171, t190, t191);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t192 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                        if (t192) {
                                        } else {
                                            gm_t76123gfet73g t193 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l41) + 8));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t193);
                                            goto gm_fdone;
                                        }
                                        goto gm_lb26;
                                    }
gm_lb27:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 454, 13);
                                }
gm_lb26:;
                            }
gm_lb25:;
                            uint64_t t194 = gm_l15;
                            uint64_t r195; bool o196 = __builtin_add_overflow(t194, ((uint64_t)1ULL), &r195);
                            uint64_t t197 = r195;
                            gm_l15 = t197;
                        }
                    }
gm_lb23:;
                    goto gm_lb1;
                }
gm_lb22:;
            }
            {
                {
                    uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t199 = (uint8_t)(t198 == ((uint8_t)10ULL));
                    if (!t199) {
                        goto gm_lb28;
                    }
                    {
                        void * t200 = gm_l0;
                        gm_tl874t2atnlh3 t201 = gm_l1;
                        gm_t88rcucg1b7uk t202 = gm_l2;
                        gm_b16_8 t203 = gm_l3;
                        gm_f_seq_of_ifahtjs((void *)&gm_l42, (void *)NULL, cx, NULL, t200, t201, t202, t203);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t204 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                    if (t204) {
                    } else {
                        gm_t76123gfet73g t205 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l42) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t205);
                        goto gm_fdone;
                    }
                    {
                        memmove(((uint8_t *)&F.l17), (((uint8_t *)&gm_l42) + 8), 40);
                    }
                    void * t206 = gm_l4;
                    uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 16));
                    gm_f_hash_u64_32bripn(t206, t207);
                    {
                        void * t208 = gm_l0;
                        gm_t88rcucg1b7uk t209 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&F.l17));
                        gm_t53at80pfdus3 t210;
                        gm_f_kind_htv6093((void *)&t210, t208, GM_CONV(gm_csguankko4lmhd, t209));
                        gm_tjcj4j2gbh6c8 t211;
                        GM_ST(uint8_t, (uint8_t *)&t211 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t211 + 0), ((uint8_t)0));
                        GM_ST(uint16_t, ((uint8_t *)&t211 + 2), ((uint16_t)8ULL));
                        uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&t210) + 0));
                        uint8_t t213 = (uint8_t)(t212 == ((uint8_t)0ULL));
                        if (t213) {
                            gm_tjcj4j2gbh6c8 t214 = GM_LD(gm_tjcj4j2gbh6c8, (((uint8_t *)&t210) + 8));
                            t213 = (uint8_t)gm_eqo3v9599nus(((uint8_t *)&t214), ((uint8_t *)&t211));
                        }
                        uint8_t t215 = (uint8_t)(t213);
                        gm_t53at80pfdus3 t216;
                        memset(&t216, 0, sizeof t216);
                        GM_ST(uint8_t, ((uint8_t *)&t216 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t216 + 8), t211);
                        if (t215) {
                            void * t217 = gm_l4;
                            void * t218 = GM_LD(void *, (((uint8_t *)&F.l17) + 24));
                            uint64_t t219 = GM_LD(uint64_t, ((((uint8_t *)&F.l17) + 24) + 8));
                            uint64_t t220 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 16));
                            uint64_t t221 = gm_f_us_cuv01fp(t220);
                            uint64_t t222 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t223 = (uintptr_t)t221;
                            if (t222 > t223 || t223 > (uintptr_t)t219) gm_trap_slice(t222, t223, t219, gm_srcl8pn2pjtmj6k, 460, 31);
                            uint64_t t224 = ((uintptr_t)t221) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t225;
                            GM_ST(void *, (uint8_t *)&t225, (void *)(((uint8_t *)t218 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t225 + 8, (uintptr_t)(t224));
                            gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                            gm_f_hash_bytes_2qnf0cp(t217, t226);
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
                    }
                    gm_l19 = ((uint64_t)0ULL);
                    uint64_t t227 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 16));
                    uint64_t t228 = gm_f_us_cuv01fp(t227);
                    gm_l18 = t228;
                    {
                        for (;;) {
                            uint64_t t229 = gm_l19;
                            uint64_t t230 = gm_l18;
                            uint8_t t231 = (t229 < t230);
                            if (!t231) {
                                goto gm_lb29;
                            }
                            {
                                {
                                    {
                                        uint64_t t232 = gm_l19;
                                        gm_l20 = t232;
                                        void * t233 = gm_l0;
                                        gm_tl874t2atnlh3 t234 = gm_l1;
                                        gm_t88rcucg1b7uk t235 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&F.l17));
                                        void * t236 = GM_LD(void *, (((uint8_t *)&F.l17) + 24));
                                        uint64_t t237 = GM_LD(uint64_t, ((((uint8_t *)&F.l17) + 24) + 8));
                                        uint64_t t238 = gm_l20;
                                        uint64_t t239 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                        uint64_t r240; bool o241 = __builtin_mul_overflow(t238, t239, &r240);
                                        if (o241) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 463, 71);
                                        uint64_t t242 = r240;
                                        uint64_t t243 = gm_l20;
                                        uint64_t r244; bool o245 = __builtin_add_overflow(t243, ((uint64_t)1ULL), &r244);
                                        if (o245) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 463, 83);
                                        uint64_t t246 = r244;
                                        uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                        uint64_t r248; bool o249 = __builtin_mul_overflow(t246, t247, &r248);
                                        if (o249) gm_trap("integer overflow in `*`", gm_srcl8pn2pjtmj6k, 463, 82);
                                        uint64_t t250 = r248;
                                        uint64_t t251 = (uintptr_t)t242;
                                        uint64_t t252 = (uintptr_t)t250;
                                        if (t251 > t252 || t252 > (uintptr_t)t237) gm_trap_slice(t251, t252, t237, gm_srcl8pn2pjtmj6k, 463, 64);
                                        uint64_t t253 = ((uintptr_t)t250) - (uintptr_t)t242;
                                        gm_b16_8 t254;
                                        GM_ST(void *, (uint8_t *)&t254, (void *)(((uint8_t *)t236 + (uintptr_t)t242 * 1)));
                                        GM_ST(uintptr_t, (uint8_t *)&t254 + 8, (uintptr_t)(t253));
                                        void * t255 = gm_l4;
                                        gm_f_hash_val_nr2e99r((void *)&gm_l43, (void *)NULL, cx, NULL, t233, t234, t235, t254, t255);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t256 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                                        if (t256) {
                                        } else {
                                            gm_t76123gfet73g t257 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l43) + 8));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t257);
                                            goto gm_fdone;
                                        }
                                        goto gm_lb32;
                                    }
gm_lb33:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 463, 13);
                                }
gm_lb32:;
                            }
gm_lb31:;
                            uint64_t t258 = gm_l19;
                            uint64_t r259; bool o260 = __builtin_add_overflow(t258, ((uint64_t)1ULL), &r259);
                            uint64_t t261 = r259;
                            gm_l19 = t261;
                        }
                    }
gm_lb29:;
                    goto gm_lb1;
                }
gm_lb28:;
            }
            {
                {
                    uint8_t t262 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t263 = (uint8_t)(t262 == ((uint8_t)13ULL));
                    if (!t263) {
                        goto gm_lb34;
                    }
                    gm_t88rcucg1b7uk t264 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l37) + 8));
                    gm_l21 = t264;
                    void * t265 = gm_l0;
                    void * t266 = GM_LD(void *, (((uint8_t *)t265) + 8));
                    gm_t88rcucg1b7uk t267 = gm_l21;
                    uint8_t t268 = gm_f_nullable_pointer_7qfmn6o(t266, GM_CONV(gm_csguankko4lmhd, t267));
                    if (t268) {
                        void * t269 = gm_l4;
                        void * t270 = gm_l0;
                        gm_b16_8 t271 = gm_l3;
                        gm_b16_8 t272 = GM_LD(gm_b16_8, ((uint8_t *)&t271));
                        uint64_t t273 = gm_f_ptr_of_b77b4c9(t270, t272);
                        gm_f_hash_u64_32bripn(t269, t273);
                    } else {
                        void * t274 = gm_l0;
                        gm_t88rcucg1b7uk t275 = gm_l2;
                        gm_b16_8 t276 = gm_l3;
                        gm_b16_8 t277 = GM_LD(gm_b16_8, ((uint8_t *)&t276));
                        uint8_t t278 = gm_f_opt_present_lb4i2ip((void *)NULL, cx, NULL, t274, t275, t277);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        if (t278) {
                            void * t279 = gm_l4;
                            gm_f_hash_u64_32bripn(t279, ((uint64_t)1ULL));
                            void * t280 = gm_l0;
                            gm_tl874t2atnlh3 t281 = gm_l1;
                            gm_t88rcucg1b7uk t282 = gm_l21;
                            void * t283 = gm_l0;
                            gm_t88rcucg1b7uk t284 = gm_l2;
                            gm_b16_8 t285 = gm_l3;
                            gm_b16_8 t286 = gm_f_opt_payload_1pbnobb((void *)NULL, cx, NULL, t283, t284, t285);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t287 = gm_l4;
                            gm_f_hash_val_nr2e99r((void *)&gm_l44, (void *)NULL, cx, NULL, t280, t281, t282, t286, t287);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t288 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                            if (t288) {
                            } else {
                                gm_t76123gfet73g t289 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l44) + 8));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t289);
                                goto gm_fdone;
                            }
                        } else {
                            void * t290 = gm_l4;
                            gm_f_hash_u64_32bripn(t290, ((uint64_t)0ULL));
                        }
                    }
                    goto gm_lb1;
                }
gm_lb34:;
                {
                    uint8_t t291 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t292 = (uint8_t)(t291 == ((uint8_t)14ULL));
                    if (!t292) {
                        goto gm_lb35;
                    }
                    gm_t88rcucg1b7uk t293 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l37) + 8));
                    gm_l22 = t293;
                    gm_t88rcucg1b7uk t294 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l37) + 8) + 4));
                    gm_l23 = t294;
                    void * t295 = gm_l0;
                    gm_t88rcucg1b7uk t296 = gm_l2;
                    gm_b16_8 t297 = gm_l3;
                    gm_b16_8 t298 = GM_LD(gm_b16_8, ((uint8_t *)&t297));
                    uint8_t t299 = gm_f_res_is_err_8jbgndp((void *)NULL, cx, NULL, t295, t296, t298);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l24 = t299;
                    void * t300 = gm_l4;
                    uint8_t t301 = gm_l24;
                    if (t301) {
                        gm_l45 = ((uint64_t)1ULL);
                    } else {
                        gm_l45 = ((uint64_t)0ULL);
                    }
                    uint64_t t302 = gm_l45;
                    gm_f_hash_u64_32bripn(t300, t302);
                    void * t303 = gm_l0;
                    gm_tl874t2atnlh3 t304 = gm_l1;
                    uint8_t t305 = gm_l24;
                    if (t305) {
                        gm_t88rcucg1b7uk t306 = gm_l22;
                        gm_l46 = t306;
                    } else {
                        gm_t88rcucg1b7uk t307 = gm_l23;
                        gm_l46 = t307;
                    }
                    gm_t88rcucg1b7uk t308 = gm_l46;
                    void * t309 = gm_l0;
                    gm_t88rcucg1b7uk t310 = gm_l2;
                    gm_b16_8 t311 = gm_l3;
                    gm_b16_8 t312 = gm_f_res_payload_pb2hcmh((void *)NULL, cx, NULL, t309, t310, t311);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t313 = gm_l4;
                    gm_f_hash_val_nr2e99r((void *)&gm_l47, (void *)NULL, cx, NULL, t303, t304, t308, t312, t313);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t314 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                    if (t314) {
                    } else {
                        gm_t76123gfet73g t315 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l47) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t315);
                        goto gm_fdone;
                    }
                    goto gm_lb1;
                }
gm_lb35:;
            }
            {
                {
                    uint8_t t316 = GM_LD(uint8_t, (((uint8_t *)&gm_l37) + 0));
                    uint8_t t317 = (uint8_t)(t316 == ((uint8_t)17ULL));
                    if (!t317) {
                        goto gm_lb36;
                    }
                    {
                        {
                            void * t318 = gm_l0;
                            gm_t88rcucg1b7uk t319 = gm_l2;
                            gm_tkn42u7na3t81 t320 = gm_f_bits_struct_ty_22ue20s(t318, GM_CONV(gm_csguankko4lmhd, t319));
                            gm_l48 = t320;
                            uint8_t t321 = (uint8_t)((((uint8_t *)&gm_l48))[4] != 0);
                            if (!t321) {
                                goto gm_lb38;
                            }
                            gm_t88rcucg1b7uk t322 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l48));
                            gm_l25 = t322;
                            {
                                void * t323 = gm_l0;
                                gm_tl874t2atnlh3 t324 = gm_l1;
                                gm_t88rcucg1b7uk t325 = gm_l25;
                                gm_b16_8 t326 = gm_l3;
                                void * t327 = gm_l4;
                                gm_f_hash_val_nr2e99r((void *)&gm_l49, (void *)NULL, cx, NULL, t323, t324, t325, t326, t327);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t328 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                            if (t328) {
                            } else {
                                gm_t76123gfet73g t329 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l49) + 8));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t329);
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
                        }
gm_lb38:;
                    }
gm_lb37:;
                    {
                        {
                            {
                                void * t330 = gm_l0;
                                gm_t88rcucg1b7uk t331 = gm_l2;
                                gm_f_union_def_h8mvf4t((void *)&gm_l50, t330, GM_CONV(gm_csguankko4lmhd, t331));
                            }
                            uint8_t t332 = (uint8_t)((((uint8_t *)&gm_l50))[24] != 0);
                            if (!t332) {
                                goto gm_lb40;
                            }
                            {
                                memmove(((uint8_t *)&gm_l26), ((uint8_t *)&gm_l50), 24);
                            }
                            void * t333 = gm_l0;
                            gm_t88rcucg1b7uk t334 = gm_l2;
                            gm_b16_8 t335 = gm_l3;
                            gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&t335));
                            gm_t9805qtonbsag t337 = gm_f_variant_index_s2p6fs4((void *)NULL, cx, NULL, t333, t334, t336);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l51 = t337;
                            uint8_t t338 = (uint8_t)((((uint8_t *)&gm_l51))[4] != 0);
                            if (t338) {
                                uint32_t t339 = GM_LD(uint32_t, ((uint8_t *)&gm_l51));
                                gm_l27 = t339;
                            } else {
                                void * t340 = gm_l0;
                                gm_tl874t2atnlh3 t341 = gm_l1;
                                gm_t88rcucg1b7uk t342 = gm_l2;
                                gm_b16_8 t343 = gm_l3;
                                gm_b16_8 t344 = GM_LD(gm_b16_8, ((uint8_t *)&t343));
                                gm_t76123gfet73g t345;
                                gm_f_invalid_tag_1gif354((void *)&t345, (void *)NULL, cx, NULL, t340, t341, t342, t344);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t345);
                                goto gm_fdone;
                            }
                            void * t346 = gm_l4;
                            uint32_t t347 = gm_l27;
                            uint64_t t348 = (uint64_t)t347;
                            gm_f_hash_u64_32bripn(t346, t348);
                            {
                                {
                                    void * t349 = GM_LD(void *, (((uint8_t *)&gm_l26) + 8));
                                    uint64_t t350 = GM_LD(uint64_t, ((((uint8_t *)&gm_l26) + 8) + 8));
                                    uint32_t t351 = gm_l27;
                                    uint64_t t352 = (uint64_t)t351;
                                    uint64_t t353 = gm_f_us_cuv01fp(t352);
                                    if ((uintptr_t)t353 >= (uintptr_t)(t350)) gm_trap_index((uintptr_t)t353, t350, gm_srcl8pn2pjtmj6k, 488, 26);
                                    uint8_t t354 = (uint8_t)((((((uint8_t *)t349) + (uintptr_t)t353 * 72) + 16))[4] != 0);
                                    if (!t354) {
                                        goto gm_lb42;
                                    }
                                    gm_t88rcucg1b7uk t355 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t349) + (uintptr_t)t353 * 72) + 16));
                                    gm_l28 = t355;
                                    void * t356 = gm_l0;
                                    gm_t88rcucg1b7uk t357 = gm_l2;
                                    uint64_t t358 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t356, t357);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint64_t t359 = gm_f_us_cuv01fp(t358);
                                    gm_l29 = t359;
                                    void * t360 = gm_l0;
                                    gm_tl874t2atnlh3 t361 = gm_l1;
                                    gm_t88rcucg1b7uk t362 = gm_l28;
                                    void * t363 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                    uint64_t t364 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                    uint64_t t365 = gm_l29;
                                    uint64_t t366 = gm_l29;
                                    void * t367 = gm_l0;
                                    gm_t88rcucg1b7uk t368 = gm_l28;
                                    uint64_t t369 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t367, t368);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint64_t t370 = gm_f_us_cuv01fp(t369);
                                    uint64_t r371; bool o372 = __builtin_add_overflow(t366, t370, &r371);
                                    if (o372) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 490, 53);
                                    uint64_t t373 = r371;
                                    uint64_t t374 = (uintptr_t)t365;
                                    uint64_t t375 = (uintptr_t)t373;
                                    if (t374 > t375 || t375 > (uintptr_t)t364) gm_trap_slice(t374, t375, t364, gm_srcl8pn2pjtmj6k, 490, 46);
                                    uint64_t t376 = ((uintptr_t)t373) - (uintptr_t)t365;
                                    gm_b16_8 t377;
                                    GM_ST(void *, (uint8_t *)&t377, (void *)(((uint8_t *)t363 + (uintptr_t)t365 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t377 + 8, (uintptr_t)(t376));
                                    void * t378 = gm_l4;
                                    gm_f_hash_val_nr2e99r((void *)&gm_l52, (void *)NULL, cx, NULL, t360, t361, t362, t377, t378);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t379 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                                    if (t379) {
                                    } else {
                                        gm_t76123gfet73g t380 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l52) + 8));
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t380);
                                        goto gm_fdone;
                                    }
                                    goto gm_lb41;
                                }
gm_lb42:;
                            }
gm_lb41:;
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
                        }
gm_lb40:;
                    }
gm_lb39:;
                    void * t381 = gm_l0;
                    gm_t88rcucg1b7uk t382 = gm_l2;
                    void * t383 = gm_f_adt_of_8k5vqud(t381, GM_CONV(gm_csguankko4lmhd, t382));
                    gm_l53 = t383;
                    uint8_t t384 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l53)) != 0);
                    if (t384) {
                        void * t385 = GM_LD(void *, ((uint8_t *)&gm_l53));
                        gm_l30 = t385;
                    } else {
                        void * t386 = gm_l0;
                        gm_tl874t2atnlh3 t387 = gm_l1;
                        void * t388 = gm_l0;
                        void * t389 = GM_LD(void *, (((uint8_t *)t388) + 8));
                        gm_t88rcucg1b7uk t390 = gm_l2;
                        gm_b16_8 t391 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t389, t390);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t392;
                        GM_ST(gm_b16_8, ((uint8_t *)&t392 + 0), t391);
                        gm_f_format_h8e7ps1((void *)&gm_l54, (void *)NULL, cx, NULL, t392);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t393 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                        if (!t393) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 8, gm_srcl8pn2pjtmj6k, 494, 56);
                        gm_b16_8 t394 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l54) + 8));
                        gm_b16_8 t395 = GM_LD(gm_b16_8, ((uint8_t *)&t394));
                        gm_t76123gfet73g t396;
                        gm_f_trap_qh84l9d((void *)&t396, (void *)NULL, cx, NULL, t386, t387, t395);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t396);
                        goto gm_fdone;
                    }
                    {
                        {
                            void * t397 = gm_l30;
                            uint8_t t398 = GM_LD(uint8_t, ((((uint8_t *)t397) + 24) + 0));
                            uint8_t t399 = (uint8_t)(t398 == ((uint8_t)0ULL));
                            if (!t399) {
                                goto gm_lb44;
                            }
                            memmove(((uint8_t *)&F.l31), ((((uint8_t *)t397) + 24) + 8), 48);
                            gm_l33 = ((uint64_t)0ULL);
                            uint64_t t400 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 8));
                            gm_l32 = t400;
                            {
                                for (;;) {
                                    uint64_t t401 = gm_l33;
                                    uint64_t t402 = gm_l32;
                                    uint8_t t403 = (t401 < t402);
                                    if (!t403) {
                                        goto gm_lb45;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t404 = gm_l33;
                                                gm_l34 = t404;
                                                void * t405 = gm_l0;
                                                gm_t88rcucg1b7uk t406 = gm_l2;
                                                uint64_t t407 = gm_l34;
                                                uint32_t t408 = (uint32_t)t407;
                                                gm_tfbnciphf3261 t409 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t405, t406, t408);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l35 = t409;
                                                uint64_t t410 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                                                uint64_t t411 = gm_f_us_cuv01fp(t410);
                                                gm_l36 = t411;
                                                void * t412 = gm_l0;
                                                gm_tl874t2atnlh3 t413 = gm_l1;
                                                gm_t88rcucg1b7uk t414 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l35));
                                                void * t415 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                                uint64_t t416 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                                uint64_t t417 = gm_l36;
                                                uint64_t t418 = gm_l36;
                                                void * t419 = gm_l0;
                                                gm_t88rcucg1b7uk t420 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l35));
                                                uint64_t t421 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t419, t420);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint64_t t422 = gm_f_us_cuv01fp(t421);
                                                uint64_t r423; bool o424 = __builtin_add_overflow(t418, t422, &r423);
                                                if (o424) gm_trap("integer overflow in `+`", gm_srcl8pn2pjtmj6k, 499, 52);
                                                uint64_t t425 = r423;
                                                uint64_t t426 = (uintptr_t)t417;
                                                uint64_t t427 = (uintptr_t)t425;
                                                if (t426 > t427 || t427 > (uintptr_t)t416) gm_trap_slice(t426, t427, t416, gm_srcl8pn2pjtmj6k, 499, 45);
                                                uint64_t t428 = ((uintptr_t)t425) - (uintptr_t)t417;
                                                gm_b16_8 t429;
                                                GM_ST(void *, (uint8_t *)&t429, (void *)(((uint8_t *)t415 + (uintptr_t)t417 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t429 + 8, (uintptr_t)(t428));
                                                void * t430 = gm_l4;
                                                gm_f_hash_val_nr2e99r((void *)&gm_l55, (void *)NULL, cx, NULL, t412, t413, t414, t429, t430);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t431 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                                                if (t431) {
                                                } else {
                                                    gm_t76123gfet73g t432 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l55) + 8));
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t432);
                                                    goto gm_fdone;
                                                }
                                                goto gm_lb48;
                                            }
gm_lb49:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 496, 13);
                                        }
gm_lb48:;
                                    }
gm_lb47:;
                                    uint64_t t433 = gm_l33;
                                    uint64_t r434; bool o435 = __builtin_add_overflow(t433, ((uint64_t)1ULL), &r434);
                                    uint64_t t436 = r434;
                                    gm_l33 = t436;
                                }
                            }
gm_lb45:;
                            goto gm_lb43;
                        }
gm_lb44:;
                        {
                            void * t437 = gm_l0;
                            gm_tl874t2atnlh3 t438 = gm_l1;
                            void * t439 = gm_l0;
                            void * t440 = GM_LD(void *, (((uint8_t *)t439) + 8));
                            gm_t88rcucg1b7uk t441 = gm_l2;
                            gm_b16_8 t442 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t440, t441);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t443;
                            GM_ST(gm_b16_8, ((uint8_t *)&t443 + 0), t442);
                            gm_f_format_8k5eh95((void *)&gm_l56, (void *)NULL, cx, NULL, t443);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t444 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                            if (!t444) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 8, gm_srcl8pn2pjtmj6k, 495, 62);
                            gm_b16_8 t445 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l56) + 8));
                            gm_b16_8 t446 = GM_LD(gm_b16_8, ((uint8_t *)&t445));
                            gm_t76123gfet73g t447;
                            gm_f_trap_qh84l9d((void *)&t447, (void *)NULL, cx, NULL, t437, t438, t446);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t447);
                            goto gm_fdone;
                        }
                    }
gm_lb43:;
                    goto gm_lb1;
                }
gm_lb36:;
            }
            {
                {
                    void * t448 = gm_l0;
                    gm_tl874t2atnlh3 t449 = gm_l1;
                    void * t450 = gm_l0;
                    void * t451 = GM_LD(void *, (((uint8_t *)t450) + 8));
                    gm_t88rcucg1b7uk t452 = gm_l2;
                    gm_b16_8 t453 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t451, t452);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t454;
                    GM_ST(gm_b16_8, ((uint8_t *)&t454 + 0), t453);
                    gm_f_format_8k5eh95((void *)&gm_l57, (void *)NULL, cx, NULL, t454);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t455 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                    if (!t455) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 8, gm_srcl8pn2pjtmj6k, 502, 35);
                    gm_b16_8 t456 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l57) + 8));
                    gm_b16_8 t457 = GM_LD(gm_b16_8, ((uint8_t *)&t456));
                    gm_t76123gfet73g t458;
                    gm_f_trap_qh84l9d((void *)&t458, (void *)NULL, cx, NULL, t448, t449, t457);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t458);
                    goto gm_fdone;
                }
gm_lb50:;
                gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 432, 5);
            }
        }
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_int_text_vrjnuu2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_int_text_vrjnuu2 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    unsigned __int128 gm_l3; /* v */
    gm_ttovhfee8mf28 gm_l4; /* info */
    GM_STACK_CHECK(&F, 416u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tg5lql6itufk7 gm_l5;
    gm_b16_8 gm_l6;
    gm_tabb710d9mq2e gm_l7;
    gm_tabb710d9mq2e gm_l8;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    unsigned __int128 t5 = gm_f_get_int_pg7p4gc(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
    gm_l3 = t5;
    void * t6 = gm_l0;
    gm_t88rcucg1b7uk t7 = gm_l1;
    gm_tg5lql6itufk7 t8 = gm_f_int_info_ulcevdv(t6, GM_CONV(gm_csguankko4lmhd, t7));
    gm_l5 = t8;
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
    if (t9) {
        gm_ttovhfee8mf28 t10 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l5));
        gm_l4 = t10;
    } else {
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(0));
        F.ret = t11;
        goto gm_fret;
    }
    uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)&gm_l4));
    if (t12) {
        unsigned __int128 t13 = gm_l3;
        __int128 t14;
        memset(&t14, 0, sizeof t14); memcpy(&t14, ((uint8_t *)&t13), sizeof t14 < 16 ? sizeof t14 : 16);
        gm_tpl3q6e8s9qo1 t15;
        GM_ST(__int128, ((uint8_t *)&t15 + 0), t14);
        gm_f_format_9qvncot((void *)&gm_l7, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
        if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcl8pn2pjtmj6k, 185, 29);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
        gm_l6 = t18;
    } else {
        unsigned __int128 t19 = gm_l3;
        gm_tsn2omssg6cpf t20;
        GM_ST(unsigned __int128, ((uint8_t *)&t20 + 0), t19);
        gm_f_format_ra7pont((void *)&gm_l8, (void *)NULL, cx, NULL, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_srcl8pn2pjtmj6k, 185, 70);
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        gm_l6 = t23;
    }
    gm_b16_8 t24 = gm_l6;
    F.ret = t24;
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_align_lavuopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_ptr_align_lavuopj F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* p */
    gm_t88rcucg1b7uk gm_l3; /* p */
    gm_t88rcucg1b7uk gm_l4; /* inner */
    GM_STACK_CHECK(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l5;
    uint64_t gm_l6;
    void * t9 = gm_l0;
    gm_t88rcucg1b7uk t10 = gm_l1;
    gm_f_kind_htv6093((void *)&gm_l5, t9, GM_CONV(gm_csguankko4lmhd, t10));
    {
        {
            {
                {
                    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t12 = (uint8_t)(t11 == ((uint8_t)8ULL));
                    if (!t12) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
                    gm_l2 = t13;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                    uint8_t t15 = (uint8_t)(t14 == ((uint8_t)9ULL));
                    if (!t15) {
                        goto gm_lb5;
                    }
                    gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l5) + 8) + 4));
                    gm_l2 = t16;
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t17 = gm_l0;
            gm_t88rcucg1b7uk t18 = gm_l2;
            uint64_t t19 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t20 = ((t19 == ((uint64_t)0ULL)));
            if (t20) {
                gm_l6 = ((uint64_t)1ULL);
            } else {
                void * t21 = gm_l0;
                gm_t88rcucg1b7uk t22 = gm_l2;
                uint64_t t23 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t21, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t23;
            }
            uint64_t t24 = gm_l6;
            F.ret = t24;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)13ULL));
            if (!t26) {
                goto gm_lb6;
            }
            gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
            gm_l4 = t27;
            void * t28 = gm_l0;
            gm_t88rcucg1b7uk t29 = gm_l4;
            uint64_t t30 = gm_f_ptr_align_lavuopj((void *)NULL, cx, NULL, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t30;
            goto gm_fret;
        }
gm_lb6:;
        {
            F.ret = ((uint64_t)1ULL);
            goto gm_fret;
        }
gm_lb7:;
        gm_trap("no match arm matched the scrutinee", gm_srcl8pn2pjtmj6k, 174, 5);
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

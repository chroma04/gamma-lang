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
typedef struct gm_cs2lqoac0a5ng4 { _Bool f0; uint32_t f1; uint64_t f2; } gm_cs2lqoac0a5ng4;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { double f0; } gm_tidu2er0d7h95;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_cs2lqoac0a5ng4) == 16 && _Alignof(gm_cs2lqoac0a5ng4) == 8 && offsetof(gm_cs2lqoac0a5ng4, f0) == 0 && offsetof(gm_cs2lqoac0a5ng4, f1) == 4 && offsetof(gm_cs2lqoac0a5ng4, f2) == 8, "layout of `IntInfo`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; uint32_t f3; uint32_t f4; } gm_t133qiba7tvhc;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_t5g5vgung6h6s;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t f3; uint8_t p4[3]; gm_b16_8 f4; gm_b16_8 f5; } gm_t8fvq98mjik04;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; } gm_tg1t0c5hkovn5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; } gm_thognv9odov03;
_Static_assert(sizeof(gm_tidu2er0d7h95) == 8 && _Alignof(gm_tidu2er0d7h95) == 8 && offsetof(gm_tidu2er0d7h95, f0) == 0, "layout of `(f64,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint32_t f3; uint8_t pe[4]; } gm_tj4tbafenpsjm;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; gm_b16_8 f9; } gm_tk6rvhh54g86p;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; gm_b16_8 f4; } gm_tpm6m3o0hprmc;
typedef struct { gm_tuglhjjmj2g58 f0; uint8_t f1; uint8_t pe[7]; } gm_tqcc6dmlne11b;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; uint64_t f5; gm_b16_8 f6; uint64_t f7; } gm_tsroeipraj6j8;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; } gm_tttl9nsdo1iu6;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t133qiba7tvhc) == 48 && _Alignof(gm_t133qiba7tvhc) == 8 && offsetof(gm_t133qiba7tvhc, f0) == 0 && offsetof(gm_t133qiba7tvhc, f1) == 8 && offsetof(gm_t133qiba7tvhc, f2) == 24 && offsetof(gm_t133qiba7tvhc, f3) == 40 && offsetof(gm_t133qiba7tvhc, f4) == 44, "layout of `(u32, string, string, u32, u32)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t5g5vgung6h6s) == 32 && _Alignof(gm_t5g5vgung6h6s) == 8 && offsetof(gm_t5g5vgung6h6s, f0) == 0 && offsetof(gm_t5g5vgung6h6s, f1) == 8 && offsetof(gm_t5g5vgung6h6s, f2) == 24, "layout of `(u32, []u8, u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t8fvq98mjik04) == 72 && _Alignof(gm_t8fvq98mjik04) == 8 && offsetof(gm_t8fvq98mjik04, f0) == 0 && offsetof(gm_t8fvq98mjik04, f1) == 16 && offsetof(gm_t8fvq98mjik04, f2) == 32 && offsetof(gm_t8fvq98mjik04, f3) == 36 && offsetof(gm_t8fvq98mjik04, f4) == 40 && offsetof(gm_t8fvq98mjik04, f5) == 56, "layout of `([]u8, string, u32, bool, string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tg1t0c5hkovn5) == 128 && _Alignof(gm_tg1t0c5hkovn5) == 8 && offsetof(gm_tg1t0c5hkovn5, f0) == 0 && offsetof(gm_tg1t0c5hkovn5, f1) == 16 && offsetof(gm_tg1t0c5hkovn5, f2) == 32 && offsetof(gm_tg1t0c5hkovn5, f3) == 48 && offsetof(gm_tg1t0c5hkovn5, f4) == 64 && offsetof(gm_tg1t0c5hkovn5, f5) == 80 && offsetof(gm_tg1t0c5hkovn5, f6) == 96 && offsetof(gm_tg1t0c5hkovn5, f7) == 112, "layout of `(string, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_t8588ma2cr9d7 f0; gm_t8588ma2cr9d7 f1; } gm_th9f6i3krj9v9;
_Static_assert(sizeof(gm_thognv9odov03) == 40 && _Alignof(gm_thognv9odov03) == 8 && offsetof(gm_thognv9odov03, f0) == 0 && offsetof(gm_thognv9odov03, f1) == 8 && offsetof(gm_thognv9odov03, f2) == 24, "layout of `ForeignFn`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj4tbafenpsjm) == 56 && _Alignof(gm_tj4tbafenpsjm) == 8 && offsetof(gm_tj4tbafenpsjm, f0) == 0 && offsetof(gm_tj4tbafenpsjm, f1) == 16 && offsetof(gm_tj4tbafenpsjm, f2) == 32 && offsetof(gm_tj4tbafenpsjm, f3) == 48, "layout of `(string, string, string, u32)`");
_Static_assert(sizeof(gm_tk6rvhh54g86p) == 160 && _Alignof(gm_tk6rvhh54g86p) == 8 && offsetof(gm_tk6rvhh54g86p, f0) == 0 && offsetof(gm_tk6rvhh54g86p, f1) == 16 && offsetof(gm_tk6rvhh54g86p, f2) == 32 && offsetof(gm_tk6rvhh54g86p, f3) == 48 && offsetof(gm_tk6rvhh54g86p, f4) == 64 && offsetof(gm_tk6rvhh54g86p, f5) == 80 && offsetof(gm_tk6rvhh54g86p, f6) == 96 && offsetof(gm_tk6rvhh54g86p, f7) == 112 && offsetof(gm_tk6rvhh54g86p, f8) == 128 && offsetof(gm_tk6rvhh54g86p, f9) == 144, "layout of `(string, string, string, string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpm6m3o0hprmc) == 64 && _Alignof(gm_tpm6m3o0hprmc) == 8 && offsetof(gm_tpm6m3o0hprmc, f0) == 0 && offsetof(gm_tpm6m3o0hprmc, f1) == 16 && offsetof(gm_tpm6m3o0hprmc, f2) == 24 && offsetof(gm_tpm6m3o0hprmc, f3) == 40 && offsetof(gm_tpm6m3o0hprmc, f4) == 48, "layout of `(string, u64, string, u64, string)`");
_Static_assert(sizeof(gm_tqcc6dmlne11b) == 24 && _Alignof(gm_tqcc6dmlne11b) == 8 && offsetof(gm_tqcc6dmlne11b, f0) == 0 && offsetof(gm_tqcc6dmlne11b, f1) == 16, "layout of `?IntInfo`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsroeipraj6j8) == 112 && _Alignof(gm_tsroeipraj6j8) == 8 && offsetof(gm_tsroeipraj6j8, f0) == 0 && offsetof(gm_tsroeipraj6j8, f1) == 16 && offsetof(gm_tsroeipraj6j8, f2) == 32 && offsetof(gm_tsroeipraj6j8, f3) == 48 && offsetof(gm_tsroeipraj6j8, f4) == 64 && offsetof(gm_tsroeipraj6j8, f5) == 80 && offsetof(gm_tsroeipraj6j8, f6) == 88 && offsetof(gm_tsroeipraj6j8, f7) == 104, "layout of `(string, string, string, string, string, u64, string, u64)`");
_Static_assert(sizeof(gm_tttl9nsdo1iu6) == 56 && _Alignof(gm_tttl9nsdo1iu6) == 8 && offsetof(gm_tttl9nsdo1iu6, f0) == 0 && offsetof(gm_tttl9nsdo1iu6, f1) == 16 && offsetof(gm_tttl9nsdo1iu6, f2) == 32 && offsetof(gm_tttl9nsdo1iu6, f3) == 48, "layout of `(string, string, string, u64)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_th9f6i3krj9v9) == 24 && _Alignof(gm_th9f6i3krj9v9) == 4 && offsetof(gm_th9f6i3krj9v9, f0) == 0 && offsetof(gm_th9f6i3krj9v9, f1) == 12, "layout of `(Builtin, Builtin)`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_alloc_tag_values_gcte1b4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_atomic_plj0t12 {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* ords */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_bit_count_77uni44 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_bits_layout_23rect9 {
    gm_frame h;
    gm_t57euh612ligr ret;
};
struct gm_fr_black_box_r6deojc {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_bool_ty_kotvo8t {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_builtin_values_vqetei1 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_convert_lqrk2n0 {
    gm_frame h;
    gm_tpautr4br7h3l l13; /* sd */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_format_00b54tv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_0gjpujh {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_15kpcel {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_21vnq4a {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_25p33s1 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_261kv0l {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_2qdci62 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3itad0c {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3j59e2u {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3s3fqnk {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3vgp4uu {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3vtl3te {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_53mv29j {
    gm_frame h;
    gm_tk6rvhh54g86p l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5rrmec0 {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_68bbqsa {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6cn78ik {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6df13cd {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7gtji42 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7lru3c2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_83msomh {
    gm_frame h;
    gm_tttl9nsdo1iu6 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_87dn9rn {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_898275p {
    gm_frame h;
    gm_tj4tbafenpsjm l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8d9jpvv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8m4h4ip {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a1ch927 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a1ec0ji {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a7bth9i {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aculdom {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_aghnr00 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_b05vb48 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c57v5iu {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c9b4vl3 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cjdn0sp {
    gm_frame h;
    gm_thognv9odov03 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ckgfhm4 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dv5f37i {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eqjcikj {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_eucvbnh {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fh9to9u {
    gm_frame h;
    gm_tk6rvhh54g86p l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fsfutor {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gf4f6rt {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gp2h96m {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h95c7u1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_he769rg {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_iqjnutf {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jagvq3q {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jj2aipm {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jpvl2js {
    gm_frame h;
    gm_th2d9c7aju8hp l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_kbs7h9q {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ljsk7ji {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lqnh3lt {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_m0j4lkh {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_m7gv7a8 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ms7m2d2 {
    gm_frame h;
    gm_tftksr4op114e l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nkjq1if {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nm3fl8o {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o73frof {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ohk11b5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p1ca3i8 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_patlqq0 {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q296tqt {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qofaffd {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s2ml2tu {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tvr4di5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ucng4n7 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uhjjs86 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v2sfrt0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_from_tag_2m7nfln {
    gm_frame h;
    gm_tckdobunaulee l12; /* vd */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_ii_cty_i2b4e03 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ii_exact_g6a19dp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_ii_ucty_b40h5ot {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_unit_17uennd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_line_l6u87p9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_0kkt9cm {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_156hint {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_21h063v {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_3utfuce {
    gm_frame h;
    gm_t8fvq98mjik04 l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_5q2h82g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_6h7r936 {
    gm_frame h;
    gm_t0uuhaljsf410 l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_6sel63v {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_6t3ttmd {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_7e19lva {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_7pb5lg6 {
    gm_frame h;
    gm_t133qiba7tvhc l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_a98fois {
    gm_frame h;
    gm_tsroeipraj6j8 l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_ahjvru7 {
    gm_frame h;
    gm_th2d9c7aju8hp l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_bq6igl9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_calnga5 {
    gm_frame h;
    gm_tif3elqk01bdl l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_di9ea86 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_evpuk0q {
    gm_frame h;
    gm_tg1t0c5hkovn5 l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_jl8o1sr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_kafc4c5 {
    gm_frame h;
    gm_tpm6m3o0hprmc l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_ki2bckn {
    gm_frame h;
    gm_th2d9c7aju8hp l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_linqala {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_n685kk5 {
    gm_frame h;
    gm_tg1t0c5hkovn5 l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_qno93os {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_b0_1 ret;
};
struct gm_fr_linef_uu8i6ue {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_v355qja {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_linef_vgjqckp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_pow_s0lbtt4 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bytes_eocneng {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
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
struct gm_fr_rotate_tpbpt1c {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_slice_len_a3383br {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_slice_ptr_hojfgr5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_kk94re3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_sys_values_qlu7stq {
    gm_frame h;
    gm_trqfvmjtt4sij l11; /* cargs */
    gm_b16_8 ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_8ekedsl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_ty_vgceilu {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
inline bool gm_eq0lfe1s2pin(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqasplg81cl8(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqg63o53ar52(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqov4e3mbi0f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_alloc_tag_values_gcte1b4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t8588ma2cr9d7 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_arith_fits_7degt3e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) double gm_f_arith_int_to_f64_tiud1hu(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_arith_pointee_align_vqoa1mk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_atomic_plj0t12(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t8588ma2cr9d7 a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_bit_count_77uni44(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t8588ma2cr9d7 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5);
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_black_box_r6deojc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_builtin_values_vqetei1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t8588ma2cr9d7 a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_convert_lqrk2n0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t8588ma2cr9d7 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_finish_int_cr0p55i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_tuglhjjmj2g58 a2, gm_t88rcucg1b7uk a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6, gm_b16_8 a7, gm_b16_8 a8, gm_b16_8 a9);
__attribute__((aligned(4))) gm_b16_8 gm_f_float_from_r6b8fu0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_00b54tv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_01lon97(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_0gjpujh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_15kpcel(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_21vnq4a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_25p33s1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_261kv0l(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_2qdci62(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_3itad0c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_3j59e2u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_3s3fqnk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_3vgp4uu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_3vtl3te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4l7juhh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_53mv29j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5rrmec0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_65ee4ra(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_68bbqsa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_6cn78ik(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6df13cd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_6dvr333(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_74pd3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_7gtji42(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_7lru3c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_817tni2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_83msomh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_87dn9rn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_898275p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8d9jpvv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8m4h4ip(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_a1ch927(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_a1ec0ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_a7bth9i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_abcvqrv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_aculdom(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_aghnr00(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_b05vb48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_bdugbhq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_c57v5iu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c9b4vl3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cjdn0sp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ckgfhm4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dq6oc41(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_dv5f37i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ee7lhc3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_eqjcikj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_eucvbnh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fh9to9u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fjqj2pj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_flhcmdb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_gf4f6rt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_gp2h96m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h5s5tmr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_h95c7u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_he769rg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_iqjnutf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_jagvq3q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_jah3ip3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_jj2aipm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_jpvl2js(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_kbs7h9q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ljsk7ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lqnh3lt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_m0j4lkh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_m3cufph(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_m7gv7a8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ms7m2d2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_nkjq1if(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_nm3fl8o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nv3e0h2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_o73frof(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ohk11b5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_p1ca3i8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_p95juln(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_patlqq0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_peglcgm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_q296tqt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qofaffd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_s2ml2tu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_tvr4di5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_ucng4n7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_uhjjs86(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_v2sfrt0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_v38mj3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_fresh_tmp_6tadl69(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_from_tag_2m7nfln(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_fn_nlk82uj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_cty_i2b4e03(gm_cs2lqoac0a5ng4 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_ii_exact_g6a19dp(gm_cs2lqoac0a5ng4 a0);
__attribute__((aligned(4))) void gm_f_ii_max_gtp5h90(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0);
__attribute__((aligned(4))) void gm_f_ii_min_lku7pdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_ucty_b40h5ot(gm_cs2lqoac0a5ng4 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_indent_line_b3mckqa(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_cty_s62af0i(uint8_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_int_info_2tcdf1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_0kkt9cm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_156hint(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_21h063v(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_29aidha(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_3utfuce(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_4m3ue17(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_5q2h82g(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_69ggc4q(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_6h7r936(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_6sel63v(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_6t3ttmd(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_7afcl2l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_7e19lva(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_7pb5lg6(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_88hn6vm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_8uttu8h(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_8v6p2qr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_9379f24(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_9hv0h73(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_a98fois(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ahjvru7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ao39327(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_bq6igl9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_calnga5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_d96evvt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tcc1rq4n1avb2 a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_di9ea86(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_eroqj8i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_evpuk0q(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_fa323qa(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_gkog1ft(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_heccqpv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_jl8o1sr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_kafc4c5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ki2bckn(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_linqala(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_n685kk5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qno93os(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_rbi6dt9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_rosnm2q(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_slpvrc7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_v355qja(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_vgjqckp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tcc1rq4n1avb2 a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_load_0hebde7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_none_v2s8s0l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_slice_sa50efe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_some_9aqq928(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_variant_srnk8ha(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, const void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_jch381e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_pow_s0lbtt4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_rotate_tpbpt1c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t8588ma2cr9d7 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_name_pcdoujk(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_values_qlu7stq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern const uint8_t gm_s02uuedsru1qu[9];
extern const uint8_t gm_s0cpkv7s4ef85[24];
extern const uint8_t gm_s0ku6rqnajsui[24];
extern const uint8_t gm_s0m9d22aen6f8[2];
extern const uint8_t gm_s12ng38ve3205[8];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2ph2ad6lffbq[17];
extern const uint8_t gm_s2q81sartj882[42];
extern const uint8_t gm_s2vj6kgk19dag[32];
extern const uint8_t gm_s315r4147rf48[40];
extern const uint8_t gm_s37otbiaffdaa[25];
extern const uint8_t gm_s38tgoanp9o78[66];
extern const uint8_t gm_s3jpgh4kdh4qh[15];
extern const uint8_t gm_s3lqm9addfamn[56];
extern const uint8_t gm_s3n5bjpleiarb[7];
extern const uint8_t gm_s42h828lb45uf[19];
extern const uint8_t gm_s4k5q4ehrakfe[15];
extern const uint8_t gm_s4mij084uljjf[26];
extern const uint8_t gm_s4nk259sisvmu[96];
extern const uint8_t gm_s54ap4j24utib[69];
extern const uint8_t gm_s565fgm0tsngr[15];
extern const uint8_t gm_s5a2m41g390kg[54];
extern const uint8_t gm_s5dmjeoiprfiv[72];
extern const uint8_t gm_s5dqpb4hur24f[37];
extern const uint8_t gm_s5ie9tn1qnf6v[9];
extern const uint8_t gm_s5t5jm6ask6ip[15];
extern const uint8_t gm_s63o5blu3j46d[16];
extern const uint8_t gm_s66fk044n1n0k[9];
extern const uint8_t gm_s6j315qbv99q6[12];
extern const uint8_t gm_s6sdj2gjsdah5[18];
extern const uint8_t gm_s7n79dja87bqg[28];
extern const uint8_t gm_s7tgpca9g8npn[11];
extern const uint8_t gm_s84bra4h6kp3s[57];
extern const uint8_t gm_s873ct0as0vg4[48];
extern const uint8_t gm_s9dq8mbgn8du3[35];
extern const uint8_t gm_s9gk1b0ve64mt[10];
extern const uint8_t gm_sader8j76ksps[27];
extern const uint8_t gm_sadot8stl6s4j[36];
extern const uint8_t gm_sc22qkqa9q462[16];
extern const uint8_t gm_scf7jg05jni41[5];
extern const uint8_t gm_scqp3lehp7mc6[17];
extern const uint8_t gm_scu4ehdqrc5fh[19];
extern const uint8_t gm_sd2ioiubqiqs5[46];
extern const uint8_t gm_sdeu8nuc597e8[19];
extern const uint8_t gm_sdsb9longvn22[40];
extern const uint8_t gm_se25mtccq2mtg[42];
extern const uint8_t gm_segii2f2mu6lb[23];
extern const uint8_t gm_sfc28giak7be5[31];
extern const uint8_t gm_sfivm1csbt9ar[29];
extern const uint8_t gm_sfltedqje18on[9];
extern const uint8_t gm_sfms0h2mph067[11];
extern const uint8_t gm_sfnph9ql8pqb1[21];
extern const uint8_t gm_sg3ba4vpic0jb[19];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgahfuanpfk2r[60];
extern const uint8_t gm_sgjc0ebf56hf4[8];
extern const uint8_t gm_sh8j22p8lpudd[15];
extern const uint8_t gm_shhak6l2rrn43[15];
extern const uint8_t gm_shhrbleiv8o3j[14];
extern const uint8_t gm_shn9a2s1f1vl0[17];
extern const uint8_t gm_sht50491g5jgu[31];
extern const uint8_t gm_si0mnqg6j1eu8[19];
extern const uint8_t gm_si5tqjfc8g60q[30];
extern const uint8_t gm_si731m8b0sr6s[4];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sipfpcphbcub1[34];
extern const uint8_t gm_sitlbs33jqqal[2];
extern const uint8_t gm_siv1vjr1gaenb[13];
extern const uint8_t gm_sjpr4fvui63qj[13];
extern const uint8_t gm_sjr44k79a48mq[4];
extern const uint8_t gm_sjt41g4f7bvrv[32];
extern const uint8_t gm_sjtp8ip8t28uh[7];
extern const uint8_t gm_sjv2au7jm0m3j[66];
extern const uint8_t gm_sk7bri4eh56ps[17];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sl6fvbluu7spr[26];
extern const uint8_t gm_sl9pujidpnv0r[4];
extern const uint8_t gm_sla3unrbmgc0v[6];
extern const uint8_t gm_sm0f8bs4gd2ma[18];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_smd81jcp0bga9[19];
extern const uint8_t gm_smhvmmegaur89[7];
extern const uint8_t gm_smjg62rnkmnon[21];
extern const uint8_t gm_smop6ueqrddak[77];
extern const uint8_t gm_smsr74eoevopl[72];
extern const uint8_t gm_smu1qqop2463k[53];
extern const uint8_t gm_sn8alsh9na9ne[12];
extern const uint8_t gm_snbn61h56c5fj[52];
extern const uint8_t gm_sntddact5d6vp[4];
extern const uint8_t gm_soiotnern2p8m[19];
extern const uint8_t gm_soj8art8cj0h6[25];
extern const uint8_t gm_sp2cos6rmlsbv[51];
extern const uint8_t gm_sp7351fukevmv[29];
extern const uint8_t gm_spi5fq66572ic[20];
extern const uint8_t gm_sqcj2s8m7mpqj[38];
extern const uint8_t gm_sqqh9r6c17rju[7];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0gtuh94b1fce[];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src2j6ljeja7ilp[];
extern const char gm_src4pc97o88riqc[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srhrn3fa70ni4[38];
extern const uint8_t gm_ss1ntuh8relgg[19];
extern const uint8_t gm_ssor6j0m3frue[14];
extern const uint8_t gm_ssvpi8gpvt9jm[10];
extern const uint8_t gm_st78509mboaa4[22];
extern const uint8_t gm_stjfbh3ag5r41[52];
extern const uint8_t gm_stol2i9gi9a35[18];
extern const uint8_t gm_stqsuuvp1i0pf[23];
extern const uint8_t gm_su8ulebf2ijla[20];
extern const uint8_t gm_suapcbuahu4i6[24];
extern const uint8_t gm_sukriak12588g[5];
extern const uint8_t gm_suqogp9sflci8[12];
extern const uint8_t gm_suvpgtuh49tp7[20];
extern const uint8_t gm_sv19dj08f7d6b[45];
extern const uint8_t gm_sv6dkmq38siqt[27];
extern const uint8_t gm_sv7lsha1e0i8r[13];
extern const uint8_t gm_svgaurqm99100[48];
extern const uint8_t gm_svpofgjtij7vj[4];
extern const uint8_t gm_svpr02j6er8vd[15];
extern const gm_tmpl gm_tpl0lggla435q5r;
extern const gm_tmpl gm_tpl0sb47rg6hcij;
extern const gm_tmpl gm_tpl1dmvfuuebopd;
extern const gm_tmpl gm_tpl1f1njss89nj4;
extern const gm_tmpl gm_tpl1rdf0mm7rjgv;
extern const gm_tmpl gm_tpl2b904kc90480;
extern const gm_tmpl gm_tpl2cg0ghfdkfbm;
extern const gm_tmpl gm_tpl2d8hdhnr24pd;
extern const gm_tmpl gm_tpl2dia2gbnfh9l;
extern const gm_tmpl gm_tpl4upfck3qu2er;
extern const gm_tmpl gm_tpl55mhnmub0ffl;
extern const gm_tmpl gm_tpl5vfcnh2qs08h;
extern const gm_tmpl gm_tpl6rsespo9a6nq;
extern const gm_tmpl gm_tpl6tnfk0d9dl8e;
extern const gm_tmpl gm_tpl7kdhh1egc9gc;
extern const gm_tmpl gm_tpl7medmjmlvos1;
extern const gm_tmpl gm_tpl82mi3mv6t0is;
extern const gm_tmpl gm_tpl8bar0du85qu4;
extern const gm_tmpl gm_tpl8c0nr5c8mas7;
extern const gm_tmpl gm_tpl8gj5o7rmo2fl;
extern const gm_tmpl gm_tpl8qng452btr2a;
extern const gm_tmpl gm_tpla1fscog446dv;
extern const gm_tmpl gm_tpla67mmq1id2h3;
extern const gm_tmpl gm_tplap4dl087fd4l;
extern const gm_tmpl gm_tplb043ibme104j;
extern const gm_tmpl gm_tplbsca6gthghjf;
extern const gm_tmpl gm_tplbtbg1u9fmcqq;
extern const gm_tmpl gm_tplc2ekf83ggtj3;
extern const gm_tmpl gm_tplck20jih4okbk;
extern const gm_tmpl gm_tplcudcaj1iluqd;
extern const gm_tmpl gm_tpld1hhk0kef14l;
extern const gm_tmpl gm_tpld47p9te8ar36;
extern const gm_tmpl gm_tpld9ts015fint3;
extern const gm_tmpl gm_tplda38mm1025ge;
extern const gm_tmpl gm_tpldfm52le63mtn;
extern const gm_tmpl gm_tpldsui47p71ms3;
extern const gm_tmpl gm_tple6jpca094i2j;
extern const gm_tmpl gm_tplej21mtlj140h;
extern const gm_tmpl gm_tplfc5euhhi8t98;
extern const gm_tmpl gm_tplfl5r6601183l;
extern const gm_tmpl gm_tplftchh41b473r;
extern const gm_tmpl gm_tplgism5t1v67p6;
extern const gm_tmpl gm_tplgv9va17kt7ll;
extern const gm_tmpl gm_tplhojm4dgu514i;
extern const gm_tmpl gm_tplhu5cefj26aau;
extern const gm_tmpl gm_tpli824iv2hf4o6;
extern const gm_tmpl gm_tpli9mr2dqh2vqo;
extern const gm_tmpl gm_tplj6imuu9dopvn;
extern const gm_tmpl gm_tplj78420ela1cg;
extern const gm_tmpl gm_tplkpmnsso79v4r;
extern const gm_tmpl gm_tplkpqhnfan9vel;
extern const gm_tmpl gm_tplkr4062p8a8bi;
extern const gm_tmpl gm_tplkv0rjh54m5gn;
extern const gm_tmpl gm_tplkv6ui0mqvq2g;
extern const gm_tmpl gm_tpllinpkb0ktkcf;
extern const gm_tmpl gm_tpllmofqkiu75lh;
extern const gm_tmpl gm_tpllpopiint29pt;
extern const gm_tmpl gm_tplm3a458dq976v;
extern const gm_tmpl gm_tplmg7sb8gq6o3a;
extern const gm_tmpl gm_tplmoo8v17lb0ql;
extern const gm_tmpl gm_tpln1d9amhhlm2o;
extern const gm_tmpl gm_tpln1ol2o7bp7d9;
extern const gm_tmpl gm_tpln4i290aeoim9;
extern const gm_tmpl gm_tplnrfprs01gfmj;
extern const gm_tmpl gm_tplnv6l3ecgreid;
extern const gm_tmpl gm_tplo2l8dmp437h2;
extern const gm_tmpl gm_tplob0n62u6gqqu;
extern const gm_tmpl gm_tplof36cm9vqs8e;
extern const gm_tmpl gm_tplp72dgv8cvduk;
extern const gm_tmpl gm_tplp7srseh4nakh;
extern const gm_tmpl gm_tplpfavv7btb1m5;
extern const gm_tmpl gm_tplpkq4i4j83tmk;
extern const gm_tmpl gm_tplpkvu14cg936d;
extern const gm_tmpl gm_tplq0gioeoiukeh;
extern const gm_tmpl gm_tplq7kai16vns6p;
extern const gm_tmpl gm_tplqn6e8rqmlvpo;
extern const gm_tmpl gm_tplqt9e8rihgogv;
extern const gm_tmpl gm_tplr8vkqgn7raqo;
extern const gm_tmpl gm_tplrqgv2l0evbdg;
extern const gm_tmpl gm_tplrtgv8em132rl;
extern const gm_tmpl gm_tplsjufktgf4o1v;
extern const gm_tmpl gm_tplso413blsqnmm;
extern const gm_tmpl gm_tplt3knh0ehirt7;
extern const gm_tmpl gm_tpltm9r5n97je13;
extern const gm_tmpl gm_tplu7gq2cm8aanh;
extern const gm_tmpl gm_tplu9uohqqitldd;
extern const gm_tmpl gm_tplugmq0iksatjg;
extern const gm_tmpl gm_tplvcccam8nulr1;
extern const gm_tmpl gm_tplvdf3kbq8rjbi;
extern const gm_tmpl gm_tplvj9n0kinsqlj;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0lfe1s2pin(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    return true;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eqasplg81cl8(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqg63o53ar52(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)21) return gm_eqehhdtpeti1(a + 4, b + 4);
    if (ta == (uint8_t)57) return gm_eqasplg81cl8(a + 4, b + 4);
    if (ta == (uint8_t)58) return gm_eqjmccqdb90f(a + 4, b + 4);
    if (ta == (uint8_t)69) return gm_eqov4e3mbi0f(a + 4, b + 4);
    if (ta == (uint8_t)73) return gm_eqtje7cegoml(a + 4, b + 4);
    if (ta == (uint8_t)80) return gm_eq0lfe1s2pin(a + 4, b + 4);
    if (ta == (uint8_t)81) return gm_eq467d5cm1d5(a + 4, b + 4);
    if (ta == (uint8_t)82) return gm_eq467d5cm1d5(a + 4, b + 4);
    return true;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqov4e3mbi0f(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a1));
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t2, t4, t5, t7);
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
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_layout_23rect9 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_bits_layout_u6kkccp((void *)&(*(gm_t57euh612ligr *)gm_out), (void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_kotvo8t(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_kotvo8t F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) void gm_f_format_00b54tv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_00b54tv F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4k5q4ehrakfe));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvcccam8nulr1), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_0gjpujh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0gjpujh F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sl6fvbluu7spr));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(25));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4upfck3qu2er), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_15kpcel(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_15kpcel F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soiotnern2p8m));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6tnfk0d9dl8e), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_21vnq4a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_21vnq4a F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s7n79dja87bqg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(27));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpldfm52le63mtn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_25p33s1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_25p33s1 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjt41g4f7bvrv));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(31));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_h5s5tmr((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl8c0nr5c8mas7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_261kv0l(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_261kv0l F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5t5jm6ask6ip));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplp7srseh4nakh), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_2qdci62(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_2qdci62 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scu4ehdqrc5fh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfl5r6601183l), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_3itad0c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_3itad0c F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqqh9r6c17rju));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsjufktgf4o1v), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_3j59e2u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3j59e2u F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgahfuanpfk2r));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(59));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_65ee4ra((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0sb47rg6hcij), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_3s3fqnk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3s3fqnk F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2ph2ad6lffbq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkv0rjh54m5gn), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_3vgp4uu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3vgp4uu F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sv19dj08f7d6b));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(44));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllinpkb0ktkcf), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_3vtl3te(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3vtl3te F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjtp8ip8t28uh));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqt9e8rihgogv), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_53mv29j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_53mv29j F;
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sd2ioiubqiqs5));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(45));
    gm_tk6rvhh54g86p t3 = GM_LD(gm_tk6rvhh54g86p, ((uint8_t *)a0));
    gm_f_format_abcvqrv((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplck20jih4okbk), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_5rrmec0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5rrmec0 F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_snbn61h56c5fj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(51));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_ee7lhc3((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplpkvu14cg936d), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_68bbqsa(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_68bbqsa F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sadot8stl6s4j));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbsca6gthghjf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_6cn78ik(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_6cn78ik F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_suapcbuahu4i6));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(23));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_74pd3in((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl8qng452btr2a), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6df13cd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6df13cd F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssor6j0m3frue));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplgv9va17kt7ll), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7gtji42(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7gtji42 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spi5fq66572ic));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplo2l8dmp437h2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7lru3c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7lru3c2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stol2i9gi9a35));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvj9n0kinsqlj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_83msomh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_83msomh F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s315r4147rf48));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(39));
    gm_tttl9nsdo1iu6 t3 = GM_LD(gm_tttl9nsdo1iu6, ((uint8_t *)a0));
    gm_f_format_nv3e0h2((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl8bar0du85qu4), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_87dn9rn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_87dn9rn F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_scqp3lehp7mc6));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(16));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpln4i290aeoim9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_898275p(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_898275p F;
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0ku6rqnajsui));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(23));
    gm_tj4tbafenpsjm t3 = GM_LD(gm_tj4tbafenpsjm, ((uint8_t *)a0));
    gm_f_format_m3cufph((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbtbg1u9fmcqq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_8d9jpvv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8d9jpvv F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sader8j76ksps));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmg7sb8gq6o3a), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8m4h4ip(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_8m4h4ip F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sfms0h2mph067));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(10));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplq0gioeoiukeh), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_a1ch927(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a1ch927 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srhrn3fa70ni4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6rsespo9a6nq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a1ec0ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_a1ec0ji F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sh8j22p8lpudd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqn6e8rqmlvpo), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_a7bth9i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a7bth9i F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6sdj2gjsdah5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplpkq4i4j83tmk), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_aculdom(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_aculdom F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svgaurqm99100));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(47));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_peglcgm((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplj6imuu9dopvn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_aghnr00(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_aghnr00 F;
    gm_t5g5vgung6h6s gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_shhak6l2rrn43));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_t5g5vgung6h6s t3 = GM_LD(gm_t5g5vgung6h6s, ((uint8_t *)a0));
    gm_f_format_817tni2((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplu9uohqqitldd), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_b05vb48(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_b05vb48 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su8ulebf2ijla));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1dmvfuuebopd), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_dv5f37i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dv5f37i F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smd81jcp0bga9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(18));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplrtgv8em132rl), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_eqjcikj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_eqjcikj F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smjg62rnkmnon));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(20));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_65ee4ra((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplhojm4dgu514i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_eucvbnh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0) {
    struct gm_fr_format_eucvbnh F;
    gm_tidu2er0d7h95 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tidu2er0d7h95 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scf7jg05jni41));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_6dvr333((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl2dia2gbnfh9l), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fh9to9u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fh9to9u F;
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5dmjeoiprfiv));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(71));
    gm_tk6rvhh54g86p t3 = GM_LD(gm_tk6rvhh54g86p, ((uint8_t *)a0));
    gm_f_format_bdugbhq((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpln1d9amhhlm2o), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fsfutor F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdsb9longvn22));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplu7gq2cm8aanh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_gf4f6rt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_gf4f6rt F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s6j315qbv99q6));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(11));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvdf3kbq8rjbi), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_gp2h96m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_gp2h96m F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7tgpca9g8npn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5vfcnh2qs08h), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_h95c7u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h95c7u1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjpr4fvui63qj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhu5cefj26aau), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_he769rg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_he769rg F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sla3unrbmgc0v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnrfprs01gfmj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_iqjnutf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_iqjnutf F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_suvpgtuh49tp7));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(19));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1rdf0mm7rjgv), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jagvq3q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_jagvq3q F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjr44k79a48mq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplj78420ela1cg), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_jj2aipm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jj2aipm F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2vj6kgk19dag));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplap4dl087fd4l), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_jpvl2js(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jpvl2js F;
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5dqpb4hur24f));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(36));
    gm_th2d9c7aju8hp t3 = GM_LD(gm_th2d9c7aju8hp, ((uint8_t *)a0));
    gm_f_format_jah3ip3((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplb043ibme104j), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_kbs7h9q(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_kbs7h9q F;
    gm_tommf4j9upomc gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_st78509mboaa4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(21));
    gm_tommf4j9upomc t3 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
    gm_f_format_dq6oc41((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplr8vkqgn7raqo), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ljsk7ji(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ljsk7ji F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sp7351fukevmv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpldsui47p71ms3), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lqnh3lt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_lqnh3lt F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sv6dkmq38siqt));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllmofqkiu75lh), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_m0j4lkh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_m0j4lkh F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfnph9ql8pqb1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl0lggla435q5r), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_m7gv7a8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_m7gv7a8 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ssvpi8gpvt9jm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_01lon97((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplej21mtlj140h), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ms7m2d2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ms7m2d2 F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s84bra4h6kp3s));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(56));
    gm_tftksr4op114e t3 = GM_LD(gm_tftksr4op114e, ((uint8_t *)a0));
    gm_f_format_flhcmdb((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl7kdhh1egc9gc), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_nkjq1if(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nkjq1if F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s37otbiaffdaa));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(24));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_65ee4ra((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllpopiint29pt), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_nm3fl8o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nm3fl8o F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smhvmmegaur89));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnv6l3ecgreid), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_o73frof(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_o73frof F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfivm1csbt9ar));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplugmq0iksatjg), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ohk11b5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ohk11b5 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sntddact5d6vp));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpld47p9te8ar36), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_p1ca3i8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_p1ca3i8 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suqogp9sflci8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpltm9r5n97je13), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_patlqq0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_patlqq0 F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5t5jm6ask6ip));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(14));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_v38mj3a((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplp7srseh4nakh), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q296tqt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_q296tqt F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc22qkqa9q462));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkpqhnfan9vel), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qofaffd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qofaffd F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4mij084uljjf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(25));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplcudcaj1iluqd), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_tvr4di5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_tvr4di5 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sukriak12588g));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkpmnsso79v4r), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ucng4n7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_ucng4n7 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3n5bjpleiarb));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfc5euhhi8t98), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_uhjjs86(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_uhjjs86 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfltedqje18on));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpld9ts015fint3), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_v2sfrt0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_v2sfrt0 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl9pujidpnv0r));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplt3knh0ehirt7), t3);
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fresh_5kokjmg F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 176));
    uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 465, 5);
    uint32_t t7 = r5;
    GM_ST(uint32_t, (((uint8_t *)t2) + 176), t7);
    void * t8 = gm_l0;
    void * t9 = gm_l0;
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 304));
    uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 466, 5);
    uint32_t t13 = r11;
    GM_ST(uint32_t, (((uint8_t *)t8) + 304), t13);
    void * t14 = gm_l0;
    uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 176));
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_cty_i2b4e03(gm_cs2lqoac0a5ng4 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ii_cty_i2b4e03 F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK_SHALLOW(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tuglhjjmj2g58, a0);
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&gm_l0));
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_b16_8 t4 = gm_f_int_cty_s62af0i(t2, t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_ii_exact_g6a19dp(gm_cs2lqoac0a5ng4 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ii_exact_g6a19dp F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK_SHALLOW(&F, 47u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tuglhjjmj2g58, a0);
    uint32_t t2 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    uint64_t t3 = (uint64_t)t2;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t r5; bool o6 = __builtin_mul_overflow(t4, ((uint64_t)8ULL), &r5);
    if (o6) gm_trap("integer overflow in `*`", gm_src4pc97o88riqc, 22, 56);
    uint64_t t7 = r5;
    uint8_t t8 = ((t3 == t7));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_ii_ucty_b40h5ot(gm_cs2lqoac0a5ng4 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ii_ucty_b40h5ot F;
    gm_tuglhjjmj2g58 gm_l0; /* ii */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tuglhjjmj2g58, a0);
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_b16_8 t3 = gm_f_int_cty_s62af0i(((uint8_t)0), t2);
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2) {
    struct gm_fr_int_ci6airg F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* signed */
    uint16_t gm_l2; /* bits */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = gm_l1;
    uint16_t t4 = gm_l2;
    gm_tjcj4j2gbh6c8 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), t3);
    GM_ST(uint16_t, ((uint8_t *)&t5 + 2), t4);
    gm_t53at80pfdus3 t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t6 + 8), t5);
    gm_t88rcucg1b7uk t7 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_17uennd F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    uint8_t t6 = gm_f_is_unit_n83ttnb(t4, GM_CONV(gm_csguankko4lmhd, t5));
    F.ret = t6;
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
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_line_l6u87p9 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* s */
    GM_STACK_CHECK(&F, 90u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    gm_b0_1 t3 = gm_f_indent_line_b3mckqa((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t4 = gm_l0;
    void * t5 = (void *)(((uint8_t *)t4) + 128);
    gm_b16_8 t6 = gm_l1;
    gm_tftb1odc739t5 t7 = gm_f_extend_0cs85gs(t5, t6);
    gm_l2 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcmok80ho8hh01, 449, 5);
    void * t9 = gm_l0;
    void * t10 = (void *)(((uint8_t *)t9) + 128);
    gm_tftb1odc739t5 t11 = gm_f_push_t83rg2m(t10, ((uint8_t)10ULL));
    gm_l3 = t11;
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcmok80ho8hh01, 450, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_0kkt9cm(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_0kkt9cm F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stqsuuvp1i0pf));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_b0_1 t4 = gm_f_linef_ao39327((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplc2ekf83ggtj3), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_156hint(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_156hint F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk7bri4eh56ps));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(16));
    gm_b0_1 t4 = gm_f_linef_slpvrc7((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplpfavv7btb1m5), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_21h063v(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_21h063v F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfc28giak7be5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
    gm_b0_1 t4 = gm_f_linef_ao39327((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplkv6ui0mqvq2g), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_3utfuce(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_3utfuce F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s54ap4j24utib));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(68));
    gm_b0_1 t4 = gm_f_linef_gkog1ft((void *)NULL, cx, NULL, t2, &(*(gm_t8fvq98mjik04 *)((uint8_t *)a1)), ((void *)&gm_tpl7medmjmlvos1), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_5q2h82g(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_linef_5q2h82g F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* args */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s5a2m41g390kg));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(53));
    gm_b0_1 t5 = gm_f_linef_eroqj8i((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplp72dgv8cvduk), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_6h7r936(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_6h7r936 F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss1ntuh8relgg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_b0_1 t4 = gm_f_linef_8uttu8h((void *)NULL, cx, NULL, t2, &(*(gm_t0uuhaljsf410 *)((uint8_t *)a1)), ((void *)&gm_tpl8gj5o7rmo2fl), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_6sel63v(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_6sel63v F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_segii2f2mu6lb));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_b0_1 t4 = gm_f_linef_rosnm2q((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplso413blsqnmm), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_6t3ttmd(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_6t3ttmd F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_si5tqjfc8g60q));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_b0_1 t4 = gm_f_linef_rosnm2q((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tpl55mhnmub0ffl), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_7e19lva(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_7e19lva F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sipfpcphbcub1));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(33));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpl2b904kc90480), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_7pb5lg6(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_7pb5lg6 F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s38tgoanp9o78));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(65));
    gm_b0_1 t4 = gm_f_linef_heccqpv((void *)NULL, cx, NULL, t2, &(*(gm_t133qiba7tvhc *)((uint8_t *)a1)), ((void *)&gm_tpl2d8hdhnr24pd), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_a98fois(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_a98fois F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smop6ueqrddak));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(76));
    gm_b0_1 t4 = gm_f_linef_88hn6vm((void *)NULL, cx, NULL, t2, &(*(gm_tsroeipraj6j8 *)((uint8_t *)a1)), ((void *)&gm_tplftchh41b473r), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ahjvru7(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_ahjvru7 F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stjfbh3ag5r41));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_b0_1 t4 = gm_f_linef_rbi6dt9((void *)NULL, cx, NULL, t2, &(*(gm_th2d9c7aju8hp *)((uint8_t *)a1)), ((void *)&gm_tpla67mmq1id2h3), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_bq6igl9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_bq6igl9 F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_siv1vjr1gaenb));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_b0_1 t4 = gm_f_linef_slpvrc7((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpl1f1njss89nj4), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_calnga5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_calnga5 F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sht50491g5jgu));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
    gm_b0_1 t4 = gm_f_linef_8v6p2qr((void *)NULL, cx, NULL, t2, &(*(gm_tif3elqk01bdl *)((uint8_t *)a1)), ((void *)&gm_tplda38mm1025ge), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_di9ea86(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_di9ea86 F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sp2cos6rmlsbv));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(50));
    gm_b0_1 t5 = gm_f_linef_7afcl2l((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplm3a458dq976v), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_evpuk0q(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_evpuk0q F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s873ct0as0vg4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_b0_1 t4 = gm_f_linef_9hv0h73((void *)NULL, cx, NULL, t2, &(*(gm_tg1t0c5hkovn5 *)((uint8_t *)a1)), ((void *)&gm_tplof36cm9vqs8e), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_jl8o1sr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_jl8o1sr F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjv2au7jm0m3j));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(65));
    gm_b0_1 t4 = gm_f_linef_fa323qa((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpli9mr2dqh2vqo), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_kafc4c5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_kafc4c5 F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smsr74eoevopl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(71));
    gm_b0_1 t4 = gm_f_linef_4m3ue17((void *)NULL, cx, NULL, t2, &(*(gm_tpm6m3o0hprmc *)((uint8_t *)a1)), ((void *)&gm_tpli824iv2hf4o6), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_ki2bckn(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_ki2bckn F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2q81sartj882));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
    gm_b0_1 t4 = gm_f_linef_69ggc4q((void *)NULL, cx, NULL, t2, &(*(gm_th2d9c7aju8hp *)((uint8_t *)a1)), ((void *)&gm_tplmoo8v17lb0ql), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_linqala(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_linqala F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soj8art8cj0h6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
    gm_b0_1 t4 = gm_f_linef_ao39327((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplq7kai16vns6p), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_n685kk5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_n685kk5 F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4nk259sisvmu));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(95));
    gm_b0_1 t4 = gm_f_linef_29aidha((void *)NULL, cx, NULL, t2, &(*(gm_tg1t0c5hkovn5 *)((uint8_t *)a1)), ((void *)&gm_tplob0n62u6gqqu), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_qno93os(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_qno93os F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3lqm9addfamn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(55));
    gm_b0_1 t4 = gm_f_linef_ao39327((void *)NULL, cx, NULL, t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplkr4062p8a8bi), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_linef_uu8i6ue F;
    void * gm_l0; /* fg */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5ie9tn1qnf6v));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_b0_1 t4 = gm_f_linef_slpvrc7((void *)NULL, cx, NULL, t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tple6jpca094i2j), t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_v355qja(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_linef_v355qja F;
    void * gm_l0; /* fg */
    gm_tnpifv20irnph gm_l1; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tnpifv20irnph t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_si731m8b0sr6s));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_b0_1 t5 = gm_f_linef_9379f24((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tpla1fscog446dv), t4);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_linef_vgjqckp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tcc1rq4n1avb2 a1) {
    struct gm_fr_linef_vgjqckp F;
    void * gm_l0; /* fg */
    gm_tcc1rq4n1avb2 gm_l1; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tcc1rq4n1avb2 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s9dq8mbgn8du3));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(34));
    gm_b0_1 t5 = gm_f_linef_d96evvt((void *)NULL, cx, NULL, t2, t3, ((void *)&gm_tplrqgv2l0evbdg), t4);
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
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_one_08qg1op F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, ((uint64_t)1ULL));
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bytes_eocneng F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 8) + 8));
    F.ret = t4;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_b16_8 t6 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t6;
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint64_t t5 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
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
    uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_slice_5dt9121 F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* elem */
    GM_STACK_CHECK(&F, 102u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_tbngattl1fo51 t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 4), t4);
    gm_t53at80pfdus3 t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)10ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t6 + 8), t5);
    gm_t88rcucg1b7uk t7 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_slice_len_a3383br F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* addr */
    GM_STACK_CHECK(&F, 169u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    {
        gm_b16_8 t2 = gm_l1;
        void * t3 = gm_l0;
        uint64_t t4 = gm_f_ptr_bytes_eocneng(t3);
        gm_t3877pk7lv748 t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
        GM_ST(uint64_t, ((uint8_t *)&t5 + 16), t4);
        gm_f_format_0gjpujh((void *)&gm_l2, (void *)NULL, cx, NULL, &t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_src0gtuh94b1fce, 301, 72);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_slice_ptr_hojfgr5 F;
    gm_b16_8 gm_l0; /* addr */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l1;
    gm_b16_8 t2 = gm_l0;
    gm_tnpifv20irnph t3;
    GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
    gm_f_format_7lru3c2((void *)&gm_l1, (void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
    if (!t4) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l1) + 8, gm_src0gtuh94b1fce, 299, 60);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    F.ret = t6;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t5 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t2, ((uint8_t)1), t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_kk94re3 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t2, ((uint8_t)0), ((uint16_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_8ekedsl F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    void * t5 = (void *)(((uint8_t *)t4) + 8);
    gm_t88rcucg1b7uk t6 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t7 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t2, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t7;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_ty_vgceilu F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t4);
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
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_unsupported_531q51r F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    {
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_f_format_fsfutor((void *)&gm_l2, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t4) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcd5arorrf8jkg, 38, 27);
    {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_tl874t2atnlh3 t7 = gm_l0;
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0), t6);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16), t7);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_alloc_tag_values_gcte1b4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t8588ma2cr9d7 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_alloc_tag_values_gcte1b4 F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t8588ma2cr9d7 gm_l2; /* op */
    gm_b16_8 gm_l3; /* tys */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l5; /* loc */
    gm_b16_8 gm_l6; /* v */
    gm_b16_8 gm_l7; /* fa */
    gm_b16_8 gm_l8; /* fp */
    gm_b16_8 gm_l9; /* fl */
    gm_b16_8 gm_l10; /* line */
    gm_b16_8 gm_l11; /* col */
    GM_STACK_CHECK(&F, 1164u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_b16_8 t5 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t5;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t7) + 1136) + 1));
    uint8_t t9 = (uint8_t)!t8;
    if (t9) {
        void * t10 = gm_l0;
        gm_b16_8 t11 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t11;
        goto gm_fret;
    }
    gm_b16_8 t12 = gm_l4;
    gm_l6 = t12;
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t15, gm_src2j6ljeja7ilp, 163, 22);
    gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t14) + (uintptr_t)((uint64_t)3ULL) * 4));
    void * t17 = GM_LD(void *, ((uint8_t *)&gm_l4));
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t18, gm_src2j6ljeja7ilp, 163, 30);
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t17) + (uintptr_t)((uint64_t)3ULL) * 16));
    gm_b16_8 t20 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t13, t16, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t20;
    gm_b16_8 t21 = gm_l7;
    gm_b16_8 t22 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t22;
    void * t23 = gm_l0;
    gm_b16_8 t24 = gm_l7;
    gm_b16_8 t25 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t23, t24);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t25;
    void * t26 = GM_LD(void *, ((uint8_t *)&gm_l4));
    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t27, gm_src2j6ljeja7ilp, 166, 13);
    gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)t26) + (uintptr_t)((uint64_t)4ULL) * 16));
    gm_l10 = t28;
    void * t29 = GM_LD(void *, ((uint8_t *)&gm_l4));
    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    if ((uintptr_t)((uint64_t)5ULL) >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)((uint64_t)5ULL), t30, gm_src2j6ljeja7ilp, 167, 12);
    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t29) + (uintptr_t)((uint64_t)5ULL) * 16));
    gm_l11 = t31;
    gm_t8588ma2cr9d7 t32 = gm_l2;
    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t32) + 0));
    uint8_t t34 = (uint8_t)((t33 == ((uint8_t)77ULL)));
    gm_t8588ma2cr9d7 t35;
    GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t35 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)77ULL));
    if (t34) {
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, ((uint8_t *)&gm_l6));
        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t38, gm_src2j6ljeja7ilp, 169, 63);
        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t37) + (uintptr_t)((uint64_t)0ULL) * 16));
        void * t40 = GM_LD(void *, ((uint8_t *)&gm_l6));
        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t41, gm_src2j6ljeja7ilp, 169, 69);
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)t40) + (uintptr_t)((uint64_t)1ULL) * 16));
        void * t43 = GM_LD(void *, ((uint8_t *)&gm_l6));
        uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t44)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t44, gm_src2j6ljeja7ilp, 169, 75);
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)t43) + (uintptr_t)((uint64_t)2ULL) * 16));
        gm_b16_8 t46 = gm_l8;
        gm_b16_8 t47 = gm_l9;
        gm_b16_8 t48 = gm_l10;
        gm_b16_8 t49 = gm_l11;
        gm_th2d9c7aju8hp t50;
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 0), t39);
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 16), t42);
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 32), t45);
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 48), t46);
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 64), t47);
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 80), t48);
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 96), t49);
        gm_b0_1 t51 = gm_f_linef_ki2bckn((void *)NULL, cx, NULL, t36, &t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    } else {
        void * t52 = gm_l0;
        void * t53 = GM_LD(void *, ((uint8_t *)&gm_l6));
        uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t54)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t54, gm_src2j6ljeja7ilp, 172, 69);
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)t53) + (uintptr_t)((uint64_t)0ULL) * 16));
        void * t56 = GM_LD(void *, ((uint8_t *)&gm_l6));
        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t57)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t57, gm_src2j6ljeja7ilp, 172, 75);
        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)t56) + (uintptr_t)((uint64_t)1ULL) * 16));
        void * t59 = GM_LD(void *, ((uint8_t *)&gm_l6));
        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t60)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t60, gm_src2j6ljeja7ilp, 172, 81);
        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)t59) + (uintptr_t)((uint64_t)2ULL) * 16));
        gm_b16_8 t62 = gm_l8;
        gm_b16_8 t63 = gm_l9;
        gm_b16_8 t64 = gm_l10;
        gm_b16_8 t65 = gm_l11;
        gm_b16_8 t66 = gm_l5;
        gm_tg1t0c5hkovn5 t67;
        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t55);
        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 16), t58);
        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 32), t61);
        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 48), t62);
        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 64), t63);
        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 80), t64);
        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 96), t65);
        GM_ST(gm_b16_8, ((uint8_t *)&t67 + 112), t66);
        gm_b0_1 t68 = gm_f_linef_evpuk0q((void *)NULL, cx, NULL, t52, &t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t69 = gm_l0;
    gm_b16_8 t70 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t69);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t70;
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
__attribute__((aligned(4))) void gm_f_atomic_plj0t12(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t8588ma2cr9d7 a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_atomic_plj0t12 F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_t8588ma2cr9d7 gm_l3; /* op */
    gm_b16_8 gm_l4; /* tys */
    gm_b16_8 gm_l5; /* vals */
    gm_b16_8 gm_l6; /* v0 */
    gm_t88rcucg1b7uk gm_l7; /* at */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    gm_b16_8 gm_l9; /* c */
    gm_b16_8 gm_l10; /* p */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    gm_b16_8 gm_l15; /* a */
    gm_b16_8 gm_l16; /* exp */
    gm_b16_8 gm_l17; /* ok */
    gm_b16_8 gm_l18; /* res */
    gm_b16_8 gm_l19; /* none */
    gm_b16_8 gm_l20; /* some */
    gm_t9qkh0rsooc8d gm_l21; /* rmw */
    gm_th2lqbo77pvsc gm_l22; /* f */
    gm_b16_8 gm_l23; /* fname */
    gm_b16_8 gm_l24; /* cmp */
    gm_b16_8 gm_l25; /* old */
    gm_b16_8 gm_l26; /* v */
    GM_STACK_CHECK(&F, 3391u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_t53at80pfdus3 gm_l27;
    gm_tabb710d9mq2e gm_l28;
    uint8_t gm_l29;
    gm_t53at80pfdus3 gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tabb710d9mq2e gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_tabb710d9mq2e gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_tabb710d9mq2e gm_l37;
    gm_tabb710d9mq2e gm_l38;
    {
        void * t33 = GM_LD(void *, ((uint8_t *)&gm_l5));
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t34)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t34, gm_src2j6ljeja7ilp, 263, 11);
        gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t33) + (uintptr_t)((uint64_t)0ULL) * 16));
        gm_l6 = t35;
        void * t36 = GM_LD(void *, ((uint8_t *)&gm_l4));
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t37)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t37, gm_src2j6ljeja7ilp, 264, 11);
        gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t36) + (uintptr_t)((uint64_t)0ULL) * 4));
        gm_l7 = t38;
        {
            {
                {
                    {
                        void * t39 = gm_l0;
                        void * t40 = GM_LD(void *, ((uint8_t *)t39));
                        void * t41 = (void *)(((uint8_t *)t40) + 8);
                        gm_t88rcucg1b7uk t42 = gm_l7;
                        gm_f_kind_ghvav22((void *)&gm_l27, t41, GM_CONV(gm_csguankko4lmhd, t42));
                        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                        uint8_t t44 = (uint8_t)(t43 == ((uint8_t)8ULL));
                        if (!t44) {
                            goto gm_lb2;
                        }
                        gm_t88rcucg1b7uk t45 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l27) + 8) + 4));
                        gm_l8 = t45;
                        void * t46 = gm_l0;
                        void * t47 = GM_LD(void *, ((uint8_t *)t46));
                        void * t48 = (void *)(((uint8_t *)t47) + 8);
                        gm_t88rcucg1b7uk t49 = gm_l8;
                        gm_b16_8 t50 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t48, t49);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l9 = t50;
                        gm_b16_8 t51 = gm_l9;
                        gm_b16_8 t52 = gm_l6;
                        gm_t6j6smj7qoctu t53;
                        GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t51);
                        GM_ST(gm_b16_8, ((uint8_t *)&t53 + 16), t52);
                        gm_f_format_8m4h4ip((void *)&gm_l28, (void *)NULL, cx, NULL, &t53);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                        if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_src2j6ljeja7ilp, 267, 10);
                        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                        gm_l10 = t55;
                        gm_trqfvmjtt4sij t56;
                        gm_f_list_epvducg((void *)&t56, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l11 = t56;
                        gm_l13 = ((uint64_t)1ULL);
                        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        gm_l12 = t57;
                        {
                            for (;;) {
                                uint64_t t58 = gm_l13;
                                uint64_t t59 = gm_l12;
                                uint8_t t60 = (t58 < t59);
                                if (!t60) {
                                    goto gm_lb3;
                                }
                                {
                                    {
                                        {
                                            uint64_t t61 = gm_l13;
                                            gm_l14 = t61;
                                            {
                                                void * t62 = gm_l0;
                                                void * t63 = GM_LD(void *, ((uint8_t *)t62));
                                                void * t64 = (void *)(((uint8_t *)t63) + 8);
                                                void * t65 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                uint64_t t67 = gm_l14;
                                                gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t65) + (uintptr_t)t67 * 4));
                                                gm_f_kind_ghvav22((void *)&gm_l30, t64, GM_CONV(gm_csguankko4lmhd, t68));
                                            }
                                            {
                                                {
                                                    uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l30) + 0));
                                                    uint8_t t70 = (uint8_t)(t69 == ((uint8_t)17ULL));
                                                    if (!t70) {
                                                        goto gm_lb9;
                                                    }
                                                    gm_l29 = ((uint8_t)0);
                                                    goto gm_lb8;
                                                }
gm_lb9:;
                                                {
                                                    gm_l29 = ((uint8_t)1);
                                                    goto gm_lb8;
                                                }
gm_lb10:;
                                                gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 270, 12);
                                            }
gm_lb8:;
                                            uint8_t t71 = gm_l29;
                                            if (t71) {
                                                goto gm_lb5;
                                            }
                                            void * t72 = gm_l0;
                                            void * t73 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                            uint64_t t74 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                            uint64_t t75 = gm_l14;
                                            gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t73) + (uintptr_t)t75 * 4));
                                            void * t77 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                            uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                            uint64_t t79 = gm_l14;
                                            if ((uintptr_t)t79 >= (uintptr_t)(t78)) gm_trap_index((uintptr_t)t79, t78, gm_src2j6ljeja7ilp, 271, 33);
                                            gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)t77) + (uintptr_t)t79 * 16));
                                            gm_b16_8 t81 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t72, t76, t80);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l15 = t81;
                                            {
                                                void * t82 = (void *)((uint8_t *)&F.l11);
                                                gm_b16_8 t83 = gm_l15;
                                                gm_tnpifv20irnph t84;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t84 + 0), t83);
                                                gm_f_format_8d9jpvv((void *)&gm_l31, (void *)NULL, cx, NULL, t84);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t85 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                                                if (!t85) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_src2j6ljeja7ilp, 272, 19);
                                                gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                                                gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                                                gm_tftb1odc739t5 t88 = gm_f_push_3ef746b(t82, t87);
                                                gm_l32 = t88;
                                            }
                                            uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                            if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_src2j6ljeja7ilp, 272, 9);
                                            goto gm_lb6;
                                        }
gm_lb7:;
                                        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 269, 5);
                                    }
gm_lb6:;
                                }
gm_lb5:;
                                uint64_t t90 = gm_l13;
                                uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                                uint64_t t93 = r91;
                                gm_l13 = t93;
                            }
                        }
gm_lb3:;
                    }
                    {
                        {
                            {
                                {
                                    uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                    uint8_t t95 = (uint8_t)(t94 == ((uint8_t)71ULL));
                                    if (!t95) {
                                        goto gm_lb12;
                                    }
                                    {
                                        void * t96 = gm_l0;
                                        gm_t88rcucg1b7uk t97 = gm_l2;
                                        gm_b16_8 t98 = gm_l9;
                                        gm_b16_8 t99 = gm_l6;
                                        gm_t6j6smj7qoctu t100;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t100 + 0), t98);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t100 + 16), t99);
                                        gm_f_format_dv5f37i((void *)&gm_l33, (void *)NULL, cx, NULL, &t100);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                        if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_src2j6ljeja7ilp, 275, 43);
                                        gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                                        gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&t102));
                                        gm_b16_8 t104 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t96, t97, t103);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t104);
                                        goto gm_fdone;
                                    }
                                }
gm_lb12:;
                                {
                                    uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                    uint8_t t106 = (uint8_t)(t105 == ((uint8_t)72ULL));
                                    if (!t106) {
                                        goto gm_lb13;
                                    }
                                    {
                                        void * t107 = gm_l0;
                                        gm_b16_8 t108 = gm_l9;
                                        gm_b16_8 t109 = gm_l6;
                                        void * t110 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                        uint64_t t111 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t111)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t111, gm_src2j6ljeja7ilp, 277, 57);
                                        gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)t110) + (uintptr_t)((uint64_t)1ULL) * 16));
                                        gm_tfeab279c16rr t113;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t113 + 0), t108);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t113 + 16), t109);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t113 + 32), t112);
                                        gm_b0_1 t114 = gm_f_linef_linqala((void *)NULL, cx, NULL, t107, &t113);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    {
                                        void * t115 = gm_l0;
                                        gm_b16_8 t116 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t115);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t116);
                                        goto gm_fdone;
                                    }
                                }
gm_lb13:;
                                {
                                    uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                    uint8_t t118 = (uint8_t)(t117 == ((uint8_t)65ULL));
                                    if (!t118) {
                                        goto gm_lb14;
                                    }
                                    {
                                        void * t119 = gm_l0;
                                        gm_t88rcucg1b7uk t120 = gm_l2;
                                        gm_b16_8 t121 = gm_l10;
                                        void * t122 = GM_LD(void *, ((uint8_t *)&F.l11));
                                        uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t123)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t123, gm_src2j6ljeja7ilp, 280, 82);
                                        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)t122) + (uintptr_t)((uint64_t)0ULL) * 16));
                                        gm_t6j6smj7qoctu t125;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t125 + 0), t121);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t125 + 16), t124);
                                        gm_f_format_6cn78ik((void *)&gm_l34, (void *)NULL, cx, NULL, &t125);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t126 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                        if (!t126) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_src2j6ljeja7ilp, 280, 41);
                                        gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                                        gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
                                        gm_b16_8 t129 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t119, t120, t128);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t129);
                                        goto gm_fdone;
                                    }
                                }
gm_lb14:;
                                {
                                    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                    uint8_t t131 = (uint8_t)(t130 == ((uint8_t)66ULL));
                                    if (!t131) {
                                        goto gm_lb15;
                                    }
                                    {
                                        void * t132 = gm_l0;
                                        gm_b16_8 t133 = gm_l10;
                                        void * t134 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                        uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t135)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t135, gm_src2j6ljeja7ilp, 282, 58);
                                        gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)t134) + (uintptr_t)((uint64_t)1ULL) * 16));
                                        void * t137 = GM_LD(void *, ((uint8_t *)&F.l11));
                                        uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t138)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t138, gm_src2j6ljeja7ilp, 282, 67);
                                        gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)t137) + (uintptr_t)((uint64_t)0ULL) * 16));
                                        gm_tfeab279c16rr t140;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t140 + 0), t133);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t140 + 16), t136);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t140 + 32), t139);
                                        gm_b0_1 t141 = gm_f_linef_6t3ttmd((void *)NULL, cx, NULL, t132, &t140);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    {
                                        void * t142 = gm_l0;
                                        gm_b16_8 t143 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t142);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t143);
                                        goto gm_fdone;
                                    }
                                }
gm_lb15:;
                            }
                            {
                                {
                                    uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                    uint8_t t145 = (uint8_t)(t144 == ((uint8_t)67ULL));
                                    if (!t145) {
                                        goto gm_lb16;
                                    }
                                    {
                                        void * t146 = gm_l0;
                                        gm_t88rcucg1b7uk t147 = gm_l2;
                                        gm_b16_8 t148 = gm_l10;
                                        void * t149 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                        uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t150)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t150, gm_src2j6ljeja7ilp, 285, 90);
                                        gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)t149) + (uintptr_t)((uint64_t)1ULL) * 16));
                                        void * t152 = GM_LD(void *, ((uint8_t *)&F.l11));
                                        uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t153)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t153, gm_src2j6ljeja7ilp, 285, 99);
                                        gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)t152) + (uintptr_t)((uint64_t)0ULL) * 16));
                                        gm_tfeab279c16rr t155;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t155 + 0), t148);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t155 + 16), t151);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t155 + 32), t154);
                                        gm_f_format_25p33s1((void *)&gm_l35, (void *)NULL, cx, NULL, &t155);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t156 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                        if (!t156) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_src2j6ljeja7ilp, 285, 41);
                                        gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                        gm_b16_8 t158 = GM_LD(gm_b16_8, ((uint8_t *)&t157));
                                        gm_b16_8 t159 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t146, t147, t158);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t159);
                                        goto gm_fdone;
                                    }
                                }
gm_lb16:;
                                {
                                    uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                    uint8_t t161 = (uint8_t)(t160 == ((uint8_t)68ULL));
                                    if (!t161) {
                                        goto gm_lb17;
                                    }
                                    void * t162 = gm_l0;
                                    gm_t88rcucg1b7uk t163 = gm_l8;
                                    void * t164 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                    uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t165)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t165, gm_src2j6ljeja7ilp, 287, 33);
                                    gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)t164) + (uintptr_t)((uint64_t)1ULL) * 16));
                                    gm_b16_8 t167 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t162, t163, t166);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l16 = t167;
                                    {
                                        void * t168 = gm_l0;
                                        void * t169 = gm_l0;
                                        void * t170 = GM_LD(void *, ((uint8_t *)t169));
                                        void * t171 = (void *)(((uint8_t *)t170) + 8);
                                        gm_t88rcucg1b7uk t172 = gm_f_bool_ty_kotvo8t((void *)NULL, cx, NULL, t171);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t173 = gm_l10;
                                        gm_b16_8 t174 = gm_l16;
                                        void * t175 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                        uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                        if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t176)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t176, gm_src2j6ljeja7ilp, 288, 119);
                                        gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)t175) + (uintptr_t)((uint64_t)2ULL) * 16));
                                        void * t178 = GM_LD(void *, ((uint8_t *)&F.l11));
                                        uint64_t t179 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t179)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t179, gm_src2j6ljeja7ilp, 288, 128);
                                        gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)t178) + (uintptr_t)((uint64_t)0ULL) * 16));
                                        void * t181 = GM_LD(void *, ((uint8_t *)&F.l11));
                                        uint64_t t182 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t182)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t182, gm_src2j6ljeja7ilp, 288, 143);
                                        gm_b16_8 t183 = GM_LD(gm_b16_8, (((uint8_t *)t181) + (uintptr_t)((uint64_t)1ULL) * 16));
                                        gm_tud7oovfvnf43 t184;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t184 + 0), t173);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t184 + 16), t174);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t184 + 32), t177);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t184 + 48), t180);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t184 + 64), t183);
                                        gm_f_format_5rrmec0((void *)&gm_l36, (void *)NULL, cx, NULL, &t184);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t185 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                        if (!t185) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_src2j6ljeja7ilp, 288, 45);
                                        gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                                        gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&t186));
                                        gm_b16_8 t188 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t168, t172, t187);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l17 = t188;
                                    }
                                    void * t189 = gm_l0;
                                    gm_t88rcucg1b7uk t190 = gm_l2;
                                    gm_b16_8 t191 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t189, t190);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l18 = t191;
                                    void * t192 = gm_l0;
                                    gm_t88rcucg1b7uk t193 = gm_l2;
                                    gm_b16_8 t194 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t192, t193);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l19 = t194;
                                    void * t195 = gm_l0;
                                    gm_t88rcucg1b7uk t196 = gm_l2;
                                    gm_b16_8 t197 = gm_l16;
                                    gm_b16_8 t198 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t195, t196, t197);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l20 = t198;
                                    {
                                        void * t199 = gm_l0;
                                        gm_b16_8 t200 = gm_l18;
                                        gm_b16_8 t201 = gm_l17;
                                        gm_b16_8 t202 = gm_l19;
                                        gm_b16_8 t203 = gm_l20;
                                        gm_t0uuhaljsf410 t204;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t204 + 0), t200);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t204 + 16), t201);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t204 + 32), t202);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t204 + 48), t203);
                                        gm_b0_1 t205 = gm_f_linef_6h7r936((void *)NULL, cx, NULL, t199, &t204);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    {
                                        gm_b16_8 t206 = gm_l18;
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t206);
                                        goto gm_fdone;
                                    }
                                }
gm_lb17:;
                            }
                            {
                                {
                                    uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                    uint8_t t208 = (uint8_t)(t207 == ((uint8_t)69ULL));
                                    if (!t208) {
                                        goto gm_lb18;
                                    }
                                    gm_t9qkh0rsooc8d t209 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 4));
                                    gm_l21 = t209;
                                    {
                                        {
                                            uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                            uint8_t t211 = (uint8_t)(t210 == ((uint8_t)0ULL));
                                            if (!t211) {
                                                goto gm_lb20;
                                            }
                                            gm_b16_8 t212;
                                            GM_ST(void *, (uint8_t *)&t212, (void *)(gm_si0mnqg6j1eu8));
                                            GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(18));
                                            GM_ST(uint8_t, (uint8_t *)&gm_l22 + 17, 0);
                                            GM_ST(uint16_t, (uint8_t *)&gm_l22 + 18, 0);
                                            GM_ST(uint32_t, (uint8_t *)&gm_l22 + 20, 0);
                                            GM_ST(gm_b16_8, ((uint8_t *)&gm_l22), t212);
                                            ((uint8_t *)&gm_l22)[16] = 1;
                                            goto gm_lb19;
                                        }
gm_lb20:;
                                        {
                                            uint8_t t213 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                            uint8_t t214 = (uint8_t)(t213 == ((uint8_t)1ULL));
                                            if (!t214) {
                                                goto gm_lb21;
                                            }
                                            gm_b16_8 t215;
                                            GM_ST(void *, (uint8_t *)&t215, (void *)(gm_sdeu8nuc597e8));
                                            GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(18));
                                            GM_ST(uint8_t, (uint8_t *)&gm_l22 + 17, 0);
                                            GM_ST(uint16_t, (uint8_t *)&gm_l22 + 18, 0);
                                            GM_ST(uint32_t, (uint8_t *)&gm_l22 + 20, 0);
                                            GM_ST(gm_b16_8, ((uint8_t *)&gm_l22), t215);
                                            ((uint8_t *)&gm_l22)[16] = 1;
                                            goto gm_lb19;
                                        }
gm_lb21:;
                                        {
                                            uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                            uint8_t t217 = (uint8_t)(t216 == ((uint8_t)2ULL));
                                            if (!t217) {
                                                goto gm_lb22;
                                            }
                                            gm_b16_8 t218;
                                            GM_ST(void *, (uint8_t *)&t218, (void *)(gm_s42h828lb45uf));
                                            GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(18));
                                            GM_ST(uint8_t, (uint8_t *)&gm_l22 + 17, 0);
                                            GM_ST(uint16_t, (uint8_t *)&gm_l22 + 18, 0);
                                            GM_ST(uint32_t, (uint8_t *)&gm_l22 + 20, 0);
                                            GM_ST(gm_b16_8, ((uint8_t *)&gm_l22), t218);
                                            ((uint8_t *)&gm_l22)[16] = 1;
                                            goto gm_lb19;
                                        }
gm_lb22:;
                                        {
                                            uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                            uint8_t t220 = (uint8_t)(t219 == ((uint8_t)3ULL));
                                            if (!t220) {
                                                goto gm_lb23;
                                            }
                                            gm_b16_8 t221;
                                            GM_ST(void *, (uint8_t *)&t221, (void *)(gm_sm0f8bs4gd2ma));
                                            GM_ST(uintptr_t, (uint8_t *)&t221 + 8, (uintptr_t)(17));
                                            GM_ST(uint8_t, (uint8_t *)&gm_l22 + 17, 0);
                                            GM_ST(uint16_t, (uint8_t *)&gm_l22 + 18, 0);
                                            GM_ST(uint32_t, (uint8_t *)&gm_l22 + 20, 0);
                                            GM_ST(gm_b16_8, ((uint8_t *)&gm_l22), t221);
                                            ((uint8_t *)&gm_l22)[16] = 1;
                                            goto gm_lb19;
                                        }
gm_lb23:;
                                        {
                                            uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                            uint8_t t223 = (uint8_t)(t222 == ((uint8_t)4ULL));
                                            if (!t223) {
                                                goto gm_lb24;
                                            }
                                            gm_b16_8 t224;
                                            GM_ST(void *, (uint8_t *)&t224, (void *)(gm_sg3ba4vpic0jb));
                                            GM_ST(uintptr_t, (uint8_t *)&t224 + 8, (uintptr_t)(18));
                                            GM_ST(uint8_t, (uint8_t *)&gm_l22 + 17, 0);
                                            GM_ST(uint16_t, (uint8_t *)&gm_l22 + 18, 0);
                                            GM_ST(uint32_t, (uint8_t *)&gm_l22 + 20, 0);
                                            GM_ST(gm_b16_8, ((uint8_t *)&gm_l22), t224);
                                            ((uint8_t *)&gm_l22)[16] = 1;
                                            goto gm_lb19;
                                        }
gm_lb24:;
                                        {
                                            {
                                                {
                                                    uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                                    uint8_t t226 = (uint8_t)(t225 == ((uint8_t)5ULL));
                                                    if (!t226) {
                                                        goto gm_lb27;
                                                    }
                                                    goto gm_lb26;
                                                }
gm_lb27:;
                                                {
                                                    uint8_t t227 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                                    uint8_t t228 = (uint8_t)(t227 == ((uint8_t)6ULL));
                                                    if (!t228) {
                                                        goto gm_lb28;
                                                    }
                                                    goto gm_lb26;
                                                }
gm_lb28:;
                                                goto gm_lb25;
                                            }
gm_lb26:;
                                            memset(&gm_l22, 0, sizeof gm_l22);
                                            goto gm_lb19;
                                        }
gm_lb25:;
                                        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 296, 26);
                                    }
gm_lb19:;
                                    {
                                        {
                                            uint8_t t229 = (uint8_t)((((uint8_t *)&gm_l22))[16] != 0);
                                            if (!t229) {
                                                goto gm_lb30;
                                            }
                                            gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
                                            gm_l23 = t230;
                                            {
                                                void * t231 = gm_l0;
                                                gm_t88rcucg1b7uk t232 = gm_l2;
                                                gm_b16_8 t233 = gm_l23;
                                                gm_b16_8 t234 = gm_l10;
                                                void * t235 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                                uint64_t t236 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                                if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t236)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t236, gm_src2j6ljeja7ilp, 305, 73);
                                                gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)t235) + (uintptr_t)((uint64_t)1ULL) * 16));
                                                void * t238 = GM_LD(void *, ((uint8_t *)&F.l11));
                                                uint64_t t239 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t239)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t239, gm_src2j6ljeja7ilp, 305, 82);
                                                gm_b16_8 t240 = GM_LD(gm_b16_8, (((uint8_t *)t238) + (uintptr_t)((uint64_t)0ULL) * 16));
                                                gm_t0uuhaljsf410 t241;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t241 + 0), t233);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t241 + 16), t234);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t241 + 32), t237);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t241 + 48), t240);
                                                gm_f_format_patlqq0((void *)&gm_l37, (void *)NULL, cx, NULL, &t241);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t242 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                                if (!t242) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_src2j6ljeja7ilp, 305, 34);
                                                gm_b16_8 t243 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                                                gm_b16_8 t244 = GM_LD(gm_b16_8, ((uint8_t *)&t243));
                                                gm_b16_8 t245 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t231, t232, t244);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t245);
                                                goto gm_fdone;
                                            }
                                        }
gm_lb30:;
                                    }
gm_lb29:;
                                    gm_t9qkh0rsooc8d t246 = gm_l21;
                                    uint8_t t247 = GM_LD(uint8_t, (((uint8_t *)&t246) + 0));
                                    uint8_t t248 = (uint8_t)((t247 == ((uint8_t)5ULL)));
                                    gm_t9qkh0rsooc8d t249;
                                    GM_ST(uint8_t, ((uint8_t *)&t249 + 0), ((uint8_t)5ULL));
                                    if (t248) {
                                        gm_b16_8 t250;
                                        GM_ST(void *, (uint8_t *)&t250, (void *)(gm_sm5be4659ulci));
                                        GM_ST(uintptr_t, (uint8_t *)&t250 + 8, (uintptr_t)(1));
                                        gm_l24 = t250;
                                    } else {
                                        gm_b16_8 t251;
                                        GM_ST(void *, (uint8_t *)&t251, (void *)(gm_s1f440st4mnkp));
                                        GM_ST(uintptr_t, (uint8_t *)&t251 + 8, (uintptr_t)(1));
                                        gm_l24 = t251;
                                    }
                                    {
                                        void * t252 = gm_l0;
                                        gm_t88rcucg1b7uk t253 = gm_l8;
                                        gm_b16_8 t254 = gm_l10;
                                        gm_tnpifv20irnph t255;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t255 + 0), t254);
                                        gm_f_format_a1ch927((void *)&gm_l38, (void *)NULL, cx, NULL, t255);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t256 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                        if (!t256) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_src2j6ljeja7ilp, 308, 33);
                                        gm_b16_8 t257 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                                        gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)&t257));
                                        gm_b16_8 t259 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t252, t253, t258);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l25 = t259;
                                    }
                                    void * t260 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                    uint64_t t261 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t261)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t261, gm_src2j6ljeja7ilp, 309, 18);
                                    gm_b16_8 t262 = GM_LD(gm_b16_8, (((uint8_t *)t260) + (uintptr_t)((uint64_t)1ULL) * 16));
                                    gm_l26 = t262;
                                    {
                                        void * t263 = gm_l0;
                                        gm_b16_8 t264 = gm_l10;
                                        gm_b16_8 t265 = gm_l25;
                                        gm_b16_8 t266 = gm_l26;
                                        gm_b16_8 t267 = gm_l24;
                                        gm_b16_8 t268 = gm_l25;
                                        gm_b16_8 t269 = gm_l26;
                                        gm_b16_8 t270 = gm_l25;
                                        void * t271 = GM_LD(void *, ((uint8_t *)&F.l11));
                                        uint64_t t272 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t272)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t272, gm_src2j6ljeja7ilp, 310, 150);
                                        gm_b16_8 t273 = GM_LD(gm_b16_8, (((uint8_t *)t271) + (uintptr_t)((uint64_t)0ULL) * 16));
                                        gm_tg1t0c5hkovn5 t274;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t274 + 0), t264);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t274 + 16), t265);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t274 + 32), t266);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t274 + 48), t267);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t274 + 64), t268);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t274 + 80), t269);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t274 + 96), t270);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t274 + 112), t273);
                                        gm_b0_1 t275 = gm_f_linef_n685kk5((void *)NULL, cx, NULL, t263, &t274);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    {
                                        gm_b16_8 t276 = gm_l25;
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t276);
                                        goto gm_fdone;
                                    }
                                }
gm_lb18:;
                            }
                            {
                                {
                                    gm_trap("reached `@unreachable()`", gm_src2j6ljeja7ilp, 313, 14);
                                }
gm_lb31:;
                                gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 274, 5);
                            }
                        }
gm_lb11:;
                    }
                }
gm_lb2:;
            }
            {
                gm_tl874t2atnlh3 t277 = gm_l1;
                gm_b16_8 t278;
                GM_ST(void *, (uint8_t *)&t278, (void *)(gm_sn8alsh9na9ne));
                GM_ST(uintptr_t, (uint8_t *)&t278 + 8, (uintptr_t)(11));
                gm_t08olq97rscn3 t279;
                gm_f_unsupported_531q51r((void *)&t279, (void *)NULL, cx, NULL, t277, t278);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t279);
                goto gm_fdone;
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
__attribute__((aligned(4))) void gm_f_bit_count_77uni44(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t8588ma2cr9d7 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5) {
    struct gm_fr_bit_count_77uni44 F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_t8588ma2cr9d7 gm_l3; /* op */
    gm_t88rcucg1b7uk gm_l4; /* at */
    gm_b16_8 gm_l5; /* v0 */
    gm_tuglhjjmj2g58 gm_l6; /* ii */
    gm_b16_8 gm_l7; /* x0 */
    gm_b16_8 gm_l8; /* x */
    gm_b16_8 gm_l9; /* c */
    gm_b16_8 gm_l10; /* call */
    gm_b16_8 gm_l11; /* r */
    gm_tuglhjjmj2g58 gm_l12; /* ri */
    gm_b16_8 gm_l13; /* n */
    GM_STACK_CHECK(&F, 1482u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tqcc6dmlne11b gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tqcc6dmlne11b gm_l23;
    {
        void * t10 = gm_l0;
        gm_t88rcucg1b7uk t11 = gm_l4;
        gm_f_int_info_2tcdf1n((void *)&gm_l14, (void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
    if (!t12) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 212, 11);
    gm_tuglhjjmj2g58 t13 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&gm_l14));
    gm_l6 = t13;
    {
        gm_tuglhjjmj2g58 t14 = gm_l6;
        gm_b16_8 t15 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t14));
        gm_b16_8 t16 = gm_l5;
        gm_t6j6smj7qoctu t17;
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t15);
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t16);
        gm_f_format_21vnq4a((void *)&gm_l15, (void *)NULL, cx, NULL, &t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src2j6ljeja7ilp, 213, 11);
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
    gm_l7 = t19;
    uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
    uint8_t t21 = (t20 >= ((uint32_t)128ULL));
    if (t21) {
        gm_b16_8 t22 = gm_l7;
        gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
        gm_l8 = t23;
    } else {
        {
            gm_b16_8 t24 = gm_l7;
            uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
            gm_tommf4j9upomc t26;
            GM_ST(uint32_t, (uint8_t *)&t26 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t24);
            GM_ST(uint32_t, ((uint8_t *)&t26 + 16), t25);
            gm_f_format_s2ml2tu((void *)&gm_l16, (void *)NULL, cx, NULL, &t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_src2j6ljeja7ilp, 215, 49);
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        gm_l8 = t29;
    }
    void * t30 = gm_l0;
    void * t31 = GM_LD(void *, ((uint8_t *)t30));
    void * t32 = (void *)(((uint8_t *)t31) + 8);
    gm_t88rcucg1b7uk t33 = gm_l2;
    gm_b16_8 t34 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t32, t33);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t34;
    {
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)28ULL));
            if (!t36) {
                goto gm_lb2;
            }
            {
                gm_b16_8 t37 = gm_l8;
                uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
                gm_tommf4j9upomc t39;
                GM_ST(uint32_t, (uint8_t *)&t39 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t37);
                GM_ST(uint32_t, ((uint8_t *)&t39 + 16), t38);
                gm_f_format_c9b4vl3((void *)&gm_l17, (void *)NULL, cx, NULL, &t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_src2j6ljeja7ilp, 218, 17);
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_l10 = t42;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)29ULL));
            if (!t44) {
                goto gm_lb3;
            }
            {
                gm_b16_8 t45 = gm_l8;
                uint32_t t46 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
                gm_tommf4j9upomc t47;
                GM_ST(uint32_t, (uint8_t *)&t47 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t47 + 0), t45);
                GM_ST(uint32_t, ((uint8_t *)&t47 + 16), t46);
                gm_f_format_a1ec0ji((void *)&gm_l18, (void *)NULL, cx, NULL, &t47);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src2j6ljeja7ilp, 219, 17);
            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
            gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
            gm_l10 = t50;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)30ULL));
            if (!t52) {
                goto gm_lb4;
            }
            gm_b16_8 t53 = gm_l8;
            gm_tnpifv20irnph t54;
            GM_ST(gm_b16_8, ((uint8_t *)&t54 + 0), t53);
            gm_f_format_q296tqt((void *)&gm_l19, (void *)NULL, cx, NULL, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
            if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_src2j6ljeja7ilp, 220, 22);
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
            gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
            gm_l10 = t57;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t59 = (uint8_t)(t58 == ((uint8_t)31ULL));
            if (!t59) {
                goto gm_lb5;
            }
            uint32_t t60 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
            if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_src2j6ljeja7ilp, 222, 16);
            uint32_t t61 = (uint32_t)(t60 % ((uint32_t)8ULL));
            uint8_t t62 = (!(t61 == ((uint32_t)0ULL)));
            if (t62) {
                gm_tl874t2atnlh3 t63 = gm_l1;
                gm_b16_8 t64;
                GM_ST(void *, (uint8_t *)&t64, (void *)(gm_s0cpkv7s4ef85));
                GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(23));
                gm_t08olq97rscn3 t65;
                gm_f_unsupported_531q51r((void *)&t65, (void *)NULL, cx, NULL, t63, t64);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t65);
                goto gm_fdone;
            }
            {
                gm_b16_8 t66 = gm_l8;
                uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
                gm_tommf4j9upomc t68;
                GM_ST(uint32_t, (uint8_t *)&t68 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t66);
                GM_ST(uint32_t, ((uint8_t *)&t68 + 16), t67);
                gm_f_format_87dn9rn((void *)&gm_l20, (void *)NULL, cx, NULL, &t68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_src2j6ljeja7ilp, 223, 13);
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
            gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
            gm_l10 = t71;
            goto gm_lb1;
        }
gm_lb5:;
        {
            {
                gm_b16_8 t72 = gm_l8;
                uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
                gm_tommf4j9upomc t74;
                GM_ST(uint32_t, (uint8_t *)&t74 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t74 + 0), t72);
                GM_ST(uint32_t, ((uint8_t *)&t74 + 16), t73);
                gm_f_format_kbs7h9q((void *)&gm_l21, (void *)NULL, cx, NULL, &t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
            if (!t75) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_src2j6ljeja7ilp, 225, 14);
            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
            gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
            gm_l10 = t77;
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 217, 20);
    }
gm_lb1:;
    {
        void * t78 = gm_l0;
        gm_t88rcucg1b7uk t79 = gm_l2;
        gm_b16_8 t80 = gm_l9;
        gm_b16_8 t81 = gm_l10;
        gm_t6j6smj7qoctu t82;
        GM_ST(gm_b16_8, ((uint8_t *)&t82 + 0), t80);
        GM_ST(gm_b16_8, ((uint8_t *)&t82 + 16), t81);
        gm_f_format_3vtl3te((void *)&gm_l22, (void *)NULL, cx, NULL, &t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
        if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_src2j6ljeja7ilp, 227, 20);
        gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
        gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
        gm_b16_8 t86 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t78, t79, t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t86;
    }
    {
        gm_t8588ma2cr9d7 t87 = gm_l3;
        gm_t8588ma2cr9d7 t88;
        GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t88 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)31ULL));
        gm_t8588ma2cr9d7 t89;
        GM_ST(uint8_t, (uint8_t *)&t89 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t89 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t89 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)34ULL));
        gm_th9f6i3krj9v9 t90;
        GM_ST(gm_t8588ma2cr9d7, ((uint8_t *)&t90 + 0), t88);
        GM_ST(gm_t8588ma2cr9d7, ((uint8_t *)&t90 + 12), t89);
        uint8_t t91 = 0;
        if (gm_eqg63o53ar52(((uint8_t *)&t90) + 0, ((uint8_t *)&t87))) t91 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t90) + 12, ((uint8_t *)&t87))) t91 = 1;
        uint8_t t92 = (uint8_t)(t91);
        if (t92) {
            {
                {
                    {
                        void * t93 = gm_l0;
                        gm_t88rcucg1b7uk t94 = gm_l2;
                        gm_f_int_info_2tcdf1n((void *)&gm_l23, (void *)NULL, cx, NULL, t93, t94);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t95 = (uint8_t)((((uint8_t *)&gm_l23))[16] != 0);
                    if (!t95) {
                        goto gm_lb8;
                    }
                    gm_tuglhjjmj2g58 t96 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&gm_l23));
                    gm_l12 = t96;
                    gm_tuglhjjmj2g58 t97 = gm_l12;
                    gm_b16_8 t98 = gm_l11;
                    gm_b16_8 t99 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t97, t98);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t99;
                    {
                        void * t100 = gm_l0;
                        gm_t88rcucg1b7uk t101 = gm_l2;
                        gm_b16_8 t102 = gm_l13;
                        gm_b16_8 t103 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t100, t101, t102);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t103);
                        goto gm_fdone;
                    }
                }
gm_lb8:;
            }
gm_lb7:;
        }
    }
    {
        gm_b16_8 t104 = gm_l11;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t104);
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
inline __attribute__((aligned(4))) void gm_f_black_box_r6deojc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_black_box_r6deojc F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v0 */
    gm_b16_8 gm_l3; /* c */
    uint64_t gm_l4; /* storage */
    uint8_t gm_l5; /* in_register */
    gm_b16_8 gm_l6; /* v */
    gm_b16_8 gm_l7; /* slot */
    gm_b16_8 gm_l8; /* p */
    GM_STACK_CHECK(&F, 675u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l9;
    uint8_t gm_l10;
    gm_t5v8n6qrn4rh4 gm_l11;
    gm_tabb710d9mq2e gm_l12;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = (void *)(((uint8_t *)t7) + 8);
    gm_t88rcucg1b7uk t9 = gm_l1;
    uint8_t t10 = gm_f_is_unit_17uennd(t8, GM_CONV(gm_csguankko4lmhd, t9));
    if (t10) {
        gm_b16_8 t11 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t11);
        goto gm_fdone;
    }
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    void * t14 = (void *)(((uint8_t *)t13) + 8);
    gm_t88rcucg1b7uk t15 = gm_l1;
    gm_b16_8 t16 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t14, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t16;
    void * t17 = gm_l0;
    void * t18 = GM_LD(void *, ((uint8_t *)t17));
    uint8_t t19 = GM_LD(uint8_t, (((((uint8_t *)t18) + 8) + 8) + 56));
    uint8_t t20 = (uint8_t)!t19;
    if (t20) {
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, ((uint8_t *)t21));
        void * t23 = (void *)(((uint8_t *)t22) + 8);
        gm_t88rcucg1b7uk t24 = gm_l1;
        gm_f_repr_53quqki((void *)&gm_l11, (void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            {
                uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                uint8_t t26 = (uint8_t)(t25 == ((uint8_t)0ULL));
                if (!t26) {
                    goto gm_lb2;
                }
                uint64_t t27 = GM_LD(uint64_t, ((((uint8_t *)&gm_l11) + 8) + 8));
                gm_l4 = t27;
                uint64_t t28 = gm_l4;
                uint8_t t29 = (t28 <= ((uint64_t)8ULL));
                gm_l10 = t29;
                goto gm_lb1;
            }
gm_lb2:;
            {
                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                uint8_t t31 = (uint8_t)(t30 == ((uint8_t)2ULL));
                if (!t31) {
                    goto gm_lb3;
                }
                gm_l10 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb3:;
            {
                gm_l10 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb4:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 325, 57);
        }
gm_lb1:;
        uint8_t t32 = gm_l10;
        gm_l9 = t32;
    } else {
        gm_l9 = ((uint8_t)0);
    }
    uint8_t t33 = gm_l9;
    gm_l5 = t33;
    uint8_t t34 = gm_l5;
    if (t34) {
        void * t35 = gm_l0;
        gm_t88rcucg1b7uk t36 = gm_l1;
        gm_b16_8 t37 = gm_l2;
        gm_b16_8 t38 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t35, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t38;
        void * t39 = gm_l0;
        gm_b16_8 t40 = gm_l6;
        gm_tnpifv20irnph t41;
        GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t40);
        gm_b0_1 t42 = gm_f_linef_7e19lva((void *)NULL, cx, NULL, t39, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t43 = gm_l6;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t43);
        goto gm_fdone;
    }
    void * t44 = gm_l0;
    gm_t88rcucg1b7uk t45 = gm_l1;
    gm_b16_8 t46 = gm_l2;
    gm_b16_8 t47 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t44, t45, t46);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t47;
    void * t48 = gm_l0;
    gm_b16_8 t49 = gm_f_fresh_tmp_6tadl69((void *)NULL, cx, NULL, t48);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t49;
    {
        void * t50 = gm_l0;
        gm_b16_8 t51 = gm_l3;
        gm_b16_8 t52 = gm_l8;
        gm_b16_8 t53 = gm_l7;
        gm_tfeab279c16rr t54;
        GM_ST(gm_b16_8, ((uint8_t *)&t54 + 0), t51);
        GM_ST(gm_b16_8, ((uint8_t *)&t54 + 16), t52);
        GM_ST(gm_b16_8, ((uint8_t *)&t54 + 32), t53);
        gm_b0_1 t55 = gm_f_linef_0kkt9cm((void *)NULL, cx, NULL, t50, &t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t56 = gm_l0;
        gm_t88rcucg1b7uk t57 = gm_l1;
        gm_b16_8 t58 = gm_l8;
        gm_tnpifv20irnph t59;
        GM_ST(gm_b16_8, ((uint8_t *)&t59 + 0), t58);
        gm_f_format_ohk11b5((void *)&gm_l12, (void *)NULL, cx, NULL, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src2j6ljeja7ilp, 338, 22);
        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
        gm_b16_8 t63 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t56, t57, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t63);
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
__attribute__((aligned(4))) void gm_f_builtin_values_vqetei1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t8588ma2cr9d7 a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_builtin_values_vqetei1 F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_t8588ma2cr9d7 gm_l3; /* op */
    gm_b16_8 gm_l4; /* tys */
    gm_b16_8 gm_l5; /* vals */
    gm_b16_8 gm_l6; /* loc */
    gm_b16_8 gm_l7; /* v0 */
    gm_t88rcucg1b7uk gm_l8; /* at */
    gm_b16_8 gm_l9; /* a */
    gm_b16_8 gm_l10; /* r */
    uint64_t gm_l11; /* sz */
    gm_tuglhjjmj2g58 gm_l12; /* ii */
    gm_b16_8 gm_l13; /* nv */
    uint64_t gm_l14; /* al */
    gm_t88rcucg1b7uk gm_l15; /* elem */
    uint64_t gm_l16; /* stride */
    gm_b16_8 gm_l17; /* p */
    gm_b16_8 gm_l18; /* x */
    gm_b16_8 gm_l19; /* sign */
    gm_b16_8 gm_l20; /* a */
    gm_b16_8 gm_l21; /* gt */
    gm_b16_8 gm_l22; /* f */
    gm_b16_8 gm_l23; /* r */
    gm_b16_8 gm_l24; /* x */
    gm_b16_8 gm_l25; /* lo */
    gm_b16_8 gm_l26; /* hi */
    gm_b16_8 gm_l27; /* r */
    gm_tuglhjjmj2g58 gm_l28; /* ii */
    gm_b16_8 gm_l29; /* min */
    gm_tuglhjjmj2g58 gm_l30; /* ii */
    gm_b16_8 gm_l31; /* u */
    gm_b16_8 gm_l32; /* a */
    gm_b16_8 gm_l33; /* b */
    gm_b16_8 gm_l34; /* f */
    gm_b16_8 gm_l35; /* r */
    gm_b16_8 gm_l36; /* f */
    gm_b16_8 gm_l37; /* r */
    gm_t9qkh0rsooc8d gm_l38; /* fl */
    gm_b16_8 gm_l39; /* a */
    gm_b16_8 gm_l40; /* f */
    gm_b16_8 gm_l41; /* a */
    GM_STACK_CHECK(&F, 7631u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_t5o0tpv5jka2l gm_l42;
    gm_tqcc6dmlne11b gm_l43;
    gm_tabb710d9mq2e gm_l44;
    gm_tabb710d9mq2e gm_l45;
    gm_t53at80pfdus3 gm_l46;
    gm_tabb710d9mq2e gm_l47;
    gm_tabb710d9mq2e gm_l48;
    uint8_t gm_l49;
    gm_t5v8n6qrn4rh4 gm_l50;
    gm_tabb710d9mq2e gm_l51;
    gm_tabb710d9mq2e gm_l52;
    uint8_t gm_l53;
    gm_t5v8n6qrn4rh4 gm_l54;
    gm_tabb710d9mq2e gm_l55;
    gm_tabb710d9mq2e gm_l56;
    uint8_t gm_l57;
    gm_t5v8n6qrn4rh4 gm_l58;
    gm_tabb710d9mq2e gm_l59;
    gm_tqcc6dmlne11b gm_l60;
    gm_tabb710d9mq2e gm_l61;
    gm_tqcc6dmlne11b gm_l62;
    gm_tabb710d9mq2e gm_l63;
    gm_tabb710d9mq2e gm_l64;
    gm_tabb710d9mq2e gm_l65;
    gm_tabb710d9mq2e gm_l66;
    gm_tabb710d9mq2e gm_l67;
    gm_tabb710d9mq2e gm_l68;
    gm_tabb710d9mq2e gm_l69;
    gm_t5o0tpv5jka2l gm_l70;
    gm_tabb710d9mq2e gm_l71;
    gm_tabb710d9mq2e gm_l72;
    {
        void * t79 = gm_l0;
        void * t80 = GM_LD(void *, ((uint8_t *)t79));
        gm_tl874t2atnlh3 t81 = gm_l1;
        gm_b16_8 t82 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t80, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t82;
        void * t83 = GM_LD(void *, ((uint8_t *)&gm_l5));
        uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t84)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t84, gm_src2j6ljeja7ilp, 7, 11);
        gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)t83) + (uintptr_t)((uint64_t)0ULL) * 16));
        gm_l7 = t85;
        void * t86 = GM_LD(void *, ((uint8_t *)&gm_l4));
        uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t87)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t87, gm_src2j6ljeja7ilp, 8, 11);
        gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t86) + (uintptr_t)((uint64_t)0ULL) * 4));
        gm_l8 = t88;
        {
            {
                {
                    {
                        {
                            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t90 = (uint8_t)(t89 == ((uint8_t)0ULL));
                            if (!t90) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t92 = (uint8_t)(t91 == ((uint8_t)1ULL));
                            if (!t92) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t94 = (uint8_t)(t93 == ((uint8_t)2ULL));
                            if (!t94) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        {
                            uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t96 = (uint8_t)(t95 == ((uint8_t)3ULL));
                            if (!t96) {
                                goto gm_lb7;
                            }
                            goto gm_lb3;
                        }
gm_lb7:;
                        {
                            uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t98 = (uint8_t)(t97 == ((uint8_t)4ULL));
                            if (!t98) {
                                goto gm_lb8;
                            }
                            goto gm_lb3;
                        }
gm_lb8:;
                        {
                            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t100 = (uint8_t)(t99 == ((uint8_t)5ULL));
                            if (!t100) {
                                goto gm_lb9;
                            }
                            goto gm_lb3;
                        }
gm_lb9:;
                        {
                            uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t102 = (uint8_t)(t101 == ((uint8_t)6ULL));
                            if (!t102) {
                                goto gm_lb10;
                            }
                            goto gm_lb3;
                        }
gm_lb10:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    {
                        void * t103 = gm_l0;
                        gm_tl874t2atnlh3 t104 = gm_l1;
                        gm_t88rcucg1b7uk t105 = gm_l2;
                        gm_t8588ma2cr9d7 t106 = gm_l3;
                        gm_t88rcucg1b7uk t107 = gm_l8;
                        gm_b16_8 t108 = gm_l7;
                        gm_f_convert_lqrk2n0((void *)&gm_l42, (void *)NULL, cx, NULL, t103, t104, t105, t106, t107, t108);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                    if (t109) {
                    } else {
                        gm_t08olq97rscn3 t110 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l42) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t110);
                        goto gm_fdone;
                    }
                    {
                        gm_b16_8 t111 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l42) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t111);
                        goto gm_fdone;
                    }
                }
gm_lb2:;
                {
                    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t113 = (uint8_t)(t112 == ((uint8_t)7ULL));
                    if (!t113) {
                        goto gm_lb11;
                    }
                    void * t114 = gm_l0;
                    gm_t88rcucg1b7uk t115 = gm_l8;
                    gm_b16_8 t116 = gm_l7;
                    gm_b16_8 t117 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t114, t115, t116);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l9 = t117;
                    void * t118 = gm_l0;
                    gm_t88rcucg1b7uk t119 = gm_l2;
                    gm_b16_8 t120 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t118, t119);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t120;
                    void * t121 = gm_l0;
                    void * t122 = GM_LD(void *, ((uint8_t *)t121));
                    void * t123 = (void *)(((uint8_t *)t122) + 8);
                    gm_t88rcucg1b7uk t124 = gm_l8;
                    uint64_t t125 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t123, t124);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l11 = t125;
                    {
                        void * t126 = gm_l0;
                        gm_b16_8 t127 = gm_l10;
                        gm_b16_8 t128 = gm_l10;
                        gm_b16_8 t129 = gm_l10;
                        gm_b16_8 t130 = gm_l9;
                        gm_b16_8 t131 = gm_l10;
                        uint64_t t132 = gm_l11;
                        gm_b16_8 t133 = gm_l10;
                        uint64_t t134 = gm_l11;
                        gm_tsroeipraj6j8 t135;
                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 0), t127);
                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 16), t128);
                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 32), t129);
                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 48), t130);
                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 64), t131);
                        GM_ST(uint64_t, ((uint8_t *)&t135 + 80), t132);
                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 88), t133);
                        GM_ST(uint64_t, ((uint8_t *)&t135 + 104), t134);
                        gm_b0_1 t136 = gm_f_linef_a98fois((void *)NULL, cx, NULL, t126, &t135);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        {
                            void * t137 = gm_l0;
                            gm_t88rcucg1b7uk t138 = gm_l2;
                            gm_f_int_info_2tcdf1n((void *)&gm_l43, (void *)NULL, cx, NULL, t137, t138);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t139 = (uint8_t)((((uint8_t *)&gm_l43))[16] != 0);
                            if (!t139) {
                                goto gm_lb13;
                            }
                            gm_tuglhjjmj2g58 t140 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&gm_l43));
                            gm_l12 = t140;
                            gm_tuglhjjmj2g58 t141 = gm_l12;
                            uint8_t t142 = gm_f_ii_exact_g6a19dp(GM_CONV(gm_cs2lqoac0a5ng4, t141));
                            uint8_t t143 = (uint8_t)!t142;
                            if (t143) {
                                gm_tuglhjjmj2g58 t144 = gm_l12;
                                gm_b16_8 t145 = gm_l10;
                                gm_b16_8 t146 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t144, t145);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l13 = t146;
                                void * t147 = gm_l0;
                                gm_b16_8 t148 = gm_l10;
                                gm_b16_8 t149 = gm_l13;
                                gm_t6j6smj7qoctu t150;
                                GM_ST(gm_b16_8, ((uint8_t *)&t150 + 0), t148);
                                GM_ST(gm_b16_8, ((uint8_t *)&t150 + 16), t149);
                                gm_b0_1 t151 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t147, &t150);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            goto gm_lb12;
                        }
gm_lb13:;
                    }
gm_lb12:;
                    {
                        gm_b16_8 t152 = gm_l10;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t152);
                        goto gm_fdone;
                    }
                }
gm_lb11:;
            }
            {
                {
                    uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t154 = (uint8_t)(t153 == ((uint8_t)8ULL));
                    if (!t154) {
                        goto gm_lb14;
                    }
                    void * t155 = gm_l0;
                    gm_t88rcucg1b7uk t156 = gm_l2;
                    uint64_t t157 = gm_f_arith_pointee_align_vqoa1mk((void *)NULL, cx, NULL, t155, t156);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l14 = t157;
                    {
                        {
                            uint64_t t158 = gm_l14;
                            uint8_t t159 = (t158 > ((uint64_t)1ULL));
                            if (!t159) {
                                goto gm_lb16;
                            }
                            void * t160 = gm_l0;
                            void * t161 = GM_LD(void *, ((uint8_t *)t160));
                            uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)t161) + 1136));
                            if (!t162) {
                                goto gm_lb16;
                            }
                            void * t163 = gm_l0;
                            gm_t88rcucg1b7uk t164 = gm_l8;
                            uint64_t t165 = gm_f_arith_pointee_align_vqoa1mk((void *)NULL, cx, NULL, t163, t164);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t t166 = gm_l14;
                            uint8_t t167 = (t165 < t166);
                            if (!t167) {
                                goto gm_lb16;
                            }
                            void * t168 = gm_l0;
                            gm_b16_8 t169 = gm_l7;
                            uint64_t t170 = gm_l14;
                            gm_b16_8 t171 = gm_l7;
                            uint64_t t172 = gm_l14;
                            gm_b16_8 t173 = gm_l6;
                            gm_tpm6m3o0hprmc t174;
                            GM_ST(gm_b16_8, ((uint8_t *)&t174 + 0), t169);
                            GM_ST(uint64_t, ((uint8_t *)&t174 + 16), t170);
                            GM_ST(gm_b16_8, ((uint8_t *)&t174 + 24), t171);
                            GM_ST(uint64_t, ((uint8_t *)&t174 + 40), t172);
                            GM_ST(gm_b16_8, ((uint8_t *)&t174 + 48), t173);
                            gm_b0_1 t175 = gm_f_linef_kafc4c5((void *)NULL, cx, NULL, t168, &t174);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb15;
                        }
gm_lb16:;
                    }
gm_lb15:;
                    void * t176 = gm_l0;
                    gm_t88rcucg1b7uk t177 = gm_l2;
                    gm_b16_8 t178 = gm_l7;
                    gm_b16_8 t179 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t176, t177, t178);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t179);
                    goto gm_fdone;
                }
gm_lb14:;
                {
                    uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t181 = (uint8_t)(t180 == ((uint8_t)9ULL));
                    if (!t181) {
                        goto gm_lb17;
                    }
                    {
                        void * t182 = gm_l0;
                        gm_t88rcucg1b7uk t183 = gm_l2;
                        gm_b16_8 t184 = gm_l7;
                        gm_tnpifv20irnph t185;
                        GM_ST(gm_b16_8, ((uint8_t *)&t185 + 0), t184);
                        gm_f_format_6df13cd((void *)&gm_l44, (void *)NULL, cx, NULL, t185);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t186 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                        if (!t186) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 8, gm_src2j6ljeja7ilp, 32, 35);
                        gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 8));
                        gm_b16_8 t188 = GM_LD(gm_b16_8, ((uint8_t *)&t187));
                        gm_b16_8 t189 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t182, t183, t188);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t189);
                        goto gm_fdone;
                    }
                }
gm_lb17:;
                {
                    uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t191 = (uint8_t)(t190 == ((uint8_t)13ULL));
                    if (!t191) {
                        goto gm_lb18;
                    }
                    {
                        void * t192 = gm_l0;
                        gm_t88rcucg1b7uk t193 = gm_l2;
                        gm_b16_8 t194 = gm_l7;
                        gm_tnpifv20irnph t195;
                        GM_ST(gm_b16_8, ((uint8_t *)&t195 + 0), t194);
                        gm_f_format_a7bth9i((void *)&gm_l45, (void *)NULL, cx, NULL, t195);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t196 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                        if (!t196) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 8, gm_src2j6ljeja7ilp, 33, 36);
                        gm_b16_8 t197 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                        gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)&t197));
                        gm_b16_8 t199 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t192, t193, t198);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t199);
                        goto gm_fdone;
                    }
                }
gm_lb18:;
            }
            {
                {
                    {
                        {
                            uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t201 = (uint8_t)(t200 == ((uint8_t)10ULL));
                            if (!t201) {
                                goto gm_lb21;
                            }
                            goto gm_lb20;
                        }
gm_lb21:;
                        {
                            uint8_t t202 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t203 = (uint8_t)(t202 == ((uint8_t)11ULL));
                            if (!t203) {
                                goto gm_lb22;
                            }
                            goto gm_lb20;
                        }
gm_lb22:;
                        {
                            uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t205 = (uint8_t)(t204 == ((uint8_t)12ULL));
                            if (!t205) {
                                goto gm_lb23;
                            }
                            goto gm_lb20;
                        }
gm_lb23:;
                        goto gm_lb19;
                    }
gm_lb20:;
                    {
                        {
                            {
                                void * t206 = gm_l0;
                                void * t207 = GM_LD(void *, ((uint8_t *)t206));
                                void * t208 = (void *)(((uint8_t *)t207) + 8);
                                gm_t88rcucg1b7uk t209 = gm_l8;
                                gm_f_kind_ghvav22((void *)&gm_l46, t208, GM_CONV(gm_csguankko4lmhd, t209));
                            }
                            uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                            uint8_t t211 = (uint8_t)(t210 == ((uint8_t)9ULL));
                            if (!t211) {
                                goto gm_lb25;
                            }
                            gm_t88rcucg1b7uk t212 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l46) + 8) + 4));
                            gm_l15 = t212;
                            void * t213 = gm_l0;
                            void * t214 = GM_LD(void *, ((uint8_t *)t213));
                            void * t215 = (void *)(((uint8_t *)t214) + 8);
                            gm_t88rcucg1b7uk t216 = gm_l15;
                            uint64_t t217 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t215, t216);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l16 = t217;
                            gm_b16_8 t218 = gm_l7;
                            gm_l17 = t218;
                            void * t219 = GM_LD(void *, ((uint8_t *)&gm_l5));
                            uint64_t t220 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t220)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t220, gm_src2j6ljeja7ilp, 38, 18);
                            gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)t219) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_l18 = t221;
                            gm_t8588ma2cr9d7 t222 = gm_l3;
                            uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&t222) + 0));
                            uint8_t t224 = (uint8_t)((t223 == ((uint8_t)12ULL)));
                            gm_t8588ma2cr9d7 t225;
                            GM_ST(uint8_t, (uint8_t *)&t225 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t225 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t225 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t225 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t225 + 0), ((uint8_t)12ULL));
                            if (t224) {
                                uint64_t t226 = gm_l16;
                                uint8_t t227 = ((t226 == ((uint64_t)0ULL)));
                                if (t227) {
                                    void * t228 = gm_l0;
                                    gm_t88rcucg1b7uk t229 = gm_l2;
                                    gm_b16_8 t230;
                                    GM_ST(void *, (uint8_t *)&t230, (void *)(gm_sicgmpca63jln));
                                    GM_ST(uintptr_t, (uint8_t *)&t230 + 8, (uintptr_t)(1));
                                    gm_b16_8 t231 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t228, t229, t230);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t231);
                                    goto gm_fdone;
                                }
                                {
                                    void * t232 = gm_l0;
                                    gm_t88rcucg1b7uk t233 = gm_l2;
                                    gm_b16_8 t234 = gm_l17;
                                    gm_b16_8 t235 = gm_l18;
                                    uint64_t t236 = gm_l16;
                                    gm_tftksr4op114e t237;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t237 + 0), t234);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t237 + 16), t235);
                                    GM_ST(uint64_t, ((uint8_t *)&t237 + 32), t236);
                                    gm_f_format_ms7m2d2((void *)&gm_l47, (void *)NULL, cx, NULL, &t237);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t238 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                                    if (!t238) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 8, gm_src2j6ljeja7ilp, 41, 34);
                                    gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
                                    gm_b16_8 t240 = GM_LD(gm_b16_8, ((uint8_t *)&t239));
                                    gm_b16_8 t241 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t232, t233, t240);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t241);
                                    goto gm_fdone;
                                }
                            }
                            gm_t8588ma2cr9d7 t242 = gm_l3;
                            uint8_t t243 = GM_LD(uint8_t, (((uint8_t *)&t242) + 0));
                            uint8_t t244 = (uint8_t)((t243 == ((uint8_t)10ULL)));
                            gm_t8588ma2cr9d7 t245;
                            GM_ST(uint8_t, (uint8_t *)&t245 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t245 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t245 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t245 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t245 + 0), ((uint8_t)10ULL));
                            if (t244) {
                                gm_b16_8 t246;
                                GM_ST(void *, (uint8_t *)&t246, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t246 + 8, (uintptr_t)(0));
                                gm_l19 = t246;
                            } else {
                                gm_b16_8 t247;
                                GM_ST(void *, (uint8_t *)&t247, (void *)(gm_skolmnfc0ccbd));
                                GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(1));
                                gm_l19 = t247;
                            }
                            {
                                void * t248 = gm_l0;
                                gm_t88rcucg1b7uk t249 = gm_l2;
                                gm_b16_8 t250 = gm_l17;
                                gm_b16_8 t251 = gm_l19;
                                gm_b16_8 t252 = gm_l18;
                                uint64_t t253 = gm_l16;
                                gm_tttl9nsdo1iu6 t254;
                                GM_ST(gm_b16_8, ((uint8_t *)&t254 + 0), t250);
                                GM_ST(gm_b16_8, ((uint8_t *)&t254 + 16), t251);
                                GM_ST(gm_b16_8, ((uint8_t *)&t254 + 32), t252);
                                GM_ST(uint64_t, ((uint8_t *)&t254 + 48), t253);
                                gm_f_format_83msomh((void *)&gm_l48, (void *)NULL, cx, NULL, &t254);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t255 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                if (!t255) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 8, gm_src2j6ljeja7ilp, 44, 30);
                                gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
                                gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&t256));
                                gm_b16_8 t258 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t248, t249, t257);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t258);
                                goto gm_fdone;
                            }
                        }
gm_lb25:;
                        gm_trap("reached `@unreachable()`", gm_src2j6ljeja7ilp, 35, 56);
                    }
gm_lb24:;
                }
gm_lb19:;
            }
            {
                {
                    uint8_t t259 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t260 = (uint8_t)(t259 == ((uint8_t)14ULL));
                    if (!t260) {
                        goto gm_lb26;
                    }
                    void * t261 = gm_l0;
                    gm_t88rcucg1b7uk t262 = gm_l8;
                    gm_b16_8 t263 = gm_l7;
                    gm_b16_8 t264 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t261, t262, t263);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l20 = t264;
                    void * t265 = gm_l0;
                    gm_t88rcucg1b7uk t266 = gm_l2;
                    gm_b16_8 t267 = gm_l20;
                    gm_b16_8 t268 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t265, t266, t267);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t268);
                    goto gm_fdone;
                }
gm_lb26:;
                {
                    uint8_t t269 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t270 = (uint8_t)(t269 == ((uint8_t)15ULL));
                    if (!t270) {
                        goto gm_lb27;
                    }
                    void * t271 = gm_l0;
                    gm_tl874t2atnlh3 t272 = gm_l1;
                    gm_t88rcucg1b7uk t273 = gm_l2;
                    gm_t88rcucg1b7uk t274 = gm_l8;
                    gm_b16_8 t275 = gm_l7;
                    gm_f_from_tag_2m7nfln((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t271, t272, t273, t274, t275);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb27:;
                {
                    {
                        {
                            uint8_t t276 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t277 = (uint8_t)(t276 == ((uint8_t)16ULL));
                            if (!t277) {
                                goto gm_lb30;
                            }
                            goto gm_lb29;
                        }
gm_lb30:;
                        {
                            uint8_t t278 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t279 = (uint8_t)(t278 == ((uint8_t)17ULL));
                            if (!t279) {
                                goto gm_lb31;
                            }
                            goto gm_lb29;
                        }
gm_lb31:;
                        goto gm_lb28;
                    }
gm_lb29:;
                    gm_t8588ma2cr9d7 t280 = gm_l3;
                    uint8_t t281 = GM_LD(uint8_t, (((uint8_t *)&t280) + 0));
                    uint8_t t282 = (uint8_t)((t281 == ((uint8_t)16ULL)));
                    gm_t8588ma2cr9d7 t283;
                    GM_ST(uint8_t, (uint8_t *)&t283 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t283 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t283 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t283 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t283 + 0), ((uint8_t)16ULL));
                    if (t282) {
                        gm_b16_8 t284;
                        GM_ST(void *, (uint8_t *)&t284, (void *)(gm_s1f440st4mnkp));
                        GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(1));
                        gm_l21 = t284;
                    } else {
                        gm_b16_8 t285;
                        GM_ST(void *, (uint8_t *)&t285, (void *)(gm_sm5be4659ulci));
                        GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)(1));
                        gm_l21 = t285;
                    }
                    {
                        void * t286 = gm_l0;
                        void * t287 = GM_LD(void *, ((uint8_t *)t286));
                        void * t288 = (void *)(((uint8_t *)t287) + 8);
                        gm_t88rcucg1b7uk t289 = gm_l8;
                        gm_f_repr_53quqki((void *)&gm_l50, (void *)NULL, cx, NULL, t288, t289);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        {
                            uint8_t t290 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                            uint8_t t291 = (uint8_t)(t290 == ((uint8_t)1ULL));
                            if (!t291) {
                                goto gm_lb33;
                            }
                            gm_l49 = ((uint8_t)1);
                            goto gm_lb32;
                        }
gm_lb33:;
                        {
                            gm_l49 = ((uint8_t)0);
                            goto gm_lb32;
                        }
gm_lb34:;
                        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 53, 16);
                    }
gm_lb32:;
                    uint8_t t292 = gm_l49;
                    if (t292) {
                        gm_t8588ma2cr9d7 t293 = gm_l3;
                        uint8_t t294 = GM_LD(uint8_t, (((uint8_t *)&t293) + 0));
                        uint8_t t295 = (uint8_t)((t294 == ((uint8_t)16ULL)));
                        gm_t8588ma2cr9d7 t296;
                        GM_ST(uint8_t, (uint8_t *)&t296 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t296 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t296 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t296 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t296 + 0), ((uint8_t)16ULL));
                        if (t295) {
                            gm_b16_8 t297;
                            GM_ST(void *, (uint8_t *)&t297, (void *)(gm_s12ng38ve3205));
                            GM_ST(uintptr_t, (uint8_t *)&t297 + 8, (uintptr_t)(7));
                            gm_l22 = t297;
                        } else {
                            gm_b16_8 t298;
                            GM_ST(void *, (uint8_t *)&t298, (void *)(gm_sgjc0ebf56hf4));
                            GM_ST(uintptr_t, (uint8_t *)&t298 + 8, (uintptr_t)(7));
                            gm_l22 = t298;
                        }
                        {
                            void * t299 = gm_l0;
                            gm_t88rcucg1b7uk t300 = gm_l2;
                            gm_b16_8 t301 = gm_l22;
                            gm_b16_8 t302 = gm_l7;
                            void * t303 = GM_LD(void *, ((uint8_t *)&gm_l5));
                            uint64_t t304 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t304)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t304, gm_src2j6ljeja7ilp, 55, 87);
                            gm_b16_8 t305 = GM_LD(gm_b16_8, (((uint8_t *)t303) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_tfeab279c16rr t306;
                            GM_ST(gm_b16_8, ((uint8_t *)&t306 + 0), t301);
                            GM_ST(gm_b16_8, ((uint8_t *)&t306 + 16), t302);
                            GM_ST(gm_b16_8, ((uint8_t *)&t306 + 32), t305);
                            gm_f_format_lqnh3lt((void *)&gm_l51, (void *)NULL, cx, NULL, &t306);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t307 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                            if (!t307) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 8, gm_src2j6ljeja7ilp, 55, 39);
                            gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 8));
                            gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
                            gm_b16_8 t310 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t299, t300, t309);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l23 = t310;
                        }
                        {
                            void * t311 = gm_l0;
                            gm_t88rcucg1b7uk t312 = gm_l2;
                            gm_b16_8 t313 = gm_l23;
                            gm_b16_8 t314 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t311, t312, t313);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t314);
                            goto gm_fdone;
                        }
                    }
                    {
                        void * t315 = gm_l0;
                        gm_t88rcucg1b7uk t316 = gm_l2;
                        gm_b16_8 t317 = gm_l7;
                        gm_b16_8 t318 = gm_l21;
                        void * t319 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t320 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t320)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t320, gm_src2j6ljeja7ilp, 58, 73);
                        gm_b16_8 t321 = GM_LD(gm_b16_8, (((uint8_t *)t319) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t322 = gm_l7;
                        void * t323 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t324 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t324)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t324, gm_src2j6ljeja7ilp, 58, 86);
                        gm_b16_8 t325 = GM_LD(gm_b16_8, (((uint8_t *)t323) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_tud7oovfvnf43 t326;
                        GM_ST(gm_b16_8, ((uint8_t *)&t326 + 0), t317);
                        GM_ST(gm_b16_8, ((uint8_t *)&t326 + 16), t318);
                        GM_ST(gm_b16_8, ((uint8_t *)&t326 + 32), t321);
                        GM_ST(gm_b16_8, ((uint8_t *)&t326 + 48), t322);
                        GM_ST(gm_b16_8, ((uint8_t *)&t326 + 64), t325);
                        gm_f_format_eqjcikj((void *)&gm_l52, (void *)NULL, cx, NULL, &t326);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t327 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                        if (!t327) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 8, gm_src2j6ljeja7ilp, 58, 30);
                        gm_b16_8 t328 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 8));
                        gm_b16_8 t329 = GM_LD(gm_b16_8, ((uint8_t *)&t328));
                        gm_b16_8 t330 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t315, t316, t329);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t330);
                        goto gm_fdone;
                    }
                }
gm_lb28:;
            }
            {
                {
                    uint8_t t331 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t332 = (uint8_t)(t331 == ((uint8_t)18ULL));
                    if (!t332) {
                        goto gm_lb35;
                    }
                    gm_b16_8 t333 = gm_l7;
                    gm_l24 = t333;
                    void * t334 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t335 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t335)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t335, gm_src2j6ljeja7ilp, 62, 19);
                    gm_b16_8 t336 = GM_LD(gm_b16_8, (((uint8_t *)t334) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_l25 = t336;
                    void * t337 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t338 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t338)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t338, gm_src2j6ljeja7ilp, 63, 19);
                    gm_b16_8 t339 = GM_LD(gm_b16_8, (((uint8_t *)t337) + (uintptr_t)((uint64_t)2ULL) * 16));
                    gm_l26 = t339;
                    {
                        void * t340 = gm_l0;
                        void * t341 = GM_LD(void *, ((uint8_t *)t340));
                        void * t342 = (void *)(((uint8_t *)t341) + 8);
                        gm_t88rcucg1b7uk t343 = gm_l8;
                        gm_f_repr_53quqki((void *)&gm_l54, (void *)NULL, cx, NULL, t342, t343);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        {
                            uint8_t t344 = GM_LD(uint8_t, (((uint8_t *)&gm_l54) + 0));
                            uint8_t t345 = (uint8_t)(t344 == ((uint8_t)1ULL));
                            if (!t345) {
                                goto gm_lb37;
                            }
                            gm_l53 = ((uint8_t)1);
                            goto gm_lb36;
                        }
gm_lb37:;
                        {
                            gm_l53 = ((uint8_t)0);
                            goto gm_lb36;
                        }
gm_lb38:;
                        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 64, 16);
                    }
gm_lb36:;
                    uint8_t t346 = gm_l53;
                    if (t346) {
                        {
                            void * t347 = gm_l0;
                            gm_t88rcucg1b7uk t348 = gm_l2;
                            gm_b16_8 t349 = gm_l24;
                            gm_b16_8 t350 = gm_l25;
                            gm_b16_8 t351 = gm_l26;
                            gm_tfeab279c16rr t352;
                            GM_ST(gm_b16_8, ((uint8_t *)&t352 + 0), t349);
                            GM_ST(gm_b16_8, ((uint8_t *)&t352 + 16), t350);
                            GM_ST(gm_b16_8, ((uint8_t *)&t352 + 32), t351);
                            gm_f_format_c57v5iu((void *)&gm_l55, (void *)NULL, cx, NULL, &t352);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t353 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                            if (!t353) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 8, gm_src2j6ljeja7ilp, 65, 39);
                            gm_b16_8 t354 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 8));
                            gm_b16_8 t355 = GM_LD(gm_b16_8, ((uint8_t *)&t354));
                            gm_b16_8 t356 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t347, t348, t355);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l27 = t356;
                        }
                        {
                            void * t357 = gm_l0;
                            gm_t88rcucg1b7uk t358 = gm_l2;
                            gm_b16_8 t359 = gm_l27;
                            gm_b16_8 t360 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t357, t358, t359);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t360);
                            goto gm_fdone;
                        }
                    }
                    {
                        void * t361 = gm_l0;
                        gm_t88rcucg1b7uk t362 = gm_l2;
                        gm_b16_8 t363 = gm_l24;
                        gm_b16_8 t364 = gm_l25;
                        gm_b16_8 t365 = gm_l25;
                        gm_b16_8 t366 = gm_l24;
                        gm_b16_8 t367 = gm_l26;
                        gm_b16_8 t368 = gm_l26;
                        gm_b16_8 t369 = gm_l24;
                        gm_th2d9c7aju8hp t370;
                        GM_ST(gm_b16_8, ((uint8_t *)&t370 + 0), t363);
                        GM_ST(gm_b16_8, ((uint8_t *)&t370 + 16), t364);
                        GM_ST(gm_b16_8, ((uint8_t *)&t370 + 32), t365);
                        GM_ST(gm_b16_8, ((uint8_t *)&t370 + 48), t366);
                        GM_ST(gm_b16_8, ((uint8_t *)&t370 + 64), t367);
                        GM_ST(gm_b16_8, ((uint8_t *)&t370 + 80), t368);
                        GM_ST(gm_b16_8, ((uint8_t *)&t370 + 96), t369);
                        gm_f_format_jpvl2js((void *)&gm_l56, (void *)NULL, cx, NULL, &t370);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t371 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                        if (!t371) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 8, gm_src2j6ljeja7ilp, 68, 30);
                        gm_b16_8 t372 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l56) + 8));
                        gm_b16_8 t373 = GM_LD(gm_b16_8, ((uint8_t *)&t372));
                        gm_b16_8 t374 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t361, t362, t373);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t374);
                        goto gm_fdone;
                    }
                }
gm_lb35:;
            }
            {
                {
                    uint8_t t375 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t376 = (uint8_t)(t375 == ((uint8_t)19ULL));
                    if (!t376) {
                        goto gm_lb39;
                    }
                    {
                        void * t377 = gm_l0;
                        void * t378 = GM_LD(void *, ((uint8_t *)t377));
                        void * t379 = (void *)(((uint8_t *)t378) + 8);
                        gm_t88rcucg1b7uk t380 = gm_l8;
                        gm_f_repr_53quqki((void *)&gm_l58, (void *)NULL, cx, NULL, t379, t380);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        {
                            uint8_t t381 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                            uint8_t t382 = (uint8_t)(t381 == ((uint8_t)1ULL));
                            if (!t382) {
                                goto gm_lb41;
                            }
                            gm_l57 = ((uint8_t)1);
                            goto gm_lb40;
                        }
gm_lb41:;
                        {
                            gm_l57 = ((uint8_t)0);
                            goto gm_lb40;
                        }
gm_lb42:;
                        gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 71, 16);
                    }
gm_lb40:;
                    uint8_t t383 = gm_l57;
                    if (t383) {
                        void * t384 = gm_l0;
                        gm_t88rcucg1b7uk t385 = gm_l2;
                        gm_b16_8 t386 = gm_l7;
                        gm_b16_8 t387 = gm_l7;
                        gm_b16_8 t388 = gm_l7;
                        gm_b16_8 t389 = gm_l7;
                        gm_b16_8 t390 = gm_l7;
                        gm_tud7oovfvnf43 t391;
                        GM_ST(gm_b16_8, ((uint8_t *)&t391 + 0), t386);
                        GM_ST(gm_b16_8, ((uint8_t *)&t391 + 16), t387);
                        GM_ST(gm_b16_8, ((uint8_t *)&t391 + 32), t388);
                        GM_ST(gm_b16_8, ((uint8_t *)&t391 + 48), t389);
                        GM_ST(gm_b16_8, ((uint8_t *)&t391 + 64), t390);
                        gm_f_format_3j59e2u((void *)&gm_l59, (void *)NULL, cx, NULL, &t391);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t392 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                        if (!t392) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 8, gm_src2j6ljeja7ilp, 72, 34);
                        gm_b16_8 t393 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l59) + 8));
                        gm_b16_8 t394 = GM_LD(gm_b16_8, ((uint8_t *)&t393));
                        gm_b16_8 t395 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t384, t385, t394);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t395);
                        goto gm_fdone;
                    }
                    {
                        void * t396 = gm_l0;
                        gm_t88rcucg1b7uk t397 = gm_l8;
                        gm_f_int_info_2tcdf1n((void *)&gm_l60, (void *)NULL, cx, NULL, t396, t397);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t398 = (uint8_t)((((uint8_t *)&gm_l60))[16] != 0);
                    if (!t398) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 74, 19);
                    gm_tuglhjjmj2g58 t399 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&gm_l60));
                    gm_l28 = t399;
                    {
                        {
                            uint8_t t400 = GM_LD(uint8_t, ((uint8_t *)&gm_l28));
                            if (!t400) {
                                goto gm_lb44;
                            }
                            void * t401 = gm_l0;
                            void * t402 = GM_LD(void *, ((uint8_t *)t401));
                            uint8_t t403 = GM_LD(uint8_t, (((uint8_t *)t402) + 1136));
                            if (!t403) {
                                goto gm_lb44;
                            }
                            {
                                void * t404 = gm_l0;
                                gm_t88rcucg1b7uk t405 = gm_l8;
                                gm_tuglhjjmj2g58 t406 = gm_l28;
                                gm_tbci5ospk5nnn t407;
                                gm_f_ii_min_lku7pdi((void *)&t407, (void *)NULL, cx, NULL, t406);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t408 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t404, t405, &t407);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l29 = t408;
                            }
                            {
                                void * t409 = gm_l0;
                                gm_b16_8 t410 = gm_l7;
                                gm_b16_8 t411 = gm_l29;
                                gm_b16_8 t412 = gm_l6;
                                gm_tfeab279c16rr t413;
                                GM_ST(gm_b16_8, ((uint8_t *)&t413 + 0), t410);
                                GM_ST(gm_b16_8, ((uint8_t *)&t413 + 16), t411);
                                GM_ST(gm_b16_8, ((uint8_t *)&t413 + 32), t412);
                                gm_b0_1 t414 = gm_f_linef_qno93os((void *)NULL, cx, NULL, t409, &t413);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            goto gm_lb43;
                        }
gm_lb44:;
                    }
gm_lb43:;
                    {
                        void * t415 = gm_l0;
                        gm_t88rcucg1b7uk t416 = gm_l2;
                        gm_b16_8 t417 = gm_l7;
                        gm_b16_8 t418 = gm_l7;
                        gm_b16_8 t419 = gm_l7;
                        gm_tfeab279c16rr t420;
                        GM_ST(gm_b16_8, ((uint8_t *)&t420 + 0), t417);
                        GM_ST(gm_b16_8, ((uint8_t *)&t420 + 16), t418);
                        GM_ST(gm_b16_8, ((uint8_t *)&t420 + 32), t419);
                        gm_f_format_iqjnutf((void *)&gm_l61, (void *)NULL, cx, NULL, &t420);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t421 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                        if (!t421) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 8, gm_src2j6ljeja7ilp, 79, 30);
                        gm_b16_8 t422 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
                        gm_b16_8 t423 = GM_LD(gm_b16_8, ((uint8_t *)&t422));
                        gm_b16_8 t424 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t415, t416, t423);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t424);
                        goto gm_fdone;
                    }
                }
gm_lb39:;
            }
            {
                {
                    uint8_t t425 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t426 = (uint8_t)(t425 == ((uint8_t)20ULL));
                    if (!t426) {
                        goto gm_lb45;
                    }
                    {
                        void * t427 = gm_l0;
                        gm_t88rcucg1b7uk t428 = gm_l8;
                        gm_f_int_info_2tcdf1n((void *)&gm_l62, (void *)NULL, cx, NULL, t427, t428);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t429 = (uint8_t)((((uint8_t *)&gm_l62))[16] != 0);
                    if (!t429) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 82, 19);
                    gm_tuglhjjmj2g58 t430 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&gm_l62));
                    gm_l30 = t430;
                    gm_tuglhjjmj2g58 t431 = gm_l30;
                    gm_b16_8 t432 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t431));
                    gm_l31 = t432;
                    gm_b16_8 t433 = gm_l7;
                    gm_l32 = t433;
                    void * t434 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t435 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t435)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t435, gm_src2j6ljeja7ilp, 85, 18);
                    gm_b16_8 t436 = GM_LD(gm_b16_8, (((uint8_t *)t434) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_l33 = t436;
                    {
                        void * t437 = gm_l0;
                        gm_t88rcucg1b7uk t438 = gm_l2;
                        gm_b16_8 t439 = gm_l32;
                        gm_b16_8 t440 = gm_l33;
                        gm_b16_8 t441 = gm_l31;
                        gm_b16_8 t442 = gm_l32;
                        gm_b16_8 t443 = gm_l31;
                        gm_b16_8 t444 = gm_l33;
                        gm_b16_8 t445 = gm_l31;
                        gm_b16_8 t446 = gm_l33;
                        gm_b16_8 t447 = gm_l31;
                        gm_b16_8 t448 = gm_l32;
                        gm_tk6rvhh54g86p t449;
                        GM_ST(gm_b16_8, ((uint8_t *)&t449 + 0), t439);
                        GM_ST(gm_b16_8, ((uint8_t *)&t449 + 16), t440);
                        GM_ST(gm_b16_8, ((uint8_t *)&t449 + 32), t441);
                        GM_ST(gm_b16_8, ((uint8_t *)&t449 + 48), t442);
                        GM_ST(gm_b16_8, ((uint8_t *)&t449 + 64), t443);
                        GM_ST(gm_b16_8, ((uint8_t *)&t449 + 80), t444);
                        GM_ST(gm_b16_8, ((uint8_t *)&t449 + 96), t445);
                        GM_ST(gm_b16_8, ((uint8_t *)&t449 + 112), t446);
                        GM_ST(gm_b16_8, ((uint8_t *)&t449 + 128), t447);
                        GM_ST(gm_b16_8, ((uint8_t *)&t449 + 144), t448);
                        gm_f_format_53mv29j((void *)&gm_l63, (void *)NULL, cx, NULL, &t449);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t450 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                        if (!t450) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 8, gm_src2j6ljeja7ilp, 86, 30);
                        gm_b16_8 t451 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l63) + 8));
                        gm_b16_8 t452 = GM_LD(gm_b16_8, ((uint8_t *)&t451));
                        gm_b16_8 t453 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t437, t438, t452);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t453);
                        goto gm_fdone;
                    }
                }
gm_lb45:;
                {
                    uint8_t t454 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t455 = (uint8_t)(t454 == ((uint8_t)22ULL));
                    if (!t455) {
                        goto gm_lb46;
                    }
                    {
                        void * t456 = gm_l0;
                        gm_t88rcucg1b7uk t457 = gm_l2;
                        gm_b16_8 t458 = gm_l7;
                        gm_tnpifv20irnph t459;
                        GM_ST(gm_b16_8, ((uint8_t *)&t459 + 0), t458);
                        gm_f_format_o73frof((void *)&gm_l64, (void *)NULL, cx, NULL, t459);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t460 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                        if (!t460) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 8, gm_src2j6ljeja7ilp, 88, 36);
                        gm_b16_8 t461 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l64) + 8));
                        gm_b16_8 t462 = GM_LD(gm_b16_8, ((uint8_t *)&t461));
                        gm_b16_8 t463 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t456, t457, t462);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t463);
                        goto gm_fdone;
                    }
                }
gm_lb46:;
                {
                    uint8_t t464 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t465 = (uint8_t)(t464 == ((uint8_t)23ULL));
                    if (!t465) {
                        goto gm_lb47;
                    }
                    {
                        void * t466 = gm_l0;
                        gm_t88rcucg1b7uk t467 = gm_l2;
                        gm_b16_8 t468 = gm_l7;
                        gm_tnpifv20irnph t469;
                        GM_ST(gm_b16_8, ((uint8_t *)&t469 + 0), t468);
                        gm_f_format_ljsk7ji((void *)&gm_l65, (void *)NULL, cx, NULL, t469);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t470 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                        if (!t470) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 8, gm_src2j6ljeja7ilp, 89, 36);
                        gm_b16_8 t471 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                        gm_b16_8 t472 = GM_LD(gm_b16_8, ((uint8_t *)&t471));
                        gm_b16_8 t473 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t466, t467, t472);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t473);
                        goto gm_fdone;
                    }
                }
gm_lb47:;
            }
            {
                {
                    uint8_t t474 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t475 = (uint8_t)(t474 == ((uint8_t)24ULL));
                    if (!t475) {
                        goto gm_lb48;
                    }
                    {
                        void * t476 = gm_l0;
                        gm_t88rcucg1b7uk t477 = gm_l2;
                        gm_b16_8 t478 = gm_l7;
                        gm_tnpifv20irnph t479;
                        GM_ST(gm_b16_8, ((uint8_t *)&t479 + 0), t478);
                        gm_f_format_jj2aipm((void *)&gm_l66, (void *)NULL, cx, NULL, t479);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t480 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                        if (!t480) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 8, gm_src2j6ljeja7ilp, 90, 39);
                        gm_b16_8 t481 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l66) + 8));
                        gm_b16_8 t482 = GM_LD(gm_b16_8, ((uint8_t *)&t481));
                        gm_b16_8 t483 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t476, t477, t482);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t483);
                        goto gm_fdone;
                    }
                }
gm_lb48:;
                {
                    uint8_t t484 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t485 = (uint8_t)(t484 == ((uint8_t)25ULL));
                    if (!t485) {
                        goto gm_lb49;
                    }
                    {
                        void * t486 = gm_l0;
                        gm_t88rcucg1b7uk t487 = gm_l2;
                        gm_b16_8 t488 = gm_l7;
                        gm_tnpifv20irnph t489;
                        GM_ST(gm_b16_8, ((uint8_t *)&t489 + 0), t488);
                        gm_f_format_ckgfhm4((void *)&gm_l67, (void *)NULL, cx, NULL, t489);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t490 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                        if (!t490) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_src2j6ljeja7ilp, 91, 38);
                        gm_b16_8 t491 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                        gm_b16_8 t492 = GM_LD(gm_b16_8, ((uint8_t *)&t491));
                        gm_b16_8 t493 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t486, t487, t492);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t493);
                        goto gm_fdone;
                    }
                }
gm_lb49:;
                {
                    uint8_t t494 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t495 = (uint8_t)(t494 == ((uint8_t)26ULL));
                    if (!t495) {
                        goto gm_lb50;
                    }
                    {
                        void * t496 = gm_l0;
                        void * t497 = GM_LD(void *, ((uint8_t *)t496));
                        void * t498 = (void *)(((uint8_t *)t497) + 8);
                        gm_t88rcucg1b7uk t499 = gm_l8;
                        gm_t53at80pfdus3 t500;
                        gm_f_kind_ghvav22((void *)&t500, t498, GM_CONV(gm_csguankko4lmhd, t499));
                        uint8_t t501 = GM_LD(uint8_t, (((uint8_t *)&t500) + 0));
                        uint8_t t502 = (uint8_t)(t501 == ((uint8_t)3ULL));
                        if (t502) {
                            uint16_t t503 = GM_LD(uint16_t, (((uint8_t *)&t500) + 8));
                            t502 = (uint8_t)(t503 == ((uint16_t)32ULL));
                        }
                        uint8_t t504 = (uint8_t)(t502);
                        gm_t53at80pfdus3 t505;
                        memset(&t505, 0, sizeof t505);
                        GM_ST(uint8_t, ((uint8_t *)&t505 + 0), ((uint8_t)3ULL));
                        GM_ST(uint16_t, ((uint8_t *)&t505 + 8), ((uint16_t)32ULL));
                        if (t504) {
                            gm_b16_8 t506;
                            GM_ST(void *, (uint8_t *)&t506, (void *)(gm_s63o5blu3j46d));
                            GM_ST(uintptr_t, (uint8_t *)&t506 + 8, (uintptr_t)(15));
                            gm_l34 = t506;
                        } else {
                            gm_b16_8 t507;
                            GM_ST(void *, (uint8_t *)&t507, (void *)(gm_s565fgm0tsngr));
                            GM_ST(uintptr_t, (uint8_t *)&t507 + 8, (uintptr_t)(14));
                            gm_l34 = t507;
                        }
                    }
                    {
                        void * t508 = gm_l0;
                        gm_t88rcucg1b7uk t509 = gm_l2;
                        gm_b16_8 t510 = gm_l34;
                        gm_b16_8 t511 = gm_l7;
                        gm_t6j6smj7qoctu t512;
                        GM_ST(gm_b16_8, ((uint8_t *)&t512 + 0), t510);
                        GM_ST(gm_b16_8, ((uint8_t *)&t512 + 16), t511);
                        gm_f_format_nm3fl8o((void *)&gm_l68, (void *)NULL, cx, NULL, &t512);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t513 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                        if (!t513) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 8, gm_src2j6ljeja7ilp, 94, 35);
                        gm_b16_8 t514 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l68) + 8));
                        gm_b16_8 t515 = GM_LD(gm_b16_8, ((uint8_t *)&t514));
                        gm_b16_8 t516 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t508, t509, t515);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l35 = t516;
                    }
                    {
                        void * t517 = gm_l0;
                        gm_t88rcucg1b7uk t518 = gm_l2;
                        gm_b16_8 t519 = gm_l35;
                        gm_b16_8 t520 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t517, t518, t519);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t520);
                        goto gm_fdone;
                    }
                }
gm_lb50:;
            }
            {
                {
                    uint8_t t521 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t522 = (uint8_t)(t521 == ((uint8_t)27ULL));
                    if (!t522) {
                        goto gm_lb51;
                    }
                    {
                        void * t523 = gm_l0;
                        void * t524 = GM_LD(void *, ((uint8_t *)t523));
                        void * t525 = (void *)(((uint8_t *)t524) + 8);
                        gm_t88rcucg1b7uk t526 = gm_l8;
                        gm_t53at80pfdus3 t527;
                        gm_f_kind_ghvav22((void *)&t527, t525, GM_CONV(gm_csguankko4lmhd, t526));
                        uint8_t t528 = GM_LD(uint8_t, (((uint8_t *)&t527) + 0));
                        uint8_t t529 = (uint8_t)(t528 == ((uint8_t)3ULL));
                        if (t529) {
                            uint16_t t530 = GM_LD(uint16_t, (((uint8_t *)&t527) + 8));
                            t529 = (uint8_t)(t530 == ((uint16_t)32ULL));
                        }
                        uint8_t t531 = (uint8_t)(t529);
                        gm_t53at80pfdus3 t532;
                        memset(&t532, 0, sizeof t532);
                        GM_ST(uint8_t, ((uint8_t *)&t532 + 0), ((uint8_t)3ULL));
                        GM_ST(uint16_t, ((uint8_t *)&t532 + 8), ((uint16_t)32ULL));
                        if (t531) {
                            gm_b16_8 t533;
                            GM_ST(void *, (uint8_t *)&t533, (void *)(gm_svpr02j6er8vd));
                            GM_ST(uintptr_t, (uint8_t *)&t533 + 8, (uintptr_t)(14));
                            gm_l36 = t533;
                        } else {
                            gm_b16_8 t534;
                            GM_ST(void *, (uint8_t *)&t534, (void *)(gm_shhrbleiv8o3j));
                            GM_ST(uintptr_t, (uint8_t *)&t534 + 8, (uintptr_t)(13));
                            gm_l36 = t534;
                        }
                    }
                    {
                        void * t535 = gm_l0;
                        gm_t88rcucg1b7uk t536 = gm_l2;
                        gm_b16_8 t537 = gm_l36;
                        gm_b16_8 t538 = gm_l7;
                        void * t539 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t540 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t540)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t540, gm_src2j6ljeja7ilp, 99, 71);
                        gm_b16_8 t541 = GM_LD(gm_b16_8, (((uint8_t *)t539) + (uintptr_t)((uint64_t)1ULL) * 16));
                        void * t542 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t543 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t543)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t543, gm_src2j6ljeja7ilp, 99, 80);
                        gm_b16_8 t544 = GM_LD(gm_b16_8, (((uint8_t *)t542) + (uintptr_t)((uint64_t)2ULL) * 16));
                        gm_t0uuhaljsf410 t545;
                        GM_ST(gm_b16_8, ((uint8_t *)&t545 + 0), t537);
                        GM_ST(gm_b16_8, ((uint8_t *)&t545 + 16), t538);
                        GM_ST(gm_b16_8, ((uint8_t *)&t545 + 32), t541);
                        GM_ST(gm_b16_8, ((uint8_t *)&t545 + 48), t544);
                        gm_f_format_261kv0l((void *)&gm_l69, (void *)NULL, cx, NULL, &t545);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t546 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                        if (!t546) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 8, gm_src2j6ljeja7ilp, 99, 35);
                        gm_b16_8 t547 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l69) + 8));
                        gm_b16_8 t548 = GM_LD(gm_b16_8, ((uint8_t *)&t547));
                        gm_b16_8 t549 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t535, t536, t548);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l37 = t549;
                    }
                    {
                        void * t550 = gm_l0;
                        gm_t88rcucg1b7uk t551 = gm_l2;
                        gm_b16_8 t552 = gm_l37;
                        gm_b16_8 t553 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t550, t551, t552);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t553);
                        goto gm_fdone;
                    }
                }
gm_lb51:;
                {
                    {
                        {
                            uint8_t t554 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t555 = (uint8_t)(t554 == ((uint8_t)65ULL));
                            if (!t555) {
                                goto gm_lb54;
                            }
                            goto gm_lb53;
                        }
gm_lb54:;
                        {
                            uint8_t t556 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t557 = (uint8_t)(t556 == ((uint8_t)66ULL));
                            if (!t557) {
                                goto gm_lb55;
                            }
                            goto gm_lb53;
                        }
gm_lb55:;
                        {
                            uint8_t t558 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t559 = (uint8_t)(t558 == ((uint8_t)67ULL));
                            if (!t559) {
                                goto gm_lb56;
                            }
                            goto gm_lb53;
                        }
gm_lb56:;
                        {
                            uint8_t t560 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t561 = (uint8_t)(t560 == ((uint8_t)68ULL));
                            if (!t561) {
                                goto gm_lb57;
                            }
                            goto gm_lb53;
                        }
gm_lb57:;
                        {
                            uint8_t t562 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t563 = (uint8_t)(t562 == ((uint8_t)69ULL));
                            if (!t563) {
                                goto gm_lb58;
                            }
                            goto gm_lb53;
                        }
gm_lb58:;
                        {
                            uint8_t t564 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t565 = (uint8_t)(t564 == ((uint8_t)71ULL));
                            if (!t565) {
                                goto gm_lb59;
                            }
                            goto gm_lb53;
                        }
gm_lb59:;
                        {
                            uint8_t t566 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t567 = (uint8_t)(t566 == ((uint8_t)72ULL));
                            if (!t567) {
                                goto gm_lb60;
                            }
                            goto gm_lb53;
                        }
gm_lb60:;
                        goto gm_lb52;
                    }
gm_lb53:;
                    {
                        void * t568 = gm_l0;
                        gm_tl874t2atnlh3 t569 = gm_l1;
                        gm_t88rcucg1b7uk t570 = gm_l2;
                        gm_t8588ma2cr9d7 t571 = gm_l3;
                        gm_b16_8 t572 = gm_l4;
                        gm_b16_8 t573 = gm_l5;
                        gm_f_atomic_plj0t12((void *)&gm_l70, (void *)NULL, cx, NULL, t568, t569, t570, t571, t572, t573);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t574 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                    if (t574) {
                    } else {
                        gm_t08olq97rscn3 t575 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l70) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t575);
                        goto gm_fdone;
                    }
                    {
                        gm_b16_8 t576 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l70) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t576);
                        goto gm_fdone;
                    }
                }
gm_lb52:;
            }
            {
                {
                    {
                        {
                            uint8_t t577 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t578 = (uint8_t)(t577 == ((uint8_t)32ULL));
                            if (!t578) {
                                goto gm_lb63;
                            }
                            goto gm_lb62;
                        }
gm_lb63:;
                        {
                            uint8_t t579 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t580 = (uint8_t)(t579 == ((uint8_t)33ULL));
                            if (!t580) {
                                goto gm_lb64;
                            }
                            goto gm_lb62;
                        }
gm_lb64:;
                        goto gm_lb61;
                    }
gm_lb62:;
                    void * t581 = gm_l0;
                    gm_t88rcucg1b7uk t582 = gm_l2;
                    gm_t8588ma2cr9d7 t583 = gm_l3;
                    gm_t88rcucg1b7uk t584 = gm_l8;
                    gm_b16_8 t585 = gm_l5;
                    gm_f_rotate_tpbpt1c((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t581, t582, t583, t584, t585);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb61:;
                {
                    {
                        {
                            uint8_t t586 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t587 = (uint8_t)(t586 == ((uint8_t)28ULL));
                            if (!t587) {
                                goto gm_lb67;
                            }
                            goto gm_lb66;
                        }
gm_lb67:;
                        {
                            uint8_t t588 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t589 = (uint8_t)(t588 == ((uint8_t)29ULL));
                            if (!t589) {
                                goto gm_lb68;
                            }
                            goto gm_lb66;
                        }
gm_lb68:;
                        {
                            uint8_t t590 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t591 = (uint8_t)(t590 == ((uint8_t)30ULL));
                            if (!t591) {
                                goto gm_lb69;
                            }
                            goto gm_lb66;
                        }
gm_lb69:;
                        {
                            uint8_t t592 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t593 = (uint8_t)(t592 == ((uint8_t)31ULL));
                            if (!t593) {
                                goto gm_lb70;
                            }
                            goto gm_lb66;
                        }
gm_lb70:;
                        {
                            uint8_t t594 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                            uint8_t t595 = (uint8_t)(t594 == ((uint8_t)34ULL));
                            if (!t595) {
                                goto gm_lb71;
                            }
                            goto gm_lb66;
                        }
gm_lb71:;
                        goto gm_lb65;
                    }
gm_lb66:;
                    void * t596 = gm_l0;
                    gm_tl874t2atnlh3 t597 = gm_l1;
                    gm_t88rcucg1b7uk t598 = gm_l2;
                    gm_t8588ma2cr9d7 t599 = gm_l3;
                    gm_t88rcucg1b7uk t600 = gm_l8;
                    gm_b16_8 t601 = gm_l7;
                    gm_f_bit_count_77uni44((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t596, t597, t598, t599, t600, t601);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb65:;
                {
                    uint8_t t602 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t603 = (uint8_t)(t602 == ((uint8_t)21ULL));
                    if (!t603) {
                        goto gm_lb72;
                    }
                    gm_t9qkh0rsooc8d t604 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l3) + 4));
                    gm_l38 = t604;
                    void * t605 = gm_l0;
                    gm_tl874t2atnlh3 t606 = gm_l1;
                    gm_t88rcucg1b7uk t607 = gm_l2;
                    gm_t9qkh0rsooc8d t608 = gm_l38;
                    gm_t88rcucg1b7uk t609 = gm_l8;
                    gm_b16_8 t610 = gm_l5;
                    gm_f_pow_s0lbtt4((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t605, t606, t607, t608, t609, t610);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb72:;
                {
                    uint8_t t611 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t612 = (uint8_t)(t611 == ((uint8_t)61ULL));
                    if (!t612) {
                        goto gm_lb73;
                    }
                    {
                        void * t613 = gm_l0;
                        gm_t88rcucg1b7uk t614 = gm_l2;
                        gm_b16_8 t615 = gm_l7;
                        gm_tnpifv20irnph t616;
                        GM_ST(gm_b16_8, ((uint8_t *)&t616 + 0), t615);
                        gm_f_format_68bbqsa((void *)&gm_l71, (void *)NULL, cx, NULL, t616);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t617 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                        if (!t617) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 8, gm_src2j6ljeja7ilp, 106, 38);
                        gm_b16_8 t618 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l71) + 8));
                        gm_b16_8 t619 = GM_LD(gm_b16_8, ((uint8_t *)&t618));
                        gm_b16_8 t620 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t613, t614, t619);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t620);
                        goto gm_fdone;
                    }
                }
gm_lb73:;
            }
            {
                {
                    uint8_t t621 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t622 = (uint8_t)(t621 == ((uint8_t)70ULL));
                    if (!t622) {
                        goto gm_lb74;
                    }
                    void * t623 = gm_l0;
                    gm_t88rcucg1b7uk t624 = gm_l8;
                    gm_b16_8 t625 = gm_l7;
                    gm_b16_8 t626 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t623, t624, t625);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l39 = t626;
                    void * t627 = gm_l0;
                    gm_b16_8 t628 = gm_l39;
                    gm_tnpifv20irnph t629;
                    GM_ST(gm_b16_8, ((uint8_t *)&t629 + 0), t628);
                    gm_b0_1 t630 = gm_f_linef_di9ea86((void *)NULL, cx, NULL, t627, t629);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t631 = gm_l0;
                    gm_b16_8 t632 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t631);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t632);
                    goto gm_fdone;
                }
gm_lb74:;
                {
                    uint8_t t633 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t634 = (uint8_t)(t633 == ((uint8_t)35ULL));
                    if (!t634) {
                        goto gm_lb75;
                    }
                    void * t635 = gm_l0;
                    gm_t88rcucg1b7uk t636 = gm_l2;
                    gm_b16_8 t637 = gm_l7;
                    gm_f_black_box_r6deojc((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t635, t636, t637);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
gm_lb75:;
                {
                    uint8_t t638 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                    uint8_t t639 = (uint8_t)(t638 == ((uint8_t)63ULL));
                    if (!t639) {
                        goto gm_lb76;
                    }
                    void * t640 = gm_l0;
                    void * t641 = GM_LD(void *, ((uint8_t *)t640));
                    void * t642 = (void *)(((uint8_t *)t641) + 2024);
                    void * t643 = gm_l0;
                    void * t644 = GM_LD(void *, ((uint8_t *)t643));
                    void * t645 = (void *)(((uint8_t *)t644) + 8);
                    gm_t88rcucg1b7uk t646 = gm_l8;
                    gm_b16_8 t647 = gm_f_hash_fn_nlk82uj((void *)NULL, cx, NULL, t642, t645, t646);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l40 = t647;
                    void * t648 = gm_l0;
                    gm_t88rcucg1b7uk t649 = gm_l8;
                    gm_b16_8 t650 = gm_l7;
                    gm_b16_8 t651 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t648, t649, t650);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l41 = t651;
                    {
                        void * t652 = gm_l0;
                        gm_t88rcucg1b7uk t653 = gm_l2;
                        gm_b16_8 t654 = gm_l40;
                        gm_b16_8 t655 = gm_l41;
                        void * t656 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t657 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t657)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t657, gm_src2j6ljeja7ilp, 116, 95);
                        gm_b16_8 t658 = GM_LD(gm_b16_8, (((uint8_t *)t656) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_tfeab279c16rr t659;
                        GM_ST(gm_b16_8, ((uint8_t *)&t659 + 0), t654);
                        GM_ST(gm_b16_8, ((uint8_t *)&t659 + 16), t655);
                        GM_ST(gm_b16_8, ((uint8_t *)&t659 + 32), t658);
                        gm_f_format_3vgp4uu((void *)&gm_l72, (void *)NULL, cx, NULL, &t659);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t660 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                        if (!t660) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 8, gm_src2j6ljeja7ilp, 116, 30);
                        gm_b16_8 t661 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l72) + 8));
                        gm_b16_8 t662 = GM_LD(gm_b16_8, ((uint8_t *)&t661));
                        gm_b16_8 t663 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t652, t653, t662);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t663);
                        goto gm_fdone;
                    }
                }
gm_lb76:;
                {
                    gm_tl874t2atnlh3 t664 = gm_l1;
                    gm_b16_8 t665;
                    GM_ST(void *, (uint8_t *)&t665, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t665 + 8, (uintptr_t)(12));
                    gm_t08olq97rscn3 t666;
                    gm_f_unsupported_531q51r((void *)&t666, (void *)NULL, cx, NULL, t664, t665);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t666);
                    goto gm_fdone;
                }
gm_lb77:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 9, 5);
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
__attribute__((aligned(4))) void gm_f_convert_lqrk2n0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t8588ma2cr9d7 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5) {
    struct gm_fr_convert_lqrk2n0 F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_t8588ma2cr9d7 gm_l3; /* op */
    gm_t88rcucg1b7uk gm_l4; /* src */
    gm_b16_8 gm_l5; /* v */
    gm_t88rcucg1b7uk gm_l6; /* target */
    gm_t88rcucg1b7uk gm_l7; /* x */
    gm_b16_8 gm_l8; /* loc */
    uint8_t gm_l9; /* src_int */
    gm_b16_8 gm_l10; /* src_val */
    gm_t88rcucg1b7uk gm_l11; /* tag */
    gm_b16_8 gm_l12; /* a */
    gm_t88rcucg1b7uk gm_l14; /* u */
    gm_b16_8 gm_l15; /* a */
    gm_tuglhjjmj2g58 gm_l16; /* ii */
    gm_b16_8 gm_l17; /* c */
    uint8_t gm_l18; /* signed */
    uint8_t gm_l19; /* si */
    gm_b16_8 gm_l20; /* fits */
    gm_b16_8 gm_l21; /* wrapped */
    gm_b16_8 gm_l22; /* res */
    gm_b16_8 gm_l23; /* w */
    gm_b16_8 gm_l24; /* some */
    gm_b16_8 gm_l25; /* none */
    gm_b16_8 gm_l26; /* min */
    gm_b16_8 gm_l27; /* max */
    gm_b16_8 gm_l28; /* neg */
    void * gm_l29; /* tys */
    gm_t88rcucg1b7uk gm_l30; /* f64_ty */
    gm_b16_8 gm_l31; /* x */
    gm_b16_8 gm_l32; /* rounded */
    gm_b16_8 gm_l33; /* r */
    gm_b16_8 gm_l34; /* lo */
    gm_b16_8 gm_l35; /* hi */
    gm_b16_8 gm_l36; /* in_range */
    gm_b16_8 gm_l37; /* res */
    gm_b16_8 gm_l38; /* w */
    gm_b16_8 gm_l39; /* some */
    gm_b16_8 gm_l40; /* none */
    gm_b16_8 gm_l41; /* min */
    gm_b16_8 gm_l42; /* max */
    gm_b16_8 gm_l43; /* r */
    gm_tcmkompbl09il gm_l44; /* bl */
    gm_tuglhjjmj2g58 gm_l45; /* ui */
    gm_b16_8 gm_l46; /* w */
    gm_b16_8 gm_l47; /* b */
    GM_STACK_CHECK(&F, 6298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_t53at80pfdus3 gm_l48;
    gm_t5v8n6qrn4rh4 gm_l49;
    gm_tabb710d9mq2e gm_l50;
    gm_tabb710d9mq2e gm_l51;
    uint8_t gm_l52;
    gm_t53at80pfdus3 gm_l53;
    gm_tabb710d9mq2e gm_l54;
    gm_tabb710d9mq2e gm_l55;
    gm_tabb710d9mq2e gm_l56;
    gm_t5v8n6qrn4rh4 gm_l57;
    gm_tqcc6dmlne11b gm_l58;
    gm_t5v8n6qrn4rh4 gm_l59;
    gm_tabb710d9mq2e gm_l60;
    gm_tabb710d9mq2e gm_l61;
    gm_tabb710d9mq2e gm_l62;
    gm_tabb710d9mq2e gm_l63;
    gm_tabb710d9mq2e gm_l64;
    gm_tabb710d9mq2e gm_l65;
    gm_tabb710d9mq2e gm_l66;
    gm_tabb710d9mq2e gm_l67;
    gm_tabb710d9mq2e gm_l68;
    gm_tabb710d9mq2e gm_l69;
    gm_tabb710d9mq2e gm_l70;
    gm_tabb710d9mq2e gm_l71;
    gm_tabb710d9mq2e gm_l72;
    gm_t57euh612ligr gm_l73;
    gm_tqcc6dmlne11b gm_l74;
    gm_tabb710d9mq2e gm_l75;
    gm_tabb710d9mq2e gm_l76;
    {
        gm_t88rcucg1b7uk t39 = gm_l2;
        gm_l6 = t39;
        gm_t8588ma2cr9d7 t40 = gm_l3;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t40) + 0));
        uint8_t t42 = (uint8_t)((t41 == ((uint8_t)5ULL)));
        gm_t8588ma2cr9d7 t43;
        GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t43 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)5ULL));
        if (t42) {
            {
                {
                    void * t44 = gm_l0;
                    void * t45 = GM_LD(void *, ((uint8_t *)t44));
                    void * t46 = (void *)(((uint8_t *)t45) + 8);
                    gm_t88rcucg1b7uk t47 = gm_l2;
                    gm_f_kind_ghvav22((void *)&gm_l48, t46, GM_CONV(gm_csguankko4lmhd, t47));
                    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&gm_l48) + 0));
                    uint8_t t49 = (uint8_t)(t48 == ((uint8_t)13ULL));
                    if (!t49) {
                        goto gm_lb2;
                    }
                    gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l48) + 8));
                    gm_l7 = t50;
                    gm_t88rcucg1b7uk t51 = gm_l7;
                    gm_l6 = t51;
                    goto gm_lb1;
                }
gm_lb2:;
            }
gm_lb1:;
        }
        void * t52 = gm_l0;
        void * t53 = GM_LD(void *, ((uint8_t *)t52));
        gm_tl874t2atnlh3 t54 = gm_l1;
        gm_b16_8 t55 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t53, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t55;
        gm_l9 = ((uint8_t)0);
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(0));
        gm_l10 = t56;
        void * t57 = gm_l0;
        void * t58 = GM_LD(void *, ((uint8_t *)t57));
        void * t59 = (void *)(((uint8_t *)t58) + 8);
        gm_t88rcucg1b7uk t60 = gm_l4;
        gm_f_repr_53quqki((void *)&gm_l49, (void *)NULL, cx, NULL, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            {
                uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l49) + 0));
                uint8_t t62 = (uint8_t)(t61 == ((uint8_t)0ULL));
                if (!t62) {
                    goto gm_lb4;
                }
                gm_l9 = ((uint8_t)1);
                gm_b16_8 t63 = gm_l5;
                gm_l10 = t63;
                goto gm_lb3;
            }
gm_lb4:;
            {
                uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l49) + 0));
                uint8_t t65 = (uint8_t)(t64 == ((uint8_t)2ULL));
                if (!t65) {
                    goto gm_lb5;
                }
                gm_l9 = ((uint8_t)1);
                gm_b16_8 t66 = gm_l5;
                gm_tnpifv20irnph t67;
                GM_ST(gm_b16_8, ((uint8_t *)&t67 + 0), t66);
                gm_f_format_p1ca3i8((void *)&gm_l50, (void *)NULL, cx, NULL, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 8, gm_src2j6ljeja7ilp, 358, 23);
                gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 8));
                gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
                gm_l10 = t70;
                goto gm_lb3;
            }
gm_lb5:;
            {
                uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l49) + 0));
                uint8_t t72 = (uint8_t)(t71 == ((uint8_t)1ULL));
                if (!t72) {
                    goto gm_lb6;
                }
                gm_b16_8 t73 = gm_l5;
                gm_tnpifv20irnph t74;
                GM_ST(gm_b16_8, ((uint8_t *)&t74 + 0), t73);
                gm_f_format_gp2h96m((void *)&gm_l51, (void *)NULL, cx, NULL, t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                if (!t75) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 8, gm_src2j6ljeja7ilp, 360, 34);
                gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 8));
                gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                gm_l10 = t77;
                goto gm_lb3;
            }
gm_lb6:;
            {
                uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l49) + 0));
                uint8_t t79 = (uint8_t)(t78 == ((uint8_t)6ULL));
                if (!t79) {
                    goto gm_lb7;
                }
                void * t80 = gm_l0;
                void * t81 = GM_LD(void *, ((uint8_t *)t80));
                void * t82 = (void *)(((uint8_t *)t81) + 8);
                gm_t88rcucg1b7uk t83 = gm_l4;
                gm_f_kind_ghvav22((void *)&gm_l53, t82, GM_CONV(gm_csguankko4lmhd, t83));
                {
                    {
                        uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l53) + 0));
                        uint8_t t85 = (uint8_t)(t84 == ((uint8_t)17ULL));
                        if (!t85) {
                            goto gm_lb9;
                        }
                        gm_l52 = ((uint8_t)1);
                        goto gm_lb8;
                    }
gm_lb9:;
                    {
                        gm_l52 = ((uint8_t)0);
                        goto gm_lb8;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 361, 18);
                }
gm_lb8:;
                uint8_t t86 = gm_l52;
                if (!t86) {
                    goto gm_lb7;
                }
                void * t87 = gm_l0;
                void * t88 = GM_LD(void *, ((uint8_t *)t87));
                void * t89 = (void *)(((uint8_t *)t88) + 8);
                gm_t88rcucg1b7uk t90 = gm_l4;
                void * t91 = gm_f_adt_of_gej99vn(t89, GM_CONV(gm_csguankko4lmhd, t90));
                {
                    {
                        uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)t91) + 24) + 0));
                        uint8_t t93 = (uint8_t)(t92 == ((uint8_t)1ULL));
                        if (!t93) {
                            goto gm_lb12;
                        }
                        gm_t88rcucg1b7uk t94 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t91) + 24) + 8));
                        gm_l11 = t94;
                        void * t95 = gm_l0;
                        gm_t88rcucg1b7uk t96 = gm_l4;
                        gm_b16_8 t97 = gm_l5;
                        gm_b16_8 t98 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t95, t96, t97);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l12 = t98;
                        gm_l9 = ((uint8_t)1);
                        void * t99 = gm_l0;
                        gm_t88rcucg1b7uk t100 = gm_l11;
                        gm_b16_8 t101 = gm_l12;
                        gm_b16_8 t102 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t99, t100, t101);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l10 = t102;
                        goto gm_lb11;
                    }
gm_lb12:;
                    {
                        uint8_t t103 = GM_LD(uint8_t, ((((uint8_t *)t91) + 24) + 0));
                        uint8_t t104 = (uint8_t)(t103 == ((uint8_t)0ULL));
                        if (!t104) {
                            goto gm_lb13;
                        }
                        memmove(((uint8_t *)&F.l13), ((((uint8_t *)t91) + 24) + 8), 48);
                        uint8_t t105 = (uint8_t)(((((uint8_t *)&F.l13) + 40))[4] != 0);
                        if (t105) {
                            gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 40));
                            gm_l14 = t106;
                        } else {
                            gm_tl874t2atnlh3 t107 = gm_l1;
                            void * t108 = gm_l0;
                            void * t109 = GM_LD(void *, ((uint8_t *)t108));
                            void * t110 = (void *)(((uint8_t *)t109) + 8);
                            gm_t88rcucg1b7uk t111 = gm_l4;
                            gm_b16_8 t112 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t110, t111);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnpifv20irnph t113;
                            GM_ST(gm_b16_8, ((uint8_t *)&t113 + 0), t112);
                            gm_f_format_m0j4lkh((void *)&gm_l54, (void *)NULL, cx, NULL, t113);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t114 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                            if (!t114) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 8, gm_src2j6ljeja7ilp, 368, 61);
                            gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l54) + 8));
                            gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
                            gm_t08olq97rscn3 t117;
                            gm_f_unsupported_531q51r((void *)&t117, (void *)NULL, cx, NULL, t107, t116);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t117);
                            goto gm_fdone;
                        }
                        void * t118 = gm_l0;
                        gm_t88rcucg1b7uk t119 = gm_l4;
                        gm_b16_8 t120 = gm_l5;
                        gm_b16_8 t121 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t118, t119, t120);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l15 = t121;
                        gm_l9 = ((uint8_t)1);
                        void * t122 = gm_l0;
                        gm_t88rcucg1b7uk t123 = gm_l14;
                        gm_b16_8 t124 = gm_l15;
                        gm_b16_8 t125 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t122, t123, t124);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l10 = t125;
                        goto gm_lb11;
                    }
gm_lb13:;
                    {
                        gm_tl874t2atnlh3 t126 = gm_l1;
                        void * t127 = gm_l0;
                        void * t128 = GM_LD(void *, ((uint8_t *)t127));
                        void * t129 = (void *)(((uint8_t *)t128) + 8);
                        gm_t88rcucg1b7uk t130 = gm_l4;
                        gm_b16_8 t131 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t129, t130);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t132;
                        GM_ST(gm_b16_8, ((uint8_t *)&t132 + 0), t131);
                        gm_f_format_m0j4lkh((void *)&gm_l55, (void *)NULL, cx, NULL, t132);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t133 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                        if (!t133) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 8, gm_src2j6ljeja7ilp, 373, 44);
                        gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 8));
                        gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                        gm_t08olq97rscn3 t136;
                        gm_f_unsupported_531q51r((void *)&t136, (void *)NULL, cx, NULL, t126, t135);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t136);
                        goto gm_fdone;
                    }
gm_lb14:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 361, 53);
                }
gm_lb11:;
                goto gm_lb3;
            }
gm_lb7:;
            {
                gm_tl874t2atnlh3 t137 = gm_l1;
                void * t138 = gm_l0;
                void * t139 = GM_LD(void *, ((uint8_t *)t138));
                void * t140 = (void *)(((uint8_t *)t139) + 8);
                gm_t88rcucg1b7uk t141 = gm_l4;
                gm_b16_8 t142 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t140, t141);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t143;
                GM_ST(gm_b16_8, ((uint8_t *)&t143 + 0), t142);
                gm_f_format_m0j4lkh((void *)&gm_l56, (void *)NULL, cx, NULL, t143);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t144 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                if (!t144) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 8, gm_src2j6ljeja7ilp, 375, 40);
                gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l56) + 8));
                gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&t145));
                gm_t08olq97rscn3 t147;
                gm_f_unsupported_531q51r((void *)&t147, (void *)NULL, cx, NULL, t137, t146);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t147);
                goto gm_fdone;
            }
gm_lb15:;
            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 351, 5);
        }
gm_lb3:;
    }
    {
        void * t148 = gm_l0;
        void * t149 = GM_LD(void *, ((uint8_t *)t148));
        void * t150 = (void *)(((uint8_t *)t149) + 8);
        gm_t88rcucg1b7uk t151 = gm_l6;
        gm_f_repr_53quqki((void *)&gm_l57, (void *)NULL, cx, NULL, t150, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            {
                {
                    {
                        uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&gm_l57) + 0));
                        uint8_t t153 = (uint8_t)(t152 == ((uint8_t)0ULL));
                        if (!t153) {
                            goto gm_lb17;
                        }
                        void * t154 = gm_l0;
                        gm_t88rcucg1b7uk t155 = gm_l6;
                        gm_f_int_info_2tcdf1n((void *)&gm_l58, (void *)NULL, cx, NULL, t154, t155);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t156 = (uint8_t)((((uint8_t *)&gm_l58))[16] != 0);
                        if (!t156) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 379, 19);
                        gm_tuglhjjmj2g58 t157 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&gm_l58));
                        gm_l16 = t157;
                        gm_tuglhjjmj2g58 t158 = gm_l16;
                        gm_b16_8 t159 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t158));
                        gm_l17 = t159;
                        uint8_t t160 = gm_l9;
                        if (t160) {
                            {
                                void * t161 = gm_l0;
                                void * t162 = GM_LD(void *, ((uint8_t *)t161));
                                void * t163 = (void *)(((uint8_t *)t162) + 8);
                                gm_t88rcucg1b7uk t164 = gm_l4;
                                gm_f_repr_53quqki((void *)&gm_l59, (void *)NULL, cx, NULL, t163, t164);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            {
                                {
                                    uint8_t t165 = GM_LD(uint8_t, (((uint8_t *)&gm_l59) + 0));
                                    uint8_t t166 = (uint8_t)(t165 == ((uint8_t)0ULL));
                                    if (!t166) {
                                        goto gm_lb19;
                                    }
                                    uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&gm_l59) + 8));
                                    gm_l18 = t167;
                                    uint8_t t168 = gm_l18;
                                    gm_l19 = t168;
                                    goto gm_lb18;
                                }
gm_lb19:;
                                {
                                    gm_l19 = ((uint8_t)1);
                                    goto gm_lb18;
                                }
gm_lb20:;
                                gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 382, 28);
                            }
gm_lb18:;
                            gm_tuglhjjmj2g58 t169 = gm_l16;
                            gm_b16_8 t170 = gm_l10;
                            uint8_t t171 = gm_l19;
                            gm_b16_8 t172 = gm_f_arith_fits_7degt3e((void *)NULL, cx, NULL, t169, t170, t171);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l20 = t172;
                            {
                                gm_tuglhjjmj2g58 t173 = gm_l16;
                                gm_b16_8 t174 = gm_l17;
                                gm_b16_8 t175 = gm_l10;
                                gm_t6j6smj7qoctu t176;
                                GM_ST(gm_b16_8, ((uint8_t *)&t176 + 0), t174);
                                GM_ST(gm_b16_8, ((uint8_t *)&t176 + 16), t175);
                                gm_f_format_3vtl3te((void *)&gm_l60, (void *)NULL, cx, NULL, &t176);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t177 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                if (!t177) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 8, gm_src2j6ljeja7ilp, 387, 42);
                                gm_b16_8 t178 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l60) + 8));
                                gm_b16_8 t179 = GM_LD(gm_b16_8, ((uint8_t *)&t178));
                                gm_b16_8 t180 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t173, t179);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l21 = t180;
                            }
                            {
                                {
                                    uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                    uint8_t t182 = (uint8_t)(t181 == ((uint8_t)5ULL));
                                    if (!t182) {
                                        goto gm_lb22;
                                    }
                                    void * t183 = gm_l0;
                                    gm_t88rcucg1b7uk t184 = gm_l2;
                                    gm_b16_8 t185 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t183, t184);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l22 = t185;
                                    void * t186 = gm_l0;
                                    gm_t88rcucg1b7uk t187 = gm_l6;
                                    gm_b16_8 t188 = gm_l21;
                                    gm_b16_8 t189 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t186, t187, t188);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l23 = t189;
                                    void * t190 = gm_l0;
                                    gm_t88rcucg1b7uk t191 = gm_l2;
                                    gm_b16_8 t192 = gm_l23;
                                    gm_b16_8 t193 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t190, t191, t192);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l24 = t193;
                                    void * t194 = gm_l0;
                                    gm_t88rcucg1b7uk t195 = gm_l2;
                                    gm_b16_8 t196 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t194, t195);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l25 = t196;
                                    {
                                        void * t197 = gm_l0;
                                        gm_b16_8 t198 = gm_l22;
                                        gm_b16_8 t199 = gm_l20;
                                        gm_b16_8 t200 = gm_l24;
                                        gm_b16_8 t201 = gm_l25;
                                        gm_t0uuhaljsf410 t202;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t202 + 0), t198);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t202 + 16), t199);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t202 + 32), t200);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t202 + 48), t201);
                                        gm_b0_1 t203 = gm_f_linef_6h7r936((void *)NULL, cx, NULL, t197, &t202);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    {
                                        gm_b16_8 t204 = gm_l22;
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t204);
                                        goto gm_fdone;
                                    }
                                }
gm_lb22:;
                                {
                                    uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                    uint8_t t206 = (uint8_t)(t205 == ((uint8_t)6ULL));
                                    if (!t206) {
                                        goto gm_lb23;
                                    }
                                    {
                                        void * t207 = gm_l0;
                                        gm_t88rcucg1b7uk t208 = gm_l6;
                                        gm_tuglhjjmj2g58 t209 = gm_l16;
                                        gm_tbci5ospk5nnn t210;
                                        gm_f_ii_min_lku7pdi((void *)&t210, (void *)NULL, cx, NULL, t209);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t211 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t207, t208, &t210);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l26 = t211;
                                    }
                                    {
                                        void * t212 = gm_l0;
                                        gm_t88rcucg1b7uk t213 = gm_l6;
                                        gm_tuglhjjmj2g58 t214 = gm_l16;
                                        gm_tbci5ospk5nnn t215;
                                        gm_f_ii_max_gtp5h90((void *)&t215, (void *)NULL, cx, NULL, t214);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t216 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t212, t213, &t215);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l27 = t216;
                                    }
                                    uint8_t t217 = gm_l19;
                                    if (t217) {
                                        gm_b16_8 t218 = gm_l10;
                                        gm_tnpifv20irnph t219;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t219 + 0), t218);
                                        gm_f_format_uhjjs86((void *)&gm_l61, (void *)NULL, cx, NULL, t219);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t220 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                        if (!t220) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 8, gm_src2j6ljeja7ilp, 400, 47);
                                        gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
                                        gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&t221));
                                        gm_l28 = t222;
                                    } else {
                                        gm_b16_8 t223;
                                        GM_ST(void *, (uint8_t *)&t223, (void *)(gm_sicgmpca63jln));
                                        GM_ST(uintptr_t, (uint8_t *)&t223 + 8, (uintptr_t)(1));
                                        gm_l28 = t223;
                                    }
                                    {
                                        void * t224 = gm_l0;
                                        gm_t88rcucg1b7uk t225 = gm_l2;
                                        gm_b16_8 t226 = gm_l20;
                                        gm_b16_8 t227 = gm_l21;
                                        gm_b16_8 t228 = gm_l28;
                                        gm_b16_8 t229 = gm_l26;
                                        gm_b16_8 t230 = gm_l27;
                                        gm_tud7oovfvnf43 t231;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t231 + 0), t226);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t231 + 16), t227);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t231 + 32), t228);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t231 + 48), t229);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t231 + 64), t230);
                                        gm_f_format_nkjq1if((void *)&gm_l62, (void *)NULL, cx, NULL, &t231);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t232 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                        if (!t232) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 8, gm_src2j6ljeja7ilp, 401, 42);
                                        gm_b16_8 t233 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l62) + 8));
                                        gm_b16_8 t234 = GM_LD(gm_b16_8, ((uint8_t *)&t233));
                                        gm_b16_8 t235 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t224, t225, t234);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t235);
                                        goto gm_fdone;
                                    }
                                }
gm_lb23:;
                                {
                                    void * t236 = gm_l0;
                                    gm_t88rcucg1b7uk t237 = gm_l2;
                                    gm_b16_8 t238 = gm_l21;
                                    gm_b16_8 t239 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t236, t237, t238);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t239);
                                    goto gm_fdone;
                                }
gm_lb24:;
                                gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 388, 17);
                            }
gm_lb21:;
                        }
                    }
                    {
                        void * t240 = gm_l0;
                        void * t241 = GM_LD(void *, ((uint8_t *)t240));
                        void * t242 = GM_LD(void *, (((uint8_t *)t241) + 8));
                        void * t243 = (void *)(((uint8_t *)t242) + 8);
                        gm_l29 = t243;
                        void * t244 = gm_l29;
                        gm_t53at80pfdus3 t245;
                        memset(&t245, 0, sizeof t245);
                        GM_ST(uint8_t, ((uint8_t *)&t245 + 0), ((uint8_t)3ULL));
                        GM_ST(uint16_t, ((uint8_t *)&t245 + 8), ((uint16_t)64ULL));
                        gm_t88rcucg1b7uk t246 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t244, &t245);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l30 = t246;
                        void * t247 = gm_l0;
                        gm_t88rcucg1b7uk t248 = gm_l30;
                        gm_b16_8 t249 = gm_l10;
                        gm_b16_8 t250 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t247, t248, t249);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l31 = t250;
                        {
                            {
                                uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                uint8_t t252 = (uint8_t)(t251 == ((uint8_t)2ULL));
                                if (!t252) {
                                    goto gm_lb26;
                                }
                                gm_b16_8 t253 = gm_l31;
                                gm_tnpifv20irnph t254;
                                GM_ST(gm_b16_8, ((uint8_t *)&t254 + 0), t253);
                                gm_f_format_3s3fqnk((void *)&gm_l63, (void *)NULL, cx, NULL, t254);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t255 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                                if (!t255) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 8, gm_src2j6ljeja7ilp, 410, 27);
                                gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l63) + 8));
                                gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&t256));
                                gm_l32 = t257;
                                goto gm_lb25;
                            }
gm_lb26:;
                            {
                                uint8_t t258 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                uint8_t t259 = (uint8_t)(t258 == ((uint8_t)3ULL));
                                if (!t259) {
                                    goto gm_lb27;
                                }
                                gm_b16_8 t260 = gm_l31;
                                gm_tnpifv20irnph t261;
                                GM_ST(gm_b16_8, ((uint8_t *)&t261 + 0), t260);
                                gm_f_format_b05vb48((void *)&gm_l64, (void *)NULL, cx, NULL, t261);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t262 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                if (!t262) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 8, gm_src2j6ljeja7ilp, 411, 27);
                                gm_b16_8 t263 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l64) + 8));
                                gm_b16_8 t264 = GM_LD(gm_b16_8, ((uint8_t *)&t263));
                                gm_l32 = t264;
                                goto gm_lb25;
                            }
gm_lb27:;
                            {
                                uint8_t t265 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                uint8_t t266 = (uint8_t)(t265 == ((uint8_t)4ULL));
                                if (!t266) {
                                    goto gm_lb28;
                                }
                                gm_b16_8 t267 = gm_l31;
                                gm_tnpifv20irnph t268;
                                GM_ST(gm_b16_8, ((uint8_t *)&t268 + 0), t267);
                                gm_f_format_2qdci62((void *)&gm_l65, (void *)NULL, cx, NULL, t268);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t269 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                                if (!t269) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 8, gm_src2j6ljeja7ilp, 412, 26);
                                gm_b16_8 t270 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                                gm_b16_8 t271 = GM_LD(gm_b16_8, ((uint8_t *)&t270));
                                gm_l32 = t271;
                                goto gm_lb25;
                            }
gm_lb28:;
                            {
                                gm_b16_8 t272 = gm_l31;
                                gm_tnpifv20irnph t273;
                                GM_ST(gm_b16_8, ((uint8_t *)&t273 + 0), t272);
                                gm_f_format_7gtji42((void *)&gm_l66, (void *)NULL, cx, NULL, t273);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t274 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                                if (!t274) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 8, gm_src2j6ljeja7ilp, 413, 22);
                                gm_b16_8 t275 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l66) + 8));
                                gm_b16_8 t276 = GM_LD(gm_b16_8, ((uint8_t *)&t275));
                                gm_l32 = t276;
                                goto gm_lb25;
                            }
gm_lb29:;
                            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 409, 31);
                        }
gm_lb25:;
                    }
                    {
                        void * t277 = gm_l0;
                        gm_t88rcucg1b7uk t278 = gm_l30;
                        gm_b16_8 t279 = gm_l32;
                        gm_b16_8 t280 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t277, t278, t279);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l33 = t280;
                        gm_tuglhjjmj2g58 t281 = gm_l16;
                        gm_tbci5ospk5nnn t282;
                        gm_f_ii_min_lku7pdi((void *)&t282, (void *)NULL, cx, NULL, t281);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        double t283 = gm_f_arith_int_to_f64_tiud1hu(&t282);
                        gm_tidu2er0d7h95 t284;
                        GM_ST(double, ((uint8_t *)&t284 + 0), t283);
                        gm_f_format_eucvbnh((void *)&gm_l67, (void *)NULL, cx, NULL, t284);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t285 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                        if (!t285) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_src2j6ljeja7ilp, 416, 19);
                        gm_b16_8 t286 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                        gm_l34 = t286;
                        gm_tuglhjjmj2g58 t287 = gm_l16;
                        gm_tbci5ospk5nnn t288;
                        gm_f_ii_max_gtp5h90((void *)&t288, (void *)NULL, cx, NULL, t287);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tbci5ospk5nnn t289;
                        gm_f_one_08qg1op((void *)&t289, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tbci5ospk5nnn t290;
                        gm_f_add_420lj6o((void *)&t290, (void *)NULL, cx, NULL, &t288, &t289);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        double t291 = gm_f_arith_int_to_f64_tiud1hu(&t290);
                        gm_tidu2er0d7h95 t292;
                        GM_ST(double, ((uint8_t *)&t292 + 0), t291);
                        gm_f_format_eucvbnh((void *)&gm_l68, (void *)NULL, cx, NULL, t292);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t293 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                        if (!t293) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 8, gm_src2j6ljeja7ilp, 417, 19);
                        gm_b16_8 t294 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l68) + 8));
                        gm_l35 = t294;
                        gm_b16_8 t295 = gm_l33;
                        gm_b16_8 t296 = gm_l33;
                        gm_b16_8 t297 = gm_l34;
                        gm_b16_8 t298 = gm_l33;
                        gm_b16_8 t299 = gm_l35;
                        gm_tud7oovfvnf43 t300;
                        GM_ST(gm_b16_8, ((uint8_t *)&t300 + 0), t295);
                        GM_ST(gm_b16_8, ((uint8_t *)&t300 + 16), t296);
                        GM_ST(gm_b16_8, ((uint8_t *)&t300 + 32), t297);
                        GM_ST(gm_b16_8, ((uint8_t *)&t300 + 48), t298);
                        GM_ST(gm_b16_8, ((uint8_t *)&t300 + 64), t299);
                        gm_f_format_aculdom((void *)&gm_l69, (void *)NULL, cx, NULL, &t300);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t301 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                        if (!t301) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l69) + 8, gm_src2j6ljeja7ilp, 418, 25);
                        gm_b16_8 t302 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l69) + 8));
                        gm_l36 = t302;
                        {
                            {
                                uint8_t t303 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                uint8_t t304 = (uint8_t)(t303 == ((uint8_t)5ULL));
                                if (!t304) {
                                    goto gm_lb31;
                                }
                                void * t305 = gm_l0;
                                gm_t88rcucg1b7uk t306 = gm_l2;
                                gm_b16_8 t307 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t305, t306);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l37 = t307;
                                {
                                    void * t308 = gm_l0;
                                    gm_b16_8 t309 = gm_l36;
                                    gm_b16_8 t310 = gm_l31;
                                    gm_b16_8 t311 = gm_l33;
                                    gm_tfeab279c16rr t312;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t312 + 0), t309);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t312 + 16), t310);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t312 + 32), t311);
                                    gm_b0_1 t313 = gm_f_linef_6sel63v((void *)NULL, cx, NULL, t308, &t312);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                {
                                    void * t314 = gm_l0;
                                    gm_t88rcucg1b7uk t315 = gm_l6;
                                    gm_b16_8 t316 = gm_l17;
                                    gm_b16_8 t317 = gm_l33;
                                    gm_t6j6smj7qoctu t318;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t318 + 0), t316);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t318 + 16), t317);
                                    gm_f_format_3vtl3te((void *)&gm_l70, (void *)NULL, cx, NULL, &t318);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t319 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                                    if (!t319) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 8, gm_src2j6ljeja7ilp, 423, 41);
                                    gm_b16_8 t320 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l70) + 8));
                                    gm_b16_8 t321 = GM_LD(gm_b16_8, ((uint8_t *)&t320));
                                    gm_b16_8 t322 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t314, t315, t321);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l38 = t322;
                                }
                                void * t323 = gm_l0;
                                gm_t88rcucg1b7uk t324 = gm_l2;
                                gm_b16_8 t325 = gm_l38;
                                gm_b16_8 t326 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t323, t324, t325);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l39 = t326;
                                {
                                    void * t327 = gm_l0;
                                    gm_b16_8 t328 = gm_l37;
                                    gm_b16_8 t329 = gm_l39;
                                    gm_t6j6smj7qoctu t330;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t330 + 0), t328);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t330 + 16), t329);
                                    gm_b0_1 t331 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t327, &t330);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                void * t332 = gm_l0;
                                gm_b16_8 t333;
                                GM_ST(void *, (uint8_t *)&t333, (void *)(gm_s02uuedsru1qu));
                                GM_ST(uintptr_t, (uint8_t *)&t333 + 8, (uintptr_t)(8));
                                gm_b0_1 t334 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t332, t333);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t335 = gm_l0;
                                gm_t88rcucg1b7uk t336 = gm_l2;
                                gm_b16_8 t337 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t335, t336);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l40 = t337;
                                {
                                    void * t338 = gm_l0;
                                    gm_b16_8 t339 = gm_l37;
                                    gm_b16_8 t340 = gm_l40;
                                    gm_t6j6smj7qoctu t341;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t341 + 0), t339);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t341 + 16), t340);
                                    gm_b0_1 t342 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t338, &t341);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                void * t343 = gm_l0;
                                gm_b16_8 t344;
                                GM_ST(void *, (uint8_t *)&t344, (void *)(gm_s2cuflhqkkuaq));
                                GM_ST(uintptr_t, (uint8_t *)&t344 + 8, (uintptr_t)(1));
                                gm_b0_1 t345 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t343, t344);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                {
                                    gm_b16_8 t346 = gm_l37;
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t346);
                                    goto gm_fdone;
                                }
                            }
gm_lb31:;
                            {
                                uint8_t t347 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                                uint8_t t348 = (uint8_t)(t347 == ((uint8_t)6ULL));
                                if (!t348) {
                                    goto gm_lb32;
                                }
                                {
                                    void * t349 = gm_l0;
                                    gm_t88rcucg1b7uk t350 = gm_l6;
                                    gm_tuglhjjmj2g58 t351 = gm_l16;
                                    gm_tbci5ospk5nnn t352;
                                    gm_f_ii_min_lku7pdi((void *)&t352, (void *)NULL, cx, NULL, t351);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b16_8 t353 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t349, t350, &t352);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l41 = t353;
                                }
                                {
                                    void * t354 = gm_l0;
                                    gm_t88rcucg1b7uk t355 = gm_l6;
                                    gm_tuglhjjmj2g58 t356 = gm_l16;
                                    gm_tbci5ospk5nnn t357;
                                    gm_f_ii_max_gtp5h90((void *)&t357, (void *)NULL, cx, NULL, t356);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b16_8 t358 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t354, t355, &t357);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l42 = t358;
                                }
                                {
                                    void * t359 = gm_l0;
                                    gm_t88rcucg1b7uk t360 = gm_l2;
                                    gm_b16_8 t361 = gm_l31;
                                    gm_b16_8 t362 = gm_l17;
                                    gm_b16_8 t363 = gm_l33;
                                    gm_b16_8 t364 = gm_l34;
                                    gm_b16_8 t365 = gm_l41;
                                    gm_b16_8 t366 = gm_l33;
                                    gm_b16_8 t367 = gm_l35;
                                    gm_b16_8 t368 = gm_l42;
                                    gm_b16_8 t369 = gm_l17;
                                    gm_b16_8 t370 = gm_l33;
                                    gm_tk6rvhh54g86p t371;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t371 + 0), t361);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t371 + 16), t362);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t371 + 32), t363);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t371 + 48), t364);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t371 + 64), t365);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t371 + 80), t366);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t371 + 96), t367);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t371 + 112), t368);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t371 + 128), t369);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t371 + 144), t370);
                                    gm_f_format_fh9to9u((void *)&gm_l71, (void *)NULL, cx, NULL, &t371);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t372 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                                    if (!t372) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 8, gm_src2j6ljeja7ilp, 435, 38);
                                    gm_b16_8 t373 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l71) + 8));
                                    gm_b16_8 t374 = GM_LD(gm_b16_8, ((uint8_t *)&t373));
                                    gm_b16_8 t375 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t359, t360, t374);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t375);
                                    goto gm_fdone;
                                }
                            }
gm_lb32:;
                            {
                                void * t376 = gm_l0;
                                void * t377 = GM_LD(void *, ((uint8_t *)t376));
                                uint8_t t378 = GM_LD(uint8_t, (((uint8_t *)t377) + 1136));
                                if (t378) {
                                    void * t379 = gm_l0;
                                    gm_b16_8 t380 = gm_l36;
                                    gm_b16_8 t381 = gm_l8;
                                    gm_t6j6smj7qoctu t382;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t382 + 0), t380);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t382 + 16), t381);
                                    gm_b0_1 t383 = gm_f_linef_jl8o1sr((void *)NULL, cx, NULL, t379, &t382);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                {
                                    void * t384 = gm_l0;
                                    gm_t88rcucg1b7uk t385 = gm_l2;
                                    gm_b16_8 t386 = gm_l17;
                                    gm_b16_8 t387 = gm_l33;
                                    gm_t6j6smj7qoctu t388;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t388 + 0), t386);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t388 + 16), t387);
                                    gm_f_format_3vtl3te((void *)&gm_l72, (void *)NULL, cx, NULL, &t388);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t389 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                                    if (!t389) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 8, gm_src2j6ljeja7ilp, 441, 38);
                                    gm_b16_8 t390 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l72) + 8));
                                    gm_b16_8 t391 = GM_LD(gm_b16_8, ((uint8_t *)&t390));
                                    gm_b16_8 t392 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t384, t385, t391);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t392);
                                    goto gm_fdone;
                                }
                            }
gm_lb33:;
                            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 419, 13);
                        }
gm_lb30:;
                    }
                }
gm_lb17:;
            }
            {
                {
                    uint8_t t393 = GM_LD(uint8_t, (((uint8_t *)&gm_l57) + 0));
                    uint8_t t394 = (uint8_t)(t393 == ((uint8_t)1ULL));
                    if (!t394) {
                        goto gm_lb34;
                    }
                    void * t395 = gm_l0;
                    gm_t88rcucg1b7uk t396 = gm_l6;
                    gm_b16_8 t397 = gm_l10;
                    gm_b16_8 t398 = gm_f_float_from_r6b8fu0((void *)NULL, cx, NULL, t395, t396, t397);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l43 = t398;
                    void * t399 = gm_l0;
                    gm_t88rcucg1b7uk t400 = gm_l2;
                    gm_b16_8 t401 = gm_l43;
                    gm_b16_8 t402 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t399, t400, t401);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t402);
                    goto gm_fdone;
                }
gm_lb34:;
                {
                    goto gm_lb16;
                }
gm_lb35:;
                gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 377, 5);
            }
        }
gm_lb16:;
    }
    {
        uint8_t t403 = gm_l9;
        if (t403) {
            {
                {
                    {
                        void * t404 = gm_l0;
                        void * t405 = GM_LD(void *, ((uint8_t *)t404));
                        void * t406 = (void *)(((uint8_t *)t405) + 8);
                        gm_t88rcucg1b7uk t407 = gm_l6;
                        gm_f_bits_layout_23rect9((void *)&gm_l73, (void *)NULL, cx, NULL, t406, t407);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t408 = (uint8_t)((((uint8_t *)&gm_l73))[24] != 0);
                    if (!t408) {
                        goto gm_lb37;
                    }
                    {
                        memmove(((uint8_t *)&gm_l44), ((uint8_t *)&gm_l73), 24);
                    }
                    {
                        void * t409 = gm_l0;
                        gm_t88rcucg1b7uk t410 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l44));
                        gm_f_int_info_2tcdf1n((void *)&gm_l74, (void *)NULL, cx, NULL, t409, t410);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t411 = (uint8_t)((((uint8_t *)&gm_l74))[16] != 0);
                    if (!t411) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 454, 19);
                    gm_tuglhjjmj2g58 t412 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&gm_l74));
                    gm_l45 = t412;
                    {
                        gm_tuglhjjmj2g58 t413 = gm_l45;
                        gm_tuglhjjmj2g58 t414 = gm_l45;
                        gm_b16_8 t415 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t414));
                        gm_b16_8 t416 = gm_l10;
                        gm_t6j6smj7qoctu t417;
                        GM_ST(gm_b16_8, ((uint8_t *)&t417 + 0), t415);
                        GM_ST(gm_b16_8, ((uint8_t *)&t417 + 16), t416);
                        gm_f_format_3vtl3te((void *)&gm_l75, (void *)NULL, cx, NULL, &t417);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t418 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                        if (!t418) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 8, gm_src2j6ljeja7ilp, 455, 32);
                        gm_b16_8 t419 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l75) + 8));
                        gm_b16_8 t420 = GM_LD(gm_b16_8, ((uint8_t *)&t419));
                        gm_b16_8 t421 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t413, t420);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l46 = t421;
                    }
                    void * t422 = gm_l0;
                    gm_t88rcucg1b7uk t423 = gm_l6;
                    gm_b16_8 t424 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t422, t423);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l47 = t424;
                    {
                        void * t425 = gm_l0;
                        gm_tuglhjjmj2g58 t426 = gm_l45;
                        gm_b16_8 t427 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t426));
                        gm_b16_8 t428 = gm_l47;
                        gm_b16_8 t429 = gm_l46;
                        gm_tfeab279c16rr t430;
                        GM_ST(gm_b16_8, ((uint8_t *)&t430 + 0), t427);
                        GM_ST(gm_b16_8, ((uint8_t *)&t430 + 16), t428);
                        GM_ST(gm_b16_8, ((uint8_t *)&t430 + 32), t429);
                        gm_b0_1 t431 = gm_f_linef_21h063v((void *)NULL, cx, NULL, t425, &t430);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_b16_8 t432 = gm_l47;
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t432);
                        goto gm_fdone;
                    }
                }
gm_lb37:;
            }
gm_lb36:;
        }
        gm_tl874t2atnlh3 t433 = gm_l1;
        void * t434 = gm_l0;
        void * t435 = GM_LD(void *, ((uint8_t *)t434));
        void * t436 = (void *)(((uint8_t *)t435) + 8);
        gm_t88rcucg1b7uk t437 = gm_l6;
        gm_b16_8 t438 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t436, t437);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t439;
        GM_ST(gm_b16_8, ((uint8_t *)&t439 + 0), t438);
        gm_f_format_15kpcel((void *)&gm_l76, (void *)NULL, cx, NULL, t439);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t440 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
        if (!t440) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 8, gm_src2j6ljeja7ilp, 461, 31);
        gm_b16_8 t441 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l76) + 8));
        gm_b16_8 t442 = GM_LD(gm_b16_8, ((uint8_t *)&t441));
        gm_t08olq97rscn3 t443;
        gm_f_unsupported_531q51r((void *)&t443, (void *)NULL, cx, NULL, t433, t442);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t443);
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
__attribute__((aligned(4))) void gm_f_from_tag_2m7nfln(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_from_tag_2m7nfln F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_t88rcucg1b7uk gm_l3; /* at */
    gm_b16_8 gm_l4; /* v0 */
    gm_t88rcucg1b7uk gm_l5; /* u */
    gm_b16_8 gm_l6; /* variants */
    gm_b16_8 gm_l7; /* res0 */
    gm_b16_8 gm_l8; /* res */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    gm_b16_8 gm_l13; /* lit */
    gm_b16_8 gm_l14; /* x */
    gm_b16_8 gm_l15; /* s */
    GM_STACK_CHECK(&F, 896u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t53at80pfdus3 gm_l16;
    uint8_t gm_l17;
    {
        {
            void * t14 = gm_l0;
            void * t15 = GM_LD(void *, ((uint8_t *)t14));
            void * t16 = (void *)(((uint8_t *)t15) + 8);
            gm_t88rcucg1b7uk t17 = gm_l2;
            gm_f_kind_ghvav22((void *)&gm_l16, t16, GM_CONV(gm_csguankko4lmhd, t17));
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)13ULL));
            if (!t19) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l16) + 8));
            gm_l5 = t20;
            {
                {
                    void * t21 = gm_l0;
                    void * t22 = GM_LD(void *, ((uint8_t *)t21));
                    void * t23 = (void *)(((uint8_t *)t22) + 8);
                    gm_t88rcucg1b7uk t24 = gm_l5;
                    void * t25 = gm_f_adt_of_gej99vn(t23, GM_CONV(gm_csguankko4lmhd, t24));
                    uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t25) + 24) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)1ULL));
                    if (!t27) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t28 = GM_LD(gm_b16_8, (((((uint8_t *)t25) + 24) + 8) + 8));
                    gm_l6 = t28;
                    void * t29 = gm_l0;
                    gm_t88rcucg1b7uk t30 = gm_l2;
                    gm_b16_8 t31 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t29, t30);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l7 = t31;
                    void * t32 = gm_l0;
                    gm_t88rcucg1b7uk t33 = gm_l2;
                    gm_b16_8 t34 = gm_l7;
                    gm_b16_8 t35 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t32, t33, t34);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l8 = t35;
                    gm_l10 = ((uint64_t)0ULL);
                    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    gm_l9 = t36;
                    {
                        for (;;) {
                            uint64_t t37 = gm_l10;
                            uint64_t t38 = gm_l9;
                            uint8_t t39 = (t37 < t38);
                            if (!t39) {
                                goto gm_lb5;
                            }
                            {
                                {
                                    {
                                        uint64_t t40 = gm_l10;
                                        gm_l11 = t40;
                                        {
                                            void * t41 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                            uint64_t t43 = gm_l11;
                                            memmove(((uint8_t *)&F.l12), (((uint8_t *)t41) + (uintptr_t)t43 * 72), 72);
                                        }
                                        {
                                            {
                                                uint8_t t44 = (uint8_t)(((((uint8_t *)&F.l12) + 16))[4] != 0);
                                                if (t44) {
                                                    goto gm_lb11;
                                                }
                                                gm_l17 = ((uint8_t)0);
                                                goto gm_lb10;
                                            }
gm_lb11:;
                                            {
                                                gm_l17 = ((uint8_t)1);
                                                goto gm_lb10;
                                            }
gm_lb12:;
                                            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 185, 12);
                                        }
gm_lb10:;
                                        uint8_t t45 = gm_l17;
                                        if (t45) {
                                            goto gm_lb7;
                                        }
                                        {
                                            void * t46 = gm_l0;
                                            gm_t88rcucg1b7uk t47 = gm_l3;
                                            gm_b16_8 t48 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t46, t47, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l12) + 24)));
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l13 = t48;
                                        }
                                        {
                                            void * t49 = gm_l0;
                                            gm_b16_8 t50 = gm_l4;
                                            gm_b16_8 t51 = gm_l13;
                                            gm_t6j6smj7qoctu t52;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t52 + 0), t50);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t52 + 16), t51);
                                            gm_b0_1 t53 = gm_f_linef_156hint((void *)NULL, cx, NULL, t49, &t52);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        {
                                            void * t54 = gm_l0;
                                            gm_t88rcucg1b7uk t55 = gm_l5;
                                            uint64_t t56 = gm_l11;
                                            gm_th2lqbo77pvsc t57;
                                            memset(&t57, 0, sizeof t57);
                                            gm_b16_8 t58 = gm_f_make_variant_srnk8ha((void *)NULL, cx, NULL, t54, t55, t56, &t57);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l14 = t58;
                                        }
                                        void * t59 = gm_l0;
                                        gm_t88rcucg1b7uk t60 = gm_l2;
                                        gm_b16_8 t61 = gm_l14;
                                        gm_b16_8 t62 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t59, t60, t61);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l15 = t62;
                                        {
                                            void * t63 = gm_l0;
                                            gm_b16_8 t64 = gm_l8;
                                            gm_b16_8 t65 = gm_l15;
                                            gm_t6j6smj7qoctu t66;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t64);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t66 + 16), t65);
                                            gm_b0_1 t67 = gm_f_linef_bq6igl9((void *)NULL, cx, NULL, t63, &t66);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        void * t68 = gm_l0;
                                        gm_b16_8 t69;
                                        GM_ST(void *, (uint8_t *)&t69, (void *)(gm_s2cuflhqkkuaq));
                                        GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(1));
                                        gm_b0_1 t70 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t68, t69);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 183, 5);
                                }
gm_lb8:;
                            }
gm_lb7:;
                            uint64_t t71 = gm_l10;
                            uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                            uint64_t t74 = r72;
                            gm_l10 = t74;
                        }
                    }
gm_lb5:;
                    gm_b16_8 t75 = gm_l8;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t75);
                    goto gm_fdone;
                }
gm_lb4:;
                {
                    gm_tl874t2atnlh3 t76 = gm_l1;
                    gm_b16_8 t77;
                    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s66fk044n1n0k));
                    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(8));
                    gm_t08olq97rscn3 t78;
                    gm_f_unsupported_531q51r((void *)&t78, (void *)NULL, cx, NULL, t76, t77);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t78);
                    goto gm_fdone;
                }
            }
gm_lb3:;
        }
gm_lb2:;
        {
            gm_tl874t2atnlh3 t79 = gm_l1;
            gm_b16_8 t80;
            GM_ST(void *, (uint8_t *)&t80, (void *)(gm_s66fk044n1n0k));
            GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(8));
            gm_t08olq97rscn3 t81;
            gm_f_unsupported_531q51r((void *)&t81, (void *)NULL, cx, NULL, t79, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t81);
            goto gm_fdone;
        }
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
__attribute__((aligned(4))) void gm_f_pow_s0lbtt4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5) {
    struct gm_fr_pow_s0lbtt4 F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_t9qkh0rsooc8d gm_l3; /* fl */
    gm_t88rcucg1b7uk gm_l4; /* at */
    gm_b16_8 gm_l5; /* vals */
    gm_b16_8 gm_l6; /* v0 */
    gm_b16_8 gm_l7; /* loc */
    gm_tuglhjjmj2g58 gm_l8; /* ii */
    gm_b16_8 gm_l9; /* c */
    gm_b16_8 gm_l10; /* r */
    gm_b16_8 gm_l11; /* o */
    gm_b16_8 gm_l12; /* f */
    gm_b16_8 gm_l13; /* min */
    gm_b16_8 gm_l14; /* max */
    gm_b16_8 gm_l15; /* b */
    gm_b16_8 gm_l16; /* sat */
    GM_STACK_CHECK(&F, 1953u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tqcc6dmlne11b gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_b16_8 gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_t5o0tpv5jka2l gm_l23;
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l5));
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t5, gm_src2j6ljeja7ilp, 239, 11);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t4) + (uintptr_t)((uint64_t)0ULL) * 16));
    gm_l6 = t6;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    gm_tl874t2atnlh3 t9 = gm_l1;
    gm_b16_8 t10 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t10;
    {
        void * t11 = gm_l0;
        gm_t88rcucg1b7uk t12 = gm_l4;
        gm_f_int_info_2tcdf1n((void *)&gm_l17, (void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
    if (t13) {
        gm_tuglhjjmj2g58 t14 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&gm_l17));
        gm_l8 = t14;
    } else {
        gm_tl874t2atnlh3 t15 = gm_l1;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s9gk1b0ve64mt));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(9));
        gm_t08olq97rscn3 t17;
        gm_f_unsupported_531q51r((void *)&t17, (void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t17);
        goto gm_fdone;
    }
    gm_tuglhjjmj2g58 t18 = gm_l8;
    gm_b16_8 t19 = gm_f_ii_cty_i2b4e03(GM_CONV(gm_cs2lqoac0a5ng4, t18));
    gm_l9 = t19;
    {
        void * t20 = gm_l0;
        uint32_t t21 = gm_f_fresh_5kokjmg(t20);
        gm_t88rcucg1b7uk t22;
        GM_ST(uint32_t, ((uint8_t *)&t22 + 0), t21);
        gm_f_format_jagvq3q((void *)&gm_l18, (void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src2j6ljeja7ilp, 243, 10);
    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
    gm_l10 = t24;
    {
        void * t25 = gm_l0;
        uint32_t t26 = gm_f_fresh_5kokjmg(t25);
        gm_t88rcucg1b7uk t27;
        GM_ST(uint32_t, ((uint8_t *)&t27 + 0), t26);
        gm_f_format_v2sfrt0((void *)&gm_l19, (void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
    if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_src2j6ljeja7ilp, 244, 10);
    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
    gm_l11 = t29;
    uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)&gm_l8));
    if (t30) {
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sitlbs33jqqal));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(1));
        gm_l20 = t31;
    } else {
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s0m9d22aen6f8));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(1));
        gm_l20 = t32;
    }
    {
        gm_b16_8 t33 = gm_l20;
        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
        uint64_t r35; bool o36 = __builtin_mul_overflow(t34, ((uint64_t)8ULL), &r35);
        if (o36) gm_trap("integer overflow in `*`", gm_src2j6ljeja7ilp, 245, 71);
        uint64_t t37 = r35;
        gm_t3877pk7lv748 t38;
        GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t33);
        GM_ST(uint64_t, ((uint8_t *)&t38 + 16), t37);
        gm_f_format_gf4f6rt((void *)&gm_l21, (void *)NULL, cx, NULL, &t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
    if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_src2j6ljeja7ilp, 245, 10);
    gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
    gm_l12 = t40;
    {
        void * t41 = gm_l0;
        gm_b16_8 t42 = gm_l11;
        gm_b16_8 t43 = gm_l9;
        gm_b16_8 t44 = gm_l10;
        gm_b16_8 t45 = gm_l12;
        gm_b16_8 t46 = gm_l6;
        void * t47 = GM_LD(void *, ((uint8_t *)&gm_l5));
        uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t48)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t48, gm_src2j6ljeja7ilp, 246, 85);
        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t47) + (uintptr_t)((uint64_t)1ULL) * 16));
        gm_b16_8 t50 = gm_l11;
        gm_th2d9c7aju8hp t51;
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 0), t42);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 16), t43);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 32), t44);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 48), t45);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 64), t46);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 80), t49);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 96), t50);
        gm_b0_1 t52 = gm_f_linef_ahjvru7((void *)NULL, cx, NULL, t41, &t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_tuglhjjmj2g58 t53 = gm_l8;
    uint8_t t54 = gm_f_ii_exact_g6a19dp(GM_CONV(gm_cs2lqoac0a5ng4, t53));
    uint8_t t55 = (uint8_t)!t54;
    if (t55) {
        {
            void * t56 = gm_l0;
            gm_t88rcucg1b7uk t57 = gm_l4;
            gm_tuglhjjmj2g58 t58 = gm_l8;
            gm_tbci5ospk5nnn t59;
            gm_f_ii_min_lku7pdi((void *)&t59, (void *)NULL, cx, NULL, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t60 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t56, t57, &t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t60;
        }
        {
            void * t61 = gm_l0;
            gm_t88rcucg1b7uk t62 = gm_l4;
            gm_tuglhjjmj2g58 t63 = gm_l8;
            gm_tbci5ospk5nnn t64;
            gm_f_ii_max_gtp5h90((void *)&t64, (void *)NULL, cx, NULL, t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t65 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t61, t62, &t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t65;
        }
        {
            void * t66 = gm_l0;
            gm_b16_8 t67 = gm_l11;
            gm_b16_8 t68 = gm_l11;
            gm_b16_8 t69 = gm_l10;
            gm_b16_8 t70 = gm_l13;
            gm_b16_8 t71 = gm_l10;
            gm_b16_8 t72 = gm_l14;
            gm_tif3elqk01bdl t73;
            GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t67);
            GM_ST(gm_b16_8, ((uint8_t *)&t73 + 16), t68);
            GM_ST(gm_b16_8, ((uint8_t *)&t73 + 32), t69);
            GM_ST(gm_b16_8, ((uint8_t *)&t73 + 48), t70);
            GM_ST(gm_b16_8, ((uint8_t *)&t73 + 64), t71);
            GM_ST(gm_b16_8, ((uint8_t *)&t73 + 80), t72);
            gm_b0_1 t74 = gm_f_linef_calnga5((void *)NULL, cx, NULL, t66, &t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
    }
    {
        {
            gm_t9qkh0rsooc8d t75 = gm_l3;
            uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&t75) + 0));
            uint8_t t77 = (uint8_t)((t76 == ((uint8_t)0ULL)));
            if (!t77) {
                goto gm_lb2;
            }
            void * t78 = gm_l0;
            void * t79 = GM_LD(void *, ((uint8_t *)t78));
            uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)t79) + 1136));
            if (!t80) {
                goto gm_lb2;
            }
            void * t81 = gm_l0;
            gm_t88rcucg1b7uk t82 = gm_l4;
            gm_b16_8 t83 = gm_l6;
            gm_t9qkh0rsooc8d t84;
            GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)0ULL));
            gm_b16_8 t85 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t81, t82, t83);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t85;
            void * t86 = gm_l0;
            gm_b16_8 t87 = gm_l11;
            gm_b16_8 t88 = gm_l15;
            uint32_t t89 = GM_LD(uint32_t, (((uint8_t *)&gm_l8) + 4));
            uint8_t t90 = GM_LD(uint8_t, ((uint8_t *)&gm_l8));
            void * t91 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t92)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t92, gm_src2j6ljeja7ilp, 255, 116);
            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)t91) + (uintptr_t)((uint64_t)1ULL) * 16));
            gm_b16_8 t94 = gm_l7;
            gm_t8fvq98mjik04 t95;
            GM_ST(uint8_t, (uint8_t *)&t95 + 37, 0);
            GM_ST(uint16_t, (uint8_t *)&t95 + 38, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t95 + 0), t87);
            GM_ST(gm_b16_8, ((uint8_t *)&t95 + 16), t88);
            GM_ST(uint32_t, ((uint8_t *)&t95 + 32), t89);
            GM_ST(uint8_t, ((uint8_t *)&t95 + 36), t90);
            GM_ST(gm_b16_8, ((uint8_t *)&t95 + 40), t93);
            GM_ST(gm_b16_8, ((uint8_t *)&t95 + 56), t94);
            gm_b0_1 t96 = gm_f_linef_3utfuce((void *)NULL, cx, NULL, t86, &t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_b16_8 t97 = gm_l6;
        void * t98 = GM_LD(void *, ((uint8_t *)&gm_l5));
        uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t99)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t99, gm_src2j6ljeja7ilp, 257, 56);
        gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)t98) + (uintptr_t)((uint64_t)1ULL) * 16));
        gm_t6j6smj7qoctu t101;
        GM_ST(gm_b16_8, ((uint8_t *)&t101 + 0), t97);
        GM_ST(gm_b16_8, ((uint8_t *)&t101 + 16), t100);
        gm_f_format_qofaffd((void *)&gm_l22, (void *)NULL, cx, NULL, &t101);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t102 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
    if (!t102) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_src2j6ljeja7ilp, 257, 12);
    gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
    gm_l16 = t103;
    {
        void * t104 = gm_l0;
        gm_t9qkh0rsooc8d t105 = gm_l3;
        gm_tuglhjjmj2g58 t106 = gm_l8;
        gm_t88rcucg1b7uk t107 = gm_l4;
        gm_t88rcucg1b7uk t108 = gm_l2;
        gm_b16_8 t109 = gm_l10;
        gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
        gm_b16_8 t111 = gm_l11;
        gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
        gm_b16_8 t113 = gm_l16;
        gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
        gm_b16_8 t115 = gm_l7;
        gm_b16_8 t116;
        GM_ST(void *, (uint8_t *)&t116, (void *)(gm_svpofgjtij7vj));
        GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(3));
        gm_f_finish_int_cr0p55i((void *)&gm_l23, (void *)NULL, cx, NULL, t104, t105, t106, t107, t108, t110, t112, t114, t115, t116);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
    if (t117) {
    } else {
        gm_t08olq97rscn3 t118 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&gm_l23) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t118);
        goto gm_fdone;
    }
    {
        gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t119);
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
inline __attribute__((aligned(4))) void gm_f_rotate_tpbpt1c(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t8588ma2cr9d7 a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4) {
    struct gm_fr_rotate_tpbpt1c F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t8588ma2cr9d7 gm_l2; /* op */
    gm_t88rcucg1b7uk gm_l3; /* at */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l5; /* v0 */
    gm_tuglhjjmj2g58 gm_l6; /* ii */
    gm_b16_8 gm_l7; /* x */
    gm_b16_8 gm_l8; /* k0 */
    gm_b16_8 gm_l9; /* k */
    gm_b16_8 gm_l10; /* c */
    gm_b16_8 gm_l11; /* r */
    gm_b16_8 gm_l12; /* n */
    GM_STACK_CHECK(&F, 967u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tqcc6dmlne11b gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tabb710d9mq2e gm_l17;
    void * t2 = GM_LD(void *, ((uint8_t *)&gm_l4));
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t3)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t3, gm_src2j6ljeja7ilp, 198, 11);
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t2) + (uintptr_t)((uint64_t)0ULL) * 16));
    gm_l5 = t4;
    {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l3;
        gm_f_int_info_2tcdf1n((void *)&gm_l13, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l13))[16] != 0);
    if (!t7) gm_trap("unwrap of null", gm_src2j6ljeja7ilp, 199, 11);
    gm_tuglhjjmj2g58 t8 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&gm_l13));
    gm_l6 = t8;
    {
        gm_tuglhjjmj2g58 t9 = gm_l6;
        gm_b16_8 t10 = gm_f_ii_ucty_b40h5ot(GM_CONV(gm_cs2lqoac0a5ng4, t9));
        gm_b16_8 t11 = gm_l5;
        gm_t6j6smj7qoctu t12;
        GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t10);
        GM_ST(gm_b16_8, ((uint8_t *)&t12 + 16), t11);
        gm_f_format_21vnq4a((void *)&gm_l14, (void *)NULL, cx, NULL, &t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
    if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src2j6ljeja7ilp, 200, 10);
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
    gm_l7 = t14;
    {
        void * t15 = GM_LD(void *, ((uint8_t *)&gm_l4));
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t16, gm_src2j6ljeja7ilp, 201, 38);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t15) + (uintptr_t)((uint64_t)1ULL) * 16));
        gm_tnpifv20irnph t18;
        GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t17);
        gm_f_format_h95c7u1((void *)&gm_l15, (void *)NULL, cx, NULL, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src2j6ljeja7ilp, 201, 11);
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
    gm_l8 = t20;
    gm_t8588ma2cr9d7 t21 = gm_l2;
    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
    uint8_t t23 = (uint8_t)((t22 == ((uint8_t)32ULL)));
    gm_t8588ma2cr9d7 t24;
    GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t24 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)32ULL));
    if (t23) {
        gm_b16_8 t25 = gm_l8;
        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
        gm_l9 = t26;
    } else {
        {
            uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
            gm_b16_8 t28 = gm_l8;
            uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
            gm_t5g5vgung6h6s t30;
            GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 28, 0);
            GM_ST(uint32_t, ((uint8_t *)&t30 + 0), t27);
            GM_ST(gm_b16_8, ((uint8_t *)&t30 + 8), t28);
            GM_ST(uint32_t, ((uint8_t *)&t30 + 24), t29);
            gm_f_format_aghnr00((void *)&gm_l16, (void *)NULL, cx, NULL, &t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_src2j6ljeja7ilp, 203, 46);
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_l9 = t33;
    }
    void * t34 = gm_l0;
    void * t35 = GM_LD(void *, ((uint8_t *)t34));
    void * t36 = (void *)(((uint8_t *)t35) + 8);
    gm_t88rcucg1b7uk t37 = gm_l1;
    gm_b16_8 t38 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t36, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l10 = t38;
    {
        void * t39 = gm_l0;
        gm_t88rcucg1b7uk t40 = gm_l1;
        gm_b16_8 t41 = gm_l10;
        gm_b16_8 t42 = gm_l7;
        gm_b16_8 t43 = gm_l9;
        uint32_t t44 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        gm_tj4tbafenpsjm t45;
        GM_ST(uint32_t, (uint8_t *)&t45 + 52, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t45 + 0), t41);
        GM_ST(gm_b16_8, ((uint8_t *)&t45 + 16), t42);
        GM_ST(gm_b16_8, ((uint8_t *)&t45 + 32), t43);
        GM_ST(uint32_t, ((uint8_t *)&t45 + 48), t44);
        gm_f_format_898275p((void *)&gm_l17, (void *)NULL, cx, NULL, &t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
        if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_src2j6ljeja7ilp, 205, 20);
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
        gm_b16_8 t49 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t39, t40, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t49;
    }
    gm_tuglhjjmj2g58 t50 = gm_l6;
    gm_b16_8 t51 = gm_l11;
    gm_b16_8 t52 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t50, t51);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l12 = t52;
    {
        void * t53 = gm_l0;
        gm_t88rcucg1b7uk t54 = gm_l1;
        gm_b16_8 t55 = gm_l12;
        gm_b16_8 t56 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t53, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5o0tpv5jka2l *)gm_out) + 8), t56);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_sys_values_qlu7stq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t9qkh0rsooc8d a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_sys_values_qlu7stq F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t9qkh0rsooc8d gm_l2; /* sop */
    gm_b16_8 gm_l3; /* tys */
    gm_b16_8 gm_l4; /* vals */
    uint32_t gm_l5; /* k */
    uint32_t gm_l6; /* k */
    gm_b16_8 gm_l7; /* addr */
    gm_b16_8 gm_l8; /* s */
    gm_b16_8 gm_l9; /* some */
    gm_b16_8 gm_l10; /* none */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* i */
    gm_b16_8 gm_l15; /* v */
    gm_b16_8 gm_l16; /* addr */
    gm_b16_8 gm_l17; /* call */
    GM_STACK_CHECK(&F, 1942u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tabb710d9mq2e gm_l22;
    uint8_t gm_l23;
    gm_t53at80pfdus3 gm_l24;
    gm_tabb710d9mq2e gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_t9qkh0rsooc8d t10 = gm_l2;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&t10) + 0));
    uint8_t t12 = (uint8_t)((t11 == ((uint8_t)44ULL)));
    gm_t9qkh0rsooc8d t13;
    GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)44ULL));
    if (t12) {
        void * t14 = gm_l0;
        uint32_t t15 = gm_f_fresh_5kokjmg(t14);
        gm_l5 = t15;
        void * t16 = gm_l0;
        uint32_t t17 = gm_l5;
        uint32_t t18 = gm_l5;
        uint32_t t19 = gm_l5;
        gm_tl874t2atnlh3 t20;
        GM_ST(uint32_t, ((uint8_t *)&t20 + 0), t17);
        GM_ST(uint32_t, ((uint8_t *)&t20 + 4), t18);
        GM_ST(uint32_t, ((uint8_t *)&t20 + 8), t19);
        gm_b0_1 t21 = gm_f_linef_5q2h82g((void *)NULL, cx, NULL, t16, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            void * t22 = gm_l0;
            gm_t88rcucg1b7uk t23 = gm_l1;
            uint32_t t24 = gm_l5;
            gm_t88rcucg1b7uk t25;
            GM_ST(uint32_t, ((uint8_t *)&t25 + 0), t24);
            gm_f_format_ucng4n7((void *)&gm_l18, (void *)NULL, cx, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src2j6ljeja7ilp, 128, 33);
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
            gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
            uint32_t t29 = gm_l5;
            gm_t88rcucg1b7uk t30;
            GM_ST(uint32_t, ((uint8_t *)&t30 + 0), t29);
            gm_f_format_3itad0c((void *)&gm_l19, (void *)NULL, cx, NULL, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
            if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_src2j6ljeja7ilp, 128, 59);
            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
            gm_b16_8 t34 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t22, t23, t28, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t34;
            goto gm_fret;
        }
    }
    gm_t9qkh0rsooc8d t35 = gm_l2;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
    uint8_t t37 = (uint8_t)((t36 == ((uint8_t)45ULL)));
    gm_t9qkh0rsooc8d t38;
    GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)45ULL));
    if (t37) {
        void * t39 = gm_l0;
        uint32_t t40 = gm_f_fresh_5kokjmg(t39);
        gm_l6 = t40;
        void * t41 = gm_l0;
        void * t42 = GM_LD(void *, ((uint8_t *)&gm_l3));
        uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t43)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t43, gm_src2j6ljeja7ilp, 132, 28);
        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t42) + (uintptr_t)((uint64_t)0ULL) * 4));
        void * t45 = GM_LD(void *, ((uint8_t *)&gm_l4));
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t46, gm_src2j6ljeja7ilp, 132, 36);
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t45) + (uintptr_t)((uint64_t)0ULL) * 16));
        gm_b16_8 t48 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t41, t44, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t48;
        void * t49 = gm_l0;
        uint32_t t50 = gm_l6;
        uint32_t t51 = gm_l6;
        gm_tcc1rq4n1avb2 t52;
        GM_ST(uint32_t, ((uint8_t *)&t52 + 0), t50);
        GM_ST(uint32_t, ((uint8_t *)&t52 + 4), t51);
        gm_b0_1 t53 = gm_f_linef_vgjqckp((void *)NULL, cx, NULL, t49, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            void * t54 = gm_l0;
            uint32_t t55 = gm_l6;
            gm_b16_8 t56 = gm_l7;
            gm_b16_8 t57 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t58 = gm_l0;
            gm_b16_8 t59 = gm_l7;
            gm_b16_8 t60 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t58, t59);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t61 = gm_l6;
            uint32_t t62 = gm_l6;
            gm_t133qiba7tvhc t63;
            GM_ST(uint32_t, (uint8_t *)&t63 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t63 + 0), t55);
            GM_ST(gm_b16_8, ((uint8_t *)&t63 + 8), t57);
            GM_ST(gm_b16_8, ((uint8_t *)&t63 + 24), t60);
            GM_ST(uint32_t, ((uint8_t *)&t63 + 40), t61);
            GM_ST(uint32_t, ((uint8_t *)&t63 + 44), t62);
            gm_b0_1 t64 = gm_f_linef_7pb5lg6((void *)NULL, cx, NULL, t54, &t63);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t65 = gm_l0;
            void * t66 = gm_l0;
            void * t67 = GM_LD(void *, ((uint8_t *)t66));
            void * t68 = (void *)(((uint8_t *)t67) + 8);
            gm_t88rcucg1b7uk t69 = gm_f_string_ty_kk94re3((void *)NULL, cx, NULL, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t70 = gm_l6;
            gm_t88rcucg1b7uk t71;
            GM_ST(uint32_t, ((uint8_t *)&t71 + 0), t70);
            gm_f_format_tvr4di5((void *)&gm_l20, (void *)NULL, cx, NULL, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_src2j6ljeja7ilp, 135, 49);
            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
            uint32_t t75 = gm_l6;
            gm_t88rcucg1b7uk t76;
            GM_ST(uint32_t, ((uint8_t *)&t76 + 0), t75);
            gm_f_format_he769rg((void *)&gm_l21, (void *)NULL, cx, NULL, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
            if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_src2j6ljeja7ilp, 135, 73);
            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
            gm_b16_8 t80 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t65, t69, t74, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t80;
        }
        void * t81 = gm_l0;
        gm_t88rcucg1b7uk t82 = gm_l1;
        gm_b16_8 t83 = gm_l8;
        gm_b16_8 t84 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t81, t82, t83);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t84;
        void * t85 = gm_l0;
        gm_t88rcucg1b7uk t86 = gm_l1;
        gm_b16_8 t87 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t85, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t87;
        {
            void * t88 = gm_l0;
            gm_t88rcucg1b7uk t89 = gm_l1;
            uint32_t t90 = gm_l6;
            gm_b16_8 t91 = gm_l9;
            gm_b16_8 t92 = gm_l10;
            gm_thognv9odov03 t93;
            GM_ST(uint32_t, (uint8_t *)&t93 + 4, 0);
            GM_ST(uint32_t, ((uint8_t *)&t93 + 0), t90);
            GM_ST(gm_b16_8, ((uint8_t *)&t93 + 8), t91);
            GM_ST(gm_b16_8, ((uint8_t *)&t93 + 24), t92);
            gm_f_format_cjdn0sp((void *)&gm_l22, (void *)NULL, cx, NULL, &t93);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
            if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_src2j6ljeja7ilp, 138, 26);
            gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
            gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
            gm_b16_8 t97 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t88, t89, t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t97;
            goto gm_fret;
        }
    }
    {
        gm_trqfvmjtt4sij t98;
        gm_f_list_epvducg((void *)&t98, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t98;
    }
    gm_l13 = ((uint64_t)0ULL);
    uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
    gm_l12 = t99;
    {
        for (;;) {
            uint64_t t100 = gm_l13;
            uint64_t t101 = gm_l12;
            uint8_t t102 = (t100 < t101);
            if (!t102) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t103 = gm_l13;
                        gm_l14 = t103;
                        void * t104 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t106 = gm_l14;
                        gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)t104) + (uintptr_t)t106 * 16));
                        gm_l15 = t107;
                        void * t108 = gm_l0;
                        void * t109 = GM_LD(void *, ((uint8_t *)t108));
                        void * t110 = (void *)(((uint8_t *)t109) + 8);
                        void * t111 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint64_t t113 = gm_l14;
                        if ((uintptr_t)t113 >= (uintptr_t)(t112)) gm_trap_index((uintptr_t)t113, t112, gm_src2j6ljeja7ilp, 143, 25);
                        gm_t88rcucg1b7uk t114 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t111) + (uintptr_t)t113 * 4));
                        gm_f_kind_ghvav22((void *)&gm_l24, t110, GM_CONV(gm_csguankko4lmhd, t114));
                        {
                            {
                                uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                uint8_t t116 = (uint8_t)(t115 == ((uint8_t)10ULL));
                                if (!t116) {
                                    goto gm_lb7;
                                }
                                gm_l23 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l23 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 143, 12);
                        }
gm_lb6:;
                        uint8_t t117 = gm_l23;
                        if (t117) {
                            void * t118 = gm_l0;
                            void * t119 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t121 = gm_l14;
                            if ((uintptr_t)t121 >= (uintptr_t)(t120)) gm_trap_index((uintptr_t)t121, t120, gm_src2j6ljeja7ilp, 144, 32);
                            gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t119) + (uintptr_t)t121 * 4));
                            gm_b16_8 t123 = gm_l15;
                            gm_b16_8 t124 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t118, t122, t123);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l16 = t124;
                            {
                                void * t125 = (void *)((uint8_t *)&F.l11);
                                gm_b16_8 t126 = gm_l16;
                                gm_b16_8 t127 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t126);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t128 = gm_l0;
                                gm_b16_8 t129 = gm_l16;
                                gm_b16_8 t130 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t128, t129);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t6j6smj7qoctu t131;
                                GM_ST(gm_b16_8, ((uint8_t *)&t131 + 0), t127);
                                GM_ST(gm_b16_8, ((uint8_t *)&t131 + 16), t130);
                                gm_f_format_00b54tv((void *)&gm_l25, (void *)NULL, cx, NULL, &t131);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 8, gm_src2j6ljeja7ilp, 145, 24);
                                gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
                                gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                                gm_tftb1odc739t5 t135 = gm_f_push_3ef746b(t125, t134);
                                gm_l26 = t135;
                            }
                            uint8_t t136 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                            if (!t136) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_src2j6ljeja7ilp, 145, 13);
                        } else {
                            void * t137 = (void *)((uint8_t *)&F.l11);
                            gm_b16_8 t138 = gm_l15;
                            gm_tftb1odc739t5 t139 = gm_f_push_3ef746b(t137, t138);
                            gm_l27 = t139;
                            uint8_t t140 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                            if (!t140) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_src2j6ljeja7ilp, 147, 13);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src2j6ljeja7ilp, 141, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t141 = gm_l13;
            uint64_t r142; bool o143 = __builtin_add_overflow(t141, ((uint64_t)1ULL), &r142);
            uint64_t t144 = r142;
            gm_l13 = t144;
        }
    }
gm_lb1:;
    {
        gm_t9qkh0rsooc8d t145 = gm_l2;
        gm_b16_8 t146 = gm_f_sys_name_pcdoujk(t145);
        gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
        gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&t147));
        gm_b16_8 t149;
        GM_ST(void *, (uint8_t *)&t149, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(2));
        gm_f_join_sfknljb((void *)&gm_l28, (void *)NULL, cx, NULL, t148, t149);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
        if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_src2j6ljeja7ilp, 150, 56);
        gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
        gm_t6j6smj7qoctu t152;
        GM_ST(gm_b16_8, ((uint8_t *)&t152 + 0), t146);
        GM_ST(gm_b16_8, ((uint8_t *)&t152 + 16), t151);
        gm_f_format_m7gv7a8((void *)&gm_l29, (void *)NULL, cx, NULL, &t152);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t153 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
    if (!t153) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_src2j6ljeja7ilp, 150, 13);
    gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
    gm_l17 = t154;
    void * t155 = gm_l0;
    void * t156 = GM_LD(void *, ((uint8_t *)t155));
    void * t157 = (void *)(((uint8_t *)t156) + 8);
    gm_t88rcucg1b7uk t158 = gm_l1;
    uint8_t t159 = gm_f_is_unit_17uennd(t157, GM_CONV(gm_csguankko4lmhd, t158));
    if (t159) {
        void * t160 = gm_l0;
        gm_b16_8 t161 = gm_l17;
        gm_tnpifv20irnph t162;
        GM_ST(gm_b16_8, ((uint8_t *)&t162 + 0), t161);
        gm_b0_1 t163 = gm_f_linef_v355qja((void *)NULL, cx, NULL, t160, t162);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t164 = gm_l0;
        gm_b16_8 t165 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t164);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t165;
        goto gm_fret;
    }
    void * t166 = gm_l0;
    gm_t88rcucg1b7uk t167 = gm_l1;
    gm_b16_8 t168 = gm_l17;
    gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
    gm_b16_8 t170 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t166, t167, t169);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t170;
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

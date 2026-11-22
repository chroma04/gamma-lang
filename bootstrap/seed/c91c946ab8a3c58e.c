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
typedef struct gm_csc7lc6jlkqkuq { uint64_t f0; uint64_t f1; } gm_csc7lc6jlkqkuq;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; } gm_thpdbp3q7rdrc;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_thpdbp3q7rdrc) == 16 && _Alignof(gm_thpdbp3q7rdrc) == 4 && offsetof(gm_thpdbp3q7rdrc, f0) == 0 && offsetof(gm_thpdbp3q7rdrc, f1) == 4 && offsetof(gm_thpdbp3q7rdrc, f2) == 8 && offsetof(gm_thpdbp3q7rdrc, f3) == 12, "layout of `(u32, u32, u32, u32)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint32_t f3; } gm_thrm96fhhdpt5;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t2792tsg5eg72;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { uint64_t f0; gm_tbgd14esf0jvi f1; } gm_tdlq2ugehok8g;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thrm96fhhdpt5) == 40 && _Alignof(gm_thrm96fhhdpt5) == 8 && offsetof(gm_thrm96fhhdpt5, f0) == 0 && offsetof(gm_thrm96fhhdpt5, f1) == 16 && offsetof(gm_thrm96fhhdpt5, f2) == 32 && offsetof(gm_thrm96fhhdpt5, f3) == 36, "layout of `(string, string, u32, u32)`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t2792tsg5eg72) == 64 && _Alignof(gm_t2792tsg5eg72) == 8 && offsetof(gm_t2792tsg5eg72, f0) == 0 && offsetof(gm_t2792tsg5eg72, f1) == 40 && offsetof(gm_t2792tsg5eg72, f2) == 48 && offsetof(gm_t2792tsg5eg72, f3) == 56, "layout of `Diagnostics`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_tdlq2ugehok8g) == 40 && _Alignof(gm_tdlq2ugehok8g) == 8 && offsetof(gm_tdlq2ugehok8g, f0) == 0 && offsetof(gm_tdlq2ugehok8g, f1) == 8, "layout of `(i: usize, x: Edit)`");
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { uint64_t f0; gm_tl7hpf5nlilqq f1; } gm_tm101l8077enc;
_Static_assert(sizeof(gm_tm101l8077enc) == 64 && _Alignof(gm_tm101l8077enc) == 8 && offsetof(gm_tm101l8077enc, f0) == 0 && offsetof(gm_tm101l8077enc, f1) == 8, "layout of `(i: usize, x: Fix)`");
struct gm_fr_add_fix_0gkcr4c {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* edits */
    gm_b0_1 ret;
};
struct gm_fr_add_fix_edits_em0b48a {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* l */
    gm_b0_1 ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_any_since_o1kpm84 {
    gm_frame h;
    gm_t4gut2c9fj1kt l5; /* d */
    uint8_t ret;
};
struct gm_fr_before_05k9hcj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_diagnostics_n3kkhf4 {
    gm_frame h;
    gm_t2792tsg5eg72 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dgpnm3t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s801pu6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_count_omtt6qd {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_error_s6hn871 {
    gm_frame h;
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_1eek7pr {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_format_32n178h {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l21cpr6 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_errors_if7ai0m {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_heading_jklompb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_ascii_t9jud42 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_json_after_line_c5i6134 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_json_before_line_2trhgro {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_json_location_e84g9mr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_json_location_split_5jggoqp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_json_string_n2ks51u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_lint_eo1tub5 {
    gm_frame h;
    gm_t4gut2c9fj1kt l3; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_jekodor {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_l10mna9 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_prbqo9j {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_rk274m7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_location_3clcfto {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mark_vedqs3l {
    gm_frame h;
    gm_t2lqu8jsj511d ret;
};
struct gm_fr_push_0o3323p {
    gm_frame h;
    gm_tl7hpf5nlilqq l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k96mobn {
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
struct gm_fr_push_o4ckdr1 {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_t4gut2c9fj1kt l4; /* old */
    gm_b0_1 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_untabbed_n2b9umq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_58vn30g {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_kfs9rll {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_urfcki5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_render_human_fmipkqr {
    gm_frame h;
    gm_trqfvmjtt4sij l14; /* shown */
    gm_b0_1 ret;
};
struct gm_fr_render_json_a7n3ru2 {
    gm_frame h;
    gm_tl7hpf5nlilqq l8; /* fx */
    gm_b0_1 ret;
};
struct gm_fr_render_pi7hvmf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_render_short_oijr4gl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_reset_dg0kphe {
    gm_frame h;
    gm_t4gut2c9fj1kt l4; /* d */
    gm_b0_1 ret;
};
struct gm_fr_severity_name_auihisp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_sort_s8jqb1h {
    gm_frame h;
    gm_t4gut2c9fj1kt l5; /* d */
    gm_b0_1 ret;
};
struct gm_fr_spaces_d2e4env {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* s */
    gm_b16_8 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_suppress_sdgq6qj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_2jbb2bh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsuppress_722t1fb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_warning_q0m5jjs {
    gm_frame h;
    gm_t4gut2c9fj1kt l2; /* d */
    gm_t4gut2c9fj1kt ret;
};
struct gm_fr_write_35ac6au {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_5ldgli3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_6qt0u2d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_bvpfm1k {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_capundt {
    gm_frame h;
    gm_thrm96fhhdpt5 l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ef6g7s0 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_gelas9q {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_he17klq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_nsck6rd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_oig1hhm {
    gm_frame h;
    gm_thrm96fhhdpt5 l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_oouhb7d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_s6arg96 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u0r3vc4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqo886974tou(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_any_since_o1kpm84(void * a0, gm_csc7lc6jlkqkuq a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_before_05k9hcj(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) uint64_t gm_f_char_count_76unqru(gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_char_offset_kr8000h(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dgpnm3t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s801pu6(void);
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_1eek7pr(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_format_32n178h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_l21cpr6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_heading_jklompb(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_ascii_t9jud42(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_json_after_line_c5i6134(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_json_before_line_2trhgro(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_json_location_e84g9mr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_json_location_split_5jggoqp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_json_string_n2ks51u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_line_text_lc2jnq4(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_lint_eo1tub5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_jekodor(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_l10mna9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_prbqo9j(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_rk274m7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_location_3clcfto(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2);
inline __attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_mark_vedqs3l(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0o3323p(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k96mobn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l0rgbjf(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1_c(void * a0, gm_t4gut2c9fj1kt a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_untabbed_n2b9umq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_58vn30g(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kfs9rll(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_urfcki5(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_human_fmipkqr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_json_a7n3ru2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_pi7hvmf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_short_oijr4gl(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_reset_dg0kphe(void * a0, gm_csc7lc6jlkqkuq a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_severity_name_auihisp(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_sort_s8jqb1h(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_spaces_d2e4env(void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_suppress_sdgq6qj(void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_2jbb2bh(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_unsuppress_722t1fb(void * a0);
inline __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2venbiq(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_35ac6au(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5ldgli3(void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6qt0u2d(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8d77kqn(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bvpfm1k(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_capundt(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e55tuqo(void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ef6g7s0(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fd01t33(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gelas9q(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_he17klq(void * a0, gm_thpdbp3q7rdrc a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lldn1qi(void * a0, gm_tcc1rq4n1avb2 a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_nsck6rd(void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oig1hhm(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oouhb7d(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s6arg96(void * a0, gm_tcc1rq4n1avb2 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_tgrsvka(void * a0, gm_thpdbp3q7rdrc a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u0r3vc4(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vpdd5m4(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline bool gm_lt467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_lt8l669blqfv(const uint8_t *a, const uint8_t *b);
extern uint8_t gm_c4h9gubvsld[16] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s0g8veu8021ch[3];
extern const uint8_t gm_s13b5om17s13f[8];
extern const uint8_t gm_s1g7hoprcf07c[66];
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s22g482qk7a40[3];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s4k2ktjht9l4a[13];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s5rqsv8c71bds[11];
extern const uint8_t gm_s5s4ac22q52tj[14];
extern const uint8_t gm_s6gt8b7hsisgu[3];
extern const uint8_t gm_s75s8qhd4m46e[6];
extern const uint8_t gm_s79s3h98matgr[6];
extern const uint8_t gm_s9caepulv0516[9];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_s9qu470anjl4i[8];
extern const uint8_t gm_sap9oatgj29c2[13];
extern const uint8_t gm_sb7kik80lfbge[11];
extern const uint8_t gm_scp1b2sk2nlmr[8];
extern const uint8_t gm_sf4pu10dkcjd3[8];
extern const uint8_t gm_sgcj0gjfdljat[11];
extern const uint8_t gm_sgiadc3jhhq91[23];
extern const uint8_t gm_sh75rh8dk51gf[12];
extern const uint8_t gm_shhr567jcclrl[10];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_siv0q8us9f9sh[13];
extern const uint8_t gm_sjbb259gf4uuc[8];
extern const uint8_t gm_skg16mqffvnq6[4];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_spchsf3taendr[3];
extern const uint8_t gm_splv67uudsbdi[23];
extern const uint8_t gm_sqtultomupp4c[22];
extern const uint8_t gm_squbucrq6qmns[5];
extern const uint8_t gm_sr3arqfjtko1m[16];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_su6414ei8c1ir[10];
extern const uint8_t gm_sue26fe85rntf[5];
extern const uint8_t gm_sun80ccb875r1[13];
extern const uint8_t gm_svj3dp4ur2bph[11];
extern const uint8_t gm_svkrg5dctlh9m[7];
extern const gm_tmpl gm_tpl7kgo5i3aumd2;
extern const gm_tmpl gm_tpl7ut0r685hcaa;
extern const gm_tmpl gm_tpl9jr09voqi3gh;
extern const gm_tmpl gm_tpl9sifsarv7t06;
extern const gm_tmpl gm_tpla6b7o385jo3o;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplkjc5mpc5i80h;
extern const gm_tmpl gm_tplmj3d4h2pi7q3;
extern const gm_tmpl gm_tplmueh9mdvaqub;
extern const gm_tmpl gm_tplp98fsht4k706;
extern const gm_tmpl gm_tplpfv6kueslks8;
extern const gm_tmpl gm_tplsgc0lnghs9b6;
extern const gm_tmpl gm_tpltur94fmrrcmk;
extern const gm_tmpl gm_tplv8uh6lopnmo9;
extern const gm_tmpl gm_tplvkvnlb2orh55;
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
inline bool gm_eqo886974tou(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
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
inline __attribute__((aligned(4))) uint8_t gm_f_before_05k9hcj(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_before_05k9hcj F;
    gm_tl874t2atnlh3 gm_l0; /* a */
    gm_tl874t2atnlh3 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 59u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = GM_CONV(gm_tl874t2atnlh3, a0);
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    uint32_t t2 = GM_LD(uint32_t, ((uint8_t *)&gm_l0));
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)&gm_l0) + 4));
    gm_tcc1rq4n1avb2 t4;
    GM_ST(uint32_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(uint32_t, ((uint8_t *)&t4 + 4), t3);
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_tcc1rq4n1avb2 t7;
    GM_ST(uint32_t, ((uint8_t *)&t7 + 0), t5);
    GM_ST(uint32_t, ((uint8_t *)&t7 + 4), t6);
    uint8_t t8 = (uint8_t)gm_lt8l669blqfv(((uint8_t *)&t4), ((uint8_t *)&t7));
    F.ret = t8;
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
inline __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_diagnostics_n3kkhf4 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_prbqo9j((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 60, 0);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 40), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 48), ((uint64_t)0ULL));
    GM_ST(uint32_t, ((uint8_t *)&(*(gm_t2792tsg5eg72 *)gm_out) + 56), ((uint32_t)0ULL));
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dgpnm3t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dgpnm3t F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s801pu6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s801pu6 F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_1eek7pr(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_1eek7pr F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_urfcki5(t12, t13, t14);
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
inline __attribute__((aligned(4))) void gm_f_format_32n178h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_32n178h F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l21cpr6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l21cpr6 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svkrg5dctlh9m));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl7kgo5i3aumd2), t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_ascii_t9jud42(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ascii_t9jud42 F;
    void * gm_l0; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 19u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 72));
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_list_jekodor(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_jekodor F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_s801pu6();
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
inline __attribute__((aligned(4))) void gm_f_list_l10mna9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_l10mna9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_dgpnm3t();
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
inline __attribute__((aligned(4))) void gm_f_list_rk274m7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_rk274m7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jbu9fo3();
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
inline __attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_mark_vedqs3l(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mark_vedqs3l F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 48));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0o3323p(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0o3323p F;
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
        gm_b16_8 t9 = gm_f_empty_slice_dgpnm3t();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_58vn30g(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 56), ((uint8_t *)a1), 56);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 56)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k96mobn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_k96mobn F;
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
        gm_b16_8 t9 = gm_f_empty_slice_s801pu6();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_urfcki5(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_58vn30g(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_58vn30g F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_urfcki5(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_urfcki5 F;
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
inline __attribute__((aligned(4))) void gm_f_suppress_sdgq6qj(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_suppress_sdgq6qj F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 56));
    uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
    if (o6) gm_trap("integer overflow in `+`", gm_srcgfglnv8p059r, 156, 5);
    uint32_t t7 = r5;
    GM_ST(uint32_t, (((uint8_t *)t2) + 56), t7);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_2jbb2bh(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_2jbb2bh F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 184)));
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
inline __attribute__((aligned(4))) void gm_f_unsuppress_722t1fb(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unsuppress_722t1fb F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 45u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, (((uint8_t *)t2) + 56));
    uint8_t t4 = (t3 > ((uint32_t)0ULL));
    if (t4) {
        void * t5 = gm_l0;
        void * t6 = gm_l0;
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t6) + 56));
        uint32_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint32_t)1ULL), &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 160, 28);
        uint32_t t10 = r8;
        GM_ST(uint32_t, (((uint8_t *)t5) + 56), t10);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_35ac6au(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_35ac6au F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_shhr567jcclrl));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(9));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl7ut0r685hcaa), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5ldgli3(void * a0, gm_t88rcucg1b7uk a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_5ldgli3 F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* args */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_siv0q8us9f9sh));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(12));
    gm_tftb1odc739t5 t5 = gm_f_write_e55tuqo(t2, t3, ((void *)&gm_tpla6b7o385jo3o), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6qt0u2d(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_6qt0u2d F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf4pu10dkcjd3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpl9jr09voqi3gh), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bvpfm1k(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_bvpfm1k F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s5s4ac22q52tj));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(13));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplp98fsht4k706), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_capundt(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_capundt F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_splv67uudsbdi));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_tftb1odc739t5 t4 = gm_f_write_vpdd5m4(t2, &(*(gm_thrm96fhhdpt5 *)((uint8_t *)a1)), ((void *)&gm_tplsgc0lnghs9b6), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ef6g7s0(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ef6g7s0 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s75s8qhd4m46e));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl9sifsarv7t06), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gelas9q(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_gelas9q F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svj3dp4ur2bph));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_tftb1odc739t5 t4 = gm_f_write_8d77kqn(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplv8uh6lopnmo9), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_he17klq(void * a0, gm_thpdbp3q7rdrc a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_he17klq F;
    void * gm_l0; /* l */
    gm_thpdbp3q7rdrc gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_thpdbp3q7rdrc t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s1g7hoprcf07c));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(65));
    gm_tftb1odc739t5 t5 = gm_f_write_tgrsvka(t2, t3, ((void *)&gm_tplmj3d4h2pi7q3), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_nsck6rd(void * a0, gm_t88rcucg1b7uk a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_nsck6rd F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* args */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sqtultomupp4c));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(21));
    gm_tftb1odc739t5 t5 = gm_f_write_e55tuqo(t2, t3, ((void *)&gm_tplmueh9mdvaqub), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oig1hhm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_oig1hhm F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr3arqfjtko1m));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_tftb1odc739t5 t4 = gm_f_write_vpdd5m4(t2, &(*(gm_thrm96fhhdpt5 *)((uint8_t *)a1)), ((void *)&gm_tplpfv6kueslks8), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_oouhb7d(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_oouhb7d F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s13b5om17s13f));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpltur94fmrrcmk), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s6arg96(void * a0, gm_tcc1rq4n1avb2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_s6arg96 F;
    void * gm_l0; /* l */
    gm_tcc1rq4n1avb2 gm_l1; /* args */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tcc1rq4n1avb2 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sgiadc3jhhq91));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(22));
    gm_tftb1odc739t5 t5 = gm_f_write_lldn1qi(t2, t3, ((void *)&gm_tplkjc5mpc5i80h), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u0r3vc4(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u0r3vc4 F;
    void * gm_l0; /* l */
    gm_t5hdnnftkup6m gm_l1; /* args */
    GM_STACK_CHECK(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb7kik80lfbge));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
    gm_tftb1odc739t5 t4 = gm_f_write_fd01t33(t2, &(*(gm_t5hdnnftkup6m *)((uint8_t *)a1)), ((void *)&gm_tplvkvnlb2orh55), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline bool gm_lt467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) < GM_LD(uint32_t, b);
}
inline bool gm_lt8l669blqfv(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return gm_lt467d5cm1d5(a + 0, b + 0);
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return gm_lt467d5cm1d5(a + 4, b + 4);
    return false;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3) {
    struct gm_fr_add_fix_0gkcr4c F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* message */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* text */
    GM_STACK_CHECK(&F, 298u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_jekodor((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l4);
        gm_tl874t2atnlh3 t4 = gm_l2;
        gm_b16_8 t5 = gm_l3;
        gm_tbgd14esf0jvi t6;
        GM_ST(uint32_t, (uint8_t *)&t6 + 12, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 0), t4);
        GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
        gm_tftb1odc739t5 t7 = gm_f_push_k96mobn(t3, &t6);
        gm_l5 = t7;
    }
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcgfglnv8p059r, 102, 5);
    {
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 120);
        gm_b16_8 t11 = gm_l1;
        gm_trqfvmjtt4sij t12 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l4));
        gm_tl7hpf5nlilqq t13;
        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t11);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t13 + 16), t12);
        gm_tftb1odc739t5 t14 = gm_f_push_0o3323p(t10, &t13);
        gm_l6 = t14;
    }
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcgfglnv8p059r, 103, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_add_fix_edits_em0b48a F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* message */
    gm_b16_8 gm_l2; /* edits */
    GM_STACK_CHECK(&F, 242u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l4;
    gm_tftb1odc739t5 gm_l5;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_jekodor((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t4 = gm_l2;
    gm_tftb1odc739t5 t5 = gm_f_extend_1eek7pr(t3, t4);
    gm_l4 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcgfglnv8p059r, 109, 5);
    {
        void * t7 = gm_l0;
        void * t8 = (void *)(((uint8_t *)t7) + 120);
        gm_b16_8 t9 = gm_l1;
        gm_trqfvmjtt4sij t10 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l3));
        gm_tl7hpf5nlilqq t11;
        GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t9);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t11 + 16), t10);
        gm_tftb1odc739t5 t12 = gm_f_push_0o3323p(t8, &t11);
        gm_l5 = t12;
    }
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcgfglnv8p059r, 110, 5);
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
extern __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_any_since_o1kpm84(void * a0, gm_csc7lc6jlkqkuq a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_since_o1kpm84 F;
    void * gm_l0; /* ds */
    gm_t2lqu8jsj511d gm_l1; /* m */
    gm_b16_8 gm_l2; /* prefix */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 228u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    gm_l2 = a2;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
    uint64_t t11 = (uintptr_t)t10;
    uint64_t t12 = (uintptr_t)t9;
    if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcgfglnv8p059r, 148, 14);
    uint64_t t13 = ((uintptr_t)t9) - (uintptr_t)t10;
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)t10 * 184)));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
    gm_l3 = t14;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t15 = gm_l4;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t20 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t18) + (uintptr_t)t20 * 184), 184);
                        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 8));
                        gm_b16_8 t22 = gm_l2;
                        uint8_t t23 = gm_f_starts_with_6vvt01e(t21, t22);
                        if (t23) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 148, 5);
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
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint8_t gm_f_before_05k9hcj(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
extern __attribute__((aligned(4))) void gm_f_diagnostics_n3kkhf4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0);
inline __attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_error_s6hn871 F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* message */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l1;
    gm_tl874t2atnlh3 t3 = gm_l0;
    gm_t9qkh0rsooc8d t4;
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)2ULL));
    gm_trqfvmjtt4sij t5;
    gm_f_list_rk274m7((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t6;
    gm_f_list_epvducg((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t7;
    gm_f_list_l10mna9((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_th2lqbo77pvsc t8;
    memset(&t8, 0, sizeof t8);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 36, 0);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 0), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 8), t2);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 24), t3);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 40), t5);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 80), t6);
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 120), t7);
    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&(*(gm_t4gut2c9fj1kt *)gm_out) + 160), t8);
    goto gm_fdone;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) uint8_t gm_f_has_errors_if7ai0m(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_heading_jklompb(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_heading_jklompb F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK(&F, 236u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tabb710d9mq2e gm_l2;
    void * t2 = gm_l0;
    uint8_t t3 = (uint8_t)(((((uint8_t *)t2) + 160))[16] != 0);
    if (t3) {
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 160));
        gm_l1 = t4;
    } else {
        void * t5 = gm_l0;
        gm_t9qkh0rsooc8d t6 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)t5));
        gm_b16_8 t7 = gm_f_severity_name_auihisp(t6);
        F.ret = t7;
        goto gm_fret;
    }
    {
        void * t8 = gm_l0;
        gm_t9qkh0rsooc8d t9 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)t8));
        gm_b16_8 t10 = gm_f_severity_name_auihisp(t9);
        gm_b16_8 t11 = gm_l1;
        gm_t6j6smj7qoctu t12;
        GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t10);
        GM_ST(gm_b16_8, ((uint8_t *)&t12 + 16), t11);
        gm_f_format_l21cpr6((void *)&gm_l2, (void *)NULL, cx, NULL, &t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcgfglnv8p059r, 88, 12);
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
    F.ret = t15;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_json_after_line_c5i6134(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_json_after_line_c5i6134 F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* out */
    gm_b16_8 gm_l3; /* message */
    gm_tcc1rq4n1avb2 gm_l4; /* lc */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l5;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t4 = gm_f_file_o15fqku(t2, t3);
    uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_tcc1rq4n1avb2 t6 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t4, t5));
    gm_l4 = t6;
    void * t7 = gm_l2;
    uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
    gm_t88rcucg1b7uk t9;
    GM_ST(uint32_t, ((uint8_t *)&t9 + 0), t8);
    gm_tftb1odc739t5 t10 = gm_f_write_nsck6rd(t7, t9);
    gm_l5 = t10;
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcgfglnv8p059r, 377, 5);
    gm_b16_8 t12 = gm_l3;
    void * t13 = gm_l2;
    gm_b0_1 t14 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t12, t13);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_json_before_line_2trhgro(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2) {
    struct gm_fr_json_before_line_2trhgro F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* out */
    void * gm_l3; /* f */
    gm_tcc1rq4n1avb2 gm_l4; /* lc */
    gm_tcc1rq4n1avb2 gm_l5; /* end */
    GM_STACK_CHECK(&F, 189u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l6;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t4 = gm_f_file_o15fqku(t2, t3);
    gm_l3 = t4;
    void * t5 = gm_l3;
    uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_tcc1rq4n1avb2 t7 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t5, t6));
    gm_l4 = t7;
    void * t8 = gm_l3;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    gm_tcc1rq4n1avb2 t10 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t8, t9));
    gm_l5 = t10;
    void * t11 = gm_l2;
    uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
    uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
    uint32_t t14 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
    uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    gm_thpdbp3q7rdrc t16;
    GM_ST(uint32_t, ((uint8_t *)&t16 + 0), t12);
    GM_ST(uint32_t, ((uint8_t *)&t16 + 4), t13);
    GM_ST(uint32_t, ((uint8_t *)&t16 + 8), t14);
    GM_ST(uint32_t, ((uint8_t *)&t16 + 12), t15);
    gm_tftb1odc739t5 t17 = gm_f_write_he17klq(t11, t16);
    gm_l6 = t17;
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcgfglnv8p059r, 370, 5);
    void * t19 = gm_l3;
    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)t19));
    void * t21 = gm_l2;
    gm_b0_1 t22 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t20, t21);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_json_location_e84g9mr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2) {
    struct gm_fr_json_location_e84g9mr F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* out */
    gm_tcc1rq4n1avb2 gm_l3; /* lc */
    GM_STACK_CHECK(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l4;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    void * t4 = gm_l2;
    gm_b0_1 t5 = gm_f_json_before_line_2trhgro((void *)NULL, cx, NULL, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t6 = gm_l0;
    uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t8 = gm_f_file_o15fqku(t6, t7);
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_tcc1rq4n1avb2 t10 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t8, t9));
    gm_l3 = t10;
    void * t11 = gm_l2;
    uint32_t t12 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
    uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_tcc1rq4n1avb2 t14;
    GM_ST(uint32_t, ((uint8_t *)&t14 + 0), t12);
    GM_ST(uint32_t, ((uint8_t *)&t14 + 4), t13);
    gm_tftb1odc739t5 t15 = gm_f_write_s6arg96(t11, t14);
    gm_l4 = t15;
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcgfglnv8p059r, 355, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_json_location_split_5jggoqp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_json_location_split_5jggoqp F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* out */
    gm_b16_8 gm_l3; /* message */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l4;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    void * t4 = gm_l2;
    gm_b0_1 t5 = gm_f_json_before_line_2trhgro((void *)NULL, cx, NULL, t2, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t6 = gm_l0;
    gm_tl874t2atnlh3 t7 = gm_l1;
    void * t8 = gm_l2;
    gm_b16_8 t9 = gm_l3;
    gm_b0_1 t10 = gm_f_json_after_line_c5i6134((void *)NULL, cx, NULL, t6, t7, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t11 = gm_l2;
    uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_t88rcucg1b7uk t13;
    GM_ST(uint32_t, ((uint8_t *)&t13 + 0), t12);
    gm_tftb1odc739t5 t14 = gm_f_write_5ldgli3(t11, t13);
    gm_l4 = t14;
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcgfglnv8p059r, 362, 5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_json_string_n2ks51u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_json_string_n2ks51u F;
    gm_b16_8 gm_l0; /* s */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 492u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    void * t17 = gm_l1;
    gm_tftb1odc739t5 t18 = gm_f_push_t83rg2m(t17, ((uint8_t)34ULL));
    gm_l5 = t18;
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t19) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcgfglnv8p059r, 386, 5);
    gm_b16_8 t20 = gm_l0;
    gm_l2 = t20;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t21 = gm_l3;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t23 = (t21 < t22);
            if (!t23) {
                goto gm_lb1;
            }
            {
                void * t24 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t26 = gm_l3;
                {
                    {
                        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)t24) + (uintptr_t)t26 * 1));
                        gm_l4 = t27;
                        {
                            {
                                uint8_t t28 = gm_l4;
                                uint8_t t29 = ((t28 == ((uint8_t)34ULL)));
                                if (!t29) {
                                    goto gm_lb7;
                                }
                                void * t30 = gm_l1;
                                gm_b16_8 t31;
                                GM_ST(void *, (uint8_t *)&t31, (void *)(gm_smee1janbl2t9));
                                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t32 = gm_f_extend_0cs85gs(t30, t31);
                                gm_l6 = t32;
                                uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                                if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcgfglnv8p059r, 389, 20);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t34 = gm_l4;
                                uint8_t t35 = ((t34 == ((uint8_t)92ULL)));
                                if (!t35) {
                                    goto gm_lb8;
                                }
                                void * t36 = gm_l1;
                                gm_b16_8 t37;
                                GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s3p8gug7bvl1p));
                                GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t38 = gm_f_extend_0cs85gs(t36, t37);
                                gm_l7 = t38;
                                uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                                if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srcgfglnv8p059r, 390, 21);
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t40 = gm_l4;
                                uint8_t t41 = ((t40 == ((uint8_t)10ULL)));
                                if (!t41) {
                                    goto gm_lb9;
                                }
                                void * t42 = gm_l1;
                                gm_b16_8 t43;
                                GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s6gt8b7hsisgu));
                                GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t44 = gm_f_extend_0cs85gs(t42, t43);
                                gm_l8 = t44;
                                uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                                if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcgfglnv8p059r, 391, 21);
                                goto gm_lb6;
                            }
gm_lb9:;
                            {
                                uint8_t t46 = gm_l4;
                                uint8_t t47 = ((t46 == ((uint8_t)13ULL)));
                                if (!t47) {
                                    goto gm_lb10;
                                }
                                void * t48 = gm_l1;
                                gm_b16_8 t49;
                                GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s9cb6j5ok7bdk));
                                GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t50 = gm_f_extend_0cs85gs(t48, t49);
                                gm_l9 = t50;
                                uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcgfglnv8p059r, 392, 21);
                                goto gm_lb6;
                            }
gm_lb10:;
                            {
                                uint8_t t52 = gm_l4;
                                uint8_t t53 = ((t52 == ((uint8_t)9ULL)));
                                if (!t53) {
                                    goto gm_lb11;
                                }
                                void * t54 = gm_l1;
                                gm_b16_8 t55;
                                GM_ST(void *, (uint8_t *)&t55, (void *)(gm_s22g482qk7a40));
                                GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t56 = gm_f_extend_0cs85gs(t54, t55);
                                gm_l10 = t56;
                                uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                                if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcgfglnv8p059r, 393, 21);
                                goto gm_lb6;
                            }
gm_lb11:;
                            {
                                uint8_t t58 = gm_l4;
                                uint8_t t59 = ((t58 == ((uint8_t)8ULL)));
                                if (!t59) {
                                    goto gm_lb12;
                                }
                                void * t60 = gm_l1;
                                gm_b16_8 t61;
                                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_s0g8veu8021ch));
                                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t62 = gm_f_extend_0cs85gs(t60, t61);
                                gm_l11 = t62;
                                uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcgfglnv8p059r, 394, 21);
                                goto gm_lb6;
                            }
gm_lb12:;
                            {
                                uint8_t t64 = gm_l4;
                                uint8_t t65 = ((t64 == ((uint8_t)12ULL)));
                                if (!t65) {
                                    goto gm_lb13;
                                }
                                void * t66 = gm_l1;
                                gm_b16_8 t67;
                                GM_ST(void *, (uint8_t *)&t67, (void *)(gm_spchsf3taendr));
                                GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t68 = gm_f_extend_0cs85gs(t66, t67);
                                gm_l12 = t68;
                                uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcgfglnv8p059r, 395, 21);
                                goto gm_lb6;
                            }
gm_lb13:;
                            {
                                uint8_t t70 = gm_l4;
                                uint8_t t71 = (t70 >= ((uint8_t)0ULL));
                                if (!t71) {
                                    goto gm_lb14;
                                }
                                uint8_t t72 = (t70 <= ((uint8_t)31ULL));
                                if (!t72) {
                                    goto gm_lb14;
                                }
                                void * t73 = gm_l1;
                                gm_b16_8 t74;
                                GM_ST(void *, (uint8_t *)&t74, (void *)(gm_squbucrq6qmns));
                                GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(4));
                                gm_tftb1odc739t5 t75 = gm_f_extend_0cs85gs(t73, t74);
                                gm_l13 = t75;
                                uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcgfglnv8p059r, 397, 17);
                                void * t77 = gm_l1;
                                void * t78 = GM_LD(void *, ((uint8_t *)gm_c4h9gubvsld));
                                uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)gm_c4h9gubvsld) + 8));
                                uint8_t t80 = gm_l4;
                                unsigned __int128 n81 = (unsigned __int128)((uint64_t)4ULL);
                                if (n81 >= 8) gm_trap_shift((uint64_t)n81, "u8", gm_srcgfglnv8p059r, 398, 36);
                                uint8_t t82 = (uint8_t)(t80 >> (int)n81);
                                uint64_t t83 = (uint64_t)t82;
                                if ((uintptr_t)t83 >= (uintptr_t)(t79)) gm_trap_index((uintptr_t)t83, t79, gm_srcgfglnv8p059r, 398, 26);
                                uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)t78) + (uintptr_t)t83 * 1));
                                gm_tftb1odc739t5 t85 = gm_f_push_t83rg2m(t77, t84);
                                gm_l14 = t85;
                                uint8_t t86 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                                if (!t86) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcgfglnv8p059r, 398, 17);
                                void * t87 = gm_l1;
                                void * t88 = GM_LD(void *, ((uint8_t *)gm_c4h9gubvsld));
                                uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)gm_c4h9gubvsld) + 8));
                                uint8_t t90 = gm_l4;
                                uint8_t t91 = (uint8_t)(t90 & ((uint8_t)15ULL));
                                uint64_t t92 = (uint64_t)t91;
                                if ((uintptr_t)t92 >= (uintptr_t)(t89)) gm_trap_index((uintptr_t)t92, t89, gm_srcgfglnv8p059r, 399, 26);
                                uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)t88) + (uintptr_t)t92 * 1));
                                gm_tftb1odc739t5 t94 = gm_f_push_t83rg2m(t87, t93);
                                gm_l15 = t94;
                                uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcgfglnv8p059r, 399, 17);
                                goto gm_lb6;
                            }
gm_lb14:;
                            {
                                void * t96 = gm_l1;
                                uint8_t t97 = gm_l4;
                                gm_tftb1odc739t5 t98 = gm_f_push_t83rg2m(t96, t97);
                                gm_l16 = t98;
                                uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                if (!t99) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcgfglnv8p059r, 401, 18);
                                goto gm_lb6;
                            }
gm_lb15:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 388, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 387, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t100 = gm_l3;
            uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
            uint64_t t103 = r101;
            gm_l3 = t103;
        }
    }
gm_lb1:;
    void * t104 = gm_l1;
    gm_tftb1odc739t5 t105 = gm_f_push_t83rg2m(t104, ((uint8_t)34ULL));
    gm_l17 = t105;
    uint8_t t106 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t106) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcgfglnv8p059r, 404, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_lint_eo1tub5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_location_3clcfto(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2) {
    struct gm_fr_location_3clcfto F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* out */
    void * gm_l3; /* f */
    gm_tcc1rq4n1avb2 gm_l4; /* lc */
    GM_STACK_CHECK(&F, 178u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l5;
    gm_tl874t2atnlh3 t2 = gm_l1;
    gm_tl874t2atnlh3 t3 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t4 = (gm_eqff85ac10o3(((uint8_t *)&t2), ((uint8_t *)&t3)));
    if (t4) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t5 = gm_l0;
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t7 = gm_f_file_o15fqku(t5, t6);
    gm_l3 = t7;
    void * t8 = gm_l3;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    gm_tcc1rq4n1avb2 t10 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t8, t9));
    gm_l4 = t10;
    void * t11 = gm_l2;
    void * t12 = gm_l3;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)t12));
    uint32_t t14 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
    uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
    gm_t5hdnnftkup6m t16;
    GM_ST(gm_b16_8, ((uint8_t *)&t16 + 0), t13);
    GM_ST(uint32_t, ((uint8_t *)&t16 + 16), t14);
    GM_ST(uint32_t, ((uint8_t *)&t16 + 20), t15);
    gm_tftb1odc739t5 t17 = gm_f_write_u0r3vc4(t11, &t16);
    gm_l5 = t17;
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcgfglnv8p059r, 293, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) gm_csc7lc6jlkqkuq gm_f_mark_vedqs3l(void * a0);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o4ckdr1 F;
    void * gm_l0; /* ds */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 342u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_tftb1odc739t5 gm_l5;
    gm_t9qkh0rsooc8d t11 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l1));
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
    uint8_t t13 = (uint8_t)((t12 == ((uint8_t)2ULL)));
    gm_t9qkh0rsooc8d t14;
    GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)2ULL));
    if (t13) {
        void * t15 = gm_l0;
        void * t16 = gm_l0;
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 48));
        uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
        if (o19) gm_trap("integer overflow in `+`", gm_srcgfglnv8p059r, 116, 31);
        uint64_t t20 = r18;
        GM_ST(uint64_t, (((uint8_t *)t15) + 48), t20);
    }
    void * t21 = gm_l0;
    uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)t21) + 56));
    uint8_t t23 = (t22 > ((uint32_t)0ULL));
    if (t23) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            gm_t9qkh0rsooc8d t24 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l1));
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&t24) + 0));
            uint8_t t26 = (uint8_t)((t25 == ((uint8_t)2ULL)));
            if (!t26) {
                goto gm_lb2;
            }
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 8));
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_scp1b2sk2nlmr));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(7));
            gm_t9qkh0rsooc8d t29;
            GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)2ULL));
            uint8_t t30 = gm_f_contains_gkki4kq(t27, t28);
            if (!t30) {
                goto gm_lb2;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 24));
    gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
    uint8_t t33 = (!gm_eqff85ac10o3(((uint8_t *)&t31), ((uint8_t *)&t32)));
    if (t33) {
        void * t34 = gm_l0;
        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)t34));
        gm_l2 = t35;
        gm_l3 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t36 = gm_l3;
                uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint8_t t38 = (t36 < t37);
                if (!t38) {
                    goto gm_lb3;
                }
                {
                    void * t39 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t41 = gm_l3;
                    {
                        {
                            memmove(((uint8_t *)&F.l4), (((uint8_t *)t39) + (uintptr_t)t41 * 184), 184);
                            {
                                {
                                    gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                                    gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l1) + 24));
                                    uint8_t t44 = (gm_eqff85ac10o3(((uint8_t *)&t42), ((uint8_t *)&t43)));
                                    if (!t44) {
                                        goto gm_lb9;
                                    }
                                    gm_t9qkh0rsooc8d t45 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l4));
                                    gm_t9qkh0rsooc8d t46 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l1));
                                    uint8_t t47 = (gm_eqo886974tou(((uint8_t *)&t45), ((uint8_t *)&t46)));
                                    if (!t47) {
                                        goto gm_lb9;
                                    }
                                    gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                                    gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 8));
                                    uint8_t t50 = (gm_eq0iqiubfm7e(((uint8_t *)&t48), ((uint8_t *)&t49)));
                                    if (!t50) {
                                        goto gm_lb9;
                                    }
                                    F.ret = GM_ZERO(gm_b0_1);
                                    goto gm_fret;
                                }
gm_lb9:;
                            }
gm_lb8:;
                            goto gm_lb6;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 121, 9);
                    }
gm_lb6:;
                }
gm_lb5:;
                uint64_t t51 = gm_l3;
                uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                uint64_t t54 = r52;
                gm_l3 = t54;
            }
        }
gm_lb3:;
    }
    gm_t9qkh0rsooc8d t55 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l1));
    uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t55) + 0));
    uint8_t t57 = (uint8_t)((t56 == ((uint8_t)2ULL)));
    gm_t9qkh0rsooc8d t58;
    GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)2ULL));
    if (t57) {
        void * t59 = gm_l0;
        void * t60 = gm_l0;
        uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)t60) + 40));
        uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
        if (o63) gm_trap("integer overflow in `+`", gm_srcgfglnv8p059r, 125, 31);
        uint64_t t64 = r62;
        GM_ST(uint64_t, (((uint8_t *)t59) + 40), t64);
    }
    void * t65 = gm_l0;
    void * t66 = (void *)((uint8_t *)t65);
    gm_tftb1odc739t5 t67 = gm_f_push_l0rgbjf(t66, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    gm_l5 = t67;
    uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srcgfglnv8p059r, 126, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_push_o4ckdr1_c(void * a0, gm_t4gut2c9fj1kt a1) { gm_f_push_o4ckdr1(a0, &a1); }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_untabbed_n2b9umq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_push_untabbed_n2b9umq F;
    void * gm_l0; /* out */
    gm_b16_8 gm_l1; /* text */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l5;
    gm_tftb1odc739t5 gm_l6;
    gm_b16_8 t7 = gm_l1;
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
                        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t11) + (uintptr_t)t13 * 1));
                        gm_l4 = t14;
                        void * t15 = gm_l0;
                        uint8_t t16 = gm_l4;
                        uint8_t t17 = ((t16 == ((uint8_t)9ULL)));
                        if (t17) {
                            gm_l5 = ((uint8_t)32ULL);
                        } else {
                            uint8_t t18 = gm_l4;
                            gm_l5 = t18;
                        }
                        uint8_t t19 = gm_l5;
                        gm_tftb1odc739t5 t20 = gm_f_push_t83rg2m(t15, t19);
                        gm_l6 = t20;
                        uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                        if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srcgfglnv8p059r, 271, 21);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 271, 5);
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
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_render_human_fmipkqr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_render_human_fmipkqr F;
    void * gm_l0; /* sm */
    void * gm_l1; /* d */
    void * gm_l2; /* out */
    void * gm_l3; /* f */
    gm_tcc1rq4n1avb2 gm_l4; /* lc */
    uint32_t gm_l5; /* line */
    uint32_t gm_l6; /* col */
    gm_b16_8 gm_l7; /* number */
    uint64_t gm_l8; /* gutter */
    gm_b16_8 gm_l9; /* pad */
    gm_b16_8 gm_l10; /* text */
    uint32_t gm_l11; /* len */
    gm_tcc1rq4n1avb2 gm_l12; /* end */
    uint32_t gm_l13; /* width */
    uint32_t gm_l15; /* caret */
    uint32_t gm_l16; /* start */
    uint32_t gm_l17; /* stop */
    gm_b16_8 gm_l18; /* body */
    uint32_t gm_l19; /* <tmp> */
    uint32_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    gm_tbgd14esf0jvi gm_l23; /* l */
    void * gm_l24; /* f */
    gm_tcc1rq4n1avb2 gm_l25; /* lc */
    gm_b16_8 gm_l26; /* <tmp> */
    uint64_t gm_l27; /* <tmp> */
    gm_b16_8 gm_l28; /* n */
    GM_STACK_CHECK(&F, 2000u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    uint32_t gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tftb1odc739t5 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    {
        void * t17 = gm_l2;
        void * t18 = gm_l1;
        gm_b16_8 t19 = gm_f_heading_jklompb((void *)NULL, cx, NULL, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t20 = gm_l1;
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t20) + 8));
        gm_t6j6smj7qoctu t22;
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t19);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 16), t21);
        gm_tftb1odc739t5 t23 = gm_f_write_oouhb7d(t17, &t22);
        gm_l29 = t23;
        uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
        if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcgfglnv8p059r, 217, 5);
        void * t25 = gm_l1;
        gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t25) + 24));
        gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
        uint8_t t28 = (!gm_eqff85ac10o3(((uint8_t *)&t26), ((uint8_t *)&t27)));
        if (t28) {
            void * t29 = gm_l0;
            void * t30 = gm_l1;
            uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)t30) + 24));
            void * t32 = gm_f_file_o15fqku(t29, t31);
            gm_l3 = t32;
            void * t33 = gm_l3;
            void * t34 = gm_l1;
            uint32_t t35 = GM_LD(uint32_t, ((((uint8_t *)t34) + 24) + 4));
            gm_tcc1rq4n1avb2 t36 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t33, t35));
            gm_l4 = t36;
            uint32_t t37 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
            gm_l5 = t37;
            uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 4));
            gm_l6 = t38;
            {
                uint32_t t39 = gm_l5;
                gm_t88rcucg1b7uk t40;
                GM_ST(uint32_t, ((uint8_t *)&t40 + 0), t39);
                gm_f_format_32n178h((void *)&gm_l30, (void *)NULL, cx, NULL, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
            if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srcgfglnv8p059r, 223, 19);
            gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
            gm_l7 = t42;
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint64_t t44 = (t43 > ((uint64_t)2ULL) ? t43 : ((uint64_t)2ULL));
            gm_l8 = t44;
            uint64_t t45 = gm_l8;
            gm_b16_8 t46 = gm_f_spaces_d2e4env((void *)NULL, cx, NULL, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t46;
            {
                void * t47 = gm_l2;
                gm_b16_8 t48 = gm_l9;
                void * t49 = gm_l3;
                gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)t49));
                uint32_t t51 = gm_l5;
                uint32_t t52 = gm_l6;
                gm_thrm96fhhdpt5 t53;
                GM_ST(gm_b16_8, ((uint8_t *)&t53 + 0), t48);
                GM_ST(gm_b16_8, ((uint8_t *)&t53 + 16), t50);
                GM_ST(uint32_t, ((uint8_t *)&t53 + 32), t51);
                GM_ST(uint32_t, ((uint8_t *)&t53 + 36), t52);
                gm_tftb1odc739t5 t54 = gm_f_write_oig1hhm(t47, &t53);
                gm_l31 = t54;
            }
            uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
            if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srcgfglnv8p059r, 226, 9);
            void * t56 = gm_l3;
            uint32_t t57 = gm_l5;
            gm_b16_8 t58 = gm_f_line_text_lc2jnq4(t56, t57);
            gm_l10 = t58;
            void * t59 = gm_l3;
            uint8_t t60 = gm_f_is_ascii_t9jud42(t59);
            if (t60) {
                uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint32_t t62 = (uint32_t)t61;
                gm_l11 = t62;
            } else {
                gm_b16_8 t63 = gm_l10;
                uint64_t t64 = gm_f_char_count_76unqru(t63);
                uint32_t t65 = (uint32_t)t64;
                gm_l11 = t65;
            }
            void * t66 = gm_l3;
            void * t67 = gm_l1;
            uint32_t t68 = GM_LD(uint32_t, ((((uint8_t *)t67) + 24) + 8));
            gm_tcc1rq4n1avb2 t69 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t66, t68));
            gm_l12 = t69;
            uint32_t t70 = GM_LD(uint32_t, ((uint8_t *)&gm_l12));
            uint32_t t71 = gm_l5;
            uint8_t t72 = ((t70 == t71));
            if (t72) {
                uint32_t t73 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 4));
                uint32_t t74 = gm_l6;
                uint32_t r75; bool o76 = __builtin_sub_overflow(t73, t74, &r75);
                if (o76) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 230, 41);
                uint32_t t77 = r75;
                uint32_t t78 = (t77 > ((uint32_t)1ULL) ? t77 : ((uint32_t)1ULL));
                gm_l13 = t78;
            } else {
                uint32_t t79 = gm_l11;
                uint32_t r80; bool o81 = __builtin_add_overflow(t79, ((uint32_t)1ULL), &r80);
                if (o81) gm_trap("integer overflow in `+`", gm_srcgfglnv8p059r, 230, 73);
                uint32_t t82 = r80;
                uint32_t t83 = gm_l6;
                uint32_t r84; bool o85 = __builtin_sub_overflow(t82, t83, &r84);
                if (o85) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 230, 73);
                uint32_t t86 = r84;
                uint32_t t87 = (t86 > ((uint32_t)1ULL) ? t86 : ((uint32_t)1ULL));
                gm_l13 = t87;
            }
            {
                gm_trqfvmjtt4sij t88;
                gm_f_list_uvfu1k1((void *)&t88, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l14 = t88;
            }
            uint32_t t89 = gm_l6;
            uint32_t r90; bool o91 = __builtin_sub_overflow(t89, ((uint32_t)1ULL), &r90);
            if (o91) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 232, 18);
            uint32_t t92 = r90;
            gm_l15 = t92;
            uint32_t t93 = gm_l11;
            uint8_t t94 = (t93 > ((uint32_t)200ULL));
            if (t94) {
                uint32_t t95 = gm_l6;
                uint32_t r96; bool o97 = __builtin_sub_overflow(t95, ((uint32_t)1ULL), &r96);
                if (o97) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 236, 23);
                uint32_t t98 = r96;
                if (((uint32_t)3ULL) == 0) gm_trap("division by zero", gm_srcgfglnv8p059r, 236, 35);
                uint32_t t99 = (uint32_t)(((uint32_t)120ULL) / ((uint32_t)3ULL));
                uint32_t r100; bool o101 = __builtin_sub_overflow(t98, t99, &r100);
                uint32_t t102 = o101 ? (0 ? ((uint32_t)4294967295ULL) : ((uint32_t)0ULL)) : r100;
                gm_l16 = t102;
                uint32_t t103 = gm_l16;
                uint32_t r104; bool o105 = __builtin_add_overflow(t103, ((uint32_t)120ULL), &r104);
                if (o105) gm_trap("integer overflow in `+`", gm_srcgfglnv8p059r, 237, 22);
                uint32_t t106 = r104;
                uint32_t t107 = gm_l11;
                uint32_t t108 = (t106 < t107 ? t106 : t107);
                gm_l17 = t108;
                uint32_t t109 = gm_l16;
                uint8_t t110 = (t109 > ((uint32_t)0ULL));
                if (t110) {
                    void * t111 = (void *)((uint8_t *)&F.l14);
                    gm_b16_8 t112;
                    GM_ST(void *, (uint8_t *)&t112, (void *)(gm_skg16mqffvnq6));
                    GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(3));
                    gm_tftb1odc739t5 t113 = gm_f_extend_0cs85gs(t111, t112);
                    gm_l32 = t113;
                    uint8_t t114 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                    if (!t114) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcgfglnv8p059r, 238, 28);
                }
                void * t115 = gm_l3;
                uint8_t t116 = gm_f_is_ascii_t9jud42(t115);
                if (t116) {
                    void * t117 = GM_LD(void *, ((uint8_t *)&gm_l10));
                    uint64_t t118 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint32_t t119 = gm_l16;
                    uint64_t t120 = (uint64_t)t119;
                    uint32_t t121 = gm_l17;
                    uint64_t t122 = (uint64_t)t121;
                    uint64_t t123 = (uintptr_t)t120;
                    uint64_t t124 = (uintptr_t)t122;
                    if (t123 > t124 || t124 > (uintptr_t)t118) gm_trap_slice(t123, t124, t118, gm_srcgfglnv8p059r, 239, 39);
                    uint64_t t125 = ((uintptr_t)t122) - (uintptr_t)t120;
                    gm_b16_8 t126;
                    GM_ST(void *, (uint8_t *)&t126, (void *)(((uint8_t *)t117 + (uintptr_t)t120 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(t125));
                    gm_l18 = t126;
                } else {
                    void * t127 = GM_LD(void *, ((uint8_t *)&gm_l10));
                    uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    gm_b16_8 t129 = gm_l10;
                    uint32_t t130 = gm_l16;
                    uint64_t t131 = (uint64_t)t130;
                    uint64_t t132 = gm_f_char_offset_kr8000h(t129, t131);
                    gm_b16_8 t133 = gm_l10;
                    uint32_t t134 = gm_l17;
                    uint64_t t135 = (uint64_t)t134;
                    uint64_t t136 = gm_f_char_offset_kr8000h(t133, t135);
                    uint64_t t137 = (uintptr_t)t132;
                    uint64_t t138 = (uintptr_t)t136;
                    if (t137 > t138 || t138 > (uintptr_t)t128) gm_trap_slice(t137, t138, t128, gm_srcgfglnv8p059r, 239, 80);
                    uint64_t t139 = ((uintptr_t)t136) - (uintptr_t)t132;
                    gm_b16_8 t140;
                    GM_ST(void *, (uint8_t *)&t140, (void *)(((uint8_t *)t127 + (uintptr_t)t132 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(t139));
                    gm_l18 = t140;
                }
                void * t141 = (void *)((uint8_t *)&F.l14);
                gm_b16_8 t142 = gm_l18;
                gm_b0_1 t143 = gm_f_push_untabbed_n2b9umq((void *)NULL, cx, NULL, t141, t142);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t144 = gm_l17;
                uint32_t t145 = gm_l11;
                uint8_t t146 = (t144 < t145);
                if (t146) {
                    void * t147 = (void *)((uint8_t *)&F.l14);
                    gm_b16_8 t148;
                    GM_ST(void *, (uint8_t *)&t148, (void *)(gm_skg16mqffvnq6));
                    GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(3));
                    gm_tftb1odc739t5 t149 = gm_f_extend_0cs85gs(t147, t148);
                    gm_l33 = t149;
                    uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                    if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcgfglnv8p059r, 241, 29);
                }
                uint32_t t151 = gm_l13;
                uint32_t t152 = gm_l17;
                uint32_t r153; bool o154 = __builtin_add_overflow(t152, ((uint32_t)1ULL), &r153);
                if (o154) gm_trap("integer overflow in `+`", gm_srcgfglnv8p059r, 242, 31);
                uint32_t t155 = r153;
                uint32_t t156 = gm_l6;
                uint32_t r157; bool o158 = __builtin_sub_overflow(t155, t156, &r157);
                if (o158) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 242, 31);
                uint32_t t159 = r157;
                uint32_t t160 = (t151 < t159 ? t151 : t159);
                gm_l13 = t160;
                uint32_t t161 = gm_l6;
                uint32_t r162; bool o163 = __builtin_sub_overflow(t161, ((uint32_t)1ULL), &r162);
                if (o163) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 243, 21);
                uint32_t t164 = r162;
                uint32_t t165 = gm_l16;
                uint32_t r166; bool o167 = __builtin_sub_overflow(t164, t165, &r166);
                if (o167) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 243, 21);
                uint32_t t168 = r166;
                uint32_t t169 = gm_l16;
                uint8_t t170 = (t169 > ((uint32_t)0ULL));
                if (t170) {
                    gm_l34 = ((uint32_t)1ULL);
                } else {
                    gm_l34 = ((uint32_t)0ULL);
                }
                uint32_t t171 = gm_l34;
                uint32_t r172; bool o173 = __builtin_add_overflow(t168, t171, &r172);
                if (o173) gm_trap("integer overflow in `+`", gm_srcgfglnv8p059r, 243, 21);
                uint32_t t174 = r172;
                gm_l15 = t174;
            } else {
                void * t175 = (void *)((uint8_t *)&F.l14);
                gm_b16_8 t176 = gm_l10;
                gm_b0_1 t177 = gm_f_push_untabbed_n2b9umq((void *)NULL, cx, NULL, t175, t176);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t178 = gm_l2;
            gm_b16_8 t179 = gm_l9;
            gm_tnpifv20irnph t180;
            GM_ST(gm_b16_8, ((uint8_t *)&t180 + 0), t179);
            gm_tftb1odc739t5 t181 = gm_f_write_ef6g7s0(t178, t180);
            gm_l35 = t181;
            uint8_t t182 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
            if (!t182) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_srcgfglnv8p059r, 247, 9);
            {
                void * t183 = gm_l2;
                uint64_t t184 = gm_l8;
                uint64_t t185 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t r186; bool o187 = __builtin_sub_overflow(t184, t185, &r186);
                if (o187) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 248, 41);
                uint64_t t188 = r186;
                gm_b16_8 t189 = gm_f_spaces_d2e4env((void *)NULL, cx, NULL, t188);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t190 = gm_l7;
                gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                gm_b16_8 t192 = GM_LD(gm_b16_8, ((uint8_t *)&t191));
                gm_tfeab279c16rr t193;
                GM_ST(gm_b16_8, ((uint8_t *)&t193 + 0), t189);
                GM_ST(gm_b16_8, ((uint8_t *)&t193 + 16), t190);
                GM_ST(gm_b16_8, ((uint8_t *)&t193 + 32), t192);
                gm_tftb1odc739t5 t194 = gm_f_write_gelas9q(t183, &t193);
                gm_l36 = t194;
            }
            uint8_t t195 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
            if (!t195) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_srcgfglnv8p059r, 248, 9);
            {
                void * t196 = gm_l2;
                gm_b16_8 t197 = gm_l9;
                uint32_t t198 = gm_l15;
                uint64_t t199 = (uint64_t)t198;
                gm_b16_8 t200 = gm_f_spaces_d2e4env((void *)NULL, cx, NULL, t199);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6j6smj7qoctu t201;
                GM_ST(gm_b16_8, ((uint8_t *)&t201 + 0), t197);
                GM_ST(gm_b16_8, ((uint8_t *)&t201 + 16), t200);
                gm_tftb1odc739t5 t202 = gm_f_write_6qt0u2d(t196, &t201);
                gm_l37 = t202;
            }
            uint8_t t203 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
            if (!t203) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srcgfglnv8p059r, 249, 9);
            gm_l20 = ((uint32_t)0ULL);
            uint32_t t204 = gm_l13;
            uint32_t t205 = (t204 > ((uint32_t)1ULL) ? t204 : ((uint32_t)1ULL));
            gm_l19 = t205;
            {
                for (;;) {
                    uint32_t t206 = gm_l20;
                    uint32_t t207 = gm_l19;
                    uint8_t t208 = (t206 < t207);
                    if (!t208) {
                        goto gm_lb1;
                    }
                    {
                        {
                            {
                                void * t209 = gm_l2;
                                gm_tftb1odc739t5 t210 = gm_f_push_t83rg2m(t209, ((uint8_t)94ULL));
                                gm_l38 = t210;
                                uint8_t t211 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                if (!t211) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srcgfglnv8p059r, 250, 36);
                                goto gm_lb4;
                            }
gm_lb5:;
                            gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 250, 9);
                        }
gm_lb4:;
                    }
gm_lb3:;
                    uint32_t t212 = gm_l20;
                    uint32_t r213; bool o214 = __builtin_add_overflow(t212, ((uint32_t)1ULL), &r213);
                    uint32_t t215 = r213;
                    gm_l20 = t215;
                }
            }
gm_lb1:;
            void * t216 = gm_l2;
            gm_tftb1odc739t5 t217 = gm_f_push_t83rg2m(t216, ((uint8_t)10ULL));
            gm_l39 = t217;
            uint8_t t218 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
            if (!t218) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_srcgfglnv8p059r, 251, 9);
        }
    }
    {
        void * t219 = gm_l1;
        gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)t219) + 40));
        gm_l21 = t220;
        gm_l22 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t221 = gm_l22;
                uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                uint8_t t223 = (t221 < t222);
                if (!t223) {
                    goto gm_lb6;
                }
                {
                    void * t224 = GM_LD(void *, ((uint8_t *)&gm_l21));
                    uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                    uint64_t t226 = gm_l22;
                    {
                        {
                            {
                                memmove(((uint8_t *)&gm_l23), (((uint8_t *)t224) + (uintptr_t)t226 * 32), 32);
                            }
                            gm_tl874t2atnlh3 t227 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l23));
                            gm_tl874t2atnlh3 t228 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                            uint8_t t229 = (gm_eqff85ac10o3(((uint8_t *)&t227), ((uint8_t *)&t228)));
                            if (t229) {
                                goto gm_lb8;
                            }
                            void * t230 = gm_l0;
                            uint32_t t231 = GM_LD(uint32_t, ((uint8_t *)&gm_l23));
                            void * t232 = gm_f_file_o15fqku(t230, t231);
                            gm_l24 = t232;
                            void * t233 = gm_l24;
                            uint32_t t234 = GM_LD(uint32_t, (((uint8_t *)&gm_l23) + 4));
                            gm_tcc1rq4n1avb2 t235 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t233, t234));
                            gm_l25 = t235;
                            {
                                void * t236 = gm_l2;
                                gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 16));
                                void * t238 = gm_l24;
                                gm_b16_8 t239 = GM_LD(gm_b16_8, ((uint8_t *)t238));
                                uint32_t t240 = GM_LD(uint32_t, ((uint8_t *)&gm_l25));
                                uint32_t t241 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 4));
                                gm_thrm96fhhdpt5 t242;
                                GM_ST(gm_b16_8, ((uint8_t *)&t242 + 0), t237);
                                GM_ST(gm_b16_8, ((uint8_t *)&t242 + 16), t239);
                                GM_ST(uint32_t, ((uint8_t *)&t242 + 32), t240);
                                GM_ST(uint32_t, ((uint8_t *)&t242 + 36), t241);
                                gm_tftb1odc739t5 t243 = gm_f_write_capundt(t236, &t242);
                                gm_l40 = t243;
                            }
                            uint8_t t244 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                            if (!t244) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srcgfglnv8p059r, 257, 9);
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 253, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t245 = gm_l22;
                uint64_t r246; bool o247 = __builtin_add_overflow(t245, ((uint64_t)1ULL), &r246);
                uint64_t t248 = r246;
                gm_l22 = t248;
            }
        }
gm_lb6:;
        void * t249 = gm_l1;
        gm_b16_8 t250 = GM_LD(gm_b16_8, (((uint8_t *)t249) + 80));
        gm_l26 = t250;
        gm_l27 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t251 = gm_l27;
                uint64_t t252 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                uint8_t t253 = (t251 < t252);
                if (!t253) {
                    goto gm_lb11;
                }
                {
                    void * t254 = GM_LD(void *, ((uint8_t *)&gm_l26));
                    uint64_t t255 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                    uint64_t t256 = gm_l27;
                    {
                        {
                            gm_b16_8 t257 = GM_LD(gm_b16_8, (((uint8_t *)t254) + (uintptr_t)t256 * 16));
                            gm_l28 = t257;
                            void * t258 = gm_l2;
                            gm_b16_8 t259 = gm_l28;
                            gm_tnpifv20irnph t260;
                            GM_ST(gm_b16_8, ((uint8_t *)&t260 + 0), t259);
                            gm_tftb1odc739t5 t261 = gm_f_write_bvpfm1k(t258, t260);
                            gm_l41 = t261;
                            uint8_t t262 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                            if (!t262) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srcgfglnv8p059r, 259, 30);
                            goto gm_lb14;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 259, 5);
                    }
gm_lb14:;
                }
gm_lb13:;
                uint64_t t263 = gm_l27;
                uint64_t r264; bool o265 = __builtin_add_overflow(t263, ((uint64_t)1ULL), &r264);
                uint64_t t266 = r264;
                gm_l27 = t266;
            }
        }
gm_lb11:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_render_json_a7n3ru2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_render_json_a7n3ru2 F;
    void * gm_l0; /* sm */
    void * gm_l1; /* d */
    void * gm_l2; /* out */
    uint8_t gm_l3; /* located */
    gm_b16_8 gm_l4; /* c */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* j */
    gm_tbgd14esf0jvi gm_l12; /* e */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_tbgd14esf0jvi gm_l16; /* l */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* i */
    gm_b16_8 gm_l20; /* n */
    GM_STACK_CHECK(&F, 1972u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tm101l8077enc gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tdlq2ugehok8g gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    gm_tdlq2ugehok8g gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tftb1odc739t5 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tftb1odc739t5 gm_l43;
    gm_tf1bjg81u0glv gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_tftb1odc739t5 gm_l46;
    gm_tftb1odc739t5 gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    {
        void * t24 = gm_l1;
        gm_tl874t2atnlh3 t25 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t24) + 24));
        gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
        uint8_t t27 = (!gm_eqff85ac10o3(((uint8_t *)&t25), ((uint8_t *)&t26)));
        gm_l3 = t27;
        void * t28 = gm_l2;
        gm_tftb1odc739t5 t29 = gm_f_push_t83rg2m(t28, ((uint8_t)123ULL));
        gm_l21 = t29;
        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcgfglnv8p059r, 299, 5);
        {
            {
                void * t31 = gm_l1;
                uint8_t t32 = (uint8_t)(((((uint8_t *)t31) + 160))[16] != 0);
                if (!t32) {
                    goto gm_lb2;
                }
                gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)t31) + 160));
                gm_l4 = t33;
                void * t34 = gm_l2;
                gm_b16_8 t35;
                GM_ST(void *, (uint8_t *)&t35, (void *)(gm_sjbb259gf4uuc));
                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(7));
                gm_tftb1odc739t5 t36 = gm_f_extend_0cs85gs(t34, t35);
                gm_l22 = t36;
                uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcgfglnv8p059r, 301, 9);
                gm_b16_8 t38 = gm_l4;
                void * t39 = gm_l2;
                gm_b0_1 t40 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t38, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t41 = gm_l2;
                gm_tftb1odc739t5 t42 = gm_f_push_t83rg2m(t41, ((uint8_t)44ULL));
                gm_l23 = t42;
                uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcgfglnv8p059r, 303, 9);
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        uint8_t t44 = gm_l3;
        if (t44) {
            void * t45 = gm_l0;
            void * t46 = gm_l1;
            gm_tl874t2atnlh3 t47 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t46) + 24));
            void * t48 = gm_l2;
            gm_b0_1 t49 = gm_f_json_before_line_2trhgro((void *)NULL, cx, NULL, t45, t47, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t50 = gm_l2;
            gm_tftb1odc739t5 t51 = gm_f_push_t83rg2m(t50, ((uint8_t)44ULL));
            gm_l24 = t51;
            uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
            if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcgfglnv8p059r, 307, 9);
        }
        void * t53 = gm_l2;
        gm_b16_8 t54;
        GM_ST(void *, (uint8_t *)&t54, (void *)(gm_su6414ei8c1ir));
        GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(9));
        gm_tftb1odc739t5 t55 = gm_f_extend_0cs85gs(t53, t54);
        gm_l25 = t55;
        uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
        if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcgfglnv8p059r, 309, 5);
        void * t57 = gm_l1;
        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)t57) + 120));
        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
        gm_l5 = t59;
        gm_l6 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t60 = gm_l6;
                uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint8_t t62 = (t60 < t61);
                if (!t62) {
                    goto gm_lb3;
                }
                {
                    {
                        uint64_t t63 = gm_l6;
                        void * t64 = GM_LD(void *, ((uint8_t *)&gm_l5));
                        uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                        uint64_t t66 = gm_l6;
                        gm_tl7hpf5nlilqq t67 = GM_LD(gm_tl7hpf5nlilqq, (((uint8_t *)t64) + (uintptr_t)t66 * 56));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l26 + 0), t63);
                        GM_ST(gm_tl7hpf5nlilqq, ((uint8_t *)&gm_l26 + 8), t67);
                    }
                    {
                        {
                            uint64_t t68 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
                            gm_l7 = t68;
                            memmove(((uint8_t *)&F.l8), (((uint8_t *)&gm_l26) + 8), 56);
                            uint64_t t69 = gm_l7;
                            uint8_t t70 = (t69 > ((uint64_t)0ULL));
                            if (t70) {
                                void * t71 = gm_l2;
                                gm_tftb1odc739t5 t72 = gm_f_push_t83rg2m(t71, ((uint8_t)44ULL));
                                gm_l27 = t72;
                                uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcgfglnv8p059r, 311, 20);
                            }
                            void * t74 = gm_l2;
                            gm_b16_8 t75;
                            GM_ST(void *, (uint8_t *)&t75, (void *)(gm_s5rqsv8c71bds));
                            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(10));
                            gm_tftb1odc739t5 t76 = gm_f_extend_0cs85gs(t74, t75);
                            gm_l28 = t76;
                            uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                            if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcgfglnv8p059r, 312, 9);
                            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 16));
                            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&t78));
                            gm_l9 = t79;
                            gm_l10 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t80 = gm_l10;
                                    uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                    uint8_t t82 = (t80 < t81);
                                    if (!t82) {
                                        goto gm_lb8;
                                    }
                                    {
                                        {
                                            uint64_t t83 = gm_l10;
                                            void * t84 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                            uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                            uint64_t t86 = gm_l10;
                                            gm_tbgd14esf0jvi t87 = GM_LD(gm_tbgd14esf0jvi, (((uint8_t *)t84) + (uintptr_t)t86 * 32));
                                            GM_ST(uint64_t, ((uint8_t *)&gm_l29 + 0), t83);
                                            GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&gm_l29 + 8), t87);
                                        }
                                        {
                                            {
                                                uint64_t t88 = GM_LD(uint64_t, ((uint8_t *)&gm_l29));
                                                gm_l11 = t88;
                                                memmove(((uint8_t *)&gm_l12), (((uint8_t *)&gm_l29) + 8), 32);
                                                uint64_t t89 = gm_l11;
                                                uint8_t t90 = (t89 > ((uint64_t)0ULL));
                                                if (t90) {
                                                    void * t91 = gm_l2;
                                                    gm_tftb1odc739t5 t92 = gm_f_push_t83rg2m(t91, ((uint8_t)44ULL));
                                                    gm_l30 = t92;
                                                    uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                                                    if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srcgfglnv8p059r, 314, 24);
                                                }
                                                void * t94 = gm_l2;
                                                gm_tftb1odc739t5 t95 = gm_f_push_t83rg2m(t94, ((uint8_t)123ULL));
                                                gm_l31 = t95;
                                                uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                                                if (!t96) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srcgfglnv8p059r, 315, 13);
                                                void * t97 = gm_l0;
                                                gm_tl874t2atnlh3 t98 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l12));
                                                void * t99 = gm_l2;
                                                gm_b0_1 t100 = gm_f_json_location_e84g9mr((void *)NULL, cx, NULL, t97, t98, t99);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t101 = gm_l2;
                                                gm_b16_8 t102;
                                                GM_ST(void *, (uint8_t *)&t102, (void *)(gm_s9caepulv0516));
                                                GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(8));
                                                gm_tftb1odc739t5 t103 = gm_f_extend_0cs85gs(t101, t102);
                                                gm_l32 = t103;
                                                uint8_t t104 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                                if (!t104) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcgfglnv8p059r, 317, 13);
                                                gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                                                void * t106 = gm_l2;
                                                gm_b0_1 t107 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t105, t106);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                void * t108 = gm_l2;
                                                gm_tftb1odc739t5 t109 = gm_f_push_t83rg2m(t108, ((uint8_t)125ULL));
                                                gm_l33 = t109;
                                                uint8_t t110 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                                if (!t110) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcgfglnv8p059r, 319, 13);
                                                goto gm_lb11;
                                            }
gm_lb12:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 313, 9);
                                        }
gm_lb11:;
                                    }
gm_lb10:;
                                    uint64_t t111 = gm_l10;
                                    uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                                    uint64_t t114 = r112;
                                    gm_l10 = t114;
                                }
                            }
gm_lb8:;
                            void * t115 = gm_l2;
                            gm_b16_8 t116;
                            GM_ST(void *, (uint8_t *)&t116, (void *)(gm_sap9oatgj29c2));
                            GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(12));
                            gm_tftb1odc739t5 t117 = gm_f_extend_0cs85gs(t115, t116);
                            gm_l34 = t117;
                            uint8_t t118 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                            if (!t118) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_srcgfglnv8p059r, 321, 9);
                            gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                            void * t120 = gm_l2;
                            gm_b0_1 t121 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t119, t120);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t122 = gm_l2;
                            gm_tftb1odc739t5 t123 = gm_f_push_t83rg2m(t122, ((uint8_t)125ULL));
                            gm_l35 = t123;
                            uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                            if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_srcgfglnv8p059r, 323, 9);
                            goto gm_lb6;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 310, 5);
                    }
gm_lb6:;
                }
gm_lb5:;
                uint64_t t125 = gm_l6;
                uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                uint64_t t128 = r126;
                gm_l6 = t128;
            }
        }
gm_lb3:;
    }
    {
        void * t129 = gm_l2;
        gm_b16_8 t130;
        GM_ST(void *, (uint8_t *)&t130, (void *)(gm_s4k2ktjht9l4a));
        GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(12));
        gm_tftb1odc739t5 t131 = gm_f_extend_0cs85gs(t129, t130);
        gm_l36 = t131;
        uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
        if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_srcgfglnv8p059r, 325, 5);
        void * t133 = gm_l1;
        gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)t133) + 40));
        gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
        gm_l13 = t135;
        gm_l14 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t136 = gm_l14;
                uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                uint8_t t138 = (t136 < t137);
                if (!t138) {
                    goto gm_lb13;
                }
                {
                    {
                        uint64_t t139 = gm_l14;
                        void * t140 = GM_LD(void *, ((uint8_t *)&gm_l13));
                        uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                        uint64_t t142 = gm_l14;
                        gm_tbgd14esf0jvi t143 = GM_LD(gm_tbgd14esf0jvi, (((uint8_t *)t140) + (uintptr_t)t142 * 32));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l37 + 0), t139);
                        GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&gm_l37 + 8), t143);
                    }
                    {
                        {
                            uint64_t t144 = GM_LD(uint64_t, ((uint8_t *)&gm_l37));
                            gm_l15 = t144;
                            memmove(((uint8_t *)&gm_l16), (((uint8_t *)&gm_l37) + 8), 32);
                            uint64_t t145 = gm_l15;
                            uint8_t t146 = (t145 > ((uint64_t)0ULL));
                            if (t146) {
                                void * t147 = gm_l2;
                                gm_tftb1odc739t5 t148 = gm_f_push_t83rg2m(t147, ((uint8_t)44ULL));
                                gm_l38 = t148;
                                uint8_t t149 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                if (!t149) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srcgfglnv8p059r, 327, 20);
                            }
                            void * t150 = gm_l2;
                            gm_tftb1odc739t5 t151 = gm_f_push_t83rg2m(t150, ((uint8_t)123ULL));
                            gm_l39 = t151;
                            uint8_t t152 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                            if (!t152) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_srcgfglnv8p059r, 328, 9);
                            void * t153 = gm_l0;
                            gm_tl874t2atnlh3 t154 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l16));
                            void * t155 = gm_l2;
                            gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 16));
                            gm_b0_1 t157 = gm_f_json_location_split_5jggoqp((void *)NULL, cx, NULL, t153, t154, t155, t156);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t158 = gm_l2;
                            gm_tftb1odc739t5 t159 = gm_f_push_t83rg2m(t158, ((uint8_t)125ULL));
                            gm_l40 = t159;
                            uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                            if (!t160) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srcgfglnv8p059r, 330, 9);
                            goto gm_lb16;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 326, 5);
                    }
gm_lb16:;
                }
gm_lb15:;
                uint64_t t161 = gm_l14;
                uint64_t r162; bool o163 = __builtin_add_overflow(t161, ((uint64_t)1ULL), &r162);
                uint64_t t164 = r162;
                gm_l14 = t164;
            }
        }
gm_lb13:;
        void * t165 = gm_l2;
        gm_b16_8 t166;
        GM_ST(void *, (uint8_t *)&t166, (void *)(gm_s2053hhvf16fm));
        GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(1));
        gm_tftb1odc739t5 t167 = gm_f_extend_0cs85gs(t165, t166);
        gm_l41 = t167;
        uint8_t t168 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
        if (!t168) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srcgfglnv8p059r, 332, 5);
    }
    {
        uint8_t t169 = gm_l3;
        if (t169) {
            void * t170 = gm_l0;
            void * t171 = gm_l1;
            gm_tl874t2atnlh3 t172 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t171) + 24));
            void * t173 = gm_l2;
            void * t174 = gm_l1;
            gm_b16_8 t175 = GM_LD(gm_b16_8, (((uint8_t *)t174) + 8));
            gm_b0_1 t176 = gm_f_json_after_line_c5i6134((void *)NULL, cx, NULL, t170, t172, t173, t175);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        } else {
            void * t177 = gm_l2;
            gm_b16_8 t178;
            GM_ST(void *, (uint8_t *)&t178, (void *)(gm_sh75rh8dk51gf));
            GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(11));
            gm_tftb1odc739t5 t179 = gm_f_extend_0cs85gs(t177, t178);
            gm_l42 = t179;
            uint8_t t180 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
            if (!t180) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srcgfglnv8p059r, 336, 9);
            void * t181 = gm_l1;
            gm_b16_8 t182 = GM_LD(gm_b16_8, (((uint8_t *)t181) + 8));
            void * t183 = gm_l2;
            gm_b0_1 t184 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t182, t183);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t185 = gm_l2;
        gm_b16_8 t186;
        GM_ST(void *, (uint8_t *)&t186, (void *)(gm_sgcj0gjfdljat));
        GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(10));
        gm_tftb1odc739t5 t187 = gm_f_extend_0cs85gs(t185, t186);
        gm_l43 = t187;
        uint8_t t188 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
        if (!t188) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_srcgfglnv8p059r, 339, 5);
        void * t189 = gm_l1;
        gm_b16_8 t190 = GM_LD(gm_b16_8, (((uint8_t *)t189) + 80));
        gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&t190));
        gm_l17 = t191;
        gm_l18 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t192 = gm_l18;
                uint64_t t193 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint8_t t194 = (t192 < t193);
                if (!t194) {
                    goto gm_lb18;
                }
                {
                    uint64_t t195 = gm_l18;
                    void * t196 = GM_LD(void *, ((uint8_t *)&gm_l17));
                    uint64_t t197 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    uint64_t t198 = gm_l18;
                    gm_b16_8 t199 = GM_LD(gm_b16_8, (((uint8_t *)t196) + (uintptr_t)t198 * 16));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l44 + 0), t195);
                    GM_ST(gm_b16_8, ((uint8_t *)&gm_l44 + 8), t199);
                    {
                        {
                            uint64_t t200 = GM_LD(uint64_t, ((uint8_t *)&gm_l44));
                            gm_l19 = t200;
                            gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 8));
                            gm_l20 = t201;
                            uint64_t t202 = gm_l19;
                            uint8_t t203 = (t202 > ((uint64_t)0ULL));
                            if (t203) {
                                void * t204 = gm_l2;
                                gm_tftb1odc739t5 t205 = gm_f_push_t83rg2m(t204, ((uint8_t)44ULL));
                                gm_l45 = t205;
                                uint8_t t206 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                if (!t206) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srcgfglnv8p059r, 341, 20);
                            }
                            gm_b16_8 t207 = gm_l20;
                            void * t208 = gm_l2;
                            gm_b0_1 t209 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t207, t208);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb21;
                        }
gm_lb22:;
                        gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 340, 5);
                    }
gm_lb21:;
                }
gm_lb20:;
                uint64_t t210 = gm_l18;
                uint64_t r211; bool o212 = __builtin_add_overflow(t210, ((uint64_t)1ULL), &r211);
                uint64_t t213 = r211;
                gm_l18 = t213;
            }
        }
gm_lb18:;
    }
    {
        void * t214 = gm_l2;
        gm_b16_8 t215;
        GM_ST(void *, (uint8_t *)&t215, (void *)(gm_s2053hhvf16fm));
        GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(1));
        gm_tftb1odc739t5 t216 = gm_f_extend_0cs85gs(t214, t215);
        gm_l46 = t216;
        uint8_t t217 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
        if (!t217) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_srcgfglnv8p059r, 344, 5);
        uint8_t t218 = gm_l3;
        if (t218) {
            void * t219 = gm_l2;
            void * t220 = gm_l1;
            uint32_t t221 = GM_LD(uint32_t, ((((uint8_t *)t220) + 24) + 4));
            gm_t88rcucg1b7uk t222;
            GM_ST(uint32_t, ((uint8_t *)&t222 + 0), t221);
            gm_tftb1odc739t5 t223 = gm_f_write_5ldgli3(t219, t222);
            gm_l47 = t223;
            uint8_t t224 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
            if (!t224) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_srcgfglnv8p059r, 345, 18);
        }
        void * t225 = gm_l2;
        gm_b16_8 t226;
        GM_ST(void *, (uint8_t *)&t226, (void *)(gm_sun80ccb875r1));
        GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(12));
        gm_tftb1odc739t5 t227 = gm_f_extend_0cs85gs(t225, t226);
        gm_l48 = t227;
        uint8_t t228 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
        if (!t228) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_srcgfglnv8p059r, 346, 5);
        void * t229 = gm_l1;
        gm_t9qkh0rsooc8d t230 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)t229));
        gm_b16_8 t231 = gm_f_severity_name_auihisp(t230);
        void * t232 = gm_l2;
        gm_b0_1 t233 = gm_f_json_string_n2ks51u((void *)NULL, cx, NULL, t231, t232);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t234 = gm_l2;
        gm_b16_8 t235;
        GM_ST(void *, (uint8_t *)&t235, (void *)(gm_sikpqtja8pqpv));
        GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t236 = gm_f_extend_0cs85gs(t234, t235);
        gm_l49 = t236;
        uint8_t t237 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
        if (!t237) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srcgfglnv8p059r, 348, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_render_pi7hvmf(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t9qkh0rsooc8d a2, void * a3) {
    struct gm_fr_render_pi7hvmf F;
    void * gm_l0; /* sm */
    void * gm_l1; /* d */
    gm_t9qkh0rsooc8d gm_l2; /* format */
    void * gm_l3; /* out */
    GM_STACK_CHECK(&F, 103u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            void * t8 = gm_l0;
            void * t9 = gm_l1;
            void * t10 = gm_l3;
            gm_b0_1 t11 = gm_f_render_human_fmipkqr((void *)NULL, cx, NULL, t8, t9, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)1ULL));
            if (!t13) {
                goto gm_lb3;
            }
            void * t14 = gm_l0;
            void * t15 = gm_l1;
            void * t16 = gm_l3;
            gm_b0_1 t17 = gm_f_render_short_oijr4gl((void *)NULL, cx, NULL, t14, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)2ULL));
            if (!t19) {
                goto gm_lb4;
            }
            void * t20 = gm_l0;
            void * t21 = gm_l1;
            void * t22 = gm_l3;
            gm_b0_1 t23 = gm_f_render_json_a7n3ru2((void *)NULL, cx, NULL, t20, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 204, 5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_render_short_oijr4gl(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_render_short_oijr4gl F;
    void * gm_l0; /* sm */
    void * gm_l1; /* d */
    void * gm_l2; /* out */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tbgd14esf0jvi gm_l5; /* l */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* n */
    GM_STACK_CHECK(&F, 606u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    void * t12 = gm_l0;
    void * t13 = gm_l1;
    gm_tl874t2atnlh3 t14 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t13) + 24));
    void * t15 = gm_l2;
    gm_b0_1 t16 = gm_f_location_3clcfto((void *)NULL, cx, NULL, t12, t14, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t17 = gm_l2;
    void * t18 = gm_l1;
    gm_b16_8 t19 = gm_f_heading_jklompb((void *)NULL, cx, NULL, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t20 = gm_l1;
    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t20) + 8));
    gm_t6j6smj7qoctu t22;
    GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t19);
    GM_ST(gm_b16_8, ((uint8_t *)&t22 + 16), t21);
    gm_tftb1odc739t5 t23 = gm_f_write_oouhb7d(t17, &t22);
    gm_l9 = t23;
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcgfglnv8p059r, 276, 5);
    void * t25 = gm_l1;
    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t25) + 40));
    gm_l3 = t26;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t27 = gm_l4;
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t29 = (t27 < t28);
            if (!t29) {
                goto gm_lb1;
            }
            {
                void * t30 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t32 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t30) + (uintptr_t)t32 * 32), 32);
                        gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l5));
                        gm_tl874t2atnlh3 t34 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
                        uint8_t t35 = (gm_eqff85ac10o3(((uint8_t *)&t33), ((uint8_t *)&t34)));
                        if (t35) {
                            goto gm_lb3;
                        }
                        void * t36 = gm_l0;
                        gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l5));
                        void * t38 = gm_l2;
                        gm_b0_1 t39 = gm_f_location_3clcfto((void *)NULL, cx, NULL, t36, t37, t38);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t40 = gm_l2;
                        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 16));
                        gm_tnpifv20irnph t42;
                        GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
                        gm_tftb1odc739t5 t43 = gm_f_write_35ac6au(t40, t42);
                        gm_l10 = t43;
                        uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcgfglnv8p059r, 280, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 277, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t45 = gm_l4;
            uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
            uint64_t t48 = r46;
            gm_l4 = t48;
        }
    }
gm_lb1:;
    void * t49 = gm_l1;
    gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)t49) + 80));
    gm_l6 = t50;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t51 = gm_l7;
            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t53 = (t51 < t52);
            if (!t53) {
                goto gm_lb6;
            }
            {
                void * t54 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t56 = gm_l7;
                {
                    {
                        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)t54) + (uintptr_t)t56 * 16));
                        gm_l8 = t57;
                        void * t58 = gm_l0;
                        void * t59 = gm_l1;
                        gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t59) + 24));
                        void * t61 = gm_l2;
                        gm_b0_1 t62 = gm_f_location_3clcfto((void *)NULL, cx, NULL, t58, t60, t61);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t63 = gm_l2;
                        gm_b16_8 t64 = gm_l8;
                        gm_tnpifv20irnph t65;
                        GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t64);
                        gm_tftb1odc739t5 t66 = gm_f_write_35ac6au(t63, t65);
                        gm_l11 = t66;
                        uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcgfglnv8p059r, 284, 9);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 282, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t68 = gm_l7;
            uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
            uint64_t t71 = r69;
            gm_l7 = t71;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_reset_dg0kphe(void * a0, gm_csc7lc6jlkqkuq a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reset_dg0kphe F;
    void * gm_l0; /* ds */
    gm_t2lqu8jsj511d gm_l1; /* m */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 253u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t2lqu8jsj511d, a1);
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
    uint64_t t11 = (uintptr_t)t10;
    uint64_t t12 = (uintptr_t)t9;
    if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcgfglnv8p059r, 139, 14);
    uint64_t t13 = ((uintptr_t)t9) - (uintptr_t)t10;
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)t10 * 184)));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
    gm_l2 = t14;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t15 = gm_l3;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t20 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t18) + (uintptr_t)t20 * 184), 184);
                        gm_t9qkh0rsooc8d t21 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l4));
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t21) + 0));
                        uint8_t t23 = (uint8_t)((t22 == ((uint8_t)2ULL)));
                        gm_t9qkh0rsooc8d t24;
                        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)2ULL));
                        if (t23) {
                            void * t25 = gm_l0;
                            void * t26 = gm_l0;
                            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 40));
                            uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
                            if (o29) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 140, 35);
                            uint64_t t30 = r28;
                            GM_ST(uint64_t, (((uint8_t *)t25) + 40), t30);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 139, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t31 = gm_l3;
            uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
            uint64_t t34 = r32;
            gm_l3 = t34;
        }
    }
gm_lb1:;
    void * t35 = gm_l0;
    void * t36 = (void *)((uint8_t *)t35);
    uint64_t t37 = GM_LD(uint64_t, ((uint8_t *)&gm_l1));
    gm_f_truncate_2jbb2bh(t36, t37);
    void * t38 = gm_l0;
    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    GM_ST(uint64_t, (((uint8_t *)t38) + 48), t39);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_severity_name_auihisp(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_severity_name_auihisp F;
    gm_t9qkh0rsooc8d gm_l0; /* s */
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
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sue26fe85rntf));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(4));
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
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s9qu470anjl4i));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(7));
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
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s79s3h98matgr));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(5));
            gm_l1 = t14;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 10, 47);
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
inline __attribute__((aligned(4))) void gm_f_sort_s8jqb1h(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_s8jqb1h F;
    void * gm_l0; /* ds */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint64_t gm_l6; /* j */
    GM_STACK_CHECK_SHALLOW(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t10 = gm_l0;
    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)t10));
    gm_l1 = t11;
    gm_l3 = ((uint64_t)1ULL);
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    gm_l2 = t12;
    {
        for (;;) {
            uint64_t t13 = gm_l3;
            uint64_t t14 = gm_l2;
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t16 = gm_l3;
                        gm_l4 = t16;
                        {
                            void * t17 = GM_LD(void *, ((uint8_t *)&gm_l1));
                            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                            uint64_t t19 = gm_l4;
                            memmove(((uint8_t *)&F.l5), (((uint8_t *)t17) + (uintptr_t)t19 * 184), 184);
                        }
                        uint64_t t20 = gm_l4;
                        gm_l6 = t20;
                        {
                            for (;;) {
                                uint64_t t21 = gm_l6;
                                uint8_t t22 = (t21 > ((uint64_t)0ULL));
                                if (!t22) {
                                    goto gm_lb6;
                                }
                                gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 24));
                                void * t24 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                uint64_t t26 = gm_l6;
                                uint64_t r27; bool o28 = __builtin_sub_overflow(t26, ((uint64_t)1ULL), &r27);
                                if (o28) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 180, 43);
                                uint64_t t29 = r27;
                                if ((uintptr_t)t29 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t29, t25, gm_srcgfglnv8p059r, 180, 40);
                                gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t24) + (uintptr_t)t29 * 184) + 24));
                                uint8_t t31 = gm_f_before_05k9hcj(GM_CONV(gm_cseahvg8v7p0vs, t23), GM_CONV(gm_cseahvg8v7p0vs, t30));
                                if (!t31) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t32 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                    uint64_t t34 = gm_l6;
                                    if ((uintptr_t)t34 >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)t34, t33, gm_srcgfglnv8p059r, 181, 13);
                                    void * t35 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                    uint64_t t37 = gm_l6;
                                    uint64_t r38; bool o39 = __builtin_sub_overflow(t37, ((uint64_t)1ULL), &r38);
                                    if (o39) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 181, 24);
                                    uint64_t t40 = r38;
                                    if ((uintptr_t)t40 >= (uintptr_t)(t36)) gm_trap_index((uintptr_t)t40, t36, gm_srcgfglnv8p059r, 181, 21);
                                    memmove((((uint8_t *)t32) + (uintptr_t)t34 * 184), (((uint8_t *)t35) + (uintptr_t)t40 * 184), 184);
                                    uint64_t t41 = gm_l6;
                                    uint64_t r42; bool o43 = __builtin_sub_overflow(t41, ((uint64_t)1ULL), &r42);
                                    if (o43) gm_trap("integer overflow in `-`", gm_srcgfglnv8p059r, 182, 13);
                                    uint64_t t44 = r42;
                                    gm_l6 = t44;
                                }
gm_lb8:;
                            }
                        }
gm_lb6:;
                        {
                            void * t45 = GM_LD(void *, ((uint8_t *)&gm_l1));
                            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                            uint64_t t47 = gm_l6;
                            if ((uintptr_t)t47 >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)t47, t46, gm_srcgfglnv8p059r, 184, 9);
                            memmove((((uint8_t *)t45) + (uintptr_t)t47 * 184), ((uint8_t *)&F.l5), 184);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 177, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t48 = gm_l3;
            uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
            uint64_t t51 = r49;
            gm_l3 = t51;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_spaces_d2e4env(void *env, gm_node *cx, gm_node *inst, uint64_t a0) {
    struct gm_fr_spaces_d2e4env F;
    uint64_t gm_l0; /* n */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l4;
    gm_trqfvmjtt4sij t7;
    gm_f_list_uvfu1k1((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t7;
    gm_l3 = ((uint64_t)0ULL);
    uint64_t t8 = gm_l0;
    gm_l2 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            uint64_t t10 = gm_l2;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t12 = (void *)((uint8_t *)&F.l1);
                        gm_tftb1odc739t5 t13 = gm_f_push_t83rg2m(t12, ((uint8_t)32ULL));
                        gm_l4 = t13;
                        uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                        if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcgfglnv8p059r, 265, 21);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcgfglnv8p059r, 265, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t15 = gm_l3;
            uint64_t r16; bool o17 = __builtin_add_overflow(t15, ((uint64_t)1ULL), &r16);
            uint64_t t18 = r16;
            gm_l3 = t18;
        }
    }
gm_lb1:;
    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
    F.ret = t20;
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
extern __attribute__((aligned(4))) void gm_f_suppress_sdgq6qj(void * a0);
extern __attribute__((aligned(4))) void gm_f_unsuppress_722t1fb(void * a0);
extern __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);

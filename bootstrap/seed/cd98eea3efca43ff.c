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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t a[16]; } gm_t51b2en4qp02s;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; uint32_t f2; uint8_t pe[4]; } gm_tk20dunvmmkbp;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
_Static_assert(sizeof(gm_csc7lc6jlkqkuq) == 16 && _Alignof(gm_csc7lc6jlkqkuq) == 8 && offsetof(gm_csc7lc6jlkqkuq, f0) == 0 && offsetof(gm_csc7lc6jlkqkuq, f1) == 8, "layout of `BlobPart`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct gm_csedogq9e4rp5r { uint32_t f0; gm_cseahvg8v7p0vs f1; } gm_csedogq9e4rp5r;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t51b2en4qp02s) == 16 && _Alignof(gm_t51b2en4qp02s) == 1 && offsetof(gm_t51b2en4qp02s, a) == 0, "layout of `[16]u8`");
typedef struct { gm_tbl8il92l97ut f0; uint8_t f1; uint8_t pe[7]; } gm_t5lf9i3n1r3co;
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; } gm_t9d4sf0vqn7dt;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(e: ^const Expr, l: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_td7tfovebirdn;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tk20dunvmmkbp) == 24 && _Alignof(gm_tk20dunvmmkbp) == 8 && offsetof(gm_tk20dunvmmkbp, f0) == 0 && offsetof(gm_tk20dunvmmkbp, f1) == 8 && offsetof(gm_tk20dunvmmkbp, f2) == 16, "layout of `(id: u32, env: ?^Frame, cx: u32)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_csedogq9e4rp5r) == 16 && _Alignof(gm_csedogq9e4rp5r) == 4 && offsetof(gm_csedogq9e4rp5r, f0) == 0 && offsetof(gm_csedogq9e4rp5r, f1) == 4, "layout of `TraceCall`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t5lf9i3n1r3co) == 24 && _Alignof(gm_t5lf9i3n1r3co) == 8 && offsetof(gm_t5lf9i3n1r3co, f0) == 0 && offsetof(gm_t5lf9i3n1r3co, f1) == 16, "layout of `?(e: ^const Expr, l: u32)`");
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint64_t v1; } f1; } gm_t833u93mr4iob;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9d4sf0vqn7dt) == 16 && _Alignof(gm_t9d4sf0vqn7dt) == 4 && offsetof(gm_t9d4sf0vqn7dt, f0) == 0 && offsetof(gm_t9d4sf0vqn7dt, f1) == 4, "layout of `TraceCall`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_b16_8 v1; } f1; } gm_td0ept12e7q8b;
_Static_assert(sizeof(gm_td7tfovebirdn) == 40 && _Alignof(gm_td7tfovebirdn) == 8 && offsetof(gm_td7tfovebirdn, f0) == 0 && offsetof(gm_td7tfovebirdn, f1) == 16 && offsetof(gm_td7tfovebirdn, f2) == 32, "layout of `([]u8, string, u32)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tk20dunvmmkbp v0; gm_td7tfovebirdn v1; gm_t9qkh0rsooc8d v2; } f1; } gm_tte9vgf6noic9;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t833u93mr4iob) == 32 && _Alignof(gm_t833u93mr4iob) == 8 && offsetof(gm_t833u93mr4iob, f0) == 0 && offsetof(gm_t833u93mr4iob, f1) == 8, "layout of `Flow!u64`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_td0ept12e7q8b) == 32 && _Alignof(gm_td0ept12e7q8b) == 8 && offsetof(gm_td0ept12e7q8b, f0) == 0 && offsetof(gm_td0ept12e7q8b, f1) == 8, "layout of `Flow!string`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; } f1; } gm_tgfdnufetdnfc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tiilmfd16v3fc f1; gm_tte9vgf6noic9 f2; uint64_t f3; uint32_t f4; uint8_t p5[4]; void * f5; uint64_t f6; } gm_tjsm4kosd5pev;
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tte9vgf6noic9) == 48 && _Alignof(gm_tte9vgf6noic9) == 8 && offsetof(gm_tte9vgf6noic9, f0) == 0 && offsetof(gm_tte9vgf6noic9, f1) == 8, "layout of `Installed`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tgfdnufetdnfc) == 48 && _Alignof(gm_tgfdnufetdnfc) == 8 && offsetof(gm_tgfdnufetdnfc, f0) == 0 && offsetof(gm_tgfdnufetdnfc, f1) == 8, "layout of `MemError!()`");
_Static_assert(sizeof(gm_tjsm4kosd5pev) == 112 && _Alignof(gm_tjsm4kosd5pev) == 8 && offsetof(gm_tjsm4kosd5pev, f0) == 0 && offsetof(gm_tjsm4kosd5pev, f1) == 8 && offsetof(gm_tjsm4kosd5pev, f2) == 32 && offsetof(gm_tjsm4kosd5pev, f3) == 80 && offsetof(gm_tjsm4kosd5pev, f4) == 88 && offsetof(gm_tjsm4kosd5pev, f5) == 96 && offsetof(gm_tjsm4kosd5pev, f6) == 104, "layout of `CtxNode`");
struct gm_fr_call_clause_bjbs6l1 {
    gm_frame h;
    gm_tjsm4kosd5pev l6; /* node */
    gm_trqfvmjtt4sij l21; /* all */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_call_function_u7ovth6 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_call_missing_5ejgl53 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_call_value_fm9m0cc {
    gm_frame h;
    gm_tddtjfggdat0d l7; /* sig */
    gm_tjsm4kosd5pev l16; /* node */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_tq06jtm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_2p7d2kv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8othtri_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_end_frame_cetr4jh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_enter_maqkh2o {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_extend_5tfshb9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_info_hocist4 {
    gm_frame h;
    void * ret;
};
struct gm_fr_format_46el73v {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5mel2nn {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jr8ev5r {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_k34396d {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_la70vrc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ms0imbl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rlkig7n {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tppjqmg {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_frame_of_dgl92on {
    gm_frame h;
    void * ret;
};
struct gm_fr_func_ene90ss {
    gm_frame h;
    void * ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_handler_decl_m66mgr8 {
    gm_frame h;
    void * ret;
};
struct gm_fr_handler_lit_b02boff {
    gm_frame h;
    void * ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_fufbgf7 {
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
struct gm_fr_new_frame_vgm56mt {
    gm_frame h;
    void * ret;
};
struct gm_fr_optional_rh6okj3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_perform_4kn6797 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_perform_native_2ppar83 {
    gm_frame h;
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_push_rluch9r {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t8up8j4_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_regrow_bslikk9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_u824ov8_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_release_jn5kqdi {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
__attribute__((aligned(4))) __attribute__((noinline)) void * gm_f_alloc_frame_31ks03n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_alloc_mem_0vu6kcs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_t9qkh0rsooc8d a4, uint8_t a5);
__attribute__((aligned(4))) void gm_f_call_clause_bjbs6l1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) void gm_f_call_function_u7ovth6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, gm_b16_8 a3, void * a4, uint32_t a5, const void * a6, uint8_t a7, gm_b16_8 a8);
__attribute__((aligned(4))) void gm_f_call_missing_5ejgl53(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_call_value_fm9m0cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_ctl_perform_kobk825(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5);
inline __attribute__((aligned(4))) void gm_f_deinit_tq06jtm(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2p7d2kv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8othtri_2(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b0_1 gm_f_end_frame_cetr4jh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b0_1 gm_f_end_frame_slow_mnbcrps(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_enter_maqkh2o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, void * a3, gm_b16_8 a4, void * a5, uint32_t a6, const void * a7, uint8_t a8, gm_b16_8 a9);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_5tfshb9(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint32_t gm_f_find_ctx_1elkgc2(void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_fn_from_addr_7a6bpqd(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_fn_info_hocist4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5mel2nn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fjqj2pj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_jr8ev5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_k34396d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_la70vrc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_format_ms0imbl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ncqd055(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_rlkig7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_tppjqmg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vepf78n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_frame_of_dgl92on(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b0_1 gm_f_grow_mem_buf_930r21a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2);
inline __attribute__((aligned(4))) void * gm_f_handler_decl_m66mgr8(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_handler_lit_b02boff(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_host_write_r26no87(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_init_9fuf99v(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_ctl_oclllo3(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_local_addr_o51qq3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) __attribute__((noinline)) void gm_f_local_alloc_qm9q4fo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) __attribute__((noinline)) void * gm_f_make_fn_info_4dk91jl(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_csc7lc6jlkqkuq gm_f_mark_kpppm1j(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_new_frame_vgm56mt(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_perform_4kn6797(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, void * a3, uint32_t a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) void gm_f_perform_native_2ppar83(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_pop_ctx_jivp36j(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_push_ctx_1rv0jgf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, const void * a3, uint64_t a4, uint32_t a5, void * a6);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t8up8j4_2(void * a0, gm_csedogq9e4rp5r a1);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_read_string_ttuk4gr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bslikk9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_u824ov8_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_release_jn5kqdi(void * a0, gm_csc7lc6jlkqkuq a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_tick_6bsfu3n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
GM_LONG_FN __attribute__((aligned(4))) void gm_f_vm_run_j1mlbuk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_vm_run_j1mlbuk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tbl8il92l97ut a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint64_t gm_f_word_o2rcrhs(void * a0, gm_b16_8 a1, uint64_t a2);
extern uint8_t gm_c1id0n0l1pf[8] __attribute__((aligned(8)));
extern uint8_t gm_cdps8qsftpu[4] __attribute__((aligned(4)));
extern uint8_t gm_kh7445i0ed8[40] __attribute__((aligned(8)));
extern uint8_t gm_krt7450irj2[40] __attribute__((aligned(8)));
extern const uint8_t gm_s38rtd27u976k[15];
extern const uint8_t gm_s3p4vlg0qjpu0[46];
extern const uint8_t gm_s63e7t279sbjq[47];
extern const uint8_t gm_s9dudft75oi1c[33];
extern const uint8_t gm_sac01n562d8ve[59];
extern const uint8_t gm_sdgcpos3mblup[37];
extern const uint8_t gm_sg6iftc6c3tph[47];
extern const uint8_t gm_sijp8lh2g2ld5[34];
extern const uint8_t gm_slg8371oger0u[32];
extern const uint8_t gm_slk5snj328gko[45];
extern const uint8_t gm_slnkfiltk5b43[57];
extern const uint8_t gm_sq5khm5l62ens[37];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr82af5e87dso[54];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6c128h826nsg[];
extern const char gm_src6hq50r4rk7e6[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuj9anpi7p1mq[];
extern const gm_tmpl gm_tpl29pahnav2feq;
extern const gm_tmpl gm_tpl7o4acs2tt6pp;
extern const gm_tmpl gm_tpl9uslg4i3oo2s;
extern const gm_tmpl gm_tplae28pp9b49u1;
extern const gm_tmpl gm_tplav8bdvvt8rem;
extern const gm_tmpl gm_tplfvv1qe6os0le;
extern const gm_tmpl gm_tplj451rojbvr15;
extern const gm_tmpl gm_tplu76evoc63de2;
extern const gm_type gm_tyo86ks9qkvt;
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
inline __attribute__((aligned(4))) void gm_f_deinit_tq06jtm(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_tq06jtm F;
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
    gm_b16_8 t21 = gm_f_empty_slice_2p7d2kv();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_name_prpp0ip F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 216));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 216) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 283, 62);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t3) + (uintptr_t)t6 * 16));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8othtri_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8othtri_2 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b0_1 gm_f_end_frame_cetr4jh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_end_frame_cetr4jh F;
    void * gm_l0; /* m */
    void * gm_l1; /* fr */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    void * t2 = gm_l1;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 96) + 8));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    if (t4) {
        gm_l4 = ((uint8_t)1);
    } else {
        void * t5 = gm_l1;
        uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 136) + 8));
        uint8_t t7 = (t6 > ((uint64_t)0ULL));
        gm_l4 = t7;
    }
    uint8_t t8 = gm_l4;
    if (t8) {
        gm_l3 = ((uint8_t)1);
    } else {
        void * t9 = gm_l1;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 8));
        uint8_t t11 = (!(t10 == ((uint32_t)0ULL)));
        gm_l3 = t11;
    }
    uint8_t t12 = gm_l3;
    if (t12) {
        gm_l2 = ((uint8_t)1);
    } else {
        void * t13 = gm_l0;
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t13) + 504));
        uint8_t t15 = (t14 >= ((uint32_t)256ULL));
        gm_l2 = t15;
    }
    uint8_t t16 = gm_l2;
    if (t16) {
        void * t17 = gm_l0;
        void * t18 = gm_l1;
        gm_b0_1 t19 = gm_f_end_frame_slow_mnbcrps((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t20 = gm_l1;
    void * t21 = gm_l0;
    void * t22 = GM_LD(void *, (((uint8_t *)t21) + 496));
    GM_ST(void *, (((uint8_t *)t20) + 208), t22);
    void * t23 = gm_l0;
    void * t24 = gm_l1;
    GM_ST(void *, (((uint8_t *)t23) + 496), t24);
    void * t25 = gm_l0;
    void * t26 = gm_l0;
    uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t26) + 504));
    uint32_t r28; bool o29 = __builtin_add_overflow(t27, ((uint32_t)1ULL), &r28);
    if (o29) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 152, 5);
    uint32_t t30 = r28;
    GM_ST(uint32_t, (((uint8_t *)t25) + 504), t30);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_5tfshb9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_5tfshb9 F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_bslikk9(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 16);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 16)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_fn_info_hocist4(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_fn_info_hocist4 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    uint64_t gm_l2; /* i */
    void * gm_l3; /* x */
    GM_STACK_CHECK(&F, 158u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t4 = gm_l1;
    uint64_t t5 = (uint64_t)t4;
    uint64_t t6 = gm_f_us_cuv01fp(t5);
    gm_l2 = t6;
    uint64_t t7 = gm_l2;
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 680) + 8));
    uint8_t t10 = (t7 < t9);
    if (t10) {
        {
            {
                void * t11 = gm_l0;
                void * t12 = GM_LD(void *, (((uint8_t *)t11) + 680));
                uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)t11) + 680) + 8));
                uint64_t t14 = gm_l2;
                if ((uintptr_t)t14 >= (uintptr_t)(t13)) gm_trap_index((uintptr_t)t14, t13, gm_src6hq50r4rk7e6, 19, 17);
                uint8_t t15 = (uint8_t)(GM_LD(void *, (((uint8_t *)t12) + (uintptr_t)t14 * 8)) != 0);
                if (!t15) {
                    goto gm_lb2;
                }
                void * t16 = GM_LD(void *, (((uint8_t *)t12) + (uintptr_t)t14 * 8));
                gm_l3 = t16;
                void * t17 = gm_l3;
                F.ret = t17;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    void * t18 = gm_l0;
    uint32_t t19 = gm_l1;
    void * t20 = gm_f_make_fn_info_4dk91jl((void *)NULL, cx, NULL, t18, t19);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) void gm_f_format_5mel2nn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5mel2nn F;
    gm_tgbgti82mf879 gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sac01n562d8ve));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(58));
    gm_tgbgti82mf879 t3 = GM_LD(gm_tgbgti82mf879, ((uint8_t *)a0));
    gm_f_format_vepf78n((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl29pahnav2feq), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jr8ev5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_jr8ev5r F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sg6iftc6c3tph));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(46));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplu76evoc63de2), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_k34396d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_k34396d F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s63e7t279sbjq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9uslg4i3oo2s), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_la70vrc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_la70vrc F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t88rcucg1b7uk t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slk5snj328gko));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_f_format_ncqd055((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplj451rojbvr15), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ms0imbl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ms0imbl F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdgcpos3mblup));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfvv1qe6os0le), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_rlkig7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rlkig7n F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sijp8lh2g2ld5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplae28pp9b49u1), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_tppjqmg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tppjqmg F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9dudft75oi1c));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7o4acs2tt6pp), t3);
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
inline __attribute__((aligned(4))) void * gm_f_frame_of_dgl92on(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_frame_of_dgl92on F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* h */
    GM_STACK_CHECK_SHALLOW(&F, 132u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    uint64_t t2 = gm_l1;
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        gm_l2 = ((uint8_t)1);
    } else {
        uint64_t t4 = gm_l1;
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 416) + 8));
        uint64_t t7 = (uint64_t)t6;
        uint8_t t8 = (t4 > t7);
        gm_l2 = t8;
    }
    uint8_t t9 = gm_l2;
    if (t9) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t10 = gm_l0;
    void * t11 = GM_LD(void *, (((uint8_t *)t10) + 416));
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t10) + 416) + 8));
    uint64_t t13 = gm_l1;
    uint64_t r14; bool o15 = __builtin_sub_overflow(t13, ((uint64_t)1ULL), &r14);
    if (o15) gm_trap("integer overflow in `-`", gm_src6hq50r4rk7e6, 82, 30);
    uint64_t t16 = r14;
    uint64_t t17 = gm_f_us_cuv01fp(t16);
    if ((uintptr_t)t17 >= (uintptr_t)(t12)) gm_trap_index((uintptr_t)t17, t12, gm_src6hq50r4rk7e6, 82, 12);
    void * t18 = GM_LD(void *, (((uint8_t *)t11) + (uintptr_t)t17 * 8));
    F.ret = t18;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_func_ene90ss(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_func_ene90ss F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    uint32_t t4 = gm_l1;
    void * t5 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) void * gm_f_handler_decl_m66mgr8(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_handler_decl_m66mgr8 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 544));
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 544) + 8));
    uint32_t t6 = gm_l1;
    uint64_t t7 = (uint64_t)t6;
    if ((uintptr_t)t7 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t7, t5, gm_srcuj9anpi7p1mq, 132, 83);
    void * t8 = (void *)(((uint8_t *)t4) + (uintptr_t)t7 * 64);
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_handler_lit_b02boff(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_handler_lit_b02boff F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* h */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 504));
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 504) + 8));
    uint32_t t6 = gm_l1;
    uint64_t t7 = (uint64_t)t6;
    if ((uintptr_t)t7 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t7, t5, gm_srcuj9anpi7p1mq, 129, 76);
    void * t8 = (void *)(((uint8_t *)t4) + (uintptr_t)t7 * 40);
    F.ret = t8;
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
inline __attribute__((aligned(4))) void gm_f_list_fufbgf7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fufbgf7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_2p7d2kv();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_new_frame_vgm56mt(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, gm_b16_8 a3, void * a4, gm_b16_8 a5) {
    struct gm_fr_new_frame_vgm56mt F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* fn_id */
    void * gm_l2; /* info */
    gm_b16_8 gm_l3; /* ret */
    void * gm_l4; /* env */
    gm_b16_8 gm_l5; /* store */
    uint64_t gm_l6; /* n */
    void * gm_l7; /* fr */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    void * t2 = gm_l2;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 120) + 8));
    gm_l6 = t3;
    void * t4 = gm_l0;
    uint8_t t5 = (uint8_t)(GM_LD(void *, (((uint8_t *)t4) + 496)) != 0);
    if (t5) {
        void * t6 = GM_LD(void *, (((uint8_t *)t4) + 496));
        gm_l7 = t6;
    } else {
        void * t7 = gm_l0;
        void * t8 = gm_l2;
        gm_b16_8 t9 = gm_l3;
        void * t10 = gm_f_alloc_frame_31ks03n((void *)NULL, cx, NULL, t7, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t10;
    }
    void * t11 = gm_l0;
    void * t12 = gm_l7;
    void * t13 = GM_LD(void *, (((uint8_t *)t12) + 208));
    GM_ST(void *, (((uint8_t *)t11) + 496), t13);
    void * t14 = gm_l0;
    void * t15 = gm_l0;
    uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)t15) + 504));
    uint32_t r17; bool o18 = __builtin_sub_overflow(t16, ((uint32_t)1ULL), &r17);
    if (o18) gm_trap("integer overflow in `-`", gm_src6hq50r4rk7e6, 93, 5);
    uint32_t t19 = r17;
    GM_ST(uint32_t, (((uint8_t *)t14) + 504), t19);
    void * t20 = gm_l7;
    uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t20) + 80) + 8));
    uint64_t t22 = gm_l6;
    uint8_t t23 = (t21 < t22);
    if (t23) {
        void * t24 = gm_l0;
        void * t25 = gm_l7;
        uint64_t t26 = gm_l6;
        gm_b0_1 t27 = gm_f_grow_mem_buf_930r21a((void *)NULL, cx, NULL, t24, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t28 = gm_l7;
    void * t29 = gm_l7;
    void * t30 = GM_LD(void *, (((uint8_t *)t29) + 80));
    uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)t29) + 80) + 8));
    uint64_t t32 = gm_l6;
    uint64_t t33 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t34 = (uintptr_t)t32;
    if (t33 > t34 || t34 > (uintptr_t)t31) gm_trap_slice(t33, t34, t31, gm_src6hq50r4rk7e6, 96, 14);
    uint64_t t35 = ((uintptr_t)t32) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t36;
    GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t30 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35));
    GM_ST(gm_b16_8, (((uint8_t *)t28) + 64), t36);
    void * t37 = gm_l7;
    void * t38 = gm_l0;
    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 736));
    GM_ST(uint64_t, ((uint8_t *)t37), t39);
    void * t40 = gm_l7;
    uint32_t t41 = gm_l1;
    GM_ST(uint32_t, (((uint8_t *)t40) + 12), t41);
    void * t42 = gm_l7;
    void * t43 = gm_l2;
    GM_ST(void *, (((uint8_t *)t42) + 16), t43);
    void * t44 = gm_l7;
    gm_b16_8 t45 = gm_l3;
    GM_ST(gm_b16_8, (((uint8_t *)t44) + 24), t45);
    void * t46 = gm_l7;
    void * t47 = gm_l4;
    GM_ST(void *, (((uint8_t *)t46) + 40), t47);
    void * t48 = gm_l7;
    gm_b16_8 t49 = gm_l5;
    GM_ST(gm_b16_8, (((uint8_t *)t48) + 48), t49);
    void * t50 = gm_l7;
    gm_t5lf9i3n1r3co t51;
    memset(&t51, 0, sizeof t51);
    GM_ST(gm_t5lf9i3n1r3co, (((uint8_t *)t50) + 176), t51);
    void * t52 = gm_l7;
    GM_ST(uint8_t, (((uint8_t *)t52) + 200), ((uint8_t)0));
    void * t53 = gm_l0;
    void * t54 = gm_l0;
    uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 736));
    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
    if (o57) gm_trap("integer overflow in `+`", gm_src6hq50r4rk7e6, 105, 5);
    uint64_t t58 = r56;
    GM_ST(uint64_t, (((uint8_t *)t53) + 736), t58);
    void * t59 = gm_l7;
    GM_ST(uint32_t, (((uint8_t *)t59) + 8), ((uint32_t)0ULL));
    void * t60 = gm_l7;
    F.ret = t60;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_optional_rh6okj3 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* inner */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 8), t3);
    gm_t88rcucg1b7uk t5 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t4);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rluch9r(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rluch9r F;
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
        gm_b16_8 t9 = gm_f_empty_slice_2p7d2kv();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_bslikk9(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t8up8j4_2(void * a0, gm_csedogq9e4rp5r a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_t8up8j4_2 F;
    void * gm_l0; /* l */
    gm_t9d4sf0vqn7dt gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t9d4sf0vqn7dt, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_8othtri_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_u824ov8_2(t8, ((uint64_t)1ULL), t10);
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
    gm_t9d4sf0vqn7dt t18 = gm_l1;
    GM_ST(gm_t9d4sf0vqn7dt, (((uint8_t *)t16) + (uintptr_t)t17 * 16), t18);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_bslikk9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_bslikk9 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_u824ov8_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_u824ov8_2 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)4ULL));
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
__attribute__((aligned(4))) void gm_f_call_clause_bjbs6l1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_call_clause_bjbs6l1 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint32_t gm_l2; /* n */
    uint32_t gm_l3; /* op */
    gm_b16_8 gm_l4; /* args */
    gm_b16_8 gm_l5; /* dst */
    gm_t5lf9i3n1r3co gm_l7; /* installing */
    void * gm_l8; /* fr */
    uint32_t gm_l9; /* h */
    void * gm_l10; /* env */
    uint32_t gm_l11; /* cx */
    uint32_t gm_l12; /* clause */
    gm_b16_8 gm_l13; /* decls */
    gm_b16_8 gm_l14; /* value */
    uint32_t gm_l15; /* outer */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint32_t gm_l18; /* d */
    void * gm_l19; /* info */
    uint32_t gm_l20; /* clause */
    gm_t9qkh0rsooc8d gm_l22; /* kind */
    GM_STACK_CHECK(&F, 1021u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    uint8_t gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    {
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 592));
        uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 592) + 8));
        uint32_t t17 = gm_l2;
        uint64_t t18 = (uint64_t)t17;
        uint64_t t19 = gm_f_us_cuv01fp(t18);
        if ((uintptr_t)t19 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t19, t16, gm_src6c128h826nsg, 109, 13);
        memmove(((uint8_t *)&F.l6), (((uint8_t *)t15) + (uintptr_t)t19 * 112), 112);
    }
    {
        memset(&gm_l7, 0, sizeof gm_l7);
    }
    {
        {
            uint8_t t20 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l6) + 96)) != 0);
            if (!t20) {
                goto gm_lb2;
            }
            void * t21 = GM_LD(void *, (((uint8_t *)&F.l6) + 96));
            gm_l8 = t21;
            void * t22 = gm_l8;
            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 88));
            gm_tbl8il92l97ut t24;
            GM_ST(uint32_t, (uint8_t *)&t24 + 12, 0);
            GM_ST(void *, ((uint8_t *)&t24 + 0), t22);
            GM_ST(uint32_t, ((uint8_t *)&t24 + 8), t23);
            GM_ST(uint8_t, (uint8_t *)&gm_l7 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l7 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l7 + 20, 0);
            GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&gm_l7), t24);
            ((uint8_t *)&gm_l7)[16] = 1;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 32) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)0ULL));
            if (!t26) {
                goto gm_lb4;
            }
            uint32_t t27 = GM_LD(uint32_t, ((((uint8_t *)&F.l6) + 32) + 8));
            gm_l9 = t27;
            void * t28 = GM_LD(void *, (((((uint8_t *)&F.l6) + 32) + 8) + 8));
            gm_l10 = t28;
            uint32_t t29 = GM_LD(uint32_t, (((((uint8_t *)&F.l6) + 32) + 8) + 16));
            gm_l11 = t29;
            void * t30 = gm_l0;
            void * t31 = GM_LD(void *, ((uint8_t *)t30));
            uint32_t t32 = gm_l9;
            void * t33 = gm_f_handler_lit_b02boff(t31, t32);
            void * t34 = GM_LD(void *, (((uint8_t *)t33) + 24));
            uint64_t t35 = GM_LD(uint64_t, ((((uint8_t *)t33) + 24) + 8));
            uint32_t t36 = gm_l3;
            uint64_t t37 = (uint64_t)t36;
            uint64_t t38 = gm_f_us_cuv01fp(t37);
            if ((uintptr_t)t38 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t38, t35, gm_src6c128h826nsg, 114, 23);
            uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)t34) + (uintptr_t)t38 * 4));
            gm_l12 = t39;
            {
                void * t40 = gm_l0;
                gm_tl874t2atnlh3 t41 = gm_l1;
                uint32_t t42 = gm_l12;
                gm_b16_8 t43 = gm_l4;
                void * t44 = gm_l10;
                uint32_t t45 = gm_l11;
                gm_b16_8 t46 = gm_l5;
                gm_t5lf9i3n1r3co t47 = GM_LD(gm_t5lf9i3n1r3co, ((uint8_t *)&gm_l7));
                gm_f_call_function_u7ovth6((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t40, t41, t42, t43, t44, t45, &t47, ((uint8_t)1), t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
        }
gm_lb4:;
        {
            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 32) + 0));
            uint8_t t50 = (uint8_t)(t49 == ((uint8_t)1ULL));
            if (!t50) {
                goto gm_lb5;
            }
            gm_b16_8 t51 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 32) + 8));
            gm_l13 = t51;
            gm_b16_8 t52 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l6) + 32) + 8) + 16));
            gm_l14 = t52;
            uint32_t t53 = GM_LD(uint32_t, (((((uint8_t *)&F.l6) + 32) + 8) + 32));
            gm_l15 = t53;
            gm_b16_8 t54 = gm_l13;
            gm_l16 = t54;
            gm_l17 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t56 = gm_l17;
                    uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint8_t t58 = (t56 < t57);
                    if (!t58) {
                        goto gm_lb6;
                    }
                    {
                        void * t60 = GM_LD(void *, ((uint8_t *)&gm_l16));
                        uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                        uint64_t t62 = gm_l17;
                        {
                            {
                                uint32_t t63 = GM_LD(uint32_t, (((uint8_t *)t60) + (uintptr_t)t62 * 4));
                                gm_l18 = t63;
                                {
                                    void * t65 = gm_l0;
                                    void * t66 = GM_LD(void *, ((uint8_t *)t65));
                                    uint32_t t67 = gm_l18;
                                    void * t68 = gm_f_handler_decl_m66mgr8(t66, t67);
                                    gm_l19 = t68;
                                    void * t69 = gm_l19;
                                    uint32_t t70 = GM_LD(uint32_t, ((uint8_t *)t69));
                                    uint32_t t71 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
                                    uint8_t t72 = (!(t70 == t71));
                                    if (t72) {
                                        gm_l23 = ((uint8_t)1);
                                    } else {
                                        void * t73 = gm_l19;
                                        void * t74 = (void *)((uint8_t *)t73);
                                        void * t75 = (void *)(((uint8_t *)&F.l6) + 8);
                                        int32_t t76 = gm_f_cmp_ref_8ntrqll(t74, t75);
                                        uint8_t t77 = (!(t76 == ((int32_t)0LL)));
                                        gm_l23 = t77;
                                    }
                                    uint8_t t78 = gm_l23;
                                    if (t78) {
                                        goto gm_lb8;
                                    }
                                    void * t79 = gm_l19;
                                    void * t80 = GM_LD(void *, (((uint8_t *)t79) + 48));
                                    uint64_t t81 = GM_LD(uint64_t, ((((uint8_t *)t79) + 48) + 8));
                                    uint32_t t82 = gm_l3;
                                    uint64_t t83 = (uint64_t)t82;
                                    uint64_t t84 = gm_f_us_cuv01fp(t83);
                                    if ((uintptr_t)t84 >= (uintptr_t)(t81)) gm_trap_index((uintptr_t)t84, t81, gm_src6c128h826nsg, 122, 27);
                                    uint32_t t85 = GM_LD(uint32_t, (((uint8_t *)t80) + (uintptr_t)t84 * 4));
                                    gm_l20 = t85;
                                    {
                                        gm_trqfvmjtt4sij t86;
                                        gm_f_list_fufbgf7((void *)&t86, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_ll59;
                                        }
                                        F.l21 = t86;
                                    }
                                    void * t87 = (void *)((uint8_t *)&F.l21);
                                    gm_b16_8 t88 = gm_l14;
                                    gm_tftb1odc739t5 t89 = gm_f_push_rluch9r(t87, t88);
                                    gm_l24 = t89;
                                    uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                    if (!t90) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src6c128h826nsg, 125, 17);
                                    void * t91 = (void *)((uint8_t *)&F.l21);
                                    gm_b16_8 t92 = gm_l4;
                                    gm_tftb1odc739t5 t93 = gm_f_extend_5tfshb9(t91, t92);
                                    gm_l25 = t93;
                                    uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                    if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_src6c128h826nsg, 126, 17);
                                    {
                                        void * t95 = gm_l0;
                                        gm_tl874t2atnlh3 t96 = gm_l1;
                                        uint32_t t97 = gm_l20;
                                        gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
                                        gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
                                        uint32_t t100 = gm_l15;
                                        gm_b16_8 t101 = gm_l5;
                                        gm_t5lf9i3n1r3co t102 = GM_LD(gm_t5lf9i3n1r3co, ((uint8_t *)&gm_l7));
                                        gm_f_call_function_u7ovth6((void *)&F.ret, (void *)NULL, cx, NULL, t95, t96, t97, t99, ((void *)0), t100, &t102, ((uint8_t)0), t101);
                                        if (gm_unw.kind) {
                                            goto gm_ld64_1;
                                        }
                                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                                        goto gm_ld64_1;
                                    }
                                }
                                {
                                    void * t103 = (void *)((uint8_t *)&F.l21);
                                    gm_f_deinit_tq06jtm(t103);
                                }
                                goto gm_ls104;
gm_ld64_1:;
                                {
                                    gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                                    void * t105 = (void *)((uint8_t *)&F.l21);
                                    gm_f_deinit_tq06jtm(t105);
                                    gm_unw = gm_saved;
                                }
                                goto gm_ll59;
gm_ls104:;
gm_lb11:;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 119, 13);
                        }
gm_lb9:;
                    }
                    goto gm_ls106;
gm_ll59:;
                    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741831) { gm_unw.kind = GM_NONE; goto gm_lb8; }
                    goto gm_ll55;
gm_ls106:;
gm_lb8:;
                    uint64_t t107 = gm_l17;
                    uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                    uint64_t t110 = r108;
                    gm_l17 = t110;
                }
            }
            goto gm_ls111;
gm_ll55:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741829) { gm_unw.kind = GM_NONE; goto gm_lb6; }
            goto gm_ll48;
gm_ls111:;
gm_lb6:;
            {
                void * t112 = gm_l0;
                gm_tl874t2atnlh3 t113 = gm_l1;
                gm_b16_8 t114;
                GM_ST(void *, (uint8_t *)&t114, (void *)(gm_sr82af5e87dso));
                GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(53));
                gm_t76123gfet73g t115;
                gm_f_trap_qh84l9d((void *)&t115, (void *)NULL, cx, NULL, t112, t113, t114);
                if (gm_unw.kind) {
                    goto gm_ll48;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t115);
                goto gm_fdone;
            }
        }
        goto gm_ls116;
gm_ll48:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741828) { gm_unw.kind = GM_NONE; goto gm_lb5; }
        goto gm_lad;
gm_ls116:;
gm_lb5:;
        {
            uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)&F.l6) + 32) + 0));
            uint8_t t118 = (uint8_t)(t117 == ((uint8_t)2ULL));
            if (!t118) {
                goto gm_lb12;
            }
            gm_t9qkh0rsooc8d t119 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&F.l6) + 32) + 8));
            gm_l22 = t119;
            void * t120 = gm_l0;
            gm_tl874t2atnlh3 t121 = gm_l1;
            gm_t9qkh0rsooc8d t122 = gm_l22;
            uint32_t t123 = gm_l3;
            gm_b16_8 t124 = gm_l4;
            gm_b16_8 t125 = gm_l5;
            gm_f_perform_native_2ppar83((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t120, t121, t122, t123, t124, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 112, 5);
    }
gm_lb3:;
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
__attribute__((aligned(4))) void gm_f_call_function_u7ovth6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, gm_b16_8 a3, void * a4, uint32_t a5, const void * a6, uint8_t a7, gm_b16_8 a8) {
    struct gm_fr_call_function_u7ovth6 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint32_t gm_l2; /* f */
    gm_b16_8 gm_l3; /* args */
    void * gm_l4; /* env */
    uint32_t gm_l5; /* cx */
    gm_t5lf9i3n1r3co gm_l6; /* installing */
    uint8_t gm_l7; /* is_clause_lit */
    gm_b16_8 gm_l8; /* dst */
    void * gm_l9; /* info */
    gm_t2lqu8jsj511d gm_l10; /* mk */
    gm_b16_8 gm_l11; /* store */
    uint64_t gm_l12; /* i */
    gm_b16_8 gm_l13; /* x */
    uint64_t gm_l14; /* off */
    gm_tcfa6l7reoosn gm_l15; /* r */
    GM_STACK_CHECK(&F, 1146u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    memcpy(&gm_l6, a6, sizeof gm_l6);
    gm_l7 = a7;
    gm_l8 = a8;
    uint8_t gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tabb710d9mq2e gm_l18;
    void * t8 = gm_l0;
    uint32_t t9 = gm_l2;
    void * t10 = gm_f_fn_info_hocist4((void *)NULL, cx, NULL, t8, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t10;
    void * t11 = gm_l9;
    {
        {
            uint8_t t12 = (uint8_t)(GM_LD(void *, (((uint8_t *)t11) + 56)) != 0);
            if (t12) {
                goto gm_lb2;
            }
            gm_l16 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l16 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 39, 8);
    }
gm_lb1:;
    uint8_t t13 = gm_l16;
    if (t13) {
        void * t14 = gm_l0;
        gm_tl874t2atnlh3 t15 = gm_l1;
        uint32_t t16 = gm_l2;
        gm_f_call_missing_5ejgl53((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t14, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    void * t18 = gm_l9;
    uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)t18) + 8) + 8));
    uint8_t t20 = (!(t17 == t19));
    if (t20) {
        void * t21 = gm_l0;
        gm_tl874t2atnlh3 t22 = gm_l1;
        void * t23 = gm_l0;
        uint32_t t24 = gm_l2;
        void * t25 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)t25));
        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        void * t28 = gm_l9;
        uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)t28) + 8) + 8));
        gm_tgbgti82mf879 t30;
        GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t26);
        GM_ST(uint64_t, ((uint8_t *)&t30 + 16), t27);
        GM_ST(uint64_t, ((uint8_t *)&t30 + 24), t29);
        gm_f_format_5mel2nn((void *)&gm_l17, (void *)NULL, cx, NULL, &t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
        if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_src6c128h826nsg, 40, 63);
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_t76123gfet73g t34;
        gm_f_trap_qh84l9d((void *)&t34, (void *)NULL, cx, NULL, t21, t22, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t34);
        goto gm_fdone;
    }
    void * t35 = gm_l9;
    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 144));
    uint64_t t37 = GM_LD(uint64_t, ((uint8_t *)gm_c1id0n0l1pf));
    uint8_t t38 = (t36 > t37);
    if (t38) {
        void * t39 = gm_l0;
        gm_tl874t2atnlh3 t40 = gm_l1;
        void * t41 = gm_l0;
        uint32_t t42 = gm_l2;
        void * t43 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t41, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)t43));
        void * t45 = gm_l9;
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 144));
        gm_t3877pk7lv748 t47;
        GM_ST(gm_b16_8, ((uint8_t *)&t47 + 0), t44);
        GM_ST(uint64_t, ((uint8_t *)&t47 + 16), t46);
        gm_f_format_jr8ev5r((void *)&gm_l18, (void *)NULL, cx, NULL, &t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
        if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src6c128h826nsg, 41, 58);
        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        gm_t76123gfet73g t51;
        gm_f_trap_qh84l9d((void *)&t51, (void *)NULL, cx, NULL, t39, t40, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t51);
        goto gm_fdone;
    }
    void * t52 = gm_l0;
    gm_t2lqu8jsj511d t53 = GM_CONV(gm_t2lqu8jsj511d, gm_f_mark_kpppm1j(t52));
    gm_l10 = t53;
    void * t54 = gm_l0;
    void * t55 = gm_l9;
    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)t55) + 64));
    gm_b16_8 t57 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t54, t56);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t57;
    gm_l12 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t58 = gm_l12;
            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t60 = (t58 < t59);
            if (!t60) {
                goto gm_lb4;
            }
            {
                void * t61 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t63 = gm_l12;
                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)t61) + (uintptr_t)t63 * 16));
                gm_l13 = t64;
                void * t65 = gm_l9;
                void * t66 = GM_LD(void *, (((uint8_t *)t65) + 72));
                uint64_t t67 = GM_LD(uint64_t, ((((uint8_t *)t65) + 72) + 8));
                void * t68 = gm_l9;
                void * t69 = GM_LD(void *, (((uint8_t *)t68) + 8));
                uint64_t t70 = GM_LD(uint64_t, ((((uint8_t *)t68) + 8) + 8));
                uint64_t t71 = gm_l12;
                if ((uintptr_t)t71 >= (uintptr_t)(t70)) gm_trap_index((uintptr_t)t71, t70, gm_src6c128h826nsg, 47, 35);
                uint32_t t72 = GM_LD(uint32_t, (((uint8_t *)t69) + (uintptr_t)t71 * 4));
                uint64_t t73 = (uint64_t)t72;
                uint64_t t74 = gm_f_us_cuv01fp(t73);
                if ((uintptr_t)t74 >= (uintptr_t)(t67)) gm_trap_index((uintptr_t)t74, t67, gm_src6c128h826nsg, 47, 19);
                uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)t66) + (uintptr_t)t74 * 8));
                uint64_t t76 = gm_f_us_cuv01fp(t75);
                gm_l14 = t76;
                void * t77 = GM_LD(void *, ((uint8_t *)&gm_l11));
                uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint64_t t79 = gm_l14;
                uint64_t t80 = gm_l14;
                uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                uint64_t r82; bool o83 = __builtin_add_overflow(t80, t81, &r82);
                if (o83) gm_trap("integer overflow in `+`", gm_src6c128h826nsg, 48, 20);
                uint64_t t84 = r82;
                uint64_t t85 = (uintptr_t)t79;
                uint64_t t86 = (uintptr_t)t84;
                if (t85 > t86 || t86 > (uintptr_t)t78) gm_trap_slice(t85, t86, t78, gm_src6c128h826nsg, 48, 9);
                uint64_t t87 = ((uintptr_t)t84) - (uintptr_t)t79;
                gm_b16_8 t88;
                GM_ST(void *, (uint8_t *)&t88, (void *)(((uint8_t *)t77 + (uintptr_t)t79 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(t87));
                gm_b16_8 t89 = gm_l13;
                gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                gm_f_copy_from_f7j6p05(t88, t90);
                uint64_t t91 = gm_l12;
                uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
                if (o93) gm_trap("integer overflow in `+`", gm_src6c128h826nsg, 49, 9);
                uint64_t t94 = r92;
                gm_l12 = t94;
            }
gm_lb6:;
        }
    }
gm_lb4:;
    {
        void * t95 = gm_l0;
        gm_tl874t2atnlh3 t96 = gm_l1;
        uint32_t t97 = gm_l2;
        void * t98 = gm_l9;
        gm_b16_8 t99 = gm_l11;
        void * t100 = gm_l4;
        uint32_t t101 = gm_l5;
        uint8_t t102 = gm_l7;
        gm_b16_8 t103 = gm_l8;
        gm_t5lf9i3n1r3co t104 = GM_LD(gm_t5lf9i3n1r3co, ((uint8_t *)&gm_l6));
        gm_f_enter_maqkh2o((void *)&gm_l15, (void *)NULL, cx, NULL, t95, t96, t97, t98, t99, t100, t101, &t104, t102, t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t105 = gm_l0;
    gm_t2lqu8jsj511d t106 = gm_l10;
    gm_f_release_jn5kqdi(t105, GM_CONV(gm_csc7lc6jlkqkuq, t106));
    {
        memmove(gm_out, ((uint8_t *)&gm_l15), 32);
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
inline __attribute__((aligned(4))) void gm_f_call_missing_5ejgl53(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2) {
    struct gm_fr_call_missing_5ejgl53 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint32_t gm_l2; /* f */
    void * gm_l3; /* def */
    GM_STACK_CHECK(&F, 346u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l4;
    gm_tabb710d9mq2e gm_l5;
    gm_tabb710d9mq2e gm_l6;
    void * t5 = gm_l0;
    uint32_t t6 = gm_l2;
    void * t7 = gm_f_func_ene90ss((void *)NULL, cx, NULL, t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t7;
    void * t8 = gm_l3;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 96) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)3ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l4 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 7, 8);
    }
gm_lb1:;
    uint8_t t11 = gm_l4;
    if (t11) {
        void * t12 = gm_l0;
        gm_tl874t2atnlh3 t13 = gm_l1;
        void * t14 = gm_l3;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)t14));
        gm_tnpifv20irnph t16;
        GM_ST(gm_b16_8, ((uint8_t *)&t16 + 0), t15);
        gm_f_format_rlkig7n((void *)&gm_l5, (void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
        if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_src6c128h826nsg, 7, 62);
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        gm_t76123gfet73g t20;
        gm_f_unsupported_70vvk1e((void *)&t20, (void *)NULL, cx, NULL, t12, t13, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t20);
        goto gm_fdone;
    }
    {
        void * t21 = gm_l0;
        gm_tl874t2atnlh3 t22 = gm_l1;
        void * t23 = gm_l3;
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)t23));
        gm_tnpifv20irnph t25;
        GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
        gm_f_format_tppjqmg((void *)&gm_l6, (void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_src6c128h826nsg, 8, 26);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        gm_t76123gfet73g t29;
        gm_f_trap_qh84l9d((void *)&t29, (void *)NULL, cx, NULL, t21, t22, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t29);
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
__attribute__((aligned(4))) void gm_f_call_value_fm9m0cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_call_value_fm9m0cc F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* cx */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* cv */
    gm_t88rcucg1b7uk gm_l4; /* cty */
    gm_b16_8 gm_l5; /* args */
    gm_b16_8 gm_l6; /* dst */
    uint32_t gm_l8; /* f */
    void * gm_l9; /* env */
    uint32_t gm_l10; /* ctx */
    uint32_t gm_l11; /* base */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_tiilmfd16v3fc gm_l14; /* eff */
    uint32_t gm_l15; /* k */
    gm_tcfa6l7reoosn gm_l17; /* r */
    uint64_t gm_l18; /* addr */
    uint32_t gm_l19; /* f */
    GM_STACK_CHECK(&F, 1100u);
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
    gm_t53at80pfdus3 gm_l20;
    gm_t9805qtonbsag gm_l21;
    gm_t9805qtonbsag gm_l22;
    gm_tabb710d9mq2e gm_l23;
    {
        {
            {
                void * t9 = gm_l0;
                gm_t88rcucg1b7uk t10 = gm_l4;
                gm_f_kind_htv6093((void *)&gm_l20, t9, GM_CONV(gm_csguankko4lmhd, t10));
            }
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)15ULL));
            if (!t12) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&F.l7), (((uint8_t *)&gm_l20) + 8), 48);
            }
            void * t13 = gm_l0;
            void * t14 = gm_l0;
            gm_b16_8 t15 = gm_l3;
            uint64_t t16 = gm_f_word_o2rcrhs(t14, t15, ((uint64_t)0ULL));
            gm_t9805qtonbsag t17 = gm_f_fn_from_addr_7a6bpqd(t13, t16);
            gm_l21 = t17;
            uint8_t t18 = (uint8_t)((((uint8_t *)&gm_l21))[4] != 0);
            if (t18) {
                uint32_t t19 = GM_LD(uint32_t, ((uint8_t *)&gm_l21));
                gm_l8 = t19;
            } else {
                void * t20 = gm_l0;
                gm_tl874t2atnlh3 t21 = gm_l2;
                gm_b16_8 t22;
                GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s3p4vlg0qjpu0));
                GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(45));
                gm_t76123gfet73g t23;
                gm_f_trap_qh84l9d((void *)&t23, (void *)NULL, cx, NULL, t20, t21, t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t23);
                goto gm_fdone;
            }
            void * t24 = gm_l0;
            void * t25 = gm_l0;
            gm_b16_8 t26 = gm_l3;
            uint64_t t27 = gm_f_word_o2rcrhs(t25, t26, ((uint64_t)1ULL));
            void * t28 = gm_f_frame_of_dgl92on(t24, t27);
            gm_l9 = t28;
            void * t29 = gm_l0;
            gm_b16_8 t30 = gm_l3;
            uint64_t t31 = gm_f_word_o2rcrhs(t29, t30, ((uint64_t)2ULL));
            uint32_t t32 = (uint32_t)t31;
            gm_l10 = t32;
            uint32_t t33 = gm_l10;
            gm_l11 = t33;
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 24));
            gm_l12 = t34;
            gm_l13 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t35 = gm_l13;
                    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                    uint8_t t37 = (t35 < t36);
                    if (!t37) {
                        goto gm_lb3;
                    }
                    {
                        void * t38 = GM_LD(void *, ((uint8_t *)&gm_l12));
                        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                        uint64_t t40 = gm_l13;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&gm_l14), (((uint8_t *)t38) + (uintptr_t)t40 * 24), 24);
                                }
                                void * t41 = gm_l0;
                                uint32_t t42 = gm_l1;
                                void * t43 = (void *)((uint8_t *)&gm_l14);
                                uint32_t t44 = gm_f_find_ctx_1elkgc2(t41, t42, t43);
                                gm_l15 = t44;
                                uint32_t t45 = gm_l15;
                                uint8_t t46 = ((t45 == ((uint32_t)0ULL)));
                                if (t46) {
                                    goto gm_lb5;
                                }
                                {
                                    void * t47 = gm_l0;
                                    void * t48 = GM_LD(void *, (((uint8_t *)t47) + 592));
                                    uint64_t t49 = GM_LD(uint64_t, ((((uint8_t *)t47) + 592) + 8));
                                    uint32_t t50 = gm_l15;
                                    uint64_t t51 = (uint64_t)t50;
                                    uint64_t t52 = gm_f_us_cuv01fp(t51);
                                    if ((uintptr_t)t52 >= (uintptr_t)(t49)) gm_trap_index((uintptr_t)t52, t49, gm_src6c128h826nsg, 23, 21);
                                    memmove(((uint8_t *)&F.l16), (((uint8_t *)t48) + (uintptr_t)t52 * 112), 112);
                                }
                                {
                                    void * t53 = gm_l0;
                                    uint32_t t54 = gm_l10;
                                    uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 80));
                                    uint32_t t56 = GM_LD(uint32_t, (((uint8_t *)&F.l16) + 88));
                                    void * t57 = GM_LD(void *, (((uint8_t *)&F.l16) + 96));
                                    uint32_t t58 = gm_f_push_ctx_1rv0jgf((void *)NULL, cx, NULL, t53, t54, &(*(gm_tiilmfd16v3fc *)(((uint8_t *)&F.l16) + 8)), &(*(gm_tte9vgf6noic9 *)(((uint8_t *)&F.l16) + 32)), t55, t56, t57);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l10 = t58;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 20, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t59 = gm_l13;
                    uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                    uint64_t t62 = r60;
                    gm_l13 = t62;
                }
            }
gm_lb3:;
            {
                void * t63 = gm_l0;
                gm_tl874t2atnlh3 t64 = gm_l2;
                uint32_t t65 = gm_l8;
                gm_b16_8 t66 = gm_l5;
                void * t67 = gm_l9;
                uint32_t t68 = gm_l10;
                gm_t5lf9i3n1r3co t69;
                memset(&t69, 0, sizeof t69);
                gm_b16_8 t70 = gm_l6;
                gm_f_call_function_u7ovth6((void *)&gm_l17, (void *)NULL, cx, NULL, t63, t64, t65, t66, t67, t68, &t69, ((uint8_t)0), t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t71 = gm_l0;
            uint32_t t72 = gm_l10;
            uint32_t t73 = gm_l11;
            gm_b0_1 t74 = gm_f_pop_ctx_jivp36j((void *)NULL, cx, NULL, t71, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            {
                memmove(gm_out, ((uint8_t *)&gm_l17), 32);
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    void * t75 = gm_l0;
    gm_b16_8 t76 = gm_l3;
    uint64_t t77 = gm_f_ptr_of_b77b4c9(t75, t76);
    gm_l18 = t77;
    uint64_t t78 = gm_l18;
    uint8_t t79 = ((t78 == ((uint64_t)0ULL)));
    if (t79) {
        void * t80 = gm_l0;
        gm_tl874t2atnlh3 t81 = gm_l2;
        gm_b16_8 t82;
        GM_ST(void *, (uint8_t *)&t82, (void *)(gm_sq5khm5l62ens));
        GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(36));
        gm_t76123gfet73g t83;
        gm_f_trap_qh84l9d((void *)&t83, (void *)NULL, cx, NULL, t80, t81, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t83);
        goto gm_fdone;
    }
    void * t84 = gm_l0;
    uint64_t t85 = gm_l18;
    gm_t9805qtonbsag t86 = gm_f_fn_from_addr_7a6bpqd(t84, t85);
    gm_l22 = t86;
    uint8_t t87 = (uint8_t)((((uint8_t *)&gm_l22))[4] != 0);
    if (t87) {
        uint32_t t88 = GM_LD(uint32_t, ((uint8_t *)&gm_l22));
        gm_l19 = t88;
    } else {
        void * t89 = gm_l0;
        gm_tl874t2atnlh3 t90 = gm_l2;
        uint64_t t91 = gm_l18;
        gm_t4ce0jd3tdro4 t92;
        GM_ST(uint64_t, ((uint8_t *)&t92 + 0), t91);
        gm_f_format_k34396d((void *)&gm_l23, (void *)NULL, cx, NULL, t92);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
        if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_src6c128h826nsg, 32, 57);
        gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
        gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
        gm_t76123gfet73g t96;
        gm_f_trap_qh84l9d((void *)&t96, (void *)NULL, cx, NULL, t89, t90, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t96);
        goto gm_fdone;
    }
    {
        void * t97 = gm_l0;
        gm_tl874t2atnlh3 t98 = gm_l2;
        uint32_t t99 = gm_l19;
        gm_b16_8 t100 = gm_l5;
        uint32_t t101 = gm_l1;
        gm_t5lf9i3n1r3co t102;
        memset(&t102, 0, sizeof t102);
        gm_b16_8 t103 = gm_l6;
        gm_f_call_function_u7ovth6((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t97, t98, t99, t100, ((void *)0), t101, &t102, ((uint8_t)0), t103);
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
__attribute__((aligned(4))) void gm_f_enter_maqkh2o(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint32_t a2, void * a3, gm_b16_8 a4, void * a5, uint32_t a6, const void * a7, uint8_t a8, gm_b16_8 a9) {
    struct gm_fr_enter_maqkh2o F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint32_t gm_l2; /* f */
    void * gm_l3; /* info */
    gm_b16_8 gm_l4; /* store */
    void * gm_l5; /* env */
    uint32_t gm_l6; /* cx */
    gm_t5lf9i3n1r3co gm_l7; /* installing */
    uint8_t gm_l8; /* is_clause_lit */
    gm_b16_8 gm_l9; /* dst */
    uint8_t gm_l10; /* marker */
    uint8_t gm_l11; /* on_fiber */
    void * gm_l12; /* fr */
    void * gm_l13; /* c */
    uint8_t gm_l14; /* frameless */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint32_t gm_l17; /* p */
    gm_t76123gfet73g gm_l18; /* fl */
    void * gm_l19; /* code */
    gm_tcfa6l7reoosn gm_l20; /* r */
    uint64_t gm_l21; /* id */
    gm_t76123gfet73g gm_l22; /* fl */
    uint64_t gm_l23; /* frame */
    void * gm_l24; /* t */
    GM_STACK_CHECK(&F, 1358u);
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
    gm_l8 = a8;
    gm_l9 = a9;
    uint8_t gm_l25;
    gm_tcfa6l7reoosn gm_l26;
    uint64_t gm_l27;
    gm_t833u93mr4iob gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_l10 = ((uint8_t)0ULL);
    void * t17 = gm_l0;
    uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)t17) + 744));
    uint32_t t19 = GM_LD(uint32_t, ((uint8_t *)gm_cdps8qsftpu));
    uint8_t t20 = (t18 >= t19);
    if (t20) {
        gm_l25 = ((uint8_t)1);
    } else {
        void * t21 = (void *)((uint8_t *)&gm_l10);
        uint64_t t22 = (uintptr_t)t21;
        void * t23 = gm_l0;
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 752));
        uint8_t t25 = (t22 < t24);
        gm_l25 = t25;
    }
    uint8_t t26 = gm_l25;
    if (t26) {
        void * t27 = gm_l0;
        gm_tl874t2atnlh3 t28 = gm_l1;
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s38rtd27u976k));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(14));
        gm_t76123gfet73g t30;
        gm_f_trap_qh84l9d((void *)&t30, (void *)NULL, cx, NULL, t27, t28, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t30);
        goto gm_fdone;
    }
    void * t31 = gm_l0;
    uint64_t t32 = GM_LD(uint64_t, ((((uint8_t *)t31) + 1672) + 8));
    uint8_t t33 = (t32 > ((uint64_t)1ULL));
    if (t33) {
        void * t34 = gm_l0;
        gm_tl874t2atnlh3 t35 = gm_l1;
        gm_f_tick_6bsfu3n((void *)&gm_l26, (void *)NULL, cx, NULL, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
        if (t36) {
        } else {
            gm_t76123gfet73g t37 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l26) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t37);
            goto gm_fdone;
        }
    }
    void * t38 = gm_l0;
    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 1264));
    uint64_t t40 = (uint64_t)~((uint64_t)0ULL);
    uint8_t t41 = (!(t39 == t40));
    gm_l11 = t41;
    {
        {
            uint8_t t42 = gm_l11;
            if (!t42) {
                goto gm_lb2;
            }
            void * t43 = gm_l3;
            uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)t43) + 144));
            void * t45 = gm_l0;
            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)t45) + 1264));
            uint8_t t47 = (t44 > t46);
            if (!t47) {
                goto gm_lb2;
            }
            void * t48 = gm_l0;
            uint8_t t49 = gm_f_fast_23em7sv(t48);
            uint8_t t50 = (uint8_t)!t49;
            if (!t50) {
                goto gm_lb2;
            }
            {
                void * t51 = gm_l0;
                gm_tl874t2atnlh3 t52 = gm_l1;
                gm_b16_8 t53;
                GM_ST(void *, (uint8_t *)&t53, (void *)(gm_slg8371oger0u));
                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(31));
                gm_t76123gfet73g t54;
                gm_f_trap_qh84l9d((void *)&t54, (void *)NULL, cx, NULL, t51, t52, t53);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t54);
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    void * t55 = gm_l0;
    uint32_t t56 = gm_l2;
    void * t57 = gm_l3;
    gm_b16_8 t58 = gm_l9;
    void * t59 = gm_l5;
    gm_b16_8 t60 = gm_l4;
    void * t61 = gm_f_new_frame_vgm56mt((void *)NULL, cx, NULL, t55, t56, t57, t58, t59, t60);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l12 = t61;
    {
        void * t62 = gm_l12;
        memmove((((uint8_t *)t62) + 176), ((uint8_t *)a7), 24);
    }
    void * t63 = gm_l12;
    uint8_t t64 = gm_l8;
    GM_ST(uint8_t, (((uint8_t *)t63) + 200), t64);
    {
        {
            void * t65 = gm_l3;
            uint8_t t66 = (uint8_t)(GM_LD(void *, (((uint8_t *)t65) + 56)) != 0);
            if (!t66) {
                goto gm_lb4;
            }
            void * t67 = GM_LD(void *, (((uint8_t *)t65) + 56));
            gm_l13 = t67;
            void * t68 = gm_l13;
            uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)t68) + 32));
            gm_l14 = t69;
            goto gm_lb3;
        }
gm_lb4:;
        gm_l14 = ((uint8_t)0);
    }
gm_lb3:;
    {
        {
            void * t70 = gm_l3;
            uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)t70) + 24));
            if (!t71) {
                goto gm_lb6;
            }
            uint8_t t72 = gm_l14;
            uint8_t t73 = (uint8_t)!t72;
            if (!t73) {
                goto gm_lb6;
            }
            void * t74 = gm_l3;
            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)t74) + 8));
            gm_l15 = t75;
            gm_l16 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t76 = gm_l16;
                    uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    uint8_t t78 = (t76 < t77);
                    if (!t78) {
                        goto gm_lb7;
                    }
                    {
                        void * t79 = GM_LD(void *, ((uint8_t *)&gm_l15));
                        uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                        uint64_t t81 = gm_l16;
                        {
                            {
                                uint32_t t82 = GM_LD(uint32_t, (((uint8_t *)t79) + (uintptr_t)t81 * 4));
                                gm_l17 = t82;
                                void * t83 = gm_l3;
                                void * t84 = GM_LD(void *, (((uint8_t *)t83) + 120));
                                uint64_t t85 = GM_LD(uint64_t, ((((uint8_t *)t83) + 120) + 8));
                                uint32_t t86 = gm_l17;
                                uint64_t t87 = (uint64_t)t86;
                                uint64_t t88 = gm_f_us_cuv01fp(t87);
                                if ((uintptr_t)t88 >= (uintptr_t)(t85)) gm_trap_index((uintptr_t)t88, t85, gm_src6c128h826nsg, 71, 16);
                                uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)t84) + (uintptr_t)t88 * 1));
                                if (t89) {
                                    void * t90 = gm_l0;
                                    gm_tl874t2atnlh3 t91 = gm_l1;
                                    void * t92 = gm_l12;
                                    uint32_t t93 = gm_l17;
                                    gm_f_local_addr_o51qq3k((void *)&gm_l28, (void *)NULL, cx, NULL, t90, t91, t92, t93);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                    if (t94) {
                                        uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                        gm_l27 = t95;
                                    } else {
                                        {
                                            memmove(((uint8_t *)&gm_l18), (((uint8_t *)&gm_l28) + 8), 24);
                                        }
                                        void * t96 = gm_l0;
                                        void * t97 = gm_l12;
                                        gm_b0_1 t98 = gm_f_end_frame_cetr4jh((void *)NULL, cx, NULL, t96, t97);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        {
                                            gm_t76123gfet73g t99 = GM_LD(gm_t76123gfet73g, ((uint8_t *)&gm_l18));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t99);
                                            goto gm_fdone;
                                        }
                                    }
                                    uint64_t t100 = gm_l27;
                                }
                                goto gm_lb10;
                            }
gm_lb11:;
                            gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 70, 9);
                        }
gm_lb10:;
                    }
gm_lb9:;
                    uint64_t t101 = gm_l16;
                    uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                    uint64_t t104 = r102;
                    gm_l16 = t104;
                }
            }
gm_lb7:;
            goto gm_lb5;
        }
gm_lb6:;
    }
gm_lb5:;
    uint8_t t105 = gm_l11;
    if (t105) {
        void * t106 = gm_l0;
        void * t107 = gm_l0;
        uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)t107) + 1264));
        void * t109 = gm_l3;
        uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)t109) + 144));
        uint64_t r111; bool o112 = __builtin_sub_overflow(t108, t110, &r111);
        if (o112) gm_trap("integer overflow in `-`", gm_src6c128h826nsg, 79, 19);
        uint64_t t113 = r111;
        GM_ST(uint64_t, (((uint8_t *)t106) + 1264), t113);
    }
    void * t114 = gm_l0;
    void * t115 = gm_l0;
    uint32_t t116 = GM_LD(uint32_t, (((uint8_t *)t115) + 744));
    uint32_t r117; bool o118 = __builtin_add_overflow(t116, ((uint32_t)1ULL), &r117);
    if (o118) gm_trap("integer overflow in `+`", gm_src6c128h826nsg, 80, 5);
    uint32_t t119 = r117;
    GM_ST(uint32_t, (((uint8_t *)t114) + 744), t119);
    void * t120 = gm_l3;
    uint8_t t121 = (uint8_t)(GM_LD(void *, (((uint8_t *)t120) + 56)) != 0);
    if (t121) {
        void * t122 = GM_LD(void *, (((uint8_t *)t120) + 56));
        gm_l19 = t122;
    } else {
        gm_trap("reached `@unreachable()`", gm_src6c128h826nsg, 81, 26);
    }
    {
        void * t123 = gm_l0;
        void * t124 = gm_l12;
        uint32_t t125 = gm_l6;
        gm_tbl8il92l97ut t126;
        GM_ST(uint32_t, (uint8_t *)&t126 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t126 + 0), t124);
        GM_ST(uint32_t, ((uint8_t *)&t126 + 8), t125);
        void * t127 = gm_l19;
        gm_b16_8 t128 = gm_l9;
        gm_f_vm_run_j1mlbuk((void *)&gm_l20, (void *)NULL, cx, NULL, t123, t126, t127, t128);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t129 = gm_l0;
    void * t130 = gm_l0;
    uint32_t t131 = GM_LD(uint32_t, (((uint8_t *)t130) + 744));
    uint32_t r132; bool o133 = __builtin_sub_overflow(t131, ((uint32_t)1ULL), &r132);
    if (o133) gm_trap("integer overflow in `-`", gm_src6c128h826nsg, 83, 5);
    uint32_t t134 = r132;
    GM_ST(uint32_t, (((uint8_t *)t129) + 744), t134);
    uint8_t t135 = gm_l11;
    if (t135) {
        void * t136 = gm_l0;
        void * t137 = gm_l0;
        uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)t137) + 1264));
        void * t139 = gm_l3;
        uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)t139) + 144));
        uint64_t r141; bool o142 = __builtin_add_overflow(t138, t140, &r141);
        if (o142) gm_trap("integer overflow in `+`", gm_src6c128h826nsg, 84, 19);
        uint64_t t143 = r141;
        GM_ST(uint64_t, (((uint8_t *)t136) + 1264), t143);
    }
    void * t144 = gm_l12;
    uint64_t t145 = GM_LD(uint64_t, ((uint8_t *)t144));
    gm_l21 = t145;
    void * t146 = gm_l0;
    void * t147 = gm_l12;
    gm_b0_1 t148 = gm_f_end_frame_cetr4jh((void *)NULL, cx, NULL, t146, t147);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t149 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
    if (t149) {
    } else {
        {
            memmove(((uint8_t *)&gm_l22), (((uint8_t *)&gm_l20) + 8), 24);
        }
        {
            {
                uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                uint8_t t151 = (uint8_t)(t150 == ((uint8_t)2ULL));
                if (!t151) {
                    goto gm_lb13;
                }
                uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                gm_l23 = t152;
                uint64_t t153 = gm_l23;
                uint64_t t154 = gm_l21;
                uint8_t t155 = ((t153 == t154));
                if (!t155) {
                    goto gm_lb13;
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
gm_lb13:;
        }
gm_lb12:;
        {
            {
                uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
                uint8_t t157 = (uint8_t)(t156 == ((uint8_t)4ULL));
                if (!t157) {
                    goto gm_lb15;
                }
                void * t158 = GM_LD(void *, (((uint8_t *)&gm_l22) + 8));
                gm_l24 = t158;
                void * t159 = gm_l0;
                gm_t9qkh0rsooc8d t160 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t159) + 88));
                uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)&t160) + 0));
                uint8_t t162 = (uint8_t)((t161 == ((uint8_t)0ULL)));
                if (!t162) {
                    goto gm_lb15;
                }
                void * t163 = gm_l24;
                void * t164 = (void *)(((uint8_t *)t163) + 32);
                uint32_t t165 = gm_l2;
                gm_tl874t2atnlh3 t166 = gm_l1;
                gm_t9d4sf0vqn7dt t167;
                GM_ST(uint32_t, ((uint8_t *)&t167 + 0), t165);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t167 + 4), t166);
                gm_t9qkh0rsooc8d t168;
                GM_ST(uint8_t, ((uint8_t *)&t168 + 0), ((uint8_t)0ULL));
                gm_tftb1odc739t5 t169 = gm_f_push_t8up8j4_2(t164, GM_CONV(gm_csedogq9e4rp5r, t167));
                gm_l29 = t169;
                uint8_t t170 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                if (!t170) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_src6c128h826nsg, 89, 50);
                goto gm_lb14;
            }
gm_lb15:;
        }
gm_lb14:;
        {
            gm_t76123gfet73g t171 = GM_LD(gm_t76123gfet73g, ((uint8_t *)&gm_l22));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t171);
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
inline __attribute__((aligned(4))) void gm_f_perform_4kn6797(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2, void * a3, uint32_t a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_perform_4kn6797 F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* cx */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* effect_ */
    uint32_t gm_l4; /* op */
    gm_b16_8 gm_l5; /* args */
    gm_b16_8 gm_l6; /* dst */
    uint32_t gm_l7; /* n */
    GM_STACK_CHECK(&F, 423u);
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
    gm_tabb710d9mq2e gm_l8;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    void * t4 = gm_l3;
    uint32_t t5 = gm_f_find_ctx_1elkgc2(t2, t3, t4);
    gm_l7 = t5;
    uint32_t t6 = gm_l7;
    uint8_t t7 = ((t6 == ((uint32_t)0ULL)));
    if (t7) {
        void * t8 = gm_l0;
        gm_tl874t2atnlh3 t9 = gm_l2;
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, (((uint8_t *)t10) + 8));
        void * t12 = (void *)(((uint8_t *)t11) + 8);
        void * t13 = gm_l3;
        uint32_t t14 = GM_LD(uint32_t, ((uint8_t *)t13));
        gm_b16_8 t15 = gm_f_effect_name_prpp0ip(t12, t14);
        gm_tnpifv20irnph t16;
        GM_ST(gm_b16_8, ((uint8_t *)&t16 + 0), t15);
        gm_f_format_ms0imbl((void *)&gm_l8, (void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_src6c128h826nsg, 102, 38);
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        gm_t76123gfet73g t20;
        gm_f_trap_qh84l9d((void *)&t20, (void *)NULL, cx, NULL, t8, t9, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t20);
        goto gm_fdone;
    }
    void * t21 = gm_l0;
    void * t22 = GM_LD(void *, ((uint8_t *)t21));
    void * t23 = gm_l3;
    uint32_t t24 = GM_LD(uint32_t, ((uint8_t *)t23));
    uint32_t t25 = gm_l4;
    uint8_t t26 = gm_f_is_ctl_oclllo3(t22, t24, t25);
    if (t26) {
        void * t27 = gm_l0;
        gm_tl874t2atnlh3 t28 = gm_l2;
        uint32_t t29 = gm_l7;
        uint32_t t30 = gm_l4;
        gm_b16_8 t31 = gm_l5;
        gm_b16_8 t32 = gm_l6;
        gm_f_ctl_perform_kobk825((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t27, t28, t29, t30, t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    void * t33 = gm_l0;
    gm_tl874t2atnlh3 t34 = gm_l2;
    uint32_t t35 = gm_l7;
    uint32_t t36 = gm_l4;
    gm_b16_8 t37 = gm_l5;
    gm_b16_8 t38 = gm_l6;
    gm_f_call_clause_bjbs6l1((void *)&(*(gm_tcfa6l7reoosn *)gm_out), (void *)NULL, cx, NULL, t33, t34, t35, t36, t37, t38);
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
__attribute__((aligned(4))) void gm_f_perform_native_2ppar83(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t9qkh0rsooc8d a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5) {
    struct gm_fr_perform_native_2ppar83 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t9qkh0rsooc8d gm_l2; /* kind */
    uint32_t gm_l3; /* op */
    gm_b16_8 gm_l4; /* args */
    gm_b16_8 gm_l5; /* dst */
    gm_b16_8 gm_l6; /* data */
    gm_b16_8 gm_l7; /* t */
    uint64_t gm_l8; /* ps */
    gm_b16_8 gm_l9; /* table */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_b16_8 gm_l13; /* s */
    uint64_t gm_l14; /* addr */
    uint64_t gm_l15; /* taddr */
    gm_b16_8 gm_l16; /* saved */
    gm_b16_8 gm_l17; /* name */
    gm_b16_8 gm_l18; /* v */
    uint64_t gm_l19; /* addr */
    gm_t51b2en4qp02s gm_l20; /* sl */
    gm_b16_8 gm_l21; /* s */
    gm_t88rcucg1b7uk gm_l22; /* st */
    GM_STACK_CHECK(&F, 2491u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_td0ept12e7q8b gm_l23;
    uint32_t gm_l24;
    gm_tf1bjg81u0glv gm_l25;
    gm_t833u93mr4iob gm_l26;
    gm_t833u93mr4iob gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_td0ept12e7q8b gm_l29;
    gm_th2lqbo77pvsc gm_l30;
    gm_t833u93mr4iob gm_l31;
    uint8_t gm_l32;
    gm_tabb710d9mq2e gm_l33;
    {
        {
            {
                {
                    {
                        {
                            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)0ULL));
                            if (!t21) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)1ULL));
                            if (!t23) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    {
                        void * t24 = gm_l0;
                        gm_tl874t2atnlh3 t25 = gm_l1;
                        void * t26 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t27, gm_src6c128h826nsg, 138, 45);
                        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)t26) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
                        gm_f_read_string_ttuk4gr((void *)&gm_l23, (void *)NULL, cx, NULL, t24, t25, t29);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                    if (t30) {
                    } else {
                        gm_t76123gfet73g t31 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l23) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t31);
                        goto gm_fdone;
                    }
                    gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                    gm_l6 = t32;
                    void * t33 = gm_l0;
                    gm_t9qkh0rsooc8d t34 = gm_l2;
                    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
                    uint8_t t36 = (uint8_t)((t35 == ((uint8_t)0ULL)));
                    gm_t9qkh0rsooc8d t37;
                    GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)0ULL));
                    if (t36) {
                        gm_l24 = ((uint32_t)1ULL);
                    } else {
                        gm_l24 = ((uint32_t)2ULL);
                    }
                    uint32_t t38 = gm_l24;
                    gm_b16_8 t39 = gm_l6;
                    gm_b0_1 t40 = gm_f_host_write_r26no87((void *)NULL, cx, NULL, t33, t38, t39);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    {
                        gm_b16_8 t41 = gm_l6;
                        gm_b0_1 t42 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t41, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kh7445i0ed8)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    {
                        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                        uint8_t t44 = (uint8_t)(t43 == ((uint8_t)2ULL));
                        if (!t44) {
                            goto gm_lb6;
                        }
                        {
                            {
                                {
                                    uint32_t t45 = gm_l3;
                                    uint8_t t46 = ((t45 == ((uint32_t)0ULL)));
                                    if (!t46) {
                                        goto gm_lb8;
                                    }
                                    {
                                        {
                                            void * t47 = gm_l0;
                                            uint8_t t48 = (uint8_t)(((((uint8_t *)t47) + 1040))[16] != 0);
                                            if (!t48) {
                                                goto gm_lb10;
                                            }
                                            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t47) + 1040));
                                            gm_l7 = t49;
                                            gm_b16_8 t50 = gm_l5;
                                            gm_b16_8 t51 = gm_l7;
                                            gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                                            gm_f_copy_from_f7j6p05(t50, t52);
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
                                    void * t53 = gm_l0;
                                    uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)t53) + 176));
                                    uint64_t t55 = gm_f_us_cuv01fp(t54);
                                    gm_l8 = t55;
                                    void * t56 = gm_l0;
                                    void * t57 = gm_l0;
                                    uint64_t t58 = GM_LD(uint64_t, ((((uint8_t *)t57) + 1024) + 8));
                                    uint64_t r59; bool o60 = __builtin_mul_overflow(t58, ((uint64_t)2ULL), &r59);
                                    if (o60) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 149, 36);
                                    uint64_t t61 = r59;
                                    uint64_t t62 = gm_l8;
                                    uint64_t r63; bool o64 = __builtin_mul_overflow(t61, t62, &r63);
                                    if (o64) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 149, 36);
                                    uint64_t t65 = r63;
                                    uint64_t t66 = (uint64_t)t65;
                                    gm_b16_8 t67 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t56, t66);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l9 = t67;
                                    void * t68 = gm_l0;
                                    gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)t68) + 1024));
                                    gm_l10 = t69;
                                    gm_l11 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t70 = gm_l11;
                                            uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                            uint8_t t72 = (t70 < t71);
                                            if (!t72) {
                                                goto gm_lb11;
                                            }
                                            {
                                                uint64_t t73 = gm_l11;
                                                void * t74 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                                uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                uint64_t t76 = gm_l11;
                                                gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)t74) + (uintptr_t)t76 * 16));
                                                GM_ST(uint64_t, ((uint8_t *)&gm_l25 + 0), t73);
                                                GM_ST(gm_b16_8, ((uint8_t *)&gm_l25 + 8), t77);
                                                {
                                                    {
                                                        uint64_t t78 = GM_LD(uint64_t, ((uint8_t *)&gm_l25));
                                                        gm_l12 = t78;
                                                        gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
                                                        gm_l13 = t79;
                                                        {
                                                            void * t80 = gm_l0;
                                                            gm_tl874t2atnlh3 t81 = gm_l1;
                                                            uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                                            uint64_t t83 = (uint64_t)t82;
                                                            gm_t9qkh0rsooc8d t84;
                                                            GM_ST(uint8_t, ((uint8_t *)&t84 + 0), ((uint8_t)3ULL));
                                                            gm_f_alloc_mem_0vu6kcs((void *)&gm_l26, (void *)NULL, cx, NULL, t80, t81, t83, ((uint64_t)1ULL), t84, ((uint8_t)0));
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        uint8_t t85 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                                        if (t85) {
                                                        } else {
                                                            gm_t76123gfet73g t86 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l26) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t86);
                                                            goto gm_fdone;
                                                        }
                                                        uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                                                        gm_l14 = t87;
                                                        {
                                                            void * t88 = gm_l0;
                                                            void * t89 = (void *)(((uint8_t *)t88) + 184);
                                                            uint64_t t90 = gm_l14;
                                                            gm_b16_8 t91 = gm_l13;
                                                            gm_tgfdnufetdnfc t92;
                                                            gm_f_init_9fuf99v((void *)&t92, t89, t90, t91);
                                                        }
                                                        void * t93 = gm_l0;
                                                        void * t94 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                                        uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                                        uint64_t t96 = gm_l12;
                                                        uint64_t r97; bool o98 = __builtin_mul_overflow(t96, ((uint64_t)2ULL), &r97);
                                                        if (o98) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 153, 39);
                                                        uint64_t t99 = r97;
                                                        uint64_t t100 = gm_l8;
                                                        uint64_t r101; bool o102 = __builtin_mul_overflow(t99, t100, &r101);
                                                        if (o102) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 153, 39);
                                                        uint64_t t103 = r101;
                                                        uint64_t t104 = gm_l12;
                                                        uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                                                        if (o106) gm_trap("integer overflow in `+`", gm_src6c128h826nsg, 153, 52);
                                                        uint64_t t107 = r105;
                                                        uint64_t r108; bool o109 = __builtin_mul_overflow(t107, ((uint64_t)2ULL), &r108);
                                                        if (o109) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 153, 51);
                                                        uint64_t t110 = r108;
                                                        uint64_t t111 = gm_l8;
                                                        uint64_t r112; bool o113 = __builtin_mul_overflow(t110, t111, &r112);
                                                        if (o113) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 153, 51);
                                                        uint64_t t114 = r112;
                                                        uint64_t t115 = (uintptr_t)t103;
                                                        uint64_t t116 = (uintptr_t)t114;
                                                        if (t115 > t116 || t116 > (uintptr_t)t95) gm_trap_slice(t115, t116, t95, gm_src6c128h826nsg, 153, 33);
                                                        uint64_t t117 = ((uintptr_t)t114) - (uintptr_t)t103;
                                                        gm_b16_8 t118;
                                                        GM_ST(void *, (uint8_t *)&t118, (void *)(((uint8_t *)t94 + (uintptr_t)t103 * 1)));
                                                        GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(t117));
                                                        uint64_t t119 = gm_l14;
                                                        uint64_t t120 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                                        uint64_t t121 = (uint64_t)t120;
                                                        gm_f_put_slice_ko14loi(t93, t118, t119, t121);
                                                        goto gm_lb14;
                                                    }
gm_lb15:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 150, 17);
                                                }
gm_lb14:;
                                            }
gm_lb13:;
                                            uint64_t t122 = gm_l11;
                                            uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                                            uint64_t t125 = r123;
                                            gm_l11 = t125;
                                        }
                                    }
gm_lb11:;
                                    {
                                        void * t126 = gm_l0;
                                        gm_tl874t2atnlh3 t127 = gm_l1;
                                        uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                        uint64_t t129 = (uint64_t)t128;
                                        gm_t9qkh0rsooc8d t130;
                                        GM_ST(uint8_t, ((uint8_t *)&t130 + 0), ((uint8_t)2ULL));
                                        gm_f_alloc_mem_0vu6kcs((void *)&gm_l27, (void *)NULL, cx, NULL, t126, t127, t129, ((uint64_t)16ULL), t130, ((uint8_t)0));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t131 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                                    if (t131) {
                                    } else {
                                        gm_t76123gfet73g t132 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l27) + 8));
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t132);
                                        goto gm_fdone;
                                    }
                                    uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                                    gm_l15 = t133;
                                    {
                                        void * t134 = gm_l0;
                                        void * t135 = (void *)(((uint8_t *)t134) + 184);
                                        uint64_t t136 = gm_l15;
                                        gm_b16_8 t137 = gm_l9;
                                        gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
                                        gm_tgfdnufetdnfc t139;
                                        gm_f_init_9fuf99v((void *)&t139, t135, t136, t138);
                                    }
                                    void * t140 = gm_l0;
                                    gm_b16_8 t141 = gm_l5;
                                    uint64_t t142 = gm_l15;
                                    void * t143 = gm_l0;
                                    uint64_t t144 = GM_LD(uint64_t, ((((uint8_t *)t143) + 1024) + 8));
                                    uint64_t t145 = (uint64_t)t144;
                                    gm_f_put_slice_ko14loi(t140, t141, t142, t145);
                                    {
                                        uint64_t t146 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                        gm_f_make_2ip78bj((void *)&gm_l28, (void *)NULL, cx, NULL, t146, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krt7450irj2)));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t147 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                                    if (!t147) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_src6c128h826nsg, 158, 26);
                                    gm_b16_8 t148 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                                    gm_l16 = t148;
                                    gm_b16_8 t149 = gm_l16;
                                    gm_b16_8 t150 = gm_l5;
                                    gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                                    gm_f_copy_from_f7j6p05(t149, t151);
                                    {
                                        void * t152 = gm_l0;
                                        gm_b16_8 t153 = gm_l16;
                                        gm_th2lqbo77pvsc t154;
                                        GM_ST(uint8_t, (uint8_t *)&t154 + 17, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t154 + 18, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t154 + 20, 0);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t154), t153);
                                        ((uint8_t *)&t154)[16] = 1;
                                        GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)t152) + 1040), t154);
                                    }
                                    goto gm_lb7;
                                }
gm_lb8:;
                            }
                            {
                                {
                                    uint32_t t155 = gm_l3;
                                    uint8_t t156 = ((t155 == ((uint32_t)1ULL)));
                                    if (!t156) {
                                        goto gm_lb16;
                                    }
                                    {
                                        void * t157 = gm_l0;
                                        gm_tl874t2atnlh3 t158 = gm_l1;
                                        void * t159 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                        uint64_t t160 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t160)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t160, gm_src6c128h826nsg, 163, 49);
                                        gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)t159) + (uintptr_t)((uint64_t)0ULL) * 16));
                                        gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
                                        gm_f_read_string_ttuk4gr((void *)&gm_l29, (void *)NULL, cx, NULL, t157, t158, t162);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t163 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                    if (t163) {
                                    } else {
                                        gm_t76123gfet73g t164 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l29) + 8));
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t164);
                                        goto gm_fdone;
                                    }
                                    gm_b16_8 t165 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                                    gm_l17 = t165;
                                    {
                                        gm_b16_8 t166 = gm_l17;
                                        gm_node *pn167 = gm_find(cx, 621690230);
                                        if (!pn167) gm_trap("no handler installed for effect `Env`", gm_src6c128h826nsg, 164, 22);
                                        gm_th2lqbo77pvsc t168;
                                        if (pn167->kind == GM_NATIVE) {
                                            const uint8_t *ev; size_t evn;
                                            bool evok = gm_env_var(GM_LD(void *, &t166), GM_LD(uintptr_t, (uint8_t *)&t166 + 8), &ev, &evn);
                                            gm_b16_8 t169;
                                            GM_ST(void *, (uint8_t *)&t169, (void *)(ev));
                                            GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(evn));
                                            gm_th2lqbo77pvsc t170;
                                            GM_ST(uint8_t, (uint8_t *)&t170 + 17, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t170 + 18, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t170 + 20, 0);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t170), t169);
                                            ((uint8_t *)&t170)[16] = 1;
                                            gm_th2lqbo77pvsc t171;
                                            memset(&t171, 0, sizeof t171);
                                            t168 = evok ? t170 : t171;
                                        } else {
                                            ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn167->clauses[1])((void *)&t168, pn167->env, pn167->hctx, pn167, t166);
                                        }
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l30 = t168;
                                    }
                                    uint8_t t172 = (uint8_t)((((uint8_t *)&gm_l30))[16] != 0);
                                    if (t172) {
                                        gm_b16_8 t173 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l30));
                                        gm_l18 = t173;
                                    } else {
                                        gm_b16_8 t174 = gm_l5;
                                        gm_f_fill_abmf5g5(t174, ((uint8_t)0ULL));
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
                                        void * t175 = gm_l0;
                                        gm_tl874t2atnlh3 t176 = gm_l1;
                                        uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                        uint64_t t178 = (uint64_t)t177;
                                        gm_t9qkh0rsooc8d t179;
                                        GM_ST(uint8_t, ((uint8_t *)&t179 + 0), ((uint8_t)3ULL));
                                        gm_f_alloc_mem_0vu6kcs((void *)&gm_l31, (void *)NULL, cx, NULL, t175, t176, t178, ((uint64_t)1ULL), t179, ((uint8_t)0));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t180 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                                    if (t180) {
                                    } else {
                                        gm_t76123gfet73g t181 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l31) + 8));
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t181);
                                        goto gm_fdone;
                                    }
                                    uint64_t t182 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                    gm_l19 = t182;
                                    {
                                        void * t183 = gm_l0;
                                        void * t184 = (void *)(((uint8_t *)t183) + 184);
                                        uint64_t t185 = gm_l19;
                                        gm_b16_8 t186 = gm_l18;
                                        gm_tgfdnufetdnfc t187;
                                        gm_f_init_9fuf99v((void *)&t187, t184, t185, t186);
                                    }
                                    void * t188 = (void *)((uint8_t *)&gm_l20);
                                    void * t189 = gm_l0;
                                    uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)t189) + 176));
                                    uint64_t r191; bool o192 = __builtin_mul_overflow(((uint64_t)2ULL), t190, &r191);
                                    if (o192) gm_trap("integer overflow in `*`", gm_src6c128h826nsg, 171, 31);
                                    uint64_t t193 = r191;
                                    uint64_t t194 = gm_f_us_cuv01fp(t193);
                                    uint64_t t195 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t196 = (uintptr_t)t194;
                                    if (t195 > t196 || t196 > (uintptr_t)((uint64_t)16ULL)) gm_trap_slice(t195, t196, ((uint64_t)16ULL), gm_src6c128h826nsg, 171, 22);
                                    uint64_t t197 = ((uintptr_t)t194) - (uintptr_t)((uint64_t)0ULL);
                                    gm_b16_8 t198;
                                    GM_ST(void *, (uint8_t *)&t198, (void *)(((uint8_t *)t188 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t198 + 8, (uintptr_t)(t197));
                                    gm_l21 = t198;
                                    void * t199 = gm_l0;
                                    gm_b16_8 t200 = gm_l21;
                                    uint64_t t201 = gm_l19;
                                    uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                    uint64_t t203 = (uint64_t)t202;
                                    gm_f_put_slice_ko14loi(t199, t200, t201, t203);
                                    void * t204 = gm_l0;
                                    void * t205 = GM_LD(void *, (((uint8_t *)t204) + 8));
                                    void * t206 = (void *)(((uint8_t *)t205) + 8);
                                    gm_t88rcucg1b7uk t207 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t206);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l22 = t207;
                                    void * t208 = gm_l0;
                                    void * t209 = gm_l0;
                                    void * t210 = GM_LD(void *, (((uint8_t *)t209) + 8));
                                    void * t211 = (void *)(((uint8_t *)t210) + 8);
                                    gm_t88rcucg1b7uk t212 = gm_l22;
                                    gm_t88rcucg1b7uk t213 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t211, t212);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_b16_8 t214 = gm_l5;
                                    gm_b16_8 t215 = gm_l21;
                                    gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
                                    gm_b0_1 t217 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t208, t213, t214, t216);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb7;
                                }
gm_lb16:;
                            }
                            {
                                {
                                    uint32_t t218 = gm_l3;
                                    uint8_t t219 = ((t218 == ((uint32_t)2ULL)));
                                    if (!t219) {
                                        goto gm_lb17;
                                    }
                                    void * t220 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                    uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t221)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t221, gm_src6c128h826nsg, 176, 35);
                                    uint64_t t222 = GM_LD(uint64_t, ((((uint8_t *)t220) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                                    uint8_t t223 = (t222 > ((uint64_t)0ULL));
                                    if (t223) {
                                        void * t224 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                        uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t225)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t225, gm_src6c128h826nsg, 176, 53);
                                        void * t226 = GM_LD(void *, (((uint8_t *)t224) + (uintptr_t)((uint64_t)0ULL) * 16));
                                        uint64_t t227 = GM_LD(uint64_t, ((((uint8_t *)t224) + (uintptr_t)((uint64_t)0ULL) * 16) + 8));
                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t227)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t227, gm_src6c128h826nsg, 176, 53);
                                        uint8_t t228 = GM_LD(uint8_t, (((uint8_t *)t226) + (uintptr_t)((uint64_t)0ULL) * 1));
                                        gm_l32 = t228;
                                    } else {
                                        gm_l32 = ((uint8_t)0ULL);
                                    }
                                    {
                                        uint8_t t229 = gm_l32;
                                        gm_t76123gfet73g t230;
                                        GM_ST(uint8_t, (uint8_t *)&t230 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t230 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t230 + 4, 0);
                                        GM_ST(uint8_t, (uint8_t *)&t230 + 9, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t230 + 10, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t230 + 12, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t230 + 16, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t230 + 0), ((uint8_t)3ULL));
                                        GM_ST(uint8_t, ((uint8_t *)&t230 + 8), t229);
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t230);
                                        goto gm_fdone;
                                    }
                                }
gm_lb17:;
                                {
                                    void * t231 = gm_l0;
                                    gm_tl874t2atnlh3 t232 = gm_l1;
                                    uint32_t t233 = gm_l3;
                                    gm_t88rcucg1b7uk t234;
                                    GM_ST(uint32_t, ((uint8_t *)&t234 + 0), t233);
                                    gm_f_format_la70vrc((void *)&gm_l33, (void *)NULL, cx, NULL, t234);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t235 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                    if (!t235) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_src6c128h826nsg, 177, 39);
                                    gm_b16_8 t236 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                                    gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
                                    gm_t76123gfet73g t238;
                                    gm_f_trap_qh84l9d((void *)&t238, (void *)NULL, cx, NULL, t231, t232, t237);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t238);
                                    goto gm_fdone;
                                }
gm_lb18:;
                                gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 142, 17);
                            }
                        }
gm_lb7:;
                    }
                    {
                        goto gm_lb1;
                    }
                }
gm_lb6:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_src6c128h826nsg, 136, 5);
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

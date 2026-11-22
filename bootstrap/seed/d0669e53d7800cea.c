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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { void * a[2]; } gm_t5fru0lp11n7t;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { void * a[3]; } gm_thmgeksj5pdth;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { void * a[1]; } gm_tn3r4rvlh0rsv;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t5fru0lp11n7t) == 16 && _Alignof(gm_t5fru0lp11n7t) == 8 && offsetof(gm_t5fru0lp11n7t, a) == 0, "layout of `[2]?^Expr`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint64_t f0; gm_b16_8 f1; gm_b16_8 f2; } gm_t7neromnu8t62;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(e: ^const Expr, l: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
_Static_assert(sizeof(gm_thmgeksj5pdth) == 24 && _Alignof(gm_thmgeksj5pdth) == 8 && offsetof(gm_thmgeksj5pdth, a) == 0, "layout of `[3]^Expr`");
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn3r4rvlh0rsv) == 8 && _Alignof(gm_tn3r4rvlh0rsv) == 8 && offsetof(gm_tn3r4rvlh0rsv, a) == 0, "layout of `[1]^Expr`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
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
_Static_assert(sizeof(gm_t7neromnu8t62) == 40 && _Alignof(gm_t7neromnu8t62) == 8 && offsetof(gm_t7neromnu8t62, f0) == 0 && offsetof(gm_t7neromnu8t62, f1) == 8 && offsetof(gm_t7neromnu8t62, f2) == 24, "layout of `(u64, string, string)`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t8588ma2cr9d7 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_t9q8lhljes74c;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9q8lhljes74c) == 32 && _Alignof(gm_t9q8lhljes74c) == 8 && offsetof(gm_t9q8lhljes74c, f0) == 0 && offsetof(gm_t9q8lhljes74c, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_t9q8lhljes74c v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_to0f5qn75cgg8;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_to0f5qn75cgg8) == 64 && _Alignof(gm_to0f5qn75cgg8) == 8 && offsetof(gm_to0f5qn75cgg8, f0) == 0 && offsetof(gm_to0f5qn75cgg8, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_to0f5qn75cgg8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tsf6i2agfnpva;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_tsf6i2agfnpva) == 88 && _Alignof(gm_tsf6i2agfnpva) == 8 && offsetof(gm_tsf6i2agfnpva, f0) == 0 && offsetof(gm_tsf6i2agfnpva, f1) == 8 && offsetof(gm_tsf6i2agfnpva, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
typedef struct { uint64_t f0; gm_t7te2tdpebbt4 f1; } gm_tl1vi0nd1uql9;
_Static_assert(sizeof(gm_tl1vi0nd1uql9) == 152 && _Alignof(gm_tl1vi0nd1uql9) == 8 && offsetof(gm_tl1vi0nd1uql9, f0) == 0 && offsetof(gm_tl1vi0nd1uql9, f1) == 8, "layout of `(i: usize, x: Arg)`");
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
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
struct gm_fr_empty_slice_rcd2jln {
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
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_format_0gvqkke {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3ctg94v {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4nuejdf {
    gm_frame h;
    gm_t7neromnu8t62 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7cgva50 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7r64gap {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9rhpk5t {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d0lfl1k {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f8dhsig {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_faaokv1 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_frnomph {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ima6g5u {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jrngvku {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l035o11 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l2fmok5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_m1f4vhf {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mpg78oo {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mrlevsk {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nnfoc79 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pdashts {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pepv3ej {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pj28nou {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q2uenf3 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qu5usm7 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rr83dbe {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_s7ci5ul {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ul5ufam {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_is_bool_ajhjgeg {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_float_3jk89qu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_vector_gkq6b88 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_lane_error_72hoonn {
    gm_frame h;
    void * ret;
};
struct gm_fr_lane_needs_a6gcpil {
    gm_frame h;
    void * ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mask_ty_cj7ln0g {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tsf6i2agfnpva l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
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
struct gm_fr_shuffle_index_atl9l9i {
    gm_frame h;
    gm_tbci5ospk5nnn l9; /* i */
    gm_tbci5ospk5nnn l16; /* i */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_to_u128_haka99r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t9p38b040grms ret;
};
struct gm_fr_to_u64_m01t1da {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_vector_binary_bicu7u0 {
    gm_frame h;
    void * ret;
};
struct gm_fr_vector_construction_fhm2mm5 {
    gm_frame h;
    gm_tkacitepg5fk0 l8; /* a */
    void * ret;
};
struct gm_fr_vector_conversion_32a9cuf {
    gm_frame h;
    void * ret;
};
struct gm_fr_vector_lane_conversion_chta07f {
    gm_frame h;
    void * ret;
};
struct gm_fr_vector_load_ssdvomk {
    gm_frame h;
    void * ret;
};
struct gm_fr_vector_method_hgn4sb1 {
    gm_frame h;
    gm_tc3oht4ksth9d l16; /* xe */
    void * ret;
};
struct gm_fr_vector_shuffle_5ime7a7 {
    gm_frame h;
    gm_tc3oht4ksth9d l10; /* e */
    gm_trqfvmjtt4sij l15; /* idx */
    gm_t7te2tdpebbt4 l19; /* a */
    gm_tc3oht4ksth9d l20; /* e */
    gm_trqfvmjtt4sij l25; /* all */
    gm_tsf6i2agfnpva l29; /* i */
    void * ret;
};
struct gm_fr_vector_unary_2mbe97t {
    gm_frame h;
    void * ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqasplg81cl8(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_arity_vmamjfj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_builtin_a7cjd7d(void *env, gm_node *cx, gm_node *inst, gm_t8588ma2cr9d7 a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_const_value_fr4coi4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_construct_array_qkofm98(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tkn42u7na3t81 a1, gm_tf5srh4mp4b3i a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_construction_call_ktqtrpo(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
inline __attribute__((aligned(4))) void gm_f_format_0gvqkke(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0q4nncb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_3ctg94v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_4nuejdf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_74pd3in(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_7cgva50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_7r64gap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_9rhpk5t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_bqgl4hq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_d0lfl1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_f8dhsig(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_faaokv1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fjqj2pj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_frnomph(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ima6g5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_jrngvku(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l035o11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_l2fmok5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_m1f4vhf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mpg78oo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_mrlevsk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_nnfoc79(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_pdashts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_pepv3ej(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_pj28nou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_q2uenf3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_qu5usm7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_rr83dbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_s7ci5ul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ul5ufam(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_bool_ajhjgeg(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_3jk89qu(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_positional_array_6sufsk8(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_signed_integer_s66djm7(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_lane_error_72hoonn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_lane_needs_a6gcpil(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint8_t gm_f_lane_type_ok_rp8uq1p(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_lossless_a54o69s(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mask_ty_cj7ln0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_math_vector_parts_49c7865(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_require_unsigned_0fhlknt(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_shuffle_index_atl9l9i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_simd_eafgrv9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_soa_construct_8nun9tl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_soa_info_pq4q43l(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_soa_type_expr_go3p0n7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_arg_grgffgp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_value_arg_i5l23i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint64_t a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_vector_binary_bicu7u0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_vector_construction_fhm2mm5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_conversion_32a9cuf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_lane_conversion_chta07f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void * gm_f_vector_load_ssdvomk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_vector_method_hgn4sb1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_vector_shape_error_3invhhe(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_vector_shuffle_5ime7a7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_vector_type_8ofo3gl(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void * gm_f_vector_unary_2mbe97t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_without_call_t1kfc9e(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
extern const uint8_t gm_s091lteo20dar[64];
extern const uint8_t gm_s0b4nsmmmqukr[12];
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s0ugop4l95o1j[6];
extern const uint8_t gm_s1t08grtrn18m[9];
extern const uint8_t gm_s2jmrvqu8r8aq[58];
extern const uint8_t gm_s2mtoab140jp6[4];
extern const uint8_t gm_s398novfl4jrd[5];
extern const uint8_t gm_s6lddausbiitu[9];
extern const uint8_t gm_s73f72842a67m[45];
extern const uint8_t gm_s7f68g16j7a3o[6];
extern const uint8_t gm_s7uepsfl7trr3[70];
extern const uint8_t gm_s8guqcjcce9oc[22];
extern const uint8_t gm_s95g98ft0cnt1[155];
extern const uint8_t gm_s95gukpblkpua[33];
extern const uint8_t gm_s9scq8a60n125[74];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sajso5sujsruj[47];
extern const uint8_t gm_salin070nim24[30];
extern const uint8_t gm_salpeh0v5rqdf[8];
extern const uint8_t gm_satil70qt25hp[52];
extern const uint8_t gm_sbanjlvk3n14i[4];
extern const uint8_t gm_sbl3pbs9195ho[4];
extern const uint8_t gm_sbqoiitqtfv75[11];
extern const uint8_t gm_scqrlin5l437v[9];
extern const uint8_t gm_sd5imeip8klsb[40];
extern const uint8_t gm_sdide75tsakl8[66];
extern const uint8_t gm_sdinre9qvo2vq[28];
extern const uint8_t gm_sejjhtlr11ojd[60];
extern const uint8_t gm_sf1qp7r4ktsd1[11];
extern const uint8_t gm_sfmov9pb6t5lt[14];
extern const uint8_t gm_sfrjcsmisriv1[3];
extern const uint8_t gm_sh1to1i0u67lu[9];
extern const uint8_t gm_shb5mqv6etf1t[5];
extern const uint8_t gm_shm0623git4mr[327];
extern const uint8_t gm_shmgu63rvoce3[36];
extern const uint8_t gm_shne2m0cj5kgi[6];
extern const uint8_t gm_sioam4mlrjuar[52];
extern const uint8_t gm_sitfp4vhvj1ba[72];
extern const uint8_t gm_sj7s3tnnvob4e[9];
extern const uint8_t gm_sjb60mo5hejk2[6];
extern const uint8_t gm_sjvgkv27ovi7g[48];
extern const uint8_t gm_skdalh0o0hret[59];
extern const uint8_t gm_skfff1ladr223[23];
extern const uint8_t gm_skftljk77e6mu[4];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_slbn3chj3p1e1[49];
extern const uint8_t gm_slu3lsfv09uua[25];
extern const uint8_t gm_sm03uudimlmgi[59];
extern const uint8_t gm_sm3o0uerjumpm[4];
extern const uint8_t gm_smgfld86ipv3f[58];
extern const uint8_t gm_snc5ns3akncuq[6];
extern const uint8_t gm_snhskkd5kfgfa[31];
extern const uint8_t gm_spj0hqsg585et[14];
extern const uint8_t gm_sqes10usq4q5e[4];
extern const uint8_t gm_sqfrv3g5v9m2n[5];
extern const uint8_t gm_sqhh38dkd5qvg[43];
extern const uint8_t gm_sqn4pcenn3reh[63];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src7u9cfdjh7s0n[];
extern const char gm_srck5orfpjti60c[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srsa6dr3d9ut4[41];
extern const uint8_t gm_st4t0ufodvddp[59];
extern const uint8_t gm_stoidbi3vfa99[30];
extern const uint8_t gm_sut92jpq3kk88[11];
extern const uint8_t gm_sv8pauo1bhltl[9];
extern const uint8_t gm_svd9icup57bmm[27];
extern const uint8_t gm_svgoe41piv341[7];
extern const gm_tmpl gm_tpl05e80h89jqvd;
extern const gm_tmpl gm_tpl0q3rsndpu857;
extern const gm_tmpl gm_tpl1ut7q7afn48u;
extern const gm_tmpl gm_tpl2lqr5lnbpsvm;
extern const gm_tmpl gm_tpl4524ekle00ud;
extern const gm_tmpl gm_tpl4phv0vr9as7f;
extern const gm_tmpl gm_tpl68oh02lfq7s5;
extern const gm_tmpl gm_tpl6boglt2rkmh4;
extern const gm_tmpl gm_tpl91a2oiuc6ot7;
extern const gm_tmpl gm_tplbds86ojmcoik;
extern const gm_tmpl gm_tplbnm8984s77af;
extern const gm_tmpl gm_tplbqqnfekph537;
extern const gm_tmpl gm_tplellk9btkq1f2;
extern const gm_tmpl gm_tplgj9d3igk4i62;
extern const gm_tmpl gm_tplh5ff53j0j0d2;
extern const gm_tmpl gm_tpligtpf80arr7h;
extern const gm_tmpl gm_tplnt454lmhb7iv;
extern const gm_tmpl gm_tploj4uaptg6qnk;
extern const gm_tmpl gm_tplqnau1elkkogs;
extern const gm_tmpl gm_tplsh3mbm625h0g;
extern const gm_tmpl gm_tplsiiifs0eedvi;
extern const gm_tmpl gm_tplsk9b8o9jgshf;
extern const gm_tmpl gm_tplt1si3io9mdpe;
extern const gm_tmpl gm_tplurkkmrij8g4f;
extern const gm_tmpl gm_tplv8s8fgl2lggu;
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
inline __attribute__((aligned(4))) void gm_f_format_0gvqkke(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0gvqkke F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sd5imeip8klsb));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(39));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplt1si3io9mdpe), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_3ctg94v(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_3ctg94v F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_skfff1ladr223));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(22));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1ut7q7afn48u), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_4nuejdf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_4nuejdf F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s091lteo20dar));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(63));
    gm_t7neromnu8t62 t3 = GM_LD(gm_t7neromnu8t62, ((uint8_t *)a0));
    gm_f_format_0q4nncb((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpligtpf80arr7h), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_7cgva50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7cgva50 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_satil70qt25hp));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl0q3rsndpu857), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7r64gap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7r64gap F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snhskkd5kfgfa));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplh5ff53j0j0d2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9rhpk5t(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_9rhpk5t F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sdinre9qvo2vq));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(27));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplurkkmrij8g4f), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_d0lfl1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_d0lfl1k F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_stoidbi3vfa99));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(29));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2lqr5lnbpsvm), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_f8dhsig(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_f8dhsig F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sitfp4vhvj1ba));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(71));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl68oh02lfq7s5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_faaokv1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_faaokv1 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sdide75tsakl8));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(65));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl91a2oiuc6ot7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_frnomph(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_frnomph F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7uepsfl7trr3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(69));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbnm8984s77af), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ima6g5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ima6g5u F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s73f72842a67m));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(44));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplsk9b8o9jgshf), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jrngvku(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jrngvku F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shmgu63rvoce3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(35));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnt454lmhb7iv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l035o11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_l035o11 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2jmrvqu8r8aq));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(57));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplv8s8fgl2lggu), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l2fmok5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_l2fmok5 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqn4pcenn3reh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6boglt2rkmh4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_m1f4vhf(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_m1f4vhf F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s9scq8a60n125));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(73));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqnau1elkkogs), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mpg78oo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mpg78oo F;
    gm_t3877pk7lv748 gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjvgkv27ovi7g));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(47));
    gm_t3877pk7lv748 t3 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
    gm_f_format_fjqj2pj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbqqnfekph537), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mrlevsk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mrlevsk F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqhh38dkd5qvg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(42));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4phv0vr9as7f), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_nnfoc79(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nnfoc79 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svd9icup57bmm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_74pd3in((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplsiiifs0eedvi), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_pdashts(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_pdashts F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sejjhtlr11ojd));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(59));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplgj9d3igk4i62), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_pepv3ej(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_pepv3ej F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sm03uudimlmgi));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(58));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4524ekle00ud), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_pj28nou(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_pj28nou F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svd9icup57bmm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(26));
    gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
    gm_f_format_bqgl4hq((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplsiiifs0eedvi), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_q2uenf3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q2uenf3 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_slbn3chj3p1e1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(48));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplellk9btkq1f2), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_qu5usm7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qu5usm7 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s95g98ft0cnt1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(154));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplsh3mbm625h0g), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_rr83dbe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rr83dbe F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shm0623git4mr));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(326));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl05e80h89jqvd), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_s7ci5ul(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_s7ci5ul F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s95gukpblkpua));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tploj4uaptg6qnk), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ul5ufam(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ul5ufam F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_srsa6dr3d9ut4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(40));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbds86ojmcoik), t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_bool_ajhjgeg(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_bool_ajhjgeg F;
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
    uint8_t t6 = (uint8_t)((t5 == ((uint8_t)4ULL)));
    gm_t53at80pfdus3 t7;
    memset(&t7, 0, sizeof t7);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)4ULL));
    F.ret = t6;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_3jk89qu(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_float_3jk89qu F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    gm_t53at80pfdus3 gm_l3;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l3, t5, GM_CONV(gm_csguankko4lmhd, t6));
    {
        {
            uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)3ULL));
            if (!t8) {
                goto gm_lb2;
            }
            gm_l2 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 10, 56);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_vector_gkq6b88(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_vector_gkq6b88 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t gm_l2;
    gm_tb7u19loo3j87 gm_l3;
    void * t5 = gm_l0;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_f_vector_parts_crrfopm((void *)&gm_l3, t5, GM_CONV(gm_csguankko4lmhd, t6));
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
        gm_trap("no match arm matched the scrutinee", gm_srck5orfpjti60c, 25, 57);
    }
gm_lb1:;
    uint8_t t8 = gm_l2;
    F.ret = t8;
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_g3tddrb_2();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mask_ty_cj7ln0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_mask_ty_cj7ln0g F;
    void * gm_l0; /* tys */
    uint64_t gm_l1; /* len */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    void * t4 = gm_l0;
    gm_t88rcucg1b7uk t5 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t6 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t2, t3, t5);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d943vgp_2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_g3tddrb_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_61fb705_2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 88), ((uint8_t *)&F.l1), 88);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 88)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_61fb705_2 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)88ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tabb710d9mq2e gm_l2;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t4 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&gm_l2, t5);
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srclv3pj5es0f8o, 624, 12);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
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
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u128_haka99r F;
    GM_STACK_CHECK_SHALLOW(&F, 49u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    if (t2) {
        memset(&(*(gm_t9p38b040grms *)gm_out), 0, sizeof (*(gm_t9p38b040grms *)gm_out));
        goto gm_fdone;
    }
    {
        gm_tbci5ospk5nnn t3 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
        gm_f_mag_to_u128_f843dal((void *)&(*(gm_t9p38b040grms *)gm_out), &t3);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u64_m01t1da F;
    unsigned __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 233u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t9p38b040grms gm_l2;
    {
        gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
        gm_f_to_u128_haka99r((void *)&gm_l2, &t2);
    }
    uint8_t t3 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
    if (t3) {
        unsigned __int128 t4 = GM_LD(unsigned __int128, ((uint8_t *)&gm_l2));
        gm_l1 = t4;
    } else {
        gm_tf5srh4mp4b3i t5;
        memset(&t5, 0, sizeof t5);
        F.ret = t5;
        goto gm_fret;
    }
    unsigned __int128 t6 = gm_l1;
    gm_tf5srh4mp4b3i t7;
    uint64_t t8 = (uint64_t)t6;
    gm_tf5srh4mp4b3i t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t9), t8);
    ((uint8_t *)&t9)[8] = 1;
    gm_tf5srh4mp4b3i t10;
    memset(&t10, 0, sizeof t10);
    t7 = ((unsigned __int128)(t6) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t9 : t10;
    F.ret = t7;
    goto gm_fret;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1ULL));
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
inline __attribute__((aligned(4))) void * gm_f_lane_error_72hoonn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_lane_error_72hoonn F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* opn */
    gm_b16_8 gm_l2; /* what */
    gm_t88rcucg1b7uk gm_l3; /* vt */
    gm_tl874t2atnlh3 gm_l4; /* span */
    GM_STACK_CHECK(&F, 293u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l5;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l4;
        gm_b16_8 t4 = gm_l1;
        gm_b16_8 t5 = gm_l2;
        void * t6 = gm_l0;
        gm_t88rcucg1b7uk t7 = gm_l3;
        gm_b16_8 t8 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfeab279c16rr t9;
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t4);
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 16), t5);
        GM_ST(gm_b16_8, ((uint8_t *)&t9 + 32), t8);
        gm_f_format_ima6g5u((void *)&gm_l5, (void *)NULL, cx, NULL, &t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
        if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_src7u9cfdjh7s0n, 124, 20);
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_b0_1 t13 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t2, t3, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    gm_tl874t2atnlh3 t16 = gm_l4;
    void * t17 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t15, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t17;
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
inline __attribute__((aligned(4))) void * gm_f_lane_needs_a6gcpil(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_lane_needs_a6gcpil F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* n */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* what */
    gm_tl874t2atnlh3 gm_l4; /* span */
    gm_t88rcucg1b7uk gm_l5; /* lane */
    GM_STACK_CHECK(&F, 430u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tb7u19loo3j87 gm_l6;
    gm_tlcm4lvu3g1a8 gm_l7;
    gm_tabb710d9mq2e gm_l8;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, ((uint8_t *)t2));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_f_vector_parts_crrfopm((void *)&gm_l6, t4, GM_CONV(gm_csguankko4lmhd, t5));
    }
    uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
    if (!t6) gm_trap("unwrap of null", gm_src7u9cfdjh7s0n, 118, 13);
    gm_tlcm4lvu3g1a8 t7 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l6));
    gm_l7 = t7;
    gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
    gm_l5 = t8;
    {
        void * t9 = gm_l0;
        gm_tl874t2atnlh3 t10 = gm_l4;
        gm_b16_8 t11 = gm_l1;
        gm_b16_8 t12 = gm_l3;
        void * t13 = gm_l0;
        gm_t88rcucg1b7uk t14 = gm_l2;
        gm_b16_8 t15 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t16 = gm_l0;
        gm_t88rcucg1b7uk t17 = gm_l5;
        gm_b16_8 t18 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0uuhaljsf410 t19;
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t11);
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 16), t12);
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 32), t15);
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 48), t18);
        gm_f_format_0gvqkke((void *)&gm_l8, (void *)NULL, cx, NULL, &t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 8, gm_src7u9cfdjh7s0n, 119, 20);
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_b0_1 t23 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t9, t10, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t24 = gm_l0;
    void * t25 = GM_LD(void *, ((uint8_t *)t24));
    gm_tl874t2atnlh3 t26 = gm_l4;
    void * t27 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t25, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t27;
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mask_ty_cj7ln0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_shuffle_index_atl9l9i(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint64_t a3, uint8_t a4) {
    struct gm_fr_shuffle_index_atl9l9i F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    uint64_t gm_l3; /* len */
    uint8_t gm_l4; /* two */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* usize_t */
    gm_tf5srh4mp4b3i gm_l7; /* k */
    uint8_t gm_l8; /* int_ok */
    gm_t88rcucg1b7uk gm_l10; /* t */
    gm_tl874t2atnlh3 gm_l11; /* at */
    void * gm_l12; /* cell */
    unsigned __int128 gm_l13; /* n */
    gm_b16_8 gm_l14; /* which */
    uint64_t gm_l15; /* x */
    GM_STACK_CHECK(&F, 1460u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l17;
    uint8_t gm_l18;
    gm_t9qkh0rsooc8d gm_l19;
    gm_tabb710d9mq2e gm_l20;
    void * gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_tabb710d9mq2e gm_l24;
    gm_tabb710d9mq2e gm_l25;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    void * t22 = (void *)(((uint8_t *)t21) + 8);
    gm_l5 = t22;
    void * t23 = gm_l5;
    gm_t88rcucg1b7uk t24 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t23);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t24;
    gm_tf5srh4mp4b3i t25;
    memset(&t25, 0, sizeof t25);
    gm_l7 = t25;
    gm_l8 = ((uint8_t)0);
    void * t26 = gm_l1;
    {
        {
            uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t26) + 8) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)0ULL));
            if (!t28) {
                goto gm_lb2;
            }
            gm_l17 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l17 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 339, 8);
    }
gm_lb1:;
    uint8_t t29 = gm_l17;
    if (t29) {
        void * t30 = gm_l0;
        void * t31 = (void *)(((uint8_t *)t30) + 96);
        void * t32 = gm_l5;
        void * t33 = gm_l1;
        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t33));
        gm_t88rcucg1b7uk t35 = gm_l6;
        gm_t9qkh0rsooc8d t36 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t31, t32, t34, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t36;
        {
            {
                uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                if (t37) {
                    goto gm_lb5;
                }
                gm_l18 = ((uint8_t)1);
                goto gm_lb4;
            }
gm_lb5:;
            {
                gm_l18 = ((uint8_t)0);
                goto gm_lb4;
            }
gm_lb6:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 339, 42);
        }
gm_lb4:;
        uint8_t t38 = gm_l18;
        uint8_t t39 = (uint8_t)!t38;
        gm_l8 = t39;
    }
    uint8_t t40 = gm_l8;
    if (t40) {
        {
            {
                {
                    void * t41 = gm_l1;
                    uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t41) + 8) + 0));
                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)0ULL));
                    if (!t43) {
                        goto gm_lb8;
                    }
                    memmove(((uint8_t *)&F.l9), ((((uint8_t *)t41) + 8) + 8), 48);
                }
                {
                    gm_tf5srh4mp4b3i t44 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l9)));
                    gm_l7 = t44;
                }
                goto gm_lb7;
            }
gm_lb8:;
            gm_tf5srh4mp4b3i t45;
            memset(&t45, 0, sizeof t45);
            F.ret = t45;
            goto gm_fret;
        }
gm_lb7:;
    } else {
        void * t46 = gm_l5;
        void * t47 = gm_l1;
        gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t47));
        uint8_t t49 = gm_f_is_error_8k27gd1(t46, GM_CONV(gm_csguankko4lmhd, t48));
        if (t49) {
            gm_tf5srh4mp4b3i t50;
            memset(&t50, 0, sizeof t50);
            F.ret = t50;
            goto gm_fret;
        } else {
            void * t51 = gm_l0;
            void * t52 = (void *)(((uint8_t *)t51) + 96);
            void * t53 = gm_l0;
            void * t54 = GM_LD(void *, ((uint8_t *)t53));
            void * t55 = gm_l1;
            gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t55));
            gm_t88rcucg1b7uk t57 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t52, t54, t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t88rcucg1b7uk t58 = gm_l6;
            uint8_t t59 = (!gm_eq9m6mvtmgde(((uint8_t *)&t57), ((uint8_t *)&t58)));
            if (t59) {
                void * t60 = gm_l0;
                void * t61 = (void *)(((uint8_t *)t60) + 96);
                void * t62 = gm_l0;
                void * t63 = GM_LD(void *, ((uint8_t *)t62));
                void * t64 = gm_l1;
                gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t64));
                gm_t88rcucg1b7uk t66 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t61, t63, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t66;
                void * t67 = gm_l0;
                void * t68 = gm_l1;
                gm_tl874t2atnlh3 t69 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t68) + 72));
                void * t70 = gm_l0;
                gm_t88rcucg1b7uk t71 = gm_l10;
                gm_b16_8 t72 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t70, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tnpifv20irnph t73;
                GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
                gm_f_format_jrngvku((void *)&gm_l20, (void *)NULL, cx, NULL, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t74 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                if (!t74) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_src7u9cfdjh7s0n, 347, 26);
                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                gm_b0_1 t77 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t67, t69, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tf5srh4mp4b3i t78;
                memset(&t78, 0, sizeof t78);
                F.ret = t78;
                goto gm_fret;
            } else {
                {
                    {
                        void * t79 = gm_l1;
                        uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)t79) + 8) + 0));
                        uint8_t t81 = (uint8_t)(t80 == ((uint8_t)14ULL));
                        if (!t81) {
                            goto gm_lb10;
                        }
                        gm_tl874t2atnlh3 t82 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t79) + 8) + 8) + 16));
                        gm_l11 = t82;
                        {
                            {
                                void * t83 = gm_l0;
                                void * t84 = GM_LD(void *, ((uint8_t *)t83));
                                gm_tl874t2atnlh3 t85 = gm_l11;
                                void * t86 = gm_l1;
                                gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t86));
                                void * t88 = gm_f_const_value_fr4coi4((void *)NULL, cx, NULL, t84, t85, t87);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l21 = t88;
                                uint8_t t89 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l21)) != 0);
                                if (!t89) {
                                    goto gm_lb12;
                                }
                                void * t90 = GM_LD(void *, ((uint8_t *)&gm_l21));
                                gm_l12 = t90;
                                {
                                    {
                                        void * t91 = gm_l12;
                                        uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)t91) + 0));
                                        uint8_t t93 = (uint8_t)(t92 == ((uint8_t)0ULL));
                                        if (!t93) {
                                            goto gm_lb14;
                                        }
                                        unsigned __int128 t94 = GM_LD(unsigned __int128, (((uint8_t *)t91) + 16));
                                        gm_l13 = t94;
                                        unsigned __int128 t95 = gm_l13;
                                        gm_tf5srh4mp4b3i t96;
                                        uint64_t t97 = (uint64_t)t95;
                                        gm_tf5srh4mp4b3i t98;
                                        GM_ST(uint8_t, (uint8_t *)&t98 + 9, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t98 + 10, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t98 + 12, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t98), t97);
                                        ((uint8_t *)&t98)[8] = 1;
                                        gm_tf5srh4mp4b3i t99;
                                        memset(&t99, 0, sizeof t99);
                                        t96 = ((unsigned __int128)(t95) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t98 : t99;
                                        gm_l7 = t96;
                                        goto gm_lb13;
                                    }
gm_lb14:;
                                }
gm_lb13:;
                                goto gm_lb11;
                            }
gm_lb12:;
                        }
gm_lb11:;
                        goto gm_lb9;
                    }
gm_lb10:;
                }
gm_lb9:;
            }
        }
    }
    uint8_t t100 = gm_l4;
    if (t100) {
        {
            void * t101 = gm_l0;
            gm_t88rcucg1b7uk t102 = gm_l2;
            gm_b16_8 t103 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t101, t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t104 = gm_l3;
            uint64_t r105; bool o106 = __builtin_sub_overflow(t104, ((uint64_t)1ULL), &r105);
            if (o106) gm_trap("integer overflow in `-`", gm_src7u9cfdjh7s0n, 354, 105);
            uint64_t t107 = r105;
            gm_t3877pk7lv748 t108;
            GM_ST(gm_b16_8, ((uint8_t *)&t108 + 0), t103);
            GM_ST(uint64_t, ((uint8_t *)&t108 + 16), t107);
            gm_f_format_mpg78oo((void *)&gm_l22, (void *)NULL, cx, NULL, &t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
        if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_src7u9cfdjh7s0n, 354, 30);
        gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
        gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&t110));
        gm_l14 = t111;
    } else {
        {
            void * t112 = gm_l0;
            gm_t88rcucg1b7uk t113 = gm_l2;
            gm_b16_8 t114 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t112, t113);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t115 = gm_l3;
            uint64_t r116; bool o117 = __builtin_sub_overflow(t115, ((uint64_t)1ULL), &r116);
            if (o117) gm_trap("integer overflow in `-`", gm_src7u9cfdjh7s0n, 354, 174);
            uint64_t t118 = r116;
            gm_t3877pk7lv748 t119;
            GM_ST(gm_b16_8, ((uint8_t *)&t119 + 0), t114);
            GM_ST(uint64_t, ((uint8_t *)&t119 + 16), t118);
            gm_f_format_3ctg94v((void *)&gm_l23, (void *)NULL, cx, NULL, &t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
        if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_src7u9cfdjh7s0n, 354, 124);
        gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
        gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
        gm_l14 = t122;
    }
    {
        {
            uint8_t t123 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
            if (!t123) {
                goto gm_lb16;
            }
            uint64_t t124 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
            gm_l15 = t124;
            uint64_t t125 = gm_l15;
            uint64_t t126 = gm_l3;
            uint8_t t127 = (t125 >= t126);
            if (t127) {
                {
                    void * t128 = gm_l0;
                    void * t129 = gm_l1;
                    gm_tl874t2atnlh3 t130 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t129) + 72));
                    uint64_t t131 = gm_l15;
                    gm_b16_8 t132 = gm_l14;
                    gm_tf1bjg81u0glv t133;
                    GM_ST(uint64_t, ((uint8_t *)&t133 + 0), t131);
                    GM_ST(gm_b16_8, ((uint8_t *)&t133 + 8), t132);
                    gm_f_format_pj28nou((void *)&gm_l24, (void *)NULL, cx, NULL, &t133);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t134 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                    if (!t134) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_src7u9cfdjh7s0n, 357, 30);
                    gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                    gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&t135));
                    gm_b0_1 t137 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t128, t130, t136);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                gm_tf5srh4mp4b3i t138;
                memset(&t138, 0, sizeof t138);
                F.ret = t138;
                goto gm_fret;
            }
            uint64_t t139 = gm_l15;
            gm_tf5srh4mp4b3i t140;
            GM_ST(uint8_t, (uint8_t *)&t140 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t140 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t140 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t140), t139);
            ((uint8_t *)&t140)[8] = 1;
            F.ret = t140;
            goto gm_fret;
        }
gm_lb16:;
    }
gm_lb15:;
    {
        {
            {
                void * t141 = gm_l1;
                uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)t141) + 8) + 0));
                uint8_t t143 = (uint8_t)(t142 == ((uint8_t)0ULL));
                if (!t143) {
                    goto gm_lb18;
                }
                memmove(((uint8_t *)&F.l16), ((((uint8_t *)t141) + 8) + 8), 48);
            }
            {
                void * t144 = gm_l0;
                void * t145 = gm_l1;
                gm_tl874t2atnlh3 t146 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t145) + 72));
                gm_b16_8 t147 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t148 = gm_l14;
                gm_t6j6smj7qoctu t149;
                GM_ST(gm_b16_8, ((uint8_t *)&t149 + 0), t147);
                GM_ST(gm_b16_8, ((uint8_t *)&t149 + 16), t148);
                gm_f_format_nnfoc79((void *)&gm_l25, (void *)NULL, cx, NULL, &t149);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 8, gm_src7u9cfdjh7s0n, 363, 26);
                gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
                gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
                gm_b0_1 t153 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t144, t146, t152);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_tf5srh4mp4b3i t154;
            memset(&t154, 0, sizeof t154);
            F.ret = t154;
            goto gm_fret;
        }
gm_lb18:;
    }
gm_lb17:;
    void * t155 = gm_l0;
    void * t156 = gm_l1;
    gm_tl874t2atnlh3 t157 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t156) + 72));
    gm_b16_8 t158;
    GM_ST(void *, (uint8_t *)&t158, (void *)(gm_skdalh0o0hret));
    GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(58));
    gm_b0_1 t159 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t155, t157, t158);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tf5srh4mp4b3i t160;
    memset(&t160, 0, sizeof t160);
    F.ret = t160;
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
__attribute__((aligned(4))) void * gm_f_vector_binary_bicu7u0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_vector_binary_bicu7u0 F;
    void * gm_l0; /* cx */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* lhs */
    void * gm_l3; /* rhs */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* vt */
    gm_tlcm4lvu3g1a8 gm_l7; /* parts */
    gm_t88rcucg1b7uk gm_l8; /* lane */
    gm_b16_8 gm_l9; /* opn */
    void * gm_l10; /* r */
    gm_t88rcucg1b7uk gm_l11; /* t */
    gm_t9qkh0rsooc8d gm_l12; /* f */
    gm_ts6oqppjh0ba3 gm_l13; /* top */
    void * gm_l14; /* r */
    gm_t88rcucg1b7uk gm_l15; /* rt */
    gm_b16_8 gm_l16; /* hint */
    uint8_t gm_l17; /* lane_int */
    uint8_t gm_l18; /* lane_float */
    uint8_t gm_l19; /* numeric */
    gm_b16_8 gm_l20; /* m */
    gm_t9qkh0rsooc8d gm_l21; /* f */
    gm_t9qkh0rsooc8d gm_l22; /* f */
    gm_t9qkh0rsooc8d gm_l23; /* f */
    gm_ts6oqppjh0ba3 gm_l24; /* top */
    gm_ts6oqppjh0ba3 gm_l25; /* top */
    GM_STACK_CHECK(&F, 3091u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tb7u19loo3j87 gm_l26;
    gm_tabb710d9mq2e gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_tabb710d9mq2e gm_l30;
    gm_tabb710d9mq2e gm_l31;
    uint8_t gm_l32;
    gm_tabb710d9mq2e gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_tabb710d9mq2e gm_l35;
    {
        void * t56 = gm_l0;
        void * t57 = GM_LD(void *, ((uint8_t *)t56));
        void * t58 = (void *)(((uint8_t *)t57) + 8);
        gm_l5 = t58;
        void * t59 = gm_l0;
        void * t60 = (void *)(((uint8_t *)t59) + 96);
        void * t61 = gm_l0;
        void * t62 = GM_LD(void *, ((uint8_t *)t61));
        void * t63 = gm_l2;
        gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t63));
        gm_t88rcucg1b7uk t65 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t60, t62, t64);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t65;
        void * t66 = gm_l5;
        gm_t88rcucg1b7uk t67 = gm_l6;
        gm_f_vector_parts_crrfopm((void *)&gm_l26, t66, GM_CONV(gm_csguankko4lmhd, t67));
        uint8_t t68 = (uint8_t)((((uint8_t *)&gm_l26))[16] != 0);
        if (t68) {
            gm_tlcm4lvu3g1a8 t69 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l26));
            gm_l7 = t69;
        } else {
            void * t70 = gm_l0;
            void * t71 = GM_LD(void *, ((uint8_t *)t70));
            gm_tl874t2atnlh3 t72 = gm_l4;
            void * t73 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t71, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t73;
            goto gm_fret;
        }
        gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
        gm_l8 = t74;
        gm_ts6oqppjh0ba3 t75 = gm_l1;
        gm_b16_8 t76 = gm_f_binop_text_0ljfc0r(t75);
        gm_l9 = t76;
        {
            {
                {
                    {
                        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t78 = (uint8_t)(t77 == ((uint8_t)20ULL));
                        if (!t78) {
                            goto gm_lb4;
                        }
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t80 = (uint8_t)(t79 == ((uint8_t)21ULL));
                        if (!t80) {
                            goto gm_lb5;
                        }
                        goto gm_lb3;
                    }
gm_lb5:;
                    goto gm_lb2;
                }
gm_lb3:;
                void * t81 = gm_l0;
                void * t82 = gm_l3;
                gm_tkn42u7na3t81 t83;
                memset(&t83, 0, sizeof t83);
                void * t84 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t81, t82, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t85 = gm_l0;
                gm_tl874t2atnlh3 t86 = gm_l4;
                gm_b16_8 t87 = gm_l9;
                gm_tnpifv20irnph t88;
                GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t87);
                gm_f_format_frnomph((void *)&gm_l27, (void *)NULL, cx, NULL, t88);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_src7u9cfdjh7s0n, 37, 28);
                gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
                gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                gm_b0_1 t92 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t85, t86, t91);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t93 = gm_l0;
                void * t94 = GM_LD(void *, ((uint8_t *)t93));
                gm_tl874t2atnlh3 t95 = gm_l4;
                void * t96 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t94, t95);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t96;
                goto gm_fret;
            }
gm_lb2:;
            {
                {
                    {
                        uint8_t t97 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t98 = (uint8_t)(t97 == ((uint8_t)14ULL));
                        if (!t98) {
                            goto gm_lb8;
                        }
                        goto gm_lb7;
                    }
gm_lb8:;
                    {
                        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t100 = (uint8_t)(t99 == ((uint8_t)15ULL));
                        if (!t100) {
                            goto gm_lb9;
                        }
                        goto gm_lb7;
                    }
gm_lb9:;
                    {
                        uint8_t t101 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t102 = (uint8_t)(t101 == ((uint8_t)10ULL));
                        if (!t102) {
                            goto gm_lb10;
                        }
                        goto gm_lb7;
                    }
gm_lb10:;
                    {
                        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t104 = (uint8_t)(t103 == ((uint8_t)11ULL));
                        if (!t104) {
                            goto gm_lb11;
                        }
                        goto gm_lb7;
                    }
gm_lb11:;
                    goto gm_lb6;
                }
gm_lb7:;
                void * t105 = gm_l0;
                void * t106 = gm_l3;
                gm_tkn42u7na3t81 t107;
                memset(&t107, 0, sizeof t107);
                void * t108 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t105, t106, t107);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t109 = gm_l0;
                gm_tl874t2atnlh3 t110 = gm_l4;
                gm_b16_8 t111 = gm_l9;
                gm_tnpifv20irnph t112;
                GM_ST(gm_b16_8, ((uint8_t *)&t112 + 0), t111);
                gm_f_format_7r64gap((void *)&gm_l28, (void *)NULL, cx, NULL, t112);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t113 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                if (!t113) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_src7u9cfdjh7s0n, 42, 28);
                gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
                gm_b0_1 t116 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t109, t110, t115);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t117 = gm_l0;
                void * t118 = GM_LD(void *, ((uint8_t *)t117));
                gm_tl874t2atnlh3 t119 = gm_l4;
                void * t120 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t118, t119);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t120;
                goto gm_fret;
            }
gm_lb6:;
            {
                {
                    {
                        uint8_t t121 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t122 = (uint8_t)(t121 == ((uint8_t)3ULL));
                        if (!t122) {
                            goto gm_lb14;
                        }
                        goto gm_lb13;
                    }
gm_lb14:;
                    {
                        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t124 = (uint8_t)(t123 == ((uint8_t)6ULL));
                        if (!t124) {
                            goto gm_lb15;
                        }
                        goto gm_lb13;
                    }
gm_lb15:;
                    goto gm_lb12;
                }
gm_lb13:;
                void * t125 = gm_l0;
                void * t126 = gm_l3;
                gm_tkn42u7na3t81 t127;
                memset(&t127, 0, sizeof t127);
                void * t128 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t125, t126, t127);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t128;
                void * t129 = gm_l5;
                gm_t88rcucg1b7uk t130 = gm_l8;
                uint8_t t131 = gm_f_is_integer_8d4f9ek(t129, GM_CONV(gm_csguankko4lmhd, t130));
                uint8_t t132 = (uint8_t)!t131;
                if (t132) {
                    {
                        void * t133 = gm_l0;
                        gm_tl874t2atnlh3 t134 = gm_l4;
                        gm_b16_8 t135 = gm_l9;
                        void * t136 = gm_l0;
                        gm_t88rcucg1b7uk t137 = gm_l6;
                        gm_b16_8 t138 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t136, t137);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6j6smj7qoctu t139;
                        GM_ST(gm_b16_8, ((uint8_t *)&t139 + 0), t135);
                        GM_ST(gm_b16_8, ((uint8_t *)&t139 + 16), t138);
                        gm_f_format_q2uenf3((void *)&gm_l29, (void *)NULL, cx, NULL, &t139);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t140 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                        if (!t140) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_src7u9cfdjh7s0n, 48, 32);
                        gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
                        gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
                        gm_b0_1 t143 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t133, t134, t142);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t144 = gm_l0;
                    void * t145 = GM_LD(void *, ((uint8_t *)t144));
                    gm_tl874t2atnlh3 t146 = gm_l4;
                    void * t147 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t145, t146);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t147;
                    goto gm_fret;
                }
                gm_ts6oqppjh0ba3 t148 = gm_l1;
                gm_t9qkh0rsooc8d t149;
                GM_ST(uint8_t, ((uint8_t *)&t149 + 0), ((uint8_t)3ULL));
                uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&t148) + 0));
                uint8_t t151 = (uint8_t)(t150 == ((uint8_t)3ULL));
                if (t151) {
                    gm_t9qkh0rsooc8d t152 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t148) + 1));
                    t151 = (uint8_t)gm_eqehhdtpeti1(((uint8_t *)&t152), ((uint8_t *)&t149));
                }
                uint8_t t153 = (uint8_t)(t151);
                gm_ts6oqppjh0ba3 t154;
                GM_ST(uint8_t, ((uint8_t *)&t154 + 0), ((uint8_t)3ULL));
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t154 + 1), t149);
                if (t153) {
                    void * t155 = gm_l0;
                    gm_tl874t2atnlh3 t156 = gm_l4;
                    gm_b16_8 t157;
                    GM_ST(void *, (uint8_t *)&t157, (void *)(gm_st4t0ufodvddp));
                    GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(58));
                    gm_b0_1 t158 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t155, t156, t157);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t159 = gm_l0;
                    void * t160 = GM_LD(void *, ((uint8_t *)t159));
                    gm_tl874t2atnlh3 t161 = gm_l4;
                    void * t162 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t160, t161);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t162;
                    goto gm_fret;
                }
                {
                    {
                        void * t163 = gm_l0;
                        void * t164 = (void *)(((uint8_t *)t163) + 96);
                        void * t165 = gm_l5;
                        void * t166 = gm_l10;
                        gm_t88rcucg1b7uk t167 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t166));
                        gm_t9qkh0rsooc8d t168;
                        GM_ST(uint8_t, ((uint8_t *)&t168 + 0), ((uint8_t)1ULL));
                        uint8_t t169 = gm_f_require_unsigned_0fhlknt((void *)NULL, cx, NULL, t164, t165, t167, t168);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t170 = (uint8_t)!t169;
                        if (!t170) {
                            goto gm_lb17;
                        }
                        void * t171 = gm_l5;
                        void * t172 = gm_l10;
                        gm_t88rcucg1b7uk t173 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t172));
                        uint8_t t174 = gm_f_is_error_8k27gd1(t171, GM_CONV(gm_csguankko4lmhd, t173));
                        uint8_t t175 = (uint8_t)!t174;
                        if (!t175) {
                            goto gm_lb17;
                        }
                        void * t176 = gm_l0;
                        void * t177 = (void *)(((uint8_t *)t176) + 96);
                        void * t178 = gm_l0;
                        void * t179 = GM_LD(void *, ((uint8_t *)t178));
                        void * t180 = gm_l10;
                        gm_t88rcucg1b7uk t181 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t180));
                        gm_t88rcucg1b7uk t182 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t177, t179, t181);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l11 = t182;
                        void * t183 = gm_l0;
                        void * t184 = gm_l10;
                        gm_tl874t2atnlh3 t185 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t184) + 72));
                        void * t186 = gm_l0;
                        gm_t88rcucg1b7uk t187 = gm_l11;
                        gm_b16_8 t188 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t186, t187);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t189;
                        GM_ST(gm_b16_8, ((uint8_t *)&t189 + 0), t188);
                        gm_f_format_pdashts((void *)&gm_l30, (void *)NULL, cx, NULL, t189);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t190 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                        if (!t190) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_src7u9cfdjh7s0n, 57, 34);
                        gm_b16_8 t191 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                        gm_b16_8 t192 = GM_LD(gm_b16_8, ((uint8_t *)&t191));
                        gm_b0_1 t193 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t183, t185, t192);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb16;
                    }
gm_lb17:;
                }
gm_lb16:;
                {
                    {
                        uint8_t t194 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t195 = (uint8_t)(t194 == ((uint8_t)3ULL));
                        if (!t195) {
                            goto gm_lb19;
                        }
                        gm_t9qkh0rsooc8d t196 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                        gm_l12 = t196;
                        gm_t9qkh0rsooc8d t197 = gm_l12;
                        gm_ts6oqppjh0ba3 t198;
                        GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t198 + 1), t197);
                        gm_l13 = t198;
                        goto gm_lb18;
                    }
gm_lb19:;
                    gm_ts6oqppjh0ba3 t199;
                    GM_ST(uint8_t, (uint8_t *)&t199 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t199 + 0), ((uint8_t)6ULL));
                    gm_l13 = t199;
                }
gm_lb18:;
                {
                    gm_t88rcucg1b7uk t200 = gm_l6;
                    gm_ts6oqppjh0ba3 t201 = gm_l13;
                    void * t202 = gm_l2;
                    void * t203 = gm_l10;
                    gm_trl9d7m1k5lbd t204;
                    GM_ST(uint16_t, (uint8_t *)&t204 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t204 + 4, 0);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t204 + 0), t201);
                    GM_ST(void *, ((uint8_t *)&t204 + 8), t202);
                    GM_ST(void *, ((uint8_t *)&t204 + 16), t203);
                    gm_tl874t2atnlh3 t205 = gm_l4;
                    gm_to0f5qn75cgg8 t206;
                    memset(&t206, 0, sizeof t206);
                    GM_ST(uint8_t, ((uint8_t *)&t206 + 0), ((uint8_t)30ULL));
                    GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t206 + 8), t204);
                    void * t207 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t200, &t206, t205);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t207;
                    goto gm_fret;
                }
            }
gm_lb12:;
            {
                goto gm_lb1;
            }
gm_lb20:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 34, 5);
        }
gm_lb1:;
    }
    {
        void * t208 = gm_l0;
        void * t209 = gm_l3;
        gm_t88rcucg1b7uk t210 = gm_l6;
        gm_tkn42u7na3t81 t211;
        GM_ST(uint8_t, (uint8_t *)&t211 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t211 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t211), t210);
        ((uint8_t *)&t211)[4] = 1;
        void * t212 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t208, t209, t211);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t212;
        void * t213 = gm_l0;
        void * t214 = (void *)(((uint8_t *)t213) + 96);
        void * t215 = gm_l0;
        void * t216 = GM_LD(void *, ((uint8_t *)t215));
        void * t217 = gm_l14;
        gm_t88rcucg1b7uk t218 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t217));
        gm_t88rcucg1b7uk t219 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t214, t216, t218);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t219;
        void * t220 = gm_l5;
        gm_t88rcucg1b7uk t221 = gm_l15;
        uint8_t t222 = gm_f_is_error_8k27gd1(t220, GM_CONV(gm_csguankko4lmhd, t221));
        if (t222) {
            void * t223 = gm_l0;
            void * t224 = GM_LD(void *, ((uint8_t *)t223));
            gm_tl874t2atnlh3 t225 = gm_l4;
            void * t226 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t224, t225);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t226;
            goto gm_fret;
        }
        gm_t88rcucg1b7uk t227 = gm_l15;
        gm_t88rcucg1b7uk t228 = gm_l6;
        uint8_t t229 = (!gm_eq9m6mvtmgde(((uint8_t *)&t227), ((uint8_t *)&t228)));
        if (t229) {
            void * t230 = gm_l5;
            gm_t88rcucg1b7uk t231 = gm_l15;
            uint8_t t232 = gm_f_is_vector_gkq6b88(t230, GM_CONV(gm_csguankko4lmhd, t231));
            if (t232) {
                gm_b16_8 t233;
                GM_ST(void *, (uint8_t *)&t233, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t233 + 8, (uintptr_t)(0));
                gm_l16 = t233;
            } else {
                gm_b16_8 t234;
                GM_ST(void *, (uint8_t *)&t234, (void *)(gm_smgfld86ipv3f));
                GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(57));
                gm_l16 = t234;
            }
            {
                void * t235 = gm_l0;
                gm_tl874t2atnlh3 t236 = gm_l4;
                gm_b16_8 t237 = gm_l9;
                void * t238 = gm_l0;
                gm_t88rcucg1b7uk t239 = gm_l6;
                gm_b16_8 t240 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t238, t239);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t241 = gm_l0;
                gm_t88rcucg1b7uk t242 = gm_l15;
                gm_b16_8 t243 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t241, t242);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t244 = gm_l16;
                gm_t0uuhaljsf410 t245;
                GM_ST(gm_b16_8, ((uint8_t *)&t245 + 0), t237);
                GM_ST(gm_b16_8, ((uint8_t *)&t245 + 16), t240);
                GM_ST(gm_b16_8, ((uint8_t *)&t245 + 32), t243);
                GM_ST(gm_b16_8, ((uint8_t *)&t245 + 48), t244);
                gm_f_format_pepv3ej((void *)&gm_l31, (void *)NULL, cx, NULL, &t245);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t246 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                if (!t246) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_src7u9cfdjh7s0n, 69, 24);
                gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                gm_b16_8 t248 = GM_LD(gm_b16_8, ((uint8_t *)&t247));
                gm_b0_1 t249 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t235, t236, t248);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t250 = gm_l0;
            void * t251 = GM_LD(void *, ((uint8_t *)t250));
            gm_tl874t2atnlh3 t252 = gm_l4;
            void * t253 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t251, t252);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t253;
            goto gm_fret;
        }
        void * t254 = gm_l5;
        gm_t88rcucg1b7uk t255 = gm_l8;
        uint8_t t256 = gm_f_is_integer_8d4f9ek(t254, GM_CONV(gm_csguankko4lmhd, t255));
        gm_l17 = t256;
    }
    {
        void * t257 = gm_l5;
        gm_t88rcucg1b7uk t258 = gm_l8;
        uint8_t t259 = gm_f_is_float_3jk89qu(t257, GM_CONV(gm_csguankko4lmhd, t258));
        gm_l18 = t259;
        uint8_t t260 = gm_l17;
        if (t260) {
            gm_l32 = ((uint8_t)1);
        } else {
            uint8_t t261 = gm_l18;
            gm_l32 = t261;
        }
        uint8_t t262 = gm_l32;
        gm_l19 = t262;
        {
            {
                {
                    {
                        uint8_t t263 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t264 = (uint8_t)(t263 == ((uint8_t)16ULL));
                        if (!t264) {
                            goto gm_lb24;
                        }
                        goto gm_lb23;
                    }
gm_lb24:;
                    {
                        uint8_t t265 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t266 = (uint8_t)(t265 == ((uint8_t)17ULL));
                        if (!t266) {
                            goto gm_lb25;
                        }
                        goto gm_lb23;
                    }
gm_lb25:;
                    {
                        uint8_t t267 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t268 = (uint8_t)(t267 == ((uint8_t)18ULL));
                        if (!t268) {
                            goto gm_lb26;
                        }
                        goto gm_lb23;
                    }
gm_lb26:;
                    {
                        uint8_t t269 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t270 = (uint8_t)(t269 == ((uint8_t)19ULL));
                        if (!t270) {
                            goto gm_lb27;
                        }
                        goto gm_lb23;
                    }
gm_lb27:;
                    goto gm_lb22;
                }
gm_lb23:;
                {
                    {
                        uint8_t t271 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t272 = (uint8_t)(t271 == ((uint8_t)16ULL));
                        if (!t272) {
                            goto gm_lb29;
                        }
                        gm_b16_8 t273;
                        GM_ST(void *, (uint8_t *)&t273, (void *)(gm_sh1to1i0u67lu));
                        GM_ST(uintptr_t, (uint8_t *)&t273 + 8, (uintptr_t)(8));
                        gm_l20 = t273;
                        goto gm_lb28;
                    }
gm_lb29:;
                    {
                        uint8_t t274 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t275 = (uint8_t)(t274 == ((uint8_t)17ULL));
                        if (!t275) {
                            goto gm_lb30;
                        }
                        gm_b16_8 t276;
                        GM_ST(void *, (uint8_t *)&t276, (void *)(gm_sv8pauo1bhltl));
                        GM_ST(uintptr_t, (uint8_t *)&t276 + 8, (uintptr_t)(8));
                        gm_l20 = t276;
                        goto gm_lb28;
                    }
gm_lb30:;
                    {
                        uint8_t t277 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t278 = (uint8_t)(t277 == ((uint8_t)18ULL));
                        if (!t278) {
                            goto gm_lb31;
                        }
                        gm_b16_8 t279;
                        GM_ST(void *, (uint8_t *)&t279, (void *)(gm_s1t08grtrn18m));
                        GM_ST(uintptr_t, (uint8_t *)&t279 + 8, (uintptr_t)(8));
                        gm_l20 = t279;
                        goto gm_lb28;
                    }
gm_lb31:;
                    {
                        gm_b16_8 t280;
                        GM_ST(void *, (uint8_t *)&t280, (void *)(gm_scqrlin5l437v));
                        GM_ST(uintptr_t, (uint8_t *)&t280 + 8, (uintptr_t)(8));
                        gm_l20 = t280;
                        goto gm_lb28;
                    }
gm_lb32:;
                    gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 77, 25);
                }
gm_lb28:;
                void * t281 = gm_l0;
                gm_tl874t2atnlh3 t282 = gm_l4;
                gm_b16_8 t283 = gm_l20;
                gm_tnpifv20irnph t284;
                GM_ST(gm_b16_8, ((uint8_t *)&t284 + 0), t283);
                gm_f_format_l2fmok5((void *)&gm_l33, (void *)NULL, cx, NULL, t284);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t285 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                if (!t285) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_src7u9cfdjh7s0n, 83, 28);
                gm_b16_8 t286 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&t286));
                gm_b0_1 t288 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t281, t282, t287);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t289 = gm_l0;
                void * t290 = GM_LD(void *, ((uint8_t *)t289));
                gm_tl874t2atnlh3 t291 = gm_l4;
                void * t292 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t290, t291);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t292;
                goto gm_fret;
            }
gm_lb22:;
            {
                {
                    {
                        uint8_t t293 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t294 = (uint8_t)(t293 == ((uint8_t)0ULL));
                        if (!t294) {
                            goto gm_lb35;
                        }
                        gm_t9qkh0rsooc8d t295 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                        gm_l21 = t295;
                        goto gm_lb34;
                    }
gm_lb35:;
                    {
                        uint8_t t296 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t297 = (uint8_t)(t296 == ((uint8_t)1ULL));
                        if (!t297) {
                            goto gm_lb36;
                        }
                        gm_t9qkh0rsooc8d t298 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                        gm_l21 = t298;
                        goto gm_lb34;
                    }
gm_lb36:;
                    {
                        uint8_t t299 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t300 = (uint8_t)(t299 == ((uint8_t)2ULL));
                        if (!t300) {
                            goto gm_lb37;
                        }
                        gm_t9qkh0rsooc8d t301 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                        gm_l21 = t301;
                        goto gm_lb34;
                    }
gm_lb37:;
                    goto gm_lb33;
                }
gm_lb34:;
                uint8_t t302 = gm_l19;
                uint8_t t303 = (uint8_t)!t302;
                if (t303) {
                    void * t304 = gm_l0;
                    gm_b16_8 t305 = gm_l9;
                    gm_b16_8 t306;
                    GM_ST(void *, (uint8_t *)&t306, (void *)(gm_sfmov9pb6t5lt));
                    GM_ST(uintptr_t, (uint8_t *)&t306 + 8, (uintptr_t)(13));
                    gm_t88rcucg1b7uk t307 = gm_l6;
                    gm_tl874t2atnlh3 t308 = gm_l4;
                    void * t309 = gm_f_lane_error_72hoonn((void *)NULL, cx, NULL, t304, t305, t306, t307, t308);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t309;
                    goto gm_fret;
                }
                gm_t9qkh0rsooc8d t310 = gm_l21;
                uint8_t t311 = GM_LD(uint8_t, (((uint8_t *)&t310) + 0));
                uint8_t t312 = (uint8_t)((t311 == ((uint8_t)3ULL)));
                gm_t9qkh0rsooc8d t313;
                GM_ST(uint8_t, ((uint8_t *)&t313 + 0), ((uint8_t)3ULL));
                if (t312) {
                    void * t314 = gm_l0;
                    gm_tl874t2atnlh3 t315 = gm_l4;
                    gm_b16_8 t316 = gm_l9;
                    gm_tnpifv20irnph t317;
                    GM_ST(gm_b16_8, ((uint8_t *)&t317 + 0), t316);
                    gm_f_format_l035o11((void *)&gm_l34, (void *)NULL, cx, NULL, t317);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t318 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                    if (!t318) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_src7u9cfdjh7s0n, 89, 32);
                    gm_b16_8 t319 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                    gm_b16_8 t320 = GM_LD(gm_b16_8, ((uint8_t *)&t319));
                    gm_b0_1 t321 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t314, t315, t320);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t322 = gm_l0;
                    void * t323 = GM_LD(void *, ((uint8_t *)t322));
                    gm_tl874t2atnlh3 t324 = gm_l4;
                    void * t325 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t323, t324);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t325;
                    goto gm_fret;
                }
                {
                    {
                        gm_t9qkh0rsooc8d t326 = gm_l21;
                        uint8_t t327 = GM_LD(uint8_t, (((uint8_t *)&t326) + 0));
                        uint8_t t328 = (uint8_t)(!(t327 == ((uint8_t)0ULL)));
                        if (!t328) {
                            goto gm_lb39;
                        }
                        uint8_t t329 = gm_l17;
                        uint8_t t330 = (uint8_t)!t329;
                        if (!t330) {
                            goto gm_lb39;
                        }
                        void * t331 = gm_l0;
                        gm_b16_8 t332 = gm_l9;
                        gm_b16_8 t333;
                        GM_ST(void *, (uint8_t *)&t333, (void *)(gm_spj0hqsg585et));
                        GM_ST(uintptr_t, (uint8_t *)&t333 + 8, (uintptr_t)(13));
                        gm_t88rcucg1b7uk t334 = gm_l6;
                        gm_tl874t2atnlh3 t335 = gm_l4;
                        gm_t9qkh0rsooc8d t336;
                        GM_ST(uint8_t, ((uint8_t *)&t336 + 0), ((uint8_t)0ULL));
                        void * t337 = gm_f_lane_error_72hoonn((void *)NULL, cx, NULL, t331, t332, t333, t334, t335);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t337;
                        goto gm_fret;
                    }
gm_lb39:;
                }
gm_lb38:;
                {
                    {
                        uint8_t t338 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t339 = (uint8_t)(t338 == ((uint8_t)0ULL));
                        if (!t339) {
                            goto gm_lb41;
                        }
                        gm_t9qkh0rsooc8d t340 = gm_l21;
                        gm_ts6oqppjh0ba3 t341;
                        GM_ST(uint8_t, ((uint8_t *)&t341 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t341 + 1), t340);
                        gm_l24 = t341;
                        goto gm_lb40;
                    }
gm_lb41:;
                    {
                        uint8_t t342 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t343 = (uint8_t)(t342 == ((uint8_t)1ULL));
                        if (!t343) {
                            goto gm_lb42;
                        }
                        gm_t9qkh0rsooc8d t344 = gm_l21;
                        gm_ts6oqppjh0ba3 t345;
                        GM_ST(uint8_t, ((uint8_t *)&t345 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t345 + 1), t344);
                        gm_l24 = t345;
                        goto gm_lb40;
                    }
gm_lb42:;
                    {
                        gm_t9qkh0rsooc8d t346 = gm_l21;
                        gm_ts6oqppjh0ba3 t347;
                        GM_ST(uint8_t, ((uint8_t *)&t347 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t347 + 1), t346);
                        gm_l24 = t347;
                        goto gm_lb40;
                    }
gm_lb43:;
                    gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 93, 30);
                }
gm_lb40:;
                {
                    gm_t88rcucg1b7uk t348 = gm_l6;
                    gm_ts6oqppjh0ba3 t349 = gm_l24;
                    void * t350 = gm_l2;
                    void * t351 = gm_l14;
                    gm_trl9d7m1k5lbd t352;
                    GM_ST(uint16_t, (uint8_t *)&t352 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t352 + 4, 0);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t352 + 0), t349);
                    GM_ST(void *, ((uint8_t *)&t352 + 8), t350);
                    GM_ST(void *, ((uint8_t *)&t352 + 16), t351);
                    gm_tl874t2atnlh3 t353 = gm_l4;
                    gm_to0f5qn75cgg8 t354;
                    memset(&t354, 0, sizeof t354);
                    GM_ST(uint8_t, ((uint8_t *)&t354 + 0), ((uint8_t)30ULL));
                    GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t354 + 8), t352);
                    void * t355 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t348, &t354, t353);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t355;
                    goto gm_fret;
                }
            }
gm_lb33:;
            {
                uint8_t t356 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                uint8_t t357 = (uint8_t)(t356 == ((uint8_t)4ULL));
                if (!t357) {
                    goto gm_lb44;
                }
                uint8_t t358 = gm_l18;
                if (t358) {
                    gm_t88rcucg1b7uk t359 = gm_l6;
                    void * t360 = gm_l2;
                    void * t361 = gm_l14;
                    gm_ts6oqppjh0ba3 t362;
                    GM_ST(uint8_t, (uint8_t *)&t362 + 1, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t362 + 0), ((uint8_t)4ULL));
                    gm_trl9d7m1k5lbd t363;
                    GM_ST(uint16_t, (uint8_t *)&t363 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t363 + 4, 0);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t363 + 0), t362);
                    GM_ST(void *, ((uint8_t *)&t363 + 8), t360);
                    GM_ST(void *, ((uint8_t *)&t363 + 16), t361);
                    gm_tl874t2atnlh3 t364 = gm_l4;
                    gm_to0f5qn75cgg8 t365;
                    memset(&t365, 0, sizeof t365);
                    GM_ST(uint8_t, ((uint8_t *)&t365 + 0), ((uint8_t)30ULL));
                    GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t365 + 8), t363);
                    void * t366 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t359, &t365, t364);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t366;
                    goto gm_fret;
                }
                goto gm_lb21;
            }
gm_lb44:;
            {
                {
                    {
                        uint8_t t367 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t368 = (uint8_t)(t367 == ((uint8_t)7ULL));
                        if (!t368) {
                            goto gm_lb47;
                        }
                        goto gm_lb46;
                    }
gm_lb47:;
                    {
                        uint8_t t369 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t370 = (uint8_t)(t369 == ((uint8_t)8ULL));
                        if (!t370) {
                            goto gm_lb48;
                        }
                        goto gm_lb46;
                    }
gm_lb48:;
                    {
                        uint8_t t371 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t372 = (uint8_t)(t371 == ((uint8_t)9ULL));
                        if (!t372) {
                            goto gm_lb49;
                        }
                        goto gm_lb46;
                    }
gm_lb49:;
                    goto gm_lb45;
                }
gm_lb46:;
                uint8_t t373 = gm_l18;
                if (t373) {
                    void * t374 = gm_l0;
                    gm_b16_8 t375 = gm_l9;
                    gm_b16_8 t376;
                    GM_ST(void *, (uint8_t *)&t376, (void *)(gm_slu3lsfv09uua));
                    GM_ST(uintptr_t, (uint8_t *)&t376 + 8, (uintptr_t)(24));
                    gm_t88rcucg1b7uk t377 = gm_l6;
                    gm_tl874t2atnlh3 t378 = gm_l4;
                    void * t379 = gm_f_lane_error_72hoonn((void *)NULL, cx, NULL, t374, t375, t376, t377, t378);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t379;
                    goto gm_fret;
                }
                {
                    {
                        uint8_t t380 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t381 = (uint8_t)(t380 == ((uint8_t)7ULL));
                        if (!t381) {
                            goto gm_lb51;
                        }
                        gm_ts6oqppjh0ba3 t382;
                        GM_ST(uint8_t, (uint8_t *)&t382 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t382 + 0), ((uint8_t)7ULL));
                        gm_l25 = t382;
                        goto gm_lb50;
                    }
gm_lb51:;
                    {
                        uint8_t t383 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                        uint8_t t384 = (uint8_t)(t383 == ((uint8_t)8ULL));
                        if (!t384) {
                            goto gm_lb52;
                        }
                        gm_ts6oqppjh0ba3 t385;
                        GM_ST(uint8_t, (uint8_t *)&t385 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t385 + 0), ((uint8_t)8ULL));
                        gm_l25 = t385;
                        goto gm_lb50;
                    }
gm_lb52:;
                    {
                        gm_ts6oqppjh0ba3 t386;
                        GM_ST(uint8_t, (uint8_t *)&t386 + 1, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t386 + 0), ((uint8_t)9ULL));
                        gm_l25 = t386;
                        goto gm_lb50;
                    }
gm_lb53:;
                    gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 103, 30);
                }
gm_lb50:;
                {
                    gm_t88rcucg1b7uk t387 = gm_l6;
                    gm_ts6oqppjh0ba3 t388 = gm_l25;
                    void * t389 = gm_l2;
                    void * t390 = gm_l14;
                    gm_trl9d7m1k5lbd t391;
                    GM_ST(uint16_t, (uint8_t *)&t391 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t391 + 4, 0);
                    GM_ST(gm_ts6oqppjh0ba3, ((uint8_t *)&t391 + 0), t388);
                    GM_ST(void *, ((uint8_t *)&t391 + 8), t389);
                    GM_ST(void *, ((uint8_t *)&t391 + 16), t390);
                    gm_tl874t2atnlh3 t392 = gm_l4;
                    gm_to0f5qn75cgg8 t393;
                    memset(&t393, 0, sizeof t393);
                    GM_ST(uint8_t, ((uint8_t *)&t393 + 0), ((uint8_t)30ULL));
                    GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t393 + 8), t391);
                    void * t394 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t387, &t393, t392);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t394;
                    goto gm_fret;
                }
            }
gm_lb45:;
            {
                goto gm_lb21;
            }
gm_lb54:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 75, 5);
        }
gm_lb21:;
    }
    {
        void * t395 = gm_l0;
        gm_tl874t2atnlh3 t396 = gm_l4;
        gm_b16_8 t397 = gm_l9;
        void * t398 = gm_l0;
        gm_t88rcucg1b7uk t399 = gm_l8;
        gm_b16_8 t400 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t398, t399);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t401;
        GM_ST(gm_b16_8, ((uint8_t *)&t401 + 0), t397);
        GM_ST(gm_b16_8, ((uint8_t *)&t401 + 16), t400);
        gm_f_format_qu5usm7((void *)&gm_l35, (void *)NULL, cx, NULL, &t401);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t402 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
        if (!t402) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_src7u9cfdjh7s0n, 113, 20);
        gm_b16_8 t403 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
        gm_b16_8 t404 = GM_LD(gm_b16_8, ((uint8_t *)&t403));
        gm_b0_1 t405 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t395, t396, t404);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t406 = gm_l0;
        void * t407 = GM_LD(void *, ((uint8_t *)t406));
        gm_tl874t2atnlh3 t408 = gm_l4;
        void * t409 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t407, t408);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t409;
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
__attribute__((aligned(4))) void * gm_f_vector_construction_fhm2mm5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_construction_fhm2mm5 F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* attrs */
    void * gm_l2; /* inner */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* call */
    gm_b16_8 gm_l5; /* args */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l9; /* st */
    gm_tjpqvfd72o62u gm_l10; /* info */
    gm_t88rcucg1b7uk gm_l11; /* t */
    void * gm_l12; /* tys */
    gm_tlcm4lvu3g1a8 gm_l13; /* p */
    gm_tlcm4lvu3g1a8 gm_l14; /* p */
    GM_STACK_CHECK(&F, 1096u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l15;
    gm_t8cc1mtbrhlpt gm_l16;
    gm_tb7u19loo3j87 gm_l17;
    gm_tb7u19loo3j87 gm_l18;
    void * t13 = gm_l2;
    void * t14 = gm_f_construction_call_ktqtrpo(t13);
    gm_l15 = t14;
    uint8_t t15 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l15)) != 0);
    if (t15) {
        void * t16 = GM_LD(void *, ((uint8_t *)&gm_l15));
        gm_l4 = t16;
    } else {
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, ((uint8_t *)t17));
        gm_tl874t2atnlh3 t19 = gm_l3;
        void * t20 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t20;
        goto gm_fret;
    }
    {
        {
            void * t21 = gm_l4;
            uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t21) + 8) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)19ULL));
            if (!t23) {
                goto gm_lb2;
            }
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((((uint8_t *)t21) + 8) + 8) + 8));
            gm_l5 = t24;
            gm_b16_8 t25 = gm_l1;
            gm_l6 = t25;
            gm_l7 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t26 = gm_l7;
                    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint8_t t28 = (t26 < t27);
                    if (!t28) {
                        goto gm_lb3;
                    }
                    {
                        void * t29 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t31 = gm_l7;
                        {
                            {
                                memmove(((uint8_t *)&F.l8), (((uint8_t *)t29) + (uintptr_t)t31 * 72), 72);
                                gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                                gm_b16_8 t33;
                                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sqes10usq4q5e));
                                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(3));
                                uint8_t t34 = (gm_eq0iqiubfm7e(((uint8_t *)&t32), ((uint8_t *)&t33)));
                                if (t34) {
                                    void * t35 = gm_l0;
                                    void * t36 = GM_LD(void *, ((uint8_t *)t35));
                                    void * t37 = gm_l0;
                                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)t37) + 8));
                                    void * t39 = gm_l2;
                                    void * t40 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t39);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tl874t2atnlh3 t41 = gm_l3;
                                    gm_t88rcucg1b7uk t42 = gm_f_soa_type_expr_go3p0n7((void *)NULL, cx, NULL, t36, t38, t40, t41);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l9 = t42;
                                    {
                                        void * t43 = gm_l0;
                                        void * t44 = GM_LD(void *, ((uint8_t *)t43));
                                        gm_t88rcucg1b7uk t45 = gm_l9;
                                        gm_f_soa_info_pq4q43l((void *)&gm_l16, t44, GM_CONV(gm_csguankko4lmhd, t45));
                                    }
                                    uint8_t t46 = (uint8_t)((((uint8_t *)&gm_l16))[32] != 0);
                                    if (t46) {
                                        memmove(((uint8_t *)&gm_l10), ((uint8_t *)&gm_l16), 32);
                                    } else {
                                        void * t47 = gm_l0;
                                        void * t48 = GM_LD(void *, ((uint8_t *)t47));
                                        gm_tl874t2atnlh3 t49 = gm_l3;
                                        void * t50 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t48, t49);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t50;
                                        goto gm_fret;
                                    }
                                    {
                                        void * t51 = gm_l0;
                                        gm_t88rcucg1b7uk t52 = gm_l9;
                                        gm_b16_8 t53 = gm_l5;
                                        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
                                        gm_tl874t2atnlh3 t55 = gm_l3;
                                        void * t56 = gm_f_soa_construct_8nun9tl((void *)NULL, cx, NULL, t51, t52, &(*(gm_tjpqvfd72o62u *)((uint8_t *)&gm_l10)), t54, t55);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t56;
                                        goto gm_fret;
                                    }
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 12, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t57 = gm_l7;
                    uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
                    uint64_t t60 = r58;
                    gm_l7 = t60;
                }
            }
gm_lb3:;
            void * t61 = gm_l0;
            void * t62 = GM_LD(void *, ((uint8_t *)t61));
            void * t63 = gm_l0;
            uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)t63) + 8));
            gm_b16_8 t65 = gm_l1;
            void * t66 = gm_l2;
            void * t67 = gm_f_without_call_t1kfc9e((void *)NULL, cx, NULL, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t68 = gm_l3;
            gm_t88rcucg1b7uk t69 = gm_f_vector_type_8ofo3gl((void *)NULL, cx, NULL, t62, t64, t65, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t69;
            void * t70 = gm_l0;
            void * t71 = GM_LD(void *, ((uint8_t *)t70));
            void * t72 = (void *)(((uint8_t *)t71) + 8);
            gm_l12 = t72;
            {
                {
                    void * t73 = gm_l12;
                    gm_t88rcucg1b7uk t74 = gm_l11;
                    gm_f_vector_parts_crrfopm((void *)&gm_l17, t73, GM_CONV(gm_csguankko4lmhd, t74));
                    uint8_t t75 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
                    if (!t75) {
                        goto gm_lb9;
                    }
                    gm_tlcm4lvu3g1a8 t76 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l17));
                    gm_l13 = t76;
                    void * t77 = gm_l0;
                    gm_t88rcucg1b7uk t78 = gm_l11;
                    gm_tkn42u7na3t81 t79;
                    GM_ST(uint8_t, (uint8_t *)&t79 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t79 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t79), t78);
                    ((uint8_t *)&t79)[4] = 1;
                    uint64_t t80 = GM_LD(uint64_t, ((uint8_t *)&gm_l13));
                    gm_tf5srh4mp4b3i t81;
                    GM_ST(uint8_t, (uint8_t *)&t81 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t81 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t81 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t81), t80);
                    ((uint8_t *)&t81)[8] = 1;
                    gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 8));
                    gm_b16_8 t83 = gm_l5;
                    gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                    gm_tl874t2atnlh3 t85 = gm_l3;
                    void * t86 = gm_f_construct_array_qkofm98((void *)NULL, cx, NULL, t77, t79, t81, t82, t84, t85);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t86;
                    goto gm_fret;
                }
gm_lb9:;
            }
gm_lb8:;
            {
                {
                    void * t87 = gm_l12;
                    gm_t88rcucg1b7uk t88 = gm_l11;
                    gm_f_math_vector_parts_49c7865((void *)&gm_l18, t87, GM_CONV(gm_csguankko4lmhd, t88));
                    uint8_t t89 = (uint8_t)((((uint8_t *)&gm_l18))[16] != 0);
                    if (!t89) {
                        goto gm_lb11;
                    }
                    gm_tlcm4lvu3g1a8 t90 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l18));
                    gm_l14 = t90;
                    void * t91 = gm_l0;
                    gm_t88rcucg1b7uk t92 = gm_l11;
                    gm_tkn42u7na3t81 t93;
                    GM_ST(uint8_t, (uint8_t *)&t93 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t93 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t93), t92);
                    ((uint8_t *)&t93)[4] = 1;
                    uint64_t t94 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                    gm_tf5srh4mp4b3i t95;
                    GM_ST(uint8_t, (uint8_t *)&t95 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t95 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t95 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t95), t94);
                    ((uint8_t *)&t95)[8] = 1;
                    gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l14) + 8));
                    gm_b16_8 t97 = gm_l5;
                    gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
                    gm_tl874t2atnlh3 t99 = gm_l3;
                    void * t100 = gm_f_construct_array_qkofm98((void *)NULL, cx, NULL, t91, t93, t95, t96, t98, t99);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t100;
                    goto gm_fret;
                }
gm_lb11:;
            }
gm_lb10:;
            void * t101 = gm_l0;
            void * t102 = GM_LD(void *, ((uint8_t *)t101));
            gm_tl874t2atnlh3 t103 = gm_l3;
            void * t104 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t102, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t104;
            goto gm_fret;
        }
gm_lb2:;
        void * t105 = gm_l0;
        void * t106 = GM_LD(void *, ((uint8_t *)t105));
        gm_tl874t2atnlh3 t107 = gm_l3;
        void * t108 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t106, t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t108;
        goto gm_fret;
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
__attribute__((aligned(4))) void * gm_f_vector_conversion_32a9cuf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_conversion_32a9cuf F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* target */
    void * gm_l2; /* x */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* st */
    uint64_t gm_l6; /* n */
    gm_t88rcucg1b7uk gm_l7; /* a */
    uint64_t gm_l8; /* m */
    gm_t88rcucg1b7uk gm_l9; /* b */
    uint8_t gm_l10; /* both */
    gm_tn3r4rvlh0rsv gm_l11; /* <temporary array> */
    gm_tn3r4rvlh0rsv gm_l12; /* <temporary array> */
    gm_b16_8 gm_l13; /* why */
    GM_STACK_CHECK(&F, 1491u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l14;
    gm_t53at80pfdus3 gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_tabb710d9mq2e gm_l22;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    void * t16 = (void *)(((uint8_t *)t15) + 8);
    gm_l4 = t16;
    void * t17 = gm_l0;
    void * t18 = (void *)(((uint8_t *)t17) + 96);
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    void * t21 = gm_l2;
    gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t21));
    gm_t88rcucg1b7uk t23 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t18, t20, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t23;
    {
        {
            void * t24 = gm_l4;
            gm_t88rcucg1b7uk t25 = gm_l1;
            uint8_t t26 = gm_f_is_vector_gkq6b88(t24, GM_CONV(gm_csguankko4lmhd, t25));
            uint8_t t27 = (uint8_t)!t26;
            if (!t27) {
                goto gm_lb2;
            }
            void * t28 = gm_l4;
            gm_t88rcucg1b7uk t29 = gm_l5;
            uint8_t t30 = gm_f_is_vector_gkq6b88(t28, GM_CONV(gm_csguankko4lmhd, t29));
            uint8_t t31 = (uint8_t)!t30;
            if (!t31) {
                goto gm_lb2;
            }
            F.ret = ((void *)0);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t32 = gm_l4;
            gm_t88rcucg1b7uk t33 = gm_l5;
            gm_f_kind_jcnt4hm((void *)&gm_l14, t32, GM_CONV(gm_csguankko4lmhd, t33));
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)11ULL));
            if (!t35) {
                goto gm_lb4;
            }
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
            gm_l6 = t36;
            gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l14) + 8) + 8));
            gm_l7 = t37;
            {
                {
                    {
                        void * t38 = gm_l4;
                        gm_t88rcucg1b7uk t39 = gm_l1;
                        gm_f_kind_jcnt4hm((void *)&gm_l15, t38, GM_CONV(gm_csguankko4lmhd, t39));
                    }
                    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)11ULL));
                    if (!t41) {
                        goto gm_lb6;
                    }
                    uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                    gm_l8 = t42;
                    gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l15) + 8) + 8));
                    gm_l9 = t43;
                    void * t44 = gm_l4;
                    gm_t88rcucg1b7uk t45 = gm_l5;
                    uint8_t t46 = gm_f_is_vector_gkq6b88(t44, GM_CONV(gm_csguankko4lmhd, t45));
                    if (t46) {
                        void * t47 = gm_l4;
                        gm_t88rcucg1b7uk t48 = gm_l1;
                        uint8_t t49 = gm_f_is_vector_gkq6b88(t47, GM_CONV(gm_csguankko4lmhd, t48));
                        gm_l16 = t49;
                    } else {
                        gm_l16 = ((uint8_t)0);
                    }
                    uint8_t t50 = gm_l16;
                    gm_l10 = t50;
                    {
                        {
                            uint64_t t51 = gm_l6;
                            uint64_t t52 = gm_l8;
                            uint8_t t53 = ((t51 == t52));
                            if (!t53) {
                                goto gm_lb8;
                            }
                            gm_t88rcucg1b7uk t54 = gm_l7;
                            gm_t88rcucg1b7uk t55 = gm_l9;
                            uint8_t t56 = (gm_eq9m6mvtmgde(((uint8_t *)&t54), ((uint8_t *)&t55)));
                            if (!t56) {
                                goto gm_lb8;
                            }
                            uint8_t t57 = gm_l10;
                            if (t57) {
                                gm_l18 = ((uint8_t)1);
                            } else {
                                void * t58 = gm_l4;
                                gm_t88rcucg1b7uk t59 = gm_l5;
                                uint8_t t60 = gm_f_is_positional_array_6sufsk8(t58, GM_CONV(gm_csguankko4lmhd, t59));
                                gm_l18 = t60;
                            }
                            uint8_t t61 = gm_l18;
                            if (t61) {
                                gm_l17 = ((uint8_t)1);
                            } else {
                                void * t62 = gm_l4;
                                gm_t88rcucg1b7uk t63 = gm_l1;
                                uint8_t t64 = gm_f_is_positional_array_6sufsk8(t62, GM_CONV(gm_csguankko4lmhd, t63));
                                gm_l17 = t64;
                            }
                            uint8_t t65 = gm_l17;
                            if (!t65) {
                                goto gm_lb8;
                            }
                            gm_t88rcucg1b7uk t66 = gm_l1;
                            void * t67 = gm_l2;
                            gm_tn3r4rvlh0rsv t68;
                            GM_ST(void *, ((uint8_t *)&t68 + 0), t67);
                            gm_t8588ma2cr9d7 t69;
                            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t69 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)7ULL));
                            gm_l11 = t68;
                            void * t70 = (void *)((uint8_t *)&gm_l11);
                            uint64_t t71 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t72;
                            GM_ST(void *, (uint8_t *)&t72, (void *)(((uint8_t *)t70 + (uintptr_t)((uint64_t)0ULL) * 8)));
                            GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(t71));
                            gm_tl874t2atnlh3 t73 = gm_l3;
                            void * t74 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t69, t66, t72, t73);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t74;
                            goto gm_fret;
                        }
gm_lb8:;
                    }
gm_lb7:;
                    {
                        {
                            uint8_t t75 = gm_l10;
                            if (!t75) {
                                goto gm_lb10;
                            }
                            uint64_t t76 = gm_l6;
                            uint64_t t77 = gm_l8;
                            uint8_t t78 = ((t76 == t77));
                            if (!t78) {
                                goto gm_lb10;
                            }
                            void * t79 = gm_l0;
                            void * t80 = GM_LD(void *, ((uint8_t *)t79));
                            gm_t88rcucg1b7uk t81 = gm_l7;
                            gm_t88rcucg1b7uk t82 = gm_l9;
                            uint8_t t83 = gm_f_lossless_a54o69s(t80, GM_CONV(gm_csguankko4lmhd, t81), GM_CONV(gm_csguankko4lmhd, t82));
                            if (!t83) {
                                goto gm_lb10;
                            }
                            gm_t88rcucg1b7uk t84 = gm_l1;
                            void * t85 = gm_l2;
                            gm_tn3r4rvlh0rsv t86;
                            GM_ST(void *, ((uint8_t *)&t86 + 0), t85);
                            gm_t8588ma2cr9d7 t87;
                            GM_ST(uint8_t, (uint8_t *)&t87 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t87 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t87 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)56ULL));
                            gm_l12 = t86;
                            void * t88 = (void *)((uint8_t *)&gm_l12);
                            uint64_t t89 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t90;
                            GM_ST(void *, (uint8_t *)&t90, (void *)(((uint8_t *)t88 + (uintptr_t)((uint64_t)0ULL) * 8)));
                            GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(t89));
                            gm_tl874t2atnlh3 t91 = gm_l3;
                            void * t92 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t87, t84, t90, t91);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t92;
                            goto gm_fret;
                        }
gm_lb10:;
                    }
gm_lb9:;
                    {
                        {
                            uint8_t t93 = gm_l10;
                            if (!t93) {
                                goto gm_lb12;
                            }
                            uint64_t t94 = gm_l6;
                            uint64_t t95 = gm_l8;
                            uint8_t t96 = ((t94 == t95));
                            if (!t96) {
                                goto gm_lb12;
                            }
                            {
                                void * t97 = gm_l0;
                                gm_t88rcucg1b7uk t98 = gm_l7;
                                gm_b16_8 t99 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t97, t98);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t100 = gm_l0;
                                gm_t88rcucg1b7uk t101 = gm_l9;
                                gm_b16_8 t102 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t100, t101);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_t6j6smj7qoctu t103;
                                GM_ST(gm_b16_8, ((uint8_t *)&t103 + 0), t99);
                                GM_ST(gm_b16_8, ((uint8_t *)&t103 + 16), t102);
                                gm_f_format_mrlevsk((void *)&gm_l19, (void *)NULL, cx, NULL, &t103);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t104 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                            if (!t104) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_src7u9cfdjh7s0n, 400, 40);
                            gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                            gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                            gm_l13 = t106;
                            goto gm_lb11;
                        }
gm_lb12:;
                        gm_b16_8 t107;
                        GM_ST(void *, (uint8_t *)&t107, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(0));
                        gm_l13 = t107;
                    }
gm_lb11:;
                    {
                        void * t108 = gm_l0;
                        gm_tl874t2atnlh3 t109 = gm_l3;
                        void * t110 = gm_l0;
                        gm_t88rcucg1b7uk t111 = gm_l5;
                        gm_b16_8 t112 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t110, t111);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t113 = gm_l0;
                        gm_t88rcucg1b7uk t114 = gm_l1;
                        gm_b16_8 t115 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t113, t114);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t116 = gm_l13;
                        gm_tfeab279c16rr t117;
                        GM_ST(gm_b16_8, ((uint8_t *)&t117 + 0), t112);
                        GM_ST(gm_b16_8, ((uint8_t *)&t117 + 16), t115);
                        GM_ST(gm_b16_8, ((uint8_t *)&t117 + 32), t116);
                        gm_f_format_d0lfl1k((void *)&gm_l20, (void *)NULL, cx, NULL, &t117);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t118 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                        if (!t118) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_src7u9cfdjh7s0n, 401, 20);
                        gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                        gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
                        gm_b0_1 t121 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t108, t109, t120);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t122 = gm_l0;
                    void * t123 = GM_LD(void *, ((uint8_t *)t122));
                    gm_tl874t2atnlh3 t124 = gm_l3;
                    void * t125 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t123, t124);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t125;
                    goto gm_fret;
                }
gm_lb6:;
                {
                    void * t126 = gm_l0;
                    gm_tl874t2atnlh3 t127 = gm_l3;
                    void * t128 = gm_l0;
                    gm_t88rcucg1b7uk t129 = gm_l5;
                    gm_b16_8 t130 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t128, t129);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t131 = gm_l0;
                    gm_t88rcucg1b7uk t132 = gm_l1;
                    gm_b16_8 t133 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t131, t132);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t134;
                    GM_ST(gm_b16_8, ((uint8_t *)&t134 + 0), t130);
                    GM_ST(gm_b16_8, ((uint8_t *)&t134 + 16), t133);
                    gm_f_format_9rhpk5t((void *)&gm_l21, (void *)NULL, cx, NULL, &t134);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t135 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                    if (!t135) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_src7u9cfdjh7s0n, 391, 24);
                    gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                    gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
                    gm_b0_1 t138 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t126, t127, t137);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t139 = gm_l0;
                void * t140 = GM_LD(void *, ((uint8_t *)t139));
                gm_tl874t2atnlh3 t141 = gm_l3;
                void * t142 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t140, t141);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t142;
                goto gm_fret;
            }
gm_lb5:;
        }
gm_lb4:;
        {
            void * t143 = gm_l0;
            gm_tl874t2atnlh3 t144 = gm_l3;
            void * t145 = gm_l0;
            gm_t88rcucg1b7uk t146 = gm_l5;
            gm_b16_8 t147 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t145, t146);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t148 = gm_l0;
            gm_t88rcucg1b7uk t149 = gm_l1;
            gm_b16_8 t150 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t148, t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t151;
            GM_ST(gm_b16_8, ((uint8_t *)&t151 + 0), t147);
            GM_ST(gm_b16_8, ((uint8_t *)&t151 + 16), t150);
            gm_f_format_9rhpk5t((void *)&gm_l22, (void *)NULL, cx, NULL, &t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t152 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
            if (!t152) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_src7u9cfdjh7s0n, 387, 24);
            gm_b16_8 t153 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
            gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&t153));
            gm_b0_1 t155 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t143, t144, t154);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t156 = gm_l0;
        void * t157 = GM_LD(void *, ((uint8_t *)t156));
        gm_tl874t2atnlh3 t158 = gm_l3;
        void * t159 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t157, t158);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t159;
        goto gm_fret;
    }
gm_lb3:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_vector_lane_conversion_chta07f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_vector_lane_conversion_chta07f F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_tlcm4lvu3g1a8 gm_l7; /* parts */
    gm_t88rcucg1b7uk gm_l8; /* lane */
    gm_t88rcucg1b7uk gm_l9; /* t */
    uint8_t gm_l10; /* from_float */
    uint8_t gm_l11; /* from_int */
    gm_t9qkh0rsooc8d gm_l12; /* op */
    uint8_t gm_l13; /* src_ok */
    gm_b16_8 gm_l14; /* msg */
    gm_tn3r4rvlh0rsv gm_l15; /* <temporary array> */
    GM_STACK_CHECK(&F, 1128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tb7u19loo3j87 gm_l16;
    gm_tkn42u7na3t81 gm_l17;
    uint8_t gm_l18;
    uint8_t gm_l19;
    uint8_t gm_l20;
    gm_tabb710d9mq2e gm_l21;
    gm_th2lqbo77pvsc gm_l22;
    void * t10 = gm_l0;
    void * t11 = GM_LD(void *, ((uint8_t *)t10));
    void * t12 = (void *)(((uint8_t *)t11) + 8);
    gm_l6 = t12;
    void * t13 = gm_l6;
    gm_t88rcucg1b7uk t14 = gm_l2;
    gm_f_vector_parts_crrfopm((void *)&gm_l16, t13, GM_CONV(gm_csguankko4lmhd, t14));
    uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l16))[16] != 0);
    if (t15) {
        gm_tlcm4lvu3g1a8 t16 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l16));
        gm_l7 = t16;
    } else {
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, ((uint8_t *)t17));
        gm_tl874t2atnlh3 t19 = gm_l5;
        void * t20 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t20;
        goto gm_fret;
    }
    gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
    gm_l8 = t21;
    void * t22 = gm_l0;
    gm_b16_8 t23 = gm_l4;
    gm_b16_8 t24 = gm_l3;
    gm_tl874t2atnlh3 t25 = gm_l5;
    uint8_t t26 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t22, t23, ((uint64_t)1ULL), t24, t25);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t27 = (uint8_t)!t26;
    if (t27) {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, ((uint8_t *)t28));
        gm_tl874t2atnlh3 t30 = gm_l5;
        void * t31 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t31;
        goto gm_fret;
    }
    void * t32 = gm_l0;
    gm_b16_8 t33 = gm_l4;
    gm_tl874t2atnlh3 t34 = gm_l5;
    gm_tkn42u7na3t81 t35 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t32, t33, ((uint64_t)0ULL), t34);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l17 = t35;
    uint8_t t36 = (uint8_t)((((uint8_t *)&gm_l17))[4] != 0);
    if (t36) {
        gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l17));
        gm_l9 = t37;
    } else {
        void * t38 = gm_l0;
        void * t39 = GM_LD(void *, ((uint8_t *)t38));
        gm_tl874t2atnlh3 t40 = gm_l5;
        void * t41 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t41;
        goto gm_fret;
    }
    void * t42 = gm_l6;
    gm_t88rcucg1b7uk t43 = gm_l8;
    uint8_t t44 = gm_f_is_float_3jk89qu(t42, GM_CONV(gm_csguankko4lmhd, t43));
    gm_l10 = t44;
    void * t45 = gm_l6;
    gm_t88rcucg1b7uk t46 = gm_l8;
    uint8_t t47 = gm_f_is_integer_8d4f9ek(t45, GM_CONV(gm_csguankko4lmhd, t46));
    gm_l11 = t47;
    {
        {
            gm_b16_8 t48 = gm_l3;
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s7f68g16j7a3o));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(5));
            uint8_t t50 = (gm_eq0iqiubfm7e(((uint8_t *)&t48), ((uint8_t *)&t49)));
            if (!t50) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t51;
            GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)1ULL));
            gm_l12 = t51;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_b16_8 t52 = gm_l3;
            gm_b16_8 t53;
            GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sjb60mo5hejk2));
            GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(5));
            uint8_t t54 = (gm_eq0iqiubfm7e(((uint8_t *)&t52), ((uint8_t *)&t53)));
            if (!t54) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t55;
            GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)2ULL));
            gm_l12 = t55;
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_b16_8 t56 = gm_l3;
            gm_b16_8 t57;
            GM_ST(void *, (uint8_t *)&t57, (void *)(gm_shne2m0cj5kgi));
            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(5));
            uint8_t t58 = (gm_eq0iqiubfm7e(((uint8_t *)&t56), ((uint8_t *)&t57)));
            if (!t58) {
                goto gm_lb4;
            }
            gm_t9qkh0rsooc8d t59;
            GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)3ULL));
            gm_l12 = t59;
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_b16_8 t60 = gm_l3;
            gm_b16_8 t61;
            GM_ST(void *, (uint8_t *)&t61, (void *)(gm_shb5mqv6etf1t));
            GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(4));
            uint8_t t62 = (gm_eq0iqiubfm7e(((uint8_t *)&t60), ((uint8_t *)&t61)));
            if (!t62) {
                goto gm_lb5;
            }
            gm_t9qkh0rsooc8d t63;
            GM_ST(uint8_t, ((uint8_t *)&t63 + 0), ((uint8_t)4ULL));
            gm_l12 = t63;
            goto gm_lb1;
        }
gm_lb5:;
        {
            gm_t9qkh0rsooc8d t64;
            GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)5ULL));
            gm_l12 = t64;
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 257, 22);
    }
gm_lb1:;
    gm_t9qkh0rsooc8d t65 = gm_l12;
    gm_t9qkh0rsooc8d t66;
    GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)1ULL));
    gm_t9qkh0rsooc8d t67;
    GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)5ULL));
    gm_t0am46iohlpol t68;
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t68 + 0), t66);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t68 + 1), t67);
    uint8_t t69 = 0;
    if (gm_eqasplg81cl8(((uint8_t *)&t68) + 0, ((uint8_t *)&t65))) t69 = 1;
    if (gm_eqasplg81cl8(((uint8_t *)&t68) + 1, ((uint8_t *)&t65))) t69 = 1;
    uint8_t t70 = (uint8_t)(t69);
    if (t70) {
        uint8_t t71 = gm_l11;
        if (t71) {
            gm_l18 = ((uint8_t)1);
        } else {
            uint8_t t72 = gm_l10;
            gm_l18 = t72;
        }
        uint8_t t73 = gm_l18;
        gm_l13 = t73;
    } else {
        uint8_t t74 = gm_l10;
        gm_l13 = t74;
    }
    void * t75 = gm_l6;
    gm_t88rcucg1b7uk t76 = gm_l9;
    uint8_t t77 = gm_f_is_integer_8d4f9ek(t75, GM_CONV(gm_csguankko4lmhd, t76));
    uint8_t t78 = (uint8_t)!t77;
    if (t78) {
        gm_l20 = ((uint8_t)1);
    } else {
        void * t79 = gm_l6;
        gm_t88rcucg1b7uk t80 = gm_l9;
        uint8_t t81 = gm_f_lane_type_ok_rp8uq1p(t79, GM_CONV(gm_csguankko4lmhd, t80));
        uint8_t t82 = (uint8_t)!t81;
        gm_l20 = t82;
    }
    uint8_t t83 = gm_l20;
    if (t83) {
        gm_l19 = ((uint8_t)1);
    } else {
        uint8_t t84 = gm_l13;
        uint8_t t85 = (uint8_t)!t84;
        gm_l19 = t85;
    }
    uint8_t t86 = gm_l19;
    if (t86) {
        {
            void * t87 = gm_l0;
            gm_tl874t2atnlh3 t88 = gm_l5;
            gm_b16_8 t89 = gm_l3;
            void * t90 = gm_l0;
            gm_t88rcucg1b7uk t91 = gm_l2;
            gm_b16_8 t92 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t90, t91);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t93 = gm_l0;
            gm_t88rcucg1b7uk t94 = gm_l9;
            gm_b16_8 t95 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t93, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tfeab279c16rr t96;
            GM_ST(gm_b16_8, ((uint8_t *)&t96 + 0), t89);
            GM_ST(gm_b16_8, ((uint8_t *)&t96 + 16), t92);
            GM_ST(gm_b16_8, ((uint8_t *)&t96 + 32), t95);
            gm_f_format_faaokv1((void *)&gm_l21, (void *)NULL, cx, NULL, &t96);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t97 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
            if (!t97) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_src7u9cfdjh7s0n, 268, 24);
            gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
            gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&t98));
            gm_b0_1 t100 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t87, t88, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t101 = gm_l0;
        void * t102 = GM_LD(void *, ((uint8_t *)t101));
        gm_tl874t2atnlh3 t103 = gm_l5;
        void * t104 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t102, t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t104;
        goto gm_fret;
    }
    {
        {
            void * t105 = gm_l0;
            void * t106 = GM_LD(void *, ((uint8_t *)t105));
            uint64_t t107 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
            gm_t88rcucg1b7uk t108 = gm_l9;
            gm_f_vector_shape_error_3invhhe((void *)&gm_l22, (void *)NULL, cx, NULL, t106, t107, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t109 = (uint8_t)((((uint8_t *)&gm_l22))[16] != 0);
            if (!t109) {
                goto gm_lb8;
            }
            gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
            gm_l14 = t110;
            void * t111 = gm_l0;
            gm_tl874t2atnlh3 t112 = gm_l5;
            gm_b16_8 t113 = gm_l14;
            gm_b0_1 t114 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t111, t112, t113);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t115 = gm_l0;
            void * t116 = GM_LD(void *, ((uint8_t *)t115));
            gm_tl874t2atnlh3 t117 = gm_l5;
            void * t118 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t116, t117);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t118;
            goto gm_fret;
        }
gm_lb8:;
    }
gm_lb7:;
    gm_t9qkh0rsooc8d t119 = gm_l12;
    void * t120 = gm_l6;
    uint64_t t121 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
    gm_t88rcucg1b7uk t122 = gm_l9;
    gm_t8588ma2cr9d7 t123;
    GM_ST(uint8_t, (uint8_t *)&t123 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t123 + 2, 0);
    GM_ST(uint8_t, (uint8_t *)&t123 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t123 + 6, 0);
    GM_ST(uint32_t, (uint8_t *)&t123 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t123 + 0), ((uint8_t)57ULL));
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t123 + 4), t119);
    gm_t88rcucg1b7uk t124 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t120, t121, t122);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t125 = gm_l1;
    gm_tn3r4rvlh0rsv t126;
    GM_ST(void *, ((uint8_t *)&t126 + 0), t125);
    gm_l15 = t126;
    void * t127 = (void *)((uint8_t *)&gm_l15);
    uint64_t t128 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t129;
    GM_ST(void *, (uint8_t *)&t129, (void *)(((uint8_t *)t127 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(t128));
    gm_tl874t2atnlh3 t130 = gm_l5;
    void * t131 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t123, t124, t129, t130);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t131;
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
inline __attribute__((aligned(4))) void * gm_f_vector_load_ssdvomk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_load_ssdvomk F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* vt */
    gm_b16_8 gm_l2; /* args */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_tlcm4lvu3g1a8 gm_l5; /* parts */
    void * gm_l6; /* s */
    gm_tn3r4rvlh0rsv gm_l7; /* <temporary array> */
    GM_STACK_CHECK(&F, 443u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tb7u19loo3j87 gm_l8;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_l4 = t4;
    void * t5 = gm_l4;
    gm_t88rcucg1b7uk t6 = gm_l1;
    gm_f_vector_parts_crrfopm((void *)&gm_l8, t5, GM_CONV(gm_csguankko4lmhd, t6));
    uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l8))[16] != 0);
    if (t7) {
        gm_tlcm4lvu3g1a8 t8 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l8));
        gm_l5 = t8;
    } else {
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, ((uint8_t *)t9));
        gm_tl874t2atnlh3 t11 = gm_l3;
        void * t12 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t12;
        goto gm_fret;
    }
    void * t13 = gm_l0;
    gm_b16_8 t14 = gm_l2;
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sqfrv3g5v9m2n));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(4));
    gm_tl874t2atnlh3 t16 = gm_l3;
    uint8_t t17 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t13, t14, ((uint64_t)1ULL), t15, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t18 = (uint8_t)!t17;
    if (t18) {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, ((uint8_t *)t19));
        gm_tl874t2atnlh3 t21 = gm_l3;
        void * t22 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t20, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t22;
        goto gm_fret;
    }
    void * t23 = gm_l0;
    gm_b16_8 t24 = gm_l2;
    void * t25 = gm_l4;
    gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
    gm_t88rcucg1b7uk t27 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t25, ((uint8_t)1), t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tl874t2atnlh3 t28 = gm_l3;
    void * t29 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t23, t24, ((uint64_t)0ULL), t27, t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l6 = t29;
    gm_t88rcucg1b7uk t30 = gm_l1;
    void * t31 = gm_l6;
    gm_tn3r4rvlh0rsv t32;
    GM_ST(void *, ((uint8_t *)&t32 + 0), t31);
    gm_t8588ma2cr9d7 t33;
    GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
    GM_ST(uint32_t, (uint8_t *)&t33 + 8, 0);
    GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)59ULL));
    gm_l7 = t32;
    void * t34 = (void *)((uint8_t *)&gm_l7);
    uint64_t t35 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t36;
    GM_ST(void *, (uint8_t *)&t36, (void *)(((uint8_t *)t34 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(t35));
    gm_tl874t2atnlh3 t37 = gm_l3;
    void * t38 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t33, t30, t36, t37);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t38;
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
__attribute__((aligned(4))) void * gm_f_vector_method_hgn4sb1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_vector_method_hgn4sb1 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* args */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* tys */
    gm_tlcm4lvu3g1a8 gm_l7; /* parts */
    uint64_t gm_l8; /* len */
    gm_t88rcucg1b7uk gm_l9; /* lane */
    uint8_t gm_l10; /* numeric */
    gm_tn3r4rvlh0rsv gm_l11; /* one */
    gm_t8588ma2cr9d7 gm_l12; /* op */
    void * gm_l13; /* y */
    gm_t8588ma2cr9d7 gm_l14; /* op */
    gm_t5fru0lp11n7t gm_l15; /* <temporary array> */
    void * gm_l17; /* x */
    gm_t88rcucg1b7uk gm_l18; /* xt */
    uint8_t gm_l19; /* same_lanes */
    gm_tlcm4lvu3g1a8 gm_l20; /* xp */
    void * gm_l21; /* y */
    gm_thmgeksj5pdth gm_l22; /* <temporary array> */
    void * gm_l23; /* y */
    gm_t5fru0lp11n7t gm_l24; /* <temporary array> */
    gm_t88rcucg1b7uk gm_l25; /* f */
    gm_b16_8 gm_l26; /* msg */
    void * gm_l27; /* dst */
    gm_t5fru0lp11n7t gm_l28; /* <temporary array> */
    GM_STACK_CHECK(&F, 5686u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tb7u19loo3j87 gm_l29;
    uint8_t gm_l30;
    gm_tb7u19loo3j87 gm_l31;
    gm_tabb710d9mq2e gm_l32;
    gm_t8588ma2cr9d7 gm_l33;
    uint8_t gm_l34;
    gm_tkn42u7na3t81 gm_l35;
    uint8_t gm_l36;
    uint8_t gm_l37;
    gm_tabb710d9mq2e gm_l38;
    gm_th2lqbo77pvsc gm_l39;
    gm_tabb710d9mq2e gm_l40;
    {
        void * t58 = gm_l0;
        void * t59 = GM_LD(void *, ((uint8_t *)t58));
        void * t60 = (void *)(((uint8_t *)t59) + 8);
        gm_l6 = t60;
        void * t61 = gm_l6;
        gm_t88rcucg1b7uk t62 = gm_l2;
        gm_f_vector_parts_crrfopm((void *)&gm_l29, t61, GM_CONV(gm_csguankko4lmhd, t62));
        uint8_t t63 = (uint8_t)((((uint8_t *)&gm_l29))[16] != 0);
        if (t63) {
            gm_tlcm4lvu3g1a8 t64 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l29));
            gm_l7 = t64;
        } else {
            void * t65 = gm_l0;
            void * t66 = GM_LD(void *, ((uint8_t *)t65));
            gm_tl874t2atnlh3 t67 = gm_l5;
            void * t68 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t68;
            goto gm_fret;
        }
        uint64_t t69 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
        gm_l8 = t69;
        gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
        gm_l9 = t70;
        void * t71 = gm_l6;
        gm_t88rcucg1b7uk t72 = gm_l9;
        uint8_t t73 = gm_f_is_integer_8d4f9ek(t71, GM_CONV(gm_csguankko4lmhd, t72));
        if (t73) {
            gm_l30 = ((uint8_t)1);
        } else {
            void * t74 = gm_l6;
            gm_t88rcucg1b7uk t75 = gm_l9;
            uint8_t t76 = gm_f_is_float_3jk89qu(t74, GM_CONV(gm_csguankko4lmhd, t75));
            gm_l30 = t76;
        }
        uint8_t t77 = gm_l30;
        gm_l10 = t77;
        void * t78 = gm_l1;
        gm_tn3r4rvlh0rsv t79;
        GM_ST(void *, ((uint8_t *)&t79 + 0), t78);
        gm_l11 = t79;
        {
            {
                {
                    {
                        {
                            gm_b16_8 t80 = gm_l3;
                            gm_b16_8 t81;
                            GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sut92jpq3kk88));
                            GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(10));
                            uint8_t t82 = (gm_eq0iqiubfm7e(((uint8_t *)&t80), ((uint8_t *)&t81)));
                            if (!t82) {
                                goto gm_lb4;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        {
                            gm_b16_8 t83 = gm_l3;
                            gm_b16_8 t84;
                            GM_ST(void *, (uint8_t *)&t84, (void *)(gm_sbqoiitqtfv75));
                            GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(10));
                            uint8_t t85 = (gm_eq0iqiubfm7e(((uint8_t *)&t83), ((uint8_t *)&t84)));
                            if (!t85) {
                                goto gm_lb5;
                            }
                            goto gm_lb3;
                        }
gm_lb5:;
                        {
                            gm_b16_8 t86 = gm_l3;
                            gm_b16_8 t87;
                            GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sf1qp7r4ktsd1));
                            GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(10));
                            uint8_t t88 = (gm_eq0iqiubfm7e(((uint8_t *)&t86), ((uint8_t *)&t87)));
                            if (!t88) {
                                goto gm_lb6;
                            }
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb2;
                    }
gm_lb3:;
                    void * t89 = gm_l0;
                    gm_b16_8 t90 = gm_l4;
                    gm_b16_8 t91 = gm_l3;
                    gm_tl874t2atnlh3 t92 = gm_l5;
                    uint8_t t93 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t89, t90, ((uint64_t)0ULL), t91, t92);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t94 = (uint8_t)!t93;
                    if (t94) {
                        void * t95 = gm_l0;
                        void * t96 = GM_LD(void *, ((uint8_t *)t95));
                        gm_tl874t2atnlh3 t97 = gm_l5;
                        void * t98 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t96, t97);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t98;
                        goto gm_fret;
                    }
                    uint8_t t99 = gm_l10;
                    uint8_t t100 = (uint8_t)!t99;
                    if (t100) {
                        void * t101 = gm_l0;
                        gm_b16_8 t102 = gm_l3;
                        gm_t88rcucg1b7uk t103 = gm_l2;
                        gm_b16_8 t104;
                        GM_ST(void *, (uint8_t *)&t104, (void *)(gm_sfmov9pb6t5lt));
                        GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(13));
                        gm_tl874t2atnlh3 t105 = gm_l5;
                        void * t106 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t101, t102, t103, t104, t105);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t106;
                        goto gm_fret;
                    }
                    {
                        {
                            gm_b16_8 t107 = gm_l3;
                            gm_b16_8 t108;
                            GM_ST(void *, (uint8_t *)&t108, (void *)(gm_sut92jpq3kk88));
                            GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(10));
                            uint8_t t109 = (gm_eq0iqiubfm7e(((uint8_t *)&t107), ((uint8_t *)&t108)));
                            if (!t109) {
                                goto gm_lb8;
                            }
                            gm_t8588ma2cr9d7 t110;
                            GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t110 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)46ULL));
                            gm_l12 = t110;
                            goto gm_lb7;
                        }
gm_lb8:;
                        {
                            gm_b16_8 t111 = gm_l3;
                            gm_b16_8 t112;
                            GM_ST(void *, (uint8_t *)&t112, (void *)(gm_sbqoiitqtfv75));
                            GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(10));
                            uint8_t t113 = (gm_eq0iqiubfm7e(((uint8_t *)&t111), ((uint8_t *)&t112)));
                            if (!t113) {
                                goto gm_lb9;
                            }
                            gm_t8588ma2cr9d7 t114;
                            GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t114 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)47ULL));
                            gm_l12 = t114;
                            goto gm_lb7;
                        }
gm_lb9:;
                        {
                            gm_t8588ma2cr9d7 t115;
                            GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t115 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)48ULL));
                            gm_l12 = t115;
                            goto gm_lb7;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 161, 31);
                    }
gm_lb7:;
                    gm_t8588ma2cr9d7 t116 = gm_l12;
                    gm_t88rcucg1b7uk t117 = gm_l9;
                    void * t118 = (void *)((uint8_t *)&gm_l11);
                    uint64_t t119 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t120;
                    GM_ST(void *, (uint8_t *)&t120, (void *)(((uint8_t *)t118 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(t119));
                    gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                    gm_tl874t2atnlh3 t122 = gm_l5;
                    void * t123 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t116, t117, t121, t122);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t123;
                    goto gm_fret;
                }
gm_lb2:;
            }
            {
                {
                    {
                        {
                            gm_b16_8 t124 = gm_l3;
                            gm_b16_8 t125;
                            GM_ST(void *, (uint8_t *)&t125, (void *)(gm_sj7s3tnnvob4e));
                            GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(8));
                            uint8_t t126 = (gm_eq0iqiubfm7e(((uint8_t *)&t124), ((uint8_t *)&t125)));
                            if (!t126) {
                                goto gm_lb13;
                            }
                            goto gm_lb12;
                        }
gm_lb13:;
                        {
                            gm_b16_8 t127 = gm_l3;
                            gm_b16_8 t128;
                            GM_ST(void *, (uint8_t *)&t128, (void *)(gm_s6lddausbiitu));
                            GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(8));
                            uint8_t t129 = (gm_eq0iqiubfm7e(((uint8_t *)&t127), ((uint8_t *)&t128)));
                            if (!t129) {
                                goto gm_lb14;
                            }
                            goto gm_lb12;
                        }
gm_lb14:;
                        {
                            gm_b16_8 t130 = gm_l3;
                            gm_b16_8 t131;
                            GM_ST(void *, (uint8_t *)&t131, (void *)(gm_sh1to1i0u67lu));
                            GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(8));
                            uint8_t t132 = (gm_eq0iqiubfm7e(((uint8_t *)&t130), ((uint8_t *)&t131)));
                            if (!t132) {
                                goto gm_lb15;
                            }
                            goto gm_lb12;
                        }
gm_lb15:;
                        {
                            gm_b16_8 t133 = gm_l3;
                            gm_b16_8 t134;
                            GM_ST(void *, (uint8_t *)&t134, (void *)(gm_sv8pauo1bhltl));
                            GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(8));
                            uint8_t t135 = (gm_eq0iqiubfm7e(((uint8_t *)&t133), ((uint8_t *)&t134)));
                            if (!t135) {
                                goto gm_lb16;
                            }
                            goto gm_lb12;
                        }
gm_lb16:;
                        {
                            gm_b16_8 t136 = gm_l3;
                            gm_b16_8 t137;
                            GM_ST(void *, (uint8_t *)&t137, (void *)(gm_s1t08grtrn18m));
                            GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(8));
                            uint8_t t138 = (gm_eq0iqiubfm7e(((uint8_t *)&t136), ((uint8_t *)&t137)));
                            if (!t138) {
                                goto gm_lb17;
                            }
                            goto gm_lb12;
                        }
gm_lb17:;
                        {
                            gm_b16_8 t139 = gm_l3;
                            gm_b16_8 t140;
                            GM_ST(void *, (uint8_t *)&t140, (void *)(gm_scqrlin5l437v));
                            GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(8));
                            uint8_t t141 = (gm_eq0iqiubfm7e(((uint8_t *)&t139), ((uint8_t *)&t140)));
                            if (!t141) {
                                goto gm_lb18;
                            }
                            goto gm_lb12;
                        }
gm_lb18:;
                        goto gm_lb11;
                    }
gm_lb12:;
                    void * t142 = gm_l0;
                    gm_b16_8 t143 = gm_l4;
                    gm_b16_8 t144 = gm_l3;
                    gm_tl874t2atnlh3 t145 = gm_l5;
                    uint8_t t146 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t142, t143, ((uint64_t)1ULL), t144, t145);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t147 = (uint8_t)!t146;
                    if (t147) {
                        void * t148 = gm_l0;
                        void * t149 = GM_LD(void *, ((uint8_t *)t148));
                        gm_tl874t2atnlh3 t150 = gm_l5;
                        void * t151 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t149, t150);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t151;
                        goto gm_fret;
                    }
                    {
                        {
                            gm_b16_8 t152 = gm_l3;
                            gm_b16_8 t153;
                            GM_ST(void *, (uint8_t *)&t153, (void *)(gm_sj7s3tnnvob4e));
                            GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(8));
                            gm_b16_8 t154;
                            GM_ST(void *, (uint8_t *)&t154, (void *)(gm_s6lddausbiitu));
                            GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(8));
                            gm_t6j6smj7qoctu t155;
                            GM_ST(gm_b16_8, ((uint8_t *)&t155 + 0), t153);
                            GM_ST(gm_b16_8, ((uint8_t *)&t155 + 16), t154);
                            uint8_t t156 = 0;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t155) + 0, ((uint8_t *)&t152))) t156 = 1;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t155) + 16, ((uint8_t *)&t152))) t156 = 1;
                            uint8_t t157 = (uint8_t)(!t156);
                            if (!t157) {
                                goto gm_lb20;
                            }
                            uint8_t t158 = gm_l10;
                            uint8_t t159 = (uint8_t)!t158;
                            if (!t159) {
                                goto gm_lb20;
                            }
                            void * t160 = gm_l0;
                            gm_b16_8 t161 = gm_l3;
                            gm_t88rcucg1b7uk t162 = gm_l2;
                            gm_b16_8 t163;
                            GM_ST(void *, (uint8_t *)&t163, (void *)(gm_sfmov9pb6t5lt));
                            GM_ST(uintptr_t, (uint8_t *)&t163 + 8, (uintptr_t)(13));
                            gm_tl874t2atnlh3 t164 = gm_l5;
                            void * t165 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t160, t161, t162, t163, t164);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t165;
                            goto gm_fret;
                        }
gm_lb20:;
                    }
gm_lb19:;
                    void * t166 = gm_l0;
                    gm_b16_8 t167 = gm_l4;
                    gm_t88rcucg1b7uk t168 = gm_l2;
                    gm_tl874t2atnlh3 t169 = gm_l5;
                    void * t170 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t166, t167, ((uint64_t)0ULL), t168, t169);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t170;
                    {
                        {
                            gm_b16_8 t171 = gm_l3;
                            gm_b16_8 t172;
                            GM_ST(void *, (uint8_t *)&t172, (void *)(gm_sj7s3tnnvob4e));
                            GM_ST(uintptr_t, (uint8_t *)&t172 + 8, (uintptr_t)(8));
                            uint8_t t173 = (gm_eq0iqiubfm7e(((uint8_t *)&t171), ((uint8_t *)&t172)));
                            if (!t173) {
                                goto gm_lb22;
                            }
                            gm_t8588ma2cr9d7 t174;
                            GM_ST(uint8_t, (uint8_t *)&t174 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t174 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t174 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t174 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)40ULL));
                            gm_l14 = t174;
                            goto gm_lb21;
                        }
gm_lb22:;
                        {
                            gm_b16_8 t175 = gm_l3;
                            gm_b16_8 t176;
                            GM_ST(void *, (uint8_t *)&t176, (void *)(gm_s6lddausbiitu));
                            GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(8));
                            uint8_t t177 = (gm_eq0iqiubfm7e(((uint8_t *)&t175), ((uint8_t *)&t176)));
                            if (!t177) {
                                goto gm_lb23;
                            }
                            gm_t8588ma2cr9d7 t178;
                            GM_ST(uint8_t, (uint8_t *)&t178 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t178 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t178 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t178 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t178 + 0), ((uint8_t)41ULL));
                            gm_l14 = t178;
                            goto gm_lb21;
                        }
gm_lb23:;
                        {
                            gm_b16_8 t179 = gm_l3;
                            gm_b16_8 t180;
                            GM_ST(void *, (uint8_t *)&t180, (void *)(gm_sh1to1i0u67lu));
                            GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(8));
                            uint8_t t181 = (gm_eq0iqiubfm7e(((uint8_t *)&t179), ((uint8_t *)&t180)));
                            if (!t181) {
                                goto gm_lb24;
                            }
                            gm_t8588ma2cr9d7 t182;
                            GM_ST(uint8_t, (uint8_t *)&t182 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t182 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t182 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t182 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t182 + 0), ((uint8_t)42ULL));
                            gm_l14 = t182;
                            goto gm_lb21;
                        }
gm_lb24:;
                        {
                            gm_b16_8 t183 = gm_l3;
                            gm_b16_8 t184;
                            GM_ST(void *, (uint8_t *)&t184, (void *)(gm_sv8pauo1bhltl));
                            GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(8));
                            uint8_t t185 = (gm_eq0iqiubfm7e(((uint8_t *)&t183), ((uint8_t *)&t184)));
                            if (!t185) {
                                goto gm_lb25;
                            }
                            gm_t8588ma2cr9d7 t186;
                            GM_ST(uint8_t, (uint8_t *)&t186 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t186 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t186 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t186 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)43ULL));
                            gm_l14 = t186;
                            goto gm_lb21;
                        }
gm_lb25:;
                        {
                            gm_b16_8 t187 = gm_l3;
                            gm_b16_8 t188;
                            GM_ST(void *, (uint8_t *)&t188, (void *)(gm_s1t08grtrn18m));
                            GM_ST(uintptr_t, (uint8_t *)&t188 + 8, (uintptr_t)(8));
                            uint8_t t189 = (gm_eq0iqiubfm7e(((uint8_t *)&t187), ((uint8_t *)&t188)));
                            if (!t189) {
                                goto gm_lb26;
                            }
                            gm_t8588ma2cr9d7 t190;
                            GM_ST(uint8_t, (uint8_t *)&t190 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t190 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t190 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t190 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t190 + 0), ((uint8_t)44ULL));
                            gm_l14 = t190;
                            goto gm_lb21;
                        }
gm_lb26:;
                        {
                            gm_t8588ma2cr9d7 t191;
                            GM_ST(uint8_t, (uint8_t *)&t191 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t191 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t191 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t191 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t191 + 0), ((uint8_t)45ULL));
                            gm_l14 = t191;
                            goto gm_lb21;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 172, 31);
                    }
gm_lb21:;
                    gm_t8588ma2cr9d7 t192 = gm_l14;
                    void * t193 = gm_l6;
                    uint64_t t194 = gm_l8;
                    gm_t88rcucg1b7uk t195 = gm_f_mask_ty_cj7ln0g((void *)NULL, cx, NULL, t193, t194);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t196 = gm_l1;
                    void * t197 = gm_l13;
                    gm_t5fru0lp11n7t t198;
                    GM_ST(void *, ((uint8_t *)&t198 + 0), t196);
                    GM_ST(void *, ((uint8_t *)&t198 + 8), t197);
                    gm_l15 = t198;
                    void * t199 = (void *)((uint8_t *)&gm_l15);
                    uint64_t t200 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t201;
                    GM_ST(void *, (uint8_t *)&t201, (void *)(((uint8_t *)t199 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t201 + 8, (uintptr_t)(t200));
                    gm_tl874t2atnlh3 t202 = gm_l5;
                    void * t203 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t192, t195, t201, t202);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t203;
                    goto gm_fret;
                }
gm_lb11:;
            }
            {
                {
                    {
                        {
                            gm_b16_8 t204 = gm_l3;
                            gm_b16_8 t205;
                            GM_ST(void *, (uint8_t *)&t205, (void *)(gm_s0nf1rg04f3m5));
                            GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(3));
                            uint8_t t206 = (gm_eq0iqiubfm7e(((uint8_t *)&t204), ((uint8_t *)&t205)));
                            if (!t206) {
                                goto gm_lb30;
                            }
                            goto gm_lb29;
                        }
gm_lb30:;
                        {
                            gm_b16_8 t207 = gm_l3;
                            gm_b16_8 t208;
                            GM_ST(void *, (uint8_t *)&t208, (void *)(gm_skftljk77e6mu));
                            GM_ST(uintptr_t, (uint8_t *)&t208 + 8, (uintptr_t)(3));
                            uint8_t t209 = (gm_eq0iqiubfm7e(((uint8_t *)&t207), ((uint8_t *)&t208)));
                            if (!t209) {
                                goto gm_lb31;
                            }
                            goto gm_lb29;
                        }
gm_lb31:;
                        {
                            gm_b16_8 t210 = gm_l3;
                            gm_b16_8 t211;
                            GM_ST(void *, (uint8_t *)&t211, (void *)(gm_snc5ns3akncuq));
                            GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(5));
                            uint8_t t212 = (gm_eq0iqiubfm7e(((uint8_t *)&t210), ((uint8_t *)&t211)));
                            if (!t212) {
                                goto gm_lb32;
                            }
                            goto gm_lb29;
                        }
gm_lb32:;
                        goto gm_lb28;
                    }
gm_lb29:;
                    void * t213 = gm_l0;
                    gm_b16_8 t214 = gm_l4;
                    gm_b16_8 t215 = gm_l3;
                    gm_tl874t2atnlh3 t216 = gm_l5;
                    uint8_t t217 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t213, t214, ((uint64_t)0ULL), t215, t216);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t218 = (uint8_t)!t217;
                    if (t218) {
                        void * t219 = gm_l0;
                        void * t220 = GM_LD(void *, ((uint8_t *)t219));
                        gm_tl874t2atnlh3 t221 = gm_l5;
                        void * t222 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t220, t221);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t222;
                        goto gm_fret;
                    }
                    void * t223 = gm_l6;
                    gm_t88rcucg1b7uk t224 = gm_l9;
                    uint8_t t225 = gm_f_is_bool_ajhjgeg(t223, GM_CONV(gm_csguankko4lmhd, t224));
                    uint8_t t226 = (uint8_t)!t225;
                    if (t226) {
                        void * t227 = gm_l0;
                        gm_b16_8 t228 = gm_l3;
                        gm_t88rcucg1b7uk t229 = gm_l2;
                        gm_b16_8 t230;
                        GM_ST(void *, (uint8_t *)&t230, (void *)(gm_s8guqcjcce9oc));
                        GM_ST(uintptr_t, (uint8_t *)&t230 + 8, (uintptr_t)(21));
                        gm_tl874t2atnlh3 t231 = gm_l5;
                        void * t232 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t227, t228, t229, t230, t231);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t232;
                        goto gm_fret;
                    }
                    gm_b16_8 t233 = gm_l3;
                    gm_b16_8 t234;
                    GM_ST(void *, (uint8_t *)&t234, (void *)(gm_s0nf1rg04f3m5));
                    GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(3));
                    uint8_t t235 = (gm_eq0iqiubfm7e(((uint8_t *)&t233), ((uint8_t *)&t234)));
                    if (t235) {
                        void * t236 = gm_l6;
                        gm_t8588ma2cr9d7 t237;
                        GM_ST(uint8_t, (uint8_t *)&t237 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t237 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t237 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t237 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t237 + 0), ((uint8_t)49ULL));
                        gm_t88rcucg1b7uk t238 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t236);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t239 = (void *)((uint8_t *)&gm_l11);
                        uint64_t t240 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t241;
                        GM_ST(void *, (uint8_t *)&t241, (void *)(((uint8_t *)t239 + (uintptr_t)((uint64_t)0ULL) * 8)));
                        GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(t240));
                        gm_b16_8 t242 = GM_LD(gm_b16_8, ((uint8_t *)&t241));
                        gm_tl874t2atnlh3 t243 = gm_l5;
                        void * t244 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t237, t238, t242, t243);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t244;
                        goto gm_fret;
                    }
                    gm_b16_8 t245 = gm_l3;
                    gm_b16_8 t246;
                    GM_ST(void *, (uint8_t *)&t246, (void *)(gm_skftljk77e6mu));
                    GM_ST(uintptr_t, (uint8_t *)&t246 + 8, (uintptr_t)(3));
                    uint8_t t247 = (gm_eq0iqiubfm7e(((uint8_t *)&t245), ((uint8_t *)&t246)));
                    if (t247) {
                        void * t248 = gm_l6;
                        gm_t8588ma2cr9d7 t249;
                        GM_ST(uint8_t, (uint8_t *)&t249 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t249 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t249 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t249 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t249 + 0), ((uint8_t)50ULL));
                        gm_t88rcucg1b7uk t250 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t248);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t251 = (void *)((uint8_t *)&gm_l11);
                        uint64_t t252 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                        gm_b16_8 t253;
                        GM_ST(void *, (uint8_t *)&t253, (void *)(((uint8_t *)t251 + (uintptr_t)((uint64_t)0ULL) * 8)));
                        GM_ST(uintptr_t, (uint8_t *)&t253 + 8, (uintptr_t)(t252));
                        gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
                        gm_tl874t2atnlh3 t255 = gm_l5;
                        void * t256 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t249, t250, t254, t255);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t256;
                        goto gm_fret;
                    }
                    void * t257 = gm_l6;
                    gm_t8588ma2cr9d7 t258;
                    GM_ST(uint8_t, (uint8_t *)&t258 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t258 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t258 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t258 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t258 + 0), ((uint8_t)51ULL));
                    gm_t88rcucg1b7uk t259 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t257);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t260 = (void *)((uint8_t *)&gm_l11);
                    uint64_t t261 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t262;
                    GM_ST(void *, (uint8_t *)&t262, (void *)(((uint8_t *)t260 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t262 + 8, (uintptr_t)(t261));
                    gm_b16_8 t263 = GM_LD(gm_b16_8, ((uint8_t *)&t262));
                    gm_tl874t2atnlh3 t264 = gm_l5;
                    void * t265 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t258, t259, t263, t264);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t265;
                    goto gm_fret;
                }
gm_lb28:;
            }
            {
                {
                    gm_b16_8 t266 = gm_l3;
                    gm_b16_8 t267;
                    GM_ST(void *, (uint8_t *)&t267, (void *)(gm_svgoe41piv341));
                    GM_ST(uintptr_t, (uint8_t *)&t267 + 8, (uintptr_t)(6));
                    uint8_t t268 = (gm_eq0iqiubfm7e(((uint8_t *)&t266), ((uint8_t *)&t267)));
                    if (!t268) {
                        goto gm_lb33;
                    }
                    void * t269 = gm_l0;
                    gm_b16_8 t270 = gm_l4;
                    gm_b16_8 t271 = gm_l3;
                    gm_tl874t2atnlh3 t272 = gm_l5;
                    uint8_t t273 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t269, t270, ((uint64_t)2ULL), t271, t272);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t274 = (uint8_t)!t273;
                    if (t274) {
                        void * t275 = gm_l0;
                        void * t276 = GM_LD(void *, ((uint8_t *)t275));
                        gm_tl874t2atnlh3 t277 = gm_l5;
                        void * t278 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t276, t277);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t278;
                        goto gm_fret;
                    }
                    void * t279 = gm_l6;
                    gm_t88rcucg1b7uk t280 = gm_l9;
                    uint8_t t281 = gm_f_is_bool_ajhjgeg(t279, GM_CONV(gm_csguankko4lmhd, t280));
                    uint8_t t282 = (uint8_t)!t281;
                    if (t282) {
                        void * t283 = gm_l0;
                        gm_b16_8 t284 = gm_l3;
                        gm_t88rcucg1b7uk t285 = gm_l2;
                        gm_b16_8 t286;
                        GM_ST(void *, (uint8_t *)&t286, (void *)(gm_s8guqcjcce9oc));
                        GM_ST(uintptr_t, (uint8_t *)&t286 + 8, (uintptr_t)(21));
                        gm_tl874t2atnlh3 t287 = gm_l5;
                        void * t288 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t283, t284, t285, t286, t287);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t288;
                        goto gm_fret;
                    }
                    {
                        {
                            {
                                void * t289 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                uint64_t t290 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t290)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t290, gm_src7u9cfdjh7s0n, 192, 32);
                                uint8_t t291 = GM_LD(uint8_t, ((((uint8_t *)t289) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                                uint8_t t292 = (uint8_t)(t291 == ((uint8_t)0ULL));
                                if (!t292) {
                                    goto gm_lb35;
                                }
                                memmove(((uint8_t *)&F.l16), ((((uint8_t *)t289) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                            }
                            void * t293 = gm_l0;
                            void * t294 = (void *)((uint8_t *)&F.l16);
                            gm_tkn42u7na3t81 t295;
                            memset(&t295, 0, sizeof t295);
                            void * t296 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t293, t294, t295);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l17 = t296;
                            void * t297 = gm_l0;
                            void * t298 = (void *)(((uint8_t *)t297) + 96);
                            void * t299 = gm_l0;
                            void * t300 = GM_LD(void *, ((uint8_t *)t299));
                            void * t301 = gm_l17;
                            gm_t88rcucg1b7uk t302 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t301));
                            gm_t88rcucg1b7uk t303 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t298, t300, t302);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l18 = t303;
                            void * t304 = gm_l6;
                            gm_t88rcucg1b7uk t305 = gm_l18;
                            uint8_t t306 = gm_f_is_error_8k27gd1(t304, GM_CONV(gm_csguankko4lmhd, t305));
                            if (t306) {
                                void * t307 = gm_l0;
                                void * t308 = GM_LD(void *, ((uint8_t *)t307));
                                gm_tl874t2atnlh3 t309 = gm_l5;
                                void * t310 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t308, t309);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t310;
                                goto gm_fret;
                            }
                            gm_l19 = ((uint8_t)0);
                            {
                                {
                                    void * t311 = gm_l6;
                                    gm_t88rcucg1b7uk t312 = gm_l18;
                                    gm_f_vector_parts_crrfopm((void *)&gm_l31, t311, GM_CONV(gm_csguankko4lmhd, t312));
                                    uint8_t t313 = (uint8_t)((((uint8_t *)&gm_l31))[16] != 0);
                                    if (!t313) {
                                        goto gm_lb37;
                                    }
                                    gm_tlcm4lvu3g1a8 t314 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l31));
                                    gm_l20 = t314;
                                    uint64_t t315 = GM_LD(uint64_t, ((uint8_t *)&gm_l20));
                                    uint64_t t316 = gm_l8;
                                    uint8_t t317 = ((t315 == t316));
                                    gm_l19 = t317;
                                    goto gm_lb36;
                                }
gm_lb37:;
                            }
gm_lb36:;
                            uint8_t t318 = gm_l19;
                            uint8_t t319 = (uint8_t)!t318;
                            if (t319) {
                                {
                                    void * t320 = gm_l0;
                                    void * t321 = gm_l17;
                                    gm_tl874t2atnlh3 t322 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t321) + 72));
                                    uint64_t t323 = gm_l8;
                                    void * t324 = gm_l0;
                                    gm_t88rcucg1b7uk t325 = gm_l2;
                                    gm_b16_8 t326 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t324, t325);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t327 = gm_l0;
                                    gm_t88rcucg1b7uk t328 = gm_l18;
                                    gm_b16_8 t329 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t327, t328);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_t7neromnu8t62 t330;
                                    GM_ST(uint64_t, ((uint8_t *)&t330 + 0), t323);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t330 + 8), t326);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t330 + 24), t329);
                                    gm_f_format_4nuejdf((void *)&gm_l32, (void *)NULL, cx, NULL, &t330);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t331 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                    if (!t331) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_src7u9cfdjh7s0n, 199, 34);
                                    gm_b16_8 t332 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                                    gm_b16_8 t333 = GM_LD(gm_b16_8, ((uint8_t *)&t332));
                                    gm_b0_1 t334 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t320, t322, t333);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                void * t335 = gm_l0;
                                void * t336 = GM_LD(void *, ((uint8_t *)t335));
                                gm_tl874t2atnlh3 t337 = gm_l5;
                                void * t338 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t336, t337);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t338;
                                goto gm_fret;
                            }
                            void * t339 = gm_l0;
                            gm_b16_8 t340 = gm_l4;
                            gm_t88rcucg1b7uk t341 = gm_l18;
                            gm_tl874t2atnlh3 t342 = gm_l5;
                            void * t343 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t339, t340, ((uint64_t)1ULL), t341, t342);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l21 = t343;
                            {
                                gm_t88rcucg1b7uk t344 = gm_l18;
                                void * t345 = gm_l1;
                                void * t346 = gm_l17;
                                void * t347 = gm_l21;
                                gm_thmgeksj5pdth t348;
                                GM_ST(void *, ((uint8_t *)&t348 + 0), t345);
                                GM_ST(void *, ((uint8_t *)&t348 + 8), t346);
                                GM_ST(void *, ((uint8_t *)&t348 + 16), t347);
                                gm_t8588ma2cr9d7 t349;
                                GM_ST(uint8_t, (uint8_t *)&t349 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t349 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t349 + 4, 0);
                                GM_ST(uint32_t, (uint8_t *)&t349 + 8, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t349 + 0), ((uint8_t)52ULL));
                                gm_l22 = t348;
                                void * t350 = (void *)((uint8_t *)&gm_l22);
                                uint64_t t351 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
                                gm_b16_8 t352;
                                GM_ST(void *, (uint8_t *)&t352, (void *)(((uint8_t *)t350 + (uintptr_t)((uint64_t)0ULL) * 8)));
                                GM_ST(uintptr_t, (uint8_t *)&t352 + 8, (uintptr_t)(t351));
                                gm_tl874t2atnlh3 t353 = gm_l5;
                                void * t354 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t349, t344, t352, t353);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t354;
                                goto gm_fret;
                            }
                        }
gm_lb35:;
                        void * t355 = gm_l0;
                        void * t356 = GM_LD(void *, ((uint8_t *)t355));
                        gm_tl874t2atnlh3 t357 = gm_l5;
                        void * t358 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t356, t357);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t358;
                        goto gm_fret;
                    }
gm_lb34:;
                }
gm_lb33:;
            }
            {
                {
                    {
                        {
                            gm_b16_8 t359 = gm_l3;
                            gm_b16_8 t360;
                            GM_ST(void *, (uint8_t *)&t360, (void *)(gm_sm3o0uerjumpm));
                            GM_ST(uintptr_t, (uint8_t *)&t360 + 8, (uintptr_t)(3));
                            uint8_t t361 = (gm_eq0iqiubfm7e(((uint8_t *)&t359), ((uint8_t *)&t360)));
                            if (!t361) {
                                goto gm_lb40;
                            }
                            goto gm_lb39;
                        }
gm_lb40:;
                        {
                            gm_b16_8 t362 = gm_l3;
                            gm_b16_8 t363;
                            GM_ST(void *, (uint8_t *)&t363, (void *)(gm_s2mtoab140jp6));
                            GM_ST(uintptr_t, (uint8_t *)&t363 + 8, (uintptr_t)(3));
                            uint8_t t364 = (gm_eq0iqiubfm7e(((uint8_t *)&t362), ((uint8_t *)&t363)));
                            if (!t364) {
                                goto gm_lb41;
                            }
                            goto gm_lb39;
                        }
gm_lb41:;
                        goto gm_lb38;
                    }
gm_lb39:;
                    void * t365 = gm_l0;
                    gm_b16_8 t366 = gm_l4;
                    gm_b16_8 t367 = gm_l3;
                    gm_tl874t2atnlh3 t368 = gm_l5;
                    uint8_t t369 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t365, t366, ((uint64_t)1ULL), t367, t368);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t370 = (uint8_t)!t369;
                    if (t370) {
                        void * t371 = gm_l0;
                        void * t372 = GM_LD(void *, ((uint8_t *)t371));
                        gm_tl874t2atnlh3 t373 = gm_l5;
                        void * t374 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t372, t373);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t374;
                        goto gm_fret;
                    }
                    uint8_t t375 = gm_l10;
                    uint8_t t376 = (uint8_t)!t375;
                    if (t376) {
                        void * t377 = gm_l0;
                        gm_b16_8 t378 = gm_l3;
                        gm_t88rcucg1b7uk t379 = gm_l2;
                        gm_b16_8 t380;
                        GM_ST(void *, (uint8_t *)&t380, (void *)(gm_sfmov9pb6t5lt));
                        GM_ST(uintptr_t, (uint8_t *)&t380 + 8, (uintptr_t)(13));
                        gm_tl874t2atnlh3 t381 = gm_l5;
                        void * t382 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t377, t378, t379, t380, t381);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t382;
                        goto gm_fret;
                    }
                    void * t383 = gm_l0;
                    gm_b16_8 t384 = gm_l4;
                    gm_t88rcucg1b7uk t385 = gm_l2;
                    gm_tl874t2atnlh3 t386 = gm_l5;
                    void * t387 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t383, t384, ((uint64_t)0ULL), t385, t386);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l23 = t387;
                    gm_b16_8 t388 = gm_l3;
                    gm_b16_8 t389;
                    GM_ST(void *, (uint8_t *)&t389, (void *)(gm_sm3o0uerjumpm));
                    GM_ST(uintptr_t, (uint8_t *)&t389 + 8, (uintptr_t)(3));
                    uint8_t t390 = (gm_eq0iqiubfm7e(((uint8_t *)&t388), ((uint8_t *)&t389)));
                    if (t390) {
                        gm_t8588ma2cr9d7 t391;
                        GM_ST(uint8_t, (uint8_t *)&t391 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t391 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t391 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t391 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t391 + 0), ((uint8_t)53ULL));
                        gm_l33 = t391;
                    } else {
                        gm_t8588ma2cr9d7 t392;
                        GM_ST(uint8_t, (uint8_t *)&t392 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t392 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t392 + 4, 0);
                        GM_ST(uint32_t, (uint8_t *)&t392 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t392 + 0), ((uint8_t)54ULL));
                        gm_l33 = t392;
                    }
                    gm_t8588ma2cr9d7 t393 = gm_l33;
                    gm_t88rcucg1b7uk t394 = gm_l2;
                    void * t395 = gm_l1;
                    void * t396 = gm_l23;
                    gm_t5fru0lp11n7t t397;
                    GM_ST(void *, ((uint8_t *)&t397 + 0), t395);
                    GM_ST(void *, ((uint8_t *)&t397 + 8), t396);
                    gm_l24 = t397;
                    void * t398 = (void *)((uint8_t *)&gm_l24);
                    uint64_t t399 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t400;
                    GM_ST(void *, (uint8_t *)&t400, (void *)(((uint8_t *)t398 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t400 + 8, (uintptr_t)(t399));
                    gm_tl874t2atnlh3 t401 = gm_l5;
                    void * t402 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t393, t394, t400, t401);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t402;
                    goto gm_fret;
                }
gm_lb38:;
                {
                    gm_b16_8 t403 = gm_l3;
                    gm_b16_8 t404;
                    GM_ST(void *, (uint8_t *)&t404, (void *)(gm_sbl3pbs9195ho));
                    GM_ST(uintptr_t, (uint8_t *)&t404 + 8, (uintptr_t)(3));
                    uint8_t t405 = (gm_eq0iqiubfm7e(((uint8_t *)&t403), ((uint8_t *)&t404)));
                    if (!t405) {
                        goto gm_lb42;
                    }
                    void * t406 = gm_l0;
                    gm_b16_8 t407 = gm_l4;
                    gm_b16_8 t408 = gm_l3;
                    gm_tl874t2atnlh3 t409 = gm_l5;
                    uint8_t t410 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t406, t407, ((uint64_t)0ULL), t408, t409);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t411 = (uint8_t)!t410;
                    if (t411) {
                        void * t412 = gm_l0;
                        void * t413 = GM_LD(void *, ((uint8_t *)t412));
                        gm_tl874t2atnlh3 t414 = gm_l5;
                        void * t415 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t413, t414);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t415;
                        goto gm_fret;
                    }
                    void * t416 = gm_l6;
                    gm_t88rcucg1b7uk t417 = gm_l9;
                    uint8_t t418 = gm_f_is_signed_integer_s66djm7(t416, GM_CONV(gm_csguankko4lmhd, t417));
                    if (t418) {
                        gm_l34 = ((uint8_t)1);
                    } else {
                        void * t419 = gm_l6;
                        gm_t88rcucg1b7uk t420 = gm_l9;
                        uint8_t t421 = gm_f_is_float_3jk89qu(t419, GM_CONV(gm_csguankko4lmhd, t420));
                        gm_l34 = t421;
                    }
                    uint8_t t422 = gm_l34;
                    uint8_t t423 = (uint8_t)!t422;
                    if (t423) {
                        void * t424 = gm_l0;
                        gm_b16_8 t425 = gm_l3;
                        gm_t88rcucg1b7uk t426 = gm_l2;
                        gm_b16_8 t427;
                        GM_ST(void *, (uint8_t *)&t427, (void *)(gm_salin070nim24));
                        GM_ST(uintptr_t, (uint8_t *)&t427 + 8, (uintptr_t)(29));
                        gm_tl874t2atnlh3 t428 = gm_l5;
                        void * t429 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t424, t425, t426, t427, t428);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t429;
                        goto gm_fret;
                    }
                    gm_t88rcucg1b7uk t430 = gm_l2;
                    void * t431 = (void *)((uint8_t *)&gm_l11);
                    uint64_t t432 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t433;
                    GM_ST(void *, (uint8_t *)&t433, (void *)(((uint8_t *)t431 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t433 + 8, (uintptr_t)(t432));
                    gm_b16_8 t434 = GM_LD(gm_b16_8, ((uint8_t *)&t433));
                    gm_tl874t2atnlh3 t435 = gm_l5;
                    gm_t8588ma2cr9d7 t436;
                    GM_ST(uint8_t, (uint8_t *)&t436 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t436 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t436 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t436 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t436 + 0), ((uint8_t)55ULL));
                    void * t437 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t436, t430, t434, t435);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t437;
                    goto gm_fret;
                }
gm_lb42:;
            }
            {
                {
                    gm_b16_8 t438 = gm_l3;
                    gm_b16_8 t439;
                    GM_ST(void *, (uint8_t *)&t439, (void *)(gm_sfrjcsmisriv1));
                    GM_ST(uintptr_t, (uint8_t *)&t439 + 8, (uintptr_t)(2));
                    uint8_t t440 = (gm_eq0iqiubfm7e(((uint8_t *)&t438), ((uint8_t *)&t439)));
                    if (!t440) {
                        goto gm_lb43;
                    }
                    void * t441 = gm_l0;
                    gm_b16_8 t442 = gm_l4;
                    gm_b16_8 t443 = gm_l3;
                    gm_tl874t2atnlh3 t444 = gm_l5;
                    uint8_t t445 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t441, t442, ((uint64_t)1ULL), t443, t444);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t446 = (uint8_t)!t445;
                    if (t446) {
                        void * t447 = gm_l0;
                        void * t448 = GM_LD(void *, ((uint8_t *)t447));
                        gm_tl874t2atnlh3 t449 = gm_l5;
                        void * t450 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t448, t449);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t450;
                        goto gm_fret;
                    }
                    void * t451 = gm_l0;
                    gm_b16_8 t452 = gm_l4;
                    gm_tl874t2atnlh3 t453 = gm_l5;
                    gm_tkn42u7na3t81 t454 = gm_f_type_arg_grgffgp((void *)NULL, cx, NULL, t451, t452, ((uint64_t)0ULL), t453);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l35 = t454;
                    uint8_t t455 = (uint8_t)((((uint8_t *)&gm_l35))[4] != 0);
                    if (t455) {
                        gm_t88rcucg1b7uk t456 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l35));
                        gm_l25 = t456;
                    } else {
                        void * t457 = gm_l0;
                        void * t458 = GM_LD(void *, ((uint8_t *)t457));
                        gm_tl874t2atnlh3 t459 = gm_l5;
                        void * t460 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t458, t459);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t460;
                        goto gm_fret;
                    }
                    void * t461 = gm_l6;
                    gm_t88rcucg1b7uk t462 = gm_l25;
                    uint8_t t463 = gm_f_is_float_3jk89qu(t461, GM_CONV(gm_csguankko4lmhd, t462));
                    uint8_t t464 = (uint8_t)!t463;
                    if (t464) {
                        gm_l37 = ((uint8_t)1);
                    } else {
                        void * t465 = gm_l6;
                        gm_t88rcucg1b7uk t466 = gm_l25;
                        uint8_t t467 = gm_f_lane_type_ok_rp8uq1p(t465, GM_CONV(gm_csguankko4lmhd, t466));
                        uint8_t t468 = (uint8_t)!t467;
                        gm_l37 = t468;
                    }
                    uint8_t t469 = gm_l37;
                    if (t469) {
                        gm_l36 = ((uint8_t)1);
                    } else {
                        uint8_t t470 = gm_l10;
                        uint8_t t471 = (uint8_t)!t470;
                        gm_l36 = t471;
                    }
                    uint8_t t472 = gm_l36;
                    if (t472) {
                        {
                            void * t473 = gm_l0;
                            gm_tl874t2atnlh3 t474 = gm_l5;
                            void * t475 = gm_l0;
                            gm_t88rcucg1b7uk t476 = gm_l9;
                            gm_b16_8 t477 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t475, t476);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t478 = gm_l0;
                            gm_t88rcucg1b7uk t479 = gm_l25;
                            gm_b16_8 t480 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t478, t479);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t6j6smj7qoctu t481;
                            GM_ST(gm_b16_8, ((uint8_t *)&t481 + 0), t477);
                            GM_ST(gm_b16_8, ((uint8_t *)&t481 + 16), t480);
                            gm_f_format_m1f4vhf((void *)&gm_l38, (void *)NULL, cx, NULL, &t481);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t482 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                            if (!t482) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_src7u9cfdjh7s0n, 220, 32);
                            gm_b16_8 t483 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                            gm_b16_8 t484 = GM_LD(gm_b16_8, ((uint8_t *)&t483));
                            gm_b0_1 t485 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t473, t474, t484);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        void * t486 = gm_l0;
                        void * t487 = GM_LD(void *, ((uint8_t *)t486));
                        gm_tl874t2atnlh3 t488 = gm_l5;
                        void * t489 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t487, t488);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t489;
                        goto gm_fret;
                    }
                    {
                        {
                            void * t490 = gm_l0;
                            void * t491 = GM_LD(void *, ((uint8_t *)t490));
                            uint64_t t492 = gm_l8;
                            gm_t88rcucg1b7uk t493 = gm_l25;
                            gm_f_vector_shape_error_3invhhe((void *)&gm_l39, (void *)NULL, cx, NULL, t491, t492, t493);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t494 = (uint8_t)((((uint8_t *)&gm_l39))[16] != 0);
                            if (!t494) {
                                goto gm_lb45;
                            }
                            gm_b16_8 t495 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l39));
                            gm_l26 = t495;
                            void * t496 = gm_l0;
                            gm_tl874t2atnlh3 t497 = gm_l5;
                            gm_b16_8 t498 = gm_l26;
                            gm_b0_1 t499 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t496, t497, t498);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            void * t500 = gm_l0;
                            void * t501 = GM_LD(void *, ((uint8_t *)t500));
                            gm_tl874t2atnlh3 t502 = gm_l5;
                            void * t503 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t501, t502);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t503;
                            goto gm_fret;
                        }
gm_lb45:;
                    }
gm_lb44:;
                    void * t504 = gm_l6;
                    uint64_t t505 = gm_l8;
                    gm_t88rcucg1b7uk t506 = gm_l25;
                    gm_t8588ma2cr9d7 t507;
                    GM_ST(uint8_t, (uint8_t *)&t507 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t507 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t507 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t507 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t507 + 0), ((uint8_t)56ULL));
                    gm_t88rcucg1b7uk t508 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t504, t505, t506);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t509 = (void *)((uint8_t *)&gm_l11);
                    uint64_t t510 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t511;
                    GM_ST(void *, (uint8_t *)&t511, (void *)(((uint8_t *)t509 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t511 + 8, (uintptr_t)(t510));
                    gm_b16_8 t512 = GM_LD(gm_b16_8, ((uint8_t *)&t511));
                    gm_tl874t2atnlh3 t513 = gm_l5;
                    void * t514 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t507, t508, t512, t513);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t514;
                    goto gm_fret;
                }
gm_lb43:;
            }
            {
                {
                    gm_b16_8 t515 = gm_l3;
                    gm_b16_8 t516;
                    GM_ST(void *, (uint8_t *)&t516, (void *)(gm_s398novfl4jrd));
                    GM_ST(uintptr_t, (uint8_t *)&t516 + 8, (uintptr_t)(4));
                    uint8_t t517 = (gm_eq0iqiubfm7e(((uint8_t *)&t515), ((uint8_t *)&t516)));
                    if (!t517) {
                        goto gm_lb46;
                    }
                    void * t518 = gm_l0;
                    gm_b16_8 t519 = gm_l4;
                    gm_b16_8 t520 = gm_l3;
                    gm_tl874t2atnlh3 t521 = gm_l5;
                    uint8_t t522 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t518, t519, ((uint64_t)0ULL), t520, t521);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t523 = (uint8_t)!t522;
                    if (t523) {
                        void * t524 = gm_l0;
                        void * t525 = GM_LD(void *, ((uint8_t *)t524));
                        gm_tl874t2atnlh3 t526 = gm_l5;
                        void * t527 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t525, t526);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t527;
                        goto gm_fret;
                    }
                    void * t528 = gm_l6;
                    gm_t88rcucg1b7uk t529 = gm_l9;
                    uint8_t t530 = gm_f_is_float_3jk89qu(t528, GM_CONV(gm_csguankko4lmhd, t529));
                    uint8_t t531 = (uint8_t)!t530;
                    if (t531) {
                        void * t532 = gm_l0;
                        gm_b16_8 t533 = gm_l3;
                        gm_t88rcucg1b7uk t534 = gm_l2;
                        gm_b16_8 t535;
                        GM_ST(void *, (uint8_t *)&t535, (void *)(gm_s0b4nsmmmqukr));
                        GM_ST(uintptr_t, (uint8_t *)&t535 + 8, (uintptr_t)(11));
                        gm_tl874t2atnlh3 t536 = gm_l5;
                        void * t537 = gm_f_lane_needs_a6gcpil((void *)NULL, cx, NULL, t532, t533, t534, t535, t536);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t537;
                        goto gm_fret;
                    }
                    gm_t9qkh0rsooc8d t538;
                    GM_ST(uint8_t, ((uint8_t *)&t538 + 0), ((uint8_t)0ULL));
                    gm_t88rcucg1b7uk t539 = gm_l2;
                    void * t540 = (void *)((uint8_t *)&gm_l11);
                    uint64_t t541 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t542;
                    GM_ST(void *, (uint8_t *)&t542, (void *)(((uint8_t *)t540 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t542 + 8, (uintptr_t)(t541));
                    gm_b16_8 t543 = GM_LD(gm_b16_8, ((uint8_t *)&t542));
                    gm_tl874t2atnlh3 t544 = gm_l5;
                    gm_t8588ma2cr9d7 t545;
                    GM_ST(uint8_t, (uint8_t *)&t545 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t545 + 2, 0);
                    GM_ST(uint8_t, (uint8_t *)&t545 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t545 + 6, 0);
                    GM_ST(uint32_t, (uint8_t *)&t545 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t545 + 0), ((uint8_t)57ULL));
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t545 + 4), t538);
                    void * t546 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t545, t539, t543, t544);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t546;
                    goto gm_fret;
                }
gm_lb46:;
                {
                    {
                        {
                            gm_b16_8 t547 = gm_l3;
                            gm_b16_8 t548;
                            GM_ST(void *, (uint8_t *)&t548, (void *)(gm_s7f68g16j7a3o));
                            GM_ST(uintptr_t, (uint8_t *)&t548 + 8, (uintptr_t)(5));
                            uint8_t t549 = (gm_eq0iqiubfm7e(((uint8_t *)&t547), ((uint8_t *)&t548)));
                            if (!t549) {
                                goto gm_lb49;
                            }
                            goto gm_lb48;
                        }
gm_lb49:;
                        {
                            gm_b16_8 t550 = gm_l3;
                            gm_b16_8 t551;
                            GM_ST(void *, (uint8_t *)&t551, (void *)(gm_sjb60mo5hejk2));
                            GM_ST(uintptr_t, (uint8_t *)&t551 + 8, (uintptr_t)(5));
                            uint8_t t552 = (gm_eq0iqiubfm7e(((uint8_t *)&t550), ((uint8_t *)&t551)));
                            if (!t552) {
                                goto gm_lb50;
                            }
                            goto gm_lb48;
                        }
gm_lb50:;
                        {
                            gm_b16_8 t553 = gm_l3;
                            gm_b16_8 t554;
                            GM_ST(void *, (uint8_t *)&t554, (void *)(gm_shne2m0cj5kgi));
                            GM_ST(uintptr_t, (uint8_t *)&t554 + 8, (uintptr_t)(5));
                            uint8_t t555 = (gm_eq0iqiubfm7e(((uint8_t *)&t553), ((uint8_t *)&t554)));
                            if (!t555) {
                                goto gm_lb51;
                            }
                            goto gm_lb48;
                        }
gm_lb51:;
                        {
                            gm_b16_8 t556 = gm_l3;
                            gm_b16_8 t557;
                            GM_ST(void *, (uint8_t *)&t557, (void *)(gm_shb5mqv6etf1t));
                            GM_ST(uintptr_t, (uint8_t *)&t557 + 8, (uintptr_t)(4));
                            uint8_t t558 = (gm_eq0iqiubfm7e(((uint8_t *)&t556), ((uint8_t *)&t557)));
                            if (!t558) {
                                goto gm_lb52;
                            }
                            goto gm_lb48;
                        }
gm_lb52:;
                        {
                            gm_b16_8 t559 = gm_l3;
                            gm_b16_8 t560;
                            GM_ST(void *, (uint8_t *)&t560, (void *)(gm_sbanjlvk3n14i));
                            GM_ST(uintptr_t, (uint8_t *)&t560 + 8, (uintptr_t)(3));
                            uint8_t t561 = (gm_eq0iqiubfm7e(((uint8_t *)&t559), ((uint8_t *)&t560)));
                            if (!t561) {
                                goto gm_lb53;
                            }
                            goto gm_lb48;
                        }
gm_lb53:;
                        goto gm_lb47;
                    }
gm_lb48:;
                    void * t562 = gm_l0;
                    void * t563 = gm_l1;
                    gm_t88rcucg1b7uk t564 = gm_l2;
                    gm_b16_8 t565 = gm_l3;
                    gm_b16_8 t566 = gm_l4;
                    gm_tl874t2atnlh3 t567 = gm_l5;
                    void * t568 = gm_f_vector_lane_conversion_chta07f((void *)NULL, cx, NULL, t562, t563, t564, t565, t566, t567);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t568;
                    goto gm_fret;
                }
gm_lb47:;
            }
            {
                {
                    gm_b16_8 t569 = gm_l3;
                    gm_b16_8 t570;
                    GM_ST(void *, (uint8_t *)&t570, (void *)(gm_salpeh0v5rqdf));
                    GM_ST(uintptr_t, (uint8_t *)&t570 + 8, (uintptr_t)(7));
                    uint8_t t571 = (gm_eq0iqiubfm7e(((uint8_t *)&t569), ((uint8_t *)&t570)));
                    if (!t571) {
                        goto gm_lb54;
                    }
                    void * t572 = gm_l0;
                    void * t573 = gm_l1;
                    gm_t88rcucg1b7uk t574 = gm_l2;
                    gm_b16_8 t575 = gm_l4;
                    gm_tl874t2atnlh3 t576 = gm_l5;
                    void * t577 = gm_f_vector_shuffle_5ime7a7((void *)NULL, cx, NULL, t572, t573, t574, t575, t576);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t577;
                    goto gm_fret;
                }
gm_lb54:;
                {
                    gm_b16_8 t578 = gm_l3;
                    gm_b16_8 t579;
                    GM_ST(void *, (uint8_t *)&t579, (void *)(gm_s0ugop4l95o1j));
                    GM_ST(uintptr_t, (uint8_t *)&t579 + 8, (uintptr_t)(5));
                    uint8_t t580 = (gm_eq0iqiubfm7e(((uint8_t *)&t578), ((uint8_t *)&t579)));
                    if (!t580) {
                        goto gm_lb55;
                    }
                    void * t581 = gm_l0;
                    gm_b16_8 t582 = gm_l4;
                    gm_b16_8 t583 = gm_l3;
                    gm_tl874t2atnlh3 t584 = gm_l5;
                    uint8_t t585 = gm_f_arity_vmamjfj((void *)NULL, cx, NULL, t581, t582, ((uint64_t)1ULL), t583, t584);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t586 = (uint8_t)!t585;
                    if (t586) {
                        void * t587 = gm_l0;
                        void * t588 = GM_LD(void *, ((uint8_t *)t587));
                        gm_tl874t2atnlh3 t589 = gm_l5;
                        void * t590 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t588, t589);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t590;
                        goto gm_fret;
                    }
                    void * t591 = gm_l0;
                    gm_b16_8 t592 = gm_l4;
                    void * t593 = gm_l6;
                    gm_t88rcucg1b7uk t594 = gm_l9;
                    gm_t88rcucg1b7uk t595 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t593, ((uint8_t)0), t594);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tl874t2atnlh3 t596 = gm_l5;
                    void * t597 = gm_f_value_arg_i5l23i1((void *)NULL, cx, NULL, t591, t592, ((uint64_t)0ULL), t595, t596);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l27 = t597;
                    void * t598 = gm_l6;
                    gm_t8588ma2cr9d7 t599;
                    GM_ST(uint8_t, (uint8_t *)&t599 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t599 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t599 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t599 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t599 + 0), ((uint8_t)60ULL));
                    gm_t88rcucg1b7uk t600 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t598);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t601 = gm_l1;
                    void * t602 = gm_l27;
                    gm_t5fru0lp11n7t t603;
                    GM_ST(void *, ((uint8_t *)&t603 + 0), t601);
                    GM_ST(void *, ((uint8_t *)&t603 + 8), t602);
                    gm_l28 = t603;
                    void * t604 = (void *)((uint8_t *)&gm_l28);
                    uint64_t t605 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t606;
                    GM_ST(void *, (uint8_t *)&t606, (void *)(((uint8_t *)t604 + (uintptr_t)((uint64_t)0ULL) * 8)));
                    GM_ST(uintptr_t, (uint8_t *)&t606 + 8, (uintptr_t)(t605));
                    gm_tl874t2atnlh3 t607 = gm_l5;
                    void * t608 = gm_f_builtin_a7cjd7d((void *)NULL, cx, NULL, t599, t600, t606, t607);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t608;
                    goto gm_fret;
                }
gm_lb55:;
                {
                    goto gm_lb1;
                }
gm_lb56:;
                gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 157, 5);
            }
        }
gm_lb1:;
    }
    {
        void * t609 = gm_l0;
        gm_tl874t2atnlh3 t610 = gm_l5;
        gm_b16_8 t611 = gm_l3;
        gm_tnpifv20irnph t612;
        GM_ST(gm_b16_8, ((uint8_t *)&t612 + 0), t611);
        gm_f_format_rr83dbe((void *)&gm_l40, (void *)NULL, cx, NULL, t612);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t613 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
        if (!t613) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 8, gm_src7u9cfdjh7s0n, 243, 20);
        gm_b16_8 t614 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l40) + 8));
        gm_b16_8 t615 = GM_LD(gm_b16_8, ((uint8_t *)&t614));
        gm_b0_1 t616 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t609, t610, t615);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t617 = gm_l0;
        void * t618 = GM_LD(void *, ((uint8_t *)t617));
        gm_tl874t2atnlh3 t619 = gm_l5;
        void * t620 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t618, t619);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t620;
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
__attribute__((aligned(4))) void * gm_f_vector_shuffle_5ime7a7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_vector_shuffle_5ime7a7 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* recv */
    gm_t88rcucg1b7uk gm_l2; /* vt */
    gm_b16_8 gm_l3; /* args */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_tlcm4lvu3g1a8 gm_l6; /* parts */
    gm_t88rcucg1b7uk gm_l7; /* usize_t */
    void * gm_l8; /* second */
    void * gm_l9; /* first */
    void * gm_l11; /* v */
    uint64_t gm_l12; /* len */
    void * gm_l13; /* w */
    gm_t88rcucg1b7uk gm_l14; /* wt */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* k */
    void * gm_l21; /* v */
    uint64_t gm_l22; /* x */
    uint64_t gm_l23; /* m */
    gm_b16_8 gm_l24; /* msg */
    void * gm_l26; /* w */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    uint8_t gm_l30; /* nv */
    GM_STACK_CHECK(&F, 2214u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tb7u19loo3j87 gm_l31;
    gm_tabb710d9mq2e gm_l32;
    gm_tl1vi0nd1uql9 gm_l33;
    uint8_t gm_l34;
    uint8_t gm_l35;
    gm_tf5srh4mp4b3i gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_th2lqbo77pvsc gm_l38;
    gm_tabb710d9mq2e gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    uint8_t gm_l43;
    {
        void * t33 = gm_l0;
        void * t34 = GM_LD(void *, ((uint8_t *)t33));
        void * t35 = (void *)(((uint8_t *)t34) + 8);
        gm_l5 = t35;
        void * t36 = gm_l5;
        gm_t88rcucg1b7uk t37 = gm_l2;
        gm_f_vector_parts_crrfopm((void *)&gm_l31, t36, GM_CONV(gm_csguankko4lmhd, t37));
        uint8_t t38 = (uint8_t)((((uint8_t *)&gm_l31))[16] != 0);
        if (t38) {
            gm_tlcm4lvu3g1a8 t39 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l31));
            gm_l6 = t39;
        } else {
            void * t40 = gm_l0;
            void * t41 = GM_LD(void *, ((uint8_t *)t40));
            gm_tl874t2atnlh3 t42 = gm_l4;
            void * t43 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t41, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t43;
            goto gm_fret;
        }
        void * t44 = gm_l5;
        gm_t88rcucg1b7uk t45 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t45;
        gm_l8 = ((void *)0);
        gm_l9 = ((void *)0);
        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t47 = (t46 > ((uint64_t)0ULL));
        if (t47) {
            {
                {
                    void * t48 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t48) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)0ULL));
                    if (!t51) {
                        goto gm_lb2;
                    }
                    memmove(((uint8_t *)&F.l10), ((((uint8_t *)t48) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                    void * t52 = gm_l0;
                    void * t53 = (void *)((uint8_t *)&F.l10);
                    gm_tkn42u7na3t81 t54;
                    memset(&t54, 0, sizeof t54);
                    void * t55 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t52, t53, t54);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l11 = t55;
                    void * t56 = gm_l5;
                    void * t57 = gm_l0;
                    void * t58 = (void *)(((uint8_t *)t57) + 96);
                    void * t59 = gm_l0;
                    void * t60 = GM_LD(void *, ((uint8_t *)t59));
                    void * t61 = gm_l11;
                    gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t61));
                    gm_t88rcucg1b7uk t63 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t58, t60, t62);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t64 = gm_f_is_vector_gkq6b88(t56, GM_CONV(gm_csguankko4lmhd, t63));
                    if (t64) {
                        void * t65 = gm_l11;
                        gm_l8 = t65;
                    } else {
                        void * t66 = gm_l11;
                        gm_l9 = t66;
                    }
                    goto gm_lb1;
                }
gm_lb2:;
            }
gm_lb1:;
        }
        uint64_t t67 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
        gm_l12 = t67;
        {
            {
                uint8_t t68 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l8)) != 0);
                if (!t68) {
                    goto gm_lb4;
                }
                void * t69 = GM_LD(void *, ((uint8_t *)&gm_l8));
                gm_l13 = t69;
                void * t70 = gm_l0;
                void * t71 = (void *)(((uint8_t *)t70) + 96);
                void * t72 = gm_l0;
                void * t73 = GM_LD(void *, ((uint8_t *)t72));
                void * t74 = gm_l13;
                gm_t88rcucg1b7uk t75 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t74));
                gm_t88rcucg1b7uk t76 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t71, t73, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l14 = t76;
                gm_t88rcucg1b7uk t77 = gm_l14;
                gm_t88rcucg1b7uk t78 = gm_l2;
                uint8_t t79 = (!gm_eq9m6mvtmgde(((uint8_t *)&t77), ((uint8_t *)&t78)));
                if (t79) {
                    {
                        void * t80 = gm_l0;
                        void * t81 = gm_l13;
                        gm_tl874t2atnlh3 t82 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t81) + 72));
                        void * t83 = gm_l0;
                        gm_t88rcucg1b7uk t84 = gm_l2;
                        gm_b16_8 t85 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t83, t84);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        void * t86 = gm_l0;
                        gm_t88rcucg1b7uk t87 = gm_l14;
                        gm_b16_8 t88 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t86, t87);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6j6smj7qoctu t89;
                        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t85);
                        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 16), t88);
                        gm_f_format_f8dhsig((void *)&gm_l32, (void *)NULL, cx, NULL, &t89);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                        if (!t90) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_src7u9cfdjh7s0n, 303, 30);
                        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
                        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                        gm_b0_1 t93 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t80, t82, t92);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    void * t94 = gm_l0;
                    void * t95 = GM_LD(void *, ((uint8_t *)t94));
                    gm_tl874t2atnlh3 t96 = gm_l4;
                    void * t97 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t95, t96);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t97;
                    goto gm_fret;
                }
                uint64_t t98 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
                uint64_t r99; bool o100 = __builtin_mul_overflow(((uint64_t)2ULL), t98, &r99);
                if (o100) gm_trap("integer overflow in `*`", gm_src7u9cfdjh7s0n, 306, 15);
                uint64_t t101 = r99;
                gm_l12 = t101;
                goto gm_lb3;
            }
gm_lb4:;
        }
gm_lb3:;
    }
    {
        gm_trqfvmjtt4sij t102;
        gm_f_list_vp1djv1_2((void *)&t102, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l15 = t102;
        gm_b16_8 t103 = gm_l3;
        gm_l16 = t103;
        gm_l17 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t104 = gm_l17;
                uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                uint8_t t106 = (t104 < t105);
                if (!t106) {
                    goto gm_lb5;
                }
                {
                    {
                        uint64_t t107 = gm_l17;
                        void * t108 = GM_LD(void *, ((uint8_t *)&gm_l16));
                        uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                        uint64_t t110 = gm_l17;
                        gm_t7te2tdpebbt4 t111 = GM_LD(gm_t7te2tdpebbt4, (((uint8_t *)t108) + (uintptr_t)t110 * 144));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l33 + 0), t107);
                        GM_ST(gm_t7te2tdpebbt4, ((uint8_t *)&gm_l33 + 8), t111);
                    }
                    {
                        {
                            uint64_t t112 = GM_LD(uint64_t, ((uint8_t *)&gm_l33));
                            gm_l18 = t112;
                            memmove(((uint8_t *)&F.l19), (((uint8_t *)&gm_l33) + 8), 144);
                            {
                                {
                                    uint64_t t113 = gm_l18;
                                    uint8_t t114 = ((t113 == ((uint64_t)0ULL)));
                                    if (!t114) {
                                        goto gm_lb11;
                                    }
                                    {
                                        {
                                            uint8_t t115 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l8)) != 0);
                                            if (t115) {
                                                goto gm_lb13;
                                            }
                                            gm_l34 = ((uint8_t)0);
                                            goto gm_lb12;
                                        }
gm_lb13:;
                                        {
                                            gm_l34 = ((uint8_t)1);
                                            goto gm_lb12;
                                        }
gm_lb14:;
                                        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 310, 23);
                                    }
gm_lb12:;
                                    uint8_t t116 = gm_l34;
                                    if (!t116) {
                                        goto gm_lb11;
                                    }
                                    goto gm_lb7;
                                }
gm_lb11:;
                            }
gm_lb10:;
                            {
                                {
                                    uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&F.l19) + 0));
                                    uint8_t t118 = (uint8_t)(t117 == ((uint8_t)0ULL));
                                    if (!t118) {
                                        goto gm_lb16;
                                    }
                                    {
                                        memmove(((uint8_t *)&F.l20), (((uint8_t *)&F.l19) + 8), 88);
                                    }
                                    uint64_t t119 = gm_l18;
                                    uint8_t t120 = ((t119 == ((uint64_t)0ULL)));
                                    if (t120) {
                                        uint8_t t121 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l9)) != 0);
                                        if (!t121) gm_trap("unwrap of null", gm_src7u9cfdjh7s0n, 315, 26);
                                        void * t122 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                        gm_l21 = t122;
                                    } else {
                                        void * t123 = gm_l0;
                                        void * t124 = (void *)((uint8_t *)&F.l20);
                                        gm_t88rcucg1b7uk t125 = gm_l7;
                                        gm_tkn42u7na3t81 t126;
                                        GM_ST(uint8_t, (uint8_t *)&t126 + 5, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t126 + 6, 0);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t126), t125);
                                        ((uint8_t *)&t126)[4] = 1;
                                        void * t127 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t123, t124, t126);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l21 = t127;
                                    }
                                    void * t128 = gm_l0;
                                    void * t129 = gm_l21;
                                    gm_t88rcucg1b7uk t130 = gm_l2;
                                    uint64_t t131 = gm_l12;
                                    {
                                        {
                                            uint8_t t132 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l8)) != 0);
                                            if (t132) {
                                                goto gm_lb18;
                                            }
                                            gm_l35 = ((uint8_t)0);
                                            goto gm_lb17;
                                        }
gm_lb18:;
                                        {
                                            gm_l35 = ((uint8_t)1);
                                            goto gm_lb17;
                                        }
gm_lb19:;
                                        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 316, 43);
                                    }
gm_lb17:;
                                    uint8_t t133 = gm_l35;
                                    gm_tf5srh4mp4b3i t134 = gm_f_shuffle_index_atl9l9i((void *)NULL, cx, NULL, t128, t129, t130, t131, t133);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l36 = t134;
                                    uint8_t t135 = (uint8_t)((((uint8_t *)&gm_l36))[8] != 0);
                                    if (t135) {
                                        uint64_t t136 = GM_LD(uint64_t, ((uint8_t *)&gm_l36));
                                        gm_l22 = t136;
                                    } else {
                                        void * t137 = gm_l0;
                                        void * t138 = GM_LD(void *, ((uint8_t *)t137));
                                        gm_tl874t2atnlh3 t139 = gm_l4;
                                        void * t140 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t138, t139);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.ret = t140;
                                        goto gm_fret;
                                    }
                                    {
                                        void * t141 = (void *)((uint8_t *)&F.l15);
                                        gm_t88rcucg1b7uk t142 = gm_l7;
                                        uint64_t t143 = gm_l22;
                                        gm_tbci5ospk5nnn t144;
                                        gm_f_from_u64_ud13q5v((void *)&t144, (void *)NULL, cx, NULL, t143);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        void * t145 = gm_l21;
                                        gm_tl874t2atnlh3 t146 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t145) + 72));
                                        gm_to0f5qn75cgg8 t147;
                                        GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t147 + 56, 0);
                                        GM_ST(uint8_t, ((uint8_t *)&t147 + 0), ((uint8_t)0ULL));
                                        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t147 + 8), t144);
                                        void * t148 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t142, &t147, t146);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t149 = gm_f_push_d943vgp_2(t141, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t148)));
                                        gm_l37 = t149;
                                    }
                                    uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                    if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_src7u9cfdjh7s0n, 317, 9);
                                    goto gm_lb15;
                                }
gm_lb16:;
                                void * t151 = gm_l0;
                                gm_tl874t2atnlh3 t152 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 128));
                                gm_b16_8 t153;
                                GM_ST(void *, (uint8_t *)&t153, (void *)(gm_sioam4mlrjuar));
                                GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(51));
                                gm_b0_1 t154 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t151, t152, t153);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t155 = gm_l0;
                                void * t156 = GM_LD(void *, ((uint8_t *)t155));
                                gm_tl874t2atnlh3 t157 = gm_l4;
                                void * t158 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t156, t157);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t158;
                                goto gm_fret;
                            }
gm_lb15:;
                            goto gm_lb8;
                        }
gm_lb9:;
                        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 309, 5);
                    }
gm_lb8:;
                }
gm_lb7:;
                uint64_t t159 = gm_l17;
                uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                uint64_t t162 = r160;
                gm_l17 = t162;
            }
        }
gm_lb5:;
    }
    {
        uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)&F.l15) + 8));
        uint64_t t164 = (uint64_t)t163;
        gm_l23 = t164;
        {
            {
                {
                    void * t165 = gm_l0;
                    void * t166 = GM_LD(void *, ((uint8_t *)t165));
                    uint64_t t167 = gm_l23;
                    gm_t88rcucg1b7uk t168 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
                    gm_f_vector_shape_error_3invhhe((void *)&gm_l38, (void *)NULL, cx, NULL, t166, t167, t168);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t169 = (uint8_t)((((uint8_t *)&gm_l38))[16] != 0);
                if (!t169) {
                    goto gm_lb21;
                }
                gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l38));
                gm_l24 = t170;
                {
                    void * t171 = gm_l0;
                    gm_tl874t2atnlh3 t172 = gm_l4;
                    gm_b16_8 t173 = gm_l24;
                    gm_tnpifv20irnph t174;
                    GM_ST(gm_b16_8, ((uint8_t *)&t174 + 0), t173);
                    gm_f_format_7cgva50((void *)&gm_l39, (void *)NULL, cx, NULL, t174);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t175 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                    if (!t175) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_src7u9cfdjh7s0n, 321, 24);
                    gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
                    gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&t176));
                    gm_b0_1 t178 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t171, t172, t177);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t179 = gm_l0;
                void * t180 = GM_LD(void *, ((uint8_t *)t179));
                gm_tl874t2atnlh3 t181 = gm_l4;
                void * t182 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t180, t181);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t182;
                goto gm_fret;
            }
gm_lb21:;
        }
gm_lb20:;
        gm_trqfvmjtt4sij t183;
        gm_f_list_vp1djv1_2((void *)&t183, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l25 = t183;
        void * t184 = (void *)((uint8_t *)&F.l25);
        void * t185 = gm_l1;
        gm_tftb1odc739t5 t186 = gm_f_push_d943vgp_2(t184, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t185)));
        gm_l40 = t186;
        uint8_t t187 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
        if (!t187) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_src7u9cfdjh7s0n, 325, 5);
        {
            {
                uint8_t t188 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l8)) != 0);
                if (!t188) {
                    goto gm_lb23;
                }
                void * t189 = GM_LD(void *, ((uint8_t *)&gm_l8));
                gm_l26 = t189;
                void * t190 = (void *)((uint8_t *)&F.l25);
                void * t191 = gm_l26;
                gm_tftb1odc739t5 t192 = gm_f_push_d943vgp_2(t190, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t191)));
                gm_l41 = t192;
                uint8_t t193 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                if (!t193) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_src7u9cfdjh7s0n, 326, 22);
                goto gm_lb22;
            }
gm_lb23:;
        }
gm_lb22:;
        gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
        gm_l27 = t194;
        gm_l28 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t195 = gm_l28;
                uint64_t t196 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                uint8_t t197 = (t195 < t196);
                if (!t197) {
                    goto gm_lb24;
                }
                {
                    void * t198 = GM_LD(void *, ((uint8_t *)&gm_l27));
                    uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                    uint64_t t200 = gm_l28;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l29), (((uint8_t *)t198) + (uintptr_t)t200 * 88), 88);
                            }
                            {
                                void * t201 = (void *)((uint8_t *)&F.l25);
                                gm_tftb1odc739t5 t202 = gm_f_push_d943vgp_2(t201, &(*(gm_tsf6i2agfnpva *)((uint8_t *)&F.l29)));
                                gm_l42 = t202;
                            }
                            uint8_t t203 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                            if (!t203) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_src7u9cfdjh7s0n, 327, 26);
                            goto gm_lb27;
                        }
gm_lb28:;
                        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 327, 5);
                    }
gm_lb27:;
                }
gm_lb26:;
                uint64_t t204 = gm_l28;
                uint64_t r205; bool o206 = __builtin_add_overflow(t204, ((uint64_t)1ULL), &r205);
                uint64_t t207 = r205;
                gm_l28 = t207;
            }
        }
gm_lb24:;
    }
    {
        {
            {
                uint8_t t208 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l8)) != 0);
                if (t208) {
                    goto gm_lb30;
                }
                gm_l43 = ((uint8_t)1);
                goto gm_lb29;
            }
gm_lb30:;
            {
                gm_l43 = ((uint8_t)0);
                goto gm_lb29;
            }
gm_lb31:;
            gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 328, 17);
        }
gm_lb29:;
        uint8_t t209 = gm_l43;
        if (t209) {
            gm_l30 = ((uint8_t)1ULL);
        } else {
            gm_l30 = ((uint8_t)2ULL);
        }
        void * t210 = gm_l5;
        uint64_t t211 = gm_l23;
        gm_t88rcucg1b7uk t212 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
        gm_t88rcucg1b7uk t213 = gm_f_simd_eafgrv9((void *)NULL, cx, NULL, t210, t211, t212);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t214 = gm_l30;
        gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
        gm_t8588ma2cr9d7 t216;
        GM_ST(uint8_t, (uint8_t *)&t216 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t216 + 2, 0);
        GM_ST(uint8_t, (uint8_t *)&t216 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t216 + 6, 0);
        GM_ST(uint32_t, (uint8_t *)&t216 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t216 + 0), ((uint8_t)58ULL));
        GM_ST(uint8_t, ((uint8_t *)&t216 + 4), t214);
        gm_t9q8lhljes74c t217;
        GM_ST(uint32_t, (uint8_t *)&t217 + 12, 0);
        GM_ST(gm_t8588ma2cr9d7, ((uint8_t *)&t217 + 0), t216);
        GM_ST(gm_b16_8, ((uint8_t *)&t217 + 16), t215);
        gm_tl874t2atnlh3 t218 = gm_l4;
        gm_to0f5qn75cgg8 t219;
        GM_ST(uint8_t, (uint8_t *)&t219 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t219 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t219 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t219 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t219 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t219 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t219 + 0), ((uint8_t)33ULL));
        GM_ST(gm_t9q8lhljes74c, ((uint8_t *)&t219 + 8), t217);
        void * t220 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t213, &t219, t218);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t220;
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
__attribute__((aligned(4))) void * gm_f_vector_unary_2mbe97t(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_vector_unary_2mbe97t F;
    void * gm_l0; /* cx */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    void * gm_l2; /* v */
    gm_tl874t2atnlh3 gm_l3; /* span */
    void * gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* vt */
    gm_tlcm4lvu3g1a8 gm_l6; /* parts */
    gm_t88rcucg1b7uk gm_l7; /* lane */
    gm_b16_8 gm_l8; /* hint */
    gm_b16_8 gm_l9; /* s */
    GM_STACK_CHECK(&F, 1007u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tb7u19loo3j87 gm_l10;
    uint8_t gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_tabb710d9mq2e gm_l13;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    void * t9 = (void *)(((uint8_t *)t8) + 8);
    gm_l4 = t9;
    void * t10 = gm_l0;
    void * t11 = (void *)(((uint8_t *)t10) + 96);
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    void * t14 = gm_l2;
    gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t14));
    gm_t88rcucg1b7uk t16 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t11, t13, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t16;
    {
        void * t17 = gm_l4;
        gm_t88rcucg1b7uk t18 = gm_l5;
        gm_f_vector_parts_crrfopm((void *)&gm_l10, t17, GM_CONV(gm_csguankko4lmhd, t18));
    }
    uint8_t t19 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
    if (t19) {
        gm_tlcm4lvu3g1a8 t20 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l10));
        gm_l6 = t20;
    } else {
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, ((uint8_t *)t21));
        gm_tl874t2atnlh3 t23 = gm_l3;
        void * t24 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t24;
        goto gm_fret;
    }
    gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l6) + 8));
    gm_l7 = t25;
    {
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)0ULL));
            if (!t27) {
                goto gm_lb2;
            }
            void * t28 = gm_l4;
            gm_t88rcucg1b7uk t29 = gm_l7;
            uint8_t t30 = gm_f_is_signed_integer_s66djm7(t28, GM_CONV(gm_csguankko4lmhd, t29));
            if (t30) {
                gm_l11 = ((uint8_t)1);
            } else {
                void * t31 = gm_l4;
                gm_t88rcucg1b7uk t32 = gm_l7;
                uint8_t t33 = gm_f_is_float_3jk89qu(t31, GM_CONV(gm_csguankko4lmhd, t32));
                gm_l11 = t33;
            }
            uint8_t t34 = gm_l11;
            if (t34) {
                gm_t88rcucg1b7uk t35 = gm_l5;
                void * t36 = gm_l2;
                gm_t9qkh0rsooc8d t37;
                GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)0ULL));
                gm_t8ghgl2jj0qha t38;
                GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t38 + 0), t37);
                GM_ST(void *, ((uint8_t *)&t38 + 8), t36);
                gm_tl874t2atnlh3 t39 = gm_l3;
                gm_to0f5qn75cgg8 t40;
                memset(&t40, 0, sizeof t40);
                GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)29ULL));
                GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t40 + 8), t38);
                void * t41 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t35, &t40, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t41;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)1ULL));
            if (!t43) {
                goto gm_lb3;
            }
            void * t44 = gm_l4;
            gm_t88rcucg1b7uk t45 = gm_l7;
            uint8_t t46 = gm_f_is_float_3jk89qu(t44, GM_CONV(gm_csguankko4lmhd, t45));
            uint8_t t47 = (uint8_t)!t46;
            if (t47) {
                gm_t88rcucg1b7uk t48 = gm_l5;
                void * t49 = gm_l2;
                gm_t9qkh0rsooc8d t50;
                GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)1ULL));
                gm_t8ghgl2jj0qha t51;
                GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t51 + 0), t50);
                GM_ST(void *, ((uint8_t *)&t51 + 8), t49);
                gm_tl874t2atnlh3 t52 = gm_l3;
                gm_to0f5qn75cgg8 t53;
                memset(&t53, 0, sizeof t53);
                GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)29ULL));
                GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t53 + 8), t51);
                void * t54 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t48, &t53, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t54;
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)5ULL));
            if (!t56) {
                goto gm_lb4;
            }
            void * t57 = gm_l4;
            gm_t88rcucg1b7uk t58 = gm_l7;
            uint8_t t59 = gm_f_is_bool_ajhjgeg(t57, GM_CONV(gm_csguankko4lmhd, t58));
            if (t59) {
                gm_b16_8 t60;
                GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sajso5sujsruj));
                GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(46));
                gm_l8 = t60;
            } else {
                gm_b16_8 t61;
                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(0));
                gm_l8 = t61;
            }
            {
                void * t62 = gm_l0;
                gm_tl874t2atnlh3 t63 = gm_l3;
                void * t64 = gm_l0;
                gm_t88rcucg1b7uk t65 = gm_l5;
                gm_b16_8 t66 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t64, t65);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t67 = gm_l8;
                gm_t6j6smj7qoctu t68;
                GM_ST(gm_b16_8, ((uint8_t *)&t68 + 0), t66);
                GM_ST(gm_b16_8, ((uint8_t *)&t68 + 16), t67);
                gm_f_format_s7ci5ul((void *)&gm_l12, (void *)NULL, cx, NULL, &t68);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src7u9cfdjh7s0n, 139, 28);
                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
                gm_b0_1 t72 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t62, t63, t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t73 = gm_l0;
            void * t74 = GM_LD(void *, ((uint8_t *)t73));
            gm_tl874t2atnlh3 t75 = gm_l3;
            void * t76 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t74, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t76;
            goto gm_fret;
        }
gm_lb4:;
        {
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_src7u9cfdjh7s0n, 134, 5);
    }
gm_lb1:;
    gm_t9qkh0rsooc8d t77 = gm_l1;
    uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&t77) + 0));
    uint8_t t79 = (uint8_t)((t78 == ((uint8_t)0ULL)));
    gm_t9qkh0rsooc8d t80;
    GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)0ULL));
    if (t79) {
        gm_b16_8 t81;
        GM_ST(void *, (uint8_t *)&t81, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(1));
        gm_l9 = t81;
    } else {
        gm_b16_8 t82;
        GM_ST(void *, (uint8_t *)&t82, (void *)(gm_sa831sfid0hf3));
        GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(1));
        gm_l9 = t82;
    }
    {
        void * t83 = gm_l0;
        gm_tl874t2atnlh3 t84 = gm_l3;
        gm_b16_8 t85 = gm_l9;
        void * t86 = gm_l0;
        gm_t88rcucg1b7uk t87 = gm_l5;
        gm_b16_8 t88 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t86, t87);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t89;
        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 0), t85);
        GM_ST(gm_b16_8, ((uint8_t *)&t89 + 16), t88);
        gm_f_format_ul5ufam((void *)&gm_l13, (void *)NULL, cx, NULL, &t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
        if (!t90) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_src7u9cfdjh7s0n, 145, 20);
        gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
        gm_b0_1 t93 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t83, t84, t92);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t94 = gm_l0;
    void * t95 = GM_LD(void *, ((uint8_t *)t94));
    gm_tl874t2atnlh3 t96 = gm_l3;
    void * t97 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t95, t96);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t97;
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

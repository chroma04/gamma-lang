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
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t pe[6]; } gm_t0rfe1ou14h21;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; int64_t f1; gm_b16_8 f2; } gm_tqj9esv5ukti3;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t0rfe1ou14h21) == 64 && _Alignof(gm_t0rfe1ou14h21) == 8 && offsetof(gm_t0rfe1ou14h21, f0) == 0 && offsetof(gm_t0rfe1ou14h21, f1) == 8 && offsetof(gm_t0rfe1ou14h21, f2) == 16 && offsetof(gm_t0rfe1ou14h21, f3) == 24 && offsetof(gm_t0rfe1ou14h21, f4) == 40 && offsetof(gm_t0rfe1ou14h21, f5) == 56 && offsetof(gm_t0rfe1ou14h21, f6) == 57, "layout of `Target`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
typedef struct { gm_b16_8 f0; gm_tqj9esv5ukti3 f1; } gm_tggvvh3lncvcg;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tqj9esv5ukti3) == 40 && _Alignof(gm_tqj9esv5ukti3) == 8 && offsetof(gm_tqj9esv5ukti3, f0) == 0 && offsetof(gm_tqj9esv5ukti3, f1) == 16 && offsetof(gm_tqj9esv5ukti3, f2) == 24, "layout of `Doc`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tggvvh3lncvcg) == 56 && _Alignof(gm_tggvvh3lncvcg) == 8 && offsetof(gm_tggvvh3lncvcg, f0) == 0 && offsetof(gm_tggvvh3lncvcg, f1) == 16, "layout of `(key: string, value: Doc)`");
struct gm_fr_check_triple_uqaropr {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* parts */
    gm_t0rfe1ou14h21 l9; /* tg */
    gm_tl572d7579hns ret;
    gm_b0_1 w0;
};
struct gm_fr_dir_of_ho73hue {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_for_body_1qm1hvv {
    gm_frame h;
    gm_tggvvh3lncvcg l0; /* <item> */
    gm_tggvvh3lncvcg l1; /* kv */
    gm_b0_1 ret;
};
struct gm_fr_for_body_i2koblr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_open_dirs_9ud02ot {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* dirs */
    gm_trqfvmjtt4sij ret;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dir_of_ho73hue(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0m0ntf2(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1qm1hvv(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_7fmdm88(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_f3mct6g(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_g9kn84c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2koblr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
extern uint8_t gm_c0a4m567j25[48] __attribute__((aligned(8)));
extern uint8_t gm_c0on8osiuo6[5] __attribute__((aligned(1)));
extern uint8_t gm_c100d3qhpv5[6] __attribute__((aligned(1)));
extern uint8_t gm_c17ggp4avhs[5] __attribute__((aligned(1)));
extern uint8_t gm_c1fmjj3efs7[6] __attribute__((aligned(1)));
extern uint8_t gm_c28e6r6fc17[3] __attribute__((aligned(1)));
extern uint8_t gm_c499h6aasrh[19] __attribute__((aligned(1)));
extern uint8_t gm_c49andki61b[4] __attribute__((aligned(4)));
extern uint8_t gm_c4b4noc2csc[8] __attribute__((aligned(1)));
extern uint8_t gm_c4q1r8o7m8d[24] __attribute__((aligned(1)));
extern uint8_t gm_c53j3ci82e6[16] __attribute__((aligned(1)));
extern uint8_t gm_c540sftfgo9[15] __attribute__((aligned(1)));
extern uint8_t gm_c6135o3s80k[13] __attribute__((aligned(1)));
extern uint8_t gm_c6ge57r72i4[11] __attribute__((aligned(1)));
extern uint8_t gm_c6j8kar54p4[2] __attribute__((aligned(1)));
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_c706grja04t[14] __attribute__((aligned(1)));
extern uint8_t gm_c741neqb2fg[3] __attribute__((aligned(1)));
extern uint8_t gm_c7jr3ergrd7[8] __attribute__((aligned(1)));
extern uint8_t gm_c87f6jpfo1e[12] __attribute__((aligned(1)));
extern uint8_t gm_c9h924ckf95[4] __attribute__((aligned(1)));
extern uint8_t gm_c9vr8sjt2in[15] __attribute__((aligned(1)));
extern uint8_t gm_ca3v3u0frmk[45] __attribute__((aligned(1)));
extern uint8_t gm_ca9a9qf5uf3[11] __attribute__((aligned(1)));
extern uint8_t gm_cajrm882b9k[83] __attribute__((aligned(1)));
extern uint8_t gm_cb104msc6as[53] __attribute__((aligned(1)));
extern uint8_t gm_cbsek8oicg9[8] __attribute__((aligned(1)));
extern uint8_t gm_ccq673m4u5l[5] __attribute__((aligned(1)));
extern uint8_t gm_ccu87ihd5ti[15] __attribute__((aligned(1)));
extern uint8_t gm_cd16n891kj6[15] __attribute__((aligned(1)));
extern uint8_t gm_cd1ptb89qfb[5] __attribute__((aligned(1)));
extern uint8_t gm_cdn7nhdpp7m[4] __attribute__((aligned(1)));
extern uint8_t gm_cdp0830p9p2[7] __attribute__((aligned(1)));
extern uint8_t gm_ce9o6afpeun[2] __attribute__((aligned(1)));
extern uint8_t gm_cem22767mo4[4] __attribute__((aligned(4)));
extern uint8_t gm_ceq9kh1e7ck[7] __attribute__((aligned(1)));
extern uint8_t gm_cfgjuuq94tk[10] __attribute__((aligned(1)));
extern uint8_t gm_cg583o7kdgj[10] __attribute__((aligned(1)));
extern uint8_t gm_cgoiuafipe4[18] __attribute__((aligned(1)));
extern uint8_t gm_cgsmfcq937e[16] __attribute__((aligned(8)));
extern uint8_t gm_cgsmfcq937e_6[16] __attribute__((aligned(8)));
extern uint8_t gm_ch5e65ekkon[10] __attribute__((aligned(1)));
extern uint8_t gm_chc2gqtscde[4] __attribute__((aligned(1)));
extern uint8_t gm_chhcticfeqe[11] __attribute__((aligned(1)));
extern uint8_t gm_ci7nr7ssi4g[5] __attribute__((aligned(1)));
extern uint8_t gm_cigg5fkjomv[7] __attribute__((aligned(1)));
extern uint8_t gm_cka5emo0tle[16] __attribute__((aligned(1)));
extern uint8_t gm_ckcutgns62h[10] __attribute__((aligned(1)));
extern uint8_t gm_cl2ue8a5800[7] __attribute__((aligned(1)));
extern uint8_t gm_clvd1m34v3b[11] __attribute__((aligned(1)));
extern uint8_t gm_cmml7o4if0q[8] __attribute__((aligned(8)));
extern uint8_t gm_cndfnv7mbeh[8] __attribute__((aligned(8)));
extern uint8_t gm_cni4niqbaug[15] __attribute__((aligned(1)));
extern uint8_t gm_cnr6mqd3ogi[57] __attribute__((aligned(1)));
extern uint8_t gm_co3qlejdntu[16] __attribute__((aligned(8)));
extern uint8_t gm_co88ci116uf[39] __attribute__((aligned(1)));
extern uint8_t gm_cobksqr67t4[8] __attribute__((aligned(8)));
extern uint8_t gm_cooifs2q83a[11] __attribute__((aligned(1)));
extern uint8_t gm_cq2ccurlbf8[14] __attribute__((aligned(1)));
extern uint8_t gm_cq8smm747r5[2] __attribute__((aligned(1)));
extern uint8_t gm_cr8vi7458l3[5] __attribute__((aligned(1)));
extern uint8_t gm_crku57l46si[13] __attribute__((aligned(1)));
extern uint8_t gm_cs1qbc2gaqs[16] __attribute__((aligned(8)));
extern uint8_t gm_csik9eh7ggu[70] __attribute__((aligned(1)));
extern uint8_t gm_csirqbmq8rr[10] __attribute__((aligned(1)));
extern uint8_t gm_ct4bgh8l6qv[16] __attribute__((aligned(1)));
extern uint8_t gm_ctds4kc1b5o[1] __attribute__((aligned(1)));
extern uint8_t gm_ctf8kltriqb[2] __attribute__((aligned(1)));
extern uint8_t gm_ctlupmqng6n[8] __attribute__((aligned(1)));
extern uint8_t gm_ctvsbqjhdl3[12] __attribute__((aligned(4)));
extern uint8_t gm_cu1bqtgc9l0[13] __attribute__((aligned(1)));
extern uint8_t gm_cuqp9vdk0eh[9] __attribute__((aligned(1)));
extern uint8_t gm_cv7ipurvl4r[14] __attribute__((aligned(1)));
extern void *const gm_hl0tthikq3q4[];
extern void *const gm_hl83gfnr17rd[];
extern void *const gm_hl87701o8eo1[];
extern void *const gm_hleu8p83p90p[];
extern void *const gm_hlgmnqv27ug4[];
extern void *const gm_hloaeaafij99[];
extern uint8_t gm_k00ckei9k4e[40] __attribute__((aligned(8)));
extern uint8_t gm_k0480fp26lr[40] __attribute__((aligned(8)));
extern uint8_t gm_k093ni2ahjb[40] __attribute__((aligned(8)));
extern uint8_t gm_k2boi01efds[40] __attribute__((aligned(8)));
extern uint8_t gm_k3501um71nb[40] __attribute__((aligned(8)));
extern uint8_t gm_k4g6hon27vb[40] __attribute__((aligned(8)));
extern uint8_t gm_k6dqjucsagv[40] __attribute__((aligned(8)));
extern uint8_t gm_k6qm3ruhsta[40] __attribute__((aligned(8)));
extern uint8_t gm_ka55ftc91cb[40] __attribute__((aligned(8)));
extern uint8_t gm_kh9bgce5chr[40] __attribute__((aligned(8)));
extern uint8_t gm_kihi8omjj3o[40] __attribute__((aligned(8)));
extern uint8_t gm_kjq4sbqctak[40] __attribute__((aligned(8)));
extern uint8_t gm_klqhd3q4fc0[40] __attribute__((aligned(8)));
extern uint8_t gm_km7ke510mna[40] __attribute__((aligned(8)));
extern uint8_t gm_knhubf3blct[40] __attribute__((aligned(8)));
extern uint8_t gm_knm7lgi0bll[40] __attribute__((aligned(8)));
extern uint8_t gm_kold22d1smp[40] __attribute__((aligned(8)));
extern uint8_t gm_kpi7tiio4tn[40] __attribute__((aligned(8)));
extern uint8_t gm_kqiqqsf7tg0[40] __attribute__((aligned(8)));
extern uint8_t gm_krbc9rueul4[40] __attribute__((aligned(8)));
extern uint8_t gm_kst3i6v7714[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0021gvebcdc4[9];
extern const uint8_t gm_s00989rdgst9u[20];
extern const uint8_t gm_s00gjhnalb1qv[36];
extern const uint8_t gm_s01mi6tf86obv[13];
extern const uint8_t gm_s024ihiaifr93[15];
extern const uint8_t gm_s02uuedsru1qu[9];
extern const uint8_t gm_s03lpulaucbu1[41];
extern const uint8_t gm_s03pmf867dcsr[2];
extern const uint8_t gm_s055t7hpo88bt[4];
extern const uint8_t gm_s05qp4ie7rju8[84];
extern const uint8_t gm_s06sbklusedgl[5];
extern const uint8_t gm_s088gqlg957uo[6];
extern const uint8_t gm_s08ekjvgo9fro[101];
extern const uint8_t gm_s09jc0n36v9gm[97];
extern const uint8_t gm_s09tkid4cqjmm[62];
extern const uint8_t gm_s0b795193rvs2[7];
extern const uint8_t gm_s0etnbnk4miq5[5];
extern const uint8_t gm_s0gu0jo9fq1ca[11];
extern const uint8_t gm_s0jm0vg43i745[11];
extern const uint8_t gm_s0km823b6osb3[13];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0njd7khs134c[31];
extern const uint8_t gm_s0nkgagpnq9jc[58];
extern const uint8_t gm_s0u0p8jkqpsv1[8];
extern const uint8_t gm_s0u47ivcj4oar[8];
extern const uint8_t gm_s0ucactj6hj0a[4];
extern const uint8_t gm_s0uf57646drba[14];
extern const uint8_t gm_s167ok8osvuve[11];
extern const uint8_t gm_s16mf4cvajh59[13];
extern const uint8_t gm_s17l5kif0frp2[19];
extern const uint8_t gm_s18e8dmribdb3[7];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1bpuqd44lur7[7];
extern const uint8_t gm_s1cqejvllajb6[3];
extern const uint8_t gm_s1dbslfck7laf[3];
extern const uint8_t gm_s1fjspgm800ug[7];
extern const uint8_t gm_s1gd0gkbe5tu9[55];
extern const uint8_t gm_s1hud20mvj11b[37];
extern const uint8_t gm_s1i7slukookmu[12];
extern const uint8_t gm_s1ie3s345dm40[27];
extern const uint8_t gm_s1jbdr25pcb3i[14];
extern const uint8_t gm_s1k3lfare8sei[4];
extern const uint8_t gm_s1llcn0p4qesk[20];
extern const uint8_t gm_s1n6uk6814mmb[53];
extern const uint8_t gm_s1nt8u0pgo6vn[17];
extern const uint8_t gm_s1pandl18hjn0[61];
extern const uint8_t gm_s1sspofc2u3bb[37];
extern const uint8_t gm_s1suv7j29n0ea[21];
extern const uint8_t gm_s1ti14uv3rrcf[65];
extern const uint8_t gm_s1tqe3mtj6vb1[8];
extern const uint8_t gm_s1u0m6lm5u1su[7];
extern const uint8_t gm_s1vtc1o6iel66[2];
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s209nrdd09rdp[4];
extern const uint8_t gm_s23qsffuqlc1v[25];
extern const uint8_t gm_s25euhn4i5qkh[72];
extern const uint8_t gm_s25jkl4c6n8uo[4];
extern const uint8_t gm_s26o5frgr2hds[7];
extern const uint8_t gm_s276s8986o4r3[23];
extern const uint8_t gm_s27ouot20tc5m[25];
extern const uint8_t gm_s2bp59r6c3pt6[23];
extern const uint8_t gm_s2d2ar24n4if2[6];
extern const uint8_t gm_s2dcdt3n36d4p[8];
extern const uint8_t gm_s2fu5nkek0lrq[7];
extern const uint8_t gm_s2k1ajrnodb02[23];
extern const uint8_t gm_s2ochj4lgmhue[41];
extern const uint8_t gm_s2okn0skhqnvu[32];
extern const uint8_t gm_s2p0dacnmvuhi[7];
extern const uint8_t gm_s2red66k9r4fi[10];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s31o876d6qij5[10];
extern const uint8_t gm_s3231a310mran[10];
extern const uint8_t gm_s3876s0nhcdfo[99];
extern const uint8_t gm_s38njmu4cr525[13];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3bgi1rlb5ne1[8];
extern const uint8_t gm_s3emon0a31046[81];
extern const uint8_t gm_s3hlvhp72jeiv[12];
extern const uint8_t gm_s3jl3afv8nlvv[4];
extern const uint8_t gm_s3loluab89lqt[11];
extern const uint8_t gm_s3mmpurhmitlr[46];
extern const uint8_t gm_s3n5bjpleiarb[7];
extern const uint8_t gm_s3ndmfc1bk22f[23];
extern const uint8_t gm_s3ojik1morrbi[9];
extern const uint8_t gm_s3opc5ivp6e66[4];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3qo1c5fkeqfk[67];
extern const uint8_t gm_s3vn3760p6488[103];
extern const uint8_t gm_s40upcq09aufa[39];
extern const uint8_t gm_s43vlgrackeg9[17];
extern const uint8_t gm_s46dkmn8ur11l[60];
extern const uint8_t gm_s4a6o0t8isvlm[17];
extern const uint8_t gm_s4a980fahtblo[3];
extern const uint8_t gm_s4bf0cndbl9lv[3];
extern const uint8_t gm_s4bo4cvutg59h[16];
extern const uint8_t gm_s4eilu97sooh1[10];
extern const uint8_t gm_s4gdu7v0gur88[11];
extern const uint8_t gm_s4j8rhn4svm30[7];
extern const uint8_t gm_s4kqn01dbredu[28];
extern const uint8_t gm_s4kuq0o3l6f7d[11];
extern const uint8_t gm_s4m15r14tbnat[61];
extern const uint8_t gm_s4mij084uljjf[26];
extern const uint8_t gm_s4nl1jq95dg65[111];
extern const uint8_t gm_s4oloc6hggc50[92];
extern const uint8_t gm_s4pk73sg6h8qk[6];
extern const uint8_t gm_s4pv32nkh7r2r[11];
extern const uint8_t gm_s4s2nr1qc94po[17];
extern const uint8_t gm_s4t8kk8uuclvr[27];
extern const uint8_t gm_s4u5fhgj0jfnp[41];
extern const uint8_t gm_s50pmptf3idds[2];
extern const uint8_t gm_s51l7o5mjnhu8[9];
extern const uint8_t gm_s52fbscfsv3pa[6];
extern const uint8_t gm_s52ieemclane5[41];
extern const uint8_t gm_s54ap4j24utib[69];
extern const uint8_t gm_s554r118mfcao[34];
extern const uint8_t gm_s55kqbt4f7gau[4];
extern const uint8_t gm_s55ncecttah5f[69];
extern const uint8_t gm_s57l2tgcfr4se[5];
extern const uint8_t gm_s5a3gd19ers1k[19];
extern const uint8_t gm_s5c0ruf0vg8mu[3];
extern const uint8_t gm_s5e527dkpbusp[5];
extern const uint8_t gm_s5q2tpl7krt8a[9];
extern const uint8_t gm_s5r4hcdoqsklk[53];
extern const uint8_t gm_s5s0ahr8bl55p[12];
extern const uint8_t gm_s5s4ac22q52tj[14];
extern const uint8_t gm_s5s65385he1ib[24];
extern const uint8_t gm_s5uhm93lpt8jn[11];
extern const uint8_t gm_s61fo677jojio[6];
extern const uint8_t gm_s626miklr3ggr[34];
extern const uint8_t gm_s64eu4ogr34e2[62];
extern const uint8_t gm_s65abge0a786m[10];
extern const uint8_t gm_s66k76ki3e7an[22];
extern const uint8_t gm_s6899ib4u9fpt[24];
extern const uint8_t gm_s68c5jkfvg5dd[86];
extern const uint8_t gm_s69d1mv9hu0dg[42];
extern const uint8_t gm_s6a2c11qu9scr[13];
extern const uint8_t gm_s6bs3s06kr0hi[10];
extern const uint8_t gm_s6dmfj45vc0q0[11];
extern const uint8_t gm_s6g8mt9e5cjjr[11];
extern const uint8_t gm_s6hha9j1e16ru[90];
extern const uint8_t gm_s6hiq18oa7ku0[32];
extern const uint8_t gm_s6mmuahmmssv6[17];
extern const uint8_t gm_s6n2mkqhnsjpk[10];
extern const uint8_t gm_s6n3pd0tmqbqc[10];
extern const uint8_t gm_s6s4ngfaj87vv[5];
extern const uint8_t gm_s6t4r831mukdr[3];
extern const uint8_t gm_s718drvtpg9kd[49];
extern const uint8_t gm_s73cunn109dv3[36];
extern const uint8_t gm_s73uhqfbphcpe[7];
extern const uint8_t gm_s73vt0nuu7890[28];
extern const uint8_t gm_s77aa5te2lvj3[6];
extern const uint8_t gm_s78l3ok4on4sg[36];
extern const uint8_t gm_s7aoh8j3iph16[51];
extern const uint8_t gm_s7c8ep4trp7bk[7];
extern const uint8_t gm_s7c9tkfjfc169[26];
extern const uint8_t gm_s7d1996rqhrdg[9];
extern const uint8_t gm_s7d4tu5fkkg12[11];
extern const uint8_t gm_s7dkbj8tvt9e3[7];
extern const uint8_t gm_s7hkl5mqa15gm[5];
extern const uint8_t gm_s7jf1bc28tar9[38];
extern const uint8_t gm_s7jhnrnbmkbbq[39];
extern const uint8_t gm_s7lb197di7d72[37];
extern const uint8_t gm_s7ni4ei2275l9[13];
extern const uint8_t gm_s7p349cm5h5jf[20];
extern const uint8_t gm_s7sn5glap1gkf[7];
extern const uint8_t gm_s7tf2msne28g3[19];
extern const uint8_t gm_s7un5bnoqg548[4];
extern const uint8_t gm_s7veagc48806u[41];
extern const uint8_t gm_s7vt7hgga3uq7[7];
extern const uint8_t gm_s821smstdk4rs[14];
extern const uint8_t gm_s82d2namrcvs3[17];
extern const uint8_t gm_s82i64ms8kog2[94];
extern const uint8_t gm_s84bfepk867cd[18];
extern const uint8_t gm_s84k1uo35omt8[10];
extern const uint8_t gm_s84lh6to0er2m[19];
extern const uint8_t gm_s867gr7sqck87[26];
extern const uint8_t gm_s86js8lt7ibda[48];
extern const uint8_t gm_s8dujv4dnroq0[24];
extern const uint8_t gm_s8eg138piktj1[12];
extern const uint8_t gm_s8j8keh1qgf0a[7];
extern const uint8_t gm_s8lp1qs73nh4k[17];
extern const uint8_t gm_s8oq9c4aac3sc[52];
extern const uint8_t gm_s8pst1ovjhtbk[71];
extern const uint8_t gm_s8tkvhpqkq5en[8];
extern const uint8_t gm_s9236sqnmc8uc[9];
extern const uint8_t gm_s93p2i934ld6p[5];
extern const uint8_t gm_s959ni36b80a4[4];
extern const uint8_t gm_s966nqk7bt67q[19];
extern const uint8_t gm_s9a8dkouqsfgp[9];
extern const uint8_t gm_s9c60h0qendav[9];
extern const uint8_t gm_s9da4sg04ql4e[60];
extern const uint8_t gm_s9ekhmse13161[5];
extern const uint8_t gm_s9fugl5o6l2co[8];
extern const uint8_t gm_s9idfla83gb22[59];
extern const uint8_t gm_s9jm1k2lfgtr0[9];
extern const uint8_t gm_s9nl6jkck8c25[8];
extern const uint8_t gm_s9npjns7bnqaj[19];
extern const uint8_t gm_s9o8gb20t6jtp[9];
extern const uint8_t gm_s9pdhp63uj9bd[7];
extern const uint8_t gm_s9pl5vgrnfja0[41];
extern const uint8_t gm_s9q38m4ka466q[32];
extern const uint8_t gm_s9r83c0uo3ifr[8];
extern const uint8_t gm_sa4nk3mrdf2d0[36];
extern const uint8_t gm_saalafjdd3tnp[9];
extern const uint8_t gm_sacua9qc75rts[7];
extern const uint8_t gm_sader8j76ksps[27];
extern const uint8_t gm_sadot8stl6s4j[36];
extern const uint8_t gm_saefgd8cv1aea[20];
extern const uint8_t gm_safiul2d9a6fl[14];
extern const uint8_t gm_sagc0kltjb8fc[12];
extern const uint8_t gm_sagfhrncf3ga2[11];
extern const uint8_t gm_sahahh1a40c5c[37];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_salbbur153hif[21];
extern const uint8_t gm_sam3pd3469drv[55];
extern const uint8_t gm_samlp70hmirme[3];
extern const uint8_t gm_san3q9ul7snd0[22];
extern const uint8_t gm_sangi489oaklo[41];
extern const uint8_t gm_sao4mumvadoct[4];
extern const uint8_t gm_sap058neht0gj[30];
extern const uint8_t gm_sapbk38ibps8q[28];
extern const uint8_t gm_saq0ier99o235[35];
extern const uint8_t gm_saqo50iv9jjmk[9];
extern const uint8_t gm_sart7dgrh11an[14];
extern const uint8_t gm_satil70qt25hp[52];
extern const uint8_t gm_sauf6ibu7mnba[5];
extern const uint8_t gm_savb5oasie77f[37];
extern const uint8_t gm_savi5cfvaqp3a[15];
extern const uint8_t gm_savnnonh6sa0m[14];
extern const uint8_t gm_sb0j8pu1hhl5q[52];
extern const uint8_t gm_sb2rimopi40u7[39];
extern const uint8_t gm_sb3efvtufksie[37];
extern const uint8_t gm_sb4hpcnufos1f[12];
extern const uint8_t gm_sb5efqp7fp176[49];
extern const uint8_t gm_sb5pfucassrk8[34];
extern const uint8_t gm_sb77prkml0t3e[15];
extern const uint8_t gm_sb7jq1p8shp96[113];
extern const uint8_t gm_sb895t6tk3m9h[6];
extern const uint8_t gm_sba439nre06r4[6];
extern const uint8_t gm_sba70pvo3j68t[16];
extern const uint8_t gm_sbbavs945cjhp[32];
extern const uint8_t gm_sbckpj1jsdt28[13];
extern const uint8_t gm_sbdqlrmtee4eo[66];
extern const uint8_t gm_sbe61ep7tock5[5];
extern const uint8_t gm_sbek54n4a6h2o[7];
extern const uint8_t gm_sbfr1qrcvp774[59];
extern const uint8_t gm_sbfupubonn4fv[41];
extern const uint8_t gm_sbg96utvan892[34];
extern const uint8_t gm_sbge807fo2bb9[19];
extern const uint8_t gm_sbgthe7nu79mi[18];
extern const uint8_t gm_sbh3nm117l7ra[35];
extern const uint8_t gm_sbm73uc3nf0c8[6];
extern const uint8_t gm_sbqhd9c69ol7k[17];
extern const uint8_t gm_sbqm20163frvc[5];
extern const uint8_t gm_sbs808v3vjc2c[8];
extern const uint8_t gm_sbsjrunf4id25[12];
extern const uint8_t gm_sbtoph8phkseu[5];
extern const uint8_t gm_sbtq9nj91tefv[10];
extern const uint8_t gm_sc2qutubarto1[24];
extern const uint8_t gm_sc2u7rsavnn5v[47];
extern const uint8_t gm_sc3s0n32fb0ju[11];
extern const uint8_t gm_sc48cegnkinfm[12];
extern const uint8_t gm_sc5r1r2po4512[6];
extern const uint8_t gm_sc6celu0e642t[6];
extern const uint8_t gm_sc6qhprasnd4f[25];
extern const uint8_t gm_sc8v94psr8v3s[21];
extern const uint8_t gm_sc9huk8sdsgbe[10];
extern const uint8_t gm_scaoagismk27j[18];
extern const uint8_t gm_scbkk1tper8o2[54];
extern const uint8_t gm_scblcr348l6he[31];
extern const uint8_t gm_schn9bp2k5kqo[47];
extern const uint8_t gm_sci406m9tkvjp[32];
extern const uint8_t gm_scjfd9b9vg99c[12];
extern const uint8_t gm_sclthecvcggbv[15];
extern const uint8_t gm_scm3afv9qr78v[5];
extern const uint8_t gm_scmp8kk3f4q6t[46];
extern const uint8_t gm_scn17gb24fbic[16];
extern const uint8_t gm_scnmhrk4ghd85[17];
extern const uint8_t gm_scon24vlgg660[15];
extern const uint8_t gm_scqusp7lgvp02[4];
extern const uint8_t gm_scrt7dkgq6215[10];
extern const uint8_t gm_scs1evojsik8q[4];
extern const uint8_t gm_scujoh2vgr9aq[22];
extern const uint8_t gm_scvhv26j3m3dm[18];
extern const uint8_t gm_scvuqmspcpb1m[6];
extern const uint8_t gm_sd2g8d0vbj98u[7];
extern const uint8_t gm_sd3f2q088bhvc[41];
extern const uint8_t gm_sd3nblniu5ogu[25];
extern const uint8_t gm_sd5qvckqda7oh[13];
extern const uint8_t gm_sd7hg1lsdu11b[15];
extern const uint8_t gm_sd912tgjus40m[28];
extern const uint8_t gm_sdainihnab59h[10];
extern const uint8_t gm_sdbih7ptecvdq[39];
extern const uint8_t gm_sdc2n4tsqs8nn[4];
extern const uint8_t gm_sddo97gu3reaa[72];
extern const uint8_t gm_sdf319kmi28bu[17];
extern const uint8_t gm_sdfa87va44k3m[24];
extern const uint8_t gm_sdikuksp636di[12];
extern const uint8_t gm_sdl557c0gjrbv[10];
extern const uint8_t gm_sdmle2qsm85vh[11];
extern const uint8_t gm_sdopt7plujc7n[16];
extern const uint8_t gm_sdru6cvprml58[29];
extern const uint8_t gm_sdtjos7av9cn7[22];
extern const uint8_t gm_sdtv0pa4ahtps[52];
extern const uint8_t gm_sdugj3qbhl84q[5];
extern const uint8_t gm_sdvrfjnpjiquk[20];
extern const uint8_t gm_se08n5d1gngu7[5];
extern const uint8_t gm_se3l2nlomk2pj[9];
extern const uint8_t gm_se5bopd2dbamm[30];
extern const uint8_t gm_se5r6p8tqdo82[31];
extern const uint8_t gm_se71q7absh67v[12];
extern const uint8_t gm_se9jjcum4l3k9[32];
extern const uint8_t gm_seakt349s3hl5[8];
extern const uint8_t gm_sebnh3ba276b8[17];
extern const uint8_t gm_sebojpbfjfgaf[3];
extern const uint8_t gm_secrjke11f3ph[13];
extern const uint8_t gm_seg12e843a462[9];
extern const uint8_t gm_segii2f2mu6lb[23];
extern const uint8_t gm_segmk88bjc49r[7];
extern const uint8_t gm_seh85cpcgte1u[57];
extern const uint8_t gm_sehat3g6ic3fn[50];
extern const uint8_t gm_sejqabkgvk3eh[12];
extern const uint8_t gm_sel2d9vtaa0ba[19];
extern const uint8_t gm_semhbv8seph85[21];
extern const uint8_t gm_senhrlorjabcl[3];
extern const uint8_t gm_seodvmll3gsjn[4];
extern const uint8_t gm_sep6flfnofnin[41];
extern const uint8_t gm_serklfkp9210u[154];
extern const uint8_t gm_sesj9tqqcsas0[6];
extern const uint8_t gm_sespjge6ouk5e[41];
extern const uint8_t gm_seuimcklr1us2[16];
extern const uint8_t gm_sevktc6u3mas1[52];
extern const uint8_t gm_sf372rql24e3n[17];
extern const uint8_t gm_sf5n57voafb5e[13];
extern const uint8_t gm_sf6chrlv83m77[22];
extern const uint8_t gm_sf90mcj0opih1[8];
extern const uint8_t gm_sfcjpmkuggqo3[10];
extern const uint8_t gm_sfcm4qg50vk74[10];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfg1v6bqcbbav[10];
extern const uint8_t gm_sfid9en449upv[11];
extern const uint8_t gm_sfivm1csbt9ar[29];
extern const uint8_t gm_sflmt70utbvg4[20];
extern const uint8_t gm_sfm035nfvvou3[29];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sfn640udk6pjn[19];
extern const uint8_t gm_sfplrdppnb8cn[23];
extern const uint8_t gm_sfq37b3iv0k5h[27];
extern const uint8_t gm_sfsqcni8p8345[42];
extern const uint8_t gm_sftp5tlacbd12[8];
extern const uint8_t gm_sfvci5f269lfp[13];
extern const uint8_t gm_sg0bllfnuem4g[7];
extern const uint8_t gm_sg0qvpnm8k9d3[13];
extern const uint8_t gm_sg0tsttb54h9c[85];
extern const uint8_t gm_sg1t2674vj2eg[32];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg5o5gf2u71g7[9];
extern const uint8_t gm_sg6k7269e3cfu[34];
extern const uint8_t gm_sg7h8lcattjoq[13];
extern const uint8_t gm_sgahfuanpfk2r[60];
extern const uint8_t gm_sgcf9cdlaf08n[6];
extern const uint8_t gm_sgchj7ubg6umm[20];
extern const uint8_t gm_sgdafdv2n5c25[5];
extern const uint8_t gm_sgdqi8a9n3f12[15];
extern const uint8_t gm_sges6pnr4b3t3[19];
extern const uint8_t gm_sgfqei77bsia8[4];
extern const uint8_t gm_sgouu8ejfsufr[10];
extern const uint8_t gm_sgqejiat83g1e[47];
extern const uint8_t gm_sgsvgcmirhojs[28];
extern const uint8_t gm_sgujtgfflv5dk[8];
extern const uint8_t gm_sgur19lpnttci[7];
extern const uint8_t gm_sh2urbm3ndjnn[31];
extern const uint8_t gm_sh540objcvln0[25];
extern const uint8_t gm_sh6hkhojsmti9[48];
extern const uint8_t gm_sh89ku1sauvot[11];
extern const uint8_t gm_sh929ihdsnkrv[27];
extern const uint8_t gm_sh9g09icusqpu[14];
extern const uint8_t gm_sha303gigke52[10];
extern const uint8_t gm_shbtdicfd8lcc[12];
extern const uint8_t gm_shcdet5ggdj93[99];
extern const uint8_t gm_shecus79pkigk[19];
extern const uint8_t gm_shegcik3mpeut[12];
extern const uint8_t gm_shjad4rl88uoh[11];
extern const uint8_t gm_shoqeplqudrjn[17];
extern const uint8_t gm_shs03k71jk157[19];
extern const uint8_t gm_shs0aou723lfc[25];
extern const uint8_t gm_sht0efblvn5a6[8];
extern const uint8_t gm_shud0j6jb8ucc[37];
extern const uint8_t gm_si1kalsj77bue[41];
extern const uint8_t gm_si2ks4lbjbojr[22];
extern const uint8_t gm_si4l87dl4tpkq[8];
extern const uint8_t gm_si5tqjfc8g60q[30];
extern const uint8_t gm_si7spjb0bqq6r[9];
extern const uint8_t gm_si9h59pjqftat[5];
extern const uint8_t gm_siampvcugondr[14];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sictcqjuumcng[63];
extern const uint8_t gm_sidgpvfsv94q1[56];
extern const uint8_t gm_sidlbibo929o4[12];
extern const uint8_t gm_sifvl6bvepiob[9];
extern const uint8_t gm_sihclrdudbhkh[9];
extern const uint8_t gm_sijeantqk7jl2[11];
extern const uint8_t gm_siji82hgrl5rk[12];
extern const uint8_t gm_sik0eji8h20le[41];
extern const uint8_t gm_sim8pu6stc37a[11];
extern const uint8_t gm_simovkrp5fi0r[16];
extern const uint8_t gm_sinogveu00g7m[36];
extern const uint8_t gm_sioam4mlrjuar[52];
extern const uint8_t gm_siq422vplpm2h[54];
extern const uint8_t gm_sisvmc7d4huip[14];
extern const uint8_t gm_sitmblfjqgruh[9];
extern const uint8_t gm_siv9ut075cfnh[20];
extern const uint8_t gm_sj294dvmirbmr[47];
extern const uint8_t gm_sj4nhb5mobdan[37];
extern const uint8_t gm_sj5iuta9ebe8p[81];
extern const uint8_t gm_sj9co2vp4uudk[13];
extern const uint8_t gm_sja1nsq0b10to[16];
extern const uint8_t gm_sjbjq95a18dio[44];
extern const uint8_t gm_sjblck9bpb6r8[28];
extern const uint8_t gm_sjcm18p35o9rq[4];
extern const uint8_t gm_sjeopa747clpb[4];
extern const uint8_t gm_sjfdnh735o7f8[5];
extern const uint8_t gm_sjgqjs06244vq[21];
extern const uint8_t gm_sjid6lq0hib0h[3];
extern const uint8_t gm_sjqjrfqdmp9ao[7];
extern const uint8_t gm_sjsb0lbgdebd6[45];
extern const uint8_t gm_sjsosmp24eqn0[36];
extern const uint8_t gm_sjuj50mlj6mg0[14];
extern const uint8_t gm_sjv2au7jm0m3j[66];
extern const uint8_t gm_sk2oomiuvp44g[6];
extern const uint8_t gm_sk30fte1u6en9[33];
extern const uint8_t gm_sk4i403fuak6l[19];
extern const uint8_t gm_sk5s72mtujop8[5];
extern const uint8_t gm_skaimjkudbui9[39];
extern const uint8_t gm_skem4050p6ec1[13];
extern const uint8_t gm_skes2iuk38v0s[12];
extern const uint8_t gm_skkiv5ia4t4au[39];
extern const uint8_t gm_skl8iadfmm1va[44];
extern const uint8_t gm_sklnpt5kca0im[15];
extern const uint8_t gm_skmas107kf6co[41];
extern const uint8_t gm_sknsei1oiabnd[13];
extern const uint8_t gm_skpk7m7mpsr06[6];
extern const uint8_t gm_skqas99ada0fa[26];
extern const uint8_t gm_skrht660q9e9a[6];
extern const uint8_t gm_sksprenpp79ih[11];
extern const uint8_t gm_sl1a3p7pocs82[15];
extern const uint8_t gm_sl1iofp814r0q[3];
extern const uint8_t gm_sl23n0qkt3j4u[28];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl58ekc3js5bs[30];
extern const uint8_t gm_sl7cl2p91o7b5[55];
extern const uint8_t gm_sl95r5djfj1r0[13];
extern const uint8_t gm_slbfl8l7n523s[7];
extern const uint8_t gm_sldsondfm6ju2[9];
extern const uint8_t gm_slf586p66t9db[6];
extern const uint8_t gm_slg8371oger0u[32];
extern const uint8_t gm_slginip33cenb[14];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slhllpfip1vpg[3];
extern const uint8_t gm_slhsp4invn4vh[7];
extern const uint8_t gm_slj3uno8bsqk4[27];
extern const uint8_t gm_slk4m2cppcp75[23];
extern const uint8_t gm_sllgvbfukep8p[24];
extern const uint8_t gm_sln1aneof3dn0[41];
extern const uint8_t gm_slnhe1h36jmub[9];
extern const uint8_t gm_slnuijmj5p1vk[19];
extern const uint8_t gm_slov3heeajhqn[12];
extern const uint8_t gm_slqtudd4gk04i[9];
extern const uint8_t gm_sltao39ib9870[6];
extern const uint8_t gm_sltjg0sbhbo73[13];
extern const uint8_t gm_sluiboi2c0d5n[58];
extern const uint8_t gm_slurd8qe898d5[10];
extern const uint8_t gm_sm017vofcj044[12];
extern const uint8_t gm_sm1m5ho3m4iqp[12];
extern const uint8_t gm_sm26kv3hefp6i[13];
extern const uint8_t gm_sm2tcicavq8d5[8];
extern const uint8_t gm_sm35km518bh68[52];
extern const uint8_t gm_sm54129hqmnsd[16];
extern const uint8_t gm_sm55k23067j17[16];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_sm6j3q0d4n2jp[3];
extern const uint8_t gm_sm77ovo2kgv5k[42];
extern const uint8_t gm_sm7s66sg1itc1[4];
extern const uint8_t gm_sm8m7jldttgpj[38];
extern const uint8_t gm_sm8si2u0a1u17[5];
extern const uint8_t gm_sm9lq3mvrfi15[7];
extern const uint8_t gm_smbjf525bk9a0[20];
extern const uint8_t gm_smdet9tftopqs[14];
extern const uint8_t gm_smedhknhdrf46[11];
extern const uint8_t gm_smjeoupcicact[10];
extern const uint8_t gm_smk08v391jpq1[23];
extern const uint8_t gm_smk9p6gbbjn5j[9];
extern const uint8_t gm_smnfu5n5pum1l[2];
extern const uint8_t gm_smnot3vdfhqsc[6];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smpg2ulm74cim[9];
extern const uint8_t gm_smqkqp8ljbr20[41];
extern const uint8_t gm_smr5gecavco41[9];
extern const uint8_t gm_smuveuet82l27[11];
extern const uint8_t gm_sn1go4g2tvcok[41];
extern const uint8_t gm_sn4o5fodoh7op[12];
extern const uint8_t gm_sn581p3lbgrjg[9];
extern const uint8_t gm_sn5qnjkjnmu95[23];
extern const uint8_t gm_sna94ju8c8em3[48];
extern const uint8_t gm_snd69jrnj9kdn[42];
extern const uint8_t gm_snekrjln06tfl[14];
extern const uint8_t gm_snh0sl2cdagdn[13];
extern const uint8_t gm_snhmu20ccir10[16];
extern const uint8_t gm_sni0seheggmp0[30];
extern const uint8_t gm_snjdrq8n0vphi[12];
extern const uint8_t gm_snkldl7hr4htt[4];
extern const uint8_t gm_snl36eh5psull[7];
extern const uint8_t gm_snomh93h7tumv[14];
extern const uint8_t gm_snpgki1p28d77[5];
extern const uint8_t gm_snr5o5tak6405[4];
extern const uint8_t gm_sns1hl83hdlcn[5];
extern const uint8_t gm_snset5tb694as[15];
extern const uint8_t gm_sntd8vrhhlrk6[15];
extern const uint8_t gm_sntolo76g17vv[7];
extern const uint8_t gm_snu0769072h68[9];
extern const uint8_t gm_snukl6jqep6jf[4];
extern const uint8_t gm_snvncjka7ph6m[7];
extern const uint8_t gm_so25i3en68imc[27];
extern const uint8_t gm_so2qoj4e0o0al[6];
extern const uint8_t gm_so4acijgbtr2b[34];
extern const uint8_t gm_so4gnucc3fl4v[7];
extern const uint8_t gm_so4pc0b7vpiib[106];
extern const uint8_t gm_so4t3hmedbui5[7];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so87vf8a6o4mm[11];
extern const uint8_t gm_so8qnkabacf8o[5];
extern const uint8_t gm_soakgb2h3g40i[4];
extern const uint8_t gm_soashnodb7lsu[23];
extern const uint8_t gm_socbkiss431o8[18];
extern const uint8_t gm_sodsqu1iesiv9[3];
extern const uint8_t gm_sog6ie61bmisu[7];
extern const uint8_t gm_soiad5r9c65ru[37];
extern const uint8_t gm_soiet16j0e8ar[34];
extern const uint8_t gm_sojc67e7a19h1[6];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_somdu21umm1af[16];
extern const uint8_t gm_somon4mdq0gpj[24];
extern const uint8_t gm_sor30tetra4nt[5];
extern const uint8_t gm_sosa5l0k6d34c[15];
extern const uint8_t gm_soscrlodichin[67];
extern const uint8_t gm_sovhd8e8hvflr[52];
extern const uint8_t gm_sp02qqgucmpt7[3];
extern const uint8_t gm_sp1g56u3ibrtp[23];
extern const uint8_t gm_sp4518j1p6r9m[19];
extern const uint8_t gm_sp4m2sm1j5k9e[23];
extern const uint8_t gm_spc0s79b28n7m[9];
extern const uint8_t gm_spc8ncjo0q4pi[11];
extern const uint8_t gm_spg9vuapom9qd[11];
extern const uint8_t gm_sphmte453j9c2[12];
extern const uint8_t gm_spicth2mans91[44];
extern const uint8_t gm_spj65mdvs9ei4[20];
extern const uint8_t gm_spj9rva8kb5ar[3];
extern const uint8_t gm_spj9t9b8lqlpp[8];
extern const uint8_t gm_spkrf9e7cr6h4[7];
extern const uint8_t gm_splvrmi68lqi3[111];
extern const uint8_t gm_spm33surstj1l[8];
extern const uint8_t gm_spm9ae4g2b2fd[14];
extern const uint8_t gm_spn9416h484so[23];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_spsaka1h4bsob[12];
extern const uint8_t gm_spsh7mv5lihcc[34];
extern const uint8_t gm_spsm6pt8hfeqv[8];
extern const uint8_t gm_sq0r7ud0kv45f[8];
extern const uint8_t gm_sq2cbiau3hhqs[11];
extern const uint8_t gm_sq2e6tj3vu7de[10];
extern const uint8_t gm_sq44fbs78td4m[33];
extern const uint8_t gm_sq52gnep1cqbp[45];
extern const uint8_t gm_sq5rlbq4gbs5v[27];
extern const uint8_t gm_sq6h1gpkivf38[41];
extern const uint8_t gm_sq7ijigadotf8[17];
extern const uint8_t gm_sq9lne9r5j3u2[8];
extern const uint8_t gm_sqaek8776r3c5[9];
extern const uint8_t gm_sqc7sit9ajc3t[3];
extern const uint8_t gm_sqdfr4k3vea5p[4];
extern const uint8_t gm_sqdqshat9srld[37];
extern const uint8_t gm_sqf5m57l1ua5d[27];
extern const uint8_t gm_sqjfo9aus0n4k[13];
extern const uint8_t gm_sql9r2b8fvivt[10];
extern const uint8_t gm_sqnjthtqs97m4[41];
extern const uint8_t gm_sqo8h5kg9id12[86];
extern const uint8_t gm_sqoq2k5v12g25[41];
extern const uint8_t gm_sqro42saen22n[108];
extern const uint8_t gm_sqrv3igf39742[6];
extern const uint8_t gm_squacltecdsom[5];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const uint8_t gm_sr244s00qj01d[3];
extern const uint8_t gm_sr29ed60si4re[7];
extern const uint8_t gm_sr3mniof9fusf[12];
extern const uint8_t gm_sr44oukgimglr[23];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sra3ncr12fqek[56];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3k278vcmqgdr[];
extern const char gm_src7h8dni243k2v[];
extern const char gm_src7nt72u848oha[];
extern const char gm_src95fgh4jdpgpn[];
extern const char gm_srccvj6up3gbolp[];
extern const char gm_srcfpg6jsb3cfp6[];
extern const char gm_srcge9itd5fie17[];
extern const char gm_srcgr2mvgis129a[];
extern const char gm_srcjjg6ldpifdau[];
extern const char gm_srck9pqpjjfjkmq[];
extern const char gm_srckfibkpt681aa[];
extern const char gm_srckqas99ada0fa[];
extern const char gm_srcmm27gepldtcm[];
extern const char gm_srcrq1gl2e1lsg5[];
extern const char gm_srcuokv9ad9oqqb[];
extern const uint8_t gm_srdnkonbfvqdq[9];
extern const uint8_t gm_srdq954rpedg3[93];
extern const uint8_t gm_srgqm2jeibjrm[16];
extern const uint8_t gm_srnejjd9belbk[18];
extern const uint8_t gm_srni26bloa03c[12];
extern const uint8_t gm_srqm47b4oe5lm[72];
extern const uint8_t gm_srsbjtfrhs8q1[16];
extern const uint8_t gm_srugm21uf2sb0[9];
extern const uint8_t gm_srv4dcfccuset[3];
extern const uint8_t gm_srv8c2gciqkuj[48];
extern const uint8_t gm_ss28cntkmcf91[1780];
extern const uint8_t gm_ss3tu1f38tkcj[6];
extern const uint8_t gm_ss4337v5p4tj3[5];
extern const uint8_t gm_ss4rn3b9npsij[14];
extern const uint8_t gm_ss6esonundp29[56];
extern const uint8_t gm_ss869lnvmj3m8[6];
extern const uint8_t gm_ss9l5l0jp0vfb[14];
extern const uint8_t gm_ssa2crodel6ih[29];
extern const uint8_t gm_ssfs2ebod65fl[38];
extern const uint8_t gm_ssgna3ki6j1do[12];
extern const uint8_t gm_ssj49pp1637ia[7];
extern const uint8_t gm_ssj9o3d4o7kbv[43];
extern const uint8_t gm_ssjmv3618n779[10];
extern const uint8_t gm_ssk4a01soguqj[7];
extern const uint8_t gm_ssk6sk59g0skj[16];
extern const uint8_t gm_sskc9227875lf[9];
extern const uint8_t gm_ssn662sheb2q9[45];
extern const uint8_t gm_ssnnp97m1kei1[8];
extern const uint8_t gm_sso11c17kdal7[35];
extern const uint8_t gm_sspb2rj2e032s[13];
extern const uint8_t gm_ssqbjasrp4m5m[22];
extern const uint8_t gm_ssqciuageol45[23];
extern const uint8_t gm_ssqmgnf1snsdj[8];
extern const uint8_t gm_sstrtuk2b4jdg[10];
extern const uint8_t gm_ssu2vb7lj6d81[21];
extern const uint8_t gm_ssv1lstjje7r5[7];
extern const uint8_t gm_st0lgjd5uosa2[6];
extern const uint8_t gm_st1c10ieohbb4[34];
extern const uint8_t gm_st1k7cra2pjic[71];
extern const uint8_t gm_st28rv7m1lueb[9];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_st3nlhq2i34b5[64];
extern const uint8_t gm_st3o1r587rnip[18];
extern const uint8_t gm_st4c97p4hek1p[27];
extern const uint8_t gm_st52k2850n5r5[5];
extern const uint8_t gm_st547kc8a6rg5[16];
extern const uint8_t gm_st6hj8v0h80gt[6];
extern const uint8_t gm_st6q7g4cbd5nc[10];
extern const uint8_t gm_st7a1scb61101[16];
extern const uint8_t gm_st7fm661apctp[17];
extern const uint8_t gm_st9og0pk896pv[12];
extern const uint8_t gm_stabrbc330nqe[18];
extern const uint8_t gm_stbt78k7uqc0m[24];
extern const uint8_t gm_std91rqsr8j0b[7];
extern const uint8_t gm_ste817enr6egu[95];
extern const uint8_t gm_stfs4nd3l87tt[48];
extern const uint8_t gm_stg3p1oju1o9b[23];
extern const uint8_t gm_stla84k0knhb0[12];
extern const uint8_t gm_stm1tprvbfoip[58];
extern const uint8_t gm_stm46orgt9h8k[317];
extern const uint8_t gm_stn1kfabf6qis[13];
extern const uint8_t gm_stoicv7ns2elf[14];
extern const uint8_t gm_stou1l3qp844f[10];
extern const uint8_t gm_stq6t1a4hvefn[14];
extern const uint8_t gm_stuutd7nfq21h[3];
extern const uint8_t gm_su2ima1nbr01d[52];
extern const uint8_t gm_su3jp69t4gf2q[41];
extern const uint8_t gm_su4aje74nkg3o[51];
extern const uint8_t gm_su88ia0hdam4d[66];
extern const uint8_t gm_sub8b9nf0ms2i[104];
extern const uint8_t gm_suc4jg0r7eog6[59];
extern const uint8_t gm_sucfks6hb8bf7[44];
extern const uint8_t gm_suctgno4l10a5[9];
extern const uint8_t gm_suff456m9tthn[6];
extern const uint8_t gm_sufk3vqnaden1[3];
extern const uint8_t gm_sujtulcrs1du3[41];
extern const uint8_t gm_sukbpodfceoe1[9];
extern const uint8_t gm_sums36kmdot2i[109];
extern const uint8_t gm_sunidvof1bsgr[33];
extern const uint8_t gm_suo1v9f675r0d[5];
extern const uint8_t gm_supi6mhbcvtna[5];
extern const uint8_t gm_surrhqnv1nojd[46];
extern const uint8_t gm_sv37ng458lg2i[50];
extern const uint8_t gm_sv48vll3qortv[40];
extern const uint8_t gm_svc1hcla4tqfb[6];
extern const uint8_t gm_svegu6hi5aibp[40];
extern const uint8_t gm_svfte0jrv51gc[30];
extern const uint8_t gm_svg1jvtfj7fgp[6];
extern const uint8_t gm_svgoe41piv341[7];
extern const uint8_t gm_svhavo13qha5k[4];
extern const uint8_t gm_svi913vrigpr0[107];
extern const uint8_t gm_svj3dp4ur2bph[11];
extern const uint8_t gm_svkkls864r4t4[30];
extern const uint8_t gm_svkmjs6p8bhtg[9];
extern const uint8_t gm_svknltl22rlbs[9];
extern const uint8_t gm_svm3nf8j0t7ot[12];
extern const uint8_t gm_svn9drpksr8pl[34];
extern const uint8_t gm_svnpflkpvd513[5];
extern const uint8_t gm_svpi81lkmjsgl[7];
extern const uint8_t gm_svq6hhfarqhmv[10];
extern const uint8_t gm_svqnr5kmirn0g[45];
extern const uint8_t gm_svrjcmg2c5dio[78];
extern const uint8_t gm_svugg9vs1s4ss[58];
extern const uint8_t gm_svuonpsui0ane[41];
extern const uint8_t gm_svv9dg17rf923[20];
extern const gm_tmpl_piece gm_tpl04hr2npsr626_p[];
extern const uint32_t gm_tpl09kglfpefe8i_i[];
extern const gm_tmpl_piece gm_tpl0euitr9hdia5_p[];
extern const gm_tmpl gm_tpl0h84r2u2heg0;
extern const uint32_t gm_tpl0h84r2u2heg0_i[];
extern const gm_tmpl_piece gm_tpl0h84r2u2heg0_p[];
extern const gm_spec *const gm_tpl0h84r2u2heg0_s[];
extern const gm_spec *const gm_tpl0j5bgvjcbn0u_s[];
extern const uint32_t gm_tpl0lbbjeb2vhti_i[];
extern const gm_tmpl_piece gm_tpl0pd502nvjf7a_p[];
extern const gm_tmpl gm_tpl0qlvf3bfgtip;
extern const uint32_t gm_tpl0qlvf3bfgtip_i[];
extern const gm_tmpl_piece gm_tpl0qlvf3bfgtip_p[];
extern const gm_spec *const gm_tpl0qlvf3bfgtip_s[];
extern const gm_spec *const gm_tpl0sb47rg6hcij_s[];
extern const gm_tmpl gm_tpl0td8lr3f4ven;
extern const uint32_t gm_tpl0td8lr3f4ven_i[];
extern const gm_tmpl_piece gm_tpl0td8lr3f4ven_p[];
extern const gm_spec *const gm_tpl0td8lr3f4ven_s[];
extern const gm_tmpl gm_tpl11g5872m2bht;
extern const uint32_t gm_tpl11g5872m2bht_i[];
extern const gm_tmpl_piece gm_tpl11g5872m2bht_p[];
extern const gm_spec *const gm_tpl11g5872m2bht_s[];
extern const gm_spec *const gm_tpl149k5asebpko_s[];
extern const gm_tmpl gm_tpl14b6qietv6bm;
extern const uint32_t gm_tpl14b6qietv6bm_i[];
extern const gm_tmpl_piece gm_tpl14b6qietv6bm_p[];
extern const gm_spec *const gm_tpl14b6qietv6bm_s[];
extern const gm_tmpl_piece gm_tpl18roma1gvhpk_p[];
extern const gm_spec *const gm_tpl18roma1gvhpk_s[];
extern const gm_spec *const gm_tpl19m2mv1u5l7n_s[];
extern const gm_tmpl_piece gm_tpl1bh0s5ll1m9j_p[];
extern const gm_tmpl_piece gm_tpl1blafg62e77e_p[];
extern const uint32_t gm_tpl1c171lb88cdd_i[];
extern const gm_tmpl_piece gm_tpl1dleoebfmvhp_p[];
extern const gm_tmpl_piece gm_tpl1f5t4gadpj5n_p[];
extern const gm_spec *const gm_tpl1g1lh0kp24oa_s[];
extern const gm_tmpl gm_tpl1gtefnja08ps;
extern const uint32_t gm_tpl1gtefnja08ps_i[];
extern const gm_tmpl_piece gm_tpl1gtefnja08ps_p[];
extern const gm_spec *const gm_tpl1gtefnja08ps_s[];
extern const uint32_t gm_tpl1iblbr9u646o_i[];
extern const gm_spec *const gm_tpl1irt5kietk82_s[];
extern const gm_tmpl_piece gm_tpl1lj3ah7qeq42_p[];
extern const gm_tmpl gm_tpl1lmdkbkeigtd;
extern const uint32_t gm_tpl1lmdkbkeigtd_i[];
extern const gm_tmpl_piece gm_tpl1lmdkbkeigtd_p[];
extern const gm_spec *const gm_tpl1lmdkbkeigtd_s[];
extern const gm_spec *const gm_tpl1rdf0mm7rjgv_s[];
extern const gm_tmpl_piece gm_tpl1rfj3tqlepqo_p[];
extern const gm_tmpl_piece gm_tpl1rl5q0rqbd7p_p[];
extern const gm_spec *const gm_tpl1u3j3qlmtjj2_s[];
extern const gm_tmpl_piece gm_tpl1vbgcviqausd_p[];
extern const uint32_t gm_tpl20a4acojo21c_i[];
extern const uint32_t gm_tpl23mh8eh9cef6_i[];
extern const gm_tmpl_piece gm_tpl24fci6dm83eo_p[];
extern const gm_tmpl gm_tpl2bm4b6itkmm6;
extern const uint32_t gm_tpl2bm4b6itkmm6_i[];
extern const gm_tmpl_piece gm_tpl2bm4b6itkmm6_p[];
extern const gm_spec *const gm_tpl2bm4b6itkmm6_s[];
extern const uint32_t gm_tpl2d8hdhnr24pd_i[];
extern const gm_tmpl_piece gm_tpl2dnrl9bvc2hf_p[];
extern const gm_tmpl_piece gm_tpl2f9qqcjkh29p_p[];
extern const gm_tmpl_piece gm_tpl2jsq89buriv9_p[];
extern const gm_spec *const gm_tpl2qal2i0dmdum_s[];
extern const gm_tmpl gm_tpl2utldc2k4g4b;
extern const uint32_t gm_tpl2utldc2k4g4b_i[];
extern const gm_tmpl_piece gm_tpl2utldc2k4g4b_p[];
extern const gm_spec *const gm_tpl2utldc2k4g4b_s[];
extern const uint32_t gm_tpl3072uo9s1dtn_i[];
extern const uint32_t gm_tpl34lim186g0fm_i[];
extern const gm_tmpl gm_tpl355ags9581ob;
extern const uint32_t gm_tpl355ags9581ob_i[];
extern const gm_tmpl_piece gm_tpl355ags9581ob_p[];
extern const gm_spec *const gm_tpl355ags9581ob_s[];
extern const uint32_t gm_tpl36bd3ievounm_i[];
extern const gm_tmpl_piece gm_tpl3jlrgt0cfgj3_p[];
extern const gm_tmpl_piece gm_tpl3kfnv3qa5itp_p[];
extern const gm_spec *const gm_tpl3lpe5kp3g9ir_s[];
extern const gm_spec *const gm_tpl3tkekujl97kv_s[];
extern const gm_tmpl gm_tpl3v2bn2jpabsf;
extern const uint32_t gm_tpl3v2bn2jpabsf_i[];
extern const gm_tmpl_piece gm_tpl3v2bn2jpabsf_p[];
extern const gm_spec *const gm_tpl3v2bn2jpabsf_s[];
extern const gm_spec *const gm_tpl44cjgoji4tlu_s[];
extern const gm_tmpl_piece gm_tpl470f2c4pe1su_p[];
extern const gm_tmpl gm_tpl48h8br68c917;
extern const uint32_t gm_tpl48h8br68c917_i[];
extern const gm_tmpl_piece gm_tpl48h8br68c917_p[];
extern const gm_spec *const gm_tpl48h8br68c917_s[];
extern const gm_tmpl_piece gm_tpl4auv8p2j34k5_p[];
extern const gm_tmpl_piece gm_tpl4c2mfb3cngak_p[];
extern const uint32_t gm_tpl4gs3p00joms4_i[];
extern const gm_tmpl_piece gm_tpl4isjs1bg9fhg_p[];
extern const gm_tmpl gm_tpl4kmab8oiki17;
extern const uint32_t gm_tpl4kmab8oiki17_i[];
extern const gm_tmpl_piece gm_tpl4kmab8oiki17_p[];
extern const gm_spec *const gm_tpl4kmab8oiki17_s[];
extern const uint32_t gm_tpl4mli0f9rjihf_i[];
extern const gm_tmpl_piece gm_tpl4npct6bth1c7_p[];
extern const gm_spec *const gm_tpl4qnmv5o1ceod_s[];
extern const uint32_t gm_tpl4u6a7su34sch_i[];
extern const gm_spec *const gm_tpl500h9j75csqa_s[];
extern const gm_tmpl_piece gm_tpl52cv2e94esne_p[];
extern const gm_spec *const gm_tpl53ihvkdedd31_s[];
extern const gm_spec *const gm_tpl56mvlv70ecs8_s[];
extern const uint32_t gm_tpl59r7h6035548_i[];
extern const gm_tmpl gm_tpl5b7905cihaqk;
extern const uint32_t gm_tpl5b7905cihaqk_i[];
extern const gm_tmpl_piece gm_tpl5b7905cihaqk_p[];
extern const gm_spec *const gm_tpl5b7905cihaqk_s[];
extern const gm_spec *const gm_tpl5g8f9c4f30p4_s[];
extern const uint32_t gm_tpl5g9nntg83p8b_i[];
extern const gm_tmpl_piece gm_tpl5hraomijgptv_p[];
extern const gm_spec *const gm_tpl5igv3ckbutrp_s[];
extern const gm_tmpl_piece gm_tpl5kgb1tfsut02_p[];
extern const uint32_t gm_tpl5o8dcpa0ctot_i[];
extern const uint32_t gm_tpl5st0uuf0quab_i[];
extern const gm_tmpl_piece gm_tpl60bi48lvp2qn_p[];
extern const uint32_t gm_tpl63iuq3je4sbr_i[];
extern const uint32_t gm_tpl65nnuqtjd24o_i[];
extern const uint32_t gm_tpl6ilgcn878l74_i[];
extern const gm_spec *const gm_tpl6md5hu7rmp82_s[];
extern const gm_spec *const gm_tpl6mra39d4fo2s_s[];
extern const gm_spec *const gm_tpl6nogrqoer8f6_s[];
extern const uint32_t gm_tpl6nq0lpkkblpc_i[];
extern const gm_tmpl gm_tpl75b43ohtuvq2;
extern const uint32_t gm_tpl75b43ohtuvq2_i[];
extern const gm_tmpl_piece gm_tpl75b43ohtuvq2_p[];
extern const gm_spec *const gm_tpl75b43ohtuvq2_s[];
extern const uint32_t gm_tpl75bcbum1ntsg_i[];
extern const uint32_t gm_tpl7eeir95ujdvn_i[];
extern const gm_tmpl_piece gm_tpl7jau285qbbrq_p[];
extern const uint32_t gm_tpl7medmjmlvos1_i[];
extern const gm_tmpl gm_tpl7net9ifts13e;
extern const uint32_t gm_tpl7net9ifts13e_i[];
extern const gm_tmpl_piece gm_tpl7net9ifts13e_p[];
extern const gm_spec *const gm_tpl7net9ifts13e_s[];
extern const gm_spec *const gm_tpl7o4acs2tt6pp_s[];
extern const gm_tmpl_piece gm_tpl7r51aeh92b80_p[];
extern const gm_tmpl_piece gm_tpl7umtk166uvmr_p[];
extern const gm_tmpl gm_tpl80jqm34ojjrn;
extern const uint32_t gm_tpl80jqm34ojjrn_i[];
extern const gm_tmpl_piece gm_tpl80jqm34ojjrn_p[];
extern const gm_spec *const gm_tpl80jqm34ojjrn_s[];
extern const gm_spec *const gm_tpl810t9tkid6i2_s[];
extern const uint32_t gm_tpl83abvjjf3qd1_i[];
extern const uint32_t gm_tpl85p2q6ermgga_i[];
extern const uint32_t gm_tpl86cip41cbf58_i[];
extern const gm_spec *const gm_tpl87um69vt0amn_s[];
extern const gm_tmpl gm_tpl8nmc259qeh4g;
extern const uint32_t gm_tpl8nmc259qeh4g_i[];
extern const gm_tmpl_piece gm_tpl8nmc259qeh4g_p[];
extern const gm_spec *const gm_tpl8nmc259qeh4g_s[];
extern const gm_spec *const gm_tpl8nqn0hncnfd4_s[];
extern const gm_tmpl_piece gm_tpl8ocve2bpsu72_p[];
extern const gm_spec *const gm_tpl8ptl55ago227_s[];
extern const gm_tmpl_piece gm_tpl8ro9atn5p92f_p[];
extern const gm_tmpl_piece gm_tpl8v9uhia7uc1b_p[];
extern const uint32_t gm_tpl94ei7rae9si2_i[];
extern const uint32_t gm_tpl955dep3t38r6_i[];
extern const gm_tmpl_piece gm_tpl97n87h3pcva1_p[];
extern const uint32_t gm_tpl98etevndg0cj_i[];
extern const gm_tmpl_piece gm_tpl9aakii48cq8t_p[];
extern const gm_tmpl gm_tpl9cnfdsdh5t3p;
extern const uint32_t gm_tpl9cnfdsdh5t3p_i[];
extern const gm_tmpl_piece gm_tpl9cnfdsdh5t3p_p[];
extern const gm_spec *const gm_tpl9cnfdsdh5t3p_s[];
extern const gm_tmpl gm_tpl9dqq7pjdmqnl;
extern const uint32_t gm_tpl9dqq7pjdmqnl_i[];
extern const gm_tmpl_piece gm_tpl9dqq7pjdmqnl_p[];
extern const gm_spec *const gm_tpl9dqq7pjdmqnl_s[];
extern const uint32_t gm_tpl9h9af6kus46t_i[];
extern const uint32_t gm_tpl9j9ok1j4nn18_i[];
extern const uint32_t gm_tpl9jr09voqi3gh_i[];
extern const gm_spec *const gm_tpl9jrfnomsrfk1_s[];
extern const gm_tmpl gm_tpl9p5406jtlms7;
extern const uint32_t gm_tpl9p5406jtlms7_i[];
extern const gm_tmpl_piece gm_tpl9p5406jtlms7_p[];
extern const gm_spec *const gm_tpl9p5406jtlms7_s[];
extern const gm_tmpl_piece gm_tpl9qchhbq9qlbm_p[];
extern const uint32_t gm_tpl9qg0hlqqke7e_i[];
extern const gm_spec *const gm_tpl9rm8d4b25q2c_s[];
extern const gm_tmpl gm_tpl9s09hqjpb581;
extern const uint32_t gm_tpl9s09hqjpb581_i[];
extern const gm_tmpl_piece gm_tpl9s09hqjpb581_p[];
extern const gm_spec *const gm_tpl9s09hqjpb581_s[];
extern const uint32_t gm_tpl9tc0m8fi3g9d_i[];
extern const gm_spec *const gm_tpla0ci4vs242iu_s[];
extern const uint32_t gm_tpla15qb3qedeuc_i[];
extern const gm_tmpl gm_tpla67mmq1id2h3;
extern const uint32_t gm_tpla67mmq1id2h3_i[];
extern const gm_tmpl_piece gm_tpla67mmq1id2h3_p[];
extern const gm_spec *const gm_tpla67mmq1id2h3_s[];
extern const gm_tmpl_piece gm_tpla8nvlakostko_p[];
extern const gm_spec *const gm_tplai4t67ppan77_s[];
extern const gm_tmpl gm_tplanqv6irfvtut;
extern const uint32_t gm_tplanqv6irfvtut_i[];
extern const gm_tmpl_piece gm_tplanqv6irfvtut_p[];
extern const gm_spec *const gm_tplanqv6irfvtut_s[];
extern const gm_tmpl_piece gm_tplao1juqpqjfv9_p[];
extern const gm_tmpl gm_tplaqps986okebj;
extern const uint32_t gm_tplaqps986okebj_i[];
extern const gm_tmpl_piece gm_tplaqps986okebj_p[];
extern const gm_spec *const gm_tplaqps986okebj_s[];
extern const gm_spec *const gm_tplb2gsdj8iioqj_s[];
extern const uint32_t gm_tplbahlus43pmvm_i[];
extern const gm_tmpl gm_tplbaqni0b3abqt;
extern const uint32_t gm_tplbaqni0b3abqt_i[];
extern const gm_tmpl_piece gm_tplbaqni0b3abqt_p[];
extern const gm_spec *const gm_tplbaqni0b3abqt_s[];
extern const gm_tmpl gm_tplbcublle8ivup;
extern const uint32_t gm_tplbcublle8ivup_i[];
extern const gm_tmpl_piece gm_tplbcublle8ivup_p[];
extern const gm_spec *const gm_tplbcublle8ivup_s[];
extern const gm_spec *const gm_tplbgmktlahcqrn_s[];
extern const uint32_t gm_tplbn5t5stpvru2_i[];
extern const gm_spec *const gm_tplbntllbe37f81_s[];
extern const gm_tmpl_piece gm_tplbo11j4rgqkb3_p[];
extern const gm_tmpl_piece gm_tplbo3vrh9egnst_p[];
extern const gm_spec *const gm_tplbo3vrh9egnst_s[];
extern const gm_spec *const gm_tplbo9rmalpf2vj_s[];
extern const uint32_t gm_tplbr0fi0201tmv_i[];
extern const uint32_t gm_tplbsca6gthghjf_i[];
extern const uint32_t gm_tplbtbg1u9fmcqq_i[];
extern const gm_spec *const gm_tplbtbg1u9fmcqq_s[];
extern const uint32_t gm_tplc3kt858jk6jp_i[];
extern const gm_tmpl_piece gm_tplc8kkdoja48ii_p[];
extern const gm_tmpl gm_tplcaav01djo2md;
extern const uint32_t gm_tplcaav01djo2md_i[];
extern const gm_tmpl_piece gm_tplcaav01djo2md_p[];
extern const gm_spec *const gm_tplcaav01djo2md_s[];
extern const gm_tmpl gm_tplcfgu8t9fq299;
extern const uint32_t gm_tplcfgu8t9fq299_i[];
extern const gm_tmpl_piece gm_tplcfgu8t9fq299_p[];
extern const gm_spec *const gm_tplcfgu8t9fq299_s[];
extern const gm_tmpl_piece gm_tplchb0p7o483ge_p[];
extern const uint32_t gm_tplcic56vj3l629_i[];
extern const gm_spec *const gm_tplcilp3mvhfo4n_s[];
extern const gm_spec *const gm_tplcktokqd7n6fo_s[];
extern const uint32_t gm_tplcl8es85s6qrs_i[];
extern const gm_tmpl gm_tplcpp8u5i709mj;
extern const uint32_t gm_tplcpp8u5i709mj_i[];
extern const gm_tmpl_piece gm_tplcpp8u5i709mj_p[];
extern const gm_spec *const gm_tplcpp8u5i709mj_s[];
extern const uint32_t gm_tplcq107msa8ktd_i[];
extern const gm_spec *const gm_tplcseelhcfsg75_s[];
extern const uint32_t gm_tpld0t14m1fld0p_i[];
extern const gm_tmpl_piece gm_tpld3o5iqghffqq_p[];
extern const gm_tmpl gm_tpld4u3te7g9c5v;
extern const uint32_t gm_tpld4u3te7g9c5v_i[];
extern const gm_tmpl_piece gm_tpld4u3te7g9c5v_p[];
extern const gm_spec *const gm_tpld4u3te7g9c5v_s[];
extern const gm_tmpl_piece gm_tpld611fjclu2tb_p[];
extern const gm_spec *const gm_tpld7d7u7to2a7u_s[];
extern const uint32_t gm_tplda38mm1025ge_i[];
extern const gm_tmpl_piece gm_tplde4760b0jcjn_p[];
extern const uint32_t gm_tpldff5gvnhne6f_i[];
extern const gm_spec *const gm_tpldftt3jdrahlt_s[];
extern const gm_tmpl_piece gm_tpldiv6dh0or1km_p[];
extern const uint32_t gm_tpldjbe17jhbkpg_i[];
extern const gm_tmpl_piece gm_tpldnca4vvdfaim_p[];
extern const gm_tmpl gm_tpldrfjfgoqe179;
extern const uint32_t gm_tpldrfjfgoqe179_i[];
extern const gm_tmpl_piece gm_tpldrfjfgoqe179_p[];
extern const gm_spec *const gm_tpldrfjfgoqe179_s[];
extern const uint32_t gm_tpldti7tgsj3l9q_i[];
extern const gm_spec *const gm_tpldv5armigctqs_s[];
extern const uint32_t gm_tple2rp5rj93mkj_i[];
extern const uint32_t gm_tple4upi9cflnq6_i[];
extern const gm_tmpl_piece gm_tple7ipevcaonst_p[];
extern const gm_tmpl gm_tplebqthjijvqt0;
extern const uint32_t gm_tplebqthjijvqt0_i[];
extern const gm_tmpl_piece gm_tplebqthjijvqt0_p[];
extern const gm_spec *const gm_tplebqthjijvqt0_s[];
extern const gm_spec *const gm_tplecdkophfbiam_s[];
extern const uint32_t gm_tplei8v65ors6jc_i[];
extern const gm_tmpl gm_tplennc1kv0529b;
extern const uint32_t gm_tplennc1kv0529b_i[];
extern const gm_tmpl_piece gm_tplennc1kv0529b_p[];
extern const gm_spec *const gm_tplennc1kv0529b_s[];
extern const uint32_t gm_tpleoop2177apvc_i[];
extern const gm_tmpl_piece gm_tplett0du8rhujl_p[];
extern const gm_spec *const gm_tplf0gekbh2mtku_s[];
extern const gm_tmpl gm_tplf2ivserdf4ic;
extern const uint32_t gm_tplf2ivserdf4ic_i[];
extern const gm_tmpl_piece gm_tplf2ivserdf4ic_p[];
extern const gm_spec *const gm_tplf2ivserdf4ic_s[];
extern const gm_tmpl gm_tplf41j0oji3nkl;
extern const uint32_t gm_tplf41j0oji3nkl_i[];
extern const gm_tmpl_piece gm_tplf41j0oji3nkl_p[];
extern const gm_spec *const gm_tplf41j0oji3nkl_s[];
extern const uint32_t gm_tplf4oacobin5qk_i[];
extern const uint32_t gm_tplf8er2klne616_i[];
extern const uint32_t gm_tplfc08pcq9dhq6_i[];
extern const uint32_t gm_tplfcgbc9s4hmn7_i[];
extern const gm_tmpl_piece gm_tplff72i6ku4gld_p[];
extern const gm_spec *const gm_tplff72i6ku4gld_s[];
extern const uint32_t gm_tplffkq5ue6h7rm_i[];
extern const gm_tmpl gm_tplfjoq6lkhl94q;
extern const uint32_t gm_tplfjoq6lkhl94q_i[];
extern const gm_tmpl_piece gm_tplfjoq6lkhl94q_p[];
extern const gm_spec *const gm_tplfjoq6lkhl94q_s[];
extern const uint32_t gm_tplfl5r6601183l_i[];
extern const gm_tmpl_piece gm_tplfmqnc0q3nc41_p[];
extern const gm_tmpl_piece gm_tplfr6nl2m52h19_p[];
extern const gm_tmpl_piece gm_tplg3qra7vq6h3a_p[];
extern const gm_spec *const gm_tplg575kk57c4i8_s[];
extern const uint32_t gm_tplg7bh9jeid72i_i[];
extern const uint32_t gm_tplgcrr0tu9874k_i[];
extern const uint32_t gm_tplgemvjdfjljaa_i[];
extern const gm_spec *const gm_tplggcatbkggmn2_s[];
extern const gm_tmpl_piece gm_tplgj9d3igk4i62_p[];
extern const gm_tmpl_piece gm_tplgp08d1iih1si_p[];
extern const uint32_t gm_tplgs2l60s65hm3_i[];
extern const uint32_t gm_tplgvbr3vejj74c_i[];
extern const uint32_t gm_tplh0kdp5708tgq_i[];
extern const gm_tmpl gm_tplh2sse9mocm92;
extern const uint32_t gm_tplh2sse9mocm92_i[];
extern const gm_tmpl_piece gm_tplh2sse9mocm92_p[];
extern const gm_spec *const gm_tplh2sse9mocm92_s[];
extern const uint32_t gm_tplh7231rl9trm2_i[];
extern const gm_tmpl gm_tplhciekfbsqng7;
extern const uint32_t gm_tplhciekfbsqng7_i[];
extern const gm_tmpl_piece gm_tplhciekfbsqng7_p[];
extern const gm_spec *const gm_tplhciekfbsqng7_s[];
extern const gm_tmpl_piece gm_tplhdb29uo8rm6j_p[];
extern const gm_tmpl_piece gm_tplhdrbi5vdvbe6_p[];
extern const gm_spec *const gm_tplhdrbi5vdvbe6_s[];
extern const gm_spec *const gm_tplhen8m8tu01kk_s[];
extern const gm_tmpl gm_tplhfdokrq767sh;
extern const uint32_t gm_tplhfdokrq767sh_i[];
extern const gm_tmpl_piece gm_tplhfdokrq767sh_p[];
extern const gm_spec *const gm_tplhfdokrq767sh_s[];
extern const gm_spec *const gm_tplhiqpct20jd67_s[];
extern const gm_spec *const gm_tplhl4q6p2b5f92_s[];
extern const gm_tmpl gm_tplhlt4sff0mrem;
extern const uint32_t gm_tplhlt4sff0mrem_i[];
extern const gm_tmpl_piece gm_tplhlt4sff0mrem_p[];
extern const gm_spec *const gm_tplhlt4sff0mrem_s[];
extern const uint32_t gm_tplhmairn7oibac_i[];
extern const gm_spec *const gm_tplhmairn7oibac_s[];
extern const uint32_t gm_tplhn9hprqs819d_i[];
extern const uint32_t gm_tplhon5g1o0vuqi_i[];
extern const gm_tmpl_piece gm_tplhp4k6h527j3k_p[];
extern const gm_spec *const gm_tplhs7ofk4c8qgg_s[];
extern const uint32_t gm_tplhu7i2tvvfh15_i[];
extern const gm_tmpl gm_tplhv087vo2q53g;
extern const uint32_t gm_tplhv087vo2q53g_i[];
extern const gm_tmpl_piece gm_tplhv087vo2q53g_p[];
extern const gm_spec *const gm_tplhv087vo2q53g_s[];
extern const gm_tmpl_piece gm_tpli08vvf1bckj4_p[];
extern const gm_spec *const gm_tpli0v9k7mac3tj_s[];
extern const gm_tmpl gm_tpli7hldrrhg7sr;
extern const uint32_t gm_tpli7hldrrhg7sr_i[];
extern const gm_tmpl_piece gm_tpli7hldrrhg7sr_p[];
extern const gm_spec *const gm_tpli7hldrrhg7sr_s[];
extern const gm_spec *const gm_tpli7ohq5o5sj5n_s[];
extern const gm_tmpl gm_tplicmhpq9e6sbc;
extern const uint32_t gm_tplicmhpq9e6sbc_i[];
extern const gm_tmpl_piece gm_tplicmhpq9e6sbc_p[];
extern const gm_spec *const gm_tplicmhpq9e6sbc_s[];
extern const gm_spec *const gm_tplidajav2hoc6k_s[];
extern const uint32_t gm_tplie81jau63n6g_i[];
extern const gm_spec *const gm_tplims1qainr636_s[];
extern const gm_tmpl gm_tplip1v4sd2477s;
extern const uint32_t gm_tplip1v4sd2477s_i[];
extern const gm_tmpl_piece gm_tplip1v4sd2477s_p[];
extern const gm_spec *const gm_tplip1v4sd2477s_s[];
extern const gm_tmpl_piece gm_tplj3bqjm2hv786_p[];
extern const gm_tmpl_piece gm_tplj3vfuklvht3e_p[];
extern const gm_spec *const gm_tplj451rojbvr15_s[];
extern const uint32_t gm_tplj8iadiucj6so_i[];
extern const uint32_t gm_tplj92mk62u70db_i[];
extern const gm_spec *const gm_tpljbco1er3agg5_s[];
extern const gm_tmpl_piece gm_tpljbk2s8tv7r8m_p[];
extern const gm_spec *const gm_tpljbk2s8tv7r8m_s[];
extern const gm_tmpl gm_tpljgv7fkm5ld25;
extern const uint32_t gm_tpljgv7fkm5ld25_i[];
extern const gm_tmpl_piece gm_tpljgv7fkm5ld25_p[];
extern const gm_spec *const gm_tpljgv7fkm5ld25_s[];
extern const uint32_t gm_tpljhar6jmbtsst_i[];
extern const uint32_t gm_tpljicvo0ni8aba_i[];
extern const gm_tmpl gm_tpljkde9cibn2er;
extern const uint32_t gm_tpljkde9cibn2er_i[];
extern const gm_tmpl_piece gm_tpljkde9cibn2er_p[];
extern const gm_spec *const gm_tpljkde9cibn2er_s[];
extern const gm_tmpl_piece gm_tpljt2av4h7c132_p[];
extern const gm_tmpl_piece gm_tpljucl1tv02je2_p[];
extern const gm_spec *const gm_tpljucl1tv02je2_s[];
extern const gm_spec *const gm_tplk0jab62rgl3v_s[];
extern const uint32_t gm_tplk34ojqiaho84_i[];
extern const gm_tmpl gm_tplk4i636uhvi9m;
extern const uint32_t gm_tplk4i636uhvi9m_i[];
extern const gm_tmpl_piece gm_tplk4i636uhvi9m_p[];
extern const gm_spec *const gm_tplk4i636uhvi9m_s[];
extern const uint32_t gm_tplk5o80h990o81_i[];
extern const gm_tmpl gm_tplk5t5jhqqvnff;
extern const uint32_t gm_tplk5t5jhqqvnff_i[];
extern const gm_tmpl_piece gm_tplk5t5jhqqvnff_p[];
extern const gm_spec *const gm_tplk5t5jhqqvnff_s[];
extern const uint32_t gm_tplk83cf3bpuiqb_i[];
extern const gm_spec *const gm_tplkd07482m1uv1_s[];
extern const gm_spec *const gm_tplkjc5mpc5i80h_s[];
extern const uint32_t gm_tplkkvn546crm4n_i[];
extern const gm_spec *const gm_tplkokmqjs81d4g_s[];
extern const gm_tmpl gm_tplkv0rjh54m5gn;
extern const uint32_t gm_tplkv0rjh54m5gn_i[];
extern const gm_tmpl_piece gm_tplkv0rjh54m5gn_p[];
extern const gm_spec *const gm_tplkv0rjh54m5gn_s[];
extern const uint32_t gm_tplkv6ui0mqvq2g_i[];
extern const uint32_t gm_tpll1i6ur15ms5i_i[];
extern const gm_spec *const gm_tpll6jntfkrhtt4_s[];
extern const gm_tmpl_piece gm_tpllak8uc09ui7v_p[];
extern const gm_spec *const gm_tpllaurt6qemv8q_s[];
extern const gm_spec *const gm_tpllc6r78fq9cl8_s[];
extern const gm_tmpl gm_tpllcjujjfoc50t;
extern const uint32_t gm_tpllcjujjfoc50t_i[];
extern const gm_tmpl_piece gm_tpllcjujjfoc50t_p[];
extern const gm_spec *const gm_tpllcjujjfoc50t_s[];
extern const uint32_t gm_tplld08lp5kl07i_i[];
extern const gm_tmpl_piece gm_tplld9eu0oenqb7_p[];
extern const gm_tmpl gm_tpllgrg03l2v74l;
extern const uint32_t gm_tpllgrg03l2v74l_i[];
extern const gm_tmpl_piece gm_tpllgrg03l2v74l_p[];
extern const gm_spec *const gm_tpllgrg03l2v74l_s[];
extern const gm_spec *const gm_tpllikrj1hgkvih_s[];
extern const gm_tmpl_piece gm_tplllolcurrktr6_p[];
extern const gm_tmpl_piece gm_tpllnpq25q3a5a4_p[];
extern const gm_spec *const gm_tplloue1uueuiig_s[];
extern const gm_tmpl gm_tpllv3q9mfan4rg;
extern const uint32_t gm_tpllv3q9mfan4rg_i[];
extern const gm_tmpl_piece gm_tpllv3q9mfan4rg_p[];
extern const gm_spec *const gm_tpllv3q9mfan4rg_s[];
extern const gm_tmpl_piece gm_tplm02t0g7r3hdp_p[];
extern const gm_spec *const gm_tplm02t0g7r3hdp_s[];
extern const gm_spec *const gm_tplm16s64mnihe4_s[];
extern const gm_tmpl gm_tplm1fuloaqnfv4;
extern const uint32_t gm_tplm1fuloaqnfv4_i[];
extern const gm_tmpl_piece gm_tplm1fuloaqnfv4_p[];
extern const gm_spec *const gm_tplm1fuloaqnfv4_s[];
extern const uint32_t gm_tplm2m182mt5ljj_i[];
extern const uint32_t gm_tplm3f7baid20me_i[];
extern const gm_tmpl gm_tplm9hj31tk9ip4;
extern const uint32_t gm_tplm9hj31tk9ip4_i[];
extern const gm_tmpl_piece gm_tplm9hj31tk9ip4_p[];
extern const gm_spec *const gm_tplm9hj31tk9ip4_s[];
extern const gm_spec *const gm_tplmcmsjqrp0d64_s[];
extern const gm_tmpl gm_tplmebto0o1gmln;
extern const uint32_t gm_tplmebto0o1gmln_i[];
extern const gm_tmpl_piece gm_tplmebto0o1gmln_p[];
extern const gm_spec *const gm_tplmebto0o1gmln_s[];
extern const gm_tmpl_piece gm_tplmemqaoor78ph_p[];
extern const gm_tmpl_piece gm_tplmg7sb8gq6o3a_p[];
extern const gm_spec *const gm_tplmnlel5aohei6_s[];
extern const uint32_t gm_tplmtg92v4cesud_i[];
extern const gm_tmpl gm_tplmv1ukn138isf;
extern const uint32_t gm_tplmv1ukn138isf_i[];
extern const gm_tmpl_piece gm_tplmv1ukn138isf_p[];
extern const gm_spec *const gm_tplmv1ukn138isf_s[];
extern const uint32_t gm_tpln1554he46gkc_i[];
extern const gm_spec *const gm_tpln1554he46gkc_s[];
extern const gm_tmpl_piece gm_tpln1d9amhhlm2o_p[];
extern const gm_spec *const gm_tpln2i759e19cgt_s[];
extern const gm_tmpl_piece gm_tpln3j86kdpqd7a_p[];
extern const gm_tmpl gm_tpln5ioe1q518f0;
extern const uint32_t gm_tpln5ioe1q518f0_i[];
extern const gm_tmpl_piece gm_tpln5ioe1q518f0_p[];
extern const gm_spec *const gm_tpln5ioe1q518f0_s[];
extern const uint32_t gm_tpln8ctpo9hror7_i[];
extern const gm_spec *const gm_tpln8ctpo9hror7_s[];
extern const gm_spec *const gm_tplncn0njcasr6v_s[];
extern const uint32_t gm_tplnitaca1dp5li_i[];
extern const gm_tmpl_piece gm_tplnlqa25lecq48_p[];
extern const gm_tmpl gm_tplnm0o65j76qg0;
extern const uint32_t gm_tplnm0o65j76qg0_i[];
extern const gm_tmpl_piece gm_tplnm0o65j76qg0_p[];
extern const gm_spec *const gm_tplnm0o65j76qg0_s[];
extern const gm_spec *const gm_tplnmufhmou1ie5_s[];
extern const gm_tmpl gm_tplnr2qhie4t5ta;
extern const uint32_t gm_tplnr2qhie4t5ta_i[];
extern const gm_tmpl_piece gm_tplnr2qhie4t5ta_p[];
extern const gm_spec *const gm_tplnr2qhie4t5ta_s[];
extern const gm_tmpl gm_tplntk567ck43t8;
extern const uint32_t gm_tplntk567ck43t8_i[];
extern const gm_tmpl_piece gm_tplntk567ck43t8_p[];
extern const gm_spec *const gm_tplntk567ck43t8_s[];
extern const gm_tmpl_piece gm_tplnunrbopfpsiq_p[];
extern const uint32_t gm_tplo4mr4l55q97r_i[];
extern const gm_tmpl gm_tplo66949vs5pfo;
extern const uint32_t gm_tplo66949vs5pfo_i[];
extern const gm_tmpl_piece gm_tplo66949vs5pfo_p[];
extern const gm_spec *const gm_tplo66949vs5pfo_s[];
extern const gm_tmpl_piece gm_tploik9nhlf98dm_p[];
extern const uint32_t gm_tploj0vopi0n43l_i[];
extern const uint32_t gm_tploj4uaptg6qnk_i[];
extern const gm_tmpl_piece gm_tplokvovhsdbec1_p[];
extern const gm_tmpl gm_tplolnkad3lslng;
extern const uint32_t gm_tplolnkad3lslng_i[];
extern const gm_tmpl_piece gm_tplolnkad3lslng_p[];
extern const gm_spec *const gm_tplolnkad3lslng_s[];
extern const gm_tmpl_piece gm_tplolonhuuitsj2_p[];
extern const uint32_t gm_tplomf18g3j9if3_i[];
extern const gm_tmpl gm_tplop2uhngiv88m;
extern const uint32_t gm_tplop2uhngiv88m_i[];
extern const gm_tmpl_piece gm_tplop2uhngiv88m_p[];
extern const gm_spec *const gm_tplop2uhngiv88m_s[];
extern const gm_tmpl_piece gm_tplopmhgmch9jsj_p[];
extern const uint32_t gm_tplotost4gd8bha_i[];
extern const gm_tmpl_piece gm_tplovoda71kp04v_p[];
extern const gm_spec *const gm_tplp4f8nloo6fdo_s[];
extern const gm_tmpl_piece gm_tplpa402pkgji4o_p[];
extern const gm_tmpl_piece gm_tplpj1vfgivuj13_p[];
extern const gm_tmpl gm_tplq12pt4e53uj3;
extern const uint32_t gm_tplq12pt4e53uj3_i[];
extern const gm_tmpl_piece gm_tplq12pt4e53uj3_p[];
extern const gm_spec *const gm_tplq12pt4e53uj3_s[];
extern const gm_tmpl_piece gm_tplq5eond7ctc71_p[];
extern const gm_spec *const gm_tplq5eond7ctc71_s[];
extern const uint32_t gm_tplqar620s2vpg7_i[];
extern const gm_tmpl gm_tplqbtvqd3a4g25;
extern const uint32_t gm_tplqbtvqd3a4g25_i[];
extern const gm_tmpl_piece gm_tplqbtvqd3a4g25_p[];
extern const gm_spec *const gm_tplqbtvqd3a4g25_s[];
extern const gm_tmpl gm_tplqc6j3ejcoft6;
extern const uint32_t gm_tplqc6j3ejcoft6_i[];
extern const gm_tmpl_piece gm_tplqc6j3ejcoft6_p[];
extern const gm_spec *const gm_tplqc6j3ejcoft6_s[];
extern const uint32_t gm_tplqf2cda88og0i_i[];
extern const uint32_t gm_tplqfgvjo31nom3_i[];
extern const gm_spec *const gm_tplqipe054or34v_s[];
extern const uint32_t gm_tplqrah664loq9e_i[];
extern const gm_tmpl_piece gm_tplqrah664loq9e_p[];
extern const gm_tmpl gm_tplr0qnbmgpa2m5;
extern const uint32_t gm_tplr0qnbmgpa2m5_i[];
extern const gm_tmpl_piece gm_tplr0qnbmgpa2m5_p[];
extern const gm_spec *const gm_tplr0qnbmgpa2m5_s[];
extern const gm_tmpl_piece gm_tplr2c5vmkfj9df_p[];
extern const gm_tmpl gm_tplr42r2lp0p4d1;
extern const uint32_t gm_tplr42r2lp0p4d1_i[];
extern const gm_tmpl_piece gm_tplr42r2lp0p4d1_p[];
extern const gm_spec *const gm_tplr42r2lp0p4d1_s[];
extern const gm_spec *const gm_tplr87ajinlifq9_s[];
extern const uint32_t gm_tplrcmh11f2ocju_i[];
extern const uint32_t gm_tplrcurrnquacuf_i[];
extern const gm_tmpl gm_tplre5cfbnr7ltv;
extern const uint32_t gm_tplre5cfbnr7ltv_i[];
extern const gm_tmpl_piece gm_tplre5cfbnr7ltv_p[];
extern const gm_spec *const gm_tplre5cfbnr7ltv_s[];
extern const uint32_t gm_tplrffp652tkgo2_i[];
extern const gm_tmpl_piece gm_tplrgtnfea81qcj_p[];
extern const gm_tmpl_piece gm_tplrh70572i8ko0_p[];
extern const uint32_t gm_tplrivm1rlf1ps3_i[];
extern const gm_tmpl gm_tplrmoc4ag4eqrb;
extern const uint32_t gm_tplrmoc4ag4eqrb_i[];
extern const gm_tmpl_piece gm_tplrmoc4ag4eqrb_p[];
extern const gm_spec *const gm_tplrmoc4ag4eqrb_s[];
extern const uint32_t gm_tplrp4haaemeqja_i[];
extern const gm_spec *const gm_tplrpvalpqjvipe_s[];
extern const uint32_t gm_tplrspdrqd1dpc8_i[];
extern const gm_spec *const gm_tplrspdrqd1dpc8_s[];
extern const gm_tmpl_piece gm_tplrtd64pk59l9s_p[];
extern const gm_spec *const gm_tplrtgv8em132rl_s[];
extern const gm_tmpl gm_tpls2fio2pf4rdq;
extern const uint32_t gm_tpls2fio2pf4rdq_i[];
extern const gm_tmpl_piece gm_tpls2fio2pf4rdq_p[];
extern const gm_spec *const gm_tpls2fio2pf4rdq_s[];
extern const uint32_t gm_tpls3fe84n8lo7i_i[];
extern const gm_tmpl gm_tpls4d1lh9jd2u8;
extern const uint32_t gm_tpls4d1lh9jd2u8_i[];
extern const gm_tmpl_piece gm_tpls4d1lh9jd2u8_p[];
extern const gm_spec *const gm_tpls4d1lh9jd2u8_s[];
extern const uint32_t gm_tpls4d220l28nlk_i[];
extern const uint32_t gm_tpls5b11sf5gq9o_i[];
extern const gm_tmpl_piece gm_tpls6cg1uub8gfo_p[];
extern const gm_spec *const gm_tpls6dj3unub5ff_s[];
extern const gm_spec *const gm_tpls7kn7htisa6n_s[];
extern const gm_tmpl gm_tpls92cg1g1s0vs;
extern const uint32_t gm_tpls92cg1g1s0vs_i[];
extern const gm_tmpl_piece gm_tpls92cg1g1s0vs_p[];
extern const gm_spec *const gm_tpls92cg1g1s0vs_s[];
extern const uint32_t gm_tplsf1c5t6qt3u3_i[];
extern const gm_tmpl gm_tplsf6r3t44km8l;
extern const uint32_t gm_tplsf6r3t44km8l_i[];
extern const gm_tmpl_piece gm_tplsf6r3t44km8l_p[];
extern const gm_spec *const gm_tplsf6r3t44km8l_s[];
extern const gm_tmpl_piece gm_tplsgm949s33v8m_p[];
extern const gm_tmpl_piece gm_tplshqe0v39oc52_p[];
extern const uint32_t gm_tplsiiifs0eedvi_i[];
extern const gm_spec *const gm_tplsjs5e78h34ng_s[];
extern const uint32_t gm_tplsjtf9pg2ergu_i[];
extern const gm_tmpl_piece gm_tplsjtf9pg2ergu_p[];
extern const gm_spec *const gm_tplsleqso6g0tjq_s[];
extern const gm_tmpl gm_tplslqhr3ac3ilc;
extern const uint32_t gm_tplslqhr3ac3ilc_i[];
extern const gm_tmpl_piece gm_tplslqhr3ac3ilc_p[];
extern const gm_spec *const gm_tplslqhr3ac3ilc_s[];
extern const gm_tmpl_piece gm_tplsnlr78cvfrao_p[];
extern const uint32_t gm_tplso01g2paq6o6_i[];
extern const gm_tmpl_piece gm_tplsrmv18emggok_p[];
extern const gm_tmpl gm_tplss5b4q9est2j;
extern const uint32_t gm_tplss5b4q9est2j_i[];
extern const gm_tmpl_piece gm_tplss5b4q9est2j_p[];
extern const gm_spec *const gm_tplss5b4q9est2j_s[];
extern const gm_tmpl gm_tplsvdv91ohcdg6;
extern const uint32_t gm_tplsvdv91ohcdg6_i[];
extern const gm_tmpl_piece gm_tplsvdv91ohcdg6_p[];
extern const gm_spec *const gm_tplsvdv91ohcdg6_s[];
extern const gm_tmpl gm_tplt129oa9ve3j7;
extern const uint32_t gm_tplt129oa9ve3j7_i[];
extern const gm_tmpl_piece gm_tplt129oa9ve3j7_p[];
extern const gm_spec *const gm_tplt129oa9ve3j7_s[];
extern const gm_tmpl gm_tplt3knh0ehirt7;
extern const uint32_t gm_tplt3knh0ehirt7_i[];
extern const gm_tmpl_piece gm_tplt3knh0ehirt7_p[];
extern const gm_spec *const gm_tplt3knh0ehirt7_s[];
extern const uint32_t gm_tplt5hu85fc8epn_i[];
extern const uint32_t gm_tplt68r4kihke54_i[];
extern const gm_spec *const gm_tpltfcdqmjl1tnb_s[];
extern const gm_tmpl gm_tpltgqv8dvi81j3;
extern const uint32_t gm_tpltgqv8dvi81j3_i[];
extern const gm_tmpl_piece gm_tpltgqv8dvi81j3_p[];
extern const gm_spec *const gm_tpltgqv8dvi81j3_s[];
extern const gm_tmpl_piece gm_tpltlq1bga4cgul_p[];
extern const uint32_t gm_tpltpd3ap2ql1ip_i[];
extern const uint32_t gm_tplts0sr46c58jl_i[];
extern const gm_spec *const gm_tpltur94fmrrcmk_s[];
extern const gm_tmpl gm_tplu0h8mqm004jg;
extern const uint32_t gm_tplu0h8mqm004jg_i[];
extern const gm_tmpl_piece gm_tplu0h8mqm004jg_p[];
extern const gm_spec *const gm_tplu0h8mqm004jg_s[];
extern const gm_tmpl gm_tplu3lqf0bugia3;
extern const uint32_t gm_tplu3lqf0bugia3_i[];
extern const gm_tmpl_piece gm_tplu3lqf0bugia3_p[];
extern const gm_spec *const gm_tplu3lqf0bugia3_s[];
extern const gm_spec *const gm_tplu4465vjgoqre_s[];
extern const gm_tmpl gm_tplu7gkm8brh8s9;
extern const uint32_t gm_tplu7gkm8brh8s9_i[];
extern const gm_tmpl_piece gm_tplu7gkm8brh8s9_p[];
extern const gm_spec *const gm_tplu7gkm8brh8s9_s[];
extern const uint32_t gm_tplu7oi9kuo0adg_i[];
extern const gm_tmpl_piece gm_tplu9669cm9mqja_p[];
extern const gm_tmpl_piece gm_tplua69gir871ue_p[];
extern const uint32_t gm_tplubod389iot6h_i[];
extern const uint32_t gm_tpluc9sbr66n218_i[];
extern const gm_spec *const gm_tplud75n1k6o39i_s[];
extern const gm_tmpl_piece gm_tplue2kdcikgcof_p[];
extern const uint32_t gm_tplugmq0iksatjg_i[];
extern const gm_tmpl_piece gm_tplugrpoiqe5r59_p[];
extern const gm_tmpl_piece gm_tpluh0hed6nj59d_p[];
extern const uint32_t gm_tpluhfd3hvppdji_i[];
extern const gm_spec *const gm_tplumuahjb5m8m8_s[];
extern const gm_tmpl gm_tplup0bf5d2eopf;
extern const uint32_t gm_tplup0bf5d2eopf_i[];
extern const gm_tmpl_piece gm_tplup0bf5d2eopf_p[];
extern const gm_spec *const gm_tplup0bf5d2eopf_s[];
extern const uint32_t gm_tplut4ib9ov72oe_i[];
extern const uint32_t gm_tpluttgpct24aib_i[];
extern const gm_tmpl_piece gm_tplv1shkj7l87mc_p[];
extern const gm_tmpl gm_tplv3jh8gpmh6o5;
extern const uint32_t gm_tplv3jh8gpmh6o5_i[];
extern const gm_tmpl_piece gm_tplv3jh8gpmh6o5_p[];
extern const gm_spec *const gm_tplv3jh8gpmh6o5_s[];
extern const uint32_t gm_tplv516qd7l6o3q_i[];
extern const gm_spec *const gm_tplv8ckuk7p1s8m_s[];
extern const gm_spec *const gm_tplv8s8fgl2lggu_s[];
extern const gm_tmpl_piece gm_tplvdft4iatcp17_p[];
extern const gm_spec *const gm_tplveaglbhvc5ls_s[];
extern const uint32_t gm_tplvf2bf6hftf4i_i[];
extern const uint32_t gm_tplviqo6u6r0sr3_i[];
extern const gm_tmpl_piece gm_tplvj9n0kinsqlj_p[];
extern const uint32_t gm_tplvkf7arb107q2_i[];
extern const gm_spec *const gm_tplvm7e7lfhcheu_s[];
extern const gm_tmpl_piece gm_tplvpjd91cp2l7g_p[];
extern const gm_tmpl_piece gm_tplvq03d5skib28_p[];
extern const gm_type gm_ty0lfe1s2pin;
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_ty929lennfd3;
extern const gm_type gm_tyasplg81cl8;
extern const gm_type gm_tyehhdtpeti1;
extern const gm_variant gm_tyg63o53ar52_v[];
extern const gm_type gm_tyjmccqdb90f;
extern const gm_type gm_tyo86ks9qkvt;
extern const gm_type gm_tyov4e3mbi0f;
extern const gm_variant gm_tyov4e3mbi0f_v[];
extern const gm_type gm_tytje7cegoml;
inline __attribute__((aligned(4))) gm_b16_8 gm_f_dir_of_ho73hue(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_dir_of_ho73hue F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    gm_th2lqbo77pvsc gm_l2;
    gm_b16_8 t2 = gm_l0;
    gm_f_parent_lk0iero((void *)&gm_l2, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t3 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
    if (t3) {
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
        gm_l1 = t4;
    } else {
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(0));
        gm_l1 = t5;
    }
    gm_b16_8 t6 = gm_l1;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1qm1hvv(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_1qm1hvv F;
    GM_STACK_CHECK(&F, 45u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tftb1odc739t5 gm_l2;
    {
        {
            memmove(((uint8_t *)&F.l1), ((uint8_t *)&F.l0), 56);
            void * t4 = (void *)((uint8_t *)&((struct gm_fr_open_dirs_9ud02ot *)((&F.h)->up))->l1);
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
            gm_b16_8 t6 = gm_f_dir_of_ho73hue((void *)NULL, cx, NULL, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t4, t6);
            gm_l2 = t7;
            uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
            if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcfpg6jsb3cfp6, 529, 36);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcfpg6jsb3cfp6, 529, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_i2koblr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_i2koblr F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* p */
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
            void * t5 = (void *)((uint8_t *)&((struct gm_fr_check_triple_uqaropr *)((&F.h)->up))->l2);
            gm_b16_8 t6 = gm_l1;
            gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
            gm_l2 = t7;
            uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
            if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcmm27gepldtcm, 275, 39);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srcmm27gepldtcm, 275, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
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
const char gm_src3k278vcmqgdr[] = "compiler/sema/adt.gm";
const char gm_src7h8dni243k2v[] = "compiler/sema/untyped.gm";
const char gm_src7nt72u848oha[] = "compiler/check/construct.gm";
const char gm_src95fgh4jdpgpn[] = "compiler/interp/contexts.gm";
const char gm_srccvj6up3gbolp[] = "compiler/sema/foreign.gm";
const char gm_srcge9itd5fie17[] = "lib/std/json/json.gm";
const char gm_srcgr2mvgis129a[] = "compiler/check/handler_decls.gm";
const char gm_srcjjg6ldpifdau[] = "compiler/cgen/bits.gm";
const char gm_srck9pqpjjfjkmq[] = "compiler/ide/ide.gm";
const char gm_srckfibkpt681aa[] = "compiler/cgen/types.gm";
const char gm_srckqas99ada0fa[] = "compiler/interp/vm_gen.gm";
const char gm_srcrq1gl2e1lsg5[] = "compiler/testing/run.gm";
const char gm_srcuokv9ad9oqqb[] = "compiler/cgen/freestanding.gm";
const gm_spec *const gm_tpl0j5bgvjcbn0u_s[] = {NULL, NULL};
const gm_spec *const gm_tpl0sb47rg6hcij_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl149k5asebpko_s[] = {NULL};
const gm_spec *const gm_tpl18roma1gvhpk_s[] = {NULL};
const gm_spec *const gm_tpl19m2mv1u5l7n_s[] = {NULL};
const gm_spec *const gm_tpl1g1lh0kp24oa_s[] = {NULL, NULL};
const gm_spec *const gm_tpl1irt5kietk82_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl1rdf0mm7rjgv_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl1u3j3qlmtjj2_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl2qal2i0dmdum_s[] = {NULL};
const gm_spec *const gm_tpl3lpe5kp3g9ir_s[] = {NULL};
const gm_spec *const gm_tpl3tkekujl97kv_s[] = {NULL};
const gm_spec *const gm_tpl44cjgoji4tlu_s[] = {NULL};
const gm_spec *const gm_tpl4qnmv5o1ceod_s[] = {NULL};
const gm_spec *const gm_tpl500h9j75csqa_s[] = {NULL};
const gm_spec *const gm_tpl53ihvkdedd31_s[] = {NULL};
const gm_spec *const gm_tpl56mvlv70ecs8_s[] = {NULL};
const gm_spec *const gm_tpl5g8f9c4f30p4_s[] = {NULL};
const gm_spec *const gm_tpl5igv3ckbutrp_s[] = {NULL};
const gm_spec *const gm_tpl6md5hu7rmp82_s[] = {NULL};
const gm_spec *const gm_tpl6mra39d4fo2s_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl6nogrqoer8f6_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl7o4acs2tt6pp_s[] = {NULL};
const gm_spec *const gm_tpl810t9tkid6i2_s[] = {NULL};
const gm_spec *const gm_tpl87um69vt0amn_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl8nqn0hncnfd4_s[] = {NULL, NULL};
const gm_spec *const gm_tpl8ptl55ago227_s[] = {NULL};
const gm_spec *const gm_tpl9dqq7pjdmqnl_s[] = {NULL};
const gm_spec *const gm_tpl9jrfnomsrfk1_s[] = {NULL, NULL};
const gm_spec *const gm_tpl9rm8d4b25q2c_s[] = {NULL};
const gm_spec *const gm_tpla0ci4vs242iu_s[] = {NULL, NULL};
const gm_spec *const gm_tplai4t67ppan77_s[] = {NULL};
const gm_spec *const gm_tplb2gsdj8iioqj_s[] = {NULL, &(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1}};
const gm_spec *const gm_tplbgmktlahcqrn_s[] = {NULL};
const gm_spec *const gm_tplbntllbe37f81_s[] = {NULL};
const gm_spec *const gm_tplbo3vrh9egnst_s[] = {NULL, NULL};
const gm_spec *const gm_tplbo9rmalpf2vj_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplbtbg1u9fmcqq_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplcilp3mvhfo4n_s[] = {&(gm_spec){32, 0, 0, 0, 0, 1, 0u, -1}};
const gm_spec *const gm_tplcktokqd7n6fo_s[] = {NULL};
const gm_spec *const gm_tplcseelhcfsg75_s[] = {NULL, NULL};
const gm_spec *const gm_tpld7d7u7to2a7u_s[] = {NULL};
const gm_spec *const gm_tpldftt3jdrahlt_s[] = {NULL, NULL};
const gm_spec *const gm_tpldv5armigctqs_s[] = {NULL, NULL};
const gm_spec *const gm_tplecdkophfbiam_s[] = {NULL};
const gm_spec *const gm_tplf0gekbh2mtku_s[] = {NULL};
const gm_spec *const gm_tplff72i6ku4gld_s[] = {NULL, NULL};
const gm_spec *const gm_tplg575kk57c4i8_s[] = {NULL, NULL};
const gm_spec *const gm_tplggcatbkggmn2_s[] = {NULL};
const gm_spec *const gm_tplhdrbi5vdvbe6_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplhen8m8tu01kk_s[] = {NULL, NULL};
const gm_spec *const gm_tplhiqpct20jd67_s[] = {NULL};
const gm_spec *const gm_tplhl4q6p2b5f92_s[] = {NULL};
const gm_spec *const gm_tplhmairn7oibac_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplhs7ofk4c8qgg_s[] = {NULL};
const gm_spec *const gm_tpli0v9k7mac3tj_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpli7ohq5o5sj5n_s[] = {NULL, NULL};
const gm_spec *const gm_tplidajav2hoc6k_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplims1qainr636_s[] = {NULL};
const gm_spec *const gm_tplj451rojbvr15_s[] = {NULL};
const gm_spec *const gm_tpljbco1er3agg5_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpljbk2s8tv7r8m_s[] = {NULL, NULL};
const gm_spec *const gm_tpljgv7fkm5ld25_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpljucl1tv02je2_s[] = {NULL};
const gm_spec *const gm_tplk0jab62rgl3v_s[] = {NULL};
const gm_spec *const gm_tplkd07482m1uv1_s[] = {NULL, NULL};
const gm_spec *const gm_tplkjc5mpc5i80h_s[] = {NULL, NULL};
const gm_spec *const gm_tplkokmqjs81d4g_s[] = {NULL, NULL};
const gm_spec *const gm_tpll6jntfkrhtt4_s[] = {NULL, NULL};
const gm_spec *const gm_tpllaurt6qemv8q_s[] = {NULL};
const gm_spec *const gm_tpllc6r78fq9cl8_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpllcjujjfoc50t_s[] = {NULL};
const gm_spec *const gm_tpllikrj1hgkvih_s[] = {NULL};
const gm_spec *const gm_tplloue1uueuiig_s[] = {NULL};
const gm_spec *const gm_tplm02t0g7r3hdp_s[] = {NULL};
const gm_spec *const gm_tplm16s64mnihe4_s[] = {NULL};
const gm_spec *const gm_tplmcmsjqrp0d64_s[] = {NULL};
const gm_spec *const gm_tplmnlel5aohei6_s[] = {NULL};
const gm_spec *const gm_tpln1554he46gkc_s[] = {NULL};
const gm_spec *const gm_tpln2i759e19cgt_s[] = {NULL};
const gm_spec *const gm_tpln8ctpo9hror7_s[] = {NULL};
const gm_spec *const gm_tplncn0njcasr6v_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplnm0o65j76qg0_s[] = {NULL, NULL};
const gm_spec *const gm_tplnmufhmou1ie5_s[] = {NULL};
const gm_spec *const gm_tplp4f8nloo6fdo_s[] = {NULL};
const gm_spec *const gm_tplq5eond7ctc71_s[] = {NULL};
const gm_spec *const gm_tplqc6j3ejcoft6_s[] = {NULL, NULL};
const gm_spec *const gm_tplqipe054or34v_s[] = {NULL};
const gm_spec *const gm_tplr0qnbmgpa2m5_s[] = {NULL, NULL};
const gm_spec *const gm_tplr87ajinlifq9_s[] = {NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplrpvalpqjvipe_s[] = {NULL, NULL};
const gm_spec *const gm_tplrspdrqd1dpc8_s[] = {NULL, NULL};
const gm_spec *const gm_tplrtgv8em132rl_s[] = {NULL, NULL};
const gm_spec *const gm_tpls6dj3unub5ff_s[] = {NULL, NULL};
const gm_spec *const gm_tpls7kn7htisa6n_s[] = {NULL};
const gm_spec *const gm_tplsjs5e78h34ng_s[] = {NULL};
const gm_spec *const gm_tplsleqso6g0tjq_s[] = {NULL};
const gm_spec *const gm_tpltfcdqmjl1tnb_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpltur94fmrrcmk_s[] = {NULL, NULL};
const gm_spec *const gm_tplu4465vjgoqre_s[] = {NULL};
const gm_spec *const gm_tplud75n1k6o39i_s[] = {NULL};
const gm_spec *const gm_tplumuahjb5m8m8_s[] = {NULL, NULL};
const gm_spec *const gm_tplv8ckuk7p1s8m_s[] = {NULL, NULL};
const gm_spec *const gm_tplv8s8fgl2lggu_s[] = {NULL};
const gm_spec *const gm_tplveaglbhvc5ls_s[] = {NULL};
const gm_spec *const gm_tplvm7e7lfhcheu_s[] = {NULL, NULL};
const gm_tmpl gm_tpl0h84r2u2heg0 = {4u, gm_tpl0h84r2u2heg0_p, gm_tpl0h84r2u2heg0_i, gm_tpl0h84r2u2heg0_s};
const gm_tmpl gm_tpl0qlvf3bfgtip = {2u, gm_tpl0qlvf3bfgtip_p, gm_tpl0qlvf3bfgtip_i, gm_tpl0qlvf3bfgtip_s};
const gm_tmpl gm_tpl0td8lr3f4ven = {2u, gm_tpl0td8lr3f4ven_p, gm_tpl0td8lr3f4ven_i, gm_tpl0td8lr3f4ven_s};
const gm_tmpl gm_tpl11g5872m2bht = {3u, gm_tpl11g5872m2bht_p, gm_tpl11g5872m2bht_i, gm_tpl11g5872m2bht_s};
const gm_tmpl gm_tpl14b6qietv6bm = {2u, gm_tpl14b6qietv6bm_p, gm_tpl14b6qietv6bm_i, gm_tpl14b6qietv6bm_s};
const gm_tmpl gm_tpl1gtefnja08ps = {1u, gm_tpl1gtefnja08ps_p, gm_tpl1gtefnja08ps_i, gm_tpl1gtefnja08ps_s};
const gm_tmpl gm_tpl1lmdkbkeigtd = {4u, gm_tpl1lmdkbkeigtd_p, gm_tpl1lmdkbkeigtd_i, gm_tpl1lmdkbkeigtd_s};
const gm_tmpl gm_tpl2bm4b6itkmm6 = {1u, gm_tpl2bm4b6itkmm6_p, gm_tpl2bm4b6itkmm6_i, gm_tpl2bm4b6itkmm6_s};
const gm_tmpl gm_tpl2utldc2k4g4b = {1u, gm_tpl2utldc2k4g4b_p, gm_tpl2utldc2k4g4b_i, gm_tpl2utldc2k4g4b_s};
const gm_tmpl gm_tpl355ags9581ob = {2u, gm_tpl355ags9581ob_p, gm_tpl355ags9581ob_i, gm_tpl355ags9581ob_s};
const gm_tmpl gm_tpl3v2bn2jpabsf = {1u, gm_tpl3v2bn2jpabsf_p, gm_tpl3v2bn2jpabsf_i, gm_tpl3v2bn2jpabsf_s};
const gm_tmpl gm_tpl48h8br68c917 = {3u, gm_tpl48h8br68c917_p, gm_tpl48h8br68c917_i, gm_tpl48h8br68c917_s};
const gm_tmpl gm_tpl4kmab8oiki17 = {1u, gm_tpl4kmab8oiki17_p, gm_tpl4kmab8oiki17_i, gm_tpl4kmab8oiki17_s};
const gm_tmpl gm_tpl5b7905cihaqk = {1u, gm_tpl5b7905cihaqk_p, gm_tpl5b7905cihaqk_i, gm_tpl5b7905cihaqk_s};
const gm_tmpl gm_tpl75b43ohtuvq2 = {1u, gm_tpl75b43ohtuvq2_p, gm_tpl75b43ohtuvq2_i, gm_tpl75b43ohtuvq2_s};
const gm_tmpl gm_tpl7net9ifts13e = {1u, gm_tpl7net9ifts13e_p, gm_tpl7net9ifts13e_i, gm_tpl7net9ifts13e_s};
const gm_tmpl gm_tpl80jqm34ojjrn = {3u, gm_tpl80jqm34ojjrn_p, gm_tpl80jqm34ojjrn_i, gm_tpl80jqm34ojjrn_s};
const gm_tmpl gm_tpl8nmc259qeh4g = {2u, gm_tpl8nmc259qeh4g_p, gm_tpl8nmc259qeh4g_i, gm_tpl8nmc259qeh4g_s};
const gm_tmpl gm_tpl9cnfdsdh5t3p = {2u, gm_tpl9cnfdsdh5t3p_p, gm_tpl9cnfdsdh5t3p_i, gm_tpl9cnfdsdh5t3p_s};
const gm_tmpl gm_tpl9dqq7pjdmqnl = {1u, gm_tpl9dqq7pjdmqnl_p, gm_tpl9dqq7pjdmqnl_i, gm_tpl9dqq7pjdmqnl_s};
const gm_tmpl gm_tpl9p5406jtlms7 = {1u, gm_tpl9p5406jtlms7_p, gm_tpl9p5406jtlms7_i, gm_tpl9p5406jtlms7_s};
const gm_tmpl gm_tpl9s09hqjpb581 = {6u, gm_tpl9s09hqjpb581_p, gm_tpl9s09hqjpb581_i, gm_tpl9s09hqjpb581_s};
const gm_tmpl gm_tpla67mmq1id2h3 = {7u, gm_tpla67mmq1id2h3_p, gm_tpla67mmq1id2h3_i, gm_tpla67mmq1id2h3_s};
const gm_tmpl gm_tplanqv6irfvtut = {3u, gm_tplanqv6irfvtut_p, gm_tplanqv6irfvtut_i, gm_tplanqv6irfvtut_s};
const gm_tmpl gm_tplaqps986okebj = {2u, gm_tplaqps986okebj_p, gm_tplaqps986okebj_i, gm_tplaqps986okebj_s};
const gm_tmpl gm_tplbaqni0b3abqt = {2u, gm_tplbaqni0b3abqt_p, gm_tplbaqni0b3abqt_i, gm_tplbaqni0b3abqt_s};
const gm_tmpl gm_tplbcublle8ivup = {1u, gm_tplbcublle8ivup_p, gm_tplbcublle8ivup_i, gm_tplbcublle8ivup_s};
const gm_tmpl gm_tplcaav01djo2md = {1u, gm_tplcaav01djo2md_p, gm_tplcaav01djo2md_i, gm_tplcaav01djo2md_s};
const gm_tmpl gm_tplcfgu8t9fq299 = {2u, gm_tplcfgu8t9fq299_p, gm_tplcfgu8t9fq299_i, gm_tplcfgu8t9fq299_s};
const gm_tmpl gm_tplcpp8u5i709mj = {3u, gm_tplcpp8u5i709mj_p, gm_tplcpp8u5i709mj_i, gm_tplcpp8u5i709mj_s};
const gm_tmpl gm_tpld4u3te7g9c5v = {1u, gm_tpld4u3te7g9c5v_p, gm_tpld4u3te7g9c5v_i, gm_tpld4u3te7g9c5v_s};
const gm_tmpl gm_tpldrfjfgoqe179 = {1u, gm_tpldrfjfgoqe179_p, gm_tpldrfjfgoqe179_i, gm_tpldrfjfgoqe179_s};
const gm_tmpl gm_tplebqthjijvqt0 = {2u, gm_tplebqthjijvqt0_p, gm_tplebqthjijvqt0_i, gm_tplebqthjijvqt0_s};
const gm_tmpl gm_tplennc1kv0529b = {2u, gm_tplennc1kv0529b_p, gm_tplennc1kv0529b_i, gm_tplennc1kv0529b_s};
const gm_tmpl gm_tplf2ivserdf4ic = {2u, gm_tplf2ivserdf4ic_p, gm_tplf2ivserdf4ic_i, gm_tplf2ivserdf4ic_s};
const gm_tmpl gm_tplf41j0oji3nkl = {1u, gm_tplf41j0oji3nkl_p, gm_tplf41j0oji3nkl_i, gm_tplf41j0oji3nkl_s};
const gm_tmpl gm_tplfjoq6lkhl94q = {3u, gm_tplfjoq6lkhl94q_p, gm_tplfjoq6lkhl94q_i, gm_tplfjoq6lkhl94q_s};
const gm_tmpl gm_tplh2sse9mocm92 = {2u, gm_tplh2sse9mocm92_p, gm_tplh2sse9mocm92_i, gm_tplh2sse9mocm92_s};
const gm_tmpl gm_tplhciekfbsqng7 = {1u, gm_tplhciekfbsqng7_p, gm_tplhciekfbsqng7_i, gm_tplhciekfbsqng7_s};
const gm_tmpl gm_tplhfdokrq767sh = {1u, gm_tplhfdokrq767sh_p, gm_tplhfdokrq767sh_i, gm_tplhfdokrq767sh_s};
const gm_tmpl gm_tplhlt4sff0mrem = {2u, gm_tplhlt4sff0mrem_p, gm_tplhlt4sff0mrem_i, gm_tplhlt4sff0mrem_s};
const gm_tmpl gm_tplhv087vo2q53g = {1u, gm_tplhv087vo2q53g_p, gm_tplhv087vo2q53g_i, gm_tplhv087vo2q53g_s};
const gm_tmpl gm_tpli7hldrrhg7sr = {1u, gm_tpli7hldrrhg7sr_p, gm_tpli7hldrrhg7sr_i, gm_tpli7hldrrhg7sr_s};
const gm_tmpl gm_tplicmhpq9e6sbc = {2u, gm_tplicmhpq9e6sbc_p, gm_tplicmhpq9e6sbc_i, gm_tplicmhpq9e6sbc_s};
const gm_tmpl gm_tplip1v4sd2477s = {1u, gm_tplip1v4sd2477s_p, gm_tplip1v4sd2477s_i, gm_tplip1v4sd2477s_s};
const gm_tmpl gm_tpljgv7fkm5ld25 = {3u, gm_tpljgv7fkm5ld25_p, gm_tpljgv7fkm5ld25_i, gm_tpljgv7fkm5ld25_s};
const gm_tmpl gm_tpljkde9cibn2er = {2u, gm_tpljkde9cibn2er_p, gm_tpljkde9cibn2er_i, gm_tpljkde9cibn2er_s};
const gm_tmpl gm_tplk4i636uhvi9m = {5u, gm_tplk4i636uhvi9m_p, gm_tplk4i636uhvi9m_i, gm_tplk4i636uhvi9m_s};
const gm_tmpl gm_tplk5t5jhqqvnff = {1u, gm_tplk5t5jhqqvnff_p, gm_tplk5t5jhqqvnff_i, gm_tplk5t5jhqqvnff_s};
const gm_tmpl gm_tplkv0rjh54m5gn = {1u, gm_tplkv0rjh54m5gn_p, gm_tplkv0rjh54m5gn_i, gm_tplkv0rjh54m5gn_s};
const gm_tmpl gm_tpllcjujjfoc50t = {1u, gm_tpllcjujjfoc50t_p, gm_tpllcjujjfoc50t_i, gm_tpllcjujjfoc50t_s};
const gm_tmpl gm_tpllgrg03l2v74l = {3u, gm_tpllgrg03l2v74l_p, gm_tpllgrg03l2v74l_i, gm_tpllgrg03l2v74l_s};
const gm_tmpl gm_tpllv3q9mfan4rg = {1u, gm_tpllv3q9mfan4rg_p, gm_tpllv3q9mfan4rg_i, gm_tpllv3q9mfan4rg_s};
const gm_tmpl gm_tplm1fuloaqnfv4 = {3u, gm_tplm1fuloaqnfv4_p, gm_tplm1fuloaqnfv4_i, gm_tplm1fuloaqnfv4_s};
const gm_tmpl gm_tplm9hj31tk9ip4 = {1u, gm_tplm9hj31tk9ip4_p, gm_tplm9hj31tk9ip4_i, gm_tplm9hj31tk9ip4_s};
const gm_tmpl gm_tplmebto0o1gmln = {9u, gm_tplmebto0o1gmln_p, gm_tplmebto0o1gmln_i, gm_tplmebto0o1gmln_s};
const gm_tmpl gm_tplmv1ukn138isf = {6u, gm_tplmv1ukn138isf_p, gm_tplmv1ukn138isf_i, gm_tplmv1ukn138isf_s};
const gm_tmpl gm_tpln5ioe1q518f0 = {1u, gm_tpln5ioe1q518f0_p, gm_tpln5ioe1q518f0_i, gm_tpln5ioe1q518f0_s};
const gm_tmpl gm_tplnm0o65j76qg0 = {2u, gm_tplnm0o65j76qg0_p, gm_tplnm0o65j76qg0_i, gm_tplnm0o65j76qg0_s};
const gm_tmpl gm_tplnr2qhie4t5ta = {4u, gm_tplnr2qhie4t5ta_p, gm_tplnr2qhie4t5ta_i, gm_tplnr2qhie4t5ta_s};
const gm_tmpl gm_tplntk567ck43t8 = {1u, gm_tplntk567ck43t8_p, gm_tplntk567ck43t8_i, gm_tplntk567ck43t8_s};
const gm_tmpl gm_tplo66949vs5pfo = {2u, gm_tplo66949vs5pfo_p, gm_tplo66949vs5pfo_i, gm_tplo66949vs5pfo_s};
const gm_tmpl gm_tplolnkad3lslng = {1u, gm_tplolnkad3lslng_p, gm_tplolnkad3lslng_i, gm_tplolnkad3lslng_s};
const gm_tmpl gm_tplop2uhngiv88m = {1u, gm_tplop2uhngiv88m_p, gm_tplop2uhngiv88m_i, gm_tplop2uhngiv88m_s};
const gm_tmpl gm_tplq12pt4e53uj3 = {2u, gm_tplq12pt4e53uj3_p, gm_tplq12pt4e53uj3_i, gm_tplq12pt4e53uj3_s};
const gm_tmpl gm_tplqbtvqd3a4g25 = {1u, gm_tplqbtvqd3a4g25_p, gm_tplqbtvqd3a4g25_i, gm_tplqbtvqd3a4g25_s};
const gm_tmpl gm_tplqc6j3ejcoft6 = {2u, gm_tplqc6j3ejcoft6_p, gm_tplqc6j3ejcoft6_i, gm_tplqc6j3ejcoft6_s};
const gm_tmpl gm_tplr0qnbmgpa2m5 = {2u, gm_tplr0qnbmgpa2m5_p, gm_tplr0qnbmgpa2m5_i, gm_tplr0qnbmgpa2m5_s};
const gm_tmpl gm_tplr42r2lp0p4d1 = {3u, gm_tplr42r2lp0p4d1_p, gm_tplr42r2lp0p4d1_i, gm_tplr42r2lp0p4d1_s};
const gm_tmpl gm_tplre5cfbnr7ltv = {2u, gm_tplre5cfbnr7ltv_p, gm_tplre5cfbnr7ltv_i, gm_tplre5cfbnr7ltv_s};
const gm_tmpl gm_tplrmoc4ag4eqrb = {4u, gm_tplrmoc4ag4eqrb_p, gm_tplrmoc4ag4eqrb_i, gm_tplrmoc4ag4eqrb_s};
const gm_tmpl gm_tpls2fio2pf4rdq = {5u, gm_tpls2fio2pf4rdq_p, gm_tpls2fio2pf4rdq_i, gm_tpls2fio2pf4rdq_s};
const gm_tmpl gm_tpls4d1lh9jd2u8 = {1u, gm_tpls4d1lh9jd2u8_p, gm_tpls4d1lh9jd2u8_i, gm_tpls4d1lh9jd2u8_s};
const gm_tmpl gm_tpls92cg1g1s0vs = {6u, gm_tpls92cg1g1s0vs_p, gm_tpls92cg1g1s0vs_i, gm_tpls92cg1g1s0vs_s};
const gm_tmpl gm_tplsf6r3t44km8l = {2u, gm_tplsf6r3t44km8l_p, gm_tplsf6r3t44km8l_i, gm_tplsf6r3t44km8l_s};
const gm_tmpl gm_tplslqhr3ac3ilc = {1u, gm_tplslqhr3ac3ilc_p, gm_tplslqhr3ac3ilc_i, gm_tplslqhr3ac3ilc_s};
const gm_tmpl gm_tplss5b4q9est2j = {7u, gm_tplss5b4q9est2j_p, gm_tplss5b4q9est2j_i, gm_tplss5b4q9est2j_s};
const gm_tmpl gm_tplsvdv91ohcdg6 = {2u, gm_tplsvdv91ohcdg6_p, gm_tplsvdv91ohcdg6_i, gm_tplsvdv91ohcdg6_s};
const gm_tmpl gm_tplt129oa9ve3j7 = {1u, gm_tplt129oa9ve3j7_p, gm_tplt129oa9ve3j7_i, gm_tplt129oa9ve3j7_s};
const gm_tmpl gm_tplt3knh0ehirt7 = {1u, gm_tplt3knh0ehirt7_p, gm_tplt3knh0ehirt7_i, gm_tplt3knh0ehirt7_s};
const gm_tmpl gm_tpltgqv8dvi81j3 = {2u, gm_tpltgqv8dvi81j3_p, gm_tpltgqv8dvi81j3_i, gm_tpltgqv8dvi81j3_s};
const gm_tmpl gm_tplu0h8mqm004jg = {1u, gm_tplu0h8mqm004jg_p, gm_tplu0h8mqm004jg_i, gm_tplu0h8mqm004jg_s};
const gm_tmpl gm_tplu3lqf0bugia3 = {1u, gm_tplu3lqf0bugia3_p, gm_tplu3lqf0bugia3_i, gm_tplu3lqf0bugia3_s};
const gm_tmpl gm_tplu7gkm8brh8s9 = {1u, gm_tplu7gkm8brh8s9_p, gm_tplu7gkm8brh8s9_i, gm_tplu7gkm8brh8s9_s};
const gm_tmpl gm_tplup0bf5d2eopf = {1u, gm_tplup0bf5d2eopf_p, gm_tplup0bf5d2eopf_i, gm_tplup0bf5d2eopf_s};
const gm_tmpl gm_tplv3jh8gpmh6o5 = {1u, gm_tplv3jh8gpmh6o5_p, gm_tplv3jh8gpmh6o5_i, gm_tplv3jh8gpmh6o5_s};
const gm_tmpl_piece gm_tpl04hr2npsr626_p[] = {{gm_sub8b9nf0ms2i, 103}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl0euitr9hdia5_p[] = {{gm_sunidvof1bsgr, 32}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl0pd502nvjf7a_p[] = {{gm_sd2g8d0vbj98u, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl11g5872m2bht_p[] = {{gm_ssqbjasrp4m5m, 21}, {gm_sr29ed60si4re, 6}, {gm_s3p0rbs9hc3lg, 1}, {gm_sq2cbiau3hhqs, 10}};
const gm_tmpl_piece gm_tpl18roma1gvhpk_p[] = {{gm_sf6chrlv83m77, 21}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl1bh0s5ll1m9j_p[] = {{gm_s1dbslfck7laf, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl1blafg62e77e_p[] = {{gm_smooaouqmm8dj, 1}, {gm_svnpflkpvd513, 4}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl1dleoebfmvhp_p[] = {{gm_sl58ekc3js5bs, 29}, {gm_sqdfr4k3vea5p, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl1f5t4gadpj5n_p[] = {{gm_spj9rva8kb5ar, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl1lj3ah7qeq42_p[] = {{gm_slj3uno8bsqk4, 26}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl1rfj3tqlepqo_p[] = {{gm_se5r6p8tqdo82, 30}, {gm_s0njd7khs134c, 30}};
const gm_tmpl_piece gm_tpl1rl5q0rqbd7p_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s9pdhp63uj9bd, 6}};
const gm_tmpl_piece gm_tpl1vbgcviqausd_p[] = {{gm_som0q53jqvjh3, 4}, {gm_slhllpfip1vpg, 2}, {gm_s6t4r831mukdr, 2}};
const gm_tmpl_piece gm_tpl24fci6dm83eo_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sh540objcvln0, 24}, {gm_sdainihnab59h, 9}, {gm_spn9416h484so, 22}};
const gm_tmpl_piece gm_tpl2dnrl9bvc2hf_p[] = {{gm_sdmle2qsm85vh, 10}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tpl2f9qqcjkh29p_p[] = {{gm_sjid6lq0hib0h, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl2jsq89buriv9_p[] = {{gm_safiul2d9a6fl, 13}, {gm_std91rqsr8j0b, 6}};
const gm_tmpl_piece gm_tpl3jlrgt0cfgj3_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sjeopa747clpb, 3}};
const gm_tmpl_piece gm_tpl3kfnv3qa5itp_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_sjfdnh735o7f8, 4}, {gm_srv4dcfccuset, 2}, {gm_srv4dcfccuset, 2}, {gm_s3p0rbs9hc3lg, 1}, {gm_s3p0rbs9hc3lg, 1}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpl470f2c4pe1su_p[] = {{gm_shbtdicfd8lcc, 11}, {gm_sinogveu00g7m, 35}};
const gm_tmpl_piece gm_tpl4auv8p2j34k5_p[] = {{gm_s1tqe3mtj6vb1, 7}, {gm_sp4518j1p6r9m, 18}, {gm_sart7dgrh11an, 13}};
const gm_tmpl_piece gm_tpl4c2mfb3cngak_p[] = {{gm_s4a6o0t8isvlm, 16}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl4isjs1bg9fhg_p[] = {{gm_s84lh6to0er2m, 18}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl4npct6bth1c7_p[] = {{gm_sqc7sit9ajc3t, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl52cv2e94esne_p[] = {{gm_s3bgi1rlb5ne1, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl5hraomijgptv_p[] = {{gm_savb5oasie77f, 36}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl5kgb1tfsut02_p[] = {{gm_spicth2mans91, 43}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl60bi48lvp2qn_p[] = {{gm_sh2urbm3ndjnn, 30}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl7jau285qbbrq_p[] = {{gm_s61fo677jojio, 5}, {gm_sfdbfrv46lihd, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl7r51aeh92b80_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_se08n5d1gngu7, 4}, {gm_s966nqk7bt67q, 18}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl7umtk166uvmr_p[] = {{gm_s8oq9c4aac3sc, 51}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl8ocve2bpsu72_p[] = {{gm_semhbv8seph85, 20}, {gm_sgur19lpnttci, 6}};
const gm_tmpl_piece gm_tpl8ro9atn5p92f_p[] = {{gm_st28rv7m1lueb, 8}, {gm_sq9lne9r5j3u2, 7}, {gm_snd69jrnj9kdn, 41}};
const gm_tmpl_piece gm_tpl8v9uhia7uc1b_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s09tkid4cqjmm, 61}};
const gm_tmpl_piece gm_tpl97n87h3pcva1_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_soscrlodichin, 66}};
const gm_tmpl_piece gm_tpl9aakii48cq8t_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tpl9qchhbq9qlbm_p[] = {{gm_sltjg0sbhbo73, 12}, {gm_s86js8lt7ibda, 47}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpla8nvlakostko_p[] = {{gm_sltao39ib9870, 5}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplao1juqpqjfv9_p[] = {{gm_s7d1996rqhrdg, 8}, {gm_ssj9o3d4o7kbv, 42}};
const gm_tmpl_piece gm_tplbo11j4rgqkb3_p[] = {{gm_s0021gvebcdc4, 8}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tplbo3vrh9egnst_p[] = {{gm_s1sspofc2u3bb, 36}, {gm_s0b795193rvs2, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplc8kkdoja48ii_p[] = {{gm_spg9vuapom9qd, 10}, {gm_st6hj8v0h80gt, 5}, {gm_svkkls864r4t4, 29}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplcfgu8t9fq299_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_saq0ier99o235, 34}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplchb0p7o483ge_p[] = {{gm_s7jhnrnbmkbbq, 38}, {gm_s0m0qaokk51g0, 3}, {gm_s6n3pd0tmqbqc, 9}};
const gm_tmpl_piece gm_tpld3o5iqghffqq_p[] = {{gm_sahahh1a40c5c, 36}, {gm_s7c8ep4trp7bk, 6}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpld611fjclu2tb_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_snset5tb694as, 14}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplde4760b0jcjn_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_scaoagismk27j, 17}, {gm_sfn640udk6pjn, 18}};
const gm_tmpl_piece gm_tpldiv6dh0or1km_p[] = {{gm_siamquc19uikr, 1}, {gm_sao4mumvadoct, 3}, {gm_s3p0rbs9hc3lg, 1}, {gm_sg5gdnrith2i6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpldnca4vvdfaim_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_s4pv32nkh7r2r, 10}, {gm_sg5gdnrith2i6, 2}, {gm_sbtq9nj91tefv, 9}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tple7ipevcaonst_p[] = {{gm_siv9ut075cfnh, 19}, {gm_s0m0qaokk51g0, 3}, {gm_sosa5l0k6d34c, 14}, {gm_s0m0qaokk51g0, 3}, {gm_snkldl7hr4htt, 3}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplett0du8rhujl_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tplff72i6ku4gld_p[] = {{gm_sauf6ibu7mnba, 4}, {gm_s7c9tkfjfc169, 25}, {gm_s73vt0nuu7890, 27}};
const gm_tmpl_piece gm_tplfmqnc0q3nc41_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_slhsp4invn4vh, 6}, {gm_s1suv7j29n0ea, 20}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplfr6nl2m52h19_p[] = {{gm_sjqjrfqdmp9ao, 6}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplg3qra7vq6h3a_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_ssfs2ebod65fl, 37}};
const gm_tmpl_piece gm_tplgj9d3igk4i62_p[] = {{gm_seh85cpcgte1u, 56}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplgp08d1iih1si_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s06sbklusedgl, 4}, {gm_scs1evojsik8q, 3}, {gm_snukl6jqep6jf, 3}, {gm_s6s4ngfaj87vv, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplhdb29uo8rm6j_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_smk9p6gbbjn5j, 8}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplhdrbi5vdvbe6_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sl7cl2p91o7b5, 54}, {gm_sh9g09icusqpu, 13}, {gm_sc8v94psr8v3s, 20}};
const gm_tmpl_piece gm_tplhp4k6h527j3k_p[] = {{gm_ssqmgnf1snsdj, 7}, {gm_s3p0rbs9hc3lg, 1}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpli08vvf1bckj4_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sl3q52ep6aa53, 1}, {gm_slgmqgkioa4u6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplj3bqjm2hv786_p[] = {{gm_suff456m9tthn, 5}, {gm_sspb2rj2e032s, 12}, {gm_s7vt7hgga3uq7, 6}, {gm_s55ncecttah5f, 68}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplj3vfuklvht3e_p[] = {{gm_siamquc19uikr, 1}, {gm_sphmte453j9c2, 11}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpljbk2s8tv7r8m_p[] = {{gm_s2u5posfkbgnq, 1}, {gm_s39mljh669qgl, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpljt2av4h7c132_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s5r4hcdoqsklk, 52}};
const gm_tmpl_piece gm_tpljucl1tv02je2_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_su4aje74nkg3o, 50}};
const gm_tmpl_piece gm_tpllak8uc09ui7v_p[] = {{gm_scon24vlgg660, 14}, {gm_s7hkl5mqa15gm, 4}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tplld9eu0oenqb7_p[] = {{gm_smooaouqmm8dj, 1}, {gm_smooaouqmm8dj, 1}, {gm_sg5gdnrith2i6, 2}, {gm_sb895t6tk3m9h, 5}, {gm_smooaouqmm8dj, 1}, {gm_sg5gdnrith2i6, 2}, {gm_sqvepl1893o5g, 2}};
const gm_tmpl_piece gm_tplllolcurrktr6_p[] = {{gm_st28rv7m1lueb, 8}, {gm_sso11c17kdal7, 34}};
const gm_tmpl_piece gm_tpllnpq25q3a5a4_p[] = {{gm_s25jkl4c6n8uo, 3}, {gm_smooaouqmm8dj, 1}, {gm_sr244s00qj01d, 2}};
const gm_tmpl_piece gm_tplm02t0g7r3hdp_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_ss4337v5p4tj3, 4}};
const gm_tmpl_piece gm_tplmemqaoor78ph_p[] = {{gm_smooaouqmm8dj, 1}, {gm_stoicv7ns2elf, 13}, {gm_smooaouqmm8dj, 1}, {gm_si4l87dl4tpkq, 7}, {gm_ssk4a01soguqj, 6}, {gm_sg5gdnrith2i6, 2}, {gm_s1u0m6lm5u1su, 6}, {gm_sg0bllfnuem4g, 6}};
const gm_tmpl_piece gm_tplmg7sb8gq6o3a_p[] = {{gm_ssqciuageol45, 22}, {gm_s4bf0cndbl9lv, 2}};
const gm_tmpl_piece gm_tpln1d9amhhlm2o_p[] = {{gm_s82d2namrcvs3, 16}, {gm_sk2oomiuvp44g, 5}, {gm_s18e8dmribdb3, 6}, {gm_sgfqei77bsia8, 3}, {gm_scs1evojsik8q, 3}, {gm_sdugj3qbhl84q, 4}, {gm_st52k2850n5r5, 4}, {gm_scs1evojsik8q, 3}, {gm_sdugj3qbhl84q, 4}, {gm_s1asl1e1tvb0k, 1}, {gm_sqvepl1893o5g, 2}};
const gm_tmpl_piece gm_tpln3j86kdpqd7a_p[] = {{gm_ss3tu1f38tkcj, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplnlqa25lecq48_p[] = {{gm_sqf5m57l1ua5d, 26}, {gm_snvncjka7ph6m, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplnunrbopfpsiq_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_scjfd9b9vg99c, 11}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tploik9nhlf98dm_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_scaoagismk27j, 17}, {gm_s6n2mkqhnsjpk, 9}, {gm_splvrmi68lqi3, 110}};
const gm_tmpl_piece gm_tplokvovhsdbec1_p[] = {{gm_skem4050p6ec1, 12}, {gm_si2ks4lbjbojr, 21}};
const gm_tmpl_piece gm_tplolonhuuitsj2_p[] = {{gm_s0u47ivcj4oar, 7}, {gm_s2053hhvf16fm, 1}};
const gm_tmpl_piece gm_tplopmhgmch9jsj_p[] = {{gm_s26o5frgr2hds, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplovoda71kp04v_p[] = {{gm_s5s0ahr8bl55p, 11}, {gm_s43vlgrackeg9, 16}, {gm_saefgd8cv1aea, 19}};
const gm_tmpl_piece gm_tplpa402pkgji4o_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_slk4m2cppcp75, 22}, {gm_srgqm2jeibjrm, 15}, {gm_sr4qbrk6tlgaa, 0}, {gm_sl3q52ep6aa53, 1}, {gm_sja1nsq0b10to, 15}};
const gm_tmpl_piece gm_tplpj1vfgivuj13_p[] = {{gm_sqaek8776r3c5, 8}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplq5eond7ctc71_p[] = {{gm_sql9r2b8fvivt, 9}, {gm_s6mmuahmmssv6, 16}};
const gm_tmpl_piece gm_tplqrah664loq9e_p[] = {{gm_svegu6hi5aibp, 39}, {gm_sebnh3ba276b8, 16}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplr2c5vmkfj9df_p[] = {{gm_soakgb2h3g40i, 3}, {gm_sr4qbrk6tlgaa, 0}, {gm_soakgb2h3g40i, 3}};
const gm_tmpl_piece gm_tplr42r2lp0p4d1_p[] = {{gm_sap058neht0gj, 29}, {gm_s0m0qaokk51g0, 3}, {gm_sl1a3p7pocs82, 14}, {gm_s959ni36b80a4, 3}};
const gm_tmpl_piece gm_tplrgtnfea81qcj_p[] = {{gm_smooaouqmm8dj, 1}, {gm_svhavo13qha5k, 3}, {gm_s1asl1e1tvb0k, 1}, {gm_s3p0rbs9hc3lg, 1}, {gm_sp02qqgucmpt7, 2}, {gm_s1asl1e1tvb0k, 1}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplrh70572i8ko0_p[] = {{gm_somon4mdq0gpj, 23}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplrtd64pk59l9s_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_smooaouqmm8dj, 1}, {gm_s1asl1e1tvb0k, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpls6cg1uub8gfo_p[] = {{gm_snh0sl2cdagdn, 12}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplsgm949s33v8m_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s05qp4ie7rju8, 83}};
const gm_tmpl_piece gm_tplshqe0v39oc52_p[] = {{gm_sajhum9b4nr96, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplsjtf9pg2ergu_p[] = {{gm_sm55k23067j17, 15}, {gm_s554r118mfcao, 33}};
const gm_tmpl_piece gm_tplsnlr78cvfrao_p[] = {{gm_snhmu20ccir10, 15}, {gm_s66k76ki3e7an, 21}};
const gm_tmpl_piece gm_tplsrmv18emggok_p[] = {{gm_sg7h8lcattjoq, 12}, {gm_slbfl8l7n523s, 6}, {gm_sgfqei77bsia8, 3}, {gm_stuutd7nfq21h, 2}, {gm_sqrv3igf39742, 5}};
const gm_tmpl_piece gm_tpltlq1bga4cgul_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s3p0rbs9hc3lg, 1}, {gm_spm33surstj1l, 7}, {gm_svv9dg17rf923, 19}, {gm_sg5gdnrith2i6, 2}, {gm_sjcm18p35o9rq, 3}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplu9669cm9mqja_p[] = {{gm_secrjke11f3ph, 12}, {gm_sm7s66sg1itc1, 3}, {gm_som0q53jqvjh3, 4}, {gm_schn9bp2k5kqo, 46}, {gm_sm77ovo2kgv5k, 41}, {gm_sl1a3p7pocs82, 14}, {gm_smpg2ulm74cim, 8}, {gm_sr4qbrk6tlgaa, 0}, {gm_so25i3en68imc, 26}, {gm_sci406m9tkvjp, 31}};
const gm_tmpl_piece gm_tplua69gir871ue_p[] = {{gm_supi6mhbcvtna, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplue2kdcikgcof_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s2u5posfkbgnq, 1}, {gm_s2u5posfkbgnq, 1}, {gm_s2u5posfkbgnq, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplugrpoiqe5r59_p[] = {{gm_snomh93h7tumv, 13}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpluh0hed6nj59d_p[] = {{gm_siamquc19uikr, 1}, {gm_s4kuq0o3l6f7d, 10}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplv1shkj7l87mc_p[] = {{gm_sns1hl83hdlcn, 4}, {gm_s2k1ajrnodb02, 22}};
const gm_tmpl_piece gm_tplvdft4iatcp17_p[] = {{gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplvj9n0kinsqlj_p[] = {{gm_savi5cfvaqp3a, 14}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplvpjd91cp2l7g_p[] = {{gm_s1bpuqd44lur7, 6}, {gm_sg5gdnrith2i6, 2}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplvq03d5skib28_p[] = {{gm_sb2rimopi40u7, 38}, {gm_s7veagc48806u, 40}};
const gm_type gm_ty929lennfd3 = {.size = 1, .align = 1, .kind = GM_K_BOOL};
const gm_type gm_ty929lennfd3;
const gm_variant gm_tyg63o53ar52_v[] = {{"Convert", 0, NULL}, {"Trunc", 1, NULL}, {"Round", 2, NULL}, {"Floor", 3, NULL}, {"Ceil", 4, NULL}, {"Checked", 5, NULL}, {"Sat", 6, NULL}, {"Bits", 7, NULL}, {"Cast", 8, NULL}, {"Addr", 9, NULL}, {"PtrAdd", 10, NULL}, {"PtrSub", 11, NULL}, {"PtrDiff", 12, NULL}, {"ToPtr", 13, NULL}, {"Tag", 14, NULL}, {"FromTag", 15, NULL}, {"Min", 16, NULL}, {"Max", 17, NULL}, {"Clamp", 18, NULL}, {"Abs", 19, NULL}, {"AbsDiff", 20, NULL}, {"Pow", 21, &gm_tyehhdtpeti1}, {"IsNan", 22, NULL}, {"IsInf", 23, NULL}, {"IsFinite", 24, NULL}, {"SignBit", 25, NULL}, {"Sqrt", 26, NULL}, {"Fma", 27, NULL}, {"Clz", 28, NULL}, {"Ctz", 29, NULL}, {"Popcount", 30, NULL}, {"Bswap", 31, NULL}, {"Rotl", 32, NULL}, {"Rotr", 33, NULL}, {"Bitreverse", 34, NULL}, {"BlackBox", 35, NULL}, {"Get", 36, NULL}, {"GetPtr", 37, NULL}, {"First", 38, NULL}, {"Last", 39, NULL}, {"LaneEq", 40, NULL}, {"LaneNe", 41, NULL}, {"LaneLt", 42, NULL}, {"LaneLe", 43, NULL}, {"LaneGt", 44, NULL}, {"LaneGe", 45, NULL}, {"ReduceSum", 46, NULL}, {"ReduceMin", 47, NULL}, {"ReduceMax", 48, NULL}, {"MaskAll", 49, NULL}, {"MaskAny", 50, NULL}, {"MaskCount", 51, NULL}, {"Select", 52, NULL}, {"LaneMin", 53, NULL}, {"LaneMax", 54, NULL}, {"LaneAbs", 55, NULL}, {"LaneConvert", 56, NULL}, {"LaneMap", 57, &gm_tyasplg81cl8}, {"Shuffle", 58, &gm_tyjmccqdb90f}, {"VecLoad", 59, NULL}, {"VecStore", 60, NULL}, {"CstrLen", 61, NULL}, {"Assert", 62, NULL}, {"Hash", 63, NULL}, {"Unreachable", 64, NULL}, {"AtomicLoad", 65, NULL}, {"AtomicStore", 66, NULL}, {"AtomicSwap", 67, NULL}, {"AtomicCas", 68, NULL}, {"AtomicRmw", 69, &gm_tyov4e3mbi0f}, {"Fence", 70, NULL}, {"VolatileLoad", 71, NULL}, {"VolatileStore", 72, NULL}, {"Sys", 73, &gm_tytje7cegoml}, {"Fiber", 74, NULL}, {"HeapAlloc", 75, NULL}, {"HeapFree", 76, NULL}, {"AllocTag", 77, NULL}, {"AllocCheck", 78, NULL}, {"CopyElems", 79, NULL}, {"Asm", 80, &gm_ty0lfe1s2pin}, {"StaticAssert", 81, &gm_ty467d5cm1d5}, {"Cover", 82, &gm_ty467d5cm1d5}};
const gm_variant gm_tyov4e3mbi0f_v[] = {{"Add", 0, NULL}, {"Sub", 1, NULL}, {"And", 2, NULL}, {"Or", 3, NULL}, {"Xor", 4, NULL}, {"Max", 5, NULL}, {"Min", 6, NULL}};
const uint32_t gm_tpl09kglfpefe8i_i[] = {0u};
const uint32_t gm_tpl0lbbjeb2vhti_i[] = {0u};
const uint32_t gm_tpl1c171lb88cdd_i[] = {0u, 1u};
const uint32_t gm_tpl1gtefnja08ps_i[] = {0u};
const uint32_t gm_tpl1iblbr9u646o_i[] = {0u};
const uint32_t gm_tpl20a4acojo21c_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpl23mh8eh9cef6_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl2bm4b6itkmm6_i[] = {0u};
const uint32_t gm_tpl2d8hdhnr24pd_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tpl3072uo9s1dtn_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl34lim186g0fm_i[] = {0u};
const uint32_t gm_tpl36bd3ievounm_i[] = {0u};
const uint32_t gm_tpl48h8br68c917_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl4gs3p00joms4_i[] = {0u, 1u};
const uint32_t gm_tpl4mli0f9rjihf_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpl4u6a7su34sch_i[] = {0u};
const uint32_t gm_tpl59r7h6035548_i[] = {0u, 1u};
const uint32_t gm_tpl5g9nntg83p8b_i[] = {0u, 1u};
const uint32_t gm_tpl5o8dcpa0ctot_i[] = {0u, 1u};
const uint32_t gm_tpl5st0uuf0quab_i[] = {0u};
const uint32_t gm_tpl63iuq3je4sbr_i[] = {0u};
const uint32_t gm_tpl65nnuqtjd24o_i[] = {0u};
const uint32_t gm_tpl6ilgcn878l74_i[] = {0u, 1u};
const uint32_t gm_tpl6nq0lpkkblpc_i[] = {0u};
const uint32_t gm_tpl75bcbum1ntsg_i[] = {0u};
const uint32_t gm_tpl7eeir95ujdvn_i[] = {0u};
const uint32_t gm_tpl7medmjmlvos1_i[] = {0u, 1u, 2u, 3u, 4u, 5u};
const uint32_t gm_tpl83abvjjf3qd1_i[] = {0u, 1u};
const uint32_t gm_tpl85p2q6ermgga_i[] = {0u};
const uint32_t gm_tpl86cip41cbf58_i[] = {0u};
const uint32_t gm_tpl94ei7rae9si2_i[] = {0u};
const uint32_t gm_tpl955dep3t38r6_i[] = {0u};
const uint32_t gm_tpl98etevndg0cj_i[] = {0u};
const uint32_t gm_tpl9h9af6kus46t_i[] = {0u, 1u};
const uint32_t gm_tpl9j9ok1j4nn18_i[] = {0u, 1u};
const uint32_t gm_tpl9jr09voqi3gh_i[] = {0u, 1u};
const uint32_t gm_tpl9qg0hlqqke7e_i[] = {0u};
const uint32_t gm_tpl9s09hqjpb581_i[] = {0u, 1u, 2u, 3u, 4u, 5u};
const uint32_t gm_tpl9tc0m8fi3g9d_i[] = {0u, 1u};
const uint32_t gm_tpla15qb3qedeuc_i[] = {0u};
const uint32_t gm_tplbahlus43pmvm_i[] = {0u, 1u};
const uint32_t gm_tplbn5t5stpvru2_i[] = {0u};
const uint32_t gm_tplbr0fi0201tmv_i[] = {0u};
const uint32_t gm_tplbsca6gthghjf_i[] = {0u};
const uint32_t gm_tplbtbg1u9fmcqq_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplc3kt858jk6jp_i[] = {0u, 1u};
const uint32_t gm_tplcaav01djo2md_i[] = {0u};
const uint32_t gm_tplcic56vj3l629_i[] = {0u};
const uint32_t gm_tplcl8es85s6qrs_i[] = {0u};
const uint32_t gm_tplcq107msa8ktd_i[] = {0u};
const uint32_t gm_tpld0t14m1fld0p_i[] = {0u, 1u, 2u};
const uint32_t gm_tplda38mm1025ge_i[] = {0u, 1u, 2u, 3u, 4u, 5u};
const uint32_t gm_tpldff5gvnhne6f_i[] = {0u, 1u, 2u};
const uint32_t gm_tpldjbe17jhbkpg_i[] = {0u, 1u};
const uint32_t gm_tpldrfjfgoqe179_i[] = {0u};
const uint32_t gm_tpldti7tgsj3l9q_i[] = {0u, 1u};
const uint32_t gm_tple2rp5rj93mkj_i[] = {0u};
const uint32_t gm_tple4upi9cflnq6_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplei8v65ors6jc_i[] = {0u, 1u};
const uint32_t gm_tpleoop2177apvc_i[] = {0u, 1u};
const uint32_t gm_tplf4oacobin5qk_i[] = {0u};
const uint32_t gm_tplf8er2klne616_i[] = {0u};
const uint32_t gm_tplfc08pcq9dhq6_i[] = {0u, 1u};
const uint32_t gm_tplfcgbc9s4hmn7_i[] = {0u};
const uint32_t gm_tplffkq5ue6h7rm_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
const uint32_t gm_tplfl5r6601183l_i[] = {0u};
const uint32_t gm_tplg7bh9jeid72i_i[] = {0u};
const uint32_t gm_tplgcrr0tu9874k_i[] = {0u};
const uint32_t gm_tplgemvjdfjljaa_i[] = {0u, 1u};
const uint32_t gm_tplgs2l60s65hm3_i[] = {0u};
const uint32_t gm_tplgvbr3vejj74c_i[] = {0u, 1u};
const uint32_t gm_tplh0kdp5708tgq_i[] = {0u};
const uint32_t gm_tplh7231rl9trm2_i[] = {0u, 1u, 2u};
const uint32_t gm_tplhmairn7oibac_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u};
const uint32_t gm_tplhn9hprqs819d_i[] = {0u};
const uint32_t gm_tplhon5g1o0vuqi_i[] = {0u, 1u};
const uint32_t gm_tplhu7i2tvvfh15_i[] = {0u, 1u, 2u};
const uint32_t gm_tplie81jau63n6g_i[] = {0u};
const uint32_t gm_tplj8iadiucj6so_i[] = {0u};
const uint32_t gm_tplj92mk62u70db_i[] = {0u};
const uint32_t gm_tpljhar6jmbtsst_i[] = {0u};
const uint32_t gm_tpljicvo0ni8aba_i[] = {0u};
const uint32_t gm_tplk34ojqiaho84_i[] = {0u};
const uint32_t gm_tplk5o80h990o81_i[] = {0u};
const uint32_t gm_tplk5t5jhqqvnff_i[] = {0u};
const uint32_t gm_tplk83cf3bpuiqb_i[] = {0u};
const uint32_t gm_tplkkvn546crm4n_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplkv6ui0mqvq2g_i[] = {0u, 1u, 2u};
const uint32_t gm_tpll1i6ur15ms5i_i[] = {0u, 1u};
const uint32_t gm_tplld08lp5kl07i_i[] = {0u};
const uint32_t gm_tplm2m182mt5ljj_i[] = {0u};
const uint32_t gm_tplm3f7baid20me_i[] = {0u, 1u};
const uint32_t gm_tplmtg92v4cesud_i[] = {0u};
const uint32_t gm_tpln1554he46gkc_i[] = {0u};
const uint32_t gm_tpln8ctpo9hror7_i[] = {0u};
const uint32_t gm_tplnitaca1dp5li_i[] = {0u, 1u};
const uint32_t gm_tplo4mr4l55q97r_i[] = {0u, 1u};
const uint32_t gm_tploj0vopi0n43l_i[] = {0u};
const uint32_t gm_tploj4uaptg6qnk_i[] = {0u, 1u};
const uint32_t gm_tplomf18g3j9if3_i[] = {0u};
const uint32_t gm_tplotost4gd8bha_i[] = {0u, 1u};
const uint32_t gm_tplqar620s2vpg7_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
const uint32_t gm_tplqf2cda88og0i_i[] = {0u, 1u};
const uint32_t gm_tplqfgvjo31nom3_i[] = {0u, 1u, 2u};
const uint32_t gm_tplqrah664loq9e_i[] = {0u, 1u};
const uint32_t gm_tplrcmh11f2ocju_i[] = {0u};
const uint32_t gm_tplrcurrnquacuf_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u};
const uint32_t gm_tplrffp652tkgo2_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplrivm1rlf1ps3_i[] = {0u, 1u};
const uint32_t gm_tplrp4haaemeqja_i[] = {0u, 1u, 2u};
const uint32_t gm_tplrspdrqd1dpc8_i[] = {0u, 1u};
const uint32_t gm_tpls3fe84n8lo7i_i[] = {0u, 1u};
const uint32_t gm_tpls4d220l28nlk_i[] = {0u};
const uint32_t gm_tpls5b11sf5gq9o_i[] = {0u};
const uint32_t gm_tplsf1c5t6qt3u3_i[] = {0u};
const uint32_t gm_tplsiiifs0eedvi_i[] = {0u, 1u};
const uint32_t gm_tplsjtf9pg2ergu_i[] = {0u};
const uint32_t gm_tplso01g2paq6o6_i[] = {0u, 1u};
const uint32_t gm_tplt5hu85fc8epn_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tplt68r4kihke54_i[] = {0u, 1u, 2u};
const uint32_t gm_tpltpd3ap2ql1ip_i[] = {0u, 1u};
const uint32_t gm_tplts0sr46c58jl_i[] = {0u};
const uint32_t gm_tplu7oi9kuo0adg_i[] = {0u};
const uint32_t gm_tplubod389iot6h_i[] = {0u};
const uint32_t gm_tpluc9sbr66n218_i[] = {0u, 1u, 2u};
const uint32_t gm_tplugmq0iksatjg_i[] = {0u};
const uint32_t gm_tpluhfd3hvppdji_i[] = {0u, 1u};
const uint32_t gm_tplut4ib9ov72oe_i[] = {0u};
const uint32_t gm_tpluttgpct24aib_i[] = {0u};
const uint32_t gm_tplv516qd7l6o3q_i[] = {0u, 1u, 2u};
const uint32_t gm_tplvf2bf6hftf4i_i[] = {0u, 1u};
const uint32_t gm_tplviqo6u6r0sr3_i[] = {0u, 1u};
const uint32_t gm_tplvkf7arb107q2_i[] = {0u};
const uint8_t gm_s00989rdgst9u[20] = " has no directory `";
const uint8_t gm_s00gjhnalb1qv[36] = "`: use letters, digits, `-` and `_`";
const uint8_t gm_s01mi6tf86obv[13] = "handler body";
const uint8_t gm_s024ihiaifr93[15] = "{} = {} && {};";
const uint8_t gm_s02uuedsru1qu[9] = "} else {";
const uint8_t gm_s03lpulaucbu1[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\362\001\000\000\026\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000";
const uint8_t gm_s03pmf867dcsr[2] = "s";
const uint8_t gm_s055t7hpo88bt[4] = "))\012";
const uint8_t gm_s088gqlg957uo[6] = ", so ";
const uint8_t gm_s08ekjvgo9fro[101] = "` might be the same effect for some type arguments, so performing either would be ambiguous (\302\24716.6)";
const uint8_t gm_s09jc0n36v9gm[97] = "`, which is nested in the function being checked; it cannot run before that function is complete";
const uint8_t gm_s0etnbnk4miq5[5] = "1 is";
const uint8_t gm_s0gu0jo9fq1ca[11] = "slice-type";
const uint8_t gm_s0jm0vg43i745[11] = "` (\302\24720.2)";
const uint8_t gm_s0km823b6osb3[13] = "\377\377\377\377\000\000\000\000\000\000\000\000";
const uint8_t gm_s0nkgagpnq9jc[58] = "` has no arithmetic: only many-pointers `[^]T` do (\302\2475.5)";
const uint8_t gm_s0u0p8jkqpsv1[8] = ", {});\012";
const uint8_t gm_s0ucactj6hj0a[4] = ".gm";
const uint8_t gm_s0uf57646drba[14] = "        case ";
const uint8_t gm_s167ok8osvuve[11] = "attributed";
const uint8_t gm_s16mf4cvajh59[13] = "static void ";
const uint8_t gm_s17l5kif0frp2[19] = "GM_LD({}, fb->arg)";
const uint8_t gm_s1cqejvllajb6[3] = "&&";
const uint8_t gm_s1fjspgm800ug[7] = "r->a{}";
const uint8_t gm_s1gd0gkbe5tu9[55] = "comparing a `bool` with `true` or `false` is redundant";
const uint8_t gm_s1hud20mvj11b[37] = "` is not an operator; compare with `";
const uint8_t gm_s1i7slukookmu[12] = "interrupted";
const uint8_t gm_s1ie3s345dm40[27] = "((uint8_t *)&{})[{}] = {};";
const uint8_t gm_s1jbdr25pcb3i[14] = "%{} = fill {}";
const uint8_t gm_s1k3lfare8sei[4] = "ret";
const uint8_t gm_s1llcn0p4qesk[20] = "a `with` expression";
const uint8_t gm_s1n6uk6814mmb[53] = "    if ((a[{}] != 0) != (b[{}] != 0)) return false;\012";
const uint8_t gm_s1nt8u0pgo6vn[17] = " = gm_args(&argn";
const uint8_t gm_s1pandl18hjn0[61] = "`{}` is not a label of an enclosing `while`, `for` or `with`";
const uint8_t gm_s1ti14uv3rrcf[65] = "does not target the architecture of `{}` (it does not define {})";
const uint8_t gm_s1vtc1o6iel66[2] = "w";
const uint8_t gm_s209nrdd09rdp[4] = "): ";
const uint8_t gm_s23qsffuqlc1v[25] = "a float, for a precision";
const uint8_t gm_s25euhn4i5qkh[72] = "; a scalar is not added to {} (\302\2477.4.7): write the other operand as one";
const uint8_t gm_s276s8986o4r3[23] = "compiler/check/func.gm";
const uint8_t gm_s27ouot20tc5m[25] = "compiler/lint/present.gm";
const uint8_t gm_s2bp59r6c3pt6[23] = "` into named arguments";
const uint8_t gm_s2d2ar24n4if2[6] = "exit ";
const uint8_t gm_s2dcdt3n36d4p[8] = "--dir=.";
const uint8_t gm_s2fu5nkek0lrq[7] = "Assert";
const uint8_t gm_s2ochj4lgmhue[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\314\000\000\000%\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_s2okn0skhqnvu[32] = "cannot assign to an element of ";
const uint8_t gm_s2p0dacnmvuhi[7] = "` for ";
const uint8_t gm_s2red66k9r4fi[10] = ", false, ";
const uint8_t gm_s31o876d6qij5[10] = "coerce {}";
const uint8_t gm_s3231a310mran[10] = "expect_eq";
const uint8_t gm_s3876s0nhcdfo[99] = "`@{}` needs the layout of `{}`, which depends on this value: the declarations form a cycle (\302\2473.4)";
const uint8_t gm_s38njmu4cr525[13] = "gm_attr_kind";
const uint8_t gm_s3emon0a31046[81] = "; a type parameter that the arguments determine is written by name, if at all: `";
const uint8_t gm_s3hlvhp72jeiv[12] = "local_const";
const uint8_t gm_s3jl3afv8nlvv[4] = "evn";
const uint8_t gm_s3loluab89lqt[11] = " \077 sizeof ";
const uint8_t gm_s3mmpurhmitlr[46] = "` in [lint] must be \"allow\", \"warn\" or \"deny\"";
const uint8_t gm_s3n5bjpleiarb[7] = "argp{}";
const uint8_t gm_s3ndmfc1bk22f[23] = "(((unsigned __int128)(";
const uint8_t gm_s3ojik1morrbi[9] = "field {}";
const uint8_t gm_s3opc5ivp6e66[4] = "Tag";
const uint8_t gm_s3qo1c5fkeqfk[67] = "`{}` is a built-in method of slices and strings: write `s.{}(\342\200\246)`";
const uint8_t gm_s3vn3760p6488[103] = "math vectors have no method `.{}`; they have `dot`, `cross`, `norm`, `min`, `max` and `abs` (\302\24721.3){}";
const uint8_t gm_s40upcq09aufa[39] = "`.norm` needs float elements, not `{}`";
const uint8_t gm_s46dkmn8ur11l[60] = "set the level of rules (comma-separated; all is every rule)";
const uint8_t gm_s4bo4cvutg59h[16] = "complex numbers";
const uint8_t gm_s4eilu97sooh1[10] = "Allocator";
const uint8_t gm_s4gdu7v0gur88[11] = "match : {}";
const uint8_t gm_s4j8rhn4svm30[7] = "TMPDIR";
const uint8_t gm_s4kqn01dbredu[28] = "float literals of this type";
const uint8_t gm_s4kuq0o3l6f7d[11] = " = fn-ref ";
const uint8_t gm_s4m15r14tbnat[61] = "invalid {} name `{}`: it is the name of the standard library";
const uint8_t gm_s4mij084uljjf[26] = "({} > 0 || ({} & 1) == 0)";
const uint8_t gm_s4nl1jq95dg65[111] = "a `#trap_hook` function must have the type `fn(msg: string, loc: SourceLocation) -> never needs Sys` (\302\2473.7.1)";
const uint8_t gm_s4oloc6hggc50[92] = "` is a symbol of this implementation's runtime, so it cannot name a foreign member (\302\24723.5)";
const uint8_t gm_s4pk73sg6h8qk[6] = "gm_xr";
const uint8_t gm_s4s2nr1qc94po[17] = "inst->with_label";
const uint8_t gm_s4t8kk8uuclvr[27] = "((uint8_t *)GM_LD(void *, ";
const uint8_t gm_s4u5fhgj0jfnp[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\002\001\000\000\014\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_s50pmptf3idds[2] = "b";
const uint8_t gm_s51l7o5mjnhu8[9] = "a number";
const uint8_t gm_s52fbscfsv3pa[6] = "; amb";
const uint8_t gm_s52ieemclane5[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\236\000\000\000\032\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_s54ap4j24utib[69] = "if ({}) gm_trap_pow((const uint8_t *)&{}, {}, {}, (uint32_t){}, {});";
const uint8_t gm_s55kqbt4f7gau[4] = "ptr";
const uint8_t gm_s57l2tgcfr4se[5] = "&gt;";
const uint8_t gm_s5a3gd19ers1k[19] = "gm_fs_set_modified";
const uint8_t gm_s5c0ruf0vg8mu[3] = "we";
const uint8_t gm_s5e527dkpbusp[5] = "\"{}\"";
const uint8_t gm_s5q2tpl7krt8a[9] = "git = {}";
const uint8_t gm_s5s4ac22q52tj[14] = "  = note: {}\012";
const uint8_t gm_s5s65385he1ib[24] = "(({}){}__builtin_inf())";
const uint8_t gm_s5uhm93lpt8jn[11] = "({} {} {})";
const uint8_t gm_s61fo677jojio[6] = "gm_ld";
const uint8_t gm_s626miklr3ggr[34] = "cannot assign to an element of {}";
const uint8_t gm_s64eu4ogr34e2[62] = "internal error: `@fiber` in a clause of a `with` without `on`";
const uint8_t gm_s65abge0a786m[10] = ")gm_rotl(";
const uint8_t gm_s6899ib4u9fpt[24] = "unexpected character U+";
const uint8_t gm_s68c5jkfvg5dd[86] = " will be evaluated again before each later input, because its definition has effects\012";
const uint8_t gm_s69d1mv9hu0dg[42] = "`#{}` applies only to a function (\302\24720.2)";
const uint8_t gm_s6a2c11qu9scr[13] = "unsigned_abs";
const uint8_t gm_s6bs3s06kr0hi[10] = "gm_bswap(";
const uint8_t gm_s6dmfj45vc0q0[11] = "atomic_min";
const uint8_t gm_s6g8mt9e5cjjr[11] = "operator `";
const uint8_t gm_s6hha9j1e16ru[90] = "this slice of a temporary array is passed to a call that may keep it after the array ends";
const uint8_t gm_s6hiq18oa7ku0[32] = "str: replace of an empty string";
const uint8_t gm_s718drvtpg9kd[49] = "internal error: a jump target in a missing frame";
const uint8_t gm_s73cunn109dv3[36] = "cannot propagate an error of type `";
const uint8_t gm_s73uhqfbphcpe[7] = "// {}\012";
const uint8_t gm_s77aa5te2lvj3[6] = "{}_{}";
const uint8_t gm_s78l3ok4on4sg[36] = "F.h.clause_lit = {} && env != NULL;";
const uint8_t gm_s7aoh8j3iph16[51] = "`@bswap` needs an integer of whole bytes, not `{}`";
const uint8_t gm_s7d4tu5fkkg12[11] = "ThreadJoin";
const uint8_t gm_s7dkbj8tvt9e3[7] = "clause";
const uint8_t gm_s7jf1bc28tar9[38] = "a bounds check of an index of type {}";
const uint8_t gm_s7lb197di7d72[37] = "`{}` takes two `{}`, not {} and {}{}";
const uint8_t gm_s7ni4ei2275l9[13] = "` losslessly";
const uint8_t gm_s7p349cm5h5jf[20] = "` needs a frame of ";
const uint8_t gm_s7sn5glap1gkf[7] = "op_sub";
const uint8_t gm_s7tf2msne28g3[19] = "repository mirrors";
const uint8_t gm_s7un5bnoqg548[4] = "```";
const uint8_t gm_s7veagc48806u[41] = "); const uint8_t *d = GM_LD(void *, v);\012";
const uint8_t gm_s821smstdk4rs[14] = "%{} = load {}";
const uint8_t gm_s82i64ms8kog2[94] = "this `with` handles `ctl` operations, so it needs a stack: `with h on stack { \342\200\246 }` (\302\24714.1)";
const uint8_t gm_s84bfepk867cd[18] = "payload_offset = ";
const uint8_t gm_s84k1uo35omt8[10] = "#undef B\012";
const uint8_t gm_s867gr7sqck87[26] = "{} {{\012#define F (*gm_Fp)\012";
const uint8_t gm_s8dujv4dnroq0[24] = "to close the constraint";
const uint8_t gm_s8eg138piktj1[12] = "uint8_t pe[";
const uint8_t gm_s8j8keh1qgf0a[7] = "try {}";
const uint8_t gm_s8lp1qs73nh4k[17] = "the C compiler `";
const uint8_t gm_s8pst1ovjhtbk[71] = "internal error: an atomic access through a value that is not a pointer";
const uint8_t gm_s8tkvhpqkq5en[8] = "x86_64h";
const uint8_t gm_s9236sqnmc8uc[9] = ", col);\012";
const uint8_t gm_s93p2i934ld6p[5] = "<fn>";
const uint8_t gm_s9a8dkouqsfgp[9] = ") == 0;\012";
const uint8_t gm_s9c60h0qendav[9] = "float -0";
const uint8_t gm_s9da4sg04ql4e[60] = "{} has no commit `{}` on a branch or tag (required by `{}`)";
const uint8_t gm_s9ekhmse13161[5] = "X\002\000\000";
const uint8_t gm_s9fugl5o6l2co[8] = "gm_unit";
const uint8_t gm_s9idfla83gb22[59] = "internal error: a comparison reached the integer operators";
const uint8_t gm_s9jm1k2lfgtr0[9] = "c interp";
const uint8_t gm_s9nl6jkck8c25[8] = "gm_ll{}";
const uint8_t gm_s9npjns7bnqaj[19] = "foreign function `";
const uint8_t gm_s9o8gb20t6jtp[9] = "VecStore";
const uint8_t gm_s9pl5vgrnfja0[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000*\001\000\000\005\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_s9q38m4ka466q[32] = ")` is not a lossless conversion";
const uint8_t gm_s9r83c0uo3ifr[8] = "(({})0)";
const uint8_t gm_sa4nk3mrdf2d0[36] = "the element and the members of `in`";
const uint8_t gm_saalafjdd3tnp[9] = "#define ";
const uint8_t gm_sacua9qc75rts[7] = "FsOpen";
const uint8_t gm_sader8j76ksps[27] = "gm_ord[GM_LD(uint8_t, {})]";
const uint8_t gm_sadot8stl6s4j[36] = "(uintptr_t)strlen((const char *){})";
const uint8_t gm_sagc0kltjb8fc[12] = "branch `{}`";
const uint8_t gm_sagfhrncf3ga2[11] = "(({}){:e})";
const uint8_t gm_salbbur153hif[21] = "memcpy(&{}, {}, {});";
const uint8_t gm_sam3pd3469drv[55] = "tuple elements must be either all named or all unnamed";
const uint8_t gm_samlp70hmirme[3] = "hi";
const uint8_t gm_san3q9ul7snd0[22] = "`rev` of dependency `";
const uint8_t gm_sangi489oaklo[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000-\000\000\000M\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sap058neht0gj[30] = "GM_ST(uintptr_t, (uint8_t *)&";
const uint8_t gm_sapbk38ibps8q[28] = "static inline __attribute__";
const uint8_t gm_saqo50iv9jjmk[9] = ".size = ";
const uint8_t gm_sart7dgrh11an[14] = " *)fb->frame;";
const uint8_t gm_satil70qt25hp[52] = "`.shuffle` makes a vector of one lane per index: {}";
const uint8_t gm_savnnonh6sa0m[14] = "gamma_lldb.py";
const uint8_t gm_sb0j8pu1hhl5q[52] = "{}'s {} is a constant, not a compile-time parameter";
const uint8_t gm_sb3efvtufksie[37] = "a function type like `fn(args) -> T`";
const uint8_t gm_sb4hpcnufos1f[12] = "loongarch64";
const uint8_t gm_sb5efqp7fp176[49] = "    uint8_t e = v[{}] != 0; h = gm_mix64(h, e);\012";
const uint8_t gm_sb5pfucassrk8[34] = "deadlock: every thread is blocked";
const uint8_t gm_sb77prkml0t3e[15] = "`, a copy of `";
const uint8_t gm_sb7jq1p8shp96[113] = "a type parameter cannot be inferred from the function `{}`: it converts to either kind of function type (\302\24716.2)";
const uint8_t gm_sba439nre06r4[6] = ") != ";
const uint8_t gm_sba70pvo3j68t[16] = "gm_exact_digits";
const uint8_t gm_sbbavs945cjhp[32] = "a compile-time function literal";
const uint8_t gm_sbckpj1jsdt28[13] = "WrapOptional";
const uint8_t gm_sbdqlrmtee4eo[66] = "`{}` is constructed with named arguments only: `{}(field: value)`";
const uint8_t gm_sbe61ep7tock5[5] = "gm_c";
const uint8_t gm_sbek54n4a6h2o[7] = "LaneGe";
const uint8_t gm_sbfr1qrcvp774[59] = "`main` may only perform ambient effects, but performs `{}`";
const uint8_t gm_sbfupubonn4fv[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\334\001\000\0000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_sbg96utvan892[34] = "internal error: native operation ";
const uint8_t gm_sbge807fo2bb9[19] = "%{} = tag-is {} {}";
const uint8_t gm_sbgthe7nu79mi[18] = "gm_fmt_hex_escape";
const uint8_t gm_sbh3nm117l7ra[35] = "` does not apply to the lanes of `";
const uint8_t gm_sbm73uc3nf0c8[6] = "`.{}`";
const uint8_t gm_sbqhd9c69ol7k[17] = "handler literals";
const uint8_t gm_sbqm20163frvc[5] = "\\n\"\012";
const uint8_t gm_sbs808v3vjc2c[8] = "] != b[";
const uint8_t gm_sbsjrunf4id25[12] = "effect body";
const uint8_t gm_sbtoph8phkseu[5] = "curl";
const uint8_t gm_sc2qutubarto1[24] = "compiler/load/loader.gm";
const uint8_t gm_sc2u7rsavnn5v[47] = "{}: warning: public {} `{}` has no doc comment";
const uint8_t gm_sc3s0n32fb0ju[11] = "gm_resolve";
const uint8_t gm_sc48cegnkinfm[12] = "(({}){}ULL)";
const uint8_t gm_sc5r1r2po4512[6] = "### `";
const uint8_t gm_sc6celu0e642t[6] = ".wasm";
const uint8_t gm_sc6qhprasnd4f[25] = "missing textDocument.uri";
const uint8_t gm_sc9huk8sdsgbe[10] = ", but a `";
const uint8_t gm_scaoagismk27j[18] = "` of dependency `";
const uint8_t gm_scbkk1tper8o2[54] = "this address of `{}` is stored where it outlives `{}`";
const uint8_t gm_scblcr348l6he[31] = "-finline-functions-called-once";
const uint8_t gm_sclthecvcggbv[15] = "build metadata";
const uint8_t gm_scm3afv9qr78v[5] = ") {\012";
const uint8_t gm_scmp8kk3f4q6t[46] = "` is constructed with named arguments only: `";
const uint8_t gm_scn17gb24fbic[16] = "{}\012version = 1\012";
const uint8_t gm_scnmhrk4ghd85[17] = "{} (os error {})";
const uint8_t gm_scon24vlgg660[15] = "  called from ";
const uint8_t gm_scqusp7lgvp02[4] = "i{}";
const uint8_t gm_scrt7dkgq6215[10] = "get_bytes";
const uint8_t gm_scujoh2vgr9aq[22] = "compiler/lir/lower.gm";
const uint8_t gm_scvhv26j3m3dm[18] = "unknown command `";
const uint8_t gm_scvuqmspcpb1m[6] = "basic";
const uint8_t gm_sd3f2q088bhvc[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\257\001\000\000\020\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_sd3nblniu5ogu[25] = "-Wno-free-nonheap-object";
const uint8_t gm_sd5qvckqda7oh[13] = "((uint8_t)1)";
const uint8_t gm_sd7hg1lsdu11b[15] = "the column `a.";
const uint8_t gm_sd912tgjus40m[28] = "` does not apply to a union";
const uint8_t gm_sdbih7ptecvdq[39] = "gm_mix128(h, (unsigned __int128)GM_LD(";
const uint8_t gm_sdc2n4tsqs8nn[4] = "0e0";
const uint8_t gm_sddo97gu3reaa[72] = "`{}` names a type; a local type may not have the name of a type (\302\2474.6)";
const uint8_t gm_sdf319kmi28bu[17] = "unknown format `";
const uint8_t gm_sdfa87va44k3m[24] = ", not to the lanes of `";
const uint8_t gm_sdikuksp636di[12] = "(void)inst;";
const uint8_t gm_sdl557c0gjrbv[10] = "coro_main";
const uint8_t gm_sdopt7plujc7n[16] = "resolveProvider";
const uint8_t gm_sdru6cvprml58[29] = "profile `{}` must be a table";
const uint8_t gm_sdtjos7av9cn7[22] = "unexpected character ";
const uint8_t gm_sdtv0pa4ahtps[52] = "_Static_assert(sizeof({}) == {}, \"debugger view\");\012";
const uint8_t gm_sdvrfjnpjiquk[20] = "cannot fetch {}: {}";
const uint8_t gm_se3l2nlomk2pj[9] = "fs_write";
const uint8_t gm_se5bopd2dbamm[30] = "`---` needs a type annotation";
const uint8_t gm_se71q7absh67v[12] = "aligned({})";
const uint8_t gm_se9jjcum4l3k9[32] = "textDocument/publishDiagnostics";
const uint8_t gm_seakt349s3hl5[8] = "{} {{}}";
const uint8_t gm_sebojpbfjfgaf[3] = "__";
const uint8_t gm_seg12e843a462[9] = "restrict";
const uint8_t gm_segii2f2mu6lb[23] = "if ({} && {} == {}) {{";
const uint8_t gm_segmk88bjc49r[7] = "extend";
const uint8_t gm_sehat3g6ic3fn[50] = "`!p` matches the error of a result, but this is `";
const uint8_t gm_sejqabkgvk3eh[12] = "; otherwise";
const uint8_t gm_sel2d9vtaa0ba[19] = "</tbody>\012</table>\012";
const uint8_t gm_senhrlorjabcl[3] = "-l";
const uint8_t gm_seodvmll3gsjn[4] = "Asm";
const uint8_t gm_sep6flfnofnin[41] = "\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000P\002\000\000P\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_serklfkp9210u[154] = "    if (file != NULL) {{ const void *fp = file; uintptr_t fl = strlen(file); memcpy(loc + {}, &fp, sizeof fp); memcpy(loc + {} + {}, &fl, sizeof fl); }}\012";
const uint8_t gm_sesj9tqqcsas0[6] = ".used";
const uint8_t gm_sespjge6ouk5e[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000X\003\000\000\022\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_seuimcklr1us2[16] = "# Module `{}`\012\012";
const uint8_t gm_sevktc6u3mas1[52] = "internal error: no installing `with` for this break";
const uint8_t gm_sf372rql24e3n[17] = "an untyped float";
const uint8_t gm_sf5n57voafb5e[13] = "gm_popcount(";
const uint8_t gm_sf6chrlv83m77[22] = "cannot read LDFLAGS: ";
const uint8_t gm_sf90mcj0opih1[8] = "{} : {}";
const uint8_t gm_sfcjpmkuggqo3[10] = "cpu_count";
const uint8_t gm_sfcm4qg50vk74[10] = "gm_strlen";
const uint8_t gm_sfg1v6bqcbbav[10] = "` (\302\2473.3)";
const uint8_t gm_sfid9en449upv[11] = "cond-match";
const uint8_t gm_sfivm1csbt9ar[29] = "(uint8_t)__builtin_isnan({})";
const uint8_t gm_sflmt70utbvg4[20] = "Too many open files";
const uint8_t gm_sfm035nfvvou3[29] = "` is not among them (\302\24711.7)";
const uint8_t gm_sfplrdppnb8cn[23] = " {\012#define F (*gm_Fp)\012";
const uint8_t gm_sfq37b3iv0k5h[27] = "` have the same tag value ";
const uint8_t gm_sfsqcni8p8345[42] = "Slice {{ lo: {}, hi: {}, inclusive: {} }}";
const uint8_t gm_sftp5tlacbd12[8] = " ns/run";
const uint8_t gm_sfvci5f269lfp[13] = "<p><a href=\"";
const uint8_t gm_sg0qvpnm8k9d3[13] = "gm_load_uint";
const uint8_t gm_sg0tsttb54h9c[85] = "`{}` is a symbol of this implementation's runtime, so it cannot be exported (\302\24723.5)";
const uint8_t gm_sg1t2674vj2eg[32] = "--sysroot={}/share/wasi-sysroot";
const uint8_t gm_sg5o5gf2u71g7[9] = "(void *)";
const uint8_t gm_sg6k7269e3cfu[34] = "gm_cancel_cont(inst->fw, {}, {});";
const uint8_t gm_sg7h8lcattjoq[13] = "for (size_t ";
const uint8_t gm_sgahfuanpfk2r[60] = "({} < 0 || (({}) == 0 && __builtin_signbit({}))) \077 -{} : {}";
const uint8_t gm_sgcf9cdlaf08n[6] = "{}:{}";
const uint8_t gm_sgchj7ubg6umm[20] = "` is declared twice";
const uint8_t gm_sgdafdv2n5c25[5] = "mode";
const uint8_t gm_sgdqi8a9n3f12[15] = "goto gm_fprop;";
const uint8_t gm_sges6pnr4b3t3[19] = "order_generic_args";
const uint8_t gm_sgouu8ejfsufr[10] = "gm_hb_tab";
const uint8_t gm_sgqejiat83g1e[47] = "` is passed to a call that may keep it after `";
const uint8_t gm_sgsvgcmirhojs[28] = "type `{}` has no field `{}`";
const uint8_t gm_sgujtgfflv5dk[8] = "integer";
const uint8_t gm_sh6hkhojsmti9[48] = "invalid unicode escape: expected `{` after `\\u`";
const uint8_t gm_sh89ku1sauvot[11] = "--coverage";
const uint8_t gm_sh929ihdsnkrv[27] = "`errdefer` at compile time";
const uint8_t gm_sha303gigke52[10] = " at line ";
const uint8_t gm_shcdet5ggdj93[99] = "also run those of the modules of the package\012that nothing imports (every directory with .gm files)";
const uint8_t gm_shecus79pkigk[19] = "if (GM_PTR_TAGGED(";
const uint8_t gm_shegcik3mpeut[12] = "fs_truncate";
const uint8_t gm_shjad4rl88uoh[11] = "gm_win_err";
const uint8_t gm_shoqeplqudrjn[17] = "\000\000\000\000\000\000\000\000\363\006\000\000\000\000\000\000";
const uint8_t gm_shs03k71jk157[19] = "this destructuring";
const uint8_t gm_shs0aou723lfc[25] = "dependency `{}` ({}): {}";
const uint8_t gm_sht0efblvn5a6[8] = "`{}` {}";
const uint8_t gm_shud0j6jb8ucc[37] = "symbol names with special characters";
const uint8_t gm_si1kalsj77bue[41] = " are out of bounds of the allocation at ";
const uint8_t gm_si4l87dl4tpkq[8] = ".code, ";
const uint8_t gm_si5tqjfc8g60q[30] = "__atomic_store_n({}, {}, {});";
const uint8_t gm_si7spjb0bqq6r[9] = "{}.{}.{}";
const uint8_t gm_si9h59pjqftat[5] = "step";
const uint8_t gm_siampvcugondr[14] = "gm_futex_wake";
const uint8_t gm_sictcqjuumcng[63] = "`{}` is a method of `Instant` or `Duration`: write `x.{}(\342\200\246)`";
const uint8_t gm_sidgpvfsv94q1[56] = "this compares a value with itself, so it is always `{}`";
const uint8_t gm_sidlbibo929o4[12] = ", subdir = ";
const uint8_t gm_sifvl6bvepiob[9] = "({} > 0)";
const uint8_t gm_sihclrdudbhkh[9] = "#global ";
const uint8_t gm_sijeantqk7jl2[11] = "({} >= {})";
const uint8_t gm_siji82hgrl5rk[12] = "{}@{}:{}:{}";
const uint8_t gm_sik0eji8h20le[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000\207\000\000\000\030\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_sim8pu6stc37a[11] = "Unresolved";
const uint8_t gm_simovkrp5fi0r[16] = "attribute `#{}`";
const uint8_t gm_sioam4mlrjuar[52] = "`.shuffle` takes lane indices, written positionally";
const uint8_t gm_siq422vplpm2h[54] = "an integer pattern needs an integer, but this is `{}`";
const uint8_t gm_sisvmc7d4huip[14] = "compile_error";
const uint8_t gm_sitmblfjqgruh[9] = " = wrap ";
const uint8_t gm_siv9ut075cfnh[20] = "memcpy((uint8_t *)&";
const uint8_t gm_sj294dvmirbmr[47] = "unexpected key or value, expected newline, `#`";
const uint8_t gm_sj4nhb5mobdan[37] = ", .with_frame = &F.h, .with_label = ";
const uint8_t gm_sj5iuta9ebe8p[81] = "\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_sj9co2vp4uudk[13] = "check range ";
const uint8_t gm_sjbjq95a18dio[44] = ", .kind = GM_NATIVE, .next = cx}; cx = &amb";
const uint8_t gm_sjblck9bpb6r8[28] = "unterminated string literal";
const uint8_t gm_sjgqjs06244vq[21] = "    {} r; {}(&r{});\012";
const uint8_t gm_sjsb0lbgdebd6[45] = "` must appear in the type the handler is for";
const uint8_t gm_sjsosmp24eqn0[36] = "error: `:type` needs an expression\012";
const uint8_t gm_sjuj50mlj6mg0[14] = "the condition";
const uint8_t gm_sjv2au7jm0m3j[66] = "if (!{}) gm_trap(\"float to integer conversion out of range\", {});";
const uint8_t gm_sk30fte1u6en9[33] = "cannot call a value of type `{}`";
const uint8_t gm_sk4i403fuak6l[19] = "referencesProvider";
const uint8_t gm_sk5s72mtujop8[5] = "expr";
const uint8_t gm_skaimjkudbui9[39] = "{} have different types: `{}` and `{}`";
const uint8_t gm_skes2iuk38v0s[12] = "{:.3} s/run";
const uint8_t gm_skkiv5ia4t4au[39] = "    print(\"{}\\n\", @type_name(@type_of(";
const uint8_t gm_skl8iadfmm1va[44] = "(\342\200\246)`, or gather its values with `collect(";
const uint8_t gm_sklnpt5kca0im[15] = "assert-fail {}";
const uint8_t gm_skmas107kf6co[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\262\001\000\000\031\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_sknsei1oiabnd[13] = "version `{}`";
const uint8_t gm_skpk7m7mpsr06[6] = " == (";
const uint8_t gm_skqas99ada0fa[26] = "compiler/interp/vm_gen.gm";
const uint8_t gm_skrht660q9e9a[6] = "{:.1}";
const uint8_t gm_sksprenpp79ih[11] = " inclusive";
const uint8_t gm_sl1iofp814r0q[3] = "-w";
const uint8_t gm_sl23n0qkt3j4u[28] = ": no such file or directory";
const uint8_t gm_sl95r5djfj1r0[13] = "if ({}{}) {{";
const uint8_t gm_sldsondfm6ju2[9] = "{}\012{}\012{}";
const uint8_t gm_slf586p66t9db[6] = "{}.gm";
const uint8_t gm_slg8371oger0u[32] = "stack overflow on a fiber stack";
const uint8_t gm_slginip33cenb[14] = "fs_next_entry";
const uint8_t gm_sllgvbfukep8p[24] = "this operator on floats";
const uint8_t gm_sln1aneof3dn0[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\300\006\000\000-\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_slnhe1h36jmub[9] = "ClockNow";
const uint8_t gm_slnuijmj5p1vk[19] = "GM_FAST_MATH_ATTR ";
const uint8_t gm_slov3heeajhqn[12] = "FsNextEntry";
const uint8_t gm_slqtudd4gk04i[9] = "--silent";
const uint8_t gm_sluiboi2c0d5n[58] = "`else e =>` captures the error of a result, but this is `";
const uint8_t gm_slurd8qe898d5[10] = "gm_checks";
const uint8_t gm_sm017vofcj044[12] = "ThreadSpawn";
const uint8_t gm_sm1m5ho3m4iqp[12] = "inherit-env";
const uint8_t gm_sm26kv3hefp6i[13] = "gamma lint: ";
const uint8_t gm_sm2tcicavq8d5[8] = "Acquire";
const uint8_t gm_sm35km518bh68[52] = "a `.` in a format spec is followed by the precision";
const uint8_t gm_sm54129hqmnsd[16] = " too large for ";
const uint8_t gm_sm55k23067j17[16] = "cannot rename `";
const uint8_t gm_sm5be4659ulci[2] = ">";
const uint8_t gm_sm6j3q0d4n2jp[3] = ":\012";
const uint8_t gm_sm77ovo2kgv5k[42] = "));\012    GM_ST(uintptr_t, (uint8_t *)&a + ";
const uint8_t gm_sm8m7jldttgpj[38] = "-Werror=implicit-function-declaration";
const uint8_t gm_sm8si2u0a1u17[5] = "rest";
const uint8_t gm_sm9lq3mvrfi15[7] = "mailto";
const uint8_t gm_smbjf525bk9a0[20] = " has no `.gm` files";
const uint8_t gm_smdet9tftopqs[14] = "cannot cast `";
const uint8_t gm_smedhknhdrf46[11] = "array-type";
const uint8_t gm_smjeoupcicact[10] = ", align: ";
const uint8_t gm_smk08v391jpq1[23] = "the builtin function `";
const uint8_t gm_smnfu5n5pum1l[2] = "n";
const uint8_t gm_smnot3vdfhqsc[6] = "Stdio";
const uint8_t gm_smqkqp8ljbr20[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\221\001\000\000\035\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_smr5gecavco41[9] = "wasmtime";
const uint8_t gm_smuveuet82l27[11] = "__x86_64__";
const uint8_t gm_sn1go4g2tvcok[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000b\001\000\000\033\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_sn4o5fodoh7op[12] = "collect_dir";
const uint8_t gm_sn581p3lbgrjg[9] = "make-err";
const uint8_t gm_sn5qnjkjnmu95[23] = "textDocument/didChange";
const uint8_t gm_sna94ju8c8em3[48] = "`lint` must be a table of rule names and levels";
const uint8_t gm_snekrjln06tfl[14] = "cannot write ";
const uint8_t gm_sni0seheggmp0[30] = "the function `{}` has no body";
const uint8_t gm_snjdrq8n0vphi[12] = "gamma-cache";
const uint8_t gm_snl36eh5psull[7] = "trap: ";
const uint8_t gm_snpgki1p28d77[5] = "simd";
const uint8_t gm_snr5o5tak6405[4] = ");\012";
const uint8_t gm_sntd8vrhhlrk6[15] = "gm_alloc_check";
const uint8_t gm_sntolo76g17vv[7] = "darwin";
const uint8_t gm_snu0769072h68[9] = "DirEntry";
const uint8_t gm_snukl6jqep6jf[4] = " : ";
const uint8_t gm_so2qoj4e0o0al[6] = "F.l{}";
const uint8_t gm_so4acijgbtr2b[34] = "misaligned pointer cast: address ";
const uint8_t gm_so4gnucc3fl4v[7] = "eprint";
const uint8_t gm_so4pc0b7vpiib[106] = "(GM_LD(uintptr_t, {} + {}) != 0 && {}({}.code, {}.env, {}, GM_LD(void *, {}), GM_LD(uintptr_t, {} + {})))";
const uint8_t gm_so4t3hmedbui5[7] = "change";
const uint8_t gm_so87vf8a6o4mm[11] = "older-than";
const uint8_t gm_so8qnkabacf8o[5] = "attr";
const uint8_t gm_soashnodb7lsu[23] = "\">All modules</a></p>\012";
const uint8_t gm_socbkiss431o8[18] = "(_)` to ignore it";
const uint8_t gm_sodsqu1iesiv9[3] = "i8";
const uint8_t gm_sog6ie61bmisu[7] = "reader";
const uint8_t gm_soiad5r9c65ru[37] = ", 0xcbf29ce484222325ULL ^ (uint64_t)";
const uint8_t gm_soiet16j0e8ar[34] = "`fill:` must be the only argument";
const uint8_t gm_sojc67e7a19h1[6] = "<{}>\012";
const uint8_t gm_somdu21umm1af[16] = "gm_altstack_key";
const uint8_t gm_sor30tetra4nt[5] = "Pow(";
const uint8_t gm_sovhd8e8hvflr[52] = "gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;";
const uint8_t gm_sp1g56u3ibrtp[23] = "GM_HUGE_FN GM_LONG_FN ";
const uint8_t gm_sp4m2sm1j5k9e[23] = " = (unsigned __int128)";
const uint8_t gm_spc0s79b28n7m[9] = ", plus: ";
const uint8_t gm_spc8ncjo0q4pi[11] = "\"{}\" in {}";
const uint8_t gm_spg9vuapom9qd[11] = "tag value ";
const uint8_t gm_spj65mdvs9ei4[20] = "passed to be stored";
const uint8_t gm_spj9t9b8lqlpp[8] = "variant";
const uint8_t gm_spkrf9e7cr6h4[7] = "#line ";
const uint8_t gm_spm33surstj1l[8] = "; bool ";
const uint8_t gm_spm9ae4g2b2fd[14] = "    return a[";
const uint8_t gm_spsaka1h4bsob[12] = "gm_env_exit";
const uint8_t gm_spsh7mv5lihcc[34] = "    if (b.data) gm_buf_free(&b);\012";
const uint8_t gm_spsm6pt8hfeqv[8] = " ms/run";
const uint8_t gm_sq0r7ud0kv45f[8] = " tests\012";
const uint8_t gm_sq2e6tj3vu7de[10] = "supervise";
const uint8_t gm_sq44fbs78td4m[33] = "compile-time evaluation failed: ";
const uint8_t gm_sq52gnep1cqbp[45] = "GM_ST(void *, (uint8_t *)&{}, (void *)({}));";
const uint8_t gm_sq5rlbq4gbs5v[27] = "inference inside this type";
const uint8_t gm_sq6h1gpkivf38[41] = "`@{}` needs {} of a known type, not `{}`";
const uint8_t gm_sq7ijigadotf8[17] = "_t, (uint8_t *)&";
const uint8_t gm_sqdfr4k3vea5p[4] = "`: ";
const uint8_t gm_sqdqshat9srld[37] = "a declaration starts here, so this `";
const uint8_t gm_sqjfo9aus0n4k[13] = "gm_alloc_tag";
const uint8_t gm_sql9r2b8fvivt[10] = "variant `";
const uint8_t gm_sqnjthtqs97m4[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000\350\000\000\000V\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sqo8h5kg9id12[86] = "`else` binds more loosely than `..`; parenthesize the bound: `lo..(x else d)` (\302\247B.1)";
const uint8_t gm_sqoq2k5v12g25[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\0001\007\000\000\035\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_sqro42saen22n[108] = "no C compiler for the target `{}`: `{}-gcc` and `clang` are not on PATH; install a cross compiler or set CC";
const uint8_t gm_squacltecdsom[5] = ")))\012";
const uint8_t gm_sr3mniof9fusf[12] = "((void *)0)";
const uint8_t gm_sr44oukgimglr[23] = " is not a power of two";
const uint8_t gm_sra3ncr12fqek[56] = "`---` may only initialize a variable, field or argument";
const uint8_t gm_srdnkonbfvqdq[9] = "children";
const uint8_t gm_srdq954rpedg3[93] = "a function with a calling convention other than C must have effects at most `{Sys}` (\302\24719.2)";
const uint8_t gm_srgqm2jeibjrm[16] = "`; call it as `";
const uint8_t gm_srnejjd9belbk[18] = "loop_with_pattern";
const uint8_t gm_srni26bloa03c[12] = "-Wl,-rpath,";
const uint8_t gm_srqm47b4oe5lm[72] = "a range bound must be a literal or a qualified constant such as `m.MAX`";
const uint8_t gm_srsbjtfrhs8q1[16] = "` needs `where ";
const uint8_t gm_srugm21uf2sb0[9] = "Shutdown";
const uint8_t gm_srv8c2gciqkuj[48] = "a tuple pattern needs a tuple, but this is `{}`";
const uint8_t gm_ss28cntkmcf91[1780] = "usage: gamma <command> [options] <file>\012\012commands:\012  lex     print the tokens of source files\012  parse   check the syntax of source files (--dump prints the syntax tree)\012  modules load a program's modules without checking them and print the module graph\012  decls   resolve a program's declarations without checking bodies and print them\012  tir     check a program and print the typed program the checker produces\012  lir     check a program and print its functions lowered for the back ends (LIR)\012  check   type-check a program (a .gm file or a module directory)\012  doc     write the API documentation of a module as Markdown or HTML (gamma doc --help)\012  lint    check a package with its tests and report the findings of the lint rules\012  fix     check a program and apply the fixes its diagnostics suggest\012  build   compile a program to a native executable via the system C compiler\012  run     compile a program via C and run it, or run it in the interpreter (--backend=interp);\012          arguments after the file go to the program\012  test    run the test declarations of a module and of the modules of its package\012          that it imports, compiled into one executable via C\012  bench   run the bench declarations the same way and report the time per run\012          (compiled in fast mode unless --mode is given)\012  new, init, fetch, update, add, remove, cache\012          the package manager (gamma help)\012  fmt     format source files in place: gamma fmt [--check] <file or directory>... (\"-\" filters stdin to stdout)\012  repl    interactive read-eval-print loop on the interpreter: gamma repl [--quiet] [file]\012          (loads the file's declarations)\012  lsp     run the language server on standard input and output\012  help    print this message\012  version print the compiler version\012\012options:\012";
const uint8_t gm_ss4rn3b9npsij[14] = "local imports";
const uint8_t gm_ss6esonundp29[56] = "    {} ta = GM_LD({}, a + {}), tb = GM_LD({}, b + {});\012";
const uint8_t gm_ss869lnvmj3m8[6] = "Fiber";
const uint8_t gm_ss9l5l0jp0vfb[14] = "element lists";
const uint8_t gm_ssa2crodel6ih[29] = "the label `{}` is never used";
const uint8_t gm_ssgna3ki6j1do[12] = "gm_snprintf";
const uint8_t gm_ssj49pp1637ia[7] = "mk_pat";
const uint8_t gm_ssjmv3618n779[10] = "ReduceMin";
const uint8_t gm_ssk4a01soguqj[7] = ".env, ";
const uint8_t gm_ssk6sk59g0skj[16] = "%{} = {} {}, {}";
const uint8_t gm_sskc9227875lf[9] = "` makes ";
const uint8_t gm_ssn662sheb2q9[45] = "use the [profile.<name>] table of gamma.toml";
const uint8_t gm_ssnnp97m1kei1[8] = "wrap_as";
const uint8_t gm_sstrtuk2b4jdg[10] = "slice-len";
const uint8_t gm_ssu2vb7lj6d81[21] = "write-deferred {} {}";
const uint8_t gm_ssv1lstjje7r5[7] = " ret;\012";
const uint8_t gm_st0lgjd5uosa2[6] = "utf-8";
const uint8_t gm_st1c10ieohbb4[34] = ";\012    return gm_unw.kind != 0;\012}\012";
const uint8_t gm_st1k7cra2pjic[71] = "    if (gm_unw.kind == GM_BREAK) {{ gm_unw.kind = GM_NONE; goto {}; }}";
const uint8_t gm_st3ja0nqi37bv[2] = "/";
const uint8_t gm_st3nlhq2i34b5[64] = "    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto ";
const uint8_t gm_st3o1r587rnip[18] = "window/logMessage";
const uint8_t gm_st4c97p4hek1p[27] = "compiler/syntax/literal.gm";
const uint8_t gm_st547kc8a6rg5[16] = "the parameter `";
const uint8_t gm_st6q7g4cbd5nc[10] = "gm_format";
const uint8_t gm_st7a1scb61101[16] = "    gm_debug = ";
const uint8_t gm_st7fm661apctp[17] = "invalid suffix `";
const uint8_t gm_st9og0pk896pv[12] = "gm_sys_read";
const uint8_t gm_stabrbc330nqe[18] = " is defined twice";
const uint8_t gm_stbt78k7uqc0m[24] = "GM_API int gm_checks = ";
const uint8_t gm_ste817enr6egu[95] = "`{}` has a calling convention other than C, so it cannot be a second-class `fn` value (\302\24719.2)";
const uint8_t gm_stfs4nd3l87tt[48] = "`.{}` matches a union variant, but this is `{}`";
const uint8_t gm_stg3p1oju1o9b[23] = "`continue` to a `with`";
const uint8_t gm_stla84k0knhb0[12] = "temp {}: {}";
const uint8_t gm_stm1tprvbfoip[58] = "attribute `#{}` does not apply to a function pointer type";
const uint8_t gm_stm46orgt9h8k[317] = "GM_NOINLINE {} {{\012    gm_buf b = {{0}};\012    uint8_t sb[GM_VIEW_SCRATCH];\012    const void *d;\012    size_t n;\012    if (s) {{ gm_format_spec(&b, t, v, s); d = b.data; n = b.len; }} else n = gm_format_view(&b, sb, t, v, &d);\012    bool u = n != 0 && {}(code, env, cx, d, n);\012    if (b.data) gm_buf_free(&b);\012    return u;\012}}\012";
const uint8_t gm_stn1kfabf6qis[13] = "{:.2} ns/run";
const uint8_t gm_stou1l3qp844f[10] = "FromNever";
const uint8_t gm_stq6t1a4hvefn[14] = "**/gamma.toml";
const uint8_t gm_su2ima1nbr01d[52] = " or any parent directory (`gamma init` creates one)";
const uint8_t gm_su3jp69t4gf2q[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\077\002\000\000\011\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_su88ia0hdam4d[66] = "misaligned pointer cast: address {:#x} is not aligned to {} bytes";
const uint8_t gm_suc4jg0r7eog6[59] = "` is not a library name: use letters, digits and `_ . + -`";
const uint8_t gm_sucfks6hb8bf7[44] = "if the function must take it, name it `_{}`";
const uint8_t gm_suctgno4l10a5[9] = "null-pat";
const uint8_t gm_sufk3vqnaden1[3] = "}'";
const uint8_t gm_sujtulcrs1du3[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\027\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000";
const uint8_t gm_sukbpodfceoe1[9] = "{}:{}:{}";
const uint8_t gm_sums36kmdot2i[109] = "the type of this value was already fixed as `{}` at an earlier use (\302\2476.1); annotate it, e.g. `n: usize = 0`";
const uint8_t gm_suo1v9f675r0d[5] = "-I{}";
const uint8_t gm_surrhqnv1nojd[46] = "`.{}` is not a valid memory order {} (\302\24719.6)";
const uint8_t gm_sv37ng458lg2i[50] = "`try` from a handler clause to a function literal";
const uint8_t gm_sv48vll3qortv[40] = "`#trap_hook` applies only to a function";
const uint8_t gm_svc1hcla4tqfb[6] = " for ";
const uint8_t gm_svfte0jrv51gc[30] = "expected `bool`, found `{}`{}";
const uint8_t gm_svg1jvtfj7fgp[6] = "F.ret";
const uint8_t gm_svgoe41piv341[7] = "select";
const uint8_t gm_svi913vrigpr0[107] = "unknown package `{}`: a path starts with `./`, `../`, `std/`, or a dependency listed in gamma.toml (\302\2473.2)";
const uint8_t gm_svj3dp4ur2bph[11] = "{}{} | {}\012";
const uint8_t gm_svkmjs6p8bhtg[9] = "lib-dirs";
const uint8_t gm_svknltl22rlbs[9] = " (\302\2476.5)";
const uint8_t gm_svm3nf8j0t7ot[12] = "gm_spec_pad";
const uint8_t gm_svn9drpksr8pl[34] = "cannot index a value of type `{}`";
const uint8_t gm_svpi81lkmjsgl[7] = "loop @";
const uint8_t gm_svq6hhfarqhmv[10] = "gm_trap(\"";
const uint8_t gm_svqnr5kmirn0g[45] = "    uintptr_t n = GM_LD(uintptr_t, a + {});\012";
const uint8_t gm_svrjcmg2c5dio[78] = "an immutable binding cannot be `---`: it could never be given a value (\302\2474.4)";
const uint8_t gm_svugg9vs1s4ss[58] = "`@variant_name` takes one union value: `@variant_name(x)`";
const uint8_t gm_svuonpsui0ane[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000>\001\000\000S\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
uint8_t gm_c0a4m567j25[48] __attribute__((aligned(8)));
uint8_t gm_c0on8osiuo6[5] __attribute__((aligned(1)));
uint8_t gm_c100d3qhpv5[6] __attribute__((aligned(1)));
uint8_t gm_c17ggp4avhs[5] __attribute__((aligned(1)));
uint8_t gm_c1fmjj3efs7[6] __attribute__((aligned(1)));
uint8_t gm_c28e6r6fc17[3] __attribute__((aligned(1)));
uint8_t gm_c499h6aasrh[19] __attribute__((aligned(1)));
uint8_t gm_c49andki61b[4] __attribute__((aligned(4)));
uint8_t gm_c4b4noc2csc[8] __attribute__((aligned(1)));
uint8_t gm_c4q1r8o7m8d[24] __attribute__((aligned(1)));
uint8_t gm_c53j3ci82e6[16] __attribute__((aligned(1)));
uint8_t gm_c540sftfgo9[15] __attribute__((aligned(1)));
uint8_t gm_c6135o3s80k[13] __attribute__((aligned(1)));
uint8_t gm_c6ge57r72i4[11] __attribute__((aligned(1)));
uint8_t gm_c6j8kar54p4[2] __attribute__((aligned(1)));
uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
uint8_t gm_c706grja04t[14] __attribute__((aligned(1)));
uint8_t gm_c741neqb2fg[3] __attribute__((aligned(1)));
uint8_t gm_c7jr3ergrd7[8] __attribute__((aligned(1)));
uint8_t gm_c87f6jpfo1e[12] __attribute__((aligned(1)));
uint8_t gm_c9h924ckf95[4] __attribute__((aligned(1)));
uint8_t gm_c9vr8sjt2in[15] __attribute__((aligned(1)));
uint8_t gm_ca3v3u0frmk[45] __attribute__((aligned(1)));
uint8_t gm_ca9a9qf5uf3[11] __attribute__((aligned(1)));
uint8_t gm_cajrm882b9k[83] __attribute__((aligned(1)));
uint8_t gm_cb104msc6as[53] __attribute__((aligned(1)));
uint8_t gm_cbsek8oicg9[8] __attribute__((aligned(1)));
uint8_t gm_ccq673m4u5l[5] __attribute__((aligned(1)));
uint8_t gm_ccu87ihd5ti[15] __attribute__((aligned(1)));
uint8_t gm_cd16n891kj6[15] __attribute__((aligned(1)));
uint8_t gm_cd1ptb89qfb[5] __attribute__((aligned(1)));
uint8_t gm_cdn7nhdpp7m[4] __attribute__((aligned(1)));
uint8_t gm_cdp0830p9p2[7] __attribute__((aligned(1)));
uint8_t gm_ce9o6afpeun[2] __attribute__((aligned(1)));
uint8_t gm_cem22767mo4[4] __attribute__((aligned(4)));
uint8_t gm_ceq9kh1e7ck[7] __attribute__((aligned(1)));
uint8_t gm_cfgjuuq94tk[10] __attribute__((aligned(1)));
uint8_t gm_cg583o7kdgj[10] __attribute__((aligned(1)));
uint8_t gm_cgoiuafipe4[18] __attribute__((aligned(1)));
uint8_t gm_cgsmfcq937e[16] __attribute__((aligned(8)));
uint8_t gm_cgsmfcq937e_6[16] __attribute__((aligned(8)));
uint8_t gm_ch5e65ekkon[10] __attribute__((aligned(1)));
uint8_t gm_chc2gqtscde[4] __attribute__((aligned(1)));
uint8_t gm_chhcticfeqe[11] __attribute__((aligned(1)));
uint8_t gm_ci7nr7ssi4g[5] __attribute__((aligned(1)));
uint8_t gm_cigg5fkjomv[7] __attribute__((aligned(1)));
uint8_t gm_cka5emo0tle[16] __attribute__((aligned(1)));
uint8_t gm_ckcutgns62h[10] __attribute__((aligned(1)));
uint8_t gm_cl2ue8a5800[7] __attribute__((aligned(1)));
uint8_t gm_clvd1m34v3b[11] __attribute__((aligned(1)));
uint8_t gm_cmml7o4if0q[8] __attribute__((aligned(8)));
uint8_t gm_cndfnv7mbeh[8] __attribute__((aligned(8)));
uint8_t gm_cni4niqbaug[15] __attribute__((aligned(1)));
uint8_t gm_cnr6mqd3ogi[57] __attribute__((aligned(1)));
uint8_t gm_co3qlejdntu[16] __attribute__((aligned(8)));
uint8_t gm_co88ci116uf[39] __attribute__((aligned(1)));
uint8_t gm_cobksqr67t4[8] __attribute__((aligned(8)));
uint8_t gm_cooifs2q83a[11] __attribute__((aligned(1)));
uint8_t gm_cq2ccurlbf8[14] __attribute__((aligned(1)));
uint8_t gm_cq8smm747r5[2] __attribute__((aligned(1)));
uint8_t gm_cr8vi7458l3[5] __attribute__((aligned(1)));
uint8_t gm_crku57l46si[13] __attribute__((aligned(1)));
uint8_t gm_cs1qbc2gaqs[16] __attribute__((aligned(8)));
uint8_t gm_csik9eh7ggu[70] __attribute__((aligned(1)));
uint8_t gm_csirqbmq8rr[10] __attribute__((aligned(1)));
uint8_t gm_ct4bgh8l6qv[16] __attribute__((aligned(1)));
uint8_t gm_ctds4kc1b5o[1] __attribute__((aligned(1)));
uint8_t gm_ctf8kltriqb[2] __attribute__((aligned(1)));
uint8_t gm_ctlupmqng6n[8] __attribute__((aligned(1)));
uint8_t gm_ctvsbqjhdl3[12] __attribute__((aligned(4)));
uint8_t gm_cu1bqtgc9l0[13] __attribute__((aligned(1)));
uint8_t gm_cuqp9vdk0eh[9] __attribute__((aligned(1)));
uint8_t gm_cv7ipurvl4r[14] __attribute__((aligned(1)));
uint8_t gm_k00ckei9k4e[40] __attribute__((aligned(8)));
uint8_t gm_k0480fp26lr[40] __attribute__((aligned(8)));
uint8_t gm_k093ni2ahjb[40] __attribute__((aligned(8)));
uint8_t gm_k2boi01efds[40] __attribute__((aligned(8)));
uint8_t gm_k3501um71nb[40] __attribute__((aligned(8)));
uint8_t gm_k4g6hon27vb[40] __attribute__((aligned(8)));
uint8_t gm_k6dqjucsagv[40] __attribute__((aligned(8)));
uint8_t gm_k6qm3ruhsta[40] __attribute__((aligned(8)));
uint8_t gm_ka55ftc91cb[40] __attribute__((aligned(8)));
uint8_t gm_kh9bgce5chr[40] __attribute__((aligned(8)));
uint8_t gm_kihi8omjj3o[40] __attribute__((aligned(8)));
uint8_t gm_kjq4sbqctak[40] __attribute__((aligned(8)));
uint8_t gm_klqhd3q4fc0[40] __attribute__((aligned(8)));
uint8_t gm_km7ke510mna[40] __attribute__((aligned(8)));
uint8_t gm_knhubf3blct[40] __attribute__((aligned(8)));
uint8_t gm_knm7lgi0bll[40] __attribute__((aligned(8)));
uint8_t gm_kold22d1smp[40] __attribute__((aligned(8)));
uint8_t gm_kpi7tiio4tn[40] __attribute__((aligned(8)));
uint8_t gm_kqiqqsf7tg0[40] __attribute__((aligned(8)));
uint8_t gm_krbc9rueul4[40] __attribute__((aligned(8)));
uint8_t gm_kst3i6v7714[40] __attribute__((aligned(8)));
void *const gm_hl0tthikq3q4[] = {(void *)gm_f_for_body_g9kn84c};
void *const gm_hl83gfnr17rd[] = {(void *)gm_f_for_body_f3mct6g};
void *const gm_hl87701o8eo1[] = {(void *)gm_f_for_body_0m0ntf2};
void *const gm_hleu8p83p90p[] = {(void *)gm_f_for_body_7fmdm88};
void *const gm_hlgmnqv27ug4[] = {(void *)gm_f_for_body_1qm1hvv};
void *const gm_hloaeaafij99[] = {(void *)gm_f_for_body_i2koblr};

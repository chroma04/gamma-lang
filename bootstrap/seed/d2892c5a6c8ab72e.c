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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { gm_tl874t2atnlh3 f0; gm_t88rcucg1b7uk f1; } gm_trlabqsjjp0ds;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint64_t v1; } f1; } gm_t833u93mr4iob;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t76123gfet73g v0; unsigned __int128 v1; } f1; } gm_tdlro53ljvkl0;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
_Static_assert(sizeof(gm_trlabqsjjp0ds) == 16 && _Alignof(gm_trlabqsjjp0ds) == 4 && offsetof(gm_trlabqsjjp0ds, f0) == 0 && offsetof(gm_trlabqsjjp0ds, f1) == 12, "layout of `(Span, Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t833u93mr4iob) == 32 && _Alignof(gm_t833u93mr4iob) == 8 && offsetof(gm_t833u93mr4iob, f0) == 0 && offsetof(gm_t833u93mr4iob, f1) == 8, "layout of `Flow!u64`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlro53ljvkl0) == 48 && _Alignof(gm_tdlro53ljvkl0) == 16 && offsetof(gm_tdlro53ljvkl0, f0) == 0 && offsetof(gm_tdlro53ljvkl0, f1) == 16, "layout of `Flow!u128`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; } f1; } gm_tgfdnufetdnfc;
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tgfdnufetdnfc) == 48 && _Alignof(gm_tgfdnufetdnfc) == 8 && offsetof(gm_tgfdnufetdnfc, f0) == 0 && offsetof(gm_tgfdnufetdnfc, f1) == 8, "layout of `MemError!()`");
typedef struct { uint64_t f0; uint8_t p1[8]; gm_t6evimt84lndq f1; } gm_thf8as07kj3uj;
_Static_assert(sizeof(gm_thf8as07kj3uj) == 64 && _Alignof(gm_thf8as07kj3uj) == 16 && offsetof(gm_thf8as07kj3uj, f0) == 0 && offsetof(gm_thf8as07kj3uj, f1) == 16, "layout of `(i: usize, x: Value)`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_bit_width_0fd5f52 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_cell_addr_bl2jmen {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_const_addr_heqn3dl {
    gm_frame h;
    gm_t833u93mr4iob ret;
};
struct gm_fr_const_bits_1of8c6t {
    gm_frame h;
    gm_t6evimt84lndq l2; /* v */
    gm_t6evimt84lndq l14; /* x */
    gm_tdlro53ljvkl0 ret;
};
struct gm_fr_const_id_2mlh0og {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_fn_addr_odbbf6a {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_format_3tqcmdm {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_46el73v {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_ftjmcbc {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_get_pk8ncss {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_global_addr_9an4270 {
    gm_frame h;
    gm_tq7qjpkmqmc5s l4; /* gl */
    gm_t833u93mr4iob ret;
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
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
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
struct gm_fr_place_addr_m751en7 {
    gm_frame h;
    gm_tpautr4br7h3l l17; /* sd */
    gm_t833u93mr4iob ret;
};
struct gm_fr_put_ptr_188p3mm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_u64_irp77r2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unsupported_70vvk1e {
    gm_frame h;
    gm_t76123gfet73g ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_value_bytes_l7s126n {
    gm_frame h;
    gm_t6evimt84lndq l2; /* v */
    gm_tckdobunaulee l18; /* vd */
    gm_t6evimt84lndq l28; /* x */
    gm_t6evimt84lndq l34; /* x */
    gm_tpautr4br7h3l l36; /* sd */
    gm_t6evimt84lndq l43; /* x */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_wr_word_npte9fs {
    gm_frame h;
    gm_b0_1 ret;
};
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_alloc_mem_0vu6kcs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_t9qkh0rsooc8d a4, uint8_t a5);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) uint32_t gm_f_bit_width_0fd5f52(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) gm_tplpbqf3duvn3 gm_f_bits_field_vqua3kd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_cell_addr_bl2jmen(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_t88rcucg1b7uk a3, uint64_t a4, uint8_t a5);
__attribute__((aligned(4))) void gm_f_const_addr_heqn3dl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_const_bits_1of8c6t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_8gjlcrm(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_mod8kqd(void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_fn_addr_odbbf6a(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_format_3tqcmdm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_46el73v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_ftjmcbc(void * a0, gm_tn68el8d14vku a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1);
__attribute__((aligned(4))) void gm_f_get_srnes22(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_global_addr_9an4270(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_global_index_nhjk80t(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_init_9fuf99v(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_make_fill_jpvm0kc(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint8_t a1, const void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_csguankko4lmhd gm_f_opt_inner_q4ga84u(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_place_addr_m751en7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f99phma(void * a0, gm_tn68el8d14vku a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_float_fiad72d(void * a0, gm_csguankko4lmhd a1, double a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_slice_ko14loi(void * a0, gm_b16_8 a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_t4gfk5t(void * a0, uint64_t a1, gm_tlcm4lvu3g1a8 a2);
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) gm_tgglga8ro3isc gm_f_res_types_pg7rvck(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_unsupported_70vvk1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) void gm_f_value_bytes_l7s126n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, uint64_t a5);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2);
extern uint8_t gm_cmiriali67r[8] __attribute__((aligned(8)));
extern uint8_t gm_k5m9lemc4jn[40] __attribute__((aligned(8)));
extern uint8_t gm_kba69021uoh[40] __attribute__((aligned(8)));
extern uint8_t gm_kjqpf95jhcm[40] __attribute__((aligned(8)));
extern uint8_t gm_koedfqu36hd[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0ggo8ek7kfrt[34];
extern const uint8_t gm_s3js9g3l46oiv[40];
extern const uint8_t gm_s67fd8s6hf84e[24];
extern const uint8_t gm_sa6mes8ijqmmk[30];
extern const uint8_t gm_sbbavs945cjhp[32];
extern const uint8_t gm_sbvmilijbiuka[26];
extern const uint8_t gm_sct0vr0h329vl[43];
extern const uint8_t gm_sfali8nl85hc9[25];
extern const uint8_t gm_sgiqpj781s1a9[36];
extern const uint8_t gm_slnkfiltk5b43[57];
extern const uint8_t gm_sqrl3kgomorsv[39];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src6hq50r4rk7e6[];
extern const char gm_src8sgunk80orlb[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const uint8_t gm_svlcdvcv2bp94[24];
extern const gm_tmpl gm_tplav8bdvvt8rem;
extern const gm_tmpl gm_tplhs7ofk4c8qgg;
extern const gm_type gm_tyo86ks9qkvt;
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_const_id_2mlh0og F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 2128);
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_t88rcucg1b7uk t5 = gm_l2;
    gm_trlabqsjjp0ds t6;
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t6 + 12), t5);
    gm_t9805qtonbsag t7 = gm_f_get_pk8ncss(t3, t6);
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_format_3tqcmdm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3tqcmdm F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbvmilijbiuka));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhs7ofk4c8qgg), t3);
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_ftjmcbc(void * a0, gm_tn68el8d14vku a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_ftjmcbc F;
    void * gm_l0; /* m */
    gm_tn68el8d14vku gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 193u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_mod8kqd(t2, t3);
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
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 32) + 24));
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_pk8ncss F;
    void * gm_l0; /* m */
    gm_trlabqsjjp0ds gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_8gjlcrm(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_t9805qtonbsag t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 24) + 20));
    gm_t9805qtonbsag t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[4] = 1;
    F.ret = t13;
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
inline __attribute__((aligned(4))) void gm_f_cell_addr_bl2jmen(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, void * a2, gm_t88rcucg1b7uk a3, uint64_t a4, uint8_t a5) {
    struct gm_fr_cell_addr_bl2jmen F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* cell */
    gm_t88rcucg1b7uk gm_l3; /* t */
    uint64_t gm_l4; /* align */
    uint8_t gm_l5; /* mutable */
    uint64_t gm_l6; /* key */
    gm_tlcm4lvu3g1a8 gm_l7; /* a */
    uint64_t gm_l8; /* n */
    uint64_t gm_l9; /* addr */
    gm_b16_8 gm_l10; /* b */
    GM_STACK_CHECK(&F, 756u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tb7u19loo3j87 gm_l11;
    gm_t9qkh0rsooc8d gm_l12;
    gm_t833u93mr4iob gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tcfa6l7reoosn gm_l16;
    {
        void * t6 = gm_l2;
        uint64_t t7 = (uintptr_t)t6;
        gm_l6 = t7;
        {
            {
                {
                    void * t8 = gm_l0;
                    void * t9 = (void *)(((uint8_t *)t8) + 872);
                    uint64_t t10 = gm_l6;
                    gm_f_get_srnes22((void *)&gm_l11, t9, t10);
                }
                uint8_t t11 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
                if (!t11) {
                    goto gm_lb3;
                }
                gm_tlcm4lvu3g1a8 t12 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l11));
                gm_l7 = t12;
                {
                    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
                    ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
                    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t13);
                    goto gm_fdone;
                }
            }
gm_lb3:;
        }
gm_lb2:;
        void * t14 = gm_l0;
        gm_t88rcucg1b7uk t15 = gm_l3;
        uint64_t t16 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t16;
        {
            void * t17 = gm_l0;
            gm_tl874t2atnlh3 t18 = gm_l1;
            uint64_t t19 = gm_l8;
            uint64_t t20 = gm_l4;
            uint8_t t21 = gm_l5;
            if (t21) {
                gm_t9qkh0rsooc8d t22;
                GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)5ULL));
                gm_l12 = t22;
            } else {
                gm_t9qkh0rsooc8d t23;
                GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)4ULL));
                gm_l12 = t23;
            }
            gm_t9qkh0rsooc8d t24 = gm_l12;
            uint8_t t25 = gm_l5;
            gm_f_alloc_mem_0vu6kcs((void *)&gm_l13, (void *)NULL, cx, NULL, t17, t18, t19, t20, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
        if (t26) {
        } else {
            gm_t76123gfet73g t27 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l13) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t27);
            goto gm_fdone;
        }
        uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
        gm_l9 = t28;
        void * t29 = gm_l0;
        void * t30 = (void *)(((uint8_t *)t29) + 872);
        uint64_t t31 = gm_l6;
        uint64_t t32 = gm_l9;
        gm_t88rcucg1b7uk t33 = gm_l3;
        gm_tlcm4lvu3g1a8 t34;
        GM_ST(uint32_t, (uint8_t *)&t34 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t34 + 0), t32);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t34 + 8), t33);
        gm_tftb1odc739t5 t35 = gm_f_put_t4gfk5t(t30, t31, t34);
        gm_l14 = t35;
        uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
        if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_src8sgunk80orlb, 20, 5);
        {
            uint64_t t37 = gm_l8;
            uint64_t t38 = gm_f_us_cuv01fp(t37);
            gm_f_make_fill_jpvm0kc((void *)&gm_l15, (void *)NULL, cx, NULL, t38, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k5m9lemc4jn)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
        if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src8sgunk80orlb, 21, 10);
        gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
        gm_l10 = t40;
        {
            void * t41 = gm_l0;
            gm_tl874t2atnlh3 t42 = gm_l1;
            void * t43 = gm_l2;
            gm_t88rcucg1b7uk t44 = gm_l3;
            gm_b16_8 t45 = gm_l10;
            gm_t6evimt84lndq t46 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t43));
            gm_f_value_bytes_l7s126n((void *)&gm_l16, (void *)NULL, cx, NULL, t41, t42, &t46, t44, t45, ((uint64_t)0ULL));
            if (gm_unw.kind) {
                goto gm_ld5_1;
            }
        }
        uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (t47) {
        } else {
            gm_t76123gfet73g t48 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l16) + 8));
            GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
            ((uint8_t *)&F.ret)[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t48);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld5_1;
        }
        {
            void * t49 = gm_l0;
            void * t50 = (void *)(((uint8_t *)t49) + 184);
            uint64_t t51 = gm_l9;
            gm_b16_8 t52 = gm_l10;
            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
            gm_tgfdnufetdnfc t54;
            gm_f_init_9fuf99v((void *)&t54, t50, t51, t53);
        }
        {
            uint64_t t55 = gm_l9;
            GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&F.ret + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&F.ret + 24, 0);
            ((uint8_t *)&F.ret)[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&F.ret + 8), t55);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld5_1;
        }
    }
    {
        gm_b16_8 t56 = gm_l10;
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
        gm_b0_1 t58 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t57, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kba69021uoh)));
    }
    goto gm_ls59;
gm_ld5_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_b16_8 t60 = gm_l10;
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
        gm_b0_1 t62 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t61, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kba69021uoh)));
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls59:;
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
inline __attribute__((aligned(4))) void gm_f_const_addr_heqn3dl(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_const_addr_heqn3dl F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tl874t2atnlh3 gm_l3; /* at */
    uint32_t gm_l4; /* id */
    void * gm_l5; /* cell */
    GM_STACK_CHECK(&F, 334u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t9805qtonbsag gm_l6;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    gm_tl874t2atnlh3 t4 = gm_l1;
    gm_t88rcucg1b7uk t5 = gm_l2;
    gm_t9805qtonbsag t6 = gm_f_const_id_2mlh0og(t3, GM_CONV(gm_cseahvg8v7p0vs, t4), GM_CONV(gm_csguankko4lmhd, t5));
    gm_l6 = t6;
    uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l6))[4] != 0);
    if (t7) {
        uint32_t t8 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
        gm_l4 = t8;
    } else {
        void * t9 = gm_l0;
        gm_tl874t2atnlh3 t10 = gm_l3;
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s0ggo8ek7kfrt));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(33));
        gm_t76123gfet73g t12;
        gm_f_unsupported_70vvk1e((void *)&t12, (void *)NULL, cx, NULL, t9, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t12);
        goto gm_fdone;
    }
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, (((uint8_t *)t13) + 8));
    void * t15 = GM_LD(void *, (((uint8_t *)t14) + 2088));
    uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)t14) + 2088) + 8));
    uint32_t t17 = gm_l4;
    uint64_t t18 = (uint64_t)t17;
    uint64_t t19 = gm_f_us_cuv01fp(t18);
    if ((uintptr_t)t19 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t19, t16, gm_src8sgunk80orlb, 8, 13);
    uint8_t t20 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t15) + (uintptr_t)t19 * 88) + 80)) != 0);
    if (t20) {
        void * t21 = GM_LD(void *, ((((uint8_t *)t15) + (uintptr_t)t19 * 88) + 80));
        gm_l5 = t21;
    } else {
        void * t22 = gm_l0;
        gm_tl874t2atnlh3 t23 = gm_l3;
        gm_b16_8 t24;
        GM_ST(void *, (uint8_t *)&t24, (void *)(gm_s3js9g3l46oiv));
        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(39));
        gm_t76123gfet73g t25;
        gm_f_unsupported_70vvk1e((void *)&t25, (void *)NULL, cx, NULL, t22, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t25);
        goto gm_fdone;
    }
    void * t26 = gm_l0;
    gm_tl874t2atnlh3 t27 = gm_l3;
    void * t28 = gm_l5;
    gm_t88rcucg1b7uk t29 = gm_l2;
    void * t30 = gm_l0;
    gm_t88rcucg1b7uk t31 = gm_l2;
    uint64_t t32 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t30, t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_cell_addr_bl2jmen((void *)&(*(gm_t833u93mr4iob *)gm_out), (void *)NULL, cx, NULL, t26, t27, t28, t29, t32, ((uint8_t)0));
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
__attribute__((aligned(4))) void gm_f_const_bits_1of8c6t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_const_bits_1of8c6t F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l3; /* t */
    uint8_t gm_l4; /* b */
    unsigned __int128 gm_l5; /* bits */
    uint32_t gm_l6; /* w */
    uint32_t gm_l7; /* index */
    gm_tcmkompbl09il gm_l8; /* u */
    void * gm_l9; /* a */
    unsigned __int128 gm_l10; /* raw */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    gm_tplpbqf3duvn3 gm_l15; /* bf */
    GM_STACK_CHECK(&F, 1183u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    gm_tdlro53ljvkl0 gm_l16;
    gm_tdlro53ljvkl0 gm_l17;
    gm_t57euh612ligr gm_l18;
    gm_thf8as07kj3uj gm_l19;
    gm_tdlro53ljvkl0 gm_l20;
    {
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)2ULL));
            if (!t14) {
                goto gm_lb2;
            }
            uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 16));
            gm_l4 = t15;
            uint8_t t16 = gm_l4;
            if (t16) {
                GM_ST(uint8_t, (uint8_t *)&gm_l16 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l16 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l16 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&gm_l16 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&gm_l16 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&gm_l16 + 40, 0);
                ((uint8_t *)&gm_l16)[0] = 0;
                GM_ST(unsigned __int128, ((uint8_t *)&gm_l16 + 16), ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)));
            } else {
                GM_ST(uint8_t, (uint8_t *)&gm_l16 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l16 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l16 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&gm_l16 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&gm_l16 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&gm_l16 + 40, 0);
                ((uint8_t *)&gm_l16)[0] = 0;
                GM_ST(unsigned __int128, ((uint8_t *)&gm_l16 + 16), ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
            }
            memmove(gm_out, ((uint8_t *)&gm_l16), 48);
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
            if (!t18) {
                goto gm_lb3;
            }
            unsigned __int128 t19 = GM_LD(unsigned __int128, (((uint8_t *)&F.l2) + 16));
            gm_l5 = t19;
            void * t20 = gm_l0;
            gm_t88rcucg1b7uk t21 = gm_l3;
            uint32_t t22 = gm_f_bit_width_0fd5f52((void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t22;
            uint32_t t23 = gm_l6;
            uint8_t t24 = (t23 >= ((uint32_t)128ULL));
            if (t24) {
                unsigned __int128 t25 = gm_l5;
                GM_ST(uint8_t, (uint8_t *)&gm_l17 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l17 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l17 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&gm_l17 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&gm_l17 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&gm_l17 + 40, 0);
                ((uint8_t *)&gm_l17)[0] = 0;
                GM_ST(unsigned __int128, ((uint8_t *)&gm_l17 + 16), t25);
            } else {
                unsigned __int128 t26 = gm_l5;
                uint32_t t27 = gm_l6;
                unsigned __int128 t28 = (unsigned __int128)t27;
                unsigned __int128 n29 = (unsigned __int128)t28;
                if (n29 >= 128) gm_trap_shift((uint64_t)n29, "u128", gm_src8sgunk80orlb, 175, 57);
                unsigned __int128 r30 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)) << (int)n29);
                bool o31 = (unsigned __int128)(r30 >> (int)n29) != ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL));
                if (o31) gm_trap("integer overflow in `<<`", gm_src8sgunk80orlb, 175, 57);
                unsigned __int128 t32 = r30;
                unsigned __int128 r33; bool o34 = __builtin_sub_overflow(t32, ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 1ULL)), &r33);
                if (o34) gm_trap("integer overflow in `-`", gm_src8sgunk80orlb, 175, 56);
                unsigned __int128 t35 = r33;
                unsigned __int128 t36 = (unsigned __int128)(t26 & t35);
                GM_ST(uint8_t, (uint8_t *)&gm_l17 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l17 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l17 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&gm_l17 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&gm_l17 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&gm_l17 + 40, 0);
                ((uint8_t *)&gm_l17)[0] = 0;
                GM_ST(unsigned __int128, ((uint8_t *)&gm_l17 + 16), t36);
            }
            memmove(gm_out, ((uint8_t *)&gm_l17), 48);
            goto gm_fdone;
        }
gm_lb3:;
        {
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)10ULL));
            if (!t38) {
                goto gm_lb4;
            }
            uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 16));
            gm_l7 = t39;
            {
                void * t40 = gm_l0;
                gm_t88rcucg1b7uk t41 = gm_l3;
                gm_f_union_def_h8mvf4t((void *)&gm_l18, t40, GM_CONV(gm_csguankko4lmhd, t41));
            }
            uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l18))[24] != 0);
            if (t42) {
                memmove(((uint8_t *)&gm_l8), ((uint8_t *)&gm_l18), 24);
            } else {
                void * t43 = gm_l0;
                gm_tl874t2atnlh3 t44 = gm_l1;
                gm_b16_8 t45;
                GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sfali8nl85hc9));
                GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(24));
                gm_t76123gfet73g t46;
                gm_f_unsupported_70vvk1e((void *)&t46, (void *)NULL, cx, NULL, t43, t44, t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 16), t46);
                goto gm_fdone;
            }
            {
                void * t47 = gm_l0;
                gm_tl874t2atnlh3 t48 = gm_l1;
                void * t49 = GM_LD(void *, (((uint8_t *)&gm_l8) + 8));
                uint64_t t50 = GM_LD(uint64_t, ((((uint8_t *)&gm_l8) + 8) + 8));
                uint32_t t51 = gm_l7;
                uint64_t t52 = (uint64_t)t51;
                uint64_t t53 = gm_f_us_cuv01fp(t52);
                if ((uintptr_t)t53 >= (uintptr_t)(t50)) gm_trap_index((uintptr_t)t53, t50, gm_src8sgunk80orlb, 179, 59);
                unsigned __int128 t54 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((((uint8_t *)t49) + (uintptr_t)t53 * 72) + 24)));
                gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l8));
                gm_t6evimt84lndq t56;
                GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t56 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t56 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t56 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t56 + 16), t54);
                gm_f_const_bits_1of8c6t((void *)&(*(gm_tdlro53ljvkl0 *)gm_out), (void *)NULL, cx, NULL, t47, t48, &t56, t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
        }
gm_lb4:;
        {
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)3ULL));
            if (!t58) {
                goto gm_lb5;
            }
            void * t59 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l9 = t59;
            gm_l10 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
            void * t60 = gm_l9;
            gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)t60));
            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
            gm_l11 = t62;
            gm_l12 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t63 = gm_l12;
                    uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint8_t t65 = (t63 < t64);
                    if (!t65) {
                        goto gm_lb6;
                    }
                    {
                        {
                            uint64_t t66 = gm_l12;
                            void * t67 = GM_LD(void *, ((uint8_t *)&gm_l11));
                            uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                            uint64_t t69 = gm_l12;
                            gm_t6evimt84lndq t70 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t67) + (uintptr_t)t69 * 48));
                            GM_ST(uint64_t, (uint8_t *)&gm_l19 + 8, 0);
                            GM_ST(uint64_t, ((uint8_t *)&gm_l19 + 0), t66);
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&gm_l19 + 16), t70);
                        }
                        {
                            {
                                uint64_t t71 = GM_LD(uint64_t, ((uint8_t *)&gm_l19));
                                gm_l13 = t71;
                                {
                                    memmove(((uint8_t *)&F.l14), (((uint8_t *)&gm_l19) + 16), 48);
                                }
                                void * t72 = gm_l0;
                                gm_t88rcucg1b7uk t73 = gm_l3;
                                uint64_t t74 = gm_l13;
                                uint32_t t75 = (uint32_t)t74;
                                gm_tplpbqf3duvn3 t76 = gm_f_bits_field_vqua3kd((void *)NULL, cx, NULL, t72, t73, t75);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l15 = t76;
                                {
                                    unsigned __int128 t77 = gm_l10;
                                    void * t78 = gm_l0;
                                    gm_tl874t2atnlh3 t79 = gm_l1;
                                    gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l15) + 4));
                                    gm_t6evimt84lndq t81 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l14));
                                    gm_f_const_bits_1of8c6t((void *)&gm_l20, (void *)NULL, cx, NULL, t78, t79, &t81, t80);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                    if (t82) {
                                    } else {
                                        gm_t76123gfet73g t83 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l20) + 16));
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 8, 0);
                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 40, 0);
                                        ((uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out))[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 16), t83);
                                        goto gm_fdone;
                                    }
                                    unsigned __int128 t84 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l20) + 16));
                                    uint32_t t85 = GM_LD(uint32_t, ((uint8_t *)&gm_l15));
                                    unsigned __int128 t86 = (unsigned __int128)t85;
                                    unsigned __int128 n87 = (unsigned __int128)t86;
                                    if (n87 >= 128) gm_trap_shift((uint64_t)n87, "u128", gm_src8sgunk80orlb, 185, 24);
                                    unsigned __int128 r88 = (unsigned __int128)((unsigned __int128)t84 << (int)n87);
                                    bool o89 = (unsigned __int128)(r88 >> (int)n87) != t84;
                                    if (o89) gm_trap("integer overflow in `<<`", gm_src8sgunk80orlb, 185, 24);
                                    unsigned __int128 t90 = r88;
                                    unsigned __int128 t91 = (unsigned __int128)(t77 | t90);
                                    gm_l10 = t91;
                                }
                                goto gm_lb9;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 183, 13);
                        }
gm_lb9:;
                    }
gm_lb8:;
                    uint64_t t92 = gm_l12;
                    uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                    uint64_t t95 = r93;
                    gm_l12 = t95;
                }
            }
gm_lb6:;
            unsigned __int128 t96 = gm_l10;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out))[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 16), t96);
            goto gm_fdone;
        }
gm_lb5:;
        {
            void * t97 = gm_l0;
            gm_tl874t2atnlh3 t98 = gm_l1;
            gm_b16_8 t99;
            GM_ST(void *, (uint8_t *)&t99, (void *)(gm_sfali8nl85hc9));
            GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(24));
            gm_t76123gfet73g t100;
            gm_f_unsupported_70vvk1e((void *)&t100, (void *)NULL, cx, NULL, t97, t98, t99);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out))[0] = 1;
            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tdlro53ljvkl0 *)gm_out) + 16), t100);
            goto gm_fdone;
        }
gm_lb11:;
        gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 171, 5);
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
__attribute__((aligned(4))) void gm_f_global_addr_9an4270(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2) {
    struct gm_fr_global_addr_9an4270 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    uint64_t gm_l2; /* decl */
    uint64_t gm_l3; /* i */
    gm_tn68el8d14vku gm_l5; /* key */
    uint64_t gm_l6; /* a */
    uint64_t gm_l7; /* al */
    uint64_t gm_l8; /* addr */
    void * gm_l9; /* cell */
    void * gm_l10; /* cell */
    gm_b16_8 gm_l11; /* b */
    GM_STACK_CHECK(&F, 1507u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l12;
    uint32_t gm_l13;
    gm_tf5srh4mp4b3i gm_l14;
    uint8_t gm_l15;
    gm_tabb710d9mq2e gm_l16;
    uint64_t gm_l17;
    gm_t833u93mr4iob gm_l18;
    gm_t833u93mr4iob gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_tcfa6l7reoosn gm_l21;
    gm_t833u93mr4iob gm_l22;
    gm_tftb1odc739t5 gm_l23;
    void * t12 = gm_l0;
    void * t13 = GM_LD(void *, ((uint8_t *)t12));
    uint64_t t14 = gm_l2;
    gm_tf5srh4mp4b3i t15 = gm_f_global_index_nhjk80t(t13, t14);
    gm_l12 = t15;
    uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l12))[8] != 0);
    if (t16) {
        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&gm_l12));
        gm_l3 = t17;
    } else {
        void * t18 = gm_l0;
        gm_tl874t2atnlh3 t19 = gm_l1;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sct0vr0h329vl));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(42));
        gm_t76123gfet73g t21;
        gm_f_trap_qh84l9d((void *)&t21, (void *)NULL, cx, NULL, t18, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t21);
        goto gm_fdone;
    }
    {
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 8));
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 1568));
        uint64_t t25 = GM_LD(uint64_t, ((((uint8_t *)t23) + 1568) + 8));
        uint64_t t26 = gm_l3;
        if ((uintptr_t)t26 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t26, t25, gm_src8sgunk80orlb, 31, 11);
        memmove(((uint8_t *)&F.l4), (((uint8_t *)t24) + (uintptr_t)t26 * 104), 104);
    }
    uint64_t t27 = gm_l3;
    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 48));
    if (t28) {
        void * t29 = gm_l0;
        uint32_t t30 = GM_LD(uint32_t, ((((uint8_t *)t29) + 1672) + 40));
        gm_l13 = t30;
    } else {
        gm_l13 = ((uint32_t)0ULL);
    }
    uint32_t t31 = gm_l13;
    gm_tn68el8d14vku t32;
    GM_ST(uint32_t, (uint8_t *)&t32 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t32 + 0), t27);
    GM_ST(uint32_t, ((uint8_t *)&t32 + 8), t31);
    gm_l5 = t32;
    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 48));
    if (t33) {
        {
            {
                void * t34 = gm_l0;
                void * t35 = (void *)(((uint8_t *)t34) + 968);
                gm_tn68el8d14vku t36 = gm_l5;
                gm_tf5srh4mp4b3i t37 = gm_f_get_ftjmcbc(t35, t36);
                gm_l14 = t37;
                uint8_t t38 = (uint8_t)((((uint8_t *)&gm_l14))[8] != 0);
                if (!t38) {
                    goto gm_lb2;
                }
                uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                gm_l6 = t39;
                uint64_t t40 = gm_l6;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
                GM_ST(uint64_t, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t40);
                goto gm_fdone;
            }
gm_lb2:;
        }
gm_lb1:;
    } else {
        void * t41 = gm_l0;
        void * t42 = GM_LD(void *, (((uint8_t *)t41) + 928));
        uint64_t t43 = GM_LD(uint64_t, ((((uint8_t *)t41) + 928) + 8));
        uint64_t t44 = gm_l3;
        if ((uintptr_t)t44 >= (uintptr_t)(t43)) gm_trap_index((uintptr_t)t44, t43, gm_src8sgunk80orlb, 36, 15);
        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)t42) + (uintptr_t)t44 * 8));
        uint8_t t46 = (!(t45 == ((uint64_t)0ULL)));
        if (t46) {
            void * t47 = gm_l0;
            void * t48 = GM_LD(void *, (((uint8_t *)t47) + 928));
            uint64_t t49 = GM_LD(uint64_t, ((((uint8_t *)t47) + 928) + 8));
            uint64_t t50 = gm_l3;
            if ((uintptr_t)t50 >= (uintptr_t)(t49)) gm_trap_index((uintptr_t)t50, t49, gm_src8sgunk80orlb, 37, 16);
            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)t48) + (uintptr_t)t50 * 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t51);
            goto gm_fdone;
        }
    }
    {
        {
            uint8_t t52 = (uint8_t)(((((uint8_t *)&F.l4) + 56))[16] != 0);
            if (t52) {
                goto gm_lb4;
            }
            gm_l15 = ((uint8_t)0);
            goto gm_lb3;
        }
gm_lb4:;
        {
            gm_l15 = ((uint8_t)1);
            goto gm_lb3;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 39, 8);
    }
gm_lb3:;
    uint8_t t53 = gm_l15;
    if (t53) {
        void * t54 = gm_l0;
        gm_tl874t2atnlh3 t55 = gm_l1;
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
        gm_tnpifv20irnph t57;
        GM_ST(gm_b16_8, ((uint8_t *)&t57 + 0), t56);
        gm_f_format_3tqcmdm((void *)&gm_l16, (void *)NULL, cx, NULL, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_src8sgunk80orlb, 39, 63);
        gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
        gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)&t59));
        gm_t76123gfet73g t61;
        gm_f_unsupported_70vvk1e((void *)&t61, (void *)NULL, cx, NULL, t54, t55, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t61);
        goto gm_fdone;
    }
    void * t62 = gm_l0;
    gm_t88rcucg1b7uk t63 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
    uint64_t t64 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t62, t63);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t65 = (uint8_t)(((((uint8_t *)&F.l4) + 80))[8] != 0);
    if (t65) {
        uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 80));
        gm_l17 = t66;
    } else {
        gm_l17 = ((uint64_t)1ULL);
    }
    uint64_t t67 = gm_l17;
    uint64_t t68 = (t64 > t67 ? t64 : t67);
    gm_l7 = t68;
    gm_l8 = ((uint64_t)0ULL);
    {
        {
            uint8_t t70 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l4) + 96)) != 0);
            if (!t70) {
                goto gm_lb7;
            }
            void * t71 = GM_LD(void *, (((uint8_t *)&F.l4) + 96));
            gm_l9 = t71;
            uint8_t t72 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 48));
            uint8_t t73 = (uint8_t)!t72;
            if (!t73) {
                goto gm_lb7;
            }
            void * t74 = gm_l0;
            gm_tl874t2atnlh3 t75 = gm_l1;
            void * t76 = gm_l9;
            gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
            uint64_t t78 = gm_l7;
            gm_f_cell_addr_bl2jmen((void *)&gm_l18, (void *)NULL, cx, NULL, t74, t75, t76, t77, t78, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_ll69;
            }
            uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (t79) {
            } else {
                gm_t76123gfet73g t80 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l18) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t80);
                goto gm_fdone;
            }
            uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
            gm_l8 = t81;
            goto gm_lb6;
        }
gm_lb7:;
        {
            {
                uint8_t t84 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l4) + 96)) != 0);
                if (!t84) {
                    goto gm_lb9;
                }
                void * t85 = GM_LD(void *, (((uint8_t *)&F.l4) + 96));
                gm_l10 = t85;
                {
                    {
                        void * t87 = gm_l0;
                        gm_tl874t2atnlh3 t88 = gm_l1;
                        void * t89 = gm_l0;
                        gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
                        uint64_t t91 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t89, t90);
                        if (gm_unw.kind) {
                            goto gm_ll83;
                        }
                        uint64_t t92 = gm_l7;
                        gm_t9qkh0rsooc8d t93;
                        GM_ST(uint8_t, ((uint8_t *)&t93 + 0), ((uint8_t)5ULL));
                        gm_f_alloc_mem_0vu6kcs((void *)&gm_l19, (void *)NULL, cx, NULL, t87, t88, t91, t92, t93, ((uint8_t)1));
                        if (gm_unw.kind) {
                            goto gm_ll83;
                        }
                    }
                    uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                    if (t94) {
                    } else {
                        gm_t76123gfet73g t95 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l19) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t95);
                        goto gm_fdone;
                    }
                    uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                    gm_l8 = t96;
                    {
                        void * t97 = gm_l0;
                        gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
                        uint64_t t99 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t97, t98);
                        if (gm_unw.kind) {
                            goto gm_ll83;
                        }
                        uint64_t t100 = gm_f_us_cuv01fp(t99);
                        gm_f_make_fill_jpvm0kc((void *)&gm_l20, (void *)NULL, cx, NULL, t100, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_koedfqu36hd)));
                        if (gm_unw.kind) {
                            goto gm_ll83;
                        }
                    }
                    uint8_t t101 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                    if (!t101) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_src8sgunk80orlb, 47, 14);
                    gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                    gm_l11 = t102;
                    {
                        void * t103 = gm_l0;
                        gm_tl874t2atnlh3 t104 = gm_l1;
                        void * t105 = gm_l10;
                        gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
                        gm_b16_8 t107 = gm_l11;
                        gm_t6evimt84lndq t108 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t105));
                        gm_f_value_bytes_l7s126n((void *)&gm_l21, (void *)NULL, cx, NULL, t103, t104, &t108, t106, t107, ((uint64_t)0ULL));
                        if (gm_unw.kind) {
                            goto gm_ld86_1;
                        }
                    }
                    uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                    if (t109) {
                    } else {
                        gm_t76123gfet73g t110 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l21) + 8));
                        GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                        ((uint8_t *)&F.ret)[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&F.ret + 8), t110);
                        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                        goto gm_ld86_1;
                    }
                    {
                        void * t111 = gm_l0;
                        void * t112 = (void *)(((uint8_t *)t111) + 184);
                        uint64_t t113 = gm_l8;
                        gm_b16_8 t114 = gm_l11;
                        gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
                        gm_tgfdnufetdnfc t116;
                        gm_f_init_9fuf99v((void *)&t116, t112, t113, t115);
                    }
                }
                {
                    gm_b16_8 t117 = gm_l11;
                    gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
                    gm_b0_1 t119 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t118, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjqpf95jhcm)));
                }
                goto gm_ls120;
gm_ld86_1:;
                {
                    gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
                    gm_b16_8 t121 = gm_l11;
                    gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
                    gm_b0_1 t123 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t122, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjqpf95jhcm)));
                    gm_unw = gm_saved;
                }
                goto gm_ll83;
gm_ls120:;
gm_lb10:;
                goto gm_lb8;
            }
            goto gm_ls124;
gm_ll83:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741832) { gm_unw.kind = GM_NONE; goto gm_lb9; }
            goto gm_ll82;
gm_ls124:;
gm_lb9:;
            void * t125 = gm_l0;
            gm_tl874t2atnlh3 t126 = gm_l1;
            void * t127 = gm_l0;
            gm_t88rcucg1b7uk t128 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 24));
            uint64_t t129 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t127, t128);
            if (gm_unw.kind) {
                goto gm_ll82;
            }
            uint64_t t130 = gm_l7;
            gm_t9qkh0rsooc8d t131;
            GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)5ULL));
            gm_f_alloc_mem_0vu6kcs((void *)&gm_l22, (void *)NULL, cx, NULL, t125, t126, t129, t130, t131, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_ll82;
            }
            uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
            if (t132) {
            } else {
                gm_t76123gfet73g t133 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l22) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t133);
                goto gm_fdone;
            }
            uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
            gm_l8 = t134;
        }
        goto gm_ls135;
gm_ll82:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741831) { gm_unw.kind = GM_NONE; goto gm_lb8; }
        goto gm_ll69;
gm_ls135:;
gm_lb8:;
    }
    goto gm_ls136;
gm_ll69:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1073741829) { gm_unw.kind = GM_NONE; goto gm_lb6; }
    goto gm_lad;
gm_ls136:;
gm_lb6:;
    uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 48));
    if (t137) {
        void * t138 = gm_l0;
        void * t139 = (void *)(((uint8_t *)t138) + 968);
        gm_tn68el8d14vku t140 = gm_l5;
        uint64_t t141 = gm_l8;
        gm_tftb1odc739t5 t142 = gm_f_put_f99phma(t139, t140, t141);
        gm_l23 = t142;
        uint8_t t143 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
        if (!t143) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_src8sgunk80orlb, 54, 26);
    } else {
        void * t144 = gm_l0;
        void * t145 = GM_LD(void *, (((uint8_t *)t144) + 928));
        uint64_t t146 = GM_LD(uint64_t, ((((uint8_t *)t144) + 928) + 8));
        uint64_t t147 = gm_l3;
        if ((uintptr_t)t147 >= (uintptr_t)(t146)) gm_trap_index((uintptr_t)t147, t146, gm_src8sgunk80orlb, 54, 68);
        uint64_t t148 = gm_l8;
        GM_ST(uint64_t, (((uint8_t *)t145) + (uintptr_t)t147 * 8), t148);
    }
    {
        uint64_t t149 = gm_l8;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t149);
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
__attribute__((aligned(4))) void gm_f_place_addr_m751en7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_place_addr_m751en7 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tkaapcii86udk gm_l2; /* pl */
    gm_t88rcucg1b7uk gm_l3; /* pointee */
    gm_t88rcucg1b7uk gm_l4; /* root */
    gm_tlcm4lvu3g1a8 gm_l5; /* known */
    void * gm_l6; /* a */
    uint64_t gm_l7; /* base */
    uint64_t gm_l8; /* addend */
    gm_t88rcucg1b7uk gm_l9; /* cur */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    uint64_t gm_l13; /* len */
    gm_t88rcucg1b7uk gm_l14; /* elem */
    gm_b16_8 gm_l15; /* elems */
    uint32_t gm_l16; /* id */
    GM_STACK_CHECK(&F, 1254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_tb7u19loo3j87 gm_l18;
    gm_t833u93mr4iob gm_l19;
    gm_t53at80pfdus3 gm_l20;
    gm_b16_8 gm_l21;
    gm_b16_8 gm_l22;
    gm_t88rcucg1b7uk t18 = gm_l3;
    gm_l4 = t18;
    {
        {
            void * t19 = gm_l0;
            void * t20 = (void *)(((uint8_t *)t19) + 872);
            void * t21 = GM_LD(void *, ((uint8_t *)a2));
            uint64_t t22 = (uintptr_t)t21;
            gm_f_get_srnes22((void *)&gm_l18, t20, t22);
            uint8_t t23 = (uint8_t)((((uint8_t *)&gm_l18))[16] != 0);
            if (!t23) {
                goto gm_lb2;
            }
            gm_tlcm4lvu3g1a8 t24 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&gm_l18));
            gm_l5 = t24;
            gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
            gm_l4 = t25;
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)a2) + 8) + 8));
                uint8_t t27 = ((t26 == ((uint64_t)1ULL)));
                if (!t27) {
                    goto gm_lb4;
                }
                void * t28 = GM_LD(void *, ((uint8_t *)a2));
                uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)t28) + 0));
                uint8_t t30 = (uint8_t)(t29 == ((uint8_t)3ULL));
                if (!t30) {
                    goto gm_lb4;
                }
                void * t31 = GM_LD(void *, (((uint8_t *)t28) + 16));
                gm_l6 = t31;
                void * t32 = gm_l0;
                void * t33 = GM_LD(void *, (((uint8_t *)t32) + 8));
                void * t34 = (void *)(((uint8_t *)t33) + 8);
                void * t35 = gm_l6;
                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 8));
                uint64_t t37 = (uint64_t)t36;
                gm_t88rcucg1b7uk t38 = gm_l3;
                gm_t88rcucg1b7uk t39 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t34, t37, t38);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l4 = t39;
                goto gm_lb3;
            }
gm_lb4:;
            uint64_t t40 = GM_LD(uint64_t, ((((uint8_t *)a2) + 8) + 8));
            uint8_t t41 = (t40 > ((uint64_t)0ULL));
            if (t41) {
                void * t42 = gm_l0;
                gm_tl874t2atnlh3 t43 = gm_l1;
                gm_b16_8 t44;
                GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sqrl3kgomorsv));
                GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(38));
                gm_t76123gfet73g t45;
                gm_f_unsupported_70vvk1e((void *)&t45, (void *)NULL, cx, NULL, t42, t43, t44);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t45);
                goto gm_fdone;
            }
        }
gm_lb3:;
    }
gm_lb1:;
    {
        void * t46 = gm_l0;
        gm_tl874t2atnlh3 t47 = gm_l1;
        void * t48 = GM_LD(void *, ((uint8_t *)a2));
        gm_t88rcucg1b7uk t49 = gm_l4;
        void * t50 = gm_l0;
        gm_t88rcucg1b7uk t51 = gm_l4;
        uint64_t t52 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t50, t51);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t53 = GM_LD(void *, ((uint8_t *)a2));
        uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)t53) + 48));
        uint8_t t55 = (uint8_t)!t54;
        gm_f_cell_addr_bl2jmen((void *)&gm_l19, (void *)NULL, cx, NULL, t46, t47, t48, t49, t52, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
    if (t56) {
    } else {
        gm_t76123gfet73g t57 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l19) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t57);
        goto gm_fdone;
    }
    uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
    gm_l7 = t58;
    gm_l8 = ((uint64_t)0ULL);
    gm_t88rcucg1b7uk t59 = gm_l4;
    gm_l9 = t59;
    gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
    gm_l10 = t60;
    gm_l11 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t61 = gm_l11;
            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            uint8_t t63 = (t61 < t62);
            if (!t63) {
                goto gm_lb5;
            }
            {
                void * t64 = GM_LD(void *, ((uint8_t *)&gm_l10));
                uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint64_t t66 = gm_l11;
                {
                    {
                        uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)t64) + (uintptr_t)t66 * 8));
                        gm_l12 = t67;
                        void * t68 = gm_l0;
                        gm_t88rcucg1b7uk t69 = gm_l9;
                        gm_f_kind_htv6093((void *)&gm_l20, t68, GM_CONV(gm_csguankko4lmhd, t69));
                        {
                            {
                                uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
                                uint8_t t71 = (uint8_t)(t70 == ((uint8_t)11ULL));
                                if (!t71) {
                                    goto gm_lb11;
                                }
                                uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                                gm_l13 = t72;
                                gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l20) + 8) + 8));
                                gm_l14 = t73;
                                uint64_t t74 = gm_l8;
                                uint64_t t75 = gm_l12;
                                void * t76 = gm_l0;
                                gm_t88rcucg1b7uk t77 = gm_l14;
                                uint64_t t78 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t76, t77);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint64_t r79; bool o80 = __builtin_mul_overflow(t75, t78, &r79);
                                if (o80) gm_trap("integer overflow in `*`", gm_src8sgunk80orlb, 151, 27);
                                uint64_t t81 = r79;
                                uint64_t r82; bool o83 = __builtin_add_overflow(t74, t81, &r82);
                                if (o83) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 151, 17);
                                uint64_t t84 = r82;
                                gm_l8 = t84;
                                uint64_t t85 = gm_l12;
                                uint64_t t86 = gm_l13;
                                uint8_t t87 = (t85 < t86);
                                if (t87) {
                                    gm_t88rcucg1b7uk t88 = gm_l14;
                                    gm_l9 = t88;
                                }
                                goto gm_lb10;
                            }
gm_lb11:;
                            {
                                uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
                                uint8_t t90 = (uint8_t)(t89 == ((uint8_t)12ULL));
                                if (!t90) {
                                    goto gm_lb12;
                                }
                                gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                                gm_l15 = t91;
                                uint64_t t92 = gm_l8;
                                void * t93 = gm_l0;
                                gm_t88rcucg1b7uk t94 = gm_l9;
                                gm_b16_8 t95 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t93, t94);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l21 = t95;
                                void * t96 = GM_LD(void *, ((uint8_t *)&gm_l21));
                                uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                uint64_t t98 = gm_l12;
                                uint64_t t99 = gm_f_us_cuv01fp(t98);
                                if ((uintptr_t)t99 >= (uintptr_t)(t97)) gm_trap_index((uintptr_t)t99, t97, gm_src8sgunk80orlb, 155, 27);
                                uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)t96) + (uintptr_t)t99 * 8));
                                uint64_t r101; bool o102 = __builtin_add_overflow(t92, t100, &r101);
                                if (o102) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 155, 17);
                                uint64_t t103 = r101;
                                gm_l8 = t103;
                                void * t104 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                uint64_t t106 = gm_l12;
                                uint64_t t107 = gm_f_us_cuv01fp(t106);
                                if ((uintptr_t)t107 >= (uintptr_t)(t105)) gm_trap_index((uintptr_t)t107, t105, gm_src8sgunk80orlb, 156, 23);
                                gm_t88rcucg1b7uk t108 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t104) + (uintptr_t)t107 * 4));
                                gm_l9 = t108;
                                goto gm_lb10;
                            }
gm_lb12:;
                            {
                                uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
                                uint8_t t110 = (uint8_t)(t109 == ((uint8_t)17ULL));
                                if (!t110) {
                                    goto gm_lb13;
                                }
                                uint32_t t111 = GM_LD(uint32_t, (((uint8_t *)&gm_l20) + 8));
                                gm_l16 = t111;
                                {
                                    {
                                        void * t112 = gm_l0;
                                        void * t113 = GM_LD(void *, (((uint8_t *)t112) + 8));
                                        void * t114 = GM_LD(void *, (((uint8_t *)t113) + 1088));
                                        uint64_t t115 = GM_LD(uint64_t, ((((uint8_t *)t113) + 1088) + 8));
                                        uint32_t t116 = gm_l16;
                                        uint64_t t117 = (uint64_t)t116;
                                        uint64_t t118 = gm_f_us_cuv01fp(t117);
                                        if ((uintptr_t)t118 >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)t118, t115, gm_src8sgunk80orlb, 159, 32);
                                        uint8_t t119 = GM_LD(uint8_t, (((((uint8_t *)t114) + (uintptr_t)t118 * 184) + 24) + 0));
                                        uint8_t t120 = (uint8_t)(t119 == ((uint8_t)0ULL));
                                        if (!t120) {
                                            goto gm_lb15;
                                        }
                                        memmove(((uint8_t *)&F.l17), (((((uint8_t *)t114) + (uintptr_t)t118 * 184) + 24) + 8), 48);
                                        uint64_t t121 = gm_l8;
                                        void * t122 = gm_l0;
                                        gm_t88rcucg1b7uk t123 = gm_l9;
                                        gm_b16_8 t124 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t122, t123);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l22 = t124;
                                        void * t125 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                        uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                        uint64_t t127 = gm_l12;
                                        uint64_t t128 = gm_f_us_cuv01fp(t127);
                                        if ((uintptr_t)t128 >= (uintptr_t)(t126)) gm_trap_index((uintptr_t)t128, t126, gm_src8sgunk80orlb, 160, 27);
                                        uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)t125) + (uintptr_t)t128 * 8));
                                        uint64_t r130; bool o131 = __builtin_add_overflow(t121, t129, &r130);
                                        if (o131) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 160, 17);
                                        uint64_t t132 = r130;
                                        gm_l8 = t132;
                                        void * t133 = GM_LD(void *, ((uint8_t *)&F.l17));
                                        uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                                        uint64_t t135 = gm_l12;
                                        uint64_t t136 = gm_f_us_cuv01fp(t135);
                                        if ((uintptr_t)t136 >= (uintptr_t)(t134)) gm_trap_index((uintptr_t)t136, t134, gm_src8sgunk80orlb, 161, 23);
                                        gm_t88rcucg1b7uk t137 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t133) + (uintptr_t)t136 * 40) + 16));
                                        gm_l9 = t137;
                                        goto gm_lb14;
                                    }
gm_lb15:;
                                    {
                                        void * t138 = gm_l0;
                                        gm_tl874t2atnlh3 t139 = gm_l1;
                                        gm_b16_8 t140;
                                        GM_ST(void *, (uint8_t *)&t140, (void *)(gm_sqrl3kgomorsv));
                                        GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(38));
                                        gm_t76123gfet73g t141;
                                        gm_f_unsupported_70vvk1e((void *)&t141, (void *)NULL, cx, NULL, t138, t139, t140);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t141);
                                        goto gm_fdone;
                                    }
                                }
gm_lb14:;
                                goto gm_lb10;
                            }
gm_lb13:;
                            {
                                void * t142 = gm_l0;
                                gm_tl874t2atnlh3 t143 = gm_l1;
                                gm_b16_8 t144;
                                GM_ST(void *, (uint8_t *)&t144, (void *)(gm_sqrl3kgomorsv));
                                GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(38));
                                gm_t76123gfet73g t145;
                                gm_f_unsupported_70vvk1e((void *)&t145, (void *)NULL, cx, NULL, t142, t143, t144);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t145);
                                goto gm_fdone;
                            }
gm_lb16:;
                            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 149, 9);
                        }
gm_lb10:;
                        goto gm_lb8;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 148, 5);
                }
gm_lb8:;
            }
gm_lb7:;
            uint64_t t146 = gm_l11;
            uint64_t r147; bool o148 = __builtin_add_overflow(t146, ((uint64_t)1ULL), &r147);
            uint64_t t149 = r147;
            gm_l11 = t149;
        }
    }
gm_lb5:;
    {
        uint64_t t150 = gm_l7;
        uint64_t t151 = gm_l8;
        uint64_t r152; bool o153 = __builtin_add_overflow(t150, t151, &r152);
        if (o153) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 166, 12);
        uint64_t t154 = r152;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out))[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&(*(gm_t833u93mr4iob *)gm_out) + 8), t154);
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
__attribute__((aligned(4))) void gm_f_value_bytes_l7s126n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, uint64_t a5) {
    struct gm_fr_value_bytes_l7s126n F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* out */
    uint64_t gm_l5; /* off */
    uint64_t gm_l6; /* o */
    unsigned __int128 gm_l7; /* bits */
    double gm_l8; /* x */
    uint8_t gm_l9; /* b */
    void * gm_l10; /* x */
    gm_t88rcucg1b7uk gm_l11; /* inner */
    void * gm_l12; /* x */
    void * gm_l13; /* x */
    gm_b16_8 gm_l14; /* offs */
    uint32_t gm_l15; /* index */
    void * gm_l16; /* payload */
    gm_tcmkompbl09il gm_l17; /* u */
    uint64_t gm_l19; /* n */
    void * gm_l20; /* x */
    gm_t88rcucg1b7uk gm_l21; /* pt */
    void * gm_l22; /* a */
    gm_t88rcucg1b7uk gm_l23; /* elem */
    uint64_t gm_l24; /* esz */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* k */
    gm_b16_8 gm_l29; /* elems */
    gm_b16_8 gm_l30; /* offs */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    uint64_t gm_l33; /* k */
    uint32_t gm_l35; /* id */
    gm_t88rcucg1b7uk gm_l37; /* u */
    unsigned __int128 gm_l38; /* raw */
    gm_b16_8 gm_l39; /* offs */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* k */
    gm_tkaapcii86udk gm_l44; /* pl */
    gm_t88rcucg1b7uk gm_l45; /* p */
    gm_t88rcucg1b7uk gm_l46; /* p */
    gm_t88rcucg1b7uk gm_l47; /* inner */
    gm_t88rcucg1b7uk gm_l48; /* p */
    gm_t88rcucg1b7uk gm_l49; /* p */
    gm_t88rcucg1b7uk gm_l50; /* pointee */
    gm_tkaapcii86udk gm_l51; /* start */
    uint64_t gm_l52; /* len */
    gm_t88rcucg1b7uk gm_l53; /* elem */
    uint64_t gm_l54; /* p */
    uint64_t gm_l55; /* addr */
    uint32_t gm_l56; /* f */
    GM_STACK_CHECK(&F, 5252u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    uint8_t gm_l57;
    gm_tcfa6l7reoosn gm_l58;
    gm_b16_8 gm_l59;
    gm_tgglga8ro3isc gm_l60;
    gm_b16_8 gm_l61;
    gm_tcfa6l7reoosn gm_l62;
    gm_tgglga8ro3isc gm_l63;
    gm_tcfa6l7reoosn gm_l64;
    gm_t57euh612ligr gm_l65;
    gm_tcfa6l7reoosn gm_l66;
    gm_t53at80pfdus3 gm_l67;
    gm_thf8as07kj3uj gm_l68;
    gm_tcfa6l7reoosn gm_l69;
    gm_thf8as07kj3uj gm_l70;
    gm_tcfa6l7reoosn gm_l71;
    gm_tdlro53ljvkl0 gm_l72;
    gm_thf8as07kj3uj gm_l73;
    gm_tcfa6l7reoosn gm_l74;
    gm_t53at80pfdus3 gm_l75;
    gm_t88rcucg1b7uk gm_l76;
    gm_t53at80pfdus3 gm_l77;
    gm_t833u93mr4iob gm_l78;
    gm_t53at80pfdus3 gm_l79;
    uint8_t gm_l80;
    gm_t833u93mr4iob gm_l81;
    {
        uint64_t t61 = gm_l5;
        uint64_t t62 = gm_f_us_cuv01fp(t61);
        gm_l6 = t62;
        {
            {
                {
                    uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t64 = (uint8_t)(t63 == ((uint8_t)0ULL));
                    if (!t64) {
                        goto gm_lb2;
                    }
                    unsigned __int128 t65 = GM_LD(unsigned __int128, (((uint8_t *)&F.l2) + 16));
                    gm_l7 = t65;
                    void * t66 = gm_l0;
                    void * t67 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t69 = gm_l6;
                    uint64_t t70 = gm_l6;
                    void * t71 = gm_l0;
                    gm_t88rcucg1b7uk t72 = gm_l3;
                    uint64_t t73 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t71, t72);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t74 = gm_f_us_cuv01fp(t73);
                    uint64_t r75; bool o76 = __builtin_add_overflow(t70, t74, &r75);
                    if (o76) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 62, 41);
                    uint64_t t77 = r75;
                    uint64_t t78 = (uintptr_t)t69;
                    uint64_t t79 = (uintptr_t)t77;
                    if (t78 > t79 || t79 > (uintptr_t)t68) gm_trap_slice(t78, t79, t68, gm_src8sgunk80orlb, 62, 34);
                    uint64_t t80 = ((uintptr_t)t77) - (uintptr_t)t69;
                    gm_b16_8 t81;
                    GM_ST(void *, (uint8_t *)&t81, (void *)(((uint8_t *)t67 + (uintptr_t)t69 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(t80));
                    unsigned __int128 t82 = gm_l7;
                    gm_f_put_uint_cddin99(t66, t81, t82);
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t84 = (uint8_t)(t83 == ((uint8_t)1ULL));
                    if (!t84) {
                        goto gm_lb3;
                    }
                    double t85 = GM_LD(double, (((uint8_t *)&F.l2) + 16));
                    gm_l8 = t85;
                    void * t86 = gm_l0;
                    gm_t88rcucg1b7uk t87 = gm_l3;
                    double t88 = gm_l8;
                    void * t89 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t91 = gm_l6;
                    uint64_t t92 = gm_l6;
                    void * t93 = gm_l0;
                    gm_t88rcucg1b7uk t94 = gm_l3;
                    uint64_t t95 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t93, t94);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t96 = gm_f_us_cuv01fp(t95);
                    uint64_t r97; bool o98 = __builtin_add_overflow(t92, t96, &r97);
                    if (o98) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 63, 47);
                    uint64_t t99 = r97;
                    uint64_t t100 = (uintptr_t)t91;
                    uint64_t t101 = (uintptr_t)t99;
                    if (t100 > t101 || t101 > (uintptr_t)t90) gm_trap_slice(t100, t101, t90, gm_src8sgunk80orlb, 63, 40);
                    uint64_t t102 = ((uintptr_t)t99) - (uintptr_t)t91;
                    gm_b16_8 t103;
                    GM_ST(void *, (uint8_t *)&t103, (void *)(((uint8_t *)t89 + (uintptr_t)t91 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(t102));
                    gm_f_put_float_fiad72d(t86, GM_CONV(gm_csguankko4lmhd, t87), t88, t103);
                    goto gm_lb1;
                }
gm_lb3:;
                {
                    uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t105 = (uint8_t)(t104 == ((uint8_t)2ULL));
                    if (!t105) {
                        goto gm_lb4;
                    }
                    uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 16));
                    gm_l9 = t106;
                    void * t107 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t108 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t109 = gm_l6;
                    if ((uintptr_t)t109 >= (uintptr_t)(t108)) gm_trap_index((uintptr_t)t109, t108, gm_src8sgunk80orlb, 64, 23);
                    uint8_t t110 = gm_l9;
                    if (t110) {
                        gm_l57 = ((uint8_t)1ULL);
                    } else {
                        gm_l57 = ((uint8_t)0ULL);
                    }
                    uint8_t t111 = gm_l57;
                    GM_ST(uint8_t, (((uint8_t *)t107) + (uintptr_t)t109 * 1), t111);
                    goto gm_lb1;
                }
gm_lb4:;
                {
                    uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t113 = (uint8_t)(t112 == ((uint8_t)6ULL));
                    if (!t113) {
                        goto gm_lb5;
                    }
                    goto gm_lb1;
                }
gm_lb5:;
                {
                    uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t115 = (uint8_t)(t114 == ((uint8_t)7ULL));
                    if (!t115) {
                        goto gm_lb6;
                    }
                    void * t116 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
                    gm_l10 = t116;
                    void * t117 = gm_l0;
                    gm_t88rcucg1b7uk t118 = gm_l3;
                    gm_t88rcucg1b7uk t119 = GM_CONV(gm_t88rcucg1b7uk, gm_f_opt_inner_q4ga84u(t117, GM_CONV(gm_csguankko4lmhd, t118)));
                    gm_l11 = t119;
                    {
                        void * t120 = gm_l0;
                        gm_tl874t2atnlh3 t121 = gm_l1;
                        void * t122 = gm_l10;
                        gm_t88rcucg1b7uk t123 = gm_l11;
                        gm_b16_8 t124 = gm_l4;
                        uint64_t t125 = gm_l5;
                        gm_t6evimt84lndq t126 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t122));
                        gm_f_value_bytes_l7s126n((void *)&gm_l58, (void *)NULL, cx, NULL, t120, t121, &t126, t123, t124, t125);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t127 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                    if (t127) {
                    } else {
                        gm_t76123gfet73g t128 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l58) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t128);
                        goto gm_fdone;
                    }
                    void * t129 = gm_l0;
                    void * t130 = GM_LD(void *, (((uint8_t *)t129) + 8));
                    gm_t88rcucg1b7uk t131 = gm_l11;
                    uint8_t t132 = gm_f_nullable_pointer_7qfmn6o(t130, GM_CONV(gm_csguankko4lmhd, t131));
                    uint8_t t133 = (uint8_t)!t132;
                    if (t133) {
                        void * t134 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t136 = gm_l5;
                        void * t137 = gm_l0;
                        gm_t88rcucg1b7uk t138 = gm_l3;
                        gm_b16_8 t139 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t137, t138);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l59 = t139;
                        void * t140 = GM_LD(void *, ((uint8_t *)&gm_l59));
                        uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l59) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t141)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t141, gm_src8sgunk80orlb, 69, 69);
                        uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)t140) + (uintptr_t)((uint64_t)1ULL) * 8));
                        uint64_t r143; bool o144 = __builtin_add_overflow(t136, t142, &r143);
                        if (o144) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 69, 63);
                        uint64_t t145 = r143;
                        uint64_t t146 = gm_f_us_cuv01fp(t145);
                        if ((uintptr_t)t146 >= (uintptr_t)(t135)) gm_trap_index((uintptr_t)t146, t135, gm_src8sgunk80orlb, 69, 56);
                        GM_ST(uint8_t, (((uint8_t *)t134) + (uintptr_t)t146 * 1), ((uint8_t)1ULL));
                    }
                    goto gm_lb1;
                }
gm_lb6:;
            }
            {
                {
                    uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t148 = (uint8_t)(t147 == ((uint8_t)8ULL));
                    if (!t148) {
                        goto gm_lb7;
                    }
                    void * t149 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
                    gm_l12 = t149;
                    {
                        void * t150 = gm_l0;
                        gm_tl874t2atnlh3 t151 = gm_l1;
                        void * t152 = gm_l12;
                        void * t153 = gm_l0;
                        gm_t88rcucg1b7uk t154 = gm_l3;
                        gm_t6evimt84lndq t155 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t152));
                        gm_tgglga8ro3isc t156 = gm_f_res_types_pg7rvck(t153, GM_CONV(gm_csguankko4lmhd, t154));
                        gm_l60 = t156;
                        gm_t88rcucg1b7uk t157 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l60) + 4));
                        gm_b16_8 t158 = gm_l4;
                        uint64_t t159 = gm_l5;
                        void * t160 = gm_l0;
                        gm_t88rcucg1b7uk t161 = gm_l3;
                        gm_b16_8 t162 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t160, t161);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l61 = t162;
                        void * t163 = GM_LD(void *, ((uint8_t *)&gm_l61));
                        uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l61) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t164)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t164, gm_src8sgunk80orlb, 71, 77);
                        uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)t163) + (uintptr_t)((uint64_t)1ULL) * 8));
                        uint64_t r166; bool o167 = __builtin_add_overflow(t159, t165, &r166);
                        if (o167) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 71, 71);
                        uint64_t t168 = r166;
                        gm_f_value_bytes_l7s126n((void *)&gm_l62, (void *)NULL, cx, NULL, t150, t151, &t155, t157, t158, t168);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t169 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                    if (t169) {
                    } else {
                        gm_t76123gfet73g t170 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l62) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t170);
                        goto gm_fdone;
                    }
                    goto gm_lb1;
                }
gm_lb7:;
                {
                    uint8_t t171 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t172 = (uint8_t)(t171 == ((uint8_t)9ULL));
                    if (!t172) {
                        goto gm_lb8;
                    }
                    void * t173 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
                    gm_l13 = t173;
                    void * t174 = gm_l0;
                    gm_t88rcucg1b7uk t175 = gm_l3;
                    gm_b16_8 t176 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t174, t175);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l14 = t176;
                    void * t177 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t178 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t179 = gm_l5;
                    void * t180 = GM_LD(void *, ((uint8_t *)&gm_l14));
                    uint64_t t181 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t181)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t181, gm_src8sgunk80orlb, 74, 26);
                    uint64_t t182 = GM_LD(uint64_t, (((uint8_t *)t180) + (uintptr_t)((uint64_t)0ULL) * 8));
                    uint64_t r183; bool o184 = __builtin_add_overflow(t179, t182, &r183);
                    if (o184) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 74, 20);
                    uint64_t t185 = r183;
                    uint64_t t186 = gm_f_us_cuv01fp(t185);
                    if ((uintptr_t)t186 >= (uintptr_t)(t178)) gm_trap_index((uintptr_t)t186, t178, gm_src8sgunk80orlb, 74, 13);
                    GM_ST(uint8_t, (((uint8_t *)t177) + (uintptr_t)t186 * 1), ((uint8_t)1ULL));
                    {
                        void * t187 = gm_l0;
                        gm_tl874t2atnlh3 t188 = gm_l1;
                        void * t189 = gm_l13;
                        void * t190 = gm_l0;
                        gm_t88rcucg1b7uk t191 = gm_l3;
                        gm_t6evimt84lndq t192 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t189));
                        gm_tgglga8ro3isc t193 = gm_f_res_types_pg7rvck(t190, GM_CONV(gm_csguankko4lmhd, t191));
                        gm_l63 = t193;
                        gm_t88rcucg1b7uk t194 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l63));
                        gm_b16_8 t195 = gm_l4;
                        uint64_t t196 = gm_l5;
                        void * t197 = GM_LD(void *, ((uint8_t *)&gm_l14));
                        uint64_t t198 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t198)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t198, gm_src8sgunk80orlb, 75, 72);
                        uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)t197) + (uintptr_t)((uint64_t)1ULL) * 8));
                        uint64_t r200; bool o201 = __builtin_add_overflow(t196, t199, &r200);
                        if (o201) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 75, 66);
                        uint64_t t202 = r200;
                        gm_f_value_bytes_l7s126n((void *)&gm_l64, (void *)NULL, cx, NULL, t187, t188, &t192, t194, t195, t202);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t203 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                    if (t203) {
                    } else {
                        gm_t76123gfet73g t204 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l64) + 8));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t204);
                        goto gm_fdone;
                    }
                    goto gm_lb1;
                }
gm_lb8:;
            }
            {
                {
                    uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t206 = (uint8_t)(t205 == ((uint8_t)10ULL));
                    if (!t206) {
                        goto gm_lb9;
                    }
                    uint32_t t207 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 16));
                    gm_l15 = t207;
                    void * t208 = GM_LD(void *, ((((uint8_t *)&F.l2) + 16) + 8));
                    gm_l16 = t208;
                    {
                        void * t209 = gm_l0;
                        gm_t88rcucg1b7uk t210 = gm_l3;
                        gm_f_union_def_h8mvf4t((void *)&gm_l65, t209, GM_CONV(gm_csguankko4lmhd, t210));
                    }
                    uint8_t t211 = (uint8_t)((((uint8_t *)&gm_l65))[24] != 0);
                    if (t211) {
                        memmove(((uint8_t *)&gm_l17), ((uint8_t *)&gm_l65), 24);
                    } else {
                        void * t212 = gm_l0;
                        gm_tl874t2atnlh3 t213 = gm_l1;
                        gm_b16_8 t214;
                        GM_ST(void *, (uint8_t *)&t214, (void *)(gm_sa6mes8ijqmmk));
                        GM_ST(uintptr_t, (uint8_t *)&t214 + 8, (uintptr_t)(29));
                        gm_t76123gfet73g t215;
                        gm_f_unsupported_70vvk1e((void *)&t215, (void *)NULL, cx, NULL, t212, t213, t214);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t215);
                        goto gm_fdone;
                    }
                    {
                        void * t216 = GM_LD(void *, (((uint8_t *)&gm_l17) + 8));
                        uint64_t t217 = GM_LD(uint64_t, ((((uint8_t *)&gm_l17) + 8) + 8));
                        uint32_t t218 = gm_l15;
                        uint64_t t219 = (uint64_t)t218;
                        uint64_t t220 = gm_f_us_cuv01fp(t219);
                        if ((uintptr_t)t220 >= (uintptr_t)(t217)) gm_trap_index((uintptr_t)t220, t217, gm_src8sgunk80orlb, 79, 19);
                        memmove(((uint8_t *)&F.l18), (((uint8_t *)t216) + (uintptr_t)t220 * 72), 72);
                    }
                    void * t221 = gm_l0;
                    gm_t88rcucg1b7uk t222 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l17));
                    uint64_t t223 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t221, t222);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint64_t t224 = gm_f_us_cuv01fp(t223);
                    gm_l19 = t224;
                    {
                        void * t225 = gm_l0;
                        void * t226 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t227 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t228 = gm_l6;
                        uint64_t t229 = gm_l6;
                        uint64_t t230 = gm_l19;
                        uint64_t r231; bool o232 = __builtin_add_overflow(t229, t230, &r231);
                        if (o232) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 81, 31);
                        uint64_t t233 = r231;
                        uint64_t t234 = (uintptr_t)t228;
                        uint64_t t235 = (uintptr_t)t233;
                        if (t234 > t235 || t235 > (uintptr_t)t227) gm_trap_slice(t234, t235, t227, gm_src8sgunk80orlb, 81, 24);
                        uint64_t t236 = ((uintptr_t)t233) - (uintptr_t)t228;
                        gm_b16_8 t237;
                        GM_ST(void *, (uint8_t *)&t237, (void *)(((uint8_t *)t226 + (uintptr_t)t228 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t237 + 8, (uintptr_t)(t236));
                        unsigned __int128 t238 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l18) + 24)));
                        gm_f_put_uint_cddin99(t225, t237, t238);
                    }
                    {
                        {
                            uint8_t t239 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l16)) != 0);
                            if (!t239) {
                                goto gm_lb11;
                            }
                            void * t240 = GM_LD(void *, ((uint8_t *)&gm_l16));
                            gm_l20 = t240;
                            uint8_t t241 = (uint8_t)(((((uint8_t *)&F.l18) + 16))[4] != 0);
                            if (t241) {
                                gm_t88rcucg1b7uk t242 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l18) + 16));
                                gm_l21 = t242;
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
                                void * t243 = gm_l0;
                                gm_tl874t2atnlh3 t244 = gm_l1;
                                void * t245 = gm_l20;
                                gm_t88rcucg1b7uk t246 = gm_l21;
                                gm_b16_8 t247 = gm_l4;
                                uint64_t t248 = gm_l5;
                                void * t249 = gm_l0;
                                gm_t88rcucg1b7uk t250 = gm_l3;
                                gm_t6evimt84lndq t251 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t245));
                                uint64_t t252 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t249, t250);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint64_t r253; bool o254 = __builtin_add_overflow(t248, t252, &r253);
                                if (o254) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 84, 54);
                                uint64_t t255 = r253;
                                gm_f_value_bytes_l7s126n((void *)&gm_l66, (void *)NULL, cx, NULL, t243, t244, &t251, t246, t247, t255);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t256 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                            if (t256) {
                            } else {
                                gm_t76123gfet73g t257 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l66) + 8));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t257);
                                goto gm_fdone;
                            }
                            goto gm_lb10;
                        }
gm_lb11:;
                    }
gm_lb10:;
                    goto gm_lb1;
                }
gm_lb9:;
            }
            {
                {
                    {
                        uint8_t t258 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                        uint8_t t259 = (uint8_t)(t258 == ((uint8_t)3ULL));
                        if (!t259) {
                            goto gm_lb12;
                        }
                        void * t260 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
                        gm_l22 = t260;
                        void * t261 = gm_l0;
                        gm_t88rcucg1b7uk t262 = gm_l3;
                        gm_f_kind_htv6093((void *)&gm_l67, t261, GM_CONV(gm_csguankko4lmhd, t262));
                        {
                            {
                                uint8_t t263 = GM_LD(uint8_t, (((uint8_t *)&gm_l67) + 0));
                                uint8_t t264 = (uint8_t)(t263 == ((uint8_t)11ULL));
                                if (!t264) {
                                    goto gm_lb14;
                                }
                                gm_t88rcucg1b7uk t265 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l67) + 8) + 8));
                                gm_l23 = t265;
                                void * t266 = gm_l0;
                                gm_t88rcucg1b7uk t267 = gm_l23;
                                uint64_t t268 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t266, t267);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l24 = t268;
                                void * t269 = gm_l22;
                                gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)t269));
                                gm_b16_8 t271 = GM_LD(gm_b16_8, ((uint8_t *)&t270));
                                gm_l25 = t271;
                                gm_l26 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t272 = gm_l26;
                                        uint64_t t273 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                        uint8_t t274 = (t272 < t273);
                                        if (!t274) {
                                            goto gm_lb15;
                                        }
                                        {
                                            {
                                                uint64_t t275 = gm_l26;
                                                void * t276 = GM_LD(void *, ((uint8_t *)&gm_l25));
                                                uint64_t t277 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                                uint64_t t278 = gm_l26;
                                                gm_t6evimt84lndq t279 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t276) + (uintptr_t)t278 * 48));
                                                GM_ST(uint64_t, (uint8_t *)&gm_l68 + 8, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&gm_l68 + 0), t275);
                                                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&gm_l68 + 16), t279);
                                            }
                                            {
                                                {
                                                    uint64_t t280 = GM_LD(uint64_t, ((uint8_t *)&gm_l68));
                                                    gm_l27 = t280;
                                                    {
                                                        memmove(((uint8_t *)&F.l28), (((uint8_t *)&gm_l68) + 16), 48);
                                                    }
                                                    {
                                                        void * t281 = gm_l0;
                                                        gm_tl874t2atnlh3 t282 = gm_l1;
                                                        gm_t88rcucg1b7uk t283 = gm_l23;
                                                        gm_b16_8 t284 = gm_l4;
                                                        uint64_t t285 = gm_l5;
                                                        uint64_t t286 = gm_l27;
                                                        gm_t6evimt84lndq t287 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l28));
                                                        uint64_t t288 = (uint64_t)t286;
                                                        uint64_t t289 = gm_l24;
                                                        uint64_t r290; bool o291 = __builtin_mul_overflow(t288, t289, &r290);
                                                        if (o291) gm_trap("integer overflow in `*`", gm_src8sgunk80orlb, 90, 94);
                                                        uint64_t t292 = r290;
                                                        uint64_t r293; bool o294 = __builtin_add_overflow(t285, t292, &r293);
                                                        if (o294) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 90, 88);
                                                        uint64_t t295 = r293;
                                                        gm_f_value_bytes_l7s126n((void *)&gm_l69, (void *)NULL, cx, NULL, t281, t282, &t287, t283, t284, t295);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                    }
                                                    uint8_t t296 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                                                    if (t296) {
                                                    } else {
                                                        gm_t76123gfet73g t297 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l69) + 8));
                                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t297);
                                                        goto gm_fdone;
                                                    }
                                                    goto gm_lb18;
                                                }
gm_lb19:;
                                                gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 90, 17);
                                            }
gm_lb18:;
                                        }
gm_lb17:;
                                        uint64_t t298 = gm_l26;
                                        uint64_t r299; bool o300 = __builtin_add_overflow(t298, ((uint64_t)1ULL), &r299);
                                        uint64_t t301 = r299;
                                        gm_l26 = t301;
                                    }
                                }
gm_lb15:;
                                goto gm_lb13;
                            }
gm_lb14:;
                            {
                                uint8_t t302 = GM_LD(uint8_t, (((uint8_t *)&gm_l67) + 0));
                                uint8_t t303 = (uint8_t)(t302 == ((uint8_t)12ULL));
                                if (!t303) {
                                    goto gm_lb20;
                                }
                                gm_b16_8 t304 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                                gm_l29 = t304;
                                void * t305 = gm_l0;
                                gm_t88rcucg1b7uk t306 = gm_l3;
                                gm_b16_8 t307 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t305, t306);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l30 = t307;
                                void * t308 = gm_l22;
                                gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)t308));
                                gm_b16_8 t310 = GM_LD(gm_b16_8, ((uint8_t *)&t309));
                                gm_l31 = t310;
                                gm_l32 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t311 = gm_l32;
                                        uint64_t t312 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                        uint8_t t313 = (t311 < t312);
                                        if (!t313) {
                                            goto gm_lb21;
                                        }
                                        {
                                            {
                                                uint64_t t314 = gm_l32;
                                                void * t315 = GM_LD(void *, ((uint8_t *)&gm_l31));
                                                uint64_t t316 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                                uint64_t t317 = gm_l32;
                                                gm_t6evimt84lndq t318 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t315) + (uintptr_t)t317 * 48));
                                                GM_ST(uint64_t, (uint8_t *)&gm_l70 + 8, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&gm_l70 + 0), t314);
                                                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&gm_l70 + 16), t318);
                                            }
                                            {
                                                {
                                                    uint64_t t319 = GM_LD(uint64_t, ((uint8_t *)&gm_l70));
                                                    gm_l33 = t319;
                                                    {
                                                        memmove(((uint8_t *)&F.l34), (((uint8_t *)&gm_l70) + 16), 48);
                                                    }
                                                    {
                                                        void * t320 = gm_l0;
                                                        gm_tl874t2atnlh3 t321 = gm_l1;
                                                        void * t322 = GM_LD(void *, ((uint8_t *)&gm_l29));
                                                        uint64_t t323 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                                                        uint64_t t324 = gm_l33;
                                                        if ((uintptr_t)t324 >= (uintptr_t)(t323)) gm_trap_index((uintptr_t)t324, t323, gm_src8sgunk80orlb, 94, 77);
                                                        gm_t88rcucg1b7uk t325 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t322) + (uintptr_t)t324 * 4));
                                                        gm_b16_8 t326 = gm_l4;
                                                        uint64_t t327 = gm_l5;
                                                        void * t328 = GM_LD(void *, ((uint8_t *)&gm_l30));
                                                        uint64_t t329 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                                        uint64_t t330 = gm_l33;
                                                        if ((uintptr_t)t330 >= (uintptr_t)(t329)) gm_trap_index((uintptr_t)t330, t329, gm_src8sgunk80orlb, 94, 98);
                                                        uint64_t t331 = GM_LD(uint64_t, (((uint8_t *)t328) + (uintptr_t)t330 * 8));
                                                        uint64_t r332; bool o333 = __builtin_add_overflow(t327, t331, &r332);
                                                        if (o333) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 94, 92);
                                                        uint64_t t334 = r332;
                                                        gm_t6evimt84lndq t335 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l34));
                                                        gm_f_value_bytes_l7s126n((void *)&gm_l71, (void *)NULL, cx, NULL, t320, t321, &t335, t325, t326, t334);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                    }
                                                    uint8_t t336 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                                                    if (t336) {
                                                    } else {
                                                        gm_t76123gfet73g t337 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l71) + 8));
                                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t337);
                                                        goto gm_fdone;
                                                    }
                                                    goto gm_lb24;
                                                }
gm_lb25:;
                                                gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 94, 17);
                                            }
gm_lb24:;
                                        }
gm_lb23:;
                                        uint64_t t338 = gm_l32;
                                        uint64_t r339; bool o340 = __builtin_add_overflow(t338, ((uint64_t)1ULL), &r339);
                                        uint64_t t341 = r339;
                                        gm_l32 = t341;
                                    }
                                }
gm_lb21:;
                                goto gm_lb13;
                            }
gm_lb20:;
                            {
                                uint8_t t342 = GM_LD(uint8_t, (((uint8_t *)&gm_l67) + 0));
                                uint8_t t343 = (uint8_t)(t342 == ((uint8_t)17ULL));
                                if (!t343) {
                                    goto gm_lb26;
                                }
                                uint32_t t344 = GM_LD(uint32_t, (((uint8_t *)&gm_l67) + 8));
                                gm_l35 = t344;
                                void * t345 = gm_l0;
                                void * t346 = GM_LD(void *, (((uint8_t *)t345) + 8));
                                void * t347 = GM_LD(void *, (((uint8_t *)t346) + 1088));
                                uint64_t t348 = GM_LD(uint64_t, ((((uint8_t *)t346) + 1088) + 8));
                                uint32_t t349 = gm_l35;
                                uint64_t t350 = (uint64_t)t349;
                                uint64_t t351 = gm_f_us_cuv01fp(t350);
                                if ((uintptr_t)t351 >= (uintptr_t)(t348)) gm_trap_index((uintptr_t)t351, t348, gm_src8sgunk80orlb, 96, 34);
                                {
                                    {
                                        uint8_t t352 = GM_LD(uint8_t, (((((uint8_t *)t347) + (uintptr_t)t351 * 184) + 24) + 0));
                                        uint8_t t353 = (uint8_t)(t352 == ((uint8_t)0ULL));
                                        if (!t353) {
                                            goto gm_lb28;
                                        }
                                        memmove(((uint8_t *)&F.l36), (((((uint8_t *)t347) + (uintptr_t)t351 * 184) + 24) + 8), 48);
                                        {
                                            {
                                                uint8_t t354 = (uint8_t)(((((uint8_t *)&F.l36) + 40))[4] != 0);
                                                if (!t354) {
                                                    goto gm_lb30;
                                                }
                                                gm_t88rcucg1b7uk t355 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l36) + 40));
                                                gm_l37 = t355;
                                                {
                                                    void * t356 = gm_l0;
                                                    gm_tl874t2atnlh3 t357 = gm_l1;
                                                    gm_t88rcucg1b7uk t358 = gm_l3;
                                                    gm_t6evimt84lndq t359 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l2));
                                                    gm_f_const_bits_1of8c6t((void *)&gm_l72, (void *)NULL, cx, NULL, t356, t357, &t359, t358);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
                                                uint8_t t360 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                                                if (t360) {
                                                } else {
                                                    gm_t76123gfet73g t361 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l72) + 16));
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t361);
                                                    goto gm_fdone;
                                                }
                                                unsigned __int128 t362 = GM_LD(unsigned __int128, (((uint8_t *)&gm_l72) + 16));
                                                gm_l38 = t362;
                                                void * t363 = gm_l0;
                                                void * t364 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                uint64_t t365 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                uint64_t t366 = gm_l6;
                                                uint64_t t367 = gm_l6;
                                                void * t368 = gm_l0;
                                                gm_t88rcucg1b7uk t369 = gm_l37;
                                                uint64_t t370 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t368, t369);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint64_t t371 = gm_f_us_cuv01fp(t370);
                                                uint64_t r372; bool o373 = __builtin_add_overflow(t367, t371, &r372);
                                                if (o373) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 100, 43);
                                                uint64_t t374 = r372;
                                                uint64_t t375 = (uintptr_t)t366;
                                                uint64_t t376 = (uintptr_t)t374;
                                                if (t375 > t376 || t376 > (uintptr_t)t365) gm_trap_slice(t375, t376, t365, gm_src8sgunk80orlb, 100, 36);
                                                uint64_t t377 = ((uintptr_t)t374) - (uintptr_t)t366;
                                                gm_b16_8 t378;
                                                GM_ST(void *, (uint8_t *)&t378, (void *)(((uint8_t *)t364 + (uintptr_t)t366 * 1)));
                                                GM_ST(uintptr_t, (uint8_t *)&t378 + 8, (uintptr_t)(t377));
                                                unsigned __int128 t379 = gm_l38;
                                                gm_f_put_uint_cddin99(t363, t378, t379);
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
gm_lb30:;
                                        }
gm_lb29:;
                                        void * t380 = gm_l0;
                                        gm_t88rcucg1b7uk t381 = gm_l3;
                                        gm_b16_8 t382 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t380, t381);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l39 = t382;
                                        void * t383 = gm_l22;
                                        gm_b16_8 t384 = GM_LD(gm_b16_8, ((uint8_t *)t383));
                                        gm_b16_8 t385 = GM_LD(gm_b16_8, ((uint8_t *)&t384));
                                        gm_l40 = t385;
                                        gm_l41 = ((uint64_t)0ULL);
                                        {
                                            for (;;) {
                                                uint64_t t386 = gm_l41;
                                                uint64_t t387 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                                                uint8_t t388 = (t386 < t387);
                                                if (!t388) {
                                                    goto gm_lb31;
                                                }
                                                {
                                                    {
                                                        uint64_t t389 = gm_l41;
                                                        void * t390 = GM_LD(void *, ((uint8_t *)&gm_l40));
                                                        uint64_t t391 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                                                        uint64_t t392 = gm_l41;
                                                        gm_t6evimt84lndq t393 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)t390) + (uintptr_t)t392 * 48));
                                                        GM_ST(uint64_t, (uint8_t *)&gm_l73 + 8, 0);
                                                        GM_ST(uint64_t, ((uint8_t *)&gm_l73 + 0), t389);
                                                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&gm_l73 + 16), t393);
                                                    }
                                                    {
                                                        {
                                                            uint64_t t394 = GM_LD(uint64_t, ((uint8_t *)&gm_l73));
                                                            gm_l42 = t394;
                                                            {
                                                                memmove(((uint8_t *)&F.l43), (((uint8_t *)&gm_l73) + 16), 48);
                                                            }
                                                            {
                                                                void * t395 = gm_l0;
                                                                gm_tl874t2atnlh3 t396 = gm_l1;
                                                                void * t397 = GM_LD(void *, ((uint8_t *)&F.l36));
                                                                uint64_t t398 = GM_LD(uint64_t, (((uint8_t *)&F.l36) + 8));
                                                                uint64_t t399 = gm_l42;
                                                                if ((uintptr_t)t399 >= (uintptr_t)(t398)) gm_trap_index((uintptr_t)t399, t398, gm_src8sgunk80orlb, 104, 81);
                                                                gm_t88rcucg1b7uk t400 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t397) + (uintptr_t)t399 * 40) + 16));
                                                                gm_b16_8 t401 = gm_l4;
                                                                uint64_t t402 = gm_l5;
                                                                void * t403 = GM_LD(void *, ((uint8_t *)&gm_l39));
                                                                uint64_t t404 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                                                                uint64_t t405 = gm_l42;
                                                                if ((uintptr_t)t405 >= (uintptr_t)(t404)) gm_trap_index((uintptr_t)t405, t404, gm_src8sgunk80orlb, 104, 109);
                                                                uint64_t t406 = GM_LD(uint64_t, (((uint8_t *)t403) + (uintptr_t)t405 * 8));
                                                                uint64_t r407; bool o408 = __builtin_add_overflow(t402, t406, &r407);
                                                                if (o408) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 104, 103);
                                                                uint64_t t409 = r407;
                                                                gm_t6evimt84lndq t410 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)&F.l43));
                                                                gm_f_value_bytes_l7s126n((void *)&gm_l74, (void *)NULL, cx, NULL, t395, t396, &t410, t400, t401, t409);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                            }
                                                            uint8_t t411 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                                                            if (t411) {
                                                            } else {
                                                                gm_t76123gfet73g t412 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l74) + 8));
                                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t412);
                                                                goto gm_fdone;
                                                            }
                                                            goto gm_lb34;
                                                        }
gm_lb35:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 104, 21);
                                                    }
gm_lb34:;
                                                }
gm_lb33:;
                                                uint64_t t413 = gm_l41;
                                                uint64_t r414; bool o415 = __builtin_add_overflow(t413, ((uint64_t)1ULL), &r414);
                                                uint64_t t416 = r414;
                                                gm_l41 = t416;
                                            }
                                        }
gm_lb31:;
                                        goto gm_lb27;
                                    }
gm_lb28:;
                                    {
                                        void * t417 = gm_l0;
                                        gm_tl874t2atnlh3 t418 = gm_l1;
                                        gm_b16_8 t419;
                                        GM_ST(void *, (uint8_t *)&t419, (void *)(gm_s67fd8s6hf84e));
                                        GM_ST(uintptr_t, (uint8_t *)&t419 + 8, (uintptr_t)(23));
                                        gm_t76123gfet73g t420;
                                        gm_f_unsupported_70vvk1e((void *)&t420, (void *)NULL, cx, NULL, t417, t418, t419);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t420);
                                        goto gm_fdone;
                                    }
gm_lb36:;
                                    gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 96, 28);
                                }
gm_lb27:;
                                goto gm_lb13;
                            }
gm_lb26:;
                            {
                                void * t421 = gm_l0;
                                gm_tl874t2atnlh3 t422 = gm_l1;
                                gm_b16_8 t423;
                                GM_ST(void *, (uint8_t *)&t423, (void *)(gm_s67fd8s6hf84e));
                                GM_ST(uintptr_t, (uint8_t *)&t423 + 8, (uintptr_t)(23));
                                gm_t76123gfet73g t424;
                                gm_f_unsupported_70vvk1e((void *)&t424, (void *)NULL, cx, NULL, t421, t422, t423);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t424);
                                goto gm_fdone;
                            }
gm_lb37:;
                            gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 87, 20);
                        }
gm_lb13:;
                    }
                    {
                        goto gm_lb1;
                    }
                }
gm_lb12:;
            }
            {
                {
                    uint8_t t425 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t426 = (uint8_t)(t425 == ((uint8_t)4ULL));
                    if (!t426) {
                        goto gm_lb38;
                    }
                    {
                        memmove(((uint8_t *)&gm_l44), (((uint8_t *)&F.l2) + 16), 24);
                    }
                    {
                        void * t427 = gm_l0;
                        gm_t88rcucg1b7uk t428 = gm_l3;
                        gm_f_kind_htv6093((void *)&gm_l75, t427, GM_CONV(gm_csguankko4lmhd, t428));
                    }
                    {
                        {
                            {
                                {
                                    uint8_t t429 = GM_LD(uint8_t, (((uint8_t *)&gm_l75) + 0));
                                    uint8_t t430 = (uint8_t)(t429 == ((uint8_t)8ULL));
                                    if (!t430) {
                                        goto gm_lb42;
                                    }
                                    gm_t88rcucg1b7uk t431 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l75) + 8) + 4));
                                    gm_l45 = t431;
                                    goto gm_lb41;
                                }
gm_lb42:;
                                {
                                    uint8_t t432 = GM_LD(uint8_t, (((uint8_t *)&gm_l75) + 0));
                                    uint8_t t433 = (uint8_t)(t432 == ((uint8_t)9ULL));
                                    if (!t433) {
                                        goto gm_lb43;
                                    }
                                    gm_t88rcucg1b7uk t434 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l75) + 8) + 4));
                                    gm_l45 = t434;
                                    goto gm_lb41;
                                }
gm_lb43:;
                                goto gm_lb40;
                            }
gm_lb41:;
                            gm_t88rcucg1b7uk t435 = gm_l45;
                            gm_l50 = t435;
                            goto gm_lb39;
                        }
gm_lb40:;
                        {
                            uint8_t t436 = GM_LD(uint8_t, (((uint8_t *)&gm_l75) + 0));
                            uint8_t t437 = (uint8_t)(t436 == ((uint8_t)13ULL));
                            if (!t437) {
                                goto gm_lb44;
                            }
                            gm_t88rcucg1b7uk t438 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l75) + 8));
                            gm_l47 = t438;
                            void * t439 = gm_l0;
                            gm_t88rcucg1b7uk t440 = gm_l47;
                            gm_f_kind_htv6093((void *)&gm_l77, t439, GM_CONV(gm_csguankko4lmhd, t440));
                            {
                                {
                                    {
                                        {
                                            uint8_t t441 = GM_LD(uint8_t, (((uint8_t *)&gm_l77) + 0));
                                            uint8_t t442 = (uint8_t)(t441 == ((uint8_t)8ULL));
                                            if (!t442) {
                                                goto gm_lb48;
                                            }
                                            gm_t88rcucg1b7uk t443 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l77) + 8) + 4));
                                            gm_l48 = t443;
                                            goto gm_lb47;
                                        }
gm_lb48:;
                                        {
                                            uint8_t t444 = GM_LD(uint8_t, (((uint8_t *)&gm_l77) + 0));
                                            uint8_t t445 = (uint8_t)(t444 == ((uint8_t)9ULL));
                                            if (!t445) {
                                                goto gm_lb49;
                                            }
                                            gm_t88rcucg1b7uk t446 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l77) + 8) + 4));
                                            gm_l48 = t446;
                                            goto gm_lb47;
                                        }
gm_lb49:;
                                        goto gm_lb46;
                                    }
gm_lb47:;
                                    gm_t88rcucg1b7uk t447 = gm_l48;
                                    gm_l76 = t447;
                                    goto gm_lb45;
                                }
gm_lb46:;
                                {
                                    void * t448 = gm_l0;
                                    gm_tl874t2atnlh3 t449 = gm_l1;
                                    gm_b16_8 t450;
                                    GM_ST(void *, (uint8_t *)&t450, (void *)(gm_sgiqpj781s1a9));
                                    GM_ST(uintptr_t, (uint8_t *)&t450 + 8, (uintptr_t)(35));
                                    gm_t76123gfet73g t451;
                                    gm_f_unsupported_70vvk1e((void *)&t451, (void *)NULL, cx, NULL, t448, t449, t450);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t451);
                                    goto gm_fdone;
                                }
gm_lb50:;
                                gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 113, 37);
                            }
gm_lb45:;
                            gm_t88rcucg1b7uk t452 = gm_l76;
                            gm_l50 = t452;
                            goto gm_lb39;
                        }
gm_lb44:;
                        {
                            void * t453 = gm_l0;
                            gm_tl874t2atnlh3 t454 = gm_l1;
                            gm_b16_8 t455;
                            GM_ST(void *, (uint8_t *)&t455, (void *)(gm_sgiqpj781s1a9));
                            GM_ST(uintptr_t, (uint8_t *)&t455 + 8, (uintptr_t)(35));
                            gm_t76123gfet73g t456;
                            gm_f_unsupported_70vvk1e((void *)&t456, (void *)NULL, cx, NULL, t453, t454, t455);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t456);
                            goto gm_fdone;
                        }
gm_lb51:;
                        gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 111, 27);
                    }
gm_lb39:;
                    {
                        void * t457 = gm_l0;
                        void * t458 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t459 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        uint64_t t460 = gm_l6;
                        uint64_t t461 = (uintptr_t)t460;
                        uint64_t t462 = (uintptr_t)t459;
                        if (t461 > t462 || t462 > (uintptr_t)t459) gm_trap_slice(t461, t462, t459, gm_src8sgunk80orlb, 119, 23);
                        uint64_t t463 = ((uintptr_t)t459) - (uintptr_t)t460;
                        gm_b16_8 t464;
                        GM_ST(void *, (uint8_t *)&t464, (void *)(((uint8_t *)t458 + (uintptr_t)t460 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t464 + 8, (uintptr_t)(t463));
                        void * t465 = gm_l0;
                        gm_tl874t2atnlh3 t466 = gm_l1;
                        gm_t88rcucg1b7uk t467 = gm_l50;
                        gm_tkaapcii86udk t468 = GM_LD(gm_tkaapcii86udk, ((uint8_t *)&gm_l44));
                        gm_f_place_addr_m751en7((void *)&gm_l78, (void *)NULL, cx, NULL, t465, t466, &t468, t467);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t469 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                        if (t469) {
                        } else {
                            gm_t76123gfet73g t470 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l78) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t470);
                            goto gm_fdone;
                        }
                        uint64_t t471 = GM_LD(uint64_t, (((uint8_t *)&gm_l78) + 8));
                        gm_f_put_ptr_188p3mm(t457, t464, t471);
                    }
                    goto gm_lb1;
                }
gm_lb38:;
            }
            {
                {
                    uint8_t t472 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t473 = (uint8_t)(t472 == ((uint8_t)5ULL));
                    if (!t473) {
                        goto gm_lb52;
                    }
                    memmove(((uint8_t *)&gm_l51), (((uint8_t *)&F.l2) + 16), 24);
                    uint64_t t474 = GM_LD(uint64_t, ((((uint8_t *)&F.l2) + 16) + 24));
                    gm_l52 = t474;
                    {
                        {
                            void * t475 = gm_l0;
                            gm_t88rcucg1b7uk t476 = gm_l3;
                            gm_f_kind_htv6093((void *)&gm_l79, t475, GM_CONV(gm_csguankko4lmhd, t476));
                            uint8_t t477 = GM_LD(uint8_t, (((uint8_t *)&gm_l79) + 0));
                            uint8_t t478 = (uint8_t)(t477 == ((uint8_t)10ULL));
                            if (!t478) {
                                goto gm_lb54;
                            }
                            gm_t88rcucg1b7uk t479 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l79) + 8) + 4));
                            gm_l53 = t479;
                            void * t480 = gm_l0;
                            uint64_t t481 = GM_LD(uint64_t, (((uint8_t *)t480) + 176));
                            uint64_t t482 = gm_f_us_cuv01fp(t481);
                            gm_l54 = t482;
                            gm_l55 = ((uint64_t)0ULL);
                            uint64_t t483 = gm_l52;
                            uint8_t t484 = (t483 > ((uint64_t)0ULL));
                            if (t484) {
                                gm_l80 = ((uint8_t)1);
                            } else {
                                uint64_t t485 = GM_LD(uint64_t, ((((uint8_t *)&gm_l51) + 8) + 8));
                                uint8_t t486 = (t485 > ((uint64_t)0ULL));
                                gm_l80 = t486;
                            }
                            uint8_t t487 = gm_l80;
                            if (t487) {
                                {
                                    void * t488 = gm_l0;
                                    gm_tl874t2atnlh3 t489 = gm_l1;
                                    gm_t88rcucg1b7uk t490 = gm_l53;
                                    gm_tkaapcii86udk t491 = GM_LD(gm_tkaapcii86udk, ((uint8_t *)&gm_l51));
                                    gm_f_place_addr_m751en7((void *)&gm_l81, (void *)NULL, cx, NULL, t488, t489, &t491, t490);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t492 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
                                if (t492) {
                                } else {
                                    gm_t76123gfet73g t493 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l81) + 8));
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t493);
                                    goto gm_fdone;
                                }
                                uint64_t t494 = GM_LD(uint64_t, (((uint8_t *)&gm_l81) + 8));
                                gm_l55 = t494;
                            }
                            void * t495 = gm_l0;
                            void * t496 = GM_LD(void *, ((uint8_t *)&gm_l4));
                            uint64_t t497 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            uint64_t t498 = gm_l6;
                            uint64_t t499 = gm_l6;
                            uint64_t t500 = gm_l54;
                            uint64_t r501; bool o502 = __builtin_mul_overflow(((uint64_t)2ULL), t500, &r501);
                            if (o502) gm_trap("integer overflow in `*`", gm_src8sgunk80orlb, 126, 36);
                            uint64_t t503 = r501;
                            uint64_t r504; bool o505 = __builtin_add_overflow(t499, t503, &r504);
                            if (o505) gm_trap("integer overflow in `+`", gm_src8sgunk80orlb, 126, 32);
                            uint64_t t506 = r504;
                            uint64_t t507 = (uintptr_t)t498;
                            uint64_t t508 = (uintptr_t)t506;
                            if (t507 > t508 || t508 > (uintptr_t)t497) gm_trap_slice(t507, t508, t497, gm_src8sgunk80orlb, 126, 25);
                            uint64_t t509 = ((uintptr_t)t506) - (uintptr_t)t498;
                            gm_b16_8 t510;
                            GM_ST(void *, (uint8_t *)&t510, (void *)(((uint8_t *)t496 + (uintptr_t)t498 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t510 + 8, (uintptr_t)(t509));
                            uint64_t t511 = gm_l55;
                            uint64_t t512 = gm_l52;
                            gm_f_put_slice_ko14loi(t495, t510, t511, t512);
                            goto gm_lb53;
                        }
gm_lb54:;
                        {
                            void * t513 = gm_l0;
                            gm_tl874t2atnlh3 t514 = gm_l1;
                            gm_b16_8 t515;
                            GM_ST(void *, (uint8_t *)&t515, (void *)(gm_svlcdvcv2bp94));
                            GM_ST(uintptr_t, (uint8_t *)&t515 + 8, (uintptr_t)(23));
                            gm_t76123gfet73g t516;
                            gm_f_unsupported_70vvk1e((void *)&t516, (void *)NULL, cx, NULL, t513, t514, t515);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t516);
                            goto gm_fdone;
                        }
                    }
gm_lb53:;
                    goto gm_lb1;
                }
gm_lb52:;
            }
            {
                {
                    uint8_t t517 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                    uint8_t t518 = (uint8_t)(t517 == ((uint8_t)11ULL));
                    if (!t518) {
                        goto gm_lb55;
                    }
                    uint32_t t519 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 16));
                    gm_l56 = t519;
                    void * t520 = gm_l0;
                    void * t521 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t522 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t523 = gm_l6;
                    uint64_t t524 = (uintptr_t)t523;
                    uint64_t t525 = (uintptr_t)t522;
                    if (t524 > t525 || t525 > (uintptr_t)t522) gm_trap_slice(t524, t525, t522, gm_src8sgunk80orlb, 128, 29);
                    uint64_t t526 = ((uintptr_t)t522) - (uintptr_t)t523;
                    gm_b16_8 t527;
                    GM_ST(void *, (uint8_t *)&t527, (void *)(((uint8_t *)t521 + (uintptr_t)t523 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t527 + 8, (uintptr_t)(t526));
                    void * t528 = gm_l0;
                    uint32_t t529 = gm_l56;
                    uint64_t t530 = gm_f_fn_addr_odbbf6a(t528, t529);
                    gm_f_put_ptr_188p3mm(t520, t527, t530);
                    goto gm_lb1;
                }
gm_lb55:;
                {
                    {
                        {
                            uint8_t t531 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t532 = (uint8_t)(t531 == ((uint8_t)12ULL));
                            if (!t532) {
                                goto gm_lb58;
                            }
                            goto gm_lb57;
                        }
gm_lb58:;
                        {
                            uint8_t t533 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
                            uint8_t t534 = (uint8_t)(t533 == ((uint8_t)13ULL));
                            if (!t534) {
                                goto gm_lb59;
                            }
                            goto gm_lb57;
                        }
gm_lb59:;
                        goto gm_lb56;
                    }
gm_lb57:;
                    {
                        void * t535 = gm_l0;
                        gm_tl874t2atnlh3 t536 = gm_l1;
                        gm_b16_8 t537;
                        GM_ST(void *, (uint8_t *)&t537, (void *)(gm_sbbavs945cjhp));
                        GM_ST(uintptr_t, (uint8_t *)&t537 + 8, (uintptr_t)(31));
                        gm_t76123gfet73g t538;
                        gm_f_unsupported_70vvk1e((void *)&t538, (void *)NULL, cx, NULL, t535, t536, t537);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t538);
                        goto gm_fdone;
                    }
                }
gm_lb56:;
                gm_trap("no match arm matched the scrutinee", gm_src8sgunk80orlb, 61, 5);
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

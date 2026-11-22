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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; } gm_t2p74ki8sl4an;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t2p74ki8sl4an) == 24 && _Alignof(gm_t2p74ki8sl4an) == 4 && offsetof(gm_t2p74ki8sl4an, f0) == 0 && offsetof(gm_t2p74ki8sl4an, f1) == 12, "layout of `(Span, Span)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { gm_t08olq97rscn3 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; void * f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tc0sbs1tp6d7f;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tc0sbs1tp6d7f) == 80 && _Alignof(gm_tc0sbs1tp6d7f) == 8 && offsetof(gm_tc0sbs1tp6d7f, f0) == 0 && offsetof(gm_tc0sbs1tp6d7f, f1) == 32 && offsetof(gm_tc0sbs1tp6d7f, f2) == 40 && offsetof(gm_tc0sbs1tp6d7f, f3) == 56 && offsetof(gm_tc0sbs1tp6d7f, f4) == 64, "layout of `OpDecl`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_check_deprecated_j3dq26t {
    gm_frame h;
    gm_tkacitepg5fk0 l6; /* attr */
    gm_t4gut2c9fj1kt l10; /* w */
    gm_b0_1 ret;
};
struct gm_fr_check_freestanding_use_n95fsk4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_contains_a8d6g03 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_decl_visible_bd168ti {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_effect_with_op_ol1t6ca {
    gm_frame h;
    gm_tv2uf4leil5hq l8; /* b */
    gm_tc0sbs1tp6d7f l12; /* op */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s801pu6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_format_1o1mt78 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_1u4jnnu {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6kfrubb {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7her5m9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_88vph9n {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8hego7t {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9ihba55 {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_adbd97i {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ahgk48j {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c610h7d {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_k09npf9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lu3aj0k {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_m420rhg {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mqdhvcm {
    gm_frame h;
    gm_tud7oovfvnf43 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ooibcq2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p16u9uu {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_sbfc2dl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vdba58s {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_vlsb354 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_espvf96 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_get_rlnefnq {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_import_fix_7822666 {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* edits */
    gm_tv2uf4leil5hq l14; /* b */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_is_decl_used_oldf2ja {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_used_outside_tbk81ks {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_jekodor {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_lookup_module_name_cukfp7t {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_module_alias_b9tjeud {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_module_member_5fd2qm9 {
    gm_frame h;
    gm_t4gut2c9fj1kt l7; /* d */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_module_named_n1mcl3b {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_note_decl_use_ui57s3b {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_note_local_use_h4lf1jh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_note_use_from_2lmujsq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_op_effect_n2jr58f {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_op_hint_rfghuf5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_public_module_member_e2i0at7 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_public_op_effect_72d4rbe {
    gm_frame h;
    gm_tv2uf4leil5hq l7; /* b */
    gm_tc0sbs1tp6d7f l11; /* op */
    gm_th2lqbo77pvsc ret;
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
struct gm_fr_regrow_urfcki5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_same_package_vmps24r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_span_64igq5o {
    gm_frame h;
    gm_tl874t2atnlh3 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_std_home_dsdl68c {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_universe_hidden_gsk2b8r {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_universe_hint_ams3f10 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_unknown_name_dmegum5 {
    gm_frame h;
    gm_t4gut2c9fj1kt l5; /* d */
    gm_t4gut2c9fj1kt l6; /* d */
    gm_trqfvmjtt4sij l8; /* edits */
    gm_b0_1 ret;
};
struct gm_fr_visible_ll07dr9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_warning_q0m5jjs {
    gm_frame h;
    gm_t4gut2c9fj1kt l2; /* d */
    gm_t4gut2c9fj1kt ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_edits_em0b48a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deprecated_j3dq26t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_freestanding_use_n95fsk4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_a8d6g03(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_effect_with_op_ol1t6ca(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2, uint8_t a3);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_effect_with_op_ol1t6ca_c(void * a0, uint64_t a1, gm_b16_8 a2, uint8_t a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s801pu6(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_dg8ttg4(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_dlnutct(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ovslcai(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_format_1o1mt78(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_1u4jnnu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_65ee4ra(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_6kfrubb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_7her5m9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_88vph9n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_8hego7t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_9ihba55(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_adbd97i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ahgk48j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c610h7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_k09npf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_lu3aj0k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_m420rhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_mqdhvcm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ooibcq2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_p16u9uu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_sbfc2dl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_vdba58s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_vlsb354(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_rlnefnq(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) void gm_f_import_fix_7822666(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_decl_used_oldf2ja(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_used_outside_tbk81ks(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_list_jekodor(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_module_alias_b9tjeud(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_note_decl_use_ui57s3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_local_use_h4lf1jh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_note_use_from_2lmujsq(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_op_effect_n2jr58f(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_op_effect_n2jr58f_c(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_op_hint_rfghuf5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_public_module_member_e2i0at7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_public_op_effect_72d4rbe(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_public_op_effect_72d4rbe_c(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k96mobn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3e4k1js(void * a0, gm_cseahvg8v7p0vs a1, uint32_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_h767iu8(void * a0, const void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_urfcki5(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rfind_ai5s2js(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_same_package_vmps24r(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_span_64igq5o(uint32_t a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_std_home_dsdl68c(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_std_home_dsdl68c_c(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_universe_hidden_gsk2b8r(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_universe_hint_ams3f10(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unknown_name_dmegum5(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) void gm_f_warning_q0m5jjs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
extern uint8_t gm_c0a4m567j25[48] __attribute__((aligned(8)));
extern uint8_t gm_c55218gtcr0[448] __attribute__((aligned(8)));
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern uint8_t gm_cfiqcgtjhen[416] __attribute__((aligned(8)));
extern const uint8_t gm_s129bqtq598qj[54];
extern const uint8_t gm_s16bsnuu7i04s[3];
extern const uint8_t gm_s1j5pceke7iuk[5];
extern const uint8_t gm_s1lnh4indnr62[4];
extern const uint8_t gm_s1o778dp7d59d[43];
extern const uint8_t gm_s2470an4q44lj[25];
extern const uint8_t gm_s3m5ql5voef28[6];
extern const uint8_t gm_s3qo1c5fkeqfk[67];
extern const uint8_t gm_s48njnbffa02m[5];
extern const uint8_t gm_s4pmpk07ugvq6[5];
extern const uint8_t gm_s5le7rpnp1iq6[9];
extern const uint8_t gm_s79oh9p16aa8u[3];
extern const uint8_t gm_s7o7e6piqr3hn[14];
extern const uint8_t gm_s7tvcka4r1prv[10];
extern const uint8_t gm_s7vusn2lig3dp[65];
extern const uint8_t gm_s84ckbhm2d3ul[9];
extern const uint8_t gm_s88rffcs9cj2p[12];
extern const uint8_t gm_s8hpuukc6o9qu[9];
extern const uint8_t gm_sbicj451tu4qm[4];
extern const uint8_t gm_sbsmevjmneb3d[55];
extern const uint8_t gm_sd9r0l17bbuoe[5];
extern const uint8_t gm_sf50oc24o4dhk[11];
extern const uint8_t gm_sfc071avthns7[21];
extern const uint8_t gm_sg6dkeojalp6f[14];
extern const uint8_t gm_sgqcrkl2nh339[4];
extern const uint8_t gm_sgqs124m28gfg[7];
extern const uint8_t gm_shrc3j552p60m[9];
extern const uint8_t gm_si4j8sh2qika1[3];
extern const uint8_t gm_sictcqjuumcng[63];
extern const uint8_t gm_sj97id02v9fh3[6];
extern const uint8_t gm_sjsaqpo8dvqo2[82];
extern const uint8_t gm_sk04qg20jmac0[4];
extern const uint8_t gm_slmii6hjigrcr[7];
extern const uint8_t gm_sltqebpmj61tp[3];
extern const uint8_t gm_smnot3vdfhqsc[6];
extern const uint8_t gm_smo0niiqr9onn[11];
extern const uint8_t gm_smrla0cd2esrp[14];
extern const uint8_t gm_snil57vrv77lr[12];
extern const uint8_t gm_snivf25e5fme8[6];
extern const uint8_t gm_snu0769072h68[9];
extern const uint8_t gm_so97uarpe8rtg[9];
extern const uint8_t gm_sonl5k4jilrbc[13];
extern const uint8_t gm_soo4rtckloi4d[13];
extern const uint8_t gm_sp7619gun38fi[63];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr5nrafa4mlm6[19];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6lm8esmv16p1[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srugm21uf2sb0[9];
extern const uint8_t gm_ssqd2isjrp8nh[11];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_st3p5nb1e6cc8[5];
extern const uint8_t gm_stingg155o3k5[8];
extern const uint8_t gm_su6mi500md00b[7];
extern const uint8_t gm_su8mkvs9pi0v1[18];
extern const uint8_t gm_suhbskqupk9jm[15];
extern const uint8_t gm_suhmdi1gd6hta[7];
extern const uint8_t gm_sulfrs2bmdlu2[6];
extern const uint8_t gm_svjsvemd189fa[40];
extern const gm_tmpl gm_tpl2gebin91ghlu;
extern const gm_tmpl gm_tpl4esqdh06rpvt;
extern const gm_tmpl gm_tpl9jrfnomsrfk1;
extern const gm_tmpl gm_tpldj14c3jgvlnb;
extern const gm_tmpl gm_tple6e96jcr39fk;
extern const gm_tmpl gm_tple75bobdc6q0g;
extern const gm_tmpl gm_tplhkjcpttbsqt0;
extern const gm_tmpl gm_tpli2qk88n6tll4;
extern const gm_tmpl gm_tplj57b11lvidge;
extern const gm_tmpl gm_tplk6g6bjh5rkat;
extern const gm_tmpl gm_tplpa402pkgji4o;
extern const gm_tmpl gm_tplqjfk6t5rn06e;
extern const gm_tmpl gm_tplrpouqnnj8vo1;
extern const gm_tmpl gm_tplt7tm5142l0mh;
extern const gm_tmpl gm_tplthkeg7jngn6q;
extern const gm_tmpl gm_tplun02iesa1r5k;
extern const gm_tmpl gm_tplvf2bf6hftf4i;
extern const gm_tmpl gm_tplvpehcchgo971;
extern const gm_tmpl gm_tplvuahs93lrft8;
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_a8d6g03(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_a8d6g03 F;
    void * gm_l0; /* m */
    gm_t2p74ki8sl4an gm_l1; /* k */
    GM_STACK_CHECK(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    void * t5 = gm_l0;
    void * t6 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t7 = gm_f_find_dg8ttg4(t5, t6);
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
inline __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_decl_visible_bd168ti F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint64_t gm_l2; /* from */
    void * gm_l3; /* decl */
    GM_STACK_CHECK_SHALLOW(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 240));
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 240) + 8));
    uint64_t t6 = gm_l1;
    if ((uintptr_t)t6 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t6, t5, gm_src6lm8esmv16p1, 190, 14);
    void * t7 = (void *)(((uint8_t *)t4) + (uintptr_t)t6 * 216);
    gm_l3 = t7;
    void * t8 = gm_l0;
    void * t9 = gm_l3;
    gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t9) + 40) + 56));
    void * t11 = gm_l3;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 32));
    uint64_t t13 = gm_l2;
    uint8_t t14 = gm_f_visible_ll07dr9(t8, t10, t12, t13);
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_emit_dph5v1e F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
    gm_f_push_o4ckdr1(t4, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    F.ret = GM_ZERO(gm_b0_1);
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
inline __attribute__((aligned(4))) void gm_f_format_1o1mt78(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_1o1mt78 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svjsvemd189fa));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(39));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvf2bf6hftf4i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_1u4jnnu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_1u4jnnu F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sk04qg20jmac0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhkjcpttbsqt0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_6kfrubb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6kfrubb F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s1lnh4indnr62));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrpouqnnj8vo1), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7her5m9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_7her5m9 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_smrla0cd2esrp));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(13));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqjfk6t5rn06e), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_88vph9n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_88vph9n F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbsmevjmneb3d));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(54));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplthkeg7jngn6q), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_8hego7t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8hego7t F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbicj451tu4qm));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplun02iesa1r5k), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9ihba55(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_9ihba55 F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s129bqtq598qj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(53));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpldj14c3jgvlnb), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_adbd97i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_adbd97i F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sp7619gun38fi));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(62));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplj57b11lvidge), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ahgk48j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ahgk48j F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s3m5ql5voef28));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(5));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tple75bobdc6q0g), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c610h7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c610h7d F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjsaqpo8dvqo2));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(81));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4esqdh06rpvt), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_k09npf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_k09npf9 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sfc071avthns7));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(20));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tple6e96jcr39fk), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_lu3aj0k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lu3aj0k F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd9r0l17bbuoe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplvpehcchgo971), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_m420rhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_m420rhg F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sr5nrafa4mlm6));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(18));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2gebin91ghlu), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_mqdhvcm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mqdhvcm F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s7vusn2lig3dp));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(64));
    gm_tud7oovfvnf43 t3 = GM_LD(gm_tud7oovfvnf43, ((uint8_t *)a0));
    gm_f_format_65ee4ra((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplpa402pkgji4o), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_p16u9uu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_p16u9uu F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s3qo1c5fkeqfk));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(66));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplvuahs93lrft8), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_sbfc2dl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_sbfc2dl F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su8mkvs9pi0v1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpli2qk88n6tll4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_vdba58s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vdba58s F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sictcqjuumcng));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(62));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9jrfnomsrfk1), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_vlsb354(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_vlsb354 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s1o778dp7d59d));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(42));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplt7tm5142l0mh), t2);
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_espvf96 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
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
    gm_tf5srh4mp4b3i t4 = gm_f_find_ovslcai(t2, t3);
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_rlnefnq(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_rlnefnq F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_dlnutct(t2, t3);
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
    uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 20) + 16));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_decl_used_oldf2ja(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_decl_used_oldf2ja F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 78u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 2664) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 2664));
        uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t6) + 2664) + 8));
        uint64_t t9 = gm_l1;
        if ((uintptr_t)t9 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t9, t8, gm_src6lm8esmv16p1, 61, 91);
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t7) + (uintptr_t)t9 * 1));
        gm_l2 = t10;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_used_outside_tbk81ks(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_used_outside_tbk81ks F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 78u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t3) + 2704) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 2704));
        uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t6) + 2704) + 8));
        uint64_t t9 = gm_l1;
        if ((uintptr_t)t9 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t9, t8, gm_src6lm8esmv16p1, 64, 97);
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)t7) + (uintptr_t)t9 * 1));
        gm_l2 = t10;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lookup_59juqoo F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 200));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 200) + 8));
    uint64_t t5 = gm_l1;
    if ((uintptr_t)t5 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t5, t4, gm_srcc2qutubarto1, 547, 78);
    void * t6 = (void *)((((uint8_t *)t3) + (uintptr_t)t5 * 184) + 56);
    gm_b16_8 t7 = gm_l2;
    gm_tf5srh4mp4b3i t8 = gm_f_get_espvf96(t6, t7);
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_note_decl_use_ui57s3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_note_decl_use_ui57s3b F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK(&F, 94u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    {
        for (;;) {
            void * t5 = gm_l0;
            uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t5) + 2664) + 8));
            uint64_t t7 = gm_l1;
            uint8_t t8 = (t6 <= t7);
            if (!t8) {
                goto gm_lb1;
            }
            {
                void * t9 = gm_l0;
                void * t10 = (void *)(((uint8_t *)t9) + 2664);
                gm_tftb1odc739t5 t11 = gm_f_push_vd1290p(t10, ((uint8_t)0));
                gm_l2 = t11;
                uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
                if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_src6lm8esmv16p1, 41, 40);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, (((uint8_t *)t13) + 2664));
    uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 2664) + 8));
    uint64_t t16 = gm_l1;
    if ((uintptr_t)t16 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t16, t15, gm_src6lm8esmv16p1, 42, 5);
    GM_ST(uint8_t, (((uint8_t *)t14) + (uintptr_t)t16 * 1), ((uint8_t)1));
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
inline __attribute__((aligned(4))) uint8_t gm_f_universe_hidden_gsk2b8r(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_universe_hidden_gsk2b8r F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 240));
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t3) + 240) + 8));
    uint64_t t6 = gm_l1;
    if ((uintptr_t)t6 >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)t6, t5, gm_src6lm8esmv16p1, 67, 64);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t4) + (uintptr_t)t6 * 216));
    void * t8 = (void *)((uint8_t *)gm_cfiqcgtjhen);
    uint64_t t9 = ((uintptr_t)((uint64_t)26ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9));
    uint8_t t11 = 0;
    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t10) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t10))) + i * 16, ((uint8_t *)&t7))) { t11 = 1; break; }
    uint8_t t12 = (uint8_t)(t11);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_deprecated_j3dq26t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_check_deprecated_j3dq26t F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* decl */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l7; /* msg */
    gm_b16_8 gm_l8; /* args */
    gm_tibupt4dmcqai gm_l9; /* lit */
    GM_STACK_CHECK(&F, 832u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l11;
    gm_tabb710d9mq2e gm_l12;
    void * t11 = gm_l0;
    uint64_t t12 = gm_l1;
    gm_tl874t2atnlh3 t13 = gm_l2;
    gm_b0_1 t14 = gm_f_check_freestanding_use_n95fsk4((void *)NULL, cx, NULL, t11, t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    void * t17 = GM_LD(void *, (((uint8_t *)t16) + 240));
    uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t16) + 240) + 8));
    uint64_t t19 = gm_l1;
    if ((uintptr_t)t19 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t19, t18, gm_src6lm8esmv16p1, 292, 14);
    void * t20 = (void *)(((uint8_t *)t17) + (uintptr_t)t19 * 216);
    gm_l3 = t20;
    void * t21 = gm_l3;
    gm_b16_8 t22 = GM_LD(gm_b16_8, ((((uint8_t *)t21) + 40) + 40));
    gm_l4 = t22;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t23 = gm_l5;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t25 = (t23 < t24);
            if (!t25) {
                goto gm_lb1;
            }
            {
                void * t26 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t28 = gm_l5;
                {
                    {
                        {
                            memmove(((uint8_t *)&F.l6), (((uint8_t *)t26) + (uintptr_t)t28 * 72), 72);
                        }
                        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                        gm_b16_8 t30;
                        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_smo0niiqr9onn));
                        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(10));
                        uint8_t t31 = (!gm_eq0iqiubfm7e(((uint8_t *)&t29), ((uint8_t *)&t30)));
                        if (t31) {
                            goto gm_lb3;
                        }
                        gm_b16_8 t32;
                        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(0));
                        gm_l7 = t32;
                        {
                            {
                                uint8_t t33 = (uint8_t)(((((uint8_t *)&F.l6) + 32))[16] != 0);
                                if (!t33) {
                                    goto gm_lb7;
                                }
                                gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 32));
                                gm_l8 = t34;
                                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                uint8_t t36 = ((t35 == ((uint64_t)1ULL)));
                                if (t36) {
                                    {
                                        {
                                            {
                                                void * t37 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                                uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t38, gm_src6lm8esmv16p1, 298, 33);
                                                uint8_t t39 = GM_LD(uint8_t, (((((uint8_t *)t37) + (uintptr_t)((uint64_t)0ULL) * 88) + 8) + 0));
                                                uint8_t t40 = (uint8_t)(t39 == ((uint8_t)2ULL));
                                                if (!t40) {
                                                    goto gm_lb9;
                                                }
                                                memmove(((uint8_t *)&gm_l9), (((((uint8_t *)t37) + (uintptr_t)((uint64_t)0ULL) * 88) + 8) + 8), 24);
                                            }
                                            {
                                                gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                                                gm_tnpifv20irnph t42;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
                                                gm_f_format_lu3aj0k((void *)&gm_l11, (void *)NULL, cx, NULL, t42);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                            if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_src6lm8esmv16p1, 298, 54);
                                            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                                            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                                            gm_l7 = t45;
                                            goto gm_lb8;
                                        }
gm_lb9:;
                                    }
gm_lb8:;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        {
                            gm_tl874t2atnlh3 t46 = gm_l2;
                            void * t47 = gm_l3;
                            gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)t47));
                            gm_b16_8 t49 = gm_l7;
                            gm_t6j6smj7qoctu t50;
                            GM_ST(gm_b16_8, ((uint8_t *)&t50 + 0), t48);
                            GM_ST(gm_b16_8, ((uint8_t *)&t50 + 16), t49);
                            gm_f_format_k09npf9((void *)&gm_l12, (void *)NULL, cx, NULL, &t50);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                            if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src6lm8esmv16p1, 301, 33);
                            gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                            gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                            gm_t4gut2c9fj1kt t54;
                            gm_f_warning_q0m5jjs((void *)&t54, (void *)NULL, cx, NULL, t46, t53);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l10 = t54;
                        }
                        void * t55 = (void *)((uint8_t *)&F.l10);
                        void * t56 = gm_l3;
                        gm_tl874t2atnlh3 t57 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t56) + 16));
                        gm_b16_8 t58;
                        GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sg6dkeojalp6f));
                        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(13));
                        gm_f_add_label_50fj7vv(t55, GM_CONV(gm_cseahvg8v7p0vs, t57), t58);
                        {
                            void * t59 = gm_l0;
                            gm_b0_1 t60 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t59, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l10)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        F.ret = GM_ZERO(gm_b0_1);
                        goto gm_fret;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 293, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t61 = gm_l5;
            uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
            uint64_t t64 = r62;
            gm_l5 = t64;
        }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_freestanding_use_n95fsk4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_check_freestanding_use_n95fsk4 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* decl */
    gm_b16_8 gm_l4; /* name */
    gm_b16_8 gm_l5; /* path */
    GM_STACK_CHECK(&F, 678u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    gm_tabb710d9mq2e gm_l10;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + 88) + 24));
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_soo4rtckloi4d));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(12));
    uint8_t t6 = (!gm_eq0iqiubfm7e(((uint8_t *)&t4), ((uint8_t *)&t5)));
    if (t6) {
        gm_l6 = ((uint8_t)1);
    } else {
        gm_tl874t2atnlh3 t7 = gm_l2;
        gm_tl874t2atnlh3 t8 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)gm_cejn1dj0054));
        uint8_t t9 = (gm_eqff85ac10o3(((uint8_t *)&t7), ((uint8_t *)&t8)));
        gm_l6 = t9;
    }
    uint8_t t10 = gm_l6;
    if (t10) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    void * t13 = GM_LD(void *, (((uint8_t *)t12) + 240));
    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 240) + 8));
    uint64_t t15 = gm_l1;
    if ((uintptr_t)t15 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t15, t14, gm_src6lm8esmv16p1, 312, 14);
    void * t16 = (void *)(((uint8_t *)t13) + (uintptr_t)t15 * 216);
    gm_l3 = t16;
    void * t17 = gm_l3;
    gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)t17));
    gm_l4 = t18;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    void * t21 = GM_LD(void *, (((uint8_t *)t20) + 200));
    uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)t20) + 200) + 8));
    void * t23 = gm_l3;
    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 32));
    if ((uintptr_t)t24 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t24, t22, gm_src6lm8esmv16p1, 314, 12);
    uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t21) + (uintptr_t)t24 * 184) + 112));
    uint8_t t26 = (uint8_t)!t25;
    if (t26) {
        gm_l7 = ((uint8_t)1);
    } else {
        gm_b16_8 t27 = gm_l4;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_suhbskqupk9jm));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(14));
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s7tvcka4r1prv));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(9));
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_s7o7e6piqr3hn));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(13));
        gm_tfeab279c16rr t31;
        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t28);
        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 16), t29);
        GM_ST(gm_b16_8, ((uint8_t *)&t31 + 32), t30);
        uint8_t t32 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t31) + 0, ((uint8_t *)&t27))) t32 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t31) + 16, ((uint8_t *)&t27))) t32 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t31) + 32, ((uint8_t *)&t27))) t32 = 1;
        uint8_t t33 = (uint8_t)(!t32);
        gm_l7 = t33;
    }
    uint8_t t34 = gm_l7;
    if (t34) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t35 = gm_l0;
    void * t36 = GM_LD(void *, ((uint8_t *)t35));
    void * t37 = GM_LD(void *, ((uint8_t *)t36));
    uint32_t t38 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
    void * t39 = gm_f_file_o15fqku(t37, t38);
    gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)t39));
    gm_l5 = t40;
    gm_b16_8 t41 = gm_l5;
    gm_b16_8 t42;
    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_slmii6hjigrcr));
    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(6));
    uint8_t t43 = gm_f_starts_with_6vvt01e(t41, t42);
    if (t43) {
        gm_l9 = ((uint8_t)1);
    } else {
        gm_b16_8 t44 = gm_l5;
        void * t45 = gm_l0;
        void * t46 = GM_LD(void *, ((uint8_t *)t45));
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t46) + 152));
        uint8_t t48 = gm_f_starts_with_6vvt01e(t44, t47);
        gm_l9 = t48;
    }
    uint8_t t49 = gm_l9;
    if (t49) {
        gm_l8 = ((uint8_t)1);
    } else {
        gm_b16_8 t50 = gm_l5;
        void * t51 = gm_l0;
        void * t52 = GM_LD(void *, ((uint8_t *)t51));
        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)t52) + 168));
        uint8_t t54 = gm_f_starts_with_6vvt01e(t50, t53);
        gm_l8 = t54;
    }
    uint8_t t55 = gm_l8;
    if (t55) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t56 = gm_l0;
    gm_tl874t2atnlh3 t57 = gm_l2;
    gm_b16_8 t58 = gm_l4;
    gm_tnpifv20irnph t59;
    GM_ST(gm_b16_8, ((uint8_t *)&t59 + 0), t58);
    gm_f_format_88vph9n((void *)&gm_l10, (void *)NULL, cx, NULL, t59);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src6lm8esmv16p1, 317, 19);
    gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
    gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
    gm_b0_1 t63 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t56, t57, t62);
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
extern __attribute__((aligned(4))) uint8_t gm_f_decl_visible_bd168ti(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_effect_with_op_ol1t6ca(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_with_op_ol1t6ca F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l2; /* name */
    uint8_t gm_l3; /* public_only */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t3877pk7lv748 gm_l6; /* n */
    void * gm_l7; /* d */
    void * gm_l9; /* lit */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 460u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t18 = gm_l0;
    void * t19 = GM_LD(void *, ((uint8_t *)t18));
    void * t20 = GM_LD(void *, (((uint8_t *)t19) + 200));
    uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t19) + 200) + 8));
    uint64_t t22 = gm_l1;
    if ((uintptr_t)t22 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t22, t21, gm_src6lm8esmv16p1, 245, 14);
    gm_b16_8 t23 = GM_LD(gm_b16_8, ((((uint8_t *)t20) + (uintptr_t)t22 * 184) + 16));
    gm_l4 = t23;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t24 = gm_l5;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t26 = (t24 < t25);
            if (!t26) {
                goto gm_lb1;
            }
            {
                void * t27 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t29 = gm_l5;
                {
                    {
                        memmove(((uint8_t *)&gm_l6), (((uint8_t *)t27) + (uintptr_t)t29 * 24), 24);
                        void * t30 = gm_l0;
                        void * t31 = GM_LD(void *, ((uint8_t *)t30));
                        void * t32 = GM_LD(void *, (((uint8_t *)t31) + 240));
                        uint64_t t33 = GM_LD(uint64_t, ((((uint8_t *)t31) + 240) + 8));
                        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                        if ((uintptr_t)t34 >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)t34, t33, gm_src6lm8esmv16p1, 246, 15);
                        void * t35 = (void *)(((uint8_t *)t32) + (uintptr_t)t34 * 216);
                        gm_l7 = t35;
                        {
                            {
                                uint8_t t36 = gm_l3;
                                if (!t36) {
                                    goto gm_lb7;
                                }
                                void * t37 = gm_l7;
                                gm_t9qkh0rsooc8d t38 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t37) + 40) + 56));
                                uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t38) + 0));
                                uint8_t t40 = (uint8_t)(!(t39 == ((uint8_t)2ULL)));
                                if (!t40) {
                                    goto gm_lb7;
                                }
                                goto gm_lb3;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        {
                            {
                                void * t41 = gm_l7;
                                uint8_t t42 = GM_LD(uint8_t, (((((uint8_t *)t41) + 40) + 64) + 0));
                                uint8_t t43 = (uint8_t)(t42 == ((uint8_t)0ULL));
                                if (!t43) {
                                    goto gm_lb9;
                                }
                                memmove(((uint8_t *)&F.l8), (((((uint8_t *)t41) + 40) + 64) + 8), 64);
                                {
                                    {
                                        void * t44 = GM_LD(void *, (((uint8_t *)&F.l8) + 40));
                                        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t44) + 8) + 0));
                                        uint8_t t46 = (uint8_t)(t45 == ((uint8_t)34ULL));
                                        if (!t46) {
                                            goto gm_lb11;
                                        }
                                        void * t47 = GM_LD(void *, ((((uint8_t *)t44) + 8) + 8));
                                        gm_l9 = t47;
                                        void * t48 = gm_l9;
                                        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t48) + 24));
                                        gm_l10 = t49;
                                        gm_l11 = ((uint64_t)0ULL);
                                        {
                                            for (;;) {
                                                uint64_t t50 = gm_l11;
                                                uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                uint8_t t52 = (t50 < t51);
                                                if (!t52) {
                                                    goto gm_lb12;
                                                }
                                                {
                                                    void * t53 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                                    uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                    uint64_t t55 = gm_l11;
                                                    {
                                                        {
                                                            memmove(((uint8_t *)&F.l12), (((uint8_t *)t53) + (uintptr_t)t55 * 80), 80);
                                                            gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                                            gm_b16_8 t57 = gm_l2;
                                                            uint8_t t58 = (gm_eq0iqiubfm7e(((uint8_t *)&t56), ((uint8_t *)&t57)));
                                                            if (t58) {
                                                                void * t59 = gm_l7;
                                                                gm_b16_8 t60 = GM_LD(gm_b16_8, ((uint8_t *)t59));
                                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t60);
                                                                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                                                                goto gm_fdone;
                                                            }
                                                            goto gm_lb15;
                                                        }
gm_lb16:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 250, 9);
                                                    }
gm_lb15:;
                                                }
gm_lb14:;
                                                uint64_t t61 = gm_l11;
                                                uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                                                uint64_t t64 = r62;
                                                gm_l11 = t64;
                                            }
                                        }
gm_lb12:;
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                    goto gm_lb3;
                                }
gm_lb10:;
                                goto gm_lb8;
                            }
gm_lb9:;
                            goto gm_lb3;
                        }
gm_lb8:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 245, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t65 = gm_l5;
            uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
            uint64_t t68 = r66;
            gm_l5 = t68;
        }
    }
gm_lb1:;
    memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_effect_with_op_ol1t6ca_c(void * a0, uint64_t a1, gm_b16_8 a2, uint8_t a3) { gm_th2lqbo77pvsc gm_r; gm_f_effect_with_op_ol1t6ca(&gm_r, a0, a1, a2, a3); return gm_r; }
__attribute__((aligned(4))) void gm_f_import_fix_7822666(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_import_fix_7822666 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l4; /* path */
    void * gm_l5; /* m */
    gm_b16_8 gm_l6; /* alias */
    gm_th2lqbo77pvsc gm_l8; /* existing */
    gm_t9805qtonbsag gm_l9; /* last_import_end */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t3877pk7lv748 gm_l12; /* n */
    void * gm_l13; /* decl */
    gm_tibupt4dmcqai gm_l15; /* p */
    uint32_t gm_l16; /* hi */
    uint32_t gm_l17; /* e */
    gm_b16_8 gm_l18; /* text */
    gm_b16_8 gm_l19; /* name_text */
    gm_b16_8 gm_l20; /* a */
    gm_b16_8 gm_l21; /* line */
    uint64_t gm_l22; /* at */
    uint32_t gm_l23; /* hi */
    uint64_t gm_l24; /* nl */
    uint64_t gm_l25; /* nl */
    GM_STACK_CHECK(&F, 1991u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_th2lqbo77pvsc gm_l26;
    uint8_t gm_l27;
    uint8_t gm_l28;
    uint8_t gm_l29;
    uint8_t gm_l30;
    gm_tf5srh4mp4b3i gm_l31;
    gm_tabb710d9mq2e gm_l32;
    gm_tf5srh4mp4b3i gm_l33;
    gm_tf5srh4mp4b3i gm_l34;
    gm_b16_8 gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_tabb710d9mq2e gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tabb710d9mq2e gm_l39;
    gm_tftb1odc739t5 gm_l40;
    {
        gm_b16_8 t28 = gm_l2;
        gm_f_std_home_dsdl68c((void *)&gm_l26, t28);
        uint8_t t29 = (uint8_t)((((uint8_t *)&gm_l26))[16] != 0);
        if (t29) {
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l26));
            gm_l4 = t30;
        } else {
            memset(&(*(gm_tqetrgo0q24b5 *)gm_out), 0, sizeof (*(gm_tqetrgo0q24b5 *)gm_out));
            goto gm_fdone;
        }
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, ((uint8_t *)t31));
        void * t33 = GM_LD(void *, (((uint8_t *)t32) + 200));
        uint64_t t34 = GM_LD(uint64_t, ((((uint8_t *)t32) + 200) + 8));
        uint64_t t35 = gm_l1;
        if ((uintptr_t)t35 >= (uintptr_t)(t34)) gm_trap_index((uintptr_t)t35, t34, gm_src6lm8esmv16p1, 114, 11);
        void * t36 = (void *)(((uint8_t *)t33) + (uintptr_t)t35 * 184);
        gm_l5 = t36;
        void * t37 = gm_l5;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)t37) + 112));
        if (t38) {
            gm_l27 = ((uint8_t)1);
        } else {
            void * t39 = gm_l5;
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)t39) + 113));
            gm_l27 = t40;
        }
        uint8_t t41 = gm_l27;
        if (t41) {
            memset(&(*(gm_tqetrgo0q24b5 *)gm_out), 0, sizeof (*(gm_tqetrgo0q24b5 *)gm_out));
            goto gm_fdone;
        }
        gm_b16_8 t42 = gm_l4;
        gm_b16_8 t43 = gm_f_module_alias_b9tjeud(t42);
        gm_l6 = t43;
        gm_trqfvmjtt4sij t44;
        gm_f_list_jekodor((void *)&t44, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t44;
        memset(&gm_l8, 0, sizeof gm_l8);
        gm_t9805qtonbsag t45;
        memset(&t45, 0, sizeof t45);
        gm_l9 = t45;
        void * t46 = gm_l5;
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t46) + 16));
        gm_l10 = t47;
        gm_l11 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t48 = gm_l11;
                uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint8_t t50 = (t48 < t49);
                if (!t50) {
                    goto gm_lb1;
                }
                {
                    void * t51 = GM_LD(void *, ((uint8_t *)&gm_l10));
                    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint64_t t53 = gm_l11;
                    {
                        {
                            memmove(((uint8_t *)&gm_l12), (((uint8_t *)t51) + (uintptr_t)t53 * 24), 24);
                            void * t54 = gm_l0;
                            void * t55 = GM_LD(void *, ((uint8_t *)t54));
                            void * t56 = GM_LD(void *, (((uint8_t *)t55) + 240));
                            uint64_t t57 = GM_LD(uint64_t, ((((uint8_t *)t55) + 240) + 8));
                            uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 16));
                            if ((uintptr_t)t58 >= (uintptr_t)(t57)) gm_trap_index((uintptr_t)t58, t57, gm_src6lm8esmv16p1, 121, 18);
                            void * t59 = (void *)(((uint8_t *)t56) + (uintptr_t)t58 * 216);
                            gm_l13 = t59;
                            void * t60 = gm_l13;
                            uint32_t t61 = GM_LD(uint32_t, ((((uint8_t *)t60) + 40) + 4));
                            uint32_t t62 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                            uint8_t t63 = (!(t61 == t62));
                            if (t63) {
                                goto gm_lb3;
                            }
                            {
                                {
                                    void * t64 = gm_l13;
                                    uint8_t t65 = GM_LD(uint8_t, (((((uint8_t *)t64) + 40) + 64) + 0));
                                    uint8_t t66 = (uint8_t)(t65 == ((uint8_t)0ULL));
                                    if (!t66) {
                                        goto gm_lb7;
                                    }
                                    memmove(((uint8_t *)&F.l14), (((((uint8_t *)t64) + 40) + 64) + 8), 64);
                                    {
                                        {
                                            uint8_t t67 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l14) + 24)) != 0);
                                            if (t67) {
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
                                        gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 124, 12);
                                    }
gm_lb8:;
                                    uint8_t t68 = gm_l29;
                                    if (t68) {
                                        gm_l28 = ((uint8_t)1);
                                    } else {
                                        uint64_t t69 = GM_LD(uint64_t, ((((uint8_t *)&F.l14) + 8) + 8));
                                        uint8_t t70 = (!(t69 == ((uint64_t)1ULL)));
                                        gm_l28 = t70;
                                    }
                                    uint8_t t71 = gm_l28;
                                    if (t71) {
                                        goto gm_lb3;
                                    }
                                    {
                                        {
                                            void * t72 = GM_LD(void *, (((uint8_t *)&F.l14) + 40));
                                            uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t72) + 8) + 0));
                                            uint8_t t74 = (uint8_t)(t73 == ((uint8_t)38ULL));
                                            if (!t74) {
                                                goto gm_lb12;
                                            }
                                            memmove(((uint8_t *)&gm_l15), ((((uint8_t *)t72) + 8) + 8), 24);
                                            gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                                            gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                                            gm_b16_8 t77 = gm_l4;
                                            uint8_t t78 = (gm_eq0iqiubfm7e(((uint8_t *)&t76), ((uint8_t *)&t77)));
                                            if (t78) {
                                                void * t79 = gm_l13;
                                                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)t79));
                                                GM_ST(uint8_t, (uint8_t *)&gm_l8 + 17, 0);
                                                GM_ST(uint16_t, (uint8_t *)&gm_l8 + 18, 0);
                                                GM_ST(uint32_t, (uint8_t *)&gm_l8 + 20, 0);
                                                GM_ST(gm_b16_8, ((uint8_t *)&gm_l8), t80);
                                                ((uint8_t *)&gm_l8)[16] = 1;
                                            }
                                            void * t81 = GM_LD(void *, (((uint8_t *)&F.l14) + 40));
                                            uint32_t t82 = GM_LD(uint32_t, ((((uint8_t *)t81) + 72) + 8));
                                            gm_l16 = t82;
                                            {
                                                {
                                                    uint8_t t83 = (uint8_t)((((uint8_t *)&gm_l9))[4] != 0);
                                                    if (!t83) {
                                                        goto gm_lb14;
                                                    }
                                                    uint32_t t84 = GM_LD(uint32_t, ((uint8_t *)&gm_l9));
                                                    gm_l17 = t84;
                                                    uint32_t t85 = gm_l16;
                                                    uint32_t t86 = gm_l17;
                                                    uint8_t t87 = (t85 > t86);
                                                    if (t87) {
                                                        uint32_t t88 = gm_l16;
                                                        gm_t9805qtonbsag t89;
                                                        GM_ST(uint8_t, (uint8_t *)&t89 + 5, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t89 + 6, 0);
                                                        GM_ST(uint32_t, ((uint8_t *)&t89), t88);
                                                        ((uint8_t *)&t89)[4] = 1;
                                                        gm_l9 = t89;
                                                    }
                                                    goto gm_lb13;
                                                }
gm_lb14:;
                                                uint32_t t90 = gm_l16;
                                                gm_t9805qtonbsag t91;
                                                GM_ST(uint8_t, (uint8_t *)&t91 + 5, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t91 + 6, 0);
                                                GM_ST(uint32_t, ((uint8_t *)&t91), t90);
                                                ((uint8_t *)&t91)[4] = 1;
                                                gm_l9 = t91;
                                            }
gm_lb13:;
                                            goto gm_lb11;
                                        }
gm_lb12:;
                                        goto gm_lb3;
                                    }
gm_lb11:;
                                    goto gm_lb6;
                                }
gm_lb7:;
                                goto gm_lb3;
                            }
gm_lb6:;
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 120, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t92 = gm_l11;
                uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
                uint64_t t95 = r93;
                gm_l11 = t95;
            }
        }
gm_lb1:;
    }
    {
        void * t96 = gm_l0;
        void * t97 = GM_LD(void *, ((uint8_t *)t96));
        void * t98 = GM_LD(void *, ((uint8_t *)t97));
        uint32_t t99 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
        void * t100 = gm_f_file_o15fqku(t98, t99);
        gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)t100) + 16));
        gm_l18 = t101;
        gm_b16_8 t102;
        GM_ST(void *, (uint8_t *)&t102, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(0));
        gm_l19 = t102;
        {
            {
                uint8_t t103 = (uint8_t)((((uint8_t *)&gm_l8))[16] != 0);
                if (!t103) {
                    goto gm_lb16;
                }
                gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
                gm_l20 = t104;
                gm_b16_8 t105 = gm_l20;
                gm_l19 = t105;
                goto gm_lb15;
            }
gm_lb16:;
            void * t106 = gm_l0;
            void * t107 = GM_LD(void *, ((uint8_t *)t106));
            uint64_t t108 = gm_l1;
            gm_b16_8 t109 = gm_l6;
            gm_tf5srh4mp4b3i t110 = gm_f_lookup_59juqoo(t107, t108, t109);
            gm_l31 = t110;
            {
                {
                    uint8_t t111 = (uint8_t)((((uint8_t *)&gm_l31))[8] != 0);
                    if (t111) {
                        goto gm_lb18;
                    }
                    gm_l30 = ((uint8_t)0);
                    goto gm_lb17;
                }
gm_lb18:;
                {
                    gm_l30 = ((uint8_t)1);
                    goto gm_lb17;
                }
gm_lb19:;
                gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 139, 12);
            }
gm_lb17:;
            uint8_t t112 = gm_l30;
            if (t112) {
                memset(&(*(gm_tqetrgo0q24b5 *)gm_out), 0, sizeof (*(gm_tqetrgo0q24b5 *)gm_out));
                goto gm_fdone;
            }
            {
                gm_b16_8 t113 = gm_l6;
                gm_b16_8 t114 = gm_l4;
                gm_t6j6smj7qoctu t115;
                GM_ST(gm_b16_8, ((uint8_t *)&t115 + 0), t113);
                GM_ST(gm_b16_8, ((uint8_t *)&t115 + 16), t114);
                gm_f_format_m420rhg((void *)&gm_l32, (void *)NULL, cx, NULL, &t115);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t116 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
            if (!t116) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_src6lm8esmv16p1, 140, 17);
            gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l32) + 8));
            gm_l21 = t117;
            gm_l22 = ((uint64_t)0ULL);
            {
                {
                    uint8_t t118 = (uint8_t)((((uint8_t *)&gm_l9))[4] != 0);
                    if (!t118) {
                        goto gm_lb21;
                    }
                    uint32_t t119 = GM_LD(uint32_t, ((uint8_t *)&gm_l9));
                    gm_l23 = t119;
                    void * t120 = GM_LD(void *, ((uint8_t *)&gm_l18));
                    uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint32_t t122 = gm_l23;
                    uint64_t t123 = (uint64_t)t122;
                    uint64_t t124 = (uintptr_t)t123;
                    uint64_t t125 = (uintptr_t)t121;
                    if (t124 > t125 || t125 > (uintptr_t)t121) gm_trap_slice(t124, t125, t121, gm_src6lm8esmv16p1, 144, 19);
                    uint64_t t126 = ((uintptr_t)t121) - (uintptr_t)t123;
                    gm_b16_8 t127;
                    GM_ST(void *, (uint8_t *)&t127, (void *)(((uint8_t *)t120 + (uintptr_t)t123 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(t126));
                    gm_tf5srh4mp4b3i t128 = gm_f_index_of_hdeg351(t127, ((uint8_t)10ULL));
                    gm_l33 = t128;
                    uint8_t t129 = (uint8_t)((((uint8_t *)&gm_l33))[8] != 0);
                    if (t129) {
                        uint64_t t130 = GM_LD(uint64_t, ((uint8_t *)&gm_l33));
                        gm_l24 = t130;
                    } else {
                        memset(&(*(gm_tqetrgo0q24b5 *)gm_out), 0, sizeof (*(gm_tqetrgo0q24b5 *)gm_out));
                        goto gm_fdone;
                    }
                    uint32_t t131 = gm_l23;
                    uint64_t t132 = (uint64_t)t131;
                    uint64_t t133 = gm_l24;
                    uint64_t r134; bool o135 = __builtin_add_overflow(t132, t133, &r134);
                    if (o135) gm_trap("integer overflow in `+`", gm_src6lm8esmv16p1, 145, 18);
                    uint64_t t136 = r134;
                    uint64_t r137; bool o138 = __builtin_add_overflow(t136, ((uint64_t)1ULL), &r137);
                    if (o138) gm_trap("integer overflow in `+`", gm_src6lm8esmv16p1, 145, 18);
                    uint64_t t139 = r137;
                    gm_l22 = t139;
                    goto gm_lb20;
                }
gm_lb21:;
                {
                    for (;;) {
                        void * t140 = GM_LD(void *, ((uint8_t *)&gm_l18));
                        uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                        uint64_t t142 = gm_l22;
                        uint64_t t143 = (uintptr_t)t142;
                        uint64_t t144 = (uintptr_t)t141;
                        if (t143 > t144 || t144 > (uintptr_t)t141) gm_trap_slice(t143, t144, t141, gm_src6lm8esmv16p1, 148, 19);
                        uint64_t t145 = ((uintptr_t)t141) - (uintptr_t)t142;
                        gm_b16_8 t146;
                        GM_ST(void *, (uint8_t *)&t146, (void *)(((uint8_t *)t140 + (uintptr_t)t142 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(t145));
                        gm_b16_8 t147;
                        GM_ST(void *, (uint8_t *)&t147, (void *)(gm_si4j8sh2qika1));
                        GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(2));
                        uint8_t t148 = gm_f_starts_with_6vvt01e(t146, t147);
                        if (!t148) {
                            goto gm_lb22;
                        }
                        {
                            {
                                {
                                    void * t149 = GM_LD(void *, ((uint8_t *)&gm_l18));
                                    uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                    uint64_t t151 = gm_l22;
                                    uint64_t t152 = (uintptr_t)t151;
                                    uint64_t t153 = (uintptr_t)t150;
                                    if (t152 > t153 || t153 > (uintptr_t)t150) gm_trap_slice(t152, t153, t150, gm_src6lm8esmv16p1, 149, 26);
                                    uint64_t t154 = ((uintptr_t)t150) - (uintptr_t)t151;
                                    gm_b16_8 t155;
                                    GM_ST(void *, (uint8_t *)&t155, (void *)(((uint8_t *)t149 + (uintptr_t)t151 * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(t154));
                                    gm_tf5srh4mp4b3i t156 = gm_f_index_of_hdeg351(t155, ((uint8_t)10ULL));
                                    gm_l34 = t156;
                                    uint8_t t157 = (uint8_t)((((uint8_t *)&gm_l34))[8] != 0);
                                    if (!t157) {
                                        goto gm_lb26;
                                    }
                                    uint64_t t158 = GM_LD(uint64_t, ((uint8_t *)&gm_l34));
                                    gm_l25 = t158;
                                    uint64_t t159 = gm_l22;
                                    uint64_t t160 = gm_l25;
                                    uint64_t r161; bool o162 = __builtin_add_overflow(t160, ((uint64_t)1ULL), &r161);
                                    if (o162) gm_trap("integer overflow in `+`", gm_src6lm8esmv16p1, 149, 60);
                                    uint64_t t163 = r161;
                                    uint64_t r164; bool o165 = __builtin_add_overflow(t159, t163, &r164);
                                    if (o165) gm_trap("integer overflow in `+`", gm_src6lm8esmv16p1, 149, 54);
                                    uint64_t t166 = r164;
                                    gm_l22 = t166;
                                    goto gm_lb25;
                                }
gm_lb26:;
                                uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                                gm_l22 = t167;
                            }
gm_lb25:;
                        }
gm_lb24:;
                    }
                }
gm_lb22:;
                uint64_t t168 = gm_l22;
                uint8_t t169 = ((t168 == ((uint64_t)0ULL)));
                if (t169) {
                    gm_b16_8 t170 = gm_l21;
                    gm_tnpifv20irnph t171;
                    GM_ST(gm_b16_8, ((uint8_t *)&t171 + 0), t170);
                    gm_f_format_1u4jnnu((void *)&gm_l36, (void *)NULL, cx, NULL, t171);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t172 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                    if (!t172) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_src6lm8esmv16p1, 151, 33);
                    gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                    gm_l35 = t173;
                } else {
                    gm_b16_8 t174 = gm_l21;
                    gm_tnpifv20irnph t175;
                    GM_ST(gm_b16_8, ((uint8_t *)&t175 + 0), t174);
                    gm_f_format_8hego7t((void *)&gm_l37, (void *)NULL, cx, NULL, t175);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t176 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                    if (!t176) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_src6lm8esmv16p1, 151, 68);
                    gm_b16_8 t177 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                    gm_l35 = t177;
                }
                gm_b16_8 t178 = gm_l35;
                gm_l21 = t178;
            }
gm_lb20:;
            {
                void * t179 = (void *)((uint8_t *)&F.l7);
                uint32_t t180 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                uint64_t t181 = gm_l22;
                uint64_t t182 = gm_l22;
                gm_tl874t2atnlh3 t183 = GM_CONV(gm_tl874t2atnlh3, gm_f_span_64igq5o(t180, t181, t182));
                gm_b16_8 t184 = gm_l21;
                gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)&t184));
                gm_tbgd14esf0jvi t186;
                GM_ST(uint32_t, (uint8_t *)&t186 + 12, 0);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t186 + 0), t183);
                GM_ST(gm_b16_8, ((uint8_t *)&t186 + 16), t185);
                gm_tftb1odc739t5 t187 = gm_f_push_k96mobn(t179, &t186);
                gm_l38 = t187;
            }
            uint8_t t188 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
            if (!t188) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_src6lm8esmv16p1, 153, 9);
            gm_b16_8 t189 = gm_l6;
            gm_l19 = t189;
        }
gm_lb15:;
    }
    {
        void * t190 = (void *)((uint8_t *)&F.l7);
        gm_tl874t2atnlh3 t191 = gm_l3;
        gm_b16_8 t192 = gm_l19;
        gm_b16_8 t193 = gm_l2;
        gm_t6j6smj7qoctu t194;
        GM_ST(gm_b16_8, ((uint8_t *)&t194 + 0), t192);
        GM_ST(gm_b16_8, ((uint8_t *)&t194 + 16), t193);
        gm_f_format_ahgk48j((void *)&gm_l39, (void *)NULL, cx, NULL, &t194);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t195 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
        if (!t195) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_src6lm8esmv16p1, 156, 44);
        gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
        gm_b16_8 t197 = GM_LD(gm_b16_8, ((uint8_t *)&t196));
        gm_tbgd14esf0jvi t198;
        GM_ST(uint32_t, (uint8_t *)&t198 + 12, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t198 + 0), t191);
        GM_ST(gm_b16_8, ((uint8_t *)&t198 + 16), t197);
        gm_tftb1odc739t5 t199 = gm_f_push_k96mobn(t190, &t198);
        gm_l40 = t199;
        uint8_t t200 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
        if (!t200) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_src6lm8esmv16p1, 156, 5);
        gm_trqfvmjtt4sij t201 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l7));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 44, 0);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out)), t201);
        ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out))[40] = 1;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_decl_used_oldf2ja(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) uint8_t gm_f_is_used_outside_tbk81ks(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2) {
    struct gm_fr_lookup_module_name_cukfp7t F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    uint64_t gm_l3; /* d */
    uint64_t gm_l4; /* prelude */
    uint64_t gm_l5; /* d */
    void * gm_l6; /* m */
    GM_STACK_CHECK(&F, 699u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l7;
    gm_tf5srh4mp4b3i gm_l8;
    gm_tf5srh4mp4b3i gm_l9;
    gm_tf5srh4mp4b3i gm_l10;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            uint64_t t6 = gm_l1;
            gm_b16_8 t7 = gm_l2;
            gm_tf5srh4mp4b3i t8 = gm_f_lookup_59juqoo(t5, t6, t7);
            gm_l7 = t8;
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
            if (!t9) {
                goto gm_lb2;
            }
            uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
            gm_l3 = t10;
            void * t11 = gm_l0;
            uint64_t t12 = gm_l3;
            gm_b0_1 t13 = gm_f_note_decl_use_ui57s3b((void *)NULL, cx, NULL, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t t14 = gm_l3;
            gm_tf5srh4mp4b3i t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[8] = 1;
            F.ret = t15;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint8_t t18 = (uint8_t)(((((uint8_t *)t17) + 320))[8] != 0);
    if (t18) {
        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t17) + 320));
        gm_l4 = t19;
    } else {
        gm_tf5srh4mp4b3i t20;
        memset(&t20, 0, sizeof t20);
        F.ret = t20;
        goto gm_fret;
    }
    void * t21 = gm_l0;
    void * t22 = GM_LD(void *, ((uint8_t *)t21));
    uint64_t t23 = gm_l4;
    gm_b16_8 t24 = gm_l2;
    gm_tf5srh4mp4b3i t25 = gm_f_lookup_59juqoo(t22, t23, t24);
    gm_l8 = t25;
    uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
    if (t26) {
        uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
        gm_l5 = t27;
    } else {
        gm_tf5srh4mp4b3i t28;
        memset(&t28, 0, sizeof t28);
        F.ret = t28;
        goto gm_fret;
    }
    void * t29 = gm_l0;
    void * t30 = GM_LD(void *, ((uint8_t *)t29));
    void * t31 = GM_LD(void *, (((uint8_t *)t30) + 200));
    uint64_t t32 = GM_LD(uint64_t, ((((uint8_t *)t30) + 200) + 8));
    uint64_t t33 = gm_l1;
    if ((uintptr_t)t33 >= (uintptr_t)(t32)) gm_trap_index((uintptr_t)t33, t32, gm_src6lm8esmv16p1, 31, 11);
    void * t34 = (void *)(((uint8_t *)t31) + (uintptr_t)t33 * 184);
    gm_l6 = t34;
    void * t35 = gm_l6;
    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t35) + 112));
    if (t36) {
        uint64_t t37 = gm_l5;
        gm_tf5srh4mp4b3i t38;
        GM_ST(uint8_t, (uint8_t *)&t38 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t38 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t38 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t38), t37);
        ((uint8_t *)&t38)[8] = 1;
        F.ret = t38;
        goto gm_fret;
    }
    void * t39 = gm_l0;
    void * t40 = GM_LD(void *, ((uint8_t *)t39));
    void * t41 = GM_LD(void *, (((uint8_t *)t40) + 240));
    uint64_t t42 = GM_LD(uint64_t, ((((uint8_t *)t40) + 240) + 8));
    uint64_t t43 = gm_l5;
    if ((uintptr_t)t43 >= (uintptr_t)(t42)) gm_trap_index((uintptr_t)t43, t42, gm_src6lm8esmv16p1, 33, 8);
    gm_t9qkh0rsooc8d t44 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t41) + (uintptr_t)t43 * 216) + 40) + 56));
    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&t44) + 0));
    uint8_t t46 = (uint8_t)(!(t45 == ((uint8_t)2ULL)));
    gm_t9qkh0rsooc8d t47;
    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)2ULL));
    if (t46) {
        gm_tf5srh4mp4b3i t48;
        memset(&t48, 0, sizeof t48);
        F.ret = t48;
        goto gm_fret;
    }
    gm_b16_8 t49 = gm_l2;
    gm_b16_8 t50;
    GM_ST(void *, (uint8_t *)&t50, (void *)(gm_st3p5nb1e6cc8));
    GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(4));
    uint8_t t51 = (gm_eq0iqiubfm7e(((uint8_t *)&t49), ((uint8_t *)&t50)));
    if (t51) {
        void * t52 = gm_l6;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)t52) + 113));
        if (t53) {
            uint64_t t54 = gm_l5;
            gm_tf5srh4mp4b3i t55;
            GM_ST(uint8_t, (uint8_t *)&t55 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t55 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t55 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t55), t54);
            ((uint8_t *)&t55)[8] = 1;
            gm_l9 = t55;
        } else {
            gm_tf5srh4mp4b3i t56;
            memset(&t56, 0, sizeof t56);
            gm_l9 = t56;
        }
        gm_tf5srh4mp4b3i t57 = gm_l9;
        F.ret = t57;
        goto gm_fret;
    }
    void * t58 = gm_l0;
    uint64_t t59 = gm_l5;
    uint8_t t60 = gm_f_universe_hidden_gsk2b8r(t58, t59);
    if (t60) {
        gm_tf5srh4mp4b3i t61;
        memset(&t61, 0, sizeof t61);
        gm_l10 = t61;
    } else {
        uint64_t t62 = gm_l5;
        gm_tf5srh4mp4b3i t63;
        GM_ST(uint8_t, (uint8_t *)&t63 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t63 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t63 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t63), t62);
        ((uint8_t *)&t63)[8] = 1;
        gm_l10 = t63;
    }
    gm_tf5srh4mp4b3i t64 = gm_l10;
    F.ret = t64;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_module_alias_b9tjeud(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_module_alias_b9tjeud F;
    gm_b16_8 gm_l0; /* path */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 217u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tf5srh4mp4b3i gm_l2;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_st3ja0nqi37bv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(1));
    gm_tf5srh4mp4b3i t4 = gm_f_rfind_ai5s2js(t2, t3);
    gm_l2 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        gm_l1 = t6;
    } else {
        gm_b16_8 t7 = gm_l0;
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_add_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `+`", gm_src6lm8esmv16p1, 91, 17);
    uint64_t t13 = r11;
    uint64_t t14 = (uintptr_t)t13;
    uint64_t t15 = (uintptr_t)t9;
    if (t14 > t15 || t15 > (uintptr_t)t9) gm_trap_slice(t14, t15, t9, gm_src6lm8esmv16p1, 91, 12);
    uint64_t t16 = ((uintptr_t)t9) - (uintptr_t)t13;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t8 + (uintptr_t)t13 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(t16));
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3) {
    struct gm_fr_module_member_5fd2qm9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* m */
    gm_t08olq97rscn3 gm_l2; /* name */
    uint64_t gm_l3; /* from */
    gm_b16_8 gm_l4; /* module_name */
    uint64_t gm_l5; /* d */
    gm_b16_8 gm_l6; /* effect_ */
    GM_STACK_CHECK(&F, 1525u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_tf5srh4mp4b3i gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_th2lqbo77pvsc gm_l11;
    gm_tabb710d9mq2e gm_l12;
    uint8_t gm_l13;
    gm_tabb710d9mq2e gm_l14;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 200));
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t7) + 200) + 8));
    uint64_t t10 = gm_l1;
    if ((uintptr_t)t10 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t10, t9, gm_src6lm8esmv16p1, 207, 20);
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t8) + (uintptr_t)t10 * 184));
    gm_l4 = t11;
    {
        {
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, ((uint8_t *)t12));
            uint64_t t14 = gm_l1;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            gm_tf5srh4mp4b3i t16 = gm_f_lookup_59juqoo(t13, t14, t15);
            gm_l8 = t16;
            uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
            if (!t17) {
                goto gm_lb2;
            }
            uint64_t t18 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
            gm_l5 = t18;
            void * t19 = gm_l0;
            uint64_t t20 = gm_l5;
            uint64_t t21 = gm_l3;
            uint8_t t22 = gm_f_decl_visible_bd168ti(t19, t20, t21);
            if (t22) {
                void * t23 = gm_l0;
                uint64_t t24 = gm_l5;
                gm_b0_1 t25 = gm_f_note_decl_use_ui57s3b((void *)NULL, cx, NULL, t23, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t26 = gm_l0;
                uint64_t t27 = gm_l5;
                uint64_t t28 = gm_l3;
                gm_b0_1 t29 = gm_f_note_use_from_2lmujsq((void *)NULL, cx, NULL, t26, t27, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t30 = gm_l0;
                uint64_t t31 = gm_l5;
                gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                gm_b0_1 t33 = gm_f_check_deprecated_j3dq26t((void *)NULL, cx, NULL, t30, t31, t32);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t34 = gm_l5;
                gm_tf5srh4mp4b3i t35;
                GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t35), t34);
                ((uint8_t *)&t35)[8] = 1;
                F.ret = t35;
                goto gm_fret;
            }
            void * t36 = gm_l0;
            void * t37 = GM_LD(void *, ((uint8_t *)t36));
            void * t38 = GM_LD(void *, (((uint8_t *)t37) + 240));
            uint64_t t39 = GM_LD(uint64_t, ((((uint8_t *)t37) + 240) + 8));
            uint64_t t40 = gm_l5;
            if ((uintptr_t)t40 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t40, t39, gm_src6lm8esmv16p1, 215, 12);
            gm_t9qkh0rsooc8d t41 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t38) + (uintptr_t)t40 * 216) + 40) + 56));
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
            uint8_t t43 = (uint8_t)((t42 == ((uint8_t)1ULL)));
            gm_t9qkh0rsooc8d t44;
            GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)1ULL));
            if (t43) {
                void * t45 = gm_l0;
                gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_b16_8 t48 = gm_l4;
                gm_t6j6smj7qoctu t49;
                GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t47);
                GM_ST(gm_b16_8, ((uint8_t *)&t49 + 16), t48);
                gm_f_format_adbd97i((void *)&gm_l9, (void *)NULL, cx, NULL, &t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src6lm8esmv16p1, 216, 32);
                gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                gm_b0_1 t53 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t45, t46, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            } else {
                void * t54 = gm_l0;
                gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_b16_8 t57 = gm_l4;
                gm_t6j6smj7qoctu t58;
                GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t56);
                GM_ST(gm_b16_8, ((uint8_t *)&t58 + 16), t57);
                gm_f_format_vlsb354((void *)&gm_l10, (void *)NULL, cx, NULL, &t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src6lm8esmv16p1, 218, 32);
                gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                gm_b0_1 t62 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t54, t55, t61);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_tf5srh4mp4b3i t63;
            memset(&t63, 0, sizeof t63);
            F.ret = t63;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            {
                void * t64 = gm_l0;
                uint64_t t65 = gm_l1;
                gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_f_public_op_effect_72d4rbe((void *)&gm_l11, t64, t65, t66);
            }
            uint8_t t67 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
            if (!t67) {
                goto gm_lb4;
            }
            gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
            gm_l6 = t68;
            {
                void * t69 = gm_l0;
                gm_tl874t2atnlh3 t70 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                gm_b16_8 t71 = gm_l6;
                gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_b16_8 t73 = gm_l4;
                gm_th2lqbo77pvsc t74;
                GM_ST(uint8_t, (uint8_t *)&t74 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t74 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t74 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t74), t73);
                ((uint8_t *)&t74)[16] = 1;
                gm_b16_8 t75 = gm_f_op_hint_rfghuf5((void *)NULL, cx, NULL, t71, t72, &t74);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b0_1 t76 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t69, t70, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_tf5srh4mp4b3i t77;
            memset(&t77, 0, sizeof t77);
            F.ret = t77;
            goto gm_fret;
        }
gm_lb4:;
    }
gm_lb3:;
    {
        gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
        gm_b16_8 t79 = gm_l4;
        gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)a2));
        gm_t6j6smj7qoctu t81;
        GM_ST(gm_b16_8, ((uint8_t *)&t81 + 0), t79);
        GM_ST(gm_b16_8, ((uint8_t *)&t81 + 16), t80);
        gm_f_format_1o1mt78((void *)&gm_l12, (void *)NULL, cx, NULL, &t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src6lm8esmv16p1, 226, 32);
        gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
        gm_t4gut2c9fj1kt t85;
        gm_f_error_s6hn871((void *)&t85, (void *)NULL, cx, NULL, t78, t84);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t85;
    }
    gm_b16_8 t86 = GM_LD(gm_b16_8, ((uint8_t *)a2));
    void * t87 = (void *)((uint8_t *)gm_c55218gtcr0);
    uint64_t t88 = ((uintptr_t)((uint64_t)28ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t89;
    GM_ST(void *, (uint8_t *)&t89, (void *)(((uint8_t *)t87 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(t88));
    uint8_t t90 = 0;
    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t89) + 8)); i++) if (gm_eq0iqiubfm7e(((uint8_t *)GM_LD(void *, ((uint8_t *)&t89))) + i * 16, ((uint8_t *)&t86))) { t90 = 1; break; }
    uint8_t t91 = (uint8_t)(t90);
    if (t91) {
        gm_l13 = ((uint8_t)1);
    } else {
        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)a2));
        gm_b16_8 t93;
        GM_ST(void *, (uint8_t *)&t93, (void *)(gm_snivf25e5fme8));
        GM_ST(uintptr_t, (uint8_t *)&t93 + 8, (uintptr_t)(5));
        gm_b16_8 t94;
        GM_ST(void *, (uint8_t *)&t94, (void *)(gm_s4pmpk07ugvq6));
        GM_ST(uintptr_t, (uint8_t *)&t94 + 8, (uintptr_t)(4));
        gm_t6j6smj7qoctu t95;
        GM_ST(gm_b16_8, ((uint8_t *)&t95 + 0), t93);
        GM_ST(gm_b16_8, ((uint8_t *)&t95 + 16), t94);
        uint8_t t96 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t95) + 0, ((uint8_t *)&t92))) t96 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t95) + 16, ((uint8_t *)&t92))) t96 = 1;
        uint8_t t97 = (uint8_t)(t96);
        gm_l13 = t97;
    }
    uint8_t t98 = gm_l13;
    if (t98) {
        void * t99 = (void *)((uint8_t *)&F.l7);
        gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)a2));
        gm_b16_8 t101 = GM_LD(gm_b16_8, ((uint8_t *)a2));
        gm_t6j6smj7qoctu t102;
        GM_ST(gm_b16_8, ((uint8_t *)&t102 + 0), t100);
        GM_ST(gm_b16_8, ((uint8_t *)&t102 + 16), t101);
        gm_f_format_p16u9uu((void *)&gm_l14, (void *)NULL, cx, NULL, &t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t103 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
        if (!t103) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src6lm8esmv16p1, 229, 20);
        gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
        gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
        gm_f_add_note_46v502s(t99, t105);
    }
    {
        void * t106 = gm_l0;
        gm_b0_1 t107 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t106, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l7)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_tf5srh4mp4b3i t108;
    memset(&t108, 0, sizeof t108);
    F.ret = t108;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_module_named_n1mcl3b F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    void * gm_l3; /* base */
    gm_t08olq97rscn3 gm_l4; /* name */
    uint64_t gm_l5; /* m */
    gm_t08olq97rscn3 gm_l6; /* id */
    uint64_t gm_l7; /* d */
    uint64_t gm_l8; /* m */
    GM_STACK_CHECK(&F, 696u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l9;
    gm_tf5srh4mp4b3i gm_l10;
    gm_t6qooom8qa2do gm_l11;
    {
        {
            void * t8 = gm_l2;
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)22ULL));
            if (!t10) {
                goto gm_lb2;
            }
            void * t11 = GM_LD(void *, ((((uint8_t *)t8) + 8) + 8));
            gm_l3 = t11;
            memmove(((uint8_t *)&gm_l4), (((((uint8_t *)t8) + 8) + 8) + 8), 32);
            void * t12 = gm_l0;
            uint64_t t13 = gm_l1;
            void * t14 = gm_l3;
            gm_tf5srh4mp4b3i t15 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t12, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t15;
            uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l9))[8] != 0);
            if (t16) {
                uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                gm_l5 = t17;
            } else {
                gm_tf5srh4mp4b3i t18;
                memset(&t18, 0, sizeof t18);
                F.ret = t18;
                goto gm_fret;
            }
            void * t19 = gm_l0;
            uint64_t t20 = gm_l5;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            uint64_t t22 = gm_l1;
            gm_tf5srh4mp4b3i t23 = gm_f_public_module_member_e2i0at7((void *)NULL, cx, NULL, t19, t20, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t23;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t24 = gm_l2;
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)6ULL));
            if (!t26) {
                goto gm_lb4;
            }
            memmove(((uint8_t *)&gm_l6), ((((uint8_t *)t24) + 8) + 8), 32);
            void * t27 = gm_l0;
            uint64_t t28 = gm_l1;
            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
            gm_tf5srh4mp4b3i t30 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t27, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t30;
            uint8_t t31 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
            if (t31) {
                uint64_t t32 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                gm_l7 = t32;
            } else {
                gm_tf5srh4mp4b3i t33;
                memset(&t33, 0, sizeof t33);
                F.ret = t33;
                goto gm_fret;
            }
            {
                {
                    void * t34 = gm_l0;
                    uint64_t t35 = gm_l7;
                    gm_f_resolve_decl_pus042v((void *)&gm_l11, (void *)NULL, cx, NULL, t34, t35);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)9ULL));
                    if (!t37) {
                        goto gm_lb6;
                    }
                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    gm_l8 = t38;
                    uint64_t t39 = gm_l8;
                    gm_tf5srh4mp4b3i t40;
                    GM_ST(uint8_t, (uint8_t *)&t40 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t40 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t40 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t40), t39);
                    ((uint8_t *)&t40)[8] = 1;
                    F.ret = t40;
                    goto gm_fret;
                }
gm_lb6:;
                gm_tf5srh4mp4b3i t41;
                memset(&t41, 0, sizeof t41);
                F.ret = t41;
                goto gm_fret;
            }
gm_lb5:;
        }
gm_lb4:;
        gm_tf5srh4mp4b3i t42;
        memset(&t42, 0, sizeof t42);
        F.ret = t42;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_note_decl_use_ui57s3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_note_local_use_h4lf1jh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_note_local_use_h4lf1jh F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* decl */
    gm_tl874t2atnlh3 gm_l2; /* at */
    uint32_t gm_l3; /* n */
    GM_STACK_CHECK(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l4;
    gm_t9805qtonbsag gm_l5;
    gm_tftb1odc739t5 gm_l6;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 2744);
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_tl874t2atnlh3 t5 = gm_l2;
        gm_t2p74ki8sl4an t6;
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 0), t4);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t6 + 12), t5);
        uint8_t t7 = gm_f_contains_a8d6g03(t3, &t6);
        if (t7) {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 2744);
        gm_tl874t2atnlh3 t10 = gm_l1;
        gm_tl874t2atnlh3 t11 = gm_l2;
        gm_t2p74ki8sl4an t12;
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t12 + 0), t10);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t12 + 12), t11);
        gm_tftb1odc739t5 t13 = gm_f_put_h767iu8(t9, &t12, ((uint8_t)1));
        gm_l4 = t13;
    }
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_src6lm8esmv16p1, 55, 5);
    void * t15 = gm_l0;
    void * t16 = (void *)(((uint8_t *)t15) + 2800);
    gm_tl874t2atnlh3 t17 = gm_l1;
    gm_t9805qtonbsag t18 = gm_f_get_rlnefnq(t16, GM_CONV(gm_cseahvg8v7p0vs, t17));
    gm_l5 = t18;
    uint8_t t19 = (uint8_t)((((uint8_t *)&gm_l5))[4] != 0);
    if (t19) {
        uint32_t t20 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
        gm_l3 = t20;
    } else {
        gm_l3 = ((uint32_t)0ULL);
    }
    void * t21 = gm_l0;
    void * t22 = (void *)(((uint8_t *)t21) + 2800);
    gm_tl874t2atnlh3 t23 = gm_l1;
    uint32_t t24 = gm_l3;
    uint32_t r25; bool o26 = __builtin_add_overflow(t24, ((uint32_t)1ULL), &r25);
    if (o26) gm_trap("integer overflow in `+`", gm_src6lm8esmv16p1, 57, 34);
    uint32_t t27 = r25;
    gm_tftb1odc739t5 t28 = gm_f_put_3e4k1js(t22, GM_CONV(gm_cseahvg8v7p0vs, t23), t27);
    gm_l6 = t28;
    uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src6lm8esmv16p1, 57, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_note_use_from_2lmujsq(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2) {
    struct gm_fr_note_use_from_2lmujsq F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* d */
    uint64_t gm_l2; /* from */
    GM_STACK_CHECK(&F, 159u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    void * t7 = GM_LD(void *, (((uint8_t *)t6) + 240));
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t6) + 240) + 8));
    uint64_t t9 = gm_l1;
    if ((uintptr_t)t9 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t9, t8, gm_src6lm8esmv16p1, 47, 8);
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t7) + (uintptr_t)t9 * 216) + 32));
    uint64_t t11 = gm_l2;
    uint8_t t12 = ((t10 == t11));
    if (t12) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        for (;;) {
            void * t13 = gm_l0;
            uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t13) + 2704) + 8));
            uint64_t t15 = gm_l1;
            uint8_t t16 = (t14 <= t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                void * t17 = gm_l0;
                void * t18 = (void *)(((uint8_t *)t17) + 2704);
                gm_tftb1odc739t5 t19 = gm_f_push_vd1290p(t18, ((uint8_t)0));
                gm_l3 = t19;
                uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
                if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_src6lm8esmv16p1, 48, 43);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t21 = gm_l0;
    void * t22 = GM_LD(void *, (((uint8_t *)t21) + 2704));
    uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 2704) + 8));
    uint64_t t24 = gm_l1;
    if ((uintptr_t)t24 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t24, t23, gm_src6lm8esmv16p1, 49, 5);
    GM_ST(uint8_t, (((uint8_t *)t22) + (uintptr_t)t24 * 1), ((uint8_t)1));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_op_effect_n2jr58f(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_op_effect_n2jr58f F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* e */
    uint64_t gm_l4; /* prelude */
    GM_STACK_CHECK_SHALLOW(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_th2lqbo77pvsc gm_l5;
    {
        {
            {
                void * t4 = gm_l0;
                uint64_t t5 = gm_l1;
                gm_b16_8 t6 = gm_l2;
                gm_f_effect_with_op_ol1t6ca((void *)&gm_l5, t4, t5, t6, ((uint8_t)0));
            }
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
            gm_l3 = t8;
            {
                gm_b16_8 t9 = gm_l3;
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t9);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    void * t10 = gm_l0;
    void * t11 = GM_LD(void *, ((uint8_t *)t10));
    uint8_t t12 = (uint8_t)(((((uint8_t *)t11) + 320))[8] != 0);
    if (t12) {
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t11) + 320));
        gm_l4 = t13;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    void * t14 = gm_l0;
    uint64_t t15 = gm_l4;
    gm_b16_8 t16 = gm_l2;
    gm_f_effect_with_op_ol1t6ca((void *)&(*(gm_th2lqbo77pvsc *)gm_out), t14, t15, t16, ((uint8_t)1));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_op_effect_n2jr58f_c(void * a0, uint64_t a1, gm_b16_8 a2) { gm_th2lqbo77pvsc gm_r; gm_f_op_effect_n2jr58f(&gm_r, a0, a1, a2); return gm_r; }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_op_hint_rfghuf5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_op_hint_rfghuf5 F;
    gm_b16_8 gm_l0; /* effect_ */
    gm_b16_8 gm_l1; /* name */
    gm_th2lqbo77pvsc gm_l2; /* qualifier */
    gm_b16_8 gm_l3; /* m */
    gm_b16_8 gm_l4; /* q */
    GM_STACK_CHECK(&F, 459u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&gm_l2, a2, sizeof gm_l2);
    gm_tabb710d9mq2e gm_l5;
    gm_tabb710d9mq2e gm_l6;
    {
        {
            uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
            if (!t4) {
                goto gm_lb2;
            }
            gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
            gm_l3 = t5;
            gm_b16_8 t6 = gm_l3;
            gm_tnpifv20irnph t7;
            GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t6);
            gm_f_format_6kfrubb((void *)&gm_l5, (void *)NULL, cx, NULL, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_src6lm8esmv16p1, 105, 37);
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
            gm_l4 = t10;
            goto gm_lb1;
        }
gm_lb2:;
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(0));
        gm_l4 = t11;
    }
gm_lb1:;
    {
        gm_b16_8 t12 = gm_l1;
        gm_b16_8 t13 = gm_l0;
        gm_b16_8 t14 = gm_l4;
        gm_b16_8 t15 = gm_l0;
        gm_b16_8 t16 = gm_l1;
        gm_tud7oovfvnf43 t17;
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t12);
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t13);
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 32), t14);
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 48), t15);
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 64), t16);
        gm_f_format_mqdhvcm((void *)&gm_l6, (void *)NULL, cx, NULL, &t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_src6lm8esmv16p1, 106, 12);
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_public_module_member_e2i0at7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3) {
    struct gm_fr_public_module_member_e2i0at7 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l2; /* name */
    uint64_t gm_l3; /* from */
    uint64_t gm_l4; /* d */
    uint64_t gm_l5; /* r */
    GM_STACK_CHECK(&F, 525u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tf5srh4mp4b3i gm_l6;
    gm_t6qooom8qa2do gm_l7;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint64_t t6 = gm_l1;
    gm_b16_8 t7 = gm_l2;
    gm_tf5srh4mp4b3i t8 = gm_f_lookup_59juqoo(t5, t6, t7);
    gm_l6 = t8;
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l6))[8] != 0);
    if (t9) {
        uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
        gm_l4 = t10;
    } else {
        gm_tf5srh4mp4b3i t11;
        memset(&t11, 0, sizeof t11);
        F.ret = t11;
        goto gm_fret;
    }
    void * t12 = gm_l0;
    uint64_t t13 = gm_l4;
    uint64_t t14 = gm_l3;
    uint8_t t15 = gm_f_decl_visible_bd168ti(t12, t13, t14);
    uint8_t t16 = (uint8_t)!t15;
    if (t16) {
        gm_tf5srh4mp4b3i t17;
        memset(&t17, 0, sizeof t17);
        F.ret = t17;
        goto gm_fret;
    }
    void * t18 = gm_l0;
    uint64_t t19 = gm_l4;
    uint64_t t20 = gm_l3;
    gm_b0_1 t21 = gm_f_note_use_from_2lmujsq((void *)NULL, cx, NULL, t18, t19, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            void * t22 = gm_l0;
            uint64_t t23 = gm_l4;
            gm_f_resolve_decl_pus042v((void *)&gm_l7, (void *)NULL, cx, NULL, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)9ULL));
            if (!t25) {
                goto gm_lb2;
            }
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            gm_l5 = t26;
            uint64_t t27 = gm_l5;
            gm_tf5srh4mp4b3i t28;
            GM_ST(uint8_t, (uint8_t *)&t28 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t28 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t28 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t28), t27);
            ((uint8_t *)&t28)[8] = 1;
            F.ret = t28;
            goto gm_fret;
        }
gm_lb2:;
        gm_tf5srh4mp4b3i t29;
        memset(&t29, 0, sizeof t29);
        F.ret = t29;
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
__attribute__((aligned(4))) void gm_f_public_op_effect_72d4rbe(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_public_op_effect_72d4rbe F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t3877pk7lv748 gm_l5; /* n */
    void * gm_l6; /* d */
    void * gm_l8; /* lit */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 459u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    void * t18 = GM_LD(void *, (((uint8_t *)t17) + 200));
    uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)t17) + 200) + 8));
    uint64_t t20 = gm_l1;
    if ((uintptr_t)t20 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t20, t19, gm_src6lm8esmv16p1, 277, 14);
    gm_b16_8 t21 = GM_LD(gm_b16_8, ((((uint8_t *)t18) + (uintptr_t)t20 * 184) + 16));
    gm_l3 = t21;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t22 = gm_l4;
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t24 = (t22 < t23);
            if (!t24) {
                goto gm_lb1;
            }
            {
                void * t25 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t27 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t25) + (uintptr_t)t27 * 24), 24);
                        void * t28 = gm_l0;
                        void * t29 = GM_LD(void *, ((uint8_t *)t28));
                        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 240));
                        uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)t29) + 240) + 8));
                        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 16));
                        if ((uintptr_t)t32 >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)t32, t31, gm_src6lm8esmv16p1, 278, 15);
                        void * t33 = (void *)(((uint8_t *)t30) + (uintptr_t)t32 * 216);
                        gm_l6 = t33;
                        void * t34 = gm_l6;
                        gm_t9qkh0rsooc8d t35 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t34) + 40) + 56));
                        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
                        uint8_t t37 = (uint8_t)((t36 == ((uint8_t)0ULL)));
                        gm_t9qkh0rsooc8d t38;
                        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
                        if (t37) {
                            goto gm_lb3;
                        }
                        {
                            {
                                void * t39 = gm_l6;
                                uint8_t t40 = GM_LD(uint8_t, (((((uint8_t *)t39) + 40) + 64) + 0));
                                uint8_t t41 = (uint8_t)(t40 == ((uint8_t)0ULL));
                                if (!t41) {
                                    goto gm_lb7;
                                }
                                memmove(((uint8_t *)&F.l7), (((((uint8_t *)t39) + 40) + 64) + 8), 64);
                                {
                                    {
                                        void * t42 = GM_LD(void *, (((uint8_t *)&F.l7) + 40));
                                        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t42) + 8) + 0));
                                        uint8_t t44 = (uint8_t)(t43 == ((uint8_t)34ULL));
                                        if (!t44) {
                                            goto gm_lb9;
                                        }
                                        void * t45 = GM_LD(void *, ((((uint8_t *)t42) + 8) + 8));
                                        gm_l8 = t45;
                                        void * t46 = gm_l8;
                                        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t46) + 24));
                                        gm_l9 = t47;
                                        gm_l10 = ((uint64_t)0ULL);
                                        {
                                            for (;;) {
                                                uint64_t t48 = gm_l10;
                                                uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                                uint8_t t50 = (t48 < t49);
                                                if (!t50) {
                                                    goto gm_lb10;
                                                }
                                                {
                                                    void * t51 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                                    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                                    uint64_t t53 = gm_l10;
                                                    {
                                                        {
                                                            memmove(((uint8_t *)&F.l11), (((uint8_t *)t51) + (uintptr_t)t53 * 80), 80);
                                                            gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&F.l11));
                                                            gm_b16_8 t55 = gm_l2;
                                                            uint8_t t56 = (gm_eq0iqiubfm7e(((uint8_t *)&t54), ((uint8_t *)&t55)));
                                                            if (t56) {
                                                                void * t57 = gm_l6;
                                                                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)t57));
                                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                                                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t58);
                                                                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                                                                goto gm_fdone;
                                                            }
                                                            goto gm_lb13;
                                                        }
gm_lb14:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 282, 9);
                                                    }
gm_lb13:;
                                                }
gm_lb12:;
                                                uint64_t t59 = gm_l10;
                                                uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                                                uint64_t t62 = r60;
                                                gm_l10 = t62;
                                            }
                                        }
gm_lb10:;
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    goto gm_lb3;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            goto gm_lb3;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 277, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t63 = gm_l4;
            uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
            uint64_t t66 = r64;
            gm_l4 = t66;
        }
    }
gm_lb1:;
    memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_public_op_effect_72d4rbe_c(void * a0, uint64_t a1, gm_b16_8 a2) { gm_th2lqbo77pvsc gm_r; gm_f_public_op_effect_72d4rbe(&gm_r, a0, a1, a2); return gm_r; }
inline __attribute__((aligned(4))) uint8_t gm_f_same_package_vmps24r(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_package_vmps24r F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* a */
    uint64_t gm_l2; /* b */
    void * gm_l3; /* ma */
    void * gm_l4; /* mb */
    GM_STACK_CHECK_SHALLOW(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l5;
    uint8_t gm_l6;
    uint64_t t2 = gm_l1;
    uint64_t t3 = gm_l2;
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    void * t5 = gm_l0;
    void * t6 = GM_LD(void *, ((uint8_t *)t5));
    void * t7 = GM_LD(void *, (((uint8_t *)t6) + 200));
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t6) + 200) + 8));
    uint64_t t9 = gm_l1;
    if ((uintptr_t)t9 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t9, t8, gm_src6lm8esmv16p1, 175, 12);
    void * t10 = (void *)(((uint8_t *)t7) + (uintptr_t)t9 * 184);
    gm_l3 = t10;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    void * t13 = GM_LD(void *, (((uint8_t *)t12) + 200));
    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 200) + 8));
    uint64_t t15 = gm_l2;
    if ((uintptr_t)t15 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t15, t14, gm_src6lm8esmv16p1, 176, 12);
    void * t16 = (void *)(((uint8_t *)t13) + (uintptr_t)t15 * 184);
    gm_l4 = t16;
    void * t17 = gm_l3;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)t17) + 112));
    uint8_t t19 = (uint8_t)!t18;
    if (t19) {
        void * t20 = gm_l4;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t20) + 112));
        uint8_t t22 = (uint8_t)!t21;
        gm_l6 = t22;
    } else {
        gm_l6 = ((uint8_t)0);
    }
    uint8_t t23 = gm_l6;
    if (t23) {
        void * t24 = gm_l3;
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 136));
        void * t26 = gm_l4;
        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)t26) + 136));
        uint8_t t28 = ((t25 == t27));
        gm_l5 = t28;
    } else {
        gm_l5 = ((uint8_t)0);
    }
    uint8_t t29 = gm_l5;
    F.ret = t29;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_std_home_dsdl68c(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_std_home_dsdl68c F;
    gm_b16_8 gm_l0; /* name */
    GM_STACK_CHECK_SHALLOW(&F, 874u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_t9bmh80edou9c t3 = GM_LD(gm_t9bmh80edou9c, ((uint8_t *)gm_c0a4m567j25));
        uint8_t t4 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(3); i++) if (gm_eq0iqiubfm7e(((uint8_t *)&t3) + i * 16, ((uint8_t *)&t2))) { t4 = 1; break; }
        uint8_t t5 = (uint8_t)(t4);
        if (t5) {
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s2470an4q44lj));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(24));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t6);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sgqs124m28gfg));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(6));
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_so97uarpe8rtg));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(8));
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_srugm21uf2sb0));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(8));
        gm_tfeab279c16rr t11;
        GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t8);
        GM_ST(gm_b16_8, ((uint8_t *)&t11 + 16), t9);
        GM_ST(gm_b16_8, ((uint8_t *)&t11 + 32), t10);
        uint8_t t12 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t11) + 0, ((uint8_t *)&t7))) t12 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t11) + 16, ((uint8_t *)&t7))) t12 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t11) + 32, ((uint8_t *)&t7))) t12 = 1;
        uint8_t t13 = (uint8_t)(t12);
        if (t13) {
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_stingg155o3k5));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(7));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t14);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t15 = gm_l0;
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s48njnbffa02m));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(4));
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sgqcrkl2nh339));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(3));
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_snil57vrv77lr));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(11));
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s5le7rpnp1iq6));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(8));
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s84ckbhm2d3ul));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(8));
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_snu0769072h68));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(8));
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_shrc3j552p60m));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(8));
        gm_th2d9c7aju8hp t23;
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t16);
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 16), t17);
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 32), t18);
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 48), t19);
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 64), t20);
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 80), t21);
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 96), t22);
        uint8_t t24 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t23) + 0, ((uint8_t *)&t15))) t24 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t23) + 16, ((uint8_t *)&t15))) t24 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t23) + 32, ((uint8_t *)&t15))) t24 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t23) + 48, ((uint8_t *)&t15))) t24 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t23) + 64, ((uint8_t *)&t15))) t24 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t23) + 80, ((uint8_t *)&t15))) t24 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t23) + 96, ((uint8_t *)&t15))) t24 = 1;
        uint8_t t25 = (uint8_t)(t24);
        if (t25) {
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_su6mi500md00b));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(6));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t26);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t27 = gm_l0;
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_sulfrs2bmdlu2));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(5));
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_smnot3vdfhqsc));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(5));
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_suhmdi1gd6hta));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(6));
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sonl5k4jilrbc));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(12));
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sj97id02v9fh3));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(5));
        gm_b16_8 t33;
        GM_ST(void *, (uint8_t *)&t33, (void *)(gm_ssqd2isjrp8nh));
        GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(10));
        gm_tif3elqk01bdl t34;
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 0), t28);
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 16), t29);
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 32), t30);
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 48), t31);
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 64), t32);
        GM_ST(gm_b16_8, ((uint8_t *)&t34 + 80), t33);
        uint8_t t35 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t34) + 0, ((uint8_t *)&t27))) t35 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t34) + 16, ((uint8_t *)&t27))) t35 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t34) + 32, ((uint8_t *)&t27))) t35 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t34) + 48, ((uint8_t *)&t27))) t35 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t34) + 64, ((uint8_t *)&t27))) t35 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t34) + 80, ((uint8_t *)&t27))) t35 = 1;
        uint8_t t36 = (uint8_t)(t35);
        if (t36) {
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s88rffcs9cj2p));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(11));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t37);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t38 = gm_l0;
        gm_b16_8 t39;
        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s79oh9p16aa8u));
        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(2));
        gm_b16_8 t40;
        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sltqebpmj61tp));
        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(2));
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s16bsnuu7i04s));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(2));
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s1j5pceke7iuk));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(4));
        gm_t0uuhaljsf410 t43;
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t39);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 16), t40);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 32), t41);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 48), t42);
        uint8_t t44 = 0;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t43) + 0, ((uint8_t *)&t38))) t44 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t43) + 16, ((uint8_t *)&t38))) t44 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t43) + 32, ((uint8_t *)&t38))) t44 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t43) + 48, ((uint8_t *)&t38))) t44 = 1;
        uint8_t t45 = (uint8_t)(t44);
        if (t45) {
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s8hpuukc6o9qu));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t46);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            goto gm_fdone;
        }
    }
    {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_std_home_dsdl68c_c(gm_b16_8 a0) { gm_th2lqbo77pvsc gm_r; gm_f_std_home_dsdl68c(&gm_r, a0); return gm_r; }
extern __attribute__((aligned(4))) uint8_t gm_f_universe_hidden_gsk2b8r(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_universe_hint_ams3f10(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_universe_hint_ams3f10 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* prelude */
    uint64_t gm_l3; /* d */
    gm_b16_8 gm_l4; /* m */
    GM_STACK_CHECK(&F, 792u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l5;
    gm_tf5srh4mp4b3i gm_l6;
    uint8_t gm_l7;
    uint8_t gm_l8;
    gm_th2lqbo77pvsc gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tabb710d9mq2e gm_l11;
    {
        gm_b16_8 t7 = gm_l1;
        gm_t9bmh80edou9c t8 = GM_LD(gm_t9bmh80edou9c, ((uint8_t *)gm_c0a4m567j25));
        uint8_t t9 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(3); i++) if (gm_eq0iqiubfm7e(((uint8_t *)&t8) + i * 16, ((uint8_t *)&t7))) { t9 = 1; break; }
        uint8_t t10 = (uint8_t)(t9);
        if (t10) {
            {
                gm_b16_8 t11 = gm_l1;
                gm_b16_8 t12 = gm_l1;
                gm_t6j6smj7qoctu t13;
                GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t11);
                GM_ST(gm_b16_8, ((uint8_t *)&t13 + 16), t12);
                gm_f_format_c610h7d((void *)&gm_l5, (void *)NULL, cx, NULL, &t13);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_src6lm8esmv16p1, 74, 16);
            {
                gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t16);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                goto gm_fdone;
            }
        }
    }
    void * t17 = gm_l0;
    void * t18 = GM_LD(void *, ((uint8_t *)t17));
    uint8_t t19 = (uint8_t)(((((uint8_t *)t18) + 320))[8] != 0);
    if (t19) {
        uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t18) + 320));
        gm_l2 = t20;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    void * t21 = gm_l0;
    void * t22 = GM_LD(void *, ((uint8_t *)t21));
    uint64_t t23 = gm_l2;
    gm_b16_8 t24 = gm_l1;
    gm_tf5srh4mp4b3i t25 = gm_f_lookup_59juqoo(t22, t23, t24);
    gm_l6 = t25;
    uint8_t t26 = (uint8_t)((((uint8_t *)&gm_l6))[8] != 0);
    if (t26) {
        uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
        gm_l3 = t27;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    void * t28 = gm_l0;
    uint64_t t29 = gm_l3;
    uint8_t t30 = gm_f_universe_hidden_gsk2b8r(t28, t29);
    uint8_t t31 = (uint8_t)!t30;
    if (t31) {
        gm_l7 = ((uint8_t)1);
    } else {
        void * t32 = gm_l0;
        void * t33 = GM_LD(void *, ((uint8_t *)t32));
        void * t34 = GM_LD(void *, (((uint8_t *)t33) + 240));
        uint64_t t35 = GM_LD(uint64_t, ((((uint8_t *)t33) + 240) + 8));
        uint64_t t36 = gm_l3;
        if ((uintptr_t)t36 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t36, t35, gm_src6lm8esmv16p1, 78, 36);
        {
            {
                uint8_t t37 = GM_LD(uint8_t, (((((uint8_t *)t34) + (uintptr_t)t36 * 216) + 200) + 0));
                uint8_t t38 = (uint8_t)(t37 == ((uint8_t)1ULL));
                if (!t38) {
                    goto gm_lb2;
                }
                gm_l8 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l8 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 78, 36);
        }
gm_lb1:;
        uint8_t t39 = gm_l8;
        gm_l7 = t39;
    }
    uint8_t t40 = gm_l7;
    if (t40) {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    {
        {
            {
                gm_b16_8 t41 = gm_l1;
                gm_f_std_home_dsdl68c((void *)&gm_l9, t41);
            }
            uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
            if (!t42) {
                goto gm_lb5;
            }
            gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
            gm_l4 = t43;
            {
                gm_b16_8 t44 = gm_l1;
                gm_b16_8 t45 = gm_l4;
                gm_b16_8 t46 = gm_l4;
                gm_b16_8 t47 = gm_f_module_alias_b9tjeud(t46);
                gm_b16_8 t48 = gm_l1;
                gm_t0uuhaljsf410 t49;
                GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t44);
                GM_ST(gm_b16_8, ((uint8_t *)&t49 + 16), t45);
                GM_ST(gm_b16_8, ((uint8_t *)&t49 + 32), t47);
                GM_ST(gm_b16_8, ((uint8_t *)&t49 + 48), t48);
                gm_f_format_9ihba55((void *)&gm_l10, (void *)NULL, cx, NULL, &t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src6lm8esmv16p1, 80, 16);
            {
                gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t52);
                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                goto gm_fdone;
            }
        }
gm_lb5:;
    }
gm_lb4:;
    {
        gm_b16_8 t53 = gm_l1;
        gm_b16_8 t54 = gm_l1;
        gm_t6j6smj7qoctu t55;
        GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t53);
        GM_ST(gm_b16_8, ((uint8_t *)&t55 + 16), t54);
        gm_f_format_vdba58s((void *)&gm_l11, (void *)NULL, cx, NULL, &t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_src6lm8esmv16p1, 82, 12);
    {
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t58);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_unknown_name_dmegum5(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_unknown_name_dmegum5 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_b16_8 gm_l4; /* effect_ */
    gm_b16_8 gm_l7; /* hint */
    GM_STACK_CHECK(&F, 1303u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_th2lqbo77pvsc gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_tabb710d9mq2e gm_l12;
    gm_th2lqbo77pvsc gm_l13;
    gm_tqetrgo0q24b5 gm_l14;
    gm_tabb710d9mq2e gm_l15;
    {
        {
            {
                void * t8 = gm_l0;
                uint64_t t9 = gm_l1;
                gm_b16_8 t10 = gm_l2;
                gm_f_op_effect_n2jr58f((void *)&gm_l9, t8, t9, t10);
            }
            uint8_t t11 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
            if (!t11) {
                goto gm_lb2;
            }
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
            gm_l4 = t12;
            {
                gm_tl874t2atnlh3 t13 = gm_l3;
                gm_b16_8 t14 = gm_l4;
                gm_b16_8 t15 = gm_l2;
                gm_th2lqbo77pvsc t16;
                memset(&t16, 0, sizeof t16);
                gm_b16_8 t17 = gm_f_op_hint_rfghuf5((void *)NULL, cx, NULL, t14, t15, &t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t4gut2c9fj1kt t18;
                gm_f_error_s6hn871((void *)&t18, (void *)NULL, cx, NULL, t13, t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l5 = t18;
            }
            {
                void * t19 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t20 = gm_l4;
                gm_b16_8 t21 = gm_l2;
                gm_t6j6smj7qoctu t22;
                GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t20);
                GM_ST(gm_b16_8, ((uint8_t *)&t22 + 16), t21);
                gm_f_format_7her5m9((void *)&gm_l10, (void *)NULL, cx, NULL, &t22);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src6lm8esmv16p1, 261, 19);
                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
                uint32_t t26 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
                uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
                gm_tl874t2atnlh3 t29;
                GM_ST(uint32_t, ((uint8_t *)&t29 + 0), t26);
                GM_ST(uint32_t, ((uint8_t *)&t29 + 4), t27);
                GM_ST(uint32_t, ((uint8_t *)&t29 + 8), t28);
                gm_b16_8 t30 = gm_l4;
                gm_tnpifv20irnph t31;
                GM_ST(gm_b16_8, ((uint8_t *)&t31 + 0), t30);
                gm_f_format_6kfrubb((void *)&gm_l11, (void *)NULL, cx, NULL, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_src6lm8esmv16p1, 261, 120);
                gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
                gm_b0_1 t35 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t19, t25, t29, t34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t36 = gm_l0;
                gm_b0_1 t37 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t36, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l5)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_tl874t2atnlh3 t38 = gm_l3;
        gm_b16_8 t39 = gm_l2;
        gm_tnpifv20irnph t40;
        GM_ST(gm_b16_8, ((uint8_t *)&t40 + 0), t39);
        gm_f_format_sbfc2dl((void *)&gm_l12, (void *)NULL, cx, NULL, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src6lm8esmv16p1, 265, 27);
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
        gm_t4gut2c9fj1kt t44;
        gm_f_error_s6hn871((void *)&t44, (void *)NULL, cx, NULL, t38, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t44;
    }
    {
        {
            void * t45 = gm_l0;
            gm_b16_8 t46 = gm_l2;
            gm_f_universe_hint_ams3f10((void *)&gm_l13, (void *)NULL, cx, NULL, t45, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t47 = (uint8_t)((((uint8_t *)&gm_l13))[16] != 0);
            if (!t47) {
                goto gm_lb4;
            }
            gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
            gm_l7 = t48;
            void * t49 = (void *)((uint8_t *)&F.l6);
            gm_b16_8 t50 = gm_l7;
            gm_f_add_note_46v502s(t49, t50);
            {
                {
                    {
                        void * t51 = gm_l0;
                        uint64_t t52 = gm_l1;
                        gm_b16_8 t53 = gm_l2;
                        gm_tl874t2atnlh3 t54 = gm_l3;
                        gm_f_import_fix_7822666((void *)&gm_l14, (void *)NULL, cx, NULL, t51, t52, t53, t54);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t55 = (uint8_t)((((uint8_t *)&gm_l14))[40] != 0);
                    if (!t55) {
                        goto gm_lb6;
                    }
                    {
                        memmove(((uint8_t *)&F.l8), ((uint8_t *)&gm_l14), 40);
                    }
                    {
                        void * t56 = (void *)((uint8_t *)&F.l6);
                        void * t57 = GM_LD(void *, ((uint8_t *)&F.l8));
                        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                        uint64_t r60; bool o61 = __builtin_sub_overflow(t59, ((uint64_t)1ULL), &r60);
                        if (o61) gm_trap("integer overflow in `-`", gm_src6lm8esmv16p1, 269, 66);
                        uint64_t t62 = r60;
                        if ((uintptr_t)t62 >= (uintptr_t)(t58)) gm_trap_index((uintptr_t)t62, t58, gm_src6lm8esmv16p1, 269, 54);
                        gm_b16_8 t63 = GM_LD(gm_b16_8, ((((uint8_t *)t57) + (uintptr_t)t62 * 32) + 16));
                        gm_tnpifv20irnph t64;
                        GM_ST(gm_b16_8, ((uint8_t *)&t64 + 0), t63);
                        gm_f_format_ooibcq2((void *)&gm_l15, (void *)NULL, cx, NULL, t64);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                        if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src6lm8esmv16p1, 269, 29);
                        gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
                        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                        gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                        gm_b0_1 t70 = gm_f_add_fix_edits_em0b48a((void *)NULL, cx, NULL, t56, t67, t69);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto gm_lb5;
                }
gm_lb6:;
            }
gm_lb5:;
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    {
        void * t71 = gm_l0;
        gm_b0_1 t72 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t71, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l6)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) uint8_t gm_f_visible_ll07dr9(void * a0, gm_t9qkh0rsooc8d a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visible_ll07dr9 F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l1; /* vis */
    uint64_t gm_l2; /* owner */
    uint64_t gm_l3; /* from */
    GM_STACK_CHECK_SHALLOW(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l4;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)2ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_l4 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)1ULL));
            if (!t9) {
                goto gm_lb3;
            }
            void * t10 = gm_l0;
            uint64_t t11 = gm_l2;
            uint64_t t12 = gm_l3;
            uint8_t t13 = gm_f_same_package_vmps24r(t10, t11, t12);
            gm_l4 = t13;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)0ULL));
            if (!t15) {
                goto gm_lb4;
            }
            uint64_t t16 = gm_l2;
            uint64_t t17 = gm_l3;
            uint8_t t18 = ((t16 == t17));
            gm_l4 = t18;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_src6lm8esmv16p1, 182, 103);
    }
gm_lb1:;
    uint8_t t19 = gm_l4;
    F.ret = t19;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}

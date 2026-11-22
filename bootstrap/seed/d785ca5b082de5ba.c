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
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_t2lqu8jsj511d f0; uint8_t f1; uint8_t pe[7]; } gm_tc70a9dbbmgkg;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tc70a9dbbmgkg) == 24 && _Alignof(gm_tc70a9dbbmgkg) == 8 && offsetof(gm_tc70a9dbbmgkg, f0) == 0 && offsetof(gm_tc70a9dbbmgkg, f1) == 16, "layout of `?(u64, u64)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; int64_t v1; uint8_t v2; gm_trqfvmjtt4sij v3; void * v4; gm_b16_8 v5; } f1; } gm_tktr4rmbindgv;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; void * v1; } f1; } gm_trssmtuv8jr0v;
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
typedef struct { gm_b16_8 f0; gm_ttu09lij36f8c f1; gm_th2lqbo77pvsc f2; } gm_t03ccdabio1fj;
_Static_assert(sizeof(gm_tktr4rmbindgv) == 48 && _Alignof(gm_tktr4rmbindgv) == 8 && offsetof(gm_tktr4rmbindgv, f0) == 0 && offsetof(gm_tktr4rmbindgv, f1) == 8, "layout of `Value`");
_Static_assert(sizeof(gm_trssmtuv8jr0v) == 40 && _Alignof(gm_trssmtuv8jr0v) == 8 && offsetof(gm_trssmtuv8jr0v, f0) == 0 && offsetof(gm_trssmtuv8jr0v, f1) == 8, "layout of `TomlError!^Table`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_t03ccdabio1fj) == 80 && _Alignof(gm_t03ccdabio1fj) == 8 && offsetof(gm_t03ccdabio1fj, f0) == 0 && offsetof(gm_t03ccdabio1fj, f1) == 16 && offsetof(gm_t03ccdabio1fj, f2) == 56, "layout of `(url: string, reference: ?(key: string, value: string), subdir: ?string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t03ccdabio1fj v1; gm_tt008mgut8glb v2; } f1; } gm_t2jeshnsn30ep;
_Static_assert(sizeof(gm_t2jeshnsn30ep) == 88 && _Alignof(gm_t2jeshnsn30ep) == 8 && offsetof(gm_t2jeshnsn30ep, f0) == 0 && offsetof(gm_t2jeshnsn30ep, f1) == 8, "layout of `NewDep`");
struct gm_fr_add_dependency_jmjffrp {
    gm_frame h;
    gm_t2jeshnsn30ep l2; /* dep */
    gm_trqfvmjtt4sij l9; /* lines */
    gm_trqfvmjtt4sij l10; /* out */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_as_table_4f7n31g {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    void * ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_entry_lines_3jhl7a4 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_format_7plp021 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f1281l5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fqec6j2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_m89t2jt {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_rkrg0f2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_header_of_2u6atoc {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_inline_value_0impjcn {
    gm_frame h;
    gm_t2jeshnsn30ep l0; /* dep */
    gm_trqfvmjtt4sij l1; /* out */
    gm_tt008mgut8glb l3; /* u */
    gm_t03ccdabio1fj l6; /* g */
    gm_b16_8 ret;
};
struct gm_fr_is_blank_or_comment_lubpopi {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_header_uakq83c {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_without_ej3mkbe {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_b16_8 ret;
};
struct gm_fr_key_of_99iu4dv {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_normalize_newlines_183t7r2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pop_vr08sii {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
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
struct gm_fr_remove_dependency_4ju7b0h {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* lines */
    gm_trqfvmjtt4sij l6; /* keep */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_split_lines_jecth9m {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* lines */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_table_range_09bngqh {
    gm_frame h;
    gm_tc70a9dbbmgkg ret;
};
struct gm_fr_write_5jambfs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ctdpla1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_gle9guv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ivc38em {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_r83e4t6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_rbbkc88 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_add_dependency_jmjffrp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2);
inline __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint64_t gm_f_entry_lines_3jhl7a4(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_format_7plp021(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_f1281l5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fqec6j2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_m89t2jt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_rkrg0f2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_get_9rme0gb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_header_of_2u6atoc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_inline_value_0impjcn(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_blank_or_comment_lubpopi(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_header_uakq83c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_without_ej3mkbe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_key_of_99iu4dv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_newlines_183t7r2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_toml_gjtmf79(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) void gm_f_remove_dependency_4ju7b0h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_replace_alloc_t7hndlq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_split_lines_jecth9m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_table_range_09bngqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_toml_string_cagbrmp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_ascii_c9j36d1(gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2venbiq(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5jambfs(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ctdpla1(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gle9guv(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ivc38em(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_r83e4t6(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rbbkc88(void * a0, gm_tnpifv20irnph a1);
extern const uint8_t gm_s1o7nhldolqsg[10];
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s5m9qimbv7e8k[3];
extern const uint8_t gm_s5q2tpl7krt8a[9];
extern const uint8_t gm_s6bbu24cfq0ok[14];
extern const uint8_t gm_s7mfvroqmll1f[3];
extern const uint8_t gm_s9nk06pmq8hl1[34];
extern const uint8_t gm_sa7refaf4ue56[45];
extern const uint8_t gm_sajfarek3i4jp[44];
extern const uint8_t gm_scs28q27cdacb[10];
extern const uint8_t gm_seccpktgmrevt[2];
extern const uint8_t gm_sf4838dfeuobc[14];
extern const uint8_t gm_sh0j2ggs1vvoa[2];
extern const uint8_t gm_shv5ofgc65v68[3];
extern const uint8_t gm_sk754mj3p1el6[2];
extern const uint8_t gm_skmt2kt2p7v0u[3];
extern const uint8_t gm_skof48dte1s1o[13];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_snomh93h7tumv[14];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_soechff4t3q96[9];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3nt8pf0qk5gn[];
extern const char gm_srclt70oog8j9io[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srl8r23tls393[15];
extern const uint8_t gm_su4op6lvvdiua[16];
extern const uint8_t gm_sv9j89ufns35b[16];
extern const uint8_t gm_svaaloo7n72id[9];
extern const gm_tmpl gm_tpl01fkj9ukhaa3;
extern const gm_tmpl gm_tpl0lbbjeb2vhti;
extern const gm_tmpl gm_tpl0qlvf3bfgtip;
extern const gm_tmpl gm_tpl7avuc618ph2c;
extern const gm_tmpl gm_tpl7kp6007qjdtb;
extern const gm_tmpl gm_tplcpa735d0vjnu;
extern const gm_tmpl gm_tplmpc7ghuokash;
extern const gm_tmpl gm_tplo8vr0jo68llo;
extern const gm_tmpl gm_tplopmhgmch9jsj;
extern const gm_tmpl gm_tplpav1vs8t9nu9;
extern const gm_tmpl gm_tplugrpoiqe5r59;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_as_table_4f7n31g F;
    void * gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 58u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    void * gm_l2;
    {
        {
            uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)4ULL));
            if (!t6) {
                goto gm_lb2;
            }
            void * t7 = GM_LD(void *, (((uint8_t *)a0) + 8));
            gm_l1 = t7;
            void * t8 = gm_l1;
            gm_l2 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l2 = ((void *)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src3nt8pf0qk5gn, 56, 53);
    }
gm_lb1:;
    void * t9 = gm_l2;
    F.ret = t9;
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
inline __attribute__((aligned(4))) void gm_f_format_7plp021(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7plp021 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su4op6lvvdiua));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplugrpoiqe5r59), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_f1281l5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_f1281l5 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_srl8r23tls393));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7kp6007qjdtb), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fqec6j2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_fqec6j2 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svaaloo7n72id));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl0qlvf3bfgtip), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_m89t2jt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_m89t2jt F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa7refaf4ue56));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7avuc618ph2c), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_rkrg0f2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_rkrg0f2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9nk06pmq8hl1));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl01fkj9ukhaa3), t3);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_blank_or_comment_lubpopi(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_blank_or_comment_lubpopi F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 110u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_trim_ascii_c9j36d1(t2);
    gm_l1 = t3;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
    if (t5) {
        gm_l2 = ((uint8_t)1);
    } else {
        gm_b16_8 t6 = gm_l1;
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_seccpktgmrevt));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
        uint8_t t8 = gm_f_starts_with_6vvt01e(t6, t7);
        gm_l2 = t8;
    }
    uint8_t t9 = gm_l2;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_header_uakq83c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_header_uakq83c F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_trim_ascii_c9j36d1(t2);
    gm_l1 = t3;
    gm_b16_8 t4 = gm_l1;
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sk754mj3p1el6));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(1));
    uint8_t t6 = gm_f_starts_with_6vvt01e(t4, t5);
    F.ret = t6;
    goto gm_fret;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_vr08sii(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_vr08sii F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_b16_8 gm_l2; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l1 = t3;
    uint64_t t4 = gm_l1;
    uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
    if (t5) {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t6) + 8));
    uint64_t t9 = gm_l1;
    uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
    if (o11) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t12 = r10;
    if ((uintptr_t)t12 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t12, t8, gm_srcrgc9bkquf6nr, 133, 10);
    gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t7) + (uintptr_t)t12 * 16));
    gm_l2 = t13;
    void * t14 = gm_l0;
    void * t15 = gm_l0;
    void * t16 = GM_LD(void *, ((uint8_t *)t15));
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t15) + 8));
    uint64_t t18 = gm_l1;
    uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
    if (o20) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t21 = r19;
    uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t23 = (uintptr_t)t21;
    if (t22 > t23 || t23 > (uintptr_t)t17) gm_trap_slice(t22, t23, t17, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t24 = ((uintptr_t)t21) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t16 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    GM_ST(gm_b16_8, ((uint8_t *)t14), t25);
    gm_b16_8 t26 = gm_l2;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t26);
    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
    goto gm_fdone;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5jambfs(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_5jambfs F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sf4838dfeuobc));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(13));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplpav1vs8t9nu9), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ctdpla1(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ctdpla1 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_soechff4t3q96));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(8));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplo8vr0jo68llo), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_gle9guv(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_gle9guv F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scs28q27cdacb));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tplmpc7ghuokash), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ivc38em(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ivc38em F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s5q2tpl7krt8a));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(8));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplopmhgmch9jsj), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_r83e4t6(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_r83e4t6 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s1o7nhldolqsg));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(9));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl0lbbjeb2vhti), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rbbkc88(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_rbbkc88 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s6bbu24cfq0ok));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(13));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplcpa735d0vjnu), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_add_dependency_jmjffrp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2) {
    struct gm_fr_add_dependency_jmjffrp F;
    gm_b16_8 gm_l0; /* original */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l3; /* text */
    gm_tob1snuii5bme gm_l4; /* e */
    void * gm_l5; /* doc */
    void * gm_l6; /* d */
    void * gm_l7; /* t */
    gm_b16_8 gm_l8; /* entry */
    gm_t2lqu8jsj511d gm_l11; /* r */
    uint64_t gm_l12; /* last */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_b16_8 gm_l15; /* l */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l18; /* l */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    gm_b16_8 gm_l22; /* l */
    gm_b16_8 gm_l23; /* h */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_b16_8 gm_l26; /* x */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_b16_8 gm_l29; /* x */
    GM_STACK_CHECK(&F, 2594u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_trssmtuv8jr0v gm_l30;
    gm_tabb710d9mq2e gm_l31;
    void * gm_l32;
    void * gm_l33;
    uint8_t gm_l34;
    void * gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_tabb710d9mq2e gm_l37;
    gm_tc70a9dbbmgkg gm_l38;
    gm_tftb1odc739t5 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tf1bjg81u0glv gm_l43;
    gm_th2lqbo77pvsc gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_tftb1odc739t5 gm_l46;
    gm_tftb1odc739t5 gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tftb1odc739t5 gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_tftb1odc739t5 gm_l54;
    {
        gm_b16_8 t43 = gm_l0;
        gm_b16_8 t44 = gm_f_normalize_newlines_183t7r2((void *)NULL, cx, NULL, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t44;
        gm_b16_8 t45 = gm_l3;
        gm_f_parse_toml_gjtmf79((void *)&gm_l30, (void *)NULL, cx, NULL, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
        if (t46) {
            void * t47 = GM_LD(void *, (((uint8_t *)&gm_l30) + 8));
            gm_l5 = t47;
        } else {
            {
                memmove(((uint8_t *)&gm_l4), (((uint8_t *)&gm_l30) + 8), 32);
            }
            {
                gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                gm_tnpifv20irnph t49;
                GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
                gm_f_format_f1281l5((void *)&gm_l31, (void *)NULL, cx, NULL, t49);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
            if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srclt70oog8j9io, 138, 54);
            {
                gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t52);
                goto gm_fdone;
            }
        }
        {
            {
                void * t53 = gm_l5;
                gm_b16_8 t54;
                GM_ST(void *, (uint8_t *)&t54, (void *)(gm_skof48dte1s1o));
                GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(12));
                void * t55 = gm_f_get_9rme0gb(t53, t54);
                gm_l32 = t55;
                uint8_t t56 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l32)) != 0);
                if (!t56) {
                    goto gm_lb2;
                }
                void * t57 = GM_LD(void *, ((uint8_t *)&gm_l32));
                gm_l6 = t57;
                void * t58 = gm_l6;
                void * t59 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t58) + 16)));
                gm_l33 = t59;
                uint8_t t60 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l33)) != 0);
                if (t60) {
                    void * t61 = GM_LD(void *, ((uint8_t *)&gm_l33));
                    gm_l7 = t61;
                } else {
                    gm_b16_8 t62;
                    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sajfarek3i4jp));
                    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(43));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t62);
                    goto gm_fdone;
                }
                void * t63 = gm_l7;
                gm_b16_8 t64 = gm_l1;
                void * t65 = gm_f_get_9rme0gb(t63, t64);
                gm_l35 = t65;
                {
                    {
                        uint8_t t66 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l35)) != 0);
                        if (t66) {
                            goto gm_lb4;
                        }
                        gm_l34 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        gm_l34 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 141, 12);
                }
gm_lb3:;
                uint8_t t67 = gm_l34;
                if (t67) {
                    {
                        gm_b16_8 t68 = gm_l1;
                        gm_tnpifv20irnph t69;
                        GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t68);
                        gm_f_format_m89t2jt((void *)&gm_l36, (void *)NULL, cx, NULL, t69);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                    if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_srclt70oog8j9io, 141, 46);
                    {
                        gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                        gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t72);
                        goto gm_fdone;
                    }
                }
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        gm_b16_8 t73 = gm_l1;
        gm_b16_8 t74 = gm_f_inline_value_0impjcn((void *)NULL, cx, NULL, &(*(gm_t2jeshnsn30ep *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t75;
        GM_ST(gm_b16_8, ((uint8_t *)&t75 + 0), t73);
        GM_ST(gm_b16_8, ((uint8_t *)&t75 + 16), t74);
        gm_f_format_fqec6j2((void *)&gm_l37, (void *)NULL, cx, NULL, &t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
        if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_srclt70oog8j9io, 143, 14);
        gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
        gm_l8 = t77;
        gm_b16_8 t78 = gm_l3;
        gm_f_split_lines_jecth9m((void *)&F.l9, (void *)NULL, cx, NULL, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t79;
        gm_f_list_uvfu1k1((void *)&t79, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t79;
        {
            {
                {
                    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                    gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
                    gm_b16_8 t82;
                    GM_ST(void *, (uint8_t *)&t82, (void *)(gm_skof48dte1s1o));
                    GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(12));
                    gm_f_table_range_09bngqh((void *)&gm_l38, (void *)NULL, cx, NULL, t81, t82);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t83 = (uint8_t)((((uint8_t *)&gm_l38))[16] != 0);
                if (!t83) {
                    goto gm_lb7;
                }
                gm_t2lqu8jsj511d t84 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)&gm_l38));
                gm_l11 = t84;
                uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                gm_l12 = t85;
                {
                    for (;;) {
                        uint64_t t86 = gm_l12;
                        uint64_t t87 = GM_LD(uint64_t, ((uint8_t *)&gm_l11));
                        uint64_t r88; bool o89 = __builtin_add_overflow(t87, ((uint64_t)1ULL), &r88);
                        if (o89) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 149, 22);
                        uint64_t t90 = r88;
                        uint8_t t91 = (t86 > t90);
                        if (!t91) {
                            goto gm_lb8;
                        }
                        void * t92 = GM_LD(void *, ((uint8_t *)&F.l9));
                        uint64_t t93 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                        uint64_t t94 = gm_l12;
                        uint64_t r95; bool o96 = __builtin_sub_overflow(t94, ((uint64_t)1ULL), &r95);
                        if (o96) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 149, 71);
                        uint64_t t97 = r95;
                        if ((uintptr_t)t97 >= (uintptr_t)(t93)) gm_trap_index((uintptr_t)t97, t93, gm_srclt70oog8j9io, 149, 59);
                        gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)t92) + (uintptr_t)t97 * 16));
                        uint8_t t99 = gm_f_is_blank_or_comment_lubpopi(t98);
                        if (!t99) {
                            goto gm_lb8;
                        }
                        {
                            uint64_t t100 = gm_l12;
                            uint64_t r101; bool o102 = __builtin_sub_overflow(t100, ((uint64_t)1ULL), &r101);
                            if (o102) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 149, 84);
                            uint64_t t103 = r101;
                            gm_l12 = t103;
                        }
gm_lb10:;
                    }
                }
gm_lb8:;
                void * t104 = GM_LD(void *, ((uint8_t *)&F.l9));
                uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                uint64_t t106 = gm_l12;
                uint64_t t107 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t108 = (uintptr_t)t106;
                if (t107 > t108 || t108 > (uintptr_t)t105) gm_trap_slice(t107, t108, t105, gm_srclt70oog8j9io, 150, 18);
                uint64_t t109 = ((uintptr_t)t106) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t110;
                GM_ST(void *, (uint8_t *)&t110, (void *)(((uint8_t *)t104 + (uintptr_t)((uint64_t)0ULL) * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(t109));
                gm_l13 = t110;
                gm_l14 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t111 = gm_l14;
                        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                        uint8_t t113 = (t111 < t112);
                        if (!t113) {
                            goto gm_lb11;
                        }
                        {
                            void * t114 = GM_LD(void *, ((uint8_t *)&gm_l13));
                            uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                            uint64_t t116 = gm_l14;
                            {
                                {
                                    gm_b16_8 t117 = GM_LD(gm_b16_8, (((uint8_t *)t114) + (uintptr_t)t116 * 16));
                                    gm_l15 = t117;
                                    void * t118 = (void *)((uint8_t *)&F.l10);
                                    gm_b16_8 t119 = gm_l15;
                                    gm_tftb1odc739t5 t120 = gm_f_extend_0cs85gs(t118, t119);
                                    gm_l39 = t120;
                                    uint8_t t121 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                                    if (!t121) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_srclt70oog8j9io, 150, 41);
                                    goto gm_lb14;
                                }
gm_lb15:;
                                gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 150, 9);
                            }
gm_lb14:;
                        }
gm_lb13:;
                        uint64_t t122 = gm_l14;
                        uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                        uint64_t t125 = r123;
                        gm_l14 = t125;
                    }
                }
gm_lb11:;
                {
                    {
                        uint64_t t126 = gm_l12;
                        uint8_t t127 = (t126 > ((uint64_t)0ULL));
                        if (!t127) {
                            goto gm_lb17;
                        }
                        void * t128 = GM_LD(void *, ((uint8_t *)&F.l9));
                        uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                        uint64_t t130 = gm_l12;
                        uint64_t r131; bool o132 = __builtin_sub_overflow(t130, ((uint64_t)1ULL), &r131);
                        if (o132) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 151, 41);
                        uint64_t t133 = r131;
                        if ((uintptr_t)t133 >= (uintptr_t)(t129)) gm_trap_index((uintptr_t)t133, t129, gm_srclt70oog8j9io, 151, 29);
                        gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)t128) + (uintptr_t)t133 * 16));
                        gm_b16_8 t135;
                        GM_ST(void *, (uint8_t *)&t135, (void *)(gm_so514oj5nbhcu));
                        GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(1));
                        uint8_t t136 = gm_f_ends_with_biriu5l(t134, t135);
                        uint8_t t137 = (uint8_t)!t136;
                        if (!t137) {
                            goto gm_lb17;
                        }
                        void * t138 = (void *)((uint8_t *)&F.l10);
                        gm_tftb1odc739t5 t139 = gm_f_push_t83rg2m(t138, ((uint8_t)10ULL));
                        gm_l40 = t139;
                        uint8_t t140 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                        if (!t140) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srclt70oog8j9io, 151, 69);
                        goto gm_lb16;
                    }
gm_lb17:;
                }
gm_lb16:;
                void * t141 = (void *)((uint8_t *)&F.l10);
                gm_b16_8 t142 = gm_l8;
                gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
                gm_tftb1odc739t5 t144 = gm_f_extend_0cs85gs(t141, t143);
                gm_l41 = t144;
                uint8_t t145 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                if (!t145) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srclt70oog8j9io, 152, 9);
                void * t146 = GM_LD(void *, ((uint8_t *)&F.l9));
                uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                uint64_t t148 = gm_l12;
                uint64_t t149 = (uintptr_t)t148;
                uint64_t t150 = (uintptr_t)t147;
                if (t149 > t150 || t150 > (uintptr_t)t147) gm_trap_slice(t149, t150, t147, gm_srclt70oog8j9io, 153, 18);
                uint64_t t151 = ((uintptr_t)t147) - (uintptr_t)t148;
                gm_b16_8 t152;
                GM_ST(void *, (uint8_t *)&t152, (void *)(((uint8_t *)t146 + (uintptr_t)t148 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(t151));
                gm_l16 = t152;
                gm_l17 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t153 = gm_l17;
                        uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                        uint8_t t155 = (t153 < t154);
                        if (!t155) {
                            goto gm_lb18;
                        }
                        {
                            void * t156 = GM_LD(void *, ((uint8_t *)&gm_l16));
                            uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                            uint64_t t158 = gm_l17;
                            {
                                {
                                    gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)t156) + (uintptr_t)t158 * 16));
                                    gm_l18 = t159;
                                    void * t160 = (void *)((uint8_t *)&F.l10);
                                    gm_b16_8 t161 = gm_l18;
                                    gm_tftb1odc739t5 t162 = gm_f_extend_0cs85gs(t160, t161);
                                    gm_l42 = t162;
                                    uint8_t t163 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                                    if (!t163) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srclt70oog8j9io, 153, 40);
                                    goto gm_lb21;
                                }
gm_lb22:;
                                gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 153, 9);
                            }
gm_lb21:;
                        }
gm_lb20:;
                        uint64_t t164 = gm_l17;
                        uint64_t r165; bool o166 = __builtin_add_overflow(t164, ((uint64_t)1ULL), &r165);
                        uint64_t t167 = r165;
                        gm_l17 = t167;
                    }
                }
gm_lb18:;
                {
                    gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                    gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t169);
                    goto gm_fdone;
                }
            }
gm_lb7:;
        }
gm_lb6:;
    }
    {
        gm_b16_8 t170 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
        gm_b16_8 t171 = GM_LD(gm_b16_8, ((uint8_t *)&t170));
        gm_l19 = t171;
        gm_l20 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t172 = gm_l20;
                uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                uint8_t t174 = (t172 < t173);
                if (!t174) {
                    goto gm_lb23;
                }
                {
                    uint64_t t175 = gm_l20;
                    void * t176 = GM_LD(void *, ((uint8_t *)&gm_l19));
                    uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                    uint64_t t178 = gm_l20;
                    gm_b16_8 t179 = GM_LD(gm_b16_8, (((uint8_t *)t176) + (uintptr_t)t178 * 16));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l43 + 0), t175);
                    GM_ST(gm_b16_8, ((uint8_t *)&gm_l43 + 8), t179);
                    {
                        {
                            uint64_t t180 = GM_LD(uint64_t, ((uint8_t *)&gm_l43));
                            gm_l21 = t180;
                            gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
                            gm_l22 = t181;
                            {
                                {
                                    gm_b16_8 t182 = gm_l22;
                                    gm_f_header_of_2u6atoc((void *)&gm_l44, (void *)NULL, cx, NULL, t182);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t183 = (uint8_t)((((uint8_t *)&gm_l44))[16] != 0);
                                    if (!t183) {
                                        goto gm_lb29;
                                    }
                                    gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l44));
                                    gm_l23 = t184;
                                    gm_b16_8 t185 = gm_l23;
                                    gm_b16_8 t186;
                                    GM_ST(void *, (uint8_t *)&t186, (void *)(gm_snomh93h7tumv));
                                    GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(13));
                                    uint8_t t187 = gm_f_starts_with_6vvt01e(t185, t186);
                                    if (t187) {
                                        void * t188 = GM_LD(void *, ((uint8_t *)&F.l9));
                                        uint64_t t189 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                        uint64_t t190 = gm_l21;
                                        uint64_t t191 = (uintptr_t)((uint64_t)0ULL);
                                        uint64_t t192 = (uintptr_t)t190;
                                        if (t191 > t192 || t192 > (uintptr_t)t189) gm_trap_slice(t191, t192, t189, gm_srclt70oog8j9io, 160, 26);
                                        uint64_t t193 = ((uintptr_t)t190) - (uintptr_t)((uint64_t)0ULL);
                                        gm_b16_8 t194;
                                        GM_ST(void *, (uint8_t *)&t194, (void *)(((uint8_t *)t188 + (uintptr_t)((uint64_t)0ULL) * 16)));
                                        GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(t193));
                                        gm_l24 = t194;
                                        gm_l25 = ((uint64_t)0ULL);
                                        {
                                            for (;;) {
                                                uint64_t t195 = gm_l25;
                                                uint64_t t196 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                uint8_t t197 = (t195 < t196);
                                                if (!t197) {
                                                    goto gm_lb30;
                                                }
                                                {
                                                    void * t198 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                                    uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                    uint64_t t200 = gm_l25;
                                                    {
                                                        {
                                                            gm_b16_8 t201 = GM_LD(gm_b16_8, (((uint8_t *)t198) + (uintptr_t)t200 * 16));
                                                            gm_l26 = t201;
                                                            void * t202 = (void *)((uint8_t *)&F.l10);
                                                            gm_b16_8 t203 = gm_l26;
                                                            gm_tftb1odc739t5 t204 = gm_f_extend_0cs85gs(t202, t203);
                                                            gm_l45 = t204;
                                                            uint8_t t205 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                                            if (!t205) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srclt70oog8j9io, 160, 46);
                                                            goto gm_lb33;
                                                        }
gm_lb34:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 160, 17);
                                                    }
gm_lb33:;
                                                }
gm_lb32:;
                                                uint64_t t206 = gm_l25;
                                                uint64_t r207; bool o208 = __builtin_add_overflow(t206, ((uint64_t)1ULL), &r207);
                                                uint64_t t209 = r207;
                                                gm_l25 = t209;
                                            }
                                        }
gm_lb30:;
                                        {
                                            {
                                                uint64_t t210 = gm_l21;
                                                uint8_t t211 = (t210 > ((uint64_t)0ULL));
                                                if (!t211) {
                                                    goto gm_lb36;
                                                }
                                                void * t212 = GM_LD(void *, ((uint8_t *)&F.l9));
                                                uint64_t t213 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                                uint64_t t214 = gm_l21;
                                                uint64_t r215; bool o216 = __builtin_sub_overflow(t214, ((uint64_t)1ULL), &r215);
                                                if (o216) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 161, 66);
                                                uint64_t t217 = r215;
                                                if ((uintptr_t)t217 >= (uintptr_t)(t213)) gm_trap_index((uintptr_t)t217, t213, gm_srclt70oog8j9io, 161, 54);
                                                gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)t212) + (uintptr_t)t217 * 16));
                                                uint8_t t219 = gm_f_is_blank_or_comment_lubpopi(t218);
                                                uint8_t t220 = (uint8_t)!t219;
                                                if (!t220) {
                                                    goto gm_lb36;
                                                }
                                                void * t221 = (void *)((uint8_t *)&F.l10);
                                                gm_tftb1odc739t5 t222 = gm_f_push_t83rg2m(t221, ((uint8_t)10ULL));
                                                gm_l46 = t222;
                                                uint8_t t223 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                                if (!t223) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_srclt70oog8j9io, 161, 76);
                                                goto gm_lb35;
                                            }
gm_lb36:;
                                        }
gm_lb35:;
                                        void * t224 = (void *)((uint8_t *)&F.l10);
                                        gm_b16_8 t225;
                                        GM_ST(void *, (uint8_t *)&t225, (void *)(gm_sv9j89ufns35b));
                                        GM_ST(uintptr_t, (uint8_t *)&t225 + 8, (uintptr_t)(15));
                                        gm_tftb1odc739t5 t226 = gm_f_extend_0cs85gs(t224, t225);
                                        gm_l47 = t226;
                                        uint8_t t227 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                                        if (!t227) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_srclt70oog8j9io, 162, 17);
                                        void * t228 = (void *)((uint8_t *)&F.l10);
                                        gm_b16_8 t229 = gm_l8;
                                        gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&t229));
                                        gm_tftb1odc739t5 t231 = gm_f_extend_0cs85gs(t228, t230);
                                        gm_l48 = t231;
                                        uint8_t t232 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                        if (!t232) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_srclt70oog8j9io, 163, 17);
                                        void * t233 = GM_LD(void *, ((uint8_t *)&F.l9));
                                        uint64_t t234 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                        uint64_t t235 = gm_l21;
                                        uint64_t t236 = (uintptr_t)t235;
                                        uint64_t t237 = (uintptr_t)t234;
                                        if (t236 > t237 || t237 > (uintptr_t)t234) gm_trap_slice(t236, t237, t234, gm_srclt70oog8j9io, 164, 26);
                                        uint64_t t238 = ((uintptr_t)t234) - (uintptr_t)t235;
                                        gm_b16_8 t239;
                                        GM_ST(void *, (uint8_t *)&t239, (void *)(((uint8_t *)t233 + (uintptr_t)t235 * 16)));
                                        GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(t238));
                                        gm_l27 = t239;
                                        gm_l28 = ((uint64_t)0ULL);
                                        {
                                            for (;;) {
                                                uint64_t t240 = gm_l28;
                                                uint64_t t241 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                                                uint8_t t242 = (t240 < t241);
                                                if (!t242) {
                                                    goto gm_lb37;
                                                }
                                                {
                                                    void * t243 = GM_LD(void *, ((uint8_t *)&gm_l27));
                                                    uint64_t t244 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                                                    uint64_t t245 = gm_l28;
                                                    {
                                                        {
                                                            gm_b16_8 t246 = GM_LD(gm_b16_8, (((uint8_t *)t243) + (uintptr_t)t245 * 16));
                                                            gm_l29 = t246;
                                                            void * t247 = (void *)((uint8_t *)&F.l10);
                                                            gm_b16_8 t248 = gm_l29;
                                                            gm_tftb1odc739t5 t249 = gm_f_extend_0cs85gs(t247, t248);
                                                            gm_l49 = t249;
                                                            uint8_t t250 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                                            if (!t250) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srclt70oog8j9io, 164, 45);
                                                            goto gm_lb40;
                                                        }
gm_lb41:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 164, 17);
                                                    }
gm_lb40:;
                                                }
gm_lb39:;
                                                uint64_t t251 = gm_l28;
                                                uint64_t r252; bool o253 = __builtin_add_overflow(t251, ((uint64_t)1ULL), &r252);
                                                uint64_t t254 = r252;
                                                gm_l28 = t254;
                                            }
                                        }
gm_lb37:;
                                        gm_b16_8 t255 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                        gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&t255));
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t256);
                                        goto gm_fdone;
                                    }
                                    goto gm_lb28;
                                }
gm_lb29:;
                            }
gm_lb28:;
                            goto gm_lb26;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 157, 5);
                    }
gm_lb26:;
                }
gm_lb25:;
                uint64_t t257 = gm_l20;
                uint64_t r258; bool o259 = __builtin_add_overflow(t257, ((uint64_t)1ULL), &r258);
                uint64_t t260 = r258;
                gm_l20 = t260;
            }
        }
gm_lb23:;
    }
    {
        void * t261 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t262 = gm_l3;
        gm_tftb1odc739t5 t263 = gm_f_extend_0cs85gs(t261, t262);
        gm_l50 = t263;
        uint8_t t264 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
        if (!t264) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srclt70oog8j9io, 169, 5);
        uint64_t t265 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t266 = (t265 > ((uint64_t)0ULL));
        if (t266) {
            gm_b16_8 t267 = gm_l3;
            gm_b16_8 t268;
            GM_ST(void *, (uint8_t *)&t268, (void *)(gm_so514oj5nbhcu));
            GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(1));
            uint8_t t269 = gm_f_ends_with_biriu5l(t267, t268);
            uint8_t t270 = (uint8_t)!t269;
            if (t270) {
                void * t271 = (void *)((uint8_t *)&F.l10);
                gm_tftb1odc739t5 t272 = gm_f_push_t83rg2m(t271, ((uint8_t)10ULL));
                gm_l51 = t272;
                uint8_t t273 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                if (!t273) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srclt70oog8j9io, 171, 39);
            }
            void * t274 = (void *)((uint8_t *)&F.l10);
            gm_tftb1odc739t5 t275 = gm_f_push_t83rg2m(t274, ((uint8_t)10ULL));
            gm_l52 = t275;
            uint8_t t276 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
            if (!t276) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_srclt70oog8j9io, 172, 9);
        }
        void * t277 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t278;
        GM_ST(void *, (uint8_t *)&t278, (void *)(gm_sv9j89ufns35b));
        GM_ST(uintptr_t, (uint8_t *)&t278 + 8, (uintptr_t)(15));
        gm_tftb1odc739t5 t279 = gm_f_extend_0cs85gs(t277, t278);
        gm_l53 = t279;
        uint8_t t280 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
        if (!t280) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_srclt70oog8j9io, 174, 5);
        void * t281 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t282 = gm_l8;
        gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&t282));
        gm_tftb1odc739t5 t284 = gm_f_extend_0cs85gs(t281, t283);
        gm_l54 = t284;
        uint8_t t285 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
        if (!t285) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_srclt70oog8j9io, 175, 5);
        gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
        gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&t286));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t287);
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
__attribute__((aligned(4))) uint64_t gm_f_entry_lines_3jhl7a4(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_entry_lines_3jhl7a4 F;
    gm_b16_8 gm_l0; /* lines */
    uint64_t gm_l1; /* at */
    int32_t gm_l2; /* depth */
    uint8_t gm_l3; /* quote */
    uint64_t gm_l4; /* n */
    uint64_t gm_l5; /* i */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 294u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l9;
    gm_l2 = ((int32_t)0LL);
    gm_l3 = ((uint8_t)0ULL);
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t10 = gm_l1;
    gm_l5 = t10;
    {
        for (;;) {
            uint64_t t11 = gm_l5;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t13 = (t11 < t12);
            if (!t13) {
                goto gm_lb1;
            }
            {
                void * t14 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t16 = gm_l5;
                gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t14) + (uintptr_t)t16 * 16));
                gm_l6 = t17;
                gm_l7 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t18 = gm_l7;
                        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint8_t t20 = (t18 < t19);
                        if (!t20) {
                            goto gm_lb4;
                        }
                        {
                            void * t21 = GM_LD(void *, ((uint8_t *)&gm_l6));
                            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint64_t t23 = gm_l7;
                            {
                                {
                                    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t21) + (uintptr_t)t23 * 1));
                                    gm_l8 = t24;
                                    uint8_t t25 = gm_l3;
                                    uint8_t t26 = (!(t25 == ((uint8_t)0ULL)));
                                    if (t26) {
                                        uint8_t t27 = gm_l8;
                                        uint8_t t28 = gm_l3;
                                        uint8_t t29 = ((t27 == t28));
                                        if (t29) {
                                            gm_l3 = ((uint8_t)0ULL);
                                        }
                                    } else {
                                        uint8_t t30 = gm_l8;
                                        uint8_t t31 = ((t30 == ((uint8_t)34ULL)));
                                        if (t31) {
                                            gm_l9 = ((uint8_t)1);
                                        } else {
                                            uint8_t t32 = gm_l8;
                                            uint8_t t33 = ((t32 == ((uint8_t)39ULL)));
                                            gm_l9 = t33;
                                        }
                                        uint8_t t34 = gm_l9;
                                        if (t34) {
                                            uint8_t t35 = gm_l8;
                                            gm_l3 = t35;
                                        } else {
                                            uint8_t t36 = gm_l8;
                                            uint8_t t37 = ((t36 == ((uint8_t)35ULL)));
                                            if (t37) {
                                                goto gm_lb4;
                                            } else {
                                                uint8_t t38 = gm_l8;
                                                gm_tqs5p4n2tq19v t39;
                                                GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)91ULL));
                                                GM_ST(uint8_t, ((uint8_t *)&t39 + 1), ((uint8_t)123ULL));
                                                uint8_t t40 = 0;
                                                if (gm_eqjmccqdb90f(((uint8_t *)&t39) + 0, ((uint8_t *)&t38))) t40 = 1;
                                                if (gm_eqjmccqdb90f(((uint8_t *)&t39) + 1, ((uint8_t *)&t38))) t40 = 1;
                                                uint8_t t41 = (uint8_t)(t40);
                                                if (t41) {
                                                    int32_t t42 = gm_l2;
                                                    int32_t r43; bool o44 = __builtin_add_overflow(t42, ((int32_t)1LL), &r43);
                                                    if (o44) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 87, 17);
                                                    int32_t t45 = r43;
                                                    gm_l2 = t45;
                                                } else {
                                                    uint8_t t46 = gm_l8;
                                                    gm_tqs5p4n2tq19v t47;
                                                    GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)93ULL));
                                                    GM_ST(uint8_t, ((uint8_t *)&t47 + 1), ((uint8_t)125ULL));
                                                    uint8_t t48 = 0;
                                                    if (gm_eqjmccqdb90f(((uint8_t *)&t47) + 0, ((uint8_t *)&t46))) t48 = 1;
                                                    if (gm_eqjmccqdb90f(((uint8_t *)&t47) + 1, ((uint8_t *)&t46))) t48 = 1;
                                                    uint8_t t49 = (uint8_t)(t48);
                                                    if (t49) {
                                                        int32_t t50 = gm_l2;
                                                        int32_t r51; bool o52 = __builtin_sub_overflow(t50, ((int32_t)1LL), &r51);
                                                        if (o52) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 89, 17);
                                                        int32_t t53 = r51;
                                                        gm_l2 = t53;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    goto gm_lb7;
                                }
gm_lb8:;
                                gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 79, 9);
                            }
gm_lb7:;
                        }
gm_lb6:;
                        uint64_t t54 = gm_l7;
                        uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
                        uint64_t t57 = r55;
                        gm_l7 = t57;
                    }
                }
gm_lb4:;
                gm_l3 = ((uint8_t)0ULL);
                uint64_t t58 = gm_l4;
                uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                if (o60) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 93, 9);
                uint64_t t61 = r59;
                gm_l4 = t61;
                uint64_t t62 = gm_l5;
                uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                if (o64) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 94, 9);
                uint64_t t65 = r63;
                gm_l5 = t65;
                int32_t t66 = gm_l2;
                uint8_t t67 = (t66 <= ((int32_t)0LL));
                if (t67) {
                    goto gm_lb1;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t68 = gm_l4;
    F.ret = t68;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_header_of_2u6atoc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_header_of_2u6atoc F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* t */
    uint64_t gm_l2; /* end */
    gm_b16_8 gm_l3; /* inner */
    GM_STACK_CHECK_SHALLOW(&F, 496u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    gm_tf5srh4mp4b3i gm_l5;
    uint8_t gm_l6;
    gm_b16_8 t4 = gm_l0;
    gm_b16_8 t5 = gm_f_trim_ascii_c9j36d1(t4);
    gm_l1 = t5;
    gm_b16_8 t6 = gm_l1;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sk754mj3p1el6));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
    uint8_t t8 = gm_f_starts_with_6vvt01e(t6, t7);
    uint8_t t9 = (uint8_t)!t8;
    if (t9) {
        gm_l4 = ((uint8_t)1);
    } else {
        gm_b16_8 t10 = gm_l1;
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_shv5ofgc65v68));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(2));
        uint8_t t12 = gm_f_starts_with_6vvt01e(t10, t11);
        gm_l4 = t12;
    }
    uint8_t t13 = gm_l4;
    if (t13) {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    gm_b16_8 t14 = gm_l1;
    gm_b16_8 t15;
    GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s2053hhvf16fm));
    GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(1));
    gm_tf5srh4mp4b3i t16 = gm_f_find_nab29co(t14, t15);
    gm_l5 = t16;
    uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
    if (t17) {
        uint64_t t18 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
        gm_l2 = t18;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    void * t19 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t21 = gm_l2;
    uint64_t t22 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t23 = (uintptr_t)t21;
    if (t22 > t23 || t23 > (uintptr_t)t20) gm_trap_slice(t22, t23, t20, gm_srclt70oog8j9io, 40, 30);
    uint64_t t24 = ((uintptr_t)t21) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t19 + (uintptr_t)((uint64_t)1ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    gm_b16_8 t26 = gm_f_trim_ascii_c9j36d1(t25);
    gm_l3 = t26;
    {
        {
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t28 = (t27 >= ((uint64_t)2ULL));
            if (!t28) {
                goto gm_lb2;
            }
            void * t29 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t30, gm_srclt70oog8j9io, 41, 28);
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t29) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint8_t t32 = ((t31 == ((uint8_t)34ULL)));
            if (t32) {
                gm_l6 = ((uint8_t)1);
            } else {
                void * t33 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t34)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t34, gm_srclt70oog8j9io, 41, 47);
                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)t33) + (uintptr_t)((uint64_t)0ULL) * 1));
                uint8_t t36 = ((t35 == ((uint8_t)39ULL)));
                gm_l6 = t36;
            }
            uint8_t t37 = gm_l6;
            if (!t37) {
                goto gm_lb2;
            }
            void * t38 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t r41; bool o42 = __builtin_sub_overflow(t40, ((uint64_t)1ULL), &r41);
            if (o42) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 41, 75);
            uint64_t t43 = r41;
            if ((uintptr_t)t43 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t43, t39, gm_srclt70oog8j9io, 41, 69);
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)t38) + (uintptr_t)t43 * 1));
            void * t45 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t46, gm_srclt70oog8j9io, 41, 93);
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)t45) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint8_t t48 = ((t44 == t47));
            if (!t48) {
                goto gm_lb2;
            }
            void * t49 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t r52; bool o53 = __builtin_sub_overflow(t51, ((uint64_t)1ULL), &r52);
            if (o53) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 42, 25);
            uint64_t t54 = r52;
            uint64_t t55 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t56 = (uintptr_t)t54;
            if (t55 > t56 || t56 > (uintptr_t)t50) gm_trap_slice(t55, t56, t50, gm_srclt70oog8j9io, 42, 16);
            uint64_t t57 = ((uintptr_t)t54) - (uintptr_t)((uint64_t)1ULL);
            gm_b16_8 t58;
            GM_ST(void *, (uint8_t *)&t58, (void *)(((uint8_t *)t49 + (uintptr_t)((uint64_t)1ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(t57));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t58);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_b16_8 t59 = gm_l3;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t59);
    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_inline_value_0impjcn(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_inline_value_0impjcn F;
    gm_b16_8 gm_l2; /* p */
    gm_b16_8 gm_l4; /* h */
    gm_b16_8 gm_l5; /* s */
    gm_t6j6smj7qoctu gm_l7; /* r */
    gm_b16_8 gm_l8; /* s */
    GM_STACK_CHECK(&F, 791u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_trqfvmjtt4sij t14;
    gm_f_list_uvfu1k1((void *)&t14, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t14;
    void * t15 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s7mfvroqmll1f));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(2));
    gm_tftb1odc739t5 t17 = gm_f_extend_0cs85gs(t15, t16);
    gm_l9 = t17;
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srclt70oog8j9io, 117, 5);
    {
        {
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)0ULL));
            if (!t20) {
                goto gm_lb2;
            }
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l2 = t21;
            void * t22 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t23 = gm_l2;
            gm_b16_8 t24 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t25;
            GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
            gm_tftb1odc739t5 t26 = gm_f_write_r83e4t6(t22, t25);
            gm_l10 = t26;
            uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srclt70oog8j9io, 119, 23);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)2ULL));
            if (!t29) {
                goto gm_lb3;
            }
            memmove(((uint8_t *)&F.l3), (((uint8_t *)a0) + 8), 64);
            void * t30 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
            gm_b16_8 t32 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t33;
            GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t32);
            gm_tftb1odc739t5 t34 = gm_f_write_ctdpla1(t30, t33);
            gm_l11 = t34;
            uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
            if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srclt70oog8j9io, 121, 13);
            {
                {
                    uint8_t t36 = (uint8_t)(((((uint8_t *)&F.l3) + 16))[16] != 0);
                    if (!t36) {
                        goto gm_lb5;
                    }
                    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
                    gm_l4 = t37;
                    void * t38 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t39 = gm_l4;
                    gm_b16_8 t40 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t39);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t41;
                    GM_ST(gm_b16_8, ((uint8_t *)&t41 + 0), t40);
                    gm_tftb1odc739t5 t42 = gm_f_write_rbbkc88(t38, t41);
                    gm_l12 = t42;
                    uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                    if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srclt70oog8j9io, 122, 32);
                    goto gm_lb4;
                }
gm_lb5:;
            }
gm_lb4:;
            {
                {
                    uint8_t t44 = (uint8_t)(((((uint8_t *)&F.l3) + 40))[16] != 0);
                    if (!t44) {
                        goto gm_lb7;
                    }
                    gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
                    gm_l5 = t45;
                    void * t46 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t47 = gm_l5;
                    gm_b16_8 t48 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t47);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t49;
                    GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
                    gm_tftb1odc739t5 t50 = gm_f_write_5jambfs(t46, t49);
                    gm_l13 = t50;
                    uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                    if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srclt70oog8j9io, 123, 32);
                    goto gm_lb6;
                }
gm_lb7:;
            }
gm_lb6:;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)1ULL));
            if (!t53) {
                goto gm_lb8;
            }
            memmove(((uint8_t *)&F.l6), (((uint8_t *)a0) + 8), 80);
            void * t54 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
            gm_b16_8 t56 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t57;
            GM_ST(gm_b16_8, ((uint8_t *)&t57 + 0), t56);
            gm_tftb1odc739t5 t58 = gm_f_write_ivc38em(t54, t57);
            gm_l14 = t58;
            uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srclt70oog8j9io, 126, 13);
            {
                {
                    uint8_t t60 = (uint8_t)(((((uint8_t *)&F.l6) + 16))[32] != 0);
                    if (!t60) {
                        goto gm_lb10;
                    }
                    {
                        memmove(((uint8_t *)&gm_l7), (((uint8_t *)&F.l6) + 16), 32);
                    }
                    {
                        void * t61 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 16));
                        gm_b16_8 t64 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t63);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6j6smj7qoctu t65;
                        GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t62);
                        GM_ST(gm_b16_8, ((uint8_t *)&t65 + 16), t64);
                        gm_tftb1odc739t5 t66 = gm_f_write_gle9guv(t61, &t65);
                        gm_l15 = t66;
                    }
                    uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                    if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srclt70oog8j9io, 127, 35);
                    goto gm_lb9;
                }
gm_lb10:;
            }
gm_lb9:;
            {
                {
                    uint8_t t68 = (uint8_t)(((((uint8_t *)&F.l6) + 56))[16] != 0);
                    if (!t68) {
                        goto gm_lb12;
                    }
                    gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 56));
                    gm_l8 = t69;
                    void * t70 = (void *)((uint8_t *)&F.l1);
                    gm_b16_8 t71 = gm_l8;
                    gm_b16_8 t72 = gm_f_toml_string_cagbrmp((void *)NULL, cx, NULL, t71);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t73;
                    GM_ST(gm_b16_8, ((uint8_t *)&t73 + 0), t72);
                    gm_tftb1odc739t5 t74 = gm_f_write_5jambfs(t70, t73);
                    gm_l16 = t74;
                    uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                    if (!t75) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srclt70oog8j9io, 128, 32);
                    goto gm_lb11;
                }
gm_lb12:;
            }
gm_lb11:;
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 118, 5);
    }
gm_lb1:;
    void * t76 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t77;
    GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s5m9qimbv7e8k));
    GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(2));
    gm_tftb1odc739t5 t78 = gm_f_extend_0cs85gs(t76, t77);
    gm_l17 = t78;
    uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srclt70oog8j9io, 131, 5);
    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)&t80));
    F.ret = t81;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_blank_or_comment_lubpopi(gm_b16_8 a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_header_uakq83c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_without_ej3mkbe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1, uint64_t a2) {
    struct gm_fr_join_without_ej3mkbe F;
    gm_b16_8 gm_l0; /* lines */
    uint64_t gm_l1; /* from */
    uint64_t gm_l2; /* to */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* l */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l9; /* l */
    GM_STACK_CHECK(&F, 548u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_trqfvmjtt4sij t12;
    gm_f_list_uvfu1k1((void *)&t12, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t12;
    void * t13 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t15 = gm_l1;
    uint64_t t16 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t17 = (uintptr_t)t15;
    if (t16 > t17 || t17 > (uintptr_t)t14) gm_trap_slice(t16, t17, t14, gm_srclt70oog8j9io, 221, 14);
    uint64_t t18 = ((uintptr_t)t15) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t13 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(t18));
    gm_l4 = t19;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t20 = gm_l5;
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            {
                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t25 = gm_l5;
                {
                    {
                        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t23) + (uintptr_t)t25 * 16));
                        gm_l6 = t26;
                        void * t27 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t28 = gm_l6;
                        gm_tftb1odc739t5 t29 = gm_f_extend_0cs85gs(t27, t28);
                        gm_l10 = t29;
                        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srclt70oog8j9io, 221, 31);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 221, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t31 = gm_l5;
            uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
            uint64_t t34 = r32;
            gm_l5 = t34;
        }
    }
gm_lb1:;
    void * t35 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t37 = gm_l2;
    uint64_t t38 = (uintptr_t)t37;
    uint64_t t39 = (uintptr_t)t36;
    if (t38 > t39 || t39 > (uintptr_t)t36) gm_trap_slice(t38, t39, t36, gm_srclt70oog8j9io, 222, 14);
    uint64_t t40 = ((uintptr_t)t36) - (uintptr_t)t37;
    gm_b16_8 t41;
    GM_ST(void *, (uint8_t *)&t41, (void *)(((uint8_t *)t35 + (uintptr_t)t37 * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(t40));
    gm_l7 = t41;
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t42 = gm_l8;
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint8_t t44 = (t42 < t43);
            if (!t44) {
                goto gm_lb6;
            }
            {
                void * t45 = GM_LD(void *, ((uint8_t *)&gm_l7));
                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint64_t t47 = gm_l8;
                {
                    {
                        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)t45) + (uintptr_t)t47 * 16));
                        gm_l9 = t48;
                        void * t49 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t50 = gm_l9;
                        gm_tftb1odc739t5 t51 = gm_f_extend_0cs85gs(t49, t50);
                        gm_l11 = t51;
                        uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srclt70oog8j9io, 222, 28);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 222, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t53 = gm_l8;
            uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
            uint64_t t56 = r54;
            gm_l8 = t56;
        }
    }
gm_lb6:;
    gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
    F.ret = t58;
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
__attribute__((aligned(4))) void gm_f_key_of_99iu4dv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_key_of_99iu4dv F;
    gm_b16_8 gm_l0; /* line */
    gm_b16_8 gm_l1; /* t */
    uint64_t gm_l2; /* eq */
    gm_b16_8 gm_l3; /* k */
    uint64_t gm_l4; /* dot */
    GM_STACK_CHECK_SHALLOW(&F, 633u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tf5srh4mp4b3i gm_l5;
    uint8_t gm_l6;
    gm_tf5srh4mp4b3i gm_l7;
    gm_b16_8 t8 = gm_l0;
    gm_b16_8 t9 = gm_f_trim_ascii_c9j36d1(t8);
    gm_l1 = t9;
    gm_b16_8 t10 = gm_l1;
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sh0j2ggs1vvoa));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(1));
    gm_tf5srh4mp4b3i t12 = gm_f_find_nab29co(t10, t11);
    gm_l5 = t12;
    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
    if (t13) {
        uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
        gm_l2 = t14;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    void * t15 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t17 = gm_l2;
    uint64_t t18 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t19 = (uintptr_t)t17;
    if (t18 > t19 || t19 > (uintptr_t)t16) gm_trap_slice(t18, t19, t16, gm_srclt70oog8j9io, 62, 26);
    uint64_t t20 = ((uintptr_t)t17) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t21;
    GM_ST(void *, (uint8_t *)&t21, (void *)(((uint8_t *)t15 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(t20));
    gm_b16_8 t22 = gm_f_trim_ascii_c9j36d1(t21);
    gm_l3 = t22;
    {
        {
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t24 = (t23 >= ((uint64_t)2ULL));
            if (!t24) {
                goto gm_lb2;
            }
            void * t25 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t26, gm_srclt70oog8j9io, 63, 24);
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)t25) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint8_t t28 = ((t27 == ((uint8_t)34ULL)));
            if (t28) {
                gm_l6 = ((uint8_t)1);
            } else {
                void * t29 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t30, gm_srclt70oog8j9io, 63, 39);
                uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t29) + (uintptr_t)((uint64_t)0ULL) * 1));
                uint8_t t32 = ((t31 == ((uint8_t)39ULL)));
                gm_l6 = t32;
            }
            uint8_t t33 = gm_l6;
            if (!t33) {
                goto gm_lb2;
            }
            void * t34 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t r37; bool o38 = __builtin_sub_overflow(t36, ((uint64_t)1ULL), &r37);
            if (o38) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 63, 59);
            uint64_t t39 = r37;
            if ((uintptr_t)t39 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t39, t35, gm_srclt70oog8j9io, 63, 57);
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)t34) + (uintptr_t)t39 * 1));
            void * t41 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t42)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t42, gm_srclt70oog8j9io, 63, 73);
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)t41) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint8_t t44 = ((t40 == t43));
            if (!t44) {
                goto gm_lb2;
            }
            void * t45 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t r48; bool o49 = __builtin_sub_overflow(t47, ((uint64_t)1ULL), &r48);
            if (o49) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 63, 92);
            uint64_t t50 = r48;
            uint64_t t51 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t52 = (uintptr_t)t50;
            if (t51 > t52 || t52 > (uintptr_t)t46) gm_trap_slice(t51, t52, t46, gm_srclt70oog8j9io, 63, 87);
            uint64_t t53 = ((uintptr_t)t50) - (uintptr_t)((uint64_t)1ULL);
            gm_b16_8 t54;
            GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t45 + (uintptr_t)((uint64_t)1ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t54);
            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t56 = (t55 > ((uint64_t)0ULL));
            if (!t56) {
                goto gm_lb4;
            }
            void * t57 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)t57) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint8_t t60 = (!(t59 == ((uint8_t)34ULL)));
            if (!t60) {
                goto gm_lb4;
            }
            void * t61 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)t61) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint8_t t64 = (!(t63 == ((uint8_t)39ULL)));
            if (!t64) {
                goto gm_lb4;
            }
            {
                {
                    gm_b16_8 t65 = gm_l3;
                    gm_b16_8 t66;
                    GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sl3q52ep6aa53));
                    GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(1));
                    gm_tf5srh4mp4b3i t67 = gm_f_find_nab29co(t65, t66);
                    gm_l7 = t67;
                    uint8_t t68 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
                    if (!t68) {
                        goto gm_lb6;
                    }
                    uint64_t t69 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                    gm_l4 = t69;
                    void * t70 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint64_t t72 = gm_l4;
                    uint64_t t73 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t74 = (uintptr_t)t72;
                    if (t73 > t74 || t74 > (uintptr_t)t71) gm_trap_slice(t73, t74, t71, gm_srclt70oog8j9io, 66, 61);
                    uint64_t t75 = ((uintptr_t)t72) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t76;
                    GM_ST(void *, (uint8_t *)&t76, (void *)(((uint8_t *)t70 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(t75));
                    gm_b16_8 t77 = gm_f_trim_ascii_c9j36d1(t76);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t77);
                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                    goto gm_fdone;
                }
gm_lb6:;
            }
gm_lb5:;
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    gm_b16_8 t78 = gm_l3;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t78);
    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_normalize_newlines_183t7r2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_normalize_newlines_183t7r2 F;
    gm_b16_8 gm_l0; /* text */
    GM_STACK_CHECK(&F, 236u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    gm_tf5srh4mp4b3i gm_l2;
    gm_tabb710d9mq2e gm_l3;
    gm_b16_8 t5 = gm_l0;
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_skmt2kt2p7v0u));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(2));
    gm_tf5srh4mp4b3i t7 = gm_f_find_nab29co(t5, t6);
    gm_l2 = t7;
    {
        {
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
            if (t8) {
                goto gm_lb2;
            }
            gm_l1 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l1 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 16, 8);
    }
gm_lb1:;
    uint8_t t9 = gm_l1;
    if (t9) {
        gm_b16_8 t10 = gm_l0;
        F.ret = t10;
        goto gm_fret;
    }
    gm_b16_8 t11 = gm_l0;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_skmt2kt2p7v0u));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(2));
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_so514oj5nbhcu));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(1));
    gm_f_replace_alloc_t7hndlq((void *)&gm_l3, (void *)NULL, cx, NULL, t11, t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srclt70oog8j9io, 17, 12);
    gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
    F.ret = t16;
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
__attribute__((aligned(4))) void gm_f_remove_dependency_4ju7b0h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_remove_dependency_4ju7b0h F;
    gm_b16_8 gm_l0; /* original */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* text */
    gm_b16_8 gm_l4; /* missing */
    gm_t2lqu8jsj511d gm_l5; /* r */
    uint8_t gm_l7; /* found */
    uint64_t gm_l8; /* i */
    uint64_t gm_l9; /* n */
    gm_b16_8 gm_l10; /* k */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* j */
    gm_b16_8 gm_l14; /* wanted */
    gm_t2lqu8jsj511d gm_l15; /* r */
    uint64_t gm_l16; /* first */
    GM_STACK_CHECK(&F, 1444u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l17;
    gm_tc70a9dbbmgkg gm_l18;
    gm_th2lqbo77pvsc gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tc70a9dbbmgkg gm_l23;
    gm_b16_8 t22 = gm_l0;
    gm_b16_8 t23 = gm_f_normalize_newlines_183t7r2((void *)NULL, cx, NULL, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t23;
    {
        gm_b16_8 t24 = gm_l2;
        gm_f_split_lines_jecth9m((void *)&F.l3, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t25 = gm_l1;
        gm_tnpifv20irnph t26;
        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
        gm_f_format_rkrg0f2((void *)&gm_l17, (void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srclt70oog8j9io, 183, 16);
    gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
    gm_l4 = t28;
    {
        {
            {
                gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(gm_skof48dte1s1o));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(12));
                gm_f_table_range_09bngqh((void *)&gm_l18, (void *)NULL, cx, NULL, t30, t31);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t32 = (uint8_t)((((uint8_t *)&gm_l18))[16] != 0);
            if (!t32) {
                goto gm_lb2;
            }
            gm_t2lqu8jsj511d t33 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)&gm_l18));
            gm_l5 = t33;
            {
                gm_trqfvmjtt4sij t34;
                gm_f_list_epvducg((void *)&t34, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l6 = t34;
            }
            gm_l7 = ((uint8_t)0);
            gm_l8 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t35 = gm_l8;
                    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                    uint8_t t37 = (t35 < t36);
                    if (!t37) {
                        goto gm_lb3;
                    }
                    {
                        {
                            {
                                uint64_t t38 = gm_l8;
                                uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
                                uint8_t t40 = (t38 > t39);
                                if (!t40) {
                                    goto gm_lb7;
                                }
                                uint64_t t41 = gm_l8;
                                uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                uint8_t t43 = (t41 < t42);
                                if (!t43) {
                                    goto gm_lb7;
                                }
                                void * t44 = GM_LD(void *, ((uint8_t *)&F.l3));
                                uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                uint64_t t46 = gm_l8;
                                gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t44) + (uintptr_t)t46 * 16));
                                uint8_t t48 = gm_f_is_blank_or_comment_lubpopi(t47);
                                uint8_t t49 = (uint8_t)!t48;
                                if (!t49) {
                                    goto gm_lb7;
                                }
                                gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                                gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                                uint64_t t52 = gm_l8;
                                uint64_t t53 = gm_f_entry_lines_3jhl7a4(t51, t52);
                                gm_l9 = t53;
                                void * t54 = GM_LD(void *, ((uint8_t *)&F.l3));
                                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                uint64_t t56 = gm_l8;
                                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)t54) + (uintptr_t)t56 * 16));
                                gm_f_key_of_99iu4dv((void *)&gm_l19, (void *)NULL, cx, NULL, t57);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t58 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
                                if (t58) {
                                    gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
                                    gm_l10 = t59;
                                } else {
                                    gm_b16_8 t60;
                                    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sr4qbrk6tlgaa));
                                    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(0));
                                    gm_l10 = t60;
                                }
                                gm_b16_8 t61 = gm_l10;
                                gm_b16_8 t62 = gm_l1;
                                uint8_t t63 = (gm_eq0iqiubfm7e(((uint8_t *)&t61), ((uint8_t *)&t62)));
                                if (t63) {
                                    gm_l7 = ((uint8_t)1);
                                    {
                                        for (;;) {
                                            uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                                            uint64_t t65 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
                                            uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                                            if (o67) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 196, 44);
                                            uint64_t t68 = r66;
                                            uint8_t t69 = (t64 > t68);
                                            if (!t69) {
                                                goto gm_lb8;
                                            }
                                            void * t70 = GM_LD(void *, ((uint8_t *)&F.l6));
                                            uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                                            uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                                            uint64_t r73; bool o74 = __builtin_sub_overflow(t72, ((uint64_t)1ULL), &r73);
                                            if (o74) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 196, 92);
                                            uint64_t t75 = r73;
                                            if ((uintptr_t)t75 >= (uintptr_t)(t71)) gm_trap_index((uintptr_t)t75, t71, gm_srclt70oog8j9io, 196, 81);
                                            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)t70) + (uintptr_t)t75 * 16));
                                            uint8_t t77 = gm_f_is_blank_or_comment_lubpopi(t76);
                                            if (!t77) {
                                                goto gm_lb8;
                                            }
                                            {
                                                void * t78 = (void *)((uint8_t *)&F.l6);
                                                gm_th2lqbo77pvsc t79;
                                                gm_f_pop_vr08sii((void *)&t79, t78);
                                            }
gm_lb10:;
                                        }
                                    }
gm_lb8:;
                                    uint64_t t80 = gm_l8;
                                    uint64_t t81 = gm_l9;
                                    uint64_t r82; bool o83 = __builtin_add_overflow(t80, t81, &r82);
                                    if (o83) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 197, 21);
                                    uint64_t t84 = r82;
                                    gm_l8 = t84;
                                    goto gm_lb5;
                                }
                                gm_l12 = ((uint64_t)0ULL);
                                uint64_t t85 = gm_l9;
                                gm_l11 = t85;
                                {
                                    for (;;) {
                                        uint64_t t86 = gm_l12;
                                        uint64_t t87 = gm_l11;
                                        uint8_t t88 = (t86 < t87);
                                        if (!t88) {
                                            goto gm_lb11;
                                        }
                                        {
                                            {
                                                {
                                                    uint64_t t89 = gm_l12;
                                                    gm_l13 = t89;
                                                    void * t90 = (void *)((uint8_t *)&F.l6);
                                                    void * t91 = GM_LD(void *, ((uint8_t *)&F.l3));
                                                    uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                                                    uint64_t t93 = gm_l8;
                                                    uint64_t t94 = gm_l13;
                                                    uint64_t r95; bool o96 = __builtin_add_overflow(t93, t94, &r95);
                                                    if (o96) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 200, 55);
                                                    uint64_t t97 = r95;
                                                    if ((uintptr_t)t97 >= (uintptr_t)(t92)) gm_trap_index((uintptr_t)t97, t92, gm_srclt70oog8j9io, 200, 43);
                                                    gm_b16_8 t98 = GM_LD(gm_b16_8, (((uint8_t *)t91) + (uintptr_t)t97 * 16));
                                                    gm_tftb1odc739t5 t99 = gm_f_push_3ef746b(t90, t98);
                                                    gm_l20 = t99;
                                                    uint8_t t100 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                                    if (!t100) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srclt70oog8j9io, 200, 33);
                                                    goto gm_lb14;
                                                }
gm_lb15:;
                                                gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 200, 17);
                                            }
gm_lb14:;
                                        }
gm_lb13:;
                                        uint64_t t101 = gm_l12;
                                        uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                                        uint64_t t104 = r102;
                                        gm_l12 = t104;
                                    }
                                }
gm_lb11:;
                                uint64_t t105 = gm_l8;
                                uint64_t t106 = gm_l9;
                                uint64_t r107; bool o108 = __builtin_add_overflow(t105, t106, &r107);
                                if (o108) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 201, 17);
                                uint64_t t109 = r107;
                                gm_l8 = t109;
                                goto gm_lb5;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        void * t110 = (void *)((uint8_t *)&F.l6);
                        void * t111 = GM_LD(void *, ((uint8_t *)&F.l3));
                        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                        uint64_t t113 = gm_l8;
                        if ((uintptr_t)t113 >= (uintptr_t)(t112)) gm_trap_index((uintptr_t)t113, t112, gm_srclt70oog8j9io, 204, 23);
                        gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)t111) + (uintptr_t)t113 * 16));
                        gm_tftb1odc739t5 t115 = gm_f_push_3ef746b(t110, t114);
                        gm_l21 = t115;
                        uint8_t t116 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                        if (!t116) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srclt70oog8j9io, 204, 13);
                        uint64_t t117 = gm_l8;
                        uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                        if (o119) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 205, 13);
                        uint64_t t120 = r118;
                        gm_l8 = t120;
                    }
gm_lb5:;
                }
            }
gm_lb3:;
            uint8_t t121 = gm_l7;
            if (t121) {
                gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                gm_b16_8 t123 = GM_LD(gm_b16_8, ((uint8_t *)&t122));
                gm_b16_8 t124 = gm_f_join_without_ej3mkbe((void *)NULL, cx, NULL, t123, ((uint64_t)0ULL), ((uint64_t)0ULL));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t124);
                goto gm_fdone;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_b16_8 t125 = gm_l1;
        gm_tnpifv20irnph t126;
        GM_ST(gm_b16_8, ((uint8_t *)&t126 + 0), t125);
        gm_f_format_7plp021((void *)&gm_l22, (void *)NULL, cx, NULL, t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t127 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
    if (!t127) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_srclt70oog8j9io, 210, 15);
    gm_b16_8 t128 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
    gm_l14 = t128;
    {
        {
            {
                gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                gm_b16_8 t131 = gm_l14;
                gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&t131));
                gm_f_table_range_09bngqh((void *)&gm_l23, (void *)NULL, cx, NULL, t130, t132);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t133 = (uint8_t)((((uint8_t *)&gm_l23))[16] != 0);
            if (!t133) {
                goto gm_lb17;
            }
            gm_t2lqu8jsj511d t134 = GM_LD(gm_t2lqu8jsj511d, ((uint8_t *)&gm_l23));
            gm_l15 = t134;
            uint64_t t135 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
            gm_l16 = t135;
            {
                for (;;) {
                    uint64_t t136 = gm_l16;
                    uint8_t t137 = (t136 > ((uint64_t)0ULL));
                    if (!t137) {
                        goto gm_lb18;
                    }
                    void * t138 = GM_LD(void *, ((uint8_t *)&F.l3));
                    uint64_t t139 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
                    uint64_t t140 = gm_l16;
                    uint64_t r141; bool o142 = __builtin_sub_overflow(t140, ((uint64_t)1ULL), &r141);
                    if (o142) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 213, 61);
                    uint64_t t143 = r141;
                    if ((uintptr_t)t143 >= (uintptr_t)(t139)) gm_trap_index((uintptr_t)t143, t139, gm_srclt70oog8j9io, 213, 49);
                    gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)t138) + (uintptr_t)t143 * 16));
                    uint8_t t145 = gm_f_is_blank_or_comment_lubpopi(t144);
                    if (!t145) {
                        goto gm_lb18;
                    }
                    {
                        uint64_t t146 = gm_l16;
                        uint64_t r147; bool o148 = __builtin_sub_overflow(t146, ((uint64_t)1ULL), &r147);
                        if (o148) gm_trap("integer overflow in `-`", gm_srclt70oog8j9io, 213, 75);
                        uint64_t t149 = r147;
                        gm_l16 = t149;
                    }
gm_lb20:;
                }
            }
gm_lb18:;
            {
                gm_b16_8 t150 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                gm_b16_8 t151 = GM_LD(gm_b16_8, ((uint8_t *)&t150));
                uint64_t t152 = gm_l16;
                uint64_t t153 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                gm_b16_8 t154 = gm_f_join_without_ej3mkbe((void *)NULL, cx, NULL, t151, t152, t153);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t154);
                goto gm_fdone;
            }
        }
gm_lb17:;
    }
gm_lb16:;
    {
        gm_b16_8 t155 = gm_l4;
        gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)&t155));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t156);
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
inline __attribute__((aligned(4))) void gm_f_split_lines_jecth9m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_split_lines_jecth9m F;
    gm_b16_8 gm_l0; /* text */
    uint64_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint8_t gm_l6; /* c */
    GM_STACK_CHECK(&F, 409u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tf5srh4mp4b3i gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    {
        gm_trqfvmjtt4sij t7;
        gm_f_list_epvducg((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t7;
    }
    gm_l2 = ((uint64_t)0ULL);
    gm_b16_8 t8 = gm_l0;
    gm_l3 = t8;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l4;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                uint64_t t12 = gm_l4;
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t15 = gm_l4;
                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t13) + (uintptr_t)t15 * 1));
                gm_tf5srh4mp4b3i t17;
                GM_ST(uint8_t, (uint8_t *)&t17 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t17 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t17 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t17 + 0), t12);
                GM_ST(uint8_t, ((uint8_t *)&t17 + 8), t16);
                gm_l7 = t17;
                {
                    {
                        uint64_t t18 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                        gm_l5 = t18;
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 8));
                        gm_l6 = t19;
                        uint8_t t20 = gm_l6;
                        uint8_t t21 = ((t20 == ((uint8_t)10ULL)));
                        if (t21) {
                            void * t22 = (void *)((uint8_t *)&F.l1);
                            void * t23 = GM_LD(void *, ((uint8_t *)&gm_l0));
                            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint64_t t25 = gm_l2;
                            uint64_t t26 = gm_l5;
                            uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                            if (o28) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 26, 36);
                            uint64_t t29 = r27;
                            uint64_t t30 = (uintptr_t)t25;
                            uint64_t t31 = (uintptr_t)t29;
                            if (t30 > t31 || t31 > (uintptr_t)t24) gm_trap_slice(t30, t31, t24, gm_srclt70oog8j9io, 26, 24);
                            uint64_t t32 = ((uintptr_t)t29) - (uintptr_t)t25;
                            gm_b16_8 t33;
                            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t23 + (uintptr_t)t25 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32));
                            gm_tftb1odc739t5 t34 = gm_f_push_3ef746b(t22, t33);
                            gm_l8 = t34;
                            uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                            if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srclt70oog8j9io, 26, 13);
                            uint64_t t36 = gm_l5;
                            uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                            if (o38) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 27, 21);
                            uint64_t t39 = r37;
                            gm_l2 = t39;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 24, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t40 = gm_l4;
            uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
            uint64_t t43 = r41;
            gm_l4 = t43;
        }
    }
gm_lb1:;
    uint64_t t44 = gm_l2;
    uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t46 = (t44 < t45);
    if (t46) {
        void * t47 = (void *)((uint8_t *)&F.l1);
        void * t48 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t50 = gm_l2;
        uint64_t t51 = ((uintptr_t)t49) - (uintptr_t)t50;
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t48 + (uintptr_t)t50 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(t51));
        gm_tftb1odc739t5 t53 = gm_f_push_3ef746b(t47, t52);
        gm_l9 = t53;
        uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srclt70oog8j9io, 30, 27);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) void gm_f_table_range_09bngqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_table_range_09bngqh F;
    gm_b16_8 gm_l0; /* lines */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    gm_b16_8 gm_l5; /* l */
    gm_b16_8 gm_l6; /* h */
    uint64_t gm_l7; /* end */
    GM_STACK_CHECK_SHALLOW(&F, 462u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf1bjg81u0glv gm_l8;
    gm_th2lqbo77pvsc gm_l9;
    gm_b16_8 t12 = gm_l0;
    gm_l2 = t12;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t13 = gm_l3;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                uint64_t t16 = gm_l3;
                void * t17 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t19 = gm_l3;
                gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t17) + (uintptr_t)t19 * 16));
                GM_ST(uint64_t, ((uint8_t *)&gm_l8 + 0), t16);
                GM_ST(gm_b16_8, ((uint8_t *)&gm_l8 + 8), t20);
                {
                    {
                        uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
                        gm_l4 = t21;
                        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                        gm_l5 = t22;
                        {
                            {
                                gm_b16_8 t23 = gm_l5;
                                gm_f_header_of_2u6atoc((void *)&gm_l9, (void *)NULL, cx, NULL, t23);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t24 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
                                if (!t24) {
                                    goto gm_lb7;
                                }
                                gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
                                gm_l6 = t25;
                                gm_b16_8 t26 = gm_l6;
                                gm_b16_8 t27 = gm_l1;
                                uint8_t t28 = (gm_eq0iqiubfm7e(((uint8_t *)&t26), ((uint8_t *)&t27)));
                                if (t28) {
                                    uint64_t t29 = gm_l4;
                                    uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)1ULL), &r30);
                                    if (o31) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 106, 24);
                                    uint64_t t32 = r30;
                                    gm_l7 = t32;
                                    {
                                        for (;;) {
                                            uint64_t t33 = gm_l7;
                                            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                            uint8_t t35 = (t33 < t34);
                                            if (!t35) {
                                                goto gm_lb8;
                                            }
                                            void * t36 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                            uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                            uint64_t t38 = gm_l7;
                                            gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)t36) + (uintptr_t)t38 * 16));
                                            uint8_t t40 = gm_f_is_header_uakq83c((void *)NULL, cx, NULL, t39);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t41 = (uint8_t)!t40;
                                            if (!t41) {
                                                goto gm_lb8;
                                            }
                                            {
                                                uint64_t t42 = gm_l7;
                                                uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                                                if (o44) gm_trap("integer overflow in `+`", gm_srclt70oog8j9io, 107, 71);
                                                uint64_t t45 = r43;
                                                gm_l7 = t45;
                                            }
gm_lb10:;
                                        }
                                    }
gm_lb8:;
                                    uint64_t t46 = gm_l4;
                                    uint64_t t47 = gm_l7;
                                    gm_t2lqu8jsj511d t48;
                                    GM_ST(uint64_t, ((uint8_t *)&t48 + 0), t46);
                                    GM_ST(uint64_t, ((uint8_t *)&t48 + 8), t47);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 17, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 18, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out) + 20, 0);
                                    GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out)), t48);
                                    ((uint8_t *)&(*(gm_tc70a9dbbmgkg *)gm_out))[16] = 1;
                                    goto gm_fdone;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclt70oog8j9io, 103, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t49 = gm_l3;
            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
            uint64_t t52 = r50;
            gm_l3 = t52;
        }
    }
gm_lb1:;
    memset(&(*(gm_tc70a9dbbmgkg *)gm_out), 0, sizeof (*(gm_tc70a9dbbmgkg *)gm_out));
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

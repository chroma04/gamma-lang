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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; } f1; } gm_t8c5h7u09n10a;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
typedef struct gm_csedogq9e4rp5r { uint32_t f0; gm_cseahvg8v7p0vs f1; } gm_csedogq9e4rp5r;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_t88rcucg1b7uk f0; gm_t9qkh0rsooc8d f1; uint8_t pe[3]; } gm_t3jsf43p1pths;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 a[12]; } gm_t61mja4eihh2b;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t8c5h7u09n10a) == 16 && _Alignof(gm_t8c5h7u09n10a) == 8 && offsetof(gm_t8c5h7u09n10a, f0) == 0 && offsetof(gm_t8c5h7u09n10a, f1) == 8, "layout of `DeclState`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; } gm_t9d4sf0vqn7dt;
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tammvh1iespi3;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_tf5srh4mp4b3i f0; } gm_tba9024q2up98;
typedef struct { uint32_t f0; gm_tqs5p4n2tq19v f1; uint8_t f2; uint8_t f3; } gm_tbcgm40aljf9h;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_tl874t2atnlh3 f1; } gm_tbvif938ns15l;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_tl874t2atnlh3 f0; gm_t88rcucg1b7uk f1; } gm_trlabqsjjp0ds;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_csedogq9e4rp5r) == 16 && _Alignof(gm_csedogq9e4rp5r) == 4 && offsetof(gm_csedogq9e4rp5r, f0) == 0 && offsetof(gm_csedogq9e4rp5r, f1) == 4, "layout of `TraceCall`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3jsf43p1pths) == 8 && _Alignof(gm_t3jsf43p1pths) == 4 && offsetof(gm_t3jsf43p1pths, f0) == 0 && offsetof(gm_t3jsf43p1pths, f1) == 4, "layout of `Mention`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t61mja4eihh2b) == 192 && _Alignof(gm_t61mja4eihh2b) == 8 && offsetof(gm_t61mja4eihh2b, a) == 0, "layout of `[12]string`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
typedef struct { gm_tbvif938ns15l f0; uint8_t f1; uint8_t pe[3]; } gm_t84dv0cg48dut;
typedef struct { gm_tba9024q2up98 f0; uint8_t f1; uint8_t pe[7]; } gm_t89h15e2c5o3p;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9d4sf0vqn7dt) == 16 && _Alignof(gm_t9d4sf0vqn7dt) == 4 && offsetof(gm_t9d4sf0vqn7dt, f0) == 0 && offsetof(gm_t9d4sf0vqn7dt, f1) == 4, "layout of `TraceCall`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tammvh1iespi3) == 56 && _Alignof(gm_tammvh1iespi3) == 8 && offsetof(gm_tammvh1iespi3, f0) == 0 && offsetof(gm_tammvh1iespi3, f1) == 16 && offsetof(gm_tammvh1iespi3, f2) == 24 && offsetof(gm_tammvh1iespi3, f3) == 40, "layout of `EffectInfo`");
_Static_assert(sizeof(gm_tba9024q2up98) == 16 && _Alignof(gm_tba9024q2up98) == 8 && offsetof(gm_tba9024q2up98, f0) == 0, "layout of `AliasTarget`");
_Static_assert(sizeof(gm_tbcgm40aljf9h) == 8 && _Alignof(gm_tbcgm40aljf9h) == 4 && offsetof(gm_tbcgm40aljf9h, f0) == 0 && offsetof(gm_tbcgm40aljf9h, f1) == 4 && offsetof(gm_tbcgm40aljf9h, f2) == 6 && offsetof(gm_tbcgm40aljf9h, f3) == 7, "layout of `FnHint`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tbvif938ns15l) == 16 && _Alignof(gm_tbvif938ns15l) == 4 && offsetof(gm_tbvif938ns15l, f0) == 0 && offsetof(gm_tbvif938ns15l, f1) == 4, "layout of `(win64: bool, span: Span)`");
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
typedef struct { gm_tdgfduqjheqdm f0; uint8_t f1; uint8_t pe[7]; } gm_tdn4erebgflsv;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
typedef struct { gm_tiilmfd16v3fc a[1]; } gm_tfe2ufaqq8re9;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tbngattl1fo51 f0; uint8_t f1; uint8_t pe[3]; } gm_thetuhtouve7f;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; uint64_t f3; void * f4; gm_tkn42u7na3t81 f5; uint64_t f6; void * f7; gm_t9qkh0rsooc8d f8; uint8_t p9[7]; void * f9; } gm_tjcptv9sagfq8;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; } gm_tpcj7kul6hlib;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint64_t f0; gm_b16_8 f1; gm_t88rcucg1b7uk f2; uint8_t p3[4]; uint64_t f3; void * f4; uint8_t f5; uint8_t p6[7]; gm_th2lqbo77pvsc f6; gm_tf5srh4mp4b3i f7; void * f8; } gm_tq7qjpkmqmc5s;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { gm_t3jsf43p1pths f0; uint8_t f1; uint8_t pe[3]; } gm_trcagkhgm1qdu;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trlabqsjjp0ds) == 16 && _Alignof(gm_trlabqsjjp0ds) == 4 && offsetof(gm_trlabqsjjp0ds, f0) == 0 && offsetof(gm_trlabqsjjp0ds, f1) == 12, "layout of `(Span, Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t84dv0cg48dut) == 20 && _Alignof(gm_t84dv0cg48dut) == 4 && offsetof(gm_t84dv0cg48dut, f0) == 0 && offsetof(gm_t84dv0cg48dut, f1) == 16, "layout of `?(win64: bool, span: Span)`");
_Static_assert(sizeof(gm_t89h15e2c5o3p) == 24 && _Alignof(gm_t89h15e2c5o3p) == 8 && offsetof(gm_t89h15e2c5o3p, f0) == 0 && offsetof(gm_t89h15e2c5o3p, f1) == 16, "layout of `?AliasTarget`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
typedef struct { gm_tkacitepg5fk0 f0; uint8_t f1; uint8_t pe[7]; } gm_tcus0chk5r9uq;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdn4erebgflsv) == 72 && _Alignof(gm_tdn4erebgflsv) == 8 && offsetof(gm_tdn4erebgflsv, f0) == 0 && offsetof(gm_tdn4erebgflsv, f1) == 64, "layout of `?FnDef`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_tpcj7kul6hlib f1; gm_trqfvmjtt4sij f2; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_trqfvmjtt4sij f6; gm_trqfvmjtt4sij f7; gm_trqfvmjtt4sij f8; uint8_t f9; uint8_t p10[7]; gm_trqfvmjtt4sij f10; gm_trqfvmjtt4sij f11; uint8_t f12; uint8_t p13[7]; gm_trqfvmjtt4sij f13; gm_trqfvmjtt4sij f14; gm_trqfvmjtt4sij f15; gm_trqfvmjtt4sij f16; gm_trqfvmjtt4sij f17; gm_trqfvmjtt4sij f18; gm_trqfvmjtt4sij f19; gm_t9805qtonbsag f20; gm_trqfvmjtt4sij f21; gm_trqfvmjtt4sij f22; gm_trqfvmjtt4sij f23; gm_trqfvmjtt4sij f24; gm_trqfvmjtt4sij f25; gm_trqfvmjtt4sij f26; gm_t4c7ipjj9s3fp f27; gm_t4c7ipjj9s3fp f28; gm_trqfvmjtt4sij f29; uint32_t f30; uint8_t p31[4]; gm_t4c7ipjj9s3fp f31; gm_t4c7ipjj9s3fp f32; uint8_t f33; uint8_t p34[7]; gm_trqfvmjtt4sij f34; gm_trqfvmjtt4sij f35; gm_trqfvmjtt4sij f36; gm_trqfvmjtt4sij f37; gm_trqfvmjtt4sij f38; gm_trqfvmjtt4sij f39; gm_trqfvmjtt4sij f40; gm_trqfvmjtt4sij f41; gm_trqfvmjtt4sij f42; gm_trqfvmjtt4sij f43; uint32_t f44; uint8_t p45[4]; gm_trqfvmjtt4sij f45; uint32_t f46; uint8_t p47[4]; gm_trqfvmjtt4sij f47; uint8_t f48; uint8_t p49[7]; gm_trqfvmjtt4sij f49; gm_trqfvmjtt4sij f50; uint32_t f51; gm_t9805qtonbsag f52; uint8_t p53[4]; gm_trqfvmjtt4sij f53; gm_t4c7ipjj9s3fp f54; gm_t4c7ipjj9s3fp f55; gm_trqfvmjtt4sij f56; uint32_t f57; uint8_t p58[4]; void * f58; void * f59; void * f60; gm_trqfvmjtt4sij f61; gm_trqfvmjtt4sij f62; gm_trqfvmjtt4sij f63; uint64_t f64; gm_t4c7ipjj9s3fp f65; gm_trqfvmjtt4sij f66; gm_trqfvmjtt4sij f67; gm_trqfvmjtt4sij f68; gm_trqfvmjtt4sij f69; uint8_t f70; uint8_t p71[7]; gm_trqfvmjtt4sij f71; gm_trqfvmjtt4sij f72; gm_t4c7ipjj9s3fp f73; gm_t4c7ipjj9s3fp f74; gm_t4c7ipjj9s3fp f75; } gm_tfbjaprcetfe7;
_Static_assert(sizeof(gm_tfe2ufaqq8re9) == 24 && _Alignof(gm_tfe2ufaqq8re9) == 8 && offsetof(gm_tfe2ufaqq8re9, a) == 0, "layout of `[1]EffectRef`");
_Static_assert(sizeof(gm_thetuhtouve7f) == 12 && _Alignof(gm_thetuhtouve7f) == 4 && offsetof(gm_thetuhtouve7f, f0) == 0 && offsetof(gm_thetuhtouve7f, f1) == 8, "layout of `?(inclusive: bool, elem: Ty)`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tjcptv9sagfq8) == 88 && _Alignof(gm_tjcptv9sagfq8) == 8 && offsetof(gm_tjcptv9sagfq8, f0) == 0 && offsetof(gm_tjcptv9sagfq8, f1) == 16 && offsetof(gm_tjcptv9sagfq8, f2) == 28 && offsetof(gm_tjcptv9sagfq8, f3) == 32 && offsetof(gm_tjcptv9sagfq8, f4) == 40 && offsetof(gm_tjcptv9sagfq8, f5) == 48 && offsetof(gm_tjcptv9sagfq8, f6) == 56 && offsetof(gm_tjcptv9sagfq8, f7) == 64 && offsetof(gm_tjcptv9sagfq8, f8) == 72 && offsetof(gm_tjcptv9sagfq8, f9) == 80, "layout of `ConstDef`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpcj7kul6hlib) == 296 && _Alignof(gm_tpcj7kul6hlib) == 8 && offsetof(gm_tpcj7kul6hlib, f0) == 0 && offsetof(gm_tpcj7kul6hlib, f1) == 40 && offsetof(gm_tpcj7kul6hlib, f2) == 80 && offsetof(gm_tpcj7kul6hlib, f3) == 136 && offsetof(gm_tpcj7kul6hlib, f4) == 176 && offsetof(gm_tpcj7kul6hlib, f5) == 216 && offsetof(gm_tpcj7kul6hlib, f6) == 256, "layout of `Types`");
_Static_assert(sizeof(gm_tq7qjpkmqmc5s) == 104 && _Alignof(gm_tq7qjpkmqmc5s) == 8 && offsetof(gm_tq7qjpkmqmc5s, f0) == 0 && offsetof(gm_tq7qjpkmqmc5s, f1) == 8 && offsetof(gm_tq7qjpkmqmc5s, f2) == 24 && offsetof(gm_tq7qjpkmqmc5s, f3) == 32 && offsetof(gm_tq7qjpkmqmc5s, f4) == 40 && offsetof(gm_tq7qjpkmqmc5s, f5) == 48 && offsetof(gm_tq7qjpkmqmc5s, f6) == 56 && offsetof(gm_tq7qjpkmqmc5s, f7) == 80 && offsetof(gm_tq7qjpkmqmc5s, f8) == 96, "layout of `GlobalInfo`");
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trcagkhgm1qdu) == 12 && _Alignof(gm_trcagkhgm1qdu) == 4 && offsetof(gm_trcagkhgm1qdu, f0) == 0 && offsetof(gm_trcagkhgm1qdu, f1) == 8, "layout of `?(ty: Ty, vis: Visibility)`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tcus0chk5r9uq) == 80 && _Alignof(gm_tcus0chk5r9uq) == 8 && offsetof(gm_tcus0chk5r9uq, f0) == 0 && offsetof(gm_tcus0chk5r9uq, f1) == 72, "layout of `?Attribute`");
_Static_assert(sizeof(gm_tfbjaprcetfe7) == 2912 && _Alignof(gm_tfbjaprcetfe7) == 8 && offsetof(gm_tfbjaprcetfe7, f0) == 0 && offsetof(gm_tfbjaprcetfe7, f1) == 8 && offsetof(gm_tfbjaprcetfe7, f2) == 304 && offsetof(gm_tfbjaprcetfe7, f3) == 344 && offsetof(gm_tfbjaprcetfe7, f4) == 384 && offsetof(gm_tfbjaprcetfe7, f5) == 424 && offsetof(gm_tfbjaprcetfe7, f6) == 464 && offsetof(gm_tfbjaprcetfe7, f7) == 504 && offsetof(gm_tfbjaprcetfe7, f8) == 544 && offsetof(gm_tfbjaprcetfe7, f9) == 584 && offsetof(gm_tfbjaprcetfe7, f10) == 592 && offsetof(gm_tfbjaprcetfe7, f11) == 632 && offsetof(gm_tfbjaprcetfe7, f12) == 672 && offsetof(gm_tfbjaprcetfe7, f13) == 680 && offsetof(gm_tfbjaprcetfe7, f14) == 720 && offsetof(gm_tfbjaprcetfe7, f15) == 760 && offsetof(gm_tfbjaprcetfe7, f16) == 800 && offsetof(gm_tfbjaprcetfe7, f17) == 840 && offsetof(gm_tfbjaprcetfe7, f18) == 880 && offsetof(gm_tfbjaprcetfe7, f19) == 920 && offsetof(gm_tfbjaprcetfe7, f20) == 960 && offsetof(gm_tfbjaprcetfe7, f21) == 968 && offsetof(gm_tfbjaprcetfe7, f22) == 1008 && offsetof(gm_tfbjaprcetfe7, f23) == 1048 && offsetof(gm_tfbjaprcetfe7, f24) == 1088 && offsetof(gm_tfbjaprcetfe7, f25) == 1128 && offsetof(gm_tfbjaprcetfe7, f26) == 1168 && offsetof(gm_tfbjaprcetfe7, f27) == 1208 && offsetof(gm_tfbjaprcetfe7, f28) == 1264 && offsetof(gm_tfbjaprcetfe7, f29) == 1320 && offsetof(gm_tfbjaprcetfe7, f30) == 1360 && offsetof(gm_tfbjaprcetfe7, f31) == 1368 && offsetof(gm_tfbjaprcetfe7, f32) == 1424 && offsetof(gm_tfbjaprcetfe7, f33) == 1480 && offsetof(gm_tfbjaprcetfe7, f34) == 1488 && offsetof(gm_tfbjaprcetfe7, f35) == 1528 && offsetof(gm_tfbjaprcetfe7, f36) == 1568 && offsetof(gm_tfbjaprcetfe7, f37) == 1608 && offsetof(gm_tfbjaprcetfe7, f38) == 1648 && offsetof(gm_tfbjaprcetfe7, f39) == 1688 && offsetof(gm_tfbjaprcetfe7, f40) == 1728 && offsetof(gm_tfbjaprcetfe7, f41) == 1768 && offsetof(gm_tfbjaprcetfe7, f42) == 1808 && offsetof(gm_tfbjaprcetfe7, f43) == 1848 && offsetof(gm_tfbjaprcetfe7, f44) == 1888 && offsetof(gm_tfbjaprcetfe7, f45) == 1896 && offsetof(gm_tfbjaprcetfe7, f46) == 1936 && offsetof(gm_tfbjaprcetfe7, f47) == 1944 && offsetof(gm_tfbjaprcetfe7, f48) == 1984 && offsetof(gm_tfbjaprcetfe7, f49) == 1992 && offsetof(gm_tfbjaprcetfe7, f50) == 2032 && offsetof(gm_tfbjaprcetfe7, f51) == 2072 && offsetof(gm_tfbjaprcetfe7, f52) == 2076 && offsetof(gm_tfbjaprcetfe7, f53) == 2088 && offsetof(gm_tfbjaprcetfe7, f54) == 2128 && offsetof(gm_tfbjaprcetfe7, f55) == 2184 && offsetof(gm_tfbjaprcetfe7, f56) == 2240 && offsetof(gm_tfbjaprcetfe7, f57) == 2280 && offsetof(gm_tfbjaprcetfe7, f58) == 2288 && offsetof(gm_tfbjaprcetfe7, f59) == 2296 && offsetof(gm_tfbjaprcetfe7, f60) == 2304 && offsetof(gm_tfbjaprcetfe7, f61) == 2312 && offsetof(gm_tfbjaprcetfe7, f62) == 2352 && offsetof(gm_tfbjaprcetfe7, f63) == 2392 && offsetof(gm_tfbjaprcetfe7, f64) == 2432 && offsetof(gm_tfbjaprcetfe7, f65) == 2440 && offsetof(gm_tfbjaprcetfe7, f66) == 2496 && offsetof(gm_tfbjaprcetfe7, f67) == 2536 && offsetof(gm_tfbjaprcetfe7, f68) == 2576 && offsetof(gm_tfbjaprcetfe7, f69) == 2616 && offsetof(gm_tfbjaprcetfe7, f70) == 2656 && offsetof(gm_tfbjaprcetfe7, f71) == 2664 && offsetof(gm_tfbjaprcetfe7, f72) == 2704 && offsetof(gm_tfbjaprcetfe7, f73) == 2744 && offsetof(gm_tfbjaprcetfe7, f74) == 2800 && offsetof(gm_tfbjaprcetfe7, f75) == 2856, "layout of `Sema`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t08olq97rscn3 f0; uint64_t f1; gm_tvpnktv5fo8nl f2; gm_t8c5h7u09n10a f3; } gm_t52j636u3vgk3;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
_Static_assert(sizeof(gm_t52j636u3vgk3) == 216 && _Alignof(gm_t52j636u3vgk3) == 8 && offsetof(gm_t52j636u3vgk3, f0) == 0 && offsetof(gm_t52j636u3vgk3, f1) == 32 && offsetof(gm_t52j636u3vgk3, f2) == 40 && offsetof(gm_t52j636u3vgk3, f3) == 200, "layout of `Decl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
struct gm_fr_add_const_8u8b1ja {
    gm_frame h;
    gm_tjcptv9sagfq8 l1; /* c */
    uint32_t ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adt_field_tys_oggq5ji {
    gm_frame h;
    gm_tpautr4br7h3l l4; /* def */
    gm_tckdobunaulee l9; /* v */
    gm_b0_1 ret;
};
struct gm_fr_adt_name_h7mjik7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_aliased_decl_jv6ilmn {
    gm_frame h;
    gm_t89h15e2c5o3p ret;
};
struct gm_fr_check_allow_attr_3321gm7 {
    gm_frame h;
    gm_tc3oht4ksth9d l5; /* a */
    gm_b0_1 ret;
};
struct gm_fr_check_method_candidates_k08rvu6 {
    gm_frame h;
    gm_t4c7ipjj9s3fp l1; /* seen */
    gm_t79rbdv4rafe5 l4; /* c */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_b0_1 ret;
};
struct gm_fr_check_pub_signatures_plkds6r {
    gm_frame h;
    gm_t6qooom8qa2do l6; /* v */
    gm_trqfvmjtt4sij l7; /* tys */
    gm_t5v0c8ia6a5mo l12; /* p */
    gm_tv0k2vg3054a6 l16; /* op */
    gm_t5v0c8ia6a5mo l19; /* p */
    gm_b0_1 ret;
};
struct gm_fr_clear_ufjejrf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_complex_of_kh32tib {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_compute_binding_cl3ogob {
    gm_frame h;
    gm_t4gut2c9fj1kt l5; /* e */
    gm_t5vbkdq81qfs7 l17; /* r */
    gm_t0ialelq3ql1k l18; /* v */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_compute_decl_gcf8mq9 {
    gm_frame h;
    gm_t52j636u3vgk3 l2; /* decl */
    gm_tvpnktv5fo8nl l3; /* item */
    gm_tv2uf4leil5hq l6; /* b */
    gm_tcus0chk5r9uq l10; /* hints */
    gm_tcus0chk5r9uq l13; /* align_attr */
    gm_t1s75i08qtptd l14; /* export */
    gm_tkacitepg5fk0 l19; /* attr */
    gm_t5vbkdq81qfs7 l22; /* r */
    gm_tbci5ospk5nnn l23; /* i */
    gm_tkacitepg5fk0 l34; /* a */
    gm_t6qooom8qa2do l35; /* v */
    gm_tkacitepg5fk0 l40; /* h */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_const_id_2mlh0og {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_declare_adt_iefkbkk {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_declare_const_or_type_4jaj4k6 {
    gm_frame h;
    gm_t6qooom8qa2do l7; /* v */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_declare_method_candidate_e20bo1i {
    gm_frame h;
    gm_tpautr4br7h3l l7; /* def */
    gm_t0mglksrkfa5e l10; /* f */
    gm_b0_1 ret;
};
struct gm_fr_def_of_m512sbu {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_effect_arg_tys_2a7r8hh {
    gm_frame h;
    gm_toa2vhe3l4hfr l8; /* a */
    gm_b0_1 ret;
};
struct gm_fr_effect_terms_bphj7th {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1fu8oa9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1mrcnd6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1scem7m {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_2k4643t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7adh53f {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_7od16il {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8nf04m8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_95isii0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_9pqhpnm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_a6or8fm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c3qnk0a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_cp9r5v5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d0lhrvs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d3uqusp {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dq2isu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_e7jr93v {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_eoluluh {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f1ngfj0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f6g91fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fh7svmj {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g6fvoq5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hii1vte {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hp0e9gl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ipqblac {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_iqgue92 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_j0utaj4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_j6dmic1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_j98q5u1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jpu5akn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_kgfehvr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_len0sng {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lh8ov0e {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lp0ck0u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_lpero57 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_mbkocse {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_nqsj3l3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oqjc4ke {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_osbc35j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ot5ttb7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_p6mn4us {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_pu15hdv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_q870hn3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qmcc4fe {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s6l2ctu {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_sc53cu2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_shtdeku {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tslvjbf {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_uq9fi1n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_v0dtga2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_field_default_expr_tqfuiff {
    gm_frame h;
    void * ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_bfunt71 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_hcjjkrm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_repdalk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_0dvj28f {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_28p84ds {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ck7vger {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e79cvtl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h4jcodi {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j4sks15 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_slm5ovr {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t5fmuah {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_u2mas3v {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v0s3sc7 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_frame_up_h0ddp1d {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_generic_of_1861hm9 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_get_espvf96 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_get_pk8ncss {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_get_qjsv97t {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hash_map_0vd239u {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_1so3e0v {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_2dqtq4c {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_84luf3l {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_8lre197 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_gsp2k0o {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_iibum07 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_iug9p19 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_t86lciv {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_ve01i1t {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hidden_part_is5hgf9 {
    gm_frame h;
    void * l0; /* s */
    gm_t9qkh0rsooc8d l2; /* need */
    gm_trcagkhgm1qdu l3; /* found */
    gm_trcagkhgm1qdu ret;
};
struct gm_fr_in_generic_scope_rvb92ab {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_ambient_m41pl1e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_positive_su660ab {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_structural_alias_qpmv4mu {
    gm_frame h;
    gm_tv2uf4leil5hq l3; /* b */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_0bg6tcp {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0bj26n7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_0npefvp {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1jabh22 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_1teief7 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_2343nn9 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_406coeg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_4kh12ib {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5pborc0 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_8r7pelg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_9q64p2g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_atkrn3m {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_au01ire {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_c02589n {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_dql1gah {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_dvbol2p {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_dvp9pla {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_eb5ua5r {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_fam8ktt {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_h93u1dq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_i6iqfhd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ib5k4gb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_je8tl46 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_kk91efj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_l3jkoh2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_m7es0sh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_mvhhv5u {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qs7fohu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qv7bq94 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_sajcold {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_t29l5ca {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_tn37ahu {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_tqcq99u {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ue2t2bm {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uun92du {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_v3cpbrb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vhqj1o4 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_non_constant_part_itk7b0v {
    gm_frame h;
    void * l0; /* s */
    gm_tkn42u7na3t81 l2; /* bad */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_pop_j4rpvm5 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_prelude_effect_gilrmm5 {
    gm_frame h;
    gm_tqu3m2l3n8mnc ret;
};
struct gm_fr_prelude_instance_arg_kh32dmf {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_prelude_template_7s9nsp0 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_0fe09r3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_4p3ivmb {
    gm_frame h;
    gm_tq7qjpkmqmc5s l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_89d0k90 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_8cr0sgp {
    gm_frame h;
    gm_tjcptv9sagfq8 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_field_tys_v4ep4t3 {
    gm_frame h;
    gm_t0mglksrkfa5e l5; /* f */
    gm_b0_1 ret;
};
struct gm_fr_push_fomabnf {
    gm_frame h;
    gm_t79rbdv4rafe5 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_h37silv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kpjuj6c {
    gm_frame h;
    gm_tagrkbqh054h7 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_puil3ql {
    gm_frame h;
    gm_tammvh1iespi3 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_range_of_dmmo768 {
    gm_frame h;
    gm_thetuhtouve7f ret;
};
struct gm_fr_range_template_dbt7218 {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_15b7omr {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4s59dpv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_5jtvbud {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8fvt97b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_c536n99 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_iloqstk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4lehgc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_tk5tca7 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_cycle_mauubd4 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* names */
    gm_t4gut2c9fj1kt l9; /* d */
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_ambient_6orl0vo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_decl_pus042v {
    gm_frame h;
    gm_t6qooom8qa2do l2; /* v */
    gm_t6qooom8qa2do l3; /* v */
    gm_trqfvmjtt4sij l6; /* type_params */
    gm_trqfvmjtt4sij l7; /* value_scope */
    gm_trqfvmjtt4sij l8; /* local_consts */
    gm_trqfvmjtt4sij l9; /* local_fns */
    gm_t6qooom8qa2do l10; /* v */
    gm_t6qooom8qa2do l11; /* r */
    gm_t6qooom8qa2do ret;
};
struct gm_fr_resolve_declarations_vrpg1fl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_sema_r1nlnnm {
    gm_frame h;
    gm_tfbjaprcetfe7 l1; /* s */
    gm_tfbjaprcetfe7 ret;
};
struct gm_fr_set_inst_def_tcpabg0 {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_b0_1 ret;
};
struct gm_fr_set_state_3j300fi {
    gm_frame h;
    gm_tagrkbqh054h7 l2; /* v */
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
struct gm_fr_state_a5bnsjb {
    gm_frame h;
    gm_tagrkbqh054h7 ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
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
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unsupported_t7lgvp9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_walk_v858co7 {
    gm_frame h;
    void * l0; /* s */
    gm_closure l2; /* f */
    gm_t53at80pfdus3 l3; /* k */
    gm_b0_1 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b);
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint32_t gm_f_add_const_8u8b1ja(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_adt_field_tys_oggq5ji(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2, void * a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_aliased_decl_jv6ilmn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_apply_callconv_197l87p(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint8_t a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_attr_power_of_two_gj02n2t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_callconv_attr_dd08ovc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_allow_attr_3321gm7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_import_cycles_2u7oqo0(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_method_candidates_k08rvu6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_pub_signatures_plkds6r(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_clear_ufjejrf(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_collect_terms_n8okg7m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_compute_binding_cl3ogob(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_compute_decl_gcf8mq9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_check_d48mnu7(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_const_type_7rijs95(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_in_74tov03(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4, uint8_t a5);
__attribute__((aligned(4))) void gm_f_declare_const_or_type_4jaj4k6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_declare_effect_9n12hc8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4);
__attribute__((aligned(4))) void gm_f_declare_fn_6piolnr(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_declare_foreign_6s86jje(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_declare_global_729nttf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_method_candidate_e20bo1i(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tdgfduqjheqdm gm_f_def_of_m512sbu_c(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_deinit_2l7c12h(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_arg_tys_2a7r8hh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1mrcnd6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1scem7m(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2k4643t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7adh53f(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7od16il(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8nf04m8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_95isii0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_9pqhpnm(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_a6or8fm(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cp9r5v5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0lhrvs(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dq2isu2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e7jr93v(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_eoluluh(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f1ngfj0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g6fvoq5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hii1vte(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hp0e9gl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ipqblac(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_iqgue92(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j0utaj4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j6dmic1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j98q5u1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jpu5akn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kgfehvr(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_len0sng(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lh8ov0e(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lp0ck0u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpero57(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mbkocse(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nqsj3l3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oqjc4ke(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_osbc35j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ot5ttb7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p6mn4us(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pu15hdv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q870hn3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s6l2ctu(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_sc53cu2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_shtdeku(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tslvjbf(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uq9fi1n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_v0dtga2(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_export_fn_8hcuasc(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) void * gm_f_field_default_expr_tqfuiff(void * a0, uint32_t a1, uint64_t a2);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_8gjlcrm(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_9qkg6n9(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ovslcai(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_hcjjkrm(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_repdalk(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_for_each_component_o5cuh7c(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_format_0dvj28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_28p84ds(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ck7vger(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_j4sks15(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mccr87v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_slm5ovr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_t5fmuah(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_u2mas3v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_v0s3sc7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_frame_up_h0ddp1d(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_get_mk00hfd(void *gm_out, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1);
inline __attribute__((aligned(4))) gm_toceffb697p0u gm_f_get_qjsv97t(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_has_splice_80pgqas(gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_hash_map_0vd239u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_1so3e0v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_2dqtq4c(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_84luf3l(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_8lre197(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_gsp2k0o(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_iibum07(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_iug9p19(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_t86lciv(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_ve01i1t(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_trcagkhgm1qdu gm_f_hidden_part_is5hgf9(void * a0, gm_csguankko4lmhd a1, gm_t9qkh0rsooc8d a2);
__attribute__((aligned(4))) uint8_t gm_f_in_generic_scope_rvb92ab(void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_s9trr09(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_ambient_m41pl1e(void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_effect_sum_dd9le16(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_positive_su660ab(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_structural_alias_qpmv4mu(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_0bg6tcp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_0npefvp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_1teief7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_2343nn9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_406coeg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_4kh12ib(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5pborc0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_8r7pelg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_9q64p2g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_atkrn3m(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_au01ire(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_c02589n(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_dql1gah(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_dvbol2p(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_dvp9pla(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_eb5ua5r(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_fam8ktt(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_h93u1dq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_i6iqfhd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ib5k4gb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_je8tl46(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_kk91efj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_l3jkoh2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_m7es0sh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_mvhhv5u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qs7fohu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qv7bq94(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_sajcold(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_t29l5ca(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_tn37ahu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_tqcq99u(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ue2t2bm(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uun92du(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_v3cpbrb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vhqj1o4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_looks_like_type_ho5qv1k(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_member_5fd2qm9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, uint64_t a3);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_module_named_n1mcl3b(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_non_constant_part_itk7b0v(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0);
__attribute__((aligned(4))) void gm_f_prelude_effect_gilrmm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_prelude_template_7s9nsp0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0fe09r3(void * a0, gm_tbcgm40aljf9h a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_4p3ivmb(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_89d0k90(void * a0, gm_csedogq9e4rp5r a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_8cr0sgp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_field_tys_v4ep4t3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_fomabnf(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h37silv(void * a0, gm_t3jsf43p1pths a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kpjuj6c(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_puil3ql(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3pvr70r(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_pjhbiaq(void * a0, const void * a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_qrf6f79(void * a0, gm_trlabqsjjp0ds a1, uint32_t a2);
__attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_range_template_dbt7218(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_15b7omr(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4s59dpv(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5jtvbud(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8fvt97b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_c536n99(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_iloqstk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4lehgc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_tk5tca7(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint64_t gm_f_remove_om8q0q2(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_cycle_mauubd4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_ambient_6orl0vo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_declarations_vrpg1fl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_resolve_effect_ref_bggirs2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_resolve_effect_set_o7rc5od(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_import_944uqa9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_sema_r1nlnnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_trap_hook_fn_5c0a4ds(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_types_8a61ga3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_utf8_valid_a8oc1t8(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2);
extern uint8_t gm_c0u09tvsmdv[1] __attribute__((aligned(1)));
extern uint8_t gm_c1n574q3spi[8] __attribute__((aligned(8)));
extern uint8_t gm_c3t36dlh4g5[16] __attribute__((aligned(8)));
extern uint8_t gm_cbbjgr0cpr9[8] __attribute__((aligned(8)));
extern uint8_t gm_cp0okt01b9q[192] __attribute__((aligned(8)));
extern const uint8_t gm_s0rphlcbno91v[8];
extern const uint8_t gm_s1ft2nrvnhcbp[10];
extern const uint8_t gm_s2ph9f020upd2[7];
extern const uint8_t gm_s2urait735ofq[5];
extern const uint8_t gm_s43cq4krigs2b[10];
extern const uint8_t gm_s4eilu97sooh1[10];
extern const uint8_t gm_s523ivho5m3dq[13];
extern const uint8_t gm_s58b2tdsrfkg1[85];
extern const uint8_t gm_s68uiiatavvqh[18];
extern const uint8_t gm_s69d1mv9hu0dg[42];
extern const uint8_t gm_s6ibuqseh9tt7[152];
extern const uint8_t gm_s6lnp3h2ojl1f[77];
extern const uint8_t gm_s6vvtrmtnt3j1[5];
extern const uint8_t gm_s7f3pdsji9g3a[9];
extern const uint8_t gm_s80p0ld1j6rfg[10];
extern const uint8_t gm_s8n5rdam5o1sm[69];
extern const uint8_t gm_s8ro53eq54s60[44];
extern const uint8_t gm_s9e3360r5ee0o[7];
extern const uint8_t gm_s9ocqto6ckbos[58];
extern const uint8_t gm_sap32k6of7kh3[66];
extern const uint8_t gm_sbqv06vnui8sl[88];
extern const uint8_t gm_sbrnv8n0dpinb[8];
extern const uint8_t gm_scacg345bm166[69];
extern const uint8_t gm_scllp0lpbljde[15];
extern const uint8_t gm_sdbvaa960ee0f[5];
extern const uint8_t gm_seidpmiq51gtp[60];
extern const uint8_t gm_ses7tdrdt8a6u[6];
extern const uint8_t gm_seue7mq07v0oc[6];
extern const uint8_t gm_sg7i6m1frjk5n[63];
extern const uint8_t gm_shge5n743ua4h[3];
extern const uint8_t gm_shofa8pdml9fb[13];
extern const uint8_t gm_shud0j6jb8ucc[37];
extern const uint8_t gm_sig8ahbvm8eb8[10];
extern const uint8_t gm_sinoph5a0op58[10];
extern const uint8_t gm_sj1k4t2de5144[6];
extern const uint8_t gm_sj2f7h0km68ut[32];
extern const uint8_t gm_sj3lfuqd4f1np[37];
extern const uint8_t gm_sjguhtmsnuhlj[58];
extern const uint8_t gm_sjqs7q3ostqbm[4];
extern const uint8_t gm_slb6cj7ndgvdm[6];
extern const uint8_t gm_slff7lshtgu6r[42];
extern const uint8_t gm_smo0niiqr9onn[11];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_soo5q2bffam33[33];
extern const uint8_t gm_spe8t3e79vnal[7];
extern const uint8_t gm_sppcsr92ukfol[64];
extern const uint8_t gm_spq5ja4c7ciga[11];
extern const uint8_t gm_sptnm8c6c52dr[7];
extern const uint8_t gm_spvg0rt0dloah[9];
extern const uint8_t gm_sq4sf3euttrc8[5];
extern const uint8_t gm_sqa5rf17c57e4[5];
extern const uint8_t gm_srb1158jlk9u7[75];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srjo6vku5gka5[20];
extern const uint8_t gm_srsiesc4al3v4[50];
extern const uint8_t gm_ss6g2koqn63bf[5];
extern const uint8_t gm_ssbb728kngg91[79];
extern const uint8_t gm_st027n235d4us[45];
extern const uint8_t gm_st6dd0k5nckh3[58];
extern const uint8_t gm_stjei6jfiba56[5];
extern const uint8_t gm_str26682vod5l[38];
extern const uint8_t gm_suuv0qpi4v4e4[8];
extern const uint8_t gm_sv48vll3qortv[40];
extern const gm_tmpl gm_tpl0stjtt2gdn50;
extern const gm_tmpl gm_tplei9v853i04sp;
extern const gm_tmpl gm_tplet6obcbkjbht;
extern const gm_tmpl gm_tplfe53mjoakni4;
extern const gm_tmpl gm_tplg45jfs21ci2c;
extern const gm_tmpl gm_tpll8q455cp9mhi;
extern const gm_tmpl gm_tplm0bsttjtt8b0;
extern const gm_tmpl gm_tplpdnopu1p8235;
extern const gm_tmpl gm_tplsbqgr9k4itq0;
extern const gm_tmpl gm_tpltb9h1t5060dp;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b) {
    if (a[8] != b[8]) return false;
    return !a[8] || gm_eq6groq37ram(a, b);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_name_h7mjik7 F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 136));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 136) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 268, 59);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t3) + (uintptr_t)t6 * 16));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_clear_ufjejrf(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_clear_ufjejrf F;
    void * gm_l0; /* l */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
    if (t6 > t7 || t7 > (uintptr_t)t5) gm_trap_slice(t6, t7, t5, gm_srcrgc9bkquf6nr, 140, 15);
    uint64_t t8 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8));
    GM_ST(gm_b16_8, ((uint8_t *)t2), t9);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_complex_of_kh32tib F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s0rphlcbno91v));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(7));
    gm_tkn42u7na3t81 t5 = gm_f_prelude_instance_arg_kh32dmf((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4) {
    struct gm_fr_declare_adt_iefkbkk F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_tf5srh4mp4b3i gm_l2; /* decl */
    gm_b16_8 gm_l3; /* name */
    void * gm_l4; /* lit */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    gm_tf5srh4mp4b3i t4 = gm_l2;
    gm_b16_8 t5 = gm_l3;
    void * t6 = gm_l4;
    gm_t88rcucg1b7uk t7 = gm_f_declare_adt_in_74tov03((void *)NULL, cx, NULL, t2, t3, t4, t5, t6, ((uint8_t)0));
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
inline __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_def_of_m512sbu F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 141u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tdn4erebgflsv gm_l3;
    {
        {
            {
                void * t4 = gm_l0;
                void * t5 = (void *)(((uint8_t *)t4) + 2184);
                uint32_t t6 = gm_l1;
                gm_f_get_mk00hfd((void *)&gm_l3, t5, t6);
            }
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[64] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l3), 64);
            }
            {
                memmove(gm_out, ((uint8_t *)&F.l2), 64);
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 384));
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t8) + 384) + 8));
    uint32_t t11 = gm_l1;
    uint64_t t12 = (uint64_t)t11;
    if ((uintptr_t)t12 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t12, t10, gm_srclv0dcbbfhves, 589, 12);
    memmove(gm_out, (((uint8_t *)t9) + (uintptr_t)t12 * 64), 64);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_terms_bphj7th(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_effect_terms_bphj7th F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_trqfvmjtt4sij t2;
    gm_f_list_qs7fohu((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t2;
    void * t3 = gm_l0;
    void * t4 = (void *)((uint8_t *)&F.l1);
    gm_b0_1 t5 = gm_f_collect_terms_n8okg7m((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1fu8oa9 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 64)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1mrcnd6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1mrcnd6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1scem7m(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1scem7m F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2k4643t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_2k4643t F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73v4ttb F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7adh53f(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7adh53f F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_7od16il(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_7od16il F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 96)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8nf04m8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8nf04m8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_95isii0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_95isii0 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 32)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_9pqhpnm(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_9pqhpnm F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_a6or8fm(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_a6or8fm F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 20)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c3qnk0a F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_cp9r5v5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_cp9r5v5 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0lhrvs(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d0lhrvs F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d3uqusp(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d3uqusp F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 28)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dq2isu2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dq2isu2 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_e7jr93v(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_e7jr93v F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_eoluluh(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_eoluluh F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f1ngfj0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f1ngfj0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f6g91fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_f6g91fe F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 224)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fh7svmj F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g6fvoq5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g6fvoq5 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hii1vte(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hii1vte F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 64)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hp0e9gl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hp0e9gl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ipqblac(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ipqblac F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 216)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_iqgue92(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_iqgue92 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j0utaj4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_j0utaj4 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j6dmic1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_j6dmic1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_j98q5u1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_j98q5u1 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jpu5akn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jpu5akn F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_kgfehvr(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_kgfehvr F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_len0sng(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_len0sng F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lh8ov0e(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lh8ov0e F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 72)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lp0ck0u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lp0ck0u F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_lpero57(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_lpero57 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mbkocse(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_mbkocse F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_nqsj3l3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_nqsj3l3 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 104)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oqjc4ke(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oqjc4ke F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_osbc35j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_osbc35j F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ot5ttb7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ot5ttb7 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 136)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_p6mn4us(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_p6mn4us F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pu15hdv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_pu15hdv F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 20)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_q870hn3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_q870hn3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qmcc4fe(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qmcc4fe F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 12)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s6l2ctu(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s6l2ctu F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_sc53cu2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_sc53cu2 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 12)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_shtdeku(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_shtdeku F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tslvjbf(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tslvjbf F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_uq9fi1n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_uq9fi1n F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_v0dtga2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_v0dtga2 F;
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
inline __attribute__((aligned(4))) void * gm_f_field_default_expr_tqfuiff(void * a0, uint32_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_field_default_expr_tqfuiff F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    uint64_t gm_l2; /* i */
    gm_b16_8 gm_l3; /* exprs */
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 1088));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1088) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srclv0dcbbfhves, 716, 14);
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)t3) + (uintptr_t)t6 * 184) + 96));
    gm_l3 = t7;
    uint64_t t8 = gm_l2;
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t10 = (t8 >= t9);
    if (t10) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t11 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t13 = gm_l2;
    void * t14 = GM_LD(void *, (((uint8_t *)t11) + (uintptr_t)t13 * 8));
    F.ret = t14;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_bfunt71 F;
    gm_t88rcucg1b7uk gm_l0; /* c */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_walk_v858co7 *)((&F.h)->up))->l0));
    gm_t88rcucg1b7uk t3 = gm_l0;
    gm_closure t4 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_walk_v858co7 *)((&F.h)->up))->l2));
    gm_f_walk_v858co7(t2, GM_CONV(gm_csguankko4lmhd, t3), t4);
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
inline __attribute__((aligned(4))) void gm_f_format_0dvj28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_0dvj28f F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sap32k6of7kh3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(65));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplet6obcbkjbht), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_28p84ds(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_28p84ds F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s69d1mv9hu0dg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplg45jfs21ci2c), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ck7vger(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ck7vger F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s58b2tdsrfkg1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(84));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpltb9h1t5060dp), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_e79cvtl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_e79cvtl F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sig8ahbvm8eb8));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpll8q455cp9mhi), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_h4jcodi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_h4jcodi F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqa5rf17c57e4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsbqgr9k4itq0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_j4sks15(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_j4sks15 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sjguhtmsnuhlj));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(57));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplm0bsttjtt8b0), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_slm5ovr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_slm5ovr F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slff7lshtgu6r));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
    gm_f_format_mccr87v((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfe53mjoakni4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_t5fmuah(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t5fmuah F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_seidpmiq51gtp));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(59));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplpdnopu1p8235), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_u2mas3v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_u2mas3v F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soo5q2bffam33));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl0stjtt2gdn50), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_v0s3sc7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_v0s3sc7 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ssbb728kngg91));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(78));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplei9v853i04sp), t2);
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
inline __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_generic_of_1861hm9 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tiilmfd16v3fc gm_l2; /* inst */
    GM_STACK_CHECK_SHALLOW(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, (((uint8_t *)t4) + 344));
            uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t4) + 344) + 8));
            uint32_t t7 = gm_l1;
            uint64_t t8 = (uint64_t)t7;
            if ((uintptr_t)t8 >= (uintptr_t)(t6)) gm_trap_index((uintptr_t)t8, t6, gm_srclv0dcbbfhves, 598, 16);
            uint8_t t9 = (uint8_t)((((((uint8_t *)t5) + (uintptr_t)t8 * 136) + 96))[24] != 0);
            if (!t9) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l2), ((((uint8_t *)t5) + (uintptr_t)t8 * 136) + 96), 24);
            uint32_t t10 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
            F.ret = t10;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    uint32_t t11 = gm_l1;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) gm_toceffb697p0u gm_f_get_qjsv97t(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_qjsv97t F;
    void * gm_l0; /* m */
    gm_tiilmfd16v3fc gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 205u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_9qkg6n9(t2, t3);
    gm_l3 = t4;
    uint8_t t5 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
        gm_l2 = t6;
    } else {
        gm_toceffb697p0u t7;
        memset(&t7, 0, sizeof t7);
        F.ret = t7;
        goto gm_fret;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t8) + 8));
    uint64_t t11 = gm_l2;
    if ((uintptr_t)t11 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t11, t10, gm_srcbsc23is03ttm, 139, 12);
    gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t9) + (uintptr_t)t11 * 48) + 32));
    gm_toceffb697p0u t13;
    GM_ST(uint8_t, (uint8_t *)&t13 + 13, 0);
    GM_ST(uint16_t, (uint8_t *)&t13 + 14, 0);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t13), t12);
    ((uint8_t *)&t13)[12] = 1;
    F.ret = t13;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_param_u9t0efs F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 40));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 40) + 8));
    uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 249, 57);
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
    uint8_t t8 = (uint8_t)(t7 & ((uint8_t)1ULL));
    uint8_t t9 = (!(t8 == ((uint8_t)0ULL)));
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_hash_map_0vd239u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_0vd239u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_j98q5u1();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40), t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_1so3e0v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_1so3e0v F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_95isii0();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40), t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_2dqtq4c(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_2dqtq4c F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_len0sng();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40), t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_84luf3l(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_84luf3l F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jpu5akn();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40), t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_8lre197(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_8lre197 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_sc53cu2();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40), t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_gsp2k0o(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_gsp2k0o F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_7adh53f();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40), t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_iibum07(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_iibum07 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lh8ov0e();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40), t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_iug9p19(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_iug9p19 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_mbkocse();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40), t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_t86lciv(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_t86lciv F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_9pqhpnm();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40), t4);
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
inline __attribute__((aligned(4))) void gm_f_hash_map_ve01i1t(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_ve01i1t F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_pu15hdv();
    gm_node *pn3 = gm_find(cx, 587034433);
    if (!pn3) gm_trap("no handler installed for effect `Alloc`", gm_srcbsc23is03ttm, 35, 96);
    gm_t3mf58gv9kf3n t4;
    t4 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn3->clauses[0])(pn3->env, pn3->hctx, pn3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 16), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 24), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 32), ((uint64_t)11400714819323198485ULL));
    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&(*(gm_t4c7ipjj9s3fp *)gm_out) + 40), t4);
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
inline __attribute__((aligned(4))) gm_trcagkhgm1qdu gm_f_hidden_part_is5hgf9(void * a0, gm_csguankko4lmhd a1, gm_t9qkh0rsooc8d a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_hidden_part_is5hgf9 F;
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    F.l2 = a2;
    gm_trcagkhgm1qdu t2;
    memset(&t2, 0, sizeof t2);
    F.l3 = t2;
    void * t3 = F.l0;
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_closure t5 = ((gm_closure){(void *)gm_f_fn_repdalk, (void *)&F.h, cx});
    gm_f_walk_v858co7(t3, GM_CONV(gm_csguankko4lmhd, t4), t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trcagkhgm1qdu t6 = F.l3;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_positive_su660ab(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_positive_su660ab F;
    GM_STACK_CHECK_SHALLOW(&F, 15u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        gm_l1 = t5;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t6 = gm_l1;
    F.ret = t6;
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
inline __attribute__((aligned(4))) void gm_f_list_0bg6tcp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0bg6tcp F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_kgfehvr();
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
inline __attribute__((aligned(4))) void gm_f_list_0bj26n7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0bj26n7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qmcc4fe();
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
inline __attribute__((aligned(4))) void gm_f_list_0npefvp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0npefvp F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_dq2isu2();
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
inline __attribute__((aligned(4))) void gm_f_list_1jabh22(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1jabh22 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_f6g91fe();
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
inline __attribute__((aligned(4))) void gm_f_list_1teief7(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_1teief7 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_1mrcnd6();
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
inline __attribute__((aligned(4))) void gm_f_list_2343nn9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_2343nn9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_eoluluh();
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
inline __attribute__((aligned(4))) void gm_f_list_406coeg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_406coeg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_j0utaj4();
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
inline __attribute__((aligned(4))) void gm_f_list_4kh12ib(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_4kh12ib F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_s6l2ctu();
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
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5dhgg4i F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_73v4ttb();
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
inline __attribute__((aligned(4))) void gm_f_list_5pborc0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5pborc0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_nqsj3l3();
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
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7s4om10 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_r81t436();
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
inline __attribute__((aligned(4))) void gm_f_list_8r7pelg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_8r7pelg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_a6or8fm();
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
inline __attribute__((aligned(4))) void gm_f_list_9q64p2g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_9q64p2g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_v0dtga2();
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
inline __attribute__((aligned(4))) void gm_f_list_atkrn3m(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_atkrn3m F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_g6fvoq5();
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
inline __attribute__((aligned(4))) void gm_f_list_au01ire(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_au01ire F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_f1ngfj0();
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
inline __attribute__((aligned(4))) void gm_f_list_c02589n(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_c02589n F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_e7jr93v();
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
inline __attribute__((aligned(4))) void gm_f_list_dql1gah(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_dql1gah F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_cp9r5v5();
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
inline __attribute__((aligned(4))) void gm_f_list_dvbol2p(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_dvbol2p F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_1fu8oa9();
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
inline __attribute__((aligned(4))) void gm_f_list_dvp9pla(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_dvp9pla F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ot5ttb7();
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
inline __attribute__((aligned(4))) void gm_f_list_eb5ua5r(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_eb5ua5r F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ipqblac();
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
inline __attribute__((aligned(4))) void gm_f_list_fam8ktt(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_fam8ktt F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_iqgue92();
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
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_go2qg0b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_d914i5u();
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
inline __attribute__((aligned(4))) void gm_f_list_h93u1dq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_h93u1dq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_oqjc4ke();
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
inline __attribute__((aligned(4))) void gm_f_list_i6iqfhd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_i6iqfhd F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lpero57();
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
inline __attribute__((aligned(4))) void gm_f_list_ib5k4gb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ib5k4gb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_d3uqusp();
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
inline __attribute__((aligned(4))) void gm_f_list_je8tl46(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_je8tl46 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_shtdeku();
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
inline __attribute__((aligned(4))) void gm_f_list_kk91efj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_kk91efj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_hp0e9gl();
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
inline __attribute__((aligned(4))) void gm_f_list_l3jkoh2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_l3jkoh2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8nf04m8();
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
inline __attribute__((aligned(4))) void gm_f_list_m7es0sh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_m7es0sh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_c3qnk0a();
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
inline __attribute__((aligned(4))) void gm_f_list_mvhhv5u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_mvhhv5u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_osbc35j();
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
inline __attribute__((aligned(4))) void gm_f_list_qs7fohu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qs7fohu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_lp0ck0u();
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
inline __attribute__((aligned(4))) void gm_f_list_qv7bq94(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qv7bq94 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_hii1vte();
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
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s83jn2q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_s1lbe5s();
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
inline __attribute__((aligned(4))) void gm_f_list_sajcold(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_sajcold F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_tslvjbf();
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
inline __attribute__((aligned(4))) void gm_f_list_t29l5ca(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_t29l5ca F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_2k4643t();
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
inline __attribute__((aligned(4))) void gm_f_list_tn37ahu(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_tn37ahu F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_q870hn3();
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
inline __attribute__((aligned(4))) void gm_f_list_tqcq99u(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_tqcq99u F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_j6dmic1();
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
inline __attribute__((aligned(4))) void gm_f_list_ue2t2bm(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ue2t2bm F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_d0lhrvs();
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
inline __attribute__((aligned(4))) void gm_f_list_uun92du(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uun92du F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_uq9fi1n();
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
inline __attribute__((aligned(4))) void gm_f_list_v3cpbrb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_v3cpbrb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_1scem7m();
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
inline __attribute__((aligned(4))) void gm_f_list_vhqj1o4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vhqj1o4 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_7od16il();
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_non_constant_part_itk7b0v(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_non_constant_part_itk7b0v F;
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_tkn42u7na3t81 t2;
    memset(&t2, 0, sizeof t2);
    F.l2 = t2;
    void * t3 = F.l0;
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_closure t5 = ((gm_closure){(void *)gm_f_fn_hcjjkrm, (void *)&F.h, cx});
    gm_f_walk_v858co7(t3, GM_CONV(gm_csguankko4lmhd, t4), t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tkn42u7na3t81 t6 = F.l2;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tf5srh4mp4b3i gm_f_pop_j4rpvm5(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_j4rpvm5 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* x */
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
        gm_tf5srh4mp4b3i t6;
        memset(&t6, 0, sizeof t6);
        F.ret = t6;
        goto gm_fret;
    }
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, ((uint8_t *)t7));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint64_t t10 = gm_l1;
    uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
    if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
    uint64_t t13 = r11;
    if ((uintptr_t)t13 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t13, t9, gm_srcrgc9bkquf6nr, 133, 10);
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t8) + (uintptr_t)t13 * 8));
    gm_l2 = t14;
    void * t15 = gm_l0;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t16) + 8));
    uint64_t t19 = gm_l1;
    uint64_t r20; bool o21 = __builtin_sub_overflow(t19, ((uint64_t)1ULL), &r20);
    if (o21) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t22 = r20;
    uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t24 = (uintptr_t)t22;
    if (t23 > t24 || t24 > (uintptr_t)t18) gm_trap_slice(t23, t24, t18, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t25 = ((uintptr_t)t22) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t15), t26);
    uint64_t t27 = gm_l2;
    gm_tf5srh4mp4b3i t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t28 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[8] = 1;
    F.ret = t28;
    goto gm_fret;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0fe09r3(void * a0, gm_tbcgm40aljf9h a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0fe09r3 F;
    void * gm_l0; /* l */
    gm_tbcgm40aljf9h gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_uq9fi1n();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_c536n99(t8, ((uint64_t)1ULL), t10);
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
    gm_tbcgm40aljf9h t18 = gm_l1;
    GM_ST(gm_tbcgm40aljf9h, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 8)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_4p3ivmb(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_4p3ivmb F;
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
        gm_b16_8 t9 = gm_f_empty_slice_nqsj3l3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_tk5tca7(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 104), ((uint8_t *)a1), 104);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 104)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_89d0k90(void * a0, gm_csedogq9e4rp5r a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_89d0k90 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_j6dmic1();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_5jtvbud(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_8cr0sgp(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_8cr0sgp F;
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
        gm_b16_8 t9 = gm_f_empty_slice_8nf04m8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_15b7omr(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 88), ((uint8_t *)a1), 88);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_fomabnf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_fomabnf F;
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
        gm_b16_8 t9 = gm_f_empty_slice_1scem7m();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_4s59dpv(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 40), ((uint8_t *)a1), 40);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h37silv(void * a0, gm_t3jsf43p1pths a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_h37silv F;
    void * gm_l0; /* l */
    gm_t3jsf43p1pths gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_iqgue92();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8fvt97b(t8, ((uint64_t)1ULL), t10);
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
    gm_t3jsf43p1pths t18 = gm_l1;
    GM_ST(gm_t3jsf43p1pths, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 8)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kpjuj6c(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kpjuj6c F;
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
        gm_b16_8 t9 = gm_f_empty_slice_ipqblac();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_j4lehgc(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 216), ((uint8_t *)&F.l1), 216);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 216)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_puil3ql(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_puil3ql F;
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
        gm_b16_8 t9 = gm_f_empty_slice_eoluluh();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_iloqstk(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ucgmlo2 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_73v4ttb();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_k4j2gj3(t8, ((uint64_t)1ULL), t10);
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
    uint64_t t18 = gm_l1;
    GM_ST(uint64_t, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 8)));
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_range_template_dbt7218(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1) {
    struct gm_fr_range_template_dbt7218 F;
    void * gm_l0; /* s */
    uint8_t gm_l1; /* inclusive */
    GM_STACK_CHECK(&F, 106u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 gm_l2;
    void * t2 = gm_l0;
    uint8_t t3 = gm_l1;
    if (t3) {
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s43cq4krigs2b));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(9));
        gm_l2 = t4;
    } else {
        gm_b16_8 t5;
        GM_ST(void *, (uint8_t *)&t5, (void *)(gm_seue7mq07v0oc));
        GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(5));
        gm_l2 = t5;
    }
    gm_b16_8 t6 = gm_l2;
    gm_t9805qtonbsag t7 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t2, t6);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_15b7omr(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_15b7omr F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4s59dpv(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4s59dpv F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)40ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_5jtvbud(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_5jtvbud F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8fvt97b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8fvt97b F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_c536n99(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_c536n99 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_iloqstk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_iloqstk F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4lehgc(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j4lehgc F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)216ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_k4j2gj3 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_tk5tca7(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_tk5tca7 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)104ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2) {
    struct gm_fr_set_inst_def_tcpabg0 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 37u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 2184);
    uint32_t t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_put_3pvr70r(t3, t4, &(*(gm_tdgfduqjheqdm *)((uint8_t *)a2)));
    gm_l3 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srclv0dcbbfhves, 615, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2) {
    struct gm_fr_set_state_3j300fi F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_tagrkbqh054h7 t4;
        gm_f_state_a5bnsjb((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, (((uint8_t *)t5) + 304));
        uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)t5) + 304) + 8));
        uint64_t t8 = gm_l1;
        if ((uintptr_t)t8 >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)t8, t7, gm_srclv0dcbbfhves, 760, 5);
        memmove((((uint8_t *)t6) + (uintptr_t)t8 * 216), ((uint8_t *)&F.l2), 216);
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
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_template_9o92cvv F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 176));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 176) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 276, 56);
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)t3) + (uintptr_t)t6 * 4));
    F.ret = t7;
    goto gm_fret;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_fdrfe6i F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_f_trim_start_179crak(t2);
    gm_b16_8 t4 = gm_f_trim_end_6sjv6rg(t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_walk_v858co7 F;
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 172u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    F.l2 = a2;
    gm_closure t2 = F.l2;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_closure t4 = t2;
    gm_node *ccx5 = t4.cx;
    void * t6 = t4.code;
    gm_b0_1 t7;
    if (GM_PTR_TAGGED(t6)) {
        ((void (*)(gm_csguankko4lmhd))GM_PTR_UNTAG(t6))(GM_CONV(gm_csguankko4lmhd, t3));
    } else {
        t7 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_t88rcucg1b7uk))t6)(t4.env, ccx5, NULL, t3);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t8 = F.l0;
    void * t9 = (void *)(((uint8_t *)t8) + 8);
    gm_t88rcucg1b7uk t10 = gm_l1;
    gm_t53at80pfdus3 t11;
    gm_f_kind_jcnt4hm((void *)&t11, t9, GM_CONV(gm_csguankko4lmhd, t10));
    F.l3 = t11;
    void * t12 = (void *)((uint8_t *)&F.l3);
    gm_closure t13 = ((gm_closure){(void *)gm_f_fn_bfunt71, (void *)&F.h, cx});
    gm_f_for_each_component_o5cuh7c(t12, t13);
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
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint32_t gm_f_add_const_8u8b1ja(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_add_const_8u8b1ja F;
    void * gm_l0; /* s */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 2088) + 8));
    uint32_t t4 = (uint32_t)t3;
    gm_l2 = t4;
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 2088);
    gm_tftb1odc739t5 t7 = gm_f_push_8cr0sgp(t6, &(*(gm_tjcptv9sagfq8 *)((uint8_t *)a1)));
    gm_l3 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srclv0dcbbfhves, 624, 5);
    void * t9 = gm_l0;
    void * t10 = (void *)(((uint8_t *)t9) + 2128);
    gm_tl874t2atnlh3 t11 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 16));
    gm_t88rcucg1b7uk t12 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 28));
    gm_trlabqsjjp0ds t13;
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t13 + 0), t11);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t13 + 12), t12);
    uint32_t t14 = gm_l2;
    gm_tftb1odc739t5 t15 = gm_f_put_qrf6f79(t10, t13, t14);
    gm_l4 = t15;
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srclv0dcbbfhves, 625, 5);
    uint32_t t17 = gm_l2;
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_adt_field_tys_oggq5ji(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9qkh0rsooc8d a2, void * a3) {
    struct gm_fr_adt_field_tys_oggq5ji F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    gm_t9qkh0rsooc8d gm_l2; /* vis */
    void * gm_l3; /* tys */
    gm_b16_8 gm_l5; /* fields */
    gm_b16_8 gm_l6; /* variants */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_t88rcucg1b7uk gm_l10; /* p */
    GM_STACK_CHECK(&F, 341u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l11;
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, (((uint8_t *)t13) + 1088));
    uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 1088) + 8));
    uint32_t t16 = gm_l1;
    uint64_t t17 = (uint64_t)t16;
    if ((uintptr_t)t17 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t17, t15, gm_srclv0dcbbfhves, 1265, 11);
    {
        {
            uint8_t t18 = GM_LD(uint8_t, (((((uint8_t *)t14) + (uintptr_t)t17 * 184) + 24) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
            if (!t19) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l4), (((((uint8_t *)t14) + (uintptr_t)t17 * 184) + 24) + 8), 48);
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
            gm_t9qkh0rsooc8d t22 = gm_l2;
            void * t23 = gm_l3;
            gm_b0_1 t24 = gm_f_push_field_tys_v4ep4t3((void *)NULL, cx, NULL, t21, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t25 = GM_LD(uint8_t, (((((uint8_t *)t14) + (uintptr_t)t17 * 184) + 24) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)2ULL));
            if (!t26) {
                goto gm_lb3;
            }
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((((uint8_t *)t14) + (uintptr_t)t17 * 184) + 24) + 8));
            gm_l5 = t27;
            gm_b16_8 t28 = gm_l5;
            gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
            gm_t9qkh0rsooc8d t30 = gm_l2;
            void * t31 = gm_l3;
            gm_b0_1 t32 = gm_f_push_field_tys_v4ep4t3((void *)NULL, cx, NULL, t29, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t33 = GM_LD(uint8_t, (((((uint8_t *)t14) + (uintptr_t)t17 * 184) + 24) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)1ULL));
            if (!t34) {
                goto gm_lb4;
            }
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((((((uint8_t *)t14) + (uintptr_t)t17 * 184) + 24) + 8) + 8));
            gm_l6 = t35;
            gm_b16_8 t36 = gm_l6;
            gm_l7 = t36;
            gm_l8 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t37 = gm_l8;
                    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint8_t t39 = (t37 < t38);
                    if (!t39) {
                        goto gm_lb5;
                    }
                    {
                        void * t40 = GM_LD(void *, ((uint8_t *)&gm_l7));
                        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                        uint64_t t42 = gm_l8;
                        {
                            {
                                memmove(((uint8_t *)&F.l9), (((uint8_t *)t40) + (uintptr_t)t42 * 72), 72);
                                {
                                    {
                                        uint8_t t43 = (uint8_t)(((((uint8_t *)&F.l9) + 16))[4] != 0);
                                        if (!t43) {
                                            goto gm_lb11;
                                        }
                                        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                                        gm_l10 = t44;
                                        void * t45 = gm_l3;
                                        gm_t88rcucg1b7uk t46 = gm_l10;
                                        gm_t9qkh0rsooc8d t47 = gm_l2;
                                        gm_t3jsf43p1pths t48;
                                        GM_ST(uint8_t, (uint8_t *)&t48 + 5, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t48 + 6, 0);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t48 + 0), t46);
                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t48 + 4), t47);
                                        gm_tftb1odc739t5 t49 = gm_f_push_h37silv(t45, t48);
                                        gm_l11 = t49;
                                        uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                        if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srclv0dcbbfhves, 1269, 33);
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                }
gm_lb10:;
                                goto gm_lb8;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1268, 32);
                        }
gm_lb8:;
                    }
gm_lb7:;
                    uint64_t t51 = gm_l8;
                    uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
                    uint64_t t54 = r52;
                    gm_l8 = t54;
                }
            }
gm_lb5:;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1265, 5);
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
inline __attribute__((aligned(4))) void gm_f_aliased_decl_jv6ilmn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_aliased_decl_jv6ilmn F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* e */
    gm_t08olq97rscn3 gm_l3; /* id */
    uint64_t gm_l4; /* d */
    void * gm_l5; /* inner */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    uint64_t gm_l8; /* m */
    GM_STACK_CHECK(&F, 408u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tf5srh4mp4b3i gm_l9;
    gm_tf5srh4mp4b3i gm_l10;
    void * t7 = gm_l2;
    {
        {
            uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)6ULL));
            if (!t9) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&gm_l3), ((((uint8_t *)t7) + 8) + 8), 32);
            }
            void * t10 = gm_l0;
            uint64_t t11 = gm_l1;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
            gm_tf5srh4mp4b3i t13 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t10, t11, t12);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t13;
            uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l9))[8] != 0);
            if (t14) {
                uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                gm_l4 = t15;
            } else {
                memset(&(*(gm_t89h15e2c5o3p *)gm_out), 0, sizeof (*(gm_t89h15e2c5o3p *)gm_out));
                goto gm_fdone;
            }
            {
                uint64_t t16 = gm_l4;
                gm_tf5srh4mp4b3i t17;
                GM_ST(uint8_t, (uint8_t *)&t17 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t17 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t17 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t17), t16);
                ((uint8_t *)&t17)[8] = 1;
                gm_tba9024q2up98 t18;
                GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t18 + 0), t17);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out) + 20, 0);
                GM_ST(gm_tba9024q2up98, ((uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out)), t18);
                ((uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out))[16] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
        {
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)9ULL));
            if (!t20) {
                goto gm_lb3;
            }
            void * t21 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 8));
            gm_l5 = t21;
            void * t22 = gm_l0;
            uint64_t t23 = gm_l1;
            void * t24 = gm_l5;
            gm_f_aliased_decl_jv6ilmn((void *)&(*(gm_t89h15e2c5o3p *)gm_out), (void *)NULL, cx, NULL, t22, t23, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb3:;
        {
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t7) + 8) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)22ULL));
            if (!t26) {
                goto gm_lb4;
            }
            void * t27 = GM_LD(void *, ((((uint8_t *)t7) + 8) + 8));
            gm_l6 = t27;
            {
                memmove(((uint8_t *)&gm_l7), (((((uint8_t *)t7) + 8) + 8) + 8), 32);
            }
            void * t28 = gm_l0;
            uint64_t t29 = gm_l1;
            void * t30 = gm_l6;
            gm_tf5srh4mp4b3i t31 = gm_f_module_named_n1mcl3b((void *)NULL, cx, NULL, t28, t29, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t31;
            uint8_t t32 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
            if (t32) {
                uint64_t t33 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                gm_l8 = t33;
            } else {
                memset(&(*(gm_t89h15e2c5o3p *)gm_out), 0, sizeof (*(gm_t89h15e2c5o3p *)gm_out));
                goto gm_fdone;
            }
            {
                void * t34 = gm_l0;
                uint64_t t35 = gm_l8;
                uint64_t t36 = gm_l1;
                gm_tf5srh4mp4b3i t37 = gm_f_module_member_5fd2qm9((void *)NULL, cx, NULL, t34, t35, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)), t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tba9024q2up98 t38;
                GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t38 + 0), t37);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out) + 20, 0);
                GM_ST(gm_tba9024q2up98, ((uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out)), t38);
                ((uint8_t *)&(*(gm_t89h15e2c5o3p *)gm_out))[16] = 1;
                goto gm_fdone;
            }
        }
gm_lb4:;
        {
            memset(&(*(gm_t89h15e2c5o3p *)gm_out), 0, sizeof (*(gm_t89h15e2c5o3p *)gm_out));
            goto gm_fdone;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1110, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_allow_attr_3321gm7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_check_allow_attr_3321gm7 F;
    void * gm_l0; /* s */
    void * gm_l1; /* attr */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 263u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l6;
    void * t10 = gm_l1;
    uint8_t t11 = (uint8_t)(((((uint8_t *)t10) + 32))[16] != 0);
    if (t11) {
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 32));
        gm_l2 = t12;
    } else {
        gm_b16_8 t13 = gm_f_empty_slice_g3tddrb();
        gm_l2 = t13;
    }
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
    if (t15) {
        void * t16 = gm_l0;
        void * t17 = gm_l1;
        gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t17) + 56));
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s8n5rdam5o1sm));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(68));
        gm_b0_1 t20 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t16, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_b16_8 t21 = gm_l2;
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
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t25) + (uintptr_t)t27 * 88), 88);
                        {
                            {
                                uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 8) + 0));
                                uint8_t t29 = (uint8_t)(t28 == ((uint8_t)2ULL));
                                if (!t29) {
                                    goto gm_lb7;
                                }
                                gm_l6 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l6 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 981, 12);
                        }
gm_lb6:;
                        uint8_t t30 = gm_l6;
                        if (t30) {
                            void * t31 = gm_l0;
                            gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 72));
                            gm_b16_8 t33;
                            GM_ST(void *, (uint8_t *)&t33, (void *)(gm_st6dd0k5nckh3));
                            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(57));
                            gm_b0_1 t34 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t31, t32, t33);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 980, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t35 = gm_l4;
            uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
            uint64_t t38 = r36;
            gm_l4 = t38;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_method_candidates_k08rvu6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_method_candidates_k08rvu6 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_tl874t2atnlh3 gm_l5; /* f */
    gm_tl874t2atnlh3 gm_l6; /* at */
    gm_tl874t2atnlh3 gm_l7; /* other */
    GM_STACK_CHECK(&F, 879u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_toceffb697p0u gm_l9;
    gm_tftb1odc739t5 gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    gm_tabb710d9mq2e gm_l13;
    {
        gm_t4c7ipjj9s3fp t9;
        gm_f_hash_map_gsp2k0o((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t9;
        void * t10 = gm_l0;
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 1992));
        gm_l2 = t11;
        gm_l3 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t12 = gm_l3;
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint8_t t14 = (t12 < t13);
                if (!t14) {
                    goto gm_lb2;
                }
                {
                    void * t15 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t17 = gm_l3;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l4), (((uint8_t *)t15) + (uintptr_t)t17 * 40), 40);
                            }
                            {
                                void * t18 = (void *)((uint8_t *)&F.l1);
                                uint32_t t19 = GM_LD(uint32_t, ((uint8_t *)&F.l4));
                                gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                                gm_tiilmfd16v3fc t21;
                                GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t21 + 0), t19);
                                GM_ST(gm_b16_8, ((uint8_t *)&t21 + 8), t20);
                                gm_toceffb697p0u t22 = gm_f_get_qjsv97t(t18, &t21);
                                gm_l9 = t22;
                            }
                            uint8_t t23 = (uint8_t)((((uint8_t *)&gm_l9))[12] != 0);
                            if (t23) {
                                gm_tl874t2atnlh3 t24 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l9));
                                gm_l5 = t24;
                            } else {
                                void * t25 = (void *)((uint8_t *)&F.l1);
                                uint32_t t26 = GM_LD(uint32_t, ((uint8_t *)&F.l4));
                                gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                                gm_tiilmfd16v3fc t28;
                                GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t28 + 0), t26);
                                GM_ST(gm_b16_8, ((uint8_t *)&t28 + 8), t27);
                                gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                                gm_tftb1odc739t5 t30 = gm_f_put_pjhbiaq(t25, &t28, GM_CONV(gm_cseahvg8v7p0vs, t29));
                                gm_l10 = t30;
                                uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                                if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srclv0dcbbfhves, 1177, 13);
                                goto gm_lb4;
                            }
                            gm_tl874t2atnlh3 t32 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                            gm_l6 = t32;
                            gm_tl874t2atnlh3 t33 = gm_l5;
                            gm_l7 = t33;
                            uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 24));
                            uint32_t t35 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
                            uint8_t t36 = (t34 < t35);
                            if (t36) {
                                gm_l11 = ((uint8_t)1);
                            } else {
                                uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 24));
                                uint32_t t38 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
                                uint8_t t39 = ((t37 == t38));
                                if (t39) {
                                    uint32_t t40 = GM_LD(uint32_t, ((((uint8_t *)&F.l4) + 24) + 4));
                                    uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                                    uint8_t t42 = (t40 < t41);
                                    gm_l12 = t42;
                                } else {
                                    gm_l12 = ((uint8_t)0);
                                }
                                uint8_t t43 = gm_l12;
                                gm_l11 = t43;
                            }
                            uint8_t t44 = gm_l11;
                            if (t44) {
                                gm_tl874t2atnlh3 t45 = gm_l5;
                                gm_l6 = t45;
                                gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l4) + 24));
                                gm_l7 = t46;
                            }
                            {
                                gm_tl874t2atnlh3 t47 = gm_l6;
                                void * t48 = gm_l0;
                                void * t49 = GM_LD(void *, (((uint8_t *)t48) + 1088));
                                uint64_t t50 = GM_LD(uint64_t, ((((uint8_t *)t48) + 1088) + 8));
                                uint32_t t51 = GM_LD(uint32_t, ((uint8_t *)&F.l4));
                                uint64_t t52 = (uint64_t)t51;
                                if ((uintptr_t)t52 >= (uintptr_t)(t50)) gm_trap_index((uintptr_t)t52, t50, gm_srclv0dcbbfhves, 1187, 127);
                                gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)t49) + (uintptr_t)t52 * 184));
                                gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                                gm_t6j6smj7qoctu t55;
                                GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t53);
                                GM_ST(gm_b16_8, ((uint8_t *)&t55 + 16), t54);
                                gm_f_format_ck7vger((void *)&gm_l13, (void *)NULL, cx, NULL, &t55);
                                if (gm_unw.kind) {
                                    goto gm_ld8_1;
                                }
                                uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srclv0dcbbfhves, 1187, 29);
                                gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
                                gm_t4gut2c9fj1kt t59;
                                gm_f_error_s6hn871((void *)&t59, (void *)NULL, cx, NULL, t47, t58);
                                if (gm_unw.kind) {
                                    goto gm_ld8_1;
                                }
                                F.l8 = t59;
                            }
                            void * t60 = (void *)((uint8_t *)&F.l8);
                            gm_tl874t2atnlh3 t61 = gm_l7;
                            gm_b16_8 t62;
                            GM_ST(void *, (uint8_t *)&t62, (void *)(gm_srjo6vku5gka5));
                            GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(19));
                            gm_f_add_label_50fj7vv(t60, GM_CONV(gm_cseahvg8v7p0vs, t61), t62);
                            {
                                void * t63 = gm_l0;
                                gm_b0_1 t64 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t63, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
                                if (gm_unw.kind) {
                                    goto gm_ld8_1;
                                }
                            }
                            goto gm_lb5;
                        }
gm_lb6:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1175, 5);
                    }
gm_lb5:;
                }
gm_lb4:;
                uint64_t t65 = gm_l3;
                uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                uint64_t t68 = r66;
                gm_l3 = t68;
            }
        }
gm_lb2:;
        void * t69 = gm_l0;
        void * t70 = (void *)(((uint8_t *)t69) + 1992);
        gm_f_clear_ufjejrf(t70);
    }
    {
        void * t71 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_2l7c12h(t71);
    }
    goto gm_ls72;
gm_ld8_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t73 = (void *)((uint8_t *)&F.l1);
        gm_f_deinit_2l7c12h(t73);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls72:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_pub_signatures_plkds6r(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_pub_signatures_plkds6r F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* d */
    gm_t9qkh0rsooc8d gm_l5; /* vis */
    uint32_t gm_l8; /* f */
    void * gm_l9; /* sig */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint32_t gm_l13; /* e */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_tiilmfd16v3fc gm_l20; /* r */
    gm_tfe2ufaqq8re9 gm_l21; /* <temporary array> */
    gm_tnpifv20irnph gm_l22; /* set */
    gm_t88rcucg1b7uk gm_l23; /* t */
    uint32_t gm_l24; /* id */
    uint32_t gm_l25; /* id */
    gm_t88rcucg1b7uk gm_l26; /* t */
    gm_t88rcucg1b7uk gm_l27; /* t */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_t3jsf43p1pths gm_l30; /* m */
    gm_t3jsf43p1pths gm_l31; /* h */
    gm_b16_8 gm_l32; /* what */
    gm_b16_8 gm_l33; /* kind */
    GM_STACK_CHECK(&F, 1914u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l34;
    uint8_t gm_l35;
    uint8_t gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    uint8_t gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    gm_t53at80pfdus3 gm_l42;
    gm_tftb1odc739t5 gm_l43;
    gm_tftb1odc739t5 gm_l44;
    gm_trcagkhgm1qdu gm_l45;
    gm_tabb710d9mq2e gm_l46;
    {
        gm_l2 = ((uint64_t)0ULL);
        void * t54 = gm_l0;
        void * t55 = GM_LD(void *, ((uint8_t *)t54));
        uint64_t t56 = GM_LD(uint64_t, ((((uint8_t *)t55) + 240) + 8));
        gm_l1 = t56;
        {
            for (;;) {
                {
                    uint64_t t57 = gm_l2;
                    uint64_t t58 = gm_l1;
                    uint8_t t59 = (t57 < t58);
                    if (!t59) {
                        goto gm_lb1;
                    }
                    {
                        {
                            {
                                {
                                    {
                                        uint64_t t60 = gm_l2;
                                        gm_l3 = t60;
                                        void * t61 = gm_l0;
                                        void * t62 = GM_LD(void *, ((uint8_t *)t61));
                                        void * t63 = GM_LD(void *, (((uint8_t *)t62) + 240));
                                        uint64_t t64 = GM_LD(uint64_t, ((((uint8_t *)t62) + 240) + 8));
                                        uint64_t t65 = gm_l3;
                                        if ((uintptr_t)t65 >= (uintptr_t)(t64)) gm_trap_index((uintptr_t)t65, t64, gm_srclv0dcbbfhves, 1198, 15);
                                        void * t66 = (void *)(((uint8_t *)t63) + (uintptr_t)t65 * 216);
                                        gm_l4 = t66;
                                        void * t67 = gm_l4;
                                        gm_t9qkh0rsooc8d t68 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t67) + 40) + 56));
                                        gm_l5 = t68;
                                        gm_t9qkh0rsooc8d t69 = gm_l5;
                                        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t69) + 0));
                                        uint8_t t71 = (uint8_t)((t70 == ((uint8_t)0ULL)));
                                        gm_t9qkh0rsooc8d t72;
                                        GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)0ULL));
                                        if (t71) {
                                            gm_l34 = ((uint8_t)1);
                                        } else {
                                            void * t73 = gm_l4;
                                            {
                                                {
                                                    uint8_t t74 = GM_LD(uint8_t, (((((uint8_t *)t73) + 40) + 64) + 0));
                                                    uint8_t t75 = (uint8_t)(t74 == ((uint8_t)0ULL));
                                                    if (!t75) {
                                                        goto gm_lb7;
                                                    }
                                                    gm_l35 = ((uint8_t)0);
                                                    goto gm_lb6;
                                                }
gm_lb7:;
                                                {
                                                    gm_l35 = ((uint8_t)1);
                                                    goto gm_lb6;
                                                }
gm_lb8:;
                                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1200, 31);
                                            }
gm_lb6:;
                                            uint8_t t76 = gm_l35;
                                            gm_l34 = t76;
                                        }
                                        uint8_t t77 = gm_l34;
                                        if (t77) {
                                            goto gm_lb3;
                                        }
                                        uint64_t t78 = gm_l3;
                                        void * t79 = gm_l0;
                                        uint64_t t80 = GM_LD(uint64_t, ((((uint8_t *)t79) + 304) + 8));
                                        uint8_t t81 = (t78 >= t80);
                                        if (t81) {
                                            goto gm_lb3;
                                        }
                                        {
                                            {
                                                {
                                                    {
                                                        void * t82 = gm_l0;
                                                        void * t83 = GM_LD(void *, (((uint8_t *)t82) + 304));
                                                        uint64_t t84 = GM_LD(uint64_t, ((((uint8_t *)t82) + 304) + 8));
                                                        uint64_t t85 = gm_l3;
                                                        if ((uintptr_t)t85 >= (uintptr_t)(t84)) gm_trap_index((uintptr_t)t85, t84, gm_srclv0dcbbfhves, 1202, 21);
                                                        uint8_t t86 = GM_LD(uint8_t, ((((uint8_t *)t83) + (uintptr_t)t85 * 216) + 0));
                                                        uint8_t t87 = (uint8_t)(t86 == ((uint8_t)2ULL));
                                                        if (!t87) {
                                                            goto gm_lb10;
                                                        }
                                                        memmove(((uint8_t *)&F.l6), ((((uint8_t *)t83) + (uintptr_t)t85 * 216) + 8), 208);
                                                        gm_trqfvmjtt4sij t88;
                                                        gm_f_list_fam8ktt((void *)&t88, (void *)NULL, cx, NULL);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        F.l7 = t88;
                                                        {
                                                            {
                                                                {
                                                                    uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                                                    uint8_t t90 = (uint8_t)(t89 == ((uint8_t)5ULL));
                                                                    if (!t90) {
                                                                        goto gm_lb12;
                                                                    }
                                                                    uint32_t t91 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
                                                                    gm_l8 = t91;
                                                                    void * t92 = gm_l0;
                                                                    void * t93 = GM_LD(void *, (((uint8_t *)t92) + 344));
                                                                    uint64_t t94 = GM_LD(uint64_t, ((((uint8_t *)t92) + 344) + 8));
                                                                    uint32_t t95 = gm_l8;
                                                                    uint64_t t96 = (uint64_t)t95;
                                                                    if ((uintptr_t)t96 >= (uintptr_t)(t94)) gm_trap_index((uintptr_t)t96, t94, gm_srclv0dcbbfhves, 1207, 25);
                                                                    void * t97 = (void *)(((uint8_t *)t93) + (uintptr_t)t96 * 136);
                                                                    gm_l9 = t97;
                                                                    void * t98 = gm_l9;
                                                                    gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)t98) + 40));
                                                                    gm_l10 = t99;
                                                                    gm_l11 = ((uint64_t)0ULL);
                                                                    {
                                                                        for (;;) {
                                                                            uint64_t t100 = gm_l11;
                                                                            uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                                            uint8_t t102 = (t100 < t101);
                                                                            if (!t102) {
                                                                                goto gm_lb13;
                                                                            }
                                                                            {
                                                                                void * t103 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                                                                uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                                                                uint64_t t105 = gm_l11;
                                                                                {
                                                                                    {
                                                                                        memmove(((uint8_t *)&F.l12), (((uint8_t *)t103) + (uintptr_t)t105 * 56), 56);
                                                                                        {
                                                                                            {
                                                                                                uint8_t t106 = (uint8_t)(((((uint8_t *)&F.l12) + 40))[4] != 0);
                                                                                                if (t106) {
                                                                                                    goto gm_lb19;
                                                                                                }
                                                                                                gm_l36 = ((uint8_t)1);
                                                                                                goto gm_lb18;
                                                                                            }
gm_lb19:;
                                                                                            {
                                                                                                gm_l36 = ((uint8_t)0);
                                                                                                goto gm_lb18;
                                                                                            }
gm_lb20:;
                                                                                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1209, 24);
                                                                                        }
gm_lb18:;
                                                                                        uint8_t t107 = gm_l36;
                                                                                        if (t107) {
                                                                                            void * t108 = (void *)((uint8_t *)&F.l7);
                                                                                            gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 16));
                                                                                            gm_t9qkh0rsooc8d t110 = gm_l5;
                                                                                            gm_t3jsf43p1pths t111;
                                                                                            GM_ST(uint8_t, (uint8_t *)&t111 + 5, 0);
                                                                                            GM_ST(uint16_t, (uint8_t *)&t111 + 6, 0);
                                                                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t111 + 0), t109);
                                                                                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t111 + 4), t110);
                                                                                            gm_tftb1odc739t5 t112 = gm_f_push_h37silv(t108, t111);
                                                                                            gm_l37 = t112;
                                                                                            uint8_t t113 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                                                                            if (!t113) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srclv0dcbbfhves, 1209, 44);
                                                                                        }
                                                                                        goto gm_lb16;
                                                                                    }
gm_lb17:;
                                                                                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1208, 17);
                                                                                }
gm_lb16:;
                                                                            }
gm_lb15:;
                                                                            uint64_t t114 = gm_l11;
                                                                            uint64_t r115; bool o116 = __builtin_add_overflow(t114, ((uint64_t)1ULL), &r115);
                                                                            uint64_t t117 = r115;
                                                                            gm_l11 = t117;
                                                                        }
                                                                    }
gm_lb13:;
                                                                    void * t118 = (void *)((uint8_t *)&F.l7);
                                                                    void * t119 = gm_l9;
                                                                    gm_t88rcucg1b7uk t120 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t119) + 56));
                                                                    gm_t9qkh0rsooc8d t121 = gm_l5;
                                                                    gm_t3jsf43p1pths t122;
                                                                    GM_ST(uint8_t, (uint8_t *)&t122 + 5, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t122 + 6, 0);
                                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t122 + 0), t120);
                                                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t122 + 4), t121);
                                                                    gm_tftb1odc739t5 t123 = gm_f_push_h37silv(t118, t122);
                                                                    gm_l38 = t123;
                                                                    uint8_t t124 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                                                    if (!t124) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srclv0dcbbfhves, 1211, 17);
                                                                    void * t125 = gm_l9;
                                                                    gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)t125) + 64));
                                                                    gm_t9qkh0rsooc8d t127 = gm_l5;
                                                                    void * t128 = (void *)((uint8_t *)&F.l7);
                                                                    gm_b0_1 t129 = gm_f_effect_arg_tys_2a7r8hh((void *)NULL, cx, NULL, t126, t127, t128);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    goto gm_lb11;
                                                                }
gm_lb12:;
                                                            }
                                                            {
                                                                {
                                                                    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                                                    uint8_t t131 = (uint8_t)(t130 == ((uint8_t)2ULL));
                                                                    if (!t131) {
                                                                        goto gm_lb21;
                                                                    }
                                                                    uint32_t t132 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
                                                                    gm_l13 = t132;
                                                                    void * t133 = gm_l0;
                                                                    void * t134 = GM_LD(void *, (((uint8_t *)t133) + 1048));
                                                                    uint64_t t135 = GM_LD(uint64_t, ((((uint8_t *)t133) + 1048) + 8));
                                                                    uint32_t t136 = gm_l13;
                                                                    uint64_t t137 = (uint64_t)t136;
                                                                    if ((uintptr_t)t137 >= (uintptr_t)(t135)) gm_trap_index((uintptr_t)t137, t135, gm_srclv0dcbbfhves, 1215, 27);
                                                                    gm_b16_8 t138 = GM_LD(gm_b16_8, ((((uint8_t *)t134) + (uintptr_t)t137 * 56) + 40));
                                                                    gm_l14 = t138;
                                                                    gm_l15 = ((uint64_t)0ULL);
                                                                    {
                                                                        for (;;) {
                                                                            uint64_t t139 = gm_l15;
                                                                            uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                                                            uint8_t t141 = (t139 < t140);
                                                                            if (!t141) {
                                                                                goto gm_lb22;
                                                                            }
                                                                            {
                                                                                void * t142 = GM_LD(void *, ((uint8_t *)&gm_l14));
                                                                                uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                                                                uint64_t t144 = gm_l15;
                                                                                {
                                                                                    {
                                                                                        memmove(((uint8_t *)&F.l16), (((uint8_t *)t142) + (uintptr_t)t144 * 48), 48);
                                                                                        gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 24));
                                                                                        gm_l17 = t145;
                                                                                        gm_l18 = ((uint64_t)0ULL);
                                                                                        {
                                                                                            for (;;) {
                                                                                                uint64_t t146 = gm_l18;
                                                                                                uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                                                                                uint8_t t148 = (t146 < t147);
                                                                                                if (!t148) {
                                                                                                    goto gm_lb27;
                                                                                                }
                                                                                                {
                                                                                                    void * t149 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                                                                                    uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                                                                                    uint64_t t151 = gm_l18;
                                                                                                    {
                                                                                                        {
                                                                                                            memmove(((uint8_t *)&F.l19), (((uint8_t *)t149) + (uintptr_t)t151 * 56), 56);
                                                                                                            {
                                                                                                                {
                                                                                                                    uint8_t t152 = (uint8_t)(((((uint8_t *)&F.l19) + 40))[4] != 0);
                                                                                                                    if (t152) {
                                                                                                                        goto gm_lb33;
                                                                                                                    }
                                                                                                                    gm_l39 = ((uint8_t)1);
                                                                                                                    goto gm_lb32;
                                                                                                                }
gm_lb33:;
                                                                                                                {
                                                                                                                    gm_l39 = ((uint8_t)0);
                                                                                                                    goto gm_lb32;
                                                                                                                }
gm_lb34:;
                                                                                                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1217, 28);
                                                                                                            }
gm_lb32:;
                                                                                                            uint8_t t153 = gm_l39;
                                                                                                            if (t153) {
                                                                                                                void * t154 = (void *)((uint8_t *)&F.l7);
                                                                                                                gm_t88rcucg1b7uk t155 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l19) + 16));
                                                                                                                gm_t9qkh0rsooc8d t156 = gm_l5;
                                                                                                                gm_t3jsf43p1pths t157;
                                                                                                                GM_ST(uint8_t, (uint8_t *)&t157 + 5, 0);
                                                                                                                GM_ST(uint16_t, (uint8_t *)&t157 + 6, 0);
                                                                                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t157 + 0), t155);
                                                                                                                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t157 + 4), t156);
                                                                                                                gm_tftb1odc739t5 t158 = gm_f_push_h37silv(t154, t157);
                                                                                                                gm_l40 = t158;
                                                                                                                uint8_t t159 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                                                                                                if (!t159) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srclv0dcbbfhves, 1217, 48);
                                                                                                            }
                                                                                                            goto gm_lb30;
                                                                                                        }
gm_lb31:;
                                                                                                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1216, 21);
                                                                                                    }
gm_lb30:;
                                                                                                }
gm_lb29:;
                                                                                                uint64_t t160 = gm_l18;
                                                                                                uint64_t r161; bool o162 = __builtin_add_overflow(t160, ((uint64_t)1ULL), &r161);
                                                                                                uint64_t t163 = r161;
                                                                                                gm_l18 = t163;
                                                                                            }
                                                                                        }
gm_lb27:;
                                                                                        void * t164 = (void *)((uint8_t *)&F.l7);
                                                                                        gm_t88rcucg1b7uk t165 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 40));
                                                                                        gm_t9qkh0rsooc8d t166 = gm_l5;
                                                                                        gm_t3jsf43p1pths t167;
                                                                                        GM_ST(uint8_t, (uint8_t *)&t167 + 5, 0);
                                                                                        GM_ST(uint16_t, (uint8_t *)&t167 + 6, 0);
                                                                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t167 + 0), t165);
                                                                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t167 + 4), t166);
                                                                                        gm_tftb1odc739t5 t168 = gm_f_push_h37silv(t164, t167);
                                                                                        gm_l41 = t168;
                                                                                        uint8_t t169 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                                                                                        if (!t169) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_srclv0dcbbfhves, 1219, 21);
                                                                                        goto gm_lb25;
                                                                                    }
gm_lb26:;
                                                                                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1215, 17);
                                                                                }
gm_lb25:;
                                                                            }
gm_lb24:;
                                                                            uint64_t t170 = gm_l15;
                                                                            uint64_t r171; bool o172 = __builtin_add_overflow(t170, ((uint64_t)1ULL), &r171);
                                                                            uint64_t t173 = r171;
                                                                            gm_l15 = t173;
                                                                        }
                                                                    }
gm_lb22:;
                                                                    goto gm_lb11;
                                                                }
gm_lb21:;
                                                            }
                                                            {
                                                                {
                                                                    uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                                                    uint8_t t175 = (uint8_t)(t174 == ((uint8_t)3ULL));
                                                                    if (!t175) {
                                                                        goto gm_lb35;
                                                                    }
                                                                    {
                                                                        memmove(((uint8_t *)&gm_l20), (((uint8_t *)&F.l6) + 8), 24);
                                                                    }
                                                                    {
                                                                        gm_tiilmfd16v3fc t176 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l20));
                                                                        gm_tfe2ufaqq8re9 t177;
                                                                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t177 + 0), t176);
                                                                        gm_l21 = t177;
                                                                    }
                                                                    void * t178 = (void *)((uint8_t *)&gm_l21);
                                                                    uint64_t t179 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                                                                    gm_b16_8 t180;
                                                                    GM_ST(void *, (uint8_t *)&t180, (void *)(((uint8_t *)t178 + (uintptr_t)((uint64_t)0ULL) * 24)));
                                                                    GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(t179));
                                                                    gm_t9qkh0rsooc8d t181 = gm_l5;
                                                                    void * t182 = (void *)((uint8_t *)&F.l7);
                                                                    gm_b0_1 t183 = gm_f_effect_arg_tys_2a7r8hh((void *)NULL, cx, NULL, t180, t181, t182);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    goto gm_lb11;
                                                                }
gm_lb35:;
                                                                {
                                                                    uint8_t t184 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                                                    uint8_t t185 = (uint8_t)(t184 == ((uint8_t)4ULL));
                                                                    if (!t185) {
                                                                        goto gm_lb36;
                                                                    }
                                                                    gm_tnpifv20irnph t186 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l6) + 8));
                                                                    gm_l22 = t186;
                                                                    gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
                                                                    gm_t9qkh0rsooc8d t188 = gm_l5;
                                                                    void * t189 = (void *)((uint8_t *)&F.l7);
                                                                    gm_b0_1 t190 = gm_f_effect_arg_tys_2a7r8hh((void *)NULL, cx, NULL, t187, t188, t189);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_lad;
                                                                    }
                                                                    goto gm_lb11;
                                                                }
gm_lb36:;
                                                                {
                                                                    uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                                                    uint8_t t192 = (uint8_t)(t191 == ((uint8_t)0ULL));
                                                                    if (!t192) {
                                                                        goto gm_lb37;
                                                                    }
                                                                    gm_t88rcucg1b7uk t193 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                                                                    gm_l23 = t193;
                                                                    {
                                                                        {
                                                                            void * t194 = gm_l0;
                                                                            void * t195 = (void *)(((uint8_t *)t194) + 8);
                                                                            gm_t88rcucg1b7uk t196 = gm_l23;
                                                                            gm_f_kind_jcnt4hm((void *)&gm_l42, t195, GM_CONV(gm_csguankko4lmhd, t196));
                                                                            uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                                                                            uint8_t t198 = (uint8_t)(t197 == ((uint8_t)17ULL));
                                                                            if (!t198) {
                                                                                goto gm_lb39;
                                                                            }
                                                                            uint32_t t199 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 8));
                                                                            gm_l24 = t199;
                                                                            void * t200 = gm_l0;
                                                                            void * t201 = GM_LD(void *, (((uint8_t *)t200) + 1088));
                                                                            uint64_t t202 = GM_LD(uint64_t, ((((uint8_t *)t200) + 1088) + 8));
                                                                            uint32_t t203 = gm_l24;
                                                                            uint64_t t204 = (uint64_t)t203;
                                                                            if ((uintptr_t)t204 >= (uintptr_t)(t202)) gm_trap_index((uintptr_t)t204, t202, gm_srclv0dcbbfhves, 1225, 53);
                                                                            gm_tf5srh4mp4b3i t205 = GM_LD(gm_tf5srh4mp4b3i, ((((uint8_t *)t201) + (uintptr_t)t204 * 184) + 168));
                                                                            uint64_t t206 = gm_l3;
                                                                            gm_tf5srh4mp4b3i t207;
                                                                            GM_ST(uint8_t, (uint8_t *)&t207 + 9, 0);
                                                                            GM_ST(uint16_t, (uint8_t *)&t207 + 10, 0);
                                                                            GM_ST(uint32_t, (uint8_t *)&t207 + 12, 0);
                                                                            GM_ST(uint64_t, ((uint8_t *)&t207), t206);
                                                                            ((uint8_t *)&t207)[8] = 1;
                                                                            uint8_t t208 = (gm_eq7mri15hk78(((uint8_t *)&t205), ((uint8_t *)&t207)));
                                                                            if (!t208) {
                                                                                goto gm_lb39;
                                                                            }
                                                                            void * t209 = gm_l0;
                                                                            uint32_t t210 = gm_l24;
                                                                            gm_t9qkh0rsooc8d t211 = gm_l5;
                                                                            void * t212 = (void *)((uint8_t *)&F.l7);
                                                                            gm_b0_1 t213 = gm_f_adt_field_tys_oggq5ji((void *)NULL, cx, NULL, t209, t210, t211, t212);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                            goto gm_lb38;
                                                                        }
gm_lb39:;
                                                                        void * t214 = (void *)((uint8_t *)&F.l7);
                                                                        gm_t88rcucg1b7uk t215 = gm_l23;
                                                                        gm_t9qkh0rsooc8d t216 = gm_l5;
                                                                        gm_t3jsf43p1pths t217;
                                                                        GM_ST(uint8_t, (uint8_t *)&t217 + 5, 0);
                                                                        GM_ST(uint16_t, (uint8_t *)&t217 + 6, 0);
                                                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t217 + 0), t215);
                                                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t217 + 4), t216);
                                                                        gm_tftb1odc739t5 t218 = gm_f_push_h37silv(t214, t217);
                                                                        gm_l43 = t218;
                                                                        uint8_t t219 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                                                                        if (!t219) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_srclv0dcbbfhves, 1228, 21);
                                                                    }
gm_lb38:;
                                                                    goto gm_lb11;
                                                                }
gm_lb37:;
                                                            }
                                                            {
                                                                {
                                                                    uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                                                    uint8_t t221 = (uint8_t)(t220 == ((uint8_t)1ULL));
                                                                    if (!t221) {
                                                                        goto gm_lb40;
                                                                    }
                                                                    uint32_t t222 = GM_LD(uint32_t, (((uint8_t *)&F.l6) + 8));
                                                                    gm_l25 = t222;
                                                                    void * t223 = gm_l0;
                                                                    void * t224 = GM_LD(void *, (((uint8_t *)t223) + 1088));
                                                                    uint64_t t225 = GM_LD(uint64_t, ((((uint8_t *)t223) + 1088) + 8));
                                                                    uint32_t t226 = gm_l25;
                                                                    uint64_t t227 = (uint64_t)t226;
                                                                    if ((uintptr_t)t227 >= (uintptr_t)(t225)) gm_trap_index((uintptr_t)t227, t225, gm_srclv0dcbbfhves, 1232, 20);
                                                                    gm_tf5srh4mp4b3i t228 = GM_LD(gm_tf5srh4mp4b3i, ((((uint8_t *)t224) + (uintptr_t)t227 * 184) + 168));
                                                                    uint64_t t229 = gm_l3;
                                                                    gm_tf5srh4mp4b3i t230;
                                                                    GM_ST(uint8_t, (uint8_t *)&t230 + 9, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t230 + 10, 0);
                                                                    GM_ST(uint32_t, (uint8_t *)&t230 + 12, 0);
                                                                    GM_ST(uint64_t, ((uint8_t *)&t230), t229);
                                                                    ((uint8_t *)&t230)[8] = 1;
                                                                    uint8_t t231 = (gm_eq7mri15hk78(((uint8_t *)&t228), ((uint8_t *)&t230)));
                                                                    if (t231) {
                                                                        void * t232 = gm_l0;
                                                                        uint32_t t233 = gm_l25;
                                                                        gm_t9qkh0rsooc8d t234 = gm_l5;
                                                                        void * t235 = (void *)((uint8_t *)&F.l7);
                                                                        gm_b0_1 t236 = gm_f_adt_field_tys_oggq5ji((void *)NULL, cx, NULL, t232, t233, t234, t235);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                    }
                                                                    goto gm_lb11;
                                                                }
gm_lb40:;
                                                                {
                                                                    {
                                                                        {
                                                                            uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                                                            uint8_t t238 = (uint8_t)(t237 == ((uint8_t)7ULL));
                                                                            if (!t238) {
                                                                                goto gm_lb43;
                                                                            }
                                                                            gm_t88rcucg1b7uk t239 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                                                                            gm_l26 = t239;
                                                                            goto gm_lb42;
                                                                        }
gm_lb43:;
                                                                        {
                                                                            uint8_t t240 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                                                            uint8_t t241 = (uint8_t)(t240 == ((uint8_t)10ULL));
                                                                            if (!t241) {
                                                                                goto gm_lb44;
                                                                            }
                                                                            gm_t88rcucg1b7uk t242 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                                                                            gm_l26 = t242;
                                                                            goto gm_lb42;
                                                                        }
gm_lb44:;
                                                                        goto gm_lb41;
                                                                    }
gm_lb42:;
                                                                    void * t243 = (void *)((uint8_t *)&F.l7);
                                                                    gm_t88rcucg1b7uk t244 = gm_l26;
                                                                    gm_t9qkh0rsooc8d t245 = gm_l5;
                                                                    gm_t3jsf43p1pths t246;
                                                                    GM_ST(uint8_t, (uint8_t *)&t246 + 5, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t246 + 6, 0);
                                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t246 + 0), t244);
                                                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t246 + 4), t245);
                                                                    gm_tftb1odc739t5 t247 = gm_f_push_h37silv(t243, t246);
                                                                    gm_l44 = t247;
                                                                    uint8_t t248 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                                                                    if (!t248) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 1, gm_srclv0dcbbfhves, 1234, 38);
                                                                    goto gm_lb11;
                                                                }
gm_lb41:;
                                                                {
                                                                    goto gm_lb11;
                                                                }
gm_lb45:;
                                                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1205, 9);
                                                            }
                                                        }
gm_lb11:;
                                                    }
                                                    {
                                                        gm_b16_8 t249 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                                                        gm_l28 = t249;
                                                        gm_l29 = ((uint64_t)0ULL);
                                                        {
                                                            for (;;) {
                                                                uint64_t t250 = gm_l29;
                                                                uint64_t t251 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                                                uint8_t t252 = (t250 < t251);
                                                                if (!t252) {
                                                                    goto gm_lb46;
                                                                }
                                                                {
                                                                    void * t253 = GM_LD(void *, ((uint8_t *)&gm_l28));
                                                                    uint64_t t254 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                                                    uint64_t t255 = gm_l29;
                                                                    {
                                                                        {
                                                                            gm_t3jsf43p1pths t256 = GM_LD(gm_t3jsf43p1pths, (((uint8_t *)t253) + (uintptr_t)t255 * 8));
                                                                            gm_l30 = t256;
                                                                            {
                                                                                {
                                                                                    void * t257 = gm_l0;
                                                                                    gm_t88rcucg1b7uk t258 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l30));
                                                                                    gm_t9qkh0rsooc8d t259 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l30) + 4));
                                                                                    gm_trcagkhgm1qdu t260 = gm_f_hidden_part_is5hgf9(t257, GM_CONV(gm_csguankko4lmhd, t258), t259);
                                                                                    gm_l45 = t260;
                                                                                    uint8_t t261 = (uint8_t)((((uint8_t *)&gm_l45))[8] != 0);
                                                                                    if (!t261) {
                                                                                        goto gm_lb52;
                                                                                    }
                                                                                    gm_t3jsf43p1pths t262 = GM_LD(gm_t3jsf43p1pths, ((uint8_t *)&gm_l45));
                                                                                    gm_l31 = t262;
                                                                                    gm_t9qkh0rsooc8d t263 = gm_l5;
                                                                                    uint8_t t264 = GM_LD(uint8_t, (((uint8_t *)&t263) + 0));
                                                                                    uint8_t t265 = (uint8_t)((t264 == ((uint8_t)1ULL)));
                                                                                    gm_t9qkh0rsooc8d t266;
                                                                                    GM_ST(uint8_t, ((uint8_t *)&t266 + 0), ((uint8_t)1ULL));
                                                                                    if (t265) {
                                                                                        gm_b16_8 t267;
                                                                                        GM_ST(void *, (uint8_t *)&t267, (void *)(gm_shofa8pdml9fb));
                                                                                        GM_ST(uintptr_t, (uint8_t *)&t267 + 8, (uintptr_t)(12));
                                                                                        gm_l32 = t267;
                                                                                    } else {
                                                                                        gm_b16_8 t268;
                                                                                        GM_ST(void *, (uint8_t *)&t268, (void *)(gm_sjqs7q3ostqbm));
                                                                                        GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(3));
                                                                                        gm_l32 = t268;
                                                                                    }
                                                                                    gm_t9qkh0rsooc8d t269 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l31) + 4));
                                                                                    uint8_t t270 = GM_LD(uint8_t, (((uint8_t *)&t269) + 0));
                                                                                    uint8_t t271 = (uint8_t)((t270 == ((uint8_t)1ULL)));
                                                                                    gm_t9qkh0rsooc8d t272;
                                                                                    GM_ST(uint8_t, ((uint8_t *)&t272 + 0), ((uint8_t)1ULL));
                                                                                    if (t271) {
                                                                                        gm_b16_8 t273;
                                                                                        GM_ST(void *, (uint8_t *)&t273, (void *)(gm_scllp0lpbljde));
                                                                                        GM_ST(uintptr_t, (uint8_t *)&t273 + 8, (uintptr_t)(14));
                                                                                        gm_l33 = t273;
                                                                                    } else {
                                                                                        gm_b16_8 t274;
                                                                                        GM_ST(void *, (uint8_t *)&t274, (void *)(gm_sbrnv8n0dpinb));
                                                                                        GM_ST(uintptr_t, (uint8_t *)&t274 + 8, (uintptr_t)(7));
                                                                                        gm_l33 = t274;
                                                                                    }
                                                                                    {
                                                                                        void * t275 = gm_l0;
                                                                                        void * t276 = gm_l4;
                                                                                        gm_tl874t2atnlh3 t277 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t276) + 16));
                                                                                        gm_b16_8 t278 = gm_l32;
                                                                                        void * t279 = gm_l4;
                                                                                        gm_b16_8 t280 = GM_LD(gm_b16_8, ((uint8_t *)t279));
                                                                                        gm_b16_8 t281 = gm_l33;
                                                                                        void * t282 = gm_l0;
                                                                                        gm_t88rcucg1b7uk t283 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l31));
                                                                                        gm_b16_8 t284 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t282, t283);
                                                                                        if (gm_unw.kind) {
                                                                                            goto gm_lad;
                                                                                        }
                                                                                        gm_t0uuhaljsf410 t285;
                                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t285 + 0), t278);
                                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t285 + 16), t280);
                                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t285 + 32), t281);
                                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t285 + 48), t284);
                                                                                        gm_f_format_t5fmuah((void *)&gm_l46, (void *)NULL, cx, NULL, &t285);
                                                                                        if (gm_unw.kind) {
                                                                                            goto gm_lad;
                                                                                        }
                                                                                        uint8_t t286 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                                                                        if (!t286) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 8, gm_srclv0dcbbfhves, 1241, 38);
                                                                                        gm_b16_8 t287 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                                                                                        gm_b16_8 t288 = GM_LD(gm_b16_8, ((uint8_t *)&t287));
                                                                                        gm_b0_1 t289 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t275, t277, t288);
                                                                                        if (gm_unw.kind) {
                                                                                            goto gm_lad;
                                                                                        }
                                                                                    }
                                                                                    goto gm_lb46;
                                                                                }
gm_lb52:;
                                                                            }
gm_lb51:;
                                                                            goto gm_lb49;
                                                                        }
gm_lb50:;
                                                                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1237, 9);
                                                                    }
gm_lb49:;
                                                                }
gm_lb48:;
                                                                uint64_t t290 = gm_l29;
                                                                uint64_t r291; bool o292 = __builtin_add_overflow(t290, ((uint64_t)1ULL), &r291);
                                                                uint64_t t293 = r291;
                                                                gm_l29 = t293;
                                                            }
                                                        }
gm_lb46:;
                                                    }
                                                    {
                                                        goto gm_lb9;
                                                    }
                                                }
gm_lb10:;
                                            }
                                            {
                                                goto gm_lb3;
                                            }
                                        }
gm_lb9:;
                                    }
                                    {
                                        goto gm_lb4;
                                    }
                                }
gm_lb5:;
                            }
                            {
                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1197, 5);
                            }
                        }
gm_lb4:;
                    }
gm_lb3:;
                }
                {
                    uint64_t t294 = gm_l2;
                    uint64_t r295; bool o296 = __builtin_add_overflow(t294, ((uint64_t)1ULL), &r295);
                    uint64_t t297 = r295;
                    gm_l2 = t297;
                }
            }
        }
gm_lb1:;
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
extern __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_compute_binding_cl3ogob(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, const void * a3, void * a4) {
    struct gm_fr_compute_binding_cl3ogob F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l2; /* module */
    gm_t08olq97rscn3 gm_l3; /* name */
    void * gm_l4; /* b */
    gm_tl874t2atnlh3 gm_l6; /* between */
    gm_b16_8 gm_l7; /* text */
    void * gm_l8; /* init */
    uint64_t gm_l9; /* m */
    void * gm_l10; /* blk */
    void * gm_l11; /* lit */
    void * gm_l12; /* lit */
    gm_tnpifv20irnph gm_l13; /* set */
    gm_t88rcucg1b7uk gm_l14; /* ty */
    void * gm_l15; /* inner */
    gm_t88rcucg1b7uk gm_l16; /* ty */
    GM_STACK_CHECK(&F, 2904u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l4 = a4;
    uint8_t gm_l19;
    uint8_t gm_l20;
    gm_t6qooom8qa2do gm_l21;
    uint8_t gm_l22;
    uint8_t gm_l23;
    uint8_t gm_l24;
    uint8_t gm_l25;
    uint8_t gm_l26;
    gm_t6qooom8qa2do gm_l27;
    uint8_t gm_l28;
    uint8_t gm_l29;
    gm_t6qooom8qa2do gm_l30;
    uint8_t gm_l31;
    gm_tkv1vap90vht2 gm_l32;
    gm_t6qooom8qa2do gm_l33;
    {
        void * t60 = gm_l4;
        gm_t9qkh0rsooc8d t61 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t60) + 32));
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t61) + 0));
        uint8_t t63 = (uint8_t)((t62 == ((uint8_t)1ULL)));
        gm_t9qkh0rsooc8d t64;
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)1ULL));
        if (t63) {
            void * t65 = gm_l4;
            void * t66 = GM_LD(void *, (((uint8_t *)t65) + 40));
            {
                {
                    uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t66) + 8) + 0));
                    uint8_t t68 = (uint8_t)(t67 == ((uint8_t)38ULL));
                    if (!t68) {
                        goto gm_lb2;
                    }
                    gm_l19 = ((uint8_t)1);
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    gm_l19 = ((uint8_t)0);
                    goto gm_lb1;
                }
gm_lb3:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 987, 12);
            }
gm_lb1:;
            uint8_t t69 = gm_l19;
            if (t69) {
                void * t70 = gm_l0;
                void * t71 = GM_LD(void *, ((uint8_t *)t70));
                uint64_t t72 = gm_l1;
                gm_b0_1 t73 = gm_f_resolve_import_944uqa9((void *)NULL, cx, NULL, t71, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)11ULL));
                goto gm_fdone;
            }
            {
                void * t74 = gm_l4;
                gm_tl874t2atnlh3 t75 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t74) + 48));
                gm_b16_8 t76;
                GM_ST(void *, (uint8_t *)&t76, (void *)(gm_s6lnp3h2ojl1f));
                GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(76));
                gm_t4gut2c9fj1kt t77;
                gm_f_error_s6hn871((void *)&t77, (void *)NULL, cx, NULL, t75, t76);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l5 = t77;
            }
            void * t78 = gm_l4;
            uint32_t t79 = GM_LD(uint32_t, (((uint8_t *)t78) + 48));
            uint32_t t80 = GM_LD(uint32_t, ((((uint8_t *)a3) + 16) + 8));
            void * t81 = gm_l4;
            void * t82 = GM_LD(void *, (((uint8_t *)t81) + 40));
            uint32_t t83 = GM_LD(uint32_t, ((((uint8_t *)t82) + 72) + 4));
            gm_tl874t2atnlh3 t84;
            GM_ST(uint32_t, ((uint8_t *)&t84 + 0), t79);
            GM_ST(uint32_t, ((uint8_t *)&t84 + 4), t80);
            GM_ST(uint32_t, ((uint8_t *)&t84 + 8), t83);
            gm_l6 = t84;
            void * t85 = gm_l0;
            void * t86 = GM_LD(void *, ((uint8_t *)t85));
            void * t87 = GM_LD(void *, ((uint8_t *)t86));
            uint32_t t88 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
            void * t89 = gm_f_file_o15fqku(t87, t88);
            void * t90 = GM_LD(void *, (((uint8_t *)t89) + 16));
            uint64_t t91 = GM_LD(uint64_t, ((((uint8_t *)t89) + 16) + 8));
            uint32_t t92 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
            uint64_t t93 = (uint64_t)t92;
            uint32_t t94 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
            uint64_t t95 = (uint64_t)t94;
            uint64_t t96 = (uintptr_t)t93;
            uint64_t t97 = (uintptr_t)t95;
            if (t96 > t97 || t97 > (uintptr_t)t91) gm_trap_slice(t96, t97, t91, gm_srclv0dcbbfhves, 995, 17);
            uint64_t t98 = ((uintptr_t)t95) - (uintptr_t)t93;
            gm_b16_8 t99;
            GM_ST(void *, (uint8_t *)&t99, (void *)(((uint8_t *)t90 + (uintptr_t)t93 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(t98));
            gm_l7 = t99;
            {
                {
                    void * t100 = gm_l4;
                    {
                        {
                            uint8_t t101 = (uint8_t)(GM_LD(void *, (((uint8_t *)t100) + 24)) != 0);
                            if (t101) {
                                goto gm_lb7;
                            }
                            gm_l20 = ((uint8_t)1);
                            goto gm_lb6;
                        }
gm_lb7:;
                        {
                            gm_l20 = ((uint8_t)0);
                            goto gm_lb6;
                        }
gm_lb8:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 996, 12);
                    }
gm_lb6:;
                    uint8_t t102 = gm_l20;
                    if (!t102) {
                        goto gm_lb5;
                    }
                    gm_b16_8 t103 = gm_l7;
                    gm_b16_8 t104 = gm_f_trim_fdrfe6i(t103);
                    gm_b16_8 t105;
                    GM_ST(void *, (uint8_t *)&t105, (void *)(gm_shge5n743ua4h));
                    GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(2));
                    uint8_t t106 = (gm_eq0iqiubfm7e(((uint8_t *)&t104), ((uint8_t *)&t105)));
                    if (!t106) {
                        goto gm_lb5;
                    }
                    void * t107 = (void *)((uint8_t *)&F.l5);
                    gm_b16_8 t108;
                    GM_ST(void *, (uint8_t *)&t108, (void *)(gm_spq5ja4c7ciga));
                    GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(10));
                    gm_tl874t2atnlh3 t109 = gm_l6;
                    gm_b16_8 t110;
                    GM_ST(void *, (uint8_t *)&t110, (void *)(gm_s2urait735ofq));
                    GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(4));
                    gm_b0_1 t111 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t107, t108, t109, t110);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb4;
                }
gm_lb5:;
            }
gm_lb4:;
            {
                void * t112 = gm_l0;
                gm_b0_1 t113 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t112, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l5)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)11ULL));
                goto gm_fdone;
            }
        }
    }
    {
        void * t114 = gm_l4;
        void * t115 = GM_LD(void *, (((uint8_t *)t114) + 40));
        gm_l8 = t115;
        void * t116 = gm_l8;
        {
            {
                {
                    uint8_t t117 = GM_LD(uint8_t, ((((uint8_t *)t116) + 8) + 0));
                    uint8_t t118 = (uint8_t)(t117 == ((uint8_t)38ULL));
                    if (!t118) {
                        goto gm_lb10;
                    }
                    void * t119 = gm_l0;
                    void * t120 = GM_LD(void *, ((uint8_t *)t119));
                    uint64_t t121 = gm_l1;
                    gm_b0_1 t122 = gm_f_resolve_import_944uqa9((void *)NULL, cx, NULL, t120, t121);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t123 = gm_l0;
                    void * t124 = GM_LD(void *, ((uint8_t *)t123));
                    void * t125 = GM_LD(void *, (((uint8_t *)t124) + 240));
                    uint64_t t126 = GM_LD(uint64_t, ((((uint8_t *)t124) + 240) + 8));
                    uint64_t t127 = gm_l1;
                    if ((uintptr_t)t127 >= (uintptr_t)(t126)) gm_trap_index((uintptr_t)t127, t126, gm_srclv0dcbbfhves, 1004, 26);
                    {
                        {
                            uint8_t t128 = GM_LD(uint8_t, (((((uint8_t *)t125) + (uintptr_t)t127 * 216) + 200) + 0));
                            uint8_t t129 = (uint8_t)(t128 == ((uint8_t)1ULL));
                            if (!t129) {
                                goto gm_lb12;
                            }
                            uint64_t t130 = GM_LD(uint64_t, (((((uint8_t *)t125) + (uintptr_t)t127 * 216) + 200) + 8));
                            gm_l9 = t130;
                            uint64_t t131 = gm_l9;
                            memset(&gm_l21, 0, sizeof gm_l21);
                            GM_ST(uint8_t, ((uint8_t *)&gm_l21 + 0), ((uint8_t)9ULL));
                            GM_ST(uint64_t, ((uint8_t *)&gm_l21 + 8), t131);
                            goto gm_lb11;
                        }
gm_lb12:;
                        {
                            memset(&gm_l21, 0, sizeof gm_l21);
                            GM_ST(uint8_t, ((uint8_t *)&gm_l21 + 0), ((uint8_t)11ULL));
                            goto gm_lb11;
                        }
gm_lb13:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1004, 20);
                    }
gm_lb11:;
                    memmove(gm_out, ((uint8_t *)&gm_l21), 208);
                    goto gm_fdone;
                }
gm_lb10:;
                {
                    uint8_t t132 = GM_LD(uint8_t, ((((uint8_t *)t116) + 8) + 0));
                    uint8_t t133 = (uint8_t)(t132 == ((uint8_t)39ULL));
                    if (!t133) {
                        goto gm_lb14;
                    }
                    void * t134 = GM_LD(void *, ((((uint8_t *)t116) + 8) + 8));
                    gm_l10 = t134;
                    void * t135 = gm_l4;
                    {
                        {
                            uint8_t t136 = (uint8_t)(GM_LD(void *, (((uint8_t *)t135) + 24)) != 0);
                            if (t136) {
                                goto gm_lb16;
                            }
                            gm_l22 = ((uint8_t)1);
                            goto gm_lb15;
                        }
gm_lb16:;
                        {
                            gm_l22 = ((uint8_t)0);
                            goto gm_lb15;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1009, 29);
                    }
gm_lb15:;
                    uint8_t t137 = gm_l22;
                    if (t137) {
                        void * t138 = gm_l0;
                        uint64_t t139 = gm_l2;
                        void * t140 = gm_l10;
                        gm_t08olq97rscn3 t141 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a3));
                        gm_f_declare_foreign_6s86jje((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t138, t139, &t141, t140);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    goto gm_lb9;
                }
gm_lb14:;
                {
                    uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)t116) + 8) + 0));
                    uint8_t t143 = (uint8_t)(t142 == ((uint8_t)36ULL));
                    if (!t143) {
                        goto gm_lb18;
                    }
                    void * t144 = GM_LD(void *, ((((uint8_t *)t116) + 8) + 8));
                    gm_l11 = t144;
                    void * t145 = gm_l4;
                    {
                        {
                            uint8_t t146 = (uint8_t)(GM_LD(void *, (((uint8_t *)t145) + 24)) != 0);
                            if (t146) {
                                goto gm_lb20;
                            }
                            gm_l23 = ((uint8_t)1);
                            goto gm_lb19;
                        }
gm_lb20:;
                        {
                            gm_l23 = ((uint8_t)0);
                            goto gm_lb19;
                        }
gm_lb21:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1010, 24);
                    }
gm_lb19:;
                    uint8_t t147 = gm_l23;
                    if (t147) {
                        void * t148 = gm_l0;
                        uint64_t t149 = gm_l2;
                        void * t150 = gm_l11;
                        gm_t08olq97rscn3 t151 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a3));
                        gm_f_declare_fn_6piolnr((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t148, t149, &t151, t150);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    goto gm_lb9;
                }
gm_lb18:;
            }
            {
                {
                    uint8_t t152 = GM_LD(uint8_t, ((((uint8_t *)t116) + 8) + 0));
                    uint8_t t153 = (uint8_t)(t152 == ((uint8_t)34ULL));
                    if (!t153) {
                        goto gm_lb22;
                    }
                    void * t154 = GM_LD(void *, ((((uint8_t *)t116) + 8) + 8));
                    gm_l12 = t154;
                    void * t155 = gm_l4;
                    {
                        {
                            uint8_t t156 = (uint8_t)(GM_LD(void *, (((uint8_t *)t155) + 24)) != 0);
                            if (t156) {
                                goto gm_lb24;
                            }
                            gm_l24 = ((uint8_t)1);
                            goto gm_lb23;
                        }
gm_lb24:;
                        {
                            gm_l24 = ((uint8_t)0);
                            goto gm_lb23;
                        }
gm_lb25:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1011, 28);
                    }
gm_lb23:;
                    uint8_t t157 = gm_l24;
                    if (t157) {
                        void * t158 = gm_l0;
                        uint64_t t159 = gm_l2;
                        uint64_t t160 = gm_l1;
                        void * t161 = gm_l12;
                        gm_t08olq97rscn3 t162 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a3));
                        gm_f_declare_effect_9n12hc8((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t158, t159, t160, &t162, t161);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_fdone;
                    }
                    goto gm_lb9;
                }
gm_lb22:;
                {
                    uint8_t t163 = GM_LD(uint8_t, ((((uint8_t *)t116) + 8) + 0));
                    uint8_t t164 = (uint8_t)(t163 == ((uint8_t)13ULL));
                    if (!t164) {
                        goto gm_lb26;
                    }
                    uint8_t t165 = GM_LD(uint8_t, (((((uint8_t *)t116) + 8) + 8) + 0));
                    uint8_t t166 = (uint8_t)(t165 == ((uint8_t)0ULL));
                    if (!t166) {
                        goto gm_lb26;
                    }
                    uint8_t t167 = GM_LD(uint8_t, ((((((uint8_t *)t116) + 8) + 8) + 1) + 0));
                    uint8_t t168 = (uint8_t)(t167 == ((uint8_t)0ULL));
                    if (!t168) {
                        goto gm_lb26;
                    }
                    {
                        {
                            void * t169 = gm_l4;
                            {
                                {
                                    uint8_t t170 = (uint8_t)(GM_LD(void *, (((uint8_t *)t169) + 24)) != 0);
                                    if (t170) {
                                        goto gm_lb30;
                                    }
                                    gm_l25 = ((uint8_t)1);
                                    goto gm_lb29;
                                }
gm_lb30:;
                                {
                                    gm_l25 = ((uint8_t)0);
                                    goto gm_lb29;
                                }
gm_lb31:;
                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1014, 16);
                            }
gm_lb29:;
                            uint8_t t171 = gm_l25;
                            if (!t171) {
                                goto gm_lb28;
                            }
                            void * t172 = gm_l0;
                            uint64_t t173 = gm_l2;
                            void * t174 = gm_l8;
                            uint8_t t175 = gm_f_is_effect_sum_dd9le16((void *)NULL, cx, NULL, t172, t173, t174);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            if (!t175) {
                                goto gm_lb28;
                            }
                            void * t176 = gm_l0;
                            uint64_t t177 = gm_l2;
                            void * t178 = gm_l8;
                            gm_b16_8 t179 = gm_f_effect_terms_bphj7th((void *)NULL, cx, NULL, t178);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t180 = GM_LD(gm_b16_8, ((uint8_t *)&t179));
                            gm_tnpifv20irnph t181 = gm_f_resolve_effect_set_o7rc5od((void *)NULL, cx, NULL, t176, t177, t180);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l13 = t181;
                            gm_tnpifv20irnph t182 = gm_l13;
                            uint8_t t183 = gm_f_has_splice_80pgqas(t182);
                            if (t183) {
                                void * t184 = gm_l0;
                                void * t185 = gm_l8;
                                gm_tl874t2atnlh3 t186 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t185) + 72));
                                gm_b16_8 t187;
                                GM_ST(void *, (uint8_t *)&t187, (void *)(gm_s9ocqto6ckbos));
                                GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(57));
                                gm_b0_1 t188 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t184, t186, t187);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)11ULL));
                                goto gm_fdone;
                            }
                            gm_tnpifv20irnph t189 = gm_l13;
                            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)4ULL));
                            GM_ST(gm_tnpifv20irnph, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 8), t189);
                            goto gm_fdone;
                        }
gm_lb28:;
                    }
gm_lb27:;
                    goto gm_lb9;
                }
gm_lb26:;
            }
            {
                {
                    uint8_t t190 = GM_LD(uint8_t, ((((uint8_t *)t116) + 8) + 0));
                    uint8_t t191 = (uint8_t)(t190 == ((uint8_t)37ULL));
                    if (!t191) {
                        goto gm_lb32;
                    }
                    void * t192 = gm_l0;
                    void * t193 = gm_l8;
                    gm_tl874t2atnlh3 t194 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t193) + 72));
                    gm_b16_8 t195;
                    GM_ST(void *, (uint8_t *)&t195, (void *)(gm_s6ibuqseh9tt7));
                    GM_ST(uintptr_t, (uint8_t *)&t195 + 8, (uintptr_t)(151));
                    gm_b0_1 t196 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t192, t194, t195);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)11ULL));
                    goto gm_fdone;
                }
gm_lb32:;
                {
                    {
                        {
                            uint8_t t197 = GM_LD(uint8_t, ((((uint8_t *)t116) + 8) + 0));
                            uint8_t t198 = (uint8_t)(t197 == ((uint8_t)32ULL));
                            if (!t198) {
                                goto gm_lb35;
                            }
                            goto gm_lb34;
                        }
gm_lb35:;
                        {
                            uint8_t t199 = GM_LD(uint8_t, ((((uint8_t *)t116) + 8) + 0));
                            uint8_t t200 = (uint8_t)(t199 == ((uint8_t)33ULL));
                            if (!t200) {
                                goto gm_lb36;
                            }
                            goto gm_lb34;
                        }
gm_lb36:;
                        goto gm_lb33;
                    }
gm_lb34:;
                    void * t201 = gm_l4;
                    {
                        {
                            uint8_t t202 = (uint8_t)(GM_LD(void *, (((uint8_t *)t201) + 24)) != 0);
                            if (t202) {
                                goto gm_lb38;
                            }
                            gm_l26 = ((uint8_t)1);
                            goto gm_lb37;
                        }
gm_lb38:;
                        {
                            gm_l26 = ((uint8_t)0);
                            goto gm_lb37;
                        }
gm_lb39:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1028, 16);
                    }
gm_lb37:;
                    uint8_t t203 = gm_l26;
                    if (t203) {
                        void * t204 = gm_l0;
                        uint64_t t205 = gm_l2;
                        uint64_t t206 = gm_l1;
                        gm_tf5srh4mp4b3i t207;
                        GM_ST(uint8_t, (uint8_t *)&t207 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t207 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t207 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t207), t206);
                        ((uint8_t *)&t207)[8] = 1;
                        gm_b16_8 t208 = GM_LD(gm_b16_8, ((uint8_t *)a3));
                        void * t209 = gm_l8;
                        gm_t88rcucg1b7uk t210 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t204, t205, t207, t208, t209);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l14 = t210;
                        void * t211 = gm_l0;
                        void * t212 = (void *)(((uint8_t *)t211) + 8);
                        gm_t88rcucg1b7uk t213 = gm_l14;
                        uint8_t t214 = gm_f_is_error_8k27gd1(t212, GM_CONV(gm_csguankko4lmhd, t213));
                        if (t214) {
                            memset(&gm_l27, 0, sizeof gm_l27);
                            GM_ST(uint8_t, ((uint8_t *)&gm_l27 + 0), ((uint8_t)11ULL));
                        } else {
                            gm_t88rcucg1b7uk t215 = gm_l14;
                            memset(&gm_l27, 0, sizeof gm_l27);
                            GM_ST(uint8_t, ((uint8_t *)&gm_l27 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l27 + 8), t215);
                        }
                        memmove(gm_out, ((uint8_t *)&gm_l27), 208);
                        goto gm_fdone;
                    }
                    goto gm_lb9;
                }
gm_lb33:;
                {
                    uint8_t t216 = GM_LD(uint8_t, ((((uint8_t *)t116) + 8) + 0));
                    uint8_t t217 = (uint8_t)(t216 == ((uint8_t)35ULL));
                    if (!t217) {
                        goto gm_lb40;
                    }
                    void * t218 = GM_LD(void *, (((((uint8_t *)t116) + 8) + 8) + 16));
                    gm_l15 = t218;
                    {
                        {
                            void * t219 = gm_l4;
                            {
                                {
                                    uint8_t t220 = (uint8_t)(GM_LD(void *, (((uint8_t *)t219) + 24)) != 0);
                                    if (t220) {
                                        goto gm_lb44;
                                    }
                                    gm_l28 = ((uint8_t)1);
                                    goto gm_lb43;
                                }
gm_lb44:;
                                {
                                    gm_l28 = ((uint8_t)0);
                                    goto gm_lb43;
                                }
gm_lb45:;
                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1034, 16);
                            }
gm_lb43:;
                            uint8_t t221 = gm_l28;
                            if (!t221) {
                                goto gm_lb42;
                            }
                            void * t222 = gm_l15;
                            {
                                {
                                    uint8_t t223 = GM_LD(uint8_t, ((((uint8_t *)t222) + 8) + 0));
                                    uint8_t t224 = (uint8_t)(t223 == ((uint8_t)32ULL));
                                    if (!t224) {
                                        goto gm_lb47;
                                    }
                                    gm_l29 = ((uint8_t)1);
                                    goto gm_lb46;
                                }
gm_lb47:;
                                {
                                    uint8_t t225 = GM_LD(uint8_t, ((((uint8_t *)t222) + 8) + 0));
                                    uint8_t t226 = (uint8_t)(t225 == ((uint8_t)33ULL));
                                    if (!t226) {
                                        goto gm_lb48;
                                    }
                                    gm_l29 = ((uint8_t)1);
                                    goto gm_lb46;
                                }
gm_lb48:;
                                {
                                    gm_l29 = ((uint8_t)0);
                                    goto gm_lb46;
                                }
gm_lb49:;
                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1034, 33);
                            }
gm_lb46:;
                            uint8_t t227 = gm_l29;
                            if (!t227) {
                                goto gm_lb42;
                            }
                            void * t228 = gm_l0;
                            uint64_t t229 = gm_l2;
                            uint64_t t230 = gm_l1;
                            gm_tf5srh4mp4b3i t231;
                            GM_ST(uint8_t, (uint8_t *)&t231 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t231 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t231 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t231), t230);
                            ((uint8_t *)&t231)[8] = 1;
                            gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)a3));
                            void * t233 = gm_l8;
                            gm_t88rcucg1b7uk t234 = gm_f_declare_adt_iefkbkk((void *)NULL, cx, NULL, t228, t229, t231, t232, t233);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l16 = t234;
                            void * t235 = gm_l0;
                            void * t236 = (void *)(((uint8_t *)t235) + 8);
                            gm_t88rcucg1b7uk t237 = gm_l16;
                            uint8_t t238 = gm_f_is_error_8k27gd1(t236, GM_CONV(gm_csguankko4lmhd, t237));
                            if (t238) {
                                memset(&gm_l30, 0, sizeof gm_l30);
                                GM_ST(uint8_t, ((uint8_t *)&gm_l30 + 0), ((uint8_t)11ULL));
                            } else {
                                gm_t88rcucg1b7uk t239 = gm_l16;
                                memset(&gm_l30, 0, sizeof gm_l30);
                                GM_ST(uint8_t, ((uint8_t *)&gm_l30 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l30 + 8), t239);
                            }
                            memmove(gm_out, ((uint8_t *)&gm_l30), 208);
                            goto gm_fdone;
                        }
gm_lb42:;
                    }
gm_lb41:;
                    goto gm_lb9;
                }
gm_lb40:;
            }
            {
                {
                    goto gm_lb9;
                }
gm_lb50:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1001, 5);
            }
        }
gm_lb9:;
    }
    {
        void * t240 = gm_l4;
        {
            {
                uint8_t t241 = (uint8_t)(GM_LD(void *, (((uint8_t *)t240) + 24)) != 0);
                if (t241) {
                    goto gm_lb52;
                }
                gm_l31 = ((uint8_t)1);
                goto gm_lb51;
            }
gm_lb52:;
            {
                gm_l31 = ((uint8_t)0);
                goto gm_lb51;
            }
gm_lb53:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1042, 8);
        }
gm_lb51:;
        uint8_t t242 = gm_l31;
        if (t242) {
            {
                {
                    {
                        void * t243 = gm_l0;
                        uint64_t t244 = gm_l2;
                        void * t245 = gm_l8;
                        gm_f_try_untyped_fj0h8i5((void *)&gm_l32, (void *)NULL, cx, NULL, t243, t244, t245);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t246 = (uint8_t)((((uint8_t *)&gm_l32))[208] != 0);
                    if (!t246) {
                        goto gm_lb55;
                    }
                    {
                        memmove(((uint8_t *)&F.l17), ((uint8_t *)&gm_l32), 208);
                    }
                    {
                        {
                            uint8_t t247 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                            uint8_t t248 = (uint8_t)(t247 == ((uint8_t)0ULL));
                            if (!t248) {
                                goto gm_lb57;
                            }
                            {
                                memmove(((uint8_t *)&F.l18), (((uint8_t *)&F.l17) + 8), 200);
                            }
                            {
                                gm_t0ialelq3ql1k t249 = GM_LD(gm_t0ialelq3ql1k, ((uint8_t *)&F.l18));
                                GM_ST(uint8_t, (uint8_t *)&gm_l33 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&gm_l33 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&gm_l33 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&gm_l33 + 0), ((uint8_t)8ULL));
                                GM_ST(gm_t0ialelq3ql1k, ((uint8_t *)&gm_l33 + 8), t249);
                            }
                            goto gm_lb56;
                        }
gm_lb57:;
                        {
                            uint8_t t250 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                            uint8_t t251 = (uint8_t)(t250 == ((uint8_t)1ULL));
                            if (!t251) {
                                goto gm_lb58;
                            }
                            memset(&gm_l33, 0, sizeof gm_l33);
                            GM_ST(uint8_t, ((uint8_t *)&gm_l33 + 0), ((uint8_t)11ULL));
                            goto gm_lb56;
                        }
gm_lb58:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1044, 20);
                    }
gm_lb56:;
                    {
                        memmove(gm_out, ((uint8_t *)&gm_l33), 208);
                        goto gm_fdone;
                    }
                }
gm_lb55:;
            }
gm_lb54:;
        }
        void * t252 = gm_l0;
        uint64_t t253 = gm_l2;
        void * t254 = gm_l4;
        void * t255 = GM_LD(void *, (((uint8_t *)t254) + 24));
        void * t256 = gm_l8;
        gm_t08olq97rscn3 t257 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)a3));
        gm_f_declare_const_or_type_4jaj4k6((void *)&(*(gm_t6qooom8qa2do *)gm_out), (void *)NULL, cx, NULL, t252, t253, &t257, t255, t256);
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
__attribute__((aligned(4))) void gm_f_compute_decl_gcf8mq9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_compute_decl_gcf8mq9 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l4; /* module */
    gm_t08olq97rscn3 gm_l5; /* name */
    uint8_t gm_l7; /* global */
    uint8_t gm_l8; /* thread_local */
    gm_tf5srh4mp4b3i gm_l9; /* fuel */
    uint8_t gm_l11; /* inline */
    gm_tbcgm40aljf9h gm_l12; /* hint_set */
    gm_t84dv0cg48dut gm_l15; /* callconv */
    gm_toceffb697p0u gm_l16; /* trap_hook */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    uint8_t gm_l20; /* ok */
    gm_b16_8 gm_l21; /* args */
    uint64_t gm_l24; /* n */
    uint8_t gm_l25; /* ok */
    gm_b16_8 gm_l26; /* args */
    gm_tibupt4dmcqai gm_l27; /* lit */
    gm_b16_8 gm_l28; /* args */
    uint8_t gm_l29; /* w */
    uint8_t gm_l30; /* ok */
    gm_b16_8 gm_l31; /* args */
    gm_b16_8 gm_l32; /* other */
    gm_tf5srh4mp4b3i gm_l33; /* global_align */
    uint64_t gm_l36; /* saved_fuel */
    gm_t88rcucg1b7uk gm_l37; /* ty */
    gm_tbvif938ns15l gm_l38; /* c */
    uint32_t gm_l39; /* f */
    uint32_t gm_l41; /* f */
    uint32_t gm_l42; /* f */
    gm_tl874t2atnlh3 gm_l43; /* span */
    uint32_t gm_l44; /* f */
    gm_t08olq97rscn3 gm_l45; /* e */
    uint32_t gm_l46; /* f */
    GM_STACK_CHECK(&F, 3624u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l47;
    gm_tkv1vap90vht2 gm_l48;
    gm_tf5srh4mp4b3i gm_l49;
    uint8_t gm_l50;
    gm_tqs5p4n2tq19v gm_l51;
    uint8_t gm_l52;
    uint8_t gm_l53;
    gm_tabb710d9mq2e gm_l54;
    gm_tabb710d9mq2e gm_l55;
    uint64_t gm_l56;
    gm_tftb1odc739t5 gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_tftb1odc739t5 gm_l59;
    uint8_t gm_l60;
    gm_tabb710d9mq2e gm_l61;
    {
        void * t90 = gm_l0;
        void * t91 = GM_LD(void *, ((uint8_t *)t90));
        void * t92 = GM_LD(void *, (((uint8_t *)t91) + 240));
        uint64_t t93 = GM_LD(uint64_t, ((((uint8_t *)t91) + 240) + 8));
        uint64_t t94 = gm_l1;
        if ((uintptr_t)t94 >= (uintptr_t)(t93)) gm_trap_index((uintptr_t)t94, t93, gm_srclv0dcbbfhves, 837, 13);
        memmove(((uint8_t *)&F.l2), (((uint8_t *)t92) + (uintptr_t)t94 * 216), 216);
        memmove(((uint8_t *)&F.l3), (((uint8_t *)&F.l2) + 40), 160);
        uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 32));
        gm_l4 = t95;
        memmove(((uint8_t *)&gm_l5), ((uint8_t *)&F.l2), 32);
        {
            {
                {
                    {
                        uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 64) + 0));
                        uint8_t t97 = (uint8_t)(t96 == ((uint8_t)0ULL));
                        if (!t97) {
                            goto gm_lb2;
                        }
                        memmove(((uint8_t *)&F.l6), ((((uint8_t *)&F.l3) + 64) + 8), 64);
                        gm_l7 = ((uint8_t)0);
                        gm_l8 = ((uint8_t)0);
                        gm_tf5srh4mp4b3i t98;
                        memset(&t98, 0, sizeof t98);
                        gm_l9 = t98;
                        memset(&F.l10, 0, sizeof F.l10);
                        gm_l11 = ((uint8_t)0);
                        gm_tqs5p4n2tq19v t99;
                        memset(&t99, 0, sizeof t99);
                        gm_tbcgm40aljf9h t100;
                        GM_ST(uint32_t, ((uint8_t *)&t100 + 0), ((uint32_t)0ULL));
                        GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t100 + 4), t99);
                        GM_ST(uint8_t, ((uint8_t *)&t100 + 6), ((uint8_t)0));
                        GM_ST(uint8_t, ((uint8_t *)&t100 + 7), ((uint8_t)0));
                        gm_l12 = t100;
                        memset(&F.l13, 0, sizeof F.l13);
                        memset(&F.l14, 0, sizeof F.l14);
                        memset(&gm_l15, 0, sizeof gm_l15);
                        gm_toceffb697p0u t101;
                        memset(&t101, 0, sizeof t101);
                        gm_l16 = t101;
                        gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
                        gm_l17 = t102;
                        gm_l18 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                {
                                    uint64_t t103 = gm_l18;
                                    uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                    uint8_t t105 = (t103 < t104);
                                    if (!t105) {
                                        goto gm_lb3;
                                    }
                                    {
                                        {
                                            void * t106 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                            uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                            uint64_t t108 = gm_l18;
                                            {
                                                {
                                                    {
                                                        {
                                                            memmove(((uint8_t *)&F.l19), (((uint8_t *)t106) + (uintptr_t)t108 * 72), 72);
                                                            {
                                                                {
                                                                    {
                                                                        gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                        gm_b16_8 t110;
                                                                        GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sptnm8c6c52dr));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(6));
                                                                        uint8_t t111 = (gm_eq0iqiubfm7e(((uint8_t *)&t109), ((uint8_t *)&t110)));
                                                                        if (!t111) {
                                                                            goto gm_lb9;
                                                                        }
                                                                        gm_l7 = ((uint8_t)1);
                                                                        goto gm_lb8;
                                                                    }
gm_lb9:;
                                                                    {
                                                                        gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                        gm_b16_8 t113;
                                                                        GM_ST(void *, (uint8_t *)&t113, (void *)(gm_sj1k4t2de5144));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(5));
                                                                        uint8_t t114 = (gm_eq0iqiubfm7e(((uint8_t *)&t112), ((uint8_t *)&t113)));
                                                                        if (!t114) {
                                                                            goto gm_lb10;
                                                                        }
                                                                        {
                                                                            {
                                                                                uint8_t t115 = (uint8_t)((((uint8_t *)&F.l13))[72] != 0);
                                                                                if (t115) {
                                                                                    goto gm_lb12;
                                                                                }
                                                                                gm_l47 = ((uint8_t)1);
                                                                                goto gm_lb11;
                                                                            }
gm_lb12:;
                                                                            {
                                                                                gm_l47 = ((uint8_t)0);
                                                                                goto gm_lb11;
                                                                            }
gm_lb13:;
                                                                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 857, 20);
                                                                        }
gm_lb11:;
                                                                        uint8_t t116 = gm_l47;
                                                                        if (t116) {
                                                                            gm_tkacitepg5fk0 t117 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)&F.l19));
                                                                            GM_ST(uint8_t, (uint8_t *)&F.l13 + 73, 0);
                                                                            GM_ST(uint16_t, (uint8_t *)&F.l13 + 74, 0);
                                                                            GM_ST(uint32_t, (uint8_t *)&F.l13 + 76, 0);
                                                                            GM_ST(gm_tkacitepg5fk0, ((uint8_t *)&F.l13), t117);
                                                                            ((uint8_t *)&F.l13)[72] = 1;
                                                                        } else {
                                                                            void * t118 = gm_l0;
                                                                            gm_tl874t2atnlh3 t119 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                                                            gm_b16_8 t120;
                                                                            GM_ST(void *, (uint8_t *)&t120, (void *)(gm_scacg345bm166));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(68));
                                                                            gm_b0_1 t121 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t118, t119, t120);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                        }
                                                                        goto gm_lb8;
                                                                    }
gm_lb10:;
                                                                    {
                                                                        gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                        gm_b16_8 t123;
                                                                        GM_ST(void *, (uint8_t *)&t123, (void *)(gm_s523ivho5m3dq));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(12));
                                                                        uint8_t t124 = (gm_eq0iqiubfm7e(((uint8_t *)&t122), ((uint8_t *)&t123)));
                                                                        if (!t124) {
                                                                            goto gm_lb14;
                                                                        }
                                                                        gm_l8 = ((uint8_t)1);
                                                                        goto gm_lb8;
                                                                    }
gm_lb14:;
                                                                    {
                                                                        gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                        gm_b16_8 t126;
                                                                        GM_ST(void *, (uint8_t *)&t126, (void *)(gm_stjei6jfiba56));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(4));
                                                                        uint8_t t127 = (gm_eq0iqiubfm7e(((uint8_t *)&t125), ((uint8_t *)&t126)));
                                                                        if (!t127) {
                                                                            goto gm_lb15;
                                                                        }
                                                                        gm_l20 = ((uint8_t)0);
                                                                        {
                                                                            {
                                                                                uint8_t t128 = (uint8_t)(((((uint8_t *)&F.l19) + 32))[16] != 0);
                                                                                if (!t128) {
                                                                                    goto gm_lb17;
                                                                                }
                                                                                gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 32));
                                                                                gm_l21 = t129;
                                                                                uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                                                                uint8_t t131 = ((t130 == ((uint64_t)1ULL)));
                                                                                if (!t131) {
                                                                                    goto gm_lb17;
                                                                                }
                                                                                {
                                                                                    {
                                                                                        {
                                                                                            void * t132 = gm_l0;
                                                                                            uint64_t t133 = gm_l4;
                                                                                            void * t134 = GM_LD(void *, ((uint8_t *)&gm_l21));
                                                                                            uint64_t t135 = GM_LD(uint64_t, (((uint8_t *)&gm_l21) + 8));
                                                                                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t135)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t135, gm_srclv0dcbbfhves, 868, 52);
                                                                                            void * t136 = (void *)(((uint8_t *)t134) + (uintptr_t)((uint64_t)0ULL) * 88);
                                                                                            gm_f_try_untyped_fj0h8i5((void *)&gm_l48, (void *)NULL, cx, NULL, t132, t133, t136);
                                                                                            if (gm_unw.kind) {
                                                                                                goto gm_lad;
                                                                                            }
                                                                                        }
                                                                                        uint8_t t137 = (uint8_t)((((uint8_t *)&gm_l48))[208] != 0);
                                                                                        if (!t137) {
                                                                                            goto gm_lb19;
                                                                                        }
                                                                                        {
                                                                                            memmove(((uint8_t *)&F.l22), ((uint8_t *)&gm_l48), 208);
                                                                                        }
                                                                                        uint8_t t138 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 0));
                                                                                        uint8_t t139 = (uint8_t)(t138 == ((uint8_t)0ULL));
                                                                                        if (!t139) {
                                                                                            goto gm_lb19;
                                                                                        }
                                                                                        uint8_t t140 = GM_LD(uint8_t, ((((uint8_t *)&F.l22) + 8) + 0));
                                                                                        uint8_t t141 = (uint8_t)(t140 == ((uint8_t)0ULL));
                                                                                        if (!t141) {
                                                                                            goto gm_lb19;
                                                                                        }
                                                                                        {
                                                                                            memmove(((uint8_t *)&F.l23), ((((uint8_t *)&F.l22) + 8) + 8), 48);
                                                                                        }
                                                                                        {
                                                                                            uint8_t t142 = gm_f_is_positive_su660ab(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l23)));
                                                                                            if (!t142) {
                                                                                                goto gm_lb19;
                                                                                            }
                                                                                        }
                                                                                        {
                                                                                            {
                                                                                                gm_tf5srh4mp4b3i t143 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l23)));
                                                                                                gm_l49 = t143;
                                                                                                uint8_t t144 = (uint8_t)((((uint8_t *)&gm_l49))[8] != 0);
                                                                                                if (!t144) {
                                                                                                    goto gm_lb21;
                                                                                                }
                                                                                                uint64_t t145 = GM_LD(uint64_t, ((uint8_t *)&gm_l49));
                                                                                                gm_l24 = t145;
                                                                                                uint64_t t146 = gm_l24;
                                                                                                gm_tf5srh4mp4b3i t147;
                                                                                                GM_ST(uint8_t, (uint8_t *)&t147 + 9, 0);
                                                                                                GM_ST(uint16_t, (uint8_t *)&t147 + 10, 0);
                                                                                                GM_ST(uint32_t, (uint8_t *)&t147 + 12, 0);
                                                                                                GM_ST(uint64_t, ((uint8_t *)&t147), t146);
                                                                                                ((uint8_t *)&t147)[8] = 1;
                                                                                                gm_l9 = t147;
                                                                                                gm_l20 = ((uint8_t)1);
                                                                                                goto gm_lb20;
                                                                                            }
gm_lb21:;
                                                                                        }
gm_lb20:;
                                                                                        goto gm_lb18;
                                                                                    }
gm_lb19:;
                                                                                }
gm_lb18:;
                                                                                goto gm_lb16;
                                                                            }
gm_lb17:;
                                                                        }
gm_lb16:;
                                                                        uint8_t t148 = gm_l20;
                                                                        uint8_t t149 = (uint8_t)!t148;
                                                                        if (t149) {
                                                                            void * t150 = gm_l0;
                                                                            gm_tl874t2atnlh3 t151 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                                                            gm_b16_8 t152;
                                                                            GM_ST(void *, (uint8_t *)&t152, (void *)(gm_st027n235d4us));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(44));
                                                                            gm_b0_1 t153 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t150, t151, t152);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                        }
                                                                        goto gm_lb8;
                                                                    }
gm_lb15:;
                                                                }
                                                                {
                                                                    {
                                                                        gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                        gm_b16_8 t155;
                                                                        GM_ST(void *, (uint8_t *)&t155, (void *)(gm_spe8t3e79vnal));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(6));
                                                                        uint8_t t156 = (gm_eq0iqiubfm7e(((uint8_t *)&t154), ((uint8_t *)&t155)));
                                                                        if (!t156) {
                                                                            goto gm_lb22;
                                                                        }
                                                                        gm_l25 = ((uint8_t)0);
                                                                        {
                                                                            {
                                                                                uint8_t t157 = (uint8_t)(((((uint8_t *)&F.l19) + 32))[16] != 0);
                                                                                if (!t157) {
                                                                                    goto gm_lb24;
                                                                                }
                                                                                gm_b16_8 t158 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 32));
                                                                                gm_l26 = t158;
                                                                                uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                                                                                uint8_t t160 = ((t159 == ((uint64_t)1ULL)));
                                                                                if (!t160) {
                                                                                    goto gm_lb24;
                                                                                }
                                                                                {
                                                                                    void * t161 = GM_LD(void *, ((uint8_t *)&gm_l26));
                                                                                    uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                                                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t162)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t162, gm_srclv0dcbbfhves, 879, 73);
                                                                                    uint8_t t163 = GM_LD(uint8_t, (((((uint8_t *)t161) + (uintptr_t)((uint64_t)0ULL) * 88) + 8) + 0));
                                                                                    uint8_t t164 = (uint8_t)(t163 == ((uint8_t)2ULL));
                                                                                    if (!t164) {
                                                                                        goto gm_lb24;
                                                                                    }
                                                                                    memmove(((uint8_t *)&gm_l27), (((((uint8_t *)t161) + (uintptr_t)((uint64_t)0ULL) * 88) + 8) + 8), 24);
                                                                                }
                                                                                gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l27));
                                                                                gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
                                                                                uint8_t t167 = gm_f_utf8_valid_a8oc1t8(t166);
                                                                                uint8_t t168 = (uint8_t)!t167;
                                                                                if (t168) {
                                                                                    void * t169 = gm_l0;
                                                                                    void * t170 = GM_LD(void *, ((uint8_t *)&gm_l26));
                                                                                    uint64_t t171 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
                                                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t171)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t171, gm_srclv0dcbbfhves, 880, 67);
                                                                                    gm_tl874t2atnlh3 t172 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t170) + (uintptr_t)((uint64_t)0ULL) * 88) + 72));
                                                                                    gm_b16_8 t173;
                                                                                    GM_ST(void *, (uint8_t *)&t173, (void *)(gm_shud0j6jb8ucc));
                                                                                    GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(36));
                                                                                    gm_b0_1 t174 = gm_f_unsupported_t7lgvp9((void *)NULL, cx, NULL, t169, t172, t173);
                                                                                    if (gm_unw.kind) {
                                                                                        goto gm_lad;
                                                                                    }
                                                                                }
                                                                                {
                                                                                    gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l27));
                                                                                    gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)&t175));
                                                                                    gm_tl874t2atnlh3 t177 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                                                                    gm_t08olq97rscn3 t178;
                                                                                    GM_ST(uint32_t, (uint8_t *)&t178 + 28, 0);
                                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t178 + 0), t176);
                                                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t178 + 16), t177);
                                                                                    GM_ST(uint8_t, (uint8_t *)&F.l14 + 33, 0);
                                                                                    GM_ST(uint16_t, (uint8_t *)&F.l14 + 34, 0);
                                                                                    GM_ST(uint32_t, (uint8_t *)&F.l14 + 36, 0);
                                                                                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&F.l14), t178);
                                                                                    ((uint8_t *)&F.l14)[32] = 1;
                                                                                }
                                                                                gm_l25 = ((uint8_t)1);
                                                                                goto gm_lb23;
                                                                            }
gm_lb24:;
                                                                        }
gm_lb23:;
                                                                        uint8_t t179 = gm_l25;
                                                                        uint8_t t180 = (uint8_t)!t179;
                                                                        if (t180) {
                                                                            void * t181 = gm_l0;
                                                                            gm_tl874t2atnlh3 t182 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                                                            gm_b16_8 t183;
                                                                            GM_ST(void *, (uint8_t *)&t183, (void *)(gm_srsiesc4al3v4));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t183 + 8, (uintptr_t)(49));
                                                                            gm_b0_1 t184 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t181, t182, t183);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                        }
                                                                        goto gm_lb8;
                                                                    }
gm_lb22:;
                                                                    {
                                                                        gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                        gm_b16_8 t186;
                                                                        GM_ST(void *, (uint8_t *)&t186, (void *)(gm_s1ft2nrvnhcbp));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(9));
                                                                        uint8_t t187 = (gm_eq0iqiubfm7e(((uint8_t *)&t185), ((uint8_t *)&t186)));
                                                                        if (!t187) {
                                                                            goto gm_lb25;
                                                                        }
                                                                        {
                                                                            {
                                                                                uint8_t t188 = (uint8_t)(((((uint8_t *)&F.l19) + 32))[16] != 0);
                                                                                if (!t188) {
                                                                                    goto gm_lb27;
                                                                                }
                                                                                gm_b16_8 t189 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 32));
                                                                                gm_l28 = t189;
                                                                                uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)&gm_l28) + 8));
                                                                                uint8_t t191 = (t190 > ((uint64_t)0ULL));
                                                                                if (!t191) {
                                                                                    goto gm_lb27;
                                                                                }
                                                                                void * t192 = gm_l0;
                                                                                gm_tl874t2atnlh3 t193 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                                                                gm_b16_8 t194;
                                                                                GM_ST(void *, (uint8_t *)&t194, (void *)(gm_sj2f7h0km68ut));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(31));
                                                                                gm_b0_1 t195 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t192, t193, t194);
                                                                                if (gm_unw.kind) {
                                                                                    goto gm_lad;
                                                                                }
                                                                                goto gm_lb26;
                                                                            }
gm_lb27:;
                                                                        }
gm_lb26:;
                                                                        gm_tl874t2atnlh3 t196 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                                                        gm_toceffb697p0u t197;
                                                                        GM_ST(uint8_t, (uint8_t *)&t197 + 13, 0);
                                                                        GM_ST(uint16_t, (uint8_t *)&t197 + 14, 0);
                                                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t197), t196);
                                                                        ((uint8_t *)&t197)[12] = 1;
                                                                        gm_l16 = t197;
                                                                        goto gm_lb8;
                                                                    }
gm_lb25:;
                                                                }
                                                                {
                                                                    {
                                                                        gm_b16_8 t198 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                        gm_b16_8 t199;
                                                                        GM_ST(void *, (uint8_t *)&t199, (void *)(gm_spvg0rt0dloah));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t199 + 8, (uintptr_t)(8));
                                                                        uint8_t t200 = (gm_eq0iqiubfm7e(((uint8_t *)&t198), ((uint8_t *)&t199)));
                                                                        if (!t200) {
                                                                            goto gm_lb28;
                                                                        }
                                                                        {
                                                                            {
                                                                                uint8_t t201 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                                                                                if (t201) {
                                                                                    goto gm_lb30;
                                                                                }
                                                                                gm_l50 = ((uint8_t)0);
                                                                                goto gm_lb29;
                                                                            }
gm_lb30:;
                                                                            {
                                                                                gm_l50 = ((uint8_t)1);
                                                                                goto gm_lb29;
                                                                            }
gm_lb31:;
                                                                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 891, 20);
                                                                        }
gm_lb29:;
                                                                        uint8_t t202 = gm_l50;
                                                                        if (t202) {
                                                                            void * t203 = gm_l0;
                                                                            gm_tl874t2atnlh3 t204 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                                                            gm_b16_8 t205;
                                                                            GM_ST(void *, (uint8_t *)&t205, (void *)(gm_str26682vod5l));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(37));
                                                                            gm_b0_1 t206 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t203, t204, t205);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                        }
                                                                        memset(&gm_l15, 0, sizeof gm_l15);
                                                                        {
                                                                            {
                                                                                void * t207 = gm_l0;
                                                                                void * t208 = (void *)((uint8_t *)&F.l19);
                                                                                gm_tqs5p4n2tq19v t209 = gm_f_callconv_attr_dd08ovc((void *)NULL, cx, NULL, t207, t208);
                                                                                if (gm_unw.kind) {
                                                                                    goto gm_lad;
                                                                                }
                                                                                gm_l51 = t209;
                                                                                uint8_t t210 = (uint8_t)((((uint8_t *)&gm_l51))[1] != 0);
                                                                                if (!t210) {
                                                                                    goto gm_lb33;
                                                                                }
                                                                                uint8_t t211 = GM_LD(uint8_t, ((uint8_t *)&gm_l51));
                                                                                gm_l29 = t211;
                                                                                uint8_t t212 = gm_l29;
                                                                                gm_tl874t2atnlh3 t213 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                                                                gm_tbvif938ns15l t214;
                                                                                GM_ST(uint8_t, (uint8_t *)&t214 + 1, 0);
                                                                                GM_ST(uint16_t, (uint8_t *)&t214 + 2, 0);
                                                                                GM_ST(uint8_t, ((uint8_t *)&t214 + 0), t212);
                                                                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t214 + 4), t213);
                                                                                GM_ST(uint8_t, (uint8_t *)&gm_l15 + 17, 0);
                                                                                GM_ST(uint16_t, (uint8_t *)&gm_l15 + 18, 0);
                                                                                GM_ST(gm_tbvif938ns15l, ((uint8_t *)&gm_l15), t214);
                                                                                ((uint8_t *)&gm_l15)[16] = 1;
                                                                                goto gm_lb32;
                                                                            }
gm_lb33:;
                                                                        }
gm_lb32:;
                                                                        goto gm_lb8;
                                                                    }
gm_lb28:;
                                                                    {
                                                                        {
                                                                            {
                                                                                gm_b16_8 t215 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                                gm_b16_8 t216;
                                                                                GM_ST(void *, (uint8_t *)&t216, (void *)(gm_s9e3360r5ee0o));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t216 + 8, (uintptr_t)(6));
                                                                                uint8_t t217 = (gm_eq0iqiubfm7e(((uint8_t *)&t215), ((uint8_t *)&t216)));
                                                                                if (!t217) {
                                                                                    goto gm_lb36;
                                                                                }
                                                                                goto gm_lb35;
                                                                            }
gm_lb36:;
                                                                            {
                                                                                gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                                gm_b16_8 t219;
                                                                                GM_ST(void *, (uint8_t *)&t219, (void *)(gm_s7f3pdsji9g3a));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t219 + 8, (uintptr_t)(8));
                                                                                uint8_t t220 = (gm_eq0iqiubfm7e(((uint8_t *)&t218), ((uint8_t *)&t219)));
                                                                                if (!t220) {
                                                                                    goto gm_lb37;
                                                                                }
                                                                                goto gm_lb35;
                                                                            }
gm_lb37:;
                                                                            {
                                                                                gm_b16_8 t221 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                                gm_b16_8 t222;
                                                                                GM_ST(void *, (uint8_t *)&t222, (void *)(gm_sq4sf3euttrc8));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t222 + 8, (uintptr_t)(4));
                                                                                uint8_t t223 = (gm_eq0iqiubfm7e(((uint8_t *)&t221), ((uint8_t *)&t222)));
                                                                                if (!t223) {
                                                                                    goto gm_lb38;
                                                                                }
                                                                                goto gm_lb35;
                                                                            }
gm_lb38:;
                                                                            {
                                                                                gm_b16_8 t224 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                                gm_b16_8 t225;
                                                                                GM_ST(void *, (uint8_t *)&t225, (void *)(gm_sinoph5a0op58));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t225 + 8, (uintptr_t)(9));
                                                                                uint8_t t226 = (gm_eq0iqiubfm7e(((uint8_t *)&t224), ((uint8_t *)&t225)));
                                                                                if (!t226) {
                                                                                    goto gm_lb39;
                                                                                }
                                                                                goto gm_lb35;
                                                                            }
gm_lb39:;
                                                                            goto gm_lb34;
                                                                        }
gm_lb35:;
                                                                        {
                                                                            gm_b16_8 t227 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                            gm_b16_8 t228;
                                                                            GM_ST(void *, (uint8_t *)&t228, (void *)(gm_s9e3360r5ee0o));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t228 + 8, (uintptr_t)(6));
                                                                            gm_b16_8 t229;
                                                                            GM_ST(void *, (uint8_t *)&t229, (void *)(gm_s7f3pdsji9g3a));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t229 + 8, (uintptr_t)(8));
                                                                            gm_t6j6smj7qoctu t230;
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t230 + 0), t228);
                                                                            GM_ST(gm_b16_8, ((uint8_t *)&t230 + 16), t229);
                                                                            uint8_t t231 = 0;
                                                                            if (gm_eq0iqiubfm7e(((uint8_t *)&t230) + 0, ((uint8_t *)&t227))) t231 = 1;
                                                                            if (gm_eq0iqiubfm7e(((uint8_t *)&t230) + 16, ((uint8_t *)&t227))) t231 = 1;
                                                                            uint8_t t232 = (uint8_t)(t231);
                                                                            if (t232) {
                                                                                gm_b16_8 t233 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                                gm_b16_8 t234;
                                                                                GM_ST(void *, (uint8_t *)&t234, (void *)(gm_s9e3360r5ee0o));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(6));
                                                                                uint8_t t235 = (gm_eq0iqiubfm7e(((uint8_t *)&t233), ((uint8_t *)&t234)));
                                                                                gm_l11 = t235;
                                                                                uint8_t t236 = gm_l11;
                                                                                gm_tqs5p4n2tq19v t237;
                                                                                GM_ST(uint8_t, ((uint8_t *)&t237), t236);
                                                                                ((uint8_t *)&t237)[1] = 1;
                                                                                GM_ST(gm_tqs5p4n2tq19v, (((uint8_t *)&gm_l12) + 4), t237);
                                                                            } else {
                                                                                gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                                gm_b16_8 t239;
                                                                                GM_ST(void *, (uint8_t *)&t239, (void *)(gm_sq4sf3euttrc8));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(4));
                                                                                uint8_t t240 = (gm_eq0iqiubfm7e(((uint8_t *)&t238), ((uint8_t *)&t239)));
                                                                                if (t240) {
                                                                                    GM_ST(uint8_t, (((uint8_t *)&gm_l12) + 6), ((uint8_t)1));
                                                                                } else {
                                                                                    GM_ST(uint8_t, (((uint8_t *)&gm_l12) + 7), ((uint8_t)1));
                                                                                }
                                                                            }
                                                                        }
                                                                        {
                                                                            gm_tkacitepg5fk0 t241 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)&F.l19));
                                                                            GM_ST(uint8_t, (uint8_t *)&F.l10 + 73, 0);
                                                                            GM_ST(uint16_t, (uint8_t *)&F.l10 + 74, 0);
                                                                            GM_ST(uint32_t, (uint8_t *)&F.l10 + 76, 0);
                                                                            GM_ST(gm_tkacitepg5fk0, ((uint8_t *)&F.l10), t241);
                                                                            ((uint8_t *)&F.l10)[72] = 1;
                                                                        }
                                                                        goto gm_lb8;
                                                                    }
gm_lb34:;
                                                                }
                                                                {
                                                                    {
                                                                        gm_b16_8 t242 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                        gm_b16_8 t243;
                                                                        GM_ST(void *, (uint8_t *)&t243, (void *)(gm_slb6cj7ndgvdm));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t243 + 8, (uintptr_t)(5));
                                                                        uint8_t t244 = (gm_eq0iqiubfm7e(((uint8_t *)&t242), ((uint8_t *)&t243)));
                                                                        if (!t244) {
                                                                            goto gm_lb40;
                                                                        }
                                                                        void * t245 = gm_l0;
                                                                        void * t246 = (void *)((uint8_t *)&F.l19);
                                                                        gm_b0_1 t247 = gm_f_check_allow_attr_3321gm7((void *)NULL, cx, NULL, t245, t246);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        goto gm_lb8;
                                                                    }
gm_lb40:;
                                                                    {
                                                                        gm_b16_8 t248 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                        gm_b16_8 t249;
                                                                        GM_ST(void *, (uint8_t *)&t249, (void *)(gm_smo0niiqr9onn));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t249 + 8, (uintptr_t)(10));
                                                                        uint8_t t250 = (gm_eq0iqiubfm7e(((uint8_t *)&t248), ((uint8_t *)&t249)));
                                                                        if (!t250) {
                                                                            goto gm_lb41;
                                                                        }
                                                                        gm_l30 = ((uint8_t)1);
                                                                        {
                                                                            {
                                                                                uint8_t t251 = (uint8_t)(((((uint8_t *)&F.l19) + 32))[16] != 0);
                                                                                if (!t251) {
                                                                                    goto gm_lb43;
                                                                                }
                                                                                gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)&F.l19) + 32));
                                                                                gm_l31 = t252;
                                                                                uint64_t t253 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                                                                uint8_t t254 = ((t253 == ((uint64_t)1ULL)));
                                                                                if (t254) {
                                                                                    void * t255 = GM_LD(void *, ((uint8_t *)&gm_l31));
                                                                                    uint64_t t256 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                                                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t256)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t256, gm_srclv0dcbbfhves, 913, 44);
                                                                                    {
                                                                                        {
                                                                                            uint8_t t257 = GM_LD(uint8_t, (((((uint8_t *)t255) + (uintptr_t)((uint64_t)0ULL) * 88) + 8) + 0));
                                                                                            uint8_t t258 = (uint8_t)(t257 == ((uint8_t)2ULL));
                                                                                            if (!t258) {
                                                                                                goto gm_lb45;
                                                                                            }
                                                                                            gm_l53 = ((uint8_t)1);
                                                                                            goto gm_lb44;
                                                                                        }
gm_lb45:;
                                                                                        {
                                                                                            gm_l53 = ((uint8_t)0);
                                                                                            goto gm_lb44;
                                                                                        }
gm_lb46:;
                                                                                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 913, 44);
                                                                                    }
gm_lb44:;
                                                                                    uint8_t t259 = gm_l53;
                                                                                    gm_l52 = t259;
                                                                                } else {
                                                                                    gm_l52 = ((uint8_t)0);
                                                                                }
                                                                                uint8_t t260 = gm_l52;
                                                                                gm_l30 = t260;
                                                                                goto gm_lb42;
                                                                            }
gm_lb43:;
                                                                        }
gm_lb42:;
                                                                        uint8_t t261 = gm_l30;
                                                                        uint8_t t262 = (uint8_t)!t261;
                                                                        if (t262) {
                                                                            void * t263 = gm_l0;
                                                                            gm_tl874t2atnlh3 t264 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                                                            gm_b16_8 t265;
                                                                            GM_ST(void *, (uint8_t *)&t265, (void *)(gm_sppcsr92ukfol));
                                                                            GM_ST(uintptr_t, (uint8_t *)&t265 + 8, (uintptr_t)(63));
                                                                            gm_b0_1 t266 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t263, t264, t265);
                                                                            if (gm_unw.kind) {
                                                                                goto gm_lad;
                                                                            }
                                                                        }
                                                                        goto gm_lb8;
                                                                    }
gm_lb41:;
                                                                    {
                                                                        {
                                                                            {
                                                                                gm_b16_8 t267 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                                gm_b16_8 t268;
                                                                                GM_ST(void *, (uint8_t *)&t268, (void *)(gm_s2ph9f020upd2));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t268 + 8, (uintptr_t)(6));
                                                                                uint8_t t269 = (gm_eq0iqiubfm7e(((uint8_t *)&t267), ((uint8_t *)&t268)));
                                                                                if (!t269) {
                                                                                    goto gm_lb49;
                                                                                }
                                                                                goto gm_lb48;
                                                                            }
gm_lb49:;
                                                                            {
                                                                                gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                                gm_b16_8 t271;
                                                                                GM_ST(void *, (uint8_t *)&t271, (void *)(gm_s6vvtrmtnt3j1));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t271 + 8, (uintptr_t)(4));
                                                                                uint8_t t272 = (gm_eq0iqiubfm7e(((uint8_t *)&t270), ((uint8_t *)&t271)));
                                                                                if (!t272) {
                                                                                    goto gm_lb50;
                                                                                }
                                                                                goto gm_lb48;
                                                                            }
gm_lb50:;
                                                                            {
                                                                                gm_b16_8 t273 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                                gm_b16_8 t274;
                                                                                GM_ST(void *, (uint8_t *)&t274, (void *)(gm_suuv0qpi4v4e4));
                                                                                GM_ST(uintptr_t, (uint8_t *)&t274 + 8, (uintptr_t)(7));
                                                                                uint8_t t275 = (gm_eq0iqiubfm7e(((uint8_t *)&t273), ((uint8_t *)&t274)));
                                                                                if (!t275) {
                                                                                    goto gm_lb51;
                                                                                }
                                                                                goto gm_lb48;
                                                                            }
gm_lb51:;
                                                                            goto gm_lb47;
                                                                        }
gm_lb48:;
                                                                        void * t276 = gm_l0;
                                                                        gm_tl874t2atnlh3 t277 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                                                        gm_b16_8 t278 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                        gm_tnpifv20irnph t279;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t279 + 0), t278);
                                                                        gm_f_format_0dvj28f((void *)&gm_l54, (void *)NULL, cx, NULL, t279);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        uint8_t t280 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                                                                        if (!t280) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 8, gm_srclv0dcbbfhves, 917, 63);
                                                                        gm_b16_8 t281 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l54) + 8));
                                                                        gm_b16_8 t282 = GM_LD(gm_b16_8, ((uint8_t *)&t281));
                                                                        gm_b0_1 t283 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t276, t277, t282);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        goto gm_lb8;
                                                                    }
gm_lb47:;
                                                                }
                                                                {
                                                                    {
                                                                        gm_b16_8 t284 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                        gm_b16_8 t285;
                                                                        GM_ST(void *, (uint8_t *)&t285, (void *)(gm_s80p0ld1j6rfg));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)(9));
                                                                        uint8_t t286 = (gm_eq0iqiubfm7e(((uint8_t *)&t284), ((uint8_t *)&t285)));
                                                                        if (!t286) {
                                                                            goto gm_lb52;
                                                                        }
                                                                        void * t287 = gm_l0;
                                                                        gm_tl874t2atnlh3 t288 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                                                        gm_b16_8 t289;
                                                                        GM_ST(void *, (uint8_t *)&t289, (void *)(gm_sg7i6m1frjk5n));
                                                                        GM_ST(uintptr_t, (uint8_t *)&t289 + 8, (uintptr_t)(62));
                                                                        gm_b0_1 t290 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t287, t288, t289);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        goto gm_lb8;
                                                                    }
gm_lb52:;
                                                                    {
                                                                        gm_b16_8 t291 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
                                                                        gm_l32 = t291;
                                                                        void * t292 = gm_l0;
                                                                        gm_tl874t2atnlh3 t293 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l19) + 56));
                                                                        gm_b16_8 t294 = gm_l32;
                                                                        gm_tnpifv20irnph t295;
                                                                        GM_ST(gm_b16_8, ((uint8_t *)&t295 + 0), t294);
                                                                        gm_f_format_u2mas3v((void *)&gm_l55, (void *)NULL, cx, NULL, t295);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        uint8_t t296 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                                                                        if (!t296) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 8, gm_srclv0dcbbfhves, 919, 41);
                                                                        gm_b16_8 t297 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 8));
                                                                        gm_b16_8 t298 = GM_LD(gm_b16_8, ((uint8_t *)&t297));
                                                                        gm_b0_1 t299 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t292, t293, t298);
                                                                        if (gm_unw.kind) {
                                                                            goto gm_lad;
                                                                        }
                                                                        goto gm_lb8;
                                                                    }
gm_lb53:;
                                                                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 853, 9);
                                                                }
                                                            }
gm_lb8:;
                                                        }
                                                        {
                                                            goto gm_lb6;
                                                        }
                                                    }
gm_lb7:;
                                                }
                                                {
                                                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 852, 5);
                                                }
                                            }
gm_lb6:;
                                        }
                                    }
gm_lb5:;
                                }
                                {
                                    uint64_t t300 = gm_l18;
                                    uint64_t r301; bool o302 = __builtin_add_overflow(t300, ((uint64_t)1ULL), &r301);
                                    uint64_t t303 = r301;
                                    gm_l18 = t303;
                                }
                            }
                        }
gm_lb3:;
                    }
                    {
                        {
                            {
                                uint8_t t304 = gm_l8;
                                if (!t304) {
                                    goto gm_lb55;
                                }
                                uint8_t t305 = gm_l7;
                                uint8_t t306 = (uint8_t)!t305;
                                if (!t306) {
                                    goto gm_lb55;
                                }
                                void * t307 = gm_l0;
                                gm_tl874t2atnlh3 t308 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l5) + 16));
                                gm_b16_8 t309;
                                GM_ST(void *, (uint8_t *)&t309, (void *)(gm_s8ro53eq54s60));
                                GM_ST(uintptr_t, (uint8_t *)&t309 + 8, (uintptr_t)(43));
                                gm_b0_1 t310 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t307, t308, t309);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb54;
                            }
gm_lb55:;
                        }
gm_lb54:;
                        gm_tf5srh4mp4b3i t311;
                        memset(&t311, 0, sizeof t311);
                        gm_l33 = t311;
                        {
                            {
                                uint8_t t312 = (uint8_t)((((uint8_t *)&F.l13))[72] != 0);
                                if (!t312) {
                                    goto gm_lb57;
                                }
                                memmove(((uint8_t *)&F.l34), ((uint8_t *)&F.l13), 72);
                                uint8_t t313 = gm_l7;
                                if (t313) {
                                    void * t314 = gm_l0;
                                    uint64_t t315 = gm_l4;
                                    void * t316 = (void *)((uint8_t *)&F.l34);
                                    gm_tf5srh4mp4b3i t317 = gm_f_attr_power_of_two_gj02n2t((void *)NULL, cx, NULL, t314, t315, t316);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l33 = t317;
                                } else {
                                    void * t318 = gm_l0;
                                    gm_tl874t2atnlh3 t319 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l34) + 56));
                                    gm_b16_8 t320;
                                    GM_ST(void *, (uint8_t *)&t320, (void *)(gm_sbqv06vnui8sl));
                                    GM_ST(uintptr_t, (uint8_t *)&t320 + 8, (uintptr_t)(87));
                                    gm_b0_1 t321 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t318, t319, t320);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                goto gm_lb56;
                            }
gm_lb57:;
                        }
gm_lb56:;
                        memset(&F.l35, 0, sizeof F.l35);
                        GM_ST(uint8_t, ((uint8_t *)&F.l35 + 0), ((uint8_t)11ULL));
                        void * t322 = gm_l0;
                        uint64_t t323 = GM_LD(uint64_t, (((uint8_t *)t322) + 2432));
                        gm_l36 = t323;
                        void * t324 = gm_l0;
                        uint8_t t325 = (uint8_t)((((uint8_t *)&gm_l9))[8] != 0);
                        if (t325) {
                            uint64_t t326 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                            gm_l56 = t326;
                        } else {
                            uint64_t t327 = GM_LD(uint64_t, ((uint8_t *)gm_c1n574q3spi));
                            gm_l56 = t327;
                        }
                        uint64_t t328 = gm_l56;
                        GM_ST(uint64_t, (((uint8_t *)t324) + 2432), t328);
                        uint8_t t329 = gm_l7;
                        if (t329) {
                            {
                                void * t330 = gm_l0;
                                uint64_t t331 = gm_l4;
                                void * t332 = (void *)((uint8_t *)&F.l6);
                                gm_t08olq97rscn3 t333 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l5));
                                gm_f_declare_global_729nttf((void *)&F.l35, (void *)NULL, cx, NULL, t330, t331, &t333, t332);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            {
                                {
                                    uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                                    uint8_t t335 = (uint8_t)(t334 == ((uint8_t)10ULL));
                                    if (!t335) {
                                        goto gm_lb59;
                                    }
                                    gm_t88rcucg1b7uk t336 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l35) + 8));
                                    gm_l37 = t336;
                                    {
                                        void * t337 = gm_l0;
                                        void * t338 = (void *)(((uint8_t *)t337) + 1568);
                                        uint64_t t339 = gm_l1;
                                        gm_b16_8 t340 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                                        gm_t88rcucg1b7uk t341 = gm_l37;
                                        uint64_t t342 = gm_l4;
                                        void * t343 = GM_LD(void *, (((uint8_t *)&F.l6) + 40));
                                        uint8_t t344 = gm_l8;
                                        gm_th2lqbo77pvsc t345;
                                        memset(&t345, 0, sizeof t345);
                                        gm_tf5srh4mp4b3i t346 = gm_l33;
                                        gm_tq7qjpkmqmc5s t347;
                                        GM_ST(uint32_t, (uint8_t *)&t347 + 28, 0);
                                        GM_ST(uint8_t, (uint8_t *)&t347 + 49, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t347 + 50, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t347 + 52, 0);
                                        GM_ST(uint64_t, ((uint8_t *)&t347 + 0), t339);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t347 + 8), t340);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t347 + 24), t341);
                                        GM_ST(uint64_t, ((uint8_t *)&t347 + 32), t342);
                                        GM_ST(void *, ((uint8_t *)&t347 + 40), t343);
                                        GM_ST(uint8_t, ((uint8_t *)&t347 + 48), t344);
                                        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t347 + 56), t345);
                                        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t347 + 80), t346);
                                        GM_ST(void *, ((uint8_t *)&t347 + 96), ((void *)0));
                                        gm_tftb1odc739t5 t348 = gm_f_push_4p3ivmb(t338, &t347);
                                        gm_l57 = t348;
                                    }
                                    uint8_t t349 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                                    if (!t349) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_srclv0dcbbfhves, 937, 13);
                                    goto gm_lb58;
                                }
gm_lb59:;
                            }
gm_lb58:;
                        } else {
                            void * t350 = gm_l0;
                            uint64_t t351 = gm_l1;
                            uint64_t t352 = gm_l4;
                            void * t353 = (void *)((uint8_t *)&F.l6);
                            gm_t08olq97rscn3 t354 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l5));
                            gm_f_compute_binding_cl3ogob((void *)&F.l35, (void *)NULL, cx, NULL, t350, t351, t352, &t354, t353);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                    }
                    {
                        void * t355 = gm_l0;
                        uint64_t t356 = gm_l36;
                        GM_ST(uint64_t, (((uint8_t *)t355) + 2432), t356);
                        {
                            {
                                uint8_t t357 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                                if (!t357) {
                                    goto gm_lb61;
                                }
                                gm_tbvif938ns15l t358 = GM_LD(gm_tbvif938ns15l, ((uint8_t *)&gm_l15));
                                gm_l38 = t358;
                                {
                                    {
                                        uint8_t t359 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                                        uint8_t t360 = (uint8_t)(t359 == ((uint8_t)5ULL));
                                        if (!t360) {
                                            goto gm_lb63;
                                        }
                                        uint32_t t361 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                                        gm_l39 = t361;
                                        void * t362 = gm_l0;
                                        uint32_t t363 = gm_l39;
                                        uint8_t t364 = GM_LD(uint8_t, ((uint8_t *)&gm_l38));
                                        gm_tl874t2atnlh3 t365 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l38) + 4));
                                        gm_b0_1 t366 = gm_f_apply_callconv_197l87p((void *)NULL, cx, NULL, t362, t363, t364, t365);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb62;
                                    }
gm_lb63:;
                                    {
                                        uint8_t t367 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                                        uint8_t t368 = (uint8_t)(t367 == ((uint8_t)11ULL));
                                        if (!t368) {
                                            goto gm_lb64;
                                        }
                                        goto gm_lb62;
                                    }
gm_lb64:;
                                    {
                                        void * t369 = gm_l0;
                                        gm_tl874t2atnlh3 t370 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l38) + 4));
                                        gm_b16_8 t371;
                                        GM_ST(void *, (uint8_t *)&t371, (void *)(gm_srb1158jlk9u7));
                                        GM_ST(uintptr_t, (uint8_t *)&t371 + 8, (uintptr_t)(74));
                                        gm_b0_1 t372 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t369, t370, t371);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb62;
                                    }
gm_lb65:;
                                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 944, 9);
                                }
gm_lb62:;
                                goto gm_lb60;
                            }
gm_lb61:;
                        }
gm_lb60:;
                        {
                            {
                                uint8_t t373 = (uint8_t)((((uint8_t *)&F.l10))[72] != 0);
                                if (!t373) {
                                    goto gm_lb67;
                                }
                                memmove(((uint8_t *)&F.l40), ((uint8_t *)&F.l10), 72);
                                {
                                    {
                                        uint8_t t374 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                                        uint8_t t375 = (uint8_t)(t374 == ((uint8_t)5ULL));
                                        if (!t375) {
                                            goto gm_lb69;
                                        }
                                        uint32_t t376 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                                        gm_l41 = t376;
                                        uint8_t t377 = gm_l11;
                                        if (!t377) {
                                            goto gm_lb69;
                                        }
                                        void * t378 = gm_l0;
                                        void * t379 = (void *)(((uint8_t *)t378) + 800);
                                        uint32_t t380 = gm_l41;
                                        gm_tl874t2atnlh3 t381 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l40) + 56));
                                        gm_t9d4sf0vqn7dt t382;
                                        GM_ST(uint32_t, ((uint8_t *)&t382 + 0), t380);
                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t382 + 4), t381);
                                        gm_tftb1odc739t5 t383 = gm_f_push_89d0k90(t379, GM_CONV(gm_csedogq9e4rp5r, t382));
                                        gm_l58 = t383;
                                        uint8_t t384 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                        if (!t384) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_srclv0dcbbfhves, 951, 37);
                                        goto gm_lb68;
                                    }
gm_lb69:;
                                }
gm_lb68:;
                                {
                                    {
                                        uint8_t t385 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                                        uint8_t t386 = (uint8_t)(t385 == ((uint8_t)5ULL));
                                        if (!t386) {
                                            goto gm_lb71;
                                        }
                                        uint32_t t387 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                                        gm_l42 = t387;
                                        uint32_t t388 = gm_l42;
                                        GM_ST(uint32_t, ((uint8_t *)&gm_l12), t388);
                                        void * t389 = gm_l0;
                                        void * t390 = (void *)(((uint8_t *)t389) + 840);
                                        gm_tbcgm40aljf9h t391 = gm_l12;
                                        gm_tftb1odc739t5 t392 = gm_f_push_0fe09r3(t390, t391);
                                        gm_l59 = t392;
                                        uint8_t t393 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                        if (!t393) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_srclv0dcbbfhves, 954, 13);
                                        goto gm_lb70;
                                    }
gm_lb71:;
                                }
gm_lb70:;
                                {
                                    {
                                        {
                                            {
                                                uint8_t t394 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                                                uint8_t t395 = (uint8_t)(t394 == ((uint8_t)5ULL));
                                                if (!t395) {
                                                    goto gm_lb75;
                                                }
                                                gm_l60 = ((uint8_t)0);
                                                goto gm_lb74;
                                            }
gm_lb75:;
                                            {
                                                gm_l60 = ((uint8_t)1);
                                                goto gm_lb74;
                                            }
gm_lb76:;
                                            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 956, 12);
                                        }
gm_lb74:;
                                        uint8_t t396 = gm_l60;
                                        if (!t396) {
                                            goto gm_lb73;
                                        }
                                        {
                                            uint8_t t397 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                                            uint8_t t398 = (uint8_t)(!(t397 == ((uint8_t)11ULL)));
                                            if (!t398) {
                                                goto gm_lb73;
                                            }
                                        }
                                        {
                                            void * t399 = gm_l0;
                                            gm_tl874t2atnlh3 t400 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l40) + 56));
                                            gm_b16_8 t401 = GM_LD(gm_b16_8, ((uint8_t *)&F.l40));
                                            gm_tnpifv20irnph t402;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t402 + 0), t401);
                                            gm_f_format_28p84ds((void *)&gm_l61, (void *)NULL, cx, NULL, t402);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t403 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                                            if (!t403) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 8, gm_srclv0dcbbfhves, 957, 29);
                                            gm_b16_8 t404 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
                                            gm_b16_8 t405 = GM_LD(gm_b16_8, ((uint8_t *)&t404));
                                            gm_b0_1 t406 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t399, t400, t405);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        goto gm_lb72;
                                    }
gm_lb73:;
                                }
gm_lb72:;
                                goto gm_lb66;
                            }
gm_lb67:;
                        }
gm_lb66:;
                    }
                    {
                        {
                            {
                                uint8_t t407 = (uint8_t)((((uint8_t *)&gm_l16))[12] != 0);
                                if (!t407) {
                                    goto gm_lb78;
                                }
                                gm_tl874t2atnlh3 t408 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l16));
                                gm_l43 = t408;
                                {
                                    {
                                        uint8_t t409 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                                        uint8_t t410 = (uint8_t)(t409 == ((uint8_t)5ULL));
                                        if (!t410) {
                                            goto gm_lb80;
                                        }
                                        uint32_t t411 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                                        gm_l44 = t411;
                                        void * t412 = gm_l0;
                                        uint32_t t413 = gm_l44;
                                        gm_tl874t2atnlh3 t414 = gm_l43;
                                        gm_b0_1 t415 = gm_f_trap_hook_fn_5c0a4ds((void *)NULL, cx, NULL, t412, t413, t414);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb79;
                                    }
gm_lb80:;
                                    {
                                        uint8_t t416 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                                        uint8_t t417 = (uint8_t)(t416 == ((uint8_t)11ULL));
                                        if (!t417) {
                                            goto gm_lb81;
                                        }
                                        goto gm_lb79;
                                    }
gm_lb81:;
                                    {
                                        void * t418 = gm_l0;
                                        gm_tl874t2atnlh3 t419 = gm_l43;
                                        gm_b16_8 t420;
                                        GM_ST(void *, (uint8_t *)&t420, (void *)(gm_sv48vll3qortv));
                                        GM_ST(uintptr_t, (uint8_t *)&t420 + 8, (uintptr_t)(39));
                                        gm_b0_1 t421 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t418, t419, t420);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb79;
                                    }
gm_lb82:;
                                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 961, 9);
                                }
gm_lb79:;
                                goto gm_lb77;
                            }
gm_lb78:;
                        }
gm_lb77:;
                        {
                            {
                                uint8_t t422 = (uint8_t)((((uint8_t *)&F.l14))[32] != 0);
                                if (!t422) {
                                    goto gm_lb84;
                                }
                                memmove(((uint8_t *)&gm_l45), ((uint8_t *)&F.l14), 32);
                                {
                                    {
                                        uint8_t t423 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                                        uint8_t t424 = (uint8_t)(t423 == ((uint8_t)5ULL));
                                        if (!t424) {
                                            goto gm_lb86;
                                        }
                                        uint32_t t425 = GM_LD(uint32_t, (((uint8_t *)&F.l35) + 8));
                                        gm_l46 = t425;
                                        void * t426 = gm_l0;
                                        uint32_t t427 = gm_l46;
                                        gm_b16_8 t428 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l45));
                                        gm_tl874t2atnlh3 t429 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l45) + 16));
                                        gm_b0_1 t430 = gm_f_export_fn_8hcuasc((void *)NULL, cx, NULL, t426, t427, t428, t429);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb85;
                                    }
gm_lb86:;
                                    {
                                        uint8_t t431 = GM_LD(uint8_t, (((uint8_t *)&F.l35) + 0));
                                        uint8_t t432 = (uint8_t)(t431 == ((uint8_t)11ULL));
                                        if (!t432) {
                                            goto gm_lb87;
                                        }
                                        goto gm_lb85;
                                    }
gm_lb87:;
                                    {
                                        void * t433 = gm_l0;
                                        gm_tl874t2atnlh3 t434 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l45) + 16));
                                        gm_b16_8 t435;
                                        GM_ST(void *, (uint8_t *)&t435, (void *)(gm_sj3lfuqd4f1np));
                                        GM_ST(uintptr_t, (uint8_t *)&t435 + 8, (uintptr_t)(36));
                                        gm_b0_1 t436 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t433, t434, t435);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb85;
                                    }
gm_lb88:;
                                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 968, 9);
                                }
gm_lb85:;
                                goto gm_lb83;
                            }
gm_lb84:;
                        }
gm_lb83:;
                        memmove(gm_out, ((uint8_t *)&F.l35), 208);
                        goto gm_fdone;
                    }
                }
gm_lb2:;
            }
            {
                memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)11ULL));
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
extern __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_declare_const_or_type_4jaj4k6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3, void * a4) {
    struct gm_fr_declare_const_or_type_4jaj4k6 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    void * gm_l3; /* annot */
    void * gm_l4; /* init */
    gm_tba9024q2up98 gm_l5; /* found */
    uint64_t gm_l6; /* d */
    void * gm_l8; /* callee */
    gm_tba9024q2up98 gm_l9; /* found */
    uint64_t gm_l10; /* d */
    gm_tiilmfd16v3fc gm_l11; /* r */
    gm_t88rcucg1b7uk gm_l12; /* ty */
    gm_tkn42u7na3t81 gm_l13; /* expected */
    void * gm_l14; /* a */
    gm_t88rcucg1b7uk gm_l15; /* t */
    void * gm_l16; /* hook */
    uint32_t gm_l17; /* id */
    gm_t88rcucg1b7uk gm_l18; /* ty */
    gm_t88rcucg1b7uk gm_l19; /* bad */
    GM_STACK_CHECK(&F, 1724u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l20;
    gm_t89h15e2c5o3p gm_l21;
    gm_t89h15e2c5o3p gm_l22;
    uint8_t gm_l23;
    gm_t6qooom8qa2do gm_l24;
    gm_tqu3m2l3n8mnc gm_l25;
    gm_t6qooom8qa2do gm_l26;
    uint8_t gm_l27;
    gm_tkn42u7na3t81 gm_l28;
    gm_t9805qtonbsag gm_l29;
    gm_tkn42u7na3t81 gm_l30;
    uint8_t gm_l31;
    gm_tkn42u7na3t81 gm_l32;
    gm_tabb710d9mq2e gm_l33;
    {
        {
            {
                uint8_t t36 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l3)) != 0);
                if (t36) {
                    goto gm_lb2;
                }
                gm_l20 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l20 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1055, 8);
        }
gm_lb1:;
        uint8_t t37 = gm_l20;
        if (t37) {
            {
                {
                    {
                        void * t38 = gm_l0;
                        uint64_t t39 = gm_l1;
                        void * t40 = gm_l4;
                        gm_f_aliased_decl_jv6ilmn((void *)&gm_l21, (void *)NULL, cx, NULL, t38, t39, t40);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t41 = (uint8_t)((((uint8_t *)&gm_l21))[16] != 0);
                    if (!t41) {
                        goto gm_lb5;
                    }
                    gm_tba9024q2up98 t42 = GM_LD(gm_tba9024q2up98, ((uint8_t *)&gm_l21));
                    gm_l5 = t42;
                    uint8_t t43 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
                    if (t43) {
                        uint64_t t44 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
                        gm_l6 = t44;
                    } else {
                        memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)11ULL));
                        goto gm_fdone;
                    }
                    {
                        void * t45 = gm_l0;
                        uint64_t t46 = gm_l6;
                        gm_f_resolve_decl_pus042v((void *)&F.l7, (void *)NULL, cx, NULL, t45, t46);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        {
                            {
                                {
                                    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                                    uint8_t t48 = (uint8_t)(t47 == ((uint8_t)7ULL));
                                    if (!t48) {
                                        goto gm_lb9;
                                    }
                                    goto gm_lb8;
                                }
gm_lb9:;
                                {
                                    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 0));
                                    uint8_t t50 = (uint8_t)(t49 == ((uint8_t)10ULL));
                                    if (!t50) {
                                        goto gm_lb10;
                                    }
                                    goto gm_lb8;
                                }
gm_lb10:;
                                goto gm_lb7;
                            }
gm_lb8:;
                            goto gm_lb6;
                        }
gm_lb7:;
                        {
                            memmove(gm_out, ((uint8_t *)&F.l7), 208);
                            goto gm_fdone;
                        }
gm_lb11:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1060, 13);
                    }
gm_lb6:;
                    goto gm_lb4;
                }
gm_lb5:;
            }
gm_lb4:;
            {
                {
                    void * t51 = gm_l4;
                    uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t51) + 8) + 0));
                    uint8_t t53 = (uint8_t)(t52 == ((uint8_t)19ULL));
                    if (!t53) {
                        goto gm_lb13;
                    }
                    void * t54 = GM_LD(void *, ((((uint8_t *)t51) + 8) + 8));
                    gm_l8 = t54;
                    {
                        {
                            void * t55 = gm_l0;
                            uint64_t t56 = gm_l1;
                            void * t57 = gm_l8;
                            gm_f_aliased_decl_jv6ilmn((void *)&gm_l22, (void *)NULL, cx, NULL, t55, t56, t57);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t58 = (uint8_t)((((uint8_t *)&gm_l22))[16] != 0);
                            if (!t58) {
                                goto gm_lb15;
                            }
                            gm_tba9024q2up98 t59 = GM_LD(gm_tba9024q2up98, ((uint8_t *)&gm_l22));
                            gm_l9 = t59;
                            {
                                {
                                    uint8_t t60 = (uint8_t)((((uint8_t *)&gm_l9))[8] != 0);
                                    if (!t60) {
                                        goto gm_lb17;
                                    }
                                    uint64_t t61 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                                    gm_l10 = t61;
                                    void * t62 = gm_l0;
                                    uint64_t t63 = gm_l10;
                                    gm_f_resolve_decl_pus042v((void *)&gm_l24, (void *)NULL, cx, NULL, t62, t63);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    {
                                        {
                                            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                                            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)2ULL));
                                            if (!t65) {
                                                goto gm_lb19;
                                            }
                                            gm_l23 = ((uint8_t)1);
                                            goto gm_lb18;
                                        }
gm_lb19:;
                                        {
                                            gm_l23 = ((uint8_t)0);
                                            goto gm_lb18;
                                        }
gm_lb20:;
                                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1069, 24);
                                    }
gm_lb18:;
                                    uint8_t t66 = gm_l23;
                                    if (t66) {
                                        {
                                            void * t67 = gm_l0;
                                            uint64_t t68 = gm_l1;
                                            void * t69 = gm_l4;
                                            gm_f_resolve_effect_ref_bggirs2((void *)&gm_l25, (void *)NULL, cx, NULL, t67, t68, t69);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        uint8_t t70 = (uint8_t)((((uint8_t *)&gm_l25))[24] != 0);
                                        if (t70) {
                                            memmove(((uint8_t *)&gm_l11), ((uint8_t *)&gm_l25), 24);
                                        } else {
                                            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                                            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)11ULL));
                                            goto gm_fdone;
                                        }
                                        {
                                            gm_tiilmfd16v3fc t71 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l11));
                                            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                                            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)3ULL));
                                            GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 8), t71);
                                            goto gm_fdone;
                                        }
                                    }
                                    goto gm_lb16;
                                }
gm_lb17:;
                            }
gm_lb16:;
                            goto gm_lb14;
                        }
gm_lb15:;
                    }
gm_lb14:;
                    goto gm_lb12;
                }
gm_lb13:;
            }
gm_lb12:;
            void * t72 = gm_l0;
            uint64_t t73 = gm_l1;
            void * t74 = gm_l4;
            uint8_t t75 = gm_f_looks_like_type_ho5qv1k((void *)NULL, cx, NULL, t72, t73, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t75) {
                void * t76 = gm_l0;
                uint64_t t77 = gm_l1;
                void * t78 = gm_l4;
                gm_t88rcucg1b7uk t79 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t76, t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t79;
                void * t80 = gm_l0;
                void * t81 = (void *)(((uint8_t *)t80) + 8);
                gm_t88rcucg1b7uk t82 = gm_l12;
                uint8_t t83 = gm_f_is_error_8k27gd1(t81, GM_CONV(gm_csguankko4lmhd, t82));
                if (t83) {
                    memset(&gm_l26, 0, sizeof gm_l26);
                    GM_ST(uint8_t, ((uint8_t *)&gm_l26 + 0), ((uint8_t)11ULL));
                } else {
                    gm_t88rcucg1b7uk t84 = gm_l12;
                    memset(&gm_l26, 0, sizeof gm_l26);
                    GM_ST(uint8_t, ((uint8_t *)&gm_l26 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l26 + 8), t84);
                }
                memmove(gm_out, ((uint8_t *)&gm_l26), 208);
                goto gm_fdone;
            }
        }
    }
    {
        gm_tkn42u7na3t81 t85;
        memset(&t85, 0, sizeof t85);
        gm_l13 = t85;
        {
            {
                uint8_t t86 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l3)) != 0);
                if (!t86) {
                    goto gm_lb22;
                }
                void * t87 = GM_LD(void *, ((uint8_t *)&gm_l3));
                gm_l14 = t87;
                void * t88 = gm_l0;
                uint64_t t89 = gm_l1;
                void * t90 = gm_l14;
                gm_t88rcucg1b7uk t91 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t88, t89, t90);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tkn42u7na3t81 t92;
                GM_ST(uint8_t, (uint8_t *)&t92 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t92 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t92), t91);
                ((uint8_t *)&t92)[4] = 1;
                gm_l13 = t92;
                goto gm_lb21;
            }
gm_lb22:;
        }
gm_lb21:;
        {
            {
                uint8_t t93 = (uint8_t)((((uint8_t *)&gm_l13))[4] != 0);
                if (!t93) {
                    goto gm_lb24;
                }
                gm_t88rcucg1b7uk t94 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l13));
                gm_l15 = t94;
                void * t95 = gm_l0;
                void * t96 = (void *)(((uint8_t *)t95) + 8);
                gm_t88rcucg1b7uk t97 = gm_l15;
                uint8_t t98 = gm_f_is_error_8k27gd1(t96, GM_CONV(gm_csguankko4lmhd, t97));
                if (!t98) {
                    goto gm_lb24;
                }
                memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)11ULL));
                goto gm_fdone;
            }
gm_lb24:;
        }
gm_lb23:;
        {
            {
                void * t99 = gm_l0;
                uint8_t t100 = (uint8_t)(GM_LD(void *, (((uint8_t *)t99) + 2288)) != 0);
                if (!t100) {
                    goto gm_lb26;
                }
                void * t101 = GM_LD(void *, (((uint8_t *)t99) + 2288));
                gm_l16 = t101;
                void * t102 = gm_l0;
                uint64_t t103 = gm_l1;
                void * t104 = gm_l4;
                gm_tkn42u7na3t81 t105 = gm_l13;
                gm_tkn42u7na3t81 t106 = gm_f_const_check_d48mnu7((void *)NULL, cx, NULL, t102, t103, t104, t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l28 = t106;
                {
                    {
                        uint8_t t107 = (uint8_t)((((uint8_t *)&gm_l28))[4] != 0);
                        if (t107) {
                            goto gm_lb28;
                        }
                        gm_l27 = ((uint8_t)1);
                        goto gm_lb27;
                    }
gm_lb28:;
                    {
                        gm_l27 = ((uint8_t)0);
                        goto gm_lb27;
                    }
gm_lb29:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1086, 32);
                }
gm_lb27:;
                uint8_t t108 = gm_l27;
                if (!t108) {
                    goto gm_lb26;
                }
                void * t109 = gm_l16;
                void * t110 = gm_l0;
                uint64_t t111 = gm_l1;
                void * t112 = gm_l4;
                gm_tkn42u7na3t81 t113 = gm_l13;
                gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_tl874t2atnlh3 t115 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                void * t116 = t109;
                gm_t9805qtonbsag t117;
                if (GM_PTR_TAGGED(t116)) {
                    t117 = ((gm_t9805qtonbsag (*)(void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_cseahvg8v7p0vs))GM_PTR_UNTAG(t116))(t110, t111, t112, t113, t114, GM_CONV(gm_cseahvg8v7p0vs, t115));
                } else {
                    t117 = ((gm_t9805qtonbsag (*)(void *, gm_node *, gm_node *, void *, uint64_t, void *, gm_tkn42u7na3t81, gm_b16_8, gm_tl874t2atnlh3))t116)(NULL, cx, NULL, t110, t111, t112, t113, t114, t115);
                }
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l29 = t117;
                uint8_t t118 = (uint8_t)((((uint8_t *)&gm_l29))[4] != 0);
                if (t118) {
                    uint32_t t119 = GM_LD(uint32_t, ((uint8_t *)&gm_l29));
                    gm_l17 = t119;
                } else {
                    memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)11ULL));
                    goto gm_fdone;
                }
                void * t120 = gm_l0;
                void * t121 = GM_LD(void *, (((uint8_t *)t120) + 2088));
                uint64_t t122 = GM_LD(uint64_t, ((((uint8_t *)t120) + 2088) + 8));
                uint32_t t123 = gm_l17;
                uint64_t t124 = (uint64_t)t123;
                if ((uintptr_t)t124 >= (uintptr_t)(t122)) gm_trap_index((uintptr_t)t124, t122, gm_srclv0dcbbfhves, 1088, 23);
                gm_t88rcucg1b7uk t125 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t121) + (uintptr_t)t124 * 88) + 28));
                memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)7ULL));
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 8), t125);
                goto gm_fdone;
            }
gm_lb26:;
        }
gm_lb25:;
    }
    {
        void * t126 = gm_l0;
        uint64_t t127 = gm_l1;
        void * t128 = gm_l4;
        gm_tkn42u7na3t81 t129 = gm_l13;
        gm_tkn42u7na3t81 t130 = gm_f_const_type_7rijs95((void *)NULL, cx, NULL, t126, t127, t128, t129);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l30 = t130;
        uint8_t t131 = (uint8_t)((((uint8_t *)&gm_l30))[4] != 0);
        if (t131) {
            gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l30));
            gm_l18 = t132;
        } else {
            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)11ULL));
            goto gm_fdone;
        }
        void * t133 = gm_l0;
        {
            {
                uint8_t t134 = (uint8_t)(GM_LD(void *, (((uint8_t *)t133) + 2288)) != 0);
                if (t134) {
                    goto gm_lb31;
                }
                gm_l31 = ((uint8_t)0);
                goto gm_lb30;
            }
gm_lb31:;
            {
                gm_l31 = ((uint8_t)1);
                goto gm_lb30;
            }
gm_lb32:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1091, 8);
        }
gm_lb30:;
        uint8_t t135 = gm_l31;
        if (t135) {
            void * t136 = gm_l0;
            gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)a2));
            gm_tl874t2atnlh3 t138 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
            gm_t88rcucg1b7uk t139 = gm_l18;
            uint64_t t140 = gm_l1;
            void * t141 = gm_l4;
            gm_tkn42u7na3t81 t142 = gm_l13;
            void * t143 = gm_l0;
            uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)t143) + 2432));
            gm_t9qkh0rsooc8d t145 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)gm_c0u09tvsmdv));
            gm_tjcptv9sagfq8 t146;
            GM_ST(uint8_t, (uint8_t *)&t146 + 73, 0);
            GM_ST(uint16_t, (uint8_t *)&t146 + 74, 0);
            GM_ST(uint32_t, (uint8_t *)&t146 + 76, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t146 + 0), t137);
            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t146 + 16), t138);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t146 + 28), t139);
            GM_ST(uint64_t, ((uint8_t *)&t146 + 32), t140);
            GM_ST(void *, ((uint8_t *)&t146 + 40), t141);
            GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t146 + 48), t142);
            GM_ST(uint64_t, ((uint8_t *)&t146 + 56), t144);
            GM_ST(void *, ((uint8_t *)&t146 + 64), ((void *)0));
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t146 + 72), t145);
            GM_ST(void *, ((uint8_t *)&t146 + 80), ((void *)0));
            uint32_t t147 = gm_f_add_const_8u8b1ja((void *)NULL, cx, NULL, t136, &t146);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t148 = gm_l0;
        void * t149 = (void *)(((uint8_t *)t148) + 8);
        gm_t88rcucg1b7uk t150 = gm_l18;
        uint8_t t151 = gm_f_is_error_8k27gd1(t149, GM_CONV(gm_csguankko4lmhd, t150));
        if (t151) {
            memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)11ULL));
            goto gm_fdone;
        }
        {
            {
                void * t152 = gm_l0;
                gm_t88rcucg1b7uk t153 = gm_l18;
                gm_tkn42u7na3t81 t154 = gm_f_non_constant_part_itk7b0v(t152, GM_CONV(gm_csguankko4lmhd, t153));
                gm_l32 = t154;
                uint8_t t155 = (uint8_t)((((uint8_t *)&gm_l32))[4] != 0);
                if (!t155) {
                    goto gm_lb34;
                }
                gm_t88rcucg1b7uk t156 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l32));
                gm_l19 = t156;
                {
                    void * t157 = gm_l0;
                    void * t158 = gm_l4;
                    gm_tl874t2atnlh3 t159 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t158) + 72));
                    void * t160 = gm_l0;
                    gm_t88rcucg1b7uk t161 = gm_l18;
                    gm_b16_8 t162 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t160, t161);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t163 = gm_l0;
                    gm_t88rcucg1b7uk t164 = gm_l19;
                    gm_b16_8 t165 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t163, t164);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t166;
                    GM_ST(gm_b16_8, ((uint8_t *)&t166 + 0), t162);
                    GM_ST(gm_b16_8, ((uint8_t *)&t166 + 16), t165);
                    gm_f_format_j4sks15((void *)&gm_l33, (void *)NULL, cx, NULL, &t166);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t167 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                    if (!t167) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srclv0dcbbfhves, 1096, 28);
                    gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                    gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
                    gm_b0_1 t170 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t157, t159, t169);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)11ULL));
                    goto gm_fdone;
                }
            }
gm_lb34:;
        }
gm_lb33:;
    }
    {
        gm_t88rcucg1b7uk t171 = gm_l18;
        memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)7ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 8), t171);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_method_candidate_e20bo1i(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_declare_method_candidate_e20bo1i F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_t08olq97rscn3 gm_l2; /* name */
    gm_t88rcucg1b7uk gm_l3; /* first */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_t88rcucg1b7uk gm_l5; /* p */
    uint32_t gm_l6; /* adt */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 723u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l11;
    gm_t53at80pfdus3 gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_t88rcucg1b7uk t13 = gm_l3;
    gm_l4 = t13;
    {
        {
            void * t14 = gm_l0;
            void * t15 = (void *)(((uint8_t *)t14) + 8);
            gm_t88rcucg1b7uk t16 = gm_l3;
            gm_f_kind_jcnt4hm((void *)&gm_l11, t15, GM_CONV(gm_csguankko4lmhd, t16));
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)8ULL));
            if (!t18) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l11) + 8) + 4));
            gm_l5 = t19;
            gm_t88rcucg1b7uk t20 = gm_l5;
            gm_l4 = t20;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            {
                void * t21 = gm_l0;
                void * t22 = (void *)(((uint8_t *)t21) + 8);
                gm_t88rcucg1b7uk t23 = gm_l4;
                gm_f_kind_jcnt4hm((void *)&gm_l12, t22, GM_CONV(gm_csguankko4lmhd, t23));
            }
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)17ULL));
            if (!t25) {
                goto gm_lb4;
            }
            uint32_t t26 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 8));
            gm_l6 = t26;
            void * t27 = gm_l0;
            void * t28 = GM_LD(void *, (((uint8_t *)t27) + 1088));
            uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)t27) + 1088) + 8));
            uint32_t t30 = gm_l6;
            uint64_t t31 = (uint64_t)t30;
            if ((uintptr_t)t31 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t31, t29, gm_srclv0dcbbfhves, 1158, 8);
            uint64_t t32 = GM_LD(uint64_t, ((((uint8_t *)t28) + (uintptr_t)t31 * 184) + 16));
            uint64_t t33 = gm_l1;
            uint8_t t34 = (!(t32 == t33));
            if (t34) {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            {
                {
                    void * t35 = gm_l0;
                    void * t36 = GM_LD(void *, (((uint8_t *)t35) + 1088));
                    uint64_t t37 = GM_LD(uint64_t, ((((uint8_t *)t35) + 1088) + 8));
                    uint32_t t38 = gm_l6;
                    uint64_t t39 = (uint64_t)t38;
                    if ((uintptr_t)t39 >= (uintptr_t)(t37)) gm_trap_index((uintptr_t)t39, t37, gm_srclv0dcbbfhves, 1159, 24);
                    uint8_t t40 = GM_LD(uint8_t, (((((uint8_t *)t36) + (uintptr_t)t39 * 184) + 24) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)0ULL));
                    if (!t41) {
                        goto gm_lb6;
                    }
                    memmove(((uint8_t *)&F.l7), (((((uint8_t *)t36) + (uintptr_t)t39 * 184) + 24) + 8), 48);
                    gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                    gm_l8 = t42;
                    gm_l9 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t43 = gm_l9;
                            uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            uint8_t t45 = (t43 < t44);
                            if (!t45) {
                                goto gm_lb7;
                            }
                            {
                                void * t46 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                uint64_t t48 = gm_l9;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l10), (((uint8_t *)t46) + (uintptr_t)t48 * 40), 40);
                                        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                                        uint8_t t51 = (gm_eq0iqiubfm7e(((uint8_t *)&t49), ((uint8_t *)&t50)));
                                        if (t51) {
                                            {
                                                void * t52 = gm_l0;
                                                gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                                                void * t54 = gm_l0;
                                                gm_t88rcucg1b7uk t55 = gm_l4;
                                                gm_b16_8 t56 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t54, t55);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                                                gm_t6j6smj7qoctu t58;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t58 + 0), t56);
                                                GM_ST(gm_b16_8, ((uint8_t *)&t58 + 16), t57);
                                                gm_f_format_v0s3sc7((void *)&gm_l13, (void *)NULL, cx, NULL, &t58);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                                if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srclv0dcbbfhves, 1162, 36);
                                                gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
                                                gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                                                gm_b0_1 t62 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t52, t53, t61);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            goto gm_lb7;
                                        }
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1160, 9);
                                }
gm_lb10:;
                            }
gm_lb9:;
                            uint64_t t63 = gm_l9;
                            uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                            uint64_t t66 = r64;
                            gm_l9 = t66;
                        }
                    }
gm_lb7:;
                    goto gm_lb5;
                }
gm_lb6:;
            }
gm_lb5:;
            {
                void * t67 = gm_l0;
                void * t68 = (void *)(((uint8_t *)t67) + 1992);
                void * t69 = gm_l0;
                void * t70 = (void *)(((uint8_t *)t69) + 8);
                uint32_t t71 = gm_l6;
                uint32_t t72 = gm_f_template_9o92cvv(t70, t71);
                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)a2));
                gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a2) + 16));
                gm_t79rbdv4rafe5 t75;
                GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t75 + 36, 0);
                GM_ST(uint32_t, ((uint8_t *)&t75 + 0), t72);
                GM_ST(gm_b16_8, ((uint8_t *)&t75 + 8), t73);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t75 + 24), t74);
                gm_tftb1odc739t5 t76 = gm_f_push_fomabnf(t68, &t75);
                gm_l14 = t76;
            }
            uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srclv0dcbbfhves, 1167, 5);
            goto gm_lb3;
        }
gm_lb4:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
gm_lb3:;
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
extern __attribute__((aligned(4))) void gm_f_def_of_m512sbu(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tdgfduqjheqdm gm_f_def_of_m512sbu_c(void * a0, uint32_t a1) { gm_tdgfduqjheqdm gm_r; gm_f_def_of_m512sbu(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_effect_arg_tys_2a7r8hh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2) {
    struct gm_fr_effect_arg_tys_2a7r8hh F;
    gm_b16_8 gm_l0; /* refs */
    gm_t9qkh0rsooc8d gm_l1; /* need */
    void * gm_l2; /* tys */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tiilmfd16v3fc gm_l5; /* r */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l9; /* t */
    GM_STACK_CHECK(&F, 279u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l10;
    gm_b16_8 t14 = gm_l0;
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
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t18) + (uintptr_t)t20 * 24), 24);
                        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                        gm_l6 = t21;
                        gm_l7 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t22 = gm_l7;
                                uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint8_t t24 = (t22 < t23);
                                if (!t24) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t25 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    uint64_t t27 = gm_l7;
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l8), (((uint8_t *)t25) + (uintptr_t)t27 * 40), 40);
                                            {
                                                {
                                                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 0));
                                                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)0ULL));
                                                    if (!t29) {
                                                        goto gm_lb12;
                                                    }
                                                    gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 8));
                                                    gm_l9 = t30;
                                                    void * t31 = gm_l2;
                                                    gm_t88rcucg1b7uk t32 = gm_l9;
                                                    gm_t9qkh0rsooc8d t33 = gm_l1;
                                                    gm_t3jsf43p1pths t34;
                                                    GM_ST(uint8_t, (uint8_t *)&t34 + 5, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t34 + 6, 0);
                                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t34 + 0), t32);
                                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t34 + 4), t33);
                                                    gm_tftb1odc739t5 t35 = gm_f_push_h37silv(t31, t34);
                                                    gm_l10 = t35;
                                                    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                                                    if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srclv0dcbbfhves, 1257, 32);
                                                    goto gm_lb11;
                                                }
gm_lb12:;
                                            }
gm_lb11:;
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1256, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t37 = gm_l7;
                                uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                                uint64_t t40 = r38;
                                gm_l7 = t40;
                            }
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1255, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t41 = gm_l4;
            uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
            uint64_t t44 = r42;
            gm_l4 = t44;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
extern __attribute__((aligned(4))) void * gm_f_field_default_expr_tqfuiff(void * a0, uint32_t a1, uint64_t a2);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_bfunt71(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_hcjjkrm(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_hcjjkrm F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    uint8_t gm_l1; /* c */
    uint8_t gm_l2; /* c */
    uint8_t gm_l3; /* c */
    uint32_t gm_l4; /* id */
    uint8_t gm_l5; /* is_bad */
    GM_STACK_CHECK_SHALLOW(&F, 178u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t53at80pfdus3 gm_l6;
    uint8_t gm_l7;
    void * t19 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_non_constant_part_itk7b0v *)((&F.h)->up))->l0));
    void * t20 = (void *)(((uint8_t *)t19) + 8);
    gm_t88rcucg1b7uk t21 = gm_l0;
    gm_f_kind_jcnt4hm((void *)&gm_l6, t20, GM_CONV(gm_csguankko4lmhd, t21));
    {
        {
            {
                {
                    uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)8ULL));
                    if (!t23) {
                        goto gm_lb4;
                    }
                    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 8));
                    gm_l1 = t24;
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t26 = (uint8_t)(t25 == ((uint8_t)9ULL));
                    if (!t26) {
                        goto gm_lb5;
                    }
                    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 8));
                    gm_l1 = t27;
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t29 = (uint8_t)(t28 == ((uint8_t)10ULL));
                    if (!t29) {
                        goto gm_lb6;
                    }
                    uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 8));
                    gm_l1 = t30;
                    goto gm_lb3;
                }
gm_lb6:;
                goto gm_lb2;
            }
gm_lb3:;
            uint8_t t31 = gm_l1;
            uint8_t t32 = (uint8_t)!t31;
            gm_l5 = t32;
            goto gm_lb1;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t34 = (uint8_t)(t33 == ((uint8_t)7ULL));
                    if (!t34) {
                        goto gm_lb9;
                    }
                    goto gm_lb8;
                }
gm_lb9:;
                {
                    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
                    uint8_t t36 = (uint8_t)(t35 == ((uint8_t)18ULL));
                    if (!t36) {
                        goto gm_lb10;
                    }
                    goto gm_lb8;
                }
gm_lb10:;
                goto gm_lb7;
            }
gm_lb8:;
            gm_l5 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)17ULL));
            if (!t38) {
                goto gm_lb11;
            }
            uint32_t t39 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
            gm_l4 = t39;
            void * t40 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_non_constant_part_itk7b0v *)((&F.h)->up))->l0));
            void * t41 = (void *)(((uint8_t *)t40) + 8);
            uint32_t t42 = gm_l4;
            gm_b16_8 t43 = gm_f_adt_name_h7mjik7(t41, t42);
            gm_b16_8 t44;
            GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s4eilu97sooh1));
            GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(9));
            uint8_t t45 = (gm_eq0iqiubfm7e(((uint8_t *)&t43), ((uint8_t *)&t44)));
            gm_l5 = t45;
            goto gm_lb1;
        }
gm_lb11:;
        {
            gm_l5 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1129, 19);
    }
gm_lb1:;
    {
        {
            uint8_t t46 = gm_l5;
            if (!t46) {
                goto gm_lb14;
            }
            {
                {
                    uint8_t t47 = (uint8_t)((((uint8_t *)&((struct gm_fr_non_constant_part_itk7b0v *)((&F.h)->up))->l2))[4] != 0);
                    if (t47) {
                        goto gm_lb16;
                    }
                    gm_l7 = ((uint8_t)1);
                    goto gm_lb15;
                }
gm_lb16:;
                {
                    gm_l7 = ((uint8_t)0);
                    goto gm_lb15;
                }
gm_lb17:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1135, 23);
            }
gm_lb15:;
            uint8_t t48 = gm_l7;
            if (!t48) {
                goto gm_lb14;
            }
            gm_t88rcucg1b7uk t49 = gm_l0;
            gm_tkn42u7na3t81 t50;
            GM_ST(uint8_t, (uint8_t *)&t50 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t50 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t50), t49);
            ((uint8_t *)&t50)[4] = 1;
            GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&((struct gm_fr_non_constant_part_itk7b0v *)((&F.h)->up))->l2), t50);
            goto gm_lb13;
        }
gm_lb14:;
    }
gm_lb13:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_repdalk(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_fn_repdalk F;
    gm_t88rcucg1b7uk gm_l0; /* x */
    uint32_t gm_l1; /* id */
    uint64_t gm_l2; /* d */
    gm_t9qkh0rsooc8d gm_l3; /* vis */
    GM_STACK_CHECK_SHALLOW(&F, 238u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    gm_t53at80pfdus3 gm_l5;
    {
        {
            uint8_t t9 = (uint8_t)((((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l3))[8] != 0);
            if (t9) {
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
        gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1287, 12);
    }
gm_lb1:;
    uint8_t t10 = gm_l4;
    if (t10) {
        {
            {
                void * t11 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l0));
                void * t12 = (void *)(((uint8_t *)t11) + 8);
                gm_t88rcucg1b7uk t13 = gm_l0;
                gm_f_kind_jcnt4hm((void *)&gm_l5, t12, GM_CONV(gm_csguankko4lmhd, t13));
                uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                uint8_t t15 = (uint8_t)(t14 == ((uint8_t)17ULL));
                if (!t15) {
                    goto gm_lb5;
                }
                uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
                gm_l1 = t16;
                {
                    {
                        void * t17 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l0));
                        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 1088));
                        uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)t17) + 1088) + 8));
                        void * t20 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l0));
                        void * t21 = (void *)(((uint8_t *)t20) + 8);
                        uint32_t t22 = gm_l1;
                        uint32_t t23 = gm_f_template_9o92cvv(t21, t22);
                        uint64_t t24 = (uint64_t)t23;
                        if ((uintptr_t)t24 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t24, t19, gm_srclv0dcbbfhves, 1289, 25);
                        uint8_t t25 = (uint8_t)((((((uint8_t *)t18) + (uintptr_t)t24 * 184) + 168))[8] != 0);
                        if (!t25) {
                            goto gm_lb7;
                        }
                        uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t18) + (uintptr_t)t24 * 184) + 168));
                        gm_l2 = t26;
                        void * t27 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l0));
                        void * t28 = GM_LD(void *, ((uint8_t *)t27));
                        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 240));
                        uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t28) + 240) + 8));
                        uint64_t t31 = gm_l2;
                        if ((uintptr_t)t31 >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)t31, t30, gm_srclv0dcbbfhves, 1290, 28);
                        gm_t9qkh0rsooc8d t32 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t29) + (uintptr_t)t31 * 216) + 40) + 56));
                        gm_l3 = t32;
                        gm_t9qkh0rsooc8d t33 = gm_l3;
                        uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&t33));
                        gm_t9qkh0rsooc8d t35 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l2));
                        uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)&t35));
                        uint8_t t37 = (t34 < t36);
                        if (t37) {
                            gm_t88rcucg1b7uk t38 = gm_l0;
                            gm_t9qkh0rsooc8d t39 = gm_l3;
                            gm_t3jsf43p1pths t40;
                            GM_ST(uint8_t, (uint8_t *)&t40 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t40 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t40 + 0), t38);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t40 + 4), t39);
                            gm_trcagkhgm1qdu t41;
                            GM_ST(uint8_t, (uint8_t *)&t41 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t41 + 10, 0);
                            GM_ST(gm_t3jsf43p1pths, ((uint8_t *)&t41), t40);
                            ((uint8_t *)&t41)[8] = 1;
                            GM_ST(gm_trcagkhgm1qdu, ((uint8_t *)&((struct gm_fr_hidden_part_is5hgf9 *)((&F.h)->up))->l3), t41);
                        }
                        goto gm_lb6;
                    }
gm_lb7:;
                }
gm_lb6:;
                goto gm_lb4;
            }
gm_lb5:;
        }
gm_lb4:;
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_frame_up_h0ddp1d(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_frame_up_h0ddp1d F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    uint32_t gm_l2; /* levels */
    uint32_t gm_l3; /* cur */
    uint32_t gm_l4; /* <tmp> */
    uint32_t gm_l5; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 175u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint32_t gm_l6;
    uint32_t t7 = gm_l1;
    gm_l3 = t7;
    gm_l5 = ((uint32_t)0ULL);
    uint32_t t8 = gm_l2;
    gm_l4 = t8;
    {
        for (;;) {
            uint32_t t9 = gm_l5;
            uint32_t t10 = gm_l4;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint32_t t12 = gm_l3;
                        uint64_t t13 = (uint64_t)t12;
                        void * t14 = gm_l0;
                        uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t14) + 384) + 8));
                        uint8_t t16 = (t13 >= t15);
                        if (t16) {
                            gm_t9805qtonbsag t17;
                            memset(&t17, 0, sizeof t17);
                            F.ret = t17;
                            goto gm_fret;
                        }
                        void * t18 = gm_l0;
                        void * t19 = GM_LD(void *, (((uint8_t *)t18) + 384));
                        uint64_t t20 = GM_LD(uint64_t, ((((uint8_t *)t18) + 384) + 8));
                        uint32_t t21 = gm_l3;
                        uint64_t t22 = (uint64_t)t21;
                        if ((uintptr_t)t22 >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)t22, t20, gm_srclv0dcbbfhves, 608, 15);
                        uint8_t t23 = (uint8_t)((((((uint8_t *)t19) + (uintptr_t)t22 * 64) + 48))[4] != 0);
                        if (t23) {
                            uint32_t t24 = GM_LD(uint32_t, ((((uint8_t *)t19) + (uintptr_t)t22 * 64) + 48));
                            gm_l6 = t24;
                        } else {
                            gm_t9805qtonbsag t25;
                            memset(&t25, 0, sizeof t25);
                            F.ret = t25;
                            goto gm_fret;
                        }
                        uint32_t t26 = gm_l6;
                        gm_l3 = t26;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 606, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint32_t t27 = gm_l5;
            uint32_t r28; bool o29 = __builtin_add_overflow(t27, ((uint32_t)1ULL), &r28);
            uint32_t t30 = r28;
            gm_l5 = t30;
        }
    }
gm_lb1:;
    uint32_t t31 = gm_l3;
    gm_t9805qtonbsag t32;
    GM_ST(uint8_t, (uint8_t *)&t32 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t32 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t32), t31);
    ((uint8_t *)&t32)[4] = 1;
    F.ret = t32;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) uint32_t gm_f_generic_of_1861hm9(void * a0, uint32_t a1);
extern __attribute__((aligned(4))) gm_trcagkhgm1qdu gm_f_hidden_part_is5hgf9(void * a0, gm_csguankko4lmhd a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) uint8_t gm_f_in_generic_scope_rvb92ab(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_in_generic_scope_rvb92ab F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    gm_tep2drsl9408b gm_l3; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 177u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 1648) + 8));
    uint8_t t9 = (t8 > ((uint64_t)0ULL));
    if (t9) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    void * t10 = gm_l0;
    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 1608));
    gm_l1 = t11;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l2;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                void * t15 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t17 = gm_l2;
                {
                    {
                        memmove(((uint8_t *)&gm_l3), (((uint8_t *)t15) + (uintptr_t)t17 * 24), 24);
                        void * t18 = gm_l0;
                        void * t19 = (void *)(((uint8_t *)t18) + 8);
                        gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l3) + 16));
                        uint8_t t21 = gm_f_has_param_u9t0efs(t19, GM_CONV(gm_csguankko4lmhd, t20));
                        if (t21) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 635, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t22 = gm_l2;
            uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
            uint64_t t25 = r23;
            gm_l2 = t25;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_ambient_m41pl1e(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_ambient_m41pl1e F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* eff */
    void * gm_l2; /* info */
    GM_STACK_CHECK_SHALLOW(&F, 377u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l3;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 1048));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1048) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_srclv0dcbbfhves, 724, 14);
    void * t7 = (void *)(((uint8_t *)t3) + (uintptr_t)t6 * 56);
    gm_l2 = t7;
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    void * t10 = GM_LD(void *, (((uint8_t *)t9) + 200));
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t9) + 200) + 8));
    void * t12 = gm_l2;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 16));
    if ((uintptr_t)t13 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t13, t11, gm_srclv0dcbbfhves, 725, 12);
    uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t10) + (uintptr_t)t13 * 184) + 112));
    uint8_t t15 = (uint8_t)!t14;
    if (t15) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t16 = gm_l2;
    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)t16));
    gm_b16_8 t18;
    GM_ST(void *, (uint8_t *)&t18, (void *)(gm_ss6g2koqn63bf));
    GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
    uint8_t t19 = (!gm_eq0iqiubfm7e(((uint8_t *)&t17), ((uint8_t *)&t18)));
    if (t19) {
        void * t20 = gm_l2;
        gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)t20));
        gm_t61mja4eihh2b t22 = GM_LD(gm_t61mja4eihh2b, ((uint8_t *)gm_cp0okt01b9q));
        uint8_t t23 = 0;
        for (uintptr_t i = 0; i < (uintptr_t)(12); i++) if (gm_eq0iqiubfm7e(((uint8_t *)&t22) + i * 16, ((uint8_t *)&t21))) { t23 = 1; break; }
        uint8_t t24 = (uint8_t)(t23);
        gm_l3 = t24;
    } else {
        gm_l3 = ((uint8_t)0);
    }
    uint8_t t25 = gm_l3;
    F.ret = t25;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_structural_alias_qpmv4mu(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_structural_alias_qpmv4mu F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    void * gm_l2; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l4;
    uint8_t gm_l5;
    void * t18 = gm_l0;
    void * t19 = GM_LD(void *, ((uint8_t *)t18));
    void * t20 = GM_LD(void *, (((uint8_t *)t19) + 240));
    uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t19) + 240) + 8));
    uint64_t t22 = gm_l1;
    if ((uintptr_t)t22 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t22, t21, gm_srclv0dcbbfhves, 814, 11);
    void * t23 = (void *)(((uint8_t *)t20) + (uintptr_t)t22 * 216);
    gm_l2 = t23;
    void * t24 = gm_l2;
    uint64_t t25 = GM_LD(uint64_t, (((((uint8_t *)t24) + 40) + 40) + 8));
    uint8_t t26 = (t25 > ((uint64_t)0ULL));
    if (t26) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    {
        {
            void * t27 = gm_l2;
            uint8_t t28 = GM_LD(uint8_t, (((((uint8_t *)t27) + 40) + 64) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)0ULL));
            if (!t29) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l3), (((((uint8_t *)t27) + 40) + 64) + 8), 64);
            {
                {
                    uint8_t t30 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l3) + 24)) != 0);
                    if (t30) {
                        goto gm_lb4;
                    }
                    gm_l4 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l4 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 817, 8);
            }
gm_lb3:;
            uint8_t t31 = gm_l4;
            if (t31) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t32 = GM_LD(void *, (((uint8_t *)&F.l3) + 40));
            {
                {
                    {
                        {
                            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)24ULL));
                            if (!t34) {
                                goto gm_lb9;
                            }
                            goto gm_lb8;
                        }
gm_lb9:;
                        {
                            uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)25ULL));
                            if (!t36) {
                                goto gm_lb10;
                            }
                            goto gm_lb8;
                        }
gm_lb10:;
                        {
                            uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)26ULL));
                            if (!t38) {
                                goto gm_lb11;
                            }
                            goto gm_lb8;
                        }
gm_lb11:;
                        {
                            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)27ULL));
                            if (!t40) {
                                goto gm_lb12;
                            }
                            goto gm_lb8;
                        }
gm_lb12:;
                        {
                            uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)28ULL));
                            if (!t42) {
                                goto gm_lb13;
                            }
                            goto gm_lb8;
                        }
gm_lb13:;
                        {
                            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                            uint8_t t44 = (uint8_t)(t43 == ((uint8_t)29ULL));
                            if (!t44) {
                                goto gm_lb14;
                            }
                            goto gm_lb8;
                        }
gm_lb14:;
                        {
                            uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                            uint8_t t46 = (uint8_t)(t45 == ((uint8_t)30ULL));
                            if (!t46) {
                                goto gm_lb15;
                            }
                            goto gm_lb8;
                        }
gm_lb15:;
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_l5 = ((uint8_t)1);
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    gm_l5 = ((uint8_t)0);
                    goto gm_lb6;
                }
gm_lb16:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 818, 12);
            }
gm_lb6:;
            uint8_t t47 = gm_l5;
            F.ret = t47;
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
extern __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_non_constant_part_itk7b0v(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_prelude_effect_gilrmm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_prelude_effect_gilrmm5 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* prelude */
    uint64_t gm_l3; /* d */
    uint32_t gm_l4; /* eff */
    GM_STACK_CHECK(&F, 440u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l5;
    gm_t6qooom8qa2do gm_l6;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint8_t t6 = (uint8_t)(((((uint8_t *)t5) + 320))[8] != 0);
    if (t6) {
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t5) + 320));
        gm_l2 = t7;
    } else {
        memset(&(*(gm_tqu3m2l3n8mnc *)gm_out), 0, sizeof (*(gm_tqu3m2l3n8mnc *)gm_out));
        goto gm_fdone;
    }
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, ((uint8_t *)t8));
    uint64_t t10 = gm_l2;
    gm_b16_8 t11 = gm_l1;
    gm_tf5srh4mp4b3i t12 = gm_f_lookup_59juqoo(t9, t10, t11);
    gm_l5 = t12;
    uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
    if (t13) {
        uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
        gm_l3 = t14;
    } else {
        memset(&(*(gm_tqu3m2l3n8mnc *)gm_out), 0, sizeof (*(gm_tqu3m2l3n8mnc *)gm_out));
        goto gm_fdone;
    }
    {
        {
            {
                void * t15 = gm_l0;
                uint64_t t16 = gm_l3;
                gm_f_resolve_decl_pus042v((void *)&gm_l6, (void *)NULL, cx, NULL, t15, t16);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)2ULL));
            if (!t18) {
                goto gm_lb2;
            }
            uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
            gm_l4 = t19;
            {
                uint32_t t20 = gm_l4;
                gm_b16_8 t21 = gm_f_empty_slice_fh7svmj();
                gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
                gm_tiilmfd16v3fc t23;
                GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t23 + 0), t20);
                GM_ST(gm_b16_8, ((uint8_t *)&t23 + 8), t22);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out) + 28, 0);
                GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out)), t23);
                ((uint8_t *)&(*(gm_tqu3m2l3n8mnc *)gm_out))[24] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
        memset(&(*(gm_tqu3m2l3n8mnc *)gm_out), 0, sizeof (*(gm_tqu3m2l3n8mnc *)gm_out));
        goto gm_fdone;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_prelude_instance_arg_kh32dmf F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* name */
    uint32_t gm_l3; /* id */
    gm_b16_8 gm_l4; /* args */
    uint32_t gm_l5; /* template */
    gm_t88rcucg1b7uk gm_l6; /* e */
    GM_STACK_CHECK(&F, 321u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l7;
    gm_t9805qtonbsag gm_l8;
    uint8_t gm_l9;
    {
        {
            void * t6 = gm_l0;
            void * t7 = (void *)(((uint8_t *)t6) + 8);
            gm_t88rcucg1b7uk t8 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l7, t7, GM_CONV(gm_csguankko4lmhd, t8));
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)17ULL));
            if (!t10) {
                goto gm_lb2;
            }
            uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 8));
            gm_l3 = t11;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l7) + 8) + 8));
            gm_l4 = t12;
            void * t13 = gm_l0;
            gm_b16_8 t14 = gm_l2;
            gm_t9805qtonbsag t15 = gm_f_prelude_template_7s9nsp0((void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t15;
            uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l8))[4] != 0);
            if (t16) {
                uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)&gm_l8));
                gm_l5 = t17;
            } else {
                gm_tkn42u7na3t81 t18;
                memset(&t18, 0, sizeof t18);
                F.ret = t18;
                goto gm_fret;
            }
            void * t19 = gm_l0;
            void * t20 = (void *)(((uint8_t *)t19) + 8);
            uint32_t t21 = gm_l3;
            uint32_t t22 = gm_f_template_9o92cvv(t20, t21);
            uint32_t t23 = gm_l5;
            uint8_t t24 = (!(t22 == t23));
            if (t24) {
                gm_l9 = ((uint8_t)1);
            } else {
                uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint8_t t26 = ((t25 == ((uint64_t)0ULL)));
                gm_l9 = t26;
            }
            uint8_t t27 = gm_l9;
            if (t27) {
                gm_tkn42u7na3t81 t28;
                memset(&t28, 0, sizeof t28);
                F.ret = t28;
                goto gm_fret;
            }
            {
                {
                    void * t29 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t30, gm_srclv0dcbbfhves, 700, 17);
                    uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t29) + (uintptr_t)((uint64_t)0ULL) * 40) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)0ULL));
                    if (!t32) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t29) + (uintptr_t)((uint64_t)0ULL) * 40) + 8));
                    gm_l6 = t33;
                    gm_t88rcucg1b7uk t34 = gm_l6;
                    gm_tkn42u7na3t81 t35;
                    GM_ST(uint8_t, (uint8_t *)&t35 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t35 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t35), t34);
                    ((uint8_t *)&t35)[4] = 1;
                    F.ret = t35;
                    goto gm_fret;
                }
gm_lb4:;
                gm_tkn42u7na3t81 t36;
                memset(&t36, 0, sizeof t36);
                F.ret = t36;
                goto gm_fret;
            }
gm_lb3:;
        }
gm_lb2:;
        gm_tkn42u7na3t81 t37;
        memset(&t37, 0, sizeof t37);
        F.ret = t37;
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_prelude_template_7s9nsp0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_prelude_template_7s9nsp0 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    uint64_t gm_l2; /* prelude */
    uint64_t gm_l3; /* d */
    uint32_t gm_l4; /* template */
    GM_STACK_CHECK(&F, 432u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l5;
    gm_t6qooom8qa2do gm_l6;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint8_t t6 = (uint8_t)(((((uint8_t *)t5) + 320))[8] != 0);
    if (t6) {
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)t5) + 320));
        gm_l2 = t7;
    } else {
        gm_t9805qtonbsag t8;
        memset(&t8, 0, sizeof t8);
        F.ret = t8;
        goto gm_fret;
    }
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, ((uint8_t *)t9));
    uint64_t t11 = gm_l2;
    gm_b16_8 t12 = gm_l1;
    gm_tf5srh4mp4b3i t13 = gm_f_lookup_59juqoo(t10, t11, t12);
    gm_l5 = t13;
    uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
    if (t14) {
        uint64_t t15 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
        gm_l3 = t15;
    } else {
        gm_t9805qtonbsag t16;
        memset(&t16, 0, sizeof t16);
        F.ret = t16;
        goto gm_fret;
    }
    {
        {
            void * t17 = gm_l0;
            uint64_t t18 = gm_l3;
            gm_f_resolve_decl_pus042v((void *)&gm_l6, (void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)1ULL));
            if (!t20) {
                goto gm_lb2;
            }
            uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 8));
            gm_l4 = t21;
            uint32_t t22 = gm_l4;
            gm_t9805qtonbsag t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t23), t22);
            ((uint8_t *)&t23)[4] = 1;
            F.ret = t23;
            goto gm_fret;
        }
gm_lb2:;
        gm_t9805qtonbsag t24;
        memset(&t24, 0, sizeof t24);
        F.ret = t24;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_field_tys_v4ep4t3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1, void * a2) {
    struct gm_fr_push_field_tys_v4ep4t3 F;
    gm_b16_8 gm_l0; /* fields */
    gm_t9qkh0rsooc8d gm_l1; /* vis */
    void * gm_l2; /* tys */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l6; /* need */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l7;
    gm_b16_8 t7 = gm_l0;
    gm_l3 = t7;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t8 = gm_l4;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                void * t11 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t13 = gm_l4;
                {
                    {
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)t11) + (uintptr_t)t13 * 40), 40);
                        gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l5) + 20));
                        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t14) + 0));
                        uint8_t t16 = (uint8_t)((t15 == ((uint8_t)0ULL)));
                        gm_t9qkh0rsooc8d t17;
                        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)0ULL));
                        if (t16) {
                            goto gm_lb3;
                        }
                        gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l5) + 20));
                        uint8_t t19 = GM_LD(uint8_t, ((uint8_t *)&t18));
                        gm_t9qkh0rsooc8d t20 = gm_l1;
                        uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        uint8_t t22 = (t19 < t21);
                        if (t22) {
                            gm_t9qkh0rsooc8d t23 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l5) + 20));
                            gm_l6 = t23;
                        } else {
                            gm_t9qkh0rsooc8d t24 = gm_l1;
                            gm_l6 = t24;
                        }
                        void * t25 = gm_l2;
                        gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 16));
                        gm_t9qkh0rsooc8d t27 = gm_l6;
                        gm_t3jsf43p1pths t28;
                        GM_ST(uint8_t, (uint8_t *)&t28 + 5, 0);
                        GM_ST(uint16_t, (uint8_t *)&t28 + 6, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t28 + 0), t26);
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t28 + 4), t27);
                        gm_tftb1odc739t5 t29 = gm_f_push_h37silv(t25, t28);
                        gm_l7 = t29;
                        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srclv0dcbbfhves, 1278, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 1275, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t31 = gm_l4;
            uint64_t r32; bool o33 = __builtin_add_overflow(t31, ((uint64_t)1ULL), &r32);
            uint64_t t34 = r32;
            gm_l4 = t34;
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
inline __attribute__((aligned(4))) gm_thetuhtouve7f gm_f_range_of_dmmo768(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_range_of_dmmo768 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* e */
    gm_t88rcucg1b7uk gm_l3; /* e */
    GM_STACK_CHECK(&F, 202u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tkn42u7na3t81 gm_l4;
    gm_tkn42u7na3t81 gm_l5;
    {
        {
            void * t6 = gm_l0;
            gm_t88rcucg1b7uk t7 = gm_l1;
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_seue7mq07v0oc));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(5));
            gm_tkn42u7na3t81 t9 = gm_f_prelude_instance_arg_kh32dmf((void *)NULL, cx, NULL, t6, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t9;
            uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l4))[4] != 0);
            if (!t10) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l4));
            gm_l2 = t11;
            gm_t88rcucg1b7uk t12 = gm_l2;
            gm_tbngattl1fo51 t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t13 + 4), t12);
            gm_thetuhtouve7f t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[8] = 1;
            F.ret = t14;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t15 = gm_l0;
            gm_t88rcucg1b7uk t16 = gm_l1;
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s43cq4krigs2b));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(9));
            gm_tkn42u7na3t81 t18 = gm_f_prelude_instance_arg_kh32dmf((void *)NULL, cx, NULL, t15, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t18;
            uint8_t t19 = (uint8_t)((((uint8_t *)&gm_l5))[4] != 0);
            if (!t19) {
                goto gm_lb4;
            }
            gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l5));
            gm_l3 = t20;
            gm_t88rcucg1b7uk t21 = gm_l3;
            gm_tbngattl1fo51 t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)1));
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t22 + 4), t21);
            gm_thetuhtouve7f t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 10, 0);
            GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t23), t22);
            ((uint8_t *)&t23)[8] = 1;
            F.ret = t23;
            goto gm_fret;
        }
gm_lb4:;
    }
gm_lb3:;
    gm_thetuhtouve7f t24;
    memset(&t24, 0, sizeof t24);
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
extern __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_range_template_dbt7218(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_cycle_mauubd4(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_report_cycle_mauubd4 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l2; /* start */
    gm_b16_8 gm_l3; /* cycle */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* d */
    gm_tl874t2atnlh3 gm_l8; /* span */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* c */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* c */
    GM_STACK_CHECK(&F, 1708u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tabb710d9mq2e gm_l20;
    void * t17 = gm_l0;
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t17) + 1768));
    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
    uint64_t t20 = gm_l1;
    gm_tf5srh4mp4b3i t21 = gm_f_index_of_s9trr09(t19, t20);
    gm_l16 = t21;
    uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
    if (t22) {
        uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&gm_l16));
        gm_l2 = t23;
    } else {
        gm_l2 = ((uint64_t)0ULL);
    }
    void * t24 = gm_l0;
    void * t25 = GM_LD(void *, (((uint8_t *)t24) + 1768));
    uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)t24) + 1768) + 8));
    uint64_t t27 = gm_l2;
    uint64_t t28 = (uintptr_t)t27;
    uint64_t t29 = (uintptr_t)t26;
    if (t28 > t29 || t29 > (uintptr_t)t26) gm_trap_slice(t28, t29, t26, gm_srclv0dcbbfhves, 826, 14);
    uint64_t t30 = ((uintptr_t)t26) - (uintptr_t)t27;
    gm_b16_8 t31;
    GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t25 + (uintptr_t)t27 * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(t30));
    gm_l3 = t31;
    {
        gm_trqfvmjtt4sij t32;
        gm_f_list_epvducg((void *)&t32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t32;
    }
    gm_b16_8 t33 = gm_l3;
    gm_l5 = t33;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t34 = gm_l6;
            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t36 = (t34 < t35);
            if (!t36) {
                goto gm_lb1;
            }
            {
                void * t37 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t39 = gm_l6;
                {
                    {
                        uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)t37) + (uintptr_t)t39 * 8));
                        gm_l7 = t40;
                        void * t41 = (void *)((uint8_t *)&F.l4);
                        void * t42 = gm_l0;
                        void * t43 = GM_LD(void *, ((uint8_t *)t42));
                        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 240));
                        uint64_t t45 = GM_LD(uint64_t, ((((uint8_t *)t43) + 240) + 8));
                        uint64_t t46 = gm_l7;
                        if ((uintptr_t)t46 >= (uintptr_t)(t45)) gm_trap_index((uintptr_t)t46, t45, gm_srclv0dcbbfhves, 828, 52);
                        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t44) + (uintptr_t)t46 * 216));
                        gm_tnpifv20irnph t48;
                        GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
                        gm_f_format_h4jcodi((void *)&gm_l17, (void *)NULL, cx, NULL, t48);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                        if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srclv0dcbbfhves, 828, 33);
                        gm_b16_8 t50 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                        gm_tftb1odc739t5 t52 = gm_f_push_3ef746b(t41, t51);
                        gm_l18 = t52;
                        uint8_t t53 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                        if (!t53) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srclv0dcbbfhves, 828, 22);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 828, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t54 = gm_l6;
            uint64_t r55; bool o56 = __builtin_add_overflow(t54, ((uint64_t)1ULL), &r55);
            uint64_t t57 = r55;
            gm_l6 = t57;
        }
    }
gm_lb1:;
    void * t58 = gm_l0;
    void * t59 = GM_LD(void *, ((uint8_t *)t58));
    void * t60 = GM_LD(void *, (((uint8_t *)t59) + 240));
    uint64_t t61 = GM_LD(uint64_t, ((((uint8_t *)t59) + 240) + 8));
    uint64_t t62 = gm_l1;
    if ((uintptr_t)t62 >= (uintptr_t)(t61)) gm_trap_index((uintptr_t)t62, t61, gm_srclv0dcbbfhves, 829, 13);
    gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t60) + (uintptr_t)t62 * 216) + 16));
    gm_l8 = t63;
    {
        gm_tl874t2atnlh3 t64 = gm_l8;
        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
        gm_b16_8 t67;
        GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sdbvaa960ee0f));
        GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(4));
        gm_f_join_sfknljb((void *)&gm_l19, (void *)NULL, cx, NULL, t66, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
        if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srclv0dcbbfhves, 830, 83);
        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
        gm_tnpifv20irnph t70;
        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t69);
        gm_f_format_slm5ovr((void *)&gm_l20, (void *)NULL, cx, NULL, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
        if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srclv0dcbbfhves, 830, 27);
        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
        gm_t4gut2c9fj1kt t74;
        gm_f_error_s6hn871((void *)&t74, (void *)NULL, cx, NULL, t64, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l9 = t74;
    }
    void * t75 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t77 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t78 = (uintptr_t)t76;
    if (t77 > t78 || t78 > (uintptr_t)t76) gm_trap_slice(t77, t78, t76, gm_srclv0dcbbfhves, 831, 14);
    uint64_t t79 = ((uintptr_t)t76) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t80;
    GM_ST(void *, (uint8_t *)&t80, (void *)(((uint8_t *)t75 + (uintptr_t)((uint64_t)1ULL) * 8)));
    GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(t79));
    gm_l10 = t80;
    gm_l11 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t81 = gm_l11;
            uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            uint8_t t83 = (t81 < t82);
            if (!t83) {
                goto gm_lb6;
            }
            {
                void * t84 = GM_LD(void *, ((uint8_t *)&gm_l10));
                uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                uint64_t t86 = gm_l11;
                {
                    {
                        uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)t84) + (uintptr_t)t86 * 8));
                        gm_l12 = t87;
                        void * t88 = (void *)((uint8_t *)&F.l9);
                        void * t89 = gm_l0;
                        void * t90 = GM_LD(void *, ((uint8_t *)t89));
                        void * t91 = GM_LD(void *, (((uint8_t *)t90) + 240));
                        uint64_t t92 = GM_LD(uint64_t, ((((uint8_t *)t90) + 240) + 8));
                        uint64_t t93 = gm_l12;
                        if ((uintptr_t)t93 >= (uintptr_t)(t92)) gm_trap_index((uintptr_t)t93, t92, gm_srclv0dcbbfhves, 831, 39);
                        gm_tl874t2atnlh3 t94 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t91) + (uintptr_t)t93 * 216) + 16));
                        gm_b16_8 t95;
                        GM_ST(void *, (uint8_t *)&t95, (void *)(gm_s68uiiatavvqh));
                        GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(17));
                        gm_f_add_label_50fj7vv(t88, GM_CONV(gm_cseahvg8v7p0vs, t94), t95);
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 831, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t96 = gm_l11;
            uint64_t r97; bool o98 = __builtin_add_overflow(t96, ((uint64_t)1ULL), &r97);
            uint64_t t99 = r97;
            gm_l11 = t99;
        }
    }
gm_lb6:;
    {
        void * t100 = gm_l0;
        gm_b0_1 t101 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t100, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l9)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_b16_8 t102 = gm_l3;
    gm_l13 = t102;
    gm_l14 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t103 = gm_l14;
            uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
            uint8_t t105 = (t103 < t104);
            if (!t105) {
                goto gm_lb11;
            }
            {
                void * t106 = GM_LD(void *, ((uint8_t *)&gm_l13));
                uint64_t t107 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                uint64_t t108 = gm_l14;
                {
                    {
                        uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)t106) + (uintptr_t)t108 * 8));
                        gm_l15 = t109;
                        {
                            void * t110 = gm_l0;
                            uint64_t t111 = gm_l15;
                            gm_t6qooom8qa2do t112;
                            memset(&t112, 0, sizeof t112);
                            GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)11ULL));
                            gm_tagrkbqh054h7 t113;
                            GM_ST(uint8_t, (uint8_t *)&t113 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t113 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t113 + 0), ((uint8_t)2ULL));
                            GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t113 + 8), t112);
                            gm_b0_1 t114 = gm_f_set_state_3j300fi((void *)NULL, cx, NULL, t110, t111, &t113);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        goto gm_lb14;
                    }
gm_lb15:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 833, 5);
                }
gm_lb14:;
            }
gm_lb13:;
            uint64_t t115 = gm_l14;
            uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
            uint64_t t118 = r116;
            gm_l14 = t118;
        }
    }
gm_lb11:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_resolve_ambient_6orl0vo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_resolve_ambient_6orl0vo F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* prelude */
    uint64_t gm_l2; /* d */
    uint32_t gm_l3; /* e */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* name */
    uint64_t gm_l7; /* d */
    GM_STACK_CHECK(&F, 842u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tf5srh4mp4b3i gm_l8;
    gm_t6qooom8qa2do gm_l9;
    gm_tf5srh4mp4b3i gm_l10;
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, ((uint8_t *)t13));
    uint8_t t15 = (uint8_t)(((((uint8_t *)t14) + 320))[8] != 0);
    if (t15) {
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t14) + 320));
        gm_l1 = t16;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, ((uint8_t *)t17));
            uint64_t t19 = gm_l1;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_ses7tdrdt8a6u));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(5));
            gm_tf5srh4mp4b3i t21 = gm_f_lookup_59juqoo(t18, t19, t20);
            gm_l8 = t21;
            uint8_t t22 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
            if (!t22) {
                goto gm_lb2;
            }
            uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
            gm_l2 = t23;
            {
                {
                    void * t24 = gm_l0;
                    uint64_t t25 = gm_l2;
                    gm_f_resolve_decl_pus042v((void *)&gm_l9, (void *)NULL, cx, NULL, t24, t25);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)2ULL));
                    if (!t27) {
                        goto gm_lb4;
                    }
                    uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 8));
                    gm_l3 = t28;
                    void * t29 = gm_l0;
                    uint32_t t30 = gm_l3;
                    gm_t9805qtonbsag t31;
                    GM_ST(uint8_t, (uint8_t *)&t31 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t31 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t31), t30);
                    ((uint8_t *)&t31)[4] = 1;
                    GM_ST(gm_t9805qtonbsag, (((uint8_t *)t29) + 2076), t31);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t32 = (void *)((uint8_t *)gm_cp0okt01b9q);
    uint64_t t33 = ((uintptr_t)((uint64_t)12ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t34;
    GM_ST(void *, (uint8_t *)&t34, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(t33));
    gm_l4 = t34;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t35 = gm_l5;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t37 = (t35 < t36);
            if (!t37) {
                goto gm_lb5;
            }
            {
                void * t38 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t40 = gm_l5;
                {
                    {
                        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)t38) + (uintptr_t)t40 * 16));
                        gm_l6 = t41;
                        {
                            {
                                void * t42 = gm_l0;
                                void * t43 = GM_LD(void *, ((uint8_t *)t42));
                                uint64_t t44 = gm_l1;
                                gm_b16_8 t45 = gm_l6;
                                gm_tf5srh4mp4b3i t46 = gm_f_lookup_59juqoo(t43, t44, t45);
                                gm_l10 = t46;
                                uint8_t t47 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
                                if (!t47) {
                                    goto gm_lb11;
                                }
                                uint64_t t48 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                                gm_l7 = t48;
                                void * t49 = gm_l0;
                                uint64_t t50 = gm_l7;
                                gm_t6qooom8qa2do t51;
                                gm_f_resolve_decl_pus042v((void *)&t51, (void *)NULL, cx, NULL, t49, t50);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb10;
                            }
gm_lb11:;
                        }
gm_lb10:;
                        goto gm_lb8;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 671, 5);
                }
gm_lb8:;
            }
gm_lb7:;
            uint64_t t52 = gm_l5;
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            uint64_t t55 = r53;
            gm_l5 = t55;
        }
    }
gm_lb5:;
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
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_resolve_decl_pus042v F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l4; /* i */
    uint8_t gm_l5; /* quiet */
    GM_STACK_CHECK(&F, 2052u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tagrkbqh054h7 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tf5srh4mp4b3i gm_l14;
    gm_tftb1odc739t5 gm_l15;
    {
        {
            void * t14 = gm_l0;
            uint64_t t15 = gm_l1;
            gm_f_state_a5bnsjb((void *)&gm_l12, (void *)NULL, cx, NULL, t14, t15);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                uint8_t t17 = (uint8_t)(t16 == ((uint8_t)2ULL));
                if (!t17) {
                    goto gm_lb3;
                }
                {
                    memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l12) + 8), 208);
                }
                {
                    memmove(gm_out, ((uint8_t *)&F.l2), 208);
                    goto gm_fdone;
                }
            }
gm_lb3:;
            {
                uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                uint8_t t19 = (uint8_t)(t18 == ((uint8_t)1ULL));
                if (!t19) {
                    goto gm_lb4;
                }
                {
                    {
                        void * t20 = gm_l0;
                        uint64_t t21 = gm_l1;
                        uint8_t t22 = gm_f_is_structural_alias_qpmv4mu(t20, t21);
                        if (!t22) {
                            goto gm_lb6;
                        }
                        uint64_t t23 = gm_l1;
                        void * t24 = gm_l0;
                        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t24) + 1808));
                        uint8_t t26 = 0;
                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t25) + 8)); i++) if (gm_eq6groq37ram(((uint8_t *)GM_LD(void *, ((uint8_t *)&t25))) + i * 8, ((uint8_t *)&t23))) { t26 = 1; break; }
                        uint8_t t27 = (uint8_t)(!t26);
                        if (!t27) {
                            goto gm_lb6;
                        }
                        void * t28 = gm_l0;
                        void * t29 = (void *)(((uint8_t *)t28) + 1808);
                        uint64_t t30 = gm_l1;
                        gm_tftb1odc739t5 t31 = gm_f_push_ucgmlo2(t29, t30);
                        gm_l13 = t31;
                        uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t32) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srclv0dcbbfhves, 772, 17);
                        {
                            void * t33 = gm_l0;
                            uint64_t t34 = gm_l1;
                            gm_f_compute_decl_gcf8mq9((void *)&F.l3, (void *)NULL, cx, NULL, t33, t34);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            {
                                void * t35 = gm_l0;
                                gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)t35) + 1808));
                                gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                                uint64_t t38 = gm_l1;
                                gm_tf5srh4mp4b3i t39 = gm_f_index_of_s9trr09(t37, t38);
                                gm_l14 = t39;
                                uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l14))[8] != 0);
                                if (!t40) {
                                    goto gm_lb8;
                                }
                                uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                                gm_l4 = t41;
                                void * t42 = gm_l0;
                                void * t43 = (void *)(((uint8_t *)t42) + 1808);
                                uint64_t t44 = gm_l4;
                                uint64_t t45 = gm_f_remove_om8q0q2(t43, t44);
                                goto gm_lb7;
                            }
gm_lb8:;
                        }
gm_lb7:;
                        {
                            memmove(gm_out, ((uint8_t *)&F.l3), 208);
                            goto gm_fdone;
                        }
                    }
gm_lb6:;
                }
gm_lb5:;
                void * t46 = gm_l0;
                uint64_t t47 = gm_l1;
                gm_b0_1 t48 = gm_f_report_cycle_mauubd4((void *)NULL, cx, NULL, t46, t47);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                memset(&(*(gm_t6qooom8qa2do *)gm_out), 0, sizeof (*(gm_t6qooom8qa2do *)gm_out));
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6qooom8qa2do *)gm_out) + 0), ((uint8_t)11ULL));
                goto gm_fdone;
            }
gm_lb4:;
            {
                uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                uint8_t t50 = (uint8_t)(t49 == ((uint8_t)0ULL));
                if (!t50) {
                    goto gm_lb9;
                }
                goto gm_lb2;
            }
gm_lb9:;
            gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 765, 5);
        }
gm_lb2:;
        {
            void * t51 = gm_l0;
            uint64_t t52 = gm_l1;
            gm_tagrkbqh054h7 t53;
            memset(&t53, 0, sizeof t53);
            GM_ST(uint8_t, ((uint8_t *)&t53 + 0), ((uint8_t)1ULL));
            gm_b0_1 t54 = gm_f_set_state_3j300fi((void *)NULL, cx, NULL, t51, t52, &t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t55 = gm_l0;
        void * t56 = (void *)(((uint8_t *)t55) + 1768);
        uint64_t t57 = gm_l1;
        gm_tftb1odc739t5 t58 = gm_f_push_ucgmlo2(t56, t57);
        gm_l15 = t58;
        uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
        if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srclv0dcbbfhves, 783, 5);
        void * t60 = gm_l0;
        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)t60) + 2656));
        gm_l5 = t61;
        void * t62 = gm_l0;
        GM_ST(uint8_t, (((uint8_t *)t62) + 2656), ((uint8_t)0));
        {
            void * t63 = gm_l0;
            memmove(((uint8_t *)&F.l6), (((uint8_t *)t63) + 1608), 40);
        }
        {
            void * t64 = gm_l0;
            memmove(((uint8_t *)&F.l7), (((uint8_t *)t64) + 1648), 40);
        }
        {
            void * t65 = gm_l0;
            memmove(((uint8_t *)&F.l8), (((uint8_t *)t65) + 1688), 40);
        }
        {
            void * t66 = gm_l0;
            memmove(((uint8_t *)&F.l9), (((uint8_t *)t66) + 1728), 40);
        }
        {
            void * t67 = gm_l0;
            gm_trqfvmjtt4sij t68;
            gm_f_list_mvhhv5u((void *)&t68, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld13_1;
            }
            GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t67) + 1608), t68);
        }
        {
            void * t69 = gm_l0;
            gm_trqfvmjtt4sij t70;
            gm_f_list_ue2t2bm((void *)&t70, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld13_1;
            }
            GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t69) + 1648), t70);
        }
        {
            void * t71 = gm_l0;
            gm_trqfvmjtt4sij t72;
            gm_f_list_1jabh22((void *)&t72, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld13_1;
            }
            GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t71) + 1688), t72);
        }
        {
            void * t73 = gm_l0;
            gm_trqfvmjtt4sij t74;
            gm_f_list_0bj26n7((void *)&t74, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_ld13_1;
            }
            GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t73) + 1728), t74);
        }
        {
            void * t75 = gm_l0;
            uint64_t t76 = gm_l1;
            gm_f_compute_decl_gcf8mq9((void *)&F.l10, (void *)NULL, cx, NULL, t75, t76);
            if (gm_unw.kind) {
                goto gm_ld13_1;
            }
        }
        {
            void * t77 = gm_l0;
            memmove((((uint8_t *)t77) + 1608), ((uint8_t *)&F.l6), 40);
        }
        {
            void * t78 = gm_l0;
            memmove((((uint8_t *)t78) + 1648), ((uint8_t *)&F.l7), 40);
        }
        {
            void * t79 = gm_l0;
            memmove((((uint8_t *)t79) + 1688), ((uint8_t *)&F.l8), 40);
        }
        {
            void * t80 = gm_l0;
            memmove((((uint8_t *)t80) + 1728), ((uint8_t *)&F.l9), 40);
        }
        void * t81 = gm_l0;
        void * t82 = (void *)(((uint8_t *)t81) + 1768);
        gm_tf5srh4mp4b3i t83 = gm_f_pop_j4rpvm5(t82);
        {
            void * t84 = gm_l0;
            void * t85 = GM_LD(void *, (((uint8_t *)t84) + 304));
            uint64_t t86 = GM_LD(uint64_t, ((((uint8_t *)t84) + 304) + 8));
            uint64_t t87 = gm_l1;
            if ((uintptr_t)t87 >= (uintptr_t)(t86)) gm_trap_index((uintptr_t)t87, t86, gm_srclv0dcbbfhves, 805, 8);
            uint8_t t88 = GM_LD(uint8_t, ((((uint8_t *)t85) + (uintptr_t)t87 * 216) + 0));
            uint8_t t89 = (uint8_t)((t88 == ((uint8_t)1ULL)));
            gm_tagrkbqh054h7 t90 = GM_LD(gm_tagrkbqh054h7, (((uint8_t *)t85) + (uintptr_t)t87 * 216));
            gm_tagrkbqh054h7 t91;
            memset(&t91, 0, sizeof t91);
            GM_ST(uint8_t, ((uint8_t *)&t91 + 0), ((uint8_t)1ULL));
            if (t89) {
                void * t92 = gm_l0;
                void * t93 = GM_LD(void *, (((uint8_t *)t92) + 304));
                uint64_t t94 = GM_LD(uint64_t, ((((uint8_t *)t92) + 304) + 8));
                uint64_t t95 = gm_l1;
                if ((uintptr_t)t95 >= (uintptr_t)(t94)) gm_trap_index((uintptr_t)t95, t94, gm_srclv0dcbbfhves, 805, 44);
                gm_t6qooom8qa2do t96 = GM_LD(gm_t6qooom8qa2do, ((uint8_t *)&F.l10));
                gm_tagrkbqh054h7 t97;
                GM_ST(uint8_t, (uint8_t *)&t97 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t97 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t97 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t97 + 0), ((uint8_t)2ULL));
                GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t97 + 8), t96);
                GM_ST(gm_tagrkbqh054h7, (((uint8_t *)t93) + (uintptr_t)t95 * 216), t97);
            }
        }
        {
            {
                {
                    void * t98 = gm_l0;
                    void * t99 = GM_LD(void *, (((uint8_t *)t98) + 304));
                    uint64_t t100 = GM_LD(uint64_t, ((((uint8_t *)t98) + 304) + 8));
                    uint64_t t101 = gm_l1;
                    if ((uintptr_t)t101 >= (uintptr_t)(t100)) gm_trap_index((uintptr_t)t101, t100, gm_srclv0dcbbfhves, 806, 20);
                    uint8_t t102 = GM_LD(uint8_t, ((((uint8_t *)t99) + (uintptr_t)t101 * 216) + 0));
                    uint8_t t103 = (uint8_t)(t102 == ((uint8_t)2ULL));
                    if (!t103) {
                        goto gm_lb11;
                    }
                    memmove(((uint8_t *)&F.l11), ((((uint8_t *)t99) + (uintptr_t)t101 * 216) + 8), 208);
                }
                {
                    memmove(&F.ret, ((uint8_t *)&F.l11), 208);
                    gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
                    goto gm_ld13_1;
                }
            }
gm_lb11:;
        }
gm_lb10:;
        {
            memset(&F.ret, 0, sizeof F.ret);
            GM_ST(uint8_t, ((uint8_t *)&F.ret + 0), ((uint8_t)11ULL));
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
            goto gm_ld13_1;
        }
    }
    {
        void * t104 = gm_l0;
        uint8_t t105 = gm_l5;
        GM_ST(uint8_t, (((uint8_t *)t104) + 2656), t105);
    }
    goto gm_ls106;
gm_ld13_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t107 = gm_l0;
        uint8_t t108 = gm_l5;
        GM_ST(uint8_t, (((uint8_t *)t107) + 2656), t108);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls106:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_resolve_declarations_vrpg1fl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_resolve_declarations_vrpg1fl F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* done */
    uint64_t gm_l2; /* m */
    uint64_t gm_l3; /* n */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 571u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l1;
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, ((uint8_t *)t11));
            uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)t12) + 200) + 8));
            uint8_t t14 = (t10 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                uint64_t t15 = gm_l1;
                gm_l2 = t15;
                uint64_t t16 = gm_l1;
                uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
                if (o18) gm_trap("integer overflow in `+`", gm_srclv0dcbbfhves, 736, 9);
                uint64_t t19 = r17;
                gm_l1 = t19;
                void * t20 = gm_l0;
                void * t21 = GM_LD(void *, ((uint8_t *)t20));
                void * t22 = GM_LD(void *, (((uint8_t *)t21) + 200));
                uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 200) + 8));
                uint64_t t24 = gm_l2;
                if ((uintptr_t)t24 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t24, t23, gm_srclv0dcbbfhves, 737, 12);
                uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t22) + (uintptr_t)t24 * 184) + 112));
                if (t25) {
                    goto gm_lb3;
                }
                void * t26 = gm_l0;
                void * t27 = GM_LD(void *, ((uint8_t *)t26));
                void * t28 = GM_LD(void *, (((uint8_t *)t27) + 200));
                uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)t27) + 200) + 8));
                uint64_t t30 = gm_l2;
                if ((uintptr_t)t30 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t30, t29, gm_srclv0dcbbfhves, 739, 14);
                uint64_t t31 = GM_LD(uint64_t, (((((uint8_t *)t28) + (uintptr_t)t30 * 184) + 16) + 8));
                gm_l3 = t31;
                gm_l5 = ((uint64_t)0ULL);
                uint64_t t32 = gm_l3;
                gm_l4 = t32;
                {
                    for (;;) {
                        uint64_t t33 = gm_l5;
                        uint64_t t34 = gm_l4;
                        uint8_t t35 = (t33 < t34);
                        if (!t35) {
                            goto gm_lb4;
                        }
                        {
                            {
                                {
                                    uint64_t t36 = gm_l5;
                                    gm_l6 = t36;
                                    void * t37 = gm_l0;
                                    void * t38 = gm_l0;
                                    void * t39 = GM_LD(void *, ((uint8_t *)t38));
                                    void * t40 = GM_LD(void *, (((uint8_t *)t39) + 200));
                                    uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + 200) + 8));
                                    uint64_t t42 = gm_l2;
                                    if ((uintptr_t)t42 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t42, t41, gm_srclv0dcbbfhves, 741, 32);
                                    void * t43 = GM_LD(void *, ((((uint8_t *)t40) + (uintptr_t)t42 * 184) + 16));
                                    uint64_t t44 = GM_LD(uint64_t, (((((uint8_t *)t40) + (uintptr_t)t42 * 184) + 16) + 8));
                                    uint64_t t45 = gm_l6;
                                    if ((uintptr_t)t45 >= (uintptr_t)(t44)) gm_trap_index((uintptr_t)t45, t44, gm_srclv0dcbbfhves, 741, 32);
                                    uint64_t t46 = GM_LD(uint64_t, ((((uint8_t *)t43) + (uintptr_t)t45 * 24) + 16));
                                    gm_t6qooom8qa2do t47;
                                    gm_f_resolve_decl_pus042v((void *)&t47, (void *)NULL, cx, NULL, t37, t46);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb7;
                                }
gm_lb8:;
                                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 740, 9);
                            }
gm_lb7:;
                        }
gm_lb6:;
                        uint64_t t48 = gm_l5;
                        uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                        uint64_t t51 = r49;
                        gm_l5 = t51;
                    }
                }
gm_lb4:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t52 = gm_l0;
    gm_b0_1 t53 = gm_f_check_method_candidates_k08rvu6((void *)NULL, cx, NULL, t52);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t54 = gm_l0;
    void * t55 = GM_LD(void *, ((uint8_t *)t54));
    gm_b0_1 t56 = gm_f_check_import_cycles_2u7oqo0((void *)NULL, cx, NULL, t55);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t57 = gm_l0;
    gm_b0_1 t58 = gm_f_check_pub_signatures_plkds6r((void *)NULL, cx, NULL, t57);
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
__attribute__((aligned(4))) void gm_f_sema_r1nlnnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_sema_r1nlnnm F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 5845u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l2;
    {
        void * t2 = gm_l0;
        gm_tpcj7kul6hlib t3;
        gm_f_types_8a61ga3((void *)&t3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t4;
        gm_f_list_eb5ua5r((void *)&t4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t5;
        gm_f_list_dvp9pla((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t6;
        gm_f_list_dvbol2p((void *)&t6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t7;
        gm_f_list_0bg6tcp((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t8;
        gm_f_list_atkrn3m((void *)&t8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t9;
        gm_f_list_m7es0sh((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t10;
        gm_f_list_qv7bq94((void *)&t10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t11;
        gm_f_list_vhqj1o4((void *)&t11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t12;
        gm_f_list_sajcold((void *)&t12, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t13;
        gm_f_list_4kh12ib((void *)&t13, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t14;
        gm_f_list_8r7pelg((void *)&t14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t15;
        gm_f_list_9q64p2g((void *)&t15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t16;
        gm_f_list_tqcq99u((void *)&t16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t17;
        gm_f_list_uun92du((void *)&t17, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t18;
        gm_f_list_epvducg((void *)&t18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t19;
        gm_f_list_go2qg0b((void *)&t19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t20;
        memset(&t20, 0, sizeof t20);
        gm_trqfvmjtt4sij t21;
        gm_f_list_c02589n((void *)&t21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t22;
        gm_f_list_c02589n((void *)&t22, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t23;
        gm_f_list_2343nn9((void *)&t23, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t24;
        gm_f_list_h93u1dq((void *)&t24, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t25;
        gm_f_list_t29l5ca((void *)&t25, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t26;
        gm_f_list_au01ire((void *)&t26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t27;
        gm_f_hash_map_iug9p19((void *)&t27, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t28;
        gm_f_hash_map_iug9p19((void *)&t28, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t29;
        gm_f_list_406coeg((void *)&t29, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t30;
        gm_f_hash_map_2dqtq4c((void *)&t30, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t31;
        gm_f_hash_map_t86lciv((void *)&t31, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t32;
        gm_f_list_go2qg0b((void *)&t32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t33;
        gm_f_list_dql1gah((void *)&t33, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t34;
        gm_f_list_5pborc0((void *)&t34, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t35;
        gm_f_list_mvhhv5u((void *)&t35, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t36;
        gm_f_list_ue2t2bm((void *)&t36, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t37;
        gm_f_list_1jabh22((void *)&t37, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t38;
        gm_f_list_0bj26n7((void *)&t38, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t39;
        gm_f_list_5dhgg4i((void *)&t39, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t40;
        gm_f_list_5dhgg4i((void *)&t40, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t41;
        gm_f_list_go2qg0b((void *)&t41, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t42;
        gm_f_list_ib5k4gb((void *)&t42, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t43;
        gm_f_list_au01ire((void *)&t43, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t44;
        gm_f_list_v3cpbrb((void *)&t44, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t45;
        gm_f_list_je8tl46((void *)&t45, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t9805qtonbsag t46;
        memset(&t46, 0, sizeof t46);
        gm_trqfvmjtt4sij t47;
        gm_f_list_l3jkoh2((void *)&t47, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t48;
        gm_f_hash_map_84luf3l((void *)&t48, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t49;
        gm_f_hash_map_iibum07((void *)&t49, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t50;
        gm_f_list_go2qg0b((void *)&t50, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t51;
        gm_f_list_tn37ahu((void *)&t51, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t52;
        gm_f_list_i6iqfhd((void *)&t52, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t53;
        gm_f_list_0npefvp((void *)&t53, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t54 = GM_LD(uint64_t, ((uint8_t *)gm_cbbjgr0cpr9));
        gm_t4c7ipjj9s3fp t55;
        gm_f_hash_map_0vd239u((void *)&t55, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t56;
        gm_f_list_1teief7((void *)&t56, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t57;
        gm_f_list_kk91efj((void *)&t57, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t58;
        gm_f_list_epvducg((void *)&t58, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t59;
        gm_f_list_7s4om10((void *)&t59, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t60;
        gm_f_list_s83jn2q((void *)&t60, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t61;
        gm_f_list_s83jn2q((void *)&t61, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t62;
        gm_f_hash_map_1so3e0v((void *)&t62, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t63;
        gm_f_hash_map_ve01i1t((void *)&t63, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t64;
        gm_f_hash_map_8lre197((void *)&t64, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfbjaprcetfe7 t65;
        memset(&t65, 0, sizeof t65);
        GM_ST(void *, ((uint8_t *)&t65 + 0), t2);
        GM_ST(gm_tpcj7kul6hlib, ((uint8_t *)&t65 + 8), t3);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 304), t4);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 344), t5);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 384), t6);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 424), t7);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 464), t8);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 504), t9);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 544), t10);
        GM_ST(uint8_t, ((uint8_t *)&t65 + 584), ((uint8_t)0));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 592), t11);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 632), t12);
        GM_ST(uint8_t, ((uint8_t *)&t65 + 672), ((uint8_t)0));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 680), t13);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 720), t14);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 760), t15);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 800), t16);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 840), t17);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 880), t18);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 920), t19);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t65 + 960), t20);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 968), t21);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1008), t22);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1048), t23);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1088), t24);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1128), t25);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1168), t26);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t65 + 1208), t27);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t65 + 1264), t28);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1320), t29);
        GM_ST(uint32_t, ((uint8_t *)&t65 + 1360), ((uint32_t)0ULL));
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t65 + 1368), t30);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t65 + 1424), t31);
        GM_ST(uint8_t, ((uint8_t *)&t65 + 1480), ((uint8_t)0));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1488), t32);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1528), t33);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1568), t34);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1608), t35);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1648), t36);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1688), t37);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1728), t38);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1768), t39);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1808), t40);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1848), t41);
        GM_ST(uint32_t, ((uint8_t *)&t65 + 1888), ((uint32_t)0ULL));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1896), t42);
        GM_ST(uint32_t, ((uint8_t *)&t65 + 1936), ((uint32_t)0ULL));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1944), t43);
        GM_ST(uint8_t, ((uint8_t *)&t65 + 1984), ((uint8_t)0));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 1992), t44);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 2032), t45);
        GM_ST(uint32_t, ((uint8_t *)&t65 + 2072), ((uint32_t)1ULL));
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t65 + 2076), t46);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 2088), t47);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t65 + 2128), t48);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t65 + 2184), t49);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 2240), t50);
        GM_ST(uint32_t, ((uint8_t *)&t65 + 2280), ((uint32_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t65 + 2288), ((void *)0));
        GM_ST(void *, ((uint8_t *)&t65 + 2296), ((void *)0));
        GM_ST(void *, ((uint8_t *)&t65 + 2304), ((void *)0));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 2312), t51);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 2352), t52);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 2392), t53);
        GM_ST(uint64_t, ((uint8_t *)&t65 + 2432), t54);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t65 + 2440), t55);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 2496), t56);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 2536), t57);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 2576), t58);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 2616), t59);
        GM_ST(uint8_t, ((uint8_t *)&t65 + 2656), ((uint8_t)0));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 2664), t60);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t65 + 2704), t61);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t65 + 2744), t62);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t65 + 2800), t63);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t65 + 2856), t64);
        F.l1 = t65;
    }
    {
        void * t66 = (void *)(((uint8_t *)&F.l1) + 1048);
        gm_b16_8 t67;
        GM_ST(void *, (uint8_t *)&t67, (void *)(gm_snenkhfev166s));
        GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(2));
        gm_b16_8 t68 = gm_f_empty_slice_d914i5u();
        gm_b16_8 t69 = gm_f_empty_slice_p6mn4us();
        gm_tammvh1iespi3 t70;
        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t67);
        GM_ST(uint64_t, ((uint8_t *)&t70 + 16), ((uint64_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 24), t68);
        GM_ST(gm_b16_8, ((uint8_t *)&t70 + 40), t69);
        gm_tftb1odc739t5 t71 = gm_f_push_puil3ql(t66, &t70);
        gm_l2 = t71;
    }
    uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srclv0dcbbfhves, 578, 5);
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 2912);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_state_a5bnsjb F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    uint64_t gm_l2; /* m */
    GM_STACK_CHECK(&F, 910u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    uint8_t gm_l4;
    {
        for (;;) {
            void * t12 = gm_l0;
            uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)t12) + 304) + 8));
            uint64_t t14 = gm_l1;
            uint8_t t15 = (t13 <= t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                void * t16 = gm_l0;
                void * t17 = (void *)(((uint8_t *)t16) + 304);
                gm_tagrkbqh054h7 t18;
                memset(&t18, 0, sizeof t18);
                GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)0ULL));
                gm_tftb1odc739t5 t19 = gm_f_push_kpjuj6c(t17, &t18);
                gm_l3 = t19;
                uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
                if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srclv0dcbbfhves, 750, 38);
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        {
            {
                void * t21 = gm_l0;
                void * t22 = GM_LD(void *, (((uint8_t *)t21) + 304));
                uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 304) + 8));
                uint64_t t24 = gm_l1;
                if ((uintptr_t)t24 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t24, t23, gm_srclv0dcbbfhves, 752, 8);
                uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t22) + (uintptr_t)t24 * 216) + 0));
                uint8_t t26 = (uint8_t)((t25 == ((uint8_t)0ULL)));
                if (!t26) {
                    goto gm_lb5;
                }
            }
            void * t27 = gm_l0;
            void * t28 = GM_LD(void *, ((uint8_t *)t27));
            void * t29 = GM_LD(void *, (((uint8_t *)t28) + 240));
            uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t28) + 240) + 8));
            uint64_t t31 = gm_l1;
            if ((uintptr_t)t31 >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)t31, t30, gm_srclv0dcbbfhves, 752, 46);
            {
                {
                    uint8_t t32 = GM_LD(uint8_t, ((((((uint8_t *)t29) + (uintptr_t)t31 * 216) + 40) + 64) + 0));
                    uint8_t t33 = (uint8_t)(t32 == ((uint8_t)4ULL));
                    if (!t33) {
                        goto gm_lb7;
                    }
                    gm_l4 = ((uint8_t)1);
                    goto gm_lb6;
                }
gm_lb7:;
                {
                    gm_l4 = ((uint8_t)0);
                    goto gm_lb6;
                }
gm_lb8:;
                gm_trap("no match arm matched the scrutinee", gm_srclv0dcbbfhves, 752, 46);
            }
gm_lb6:;
            uint8_t t34 = gm_l4;
            if (!t34) {
                goto gm_lb5;
            }
            {
                {
                    void * t35 = gm_l0;
                    void * t36 = GM_LD(void *, ((uint8_t *)t35));
                    void * t37 = GM_LD(void *, (((uint8_t *)t36) + 240));
                    uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)t36) + 240) + 8));
                    uint64_t t39 = gm_l1;
                    if ((uintptr_t)t39 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t39, t38, gm_srclv0dcbbfhves, 753, 26);
                    uint8_t t40 = GM_LD(uint8_t, (((((uint8_t *)t37) + (uintptr_t)t39 * 216) + 200) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)1ULL));
                    if (!t41) {
                        goto gm_lb10;
                    }
                    uint64_t t42 = GM_LD(uint64_t, (((((uint8_t *)t37) + (uintptr_t)t39 * 216) + 200) + 8));
                    gm_l2 = t42;
                    {
                        void * t43 = gm_l0;
                        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 304));
                        uint64_t t45 = GM_LD(uint64_t, ((((uint8_t *)t43) + 304) + 8));
                        uint64_t t46 = gm_l1;
                        if ((uintptr_t)t46 >= (uintptr_t)(t45)) gm_trap_index((uintptr_t)t46, t45, gm_srclv0dcbbfhves, 753, 54);
                        uint64_t t47 = gm_l2;
                        gm_t6qooom8qa2do t48;
                        memset(&t48, 0, sizeof t48);
                        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)9ULL));
                        GM_ST(uint64_t, ((uint8_t *)&t48 + 8), t47);
                        gm_tagrkbqh054h7 t49;
                        GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t49 + 8), t48);
                        GM_ST(gm_tagrkbqh054h7, (((uint8_t *)t44) + (uintptr_t)t46 * 216), t49);
                    }
                    goto gm_lb9;
                }
gm_lb10:;
            }
gm_lb9:;
            goto gm_lb4;
        }
gm_lb5:;
    }
gm_lb4:;
    void * t50 = gm_l0;
    void * t51 = GM_LD(void *, (((uint8_t *)t50) + 304));
    uint64_t t52 = GM_LD(uint64_t, ((((uint8_t *)t50) + 304) + 8));
    uint64_t t53 = gm_l1;
    if ((uintptr_t)t53 >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)t53, t52, gm_srclv0dcbbfhves, 755, 12);
    memmove(gm_out, (((uint8_t *)t51) + (uintptr_t)t53 * 216), 216);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_unsupported_t7lgvp9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_unsupported_t7lgvp9 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK(&F, 177u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l3;
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)gm_c3t36dlh4g5));
        gm_b16_8 t5 = gm_l2;
        gm_t6j6smj7qoctu t6;
        GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t4);
        GM_ST(gm_b16_8, ((uint8_t *)&t6 + 16), t5);
        gm_f_format_e79cvtl((void *)&gm_l3, (void *)NULL, cx, NULL, &t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srclv0dcbbfhves, 643, 19);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_b0_1 t10 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t2, t3, t9);
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
extern __attribute__((aligned(4))) void gm_f_walk_v858co7(void * a0, gm_csguankko4lmhd a1, gm_closure a2);

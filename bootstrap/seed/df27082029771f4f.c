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
typedef struct { _Alignas(8) uint8_t b[0]; } gm_b0_8;
typedef struct { _Alignas(8) uint8_t b[16]; } gm_b16_8;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint8_t f1; uint8_t f2; uint8_t pe[6]; } gm_t553jgmqd1tlr;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { gm_b16_8 a[2]; } gm_t0kl141g5m2qf;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
_Static_assert(sizeof(gm_t553jgmqd1tlr) == 16 && _Alignof(gm_t553jgmqd1tlr) == 8 && offsetof(gm_t553jgmqd1tlr, f0) == 0 && offsetof(gm_t553jgmqd1tlr, f1) == 8 && offsetof(gm_t553jgmqd1tlr, f2) == 9, "layout of `Policy`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; } gm_th2d9c7aju8hp;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 a[10]; } gm_thr4tus9rimig;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t4ce0jd3tdro4 v0; } f1; } gm_ttiiv5vhiuvvr;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tu1089hhijadf;
_Static_assert(sizeof(gm_t0kl141g5m2qf) == 32 && _Alignof(gm_t0kl141g5m2qf) == 8 && offsetof(gm_t0kl141g5m2qf, a) == 0, "layout of `[2][]const string`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_trqfvmjtt4sij v1; } f1; } gm_t470aqkia5212;
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t3ujhu6qph7de v0; } f1; } gm_t715ajl6o1ipo;
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_trqfvmjtt4sij f4; } gm_t9j922kfqcdi9;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tibupt4dmcqai v1; } f1; } gm_te2n4j2vtm53d;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; uint8_t f2; uint8_t pe[7]; } gm_tecsjmmgeeh52;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2d9c7aju8hp) == 112 && _Alignof(gm_th2d9c7aju8hp) == 8 && offsetof(gm_th2d9c7aju8hp, f0) == 0 && offsetof(gm_th2d9c7aju8hp, f1) == 16 && offsetof(gm_th2d9c7aju8hp, f2) == 32 && offsetof(gm_th2d9c7aju8hp, f3) == 48 && offsetof(gm_th2d9c7aju8hp, f4) == 64 && offsetof(gm_th2d9c7aju8hp, f5) == 80 && offsetof(gm_th2d9c7aju8hp, f6) == 96, "layout of `(string, string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thr4tus9rimig) == 160 && _Alignof(gm_thr4tus9rimig) == 8 && offsetof(gm_thr4tus9rimig, a) == 0, "layout of `[10]string`");
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; } gm_tl7hpf5nlilqq;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tu1089hhijadf f0; uint8_t f1; uint8_t pe[7]; } gm_tom1ad5bcqgi1;
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; } gm_tr8dvlu9ik4gj;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
_Static_assert(sizeof(gm_ttiiv5vhiuvvr) == 16 && _Alignof(gm_ttiiv5vhiuvvr) == 8 && offsetof(gm_ttiiv5vhiuvvr, f0) == 0 && offsetof(gm_ttiiv5vhiuvvr, f1) == 8, "layout of `ParseError`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tu1089hhijadf) == 56 && _Alignof(gm_tu1089hhijadf) == 8 && offsetof(gm_tu1089hhijadf, f0) == 0 && offsetof(gm_tu1089hhijadf, f1) == 8 && offsetof(gm_tu1089hhijadf, f2) == 16 && offsetof(gm_tu1089hhijadf, f3) == 24 && offsetof(gm_tu1089hhijadf, f4) == 40, "layout of `Version`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttiiv5vhiuvvr v0; uint64_t v1; } f1; } gm_tutnubt6u4hoq;
typedef struct { gm_b16_8 f0; gm_ttu09lij36f8c f1; gm_th2lqbo77pvsc f2; } gm_t03ccdabio1fj;
_Static_assert(sizeof(gm_t470aqkia5212) == 48 && _Alignof(gm_t470aqkia5212) == 8 && offsetof(gm_t470aqkia5212, f0) == 0 && offsetof(gm_t470aqkia5212, f1) == 8, "layout of `string!List(string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tr8dvlu9ik4gj v1; } f1; } gm_t5detta1b4gnk;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tin97tt4ud0s1 v1; } f1; } gm_t62efos6ssciq;
_Static_assert(sizeof(gm_t715ajl6o1ipo) == 32 && _Alignof(gm_t715ajl6o1ipo) == 8 && offsetof(gm_t715ajl6o1ipo, f0) == 0 && offsetof(gm_t715ajl6o1ipo, f1) == 8, "layout of `Taken`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t3ujhu6qph7de v0; gm_tr8dvlu9ik4gj v1; } f1; } gm_t77ebp3ogdble;
_Static_assert(sizeof(gm_t9j922kfqcdi9) == 72 && _Alignof(gm_t9j922kfqcdi9) == 8 && offsetof(gm_t9j922kfqcdi9, f0) == 0 && offsetof(gm_t9j922kfqcdi9, f1) == 8 && offsetof(gm_t9j922kfqcdi9, f2) == 16 && offsetof(gm_t9j922kfqcdi9, f3) == 24 && offsetof(gm_t9j922kfqcdi9, f4) == 32, "layout of `CleanReport`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tecsjmmgeeh52 v1; } f1; } gm_tcl27d4j9lskg;
_Static_assert(sizeof(gm_te2n4j2vtm53d) == 32 && _Alignof(gm_te2n4j2vtm53d) == 8 && offsetof(gm_te2n4j2vtm53d, f0) == 0 && offsetof(gm_te2n4j2vtm53d, f1) == 8, "layout of `IoError!Entry`");
_Static_assert(sizeof(gm_tecsjmmgeeh52) == 88 && _Alignof(gm_tecsjmmgeeh52) == 8 && offsetof(gm_tecsjmmgeeh52, f0) == 0 && offsetof(gm_tecsjmmgeeh52, f1) == 40 && offsetof(gm_tecsjmmgeeh52, f2) == 80, "layout of `ScaffoldReport`");
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t3ujhu6qph7de v0; gm_tom1ad5bcqgi1 v1; } f1; } gm_tis0irsd01rfp;
_Static_assert(sizeof(gm_tl7hpf5nlilqq) == 56 && _Alignof(gm_tl7hpf5nlilqq) == 8 && offsetof(gm_tl7hpf5nlilqq, f0) == 0 && offsetof(gm_tl7hpf5nlilqq, f1) == 16, "layout of `Fix`");
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_trqfvmjtt4sij f2; gm_ti0g3bmheuq9o f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; } gm_tm43kqe3e8b4a;
_Static_assert(sizeof(gm_tom1ad5bcqgi1) == 64 && _Alignof(gm_tom1ad5bcqgi1) == 8 && offsetof(gm_tom1ad5bcqgi1, f0) == 0 && offsetof(gm_tom1ad5bcqgi1, f1) == 56, "layout of `?Version`");
_Static_assert(sizeof(gm_tr8dvlu9ik4gj) == 120 && _Alignof(gm_tr8dvlu9ik4gj) == 8 && offsetof(gm_tr8dvlu9ik4gj, f0) == 0 && offsetof(gm_tr8dvlu9ik4gj, f1) == 40 && offsetof(gm_tr8dvlu9ik4gj, f2) == 80, "layout of `Extras`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
_Static_assert(sizeof(gm_tutnubt6u4hoq) == 24 && _Alignof(gm_tutnubt6u4hoq) == 8 && offsetof(gm_tutnubt6u4hoq, f0) == 0 && offsetof(gm_tutnubt6u4hoq, f1) == 8, "layout of `ParseError!usize`");
typedef struct { gm_tin97tt4ud0s1 f0; uint8_t f1; uint8_t pe[7]; } gm_tv37d3dpt2opu;
_Static_assert(sizeof(gm_t03ccdabio1fj) == 80 && _Alignof(gm_t03ccdabio1fj) == 8 && offsetof(gm_t03ccdabio1fj, f0) == 0 && offsetof(gm_t03ccdabio1fj, f1) == 16 && offsetof(gm_t03ccdabio1fj, f2) == 56, "layout of `(url: string, reference: ?(key: string, value: string), subdir: ?string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tv37d3dpt2opu v1; } f1; } gm_t074vrl1hb4f6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t03ccdabio1fj v1; gm_tt008mgut8glb v2; } f1; } gm_t2jeshnsn30ep;
_Static_assert(sizeof(gm_t5detta1b4gnk) == 128 && _Alignof(gm_t5detta1b4gnk) == 8 && offsetof(gm_t5detta1b4gnk, f0) == 0 && offsetof(gm_t5detta1b4gnk, f1) == 8, "layout of `string!Extras`");
_Static_assert(sizeof(gm_t62efos6ssciq) == 48 && _Alignof(gm_t62efos6ssciq) == 8 && offsetof(gm_t62efos6ssciq, f0) == 0 && offsetof(gm_t62efos6ssciq, f1) == 8, "layout of `string!VersionReq`");
_Static_assert(sizeof(gm_t77ebp3ogdble) == 128 && _Alignof(gm_t77ebp3ogdble) == 8 && offsetof(gm_t77ebp3ogdble, f0) == 0 && offsetof(gm_t77ebp3ogdble, f1) == 8, "layout of `Failure!Parsed`");
_Static_assert(sizeof(gm_tcl27d4j9lskg) == 96 && _Alignof(gm_tcl27d4j9lskg) == 8 && offsetof(gm_tcl27d4j9lskg, f0) == 0 && offsetof(gm_tcl27d4j9lskg, f1) == 8, "layout of `string!ScaffoldReport`");
_Static_assert(sizeof(gm_tis0irsd01rfp) == 72 && _Alignof(gm_tis0irsd01rfp) == 8 && offsetof(gm_tis0irsd01rfp, f0) == 0 && offsetof(gm_tis0irsd01rfp, f1) == 8, "layout of `Failure!?Version`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tjgmtdrk88t7m;
_Static_assert(sizeof(gm_tm43kqe3e8b4a) == 320 && _Alignof(gm_tm43kqe3e8b4a) == 8 && offsetof(gm_tm43kqe3e8b4a, f0) == 0 && offsetof(gm_tm43kqe3e8b4a, f1) == 24 && offsetof(gm_tm43kqe3e8b4a, f2) == 48 && offsetof(gm_tm43kqe3e8b4a, f3) == 88 && offsetof(gm_tm43kqe3e8b4a, f4) == 240 && offsetof(gm_tm43kqe3e8b4a, f5) == 280, "layout of `Manifest`");
_Static_assert(sizeof(gm_tv37d3dpt2opu) == 48 && _Alignof(gm_tv37d3dpt2opu) == 8 && offsetof(gm_tv37d3dpt2opu, f0) == 0 && offsetof(gm_tv37d3dpt2opu, f1) == 40, "layout of `?Resolution`");
_Static_assert(sizeof(gm_t074vrl1hb4f6) == 56 && _Alignof(gm_t074vrl1hb4f6) == 8 && offsetof(gm_t074vrl1hb4f6, f0) == 0 && offsetof(gm_t074vrl1hb4f6, f1) == 8, "layout of `string!?Resolution`");
_Static_assert(sizeof(gm_t2jeshnsn30ep) == 88 && _Alignof(gm_t2jeshnsn30ep) == 8 && offsetof(gm_t2jeshnsn30ep, f0) == 0 && offsetof(gm_t2jeshnsn30ep, f1) == 8, "layout of `NewDep`");
_Static_assert(sizeof(gm_tjgmtdrk88t7m) == 328 && _Alignof(gm_tjgmtdrk88t7m) == 8 && offsetof(gm_tjgmtdrk88t7m, f0) == 0 && offsetof(gm_tjgmtdrk88t7m, f1) == 8, "layout of `ManifestError!Manifest`");
struct gm_fr_add_04lv2cc {
    gm_frame h;
    gm_thr4tus9rimig l1; /* keys */
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_trqfvmjtt4sij l8; /* refs */
    gm_t0uuhaljsf410 l9; /* <tmp> */
    gm_tdmi4fk9nhqa7 l10; /* <tmp> */
    gm_tfeab279c16rr l15; /* <tmp> */
    gm_t9bmh80edou9c l16; /* <tmp> */
    gm_t2jeshnsn30ep l20; /* dep */
    gm_ttu09lij36f8c l26; /* reference */
    gm_tu1089hhijadf l30; /* v */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_cache_command_7uqmos8 {
    gm_frame h;
    gm_tr8dvlu9ik4gj l9; /* p */
    gm_t9j922kfqcdi9 l15; /* report */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_canonicalize_epo7cln {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_contains_a7cccd4 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_pe7uj7v {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fail_o80k3rv {
    gm_frame h;
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_fetch_l6q1k6p {
    gm_frame h;
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_trqfvmjtt4sij l4; /* given */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_for_body_0m0ntf2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_14t1s38 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_27unvt9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5uun4vl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6dqa07h {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6ta43n5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_7lopc28 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_da7bimt {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dhjqjjt {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e2683lk {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fbigqms {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_gl2aoqn {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_is5aam9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j0ggm04 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n537vp2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n6eprd4 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_pah1p62 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tffhbfn {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_udqe6i2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ujusd29 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_v71csbq {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_flag_4n9tu40 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_io_message_p7jbrea {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_is_dir_2kkkskr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_dir_6sd3mnh {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_package_command_ufjt5rp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_join_sb1m5es {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g9fqnp2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_new_package_hf828qc {
    gm_frame h;
    gm_tr8dvlu9ik4gj l4; /* p */
    gm_b16_8 l5; /* dir */
    uint8_t l11; /* non_empty */
    uint8_t l12; /* missing */
    gm_tecsjmmgeeh52 l22; /* report */
    gm_t715ajl6o1ipo ret;
    gm_b0_1 w0;
};
struct gm_fr_newest_version_dsisujv {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* tags */
    gm_tom1ad5bcqgi1 l3; /* best */
    gm_tu1089hhijadf l7; /* v */
    gm_tu1089hhijadf l8; /* b */
    gm_tis0irsd01rfp ret;
};
struct gm_fr_parse_args_7blm2mp {
    gm_frame h;
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_t5detta1b4gnk ret;
};
struct gm_fr_push_2tc790g {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8bt91lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_relative_path_3klciqu {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* f */
    gm_trqfvmjtt4sij l3; /* t */
    gm_trqfvmjtt4sij l5; /* parts */
    gm_b16_8 ret;
};
struct gm_fr_remove_bar6kga {
    gm_frame h;
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_root_of_cluda11 {
    gm_frame h;
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_run_command_s4fhqaq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_update_oi0ok0i {
    gm_frame h;
    gm_tr8dvlu9ik4gj l3; /* p */
    gm_t715ajl6o1ipo ret;
};
struct gm_fr_usage_failure_cgg6v1t {
    gm_frame h;
    gm_t5detta1b4gnk l0; /* r */
    gm_t77ebp3ogdble ret;
};
struct gm_fr_value_of_0km7vvp {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_add_04lv2cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_add_dependency_jmjffrp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_cache_command_7uqmos8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_cache_dir_81h90qu(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_canonicalize_n3e3qri(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_clean_97pd952(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t553jgmqd1tlr a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_version_0oibvro(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_contains_a7cccd4(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pe7uj7v(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_suo216n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_fail_o80k3rv(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t3ujhu6qph7de gm_f_fail_o80k3rv_c(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_fetch_l6q1k6p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_file_name_em7v8lv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_find_root_kjnm6hf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0m0ntf2(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_14t1s38(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_27unvt9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5uun4vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_6dqa07h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_6ta43n5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_7lopc28(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9u9ohk5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_b73b468(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_da7bimt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_dhjqjjt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_e2683lk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_fbigqms(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_gl2aoqn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_is5aam9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_j0ggm04(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_n537vp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_n6eprd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_pah1p62(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_tffhbfn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_udqe6i2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ujusd29(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_v71csbq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) uint8_t gm_f_has_flag_4n9tu40(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_package_command_ufjt5rp(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g9fqnp2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_make_dir_all_q6phjan(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_name_problem_6r4mp11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_new_package_hf828qc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_newest_version_dsisujv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
__attribute__((aligned(4))) void gm_f_parse_args_7blm2mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_parse_int_8aueeu2(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_version_req_alipvfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2tc790g(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_read_text_g0qbj1t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8bt91lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_relative_path_3klciqu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_remote_tags_rssa7s7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_remove_bar6kga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_remove_dependency_4ju7b0h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_root_of_cluda11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_run_command_s4fhqaq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_sanitize_name_3i9pjsp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_scaffold_ansfc6m(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_split_cbk4ks8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_split_once_mqcu1ce(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_summary_s3h16om(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1);
__attribute__((aligned(4))) void gm_f_sync_ht164ep(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_tqs5p4n2tq19v a1, const void * a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_tag_version_aloeltn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_ascii_c9j36d1(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_update_oi0ok0i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_url_problem_38kdh3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_usage_failure_cgg6v1t(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_t77ebp3ogdble gm_f_usage_failure_cgg6v1t_c(gm_t5detta1b4gnk a0);
__attribute__((aligned(4))) void gm_f_value_of_0km7vvp(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_value_of_0km7vvp_c(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_version_string_k02v4te(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_write_atomic_v74ncu3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
GM_NOINLINE bool gm_pf1m4b8q4e78(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf454qfrpt6r(gm_node *cx, const void *d, uintptr_t n, const char *file, uint32_t line, uint32_t col);
GM_NOINLINE bool gm_pf70j0q3sgt1(gm_node *cx, const gm_type *d, const void *v, const char *file, uint32_t line, uint32_t col);
extern uint8_t gm_cftc9ncjfm7[16] __attribute__((aligned(8)));
extern uint8_t gm_crqb3tjd618[16] __attribute__((aligned(8)));
extern void *const gm_hl87701o8eo1[];
extern const uint8_t gm_s0bmhg7t703ts[22];
extern const uint8_t gm_s0jho24omhfui[7];
extern const uint8_t gm_s0k6ioi39f878[4];
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s0ri8hi6r25m8[37];
extern const uint8_t gm_s2n0v309tod60[70];
extern const uint8_t gm_s2n3d0ogcjggg[14];
extern const uint8_t gm_s30cnsbbn0c4o[4];
extern const uint8_t gm_s3lbmpmt95hvh[7];
extern const uint8_t gm_s3v0kr2fdpfld[5];
extern const uint8_t gm_s420tm9ua7ggg[4];
extern const uint8_t gm_s46khuklhbvqd[21];
extern const uint8_t gm_s4o70d89183c2[4];
extern const uint8_t gm_s4u8fr1tm8vl7[40];
extern const uint8_t gm_s4uu8ali3icr8[40];
extern const uint8_t gm_s5fgj0n2t69gl[8];
extern const uint8_t gm_s5i2d5hm88htd[4];
extern const uint8_t gm_s6pnfrub0rj1t[6];
extern const uint8_t gm_s77fggjckb5ln[41];
extern const uint8_t gm_s7ch91qn11t34[25];
extern const uint8_t gm_s7hkl5mqa15gm[5];
extern const uint8_t gm_s7mqv7hf4fso0[27];
extern const uint8_t gm_s7spn02nsqs4u[18];
extern const uint8_t gm_s8dbdqj1sqibj[3];
extern const uint8_t gm_s8f9rv6hrr024[11];
extern const uint8_t gm_s9m6kd6gec368[14];
extern const uint8_t gm_s9mhd8h7bolj3[48];
extern const uint8_t gm_s9pknf9g05okt[3];
extern const uint8_t gm_sa29cuhpvu442[54];
extern const uint8_t gm_sa2h9vonan0ma[4];
extern const uint8_t gm_saicih5tjo4fl[43];
extern const uint8_t gm_sankagtb3ecgn[58];
extern const uint8_t gm_saq7q763oq882[48];
extern const uint8_t gm_saqbkgqthogiq[30];
extern const uint8_t gm_sb9fu6ufbt9g1[17];
extern const uint8_t gm_sbcpd47bv0ru9[63];
extern const uint8_t gm_sbg03hl2obk72[10];
extern const uint8_t gm_sbvsnnbg6vc88[23];
extern const uint8_t gm_sd1smnnii4cig[7];
extern const uint8_t gm_se24c7hpp401n[11];
extern const uint8_t gm_se39cci44d9rt[35];
extern const uint8_t gm_se48mcej0nimj[16];
extern const uint8_t gm_seo2gq6ouvlf6[59];
extern const uint8_t gm_sf9ilivr4c86m[4];
extern const uint8_t gm_sfvspsnv23gbt[7];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgc7crh43q9tj[6];
extern const uint8_t gm_sgu3j1te831tl[24];
extern const uint8_t gm_sh0j2ggs1vvoa[2];
extern const uint8_t gm_sius9pki5u4e3[5];
extern const uint8_t gm_sivh46n0690a4[4];
extern const uint8_t gm_sj56dkk1peu3m[54];
extern const uint8_t gm_sjgng334veu6f[8];
extern const uint8_t gm_ske39klqfe71k[82];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_skq2b15b87en9[63];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sla3alt5340mf[8];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_slrtg04n254p4[19];
extern const uint8_t gm_slskjtl6alfrd[6];
extern const uint8_t gm_smimi08sfrg0n[5];
extern const uint8_t gm_smsc5gshqt6cd[20];
extern const uint8_t gm_snboa22b96bbc[4];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snl38trih7qf5[19];
extern const uint8_t gm_snmm68ee1rq48[41];
extern const uint8_t gm_snqvo5bgc2fch[8];
extern const uint8_t gm_snvisme5hp379[7];
extern const uint8_t gm_so3hs9kj1bue6[21];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so6e0kl9gkq21[46];
extern const uint8_t gm_so87vf8a6o4mm[11];
extern const uint8_t gm_so9n4mmd960c7[6];
extern const uint8_t gm_soohe0i8iafj3[20];
extern const uint8_t gm_spb7dvkqr71v2[77];
extern const uint8_t gm_sq99f4u8gmi6n[28];
extern const uint8_t gm_sqljkj8r253om[7];
extern const uint8_t gm_sr43s52rmecnf[33];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr88r273kmptc[21];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srccr53ejqr1dde[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srgao1ir81l50[14];
extern const uint8_t gm_ss9d51lhe1orq[46];
extern const uint8_t gm_sshhi6fn3vfaa[45];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_suass5sid4b6e[5];
extern const uint8_t gm_sueadt05fevtm[18];
extern const uint8_t gm_suq3bgh3anb87[58];
extern const uint8_t gm_svdospumf3o67[14];
extern const uint8_t gm_svsegvhrrmf7h[18];
extern const gm_tmpl gm_tpl1dleoebfmvhp;
extern const gm_tmpl gm_tpl1lvejpvpq0qh;
extern const gm_tmpl gm_tpl4fm7lnhj31fn;
extern const gm_tmpl gm_tpl75h0vbo3vani;
extern const gm_tmpl gm_tpl8nqn0hncnfd4;
extern const gm_tmpl gm_tpl94ei7rae9si2;
extern const gm_tmpl gm_tpla0a5a633lqve;
extern const gm_tmpl gm_tplbmf4neusqbo5;
extern const gm_tmpl gm_tplc0d2186db2jt;
extern const gm_tmpl gm_tpligbj3kvvunpr;
extern const gm_tmpl gm_tpliqpvgtpo05sn;
extern const gm_tmpl gm_tplj6ss1j7sgouf;
extern const gm_tmpl gm_tpljq1be78i0ptp;
extern const gm_tmpl gm_tplkucr7969socr;
extern const gm_tmpl gm_tpll3bcqsckmobi;
extern const gm_tmpl gm_tplnrs6u0h2oc92;
extern const gm_tmpl gm_tplqipe054or34v;
extern const gm_tmpl gm_tpls4bcac20qnq6;
extern const gm_tmpl gm_tplsc9pqkp31v7e;
extern const gm_tmpl gm_tplu3lqf0bugia3;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline __attribute__((aligned(4))) void gm_f_canonicalize_epo7cln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_canonicalize_epo7cln F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_f_canonicalize_n3e3qri((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_pe7uj7v(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_pe7uj7v F;
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
inline __attribute__((aligned(4))) void gm_f_fail_o80k3rv(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fail_o80k3rv F;
    gm_b16_8 gm_l0; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 0), ((uint8_t)1ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t2);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_14t1s38(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_14t1s38 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa29cuhpvu442));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplu3lqf0bugia3), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_27unvt9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_27unvt9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7spn02nsqs4u));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(17));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplc0d2186db2jt), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5uun4vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5uun4vl F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s46khuklhbvqd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(20));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1lvejpvpq0qh), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_6dqa07h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6dqa07h F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0bmhg7t703ts));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsc9pqkp31v7e), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_6ta43n5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_6ta43n5 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sj56dkk1peu3m));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(53));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpla0a5a633lqve), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_7lopc28(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_7lopc28 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s77fggjckb5ln));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl94ei7rae9si2), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_da7bimt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_da7bimt F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9mhd8h7bolj3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkucr7969socr), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_dhjqjjt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dhjqjjt F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5i2d5hm88htd));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpligbj3kvvunpr), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_e2683lk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_e2683lk F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4u8fr1tm8vl7));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(39));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpls4bcac20qnq6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fbigqms(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fbigqms F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snl38trih7qf5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpll3bcqsckmobi), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_gl2aoqn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_gl2aoqn F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so3hs9kj1bue6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnrs6u0h2oc92), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_is5aam9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_is5aam9 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svsegvhrrmf7h));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(17));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl8nqn0hncnfd4), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_j0ggm04(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_j0ggm04 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_soohe0i8iafj3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplqipe054or34v), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_n537vp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_n537vp2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr88r273kmptc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4fm7lnhj31fn), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_n6eprd4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_n6eprd4 F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sbg03hl2obk72));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(9));
    gm_tq3m5qgvkpje8 t3 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
    gm_f_format_b73b468((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplbmf4neusqbo5), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_pah1p62(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_pah1p62 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0ri8hi6r25m8));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(36));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl1dleoebfmvhp), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_tffhbfn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tffhbfn F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_saq7q763oq882));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl75h0vbo3vani), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_udqe6i2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_udqe6i2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skq2b15b87en9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplj6ss1j7sgouf), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ujusd29(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ujusd29 F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_ske39klqfe71k));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(81));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_9u9ohk5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpliqpvgtpo05sn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_v71csbq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_v71csbq F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spb7dvkqr71v2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(76));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpljq1be78i0ptp), t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0) {
    struct gm_fr_io_message_p7jbrea F;
    gm_toe5fmr3vvf4d gm_l0; /* e */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_toe5fmr3vvf4d t2 = gm_l0;
    gm_b16_8 t3 = gm_f_os_error_text_319eh6h((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_dir_2kkkskr F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 35u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    uint8_t t3 = gm_f_is_dir_6sd3mnh((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_dir_6sd3mnh F;
    gm_b16_8 gm_l0; /* p */
    gm_t5j14m8ru132p gm_l1; /* info */
    GM_STACK_CHECK(&F, 151u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tih6h4avv5nrb gm_l2;
    gm_b16_8 t2 = gm_l0;
    gm_node *pn3 = gm_find(cx, 2126215853);
    if (!pn3) gm_trap("no handler installed for effect `Fs`", gm_srcc2qutubarto1, 229, 13);
    gm_tih6h4avv5nrb t4;
    ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn3->clauses[5])((void *)&t4, pn3->env, pn3->hctx, pn3, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t4;
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (t5) {
        memmove(((uint8_t *)&gm_l1), (((uint8_t *)&gm_l2) + 8), 32);
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_t9qkh0rsooc8d t6 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 8));
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    uint8_t t8 = (uint8_t)((t7 == ((uint8_t)1ULL)));
    gm_t9qkh0rsooc8d t9;
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)1ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_join_sb1m5es F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 112u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_join_qveh5b2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_g9fqnp2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g9fqnp2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_pe7uj7v();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2tc790g(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_2tc790g F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_pe7uj7v();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8bt91lk(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8bt91lk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8bt91lk F;
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
inline __attribute__((aligned(4))) void gm_f_usage_failure_cgg6v1t(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_usage_failure_cgg6v1t F;
    gm_b16_8 gm_l1; /* e */
    GM_STACK_CHECK_SHALLOW(&F, 321u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t77ebp3ogdble gm_l2;
    uint8_t t2 = (uint8_t)!((((uint8_t *)a0))[0] != 0);
    if (t2) {
        gm_tr8dvlu9ik4gj t3 = GM_LD(gm_tr8dvlu9ik4gj, (((uint8_t *)a0) + 8));
        GM_ST(uint8_t, (uint8_t *)&gm_l2 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l2 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l2 + 4, 0);
        ((uint8_t *)&gm_l2)[0] = 0;
        GM_ST(gm_tr8dvlu9ik4gj, ((uint8_t *)&gm_l2 + 8), t3);
    } else {
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_l1 = t4;
        {
            gm_b16_8 t5 = gm_l1;
            gm_t3ujhu6qph7de t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t6 + 8), t5);
            memset(&gm_l2, 0, sizeof gm_l2);
            ((uint8_t *)&gm_l2)[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&gm_l2 + 8), t6);
        }
    }
    memmove(gm_out, ((uint8_t *)&gm_l2), 128);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_add_04lv2cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_add_04lv2cc F;
    gm_b16_8 gm_l0; /* args */
    gm_b0_8 gm_l2; /* <temporary array> */
    gm_b16_8 gm_l4; /* name */
    gm_b16_8 gm_l5; /* problem */
    gm_b16_8 gm_l6; /* e */
    gm_b16_8 gm_l7; /* root */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l13; /* k */
    uint64_t gm_l14; /* sources */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_b16_8 gm_l19; /* k */
    gm_b16_8 gm_l21; /* url */
    gm_b16_8 gm_l22; /* path */
    gm_b16_8 gm_l23; /* abs */
    gm_b16_8 gm_l24; /* url */
    gm_b16_8 gm_l25; /* problem */
    gm_b16_8 gm_l27; /* k */
    gm_b16_8 gm_l28; /* v */
    gm_b16_8 gm_l29; /* e */
    gm_b16_8 gm_l31; /* manifest_path */
    gm_b16_8 gm_l32; /* e */
    gm_b16_8 gm_l33; /* old_text */
    gm_tf1bjg81u0glv gm_l34; /* e */
    gm_b16_8 gm_l35; /* e */
    gm_b16_8 gm_l36; /* new_text */
    gm_tf1bjg81u0glv gm_l37; /* e */
    gm_b16_8 gm_l38; /* e */
    gm_b16_8 gm_l39; /* e */
    gm_b16_8 gm_l40; /* e2 */
    GM_STACK_CHECK(&F, 7586u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t77ebp3ogdble gm_l41;
    gm_th2lqbo77pvsc gm_l42;
    gm_b16_8 gm_l43;
    gm_th2lqbo77pvsc gm_l44;
    gm_t3ujhu6qph7de gm_l45;
    uint8_t gm_l46;
    gm_th2lqbo77pvsc gm_l47;
    gm_tftb1odc739t5 gm_l48;
    uint8_t gm_l49;
    gm_th2lqbo77pvsc gm_l50;
    uint8_t gm_l51;
    gm_th2lqbo77pvsc gm_l52;
    uint8_t gm_l53;
    gm_th2lqbo77pvsc gm_l54;
    gm_th2lqbo77pvsc gm_l55;
    gm_th2lqbo77pvsc gm_l56;
    uint8_t gm_l57;
    uint8_t gm_l58;
    gm_th2lqbo77pvsc gm_l59;
    gm_th2lqbo77pvsc gm_l60;
    gm_tabb710d9mq2e gm_l61;
    gm_th2lqbo77pvsc gm_l62;
    gm_th2lqbo77pvsc gm_l63;
    gm_th2lqbo77pvsc gm_l64;
    gm_tin97tt4ud0s1 gm_l65;
    gm_t62efos6ssciq gm_l66;
    gm_tabb710d9mq2e gm_l67;
    gm_tis0irsd01rfp gm_l68;
    gm_tom1ad5bcqgi1 gm_l69;
    gm_tabb710d9mq2e gm_l70;
    gm_t3ujhu6qph7de gm_l71;
    gm_tm43kqe3e8b4a gm_l72;
    gm_tjgmtdrk88t7m gm_l73;
    gm_tabb710d9mq2e gm_l74;
    gm_t3ujhu6qph7de gm_l75;
    gm_tm43kqe3e8b4a gm_l76;
    gm_tjgmtdrk88t7m gm_l77;
    gm_tabb710d9mq2e gm_l78;
    gm_tl572d7579hns gm_l79;
    gm_tv37d3dpt2opu gm_l80;
    gm_t074vrl1hb4f6 gm_l81;
    gm_tl572d7579hns gm_l82;
    {
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_smimi08sfrg0n));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(4));
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_s0k6ioi39f878));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(3));
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sa2h9vonan0ma));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(3));
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sd1smnnii4cig));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(6));
        gm_b16_8 t45;
        GM_ST(void *, (uint8_t *)&t45, (void *)(gm_s420tm9ua7ggg));
        GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(3));
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s0jho24omhfui));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(6));
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sf9ilivr4c86m));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(3));
        gm_b16_8 t48;
        GM_ST(void *, (uint8_t *)&t48, (void *)(gm_sla3alt5340mf));
        GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(7));
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sfvspsnv23gbt));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(6));
        gm_b16_8 t50;
        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_snboa22b96bbc));
        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(3));
        gm_thr4tus9rimig t51;
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 0), t41);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 16), t42);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 32), t43);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 48), t44);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 64), t45);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 80), t46);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 96), t47);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 112), t48);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 128), t49);
        GM_ST(gm_b16_8, ((uint8_t *)&t51 + 144), t50);
        F.l1 = t51;
        gm_b16_8 t52 = gm_l0;
        void * t53 = (void *)((uint8_t *)&F.l1);
        uint64_t t54 = ((uintptr_t)((uint64_t)10ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(((uint8_t *)t53 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(t54));
        gm_b16_8 t56 = GM_LD(gm_b16_8, ((uint8_t *)&t55));
        void * t57 = (void *)((uint8_t *)&gm_l2);
        uint64_t t58 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(((uint8_t *)t57 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(t58));
        gm_t5detta1b4gnk t60;
        gm_f_parse_args_7blm2mp((void *)&t60, (void *)NULL, cx, NULL, t52, t56, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_usage_failure_cgg6v1t((void *)&gm_l41, &t60);
        uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
        if (t61) {
        } else {
            gm_t3ujhu6qph7de t62 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l41) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t62);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l41) + 8), 120);
        uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
        uint8_t t64 = (!(t63 == ((uint64_t)1ULL)));
        if (t64) {
            gm_b16_8 t65;
            GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s4uu8ali3icr8));
            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(39));
            gm_t3ujhu6qph7de t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t66 + 8), t65);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t66);
            goto gm_fdone;
        }
        void * t67 = GM_LD(void *, ((uint8_t *)&F.l3));
        uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t68)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t68, gm_srccr53ejqr1dde, 191, 13);
        gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)t67) + (uintptr_t)((uint64_t)0ULL) * 16));
        gm_l4 = t69;
        {
            {
                {
                    gm_b16_8 t70 = gm_l4;
                    gm_b16_8 t71;
                    GM_ST(void *, (uint8_t *)&t71, (void *)(gm_s8f9rv6hrr024));
                    GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(10));
                    gm_f_name_problem_6r4mp11((void *)&gm_l42, (void *)NULL, cx, NULL, t70, t71);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t72 = (uint8_t)((((uint8_t *)&gm_l42))[16] != 0);
                if (!t72) {
                    goto gm_lb2;
                }
                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l42));
                gm_l5 = t73;
                {
                    gm_b16_8 t74 = gm_l5;
                    gm_t3ujhu6qph7de t75;
                    gm_f_fail_o80k3rv((void *)&t75, t74);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t75);
                    goto gm_fdone;
                }
            }
gm_lb2:;
        }
gm_lb1:;
        void * t76 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t77;
        GM_ST(void *, (uint8_t *)&t77, (void *)(gm_snboa22b96bbc));
        GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(3));
        gm_f_value_of_0km7vvp((void *)&gm_l44, t76, t77);
        uint8_t t78 = (uint8_t)((((uint8_t *)&gm_l44))[16] != 0);
        if (t78) {
            gm_b16_8 t79 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l44));
            gm_l43 = t79;
        } else {
            gm_b16_8 t80;
            GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(1));
            gm_l43 = t80;
        }
    }
    {
        gm_b16_8 t81 = gm_l43;
        gm_f_root_of_cluda11((void *)&gm_l45, (void *)NULL, cx, NULL, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
        if (t82) {
            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
            gm_l7 = t83;
        } else {
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
            gm_l6 = t84;
            {
                gm_b16_8 t85 = gm_l6;
                gm_t3ujhu6qph7de t86;
                gm_f_fail_o80k3rv((void *)&t86, t85);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t86);
                goto gm_fdone;
            }
        }
        gm_trqfvmjtt4sij t87;
        gm_f_list_epvducg((void *)&t87, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l8 = t87;
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_s420tm9ua7ggg));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(3));
        gm_b16_8 t89;
        GM_ST(void *, (uint8_t *)&t89, (void *)(gm_s0jho24omhfui));
        GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(6));
        gm_b16_8 t90;
        GM_ST(void *, (uint8_t *)&t90, (void *)(gm_sf9ilivr4c86m));
        GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(3));
        gm_b16_8 t91;
        GM_ST(void *, (uint8_t *)&t91, (void *)(gm_sla3alt5340mf));
        GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(7));
        GM_ST(gm_b16_8, ((uint8_t *)&F.l9 + 0), t88);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l9 + 16), t89);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l9 + 32), t90);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l9 + 48), t91);
        gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 16));
        gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 32));
        gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 48));
        gm_tdmi4fk9nhqa7 t96;
        GM_ST(gm_b16_8, ((uint8_t *)&t96 + 0), t92);
        GM_ST(gm_b16_8, ((uint8_t *)&t96 + 16), t93);
        GM_ST(gm_b16_8, ((uint8_t *)&t96 + 32), t94);
        GM_ST(gm_b16_8, ((uint8_t *)&t96 + 48), t95);
        F.l10 = t96;
        void * t97 = (void *)((uint8_t *)&F.l10);
        uint64_t t98 = ((uintptr_t)((uint64_t)4ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t99;
        GM_ST(void *, (uint8_t *)&t99, (void *)(((uint8_t *)t97 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(t98));
        gm_l11 = t99;
        gm_l12 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t100 = gm_l12;
                uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t102 = (t100 < t101);
                if (!t102) {
                    goto gm_lb3;
                }
                {
                    void * t103 = GM_LD(void *, ((uint8_t *)&gm_l11));
                    uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint64_t t105 = gm_l12;
                    {
                        {
                            gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)t103) + (uintptr_t)t105 * 16));
                            gm_l13 = t106;
                            void * t107 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t108 = gm_l13;
                            gm_f_value_of_0km7vvp((void *)&gm_l47, t107, t108);
                            {
                                {
                                    uint8_t t109 = (uint8_t)((((uint8_t *)&gm_l47))[16] != 0);
                                    if (t109) {
                                        goto gm_lb9;
                                    }
                                    gm_l46 = ((uint8_t)0);
                                    goto gm_lb8;
                                }
gm_lb9:;
                                {
                                    gm_l46 = ((uint8_t)1);
                                    goto gm_lb8;
                                }
gm_lb10:;
                                gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 196, 12);
                            }
gm_lb8:;
                            uint8_t t110 = gm_l46;
                            if (t110) {
                                void * t111 = (void *)((uint8_t *)&F.l8);
                                gm_b16_8 t112 = gm_l13;
                                gm_tftb1odc739t5 t113 = gm_f_push_3ef746b(t111, t112);
                                gm_l48 = t113;
                                uint8_t t114 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                if (!t114) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_srccr53ejqr1dde, 196, 42);
                            }
                            goto gm_lb6;
                        }
gm_lb7:;
                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 195, 5);
                    }
gm_lb6:;
                }
gm_lb5:;
                uint64_t t115 = gm_l12;
                uint64_t r116; bool o117 = __builtin_add_overflow(t115, ((uint64_t)1ULL), &r116);
                uint64_t t118 = r116;
                gm_l12 = t118;
            }
        }
gm_lb3:;
    }
    {
        gm_l14 = ((uint64_t)0ULL);
        gm_b16_8 t119;
        GM_ST(void *, (uint8_t *)&t119, (void *)(gm_smimi08sfrg0n));
        GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(4));
        gm_b16_8 t120;
        GM_ST(void *, (uint8_t *)&t120, (void *)(gm_s0k6ioi39f878));
        GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(3));
        gm_b16_8 t121;
        GM_ST(void *, (uint8_t *)&t121, (void *)(gm_sa2h9vonan0ma));
        GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, ((uint8_t *)&F.l15 + 0), t119);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l15 + 16), t120);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l15 + 32), t121);
        gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
        gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 16));
        gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 32));
        gm_t9bmh80edou9c t125;
        GM_ST(gm_b16_8, ((uint8_t *)&t125 + 0), t122);
        GM_ST(gm_b16_8, ((uint8_t *)&t125 + 16), t123);
        GM_ST(gm_b16_8, ((uint8_t *)&t125 + 32), t124);
        F.l16 = t125;
        void * t126 = (void *)((uint8_t *)&F.l16);
        uint64_t t127 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t128;
        GM_ST(void *, (uint8_t *)&t128, (void *)(((uint8_t *)t126 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(t127));
        gm_l17 = t128;
        gm_l18 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t129 = gm_l18;
                uint64_t t130 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint8_t t131 = (t129 < t130);
                if (!t131) {
                    goto gm_lb11;
                }
                {
                    void * t132 = GM_LD(void *, ((uint8_t *)&gm_l17));
                    uint64_t t133 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    uint64_t t134 = gm_l18;
                    {
                        {
                            gm_b16_8 t135 = GM_LD(gm_b16_8, (((uint8_t *)t132) + (uintptr_t)t134 * 16));
                            gm_l19 = t135;
                            void * t136 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t137 = gm_l19;
                            gm_f_value_of_0km7vvp((void *)&gm_l50, t136, t137);
                            {
                                {
                                    uint8_t t138 = (uint8_t)((((uint8_t *)&gm_l50))[16] != 0);
                                    if (t138) {
                                        goto gm_lb17;
                                    }
                                    gm_l49 = ((uint8_t)0);
                                    goto gm_lb16;
                                }
gm_lb17:;
                                {
                                    gm_l49 = ((uint8_t)1);
                                    goto gm_lb16;
                                }
gm_lb18:;
                                gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 200, 12);
                            }
gm_lb16:;
                            uint8_t t139 = gm_l49;
                            if (t139) {
                                uint64_t t140 = gm_l14;
                                uint64_t r141; bool o142 = __builtin_add_overflow(t140, ((uint64_t)1ULL), &r141);
                                if (o142) gm_trap("integer overflow in `+`", gm_srccr53ejqr1dde, 200, 42);
                                uint64_t t143 = r141;
                                gm_l14 = t143;
                            }
                            goto gm_lb14;
                        }
gm_lb15:;
                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 199, 5);
                    }
gm_lb14:;
                }
gm_lb13:;
                uint64_t t144 = gm_l18;
                uint64_t r145; bool o146 = __builtin_add_overflow(t144, ((uint64_t)1ULL), &r145);
                uint64_t t147 = r145;
                gm_l18 = t147;
            }
        }
gm_lb11:;
        uint64_t t148 = gm_l14;
        uint8_t t149 = (t148 > ((uint64_t)1ULL));
        if (t149) {
            gm_b16_8 t150;
            GM_ST(void *, (uint8_t *)&t150, (void *)(gm_so6e0kl9gkq21));
            GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(45));
            gm_t3ujhu6qph7de t151;
            GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t151 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t151 + 8), t150);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t151);
            goto gm_fdone;
        }
        {
            {
                {
                    void * t152 = (void *)((uint8_t *)&F.l3);
                    gm_b16_8 t153;
                    GM_ST(void *, (uint8_t *)&t153, (void *)(gm_sd1smnnii4cig));
                    GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(6));
                    gm_f_value_of_0km7vvp((void *)&gm_l52, t152, t153);
                }
                {
                    {
                        uint8_t t154 = (uint8_t)((((uint8_t *)&gm_l52))[16] != 0);
                        if (t154) {
                            goto gm_lb22;
                        }
                        gm_l51 = ((uint8_t)0);
                        goto gm_lb21;
                    }
gm_lb22:;
                    {
                        gm_l51 = ((uint8_t)1);
                        goto gm_lb21;
                    }
gm_lb23:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 203, 8);
                }
gm_lb21:;
                uint8_t t155 = gm_l51;
                if (!t155) {
                    goto gm_lb20;
                }
                {
                    void * t156 = (void *)((uint8_t *)&F.l3);
                    gm_b16_8 t157;
                    GM_ST(void *, (uint8_t *)&t157, (void *)(gm_sa2h9vonan0ma));
                    GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(3));
                    gm_f_value_of_0km7vvp((void *)&gm_l54, t156, t157);
                }
                {
                    {
                        uint8_t t158 = (uint8_t)((((uint8_t *)&gm_l54))[16] != 0);
                        if (t158) {
                            goto gm_lb25;
                        }
                        gm_l53 = ((uint8_t)1);
                        goto gm_lb24;
                    }
gm_lb25:;
                    {
                        gm_l53 = ((uint8_t)0);
                        goto gm_lb24;
                    }
gm_lb26:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 203, 47);
                }
gm_lb24:;
                uint8_t t159 = gm_l53;
                if (!t159) {
                    goto gm_lb20;
                }
                {
                    gm_b16_8 t160;
                    GM_ST(void *, (uint8_t *)&t160, (void *)(gm_s7ch91qn11t34));
                    GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(24));
                    gm_t3ujhu6qph7de t161;
                    GM_ST(uint8_t, (uint8_t *)&t161 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t161 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t161 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t161 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t161 + 8), t160);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t161);
                    goto gm_fdone;
                }
            }
gm_lb20:;
        }
gm_lb19:;
    }
    {
        gm_b16_8 t162;
        GM_ST(void *, (uint8_t *)&t162, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(0));
        memset(&F.l20, 0, sizeof F.l20);
        GM_ST(uint8_t, ((uint8_t *)&F.l20 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&F.l20 + 8), t162);
        {
            {
                {
                    void * t163 = (void *)((uint8_t *)&F.l3);
                    gm_b16_8 t164;
                    GM_ST(void *, (uint8_t *)&t164, (void *)(gm_sa2h9vonan0ma));
                    GM_ST(uintptr_t, (uint8_t *)&t164 + 8, (uintptr_t)(3));
                    gm_f_value_of_0km7vvp((void *)&gm_l55, t163, t164);
                }
                uint8_t t165 = (uint8_t)((((uint8_t *)&gm_l55))[16] != 0);
                if (!t165) {
                    goto gm_lb28;
                }
                gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l55));
                gm_l21 = t166;
                uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                uint8_t t168 = (t167 > ((uint64_t)0ULL));
                if (t168) {
                    gm_b16_8 t169;
                    GM_ST(void *, (uint8_t *)&t169, (void *)(gm_suq3bgh3anb87));
                    GM_ST(uintptr_t, (uint8_t *)&t169 + 8, (uintptr_t)(57));
                    gm_t3ujhu6qph7de t170;
                    GM_ST(uint8_t, (uint8_t *)&t170 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t170 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t170 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t170 + 8), t169);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t170);
                    goto gm_fdone;
                }
                {
                    gm_b16_8 t171 = gm_l21;
                    void * t172 = (void *)((uint8_t *)&F.l3);
                    gm_b16_8 t173;
                    GM_ST(void *, (uint8_t *)&t173, (void *)(gm_sd1smnnii4cig));
                    GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(6));
                    gm_th2lqbo77pvsc t174;
                    gm_f_value_of_0km7vvp((void *)&t174, t172, t173);
                    void * t175 = (void *)((uint8_t *)&F.l3);
                    gm_b16_8 t176;
                    GM_ST(void *, (uint8_t *)&t176, (void *)(gm_sfvspsnv23gbt));
                    GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(6));
                    gm_th2lqbo77pvsc t177;
                    gm_f_value_of_0km7vvp((void *)&t177, t175, t176);
                    gm_tt008mgut8glb t178;
                    GM_ST(gm_b16_8, ((uint8_t *)&t178 + 0), t171);
                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t178 + 16), t174);
                    GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t178 + 40), t177);
                    GM_ST(uint8_t, (uint8_t *)&F.l20 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.l20 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.l20 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&F.l20 + 72, 0);
                    GM_ST(uint64_t, (uint8_t *)&F.l20 + 80, 0);
                    GM_ST(uint8_t, ((uint8_t *)&F.l20 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_tt008mgut8glb, ((uint8_t *)&F.l20 + 8), t178);
                }
                goto gm_lb27;
            }
gm_lb28:;
            {
                {
                    {
                        void * t179 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t180;
                        GM_ST(void *, (uint8_t *)&t180, (void *)(gm_smimi08sfrg0n));
                        GM_ST(uintptr_t, (uint8_t *)&t180 + 8, (uintptr_t)(4));
                        gm_f_value_of_0km7vvp((void *)&gm_l56, t179, t180);
                    }
                    uint8_t t181 = (uint8_t)((((uint8_t *)&gm_l56))[16] != 0);
                    if (!t181) {
                        goto gm_lb30;
                    }
                    gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l56));
                    gm_l22 = t182;
                    uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                    uint8_t t184 = (t183 > ((uint64_t)0ULL));
                    if (t184) {
                        gm_l57 = ((uint8_t)1);
                    } else {
                        void * t185 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t186;
                        GM_ST(void *, (uint8_t *)&t186, (void *)(gm_sfvspsnv23gbt));
                        GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(6));
                        gm_f_value_of_0km7vvp((void *)&gm_l59, t185, t186);
                        {
                            {
                                uint8_t t187 = (uint8_t)((((uint8_t *)&gm_l59))[16] != 0);
                                if (t187) {
                                    goto gm_lb32;
                                }
                                gm_l58 = ((uint8_t)0);
                                goto gm_lb31;
                            }
gm_lb32:;
                            {
                                gm_l58 = ((uint8_t)1);
                                goto gm_lb31;
                            }
gm_lb33:;
                            gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 211, 34);
                        }
gm_lb31:;
                        uint8_t t188 = gm_l58;
                        gm_l57 = t188;
                    }
                    uint8_t t189 = gm_l57;
                    if (t189) {
                        gm_b16_8 t190;
                        GM_ST(void *, (uint8_t *)&t190, (void *)(gm_s2n0v309tod60));
                        GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(69));
                        gm_t3ujhu6qph7de t191;
                        GM_ST(uint8_t, (uint8_t *)&t191 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t191 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t191 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t191 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t191 + 8), t190);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t191);
                        goto gm_fdone;
                    }
                    {
                        gm_b16_8 t192 = gm_l22;
                        gm_f_canonicalize_epo7cln((void *)&gm_l60, (void *)NULL, cx, NULL, t192);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t193 = (uint8_t)((((uint8_t *)&gm_l60))[16] != 0);
                    if (t193) {
                        gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l60));
                        gm_l23 = t194;
                    } else {
                        {
                            gm_b16_8 t195 = gm_l22;
                            gm_tnpifv20irnph t196;
                            GM_ST(gm_b16_8, ((uint8_t *)&t196 + 0), t195);
                            gm_f_format_udqe6i2((void *)&gm_l61, (void *)NULL, cx, NULL, t196);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t197 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                        if (!t197) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 8, gm_srccr53ejqr1dde, 215, 26);
                        {
                            gm_b16_8 t198 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
                            gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)&t198));
                            gm_t3ujhu6qph7de t200;
                            gm_f_fail_o80k3rv((void *)&t200, t199);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t200);
                            goto gm_fdone;
                        }
                    }
                    {
                        gm_b16_8 t201 = gm_l7;
                        gm_b16_8 t202 = gm_l23;
                        gm_b16_8 t203 = gm_f_relative_path_3klciqu((void *)NULL, cx, NULL, t201, t202);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        memset(&F.l20, 0, sizeof F.l20);
                        GM_ST(uint8_t, ((uint8_t *)&F.l20 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&F.l20 + 8), t203);
                    }
                    goto gm_lb29;
                }
gm_lb30:;
                {
                    {
                        {
                            void * t204 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t205;
                            GM_ST(void *, (uint8_t *)&t205, (void *)(gm_s0k6ioi39f878));
                            GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(3));
                            gm_f_value_of_0km7vvp((void *)&gm_l62, t204, t205);
                        }
                        uint8_t t206 = (uint8_t)((((uint8_t *)&gm_l62))[16] != 0);
                        if (!t206) {
                            goto gm_lb35;
                        }
                        gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l62));
                        gm_l24 = t207;
                        {
                            {
                                {
                                    gm_b16_8 t208 = gm_l24;
                                    gm_f_url_problem_38kdh3a((void *)&gm_l63, (void *)NULL, cx, NULL, t208);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t209 = (uint8_t)((((uint8_t *)&gm_l63))[16] != 0);
                                if (!t209) {
                                    goto gm_lb37;
                                }
                                gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l63));
                                gm_l25 = t210;
                                {
                                    gm_b16_8 t211 = gm_l25;
                                    gm_t3ujhu6qph7de t212;
                                    GM_ST(uint8_t, (uint8_t *)&t212 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t212 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t212 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t212 + 0), ((uint8_t)0ULL));
                                    GM_ST(gm_b16_8, ((uint8_t *)&t212 + 8), t211);
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t212);
                                    goto gm_fdone;
                                }
                            }
gm_lb37:;
                        }
gm_lb36:;
                        uint64_t t213 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                        uint8_t t214 = (t213 > ((uint64_t)1ULL));
                        if (t214) {
                            gm_b16_8 t215;
                            GM_ST(void *, (uint8_t *)&t215, (void *)(gm_sankagtb3ecgn));
                            GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(57));
                            gm_t3ujhu6qph7de t216;
                            GM_ST(uint8_t, (uint8_t *)&t216 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t216 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t216 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t216 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t216 + 8), t215);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t216);
                            goto gm_fdone;
                        }
                        {
                            memset(&F.l26, 0, sizeof F.l26);
                        }
                        uint64_t t217 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                        uint8_t t218 = ((t217 == ((uint64_t)1ULL)));
                        if (t218) {
                            void * t219 = GM_LD(void *, ((uint8_t *)&F.l8));
                            uint64_t t220 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t220)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t220, gm_srccr53ejqr1dde, 223, 18);
                            gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)t219) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_l27 = t221;
                            {
                                void * t222 = (void *)((uint8_t *)&F.l3);
                                gm_b16_8 t223 = gm_l27;
                                gm_f_value_of_0km7vvp((void *)&gm_l64, t222, t223);
                            }
                            uint8_t t224 = (uint8_t)((((uint8_t *)&gm_l64))[16] != 0);
                            if (!t224) gm_trap("unwrap of null", gm_srccr53ejqr1dde, 224, 18);
                            gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l64));
                            gm_l28 = t225;
                            gm_b16_8 t226 = gm_l27;
                            gm_b16_8 t227;
                            GM_ST(void *, (uint8_t *)&t227, (void *)(gm_sla3alt5340mf));
                            GM_ST(uintptr_t, (uint8_t *)&t227 + 8, (uintptr_t)(7));
                            uint8_t t228 = (gm_eq0iqiubfm7e(((uint8_t *)&t226), ((uint8_t *)&t227)));
                            if (t228) {
                                {
                                    gm_b16_8 t229 = gm_l28;
                                    gm_b16_8 t230 = gm_f_trim_ascii_c9j36d1(t229);
                                    gm_f_parse_version_req_alipvfi((void *)&gm_l66, (void *)NULL, cx, NULL, t230);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t231 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                                if (t231) {
                                    memmove(((uint8_t *)&gm_l65), (((uint8_t *)&gm_l66) + 8), 40);
                                } else {
                                    gm_b16_8 t232 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l66) + 8));
                                    gm_l29 = t232;
                                    {
                                        gm_b16_8 t233 = gm_l28;
                                        gm_b16_8 t234 = gm_l29;
                                        gm_t6j6smj7qoctu t235;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t235 + 0), t233);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t235 + 16), t234);
                                        gm_f_format_pah1p62((void *)&gm_l67, (void *)NULL, cx, NULL, &t235);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t236 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                                    if (!t236) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_srccr53ejqr1dde, 227, 34);
                                    {
                                        gm_b16_8 t237 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                                        gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&t237));
                                        gm_t3ujhu6qph7de t239;
                                        gm_f_fail_o80k3rv((void *)&t239, t238);
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t239);
                                        goto gm_fdone;
                                    }
                                }
                                {
                                }
                            }
                            {
                                gm_b16_8 t240 = gm_l27;
                                gm_b16_8 t241 = gm_l28;
                                gm_t6j6smj7qoctu t242;
                                GM_ST(gm_b16_8, ((uint8_t *)&t242 + 0), t240);
                                GM_ST(gm_b16_8, ((uint8_t *)&t242 + 16), t241);
                                GM_ST(uint8_t, (uint8_t *)&F.l26 + 33, 0);
                                GM_ST(uint16_t, (uint8_t *)&F.l26 + 34, 0);
                                GM_ST(uint32_t, (uint8_t *)&F.l26 + 36, 0);
                                GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&F.l26), t242);
                                ((uint8_t *)&F.l26)[32] = 1;
                            }
                        } else {
                            {
                                {
                                    {
                                        gm_b16_8 t243 = gm_l24;
                                        gm_f_newest_version_dsisujv((void *)&gm_l68, (void *)NULL, cx, NULL, t243);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t244 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                                    if (t244) {
                                    } else {
                                        gm_t3ujhu6qph7de t245 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l68) + 8));
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t245);
                                        goto gm_fdone;
                                    }
                                    {
                                        memmove(((uint8_t *)&gm_l69), (((uint8_t *)&gm_l68) + 8), 64);
                                    }
                                    uint8_t t246 = (uint8_t)((((uint8_t *)&gm_l69))[56] != 0);
                                    if (!t246) {
                                        goto gm_lb39;
                                    }
                                    {
                                        memmove(((uint8_t *)&F.l30), ((uint8_t *)&gm_l69), 56);
                                    }
                                    {
                                        gm_b16_8 t247;
                                        GM_ST(void *, (uint8_t *)&t247, (void *)(gm_sla3alt5340mf));
                                        GM_ST(uintptr_t, (uint8_t *)&t247 + 8, (uintptr_t)(7));
                                        gm_b16_8 t248 = gm_f_version_string_k02v4te((void *)NULL, cx, NULL, &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l30)));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tnpifv20irnph t249;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t249 + 0), t248);
                                        gm_f_format_dhjqjjt((void *)&gm_l70, (void *)NULL, cx, NULL, t249);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t250 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                                        if (!t250) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 8, gm_srccr53ejqr1dde, 232, 49);
                                        gm_b16_8 t251 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l70) + 8));
                                        gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&t251));
                                        gm_t6j6smj7qoctu t253;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t253 + 0), t247);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t253 + 16), t252);
                                        GM_ST(uint8_t, (uint8_t *)&F.l26 + 33, 0);
                                        GM_ST(uint16_t, (uint8_t *)&F.l26 + 34, 0);
                                        GM_ST(uint32_t, (uint8_t *)&F.l26 + 36, 0);
                                        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&F.l26), t253);
                                        ((uint8_t *)&F.l26)[32] = 1;
                                    }
                                    goto gm_lb38;
                                }
gm_lb39:;
                            }
gm_lb38:;
                        }
                        {
                            gm_b16_8 t254 = gm_l24;
                            void * t255 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t256;
                            GM_ST(void *, (uint8_t *)&t256, (void *)(gm_sfvspsnv23gbt));
                            GM_ST(uintptr_t, (uint8_t *)&t256 + 8, (uintptr_t)(6));
                            gm_ttu09lij36f8c t257 = GM_LD(gm_ttu09lij36f8c, ((uint8_t *)&F.l26));
                            gm_th2lqbo77pvsc t258;
                            gm_f_value_of_0km7vvp((void *)&t258, t255, t256);
                            gm_t03ccdabio1fj t259;
                            GM_ST(gm_b16_8, ((uint8_t *)&t259 + 0), t254);
                            GM_ST(gm_ttu09lij36f8c, ((uint8_t *)&t259 + 16), t257);
                            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t259 + 56), t258);
                            GM_ST(uint8_t, (uint8_t *)&F.l20 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&F.l20 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&F.l20 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&F.l20 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_t03ccdabio1fj, ((uint8_t *)&F.l20 + 8), t259);
                        }
                        goto gm_lb34;
                    }
gm_lb35:;
                    {
                        gm_b16_8 t260;
                        GM_ST(void *, (uint8_t *)&t260, (void *)(gm_seo2gq6ouvlf6));
                        GM_ST(uintptr_t, (uint8_t *)&t260 + 8, (uintptr_t)(58));
                        gm_t3ujhu6qph7de t261;
                        GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t261 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t261 + 8), t260);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t261);
                        goto gm_fdone;
                    }
                }
gm_lb34:;
            }
gm_lb29:;
        }
gm_lb27:;
    }
    {
        gm_b16_8 t262 = gm_l7;
        gm_b16_8 t263;
        GM_ST(void *, (uint8_t *)&t263, (void *)(gm_se24c7hpp401n));
        GM_ST(uintptr_t, (uint8_t *)&t263 + 8, (uintptr_t)(10));
        gm_b16_8 t264 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t262, t263);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l31 = t264;
        gm_b16_8 t265 = gm_l31;
        gm_f_read_text_g0qbj1t((void *)&gm_l71, (void *)NULL, cx, NULL, t265);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t266 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
        if (t266) {
            gm_b16_8 t267 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l71) + 8));
            gm_l33 = t267;
        } else {
            gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l71) + 8));
            gm_l32 = t268;
            {
                gm_b16_8 t269 = gm_l32;
                gm_t3ujhu6qph7de t270;
                gm_f_fail_o80k3rv((void *)&t270, t269);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t270);
                goto gm_fdone;
            }
        }
        gm_b16_8 t271 = gm_l33;
        gm_f_parse_manifest_b61s3ln((void *)&gm_l73, (void *)NULL, cx, NULL, t271);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t272 = (uint8_t)!((((uint8_t *)&gm_l73))[0] != 0);
        if (t272) {
            memmove(((uint8_t *)&gm_l72), (((uint8_t *)&gm_l73) + 8), 320);
        } else {
            {
                memmove(((uint8_t *)&gm_l34), (((uint8_t *)&gm_l73) + 8), 24);
            }
            {
                gm_b16_8 t273 = gm_l31;
                uint64_t t274 = GM_LD(uint64_t, ((uint8_t *)&gm_l34));
                gm_b16_8 t275 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                gm_tq3m5qgvkpje8 t276;
                GM_ST(gm_b16_8, ((uint8_t *)&t276 + 0), t273);
                GM_ST(uint64_t, ((uint8_t *)&t276 + 16), t274);
                GM_ST(gm_b16_8, ((uint8_t *)&t276 + 24), t275);
                gm_f_format_n6eprd4((void *)&gm_l74, (void *)NULL, cx, NULL, &t276);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t277 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
            if (!t277) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 8, gm_srccr53ejqr1dde, 241, 22);
            {
                gm_b16_8 t278 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l74) + 8));
                gm_b16_8 t279 = GM_LD(gm_b16_8, ((uint8_t *)&t278));
                gm_t3ujhu6qph7de t280;
                gm_f_fail_o80k3rv((void *)&t280, t279);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t280);
                goto gm_fdone;
            }
        }
        gm_b16_8 t281 = gm_l33;
        gm_b16_8 t282 = gm_l4;
        gm_tm43kqe3e8b4a t283 = GM_LD(gm_tm43kqe3e8b4a, ((uint8_t *)&gm_l72));
        gm_t2jeshnsn30ep t284 = GM_LD(gm_t2jeshnsn30ep, ((uint8_t *)&F.l20));
        gm_f_add_dependency_jmjffrp((void *)&gm_l75, (void *)NULL, cx, NULL, t281, t282, &t284);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t285 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
        if (t285) {
            gm_b16_8 t286 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l75) + 8));
            gm_l36 = t286;
        } else {
            gm_b16_8 t287 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l75) + 8));
            gm_l35 = t287;
            {
                gm_b16_8 t288 = gm_l35;
                gm_t3ujhu6qph7de t289;
                gm_f_fail_o80k3rv((void *)&t289, t288);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t289);
                goto gm_fdone;
            }
        }
        gm_b16_8 t290 = gm_l36;
        gm_f_parse_manifest_b61s3ln((void *)&gm_l77, (void *)NULL, cx, NULL, t290);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t291 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
        if (t291) {
            memmove(((uint8_t *)&gm_l76), (((uint8_t *)&gm_l77) + 8), 320);
        } else {
            {
                memmove(((uint8_t *)&gm_l37), (((uint8_t *)&gm_l77) + 8), 24);
            }
            {
                gm_b16_8 t292 = gm_l31;
                uint64_t t293 = GM_LD(uint64_t, ((uint8_t *)&gm_l37));
                gm_b16_8 t294 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l37) + 8));
                gm_tq3m5qgvkpje8 t295;
                GM_ST(gm_b16_8, ((uint8_t *)&t295 + 0), t292);
                GM_ST(uint64_t, ((uint8_t *)&t295 + 16), t293);
                GM_ST(gm_b16_8, ((uint8_t *)&t295 + 24), t294);
                gm_f_format_n6eprd4((void *)&gm_l78, (void *)NULL, cx, NULL, &t295);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t296 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
            if (!t296) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 8, gm_srccr53ejqr1dde, 246, 22);
            {
                gm_b16_8 t297 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l78) + 8));
                gm_b16_8 t298 = GM_LD(gm_b16_8, ((uint8_t *)&t297));
                gm_t3ujhu6qph7de t299;
                gm_f_fail_o80k3rv((void *)&t299, t298);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t299);
                goto gm_fdone;
            }
        }
    }
    {
        gm_b16_8 t300 = gm_l31;
        gm_b16_8 t301 = gm_l36;
        gm_tm43kqe3e8b4a t302 = GM_LD(gm_tm43kqe3e8b4a, ((uint8_t *)&gm_l76));
        gm_f_write_atomic_v74ncu3((void *)&gm_l79, (void *)NULL, cx, NULL, t300, t301);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t303 = (uint8_t)!((((uint8_t *)&gm_l79))[0] != 0);
        if (t303) {
        } else {
            gm_b16_8 t304 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l79) + 8));
            gm_l38 = t304;
            {
                gm_b16_8 t305 = gm_l38;
                gm_t3ujhu6qph7de t306;
                gm_f_fail_o80k3rv((void *)&t306, t305);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t306);
                goto gm_fdone;
            }
        }
        gm_b16_8 t307 = gm_l7;
        gm_tqs5p4n2tq19v t308;
        GM_ST(uint8_t, ((uint8_t *)&t308 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t308 + 1), ((uint8_t)0));
        gm_tl572d7579hns t309;
        GM_ST(uint8_t, (uint8_t *)&t309 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t309 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t309 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t309 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t309 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t309 + 0), ((uint8_t)0ULL));
        gm_f_sync_ht164ep((void *)&gm_l81, (void *)NULL, cx, NULL, t307, t308, &t309, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t310 = (uint8_t)!((((uint8_t *)&gm_l81))[0] != 0);
        if (t310) {
            memmove(((uint8_t *)&gm_l80), (((uint8_t *)&gm_l81) + 8), 48);
        } else {
            gm_b16_8 t311 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l81) + 8));
            gm_l39 = t311;
            {
                gm_b16_8 t312 = gm_l31;
                gm_b16_8 t313 = gm_l33;
                gm_f_write_atomic_v74ncu3((void *)&gm_l82, (void *)NULL, cx, NULL, t312, t313);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t314 = (uint8_t)!((((uint8_t *)&gm_l82))[0] != 0);
            if (t314) {
            } else {
                gm_b16_8 t315 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l82) + 8));
                gm_l40 = t315;
                {
                    gm_b16_8 t316 = gm_l40;
                    gm_t3ujhu6qph7de t317;
                    gm_f_fail_o80k3rv((void *)&t317, t316);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t317);
                    goto gm_fdone;
                }
            }
            {
                gm_b16_8 t318 = gm_l39;
                gm_t3ujhu6qph7de t319;
                gm_f_fail_o80k3rv((void *)&t319, t318);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t319);
                goto gm_fdone;
            }
        }
        gm_b16_8 t320 = gm_l4;
        gm_tv37d3dpt2opu t321 = GM_LD(gm_tv37d3dpt2opu, ((uint8_t *)&gm_l80));
        if (gm_pf454qfrpt6r(cx, gm_svdospumf3o67, 13, gm_srccr53ejqr1dde, 253, 5)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t320) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t320)), GM_LD(uintptr_t, ((uint8_t *)&t320) + 8), gm_srccr53ejqr1dde, 253, 5))) {
            goto gm_lad;
        }
        if (gm_pf454qfrpt6r(cx, gm_sueadt05fevtm, 17, gm_srccr53ejqr1dde, 253, 5)) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
__attribute__((aligned(4))) void gm_f_cache_command_7uqmos8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_cache_command_7uqmos8 F;
    gm_b16_8 gm_l0; /* args */
    gm_b16_8 gm_l1; /* sub */
    gm_b16_8 gm_l2; /* rest */
    gm_th2lqbo77pvsc gm_l3; /* dir */
    gm_b0_8 gm_l4; /* <temporary array> */
    gm_b0_8 gm_l5; /* <temporary array> */
    gm_b16_8 gm_l6; /* d */
    gm_tpmdr2hmdhsqr gm_l7; /* <temporary array> */
    gm_t0kl141g5m2qf gm_l8; /* <temporary array> */
    uint64_t gm_l10; /* days */
    gm_b16_8 gm_l11; /* v */
    gm_b16_8 gm_l12; /* d */
    uint64_t gm_l13; /* secs */
    gm_t553jgmqd1tlr gm_l14; /* policy */
    GM_STACK_CHECK(&F, 2423u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t77ebp3ogdble gm_l16;
    gm_t77ebp3ogdble gm_l17;
    gm_th2lqbo77pvsc gm_l18;
    uint64_t gm_l19;
    gm_tutnubt6u4hoq gm_l20;
    gm_tabb710d9mq2e gm_l21;
    uint8_t gm_l22;
    gm_th2lqbo77pvsc gm_l23;
    gm_tf5srh4mp4b3i gm_l24;
    uint64_t gm_l25;
    gm_tf5srh4mp4b3i gm_l26;
    gm_tabb710d9mq2e gm_l27;
    gm_tabb710d9mq2e gm_l28;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t14 = ((t13 == ((uint64_t)0ULL)));
    if (t14) {
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_sshhi6fn3vfaa));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(44));
        gm_t3ujhu6qph7de t16;
        GM_ST(uint8_t, (uint8_t *)&t16 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t16 + 8), t15);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t16);
        goto gm_fdone;
    }
    void * t17 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t18, gm_srccr53ejqr1dde, 292, 12);
    gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t17) + (uintptr_t)((uint64_t)0ULL) * 16));
    gm_l1 = t19;
    void * t20 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t22 = (uintptr_t)((uint64_t)1ULL);
    uint64_t t23 = (uintptr_t)t21;
    if (t22 > t23 || t23 > (uintptr_t)t21) gm_trap_slice(t22, t23, t21, gm_srccr53ejqr1dde, 293, 13);
    uint64_t t24 = ((uintptr_t)t21) - (uintptr_t)((uint64_t)1ULL);
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)1ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    gm_l2 = t25;
    gm_f_cache_dir_81h90qu((void *)&gm_l3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        {
            gm_b16_8 t26 = gm_l1;
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_snboa22b96bbc));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(3));
            uint8_t t28 = (gm_eq0iqiubfm7e(((uint8_t *)&t26), ((uint8_t *)&t27)));
            if (!t28) {
                goto gm_lb2;
            }
            {
                gm_b16_8 t29 = gm_l2;
                void * t30 = (void *)((uint8_t *)&gm_l4);
                uint64_t t31 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t32;
                GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t30 + (uintptr_t)((uint64_t)0ULL) * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31));
                void * t33 = (void *)((uint8_t *)&gm_l5);
                uint64_t t34 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t35;
                GM_ST(void *, (uint8_t *)&t35, (void *)(((uint8_t *)t33 + (uintptr_t)((uint64_t)0ULL) * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(t34));
                gm_t5detta1b4gnk t36;
                gm_f_parse_args_7blm2mp((void *)&t36, (void *)NULL, cx, NULL, t29, t32, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_usage_failure_cgg6v1t((void *)&gm_l16, &t36);
            }
            uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
            if (t37) {
            } else {
                gm_t3ujhu6qph7de t38 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l16) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t38);
                goto gm_fdone;
            }
            {
            }
            uint8_t t39 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (t39) {
                gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                gm_l6 = t40;
            } else {
                gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)gm_cftc9ncjfm7));
                gm_t3ujhu6qph7de t42;
                gm_f_fail_o80k3rv((void *)&t42, t41);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t42);
                goto gm_fdone;
            }
            gm_b16_8 t43 = gm_l6;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t43) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t43)), GM_LD(uintptr_t, ((uint8_t *)&t43) + 8), gm_srccr53ejqr1dde, 300, 13))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 300, 13)) {
                goto gm_lad;
            }
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), GM_ZERO(gm_b0_1));
                goto gm_fdone;
            }
        }
gm_lb2:;
        {
            gm_b16_8 t44 = gm_l1;
            gm_b16_8 t45;
            GM_ST(void *, (uint8_t *)&t45, (void *)(gm_slskjtl6alfrd));
            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(5));
            uint8_t t46 = (gm_eq0iqiubfm7e(((uint8_t *)&t44), ((uint8_t *)&t45)));
            if (!t46) {
                goto gm_lb3;
            }
            {
                gm_b16_8 t47 = gm_l2;
                gm_b16_8 t48;
                GM_ST(void *, (uint8_t *)&t48, (void *)(gm_so87vf8a6o4mm));
                GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(10));
                gm_tpmdr2hmdhsqr t49;
                GM_ST(gm_b16_8, ((uint8_t *)&t49 + 0), t48);
                gm_l7 = t49;
                void * t50 = (void *)((uint8_t *)&gm_l7);
                uint64_t t51 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t52;
                GM_ST(void *, (uint8_t *)&t52, (void *)(((uint8_t *)t50 + (uintptr_t)((uint64_t)0ULL) * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(t51));
                gm_b16_8 t53;
                GM_ST(void *, (uint8_t *)&t53, (void *)(gm_s0nf1rg04f3m5));
                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(3));
                gm_b16_8 t54;
                GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sjgng334veu6f));
                GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(7));
                gm_t0kl141g5m2qf t55;
                GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t53);
                GM_ST(gm_b16_8, ((uint8_t *)&t55 + 16), t54);
                gm_l8 = t55;
                void * t56 = (void *)((uint8_t *)&gm_l8);
                uint64_t t57 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t58;
                GM_ST(void *, (uint8_t *)&t58, (void *)(((uint8_t *)t56 + (uintptr_t)((uint64_t)0ULL) * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(t57));
                gm_t5detta1b4gnk t59;
                gm_f_parse_args_7blm2mp((void *)&t59, (void *)NULL, cx, NULL, t47, t52, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_usage_failure_cgg6v1t((void *)&gm_l17, &t59);
            }
            uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (t60) {
            } else {
                gm_t3ujhu6qph7de t61 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l17) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t61);
                goto gm_fdone;
            }
            {
                memmove(((uint8_t *)&F.l9), (((uint8_t *)&gm_l17) + 8), 120);
            }
            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
            uint8_t t63 = (t62 > ((uint64_t)0ULL));
            if (t63) {
                gm_b16_8 t64;
                GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sr43s52rmecnf));
                GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(32));
                gm_t3ujhu6qph7de t65;
                GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t65 + 8), t64);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t65);
                goto gm_fdone;
            }
            gm_l10 = ((uint64_t)30ULL);
            {
                {
                    {
                        void * t66 = (void *)((uint8_t *)&F.l9);
                        gm_b16_8 t67;
                        GM_ST(void *, (uint8_t *)&t67, (void *)(gm_so87vf8a6o4mm));
                        GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(10));
                        gm_f_value_of_0km7vvp((void *)&gm_l18, t66, t67);
                    }
                    uint8_t t68 = (uint8_t)((((uint8_t *)&gm_l18))[16] != 0);
                    if (!t68) {
                        goto gm_lb5;
                    }
                    gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l18));
                    gm_l11 = t69;
                    {
                        gm_b16_8 t70 = gm_l11;
                        gm_f_parse_int_8aueeu2((void *)&gm_l20, t70);
                    }
                    uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                    if (t71) {
                        uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                        gm_l19 = t72;
                    } else {
                        {
                            gm_b16_8 t73 = gm_l11;
                            gm_tnpifv20irnph t74;
                            GM_ST(gm_b16_8, ((uint8_t *)&t74 + 0), t73);
                            gm_f_format_da7bimt((void *)&gm_l21, (void *)NULL, cx, NULL, t74);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                        if (!t75) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 8, gm_srccr53ejqr1dde, 309, 43);
                        {
                            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                            gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
                            gm_t3ujhu6qph7de t78;
                            GM_ST(uint8_t, (uint8_t *)&t78 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t78 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t78 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t78 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t78 + 8), t77);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t78);
                            goto gm_fdone;
                        }
                    }
                    uint64_t t79 = gm_l19;
                    gm_l10 = t79;
                    goto gm_lb4;
                }
gm_lb5:;
            }
gm_lb4:;
            {
                {
                    void * t80 = (void *)((uint8_t *)&F.l9);
                    gm_b16_8 t81;
                    GM_ST(void *, (uint8_t *)&t81, (void *)(gm_s0nf1rg04f3m5));
                    GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(3));
                    uint8_t t82 = gm_f_has_flag_4n9tu40(t80, t81);
                    if (!t82) {
                        goto gm_lb7;
                    }
                    {
                        void * t83 = (void *)((uint8_t *)&F.l9);
                        gm_b16_8 t84;
                        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_so87vf8a6o4mm));
                        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(10));
                        gm_f_value_of_0km7vvp((void *)&gm_l23, t83, t84);
                    }
                    {
                        {
                            uint8_t t85 = (uint8_t)((((uint8_t *)&gm_l23))[16] != 0);
                            if (t85) {
                                goto gm_lb9;
                            }
                            gm_l22 = ((uint8_t)0);
                            goto gm_lb8;
                        }
gm_lb9:;
                        {
                            gm_l22 = ((uint8_t)1);
                            goto gm_lb8;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 312, 40);
                    }
gm_lb8:;
                    uint8_t t86 = gm_l22;
                    if (!t86) {
                        goto gm_lb7;
                    }
                    {
                        gm_b16_8 t87;
                        GM_ST(void *, (uint8_t *)&t87, (void *)(gm_ss9d51lhe1orq));
                        GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(45));
                        gm_t3ujhu6qph7de t88;
                        GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t88 + 8), t87);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t88);
                        goto gm_fdone;
                    }
                }
gm_lb7:;
            }
gm_lb6:;
            uint8_t t89 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
            if (t89) {
                gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                gm_l12 = t90;
            } else {
                gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)gm_cftc9ncjfm7));
                gm_t3ujhu6qph7de t92;
                gm_f_fail_o80k3rv((void *)&t92, t91);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t92);
                goto gm_fdone;
            }
            uint64_t t93 = gm_l10;
            uint64_t r94; bool o95 = __builtin_mul_overflow(t93, ((uint64_t)86400ULL), &r94);
            gm_tf5srh4mp4b3i t96;
            if (o95) {
            gm_tf5srh4mp4b3i t97;
            memset(&t97, 0, sizeof t97);
                t96 = t97;
            } else {
            uint64_t t98 = r94;
            gm_tf5srh4mp4b3i t99;
            GM_ST(uint8_t, (uint8_t *)&t99 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t99 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t99 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t99), t98);
            ((uint8_t *)&t99)[8] = 1;
                t96 = t99;
            }
            gm_l24 = t96;
            uint8_t t100 = (uint8_t)((((uint8_t *)&gm_l24))[8] != 0);
            if (t100) {
                uint64_t t101 = GM_LD(uint64_t, ((uint8_t *)&gm_l24));
                gm_l13 = t101;
            } else {
                if (((uint64_t)1000000000ULL) == 0) gm_trap("division by zero", gm_srccr53ejqr1dde, 316, 40);
                uint64_t t102 = (uint64_t)(((uint64_t)18446744073709551615ULL) / ((uint64_t)1000000000ULL));
                gm_l13 = t102;
            }
            uint64_t t103 = gm_l13;
            uint64_t r104; bool o105 = __builtin_mul_overflow(t103, ((uint64_t)1000000000ULL), &r104);
            gm_tf5srh4mp4b3i t106;
            if (o105) {
            gm_tf5srh4mp4b3i t107;
            memset(&t107, 0, sizeof t107);
                t106 = t107;
            } else {
            uint64_t t108 = r104;
            gm_tf5srh4mp4b3i t109;
            GM_ST(uint8_t, (uint8_t *)&t109 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t109 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t109 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t109), t108);
            ((uint8_t *)&t109)[8] = 1;
                t106 = t109;
            }
            gm_l26 = t106;
            uint8_t t110 = (uint8_t)((((uint8_t *)&gm_l26))[8] != 0);
            if (t110) {
                uint64_t t111 = GM_LD(uint64_t, ((uint8_t *)&gm_l26));
                gm_l25 = t111;
            } else {
                gm_l25 = ((uint64_t)18446744073709551615ULL);
            }
            uint64_t t112 = gm_l25;
            void * t113 = (void *)((uint8_t *)&F.l9);
            gm_b16_8 t114;
            GM_ST(void *, (uint8_t *)&t114, (void *)(gm_s0nf1rg04f3m5));
            GM_ST(uintptr_t, (uint8_t *)&t114 + 8, (uintptr_t)(3));
            uint8_t t115 = gm_f_has_flag_4n9tu40(t113, t114);
            void * t116 = (void *)((uint8_t *)&F.l9);
            gm_b16_8 t117;
            GM_ST(void *, (uint8_t *)&t117, (void *)(gm_sjgng334veu6f));
            GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(7));
            uint8_t t118 = gm_f_has_flag_4n9tu40(t116, t117);
            gm_t553jgmqd1tlr t119;
            GM_ST(uint16_t, (uint8_t *)&t119 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t119 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t119 + 0), t112);
            GM_ST(uint8_t, ((uint8_t *)&t119 + 8), t115);
            GM_ST(uint8_t, ((uint8_t *)&t119 + 9), t118);
            gm_l14 = t119;
            {
                gm_b16_8 t120 = gm_l12;
                gm_t553jgmqd1tlr t121 = gm_l14;
                gm_t9j922kfqcdi9 t122;
                gm_f_clean_97pd952((void *)&t122, (void *)NULL, cx, NULL, t120, t121);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l15 = t122;
            }
            void * t123 = (void *)((uint8_t *)&F.l15);
            uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 9));
            gm_b16_8 t125 = gm_f_summary_s3h16om((void *)NULL, cx, NULL, t123, t124);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t126 = gm_l12;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t125) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t125)), GM_LD(uintptr_t, ((uint8_t *)&t125) + 8), gm_srccr53ejqr1dde, 323, 13))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_s7hkl5mqa15gm, 4, gm_srccr53ejqr1dde, 323, 13)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t126) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t126)), GM_LD(uintptr_t, ((uint8_t *)&t126) + 8), gm_srccr53ejqr1dde, 323, 13))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 323, 13)) {
                goto gm_lad;
            }
            uint64_t t127 = GM_LD(uint64_t, ((((uint8_t *)&F.l15) + 32) + 8));
            uint8_t t128 = (t127 > ((uint64_t)0ULL));
            if (t128) {
                {
                    gm_b16_8 t129 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 32));
                    gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&t129));
                    gm_b16_8 t131;
                    GM_ST(void *, (uint8_t *)&t131, (void *)(gm_so514oj5nbhcu));
                    GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(1));
                    gm_f_join_sfknljb((void *)&gm_l27, (void *)NULL, cx, NULL, t130, t131);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 8, gm_srccr53ejqr1dde, 324, 61);
                {
                    gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l27) + 8));
                    gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                    gm_t3ujhu6qph7de t135;
                    gm_f_fail_o80k3rv((void *)&t135, t134);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t135);
                    goto gm_fdone;
                }
            }
            {
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 24, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 0;
                GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), GM_ZERO(gm_b0_1));
                goto gm_fdone;
            }
        }
gm_lb3:;
        {
            {
                gm_b16_8 t136 = gm_l1;
                gm_tnpifv20irnph t137;
                GM_ST(gm_b16_8, ((uint8_t *)&t137 + 0), t136);
                gm_f_format_6ta43n5((void *)&gm_l28, (void *)NULL, cx, NULL, t137);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t138 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
            if (!t138) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_srccr53ejqr1dde, 327, 36);
            {
                gm_b16_8 t139 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
                gm_b16_8 t140 = GM_LD(gm_b16_8, ((uint8_t *)&t139));
                gm_t3ujhu6qph7de t141;
                GM_ST(uint8_t, (uint8_t *)&t141 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t141 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t141 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t141 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t141 + 8), t140);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t141);
                goto gm_fdone;
            }
        }
gm_lb11:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 296, 5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_a7cccd4(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_a7cccd4 F;
    gm_b16_8 gm_l0; /* names */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t7 = gm_l0;
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
                        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t11) + (uintptr_t)t13 * 16));
                        gm_l4 = t14;
                        gm_b16_8 t15 = gm_l4;
                        gm_b16_8 t16 = gm_l1;
                        uint8_t t17 = (gm_eq0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16)));
                        if (t17) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 61, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t18 = gm_l3;
            uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
            uint64_t t21 = r19;
            gm_l3 = t21;
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
extern __attribute__((aligned(4))) void gm_f_fail_o80k3rv(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_t3ujhu6qph7de gm_f_fail_o80k3rv_c(gm_b16_8 a0) { gm_t3ujhu6qph7de gm_r; gm_f_fail_o80k3rv(&gm_r, a0); return gm_r; }
__attribute__((aligned(4))) void gm_f_fetch_l6q1k6p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fetch_l6q1k6p F;
    gm_b16_8 gm_l0; /* args */
    gm_tpmdr2hmdhsqr gm_l1; /* <temporary array> */
    gm_t0kl141g5m2qf gm_l2; /* <temporary array> */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* d */
    gm_b16_8 gm_l8; /* d */
    gm_b16_8 gm_l9; /* dir */
    gm_b16_8 gm_l10; /* e */
    gm_b16_8 gm_l11; /* root */
    gm_tqs5p4n2tq19v gm_l12; /* opts */
    gm_b16_8 gm_l13; /* e */
    GM_STACK_CHECK(&F, 1426u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t77ebp3ogdble gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_th2lqbo77pvsc gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_th2lqbo77pvsc gm_l18;
    gm_th2lqbo77pvsc gm_l19;
    gm_t3ujhu6qph7de gm_l20;
    gm_tv37d3dpt2opu gm_l21;
    gm_t074vrl1hb4f6 gm_l22;
    {
        gm_b16_8 t9 = gm_l0;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_snboa22b96bbc));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(3));
        gm_tpmdr2hmdhsqr t11;
        GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
        gm_l1 = t11;
        void * t12 = (void *)((uint8_t *)&gm_l1);
        uint64_t t13 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t12 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s5fgj0n2t69gl));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(7));
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_snvisme5hp379));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(6));
        gm_t0kl141g5m2qf t17;
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t15);
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t16);
        gm_l2 = t17;
        void * t18 = (void *)((uint8_t *)&gm_l2);
        uint64_t t19 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(((uint8_t *)t18 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(t19));
        gm_t5detta1b4gnk t21;
        gm_f_parse_args_7blm2mp((void *)&t21, (void *)NULL, cx, NULL, t9, t14, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_usage_failure_cgg6v1t((void *)&gm_l14, &t21);
    }
    uint8_t t22 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
    if (t22) {
    } else {
        gm_t3ujhu6qph7de t23 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l14) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t23);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l14) + 8), 120);
    }
    {
        gm_trqfvmjtt4sij t24;
        gm_f_list_epvducg((void *)&t24, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t24;
    }
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_l5 = t25;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t26 = gm_l6;
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t28 = (t26 < t27);
            if (!t28) {
                goto gm_lb1;
            }
            {
                void * t29 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t31 = gm_l6;
                {
                    {
                        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)t29) + (uintptr_t)t31 * 16));
                        gm_l7 = t32;
                        void * t33 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t34 = gm_l7;
                        gm_tftb1odc739t5 t35 = gm_f_push_3ef746b(t33, t34);
                        gm_l15 = t35;
                        uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                        if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srccr53ejqr1dde, 156, 28);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 156, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t37 = gm_l6;
            uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
            uint64_t t40 = r38;
            gm_l6 = t40;
        }
    }
gm_lb1:;
    {
        {
            void * t41 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(gm_snboa22b96bbc));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(3));
            gm_f_value_of_0km7vvp((void *)&gm_l16, t41, t42);
            uint8_t t43 = (uint8_t)((((uint8_t *)&gm_l16))[16] != 0);
            if (!t43) {
                goto gm_lb7;
            }
            gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l16));
            gm_l8 = t44;
            void * t45 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t46 = gm_l8;
            gm_tftb1odc739t5 t47 = gm_f_push_3ef746b(t45, t46);
            gm_l17 = t47;
            uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srccr53ejqr1dde, 157, 35);
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
    uint8_t t50 = (t49 > ((uint64_t)1ULL));
    if (t50) {
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_saqbkgqthogiq));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(29));
        gm_t3ujhu6qph7de t52;
        GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t52 + 8), t51);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t52);
        goto gm_fdone;
    }
    void * t53 = GM_LD(void *, ((uint8_t *)&F.l4));
    uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
    uint8_t t55 = (((uint64_t)0ULL) < t54);
    if (t55) {
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)t53) + (uintptr_t)((uint64_t)0ULL) * 16));
        GM_ST(uint8_t, (uint8_t *)&gm_l18 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l18 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l18 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l18), t56);
        ((uint8_t *)&gm_l18)[16] = 1;
    } else {
        memset(&gm_l18, 0, sizeof gm_l18);
    }
    {
        memmove(((uint8_t *)&gm_l19), ((uint8_t *)&gm_l18), 24);
    }
    uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l19))[16] != 0);
    if (t57) {
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l19));
        gm_l9 = t58;
    } else {
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(1));
        gm_l9 = t59;
    }
    {
        gm_b16_8 t60 = gm_l9;
        gm_f_root_of_cluda11((void *)&gm_l20, (void *)NULL, cx, NULL, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
    if (t61) {
        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
        gm_l11 = t62;
    } else {
        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
        gm_l10 = t63;
        {
            gm_b16_8 t64 = gm_l10;
            gm_t3ujhu6qph7de t65;
            gm_f_fail_o80k3rv((void *)&t65, t64);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t65);
            goto gm_fdone;
        }
    }
    void * t66 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t67;
    GM_ST(void *, (uint8_t *)&t67, (void *)(gm_s5fgj0n2t69gl));
    GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(7));
    uint8_t t68 = gm_f_has_flag_4n9tu40(t66, t67);
    void * t69 = (void *)((uint8_t *)&F.l3);
    gm_b16_8 t70;
    GM_ST(void *, (uint8_t *)&t70, (void *)(gm_snvisme5hp379));
    GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(6));
    uint8_t t71 = gm_f_has_flag_4n9tu40(t69, t70);
    gm_tqs5p4n2tq19v t72;
    GM_ST(uint8_t, ((uint8_t *)&t72 + 0), t68);
    GM_ST(uint8_t, ((uint8_t *)&t72 + 1), t71);
    gm_l12 = t72;
    {
        gm_b16_8 t73 = gm_l11;
        gm_tqs5p4n2tq19v t74 = gm_l12;
        gm_tl572d7579hns t75;
        GM_ST(uint8_t, (uint8_t *)&t75 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t75 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t75 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t75 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t75 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)0ULL));
        gm_f_sync_ht164ep((void *)&gm_l22, (void *)NULL, cx, NULL, t73, t74, &t75, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
    if (t76) {
        memmove(((uint8_t *)&gm_l21), (((uint8_t *)&gm_l22) + 8), 48);
    } else {
        gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
        gm_l13 = t77;
        {
            gm_b16_8 t78 = gm_l13;
            gm_t3ujhu6qph7de t79;
            gm_f_fail_o80k3rv((void *)&t79, t78);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t79);
            goto gm_fdone;
        }
    }
    {
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_0m0ntf2(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_0m0ntf2 F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_toe5fmr3vvf4d gm_l2; /* e */
    GM_STACK_CHECK(&F, 309u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tibupt4dmcqai gm_l3;
    gm_tabb710d9mq2e gm_l4;
    {
        {
            {
                memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l0), 32);
            }
            uint8_t t4 = (uint8_t)!((((uint8_t *)&gm_l1))[0] != 0);
            if (t4) {
                memmove(((uint8_t *)&gm_l3), (((uint8_t *)&gm_l1) + 8), 24);
            } else {
                gm_toe5fmr3vvf4d t5 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l1) + 8));
                gm_l2 = t5;
                gm_toe5fmr3vvf4d t6 = gm_l2;
                uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
                uint8_t t8 = (uint8_t)((t7 == ((uint8_t)0ULL)));
                gm_toe5fmr3vvf4d t9;
                GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)0ULL));
                if (t8) {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_new_package_hf828qc *)((&F.h)->up))->l12), ((uint8_t)1));
                } else {
                    {
                        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_new_package_hf828qc *)((&F.h)->up))->l5));
                        gm_toe5fmr3vvf4d t11 = gm_l2;
                        gm_b16_8 t12 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t11);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6j6smj7qoctu t13;
                        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 0), t10);
                        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 16), t12);
                        gm_f_format_is5aam9((void *)&gm_l4, (void *)NULL, cx, NULL, &t13);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_srccr53ejqr1dde, 362, 34);
                    {
                        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
                        gm_t3ujhu6qph7de t17;
                        gm_f_fail_o80k3rv((void *)&t17, t16);
                        gm_t715ajl6o1ipo t18;
                        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
                        ((uint8_t *)&t18)[0] = 1;
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&t18 + 8), t17);
                        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
                        ((struct gm_fr_new_package_hf828qc *)((&F.h)->up))->ret = t18;
                        gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t19;
                        goto gm_lad;
                    }
                }
                gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
                goto gm_lad;
            }
            {
            }
            GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_new_package_hf828qc *)((&F.h)->up))->l11), ((uint8_t)1));
            gm_unw.kind = GM_BREAK; gm_unw.frame = inst->with_frame; gm_unw.label = inst->with_label; gm_unw.is_err = 0;
            goto gm_lad;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 357, 9);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_flag_4n9tu40(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_flag_4n9tu40 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* f */
    GM_STACK_CHECK_SHALLOW(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 80));
    gm_l2 = t8;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t14 = gm_l3;
                {
                    {
                        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t12) + (uintptr_t)t14 * 16));
                        gm_l4 = t15;
                        gm_b16_8 t16 = gm_l4;
                        gm_b16_8 t17 = gm_l1;
                        uint8_t t18 = (gm_eq0iqiubfm7e(((uint8_t *)&t16), ((uint8_t *)&t17)));
                        if (t18) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 54, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t19 = gm_l3;
            uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
            uint64_t t22 = r20;
            gm_l3 = t22;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_package_command_ufjt5rp(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_package_command_ufjt5rp F;
    gm_b16_8 gm_l0; /* command */
    GM_STACK_CHECK_SHALLOW(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6pnfrub0rj1t));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s3lbmpmt95hvh));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(6));
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_s4o70d89183c2));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(3));
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sqljkj8r253om));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(6));
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sivh46n0690a4));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(3));
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_suass5sid4b6e));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(4));
    gm_b16_8 t9;
    GM_ST(void *, (uint8_t *)&t9, (void *)(gm_so9n4mmd960c7));
    GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(5));
    gm_th2d9c7aju8hp t10;
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 16), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 32), t5);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 48), t6);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 64), t7);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 80), t8);
    GM_ST(gm_b16_8, ((uint8_t *)&t10 + 96), t9);
    uint8_t t11 = 0;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 0, ((uint8_t *)&t2))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 16, ((uint8_t *)&t2))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 32, ((uint8_t *)&t2))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 48, ((uint8_t *)&t2))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 64, ((uint8_t *)&t2))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 80, ((uint8_t *)&t2))) t11 = 1;
    if (gm_eq0iqiubfm7e(((uint8_t *)&t10) + 96, ((uint8_t *)&t2))) t11 = 1;
    uint8_t t12 = (uint8_t)(t11);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_new_package_hf828qc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1) {
    struct gm_fr_new_package_hf828qc F;
    gm_b16_8 gm_l0; /* args */
    uint8_t gm_l1; /* create */
    gm_t0kl141g5m2qf gm_l2; /* <temporary array> */
    gm_b0_8 gm_l3; /* <temporary array> */
    uint8_t gm_l6; /* git_init */
    gm_b16_8 gm_l7; /* v */
    gm_b16_8 gm_l8; /* n */
    gm_b16_8 gm_l9; /* problem */
    uint8_t gm_l10; /* created_here */
    gm_b16_8 gm_l13; /* <tmp> */
    gm_toe5fmr3vvf4d gm_l14; /* e */
    gm_th2lqbo77pvsc gm_l15; /* derived_from */
    gm_b16_8 gm_l16; /* name */
    gm_b16_8 gm_l17; /* n */
    gm_b16_8 gm_l18; /* abs */
    gm_b16_8 gm_l19; /* dir_name */
    gm_b16_8 gm_l20; /* problem */
    gm_b16_8 gm_l21; /* e */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_b16_8 gm_l25; /* f */
    uint8_t gm_l26; /* existing_manifest */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_b16_8 gm_l29; /* f */
    gm_b16_8 gm_l30; /* d */
    GM_STACK_CHECK(&F, 3157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t77ebp3ogdble gm_l31;
    gm_th2lqbo77pvsc gm_l32;
    gm_tabb710d9mq2e gm_l33;
    gm_th2lqbo77pvsc gm_l34;
    gm_th2lqbo77pvsc gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_ttln0d0hd70en gm_l37;
    gm_tabb710d9mq2e gm_l38;
    gm_tabb710d9mq2e gm_l39;
    gm_th2lqbo77pvsc gm_l40;
    gm_th2lqbo77pvsc gm_l41;
    gm_tabb710d9mq2e gm_l42;
    gm_th2lqbo77pvsc gm_l43;
    gm_th2lqbo77pvsc gm_l44;
    gm_tabb710d9mq2e gm_l45;
    gm_tcl27d4j9lskg gm_l46;
    gm_tabb710d9mq2e gm_l47;
    {
        gm_b16_8 t29 = gm_l0;
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_sius9pki5u4e3));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(4));
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_s30cnsbbn0c4o));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(3));
        gm_t0kl141g5m2qf t32;
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 0), t30);
        GM_ST(gm_b16_8, ((uint8_t *)&t32 + 16), t31);
        gm_l2 = t32;
        void * t33 = (void *)((uint8_t *)&gm_l2);
        uint64_t t34 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(((uint8_t *)t33 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(t34));
        void * t36 = (void *)((uint8_t *)&gm_l3);
        uint64_t t37 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(((uint8_t *)t36 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(t37));
        gm_t5detta1b4gnk t39;
        gm_f_parse_args_7blm2mp((void *)&t39, (void *)NULL, cx, NULL, t29, t35, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_usage_failure_cgg6v1t((void *)&gm_l31, &t39);
        uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
        if (t40) {
        } else {
            gm_t3ujhu6qph7de t41 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l31) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t41);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l31) + 8), 120);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(1));
        F.l5 = t42;
        uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
        uint8_t t44 = ((t43 == ((uint64_t)1ULL)));
        if (t44) {
            void * t45 = GM_LD(void *, ((uint8_t *)&F.l4));
            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t46, gm_srccr53ejqr1dde, 335, 15);
            gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)t45) + (uintptr_t)((uint64_t)0ULL) * 16));
            F.l5 = t47;
        } else {
            uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
            uint8_t t49 = ((t48 == ((uint64_t)0ULL)));
            if (t49) {
                uint8_t t50 = gm_l1;
                if (t50) {
                    gm_b16_8 t51;
                    GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sgu3j1te831tl));
                    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(23));
                    gm_t3ujhu6qph7de t52;
                    GM_ST(uint8_t, (uint8_t *)&t52 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t52 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t52 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t52 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&t52 + 8), t51);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t52);
                    goto gm_fdone;
                }
            } else {
                gm_b16_8 t53;
                GM_ST(void *, (uint8_t *)&t53, (void *)(gm_saqbkgqthogiq));
                GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(29));
                gm_t3ujhu6qph7de t54;
                GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t54 + 8), t53);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t54);
                goto gm_fdone;
            }
        }
        gm_l6 = ((uint8_t)1);
        {
            {
                void * t55 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t56;
                GM_ST(void *, (uint8_t *)&t56, (void *)(gm_s30cnsbbn0c4o));
                GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(3));
                gm_f_value_of_0km7vvp((void *)&gm_l32, t55, t56);
                uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l32))[16] != 0);
                if (!t57) {
                    goto gm_lb2;
                }
                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l32));
                gm_l7 = t58;
                {
                    {
                        gm_b16_8 t59 = gm_l7;
                        gm_b16_8 t60;
                        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s0k6ioi39f878));
                        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(3));
                        uint8_t t61 = (gm_eq0iqiubfm7e(((uint8_t *)&t59), ((uint8_t *)&t60)));
                        if (!t61) {
                            goto gm_lb4;
                        }
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        gm_b16_8 t62 = gm_l7;
                        gm_b16_8 t63;
                        GM_ST(void *, (uint8_t *)&t63, (void *)(gm_s3v0kr2fdpfld));
                        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(4));
                        uint8_t t64 = (gm_eq0iqiubfm7e(((uint8_t *)&t62), ((uint8_t *)&t63)));
                        if (!t64) {
                            goto gm_lb5;
                        }
                        gm_l6 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb5:;
                    {
                        {
                            gm_b16_8 t65 = gm_l7;
                            gm_tnpifv20irnph t66;
                            GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
                            gm_f_format_e2683lk((void *)&gm_l33, (void *)NULL, cx, NULL, t66);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                        if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srccr53ejqr1dde, 346, 40);
                        {
                            gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l33) + 8));
                            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                            gm_t3ujhu6qph7de t70;
                            GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&t70 + 8), t69);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t70);
                            goto gm_fdone;
                        }
                    }
gm_lb6:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 343, 9);
                }
gm_lb3:;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        {
            {
                void * t71 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t72;
                GM_ST(void *, (uint8_t *)&t72, (void *)(gm_sius9pki5u4e3));
                GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(4));
                gm_f_value_of_0km7vvp((void *)&gm_l34, t71, t72);
                uint8_t t73 = (uint8_t)((((uint8_t *)&gm_l34))[16] != 0);
                if (!t73) {
                    goto gm_lb8;
                }
                gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l34));
                gm_l8 = t74;
                {
                    {
                        {
                            gm_b16_8 t75 = gm_l8;
                            gm_b16_8 t76;
                            GM_ST(void *, (uint8_t *)&t76, (void *)(gm_snqvo5bgc2fch));
                            GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(7));
                            gm_f_name_problem_6r4mp11((void *)&gm_l35, (void *)NULL, cx, NULL, t75, t76);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t77 = (uint8_t)((((uint8_t *)&gm_l35))[16] != 0);
                        if (!t77) {
                            goto gm_lb10;
                        }
                        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l35));
                        gm_l9 = t78;
                        {
                            gm_b16_8 t79 = gm_l9;
                            gm_t3ujhu6qph7de t80;
                            gm_f_fail_o80k3rv((void *)&t80, t79);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t80);
                            goto gm_fdone;
                        }
                    }
gm_lb10:;
                }
gm_lb9:;
                goto gm_lb7;
            }
gm_lb8:;
        }
gm_lb7:;
        gm_l10 = ((uint8_t)0);
        uint8_t t81 = gm_l1;
        if (t81) {
            F.l11 = ((uint8_t)0);
            F.l12 = ((uint8_t)0);
            gm_b16_8 t82 = F.l5;
            gm_l13 = t82;
            {
                gm_node wn83 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hl87701o8eo1, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
                gm_b16_8 t86 = gm_l13;
                gm_b0_1 t87 = gm_f_entries_44o5n9f_suo216n((void *)NULL, (&wn83), NULL, t86);
                if (gm_unw.kind) {
                    goto gm_ll85;
                }
            }
            goto gm_ls88;
gm_ll85:;
            if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we84; }
            goto gm_lad;
gm_ls88:;
gm_lb11:;
we84:;
            uint8_t t89 = F.l11;
            if (t89) {
                {
                    gm_b16_8 t90 = F.l5;
                    gm_tnpifv20irnph t91;
                    GM_ST(gm_b16_8, ((uint8_t *)&t91 + 0), t90);
                    gm_f_format_7lopc28((void *)&gm_l36, (void *)NULL, cx, NULL, t91);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_srccr53ejqr1dde, 369, 37);
                {
                    gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                    gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                    gm_t3ujhu6qph7de t95;
                    gm_f_fail_o80k3rv((void *)&t95, t94);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t95);
                    goto gm_fdone;
                }
            }
            uint8_t t96 = F.l12;
            if (t96) {
                gm_b16_8 t97 = F.l5;
                gm_ttln0d0hd70en t98 = gm_f_make_dir_all_q6phjan((void *)NULL, cx, NULL, t97);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l37 = t98;
                uint8_t t99 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                if (t99) {
                } else {
                    gm_toe5fmr3vvf4d t100 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l37) + 4));
                    gm_l14 = t100;
                    {
                        gm_b16_8 t101 = F.l5;
                        gm_toe5fmr3vvf4d t102 = gm_l14;
                        gm_b16_8 t103 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t102);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6j6smj7qoctu t104;
                        GM_ST(gm_b16_8, ((uint8_t *)&t104 + 0), t101);
                        GM_ST(gm_b16_8, ((uint8_t *)&t104 + 16), t103);
                        gm_f_format_5uun4vl((void *)&gm_l38, (void *)NULL, cx, NULL, &t104);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                    if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 8, gm_srccr53ejqr1dde, 371, 59);
                    {
                        gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l38) + 8));
                        gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                        gm_t3ujhu6qph7de t108;
                        gm_f_fail_o80k3rv((void *)&t108, t107);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t108);
                        goto gm_fdone;
                    }
                }
                gm_l10 = ((uint8_t)1);
            }
        } else {
            gm_b16_8 t109 = F.l5;
            uint8_t t110 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t111 = (uint8_t)!t110;
            if (t111) {
                {
                    gm_b16_8 t112 = F.l5;
                    gm_tnpifv20irnph t113;
                    GM_ST(gm_b16_8, ((uint8_t *)&t113 + 0), t112);
                    gm_f_format_tffhbfn((void *)&gm_l39, (void *)NULL, cx, NULL, t113);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t114 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                if (!t114) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_srccr53ejqr1dde, 375, 22);
                {
                    gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l39) + 8));
                    gm_b16_8 t116 = GM_LD(gm_b16_8, ((uint8_t *)&t115));
                    gm_t3ujhu6qph7de t117;
                    gm_f_fail_o80k3rv((void *)&t117, t116);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t117);
                    goto gm_fdone;
                }
            }
        }
    }
    {
        memset(&gm_l15, 0, sizeof gm_l15);
        gm_b16_8 t118;
        GM_ST(void *, (uint8_t *)&t118, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(0));
        gm_l16 = t118;
        {
            {
                void * t119 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t120;
                GM_ST(void *, (uint8_t *)&t120, (void *)(gm_sius9pki5u4e3));
                GM_ST(uintptr_t, (uint8_t *)&t120 + 8, (uintptr_t)(4));
                gm_f_value_of_0km7vvp((void *)&gm_l40, t119, t120);
                uint8_t t121 = (uint8_t)((((uint8_t *)&gm_l40))[16] != 0);
                if (!t121) {
                    goto gm_lb13;
                }
                gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l40));
                gm_l17 = t122;
                gm_b16_8 t123 = gm_l17;
                gm_l16 = t123;
                goto gm_lb12;
            }
gm_lb13:;
            {
                gm_b16_8 t124 = F.l5;
                gm_f_canonicalize_epo7cln((void *)&gm_l41, (void *)NULL, cx, NULL, t124);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t125 = (uint8_t)((((uint8_t *)&gm_l41))[16] != 0);
            if (t125) {
                gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l41));
                gm_l18 = t126;
            } else {
                {
                    gm_b16_8 t127 = F.l5;
                    gm_tnpifv20irnph t128;
                    GM_ST(gm_b16_8, ((uint8_t *)&t128 + 0), t127);
                    gm_f_format_14t1s38((void *)&gm_l42, (void *)NULL, cx, NULL, t128);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t129 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
                if (!t129) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 8, gm_srccr53ejqr1dde, 383, 26);
                {
                    gm_b16_8 t130 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l42) + 8));
                    gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&t130));
                    gm_t3ujhu6qph7de t132;
                    gm_f_fail_o80k3rv((void *)&t132, t131);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                    GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t132);
                    goto gm_fdone;
                }
            }
            {
                gm_b16_8 t133 = gm_l18;
                gm_f_file_name_em7v8lv((void *)&gm_l43, (void *)NULL, cx, NULL, t133);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t134 = (uint8_t)((((uint8_t *)&gm_l43))[16] != 0);
            if (t134) {
                gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l43));
                gm_l19 = t135;
            } else {
                gm_b16_8 t136;
                GM_ST(void *, (uint8_t *)&t136, (void *)(gm_sr4qbrk6tlgaa));
                GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(0));
                gm_l19 = t136;
            }
            gm_b16_8 t137 = gm_l19;
            gm_b16_8 t138 = gm_f_sanitize_name_3i9pjsp((void *)NULL, cx, NULL, t137);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l16 = t138;
            {
                {
                    {
                        gm_b16_8 t139 = gm_l16;
                        gm_b16_8 t140;
                        GM_ST(void *, (uint8_t *)&t140, (void *)(gm_snqvo5bgc2fch));
                        GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(7));
                        gm_f_name_problem_6r4mp11((void *)&gm_l44, (void *)NULL, cx, NULL, t139, t140);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t141 = (uint8_t)((((uint8_t *)&gm_l44))[16] != 0);
                    if (!t141) {
                        goto gm_lb15;
                    }
                    gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l44));
                    gm_l20 = t142;
                    uint8_t t143 = gm_l10;
                    if (t143) {
                        gm_b16_8 t144 = F.l5;
                        gm_node *pn145 = gm_find(cx, 2126215853);
                        if (!pn145) gm_trap("no handler installed for effect `Fs`", gm_srccr53ejqr1dde, 388, 35);
                        gm_ttln0d0hd70en t146;
                        t146 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn145->clauses[9])(pn145->env, pn145->hctx, pn145, t144);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_b16_8 t147 = gm_l19;
                        gm_b16_8 t148 = gm_l16;
                        gm_b16_8 t149 = gm_l20;
                        gm_tfeab279c16rr t150;
                        GM_ST(gm_b16_8, ((uint8_t *)&t150 + 0), t147);
                        GM_ST(gm_b16_8, ((uint8_t *)&t150 + 16), t148);
                        GM_ST(gm_b16_8, ((uint8_t *)&t150 + 32), t149);
                        gm_f_format_ujusd29((void *)&gm_l45, (void *)NULL, cx, NULL, &t150);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t151 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                    if (!t151) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 8, gm_srccr53ejqr1dde, 389, 26);
                    {
                        gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                        gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
                        gm_t3ujhu6qph7de t154;
                        gm_f_fail_o80k3rv((void *)&t154, t153);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t154);
                        goto gm_fdone;
                    }
                }
gm_lb15:;
            }
gm_lb14:;
            gm_b16_8 t155 = gm_l16;
            gm_b16_8 t156 = gm_l19;
            uint8_t t157 = (!gm_eq0iqiubfm7e(((uint8_t *)&t155), ((uint8_t *)&t156)));
            if (t157) {
                gm_b16_8 t158 = gm_l19;
                GM_ST(uint8_t, (uint8_t *)&gm_l15 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l15 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l15 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&gm_l15), t158);
                ((uint8_t *)&gm_l15)[16] = 1;
            }
        }
gm_lb12:;
    }
    {
        gm_b16_8 t159 = F.l5;
        gm_b16_8 t160 = gm_l16;
        uint8_t t161 = gm_l6;
        gm_f_scaffold_ansfc6m((void *)&gm_l46, (void *)NULL, cx, NULL, t159, t160, t161);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t162 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
        if (t162) {
            memmove(((uint8_t *)&F.l22), (((uint8_t *)&gm_l46) + 8), 88);
        } else {
            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
            gm_l21 = t163;
            {
                gm_b16_8 t164 = gm_l21;
                gm_t3ujhu6qph7de t165;
                gm_f_fail_o80k3rv((void *)&t165, t164);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t165);
                goto gm_fdone;
            }
        }
        gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 40));
        gm_l23 = t166;
        gm_l24 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t168 = gm_l24;
                uint64_t t169 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                uint8_t t170 = (t168 < t169);
                if (!t170) {
                    goto gm_lb16;
                }
                {
                    void * t171 = GM_LD(void *, ((uint8_t *)&gm_l23));
                    uint64_t t172 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                    uint64_t t173 = gm_l24;
                    {
                        {
                            gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)t171) + (uintptr_t)t173 * 16));
                            gm_l25 = t174;
                            gm_b16_8 t175 = gm_l25;
                            if (gm_pf1m4b8q4e78(cx, gm_srgao1ir81l50, 13, gm_srccr53ejqr1dde, 394, 37)) {
                                goto gm_ll167;
                            }
                            if ((GM_LD(uintptr_t, ((uint8_t *)&t175) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t175)), GM_LD(uintptr_t, ((uint8_t *)&t175) + 8), gm_srccr53ejqr1dde, 394, 37))) {
                                goto gm_ll167;
                            }
                            if (gm_pf1m4b8q4e78(cx, gm_slrtg04n254p4, 18, gm_srccr53ejqr1dde, 394, 37)) {
                                goto gm_ll167;
                            }
                            goto gm_lb19;
                        }
gm_lb20:;
                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 394, 5);
                    }
gm_lb19:;
                }
gm_lb18:;
                uint64_t t176 = gm_l24;
                uint64_t r177; bool o178 = __builtin_add_overflow(t176, ((uint64_t)1ULL), &r177);
                uint64_t t179 = r177;
                gm_l24 = t179;
            }
        }
        goto gm_ls180;
gm_ll167:;
        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb16; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb18; }
        }
        goto gm_lad;
gm_ls180:;
gm_lb16:;
        uint64_t t181 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 8));
        uint8_t t182 = ((t181 == ((uint64_t)0ULL)));
        if (t182) {
            gm_b16_8 t183 = F.l5;
            if (gm_pf1m4b8q4e78(cx, gm_s7mqv7hf4fso0, 26, gm_srccr53ejqr1dde, 396, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t183) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t183)), GM_LD(uintptr_t, ((uint8_t *)&t183) + 8), gm_srccr53ejqr1dde, 396, 9))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 396, 9)) {
                goto gm_lad;
            }
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 0;
            GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), GM_ZERO(gm_b0_1));
            goto gm_fdone;
        }
        gm_l26 = ((uint8_t)0);
        gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 40));
        gm_l27 = t184;
        gm_l28 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t186 = gm_l28;
                uint64_t t187 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                uint8_t t188 = (t186 < t187);
                if (!t188) {
                    goto gm_lb21;
                }
                {
                    void * t189 = GM_LD(void *, ((uint8_t *)&gm_l27));
                    uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                    uint64_t t191 = gm_l28;
                    {
                        {
                            gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)t189) + (uintptr_t)t191 * 16));
                            gm_l29 = t192;
                            gm_b16_8 t193 = gm_l29;
                            gm_b16_8 t194;
                            GM_ST(void *, (uint8_t *)&t194, (void *)(gm_se24c7hpp401n));
                            GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(10));
                            uint8_t t195 = (gm_eq0iqiubfm7e(((uint8_t *)&t193), ((uint8_t *)&t194)));
                            if (t195) {
                                gm_l26 = ((uint8_t)1);
                            }
                            goto gm_lb24;
                        }
gm_lb25:;
                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 400, 5);
                    }
gm_lb24:;
                }
gm_lb23:;
                uint64_t t196 = gm_l28;
                uint64_t r197; bool o198 = __builtin_add_overflow(t196, ((uint64_t)1ULL), &r197);
                uint64_t t199 = r197;
                gm_l28 = t199;
            }
        }
gm_lb21:;
    }
    {
        uint8_t t200 = gm_l26;
        if (t200) {
            gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
            gm_b16_8 t202 = GM_LD(gm_b16_8, ((uint8_t *)&t201));
            gm_b16_8 t203;
            GM_ST(void *, (uint8_t *)&t203, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(2));
            gm_f_join_sfknljb((void *)&gm_l47, (void *)NULL, cx, NULL, t202, t203);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t204 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
            if (!t204) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 8, gm_srccr53ejqr1dde, 404, 81);
            gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l47) + 8));
            gm_b16_8 t206 = F.l5;
            if (gm_pf1m4b8q4e78(cx, gm_s2n3d0ogcjggg, 13, gm_srccr53ejqr1dde, 404, 9)) {
                goto gm_lad;
            }
            if (gm_pf70j0q3sgt1(cx, &gm_tybihgc69jv3, ((uint8_t *)&t205), gm_srccr53ejqr1dde, 404, 9)) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_s7hkl5mqa15gm, 4, gm_srccr53ejqr1dde, 404, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t206) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t206)), GM_LD(uintptr_t, ((uint8_t *)&t206) + 8), gm_srccr53ejqr1dde, 404, 9))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_snmm68ee1rq48, 40, gm_srccr53ejqr1dde, 404, 9)) {
                goto gm_lad;
            }
        } else {
            gm_b16_8 t207 = gm_l16;
            gm_b16_8 t208 = F.l5;
            if (gm_pf1m4b8q4e78(cx, gm_sbvsnnbg6vc88, 22, gm_srccr53ejqr1dde, 406, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t207) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t207)), GM_LD(uintptr_t, ((uint8_t *)&t207) + 8), gm_srccr53ejqr1dde, 406, 9))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_sgc7crh43q9tj, 5, gm_srccr53ejqr1dde, 406, 9)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t208) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t208)), GM_LD(uintptr_t, ((uint8_t *)&t208) + 8), gm_srccr53ejqr1dde, 406, 9))) {
                goto gm_lad;
            }
            if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 406, 9)) {
                goto gm_lad;
            }
        }
        {
            {
                uint8_t t209 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                if (!t209) {
                    goto gm_lb27;
                }
                gm_b16_8 t210 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                gm_l30 = t210;
                gm_b16_8 t211 = gm_l30;
                if (gm_pf1m4b8q4e78(cx, gm_se48mcej0nimj, 15, gm_srccr53ejqr1dde, 409, 9)) {
                    goto gm_lad;
                }
                if ((GM_LD(uintptr_t, ((uint8_t *)&t211) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t211)), GM_LD(uintptr_t, ((uint8_t *)&t211) + 8), gm_srccr53ejqr1dde, 409, 9))) {
                    goto gm_lad;
                }
                if (gm_pf1m4b8q4e78(cx, gm_sbcpd47bv0ru9, 62, gm_srccr53ejqr1dde, 409, 9)) {
                    goto gm_lad;
                }
                goto gm_lb26;
            }
gm_lb27:;
        }
gm_lb26:;
        uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&F.l22) + 80));
        if (t212) {
            if (gm_pf1m4b8q4e78(cx, gm_se39cci44d9rt, 34, gm_srccr53ejqr1dde, 411, 33)) {
                goto gm_lad;
            }
        }
        gm_b16_8 t213 = F.l5;
        gm_b16_8 t214 = F.l5;
        if (gm_pf1m4b8q4e78(cx, gm_sq99f4u8gmi6n, 27, gm_srccr53ejqr1dde, 412, 5)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t213) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t213)), GM_LD(uintptr_t, ((uint8_t *)&t213) + 8), gm_srccr53ejqr1dde, 412, 5))) {
            goto gm_lad;
        }
        if (gm_pf1m4b8q4e78(cx, gm_sb9fu6ufbt9g1, 16, gm_srccr53ejqr1dde, 412, 5)) {
            goto gm_lad;
        }
        if ((GM_LD(uintptr_t, ((uint8_t *)&t214) + 8) != 0 && gm_pf1m4b8q4e78(cx, GM_LD(void *, ((uint8_t *)&t214)), GM_LD(uintptr_t, ((uint8_t *)&t214) + 8), gm_srccr53ejqr1dde, 412, 5))) {
            goto gm_lad;
        }
        if (gm_pf1m4b8q4e78(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 412, 5)) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
inline __attribute__((aligned(4))) void gm_f_newest_version_dsisujv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_newest_version_dsisujv F;
    gm_b16_8 gm_l0; /* url */
    gm_b16_8 gm_l1; /* e */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_b16_8 gm_l6; /* t */
    GM_STACK_CHECK(&F, 498u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t470aqkia5212 gm_l9;
    gm_tom1ad5bcqgi1 gm_l10;
    {
        gm_b16_8 t9 = gm_l0;
        gm_f_remote_tags_rssa7s7((void *)&gm_l9, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t10) {
        memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l9) + 8), 40);
    } else {
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_l1 = t11;
        {
            gm_b16_8 t12 = gm_l1;
            gm_t3ujhu6qph7de t13;
            gm_f_fail_o80k3rv((void *)&t13, t12);
            memset(&(*(gm_tis0irsd01rfp *)gm_out), 0, sizeof (*(gm_tis0irsd01rfp *)gm_out));
            ((uint8_t *)&(*(gm_tis0irsd01rfp *)gm_out))[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_tis0irsd01rfp *)gm_out) + 8), t13);
            goto gm_fdone;
        }
    }
    {
        memset(&F.l3, 0, sizeof F.l3);
    }
    gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    gm_l4 = t14;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t15 = gm_l5;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t20 = gm_l5;
                {
                    {
                        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t18) + (uintptr_t)t20 * 16));
                        gm_l6 = t21;
                        {
                            gm_b16_8 t22 = gm_l6;
                            gm_f_tag_version_aloeltn((void *)&gm_l10, (void *)NULL, cx, NULL, t22);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t23 = (uint8_t)((((uint8_t *)&gm_l10))[56] != 0);
                        if (t23) {
                            memmove(((uint8_t *)&F.l7), ((uint8_t *)&gm_l10), 56);
                        } else {
                            goto gm_lb3;
                        }
                        uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)&F.l7) + 24) + 8));
                        uint8_t t25 = (t24 > ((uint64_t)0ULL));
                        if (t25) {
                            goto gm_lb3;
                        }
                        {
                            {
                                uint8_t t26 = (uint8_t)((((uint8_t *)&F.l3))[56] != 0);
                                if (!t26) {
                                    goto gm_lb7;
                                }
                                {
                                    memmove(((uint8_t *)&F.l8), ((uint8_t *)&F.l3), 56);
                                }
                                {
                                    int32_t t27 = gm_f_cmp_version_0oibvro(&(*(gm_tu1089hhijadf *)((uint8_t *)&F.l7)), &(*(gm_tu1089hhijadf *)((uint8_t *)&F.l8)));
                                    uint8_t t28 = (t27 < ((int32_t)0LL));
                                    if (t28) {
                                        goto gm_lb3;
                                    }
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        {
                            gm_tu1089hhijadf t29 = GM_LD(gm_tu1089hhijadf, ((uint8_t *)&F.l7));
                            GM_ST(uint8_t, (uint8_t *)&F.l3 + 57, 0);
                            GM_ST(uint16_t, (uint8_t *)&F.l3 + 58, 0);
                            GM_ST(uint32_t, (uint8_t *)&F.l3 + 60, 0);
                            GM_ST(gm_tu1089hhijadf, ((uint8_t *)&F.l3), t29);
                            ((uint8_t *)&F.l3)[56] = 1;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 261, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t30 = gm_l5;
            uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
            uint64_t t33 = r31;
            gm_l5 = t33;
        }
    }
gm_lb1:;
    {
        gm_tom1ad5bcqgi1 t34 = GM_LD(gm_tom1ad5bcqgi1, ((uint8_t *)&F.l3));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tis0irsd01rfp *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tis0irsd01rfp *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tis0irsd01rfp *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tis0irsd01rfp *)gm_out))[0] = 0;
        GM_ST(gm_tom1ad5bcqgi1, ((uint8_t *)&(*(gm_tis0irsd01rfp *)gm_out) + 8), t34);
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
__attribute__((aligned(4))) void gm_f_parse_args_7blm2mp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_parse_args_7blm2mp F;
    gm_b16_8 gm_l0; /* args */
    gm_b16_8 gm_l1; /* value_opts */
    gm_b16_8 gm_l2; /* flag_opts */
    uint64_t gm_l4; /* i */
    gm_b16_8 gm_l5; /* a */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* rest */
    gm_b16_8 gm_l9; /* opt */
    gm_b16_8 gm_l10; /* name */
    gm_th2lqbo77pvsc gm_l11; /* inline */
    gm_t6j6smj7qoctu gm_l12; /* split */
    gm_b16_8 gm_l13; /* v */
    GM_STACK_CHECK(&F, 1941u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l14;
    gm_th2lqbo77pvsc gm_l15;
    gm_ttu09lij36f8c gm_l16;
    uint8_t gm_l17;
    gm_tabb710d9mq2e gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tabb710d9mq2e gm_l20;
    uint8_t gm_l21;
    gm_th2lqbo77pvsc gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tabb710d9mq2e gm_l25;
    gm_tabb710d9mq2e gm_l26;
    gm_tftb1odc739t5 gm_l27;
    {
        gm_trqfvmjtt4sij t24;
        gm_f_list_epvducg((void *)&t24, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t25;
        gm_f_list_g9fqnp2((void *)&t25, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t26;
        gm_f_list_epvducg((void *)&t26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tr8dvlu9ik4gj t27;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t27 + 0), t24);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t27 + 40), t25);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t27 + 80), t26);
        F.l3 = t27;
    }
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t28 = gm_l4;
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t30 = (t28 < t29);
            if (!t30) {
                goto gm_lb1;
            }
            {
                void * t31 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t33 = gm_l4;
                gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t31) + (uintptr_t)t33 * 16));
                gm_l5 = t34;
                uint64_t t35 = gm_l4;
                uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                if (o37) gm_trap("integer overflow in `+`", gm_srccr53ejqr1dde, 72, 9);
                uint64_t t38 = r36;
                gm_l4 = t38;
                gm_b16_8 t39 = gm_l5;
                gm_b16_8 t40;
                GM_ST(void *, (uint8_t *)&t40, (void *)(gm_s9pknf9g05okt));
                GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(2));
                uint8_t t41 = (gm_eq0iqiubfm7e(((uint8_t *)&t39), ((uint8_t *)&t40)));
                if (t41) {
                    void * t42 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint64_t t44 = gm_l4;
                    uint64_t t45 = (uintptr_t)t44;
                    uint64_t t46 = (uintptr_t)t43;
                    if (t45 > t46 || t46 > (uintptr_t)t43) gm_trap_slice(t45, t46, t43, gm_srccr53ejqr1dde, 75, 25);
                    uint64_t t47 = ((uintptr_t)t43) - (uintptr_t)t44;
                    gm_b16_8 t48;
                    GM_ST(void *, (uint8_t *)&t48, (void *)(((uint8_t *)t42 + (uintptr_t)t44 * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t48 + 8, (uintptr_t)(t47));
                    gm_l6 = t48;
                    gm_l7 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t49 = gm_l7;
                            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint8_t t51 = (t49 < t50);
                            if (!t51) {
                                goto gm_lb4;
                            }
                            {
                                void * t52 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint64_t t54 = gm_l7;
                                {
                                    {
                                        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)t52) + (uintptr_t)t54 * 16));
                                        gm_l8 = t55;
                                        void * t56 = (void *)((uint8_t *)&F.l3);
                                        gm_b16_8 t57 = gm_l8;
                                        gm_tftb1odc739t5 t58 = gm_f_push_3ef746b(t56, t57);
                                        gm_l14 = t58;
                                        uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                                        if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srccr53ejqr1dde, 75, 37);
                                        goto gm_lb7;
                                    }
gm_lb8:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 75, 13);
                                }
gm_lb7:;
                            }
gm_lb6:;
                            uint64_t t60 = gm_l7;
                            uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                            uint64_t t63 = r61;
                            gm_l7 = t63;
                        }
                    }
gm_lb4:;
                    goto gm_lb1;
                }
                {
                    {
                        {
                            gm_b16_8 t64 = gm_l5;
                            gm_b16_8 t65;
                            GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s9pknf9g05okt));
                            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(2));
                            gm_f_strip_prefix_b4949s8((void *)&gm_l15, t64, t65);
                        }
                        uint8_t t66 = (uint8_t)((((uint8_t *)&gm_l15))[16] != 0);
                        if (!t66) {
                            goto gm_lb10;
                        }
                        gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l15));
                        gm_l9 = t67;
                        gm_b16_8 t68 = gm_l9;
                        gm_l10 = t68;
                        {
                            memset(&gm_l11, 0, sizeof gm_l11);
                        }
                        {
                            {
                                {
                                    gm_b16_8 t69 = gm_l9;
                                    gm_b16_8 t70;
                                    GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sh0j2ggs1vvoa));
                                    GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(1));
                                    gm_f_split_once_mqcu1ce((void *)&gm_l16, t69, t70);
                                }
                                uint8_t t71 = (uint8_t)((((uint8_t *)&gm_l16))[32] != 0);
                                if (!t71) {
                                    goto gm_lb12;
                                }
                                {
                                    memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l16), 32);
                                }
                                gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
                                gm_l10 = t72;
                                {
                                    gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 16));
                                    GM_ST(uint8_t, (uint8_t *)&gm_l11 + 17, 0);
                                    GM_ST(uint16_t, (uint8_t *)&gm_l11 + 18, 0);
                                    GM_ST(uint32_t, (uint8_t *)&gm_l11 + 20, 0);
                                    GM_ST(gm_b16_8, ((uint8_t *)&gm_l11), t73);
                                    ((uint8_t *)&gm_l11)[16] = 1;
                                }
                                goto gm_lb11;
                            }
gm_lb12:;
                        }
gm_lb11:;
                        {
                            {
                                gm_b16_8 t74 = gm_l2;
                                gm_b16_8 t75 = gm_l10;
                                uint8_t t76 = gm_f_contains_a7cccd4(t74, t75);
                                if (!t76) {
                                    goto gm_lb14;
                                }
                                {
                                    {
                                        uint8_t t77 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
                                        if (t77) {
                                            goto gm_lb16;
                                        }
                                        gm_l17 = ((uint8_t)0);
                                        goto gm_lb15;
                                    }
gm_lb16:;
                                    {
                                        gm_l17 = ((uint8_t)1);
                                        goto gm_lb15;
                                    }
gm_lb17:;
                                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 85, 46);
                                }
gm_lb15:;
                                uint8_t t78 = gm_l17;
                                if (!t78) {
                                    goto gm_lb14;
                                }
                                {
                                    gm_b16_8 t79 = gm_l10;
                                    gm_tnpifv20irnph t80;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t80 + 0), t79);
                                    gm_f_format_6dqa07h((void *)&gm_l18, (void *)NULL, cx, NULL, t80);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t81 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                                if (!t81) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srccr53ejqr1dde, 86, 25);
                                {
                                    gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                                    gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&t82));
                                    memset(&(*(gm_t5detta1b4gnk *)gm_out), 0, sizeof (*(gm_t5detta1b4gnk *)gm_out));
                                    ((uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out))[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out) + 8), t83);
                                    goto gm_fdone;
                                }
                            }
gm_lb14:;
                            gm_b16_8 t84 = gm_l2;
                            gm_b16_8 t85 = gm_l10;
                            uint8_t t86 = gm_f_contains_a7cccd4(t84, t85);
                            if (t86) {
                                void * t87 = (void *)((uint8_t *)&F.l3);
                                gm_b16_8 t88 = gm_l10;
                                uint8_t t89 = gm_f_has_flag_4n9tu40(t87, t88);
                                uint8_t t90 = (uint8_t)!t89;
                                if (t90) {
                                    void * t91 = (void *)(((uint8_t *)&F.l3) + 80);
                                    gm_b16_8 t92 = gm_l10;
                                    gm_tftb1odc739t5 t93 = gm_f_push_3ef746b(t91, t92);
                                    gm_l19 = t93;
                                    uint8_t t94 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                    if (!t94) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srccr53ejqr1dde, 88, 45);
                                }
                            } else {
                                gm_b16_8 t95 = gm_l1;
                                gm_b16_8 t96 = gm_l10;
                                uint8_t t97 = gm_f_contains_a7cccd4(t95, t96);
                                if (t97) {
                                    uint8_t t98 = (uint8_t)((((uint8_t *)&gm_l11))[16] != 0);
                                    if (t98) {
                                        gm_b16_8 t99 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l11));
                                        gm_l13 = t99;
                                    } else {
                                        uint64_t t100 = gm_l4;
                                        uint64_t t101 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                        uint8_t t102 = (t100 >= t101);
                                        if (t102) {
                                            {
                                                gm_b16_8 t103 = gm_l10;
                                                gm_tnpifv20irnph t104;
                                                GM_ST(gm_b16_8, ((uint8_t *)&t104 + 0), t103);
                                                gm_f_format_n537vp2((void *)&gm_l20, (void *)NULL, cx, NULL, t104);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                                            if (!t105) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srccr53ejqr1dde, 91, 48);
                                            {
                                                gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                                                gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                                                memset(&(*(gm_t5detta1b4gnk *)gm_out), 0, sizeof (*(gm_t5detta1b4gnk *)gm_out));
                                                ((uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out))[0] = 1;
                                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out) + 8), t107);
                                                goto gm_fdone;
                                            }
                                        }
                                        uint64_t t108 = gm_l4;
                                        uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                                        if (o110) gm_trap("integer overflow in `+`", gm_srccr53ejqr1dde, 92, 21);
                                        uint64_t t111 = r109;
                                        gm_l4 = t111;
                                        void * t112 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                        uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                        uint64_t t114 = gm_l4;
                                        uint64_t r115; bool o116 = __builtin_sub_overflow(t114, ((uint64_t)1ULL), &r115);
                                        if (o116) gm_trap("integer overflow in `-`", gm_srccr53ejqr1dde, 93, 26);
                                        uint64_t t117 = r115;
                                        if ((uintptr_t)t117 >= (uintptr_t)(t113)) gm_trap_index((uintptr_t)t117, t113, gm_srccr53ejqr1dde, 93, 21);
                                        gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)t112) + (uintptr_t)t117 * 16));
                                        gm_l13 = t118;
                                    }
                                    {
                                        void * t119 = (void *)((uint8_t *)&F.l3);
                                        gm_b16_8 t120 = gm_l10;
                                        gm_f_value_of_0km7vvp((void *)&gm_l22, t119, t120);
                                    }
                                    {
                                        {
                                            uint8_t t121 = (uint8_t)((((uint8_t *)&gm_l22))[16] != 0);
                                            if (t121) {
                                                goto gm_lb19;
                                            }
                                            gm_l21 = ((uint8_t)0);
                                            goto gm_lb18;
                                        }
gm_lb19:;
                                        {
                                            gm_l21 = ((uint8_t)1);
                                            goto gm_lb18;
                                        }
gm_lb20:;
                                        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 95, 20);
                                    }
gm_lb18:;
                                    uint8_t t122 = gm_l21;
                                    if (t122) {
                                        {
                                            gm_b16_8 t123 = gm_l10;
                                            gm_tnpifv20irnph t124;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t124 + 0), t123);
                                            gm_f_format_fbigqms((void *)&gm_l23, (void *)NULL, cx, NULL, t124);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        uint8_t t125 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                        if (!t125) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_srccr53ejqr1dde, 95, 61);
                                        {
                                            gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                                            gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
                                            memset(&(*(gm_t5detta1b4gnk *)gm_out), 0, sizeof (*(gm_t5detta1b4gnk *)gm_out));
                                            ((uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out))[0] = 1;
                                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out) + 8), t127);
                                            goto gm_fdone;
                                        }
                                    }
                                    {
                                        void * t128 = (void *)(((uint8_t *)&F.l3) + 40);
                                        gm_b16_8 t129 = gm_l10;
                                        gm_b16_8 t130 = gm_l13;
                                        gm_t6j6smj7qoctu t131;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t131 + 0), t129);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t131 + 16), t130);
                                        gm_tftb1odc739t5 t132 = gm_f_push_2tc790g(t128, &t131);
                                        gm_l24 = t132;
                                    }
                                    uint8_t t133 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                    if (!t133) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srccr53ejqr1dde, 96, 17);
                                } else {
                                    {
                                        gm_b16_8 t134 = gm_l5;
                                        gm_tnpifv20irnph t135;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t135 + 0), t134);
                                        gm_f_format_j0ggm04((void *)&gm_l25, (void *)NULL, cx, NULL, t135);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    uint8_t t136 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                    if (!t136) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 8, gm_srccr53ejqr1dde, 98, 25);
                                    {
                                        gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
                                        gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
                                        memset(&(*(gm_t5detta1b4gnk *)gm_out), 0, sizeof (*(gm_t5detta1b4gnk *)gm_out));
                                        ((uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out))[0] = 1;
                                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out) + 8), t138);
                                        goto gm_fdone;
                                    }
                                }
                            }
                        }
gm_lb13:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    {
                        {
                            gm_b16_8 t139 = gm_l5;
                            gm_b16_8 t140;
                            GM_ST(void *, (uint8_t *)&t140, (void *)(gm_skolmnfc0ccbd));
                            GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(1));
                            uint8_t t141 = gm_f_starts_with_6vvt01e(t139, t140);
                            if (!t141) {
                                goto gm_lb22;
                            }
                            uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                            uint8_t t143 = (t142 > ((uint64_t)1ULL));
                            if (!t143) {
                                goto gm_lb22;
                            }
                            {
                                gm_b16_8 t144 = gm_l5;
                                gm_tnpifv20irnph t145;
                                GM_ST(gm_b16_8, ((uint8_t *)&t145 + 0), t144);
                                gm_f_format_j0ggm04((void *)&gm_l26, (void *)NULL, cx, NULL, t145);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t146 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                            if (!t146) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_srccr53ejqr1dde, 101, 21);
                            {
                                gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                                gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&t147));
                                memset(&(*(gm_t5detta1b4gnk *)gm_out), 0, sizeof (*(gm_t5detta1b4gnk *)gm_out));
                                ((uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out))[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out) + 8), t148);
                                goto gm_fdone;
                            }
                        }
gm_lb22:;
                        void * t149 = (void *)((uint8_t *)&F.l3);
                        gm_b16_8 t150 = gm_l5;
                        gm_tftb1odc739t5 t151 = gm_f_push_3ef746b(t149, t150);
                        gm_l27 = t151;
                        uint8_t t152 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                        if (!t152) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srccr53ejqr1dde, 103, 13);
                    }
gm_lb21:;
                }
gm_lb9:;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        gm_tr8dvlu9ik4gj t153 = GM_LD(gm_tr8dvlu9ik4gj, ((uint8_t *)&F.l3));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out))[0] = 0;
        GM_ST(gm_tr8dvlu9ik4gj, ((uint8_t *)&(*(gm_t5detta1b4gnk *)gm_out) + 8), t153);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_relative_path_3klciqu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_relative_path_3klciqu F;
    gm_b16_8 gm_l0; /* from */
    gm_b16_8 gm_l1; /* target */
    uint64_t gm_l4; /* common */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* x */
    GM_STACK_CHECK(&F, 817u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tl7hpf5nlilqq gm_l11;
    gm_tl7hpf5nlilqq gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tabb710d9mq2e gm_l15;
    {
        gm_b16_8 t15 = gm_l0;
        gm_f_split_cbk4ks8((void *)&gm_l11, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l11) + 16), 40);
    }
    {
        gm_b16_8 t16 = gm_l1;
        gm_f_split_cbk4ks8((void *)&gm_l12, (void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l12) + 16), 40);
    }
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l4;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb1;
            }
            uint64_t t20 = gm_l4;
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            void * t23 = GM_LD(void *, ((uint8_t *)&F.l2));
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
            uint64_t t25 = gm_l4;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t23) + (uintptr_t)t25 * 16));
            void * t27 = GM_LD(void *, ((uint8_t *)&F.l3));
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
            uint64_t t29 = gm_l4;
            gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)t27) + (uintptr_t)t29 * 16));
            uint8_t t31 = (gm_eq0iqiubfm7e(((uint8_t *)&t26), ((uint8_t *)&t30)));
            if (!t31) {
                goto gm_lb1;
            }
            {
                uint64_t t32 = gm_l4;
                uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                if (o34) gm_trap("integer overflow in `+`", gm_srccr53ejqr1dde, 179, 98);
                uint64_t t35 = r33;
                gm_l4 = t35;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t36;
        gm_f_list_epvducg((void *)&t36, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t36;
    }
    uint64_t t37 = gm_l4;
    gm_l7 = t37;
    uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
    gm_l6 = t38;
    {
        for (;;) {
            uint64_t t39 = gm_l7;
            uint64_t t40 = gm_l6;
            uint8_t t41 = (t39 < t40);
            if (!t41) {
                goto gm_lb4;
            }
            {
                {
                    {
                        void * t42 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t43;
                        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_snenkhfev166s));
                        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t44 = gm_f_push_3ef746b(t42, t43);
                        gm_l13 = t44;
                        uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srccr53ejqr1dde, 181, 36);
                        goto gm_lb7;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 181, 5);
                }
gm_lb7:;
            }
gm_lb6:;
            uint64_t t46 = gm_l7;
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
            uint64_t t49 = r47;
            gm_l7 = t49;
        }
    }
gm_lb4:;
    void * t50 = GM_LD(void *, ((uint8_t *)&F.l3));
    uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    uint64_t t52 = gm_l4;
    uint64_t t53 = (uintptr_t)t52;
    uint64_t t54 = (uintptr_t)t51;
    if (t53 > t54 || t54 > (uintptr_t)t51) gm_trap_slice(t53, t54, t51, gm_srccr53ejqr1dde, 182, 14);
    uint64_t t55 = ((uintptr_t)t51) - (uintptr_t)t52;
    gm_b16_8 t56;
    GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t50 + (uintptr_t)t52 * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55));
    gm_l8 = t56;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t57 = gm_l9;
            uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t59 = (t57 < t58);
            if (!t59) {
                goto gm_lb9;
            }
            {
                void * t60 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t62 = gm_l9;
                {
                    {
                        gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)t60) + (uintptr_t)t62 * 16));
                        gm_l10 = t63;
                        void * t64 = (void *)((uint8_t *)&F.l5);
                        gm_b16_8 t65 = gm_l10;
                        gm_tftb1odc739t5 t66 = gm_f_push_3ef746b(t64, t65);
                        gm_l14 = t66;
                        uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                        if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srccr53ejqr1dde, 182, 34);
                        goto gm_lb12;
                    }
gm_lb13:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 182, 5);
                }
gm_lb12:;
            }
gm_lb11:;
            uint64_t t68 = gm_l9;
            uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
            uint64_t t71 = r69;
            gm_l9 = t71;
        }
    }
gm_lb9:;
    uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
    uint8_t t73 = ((t72 == ((uint64_t)0ULL)));
    if (t73) {
        gm_b16_8 t74;
        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(1));
        F.ret = t74;
        goto gm_fret;
    }
    {
        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
        gm_b16_8 t77;
        GM_ST(void *, (uint8_t *)&t77, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(1));
        gm_f_join_sfknljb((void *)&gm_l15, (void *)NULL, cx, NULL, t76, t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srccr53ejqr1dde, 184, 12);
    gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
    F.ret = t80;
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
__attribute__((aligned(4))) void gm_f_remove_bar6kga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_remove_bar6kga F;
    gm_b16_8 gm_l0; /* args */
    gm_tpmdr2hmdhsqr gm_l1; /* <temporary array> */
    gm_b0_8 gm_l2; /* <temporary array> */
    gm_b16_8 gm_l4; /* name */
    gm_b16_8 gm_l5; /* e */
    gm_b16_8 gm_l6; /* root */
    gm_b16_8 gm_l7; /* manifest_path */
    gm_b16_8 gm_l8; /* e */
    gm_b16_8 gm_l9; /* old_text */
    gm_b16_8 gm_l10; /* e */
    gm_b16_8 gm_l11; /* new_text */
    gm_b16_8 gm_l12; /* e */
    gm_b16_8 gm_l13; /* e */
    gm_b16_8 gm_l14; /* e2 */
    GM_STACK_CHECK(&F, 1627u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t77ebp3ogdble gm_l15;
    gm_b16_8 gm_l16;
    gm_th2lqbo77pvsc gm_l17;
    gm_t3ujhu6qph7de gm_l18;
    gm_t3ujhu6qph7de gm_l19;
    gm_t3ujhu6qph7de gm_l20;
    gm_tl572d7579hns gm_l21;
    gm_tv37d3dpt2opu gm_l22;
    gm_t074vrl1hb4f6 gm_l23;
    gm_tl572d7579hns gm_l24;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snboa22b96bbc));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_tpmdr2hmdhsqr t4;
        GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
        gm_l1 = t4;
        void * t5 = (void *)((uint8_t *)&gm_l1);
        uint64_t t6 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
        void * t8 = (void *)((uint8_t *)&gm_l2);
        uint64_t t9 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9));
        gm_t5detta1b4gnk t11;
        gm_f_parse_args_7blm2mp((void *)&t11, (void *)NULL, cx, NULL, t2, t7, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_usage_failure_cgg6v1t((void *)&gm_l15, &t11);
    }
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
    if (t12) {
    } else {
        gm_t3ujhu6qph7de t13 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l15) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t13);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l15) + 8), 120);
    }
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    uint8_t t15 = (!(t14 == ((uint64_t)1ULL)));
    if (t15) {
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_saicih5tjo4fl));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(42));
        gm_t3ujhu6qph7de t17;
        GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 8), t16);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t17);
        goto gm_fdone;
    }
    void * t18 = GM_LD(void *, ((uint8_t *)&F.l3));
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t19, gm_srccr53ejqr1dde, 275, 13);
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)t18) + (uintptr_t)((uint64_t)0ULL) * 16));
    gm_l4 = t20;
    {
        void * t21 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_snboa22b96bbc));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(3));
        gm_f_value_of_0km7vvp((void *)&gm_l17, t21, t22);
    }
    uint8_t t23 = (uint8_t)((((uint8_t *)&gm_l17))[16] != 0);
    if (t23) {
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
        gm_l16 = t24;
    } else {
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(1));
        gm_l16 = t25;
    }
    {
        gm_b16_8 t26 = gm_l16;
        gm_f_root_of_cluda11((void *)&gm_l18, (void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
    if (t27) {
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
        gm_l6 = t28;
    } else {
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
        gm_l5 = t29;
        {
            gm_b16_8 t30 = gm_l5;
            gm_t3ujhu6qph7de t31;
            gm_f_fail_o80k3rv((void *)&t31, t30);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t31);
            goto gm_fdone;
        }
    }
    gm_b16_8 t32 = gm_l6;
    gm_b16_8 t33;
    GM_ST(void *, (uint8_t *)&t33, (void *)(gm_se24c7hpp401n));
    GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(10));
    gm_b16_8 t34 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t32, t33);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t34;
    {
        gm_b16_8 t35 = gm_l7;
        gm_f_read_text_g0qbj1t((void *)&gm_l19, (void *)NULL, cx, NULL, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
    if (t36) {
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
        gm_l9 = t37;
    } else {
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
        gm_l8 = t38;
        {
            gm_b16_8 t39 = gm_l8;
            gm_t3ujhu6qph7de t40;
            gm_f_fail_o80k3rv((void *)&t40, t39);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t40);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t41 = gm_l9;
        gm_b16_8 t42 = gm_l4;
        gm_f_remove_dependency_4ju7b0h((void *)&gm_l20, (void *)NULL, cx, NULL, t41, t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
    if (t43) {
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
        gm_l11 = t44;
    } else {
        gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
        gm_l10 = t45;
        {
            gm_b16_8 t46 = gm_l10;
            gm_t3ujhu6qph7de t47;
            gm_f_fail_o80k3rv((void *)&t47, t46);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t47);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t48 = gm_l7;
        gm_b16_8 t49 = gm_l11;
        gm_f_write_atomic_v74ncu3((void *)&gm_l21, (void *)NULL, cx, NULL, t48, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
    if (t50) {
    } else {
        gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
        gm_l12 = t51;
        {
            gm_b16_8 t52 = gm_l12;
            gm_t3ujhu6qph7de t53;
            gm_f_fail_o80k3rv((void *)&t53, t52);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t53);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t54 = gm_l6;
        gm_tqs5p4n2tq19v t55;
        GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t55 + 1), ((uint8_t)0));
        gm_tl572d7579hns t56;
        GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t56 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t56 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)0ULL));
        gm_f_sync_ht164ep((void *)&gm_l23, (void *)NULL, cx, NULL, t54, t55, &t56, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
    if (t57) {
        memmove(((uint8_t *)&gm_l22), (((uint8_t *)&gm_l23) + 8), 48);
    } else {
        gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
        gm_l13 = t58;
        {
            gm_b16_8 t59 = gm_l7;
            gm_b16_8 t60 = gm_l9;
            gm_f_write_atomic_v74ncu3((void *)&gm_l24, (void *)NULL, cx, NULL, t59, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
        if (t61) {
        } else {
            gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
            gm_l14 = t62;
            {
                gm_b16_8 t63 = gm_l14;
                gm_t3ujhu6qph7de t64;
                gm_f_fail_o80k3rv((void *)&t64, t63);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t64);
                goto gm_fdone;
            }
        }
        {
            gm_b16_8 t65 = gm_l13;
            gm_t3ujhu6qph7de t66;
            gm_f_fail_o80k3rv((void *)&t66, t65);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t66);
            goto gm_fdone;
        }
    }
    {
    }
    gm_b16_8 t67 = gm_l4;
    if (gm_pf454qfrpt6r(cx, gm_s9m6kd6gec368, 13, gm_srccr53ejqr1dde, 285, 5)) {
        goto gm_lad;
    }
    if ((GM_LD(uintptr_t, ((uint8_t *)&t67) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t67)), GM_LD(uintptr_t, ((uint8_t *)&t67) + 8), gm_srccr53ejqr1dde, 285, 5))) {
        goto gm_lad;
    }
    if (gm_pf454qfrpt6r(cx, gm_smsc5gshqt6cd, 19, gm_srccr53ejqr1dde, 285, 5)) {
        goto gm_lad;
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
inline __attribute__((aligned(4))) void gm_f_root_of_cluda11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_root_of_cluda11 F;
    gm_b16_8 gm_l0; /* dir */
    GM_STACK_CHECK(&F, 374u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    gm_tih6h4avv5nrb gm_l2;
    gm_tabb710d9mq2e gm_l3;
    gm_t3ujhu6qph7de gm_l4;
    gm_th2lqbo77pvsc gm_l5;
    gm_tabb710d9mq2e gm_l6;
    {
        gm_b16_8 t5 = gm_l0;
        gm_node *pn6 = gm_find(cx, 2126215853);
        if (!pn6) gm_trap("no handler installed for effect `Fs`", gm_srccr53ejqr1dde, 110, 8);
        gm_tih6h4avv5nrb t7;
        ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn6->clauses[5])((void *)&t7, pn6->env, pn6->hctx, pn6, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t7;
    }
    {
        {
            uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
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
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 110, 8);
    }
gm_lb1:;
    uint8_t t9 = gm_l1;
    if (t9) {
        {
            gm_b16_8 t10 = gm_l0;
            gm_tnpifv20irnph t11;
            GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
            gm_f_format_27unvt9((void *)&gm_l3, (void *)NULL, cx, NULL, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srccr53ejqr1dde, 110, 37);
        {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t14);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t15 = gm_l0;
        gm_f_find_root_kjnm6hf((void *)&gm_l5, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
    if (t16) {
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
        GM_ST(uint8_t, (uint8_t *)&gm_l4 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l4 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l4 + 4, 0);
        ((uint8_t *)&gm_l4)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l4 + 8), t17);
    } else {
        {
            gm_b16_8 t18 = gm_l0;
            gm_tnpifv20irnph t19;
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t18);
            gm_f_format_v71csbq((void *)&gm_l6, (void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srccr53ejqr1dde, 111, 33);
        {
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
            GM_ST(uint8_t, (uint8_t *)&gm_l4 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l4 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l4 + 4, 0);
            ((uint8_t *)&gm_l4)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l4 + 8), t22);
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l4), 24);
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
inline __attribute__((aligned(4))) uint8_t gm_f_run_command_s4fhqaq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_run_command_s4fhqaq F;
    gm_b16_8 gm_l0; /* tool */
    gm_b16_8 gm_l1; /* command */
    gm_b16_8 gm_l2; /* args */
    gm_t715ajl6o1ipo gm_l3; /* result */
    gm_b16_8 gm_l4; /* e */
    gm_b16_8 gm_l5; /* e */
    GM_STACK_CHECK(&F, 688u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tabb710d9mq2e gm_l6;
    {
        {
            gm_b16_8 t15 = gm_l1;
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s6pnfrub0rj1t));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(5));
            uint8_t t17 = (gm_eq0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16)));
            if (!t17) {
                goto gm_lb2;
            }
            gm_b16_8 t18 = gm_l2;
            gm_f_fetch_l6q1k6p((void *)&gm_l3, (void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_b16_8 t19 = gm_l1;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s3lbmpmt95hvh));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(6));
            uint8_t t21 = (gm_eq0iqiubfm7e(((uint8_t *)&t19), ((uint8_t *)&t20)));
            if (!t21) {
                goto gm_lb3;
            }
            gm_b16_8 t22 = gm_l2;
            gm_f_update_oi0ok0i((void *)&gm_l3, (void *)NULL, cx, NULL, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_b16_8 t23 = gm_l1;
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_s4o70d89183c2));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(3));
            uint8_t t25 = (gm_eq0iqiubfm7e(((uint8_t *)&t23), ((uint8_t *)&t24)));
            if (!t25) {
                goto gm_lb4;
            }
            gm_b16_8 t26 = gm_l2;
            gm_f_add_04lv2cc((void *)&gm_l3, (void *)NULL, cx, NULL, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_b16_8 t27 = gm_l1;
            gm_b16_8 t28;
            GM_ST(void *, (uint8_t *)&t28, (void *)(gm_sqljkj8r253om));
            GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(6));
            uint8_t t29 = (gm_eq0iqiubfm7e(((uint8_t *)&t27), ((uint8_t *)&t28)));
            if (!t29) {
                goto gm_lb5;
            }
            gm_b16_8 t30 = gm_l2;
            gm_f_remove_bar6kga((void *)&gm_l3, (void *)NULL, cx, NULL, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb5:;
        {
            gm_b16_8 t31 = gm_l1;
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sivh46n0690a4));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(3));
            uint8_t t33 = (gm_eq0iqiubfm7e(((uint8_t *)&t31), ((uint8_t *)&t32)));
            if (!t33) {
                goto gm_lb6;
            }
            gm_b16_8 t34 = gm_l2;
            gm_f_new_package_hf828qc((void *)&gm_l3, (void *)NULL, cx, NULL, t34, ((uint8_t)1));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb6:;
        {
            gm_b16_8 t35 = gm_l1;
            gm_b16_8 t36;
            GM_ST(void *, (uint8_t *)&t36, (void *)(gm_suass5sid4b6e));
            GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(4));
            uint8_t t37 = (gm_eq0iqiubfm7e(((uint8_t *)&t35), ((uint8_t *)&t36)));
            if (!t37) {
                goto gm_lb7;
            }
            gm_b16_8 t38 = gm_l2;
            gm_f_new_package_hf828qc((void *)&gm_l3, (void *)NULL, cx, NULL, t38, ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb7:;
        {
            gm_b16_8 t39 = gm_l1;
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_so9n4mmd960c7));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(5));
            uint8_t t41 = (gm_eq0iqiubfm7e(((uint8_t *)&t39), ((uint8_t *)&t40)));
            if (!t41) {
                goto gm_lb8;
            }
            gm_b16_8 t42 = gm_l2;
            gm_f_cache_command_7uqmos8((void *)&gm_l3, (void *)NULL, cx, NULL, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb8:;
        {
            {
                gm_b16_8 t43 = gm_l1;
                gm_tnpifv20irnph t44;
                GM_ST(gm_b16_8, ((uint8_t *)&t44 + 0), t43);
                gm_f_format_gl2aoqn((void *)&gm_l6, (void *)NULL, cx, NULL, t44);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_srccr53ejqr1dde, 131, 29);
            {
                gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&t46));
                gm_t3ujhu6qph7de t48;
                GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t48 + 8), t47);
                GM_ST(uint8_t, (uint8_t *)&gm_l3 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l3 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l3 + 4, 0);
                ((uint8_t *)&gm_l3)[0] = 1;
                GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&gm_l3 + 8), t48);
            }
            goto gm_lb1;
        }
gm_lb9:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 123, 26);
    }
gm_lb1:;
    {
        {
            uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (t49) {
                goto gm_lb11;
            }
            uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)&gm_l3) + 8) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)0ULL));
            if (!t51) {
                goto gm_lb11;
            }
            gm_b16_8 t52 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l3) + 8) + 8));
            gm_l4 = t52;
            gm_b16_8 t53 = gm_l0;
            gm_b16_8 t54 = gm_l4;
            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)gm_crqb3tjd618));
            if ((GM_LD(uintptr_t, ((uint8_t *)&t53) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t53)), GM_LD(uintptr_t, ((uint8_t *)&t53) + 8), gm_srccr53ejqr1dde, 135, 13))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_slgmqgkioa4u6, 2, gm_srccr53ejqr1dde, 135, 13)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t54) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t54)), GM_LD(uintptr_t, ((uint8_t *)&t54) + 8), gm_srccr53ejqr1dde, 135, 13))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_s8dbdqj1sqibj, 2, gm_srccr53ejqr1dde, 135, 13)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t55) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t55)), GM_LD(uintptr_t, ((uint8_t *)&t55) + 8), gm_srccr53ejqr1dde, 135, 13))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 135, 13)) {
                goto gm_lad;
            }
            F.ret = ((uint8_t)2ULL);
            goto gm_fret;
        }
gm_lb11:;
        {
            uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (t56) {
                goto gm_lb12;
            }
            uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)&gm_l3) + 8) + 0));
            uint8_t t58 = (uint8_t)(t57 == ((uint8_t)1ULL));
            if (!t58) {
                goto gm_lb12;
            }
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l3) + 8) + 8));
            gm_l5 = t59;
            gm_b16_8 t60 = gm_l0;
            gm_b16_8 t61 = gm_l5;
            if ((GM_LD(uintptr_t, ((uint8_t *)&t60) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t60)), GM_LD(uintptr_t, ((uint8_t *)&t60) + 8), gm_srccr53ejqr1dde, 139, 13))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_slgmqgkioa4u6, 2, gm_srccr53ejqr1dde, 139, 13)) {
                goto gm_lad;
            }
            if ((GM_LD(uintptr_t, ((uint8_t *)&t61) + 8) != 0 && gm_pf454qfrpt6r(cx, GM_LD(void *, ((uint8_t *)&t61)), GM_LD(uintptr_t, ((uint8_t *)&t61) + 8), gm_srccr53ejqr1dde, 139, 13))) {
                goto gm_lad;
            }
            if (gm_pf454qfrpt6r(cx, gm_so514oj5nbhcu, 1, gm_srccr53ejqr1dde, 139, 13)) {
                goto gm_lad;
            }
            F.ret = ((uint8_t)1ULL);
            goto gm_fret;
        }
gm_lb12:;
        {
            F.ret = ((uint8_t)0ULL);
            goto gm_fret;
        }
gm_lb13:;
        gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 133, 5);
    }
gm_lb10:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_update_oi0ok0i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_update_oi0ok0i F;
    gm_b16_8 gm_l0; /* args */
    gm_tpmdr2hmdhsqr gm_l1; /* <temporary array> */
    gm_b0_8 gm_l2; /* <temporary array> */
    gm_b16_8 gm_l4; /* e */
    gm_b16_8 gm_l5; /* root */
    gm_tl572d7579hns gm_l6; /* refresh */
    gm_b16_8 gm_l7; /* e */
    GM_STACK_CHECK(&F, 951u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t77ebp3ogdble gm_l8;
    gm_b16_8 gm_l9;
    gm_th2lqbo77pvsc gm_l10;
    gm_t3ujhu6qph7de gm_l11;
    gm_tv37d3dpt2opu gm_l12;
    gm_t074vrl1hb4f6 gm_l13;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snboa22b96bbc));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_tpmdr2hmdhsqr t4;
        GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
        gm_l1 = t4;
        void * t5 = (void *)((uint8_t *)&gm_l1);
        uint64_t t6 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(t6));
        void * t8 = (void *)((uint8_t *)&gm_l2);
        uint64_t t9 = ((uintptr_t)((uint64_t)0ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9));
        gm_t5detta1b4gnk t11;
        gm_f_parse_args_7blm2mp((void *)&t11, (void *)NULL, cx, NULL, t2, t7, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_usage_failure_cgg6v1t((void *)&gm_l8, &t11);
    }
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (t12) {
    } else {
        gm_t3ujhu6qph7de t13 = GM_LD(gm_t3ujhu6qph7de, (((uint8_t *)&gm_l8) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
        GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t13);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l8) + 8), 120);
    }
    {
        void * t14 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t15;
        GM_ST(void *, (uint8_t *)&t15, (void *)(gm_snboa22b96bbc));
        GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(3));
        gm_f_value_of_0km7vvp((void *)&gm_l10, t14, t15);
    }
    uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l10))[16] != 0);
    if (t16) {
        gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
        gm_l9 = t17;
    } else {
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
        gm_l9 = t18;
    }
    {
        gm_b16_8 t19 = gm_l9;
        gm_f_root_of_cluda11((void *)&gm_l11, (void *)NULL, cx, NULL, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (t20) {
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        gm_l5 = t21;
    } else {
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        gm_l4 = t22;
        {
            gm_b16_8 t23 = gm_l4;
            gm_t3ujhu6qph7de t24;
            gm_f_fail_o80k3rv((void *)&t24, t23);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t24);
            goto gm_fdone;
        }
    }
    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
    uint8_t t26 = ((t25 == ((uint64_t)0ULL)));
    if (t26) {
        GM_ST(uint8_t, (uint8_t *)&gm_l6 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l6 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l6 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&gm_l6 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&gm_l6 + 16, 0);
        GM_ST(uint8_t, ((uint8_t *)&gm_l6 + 0), ((uint8_t)1ULL));
    } else {
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        GM_ST(uint8_t, (uint8_t *)&gm_l6 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l6 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l6 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&gm_l6 + 0), ((uint8_t)2ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l6 + 8), t28);
    }
    {
        gm_b16_8 t29 = gm_l5;
        gm_tqs5p4n2tq19v t30;
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t30 + 1), ((uint8_t)0));
        gm_tl572d7579hns t31 = GM_LD(gm_tl572d7579hns, ((uint8_t *)&gm_l6));
        gm_f_sync_ht164ep((void *)&gm_l13, (void *)NULL, cx, NULL, t29, t30, &t31, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (t32) {
        memmove(((uint8_t *)&gm_l12), (((uint8_t *)&gm_l13) + 8), 48);
    } else {
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
        gm_l7 = t33;
        {
            gm_b16_8 t34 = gm_l7;
            gm_t3ujhu6qph7de t35;
            gm_f_fail_o80k3rv((void *)&t35, t34);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 1;
            GM_ST(gm_t3ujhu6qph7de, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), t35);
            goto gm_fdone;
        }
    }
    {
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_t715ajl6o1ipo *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
extern __attribute__((aligned(4))) void gm_f_usage_failure_cgg6v1t(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_t77ebp3ogdble gm_f_usage_failure_cgg6v1t_c(gm_t5detta1b4gnk a0) { gm_t77ebp3ogdble gm_r; gm_f_usage_failure_cgg6v1t(&gm_r, &a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_value_of_0km7vvp(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_value_of_0km7vvp F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t6j6smj7qoctu gm_l4; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 210u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 40));
    gm_l2 = t8;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l3;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t14 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t12) + (uintptr_t)t14 * 32), 32);
                        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
                        gm_b16_8 t16 = gm_l1;
                        uint8_t t17 = (gm_eq0iqiubfm7e(((uint8_t *)&t15), ((uint8_t *)&t16)));
                        if (t17) {
                            gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 16));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t18);
                            ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srccr53ejqr1dde, 47, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t19 = gm_l3;
            uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
            uint64_t t22 = r20;
            gm_l3 = t22;
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
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_value_of_0km7vvp_c(void * a0, gm_b16_8 a1) { gm_th2lqbo77pvsc gm_r; gm_f_value_of_0km7vvp(&gm_r, a0, a1); return gm_r; }

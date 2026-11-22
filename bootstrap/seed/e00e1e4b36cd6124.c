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
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_tqs5p4n2tq19v f0; gm_tqs5p4n2tq19v f1; gm_tqs5p4n2tq19v f2; gm_tqs5p4n2tq19v f3; gm_tqs5p4n2tq19v f4; gm_tqs5p4n2tq19v f5; gm_tqs5p4n2tq19v f6; } gm_t6s1lqopkm7fs;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint8_t f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t p5[3]; uint32_t f5; gm_t9805qtonbsag f6; gm_t9qkh0rsooc8d f7; uint8_t pe[3]; } gm_t7g88i52rrtm5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
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
typedef struct { gm_b16_8 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tgabaikr08mjg;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_tf5srh4mp4b3i f1; } gm_tj64ui6kruni6;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tf5srh4mp4b3i f1; uint8_t f2; uint8_t pe[7]; } gm_tjpqvfd72o62u;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { gm_t9805qtonbsag f0; uint64_t f1; uint8_t f2; uint8_t pe[7]; } gm_tmnrj1ud20adj;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; } gm_tq3m5qgvkpje8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tfeab279c16rr v0; gm_tj64ui6kruni6 v1; } f1; } gm_t1jll27j7ae34;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t6s1lqopkm7fs) == 14 && _Alignof(gm_t6s1lqopkm7fs) == 1 && offsetof(gm_t6s1lqopkm7fs, f0) == 0 && offsetof(gm_t6s1lqopkm7fs, f1) == 2 && offsetof(gm_t6s1lqopkm7fs, f2) == 4 && offsetof(gm_t6s1lqopkm7fs, f3) == 6 && offsetof(gm_t6s1lqopkm7fs, f4) == 8 && offsetof(gm_t6s1lqopkm7fs, f5) == 10 && offsetof(gm_t6s1lqopkm7fs, f6) == 12, "layout of `(?u8, ?u8, ?u8, ?u8, ?u8, ?u8, ?u8)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t7g88i52rrtm5) == 24 && _Alignof(gm_t7g88i52rrtm5) == 4 && offsetof(gm_t7g88i52rrtm5, f0) == 0 && offsetof(gm_t7g88i52rrtm5, f1) == 1 && offsetof(gm_t7g88i52rrtm5, f2) == 2 && offsetof(gm_t7g88i52rrtm5, f3) == 3 && offsetof(gm_t7g88i52rrtm5, f4) == 4 && offsetof(gm_t7g88i52rrtm5, f5) == 8 && offsetof(gm_t7g88i52rrtm5, f6) == 12 && offsetof(gm_t7g88i52rrtm5, f7) == 20, "layout of `FmtSpec`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
typedef struct { gm_tjpqvfd72o62u f0; uint8_t f1; uint8_t pe[7]; } gm_t8cc1mtbrhlpt;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t8588ma2cr9d7 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_t9q8lhljes74c;
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?UnOp`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
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
_Static_assert(sizeof(gm_tgabaikr08mjg) == 40 && _Alignof(gm_tgabaikr08mjg) == 8 && offsetof(gm_tgabaikr08mjg, f0) == 0 && offsetof(gm_tgabaikr08mjg, f1) == 16 && offsetof(gm_tgabaikr08mjg, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_t0uuhaljsf410 f0; uint8_t f1; uint8_t pe[7]; } gm_ti6pdj2sunpe4;
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tj64ui6kruni6) == 32 && _Alignof(gm_tj64ui6kruni6) == 8 && offsetof(gm_tj64ui6kruni6, f0) == 0 && offsetof(gm_tj64ui6kruni6, f1) == 16, "layout of `(string, ?usize)`");
_Static_assert(sizeof(gm_tjpqvfd72o62u) == 32 && _Alignof(gm_tjpqvfd72o62u) == 8 && offsetof(gm_tjpqvfd72o62u, f0) == 0 && offsetof(gm_tjpqvfd72o62u, f1) == 8 && offsetof(gm_tjpqvfd72o62u, f2) == 24, "layout of `SoaInfo`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tmnrj1ud20adj) == 24 && _Alignof(gm_tmnrj1ud20adj) == 8 && offsetof(gm_tmnrj1ud20adj, f0) == 0 && offsetof(gm_tmnrj1ud20adj, f1) == 8 && offsetof(gm_tmnrj1ud20adj, f2) == 16, "layout of `Number`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t7g88i52rrtm5 v0; gm_b16_8 v1; } f1; } gm_tpu7cfmd3m0cd;
_Static_assert(sizeof(gm_tq3m5qgvkpje8) == 40 && _Alignof(gm_tq3m5qgvkpje8) == 8 && offsetof(gm_tq3m5qgvkpje8, f0) == 0 && offsetof(gm_tq3m5qgvkpje8, f1) == 16 && offsetof(gm_tq3m5qgvkpje8, f2) == 24, "layout of `(string, usize, string)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { uint32_t f0; gm_tkn42u7na3t81 f1; } gm_tuo61a5rdrbtf;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
_Static_assert(sizeof(gm_t1jll27j7ae34) == 56 && _Alignof(gm_t1jll27j7ae34) == 8 && offsetof(gm_t1jll27j7ae34, f0) == 0 && offsetof(gm_t1jll27j7ae34, f1) == 8, "layout of `Parsed`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8cc1mtbrhlpt) == 40 && _Alignof(gm_t8cc1mtbrhlpt) == 8 && offsetof(gm_t8cc1mtbrhlpt, f0) == 0 && offsetof(gm_t8cc1mtbrhlpt, f1) == 32, "layout of `?SoaInfo`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9q8lhljes74c) == 32 && _Alignof(gm_t9q8lhljes74c) == 8 && offsetof(gm_t9q8lhljes74c, f0) == 0 && offsetof(gm_t9q8lhljes74c, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
_Static_assert(sizeof(gm_ti6pdj2sunpe4) == 72 && _Alignof(gm_ti6pdj2sunpe4) == 8 && offsetof(gm_ti6pdj2sunpe4, f0) == 0 && offsetof(gm_ti6pdj2sunpe4, f1) == 64, "layout of `?TemplateArgs`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpu7cfmd3m0cd) == 32 && _Alignof(gm_tpu7cfmd3m0cd) == 8 && offsetof(gm_tpu7cfmd3m0cd, f0) == 0 && offsetof(gm_tpu7cfmd3m0cd, f1) == 8, "layout of `SpecResult`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tuo61a5rdrbtf) == 12 && _Alignof(gm_tuo61a5rdrbtf) == 4 && offsetof(gm_tuo61a5rdrbtf, f0) == 0 && offsetof(gm_tuo61a5rdrbtf, f1) == 4, "layout of `(param: u32, args: ?Ty)`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; void * f4; } gm_t150deeftog49;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_th1g69d7vpipg f0; void * f1; void * f2; uint32_t f3; uint8_t pe[4]; } gm_t9i38ih07bojq;
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t150deeftog49) == 96 && _Alignof(gm_t150deeftog49) == 8 && offsetof(gm_t150deeftog49, f0) == 0 && offsetof(gm_t150deeftog49, f1) == 40 && offsetof(gm_t150deeftog49, f2) == 56 && offsetof(gm_t150deeftog49, f3) == 72 && offsetof(gm_t150deeftog49, f4) == 88, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_t9i38ih07bojq) == 64 && _Alignof(gm_t9i38ih07bojq) == 8 && offsetof(gm_t9i38ih07bojq, f0) == 0 && offsetof(gm_t9i38ih07bojq, f1) == 40 && offsetof(gm_t9i38ih07bojq, f2) == 48 && offsetof(gm_t9i38ih07bojq, f3) == 56, "layout of `WriteDeferredExpr`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_t9q8lhljes74c v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_to0f5qn75cgg8;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_to0f5qn75cgg8) == 64 && _Alignof(gm_to0f5qn75cgg8) == 8 && offsetof(gm_to0f5qn75cgg8, f0) == 0 && offsetof(gm_to0f5qn75cgg8, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_to0f5qn75cgg8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tsf6i2agfnpva;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_tsf6i2agfnpva) == 88 && _Alignof(gm_tsf6i2agfnpva) == 8 && offsetof(gm_tsf6i2agfnpva, f0) == 0 && offsetof(gm_tsf6i2agfnpva, f1) == 8 && offsetof(gm_tsf6i2agfnpva, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_align_of_mks0ipd {
    gm_frame h;
    gm_taari1s0m51a7 ret;
};
struct gm_fr_block_of_2gjl2i1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_build_write_k6dcf8r {
    gm_frame h;
    gm_th1g69d7vpipg l1; /* sink */
    gm_t0uuhaljsf410 l10; /* ta */
    gm_trqfvmjtt4sij l19; /* stmts */
    void * ret;
};
struct gm_fr_check_spec_4vp7jhg {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_check_template_brtb8t0 {
    gm_frame h;
    gm_tfeab279c16rr l2; /* parsed */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_const_template_5dvtt1m {
    gm_frame h;
    gm_tv2uf4leil5hq l10; /* b */
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
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb {
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
struct gm_fr_empty_slice_moftl2f {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n936iee {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vj3ckdj_2 {
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
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_name_at_5o2j1h9 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fn_written_qtvia9n {
    gm_frame h;
    gm_t4gut2c9fj1kt l2; /* d */
    gm_t4gut2c9fj1kt l4; /* d */
    gm_b0_1 ret;
};
struct gm_fr_format_0t8rkc3 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_2h4f9a5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_2rf8n5r {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_4duro1p {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_68lf2sm {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9r6mq5d {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cnhv2id {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_cub3u7l {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_f2fgi8v {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_h02tha7 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_j79pp86 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jvf1ma2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_k6j7893 {
    gm_frame h;
    gm_tq3m5qgvkpje8 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ksi8bsm {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_les73rv {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_mlqiori {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_oq31u8d {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_td6patj {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_qtk22f3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_number_at_esf7qkk {
    gm_frame h;
    gm_tmnrj1ud20adj ret;
};
struct gm_fr_numbered_error_07hll20 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* t */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_parse_spec_iod83cp {
    gm_frame h;
    gm_tpu7cfmd3m0cd ret;
};
struct gm_fr_parse_template_rlpl2k8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* pieces */
    gm_trqfvmjtt4sij l2; /* specs */
    gm_trqfvmjtt4sij l3; /* cur */
    gm_trqfvmjtt4sij l4; /* indices */
    gm_t1jll27j7ae34 ret;
};
struct gm_fr_placeholders_text_rtvia6b {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_0p2fb2b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tsf6i2agfnpva l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_nbmosph {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
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
struct gm_fr_regrow_61fb705 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_a6uf79v {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_thihbkv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_resolve_write_e97fjom {
    gm_frame h;
    gm_tfeab279c16rr l12; /* t */
    gm_tfeab279c16rr l14; /* parsed */
    gm_trqfvmjtt4sij l20; /* stmts */
    gm_trqfvmjtt4sij l21; /* fields */
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_source_offset_of_vlcjdq6 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_span_text_sv6qv18 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_template_args_vf1gdn1 {
    gm_frame h;
    gm_tfeab279c16rr l6; /* t */
    gm_t4gut2c9fj1kt l8; /* d */
    gm_tfeab279c16rr l13; /* parsed */
    gm_trqfvmjtt4sij l16; /* stmts */
    gm_trqfvmjtt4sij l17; /* values */
    gm_tc3oht4ksth9d l18; /* a */
    gm_trqfvmjtt4sij l31; /* vals */
    gm_t7te2tdpebbt4 l34; /* a */
    gm_tc3oht4ksth9d l35; /* e */
    gm_trqfvmjtt4sij l37; /* reads */
    gm_ti6pdj2sunpe4 ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_write_deferred_kbicf34 {
    gm_frame h;
    gm_th1g69d7vpipg l1; /* sink */
    gm_tc3oht4ksth9d l7; /* e */
    gm_trqfvmjtt4sij l10; /* elems */
    gm_t7te2tdpebbt4 l13; /* a */
    gm_tc3oht4ksth9d l14; /* e */
    gm_trqfvmjtt4sij l15; /* ts */
    gm_tsf6i2agfnpva l18; /* x */
    void * ret;
};
struct gm_fr_write_table_5n1kjkn {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* stmts */
    gm_trqfvmjtt4sij l6; /* fields */
    gm_b0_1 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b);
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b);
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_add_fix_0gkcr4c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_align_of_mks0ipd(uint8_t a0);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void * gm_f_build_write_k6dcf8r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_check_spec_4vp7jhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_check_template_brtb8t0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) void gm_f_const_string_jdjj62s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_const_template_5dvtt1m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_default_spec_9ntjq2s(void *gm_out);
__attribute__((aligned(4))) void * gm_f_elements_tuple_gdm5n6q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_moftl2f(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n936iee(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_fn_name_at_5o2j1h9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_fn_written_qtvia9n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_format_0t8rkc3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_2h4f9a5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_2rf8n5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_4duro1p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_68lf2sm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_9r6mq5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_b73b468(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_cnhv2id(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_cub3u7l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_f2fgi8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_h02tha7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_j79pp86(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_jvf1ma2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_k6j7893(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_ksi8bsm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_les73rv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_mlqiori(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_oq31u8d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rkil4k4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_td6patj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_default_spec_cdd8jng(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_qtk22f3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_2pnaa87(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_dsbahke(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ges19gd(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_number_at_esf7qkk(void *gm_out, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) gm_tmnrj1ud20adj gm_f_number_at_esf7qkk_c(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_numbered_error_07hll20(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_parse_spec_iod83cp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_template_rlpl2k8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_placeholders_text_rtvia6b(void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0p2fb2b(void * a0, gm_tuo61a5rdrbtf a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_nbmosph(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_a6uf79v(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_thihbkv(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_resolve_decl_pus042v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_write_e97fjom(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_settle_h9ikvs5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_soa_columns_tuple_ns477md(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_soa_of_jufdcj2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_source_offset_of_vlcjdq6(gm_b16_8 a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_span_text_sv6qv18(void * a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_template_args_vf1gdn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_var_kind_1sl5sig(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_write_deferred_kbicf34(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_table_5n1kjkn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
extern uint8_t gm_k7peov41e38[40] __attribute__((aligned(8)));
extern uint8_t gm_kei7njaa9je[40] __attribute__((aligned(8)));
extern uint8_t gm_kg028u69smr[40] __attribute__((aligned(8)));
extern uint8_t gm_kh47l71qsnm[40] __attribute__((aligned(8)));
extern uint8_t gm_khvqu09sagr[40] __attribute__((aligned(8)));
extern uint8_t gm_kl9vm3d7kkf[40] __attribute__((aligned(8)));
extern uint8_t gm_kmj9t14cb35[40] __attribute__((aligned(8)));
extern uint8_t gm_knhubf3blct[40] __attribute__((aligned(8)));
extern uint8_t gm_kr6fmufdvee[40] __attribute__((aligned(8)));
extern const uint8_t gm_s23qsffuqlc1v[25];
extern const uint8_t gm_s378kfmdl5enf[17];
extern const uint8_t gm_s3on77ql0ndko[17];
extern const uint8_t gm_s4087kcl9bpus[29];
extern const uint8_t gm_s40jlud5pi248[16];
extern const uint8_t gm_s4k55k154s62m[120];
extern const uint8_t gm_s4kt3mhlp0or9[11];
extern const uint8_t gm_s5q33up034m64[37];
extern const uint8_t gm_s6of1cedjphs7[3];
extern const uint8_t gm_s721l5hj93rdl[50];
extern const uint8_t gm_s7an5gtrdgen2[14];
extern const uint8_t gm_s8f0u3tjempi4[53];
extern const uint8_t gm_s92lujmr0a2c3[35];
extern const uint8_t gm_s93i7ga7kbfpu[34];
extern const uint8_t gm_sacrl98mg5g43[121];
extern const uint8_t gm_sc4vvo9i5cgel[86];
extern const uint8_t gm_scak4selic6pl[21];
extern const uint8_t gm_scne9acm49p4h[11];
extern const uint8_t gm_sf1qb0mllbs1p[34];
extern const uint8_t gm_sfl0j5tlur0e6[55];
extern const uint8_t gm_sfl0m0vifcu3m[82];
extern const uint8_t gm_sfu0in59pituf[62];
extern const uint8_t gm_sggmh06s4b0cs[23];
extern const uint8_t gm_sghh3jr7dlapd[66];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sjil3d717dq6h[63];
extern const uint8_t gm_sl9bj93djc5da[38];
extern const uint8_t gm_sm35km518bh68[52];
extern const uint8_t gm_smhli04hme60d[30];
extern const uint8_t gm_smjth8dldg6d0[59];
extern const uint8_t gm_smom6v4b4s28c[52];
extern const uint8_t gm_smvgcg1j21t6v[27];
extern const uint8_t gm_sn916351kmsu6[36];
extern const uint8_t gm_snf4l98ljss2u[89];
extern const uint8_t gm_snuinkttlbd28[14];
extern const uint8_t gm_soreld657hea2[25];
extern const uint8_t gm_sp920qd66qq8t[20];
extern const uint8_t gm_sppcm1qavsp1k[80];
extern const uint8_t gm_sqcvjnsub5bfs[53];
extern const uint8_t gm_sqqi4njr3i0t4[46];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr5s632lvio16[62];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src6d57grm686e3[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcrorgjlg51brd[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_srundlmqvm06p[60];
extern const uint8_t gm_sruq3cf9ro3q2[66];
extern const uint8_t gm_st4gqcsm35nph[90];
extern const uint8_t gm_suo9n1e1rnjb6[20];
extern const uint8_t gm_sutmkmalrac6l[45];
extern const uint8_t gm_sves946fckobv[104];
extern const uint8_t gm_svgiumrfk8vkf[73];
extern const gm_tmpl gm_tpl1nqa0en23gvp;
extern const gm_tmpl gm_tpl7ilj5n9objtq;
extern const gm_tmpl gm_tpl7m3t7r6fqpf7;
extern const gm_tmpl gm_tpl8nvimfep33sn;
extern const gm_tmpl gm_tpl9va1ubk8bean;
extern const gm_tmpl gm_tplbnt4jnaihcvu;
extern const gm_tmpl gm_tple0332mlih7si;
extern const gm_tmpl gm_tplg91jq41nbbkt;
extern const gm_tmpl gm_tplhciekfbsqng7;
extern const gm_tmpl gm_tpllf7nufk7lvkm;
extern const gm_tmpl gm_tpllihoeldkkkhv;
extern const gm_tmpl gm_tplm8s7gnb70j1j;
extern const gm_tmpl gm_tplm9t4pej720h2;
extern const gm_tmpl gm_tplnlqa25lecq48;
extern const gm_tmpl gm_tplnrt3ito76b2v;
extern const gm_tmpl gm_tploj8vgi84m11i;
extern const gm_tmpl gm_tplsnmu9a7htqov;
extern const gm_tmpl gm_tplu3gnb6pu4f3h;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq5307j5k9ms(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_equ1rt9sffme(a, b);
}
inline bool gm_eq929lennfd3(const uint8_t *a, const uint8_t *b) {
    return (a[0] != 0) == (b[0] != 0);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq929lennfd3(a + 0, b + 0)) return false;
    if (!gm_eq9oqmmiu8qc(a + 2, b + 2)) return false;
    return true;
}
inline bool gm_equ1rt9sffme(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_block_of_2gjl2i1 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* stmts */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t5 = gm_l1;
    gm_tl874t2atnlh3 t6 = gm_l2;
    void * t7 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t4, t5, ((void *)0), t6);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_moftl2f(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_moftl2f F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n936iee(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n936iee F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vj3ckdj_2 F;
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
inline __attribute__((aligned(4))) void gm_f_format_0t8rkc3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_0t8rkc3 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sn916351kmsu6));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(35));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9va1ubk8bean), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_2h4f9a5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_2h4f9a5 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sf1qb0mllbs1p));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(33));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplg91jq41nbbkt), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_2rf8n5r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_2rf8n5r F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sghh3jr7dlapd));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(65));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl8nvimfep33sn), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_4duro1p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_4duro1p F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smjth8dldg6d0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(58));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplbnt4jnaihcvu), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_68lf2sm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_68lf2sm F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scak4selic6pl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpllihoeldkkkhv), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9r6mq5d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_9r6mq5d F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s40jlud5pi248));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplnrt3ito76b2v), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_cnhv2id(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_cnhv2id F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_svgiumrfk8vkf));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(72));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllf7nufk7lvkm), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_cub3u7l(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_cub3u7l F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr5s632lvio16));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(61));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplhciekfbsqng7), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_f2fgi8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_f2fgi8v F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_st4gqcsm35nph));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(89));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplsnmu9a7htqov), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_h02tha7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_h02tha7 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_suo9n1e1rnjb6));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tple0332mlih7si), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_j79pp86(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_j79pp86 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sacrl98mg5g43));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(120));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplu3gnb6pu4f3h), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_jvf1ma2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jvf1ma2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqcvjnsub5bfs));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tploj8vgi84m11i), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_k6j7893(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_k6j7893 F;
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sruq3cf9ro3q2));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(65));
    gm_tq3m5qgvkpje8 t3 = GM_LD(gm_tq3m5qgvkpje8, ((uint8_t *)a0));
    gm_f_format_b73b468((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplm9t4pej720h2), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_ksi8bsm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ksi8bsm F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s5q33up034m64));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(36));
    gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
    gm_f_format_rkil4k4((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnlqa25lecq48), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_les73rv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_les73rv F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sfl0m0vifcu3m));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(81));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl1nqa0en23gvp), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_mlqiori(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_mlqiori F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s92lujmr0a2c3));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7ilj5n9objtq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_oq31u8d(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_oq31u8d F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smhli04hme60d));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplm8s7gnb70j1j), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_td6patj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_td6patj F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snuinkttlbd28));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl7m3t7r6fqpf7), t3);
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
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ir0h3tc_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_vj3ckdj_2();
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
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nof7vfj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_rcd2jln();
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
inline __attribute__((aligned(4))) void gm_f_list_qtk22f3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_qtk22f3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_moftl2f();
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
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vp1djv1 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_g3tddrb();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_placeholders_text_rtvia6b(void *env, gm_node *cx, gm_node *inst, uint64_t a0) {
    struct gm_fr_placeholders_text_rtvia6b F;
    uint64_t gm_l0; /* n */
    GM_STACK_CHECK(&F, 170u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    gm_tabb710d9mq2e gm_l2;
    uint64_t t2 = gm_l0;
    uint8_t t3 = ((t2 == ((uint64_t)1ULL)));
    if (t3) {
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s7an5gtrdgen2));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(13));
        gm_l1 = t4;
    } else {
        uint64_t t5 = gm_l0;
        gm_t4ce0jd3tdro4 t6;
        GM_ST(uint64_t, ((uint8_t *)&t6 + 0), t5);
        gm_f_format_9r6mq5d((void *)&gm_l2, (void *)NULL, cx, NULL, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
        if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_src6d57grm686e3, 349, 97);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_l1 = t9;
    }
    gm_b16_8 t10 = gm_l1;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0p2fb2b(void * a0, gm_tuo61a5rdrbtf a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0p2fb2b F;
    void * gm_l0; /* l */
    gm_tuo61a5rdrbtf gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_n936iee();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_thihbkv(t8, ((uint64_t)1ULL), t10);
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
    gm_tuo61a5rdrbtf t18 = gm_l1;
    GM_ST(gm_tuo61a5rdrbtf, (((uint8_t *)t16) + (uintptr_t)t17 * 12), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 12)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d6c76ud_2 F;
    void * gm_l0; /* l */
    gm_trcmmd12cefb1 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
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
        gm_b16_8 t9 = gm_f_empty_slice_vj3ckdj_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h08har5_2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 32), ((uint8_t *)&gm_l1), 32);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d943vgp F;
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
        gm_b16_8 t9 = gm_f_empty_slice_g3tddrb();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_61fb705(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_k3l9nic F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_rcd2jln();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_j4iu2lk(t8, ((uint64_t)1ULL), t10);
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
    gm_t88rcucg1b7uk t18 = gm_l1;
    GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)t16) + (uintptr_t)t17 * 4), t18);
    void * t19 = gm_l0;
    void * t20 = gm_l0;
    void * t21 = GM_LD(void *, ((uint8_t *)t20));
    uint64_t t22 = gm_l2;
    uint64_t r23; bool o24 = __builtin_add_overflow(t22, ((uint64_t)1ULL), &r23);
    uint64_t t25 = r23;
    uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t27;
    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t21 + (uintptr_t)((uint64_t)0ULL) * 4)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_nbmosph(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_nbmosph F;
    void * gm_l0; /* l */
    gm_t7g88i52rrtm5 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_moftl2f();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_a6uf79v(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 24), ((uint8_t *)a1), 24);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 24)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_61fb705 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_a6uf79v(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_a6uf79v F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h08har5_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j4iu2lk F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)4ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_thihbkv(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_thihbkv F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)12ULL), ((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_span_text_sv6qv18(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_span_text_sv6qv18 F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)t2));
    void * t4 = GM_LD(void *, ((uint8_t *)t3));
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t7 = gm_f_file_o15fqku(t5, t6);
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 16));
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t7) + 16) + 8));
    uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t11 = (uint64_t)t10;
    uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t13 = (uint64_t)t12;
    uint64_t t14 = (uintptr_t)t11;
    uint64_t t15 = (uintptr_t)t13;
    if (t14 > t15 || t15 > (uintptr_t)t9) gm_trap_slice(t14, t15, t9, gm_srcrorgjlg51brd, 723, 60);
    uint64_t t16 = ((uintptr_t)t13) - (uintptr_t)t11;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(((uint8_t *)t8 + (uintptr_t)t11 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(t16));
    F.ret = t17;
    goto gm_fret;
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
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_temp_ilbvj8q F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shv1k5m05egbe));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_tl874t2atnlh3 t5 = gm_l2;
    uint32_t t6 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t2, t3, t4, ((uint8_t)1), t5);
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
inline __attribute__((aligned(4))) gm_taari1s0m51a7 gm_f_align_of_mks0ipd(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_align_of_mks0ipd F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 26u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_taari1s0m51a7 gm_l1;
    {
        {
            uint8_t t7 = gm_l0;
            uint8_t t8 = ((t7 == ((uint8_t)60ULL)));
            if (!t8) {
                goto gm_lb2;
            }
            gm_t9qkh0rsooc8d t9;
            GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)1ULL));
            gm_taari1s0m51a7 t10;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t10), t9);
            ((uint8_t *)&t10)[1] = 1;
            gm_l1 = t10;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t11 = gm_l0;
            uint8_t t12 = ((t11 == ((uint8_t)62ULL)));
            if (!t12) {
                goto gm_lb3;
            }
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)2ULL));
            gm_taari1s0m51a7 t14;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[1] = 1;
            gm_l1 = t14;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t15 = gm_l0;
            uint8_t t16 = ((t15 == ((uint8_t)94ULL)));
            if (!t16) {
                goto gm_lb4;
            }
            gm_t9qkh0rsooc8d t17;
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)3ULL));
            gm_taari1s0m51a7 t18;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18), t17);
            ((uint8_t *)&t18)[1] = 1;
            gm_l1 = t18;
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_taari1s0m51a7 t19;
            memset(&t19, 0, sizeof t19);
            gm_l1 = t19;
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 113, 43);
    }
gm_lb1:;
    gm_taari1s0m51a7 t20 = gm_l1;
    F.ret = t20;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_build_write_k6dcf8r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_build_write_k6dcf8r F;
    void * gm_l0; /* cx */
    void * gm_l2; /* template */
    gm_b16_8 gm_l3; /* rest */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* t */
    gm_b16_8 gm_l6; /* bytes */
    gm_tibupt4dmcqai gm_l7; /* lit */
    gm_b16_8 gm_l8; /* b */
    gm_b16_8 gm_l9; /* b */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* k */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* k */
    void * gm_l17; /* value */
    void * gm_l18; /* write */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_trcmmd12cefb1 gm_l22; /* st */
    GM_STACK_CHECK(&F, 1868u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l23;
    uint8_t gm_l24;
    gm_th2lqbo77pvsc gm_l25;
    gm_th2lqbo77pvsc gm_l26;
    gm_ti6pdj2sunpe4 gm_l27;
    uint8_t gm_l28;
    gm_t8cc1mtbrhlpt gm_l29;
    gm_tq7s98ihonkju gm_l30;
    gm_tq7s98ihonkju gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    {
        void * t32 = gm_l2;
        {
            {
                uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                uint8_t t34 = (uint8_t)(t33 == ((uint8_t)6ULL));
                if (!t34) {
                    goto gm_lb2;
                }
                gm_l23 = ((uint8_t)1);
                goto gm_lb1;
            }
gm_lb2:;
            {
                gm_l23 = ((uint8_t)0);
                goto gm_lb1;
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 285, 8);
        }
gm_lb1:;
        uint8_t t35 = gm_l23;
        if (t35) {
            void * t36 = gm_l0;
            void * t37 = gm_l2;
            void * t38 = gm_l0;
            void * t39 = GM_LD(void *, ((uint8_t *)t38));
            void * t40 = (void *)(((uint8_t *)t39) + 8);
            gm_t88rcucg1b7uk t41 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tkn42u7na3t81 t42;
            GM_ST(uint8_t, (uint8_t *)&t42 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t42 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t42), t41);
            ((uint8_t *)&t42)[4] = 1;
            void * t43 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t36, t37, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t43;
            void * t44 = gm_l5;
            {
                {
                    uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t44) + 8) + 0));
                    uint8_t t46 = (uint8_t)(t45 == ((uint8_t)9ULL));
                    if (!t46) {
                        goto gm_lb5;
                    }
                    gm_l24 = ((uint8_t)1);
                    goto gm_lb4;
                }
gm_lb5:;
                {
                    gm_l24 = ((uint8_t)0);
                    goto gm_lb4;
                }
gm_lb6:;
                gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 287, 12);
            }
gm_lb4:;
            uint8_t t47 = gm_l24;
            if (t47) {
                void * t48 = gm_l0;
                void * t49 = gm_l5;
                gm_b16_8 t50 = gm_l3;
                gm_tl874t2atnlh3 t51 = gm_l4;
                void * t52 = gm_f_write_deferred_kbicf34((void *)NULL, cx, NULL, t48, &(*(gm_th1g69d7vpipg *)((uint8_t *)&F.l1)), t49, t50, t51);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t52;
                goto gm_fret;
            }
        }
        gm_b16_8 t53 = gm_f_empty_slice_ijtp9lo();
        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&t53));
        gm_l6 = t54;
        {
            {
                void * t55 = gm_l2;
                uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t55) + 8) + 0));
                uint8_t t57 = (uint8_t)(t56 == ((uint8_t)2ULL));
                if (!t57) {
                    goto gm_lb8;
                }
                memmove(((uint8_t *)&gm_l7), ((((uint8_t *)t55) + 8) + 8), 24);
                gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                gm_l6 = t59;
                goto gm_lb7;
            }
gm_lb8:;
            {
                {
                    void * t60 = gm_l0;
                    void * t61 = gm_l2;
                    void * t62 = gm_l0;
                    uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)t62) + 8));
                    gm_f_const_template_5dvtt1m((void *)&gm_l25, (void *)NULL, cx, NULL, t60, t61, t63, ((uint32_t)0ULL));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t64 = (uint8_t)((((uint8_t *)&gm_l25))[16] != 0);
                    if (!t64) {
                        goto gm_lb10;
                    }
                    gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l25));
                    gm_l8 = t65;
                    gm_b16_8 t66 = gm_l8;
                    gm_l6 = t66;
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    {
                        void * t67 = gm_l0;
                        void * t68 = GM_LD(void *, ((uint8_t *)t67));
                        void * t69 = gm_l0;
                        uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)t69) + 8));
                        void * t71 = gm_l2;
                        gm_f_const_string_jdjj62s((void *)&gm_l26, (void *)NULL, cx, NULL, t68, t70, t71);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t72 = (uint8_t)((((uint8_t *)&gm_l26))[16] != 0);
                        if (!t72) {
                            goto gm_lb12;
                        }
                        gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l26));
                        gm_l9 = t73;
                        gm_b16_8 t74 = gm_l9;
                        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                        gm_l6 = t75;
                        goto gm_lb11;
                    }
gm_lb12:;
                    void * t76 = gm_l0;
                    void * t77 = gm_l2;
                    gm_tl874t2atnlh3 t78 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t77) + 72));
                    gm_b16_8 t79;
                    GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sves946fckobv));
                    GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(103));
                    gm_b0_1 t80 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t76, t78, t79);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t81 = gm_l0;
                    void * t82 = GM_LD(void *, ((uint8_t *)t81));
                    gm_tl874t2atnlh3 t83 = gm_l4;
                    void * t84 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t82, t83);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t84;
                    goto gm_fret;
                }
gm_lb11:;
            }
gm_lb9:;
        }
gm_lb7:;
    }
    {
        void * t85 = gm_l0;
        gm_b16_8 t86 = gm_l6;
        void * t87 = gm_l2;
        gm_tl874t2atnlh3 t88 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + 72));
        gm_b16_8 t89 = gm_l3;
        gm_tl874t2atnlh3 t90 = gm_l4;
        gm_f_template_args_vf1gdn1((void *)&gm_l27, (void *)NULL, cx, NULL, t85, t86, t88, t89, t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t91 = (uint8_t)((((uint8_t *)&gm_l27))[64] != 0);
        if (t91) {
            memmove(((uint8_t *)&F.l10), ((uint8_t *)&gm_l27), 64);
        } else {
            void * t92 = gm_l0;
            void * t93 = GM_LD(void *, ((uint8_t *)t92));
            gm_tl874t2atnlh3 t94 = gm_l4;
            void * t95 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t93, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t95;
            goto gm_fret;
        }
        gm_l12 = ((uint64_t)0ULL);
        uint64_t t96 = GM_LD(uint64_t, ((((uint8_t *)&F.l10) + 32) + 8));
        gm_l11 = t96;
        {
            for (;;) {
                uint64_t t97 = gm_l12;
                uint64_t t98 = gm_l11;
                uint8_t t99 = (t97 < t98);
                if (!t99) {
                    goto gm_lb13;
                }
                {
                    {
                        {
                            uint64_t t100 = gm_l12;
                            gm_l13 = t100;
                            void * t101 = gm_l0;
                            void * t102 = GM_LD(void *, (((uint8_t *)&F.l10) + 32));
                            uint64_t t103 = GM_LD(uint64_t, ((((uint8_t *)&F.l10) + 32) + 8));
                            uint64_t t104 = gm_l13;
                            void * t105 = (void *)(((uint8_t *)t102) + (uintptr_t)t104 * 88);
                            gm_b0_1 t106 = gm_f_fn_written_qtvia9n((void *)NULL, cx, NULL, t101, t105);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb16;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 301, 5);
                    }
gm_lb16:;
                }
gm_lb15:;
                uint64_t t107 = gm_l12;
                uint64_t r108; bool o109 = __builtin_add_overflow(t107, ((uint64_t)1ULL), &r108);
                uint64_t t110 = r108;
                gm_l12 = t110;
            }
        }
gm_lb13:;
        gm_l15 = ((uint64_t)0ULL);
        uint64_t t111 = GM_LD(uint64_t, ((((uint8_t *)&F.l10) + 32) + 8));
        gm_l14 = t111;
        {
            for (;;) {
                uint64_t t112 = gm_l15;
                uint64_t t113 = gm_l14;
                uint8_t t114 = (t112 < t113);
                if (!t114) {
                    goto gm_lb18;
                }
                {
                    {
                        {
                            uint64_t t115 = gm_l15;
                            gm_l16 = t115;
                            void * t116 = gm_l0;
                            void * t117 = GM_LD(void *, (((uint8_t *)&F.l10) + 32));
                            uint64_t t118 = GM_LD(uint64_t, ((((uint8_t *)&F.l10) + 32) + 8));
                            uint64_t t119 = gm_l16;
                            void * t120 = (void *)(((uint8_t *)t117) + (uintptr_t)t119 * 88);
                            gm_f_soa_of_jufdcj2((void *)&gm_l29, (void *)NULL, cx, NULL, t116, t120);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            {
                                {
                                    uint8_t t121 = (uint8_t)((((uint8_t *)&gm_l29))[32] != 0);
                                    if (t121) {
                                        goto gm_lb24;
                                    }
                                    gm_l28 = ((uint8_t)0);
                                    goto gm_lb23;
                                }
gm_lb24:;
                                {
                                    gm_l28 = ((uint8_t)1);
                                    goto gm_lb23;
                                }
gm_lb25:;
                                gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 304, 12);
                            }
gm_lb23:;
                            uint8_t t122 = gm_l28;
                            if (t122) {
                                {
                                    void * t123 = GM_LD(void *, (((uint8_t *)&F.l10) + 32));
                                    uint64_t t124 = GM_LD(uint64_t, ((((uint8_t *)&F.l10) + 32) + 8));
                                    uint64_t t125 = gm_l16;
                                    gm_tq7s98ihonkju t126 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_tsf6i2agfnpva *)(((uint8_t *)t123) + (uintptr_t)t125 * 88)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kmj9t14cb35)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l30 = t126;
                                }
                                uint8_t t127 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                                if (!t127) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_src6d57grm686e3, 305, 22);
                                void * t128 = GM_LD(void *, (((uint8_t *)&gm_l30) + 8));
                                gm_l17 = t128;
                                {
                                    void * t129 = GM_LD(void *, (((uint8_t *)&F.l10) + 32));
                                    uint64_t t130 = GM_LD(uint64_t, ((((uint8_t *)&F.l10) + 32) + 8));
                                    uint64_t t131 = gm_l16;
                                    void * t132 = gm_l0;
                                    void * t133 = gm_l17;
                                    gm_b16_8 t134;
                                    GM_ST(void *, (uint8_t *)&t134, (void *)(gm_s4kt3mhlp0or9));
                                    GM_ST(uintptr_t, (uint8_t *)&t134 + 8, (uintptr_t)(10));
                                    void * t135 = gm_f_soa_columns_tuple_ns477md((void *)NULL, cx, NULL, t132, t133, t134);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    memmove((((uint8_t *)t129) + (uintptr_t)t131 * 88), ((uint8_t *)t135), 88);
                                }
                            }
                            goto gm_lb21;
                        }
gm_lb22:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 303, 5);
                    }
gm_lb21:;
                }
gm_lb20:;
                uint64_t t136 = gm_l15;
                uint64_t r137; bool o138 = __builtin_add_overflow(t136, ((uint64_t)1ULL), &r137);
                uint64_t t139 = r137;
                gm_l15 = t139;
            }
        }
gm_lb18:;
    }
    {
        void * t140 = gm_l0;
        void * t141 = GM_LD(void *, ((uint8_t *)t140));
        void * t142 = (void *)(((uint8_t *)t141) + 8);
        gm_t88rcucg1b7uk t143 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t142);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
        gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 16));
        gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 32));
        gm_th1g69d7vpipg t147 = GM_LD(gm_th1g69d7vpipg, ((uint8_t *)&F.l1));
        gm_t150deeftog49 t148;
        GM_ST(gm_th1g69d7vpipg, ((uint8_t *)&t148 + 0), t147);
        GM_ST(gm_b16_8, ((uint8_t *)&t148 + 40), t144);
        GM_ST(gm_b16_8, ((uint8_t *)&t148 + 56), t145);
        GM_ST(gm_b16_8, ((uint8_t *)&t148 + 72), t146);
        GM_ST(void *, ((uint8_t *)&t148 + 88), ((void *)0));
        gm_tq7s98ihonkju t149 = gm_f_new_2pnaa87((void *)NULL, cx, NULL, &t148, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kl9vm3d7kkf)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l31 = t149;
        uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
        if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_src6d57grm686e3, 309, 45);
        void * t151 = GM_LD(void *, (((uint8_t *)&gm_l31) + 8));
        gm_tl874t2atnlh3 t152 = gm_l4;
        gm_to0f5qn75cgg8 t153;
        memset(&t153, 0, sizeof t153);
        GM_ST(uint8_t, ((uint8_t *)&t153 + 0), ((uint8_t)55ULL));
        GM_ST(void *, ((uint8_t *)&t153 + 8), t151);
        void * t154 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t143, &t153, t152);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l18 = t154;
        uint64_t t155 = GM_LD(uint64_t, ((((uint8_t *)&F.l10) + 48) + 8));
        uint8_t t156 = ((t155 == ((uint64_t)0ULL)));
        if (t156) {
            void * t157 = gm_l18;
            F.ret = t157;
            goto gm_fret;
        }
        gm_trqfvmjtt4sij t158;
        gm_f_list_ir0h3tc_2((void *)&t158, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l19 = t158;
        gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 48));
        gm_l20 = t159;
        gm_l21 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t160 = gm_l21;
                uint64_t t161 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                uint8_t t162 = (t160 < t161);
                if (!t162) {
                    goto gm_lb26;
                }
                {
                    void * t163 = GM_LD(void *, ((uint8_t *)&gm_l20));
                    uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                    uint64_t t165 = gm_l21;
                    {
                        {
                            {
                                memmove(((uint8_t *)&gm_l22), (((uint8_t *)t163) + (uintptr_t)t165 * 32), 32);
                            }
                            {
                                void * t166 = (void *)((uint8_t *)&F.l19);
                                gm_tftb1odc739t5 t167 = gm_f_push_d6c76ud_2(t166, &(*(gm_trcmmd12cefb1 *)((uint8_t *)&gm_l22)));
                                gm_l32 = t167;
                            }
                            uint8_t t168 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                            if (!t168) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_src6d57grm686e3, 312, 26);
                            goto gm_lb29;
                        }
gm_lb30:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 312, 5);
                    }
gm_lb29:;
                }
gm_lb28:;
                uint64_t t169 = gm_l21;
                uint64_t r170; bool o171 = __builtin_add_overflow(t169, ((uint64_t)1ULL), &r170);
                uint64_t t172 = r170;
                gm_l21 = t172;
            }
        }
gm_lb26:;
        void * t173 = (void *)((uint8_t *)&F.l19);
        void * t174 = gm_l18;
        gm_trcmmd12cefb1 t175;
        GM_ST(uint8_t, (uint8_t *)&t175 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t175 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t175 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t175 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t175 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)4ULL));
        GM_ST(void *, ((uint8_t *)&t175 + 8), t174);
        gm_tftb1odc739t5 t176 = gm_f_push_d6c76ud_2(t173, &t175);
        gm_l33 = t176;
        uint8_t t177 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
        if (!t177) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_src6d57grm686e3, 313, 5);
        void * t178 = gm_l0;
        void * t179 = GM_LD(void *, ((uint8_t *)t178));
        gm_b16_8 t180 = GM_LD(gm_b16_8, ((uint8_t *)&F.l19));
        gm_tl874t2atnlh3 t181 = gm_l4;
        void * t182 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t179, t180, t181);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t182;
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
__attribute__((aligned(4))) void gm_f_check_spec_4vp7jhg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_check_spec_4vp7jhg F;
    void * gm_l0; /* s */
    void * gm_l1; /* spec */
    gm_t88rcucg1b7uk gm_l2; /* t */
    void * gm_l3; /* tys */
    uint8_t gm_l4; /* int */
    uint8_t gm_l5; /* float */
    uint8_t gm_l6; /* based */
    uint8_t gm_l7; /* bytes */
    gm_t88rcucg1b7uk gm_l8; /* elem */
    gm_th2lqbo77pvsc gm_l9; /* want */
    gm_b16_8 gm_l10; /* w */
    GM_STACK_CHECK(&F, 750u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    gm_t53at80pfdus3 gm_l15;
    uint8_t gm_l16;
    uint8_t gm_l17;
    uint8_t gm_l18;
    gm_tabb710d9mq2e gm_l19;
    void * t20 = gm_l0;
    void * t21 = (void *)(((uint8_t *)t20) + 8);
    gm_l3 = t21;
    void * t22 = gm_l3;
    gm_t88rcucg1b7uk t23 = gm_l2;
    uint8_t t24 = gm_f_is_integer_8d4f9ek(t22, GM_CONV(gm_csguankko4lmhd, t23));
    gm_l4 = t24;
    {
        void * t25 = gm_l3;
        gm_t88rcucg1b7uk t26 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&gm_l11, t25, GM_CONV(gm_csguankko4lmhd, t26));
    }
    {
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)3ULL));
            if (!t28) {
                goto gm_lb2;
            }
            gm_l5 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l5 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 216, 14);
    }
gm_lb1:;
    void * t29 = gm_l1;
    gm_t9qkh0rsooc8d t30 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t29) + 20));
    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
    uint8_t t32 = (uint8_t)((t31 == ((uint8_t)1ULL)));
    gm_t9qkh0rsooc8d t33;
    GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)1ULL));
    if (t32) {
        gm_l14 = ((uint8_t)1);
    } else {
        void * t34 = gm_l1;
        gm_t9qkh0rsooc8d t35 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t34) + 20));
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
        uint8_t t37 = (uint8_t)((t36 == ((uint8_t)2ULL)));
        gm_t9qkh0rsooc8d t38;
        GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)2ULL));
        gm_l14 = t37;
    }
    uint8_t t39 = gm_l14;
    if (t39) {
        gm_l13 = ((uint8_t)1);
    } else {
        void * t40 = gm_l1;
        gm_t9qkh0rsooc8d t41 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t40) + 20));
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t41) + 0));
        uint8_t t43 = (uint8_t)((t42 == ((uint8_t)3ULL)));
        gm_t9qkh0rsooc8d t44;
        GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)3ULL));
        gm_l13 = t43;
    }
    uint8_t t45 = gm_l13;
    if (t45) {
        gm_l12 = ((uint8_t)1);
    } else {
        void * t46 = gm_l1;
        gm_t9qkh0rsooc8d t47 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t46) + 20));
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t47) + 0));
        uint8_t t49 = (uint8_t)((t48 == ((uint8_t)4ULL)));
        gm_t9qkh0rsooc8d t50;
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)4ULL));
        gm_l12 = t49;
    }
    uint8_t t51 = gm_l12;
    gm_l6 = t51;
    gm_l7 = ((uint8_t)0);
    {
        {
            {
                void * t52 = gm_l3;
                gm_t88rcucg1b7uk t53 = gm_l2;
                gm_f_kind_jcnt4hm((void *)&gm_l15, t52, GM_CONV(gm_csguankko4lmhd, t53));
            }
            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
            uint8_t t55 = (uint8_t)(t54 == ((uint8_t)10ULL));
            if (!t55) {
                goto gm_lb5;
            }
            gm_t88rcucg1b7uk t56 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l15) + 8) + 4));
            gm_l8 = t56;
            {
                void * t57 = gm_l3;
                gm_t88rcucg1b7uk t58 = gm_l8;
                gm_t53at80pfdus3 t59;
                gm_f_kind_jcnt4hm((void *)&t59, t57, GM_CONV(gm_csguankko4lmhd, t58));
                gm_tjcj4j2gbh6c8 t60;
                GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
                GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)0));
                GM_ST(uint16_t, ((uint8_t *)&t60 + 2), ((uint16_t)8ULL));
                uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&t59) + 0));
                uint8_t t62 = (uint8_t)(t61 == ((uint8_t)0ULL));
                if (t62) {
                    gm_tjcj4j2gbh6c8 t63 = GM_LD(gm_tjcj4j2gbh6c8, (((uint8_t *)&t59) + 8));
                    t62 = (uint8_t)gm_eqo3v9599nus(((uint8_t *)&t63), ((uint8_t *)&t60));
                }
                uint8_t t64 = (uint8_t)(t62);
                gm_t53at80pfdus3 t65;
                memset(&t65, 0, sizeof t65);
                GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t65 + 8), t60);
                gm_l7 = t64;
            }
            goto gm_lb4;
        }
gm_lb5:;
    }
gm_lb4:;
    {
        memset(&gm_l9, 0, sizeof gm_l9);
    }
    {
        {
            void * t66 = gm_l1;
            gm_t9qkh0rsooc8d t67 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t66) + 20));
            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t67) + 0));
            uint8_t t69 = (uint8_t)((t68 == ((uint8_t)6ULL)));
            if (!t69) {
                goto gm_lb7;
            }
            uint8_t t70 = gm_l7;
            uint8_t t71 = (uint8_t)!t70;
            if (!t71) {
                goto gm_lb7;
            }
            gm_b16_8 t72;
            GM_ST(void *, (uint8_t *)&t72, (void *)(gm_s93i7ga7kbfpu));
            GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(33));
            gm_t9qkh0rsooc8d t73;
            GM_ST(uint8_t, ((uint8_t *)&t73 + 0), ((uint8_t)6ULL));
            GM_ST(uint8_t, (uint8_t *)&gm_l9 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l9 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l9 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&gm_l9), t72);
            ((uint8_t *)&gm_l9)[16] = 1;
            goto gm_lb6;
        }
gm_lb7:;
        {
            {
                uint8_t t74 = gm_l6;
                if (!t74) {
                    goto gm_lb9;
                }
                uint8_t t75 = gm_l4;
                uint8_t t76 = (uint8_t)!t75;
                if (!t76) {
                    goto gm_lb9;
                }
                gm_b16_8 t77;
                GM_ST(void *, (uint8_t *)&t77, (void *)(gm_sggmh06s4b0cs));
                GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(22));
                GM_ST(uint8_t, (uint8_t *)&gm_l9 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l9 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l9 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&gm_l9), t77);
                ((uint8_t *)&gm_l9)[16] = 1;
                goto gm_lb8;
            }
gm_lb9:;
            {
                {
                    void * t78 = gm_l1;
                    gm_t9qkh0rsooc8d t79 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t78) + 20));
                    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&t79) + 0));
                    uint8_t t81 = (uint8_t)((t80 == ((uint8_t)5ULL)));
                    if (!t81) {
                        goto gm_lb11;
                    }
                    uint8_t t82 = gm_l5;
                    uint8_t t83 = (uint8_t)!t82;
                    if (!t83) {
                        goto gm_lb11;
                    }
                    gm_b16_8 t84;
                    GM_ST(void *, (uint8_t *)&t84, (void *)(gm_s3on77ql0ndko));
                    GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(16));
                    gm_t9qkh0rsooc8d t85;
                    GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)5ULL));
                    GM_ST(uint8_t, (uint8_t *)&gm_l9 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&gm_l9 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&gm_l9 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&gm_l9), t84);
                    ((uint8_t *)&gm_l9)[16] = 1;
                    goto gm_lb10;
                }
gm_lb11:;
                {
                    {
                        void * t86 = gm_l1;
                        {
                            {
                                uint8_t t87 = (uint8_t)(((((uint8_t *)t86) + 12))[4] != 0);
                                if (t87) {
                                    goto gm_lb15;
                                }
                                gm_l16 = ((uint8_t)0);
                                goto gm_lb14;
                            }
gm_lb15:;
                            {
                                gm_l16 = ((uint8_t)1);
                                goto gm_lb14;
                            }
gm_lb16:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 227, 15);
                        }
gm_lb14:;
                        uint8_t t88 = gm_l16;
                        if (!t88) {
                            goto gm_lb13;
                        }
                        uint8_t t89 = gm_l5;
                        uint8_t t90 = (uint8_t)!t89;
                        if (!t90) {
                            goto gm_lb13;
                        }
                        gm_b16_8 t91;
                        GM_ST(void *, (uint8_t *)&t91, (void *)(gm_s23qsffuqlc1v));
                        GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(24));
                        GM_ST(uint8_t, (uint8_t *)&gm_l9 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l9 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l9 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l9), t91);
                        ((uint8_t *)&gm_l9)[16] = 1;
                        goto gm_lb12;
                    }
gm_lb13:;
                    {
                        {
                            void * t92 = gm_l1;
                            uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)t92) + 2));
                            if (t93) {
                                gm_l17 = ((uint8_t)1);
                            } else {
                                void * t94 = gm_l1;
                                uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)t94) + 4));
                                gm_l17 = t95;
                            }
                            uint8_t t96 = gm_l17;
                            if (!t96) {
                                goto gm_lb18;
                            }
                            uint8_t t97 = gm_l4;
                            if (t97) {
                                gm_l18 = ((uint8_t)1);
                            } else {
                                uint8_t t98 = gm_l5;
                                gm_l18 = t98;
                            }
                            uint8_t t99 = gm_l18;
                            uint8_t t100 = (uint8_t)!t99;
                            if (!t100) {
                                goto gm_lb18;
                            }
                            gm_b16_8 t101;
                            GM_ST(void *, (uint8_t *)&t101, (void *)(gm_soreld657hea2));
                            GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(24));
                            GM_ST(uint8_t, (uint8_t *)&gm_l9 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&gm_l9 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&gm_l9 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&gm_l9), t101);
                            ((uint8_t *)&gm_l9)[16] = 1;
                            goto gm_lb17;
                        }
gm_lb18:;
                    }
gm_lb17:;
                }
gm_lb12:;
            }
gm_lb10:;
        }
gm_lb8:;
    }
gm_lb6:;
    uint8_t t102 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
    if (t102) {
        gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
        gm_l10 = t103;
    } else {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    void * t104 = gm_l3;
    gm_t88rcucg1b7uk t105 = gm_l2;
    uint8_t t106 = gm_f_is_error_8k27gd1(t104, GM_CONV(gm_csguankko4lmhd, t105));
    if (t106) {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    {
        gm_b16_8 t107 = gm_l10;
        void * t108 = gm_l0;
        gm_t88rcucg1b7uk t109 = gm_l2;
        gm_b16_8 t110 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t108, t109);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6j6smj7qoctu t111;
        GM_ST(gm_b16_8, ((uint8_t *)&t111 + 0), t107);
        GM_ST(gm_b16_8, ((uint8_t *)&t111 + 16), t110);
        gm_f_format_0t8rkc3((void *)&gm_l19, (void *)NULL, cx, NULL, &t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
    if (!t112) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_src6d57grm686e3, 234, 12);
    {
        gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
        gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t114);
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
__attribute__((aligned(4))) void gm_f_check_template_brtb8t0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_check_template_brtb8t0 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_tkn42u7na3t81 gm_l3; /* args */
    void * gm_l4; /* tys */
    uint64_t gm_l5; /* n */
    gm_th2lqbo77pvsc gm_l6; /* elems */
    gm_t88rcucg1b7uk gm_l7; /* a */
    gm_b16_8 gm_l8; /* es */
    gm_t88rcucg1b7uk gm_l9; /* a */
    gm_b16_8 gm_l10; /* es */
    gm_b16_8 gm_l11; /* msg */
    gm_b16_8 gm_l12; /* given */
    uint8_t gm_l13; /* ok */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* k */
    uint64_t gm_l17; /* at */
    gm_b16_8 gm_l18; /* msg */
    GM_STACK_CHECK(&F, 1387u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l19;
    gm_tabb710d9mq2e gm_l20;
    gm_th2lqbo77pvsc gm_l21;
    gm_tabb710d9mq2e gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_tabb710d9mq2e gm_l24;
    gm_th2lqbo77pvsc gm_l25;
    gm_tabb710d9mq2e gm_l26;
    gm_th2lqbo77pvsc gm_l27;
    void * t15 = gm_l0;
    void * t16 = (void *)(((uint8_t *)t15) + 8);
    gm_l4 = t16;
    uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)a2) + 16) + 8));
    gm_l5 = t17;
    {
        gm_b16_8 t18 = gm_f_empty_slice_rcd2jln();
        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
        GM_ST(uint8_t, (uint8_t *)&gm_l6 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l6 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l6 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l6), t19);
        ((uint8_t *)&gm_l6)[16] = 1;
    }
    {
        {
            uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
            if (!t20) {
                goto gm_lb2;
            }
            gm_t88rcucg1b7uk t21 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
            gm_l7 = t21;
            memset(&gm_l6, 0, sizeof gm_l6);
            {
                {
                    {
                        void * t22 = gm_l4;
                        gm_t88rcucg1b7uk t23 = gm_l7;
                        gm_f_kind_jcnt4hm((void *)&gm_l19, t22, GM_CONV(gm_csguankko4lmhd, t23));
                    }
                    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                    uint8_t t25 = (uint8_t)(t24 == ((uint8_t)12ULL));
                    if (!t25) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                    gm_l8 = t26;
                    {
                        gm_b16_8 t27 = gm_l8;
                        GM_ST(uint8_t, (uint8_t *)&gm_l6 + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&gm_l6 + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&gm_l6 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&gm_l6), t27);
                        ((uint8_t *)&gm_l6)[16] = 1;
                    }
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
    uint8_t t28 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
    if (t28) {
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
        gm_l10 = t29;
    } else {
        uint8_t t30 = (uint8_t)((((uint8_t *)&gm_l3))[4] != 0);
        if (t30) {
            gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
            gm_l9 = t31;
        } else {
            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
            goto gm_fdone;
        }
        {
            void * t32 = gm_l0;
            gm_tl874t2atnlh3 t33 = gm_l1;
            uint64_t t34 = gm_l5;
            gm_b16_8 t35 = gm_f_placeholders_text_rtvia6b((void *)NULL, cx, NULL, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t36 = gm_l0;
            gm_t88rcucg1b7uk t37 = gm_l9;
            gm_b16_8 t38 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t39;
            GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t35);
            GM_ST(gm_b16_8, ((uint8_t *)&t39 + 16), t38);
            gm_f_format_cnhv2id((void *)&gm_l20, (void *)NULL, cx, NULL, &t39);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_src6d57grm686e3, 566, 23);
            gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
            gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
            gm_b0_1 t43 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t32, t33, t42);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
            goto gm_fdone;
        }
    }
    gm_b16_8 t44 = gm_l10;
    gm_l10 = t44;
    uint64_t t45 = GM_LD(uint64_t, ((((uint8_t *)a2) + 32) + 8));
    uint8_t t46 = (t45 > ((uint64_t)0ULL));
    if (t46) {
        {
            {
                {
                    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    gm_tfeab279c16rr t48 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a2));
                    gm_f_numbered_error_07hll20((void *)&gm_l21, (void *)NULL, cx, NULL, &t48, t47);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t49 = (uint8_t)((((uint8_t *)&gm_l21))[16] != 0);
                if (!t49) {
                    goto gm_lb6;
                }
                gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l21));
                gm_l11 = t50;
                {
                    void * t51 = gm_l0;
                    gm_tl874t2atnlh3 t52 = gm_l1;
                    gm_b16_8 t53 = gm_l11;
                    gm_tnpifv20irnph t54;
                    GM_ST(gm_b16_8, ((uint8_t *)&t54 + 0), t53);
                    gm_f_format_mlqiori((void *)&gm_l22, (void *)NULL, cx, NULL, t54);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                    if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 8, gm_src6d57grm686e3, 571, 27);
                    gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                    gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                    gm_b0_1 t58 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t51, t52, t57);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                    goto gm_fdone;
                }
            }
gm_lb6:;
        }
gm_lb5:;
    } else {
        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
        uint64_t t60 = gm_l5;
        uint8_t t61 = (!(t59 == t60));
        if (t61) {
            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
            uint8_t t63 = ((t62 == ((uint64_t)1ULL)));
            if (t63) {
                gm_b16_8 t64;
                GM_ST(void *, (uint8_t *)&t64, (void *)(gm_scne9acm49p4h));
                GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(10));
                gm_l12 = t64;
            } else {
                uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                gm_t4ce0jd3tdro4 t66;
                GM_ST(uint64_t, ((uint8_t *)&t66 + 0), t65);
                gm_f_format_td6patj((void *)&gm_l23, (void *)NULL, cx, NULL, t66);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_src6d57grm686e3, 575, 57);
                gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
                gm_l12 = t69;
            }
            {
                void * t70 = gm_l0;
                gm_tl874t2atnlh3 t71 = gm_l1;
                uint64_t t72 = gm_l5;
                gm_b16_8 t73 = gm_f_placeholders_text_rtvia6b((void *)NULL, cx, NULL, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t74 = gm_l12;
                gm_t6j6smj7qoctu t75;
                GM_ST(gm_b16_8, ((uint8_t *)&t75 + 0), t73);
                GM_ST(gm_b16_8, ((uint8_t *)&t75 + 16), t74);
                gm_f_format_2rf8n5r((void *)&gm_l24, (void *)NULL, cx, NULL, &t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_src6d57grm686e3, 576, 23);
                gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                gm_b0_1 t79 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t70, t71, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                goto gm_fdone;
            }
        }
    }
    gm_l13 = ((uint8_t)1);
    gm_l15 = ((uint64_t)0ULL);
    uint64_t t80 = gm_l5;
    gm_l14 = t80;
    {
        for (;;) {
            uint64_t t81 = gm_l15;
            uint64_t t82 = gm_l14;
            uint8_t t83 = (t81 < t82);
            if (!t83) {
                goto gm_lb7;
            }
            {
                {
                    {
                        uint64_t t84 = gm_l15;
                        gm_l16 = t84;
                        uint64_t t85 = GM_LD(uint64_t, ((((uint8_t *)a2) + 32) + 8));
                        uint8_t t86 = (t85 > ((uint64_t)0ULL));
                        if (t86) {
                            void * t87 = GM_LD(void *, (((uint8_t *)a2) + 32));
                            uint64_t t88 = GM_LD(uint64_t, ((((uint8_t *)a2) + 32) + 8));
                            uint64_t t89 = gm_l16;
                            if ((uintptr_t)t89 >= (uintptr_t)(t88)) gm_trap_index((uintptr_t)t89, t88, gm_src6d57grm686e3, 581, 43);
                            uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)t87) + (uintptr_t)t89 * 8));
                            gm_l17 = t90;
                        } else {
                            uint64_t t91 = gm_l16;
                            gm_l17 = t91;
                        }
                        {
                            {
                                {
                                    void * t92 = gm_l0;
                                    void * t93 = GM_LD(void *, (((uint8_t *)a2) + 16));
                                    uint64_t t94 = GM_LD(uint64_t, ((((uint8_t *)a2) + 16) + 8));
                                    uint64_t t95 = gm_l16;
                                    void * t96 = (void *)(((uint8_t *)t93) + (uintptr_t)t95 * 24);
                                    void * t97 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                    uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                    uint64_t t99 = gm_l17;
                                    if ((uintptr_t)t99 >= (uintptr_t)(t98)) gm_trap_index((uintptr_t)t99, t98, gm_src6d57grm686e3, 582, 51);
                                    gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t97) + (uintptr_t)t99 * 4));
                                    gm_f_check_spec_4vp7jhg((void *)&gm_l25, (void *)NULL, cx, NULL, t92, t96, t100);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t101 = (uint8_t)((((uint8_t *)&gm_l25))[16] != 0);
                                if (!t101) {
                                    goto gm_lb13;
                                }
                                gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l25));
                                gm_l18 = t102;
                                {
                                    void * t103 = gm_l0;
                                    gm_tl874t2atnlh3 t104 = gm_l1;
                                    gm_b16_8 t105 = gm_l18;
                                    gm_tnpifv20irnph t106;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t106 + 0), t105);
                                    gm_f_format_mlqiori((void *)&gm_l26, (void *)NULL, cx, NULL, t106);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                    if (!t107) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_src6d57grm686e3, 583, 27);
                                    gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l26) + 8));
                                    gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
                                    gm_b0_1 t110 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t103, t104, t109);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                gm_l13 = ((uint8_t)0);
                                goto gm_lb12;
                            }
gm_lb13:;
                        }
gm_lb12:;
                        goto gm_lb10;
                    }
gm_lb11:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 580, 5);
                }
gm_lb10:;
            }
gm_lb9:;
            uint64_t t111 = gm_l15;
            uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
            uint64_t t114 = r112;
            gm_l15 = t114;
        }
    }
gm_lb7:;
    uint8_t t115 = gm_l13;
    if (t115) {
        gm_b16_8 t116 = gm_l10;
        GM_ST(uint8_t, (uint8_t *)&gm_l27 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&gm_l27 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&gm_l27 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l27), t116);
        ((uint8_t *)&gm_l27)[16] = 1;
    } else {
        memset(&gm_l27, 0, sizeof gm_l27);
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l27), 24);
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
__attribute__((aligned(4))) void gm_f_const_template_5dvtt1m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint64_t a2, uint32_t a3) {
    struct gm_fr_const_template_5dvtt1m F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    uint64_t gm_l2; /* module */
    uint32_t gm_l3; /* depth */
    void * gm_l4; /* inner */
    gm_tibupt4dmcqai gm_l5; /* lit */
    gm_t08olq97rscn3 gm_l6; /* id */
    uint64_t gm_l7; /* d */
    gm_t88rcucg1b7uk gm_l8; /* ty */
    void * gm_l9; /* item */
    GM_STACK_CHECK(&F, 942u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_th2lqbo77pvsc gm_l11;
    uint8_t gm_l12;
    gm_tqvu78rpi6q1h gm_l13;
    gm_tf5srh4mp4b3i gm_l14;
    gm_t6qooom8qa2do gm_l15;
    uint32_t t16 = gm_l3;
    uint8_t t17 = (t16 > ((uint32_t)8ULL));
    if (t17) {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    void * t18 = gm_l1;
    {
        {
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)9ULL));
            if (!t20) {
                goto gm_lb2;
            }
            void * t21 = GM_LD(void *, ((((uint8_t *)t18) + 8) + 8));
            gm_l4 = t21;
            void * t22 = gm_l0;
            void * t23 = gm_l4;
            uint64_t t24 = gm_l2;
            uint32_t t25 = gm_l3;
            gm_f_const_template_5dvtt1m((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t22, t23, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
gm_lb2:;
        {
            uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            uint8_t t27 = (uint8_t)(t26 == ((uint8_t)2ULL));
            if (!t27) {
                goto gm_lb3;
            }
            {
                memmove(((uint8_t *)&gm_l5), ((((uint8_t *)t18) + 8) + 8), 24);
            }
            gm_t9qkh0rsooc8d t28 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l5) + 16));
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t28) + 0));
            uint8_t t30 = (uint8_t)((t29 == ((uint8_t)2ULL)));
            gm_t9qkh0rsooc8d t31;
            GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)2ULL));
            if (t30) {
                memset(&gm_l11, 0, sizeof gm_l11);
            } else {
                gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                GM_ST(uint8_t, (uint8_t *)&gm_l11 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&gm_l11 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&gm_l11 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&gm_l11), t33);
                ((uint8_t *)&gm_l11)[16] = 1;
            }
            {
                memmove(gm_out, ((uint8_t *)&gm_l11), 24);
                goto gm_fdone;
            }
        }
gm_lb3:;
        {
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)6ULL));
            if (!t35) {
                goto gm_lb4;
            }
            memmove(((uint8_t *)&gm_l6), ((((uint8_t *)t18) + 8) + 8), 32);
            {
                {
                    uint32_t t36 = gm_l3;
                    uint8_t t37 = ((t36 == ((uint32_t)0ULL)));
                    if (!t37) {
                        goto gm_lb6;
                    }
                    {
                        void * t38 = gm_l0;
                        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
                        gm_f_lookup_local_6f38t7p((void *)&gm_l13, t38, t39);
                    }
                    {
                        {
                            uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l13))[208] != 0);
                            if (t40) {
                                goto gm_lb8;
                            }
                            gm_l12 = ((uint8_t)0);
                            goto gm_lb7;
                        }
gm_lb8:;
                        {
                            gm_l12 = ((uint8_t)1);
                            goto gm_lb7;
                        }
gm_lb9:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 267, 31);
                    }
gm_lb7:;
                    uint8_t t41 = gm_l12;
                    if (!t41) {
                        goto gm_lb6;
                    }
                    {
                        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                        goto gm_fdone;
                    }
                }
gm_lb6:;
            }
gm_lb5:;
            void * t42 = gm_l0;
            void * t43 = GM_LD(void *, ((uint8_t *)t42));
            uint64_t t44 = gm_l2;
            gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
            gm_tf5srh4mp4b3i t46 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t43, t44, t45);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t46;
            uint8_t t47 = (uint8_t)((((uint8_t *)&gm_l14))[8] != 0);
            if (t47) {
                uint64_t t48 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                gm_l7 = t48;
            } else {
                memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                goto gm_fdone;
            }
            {
                {
                    void * t49 = gm_l0;
                    void * t50 = GM_LD(void *, ((uint8_t *)t49));
                    uint64_t t51 = gm_l7;
                    gm_f_resolve_decl_pus042v((void *)&gm_l15, (void *)NULL, cx, NULL, t50, t51);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                    uint8_t t53 = (uint8_t)(t52 == ((uint8_t)7ULL));
                    if (!t53) {
                        goto gm_lb11;
                    }
                    gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l15) + 8));
                    gm_l8 = t54;
                    gm_t88rcucg1b7uk t55 = gm_l8;
                    void * t56 = gm_l0;
                    void * t57 = GM_LD(void *, ((uint8_t *)t56));
                    void * t58 = (void *)(((uint8_t *)t57) + 8);
                    gm_t88rcucg1b7uk t59 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t58);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t60 = (!gm_eq9m6mvtmgde(((uint8_t *)&t55), ((uint8_t *)&t59)));
                    if (t60) {
                        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                        goto gm_fdone;
                    }
                    void * t61 = gm_l0;
                    void * t62 = GM_LD(void *, ((uint8_t *)t61));
                    void * t63 = GM_LD(void *, ((uint8_t *)t62));
                    void * t64 = GM_LD(void *, (((uint8_t *)t63) + 240));
                    uint64_t t65 = GM_LD(uint64_t, ((((uint8_t *)t63) + 240) + 8));
                    uint64_t t66 = gm_l7;
                    if ((uintptr_t)t66 >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)t66, t65, gm_src6d57grm686e3, 271, 22);
                    void * t67 = (void *)(((uint8_t *)t64) + (uintptr_t)t66 * 216);
                    gm_l9 = t67;
                    {
                        {
                            {
                                void * t68 = gm_l9;
                                uint8_t t69 = GM_LD(uint8_t, (((((uint8_t *)t68) + 40) + 64) + 0));
                                uint8_t t70 = (uint8_t)(t69 == ((uint8_t)0ULL));
                                if (!t70) {
                                    goto gm_lb13;
                                }
                                memmove(((uint8_t *)&F.l10), (((((uint8_t *)t68) + 40) + 64) + 8), 64);
                            }
                            uint64_t t71 = GM_LD(uint64_t, ((((uint8_t *)&F.l10) + 8) + 8));
                            uint8_t t72 = (!(t71 == ((uint64_t)1ULL)));
                            if (t72) {
                                memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                                goto gm_fdone;
                            }
                            {
                                void * t73 = gm_l0;
                                void * t74 = GM_LD(void *, (((uint8_t *)&F.l10) + 40));
                                void * t75 = gm_l9;
                                uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)t75) + 32));
                                uint32_t t77 = gm_l3;
                                uint32_t r78; bool o79 = __builtin_add_overflow(t77, ((uint32_t)1ULL), &r78);
                                if (o79) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 274, 59);
                                uint32_t t80 = r78;
                                gm_f_const_template_5dvtt1m((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t73, t74, t76, t80);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_fdone;
                            }
                        }
gm_lb13:;
                        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                        goto gm_fdone;
                    }
gm_lb12:;
                }
gm_lb11:;
                memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                goto gm_fdone;
            }
gm_lb10:;
        }
gm_lb4:;
        {
            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
            goto gm_fdone;
        }
gm_lb14:;
        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 263, 5);
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
inline __attribute__((aligned(4))) void gm_f_fn_name_at_5o2j1h9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_fn_name_at_5o2j1h9 F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* text */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* c */
    uint8_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 232u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    void * t7 = gm_l0;
    gm_tl874t2atnlh3 t8 = gm_l1;
    gm_b16_8 t9 = gm_f_span_text_sv6qv18(t7, GM_CONV(gm_cseahvg8v7p0vs, t8));
    gm_l2 = t9;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
    if (t11) {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
    gm_b16_8 t12 = gm_l2;
    gm_l3 = t12;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t13 = gm_l4;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t18 = gm_l4;
                {
                    {
                        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t16) + (uintptr_t)t18 * 1));
                        gm_l5 = t19;
                        uint8_t t20 = gm_l5;
                        gm_l6 = t20;
                        uint8_t t21 = gm_l6;
                        uint8_t t22 = (((uint8_t)97ULL) <= t21);
                        if (t22) {
                            uint8_t t23 = gm_l6;
                            uint8_t t24 = (t23 <= ((uint8_t)122ULL));
                            gm_l12 = t24;
                        } else {
                            gm_l12 = ((uint8_t)0);
                        }
                        uint8_t t25 = gm_l12;
                        if (t25) {
                            gm_l11 = ((uint8_t)1);
                        } else {
                            uint8_t t26 = gm_l5;
                            gm_l7 = t26;
                            uint8_t t27 = gm_l7;
                            uint8_t t28 = (((uint8_t)65ULL) <= t27);
                            if (t28) {
                                uint8_t t29 = gm_l7;
                                uint8_t t30 = (t29 <= ((uint8_t)90ULL));
                                gm_l13 = t30;
                            } else {
                                gm_l13 = ((uint8_t)0);
                            }
                            uint8_t t31 = gm_l13;
                            gm_l11 = t31;
                        }
                        uint8_t t32 = gm_l11;
                        if (t32) {
                            gm_l10 = ((uint8_t)1);
                        } else {
                            uint8_t t33 = gm_l5;
                            gm_l8 = t33;
                            uint8_t t34 = gm_l8;
                            uint8_t t35 = (((uint8_t)48ULL) <= t34);
                            if (t35) {
                                uint8_t t36 = gm_l8;
                                uint8_t t37 = (t36 <= ((uint8_t)57ULL));
                                gm_l14 = t37;
                            } else {
                                gm_l14 = ((uint8_t)0);
                            }
                            uint8_t t38 = gm_l14;
                            gm_l10 = t38;
                        }
                        uint8_t t39 = gm_l10;
                        if (t39) {
                            gm_l9 = ((uint8_t)1);
                        } else {
                            uint8_t t40 = gm_l5;
                            gm_tqs5p4n2tq19v t41;
                            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)95ULL));
                            GM_ST(uint8_t, ((uint8_t *)&t41 + 1), ((uint8_t)46ULL));
                            uint8_t t42 = 0;
                            if (gm_eqjmccqdb90f(((uint8_t *)&t41) + 0, ((uint8_t *)&t40))) t42 = 1;
                            if (gm_eqjmccqdb90f(((uint8_t *)&t41) + 1, ((uint8_t *)&t40))) t42 = 1;
                            uint8_t t43 = (uint8_t)(t42);
                            gm_l9 = t43;
                        }
                        uint8_t t44 = gm_l9;
                        uint8_t t45 = (uint8_t)!t44;
                        if (t45) {
                            memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                            goto gm_fdone;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 499, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t46 = gm_l4;
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
            uint64_t t49 = r47;
            gm_l4 = t49;
        }
    }
gm_lb1:;
    gm_b16_8 t50 = gm_l2;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t50);
    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_written_qtvia9n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_written_qtvia9n F;
    void * gm_l0; /* cx */
    void * gm_l1; /* v */
    gm_b16_8 gm_l3; /* name */
    GM_STACK_CHECK(&F, 825u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l5;
    gm_tabb710d9mq2e gm_l6;
    gm_tabb710d9mq2e gm_l7;
    {
        {
            void * t4 = gm_l1;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)26ULL));
            if (!t6) {
                goto gm_lb2;
            }
            {
                void * t7 = gm_l0;
                void * t8 = gm_l1;
                gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t8) + 72));
                gm_f_fn_name_at_5o2j1h9((void *)&gm_l5, (void *)NULL, cx, NULL, t7, t9);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t10 = (uint8_t)((((uint8_t *)&gm_l5))[16] != 0);
            if (t10) {
                gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l5));
                gm_l3 = t11;
            } else {
                {
                    void * t12 = gm_l1;
                    gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t12) + 72));
                    gm_b16_8 t14;
                    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sfl0j5tlur0e6));
                    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(54));
                    gm_t4gut2c9fj1kt t15;
                    gm_f_error_s6hn871((void *)&t15, (void *)NULL, cx, NULL, t13, t14);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l2 = t15;
                }
                void * t16 = (void *)((uint8_t *)&F.l2);
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(gm_snf4l98ljss2u));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(88));
                gm_f_add_note_46v502s(t16, t17);
                {
                    void * t18 = gm_l0;
                    void * t19 = GM_LD(void *, ((uint8_t *)t18));
                    gm_b0_1 t20 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t19, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l2)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            gm_b16_8 t21 = gm_l3;
            gm_l3 = t21;
            {
                void * t22 = gm_l1;
                gm_tl874t2atnlh3 t23 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t22) + 72));
                gm_b16_8 t24 = gm_l3;
                gm_tnpifv20irnph t25;
                GM_ST(gm_b16_8, ((uint8_t *)&t25 + 0), t24);
                gm_f_format_cub3u7l((void *)&gm_l6, (void *)NULL, cx, NULL, t25);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
                if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 8, gm_src6d57grm686e3, 489, 29);
                gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
                gm_t4gut2c9fj1kt t29;
                gm_f_error_s6hn871((void *)&t29, (void *)NULL, cx, NULL, t23, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l4 = t29;
            }
            {
                void * t30 = (void *)((uint8_t *)&F.l4);
                gm_b16_8 t31 = gm_l3;
                gm_b16_8 t32 = gm_l3;
                gm_t6j6smj7qoctu t33;
                GM_ST(gm_b16_8, ((uint8_t *)&t33 + 0), t31);
                GM_ST(gm_b16_8, ((uint8_t *)&t33 + 16), t32);
                gm_f_format_j79pp86((void *)&gm_l7, (void *)NULL, cx, NULL, &t33);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                if (!t34) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_src6d57grm686e3, 490, 16);
                gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
                gm_f_add_note_46v502s(t30, t36);
            }
            {
                void * t37 = gm_l0;
                void * t38 = GM_LD(void *, ((uint8_t *)t37));
                gm_b0_1 t39 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t38, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb2:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_number_at_esf7qkk(void *gm_out, gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_number_at_esf7qkk F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* start */
    uint64_t gm_l2; /* i */
    uint64_t gm_l3; /* n */
    uint8_t gm_l4; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 266u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l5;
    uint64_t t5 = gm_l1;
    gm_l2 = t5;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t6 = gm_l2;
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint8_t t8 = (t6 < t7);
            if (!t8) {
                goto gm_lb1;
            }
            void * t9 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t11 = gm_l2;
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t9) + (uintptr_t)t11 * 1));
            gm_l4 = t12;
            uint8_t t13 = gm_l4;
            uint8_t t14 = (((uint8_t)48ULL) <= t13);
            if (t14) {
                uint8_t t15 = gm_l4;
                uint8_t t16 = (t15 <= ((uint8_t)57ULL));
                gm_l5 = t16;
            } else {
                gm_l5 = ((uint8_t)0);
            }
            uint8_t t17 = gm_l5;
            if (!t17) {
                goto gm_lb1;
            }
            {
                uint64_t t18 = gm_l3;
                uint64_t r19; bool o20 = __builtin_mul_overflow(t18, ((uint64_t)10ULL), &r19);
                if (o20) gm_trap("integer overflow in `*`", gm_src6d57grm686e3, 132, 13);
                uint64_t t21 = r19;
                void * t22 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t24 = gm_l2;
                uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)t22) + (uintptr_t)t24 * 1));
                uint8_t r26; bool o27 = __builtin_sub_overflow(t25, ((uint8_t)48ULL), &r26);
                if (o27) gm_trap("integer overflow in `-`", gm_src6d57grm686e3, 132, 26);
                uint8_t t28 = r26;
                uint64_t t29 = (uint64_t)t28;
                uint64_t r30; bool o31 = __builtin_add_overflow(t21, t29, &r30);
                if (o31) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 132, 13);
                uint64_t t32 = r30;
                gm_l3 = t32;
                uint64_t t33 = gm_l3;
                uint8_t t34 = (t33 > ((uint64_t)65536ULL));
                if (t34) {
                    gm_l3 = ((uint64_t)65537ULL);
                }
                uint64_t t35 = gm_l2;
                uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                if (o37) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 134, 9);
                uint64_t t38 = r36;
                gm_l2 = t38;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t39 = gm_l2;
    uint64_t t40 = gm_l1;
    uint8_t t41 = ((t39 == t40));
    if (t41) {
        gm_t9805qtonbsag t42;
        memset(&t42, 0, sizeof t42);
        uint64_t t43 = gm_l1;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 20, 0);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 0), t42);
        GM_ST(uint64_t, ((uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 8), t43);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 16), ((uint8_t)0));
        goto gm_fdone;
    }
    uint64_t t44 = gm_l3;
    uint8_t t45 = (t44 > ((uint64_t)65536ULL));
    if (t45) {
        gm_t9805qtonbsag t46;
        memset(&t46, 0, sizeof t46);
        uint64_t t47 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 20, 0);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 0), t46);
        GM_ST(uint64_t, ((uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 8), t47);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 16), ((uint8_t)1));
        goto gm_fdone;
    }
    uint64_t t48 = gm_l3;
    uint32_t t49 = (uint32_t)t48;
    gm_t9805qtonbsag t50;
    GM_ST(uint8_t, (uint8_t *)&t50 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t50 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t50), t49);
    ((uint8_t *)&t50)[4] = 1;
    uint64_t t51 = gm_l2;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 20, 0);
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 0), t50);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 8), t51);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_tmnrj1ud20adj *)gm_out) + 16), ((uint8_t)0));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tmnrj1ud20adj gm_f_number_at_esf7qkk_c(gm_b16_8 a0, uint64_t a1) { gm_tmnrj1ud20adj gm_r; gm_f_number_at_esf7qkk(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_numbered_error_07hll20(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_numbered_error_07hll20 F;
    uint64_t gm_l1; /* m */
    gm_b16_8 gm_l2; /* given */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* ix */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* k */
    uint8_t gm_l9; /* used */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* ix */
    GM_STACK_CHECK(&F, 693u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_b16_8 gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tabb710d9mq2e gm_l16;
    uint64_t t17 = gm_l1;
    uint8_t t18 = ((t17 == ((uint64_t)0ULL)));
    if (t18) {
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sp920qd66qq8t));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(19));
        gm_l2 = t19;
    } else {
        uint64_t t20 = gm_l1;
        uint8_t t21 = ((t20 == ((uint64_t)1ULL)));
        if (t21) {
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s378kfmdl5enf));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(16));
            gm_l13 = t22;
        } else {
            uint64_t t23 = gm_l1;
            gm_t4ce0jd3tdro4 t24;
            GM_ST(uint64_t, ((uint8_t *)&t24 + 0), t23);
            gm_f_format_h02tha7((void *)&gm_l14, (void *)NULL, cx, NULL, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src6d57grm686e3, 334, 9);
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
            gm_l13 = t27;
        }
        gm_b16_8 t28 = gm_l13;
        gm_l2 = t28;
    }
    gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 32));
    gm_l3 = t29;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t30 = gm_l4;
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t32 = (t30 < t31);
            if (!t32) {
                goto gm_lb1;
            }
            {
                void * t33 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t35 = gm_l4;
                {
                    {
                        uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t33) + (uintptr_t)t35 * 8));
                        gm_l5 = t36;
                        uint64_t t37 = gm_l5;
                        uint64_t t38 = gm_l1;
                        uint8_t t39 = (t37 >= t38);
                        if (t39) {
                            {
                                uint64_t t40 = gm_l5;
                                gm_b16_8 t41 = gm_l2;
                                gm_tf1bjg81u0glv t42;
                                GM_ST(uint64_t, ((uint8_t *)&t42 + 0), t40);
                                GM_ST(gm_b16_8, ((uint8_t *)&t42 + 8), t41);
                                gm_f_format_ksi8bsm((void *)&gm_l15, (void *)NULL, cx, NULL, &t42);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                            if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src6d57grm686e3, 337, 29);
                            {
                                gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                                gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t45);
                                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                                goto gm_fdone;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 336, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t46 = gm_l4;
            uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
            uint64_t t49 = r47;
            gm_l4 = t49;
        }
    }
gm_lb1:;
    gm_l7 = ((uint64_t)0ULL);
    uint64_t t50 = gm_l1;
    gm_l6 = t50;
    {
        for (;;) {
            uint64_t t51 = gm_l7;
            uint64_t t52 = gm_l6;
            uint8_t t53 = (t51 < t52);
            if (!t53) {
                goto gm_lb6;
            }
            {
                {
                    {
                        uint64_t t54 = gm_l7;
                        gm_l8 = t54;
                        gm_l9 = ((uint8_t)0);
                        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 32));
                        gm_l10 = t55;
                        gm_l11 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t56 = gm_l11;
                                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                uint8_t t58 = (t56 < t57);
                                if (!t58) {
                                    goto gm_lb11;
                                }
                                {
                                    void * t59 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                    uint64_t t61 = gm_l11;
                                    {
                                        {
                                            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)t59) + (uintptr_t)t61 * 8));
                                            gm_l12 = t62;
                                            uint64_t t63 = gm_l12;
                                            uint64_t t64 = gm_l8;
                                            uint8_t t65 = ((t63 == t64));
                                            if (t65) {
                                                gm_l9 = ((uint8_t)1);
                                            }
                                            goto gm_lb14;
                                        }
gm_lb15:;
                                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 341, 9);
                                    }
gm_lb14:;
                                }
gm_lb13:;
                                uint64_t t66 = gm_l11;
                                uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                                uint64_t t69 = r67;
                                gm_l11 = t69;
                            }
                        }
gm_lb11:;
                        uint8_t t70 = gm_l9;
                        uint8_t t71 = (uint8_t)!t70;
                        if (t71) {
                            {
                                uint64_t t72 = gm_l8;
                                gm_t4ce0jd3tdro4 t73;
                                GM_ST(uint64_t, ((uint8_t *)&t73 + 0), t72);
                                gm_f_format_4duro1p((void *)&gm_l16, (void *)NULL, cx, NULL, t73);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t74 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                            if (!t74) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_src6d57grm686e3, 344, 30);
                            {
                                gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                                gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t76);
                                ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                                goto gm_fdone;
                            }
                        }
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 339, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t77 = gm_l7;
            uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
            uint64_t t80 = r78;
            gm_l7 = t80;
        }
    }
gm_lb6:;
    memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
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
__attribute__((aligned(4))) void gm_f_parse_spec_iod83cp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_spec_iod83cp F;
    gm_b16_8 gm_l0; /* s */
    gm_t7g88i52rrtm5 gm_l1; /* spec */
    uint64_t gm_l2; /* i */
    gm_taari1s0m51a7 gm_l3; /* second */
    gm_taari1s0m51a7 gm_l4; /* first */
    gm_t9qkh0rsooc8d gm_l5; /* a */
    gm_t9qkh0rsooc8d gm_l6; /* a */
    gm_tmnrj1ud20adj gm_l7; /* w */
    uint32_t gm_l8; /* v */
    gm_tmnrj1ud20adj gm_l9; /* p */
    uint32_t gm_l10; /* pv */
    uint8_t gm_l11; /* c */
    uint8_t gm_l12; /* based */
    GM_STACK_CHECK(&F, 1023u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    gm_tqs5p4n2tq19v gm_l16;
    gm_tqs5p4n2tq19v gm_l17;
    gm_tqs5p4n2tq19v gm_l18;
    gm_tqs5p4n2tq19v gm_l19;
    gm_tqs5p4n2tq19v gm_l20;
    gm_tqs5p4n2tq19v gm_l21;
    gm_t9qkh0rsooc8d gm_l22;
    gm_tabb710d9mq2e gm_l23;
    gm_tabb710d9mq2e gm_l24;
    uint8_t gm_l25;
    uint8_t gm_l26;
    uint8_t gm_l27;
    uint8_t gm_l28;
    {
        gm_f_default_spec_9ntjq2s((void *)&gm_l1);
        gm_l2 = ((uint64_t)0ULL);
        gm_taari1s0m51a7 t27;
        memset(&t27, 0, sizeof t27);
        gm_l3 = t27;
        uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint8_t t29 = (t28 >= ((uint64_t)2ULL));
        if (t29) {
            void * t30 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t31, gm_src6d57grm686e3, 147, 39);
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)t30) + (uintptr_t)((uint64_t)1ULL) * 1));
            gm_taari1s0m51a7 t33 = gm_f_align_of_mks0ipd(t32);
            gm_l3 = t33;
        }
        gm_taari1s0m51a7 t34;
        memset(&t34, 0, sizeof t34);
        gm_l4 = t34;
        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint8_t t36 = (t35 >= ((uint64_t)1ULL));
        if (t36) {
            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t38, gm_src6d57grm686e3, 149, 38);
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)((uint64_t)0ULL) * 1));
            gm_taari1s0m51a7 t40 = gm_f_align_of_mks0ipd(t39);
            gm_l4 = t40;
        }
        {
            {
                uint8_t t41 = (uint8_t)((((uint8_t *)&gm_l3))[1] != 0);
                if (!t41) {
                    goto gm_lb2;
                }
                gm_t9qkh0rsooc8d t42 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l3));
                gm_l5 = t42;
                void * t43 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t44)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t44, gm_src6d57grm686e3, 151, 12);
                uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)t43) + (uintptr_t)((uint64_t)0ULL) * 1));
                uint8_t t46 = ((t45 == ((uint8_t)123ULL)));
                if (t46) {
                    gm_l15 = ((uint8_t)1);
                } else {
                    void * t47 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t48)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t48, gm_src6d57grm686e3, 151, 27);
                    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)t47) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t50 = (t49 >= ((uint8_t)128ULL));
                    gm_l15 = t50;
                }
                uint8_t t51 = gm_l15;
                if (t51) {
                    gm_l14 = ((uint8_t)1);
                } else {
                    void * t52 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t53)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t53, gm_src6d57grm686e3, 151, 42);
                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)t52) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t55 = (t54 < ((uint8_t)32ULL));
                    gm_l14 = t55;
                }
                uint8_t t56 = gm_l14;
                if (t56) {
                    gm_l13 = ((uint8_t)1);
                } else {
                    void * t57 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t58)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t58, gm_src6d57grm686e3, 151, 55);
                    uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)t57) + (uintptr_t)((uint64_t)0ULL) * 1));
                    uint8_t t60 = ((t59 == ((uint8_t)127ULL)));
                    gm_l13 = t60;
                }
                uint8_t t61 = gm_l13;
                if (t61) {
                    gm_b16_8 t62;
                    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sc4vvo9i5cgel));
                    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(85));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0), ((uint8_t)1ULL));
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8), t62);
                    goto gm_fdone;
                }
                void * t63 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t64)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t64, gm_src6d57grm686e3, 154, 21);
                uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)t63) + (uintptr_t)((uint64_t)0ULL) * 1));
                GM_ST(uint8_t, ((uint8_t *)&gm_l1), t65);
                gm_t9qkh0rsooc8d t66 = gm_l5;
                GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1), t66);
                gm_l2 = ((uint64_t)2ULL);
                goto gm_lb1;
            }
gm_lb2:;
            {
                {
                    uint8_t t67 = (uint8_t)((((uint8_t *)&gm_l4))[1] != 0);
                    if (!t67) {
                        goto gm_lb4;
                    }
                    gm_t9qkh0rsooc8d t68 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l4));
                    gm_l6 = t68;
                    gm_t9qkh0rsooc8d t69 = gm_l6;
                    GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1), t69);
                    gm_l2 = ((uint64_t)1ULL);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
        }
gm_lb1:;
    }
    {
        void * t70 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t72 = gm_l2;
        uint8_t t73 = (t72 < t71);
        if (t73) {
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)t70) + (uintptr_t)t72 * 1));
            gm_tqs5p4n2tq19v t75;
            GM_ST(uint8_t, ((uint8_t *)&t75), t74);
            ((uint8_t *)&t75)[1] = 1;
            gm_l16 = t75;
        } else {
            gm_tqs5p4n2tq19v t76;
            memset(&t76, 0, sizeof t76);
            gm_l16 = t76;
        }
        gm_tqs5p4n2tq19v t77 = gm_l16;
        gm_tqs5p4n2tq19v t78;
        GM_ST(uint8_t, ((uint8_t *)&t78), ((uint8_t)43ULL));
        ((uint8_t *)&t78)[1] = 1;
        uint8_t t79 = (gm_eqb44cb3cf1n(((uint8_t *)&t77), ((uint8_t *)&t78)));
        if (t79) {
            GM_ST(uint8_t, (((uint8_t *)&gm_l1) + 2), ((uint8_t)1));
            uint64_t t80 = gm_l2;
            uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
            if (o82) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 163, 9);
            uint64_t t83 = r81;
            gm_l2 = t83;
        }
        void * t84 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t86 = gm_l2;
        uint8_t t87 = (t86 < t85);
        if (t87) {
            uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)t84) + (uintptr_t)t86 * 1));
            gm_tqs5p4n2tq19v t89;
            GM_ST(uint8_t, ((uint8_t *)&t89), t88);
            ((uint8_t *)&t89)[1] = 1;
            gm_l17 = t89;
        } else {
            gm_tqs5p4n2tq19v t90;
            memset(&t90, 0, sizeof t90);
            gm_l17 = t90;
        }
        gm_tqs5p4n2tq19v t91 = gm_l17;
        gm_tqs5p4n2tq19v t92;
        GM_ST(uint8_t, ((uint8_t *)&t92), ((uint8_t)35ULL));
        ((uint8_t *)&t92)[1] = 1;
        uint8_t t93 = (gm_eqb44cb3cf1n(((uint8_t *)&t91), ((uint8_t *)&t92)));
        if (t93) {
            GM_ST(uint8_t, (((uint8_t *)&gm_l1) + 3), ((uint8_t)1));
            uint64_t t94 = gm_l2;
            uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
            if (o96) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 167, 9);
            uint64_t t97 = r95;
            gm_l2 = t97;
        }
        void * t98 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t100 = gm_l2;
        uint8_t t101 = (t100 < t99);
        if (t101) {
            uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)t98) + (uintptr_t)t100 * 1));
            gm_tqs5p4n2tq19v t103;
            GM_ST(uint8_t, ((uint8_t *)&t103), t102);
            ((uint8_t *)&t103)[1] = 1;
            gm_l18 = t103;
        } else {
            gm_tqs5p4n2tq19v t104;
            memset(&t104, 0, sizeof t104);
            gm_l18 = t104;
        }
        gm_tqs5p4n2tq19v t105 = gm_l18;
        gm_tqs5p4n2tq19v t106;
        GM_ST(uint8_t, ((uint8_t *)&t106), ((uint8_t)48ULL));
        ((uint8_t *)&t106)[1] = 1;
        uint8_t t107 = (gm_eqb44cb3cf1n(((uint8_t *)&t105), ((uint8_t *)&t106)));
        if (t107) {
            GM_ST(uint8_t, (((uint8_t *)&gm_l1) + 4), ((uint8_t)1));
            uint64_t t108 = gm_l2;
            uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
            if (o110) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 171, 9);
            uint64_t t111 = r109;
            gm_l2 = t111;
        }
        gm_b16_8 t112 = gm_l0;
        uint64_t t113 = gm_l2;
        gm_f_number_at_esf7qkk((void *)&gm_l7, t112, t113);
        uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 16));
        if (t114) {
            gm_b16_8 t115;
            GM_ST(void *, (uint8_t *)&t115, (void *)(gm_sutmkmalrac6l));
            GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(44));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0), ((uint8_t)1ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8), t115);
            goto gm_fdone;
        }
        {
            {
                uint8_t t116 = (uint8_t)((((uint8_t *)&gm_l7))[4] != 0);
                if (!t116) {
                    goto gm_lb6;
                }
                uint32_t t117 = GM_LD(uint32_t, ((uint8_t *)&gm_l7));
                gm_l8 = t117;
                uint32_t t118 = gm_l8;
                GM_ST(uint32_t, (((uint8_t *)&gm_l1) + 8), t118);
                goto gm_lb5;
            }
gm_lb6:;
        }
gm_lb5:;
    }
    {
        uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
        gm_l2 = t119;
        void * t120 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t122 = gm_l2;
        uint8_t t123 = (t122 < t121);
        if (t123) {
            uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)t120) + (uintptr_t)t122 * 1));
            gm_tqs5p4n2tq19v t125;
            GM_ST(uint8_t, ((uint8_t *)&t125), t124);
            ((uint8_t *)&t125)[1] = 1;
            gm_l19 = t125;
        } else {
            gm_tqs5p4n2tq19v t126;
            memset(&t126, 0, sizeof t126);
            gm_l19 = t126;
        }
        gm_tqs5p4n2tq19v t127 = gm_l19;
        gm_tqs5p4n2tq19v t128;
        GM_ST(uint8_t, ((uint8_t *)&t128), ((uint8_t)46ULL));
        ((uint8_t *)&t128)[1] = 1;
        uint8_t t129 = (gm_eqb44cb3cf1n(((uint8_t *)&t127), ((uint8_t *)&t128)));
        if (t129) {
            uint64_t t130 = gm_l2;
            uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
            if (o132) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 178, 9);
            uint64_t t133 = r131;
            gm_l2 = t133;
            gm_b16_8 t134 = gm_l0;
            uint64_t t135 = gm_l2;
            gm_f_number_at_esf7qkk((void *)&gm_l9, t134, t135);
            uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 16));
            if (t136) {
                gm_b16_8 t137;
                GM_ST(void *, (uint8_t *)&t137, (void *)(gm_sutmkmalrac6l));
                GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(44));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8), t137);
                goto gm_fdone;
            }
            uint8_t t138 = (uint8_t)((((uint8_t *)&gm_l9))[4] != 0);
            if (t138) {
                uint32_t t139 = GM_LD(uint32_t, ((uint8_t *)&gm_l9));
                gm_l10 = t139;
            } else {
                gm_b16_8 t140;
                GM_ST(void *, (uint8_t *)&t140, (void *)(gm_sm35km518bh68));
                GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(51));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8), t140);
                goto gm_fdone;
            }
            uint32_t t141 = gm_l10;
            gm_t9805qtonbsag t142;
            GM_ST(uint8_t, (uint8_t *)&t142 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t142 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t142), t141);
            ((uint8_t *)&t142)[4] = 1;
            GM_ST(gm_t9805qtonbsag, (((uint8_t *)&gm_l1) + 12), t142);
            uint64_t t143 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
            gm_l2 = t143;
        }
        {
            {
                void * t144 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t145 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t146 = gm_l2;
                uint8_t t147 = (t146 < t145);
                if (t147) {
                    uint8_t t148 = GM_LD(uint8_t, (((uint8_t *)t144) + (uintptr_t)t146 * 1));
                    gm_tqs5p4n2tq19v t149;
                    GM_ST(uint8_t, ((uint8_t *)&t149), t148);
                    ((uint8_t *)&t149)[1] = 1;
                    gm_l20 = t149;
                } else {
                    gm_tqs5p4n2tq19v t150;
                    memset(&t150, 0, sizeof t150);
                    gm_l20 = t150;
                }
                gm_tqs5p4n2tq19v t151 = gm_l20;
                gm_l21 = t151;
                uint8_t t152 = (uint8_t)((((uint8_t *)&gm_l21))[1] != 0);
                if (!t152) {
                    goto gm_lb8;
                }
                uint8_t t153 = GM_LD(uint8_t, ((uint8_t *)&gm_l21));
                gm_l11 = t153;
                {
                    {
                        uint8_t t154 = gm_l11;
                        uint8_t t155 = ((t154 == ((uint8_t)120ULL)));
                        if (!t155) {
                            goto gm_lb10;
                        }
                        gm_t9qkh0rsooc8d t156;
                        GM_ST(uint8_t, ((uint8_t *)&t156 + 0), ((uint8_t)1ULL));
                        gm_l22 = t156;
                        goto gm_lb9;
                    }
gm_lb10:;
                    {
                        uint8_t t157 = gm_l11;
                        uint8_t t158 = ((t157 == ((uint8_t)88ULL)));
                        if (!t158) {
                            goto gm_lb11;
                        }
                        gm_t9qkh0rsooc8d t159;
                        GM_ST(uint8_t, ((uint8_t *)&t159 + 0), ((uint8_t)2ULL));
                        gm_l22 = t159;
                        goto gm_lb9;
                    }
gm_lb11:;
                    {
                        uint8_t t160 = gm_l11;
                        uint8_t t161 = ((t160 == ((uint8_t)111ULL)));
                        if (!t161) {
                            goto gm_lb12;
                        }
                        gm_t9qkh0rsooc8d t162;
                        GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)3ULL));
                        gm_l22 = t162;
                        goto gm_lb9;
                    }
gm_lb12:;
                    {
                        uint8_t t163 = gm_l11;
                        uint8_t t164 = ((t163 == ((uint8_t)98ULL)));
                        if (!t164) {
                            goto gm_lb13;
                        }
                        gm_t9qkh0rsooc8d t165;
                        GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)4ULL));
                        gm_l22 = t165;
                        goto gm_lb9;
                    }
gm_lb13:;
                    {
                        uint8_t t166 = gm_l11;
                        uint8_t t167 = ((t166 == ((uint8_t)101ULL)));
                        if (!t167) {
                            goto gm_lb14;
                        }
                        gm_t9qkh0rsooc8d t168;
                        GM_ST(uint8_t, ((uint8_t *)&t168 + 0), ((uint8_t)5ULL));
                        gm_l22 = t168;
                        goto gm_lb9;
                    }
gm_lb14:;
                    {
                        uint8_t t169 = gm_l11;
                        uint8_t t170 = ((t169 == ((uint8_t)113ULL)));
                        if (!t170) {
                            goto gm_lb15;
                        }
                        gm_t9qkh0rsooc8d t171;
                        GM_ST(uint8_t, ((uint8_t *)&t171 + 0), ((uint8_t)6ULL));
                        gm_l22 = t171;
                        goto gm_lb9;
                    }
gm_lb15:;
                    {
                        {
                            gm_b16_8 t172 = gm_l0;
                            gm_tnpifv20irnph t173;
                            GM_ST(gm_b16_8, ((uint8_t *)&t173 + 0), t172);
                            gm_f_format_oq31u8d((void *)&gm_l23, (void *)NULL, cx, NULL, t173);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t174 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                        if (!t174) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 8, gm_src6d57grm686e3, 193, 30);
                        {
                            gm_b16_8 t175 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l23) + 8));
                            gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)&t175));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0), ((uint8_t)1ULL));
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8), t176);
                            goto gm_fdone;
                        }
                    }
gm_lb16:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 186, 21);
                }
gm_lb9:;
                gm_t9qkh0rsooc8d t177 = gm_l22;
                GM_ST(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 20), t177);
                uint64_t t178 = gm_l2;
                uint64_t r179; bool o180 = __builtin_add_overflow(t178, ((uint64_t)1ULL), &r179);
                if (o180) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 195, 9);
                uint64_t t181 = r179;
                gm_l2 = t181;
                goto gm_lb7;
            }
gm_lb8:;
        }
gm_lb7:;
    }
    {
        uint64_t t182 = gm_l2;
        uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint8_t t184 = (!(t182 == t183));
        if (t184) {
            {
                gm_b16_8 t185 = gm_l0;
                gm_tnpifv20irnph t186;
                GM_ST(gm_b16_8, ((uint8_t *)&t186 + 0), t185);
                gm_f_format_f2fgi8v((void *)&gm_l24, (void *)NULL, cx, NULL, t186);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t187 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
            if (!t187) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_src6d57grm686e3, 198, 21);
            {
                gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                gm_b16_8 t189 = GM_LD(gm_b16_8, ((uint8_t *)&t188));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8), t189);
                goto gm_fdone;
            }
        }
        {
            {
                uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 4));
                if (!t190) {
                    goto gm_lb18;
                }
                gm_t9qkh0rsooc8d t191 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
                uint8_t t192 = GM_LD(uint8_t, (((uint8_t *)&t191) + 0));
                uint8_t t193 = (uint8_t)(!(t192 == ((uint8_t)0ULL)));
                if (!t193) {
                    goto gm_lb18;
                }
                gm_b16_8 t194;
                GM_ST(void *, (uint8_t *)&t194, (void *)(gm_sjil3d717dq6h));
                GM_ST(uintptr_t, (uint8_t *)&t194 + 8, (uintptr_t)(62));
                gm_t9qkh0rsooc8d t195;
                GM_ST(uint8_t, ((uint8_t *)&t195 + 0), ((uint8_t)0ULL));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8), t194);
                goto gm_fdone;
            }
gm_lb18:;
        }
gm_lb17:;
        gm_t9qkh0rsooc8d t196 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 20));
        uint8_t t197 = GM_LD(uint8_t, (((uint8_t *)&t196) + 0));
        uint8_t t198 = (uint8_t)((t197 == ((uint8_t)1ULL)));
        gm_t9qkh0rsooc8d t199;
        GM_ST(uint8_t, ((uint8_t *)&t199 + 0), ((uint8_t)1ULL));
        if (t198) {
            gm_l27 = ((uint8_t)1);
        } else {
            gm_t9qkh0rsooc8d t200 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 20));
            uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&t200) + 0));
            uint8_t t202 = (uint8_t)((t201 == ((uint8_t)2ULL)));
            gm_t9qkh0rsooc8d t203;
            GM_ST(uint8_t, ((uint8_t *)&t203 + 0), ((uint8_t)2ULL));
            gm_l27 = t202;
        }
        uint8_t t204 = gm_l27;
        if (t204) {
            gm_l26 = ((uint8_t)1);
        } else {
            gm_t9qkh0rsooc8d t205 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 20));
            uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&t205) + 0));
            uint8_t t207 = (uint8_t)((t206 == ((uint8_t)3ULL)));
            gm_t9qkh0rsooc8d t208;
            GM_ST(uint8_t, ((uint8_t *)&t208 + 0), ((uint8_t)3ULL));
            gm_l26 = t207;
        }
        uint8_t t209 = gm_l26;
        if (t209) {
            gm_l25 = ((uint8_t)1);
        } else {
            gm_t9qkh0rsooc8d t210 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 20));
            uint8_t t211 = GM_LD(uint8_t, (((uint8_t *)&t210) + 0));
            uint8_t t212 = (uint8_t)((t211 == ((uint8_t)4ULL)));
            gm_t9qkh0rsooc8d t213;
            GM_ST(uint8_t, ((uint8_t *)&t213 + 0), ((uint8_t)4ULL));
            gm_l25 = t212;
        }
        uint8_t t214 = gm_l25;
        gm_l12 = t214;
        {
            {
                uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 3));
                if (!t215) {
                    goto gm_lb20;
                }
                uint8_t t216 = gm_l12;
                uint8_t t217 = (uint8_t)!t216;
                if (!t217) {
                    goto gm_lb20;
                }
                gm_b16_8 t218;
                GM_ST(void *, (uint8_t *)&t218, (void *)(gm_s8f0u3tjempi4));
                GM_ST(uintptr_t, (uint8_t *)&t218 + 8, (uintptr_t)(52));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8), t218);
                goto gm_fdone;
            }
gm_lb20:;
        }
gm_lb19:;
        {
            {
                {
                    {
                        uint8_t t219 = (uint8_t)(((((uint8_t *)&gm_l1) + 12))[4] != 0);
                        if (t219) {
                            goto gm_lb24;
                        }
                        gm_l28 = ((uint8_t)0);
                        goto gm_lb23;
                    }
gm_lb24:;
                    {
                        gm_l28 = ((uint8_t)1);
                        goto gm_lb23;
                    }
gm_lb25:;
                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 205, 8);
                }
gm_lb23:;
                uint8_t t220 = gm_l28;
                if (!t220) {
                    goto gm_lb22;
                }
                gm_t9qkh0rsooc8d t221 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 20));
                uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&t221) + 0));
                uint8_t t223 = (uint8_t)(!(t222 == ((uint8_t)0ULL)));
                if (!t223) {
                    goto gm_lb22;
                }
                gm_t9qkh0rsooc8d t224 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 20));
                uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&t224) + 0));
                uint8_t t226 = (uint8_t)(!(t225 == ((uint8_t)5ULL)));
                if (!t226) {
                    goto gm_lb22;
                }
                gm_b16_8 t227;
                GM_ST(void *, (uint8_t *)&t227, (void *)(gm_sfu0in59pituf));
                GM_ST(uintptr_t, (uint8_t *)&t227 + 8, (uintptr_t)(61));
                gm_t9qkh0rsooc8d t228;
                GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)0ULL));
                gm_t9qkh0rsooc8d t229;
                GM_ST(uint8_t, ((uint8_t *)&t229 + 0), ((uint8_t)5ULL));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0), ((uint8_t)1ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8), t227);
                goto gm_fdone;
            }
gm_lb22:;
        }
gm_lb21:;
    }
    {
        gm_t7g88i52rrtm5 t230 = GM_LD(gm_t7g88i52rrtm5, ((uint8_t *)&gm_l1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 0), ((uint8_t)0ULL));
        GM_ST(gm_t7g88i52rrtm5, ((uint8_t *)&(*(gm_tpu7cfmd3m0cd *)gm_out) + 8), t230);
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
__attribute__((aligned(4))) void gm_f_parse_template_rlpl2k8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_template_rlpl2k8 F;
    gm_b16_8 gm_l0; /* t */
    uint64_t gm_l5; /* plain */
    uint64_t gm_l6; /* i */
    uint8_t gm_l7; /* c */
    gm_tqs5p4n2tq19v gm_l8; /* next */
    uint64_t gm_l9; /* j */
    gm_tf5srh4mp4b3i gm_l10; /* idx */
    uint8_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* start */
    uint64_t gm_l13; /* n */
    uint8_t gm_l14; /* <tmp> */
    gm_tf5srh4mp4b3i gm_l15; /* close */
    uint64_t gm_l16; /* k */
    uint64_t gm_l17; /* end */
    gm_tpu7cfmd3m0cd gm_l18; /* r */
    gm_t7g88i52rrtm5 gm_l19; /* sp */
    gm_b16_8 gm_l20; /* m */
    gm_t7g88i52rrtm5 gm_l21; /* spec */
    uint64_t gm_l22; /* v */
    GM_STACK_CHECK(&F, 2135u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l23;
    uint8_t gm_l24;
    uint8_t gm_l25;
    gm_tftb1odc739t5 gm_l26;
    uint8_t gm_l27;
    uint8_t gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_tftb1odc739t5 gm_l35;
    {
        gm_trqfvmjtt4sij t26;
        gm_f_list_epvducg((void *)&t26, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t26;
        gm_trqfvmjtt4sij t27;
        gm_f_list_qtk22f3((void *)&t27, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t27;
        gm_trqfvmjtt4sij t28;
        gm_f_list_uvfu1k1((void *)&t28, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t28;
        gm_trqfvmjtt4sij t29;
        gm_f_list_5dhgg4i((void *)&t29, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t29;
        gm_l5 = ((uint64_t)0ULL);
        gm_l6 = ((uint64_t)0ULL);
        {
            for (;;) {
                {
                    uint64_t t30 = gm_l6;
                    uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint8_t t32 = (t30 < t31);
                    if (!t32) {
                        goto gm_lb1;
                    }
                    {
                        {
                            void * t33 = GM_LD(void *, ((uint8_t *)&gm_l0));
                            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint64_t t35 = gm_l6;
                            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t33) + (uintptr_t)t35 * 1));
                            gm_l7 = t36;
                            uint64_t t37 = gm_l6;
                            uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                            if (o39) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 42, 24);
                            uint64_t t40 = r38;
                            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint8_t t42 = (t40 < t41);
                            if (t42) {
                                void * t43 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t45 = gm_l6;
                                uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                                if (o47) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 42, 42);
                                uint64_t t48 = r46;
                                if ((uintptr_t)t48 >= (uintptr_t)(t44)) gm_trap_index((uintptr_t)t48, t44, gm_src6d57grm686e3, 42, 40);
                                uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)t43) + (uintptr_t)t48 * 1));
                                gm_tqs5p4n2tq19v t50;
                                GM_ST(uint8_t, ((uint8_t *)&t50), t49);
                                ((uint8_t *)&t50)[1] = 1;
                                gm_l8 = t50;
                            } else {
                                gm_tqs5p4n2tq19v t51;
                                memset(&t51, 0, sizeof t51);
                                gm_l8 = t51;
                            }
                            uint8_t t52 = gm_l7;
                            uint8_t t53 = ((t52 == ((uint8_t)123ULL)));
                            if (t53) {
                                gm_tqs5p4n2tq19v t54 = gm_l8;
                                gm_tqs5p4n2tq19v t55;
                                GM_ST(uint8_t, ((uint8_t *)&t55), ((uint8_t)123ULL));
                                ((uint8_t *)&t55)[1] = 1;
                                uint8_t t56 = (gm_eqb44cb3cf1n(((uint8_t *)&t54), ((uint8_t *)&t55)));
                                gm_l24 = t56;
                            } else {
                                gm_l24 = ((uint8_t)0);
                            }
                            uint8_t t57 = gm_l24;
                            if (t57) {
                                gm_l23 = ((uint8_t)1);
                            } else {
                                uint8_t t58 = gm_l7;
                                uint8_t t59 = ((t58 == ((uint8_t)125ULL)));
                                if (t59) {
                                    gm_tqs5p4n2tq19v t60 = gm_l8;
                                    gm_tqs5p4n2tq19v t61;
                                    GM_ST(uint8_t, ((uint8_t *)&t61), ((uint8_t)125ULL));
                                    ((uint8_t *)&t61)[1] = 1;
                                    uint8_t t62 = (gm_eqb44cb3cf1n(((uint8_t *)&t60), ((uint8_t *)&t61)));
                                    gm_l25 = t62;
                                } else {
                                    gm_l25 = ((uint8_t)0);
                                }
                                uint8_t t63 = gm_l25;
                                gm_l23 = t63;
                            }
                            uint8_t t64 = gm_l23;
                            if (t64) {
                                void * t65 = (void *)((uint8_t *)&F.l3);
                                uint8_t t66 = gm_l7;
                                gm_tftb1odc739t5 t67 = gm_f_push_t83rg2m(t65, t66);
                                gm_l26 = t67;
                                uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                                if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_src6d57grm686e3, 44, 13);
                                uint64_t t69 = gm_l6;
                                uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)2ULL), &r70);
                                if (o71) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 45, 13);
                                uint64_t t72 = r70;
                                gm_l6 = t72;
                            } else {
                                {
                                    uint8_t t73 = gm_l7;
                                    uint8_t t74 = ((t73 == ((uint8_t)123ULL)));
                                    if (t74) {
                                        uint64_t t75 = gm_l6;
                                        uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                                        if (o77) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 48, 18);
                                        uint64_t t78 = r76;
                                        gm_l9 = t78;
                                        gm_tf5srh4mp4b3i t79;
                                        memset(&t79, 0, sizeof t79);
                                        gm_l10 = t79;
                                        {
                                            {
                                                uint64_t t80 = gm_l9;
                                                uint64_t t81 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                uint8_t t82 = (t80 < t81);
                                                if (!t82) {
                                                    goto gm_lb5;
                                                }
                                                void * t83 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                uint64_t t85 = gm_l9;
                                                uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)t83) + (uintptr_t)t85 * 1));
                                                gm_l11 = t86;
                                                uint8_t t87 = gm_l11;
                                                uint8_t t88 = (((uint8_t)48ULL) <= t87);
                                                if (t88) {
                                                    uint8_t t89 = gm_l11;
                                                    uint8_t t90 = (t89 <= ((uint8_t)57ULL));
                                                    gm_l27 = t90;
                                                } else {
                                                    gm_l27 = ((uint8_t)0);
                                                }
                                                uint8_t t91 = gm_l27;
                                                if (!t91) {
                                                    goto gm_lb5;
                                                }
                                                uint64_t t92 = gm_l9;
                                                gm_l12 = t92;
                                                gm_l13 = ((uint64_t)0ULL);
                                                {
                                                    for (;;) {
                                                        uint64_t t93 = gm_l9;
                                                        uint64_t t94 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                        uint8_t t95 = (t93 < t94);
                                                        if (!t95) {
                                                            goto gm_lb6;
                                                        }
                                                        void * t96 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                        uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                        uint64_t t98 = gm_l9;
                                                        uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)t96) + (uintptr_t)t98 * 1));
                                                        gm_l14 = t99;
                                                        uint8_t t100 = gm_l14;
                                                        uint8_t t101 = (((uint8_t)48ULL) <= t100);
                                                        if (t101) {
                                                            uint8_t t102 = gm_l14;
                                                            uint8_t t103 = (t102 <= ((uint8_t)57ULL));
                                                            gm_l28 = t103;
                                                        } else {
                                                            gm_l28 = ((uint8_t)0);
                                                        }
                                                        uint8_t t104 = gm_l28;
                                                        if (!t104) {
                                                            goto gm_lb6;
                                                        }
                                                        {
                                                            uint64_t t105 = gm_l13;
                                                            uint64_t r106; bool o107 = __builtin_mul_overflow(t105, ((uint64_t)10ULL), &r106);
                                                            if (o107) gm_trap("integer overflow in `*`", gm_src6d57grm686e3, 54, 25);
                                                            uint64_t t108 = r106;
                                                            void * t109 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                            uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                            uint64_t t111 = gm_l9;
                                                            uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)t109) + (uintptr_t)t111 * 1));
                                                            uint8_t r113; bool o114 = __builtin_sub_overflow(t112, ((uint8_t)48ULL), &r113);
                                                            if (o114) gm_trap("integer overflow in `-`", gm_src6d57grm686e3, 54, 40);
                                                            uint8_t t115 = r113;
                                                            uint64_t t116 = (uint64_t)t115;
                                                            uint64_t r117; bool o118 = __builtin_add_overflow(t108, t116, &r117);
                                                            if (o118) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 54, 25);
                                                            uint64_t t119 = r117;
                                                            gm_l13 = t119;
                                                            uint64_t t120 = gm_l13;
                                                            uint8_t t121 = (t120 > ((uint64_t)65536ULL));
                                                            if (t121) {
                                                                gm_l13 = ((uint64_t)65537ULL);
                                                            }
                                                            uint64_t t122 = gm_l9;
                                                            uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                                                            if (o124) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 56, 21);
                                                            uint64_t t125 = r123;
                                                            gm_l9 = t125;
                                                        }
gm_lb8:;
                                                    }
                                                }
gm_lb6:;
                                                {
                                                    {
                                                        void * t126 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                        uint64_t t127 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                        uint64_t t128 = gm_l12;
                                                        uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)t126) + (uintptr_t)t128 * 1));
                                                        uint8_t t130 = ((t129 == ((uint8_t)48ULL)));
                                                        if (!t130) {
                                                            goto gm_lb10;
                                                        }
                                                        uint64_t t131 = gm_l9;
                                                        uint64_t t132 = gm_l12;
                                                        uint64_t r133; bool o134 = __builtin_sub_overflow(t131, t132, &r133);
                                                        if (o134) gm_trap("integer overflow in `-`", gm_src6d57grm686e3, 58, 40);
                                                        uint64_t t135 = r133;
                                                        uint8_t t136 = (t135 > ((uint64_t)1ULL));
                                                        if (!t136) {
                                                            goto gm_lb10;
                                                        }
                                                        {
                                                            gm_b16_8 t137;
                                                            GM_ST(void *, (uint8_t *)&t137, (void *)(gm_s721l5hj93rdl));
                                                            GM_ST(uintptr_t, (uint8_t *)&t137 + 8, (uintptr_t)(49));
                                                            gm_tf5srh4mp4b3i t138;
                                                            memset(&t138, 0, sizeof t138);
                                                            gm_tj64ui6kruni6 t139;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t139 + 0), t137);
                                                            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t139 + 16), t138);
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
                                                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 40, 0);
                                                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 48, 0);
                                                            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0), ((uint8_t)1ULL));
                                                            GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8), t139);
                                                            goto gm_fdone;
                                                        }
                                                    }
gm_lb10:;
                                                }
gm_lb9:;
                                                uint64_t t140 = gm_l13;
                                                uint8_t t141 = (t140 > ((uint64_t)65536ULL));
                                                if (t141) {
                                                    gm_b16_8 t142;
                                                    GM_ST(void *, (uint8_t *)&t142, (void *)(gm_sqqi4njr3i0t4));
                                                    GM_ST(uintptr_t, (uint8_t *)&t142 + 8, (uintptr_t)(45));
                                                    gm_tf5srh4mp4b3i t143;
                                                    memset(&t143, 0, sizeof t143);
                                                    gm_tj64ui6kruni6 t144;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t144 + 0), t142);
                                                    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t144 + 16), t143);
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 40, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 48, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0), ((uint8_t)1ULL));
                                                    GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8), t144);
                                                    goto gm_fdone;
                                                }
                                                uint64_t t145 = gm_l13;
                                                gm_tf5srh4mp4b3i t146;
                                                GM_ST(uint8_t, (uint8_t *)&t146 + 9, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t146 + 10, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t146 + 12, 0);
                                                GM_ST(uint64_t, ((uint8_t *)&t146), t145);
                                                ((uint8_t *)&t146)[8] = 1;
                                                gm_l10 = t146;
                                                goto gm_lb4;
                                            }
gm_lb5:;
                                        }
gm_lb4:;
                                        {
                                            {
                                                uint64_t t147 = gm_l9;
                                                uint64_t t148 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                uint8_t t149 = (t147 < t148);
                                                if (!t149) {
                                                    goto gm_lb12;
                                                }
                                                void * t150 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                uint64_t t151 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                uint64_t t152 = gm_l9;
                                                uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)t150) + (uintptr_t)t152 * 1));
                                                uint8_t t154 = ((t153 == ((uint8_t)125ULL)));
                                                if (!t154) {
                                                    goto gm_lb12;
                                                }
                                                void * t155 = (void *)((uint8_t *)&F.l1);
                                                gm_b16_8 t156 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                                                gm_b16_8 t157 = GM_LD(gm_b16_8, ((uint8_t *)&t156));
                                                gm_tftb1odc739t5 t158 = gm_f_push_3ef746b(t155, t157);
                                                gm_l29 = t158;
                                                uint8_t t159 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                                if (!t159) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_src6d57grm686e3, 67, 17);
                                                {
                                                    gm_trqfvmjtt4sij t160;
                                                    gm_f_list_uvfu1k1((void *)&t160, (void *)NULL, cx, NULL);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    F.l3 = t160;
                                                }
                                                {
                                                    void * t161 = (void *)((uint8_t *)&F.l2);
                                                    gm_t7g88i52rrtm5 t162;
                                                    gm_f_default_spec_9ntjq2s((void *)&t162);
                                                    gm_tftb1odc739t5 t163 = gm_f_push_nbmosph(t161, &t162);
                                                    gm_l30 = t163;
                                                }
                                                uint8_t t164 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                                                if (!t164) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_src6d57grm686e3, 69, 17);
                                                uint64_t t165 = gm_l9;
                                                uint64_t r166; bool o167 = __builtin_add_overflow(t165, ((uint64_t)1ULL), &r166);
                                                if (o167) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 70, 21);
                                                uint64_t t168 = r166;
                                                gm_l6 = t168;
                                                goto gm_lb11;
                                            }
gm_lb12:;
                                            {
                                                {
                                                    uint64_t t169 = gm_l9;
                                                    uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                    uint8_t t171 = (t169 < t170);
                                                    if (!t171) {
                                                        goto gm_lb14;
                                                    }
                                                    void * t172 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                    uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                    uint64_t t174 = gm_l9;
                                                    uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)t172) + (uintptr_t)t174 * 1));
                                                    uint8_t t176 = ((t175 == ((uint8_t)58ULL)));
                                                    if (!t176) {
                                                        goto gm_lb14;
                                                    }
                                                    gm_tf5srh4mp4b3i t177;
                                                    memset(&t177, 0, sizeof t177);
                                                    gm_l15 = t177;
                                                    uint64_t t178 = gm_l9;
                                                    uint64_t r179; bool o180 = __builtin_add_overflow(t178, ((uint64_t)1ULL), &r179);
                                                    if (o180) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 73, 22);
                                                    uint64_t t181 = r179;
                                                    gm_l16 = t181;
                                                    {
                                                        for (;;) {
                                                            uint64_t t182 = gm_l16;
                                                            uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                            uint8_t t184 = (t182 < t183);
                                                            if (!t184) {
                                                                goto gm_lb15;
                                                            }
                                                            {
                                                                void * t185 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                                uint64_t t186 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                                uint64_t t187 = gm_l16;
                                                                uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)t185) + (uintptr_t)t187 * 1));
                                                                uint8_t t189 = ((t188 == ((uint8_t)125ULL)));
                                                                if (t189) {
                                                                    uint64_t t190 = gm_l16;
                                                                    gm_tf5srh4mp4b3i t191;
                                                                    GM_ST(uint8_t, (uint8_t *)&t191 + 9, 0);
                                                                    GM_ST(uint16_t, (uint8_t *)&t191 + 10, 0);
                                                                    GM_ST(uint32_t, (uint8_t *)&t191 + 12, 0);
                                                                    GM_ST(uint64_t, ((uint8_t *)&t191), t190);
                                                                    ((uint8_t *)&t191)[8] = 1;
                                                                    gm_l15 = t191;
                                                                    goto gm_lb15;
                                                                }
                                                                uint64_t t192 = gm_l16;
                                                                uint64_t r193; bool o194 = __builtin_add_overflow(t192, ((uint64_t)1ULL), &r193);
                                                                if (o194) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 79, 21);
                                                                uint64_t t195 = r193;
                                                                gm_l16 = t195;
                                                            }
gm_lb17:;
                                                        }
                                                    }
gm_lb15:;
                                                    uint8_t t196 = (uint8_t)((((uint8_t *)&gm_l15))[8] != 0);
                                                    if (t196) {
                                                        uint64_t t197 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                                                        gm_l17 = t197;
                                                    } else {
                                                        gm_b16_8 t198;
                                                        GM_ST(void *, (uint8_t *)&t198, (void *)(gm_sl9bj93djc5da));
                                                        GM_ST(uintptr_t, (uint8_t *)&t198 + 8, (uintptr_t)(37));
                                                        gm_tf5srh4mp4b3i t199;
                                                        memset(&t199, 0, sizeof t199);
                                                        gm_tj64ui6kruni6 t200;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t200 + 0), t198);
                                                        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t200 + 16), t199);
                                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
                                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 40, 0);
                                                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 48, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0), ((uint8_t)1ULL));
                                                        GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8), t200);
                                                        goto gm_fdone;
                                                    }
                                                    {
                                                        void * t201 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                                        uint64_t t202 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                                        uint64_t t203 = gm_l9;
                                                        uint64_t r204; bool o205 = __builtin_add_overflow(t203, ((uint64_t)1ULL), &r204);
                                                        if (o205) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 82, 35);
                                                        uint64_t t206 = r204;
                                                        uint64_t t207 = gm_l17;
                                                        uint64_t t208 = (uintptr_t)t206;
                                                        uint64_t t209 = (uintptr_t)t207;
                                                        if (t208 > t209 || t209 > (uintptr_t)t202) gm_trap_slice(t208, t209, t202, gm_src6d57grm686e3, 82, 33);
                                                        uint64_t t210 = ((uintptr_t)t207) - (uintptr_t)t206;
                                                        gm_b16_8 t211;
                                                        GM_ST(void *, (uint8_t *)&t211, (void *)(((uint8_t *)t201 + (uintptr_t)t206 * 1)));
                                                        GM_ST(uintptr_t, (uint8_t *)&t211 + 8, (uintptr_t)(t210));
                                                        gm_f_parse_spec_iod83cp((void *)&gm_l18, (void *)NULL, cx, NULL, t211);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                    }
                                                    {
                                                        {
                                                            uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                                                            uint8_t t213 = (uint8_t)(t212 == ((uint8_t)0ULL));
                                                            if (!t213) {
                                                                goto gm_lb19;
                                                            }
                                                            {
                                                                memmove(((uint8_t *)&gm_l19), (((uint8_t *)&gm_l18) + 8), 24);
                                                            }
                                                            {
                                                                memmove(((uint8_t *)&gm_l21), ((uint8_t *)&gm_l19), 24);
                                                            }
                                                            goto gm_lb18;
                                                        }
gm_lb19:;
                                                        {
                                                            uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                                                            uint8_t t215 = (uint8_t)(t214 == ((uint8_t)1ULL));
                                                            if (!t215) {
                                                                goto gm_lb20;
                                                            }
                                                            gm_b16_8 t216 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                                                            gm_l20 = t216;
                                                            {
                                                                gm_b16_8 t217 = gm_l20;
                                                                gm_tf5srh4mp4b3i t218;
                                                                memset(&t218, 0, sizeof t218);
                                                                gm_tj64ui6kruni6 t219;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t219 + 0), t217);
                                                                GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t219 + 16), t218);
                                                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
                                                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
                                                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 40, 0);
                                                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 48, 0);
                                                                GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0), ((uint8_t)1ULL));
                                                                GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8), t219);
                                                                goto gm_fdone;
                                                            }
                                                        }
gm_lb20:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 83, 25);
                                                    }
gm_lb18:;
                                                    void * t220 = (void *)((uint8_t *)&F.l1);
                                                    gm_b16_8 t221 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                                                    gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&t221));
                                                    gm_tftb1odc739t5 t223 = gm_f_push_3ef746b(t220, t222);
                                                    gm_l31 = t223;
                                                    uint8_t t224 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                                                    if (!t224) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_src6d57grm686e3, 87, 17);
                                                    {
                                                        gm_trqfvmjtt4sij t225;
                                                        gm_f_list_uvfu1k1((void *)&t225, (void *)NULL, cx, NULL);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        F.l3 = t225;
                                                    }
                                                    {
                                                        void * t226 = (void *)((uint8_t *)&F.l2);
                                                        gm_tftb1odc739t5 t227 = gm_f_push_nbmosph(t226, &(*(gm_t7g88i52rrtm5 *)((uint8_t *)&gm_l21)));
                                                        gm_l32 = t227;
                                                    }
                                                    uint8_t t228 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                                                    if (!t228) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_src6d57grm686e3, 89, 17);
                                                    uint64_t t229 = gm_l17;
                                                    uint64_t r230; bool o231 = __builtin_add_overflow(t229, ((uint64_t)1ULL), &r230);
                                                    if (o231) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 90, 21);
                                                    uint64_t t232 = r230;
                                                    gm_l6 = t232;
                                                    goto gm_lb13;
                                                }
gm_lb14:;
                                                {
                                                    gm_b16_8 t233;
                                                    GM_ST(void *, (uint8_t *)&t233, (void *)(gm_s4k55k154s62m));
                                                    GM_ST(uintptr_t, (uint8_t *)&t233 + 8, (uintptr_t)(119));
                                                    gm_tf5srh4mp4b3i t234;
                                                    memset(&t234, 0, sizeof t234);
                                                    gm_tj64ui6kruni6 t235;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t235 + 0), t233);
                                                    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t235 + 16), t234);
                                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
                                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 40, 0);
                                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 48, 0);
                                                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0), ((uint8_t)1ULL));
                                                    GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8), t235);
                                                    goto gm_fdone;
                                                }
                                            }
gm_lb13:;
                                        }
gm_lb11:;
                                        {
                                            {
                                                uint8_t t236 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
                                                if (!t236) {
                                                    goto gm_lb22;
                                                }
                                                uint64_t t237 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                                                gm_l22 = t237;
                                                void * t238 = (void *)((uint8_t *)&F.l4);
                                                uint64_t t239 = gm_l22;
                                                gm_tftb1odc739t5 t240 = gm_f_push_ucgmlo2(t238, t239);
                                                gm_l33 = t240;
                                                uint8_t t241 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                                                if (!t241) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_src6d57grm686e3, 95, 17);
                                                goto gm_lb21;
                                            }
gm_lb22:;
                                            uint64_t t242 = gm_l5;
                                            uint64_t r243; bool o244 = __builtin_add_overflow(t242, ((uint64_t)1ULL), &r243);
                                            if (o244) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 97, 17);
                                            uint64_t t245 = r243;
                                            gm_l5 = t245;
                                        }
gm_lb21:;
                                    } else {
                                        uint8_t t246 = gm_l7;
                                        uint8_t t247 = ((t246 == ((uint8_t)125ULL)));
                                        if (t247) {
                                            gm_b16_8 t248;
                                            GM_ST(void *, (uint8_t *)&t248, (void *)(gm_smvgcg1j21t6v));
                                            GM_ST(uintptr_t, (uint8_t *)&t248 + 8, (uintptr_t)(26));
                                            uint64_t t249 = gm_l6;
                                            gm_tf5srh4mp4b3i t250;
                                            GM_ST(uint8_t, (uint8_t *)&t250 + 9, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t250 + 10, 0);
                                            GM_ST(uint32_t, (uint8_t *)&t250 + 12, 0);
                                            GM_ST(uint64_t, ((uint8_t *)&t250), t249);
                                            ((uint8_t *)&t250)[8] = 1;
                                            gm_tj64ui6kruni6 t251;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t251 + 0), t248);
                                            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t251 + 16), t250);
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
                                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 40, 0);
                                            GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 48, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0), ((uint8_t)1ULL));
                                            GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8), t251);
                                            goto gm_fdone;
                                        } else {
                                            void * t252 = (void *)((uint8_t *)&F.l3);
                                            uint8_t t253 = gm_l7;
                                            gm_tftb1odc739t5 t254 = gm_f_push_t83rg2m(t252, t253);
                                            gm_l34 = t254;
                                            uint8_t t255 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                            if (!t255) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_src6d57grm686e3, 102, 13);
                                            uint64_t t256 = gm_l6;
                                            uint64_t r257; bool o258 = __builtin_add_overflow(t256, ((uint64_t)1ULL), &r257);
                                            if (o258) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 103, 13);
                                            uint64_t t259 = r257;
                                            gm_l6 = t259;
                                        }
                                    }
                                }
                            }
                        }
                    }
gm_lb3:;
                }
            }
        }
gm_lb1:;
    }
    {
        void * t260 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t261 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_b16_8 t262 = GM_LD(gm_b16_8, ((uint8_t *)&t261));
        gm_tftb1odc739t5 t263 = gm_f_push_3ef746b(t260, t262);
        gm_l35 = t263;
        uint8_t t264 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
        if (!t264) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_src6d57grm686e3, 106, 5);
        {
            {
                uint64_t t265 = gm_l5;
                uint8_t t266 = (t265 > ((uint64_t)0ULL));
                if (!t266) {
                    goto gm_lb24;
                }
                uint64_t t267 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                uint8_t t268 = (t267 > ((uint64_t)0ULL));
                if (!t268) {
                    goto gm_lb24;
                }
                {
                    gm_b16_8 t269;
                    GM_ST(void *, (uint8_t *)&t269, (void *)(gm_sppcm1qavsp1k));
                    GM_ST(uintptr_t, (uint8_t *)&t269 + 8, (uintptr_t)(79));
                    gm_tf5srh4mp4b3i t270;
                    memset(&t270, 0, sizeof t270);
                    gm_tj64ui6kruni6 t271;
                    GM_ST(gm_b16_8, ((uint8_t *)&t271 + 0), t269);
                    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t271 + 16), t270);
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 48, 0);
                    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tj64ui6kruni6, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8), t271);
                    goto gm_fdone;
                }
            }
gm_lb24:;
        }
gm_lb23:;
        gm_b16_8 t272 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
        gm_b16_8 t273 = GM_LD(gm_b16_8, ((uint8_t *)&t272));
        gm_b16_8 t274 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
        gm_b16_8 t275 = GM_LD(gm_b16_8, ((uint8_t *)&t274));
        gm_b16_8 t276 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_b16_8 t277 = GM_LD(gm_b16_8, ((uint8_t *)&t276));
        gm_tfeab279c16rr t278;
        GM_ST(gm_b16_8, ((uint8_t *)&t278 + 0), t273);
        GM_ST(gm_b16_8, ((uint8_t *)&t278 + 16), t275);
        GM_ST(gm_b16_8, ((uint8_t *)&t278 + 32), t277);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 0), ((uint8_t)0ULL));
        GM_ST(gm_tfeab279c16rr, ((uint8_t *)&(*(gm_t1jll27j7ae34 *)gm_out) + 8), t278);
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_placeholders_text_rtvia6b(void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_resolve_write_e97fjom(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_resolve_write_e97fjom F;
    void * gm_l0; /* s */
    void * gm_l1; /* mc */
    void * gm_l2; /* e */
    void * gm_l3; /* w */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint32_t gm_l5; /* l */
    void * gm_l6; /* sites */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_tl874t2atnlh3 gm_l9; /* x */
    void * gm_l10; /* a */
    gm_b16_8 gm_l11; /* text */
    gm_tj64ui6kruni6 gm_l13; /* err */
    void * gm_l15; /* a */
    gm_b16_8 gm_l16; /* es */
    uint64_t gm_l17; /* n */
    uint8_t gm_l18; /* numbered */
    void * gm_l19; /* a */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    uint64_t gm_l25; /* at */
    void * gm_l26; /* base */
    void * gm_l27; /* write */
    GM_STACK_CHECK(&F, 2505u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tkn42u7na3t81 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_t1jll27j7ae34 gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_tkn42u7na3t81 gm_l32;
    gm_th2lqbo77pvsc gm_l33;
    gm_tq7s98ihonkju gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    gm_tq7s98ihonkju gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tq7s98ihonkju gm_l39;
    {
        {
            {
                {
                    {
                        void * t25 = gm_l2;
                        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t25) + 8) + 0));
                        uint8_t t27 = (uint8_t)(t26 == ((uint8_t)56ULL));
                        if (!t27) {
                            goto gm_lb2;
                        }
                        void * t28 = GM_LD(void *, ((((uint8_t *)t25) + 8) + 8));
                        gm_l3 = t28;
                        void * t29 = gm_l1;
                        gm_tl874t2atnlh3 t30 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t29) + 96));
                        gm_l4 = t30;
                        void * t31 = gm_l2;
                        gm_b16_8 t32 = gm_f_empty_slice_g3tddrb_2();
                        gm_to0f5qn75cgg8 t33;
                        memset(&t33, 0, sizeof t33);
                        GM_ST(uint8_t, ((uint8_t *)&t33 + 0), ((uint8_t)35ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t33 + 8), t32);
                        GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t31) + 8), t33);
                        {
                            {
                                void * t34 = gm_l3;
                                void * t35 = GM_LD(void *, (((uint8_t *)t34) + 40));
                                uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                                uint8_t t37 = (uint8_t)(t36 == ((uint8_t)15ULL));
                                if (!t37) {
                                    goto gm_lb4;
                                }
                                uint32_t t38 = GM_LD(uint32_t, ((((uint8_t *)t35) + 8) + 8));
                                gm_l5 = t38;
                                void * t39 = gm_l1;
                                uint8_t t40 = (uint8_t)(GM_LD(void *, (((uint8_t *)t39) + 136)) != 0);
                                if (!t40) {
                                    goto gm_lb4;
                                }
                                void * t41 = GM_LD(void *, (((uint8_t *)t39) + 136));
                                gm_l6 = t41;
                                void * t42 = gm_l1;
                                gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)t42) + 120));
                                gm_l7 = t43;
                                gm_l8 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t44 = gm_l8;
                                        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                        uint8_t t46 = (t44 < t45);
                                        if (!t46) {
                                            goto gm_lb5;
                                        }
                                        {
                                            void * t47 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                            uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                            uint64_t t49 = gm_l8;
                                            {
                                                {
                                                    gm_tl874t2atnlh3 t50 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t47) + (uintptr_t)t49 * 12));
                                                    gm_l9 = t50;
                                                    uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)&gm_l9) + 4));
                                                    uint32_t t52 = gm_l5;
                                                    uint8_t t53 = ((t51 == t52));
                                                    if (t53) {
                                                        void * t54 = gm_l6;
                                                        uint32_t t55 = GM_LD(uint32_t, ((uint8_t *)&gm_l9));
                                                        {
                                                            {
                                                                void * t56 = gm_l3;
                                                                uint8_t t57 = (uint8_t)(GM_LD(void *, (((uint8_t *)t56) + 48)) != 0);
                                                                if (!t57) {
                                                                    goto gm_lb11;
                                                                }
                                                                void * t58 = GM_LD(void *, (((uint8_t *)t56) + 48));
                                                                gm_l10 = t58;
                                                                void * t59 = gm_l10;
                                                                gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t59));
                                                                gm_tkn42u7na3t81 t61;
                                                                GM_ST(uint8_t, (uint8_t *)&t61 + 5, 0);
                                                                GM_ST(uint16_t, (uint8_t *)&t61 + 6, 0);
                                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t61), t60);
                                                                ((uint8_t *)&t61)[4] = 1;
                                                                gm_l28 = t61;
                                                                goto gm_lb10;
                                                            }
gm_lb11:;
                                                            gm_tkn42u7na3t81 t62;
                                                            memset(&t62, 0, sizeof t62);
                                                            gm_l28 = t62;
                                                        }
gm_lb10:;
                                                        gm_tkn42u7na3t81 t63 = gm_l28;
                                                        gm_tuo61a5rdrbtf t64;
                                                        GM_ST(uint32_t, ((uint8_t *)&t64 + 0), t55);
                                                        GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t64 + 4), t63);
                                                        gm_tftb1odc739t5 t65 = gm_f_push_0p2fb2b(t54, t64);
                                                        gm_l29 = t65;
                                                        uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                                        if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_src6d57grm686e3, 516, 31);
                                                    }
                                                    goto gm_lb8;
                                                }
gm_lb9:;
                                                gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 515, 9);
                                            }
gm_lb8:;
                                        }
gm_lb7:;
                                        uint64_t t67 = gm_l8;
                                        uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                                        uint64_t t70 = r68;
                                        gm_l8 = t70;
                                    }
                                }
gm_lb5:;
                                void * t71 = gm_l0;
                                void * t72 = gm_l2;
                                void * t73 = gm_l3;
                                gm_b0_1 t74 = gm_f_write_table_5n1kjkn((void *)NULL, cx, NULL, t71, t72, t73);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = GM_ZERO(gm_b0_1);
                                goto gm_fret;
                            }
gm_lb4:;
                        }
gm_lb3:;
                    }
                    {
                        {
                            {
                                void * t75 = gm_l3;
                                void * t76 = GM_LD(void *, (((uint8_t *)t75) + 40));
                                uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t76) + 8) + 0));
                                uint8_t t78 = (uint8_t)(t77 == ((uint8_t)5ULL));
                                if (!t78) {
                                    goto gm_lb13;
                                }
                                gm_b16_8 t79 = GM_LD(gm_b16_8, ((((uint8_t *)t76) + 8) + 8));
                                gm_l11 = t79;
                                {
                                    gm_b16_8 t80 = gm_l11;
                                    gm_f_parse_template_rlpl2k8((void *)&gm_l30, (void *)NULL, cx, NULL, t80);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                {
                                    {
                                        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l30) + 0));
                                        uint8_t t82 = (uint8_t)(t81 == ((uint8_t)0ULL));
                                        if (!t82) {
                                            goto gm_lb15;
                                        }
                                        {
                                            memmove(((uint8_t *)&F.l12), (((uint8_t *)&gm_l30) + 8), 48);
                                        }
                                        {
                                            memmove(((uint8_t *)&F.l14), ((uint8_t *)&F.l12), 48);
                                        }
                                        goto gm_lb14;
                                    }
gm_lb15:;
                                    {
                                        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l30) + 0));
                                        uint8_t t84 = (uint8_t)(t83 == ((uint8_t)1ULL));
                                        if (!t84) {
                                            goto gm_lb16;
                                        }
                                        {
                                            memmove(((uint8_t *)&gm_l13), (((uint8_t *)&gm_l30) + 8), 32);
                                        }
                                        {
                                            void * t85 = gm_l0;
                                            gm_tl874t2atnlh3 t86 = gm_l4;
                                            gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l13));
                                            gm_tnpifv20irnph t88;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t87);
                                            gm_f_format_jvf1ma2((void *)&gm_l31, (void *)NULL, cx, NULL, t88);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                                            if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_src6d57grm686e3, 528, 27);
                                            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                                            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
                                            gm_b0_1 t92 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t85, t86, t91);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        F.ret = GM_ZERO(gm_b0_1);
                                        goto gm_fret;
                                    }
gm_lb16:;
                                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 525, 15);
                                }
gm_lb14:;
                                {
                                    void * t93 = gm_l0;
                                    gm_tl874t2atnlh3 t94 = gm_l4;
                                    gm_tfeab279c16rr t95 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)&F.l14));
                                    {
                                        {
                                            void * t96 = gm_l3;
                                            uint8_t t97 = (uint8_t)(GM_LD(void *, (((uint8_t *)t96) + 48)) != 0);
                                            if (!t97) {
                                                goto gm_lb18;
                                            }
                                            void * t98 = GM_LD(void *, (((uint8_t *)t96) + 48));
                                            gm_l15 = t98;
                                            void * t99 = gm_l15;
                                            gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t99));
                                            gm_tkn42u7na3t81 t101;
                                            GM_ST(uint8_t, (uint8_t *)&t101 + 5, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t101 + 6, 0);
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t101), t100);
                                            ((uint8_t *)&t101)[4] = 1;
                                            gm_l32 = t101;
                                            goto gm_lb17;
                                        }
gm_lb18:;
                                        gm_tkn42u7na3t81 t102;
                                        memset(&t102, 0, sizeof t102);
                                        gm_l32 = t102;
                                    }
gm_lb17:;
                                    gm_tkn42u7na3t81 t103 = gm_l32;
                                    gm_f_check_template_brtb8t0((void *)&gm_l33, (void *)NULL, cx, NULL, t93, t94, &t95, t103);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t104 = (uint8_t)((((uint8_t *)&gm_l33))[16] != 0);
                                if (t104) {
                                    gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l33));
                                    gm_l16 = t105;
                                } else {
                                    F.ret = GM_ZERO(gm_b0_1);
                                    goto gm_fret;
                                }
                                uint64_t t106 = GM_LD(uint64_t, ((((uint8_t *)&F.l14) + 16) + 8));
                                gm_l17 = t106;
                                uint64_t t107 = GM_LD(uint64_t, ((((uint8_t *)&F.l14) + 32) + 8));
                                uint8_t t108 = (t107 > ((uint64_t)0ULL));
                                gm_l18 = t108;
                                void * t109 = gm_l3;
                                uint8_t t110 = (uint8_t)(GM_LD(void *, (((uint8_t *)t109) + 48)) != 0);
                                if (t110) {
                                    void * t111 = GM_LD(void *, (((uint8_t *)t109) + 48));
                                    gm_l19 = t111;
                                } else {
                                    {
                                        void * t112 = gm_l2;
                                        void * t113 = gm_l3;
                                        gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                        gm_b16_8 t115 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 16));
                                        gm_th1g69d7vpipg t116 = GM_LD(gm_th1g69d7vpipg, ((uint8_t *)t113));
                                        gm_b16_8 t117 = gm_f_empty_slice_g3tddrb_2();
                                        gm_t150deeftog49 t118;
                                        GM_ST(gm_th1g69d7vpipg, ((uint8_t *)&t118 + 0), t116);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t118 + 40), t114);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t118 + 56), t115);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t118 + 72), t117);
                                        GM_ST(void *, ((uint8_t *)&t118 + 88), ((void *)0));
                                        gm_tq7s98ihonkju t119 = gm_f_new_2pnaa87((void *)NULL, cx, NULL, &t118, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kei7njaa9je)));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l34 = t119;
                                        uint8_t t120 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                                        if (!t120) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_src6d57grm686e3, 536, 25);
                                        void * t121 = GM_LD(void *, (((uint8_t *)&gm_l34) + 8));
                                        gm_to0f5qn75cgg8 t122;
                                        memset(&t122, 0, sizeof t122);
                                        GM_ST(uint8_t, ((uint8_t *)&t122 + 0), ((uint8_t)55ULL));
                                        GM_ST(void *, ((uint8_t *)&t122 + 8), t121);
                                        GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t112) + 8), t122);
                                    }
                                    F.ret = GM_ZERO(gm_b0_1);
                                    goto gm_fret;
                                }
                                {
                                    gm_trqfvmjtt4sij t123;
                                    gm_f_list_ir0h3tc_2((void *)&t123, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l20 = t123;
                                }
                                {
                                    void * t124 = (void *)((uint8_t *)&F.l20);
                                    void * t125 = gm_l3;
                                    uint32_t t126 = GM_LD(uint32_t, (((uint8_t *)t125) + 56));
                                    void * t127 = gm_l19;
                                    gm_ttggq7ng4kp3p t128;
                                    GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
                                    GM_ST(uint32_t, ((uint8_t *)&t128 + 0), t126);
                                    GM_ST(void *, ((uint8_t *)&t128 + 8), t127);
                                    gm_trcmmd12cefb1 t129;
                                    GM_ST(uint8_t, (uint8_t *)&t129 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t129 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t129 + 24, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)0ULL));
                                    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t129 + 8), t128);
                                    gm_tftb1odc739t5 t130 = gm_f_push_d6c76ud_2(t124, &t129);
                                    gm_l35 = t130;
                                }
                                uint8_t t131 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                                if (!t131) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_src6d57grm686e3, 540, 5);
                                {
                                    gm_trqfvmjtt4sij t132;
                                    gm_f_list_vp1djv1_2((void *)&t132, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l21 = t132;
                                }
                                gm_l23 = ((uint64_t)0ULL);
                                uint64_t t133 = gm_l17;
                                gm_l22 = t133;
                                {
                                    for (;;) {
                                        uint64_t t134 = gm_l23;
                                        uint64_t t135 = gm_l22;
                                        uint8_t t136 = (t134 < t135);
                                        if (!t136) {
                                            goto gm_lb19;
                                        }
                                        {
                                            {
                                                {
                                                    uint64_t t137 = gm_l23;
                                                    gm_l24 = t137;
                                                    uint8_t t138 = gm_l18;
                                                    if (t138) {
                                                        void * t139 = GM_LD(void *, (((uint8_t *)&F.l14) + 32));
                                                        uint64_t t140 = GM_LD(uint64_t, ((((uint8_t *)&F.l14) + 32) + 8));
                                                        uint64_t t141 = gm_l24;
                                                        if ((uintptr_t)t141 >= (uintptr_t)(t140)) gm_trap_index((uintptr_t)t141, t140, gm_src6d57grm686e3, 543, 29);
                                                        uint64_t t142 = GM_LD(uint64_t, (((uint8_t *)t139) + (uintptr_t)t141 * 8));
                                                        gm_l25 = t142;
                                                    } else {
                                                        uint64_t t143 = gm_l24;
                                                        gm_l25 = t143;
                                                    }
                                                    {
                                                        void * t144 = gm_l19;
                                                        gm_t88rcucg1b7uk t145 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t144));
                                                        void * t146 = gm_l3;
                                                        uint32_t t147 = GM_LD(uint32_t, (((uint8_t *)t146) + 56));
                                                        void * t148 = gm_l2;
                                                        gm_tl874t2atnlh3 t149 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t148) + 72));
                                                        gm_to0f5qn75cgg8 t150;
                                                        memset(&t150, 0, sizeof t150);
                                                        GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)15ULL));
                                                        GM_ST(uint32_t, ((uint8_t *)&t150 + 8), t147);
                                                        void * t151 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t145, &t150, t149);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_l26 = t151;
                                                    }
                                                    {
                                                        void * t152 = (void *)((uint8_t *)&F.l21);
                                                        void * t153 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                                        uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                                        uint64_t t155 = gm_l25;
                                                        if ((uintptr_t)t155 >= (uintptr_t)(t154)) gm_trap_index((uintptr_t)t155, t154, gm_src6d57grm686e3, 545, 28);
                                                        gm_t88rcucg1b7uk t156 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t153) + (uintptr_t)t155 * 4));
                                                        void * t157 = gm_l26;
                                                        uint64_t t158 = gm_l25;
                                                        uint32_t t159 = (uint32_t)t158;
                                                        gm_tbl8il92l97ut t160;
                                                        GM_ST(uint32_t, (uint8_t *)&t160 + 12, 0);
                                                        GM_ST(void *, ((uint8_t *)&t160 + 0), t157);
                                                        GM_ST(uint32_t, ((uint8_t *)&t160 + 8), t159);
                                                        void * t161 = gm_l2;
                                                        gm_tl874t2atnlh3 t162 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t161) + 72));
                                                        gm_to0f5qn75cgg8 t163;
                                                        memset(&t163, 0, sizeof t163);
                                                        GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)19ULL));
                                                        GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t163 + 8), t160);
                                                        void * t164 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t156, &t163, t162);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_tftb1odc739t5 t165 = gm_f_push_d943vgp_2(t152, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t164)));
                                                        gm_l36 = t165;
                                                    }
                                                    uint8_t t166 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                                    if (!t166) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_src6d57grm686e3, 545, 9);
                                                    goto gm_lb22;
                                                }
gm_lb23:;
                                                gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 542, 5);
                                            }
gm_lb22:;
                                        }
gm_lb21:;
                                        uint64_t t167 = gm_l23;
                                        uint64_t r168; bool o169 = __builtin_add_overflow(t167, ((uint64_t)1ULL), &r168);
                                        uint64_t t170 = r168;
                                        gm_l23 = t170;
                                    }
                                }
gm_lb19:;
                                {
                                    void * t171 = gm_l0;
                                    void * t172 = (void *)(((uint8_t *)t171) + 8);
                                    gm_t88rcucg1b7uk t173 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t172);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t174 = gm_l3;
                                    gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                                    gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 16));
                                    gm_b16_8 t177 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
                                    gm_th1g69d7vpipg t178 = GM_LD(gm_th1g69d7vpipg, ((uint8_t *)t174));
                                    gm_t150deeftog49 t179;
                                    GM_ST(gm_th1g69d7vpipg, ((uint8_t *)&t179 + 0), t178);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t179 + 40), t175);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t179 + 56), t176);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t179 + 72), t177);
                                    GM_ST(void *, ((uint8_t *)&t179 + 88), ((void *)0));
                                    gm_tq7s98ihonkju t180 = gm_f_new_2pnaa87((void *)NULL, cx, NULL, &t179, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knhubf3blct)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l37 = t180;
                                    uint8_t t181 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                    if (!t181) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 8, gm_src6d57grm686e3, 547, 42);
                                    void * t182 = GM_LD(void *, (((uint8_t *)&gm_l37) + 8));
                                    void * t183 = gm_l2;
                                    gm_tl874t2atnlh3 t184 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t183) + 72));
                                    gm_to0f5qn75cgg8 t185;
                                    memset(&t185, 0, sizeof t185);
                                    GM_ST(uint8_t, ((uint8_t *)&t185 + 0), ((uint8_t)55ULL));
                                    GM_ST(void *, ((uint8_t *)&t185 + 8), t182);
                                    void * t186 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t173, &t185, t184);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l27 = t186;
                                }
                                {
                                    void * t187 = (void *)((uint8_t *)&F.l20);
                                    void * t188 = gm_l27;
                                    gm_trcmmd12cefb1 t189;
                                    GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t189 + 16, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t189 + 24, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)4ULL));
                                    GM_ST(void *, ((uint8_t *)&t189 + 8), t188);
                                    gm_tftb1odc739t5 t190 = gm_f_push_d6c76ud_2(t187, &t189);
                                    gm_l38 = t190;
                                }
                                uint8_t t191 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                if (!t191) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_src6d57grm686e3, 548, 5);
                                {
                                    void * t192 = gm_l2;
                                    gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                                    void * t194 = gm_l2;
                                    gm_tl874t2atnlh3 t195 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t194) + 72));
                                    gm_tgabaikr08mjg t196;
                                    GM_ST(uint32_t, (uint8_t *)&t196 + 36, 0);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t196 + 0), t193);
                                    GM_ST(void *, ((uint8_t *)&t196 + 16), ((void *)0));
                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t196 + 24), t195);
                                    gm_tq7s98ihonkju t197 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t196, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k7peov41e38)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l39 = t197;
                                    uint8_t t198 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                                    if (!t198) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 8, gm_src6d57grm686e3, 549, 21);
                                    void * t199 = GM_LD(void *, (((uint8_t *)&gm_l39) + 8));
                                    gm_to0f5qn75cgg8 t200;
                                    memset(&t200, 0, sizeof t200);
                                    GM_ST(uint8_t, ((uint8_t *)&t200 + 0), ((uint8_t)46ULL));
                                    GM_ST(void *, ((uint8_t *)&t200 + 8), t199);
                                    GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t192) + 8), t200);
                                }
                                goto gm_lb12;
                            }
gm_lb13:;
                            void * t201 = gm_l0;
                            gm_tl874t2atnlh3 t202 = gm_l4;
                            gm_b16_8 t203;
                            GM_ST(void *, (uint8_t *)&t203, (void *)(gm_srundlmqvm06p));
                            GM_ST(uintptr_t, (uint8_t *)&t203 + 8, (uintptr_t)(59));
                            gm_b0_1 t204 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t201, t202, t203);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
gm_lb12:;
                    }
                    {
                        goto gm_lb1;
                    }
                }
gm_lb2:;
            }
            {
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_source_offset_of_vlcjdq6(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_source_offset_of_vlcjdq6 F;
    gm_b16_8 gm_l0; /* lit */
    uint64_t gm_l1; /* at */
    gm_b16_8 gm_l2; /* b */
    uint64_t gm_l3; /* i */
    uint64_t gm_l4; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 564u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l5;
    uint8_t gm_l6;
    gm_tf5srh4mp4b3i gm_l7;
    gm_tqs5p4n2tq19v gm_l8;
    gm_b16_8 t7 = gm_l0;
    gm_l2 = t7;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t9 = (t8 < ((uint64_t)2ULL));
    if (t9) {
        gm_l6 = ((uint8_t)1);
    } else {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t11, gm_src6d57grm686e3, 242, 21);
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)((uint64_t)0ULL) * 1));
        uint8_t t13 = (!(t12 == ((uint8_t)34ULL)));
        gm_l6 = t13;
    }
    uint8_t t14 = gm_l6;
    if (t14) {
        gm_l5 = ((uint8_t)1);
    } else {
        void * t15 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
        if (o19) gm_trap("integer overflow in `-`", gm_src6d57grm686e3, 242, 42);
        uint64_t t20 = r18;
        if ((uintptr_t)t20 >= (uintptr_t)(t16)) gm_trap_index((uintptr_t)t20, t16, gm_src6d57grm686e3, 242, 40);
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t15) + (uintptr_t)t20 * 1));
        uint8_t t22 = (!(t21 == ((uint8_t)34ULL)));
        gm_l5 = t22;
    }
    uint8_t t23 = gm_l5;
    if (t23) {
        gm_tf5srh4mp4b3i t24;
        memset(&t24, 0, sizeof t24);
        F.ret = t24;
        goto gm_fret;
    }
    {
        {
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t26 = (t25 >= ((uint64_t)3ULL));
            if (!t26) {
                goto gm_lb2;
            }
            void * t27 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t28)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t28, gm_src6d57grm686e3, 243, 23);
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)t27) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint8_t t30 = ((t29 == ((uint8_t)34ULL)));
            if (!t30) {
                goto gm_lb2;
            }
            void * t31 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t32)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t32, gm_src6d57grm686e3, 243, 39);
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)t31) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint8_t t34 = ((t33 == ((uint8_t)34ULL)));
            if (!t34) {
                goto gm_lb2;
            }
            gm_tf5srh4mp4b3i t35;
            memset(&t35, 0, sizeof t35);
            F.ret = t35;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_l3 = ((uint64_t)1ULL);
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t36 = gm_l3;
            uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t r38; bool o39 = __builtin_sub_overflow(t37, ((uint64_t)1ULL), &r38);
            if (o39) gm_trap("integer overflow in `-`", gm_src6d57grm686e3, 246, 15);
            uint64_t t40 = r38;
            uint8_t t41 = (t36 < t40);
            if (!t41) {
                goto gm_lb3;
            }
            {
                uint64_t t42 = gm_l4;
                uint64_t t43 = gm_l1;
                uint8_t t44 = ((t42 == t43));
                if (t44) {
                    void * t45 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t47 = gm_l3;
                    if ((uintptr_t)t47 >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)t47, t46, gm_src6d57grm686e3, 247, 32);
                    uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t45) + (uintptr_t)t47 * 1));
                    uint8_t t49 = (!(t48 == ((uint8_t)92ULL)));
                    if (t49) {
                        uint64_t t50 = gm_l3;
                        gm_tf5srh4mp4b3i t51;
                        GM_ST(uint8_t, (uint8_t *)&t51 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t51 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t51 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t51), t50);
                        ((uint8_t *)&t51)[8] = 1;
                        gm_l7 = t51;
                    } else {
                        gm_tf5srh4mp4b3i t52;
                        memset(&t52, 0, sizeof t52);
                        gm_l7 = t52;
                    }
                    gm_tf5srh4mp4b3i t53 = gm_l7;
                    F.ret = t53;
                    goto gm_fret;
                }
                void * t54 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t56 = gm_l3;
                if ((uintptr_t)t56 >= (uintptr_t)(t55)) gm_trap_index((uintptr_t)t56, t55, gm_src6d57grm686e3, 248, 12);
                uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t54) + (uintptr_t)t56 * 1));
                uint8_t t58 = ((t57 == ((uint8_t)92ULL)));
                if (t58) {
                    void * t59 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t61 = gm_l3;
                    uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                    if (o63) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 249, 22);
                    uint64_t t64 = r62;
                    uint8_t t65 = (t64 < t60);
                    if (t65) {
                        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t59) + (uintptr_t)t64 * 1));
                        gm_tqs5p4n2tq19v t67;
                        GM_ST(uint8_t, ((uint8_t *)&t67), t66);
                        ((uint8_t *)&t67)[1] = 1;
                        gm_l8 = t67;
                    } else {
                        gm_tqs5p4n2tq19v t68;
                        memset(&t68, 0, sizeof t68);
                        gm_l8 = t68;
                    }
                    gm_tqs5p4n2tq19v t69 = gm_l8;
                    gm_tqs5p4n2tq19v t70;
                    GM_ST(uint8_t, ((uint8_t *)&t70), ((uint8_t)110ULL));
                    ((uint8_t *)&t70)[1] = 1;
                    gm_tqs5p4n2tq19v t71;
                    GM_ST(uint8_t, ((uint8_t *)&t71), ((uint8_t)116ULL));
                    ((uint8_t *)&t71)[1] = 1;
                    gm_tqs5p4n2tq19v t72;
                    GM_ST(uint8_t, ((uint8_t *)&t72), ((uint8_t)114ULL));
                    ((uint8_t *)&t72)[1] = 1;
                    gm_tqs5p4n2tq19v t73;
                    GM_ST(uint8_t, ((uint8_t *)&t73), ((uint8_t)48ULL));
                    ((uint8_t *)&t73)[1] = 1;
                    gm_tqs5p4n2tq19v t74;
                    GM_ST(uint8_t, ((uint8_t *)&t74), ((uint8_t)92ULL));
                    ((uint8_t *)&t74)[1] = 1;
                    gm_tqs5p4n2tq19v t75;
                    GM_ST(uint8_t, ((uint8_t *)&t75), ((uint8_t)34ULL));
                    ((uint8_t *)&t75)[1] = 1;
                    gm_tqs5p4n2tq19v t76;
                    GM_ST(uint8_t, ((uint8_t *)&t76), ((uint8_t)39ULL));
                    ((uint8_t *)&t76)[1] = 1;
                    gm_t6s1lqopkm7fs t77;
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t77 + 0), t70);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t77 + 2), t71);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t77 + 4), t72);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t77 + 6), t73);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t77 + 8), t74);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t77 + 10), t75);
                    GM_ST(gm_tqs5p4n2tq19v, ((uint8_t *)&t77 + 12), t76);
                    uint8_t t78 = 0;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t77) + 0, ((uint8_t *)&t69))) t78 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t77) + 2, ((uint8_t *)&t69))) t78 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t77) + 4, ((uint8_t *)&t69))) t78 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t77) + 6, ((uint8_t *)&t69))) t78 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t77) + 8, ((uint8_t *)&t69))) t78 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t77) + 10, ((uint8_t *)&t69))) t78 = 1;
                    if (gm_eqb44cb3cf1n(((uint8_t *)&t77) + 12, ((uint8_t *)&t69))) t78 = 1;
                    uint8_t t79 = (uint8_t)(!t78);
                    if (t79) {
                        gm_tf5srh4mp4b3i t80;
                        memset(&t80, 0, sizeof t80);
                        F.ret = t80;
                        goto gm_fret;
                    }
                    uint64_t t81 = gm_l3;
                    uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)2ULL), &r82);
                    if (o83) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 250, 13);
                    uint64_t t84 = r82;
                    gm_l3 = t84;
                } else {
                    uint64_t t85 = gm_l3;
                    uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                    if (o87) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 252, 13);
                    uint64_t t88 = r86;
                    gm_l3 = t88;
                }
                uint64_t t89 = gm_l4;
                uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                if (o91) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 254, 9);
                uint64_t t92 = r90;
                gm_l4 = t92;
            }
gm_lb5:;
        }
    }
gm_lb3:;
    gm_tf5srh4mp4b3i t93;
    memset(&t93, 0, sizeof t93);
    F.ret = t93;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_template_args_vf1gdn1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_template_args_vf1gdn1 F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* bytes */
    gm_tl874t2atnlh3 gm_l2; /* template_span */
    gm_b16_8 gm_l3; /* rest */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_tj64ui6kruni6 gm_l7; /* err */
    uint64_t gm_l9; /* at */
    gm_b16_8 gm_l10; /* text */
    uint64_t gm_l11; /* off */
    uint32_t gm_l12; /* lo */
    uint64_t gm_l14; /* n */
    uint8_t gm_l15; /* numbered */
    void * gm_l19; /* v0 */
    gm_t88rcucg1b7uk gm_l20; /* written */
    void * gm_l21; /* v */
    gm_t88rcucg1b7uk gm_l22; /* t */
    gm_b16_8 gm_l23; /* elems */
    uint32_t gm_l24; /* tmp */
    uint64_t gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    uint64_t gm_l27; /* i */
    uint64_t gm_l28; /* at */
    void * gm_l29; /* base */
    gm_b16_8 gm_l30; /* elems */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_b16_8 gm_l36; /* msg */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* k */
    void * gm_l41; /* v */
    gm_b16_8 gm_l42; /* <tmp> */
    uint64_t gm_l43; /* <tmp> */
    uint64_t gm_l44; /* ix */
    gm_b16_8 gm_l45; /* given */
    uint64_t gm_l46; /* <tmp> */
    uint64_t gm_l47; /* <tmp> */
    uint64_t gm_l48; /* k */
    uint64_t gm_l49; /* <tmp> */
    uint64_t gm_l50; /* <tmp> */
    uint64_t gm_l51; /* k */
    void * gm_l52; /* spec */
    void * gm_l53; /* v */
    uint8_t gm_l54; /* wants_float */
    gm_t88rcucg1b7uk gm_l55; /* t */
    gm_b16_8 gm_l56; /* msg */
    GM_STACK_CHECK(&F, 4225u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_t1jll27j7ae34 gm_l57;
    gm_tabb710d9mq2e gm_l58;
    gm_tf5srh4mp4b3i gm_l59;
    void * gm_l60;
    gm_t53at80pfdus3 gm_l61;
    uint8_t gm_l62;
    uint8_t gm_l63;
    gm_th2lqbo77pvsc gm_l64;
    gm_tftb1odc739t5 gm_l65;
    gm_tftb1odc739t5 gm_l66;
    gm_t53at80pfdus3 gm_l67;
    gm_b16_8 gm_l68;
    gm_th2lqbo77pvsc gm_l69;
    gm_tabb710d9mq2e gm_l70;
    gm_tabb710d9mq2e gm_l71;
    gm_tftb1odc739t5 gm_l72;
    gm_th2lqbo77pvsc gm_l73;
    gm_tftb1odc739t5 gm_l74;
    gm_tftb1odc739t5 gm_l75;
    gm_tabb710d9mq2e gm_l76;
    gm_tabb710d9mq2e gm_l77;
    gm_tftb1odc739t5 gm_l78;
    uint8_t gm_l79;
    uint8_t gm_l80;
    gm_th2lqbo77pvsc gm_l81;
    {
        void * t59 = gm_l0;
        void * t60 = GM_LD(void *, ((uint8_t *)t59));
        void * t61 = (void *)(((uint8_t *)t60) + 8);
        gm_l5 = t61;
        gm_b16_8 t62 = gm_l1;
        gm_f_parse_template_rlpl2k8((void *)&gm_l57, (void *)NULL, cx, NULL, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            {
                uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l57) + 0));
                uint8_t t64 = (uint8_t)(t63 == ((uint8_t)0ULL));
                if (!t64) {
                    goto gm_lb2;
                }
                {
                    memmove(((uint8_t *)&F.l6), (((uint8_t *)&gm_l57) + 8), 48);
                }
                {
                    memmove(((uint8_t *)&F.l13), ((uint8_t *)&F.l6), 48);
                }
                goto gm_lb1;
            }
gm_lb2:;
            {
                uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l57) + 0));
                uint8_t t66 = (uint8_t)(t65 == ((uint8_t)1ULL));
                if (!t66) {
                    goto gm_lb3;
                }
                {
                    memmove(((uint8_t *)&gm_l7), (((uint8_t *)&gm_l57) + 8), 32);
                }
                {
                    gm_tl874t2atnlh3 t67 = gm_l2;
                    gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
                    gm_tnpifv20irnph t69;
                    GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t68);
                    gm_f_format_68lf2sm((void *)&gm_l58, (void *)NULL, cx, NULL, t69);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                    if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 8, gm_src6d57grm686e3, 358, 44);
                    gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                    gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                    gm_t4gut2c9fj1kt t73;
                    gm_f_error_s6hn871((void *)&t73, (void *)NULL, cx, NULL, t67, t72);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l8 = t73;
                }
                {
                    {
                        uint8_t t74 = (uint8_t)(((((uint8_t *)&gm_l7) + 16))[8] != 0);
                        if (!t74) {
                            goto gm_lb5;
                        }
                        uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 16));
                        gm_l9 = t75;
                        void * t76 = gm_l0;
                        void * t77 = GM_LD(void *, ((uint8_t *)t76));
                        void * t78 = GM_LD(void *, ((uint8_t *)t77));
                        void * t79 = GM_LD(void *, ((uint8_t *)t78));
                        uint32_t t80 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
                        void * t81 = gm_f_file_o15fqku(t79, t80);
                        void * t82 = GM_LD(void *, (((uint8_t *)t81) + 16));
                        uint64_t t83 = GM_LD(uint64_t, ((((uint8_t *)t81) + 16) + 8));
                        uint32_t t84 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
                        uint64_t t85 = (uint64_t)t84;
                        uint32_t t86 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t87 = (uint64_t)t86;
                        uint64_t t88 = (uintptr_t)t85;
                        uint64_t t89 = (uintptr_t)t87;
                        if (t88 > t89 || t89 > (uintptr_t)t83) gm_trap_slice(t88, t89, t83, gm_src6d57grm686e3, 361, 25);
                        uint64_t t90 = ((uintptr_t)t87) - (uintptr_t)t85;
                        gm_b16_8 t91;
                        GM_ST(void *, (uint8_t *)&t91, (void *)(((uint8_t *)t82 + (uintptr_t)t85 * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(t90));
                        gm_l10 = t91;
                        {
                            {
                                gm_b16_8 t92 = gm_l10;
                                uint64_t t93 = gm_l9;
                                gm_tf5srh4mp4b3i t94 = gm_f_source_offset_of_vlcjdq6(t92, t93);
                                gm_l59 = t94;
                                uint8_t t95 = (uint8_t)((((uint8_t *)&gm_l59))[8] != 0);
                                if (!t95) {
                                    goto gm_lb7;
                                }
                                uint64_t t96 = GM_LD(uint64_t, ((uint8_t *)&gm_l59));
                                gm_l11 = t96;
                                uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
                                uint64_t t98 = gm_l11;
                                uint32_t t99 = (uint32_t)t98;
                                uint32_t r100; bool o101 = __builtin_add_overflow(t97, t99, &r100);
                                if (o101) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 363, 27);
                                uint32_t t102 = r100;
                                gm_l12 = t102;
                                void * t103 = (void *)((uint8_t *)&F.l8);
                                gm_b16_8 t104;
                                GM_ST(void *, (uint8_t *)&t104, (void *)(gm_s4087kcl9bpus));
                                GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(28));
                                uint32_t t105 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
                                uint32_t t106 = gm_l12;
                                uint32_t t107 = gm_l12;
                                uint32_t r108; bool o109 = __builtin_add_overflow(t107, ((uint32_t)1ULL), &r108);
                                if (o109) gm_trap("integer overflow in `+`", gm_src6d57grm686e3, 364, 113);
                                uint32_t t110 = r108;
                                gm_tl874t2atnlh3 t111;
                                GM_ST(uint32_t, ((uint8_t *)&t111 + 0), t105);
                                GM_ST(uint32_t, ((uint8_t *)&t111 + 4), t106);
                                GM_ST(uint32_t, ((uint8_t *)&t111 + 8), t110);
                                gm_b16_8 t112;
                                GM_ST(void *, (uint8_t *)&t112, (void *)(gm_s6of1cedjphs7));
                                GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(2));
                                gm_b0_1 t113 = gm_f_add_fix_0gkcr4c((void *)NULL, cx, NULL, t103, t104, t111, t112);
                                if (gm_unw.kind) {
                                    goto gm_lad;
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
                {
                    void * t114 = gm_l0;
                    void * t115 = GM_LD(void *, ((uint8_t *)t114));
                    gm_b0_1 t116 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t115, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l8)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    memset(&(*(gm_ti6pdj2sunpe4 *)gm_out), 0, sizeof (*(gm_ti6pdj2sunpe4 *)gm_out));
                    goto gm_fdone;
                }
            }
gm_lb3:;
            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 355, 15);
        }
gm_lb1:;
    }
    {
        memmove(((uint8_t *)&F.l13), ((uint8_t *)&F.l13), 48);
        uint64_t t117 = GM_LD(uint64_t, ((((uint8_t *)&F.l13) + 16) + 8));
        gm_l14 = t117;
        uint64_t t118 = GM_LD(uint64_t, ((((uint8_t *)&F.l13) + 32) + 8));
        uint8_t t119 = (t118 > ((uint64_t)0ULL));
        gm_l15 = t119;
        gm_trqfvmjtt4sij t120;
        gm_f_list_ir0h3tc_2((void *)&t120, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l16 = t120;
        gm_trqfvmjtt4sij t121;
        gm_f_list_vp1djv1_2((void *)&t121, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l17 = t121;
        {
            {
                {
                    uint64_t t122 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    uint8_t t123 = ((t122 == ((uint64_t)1ULL)));
                    if (!t123) {
                        goto gm_lb9;
                    }
                    void * t124 = GM_LD(void *, ((uint8_t *)&gm_l3));
                    uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t125)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t125, gm_src6d57grm686e3, 375, 40);
                    uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)t124) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
                    uint8_t t127 = (uint8_t)(t126 == ((uint8_t)2ULL));
                    if (!t127) {
                        goto gm_lb9;
                    }
                    memmove(((uint8_t *)&F.l18), ((((uint8_t *)t124) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
                    void * t128 = gm_l0;
                    void * t129 = (void *)((uint8_t *)&F.l18);
                    gm_tkn42u7na3t81 t130;
                    memset(&t130, 0, sizeof t130);
                    void * t131 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t128, t129, t130);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l19 = t131;
                    void * t132 = gm_l0;
                    void * t133 = (void *)(((uint8_t *)t132) + 96);
                    void * t134 = gm_l0;
                    void * t135 = GM_LD(void *, ((uint8_t *)t134));
                    void * t136 = gm_l19;
                    gm_t88rcucg1b7uk t137 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t136));
                    gm_t88rcucg1b7uk t138 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t133, t135, t137);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l20 = t138;
                    void * t139 = gm_l0;
                    void * t140 = gm_l19;
                    gm_tl874t2atnlh3 t141 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 72));
                    void * t142 = gm_f_elements_tuple_gdm5n6q((void *)NULL, cx, NULL, t139, t140, t141);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l60 = t142;
                    uint8_t t143 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l60)) != 0);
                    if (t143) {
                        void * t144 = GM_LD(void *, ((uint8_t *)&gm_l60));
                        gm_l21 = t144;
                    } else {
                        memset(&(*(gm_ti6pdj2sunpe4 *)gm_out), 0, sizeof (*(gm_ti6pdj2sunpe4 *)gm_out));
                        goto gm_fdone;
                    }
                    void * t145 = gm_l0;
                    void * t146 = (void *)(((uint8_t *)t145) + 96);
                    void * t147 = gm_l0;
                    void * t148 = GM_LD(void *, ((uint8_t *)t147));
                    void * t149 = gm_l21;
                    gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t149));
                    gm_t88rcucg1b7uk t151 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t146, t148, t150);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l22 = t151;
                    {
                        {
                            {
                                void * t152 = gm_l5;
                                gm_t88rcucg1b7uk t153 = gm_l22;
                                gm_f_kind_jcnt4hm((void *)&gm_l61, t152, GM_CONV(gm_csguankko4lmhd, t153));
                            }
                            uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l61) + 0));
                            uint8_t t155 = (uint8_t)(t154 == ((uint8_t)12ULL));
                            if (!t155) {
                                goto gm_lb11;
                            }
                            gm_b16_8 t156 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
                            gm_l23 = t156;
                            uint8_t t157 = gm_l15;
                            if (t157) {
                                {
                                    uint64_t t158 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                    gm_tfeab279c16rr t159 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)&F.l13));
                                    gm_f_numbered_error_07hll20((void *)&gm_l64, (void *)NULL, cx, NULL, &t159, t158);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                {
                                    {
                                        uint8_t t160 = (uint8_t)((((uint8_t *)&gm_l64))[16] != 0);
                                        if (t160) {
                                            goto gm_lb13;
                                        }
                                        gm_l63 = ((uint8_t)1);
                                        goto gm_lb12;
                                    }
gm_lb13:;
                                    {
                                        gm_l63 = ((uint8_t)0);
                                        goto gm_lb12;
                                    }
gm_lb14:;
                                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 381, 63);
                                }
gm_lb12:;
                                uint8_t t161 = gm_l63;
                                gm_l62 = t161;
                            } else {
                                uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                uint64_t t163 = gm_l14;
                                uint8_t t164 = ((t162 == t163));
                                gm_l62 = t164;
                            }
                            uint8_t t165 = gm_l62;
                            if (!t165) {
                                goto gm_lb11;
                            }
                            void * t166 = gm_l0;
                            gm_t88rcucg1b7uk t167 = gm_l22;
                            gm_tl874t2atnlh3 t168 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 72));
                            uint32_t t169 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t166, t167, t168);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l24 = t169;
                            {
                                void * t170 = (void *)((uint8_t *)&F.l16);
                                uint32_t t171 = gm_l24;
                                void * t172 = gm_l21;
                                gm_ttggq7ng4kp3p t173;
                                GM_ST(uint32_t, (uint8_t *)&t173 + 4, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t173 + 0), t171);
                                GM_ST(void *, ((uint8_t *)&t173 + 8), t172);
                                gm_trcmmd12cefb1 t174;
                                GM_ST(uint8_t, (uint8_t *)&t174 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t174 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t174 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t174 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t174 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t174 + 8), t173);
                                gm_tftb1odc739t5 t175 = gm_f_push_d6c76ud_2(t170, &t174);
                                gm_l65 = t175;
                            }
                            uint8_t t176 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                            if (!t176) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 1, gm_src6d57grm686e3, 383, 13);
                            gm_l26 = ((uint64_t)0ULL);
                            uint64_t t177 = gm_l14;
                            gm_l25 = t177;
                            {
                                for (;;) {
                                    uint64_t t178 = gm_l26;
                                    uint64_t t179 = gm_l25;
                                    uint8_t t180 = (t178 < t179);
                                    if (!t180) {
                                        goto gm_lb15;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t181 = gm_l26;
                                                gm_l27 = t181;
                                                uint8_t t182 = gm_l15;
                                                if (t182) {
                                                    void * t183 = GM_LD(void *, (((uint8_t *)&F.l13) + 32));
                                                    uint64_t t184 = GM_LD(uint64_t, ((((uint8_t *)&F.l13) + 32) + 8));
                                                    uint64_t t185 = gm_l27;
                                                    if ((uintptr_t)t185 >= (uintptr_t)(t184)) gm_trap_index((uintptr_t)t185, t184, gm_src6d57grm686e3, 385, 37);
                                                    uint64_t t186 = GM_LD(uint64_t, (((uint8_t *)t183) + (uintptr_t)t185 * 8));
                                                    gm_l28 = t186;
                                                } else {
                                                    uint64_t t187 = gm_l27;
                                                    gm_l28 = t187;
                                                }
                                                {
                                                    gm_t88rcucg1b7uk t188 = gm_l22;
                                                    uint32_t t189 = gm_l24;
                                                    gm_tl874t2atnlh3 t190 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 72));
                                                    gm_to0f5qn75cgg8 t191;
                                                    memset(&t191, 0, sizeof t191);
                                                    GM_ST(uint8_t, ((uint8_t *)&t191 + 0), ((uint8_t)15ULL));
                                                    GM_ST(uint32_t, ((uint8_t *)&t191 + 8), t189);
                                                    void * t192 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t188, &t191, t190);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l29 = t192;
                                                }
                                                {
                                                    void * t193 = (void *)((uint8_t *)&F.l17);
                                                    void * t194 = GM_LD(void *, ((uint8_t *)&gm_l23));
                                                    uint64_t t195 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                                                    uint64_t t196 = gm_l28;
                                                    if ((uintptr_t)t196 >= (uintptr_t)(t195)) gm_trap_index((uintptr_t)t196, t195, gm_src6d57grm686e3, 387, 36);
                                                    gm_t88rcucg1b7uk t197 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t194) + (uintptr_t)t196 * 4));
                                                    void * t198 = gm_l29;
                                                    uint64_t t199 = gm_l28;
                                                    uint32_t t200 = (uint32_t)t199;
                                                    gm_tbl8il92l97ut t201;
                                                    GM_ST(uint32_t, (uint8_t *)&t201 + 12, 0);
                                                    GM_ST(void *, ((uint8_t *)&t201 + 0), t198);
                                                    GM_ST(uint32_t, ((uint8_t *)&t201 + 8), t200);
                                                    gm_tl874t2atnlh3 t202 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 72));
                                                    gm_to0f5qn75cgg8 t203;
                                                    memset(&t203, 0, sizeof t203);
                                                    GM_ST(uint8_t, ((uint8_t *)&t203 + 0), ((uint8_t)19ULL));
                                                    GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t203 + 8), t201);
                                                    void * t204 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t197, &t203, t202);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_tftb1odc739t5 t205 = gm_f_push_d943vgp_2(t193, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t204)));
                                                    gm_l66 = t205;
                                                }
                                                uint8_t t206 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                                                if (!t206) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_src6d57grm686e3, 387, 17);
                                                goto gm_lb18;
                                            }
gm_lb19:;
                                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 384, 13);
                                        }
gm_lb18:;
                                    }
gm_lb17:;
                                    uint64_t t207 = gm_l26;
                                    uint64_t r208; bool o209 = __builtin_add_overflow(t207, ((uint64_t)1ULL), &r208);
                                    uint64_t t210 = r208;
                                    gm_l26 = t210;
                                }
                            }
gm_lb15:;
                            goto gm_lb10;
                        }
gm_lb11:;
                        void * t211 = gm_l5;
                        gm_t88rcucg1b7uk t212 = gm_l22;
                        uint8_t t213 = gm_f_is_error_8k27gd1(t211, GM_CONV(gm_csguankko4lmhd, t212));
                        if (t213) {
                            memset(&(*(gm_ti6pdj2sunpe4 *)gm_out), 0, sizeof (*(gm_ti6pdj2sunpe4 *)gm_out));
                            goto gm_fdone;
                        } else {
                            uint8_t t214 = gm_l15;
                            if (t214) {
                                {
                                    {
                                        {
                                            void * t215 = gm_l5;
                                            gm_t88rcucg1b7uk t216 = gm_l22;
                                            gm_f_kind_jcnt4hm((void *)&gm_l67, t215, GM_CONV(gm_csguankko4lmhd, t216));
                                        }
                                        uint8_t t217 = GM_LD(uint8_t, (((uint8_t *)&gm_l67) + 0));
                                        uint8_t t218 = (uint8_t)(t217 == ((uint8_t)12ULL));
                                        if (!t218) {
                                            goto gm_lb21;
                                        }
                                        gm_b16_8 t219 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                                        gm_l30 = t219;
                                        {
                                            void * t220 = gm_l0;
                                            gm_tl874t2atnlh3 t221 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 72));
                                            uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                                            gm_tfeab279c16rr t223 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)&F.l13));
                                            gm_f_numbered_error_07hll20((void *)&gm_l69, (void *)NULL, cx, NULL, &t223, t222);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            uint8_t t224 = (uint8_t)((((uint8_t *)&gm_l69))[16] != 0);
                                            if (t224) {
                                                gm_b16_8 t225 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l69));
                                                gm_l68 = t225;
                                            } else {
                                                gm_b16_8 t226;
                                                GM_ST(void *, (uint8_t *)&t226, (void *)(gm_sr4qbrk6tlgaa));
                                                GM_ST(uintptr_t, (uint8_t *)&t226 + 8, (uintptr_t)(0));
                                                gm_l68 = t226;
                                            }
                                            gm_b16_8 t227 = gm_l68;
                                            gm_b0_1 t228 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t220, t221, t227);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        }
                                        goto gm_lb20;
                                    }
gm_lb21:;
                                    void * t229 = gm_l0;
                                    gm_tl874t2atnlh3 t230 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 72));
                                    void * t231 = gm_l0;
                                    gm_t88rcucg1b7uk t232 = gm_l20;
                                    gm_b16_8 t233 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t231, t232);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tnpifv20irnph t234;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t234 + 0), t233);
                                    gm_f_format_les73rv((void *)&gm_l70, (void *)NULL, cx, NULL, t234);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t235 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                                    if (!t235) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l70) + 8, gm_src6d57grm686e3, 395, 34);
                                    gm_b16_8 t236 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l70) + 8));
                                    gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&t236));
                                    gm_b0_1 t238 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t229, t230, t237);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
gm_lb20:;
                                memset(&(*(gm_ti6pdj2sunpe4 *)gm_out), 0, sizeof (*(gm_ti6pdj2sunpe4 *)gm_out));
                                goto gm_fdone;
                            } else {
                                {
                                    void * t239 = gm_l0;
                                    gm_tl874t2atnlh3 t240 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 72));
                                    uint64_t t241 = gm_l14;
                                    gm_b16_8 t242 = gm_f_placeholders_text_rtvia6b((void *)NULL, cx, NULL, t241);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint64_t t243 = gm_l14;
                                    void * t244 = gm_l0;
                                    gm_t88rcucg1b7uk t245 = gm_l20;
                                    gm_b16_8 t246 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t244, t245);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tq3m5qgvkpje8 t247;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t247 + 0), t242);
                                    GM_ST(uint64_t, ((uint8_t *)&t247 + 16), t243);
                                    GM_ST(gm_b16_8, ((uint8_t *)&t247 + 24), t246);
                                    gm_f_format_k6j7893((void *)&gm_l71, (void *)NULL, cx, NULL, &t247);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t248 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                                    if (!t248) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l71) + 8, gm_src6d57grm686e3, 399, 30);
                                    gm_b16_8 t249 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l71) + 8));
                                    gm_b16_8 t250 = GM_LD(gm_b16_8, ((uint8_t *)&t249));
                                    gm_b0_1 t251 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t239, t240, t250);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                {
                                    memset(&(*(gm_ti6pdj2sunpe4 *)gm_out), 0, sizeof (*(gm_ti6pdj2sunpe4 *)gm_out));
                                    goto gm_fdone;
                                }
                            }
                        }
                    }
gm_lb10:;
                    goto gm_lb8;
                }
gm_lb9:;
            }
            {
                gm_trqfvmjtt4sij t252;
                gm_f_list_vp1djv1((void *)&t252, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l31 = t252;
                gm_b16_8 t253 = gm_l3;
                gm_l32 = t253;
                gm_l33 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t254 = gm_l33;
                        uint64_t t255 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                        uint8_t t256 = (t254 < t255);
                        if (!t256) {
                            goto gm_lb22;
                        }
                        {
                            void * t257 = GM_LD(void *, ((uint8_t *)&gm_l32));
                            uint64_t t258 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                            uint64_t t259 = gm_l33;
                            {
                                {
                                    memmove(((uint8_t *)&F.l34), (((uint8_t *)t257) + (uintptr_t)t259 * 144), 144);
                                    {
                                        {
                                            uint8_t t260 = GM_LD(uint8_t, (((uint8_t *)&F.l34) + 0));
                                            uint8_t t261 = (uint8_t)(t260 == ((uint8_t)0ULL));
                                            if (!t261) {
                                                goto gm_lb28;
                                            }
                                            {
                                                memmove(((uint8_t *)&F.l35), (((uint8_t *)&F.l34) + 8), 88);
                                            }
                                            {
                                                void * t262 = (void *)((uint8_t *)&F.l31);
                                                gm_tftb1odc739t5 t263 = gm_f_push_d943vgp(t262, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l35)));
                                                gm_l72 = t263;
                                            }
                                            uint8_t t264 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                                            if (!t264) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l72) + 1, gm_src6d57grm686e3, 409, 13);
                                            goto gm_lb27;
                                        }
gm_lb28:;
                                        void * t265 = gm_l0;
                                        gm_tl874t2atnlh3 t266 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l34) + 128));
                                        gm_b16_8 t267;
                                        GM_ST(void *, (uint8_t *)&t267, (void *)(gm_smom6v4b4s28c));
                                        GM_ST(uintptr_t, (uint8_t *)&t267 + 8, (uintptr_t)(51));
                                        gm_b0_1 t268 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t265, t266, t267);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        memset(&(*(gm_ti6pdj2sunpe4 *)gm_out), 0, sizeof (*(gm_ti6pdj2sunpe4 *)gm_out));
                                        goto gm_fdone;
                                    }
gm_lb27:;
                                    goto gm_lb25;
                                }
gm_lb26:;
                                gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 404, 9);
                            }
gm_lb25:;
                        }
gm_lb24:;
                        uint64_t t269 = gm_l33;
                        uint64_t r270; bool o271 = __builtin_add_overflow(t269, ((uint64_t)1ULL), &r270);
                        uint64_t t272 = r270;
                        gm_l33 = t272;
                    }
                }
gm_lb22:;
                uint8_t t273 = gm_l15;
                if (t273) {
                    {
                        {
                            {
                                uint64_t t274 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 8));
                                gm_tfeab279c16rr t275 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)&F.l13));
                                gm_f_numbered_error_07hll20((void *)&gm_l73, (void *)NULL, cx, NULL, &t275, t274);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t276 = (uint8_t)((((uint8_t *)&gm_l73))[16] != 0);
                            if (!t276) {
                                goto gm_lb30;
                            }
                            gm_b16_8 t277 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l73));
                            gm_l36 = t277;
                            void * t278 = gm_l0;
                            gm_tl874t2atnlh3 t279 = gm_l4;
                            gm_b16_8 t280 = gm_l36;
                            gm_b0_1 t281 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t278, t279, t280);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            {
                                memset(&(*(gm_ti6pdj2sunpe4 *)gm_out), 0, sizeof (*(gm_ti6pdj2sunpe4 *)gm_out));
                                goto gm_fdone;
                            }
                        }
gm_lb30:;
                    }
gm_lb29:;
                    gm_trqfvmjtt4sij t282;
                    gm_f_list_vp1djv1_2((void *)&t282, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l37 = t282;
                    gm_l39 = ((uint64_t)0ULL);
                    uint64_t t283 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 8));
                    gm_l38 = t283;
                    {
                        for (;;) {
                            uint64_t t284 = gm_l39;
                            uint64_t t285 = gm_l38;
                            uint8_t t286 = (t284 < t285);
                            if (!t286) {
                                goto gm_lb31;
                            }
                            {
                                {
                                    {
                                        uint64_t t287 = gm_l39;
                                        gm_l40 = t287;
                                        void * t288 = gm_l0;
                                        void * t289 = GM_LD(void *, ((uint8_t *)&F.l31));
                                        uint64_t t290 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 8));
                                        uint64_t t291 = gm_l40;
                                        if ((uintptr_t)t291 >= (uintptr_t)(t290)) gm_trap_index((uintptr_t)t291, t290, gm_src6d57grm686e3, 420, 31);
                                        void * t292 = (void *)(((uint8_t *)t289) + (uintptr_t)t291 * 88);
                                        gm_tkn42u7na3t81 t293;
                                        memset(&t293, 0, sizeof t293);
                                        void * t294 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t288, t292, t293);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l41 = t294;
                                        void * t295 = (void *)((uint8_t *)&F.l37);
                                        void * t296 = gm_l0;
                                        void * t297 = gm_l41;
                                        void * t298 = (void *)((uint8_t *)&F.l16);
                                        void * t299 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t296, t297, t298);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t300 = gm_f_push_d943vgp_2(t295, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t299)));
                                        gm_l74 = t300;
                                        uint8_t t301 = (uint8_t)!((((uint8_t *)&gm_l74))[0] != 0);
                                        if (!t301) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l74) + 1, gm_src6d57grm686e3, 421, 17);
                                        goto gm_lb34;
                                    }
gm_lb35:;
                                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 419, 13);
                                }
gm_lb34:;
                            }
gm_lb33:;
                            uint64_t t302 = gm_l39;
                            uint64_t r303; bool o304 = __builtin_add_overflow(t302, ((uint64_t)1ULL), &r303);
                            uint64_t t305 = r303;
                            gm_l39 = t305;
                        }
                    }
gm_lb31:;
                    gm_b16_8 t306 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 32));
                    gm_l42 = t306;
                    gm_l43 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t307 = gm_l43;
                            uint64_t t308 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                            uint8_t t309 = (t307 < t308);
                            if (!t309) {
                                goto gm_lb36;
                            }
                            {
                                void * t310 = GM_LD(void *, ((uint8_t *)&gm_l42));
                                uint64_t t311 = GM_LD(uint64_t, (((uint8_t *)&gm_l42) + 8));
                                uint64_t t312 = gm_l43;
                                {
                                    {
                                        uint64_t t313 = GM_LD(uint64_t, (((uint8_t *)t310) + (uintptr_t)t312 * 8));
                                        gm_l44 = t313;
                                        void * t314 = (void *)((uint8_t *)&F.l17);
                                        void * t315 = GM_LD(void *, ((uint8_t *)&F.l37));
                                        uint64_t t316 = GM_LD(uint64_t, (((uint8_t *)&F.l37) + 8));
                                        uint64_t t317 = gm_l44;
                                        if ((uintptr_t)t317 >= (uintptr_t)(t316)) gm_trap_index((uintptr_t)t317, t316, gm_src6d57grm686e3, 423, 52);
                                        gm_tftb1odc739t5 t318 = gm_f_push_d943vgp_2(t314, &(*(gm_tsf6i2agfnpva *)(((uint8_t *)t315) + (uintptr_t)t317 * 88)));
                                        gm_l75 = t318;
                                        uint8_t t319 = (uint8_t)!((((uint8_t *)&gm_l75))[0] != 0);
                                        if (!t319) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l75) + 1, gm_src6d57grm686e3, 423, 40);
                                        goto gm_lb39;
                                    }
gm_lb40:;
                                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 423, 13);
                                }
gm_lb39:;
                            }
gm_lb38:;
                            uint64_t t320 = gm_l43;
                            uint64_t r321; bool o322 = __builtin_add_overflow(t320, ((uint64_t)1ULL), &r321);
                            uint64_t t323 = r321;
                            gm_l43 = t323;
                        }
                    }
gm_lb36:;
                } else {
                    uint64_t t324 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 8));
                    uint64_t t325 = gm_l14;
                    uint8_t t326 = (!(t324 == t325));
                    if (t326) {
                        uint64_t t327 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 8));
                        uint8_t t328 = ((t327 == ((uint64_t)1ULL)));
                        if (t328) {
                            gm_b16_8 t329;
                            GM_ST(void *, (uint8_t *)&t329, (void *)(gm_scne9acm49p4h));
                            GM_ST(uintptr_t, (uint8_t *)&t329 + 8, (uintptr_t)(10));
                            gm_l45 = t329;
                        } else {
                            uint64_t t330 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 8));
                            gm_t4ce0jd3tdro4 t331;
                            GM_ST(uint64_t, ((uint8_t *)&t331 + 0), t330);
                            gm_f_format_td6patj((void *)&gm_l76, (void *)NULL, cx, NULL, t331);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t332 = (uint8_t)!((((uint8_t *)&gm_l76))[0] != 0);
                            if (!t332) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l76) + 8, gm_src6d57grm686e3, 425, 69);
                            gm_b16_8 t333 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l76) + 8));
                            gm_b16_8 t334 = GM_LD(gm_b16_8, ((uint8_t *)&t333));
                            gm_l45 = t334;
                        }
                        {
                            void * t335 = gm_l0;
                            gm_tl874t2atnlh3 t336 = gm_l4;
                            uint64_t t337 = gm_l14;
                            gm_b16_8 t338 = gm_f_placeholders_text_rtvia6b((void *)NULL, cx, NULL, t337);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_b16_8 t339 = gm_l45;
                            gm_t6j6smj7qoctu t340;
                            GM_ST(gm_b16_8, ((uint8_t *)&t340 + 0), t338);
                            GM_ST(gm_b16_8, ((uint8_t *)&t340 + 16), t339);
                            gm_f_format_2h4f9a5((void *)&gm_l77, (void *)NULL, cx, NULL, &t340);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t341 = (uint8_t)!((((uint8_t *)&gm_l77))[0] != 0);
                            if (!t341) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l77) + 8, gm_src6d57grm686e3, 426, 28);
                            gm_b16_8 t342 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l77) + 8));
                            gm_b16_8 t343 = GM_LD(gm_b16_8, ((uint8_t *)&t342));
                            gm_b0_1 t344 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t335, t336, t343);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            memset(&(*(gm_ti6pdj2sunpe4 *)gm_out), 0, sizeof (*(gm_ti6pdj2sunpe4 *)gm_out));
                            goto gm_fdone;
                        }
                    }
                }
            }
            {
                uint8_t t345 = gm_l15;
                uint8_t t346 = (uint8_t)!t345;
                if (t346) {
                    gm_l47 = ((uint64_t)0ULL);
                    uint64_t t347 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 8));
                    gm_l46 = t347;
                    {
                        for (;;) {
                            uint64_t t348 = gm_l47;
                            uint64_t t349 = gm_l46;
                            uint8_t t350 = (t348 < t349);
                            if (!t350) {
                                goto gm_lb41;
                            }
                            {
                                {
                                    {
                                        uint64_t t351 = gm_l47;
                                        gm_l48 = t351;
                                        void * t352 = (void *)((uint8_t *)&F.l17);
                                        void * t353 = gm_l0;
                                        void * t354 = GM_LD(void *, ((uint8_t *)&F.l31));
                                        uint64_t t355 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 8));
                                        uint64_t t356 = gm_l48;
                                        if ((uintptr_t)t356 >= (uintptr_t)(t355)) gm_trap_index((uintptr_t)t356, t355, gm_src6d57grm686e3, 430, 63);
                                        void * t357 = (void *)(((uint8_t *)t354) + (uintptr_t)t356 * 88);
                                        gm_tkn42u7na3t81 t358;
                                        memset(&t358, 0, sizeof t358);
                                        void * t359 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t353, t357, t358);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t360 = gm_f_push_d943vgp_2(t352, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t359)));
                                        gm_l78 = t360;
                                        uint8_t t361 = (uint8_t)!((((uint8_t *)&gm_l78))[0] != 0);
                                        if (!t361) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l78) + 1, gm_src6d57grm686e3, 430, 42);
                                        goto gm_lb44;
                                    }
gm_lb45:;
                                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 430, 13);
                                }
gm_lb44:;
                            }
gm_lb43:;
                            uint64_t t362 = gm_l47;
                            uint64_t r363; bool o364 = __builtin_add_overflow(t362, ((uint64_t)1ULL), &r363);
                            uint64_t t365 = r363;
                            gm_l47 = t365;
                        }
                    }
gm_lb41:;
                }
            }
        }
gm_lb8:;
    }
    {
        gm_l50 = ((uint64_t)0ULL);
        uint64_t t366 = gm_l14;
        gm_l49 = t366;
        {
            for (;;) {
                uint64_t t367 = gm_l50;
                uint64_t t368 = gm_l49;
                uint8_t t369 = (t367 < t368);
                if (!t369) {
                    goto gm_lb46;
                }
                {
                    {
                        {
                            uint64_t t370 = gm_l50;
                            gm_l51 = t370;
                            void * t371 = GM_LD(void *, (((uint8_t *)&F.l13) + 16));
                            uint64_t t372 = GM_LD(uint64_t, ((((uint8_t *)&F.l13) + 16) + 8));
                            uint64_t t373 = gm_l51;
                            void * t374 = (void *)(((uint8_t *)t371) + (uintptr_t)t373 * 24);
                            gm_l52 = t374;
                            void * t375 = gm_l52;
                            uint8_t t376 = gm_f_is_default_spec_cdd8jng(t375);
                            if (t376) {
                                goto gm_lb48;
                            }
                            void * t377 = GM_LD(void *, ((uint8_t *)&F.l17));
                            uint64_t t378 = GM_LD(uint64_t, (((uint8_t *)&F.l17) + 8));
                            uint64_t t379 = gm_l51;
                            if ((uintptr_t)t379 >= (uintptr_t)(t378)) gm_trap_index((uintptr_t)t379, t378, gm_src6d57grm686e3, 436, 15);
                            void * t380 = (void *)(((uint8_t *)t377) + (uintptr_t)t379 * 88);
                            gm_l53 = t380;
                            void * t381 = gm_l52;
                            gm_t9qkh0rsooc8d t382 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t381) + 20));
                            uint8_t t383 = GM_LD(uint8_t, (((uint8_t *)&t382) + 0));
                            uint8_t t384 = (uint8_t)((t383 == ((uint8_t)5ULL)));
                            gm_t9qkh0rsooc8d t385;
                            GM_ST(uint8_t, ((uint8_t *)&t385 + 0), ((uint8_t)5ULL));
                            if (t384) {
                                gm_l79 = ((uint8_t)1);
                            } else {
                                void * t386 = gm_l52;
                                {
                                    {
                                        uint8_t t387 = (uint8_t)(((((uint8_t *)t386) + 12))[4] != 0);
                                        if (t387) {
                                            goto gm_lb52;
                                        }
                                        gm_l80 = ((uint8_t)0);
                                        goto gm_lb51;
                                    }
gm_lb52:;
                                    {
                                        gm_l80 = ((uint8_t)1);
                                        goto gm_lb51;
                                    }
gm_lb53:;
                                    gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 438, 45);
                                }
gm_lb51:;
                                uint8_t t388 = gm_l80;
                                gm_l79 = t388;
                            }
                            uint8_t t389 = gm_l79;
                            gm_l54 = t389;
                            {
                                {
                                    uint8_t t390 = gm_l54;
                                    if (!t390) {
                                        goto gm_lb55;
                                    }
                                    void * t391 = gm_l0;
                                    void * t392 = (void *)(((uint8_t *)t391) + 96);
                                    void * t393 = gm_l5;
                                    void * t394 = gm_l53;
                                    gm_t88rcucg1b7uk t395 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t394));
                                    gm_taari1s0m51a7 t396 = gm_f_var_kind_1sl5sig((void *)NULL, cx, NULL, t392, t393, t395);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_t9qkh0rsooc8d t397;
                                    GM_ST(uint8_t, ((uint8_t *)&t397 + 0), ((uint8_t)0ULL));
                                    gm_taari1s0m51a7 t398;
                                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t398), t397);
                                    ((uint8_t *)&t398)[1] = 1;
                                    uint8_t t399 = (gm_eq5307j5k9ms(((uint8_t *)&t396), ((uint8_t *)&t398)));
                                    if (!t399) {
                                        goto gm_lb55;
                                    }
                                    void * t400 = gm_l0;
                                    void * t401 = (void *)(((uint8_t *)t400) + 96);
                                    void * t402 = gm_l5;
                                    void * t403 = gm_l53;
                                    gm_t88rcucg1b7uk t404 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t403));
                                    void * t405 = gm_l5;
                                    gm_t53at80pfdus3 t406;
                                    memset(&t406, 0, sizeof t406);
                                    GM_ST(uint8_t, ((uint8_t *)&t406 + 0), ((uint8_t)3ULL));
                                    GM_ST(uint16_t, ((uint8_t *)&t406 + 8), ((uint16_t)64ULL));
                                    gm_t88rcucg1b7uk t407 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t405, &t406);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_t9qkh0rsooc8d t408 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t401, t402, t404, t407);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb54;
                                }
gm_lb55:;
                            }
gm_lb54:;
                            void * t409 = gm_l0;
                            void * t410 = gm_l53;
                            gm_t88rcucg1b7uk t411 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t410));
                            void * t412 = gm_l53;
                            gm_tl874t2atnlh3 t413 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t412) + 72));
                            gm_t88rcucg1b7uk t414 = gm_f_settle_h9ikvs5((void *)NULL, cx, NULL, t409, t411, t413);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l55 = t414;
                            void * t415 = gm_l5;
                            gm_t88rcucg1b7uk t416 = gm_l55;
                            uint8_t t417 = gm_f_has_param_u9t0efs(t415, GM_CONV(gm_csguankko4lmhd, t416));
                            if (t417) {
                                goto gm_lb48;
                            }
                            {
                                {
                                    void * t418 = gm_l0;
                                    void * t419 = GM_LD(void *, ((uint8_t *)t418));
                                    void * t420 = gm_l52;
                                    gm_t88rcucg1b7uk t421 = gm_l55;
                                    gm_f_check_spec_4vp7jhg((void *)&gm_l81, (void *)NULL, cx, NULL, t419, t420, t421);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t422 = (uint8_t)((((uint8_t *)&gm_l81))[16] != 0);
                                    if (!t422) {
                                        goto gm_lb57;
                                    }
                                    gm_b16_8 t423 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l81));
                                    gm_l56 = t423;
                                    void * t424 = gm_l0;
                                    void * t425 = gm_l53;
                                    gm_tl874t2atnlh3 t426 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t425) + 72));
                                    gm_b16_8 t427 = gm_l56;
                                    gm_b0_1 t428 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t424, t426, t427);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb56;
                                }
gm_lb57:;
                            }
gm_lb56:;
                            goto gm_lb49;
                        }
gm_lb50:;
                        gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 433, 5);
                    }
gm_lb49:;
                }
gm_lb48:;
                uint64_t t429 = gm_l50;
                uint64_t r430; bool o431 = __builtin_add_overflow(t429, ((uint64_t)1ULL), &r430);
                uint64_t t432 = r430;
                gm_l50 = t432;
            }
        }
gm_lb46:;
    }
    {
        gm_b16_8 t433 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
        gm_b16_8 t434 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 16));
        gm_b16_8 t435 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
        gm_b16_8 t436 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
        gm_t0uuhaljsf410 t437;
        GM_ST(gm_b16_8, ((uint8_t *)&t437 + 0), t433);
        GM_ST(gm_b16_8, ((uint8_t *)&t437 + 16), t434);
        GM_ST(gm_b16_8, ((uint8_t *)&t437 + 32), t435);
        GM_ST(gm_b16_8, ((uint8_t *)&t437 + 48), t436);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out) + 65, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out) + 66, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out) + 68, 0);
        GM_ST(gm_t0uuhaljsf410, ((uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out)), t437);
        ((uint8_t *)&(*(gm_ti6pdj2sunpe4 *)gm_out))[64] = 1;
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
__attribute__((aligned(4))) void * gm_f_write_deferred_kbicf34(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, void * a2, gm_b16_8 a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_write_deferred_kbicf34 F;
    void * gm_l0; /* cx */
    void * gm_l2; /* t */
    gm_b16_8 gm_l3; /* rest */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    void * gm_l6; /* args */
    void * gm_l8; /* v */
    void * gm_l9; /* a */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_t88rcucg1b7uk gm_l19; /* ty */
    void * gm_l20; /* a */
    gm_t88rcucg1b7uk gm_l21; /* ty */
    uint32_t gm_l22; /* tmp */
    GM_STACK_CHECK(&F, 1421u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    void * gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tq7s98ihonkju gm_l26;
    void * t18 = gm_l0;
    void * t19 = GM_LD(void *, ((uint8_t *)t18));
    void * t20 = (void *)(((uint8_t *)t19) + 8);
    gm_l5 = t20;
    gm_l6 = ((void *)0);
    {
        {
            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t22 = ((t21 == ((uint64_t)1ULL)));
            if (!t22) {
                goto gm_lb2;
            }
            void * t23 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t24)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t24, gm_src6d57grm686e3, 455, 40);
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t23) + (uintptr_t)((uint64_t)0ULL) * 144) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)2ULL));
            if (!t26) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l7), ((((uint8_t *)t23) + (uintptr_t)((uint64_t)0ULL) * 144) + 8), 88);
            void * t27 = gm_l0;
            void * t28 = (void *)((uint8_t *)&F.l7);
            gm_tkn42u7na3t81 t29;
            memset(&t29, 0, sizeof t29);
            void * t30 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t27, t28, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t30;
            void * t31 = gm_l0;
            void * t32 = gm_l8;
            gm_tl874t2atnlh3 t33 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l7) + 72));
            void * t34 = gm_f_elements_tuple_gdm5n6q((void *)NULL, cx, NULL, t31, t32, t33);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t34;
            uint8_t t35 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l23)) != 0);
            if (t35) {
                void * t36 = GM_LD(void *, ((uint8_t *)&gm_l23));
                gm_l9 = t36;
            } else {
                void * t37 = gm_l0;
                void * t38 = GM_LD(void *, ((uint8_t *)t37));
                gm_tl874t2atnlh3 t39 = gm_l4;
                void * t40 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t38, t39);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t40;
                goto gm_fret;
            }
            void * t41 = gm_l9;
            gm_l6 = t41;
            goto gm_lb1;
        }
gm_lb2:;
        uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t43 = (t42 > ((uint64_t)0ULL));
        if (t43) {
            {
                gm_trqfvmjtt4sij t44;
                gm_f_list_vp1djv1_2((void *)&t44, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l10 = t44;
            }
            gm_b16_8 t45 = gm_l3;
            gm_l11 = t45;
            gm_l12 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t46 = gm_l12;
                    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint8_t t48 = (t46 < t47);
                    if (!t48) {
                        goto gm_lb3;
                    }
                    {
                        void * t49 = GM_LD(void *, ((uint8_t *)&gm_l11));
                        uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                        uint64_t t51 = gm_l12;
                        {
                            {
                                memmove(((uint8_t *)&F.l13), (((uint8_t *)t49) + (uintptr_t)t51 * 144), 144);
                                {
                                    {
                                        uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
                                        uint8_t t53 = (uint8_t)(t52 == ((uint8_t)0ULL));
                                        if (!t53) {
                                            goto gm_lb9;
                                        }
                                        {
                                            memmove(((uint8_t *)&F.l14), (((uint8_t *)&F.l13) + 8), 88);
                                        }
                                        {
                                            void * t54 = (void *)((uint8_t *)&F.l10);
                                            void * t55 = gm_l0;
                                            void * t56 = (void *)((uint8_t *)&F.l14);
                                            gm_tkn42u7na3t81 t57;
                                            memset(&t57, 0, sizeof t57);
                                            void * t58 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t55, t56, t57);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_tftb1odc739t5 t59 = gm_f_push_d943vgp_2(t54, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t58)));
                                            gm_l24 = t59;
                                        }
                                        uint8_t t60 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                        if (!t60) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src6d57grm686e3, 466, 13);
                                        void * t61 = gm_l0;
                                        void * t62 = GM_LD(void *, ((uint8_t *)&F.l10));
                                        uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                        uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                        uint64_t r65; bool o66 = __builtin_sub_overflow(t64, ((uint64_t)1ULL), &r65);
                                        if (o66) gm_trap("integer overflow in `-`", gm_src6d57grm686e3, 467, 40);
                                        uint64_t t67 = r65;
                                        if ((uintptr_t)t67 >= (uintptr_t)(t63)) gm_trap_index((uintptr_t)t67, t63, gm_src6d57grm686e3, 467, 28);
                                        void * t68 = (void *)(((uint8_t *)t62) + (uintptr_t)t67 * 88);
                                        gm_b0_1 t69 = gm_f_fn_written_qtvia9n((void *)NULL, cx, NULL, t61, t68);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    void * t70 = gm_l0;
                                    gm_tl874t2atnlh3 t71 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l13) + 128));
                                    gm_b16_8 t72;
                                    GM_ST(void *, (uint8_t *)&t72, (void *)(gm_smom6v4b4s28c));
                                    GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(51));
                                    gm_b0_1 t73 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t70, t71, t72);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    void * t74 = gm_l0;
                                    void * t75 = GM_LD(void *, ((uint8_t *)t74));
                                    gm_tl874t2atnlh3 t76 = gm_l4;
                                    void * t77 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t75, t76);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = t77;
                                    goto gm_fret;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 461, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t78 = gm_l12;
                    uint64_t r79; bool o80 = __builtin_add_overflow(t78, ((uint64_t)1ULL), &r79);
                    uint64_t t81 = r79;
                    gm_l12 = t81;
                }
            }
gm_lb3:;
            {
                gm_trqfvmjtt4sij t82;
                gm_f_list_nof7vfj((void *)&t82, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l15 = t82;
            }
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
            gm_l16 = t83;
            gm_l17 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t84 = gm_l17;
                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint8_t t86 = (t84 < t85);
                    if (!t86) {
                        goto gm_lb10;
                    }
                    {
                        void * t87 = GM_LD(void *, ((uint8_t *)&gm_l16));
                        uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                        uint64_t t89 = gm_l17;
                        {
                            {
                                memmove(((uint8_t *)&F.l18), (((uint8_t *)t87) + (uintptr_t)t89 * 88), 88);
                                void * t90 = (void *)((uint8_t *)&F.l15);
                                void * t91 = gm_l0;
                                void * t92 = (void *)(((uint8_t *)t91) + 96);
                                void * t93 = gm_l0;
                                void * t94 = GM_LD(void *, ((uint8_t *)t93));
                                gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&F.l18));
                                gm_t88rcucg1b7uk t96 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t92, t94, t95);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t97 = gm_f_push_k3l9nic(t90, GM_CONV(gm_csguankko4lmhd, t96));
                                gm_l25 = t97;
                                uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_src6d57grm686e3, 470, 32);
                                goto gm_lb13;
                            }
gm_lb14:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 470, 9);
                        }
gm_lb13:;
                    }
gm_lb12:;
                    uint64_t t99 = gm_l17;
                    uint64_t r100; bool o101 = __builtin_add_overflow(t99, ((uint64_t)1ULL), &r100);
                    uint64_t t102 = r100;
                    gm_l17 = t102;
                }
            }
gm_lb10:;
            {
                void * t103 = gm_l5;
                gm_b16_8 t104 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
                gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
                gm_b16_8 t106 = gm_f_empty_slice_5nn7ph0();
                gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                gm_t6j6smj7qoctu t108;
                GM_ST(gm_b16_8, ((uint8_t *)&t108 + 0), t105);
                GM_ST(gm_b16_8, ((uint8_t *)&t108 + 16), t107);
                gm_t53at80pfdus3 t109;
                GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t109 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t109 + 48, 0);
                GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)12ULL));
                GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t109 + 8), t108);
                gm_t88rcucg1b7uk t110 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t103, &t109);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t110;
            }
            {
                gm_t88rcucg1b7uk t111 = gm_l19;
                gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                gm_tl874t2atnlh3 t113 = gm_l4;
                gm_to0f5qn75cgg8 t114;
                memset(&t114, 0, sizeof t114);
                GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)35ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t114 + 8), t112);
                void * t115 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t111, &t114, t113);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l6 = t115;
            }
        }
    }
gm_lb1:;
    {
        {
            uint8_t t116 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l6)) != 0);
            if (!t116) {
                goto gm_lb16;
            }
            void * t117 = GM_LD(void *, ((uint8_t *)&gm_l6));
            gm_l20 = t117;
            void * t118 = gm_l0;
            void * t119 = (void *)(((uint8_t *)t118) + 96);
            void * t120 = gm_l0;
            void * t121 = GM_LD(void *, ((uint8_t *)t120));
            void * t122 = gm_l20;
            gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t122));
            gm_t88rcucg1b7uk t124 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t119, t121, t123);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l21 = t124;
            goto gm_lb15;
        }
gm_lb16:;
        void * t125 = gm_l5;
        gm_t88rcucg1b7uk t126 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t125);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l21 = t126;
    }
gm_lb15:;
    void * t127 = gm_l0;
    gm_t88rcucg1b7uk t128 = gm_l21;
    gm_tl874t2atnlh3 t129 = gm_l4;
    uint32_t t130 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t127, t128, t129);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l22 = t130;
    {
        void * t131 = gm_l5;
        gm_t88rcucg1b7uk t132 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t131);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t133 = gm_l2;
        void * t134 = gm_l6;
        uint32_t t135 = gm_l22;
        gm_th1g69d7vpipg t136 = GM_LD(gm_th1g69d7vpipg, ((uint8_t *)a1));
        gm_t9i38ih07bojq t137;
        GM_ST(uint32_t, (uint8_t *)&t137 + 60, 0);
        GM_ST(gm_th1g69d7vpipg, ((uint8_t *)&t137 + 0), t136);
        GM_ST(void *, ((uint8_t *)&t137 + 40), t133);
        GM_ST(void *, ((uint8_t *)&t137 + 48), t134);
        GM_ST(uint32_t, ((uint8_t *)&t137 + 56), t135);
        gm_tq7s98ihonkju t138 = gm_f_new_ges19gd((void *)NULL, cx, NULL, &t137, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kr6fmufdvee)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l26 = t138;
        uint8_t t139 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
        if (!t139) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 8, gm_src6d57grm686e3, 476, 46);
        void * t140 = GM_LD(void *, (((uint8_t *)&gm_l26) + 8));
        gm_tl874t2atnlh3 t141 = gm_l4;
        gm_to0f5qn75cgg8 t142;
        memset(&t142, 0, sizeof t142);
        GM_ST(uint8_t, ((uint8_t *)&t142 + 0), ((uint8_t)56ULL));
        GM_ST(void *, ((uint8_t *)&t142 + 8), t140);
        void * t143 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t132, &t142, t141);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t143;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_table_5n1kjkn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_write_table_5n1kjkn F;
    void * gm_l0; /* s */
    void * gm_l1; /* e */
    void * gm_l2; /* w */
    void * gm_l3; /* a */
    gm_b16_8 gm_l4; /* es */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    void * gm_l10; /* base */
    void * gm_l11; /* write */
    GM_STACK_CHECK(&F, 1690u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tq7s98ihonkju gm_l12;
    gm_t53at80pfdus3 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tq7s98ihonkju gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tq7s98ihonkju gm_l18;
    void * t9 = gm_l2;
    uint8_t t10 = (uint8_t)(GM_LD(void *, (((uint8_t *)t9) + 48)) != 0);
    if (t10) {
        void * t11 = GM_LD(void *, (((uint8_t *)t9) + 48));
        gm_l3 = t11;
    } else {
        {
            void * t12 = gm_l1;
            void * t13 = gm_l2;
            gm_th1g69d7vpipg t14 = GM_LD(gm_th1g69d7vpipg, ((uint8_t *)t13));
            gm_b16_8 t15 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            gm_b16_8 t17 = gm_f_empty_slice_moftl2f();
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            gm_b16_8 t19 = gm_f_empty_slice_g3tddrb_2();
            void * t20 = gm_l2;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 40));
            gm_t150deeftog49 t22;
            GM_ST(gm_th1g69d7vpipg, ((uint8_t *)&t22 + 0), t14);
            GM_ST(gm_b16_8, ((uint8_t *)&t22 + 40), t16);
            GM_ST(gm_b16_8, ((uint8_t *)&t22 + 56), t18);
            GM_ST(gm_b16_8, ((uint8_t *)&t22 + 72), t19);
            GM_ST(void *, ((uint8_t *)&t22 + 88), t21);
            gm_tq7s98ihonkju t23 = gm_f_new_2pnaa87((void *)NULL, cx, NULL, &t22, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kg028u69smr)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t23;
            uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
            if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src6d57grm686e3, 594, 25);
            void * t25 = GM_LD(void *, (((uint8_t *)&gm_l12) + 8));
            gm_to0f5qn75cgg8 t26;
            memset(&t26, 0, sizeof t26);
            GM_ST(uint8_t, ((uint8_t *)&t26 + 0), ((uint8_t)55ULL));
            GM_ST(void *, ((uint8_t *)&t26 + 8), t25);
            GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t12) + 8), t26);
        }
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            {
                void * t27 = gm_l0;
                void * t28 = (void *)(((uint8_t *)t27) + 8);
                void * t29 = gm_l3;
                gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t29));
                gm_f_kind_jcnt4hm((void *)&gm_l13, t28, GM_CONV(gm_csguankko4lmhd, t30));
            }
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)12ULL));
            if (!t32) {
                goto gm_lb2;
            }
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
            gm_l4 = t33;
            {
                gm_trqfvmjtt4sij t34;
                gm_f_list_ir0h3tc_2((void *)&t34, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l5 = t34;
            }
            {
                void * t35 = (void *)((uint8_t *)&F.l5);
                void * t36 = gm_l2;
                uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)t36) + 56));
                void * t38 = gm_l3;
                gm_ttggq7ng4kp3p t39;
                GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t39 + 0), t37);
                GM_ST(void *, ((uint8_t *)&t39 + 8), t38);
                gm_trcmmd12cefb1 t40;
                GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t40 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t40 + 8), t39);
                gm_tftb1odc739t5 t41 = gm_f_push_d6c76ud_2(t35, &t40);
                gm_l14 = t41;
            }
            uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_src6d57grm686e3, 600, 5);
            {
                gm_trqfvmjtt4sij t43;
                gm_f_list_vp1djv1_2((void *)&t43, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l6 = t43;
            }
            gm_l8 = ((uint64_t)0ULL);
            uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            gm_l7 = t44;
            {
                for (;;) {
                    uint64_t t45 = gm_l8;
                    uint64_t t46 = gm_l7;
                    uint8_t t47 = (t45 < t46);
                    if (!t47) {
                        goto gm_lb3;
                    }
                    {
                        {
                            {
                                uint64_t t48 = gm_l8;
                                gm_l9 = t48;
                                {
                                    void * t49 = gm_l3;
                                    gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t49));
                                    void * t51 = gm_l2;
                                    uint32_t t52 = GM_LD(uint32_t, (((uint8_t *)t51) + 56));
                                    void * t53 = gm_l1;
                                    gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t53) + 72));
                                    gm_to0f5qn75cgg8 t55;
                                    memset(&t55, 0, sizeof t55);
                                    GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)15ULL));
                                    GM_ST(uint32_t, ((uint8_t *)&t55 + 8), t52);
                                    void * t56 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t50, &t55, t54);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l10 = t56;
                                }
                                {
                                    void * t57 = (void *)((uint8_t *)&F.l6);
                                    void * t58 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                    uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                    uint64_t t60 = gm_l9;
                                    gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t58) + (uintptr_t)t60 * 4));
                                    void * t62 = gm_l10;
                                    uint64_t t63 = gm_l9;
                                    uint32_t t64 = (uint32_t)t63;
                                    gm_tbl8il92l97ut t65;
                                    GM_ST(uint32_t, (uint8_t *)&t65 + 12, 0);
                                    GM_ST(void *, ((uint8_t *)&t65 + 0), t62);
                                    GM_ST(uint32_t, ((uint8_t *)&t65 + 8), t64);
                                    void * t66 = gm_l1;
                                    gm_tl874t2atnlh3 t67 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t66) + 72));
                                    gm_to0f5qn75cgg8 t68;
                                    memset(&t68, 0, sizeof t68);
                                    GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)19ULL));
                                    GM_ST(gm_tbl8il92l97ut, ((uint8_t *)&t68 + 8), t65);
                                    void * t69 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t61, &t68, t67);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tftb1odc739t5 t70 = gm_f_push_d943vgp_2(t57, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t69)));
                                    gm_l15 = t70;
                                }
                                uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_src6d57grm686e3, 604, 9);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src6d57grm686e3, 602, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t72 = gm_l8;
                    uint64_t r73; bool o74 = __builtin_add_overflow(t72, ((uint64_t)1ULL), &r73);
                    uint64_t t75 = r73;
                    gm_l8 = t75;
                }
            }
gm_lb3:;
            {
                void * t76 = gm_l0;
                void * t77 = (void *)(((uint8_t *)t76) + 8);
                gm_t88rcucg1b7uk t78 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t77);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t79 = gm_l2;
                gm_th1g69d7vpipg t80 = GM_LD(gm_th1g69d7vpipg, ((uint8_t *)t79));
                gm_b16_8 t81 = gm_f_empty_slice_5nn7ph0();
                gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
                gm_b16_8 t83 = gm_f_empty_slice_moftl2f();
                gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
                gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                void * t86 = gm_l2;
                void * t87 = GM_LD(void *, (((uint8_t *)t86) + 40));
                gm_t150deeftog49 t88;
                GM_ST(gm_th1g69d7vpipg, ((uint8_t *)&t88 + 0), t80);
                GM_ST(gm_b16_8, ((uint8_t *)&t88 + 40), t82);
                GM_ST(gm_b16_8, ((uint8_t *)&t88 + 56), t84);
                GM_ST(gm_b16_8, ((uint8_t *)&t88 + 72), t85);
                GM_ST(void *, ((uint8_t *)&t88 + 88), t87);
                gm_tq7s98ihonkju t89 = gm_f_new_2pnaa87((void *)NULL, cx, NULL, &t88, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_khvqu09sagr)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t89;
                uint8_t t90 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                if (!t90) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_src6d57grm686e3, 606, 42);
                void * t91 = GM_LD(void *, (((uint8_t *)&gm_l16) + 8));
                void * t92 = gm_l1;
                gm_tl874t2atnlh3 t93 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t92) + 72));
                gm_to0f5qn75cgg8 t94;
                memset(&t94, 0, sizeof t94);
                GM_ST(uint8_t, ((uint8_t *)&t94 + 0), ((uint8_t)55ULL));
                GM_ST(void *, ((uint8_t *)&t94 + 8), t91);
                void * t95 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t78, &t94, t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t95;
            }
            {
                void * t96 = (void *)((uint8_t *)&F.l5);
                void * t97 = gm_l11;
                gm_trcmmd12cefb1 t98;
                GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t98 + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&t98 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)4ULL));
                GM_ST(void *, ((uint8_t *)&t98 + 8), t97);
                gm_tftb1odc739t5 t99 = gm_f_push_d6c76ud_2(t96, &t98);
                gm_l17 = t99;
            }
            uint8_t t100 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (!t100) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_src6d57grm686e3, 607, 5);
            {
                void * t101 = gm_l1;
                gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
                void * t103 = gm_l1;
                gm_tl874t2atnlh3 t104 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t103) + 72));
                gm_tgabaikr08mjg t105;
                GM_ST(uint32_t, (uint8_t *)&t105 + 36, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t105 + 0), t102);
                GM_ST(void *, ((uint8_t *)&t105 + 16), ((void *)0));
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t105 + 24), t104);
                gm_tq7s98ihonkju t106 = gm_f_new_dsbahke((void *)NULL, cx, NULL, &t105, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kh47l71qsnm)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t106;
                uint8_t t107 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                if (!t107) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_src6d57grm686e3, 608, 21);
                void * t108 = GM_LD(void *, (((uint8_t *)&gm_l18) + 8));
                gm_to0f5qn75cgg8 t109;
                memset(&t109, 0, sizeof t109);
                GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)46ULL));
                GM_ST(void *, ((uint8_t *)&t109 + 8), t108);
                GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t101) + 8), t109);
            }
            goto gm_lb1;
        }
gm_lb2:;
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
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

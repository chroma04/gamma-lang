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
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; } gm_tkpb4kplftgaa;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; uint64_t f1; gm_tf5srh4mp4b3i f2; gm_tf5srh4mp4b3i f3; gm_b16_8 f4; } gm_t2pjhvnad3fjn;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_b16_8 f1; } gm_thervm8j23qet;
_Static_assert(sizeof(gm_tkpb4kplftgaa) == 24 && _Alignof(gm_tkpb4kplftgaa) == 8 && offsetof(gm_tkpb4kplftgaa, f0) == 0 && offsetof(gm_tkpb4kplftgaa, f1) == 8 && offsetof(gm_tkpb4kplftgaa, f2) == 16, "layout of `(u64, u64, u64)`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tu1089hhijadf;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t6j6smj7qoctu v1; } f1; } gm_t22b1ebhvur5a;
_Static_assert(sizeof(gm_t2pjhvnad3fjn) == 64 && _Alignof(gm_t2pjhvnad3fjn) == 8 && offsetof(gm_t2pjhvnad3fjn, f0) == 0 && offsetof(gm_t2pjhvnad3fjn, f1) == 8 && offsetof(gm_t2pjhvnad3fjn, f2) == 16 && offsetof(gm_t2pjhvnad3fjn, f3) == 32 && offsetof(gm_t2pjhvnad3fjn, f4) == 48, "layout of `Comparator`");
typedef struct { gm_t2pjhvnad3fjn f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; } gm_t3dk6o644dajt;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_thervm8j23qet f0; uint8_t f1; uint8_t pe[7]; } gm_tadokt2of3tma;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_thervm8j23qet) == 24 && _Alignof(gm_thervm8j23qet) == 8 && offsetof(gm_thervm8j23qet, f0) == 0 && offsetof(gm_thervm8j23qet, f1) == 8, "layout of `Slot(string, ())`");
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
typedef struct { uint64_t f0; gm_t2pjhvnad3fjn f1; } gm_tlqibld3f7ga5;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tf1bjg81u0glv v1; } f1; } gm_trto8sim72asr;
_Static_assert(sizeof(gm_tu1089hhijadf) == 56 && _Alignof(gm_tu1089hhijadf) == 8 && offsetof(gm_tu1089hhijadf, f0) == 0 && offsetof(gm_tu1089hhijadf, f1) == 8 && offsetof(gm_tu1089hhijadf, f2) == 16 && offsetof(gm_tu1089hhijadf, f3) == 24 && offsetof(gm_tu1089hhijadf, f4) == 40, "layout of `Version`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tu1089hhijadf v1; } f1; } gm_tvtl3n8qta358;
_Static_assert(sizeof(gm_t22b1ebhvur5a) == 40 && _Alignof(gm_t22b1ebhvur5a) == 8 && offsetof(gm_t22b1ebhvur5a, f0) == 0 && offsetof(gm_t22b1ebhvur5a, f1) == 8, "layout of `string!(ident: string, rest: string)`");
_Static_assert(sizeof(gm_t3dk6o644dajt) == 88 && _Alignof(gm_t3dk6o644dajt) == 8 && offsetof(gm_t3dk6o644dajt, f0) == 0 && offsetof(gm_t3dk6o644dajt, f1) == 64 && offsetof(gm_t3dk6o644dajt, f2) == 72, "layout of `SemComparator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tin97tt4ud0s1 v1; } f1; } gm_t62efos6ssciq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t3dk6o644dajt v1; } f1; } gm_t88cvv9hlbqql;
_Static_assert(sizeof(gm_tadokt2of3tma) == 32 && _Alignof(gm_tadokt2of3tma) == 8 && offsetof(gm_tadokt2of3tma, f0) == 0 && offsetof(gm_tadokt2of3tma, f1) == 24, "layout of `?(ch: u8, rest: string)`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tlqibld3f7ga5) == 72 && _Alignof(gm_tlqibld3f7ga5) == 8 && offsetof(gm_tlqibld3f7ga5, f0) == 0 && offsetof(gm_tlqibld3f7ga5, f1) == 8, "layout of `(i: usize, x: Comparator)`");
_Static_assert(sizeof(gm_trto8sim72asr) == 32 && _Alignof(gm_trto8sim72asr) == 8 && offsetof(gm_trto8sim72asr, f0) == 0 && offsetof(gm_trto8sim72asr, f1) == 8, "layout of `string!SemStep`");
_Static_assert(sizeof(gm_tvtl3n8qta358) == 64 && _Alignof(gm_tvtl3n8qta358) == 8 && offsetof(gm_tvtl3n8qta358, f0) == 0 && offsetof(gm_tvtl3n8qta358, f1) == 8, "layout of `string!Version`");
_Static_assert(sizeof(gm_t62efos6ssciq) == 48 && _Alignof(gm_t62efos6ssciq) == 8 && offsetof(gm_t62efos6ssciq, f0) == 0 && offsetof(gm_t62efos6ssciq, f1) == 8, "layout of `string!VersionReq`");
_Static_assert(sizeof(gm_t88cvv9hlbqql) == 96 && _Alignof(gm_t88cvv9hlbqql) == 8 && offsetof(gm_t88cvv9hlbqql, f0) == 0 && offsetof(gm_t88cvv9hlbqql, f1) == 8, "layout of `string!SemComparator`");
struct gm_fr_all_digits_e4o9vet {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_char_len_9cgkjuk {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_cmp_caret_eeaqse6 {
    gm_frame h;
    gm_tu1089hhijadf l1; /* v */
    uint8_t ret;
};
struct gm_fr_cmp_exact_0js3s6i {
    gm_frame h;
    gm_tu1089hhijadf l1; /* v */
    uint8_t ret;
};
struct gm_fr_cmp_greater_lvmmlp1 {
    gm_frame h;
    gm_tu1089hhijadf l1; /* v */
    uint8_t ret;
};
struct gm_fr_cmp_identifiers_gvvb8ee {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_cmp_less_gjsegc2 {
    gm_frame h;
    gm_tu1089hhijadf l1; /* v */
    uint8_t ret;
};
struct gm_fr_cmp_matches_h8hjnru {
    gm_frame h;
    gm_tu1089hhijadf l1; /* v */
    uint8_t ret;
};
struct gm_fr_cmp_pre_g49p184 {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_cmp_text_523cnan {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_cmp_tilde_e240g5s {
    gm_frame h;
    gm_tu1089hhijadf l1; /* v */
    uint8_t ret;
};
struct gm_fr_cmp_u64_ndpfrv2 {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_cmp_version_0oibvro {
    gm_frame h;
    gm_tu1089hhijadf l0; /* a */
    gm_tu1089hhijadf l1; /* b */
    int32_t ret;
};
struct gm_fr_empty_slice_9k8ef2n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_first_char_2uom2hg {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_format_23lhd7n {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_3aass3a {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_6gn7o2j {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_744g2li {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9c3j64r {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dbdhrrf {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fjhi9ui {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_lac0oi2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_qner803 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_list_pf20o2d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_parse_version_9a6ns50 {
    gm_frame h;
    gm_tu1089hhijadf l1; /* v */
    gm_tvtl3n8qta358 ret;
};
struct gm_fr_parse_version_req_alipvfi {
    gm_frame h;
    gm_tin97tt4ud0s1 l4; /* req */
    gm_t3dk6o644dajt l9; /* parsed */
    gm_t62efos6ssciq ret;
};
struct gm_fr_push_qavl6tf {
    gm_frame h;
    gm_t2pjhvnad3fjn l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_quoted_char_jpbulib {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_regrow_0k8d3ok {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_req_equal_7ia8ip6 {
    gm_frame h;
    gm_t2pjhvnad3fjn l5; /* x */
    gm_t2pjhvnad3fjn l6; /* y */
    uint8_t ret;
};
struct gm_fr_req_matches_dflcrek {
    gm_frame h;
    gm_tu1089hhijadf l1; /* v */
    gm_t2pjhvnad3fjn l4; /* c */
    gm_t2pjhvnad3fjn l7; /* c */
    uint8_t ret;
};
struct gm_fr_req_string_fk059jr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_t2pjhvnad3fjn l5; /* c */
    gm_b16_8 ret;
};
struct gm_fr_sem_comparator_qf8qea0 {
    gm_frame h;
    gm_t88cvv9hlbqql ret;
};
struct gm_fr_sem_dot_4gikq3f {
    gm_frame h;
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_sem_identifier_dte5vh0 {
    gm_frame h;
    gm_t22b1ebhvur5a ret;
};
struct gm_fr_sem_numeric_oik4dle {
    gm_frame h;
    gm_trto8sim72asr ret;
};
struct gm_fr_sem_pos_name_bpi6shr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_sem_wildcard_15qmfol {
    gm_frame h;
    gm_tadokt2of3tma ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_trim_spaces_i65urmt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_trim_zeros_q2fhlfl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_version_string_k02v4te {
    gm_frame h;
    gm_tu1089hhijadf l0; /* v */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_write_7arl25u {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_apgl18i {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_frfjpi8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_mlkl1uj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_tmm3c98 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b);
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqmpqhhfmb90(const uint8_t *a, const uint8_t *b);
inline bool gm_eqmqffvrbc2a(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) uint8_t gm_f_all_digits_e4o9vet(gm_b16_8 a0);
__attribute__((aligned(4))) uint64_t gm_f_char_len_9cgkjuk(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_cmp_caret_eeaqse6(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_cmp_caret_eeaqse6_c(void * a0, gm_tu1089hhijadf a1);
__attribute__((aligned(4))) uint8_t gm_f_cmp_exact_0js3s6i(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_cmp_exact_0js3s6i_c(void * a0, gm_tu1089hhijadf a1);
__attribute__((aligned(4))) uint8_t gm_f_cmp_greater_lvmmlp1(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_cmp_greater_lvmmlp1_c(void * a0, gm_tu1089hhijadf a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_identifiers_gvvb8ee(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_cmp_less_gjsegc2(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_cmp_less_gjsegc2_c(void * a0, gm_tu1089hhijadf a1);
__attribute__((aligned(4))) uint8_t gm_f_cmp_matches_h8hjnru(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_cmp_matches_h8hjnru_c(void * a0, gm_tu1089hhijadf a1);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_pre_g49p184(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_text_523cnan(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_cmp_tilde_e240g5s(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_cmp_tilde_e240g5s_c(void * a0, gm_tu1089hhijadf a1);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_u64_ndpfrv2(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_version_0oibvro(const void * a0, const void * a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_version_0oibvro_c(gm_tu1089hhijadf a0, gm_tu1089hhijadf a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_9k8ef2n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_first_char_2uom2hg(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_format_23lhd7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_3aass3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_6gn7o2j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_744g2li(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_9c3j64r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_dbdhrrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_fjhi9ui(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_lac0oi2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_qner803(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_list_pf20o2d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_parse_version_9a6ns50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_version_req_alipvfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_qavl6tf(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_quoted_char_jpbulib(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0k8d3ok(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) uint8_t gm_f_req_equal_7ia8ip6(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_req_matches_dflcrek(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_req_matches_dflcrek_c(void * a0, gm_tu1089hhijadf a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_req_string_fk059jr(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_sem_comparator_qf8qea0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_sem_dot_4gikq3f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_sem_identifier_dte5vh0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) void gm_f_sem_numeric_oik4dle(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sem_pos_name_bpi6shr(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_sem_wildcard_15qmfol(void *gm_out, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tadokt2of3tma gm_f_sem_wildcard_15qmfol_c(gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_spaces_i65urmt(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_zeros_q2fhlfl(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_version_string_k02v4te(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7arl25u(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_apgl18i(void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_frfjpi8(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_guk0b42(void * a0, const void * a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hqo1u7h(void * a0, gm_t4ce0jd3tdro4 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_mlkl1uj(void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_tmm3c98(void * a0, gm_tnpifv20irnph a1);
extern uint8_t gm_cuqbj62gqhf[16] __attribute__((aligned(8)));
extern uint8_t gm_cuqbj62gqhf_2[16] __attribute__((aligned(8)));
extern const uint8_t gm_s02ru83e3fffc[3];
extern const uint8_t gm_s129prqlnb2kt[41];
extern const uint8_t gm_s147lrab6u68i[29];
extern const uint8_t gm_s1f440st4mnkp[2];
extern const uint8_t gm_s2keeidsqic9l[41];
extern const uint8_t gm_s2nmq6ak9nl9d[5];
extern const uint8_t gm_s3b32la11650d[51];
extern const uint8_t gm_s3n43rret3i7v[21];
extern const uint8_t gm_s460n81rl2c9c[5];
extern const uint8_t gm_s4b0873bt7c1q[5];
extern const uint8_t gm_s4g3cajt15dmi[3];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s5ocg5vi2qib3[21];
extern const uint8_t gm_s6s96daqmhcs9[2];
extern const uint8_t gm_s9hqndl9se5nc[31];
extern const uint8_t gm_s9r72jgd5hb05[5];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sajhum9b4nr96[2];
extern const uint8_t gm_sc1nadatuanhc[5];
extern const uint8_t gm_sc8o4oaph9gbr[2];
extern const uint8_t gm_sclthecvcggbv[15];
extern const uint8_t gm_sct07pl02n02h[27];
extern const uint8_t gm_sd70ob31745fn[4];
extern const uint8_t gm_setdfq42nhg1q[5];
extern const uint8_t gm_sfd6isdlu0d13[21];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sh0j2ggs1vvoa[2];
extern const uint8_t gm_sh4f06c2ecgj1[5];
extern const uint8_t gm_shq9t6lutffbf[4];
extern const uint8_t gm_shroj9d6fajb6[4];
extern const uint8_t gm_si7spjb0bqq6r[9];
extern const uint8_t gm_sio5n0k35hfb4[65];
extern const uint8_t gm_sjm2vqdj9hddi[51];
extern const uint8_t gm_sk71kk4665fg9[34];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_slv9mko23s9sn[33];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_sn7tl9jolqoj1[3];
extern const uint8_t gm_sp51dovg85ku9[40];
extern const uint8_t gm_sr499ju9p7ks2[23];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcij34jbkb4qlc[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_sua0n2tdbtos7[40];
extern const uint8_t gm_sviu03g0f5s30[43];
extern const gm_tmpl gm_tpl00nmcu166crh;
extern const gm_tmpl gm_tpl150n44984e0j;
extern const gm_tmpl gm_tpl4lt7abhi3ej4;
extern const gm_tmpl gm_tpl6a59mrfvoo09;
extern const gm_tmpl gm_tpl80egqk8smgev;
extern const gm_tmpl gm_tpl9qg0hlqqke7e;
extern const gm_tmpl gm_tpld7ebjjnpmsr0;
extern const gm_tmpl gm_tpldg1m204qb08c;
extern const gm_tmpl gm_tpldrlq7uo0cbrm;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplpaq33rl4cnb8;
extern const gm_tmpl gm_tplshqe0v39oc52;
extern const gm_tmpl gm_tplsvdv91ohcdg6;
extern const gm_tmpl gm_tplutkid6t0497p;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eqb2nno7n67r(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eqb44cb3cf1n(const uint8_t *a, const uint8_t *b) {
    if (a[1] != b[1]) return false;
    return !a[1] || gm_eqjmccqdb90f(a, b);
}
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline bool gm_eqmpqhhfmb90(const uint8_t *a, const uint8_t *b) {
    if (a[8] != b[8]) return false;
    return !a[8] || gm_eqb2nno7n67r(a, b);
}
inline bool gm_eqmqffvrbc2a(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline __attribute__((aligned(4))) int32_t gm_f_cmp_pre_g49p184(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_pre_g49p184 F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 111u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    int32_t gm_l2;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (t5) {
            gm_l2 = ((int32_t)0LL);
        } else {
            gm_l2 = ((int32_t)1LL);
        }
        int32_t t6 = gm_l2;
        F.ret = t6;
        goto gm_fret;
    }
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
    if (t8) {
        F.ret = ((int32_t)-1LL);
        goto gm_fret;
    }
    gm_b16_8 t9 = gm_l0;
    gm_b16_8 t10 = gm_l1;
    int32_t t11 = gm_f_cmp_identifiers_gvvb8ee(t9, t10);
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int32_t gm_f_cmp_u64_ndpfrv2(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_u64_ndpfrv2 F;
    uint64_t gm_l0; /* a */
    uint64_t gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    int32_t gm_l2;
    int32_t gm_l3;
    uint64_t t2 = gm_l0;
    uint64_t t3 = gm_l1;
    uint8_t t4 = (t2 < t3);
    if (t4) {
        gm_l2 = ((int32_t)-1LL);
    } else {
        uint64_t t5 = gm_l0;
        uint64_t t6 = gm_l1;
        uint8_t t7 = (t5 > t6);
        if (t7) {
            gm_l3 = ((int32_t)1LL);
        } else {
            gm_l3 = ((int32_t)0LL);
        }
        int32_t t8 = gm_l3;
        gm_l2 = t8;
    }
    int32_t t9 = gm_l2;
    F.ret = t9;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_9k8ef2n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_9k8ef2n F;
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
inline __attribute__((aligned(4))) uint8_t gm_f_first_char_2uom2hg(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_first_char_2uom2hg F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 50u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l1;
    gm_tqs5p4n2tq19v gm_l2;
    gm_tqs5p4n2tq19v gm_l3;
    void * t2 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t4 = (((uint64_t)0ULL) < t3);
    if (t4) {
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t2) + (uintptr_t)((uint64_t)0ULL) * 1));
        gm_tqs5p4n2tq19v t6;
        GM_ST(uint8_t, ((uint8_t *)&t6), t5);
        ((uint8_t *)&t6)[1] = 1;
        gm_l2 = t6;
    } else {
        gm_tqs5p4n2tq19v t7;
        memset(&t7, 0, sizeof t7);
        gm_l2 = t7;
    }
    gm_tqs5p4n2tq19v t8 = gm_l2;
    gm_l3 = t8;
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l3))[1] != 0);
    if (t9) {
        uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
        gm_l1 = t10;
    } else {
        gm_l1 = ((uint8_t)0ULL);
    }
    uint8_t t11 = gm_l1;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_23lhd7n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_23lhd7n F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sio5n0k35hfb4));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(64));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6a59mrfvoo09), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_3aass3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_3aass3a F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9r72jgd5hb05));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl80egqk8smgev), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_6gn7o2j(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_6gn7o2j F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sk71kk4665fg9));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(33));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplpaq33rl4cnb8), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_744g2li(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_744g2li F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_slv9mko23s9sn));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(32));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplsvdv91ohcdg6), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_9c3j64r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_9c3j64r F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2keeidsqic9l));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpld7ebjjnpmsr0), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_dbdhrrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dbdhrrf F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s147lrab6u68i));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl4lt7abhi3ej4), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_fjhi9ui(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fjhi9ui F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sct07pl02n02h));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(26));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplutkid6t0497p), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_lac0oi2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_lac0oi2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9hqndl9se5nc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl00nmcu166crh), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_qner803(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_qner803 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s129prqlnb2kt));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(40));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpldg1m204qb08c), t2);
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
inline __attribute__((aligned(4))) void gm_f_list_pf20o2d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pf20o2d F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_9k8ef2n();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_qavl6tf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_qavl6tf F;
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
        gm_b16_8 t9 = gm_f_empty_slice_9k8ef2n();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_0k8d3ok(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 64), ((uint8_t *)&F.l1), 64);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 64)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0k8d3ok(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0k8d3ok F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)64ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7arl25u(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_7arl25u F;
    void * gm_l0; /* l */
    gm_tkpb4kplftgaa gm_l1; /* args */
    GM_STACK_CHECK(&F, 62u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_si7spjb0bqq6r));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_tftb1odc739t5 t4 = gm_f_write_guk0b42(t2, &(*(gm_tkpb4kplftgaa *)((uint8_t *)a1)), ((void *)&gm_tpl150n44984e0j), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_apgl18i(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_apgl18i F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(2));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tplk34ojqiaho84), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_frfjpi8(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_frfjpi8 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_shroj9d6fajb6));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplshqe0v39oc52), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_mlkl1uj(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_mlkl1uj F;
    void * gm_l0; /* l */
    gm_t4ce0jd3tdro4 gm_l1; /* args */
    GM_STACK_CHECK(&F, 54u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t4ce0jd3tdro4 t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_shq9t6lutffbf));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tpldrlq7uo0cbrm), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_tmm3c98(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_tmm3c98 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sd70ob31745fn));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl9qg0hlqqke7e), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_all_digits_e4o9vet(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_all_digits_e4o9vet F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    uint8_t gm_l4; /* <tmp> */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l5;
    gm_b16_8 t7 = gm_l0;
    gm_l1 = t7;
    gm_l2 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t8 = gm_l2;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t10 = (t8 < t9);
            if (!t10) {
                goto gm_lb1;
            }
            {
                void * t11 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t13 = gm_l2;
                {
                    {
                        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t11) + (uintptr_t)t13 * 1));
                        gm_l3 = t14;
                        uint8_t t15 = gm_l3;
                        gm_l4 = t15;
                        uint8_t t16 = gm_l4;
                        uint8_t t17 = (((uint8_t)48ULL) <= t16);
                        if (t17) {
                            uint8_t t18 = gm_l4;
                            uint8_t t19 = (t18 <= ((uint8_t)57ULL));
                            gm_l5 = t19;
                        } else {
                            gm_l5 = ((uint8_t)0);
                        }
                        uint8_t t20 = gm_l5;
                        uint8_t t21 = (uint8_t)!t20;
                        if (t21) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcij34jbkb4qlc, 344, 5);
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
    F.ret = ((uint8_t)1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_char_len_9cgkjuk(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_char_len_9cgkjuk F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* b */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t gm_l3;
    uint64_t gm_l4;
    uint64_t gm_l5;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        F.ret = ((uint64_t)0ULL);
        goto gm_fret;
    }
    void * t4 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t5, gm_srcij34jbkb4qlc, 71, 10);
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t4) + (uintptr_t)((uint64_t)0ULL) * 1));
    gm_l1 = t6;
    uint8_t t7 = gm_l1;
    uint8_t t8 = (t7 < ((uint8_t)128ULL));
    if (t8) {
        gm_l2 = ((uint64_t)1ULL);
    } else {
        uint8_t t9 = gm_l1;
        uint8_t t10 = (t9 >= ((uint8_t)240ULL));
        if (t10) {
            gm_l3 = ((uint64_t)4ULL);
        } else {
            uint8_t t11 = gm_l1;
            uint8_t t12 = (t11 >= ((uint8_t)224ULL));
            if (t12) {
                gm_l4 = ((uint64_t)3ULL);
            } else {
                gm_l4 = ((uint64_t)2ULL);
            }
            uint64_t t13 = gm_l4;
            gm_l3 = t13;
        }
        uint64_t t14 = gm_l3;
        gm_l2 = t14;
    }
    uint64_t t15 = gm_l2;
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t17 = (t15 > t16);
    if (t17) {
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        gm_l5 = t18;
    } else {
        uint64_t t19 = gm_l2;
        gm_l5 = t19;
    }
    uint64_t t20 = gm_l5;
    F.ret = t20;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_cmp_caret_eeaqse6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_caret_eeaqse6 F;
    void * gm_l0; /* c */
    uint64_t gm_l2; /* minor */
    uint64_t gm_l3; /* patch */
    GM_STACK_CHECK_SHALLOW(&F, 362u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l4;
    uint64_t t2 = GM_LD(uint64_t, ((uint8_t *)a1));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (!(t2 == t4));
    if (t5) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t6 = gm_l0;
    uint8_t t7 = (uint8_t)(((((uint8_t *)t6) + 16))[8] != 0);
    if (t7) {
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t6) + 16));
        gm_l2 = t8;
    } else {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    void * t9 = gm_l0;
    uint8_t t10 = (uint8_t)(((((uint8_t *)t9) + 32))[8] != 0);
    if (t10) {
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t9) + 32));
        gm_l3 = t11;
    } else {
        void * t12 = gm_l0;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 8));
        uint8_t t14 = (t13 > ((uint64_t)0ULL));
        if (t14) {
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
            uint64_t t16 = gm_l2;
            uint8_t t17 = (t15 >= t16);
            F.ret = t17;
            goto gm_fret;
        }
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        uint64_t t19 = gm_l2;
        uint8_t t20 = ((t18 == t19));
        F.ret = t20;
        goto gm_fret;
    }
    void * t21 = gm_l0;
    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 8));
    uint8_t t23 = (t22 > ((uint64_t)0ULL));
    if (t23) {
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        uint64_t t25 = gm_l2;
        uint8_t t26 = (!(t24 == t25));
        if (t26) {
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
            uint64_t t28 = gm_l2;
            uint8_t t29 = (t27 > t28);
            F.ret = t29;
            goto gm_fret;
        }
        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
        uint64_t t31 = gm_l3;
        uint8_t t32 = (!(t30 == t31));
        if (t32) {
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
            uint64_t t34 = gm_l3;
            uint8_t t35 = (t33 > t34);
            F.ret = t35;
            goto gm_fret;
        }
    } else {
        uint64_t t36 = gm_l2;
        uint8_t t37 = (t36 > ((uint64_t)0ULL));
        if (t37) {
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
            uint64_t t39 = gm_l2;
            uint8_t t40 = (!(t38 == t39));
            if (t40) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
            uint64_t t42 = gm_l3;
            uint8_t t43 = (!(t41 == t42));
            if (t43) {
                uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
                uint64_t t45 = gm_l3;
                uint8_t t46 = (t44 > t45);
                F.ret = t46;
                goto gm_fret;
            }
        } else {
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
            uint64_t t48 = gm_l2;
            uint8_t t49 = (!(t47 == t48));
            if (t49) {
                gm_l4 = ((uint8_t)1);
            } else {
                uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
                uint64_t t51 = gm_l3;
                uint8_t t52 = (!(t50 == t51));
                gm_l4 = t52;
            }
            uint8_t t53 = gm_l4;
            if (t53) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
        }
    }
    gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 24));
    void * t55 = gm_l0;
    gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)t55) + 48));
    int32_t t57 = gm_f_cmp_pre_g49p184(t54, t56);
    uint8_t t58 = (t57 >= ((int32_t)0LL));
    F.ret = t58;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_cmp_caret_eeaqse6_c(void * a0, gm_tu1089hhijadf a1) { return gm_f_cmp_caret_eeaqse6(a0, &a1); }
inline __attribute__((aligned(4))) uint8_t gm_f_cmp_exact_0js3s6i(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_exact_0js3s6i F;
    void * gm_l0; /* c */
    uint64_t gm_l2; /* m */
    uint64_t gm_l3; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)a1));
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint8_t t9 = (!(t6 == t8));
    if (t9) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    {
        {
            void * t10 = gm_l0;
            uint8_t t11 = (uint8_t)(((((uint8_t *)t10) + 16))[8] != 0);
            if (!t11) {
                goto gm_lb2;
            }
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t10) + 16));
            gm_l2 = t12;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
            uint64_t t14 = gm_l2;
            uint8_t t15 = (!(t13 == t14));
            if (t15) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t16 = gm_l0;
            uint8_t t17 = (uint8_t)(((((uint8_t *)t16) + 32))[8] != 0);
            if (!t17) {
                goto gm_lb4;
            }
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t16) + 32));
            gm_l3 = t18;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
            uint64_t t20 = gm_l3;
            uint8_t t21 = (!(t19 == t20));
            if (t21) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 24));
    void * t23 = gm_l0;
    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t23) + 48));
    uint8_t t25 = (gm_eq0iqiubfm7e(((uint8_t *)&t22), ((uint8_t *)&t24)));
    F.ret = t25;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_cmp_exact_0js3s6i_c(void * a0, gm_tu1089hhijadf a1) { return gm_f_cmp_exact_0js3s6i(a0, &a1); }
inline __attribute__((aligned(4))) uint8_t gm_f_cmp_greater_lvmmlp1(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_greater_lvmmlp1 F;
    void * gm_l0; /* c */
    uint64_t gm_l2; /* m */
    uint64_t gm_l3; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 223u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t2 = GM_LD(uint64_t, ((uint8_t *)a1));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (!(t2 == t4));
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)a1));
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint8_t t9 = (t6 > t8);
        F.ret = t9;
        goto gm_fret;
    }
    void * t10 = gm_l0;
    uint8_t t11 = (uint8_t)(((((uint8_t *)t10) + 16))[8] != 0);
    if (t11) {
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t10) + 16));
        gm_l2 = t12;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
    uint64_t t14 = gm_l2;
    uint8_t t15 = (!(t13 == t14));
    if (t15) {
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        uint64_t t17 = gm_l2;
        uint8_t t18 = (t16 > t17);
        F.ret = t18;
        goto gm_fret;
    }
    void * t19 = gm_l0;
    uint8_t t20 = (uint8_t)(((((uint8_t *)t19) + 32))[8] != 0);
    if (t20) {
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t19) + 32));
        gm_l3 = t21;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
    uint64_t t23 = gm_l3;
    uint8_t t24 = (!(t22 == t23));
    if (t24) {
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
        uint64_t t26 = gm_l3;
        uint8_t t27 = (t25 > t26);
        F.ret = t27;
        goto gm_fret;
    }
    gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 24));
    void * t29 = gm_l0;
    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)t29) + 48));
    int32_t t31 = gm_f_cmp_pre_g49p184(t28, t30);
    uint8_t t32 = (t31 > ((int32_t)0LL));
    F.ret = t32;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_cmp_greater_lvmmlp1_c(void * a0, gm_tu1089hhijadf a1) { return gm_f_cmp_greater_lvmmlp1(a0, &a1); }
__attribute__((aligned(4))) int32_t gm_f_cmp_identifiers_gvvb8ee(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_identifiers_gvvb8ee F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* ia */
    uint64_t gm_l3; /* ib */
    uint64_t gm_l4; /* ea */
    uint64_t gm_l5; /* eb */
    gm_b16_8 gm_l6; /* pa */
    gm_b16_8 gm_l7; /* pb */
    int32_t gm_l8; /* o */
    uint8_t gm_l9; /* da */
    uint8_t gm_l10; /* db */
    gm_b16_8 gm_l11; /* va */
    gm_b16_8 gm_l12; /* vb */
    GM_STACK_CHECK_SHALLOW(&F, 799u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    int32_t gm_l13;
    int32_t gm_l14;
    int32_t gm_l15;
    int32_t gm_l16;
    int32_t gm_l17;
    gm_l2 = ((uint64_t)0ULL);
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                uint64_t t13 = gm_l2;
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint8_t t15 = (t13 > t14);
                if (t15) {
                    uint64_t t16 = gm_l3;
                    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                    uint8_t t18 = (t16 > t17);
                    if (t18) {
                        gm_l13 = ((int32_t)0LL);
                    } else {
                        gm_l13 = ((int32_t)-1LL);
                    }
                    int32_t t19 = gm_l13;
                    F.ret = t19;
                    goto gm_fret;
                }
                uint64_t t20 = gm_l3;
                uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint8_t t22 = (t20 > t21);
                if (t22) {
                    F.ret = ((int32_t)1LL);
                    goto gm_fret;
                }
                uint64_t t23 = gm_l2;
                gm_l4 = t23;
                {
                    for (;;) {
                        uint64_t t24 = gm_l4;
                        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint8_t t26 = (t24 < t25);
                        if (!t26) {
                            goto gm_lb4;
                        }
                        void * t27 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t29 = gm_l4;
                        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t27) + (uintptr_t)t29 * 1));
                        uint8_t t31 = (!(t30 == ((uint8_t)46ULL)));
                        if (!t31) {
                            goto gm_lb4;
                        }
                        {
                            uint64_t t32 = gm_l4;
                            uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                            if (o34) gm_trap("integer overflow in `+`", gm_srcij34jbkb4qlc, 381, 49);
                            uint64_t t35 = r33;
                            gm_l4 = t35;
                        }
gm_lb6:;
                    }
                }
gm_lb4:;
                uint64_t t36 = gm_l3;
                gm_l5 = t36;
                {
                    for (;;) {
                        uint64_t t37 = gm_l5;
                        uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint8_t t39 = (t37 < t38);
                        if (!t39) {
                            goto gm_lb7;
                        }
                        void * t40 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t42 = gm_l5;
                        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)t40) + (uintptr_t)t42 * 1));
                        uint8_t t44 = (!(t43 == ((uint8_t)46ULL)));
                        if (!t44) {
                            goto gm_lb7;
                        }
                        {
                            uint64_t t45 = gm_l5;
                            uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                            if (o47) gm_trap("integer overflow in `+`", gm_srcij34jbkb4qlc, 383, 49);
                            uint64_t t48 = r46;
                            gm_l5 = t48;
                        }
gm_lb9:;
                    }
                }
gm_lb7:;
                void * t49 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t51 = gm_l2;
                uint64_t t52 = gm_l4;
                uint64_t t53 = (uintptr_t)t51;
                uint64_t t54 = (uintptr_t)t52;
                if (t53 > t54 || t54 > (uintptr_t)t50) gm_trap_slice(t53, t54, t50, gm_srcij34jbkb4qlc, 384, 15);
                uint64_t t55 = ((uintptr_t)t52) - (uintptr_t)t51;
                gm_b16_8 t56;
                GM_ST(void *, (uint8_t *)&t56, (void *)(((uint8_t *)t49 + (uintptr_t)t51 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(t55));
                gm_l6 = t56;
                void * t57 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t59 = gm_l3;
                uint64_t t60 = gm_l5;
                uint64_t t61 = (uintptr_t)t59;
                uint64_t t62 = (uintptr_t)t60;
                if (t61 > t62 || t62 > (uintptr_t)t58) gm_trap_slice(t61, t62, t58, gm_srcij34jbkb4qlc, 385, 15);
                uint64_t t63 = ((uintptr_t)t60) - (uintptr_t)t59;
                gm_b16_8 t64;
                GM_ST(void *, (uint8_t *)&t64, (void *)(((uint8_t *)t57 + (uintptr_t)t59 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(t63));
                gm_l7 = t64;
                gm_l8 = ((int32_t)0LL);
                gm_b16_8 t65 = gm_l6;
                uint8_t t66 = gm_f_all_digits_e4o9vet(t65);
                gm_l9 = t66;
                gm_b16_8 t67 = gm_l7;
                uint8_t t68 = gm_f_all_digits_e4o9vet(t67);
                gm_l10 = t68;
                {
                    {
                        uint8_t t69 = gm_l9;
                        if (!t69) {
                            goto gm_lb11;
                        }
                        uint8_t t70 = gm_l10;
                        if (!t70) {
                            goto gm_lb11;
                        }
                        gm_b16_8 t71 = gm_l6;
                        gm_b16_8 t72 = gm_f_trim_zeros_q2fhlfl(t71);
                        gm_l11 = t72;
                        gm_b16_8 t73 = gm_l7;
                        gm_b16_8 t74 = gm_f_trim_zeros_q2fhlfl(t73);
                        gm_l12 = t74;
                        uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                        uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                        uint8_t t77 = (t75 < t76);
                        if (t77) {
                            gm_l14 = ((int32_t)-1LL);
                        } else {
                            uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                            uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                            uint8_t t80 = (t78 > t79);
                            if (t80) {
                                gm_l15 = ((int32_t)1LL);
                            } else {
                                gm_b16_8 t81 = gm_l11;
                                gm_b16_8 t82 = gm_l12;
                                int32_t t83 = gm_f_cmp_text_523cnan(t81, t82);
                                gm_l15 = t83;
                            }
                            int32_t t84 = gm_l15;
                            gm_l14 = t84;
                        }
                        int32_t t85 = gm_l14;
                        gm_l8 = t85;
                        int32_t t86 = gm_l8;
                        uint8_t t87 = ((t86 == ((int32_t)0LL)));
                        if (t87) {
                            uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            uint8_t t90 = (t88 < t89);
                            if (t90) {
                                gm_l16 = ((int32_t)-1LL);
                            } else {
                                uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                uint8_t t93 = (t91 > t92);
                                if (t93) {
                                    gm_l17 = ((int32_t)1LL);
                                } else {
                                    gm_l17 = ((int32_t)0LL);
                                }
                                int32_t t94 = gm_l17;
                                gm_l16 = t94;
                            }
                            int32_t t95 = gm_l16;
                            gm_l8 = t95;
                        }
                        goto gm_lb10;
                    }
gm_lb11:;
                    uint8_t t96 = gm_l9;
                    if (t96) {
                        F.ret = ((int32_t)-1LL);
                        goto gm_fret;
                    } else {
                        uint8_t t97 = gm_l10;
                        if (t97) {
                            F.ret = ((int32_t)1LL);
                            goto gm_fret;
                        } else {
                            gm_b16_8 t98 = gm_l6;
                            gm_b16_8 t99 = gm_l7;
                            int32_t t100 = gm_f_cmp_text_523cnan(t98, t99);
                            gm_l8 = t100;
                        }
                    }
                }
gm_lb10:;
                int32_t t101 = gm_l8;
                uint8_t t102 = (!(t101 == ((int32_t)0LL)));
                if (t102) {
                    int32_t t103 = gm_l8;
                    F.ret = t103;
                    goto gm_fret;
                }
                uint64_t t104 = gm_l4;
                uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                if (o106) gm_trap("integer overflow in `+`", gm_srcij34jbkb4qlc, 402, 14);
                uint64_t t107 = r105;
                gm_l2 = t107;
                uint64_t t108 = gm_l5;
                uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                if (o110) gm_trap("integer overflow in `+`", gm_srcij34jbkb4qlc, 403, 14);
                uint64_t t111 = r109;
                gm_l3 = t111;
            }
gm_lb3:;
        }
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_cmp_less_gjsegc2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_less_gjsegc2 F;
    void * gm_l0; /* c */
    uint64_t gm_l2; /* m */
    uint64_t gm_l3; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 223u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t2 = GM_LD(uint64_t, ((uint8_t *)a1));
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (!(t2 == t4));
    if (t5) {
        uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)a1));
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint8_t t9 = (t6 < t8);
        F.ret = t9;
        goto gm_fret;
    }
    void * t10 = gm_l0;
    uint8_t t11 = (uint8_t)(((((uint8_t *)t10) + 16))[8] != 0);
    if (t11) {
        uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t10) + 16));
        gm_l2 = t12;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
    uint64_t t14 = gm_l2;
    uint8_t t15 = (!(t13 == t14));
    if (t15) {
        uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        uint64_t t17 = gm_l2;
        uint8_t t18 = (t16 < t17);
        F.ret = t18;
        goto gm_fret;
    }
    void * t19 = gm_l0;
    uint8_t t20 = (uint8_t)(((((uint8_t *)t19) + 32))[8] != 0);
    if (t20) {
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t19) + 32));
        gm_l3 = t21;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
    uint64_t t23 = gm_l3;
    uint8_t t24 = (!(t22 == t23));
    if (t24) {
        uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
        uint64_t t26 = gm_l3;
        uint8_t t27 = (t25 < t26);
        F.ret = t27;
        goto gm_fret;
    }
    gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 24));
    void * t29 = gm_l0;
    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)t29) + 48));
    int32_t t31 = gm_f_cmp_pre_g49p184(t28, t30);
    uint8_t t32 = (t31 < ((int32_t)0LL));
    F.ret = t32;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_cmp_less_gjsegc2_c(void * a0, gm_tu1089hhijadf a1) { return gm_f_cmp_less_gjsegc2(a0, &a1); }
inline __attribute__((aligned(4))) uint8_t gm_f_cmp_matches_h8hjnru(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_matches_h8hjnru F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    uint8_t gm_l3;
    uint8_t gm_l4;
    void * t13 = gm_l0;
    {
        {
            {
                {
                    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
                    uint8_t t15 = (uint8_t)(t14 == ((uint8_t)0ULL));
                    if (!t15) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
                    uint8_t t17 = (uint8_t)(t16 == ((uint8_t)7ULL));
                    if (!t17) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                goto gm_lb2;
            }
gm_lb3:;
            void * t18 = gm_l0;
            uint8_t t19 = gm_f_cmp_exact_0js3s6i(t18, &(*(gm_tu1089hhijadf *)((uint8_t *)a1)));
            gm_l2 = t19;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)1ULL));
            if (!t21) {
                goto gm_lb6;
            }
            void * t22 = gm_l0;
            uint8_t t23 = gm_f_cmp_greater_lvmmlp1(t22, &(*(gm_tu1089hhijadf *)((uint8_t *)a1)));
            gm_l2 = t23;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)2ULL));
            if (!t25) {
                goto gm_lb7;
            }
            void * t26 = gm_l0;
            uint8_t t27 = gm_f_cmp_exact_0js3s6i(t26, &(*(gm_tu1089hhijadf *)((uint8_t *)a1)));
            if (t27) {
                gm_l3 = ((uint8_t)1);
            } else {
                void * t28 = gm_l0;
                uint8_t t29 = gm_f_cmp_greater_lvmmlp1(t28, &(*(gm_tu1089hhijadf *)((uint8_t *)a1)));
                gm_l3 = t29;
            }
            uint8_t t30 = gm_l3;
            gm_l2 = t30;
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)3ULL));
            if (!t32) {
                goto gm_lb8;
            }
            void * t33 = gm_l0;
            uint8_t t34 = gm_f_cmp_less_gjsegc2(t33, &(*(gm_tu1089hhijadf *)((uint8_t *)a1)));
            gm_l2 = t34;
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)4ULL));
            if (!t36) {
                goto gm_lb9;
            }
            void * t37 = gm_l0;
            uint8_t t38 = gm_f_cmp_exact_0js3s6i(t37, &(*(gm_tu1089hhijadf *)((uint8_t *)a1)));
            if (t38) {
                gm_l4 = ((uint8_t)1);
            } else {
                void * t39 = gm_l0;
                uint8_t t40 = gm_f_cmp_less_gjsegc2(t39, &(*(gm_tu1089hhijadf *)((uint8_t *)a1)));
                gm_l4 = t40;
            }
            uint8_t t41 = gm_l4;
            gm_l2 = t41;
            goto gm_lb1;
        }
gm_lb9:;
        {
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t43 = (uint8_t)(t42 == ((uint8_t)5ULL));
            if (!t43) {
                goto gm_lb10;
            }
            void * t44 = gm_l0;
            uint8_t t45 = gm_f_cmp_tilde_e240g5s(t44, &(*(gm_tu1089hhijadf *)((uint8_t *)a1)));
            gm_l2 = t45;
            goto gm_lb1;
        }
gm_lb10:;
        {
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)t13) + 0));
            uint8_t t47 = (uint8_t)(t46 == ((uint8_t)6ULL));
            if (!t47) {
                goto gm_lb11;
            }
            void * t48 = gm_l0;
            uint8_t t49 = gm_f_cmp_caret_eeaqse6(t48, &(*(gm_tu1089hhijadf *)((uint8_t *)a1)));
            gm_l2 = t49;
            goto gm_lb1;
        }
gm_lb11:;
        gm_trap("no match arm matched the scrutinee", gm_srcij34jbkb4qlc, 433, 12);
    }
gm_lb1:;
    uint8_t t50 = gm_l2;
    F.ret = t50;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_cmp_matches_h8hjnru_c(void * a0, gm_tu1089hhijadf a1) { return gm_f_cmp_matches_h8hjnru(a0, &a1); }
extern __attribute__((aligned(4))) int32_t gm_f_cmp_pre_g49p184(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_text_523cnan(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_text_523cnan F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* n */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 290u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t9 = (t7 < t8);
    if (t9) {
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        gm_l2 = t10;
    } else {
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_l2 = t11;
    }
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t12 = gm_l2;
    gm_l3 = t12;
    {
        for (;;) {
            uint64_t t13 = gm_l4;
            uint64_t t14 = gm_l3;
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t16 = gm_l4;
                        gm_l5 = t16;
                        void * t17 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t19 = gm_l5;
                        if ((uintptr_t)t19 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t19, t18, gm_srcij34jbkb4qlc, 354, 12);
                        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)t17) + (uintptr_t)t19 * 1));
                        void * t21 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t23 = gm_l5;
                        if ((uintptr_t)t23 >= (uintptr_t)(t22)) gm_trap_index((uintptr_t)t23, t22, gm_srcij34jbkb4qlc, 354, 19);
                        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)t21) + (uintptr_t)t23 * 1));
                        uint8_t t25 = (t20 < t24);
                        if (t25) {
                            F.ret = ((int32_t)-1LL);
                            goto gm_fret;
                        }
                        void * t26 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t28 = gm_l5;
                        if ((uintptr_t)t28 >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)t28, t27, gm_srcij34jbkb4qlc, 355, 12);
                        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)t26) + (uintptr_t)t28 * 1));
                        void * t30 = GM_LD(void *, ((uint8_t *)&gm_l1));
                        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t t32 = gm_l5;
                        if ((uintptr_t)t32 >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)t32, t31, gm_srcij34jbkb4qlc, 355, 19);
                        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)t30) + (uintptr_t)t32 * 1));
                        uint8_t t34 = (t29 > t33);
                        if (t34) {
                            F.ret = ((int32_t)1LL);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcij34jbkb4qlc, 353, 5);
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
    uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t41 = (t39 < t40);
    if (t41) {
        F.ret = ((int32_t)-1LL);
        goto gm_fret;
    }
    uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t44 = (t42 > t43);
    if (t44) {
        F.ret = ((int32_t)1LL);
        goto gm_fret;
    }
    F.ret = ((int32_t)0LL);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_cmp_tilde_e240g5s(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_tilde_e240g5s F;
    void * gm_l0; /* c */
    uint64_t gm_l2; /* m */
    uint64_t gm_l3; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 181u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t6 = GM_LD(uint64_t, ((uint8_t *)a1));
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    uint8_t t9 = (!(t6 == t8));
    if (t9) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    {
        {
            void * t10 = gm_l0;
            uint8_t t11 = (uint8_t)(((((uint8_t *)t10) + 16))[8] != 0);
            if (!t11) {
                goto gm_lb2;
            }
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t10) + 16));
            gm_l2 = t12;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
            uint64_t t14 = gm_l2;
            uint8_t t15 = (!(t13 == t14));
            if (t15) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        {
            void * t16 = gm_l0;
            uint8_t t17 = (uint8_t)(((((uint8_t *)t16) + 32))[8] != 0);
            if (!t17) {
                goto gm_lb4;
            }
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t16) + 32));
            gm_l3 = t18;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
            uint64_t t20 = gm_l3;
            uint8_t t21 = (!(t19 == t20));
            if (t21) {
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
                uint64_t t23 = gm_l3;
                uint8_t t24 = (t22 > t23);
                F.ret = t24;
                goto gm_fret;
            }
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 24));
    void * t26 = gm_l0;
    gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)t26) + 48));
    int32_t t28 = gm_f_cmp_pre_g49p184(t25, t27);
    uint8_t t29 = (t28 >= ((int32_t)0LL));
    F.ret = t29;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_cmp_tilde_e240g5s_c(void * a0, gm_tu1089hhijadf a1) { return gm_f_cmp_tilde_e240g5s(a0, &a1); }
extern __attribute__((aligned(4))) int32_t gm_f_cmp_u64_ndpfrv2(uint64_t a0, uint64_t a1);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_version_0oibvro(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_version_0oibvro F;
    int32_t gm_l2; /* o */
    GM_STACK_CHECK_SHALLOW(&F, 215u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    int32_t gm_l3;
    uint64_t t2 = GM_LD(uint64_t, ((uint8_t *)a0));
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)a1));
    int32_t t4 = gm_f_cmp_u64_ndpfrv2(t2, t3);
    gm_l2 = t4;
    int32_t t5 = gm_l2;
    uint8_t t6 = (!(t5 == ((int32_t)0LL)));
    if (t6) {
        int32_t t7 = gm_l2;
        F.ret = t7;
        goto gm_fret;
    }
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
    int32_t t10 = gm_f_cmp_u64_ndpfrv2(t8, t9);
    gm_l2 = t10;
    int32_t t11 = gm_l2;
    uint8_t t12 = (!(t11 == ((int32_t)0LL)));
    if (t12) {
        int32_t t13 = gm_l2;
        F.ret = t13;
        goto gm_fret;
    }
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)a0) + 16));
    uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
    int32_t t16 = gm_f_cmp_u64_ndpfrv2(t14, t15);
    gm_l2 = t16;
    int32_t t17 = gm_l2;
    uint8_t t18 = (!(t17 == ((int32_t)0LL)));
    if (t18) {
        int32_t t19 = gm_l2;
        F.ret = t19;
        goto gm_fret;
    }
    gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 24));
    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 24));
    int32_t t22 = gm_f_cmp_pre_g49p184(t20, t21);
    gm_l2 = t22;
    int32_t t23 = gm_l2;
    uint8_t t24 = (!(t23 == ((int32_t)0LL)));
    if (t24) {
        int32_t t25 = gm_l2;
        F.ret = t25;
        goto gm_fret;
    }
    uint64_t t26 = GM_LD(uint64_t, ((((uint8_t *)a0) + 40) + 8));
    uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
    if (t27) {
        uint64_t t28 = GM_LD(uint64_t, ((((uint8_t *)a1) + 40) + 8));
        uint8_t t29 = ((t28 == ((uint64_t)0ULL)));
        if (t29) {
            gm_l3 = ((int32_t)0LL);
        } else {
            gm_l3 = ((int32_t)-1LL);
        }
        int32_t t30 = gm_l3;
        F.ret = t30;
        goto gm_fret;
    }
    uint64_t t31 = GM_LD(uint64_t, ((((uint8_t *)a1) + 40) + 8));
    uint8_t t32 = ((t31 == ((uint64_t)0ULL)));
    if (t32) {
        F.ret = ((int32_t)1LL);
        goto gm_fret;
    }
    gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 40));
    gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 40));
    int32_t t35 = gm_f_cmp_identifiers_gvvb8ee(t33, t34);
    F.ret = t35;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) int32_t gm_f_cmp_version_0oibvro_c(gm_tu1089hhijadf a0, gm_tu1089hhijadf a1) { return gm_f_cmp_version_0oibvro(&a0, &a1); }
extern __attribute__((aligned(4))) uint8_t gm_f_first_char_2uom2hg(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_version_9a6ns50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_version_9a6ns50 F;
    gm_b16_8 gm_l0; /* text */
    gm_tf1bjg81u0glv gm_l2; /* a */
    gm_b16_8 gm_l3; /* rest */
    gm_tf1bjg81u0glv gm_l4; /* b */
    gm_tf1bjg81u0glv gm_l5; /* c */
    gm_t9qkh0rsooc8d gm_l6; /* pos */
    gm_t6j6smj7qoctu gm_l7; /* id */
    gm_t6j6smj7qoctu gm_l8; /* id */
    GM_STACK_CHECK(&F, 1291u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_trto8sim72asr gm_l9;
    gm_t3ujhu6qph7de gm_l10;
    gm_trto8sim72asr gm_l11;
    gm_t3ujhu6qph7de gm_l12;
    gm_trto8sim72asr gm_l13;
    gm_t22b1ebhvur5a gm_l14;
    gm_t22b1ebhvur5a gm_l15;
    gm_tabb710d9mq2e gm_l16;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sp51dovg85ku9));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(39));
        memset(&(*(gm_tvtl3n8qta358 *)gm_out), 0, sizeof (*(gm_tvtl3n8qta358 *)gm_out));
        ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 8), t4);
        goto gm_fdone;
    }
    {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)gm_cuqbj62gqhf));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)gm_cuqbj62gqhf_2));
        GM_ST(uint64_t, ((uint8_t *)&F.l1 + 0), ((uint64_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&F.l1 + 8), ((uint64_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&F.l1 + 16), ((uint64_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&F.l1 + 24), t5);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l1 + 40), t6);
    }
    {
        gm_b16_8 t7 = gm_l0;
        gm_t9qkh0rsooc8d t8;
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)0ULL));
        gm_f_sem_numeric_oik4dle((void *)&gm_l9, (void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t9) {
    } else {
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        memset(&(*(gm_tvtl3n8qta358 *)gm_out), 0, sizeof (*(gm_tvtl3n8qta358 *)gm_out));
        ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&gm_l2), (((uint8_t *)&gm_l9) + 8), 24);
    }
    uint64_t t11 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
    GM_ST(uint64_t, ((uint8_t *)&F.l1), t11);
    {
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
        gm_t9qkh0rsooc8d t13;
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
        gm_f_sem_dot_4gikq3f((void *)&gm_l10, (void *)NULL, cx, NULL, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (t14) {
    } else {
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
        memset(&(*(gm_tvtl3n8qta358 *)gm_out), 0, sizeof (*(gm_tvtl3n8qta358 *)gm_out));
        ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 8), t15);
        goto gm_fdone;
    }
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
    gm_l3 = t16;
    {
        gm_b16_8 t17 = gm_l3;
        gm_t9qkh0rsooc8d t18;
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)1ULL));
        gm_f_sem_numeric_oik4dle((void *)&gm_l11, (void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t19 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (t19) {
    } else {
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
        memset(&(*(gm_tvtl3n8qta358 *)gm_out), 0, sizeof (*(gm_tvtl3n8qta358 *)gm_out));
        ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 8), t20);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&gm_l4), (((uint8_t *)&gm_l11) + 8), 24);
    }
    uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
    GM_ST(uint64_t, (((uint8_t *)&F.l1) + 8), t21);
    {
        gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
        gm_t9qkh0rsooc8d t23;
        GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)1ULL));
        gm_f_sem_dot_4gikq3f((void *)&gm_l12, (void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (t24) {
    } else {
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        memset(&(*(gm_tvtl3n8qta358 *)gm_out), 0, sizeof (*(gm_tvtl3n8qta358 *)gm_out));
        ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 8), t25);
        goto gm_fdone;
    }
    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
    gm_l3 = t26;
    {
        gm_b16_8 t27 = gm_l3;
        gm_t9qkh0rsooc8d t28;
        GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)2ULL));
        gm_f_sem_numeric_oik4dle((void *)&gm_l13, (void *)NULL, cx, NULL, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (t29) {
    } else {
        gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
        memset(&(*(gm_tvtl3n8qta358 *)gm_out), 0, sizeof (*(gm_tvtl3n8qta358 *)gm_out));
        ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 8), t30);
        goto gm_fdone;
    }
    {
        memmove(((uint8_t *)&gm_l5), (((uint8_t *)&gm_l13) + 8), 24);
    }
    uint64_t t31 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
    GM_ST(uint64_t, (((uint8_t *)&F.l1) + 16), t31);
    gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_l3 = t32;
    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t34 = ((t33 == ((uint64_t)0ULL)));
    if (t34) {
        gm_tu1089hhijadf t35 = GM_LD(gm_tu1089hhijadf, ((uint8_t *)&F.l1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out))[0] = 0;
        GM_ST(gm_tu1089hhijadf, ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 8), t35);
        goto gm_fdone;
    }
    gm_t9qkh0rsooc8d t36;
    GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)2ULL));
    gm_l6 = t36;
    gm_b16_8 t37 = gm_l3;
    gm_b16_8 t38;
    GM_ST(void *, (uint8_t *)&t38, (void *)(gm_skolmnfc0ccbd));
    GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(1));
    uint8_t t39 = gm_f_starts_with_6vvt01e(t37, t38);
    if (t39) {
        gm_t9qkh0rsooc8d t40;
        GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)3ULL));
        gm_l6 = t40;
        {
            void * t41 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t43 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t44 = (uintptr_t)t42;
            if (t43 > t44 || t44 > (uintptr_t)t42) gm_trap_slice(t43, t44, t42, gm_srcij34jbkb4qlc, 153, 34);
            uint64_t t45 = ((uintptr_t)t42) - (uintptr_t)((uint64_t)1ULL);
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t41 + (uintptr_t)((uint64_t)1ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45));
            gm_t9qkh0rsooc8d t47;
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)3ULL));
            gm_f_sem_identifier_dte5vh0((void *)&gm_l14, (void *)NULL, cx, NULL, t46, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
        if (t48) {
        } else {
            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
            memset(&(*(gm_tvtl3n8qta358 *)gm_out), 0, sizeof (*(gm_tvtl3n8qta358 *)gm_out));
            ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 8), t49);
            goto gm_fdone;
        }
        {
            memmove(((uint8_t *)&gm_l7), (((uint8_t *)&gm_l14) + 8), 32);
        }
        uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
        uint8_t t51 = ((t50 == ((uint64_t)0ULL)));
        if (t51) {
            gm_b16_8 t52;
            GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s3b32la11650d));
            GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(50));
            memset(&(*(gm_tvtl3n8qta358 *)gm_out), 0, sizeof (*(gm_tvtl3n8qta358 *)gm_out));
            ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 8), t52);
            goto gm_fdone;
        }
        gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l7));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l1) + 24), t53);
        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 16));
        gm_l3 = t54;
    }
    gm_b16_8 t55 = gm_l3;
    gm_b16_8 t56;
    GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sajhum9b4nr96));
    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(1));
    uint8_t t57 = gm_f_starts_with_6vvt01e(t55, t56);
    if (t57) {
        gm_t9qkh0rsooc8d t58;
        GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)4ULL));
        gm_l6 = t58;
        {
            void * t59 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t61 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t62 = (uintptr_t)t60;
            if (t61 > t62 || t62 > (uintptr_t)t60) gm_trap_slice(t61, t62, t60, gm_srcij34jbkb4qlc, 160, 34);
            uint64_t t63 = ((uintptr_t)t60) - (uintptr_t)((uint64_t)1ULL);
            gm_b16_8 t64;
            GM_ST(void *, (uint8_t *)&t64, (void *)(((uint8_t *)t59 + (uintptr_t)((uint64_t)1ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(t63));
            gm_t9qkh0rsooc8d t65;
            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)4ULL));
            gm_f_sem_identifier_dte5vh0((void *)&gm_l15, (void *)NULL, cx, NULL, t64, t65);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
        if (t66) {
        } else {
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
            memset(&(*(gm_tvtl3n8qta358 *)gm_out), 0, sizeof (*(gm_tvtl3n8qta358 *)gm_out));
            ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 8), t67);
            goto gm_fdone;
        }
        {
            memmove(((uint8_t *)&gm_l8), (((uint8_t *)&gm_l15) + 8), 32);
        }
        uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
        uint8_t t69 = ((t68 == ((uint64_t)0ULL)));
        if (t69) {
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sviu03g0f5s30));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(42));
            memset(&(*(gm_tvtl3n8qta358 *)gm_out), 0, sizeof (*(gm_tvtl3n8qta358 *)gm_out));
            ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 8), t70);
            goto gm_fdone;
        }
        gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l1) + 40), t71);
        gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 16));
        gm_l3 = t72;
    }
    uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t74 = (t73 > ((uint64_t)0ULL));
    if (t74) {
        {
            gm_b16_8 t75 = gm_l3;
            gm_b16_8 t76 = gm_f_quoted_char_jpbulib((void *)NULL, cx, NULL, t75);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t77 = gm_l6;
            gm_b16_8 t78 = gm_f_sem_pos_name_bpi6shr(t77);
            gm_t6j6smj7qoctu t79;
            GM_ST(gm_b16_8, ((uint8_t *)&t79 + 0), t76);
            GM_ST(gm_b16_8, ((uint8_t *)&t79 + 16), t78);
            gm_f_format_744g2li((void *)&gm_l16, (void *)NULL, cx, NULL, &t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (!t80) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcij34jbkb4qlc, 165, 31);
        {
            gm_b16_8 t81 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
            gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
            memset(&(*(gm_tvtl3n8qta358 *)gm_out), 0, sizeof (*(gm_tvtl3n8qta358 *)gm_out));
            ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 8), t82);
            goto gm_fdone;
        }
    }
    {
        gm_tu1089hhijadf t83 = GM_LD(gm_tu1089hhijadf, ((uint8_t *)&F.l1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out))[0] = 0;
        GM_ST(gm_tu1089hhijadf, ((uint8_t *)&(*(gm_tvtl3n8qta358 *)gm_out) + 8), t83);
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
__attribute__((aligned(4))) void gm_f_parse_version_req_alipvfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_version_req_alipvfi F;
    gm_b16_8 gm_l0; /* input */
    gm_b16_8 gm_l1; /* text */
    gm_thervm8j23qet gm_l2; /* w */
    gm_b16_8 gm_l3; /* rest */
    uint64_t gm_l5; /* depth */
    gm_b16_8 gm_l6; /* e */
    gm_thervm8j23qet gm_l7; /* w */
    gm_b16_8 gm_l8; /* rest */
    GM_STACK_CHECK(&F, 1350u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tadokt2of3tma gm_l10;
    gm_tabb710d9mq2e gm_l11;
    gm_t88cvv9hlbqql gm_l12;
    gm_tadokt2of3tma gm_l13;
    uint8_t gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_b16_8 t9 = gm_l0;
    gm_b16_8 t10 = gm_f_trim_spaces_i65urmt(t9);
    gm_l1 = t10;
    {
        {
            {
                gm_b16_8 t11 = gm_l1;
                gm_f_sem_wildcard_15qmfol((void *)&gm_l10, t11);
            }
            uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l10))[24] != 0);
            if (!t12) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&gm_l2), ((uint8_t *)&gm_l10), 24);
            }
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
            gm_b16_8 t14 = gm_f_trim_spaces_i65urmt(t13);
            gm_l3 = t14;
            uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t16 = ((t15 == ((uint64_t)0ULL)));
            if (t16) {
                gm_trqfvmjtt4sij t17;
                gm_f_list_pf20o2d((void *)&t17, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tin97tt4ud0s1 t18;
                GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t18 + 0), t17);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 0;
                GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8), t18);
                goto gm_fdone;
            }
            gm_b16_8 t19 = gm_l3;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sc8o4oaph9gbr));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(1));
            uint8_t t21 = gm_f_starts_with_6vvt01e(t19, t20);
            if (t21) {
                {
                    void * t22 = GM_LD(void *, ((uint8_t *)&gm_l1));
                    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                    uint64_t t24 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t25 = (uintptr_t)((uint64_t)1ULL);
                    if (t24 > t25 || t25 > (uintptr_t)t23) gm_trap_slice(t24, t25, t23, gm_srcij34jbkb4qlc, 266, 100);
                    uint64_t t26 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t27;
                    GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t22 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26));
                    gm_tnpifv20irnph t28;
                    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t27);
                    gm_f_format_23lhd7n((void *)&gm_l11, (void *)NULL, cx, NULL, t28);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcij34jbkb4qlc, 266, 21);
                {
                    gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                    gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&t30));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 40, 0);
                    ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8), t31);
                    goto gm_fdone;
                }
            }
            {
                gm_b16_8 t32;
                GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sjm2vqdj9hddi));
                GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(50));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8), t32);
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t33;
        gm_f_list_pf20o2d((void *)&t33, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tin97tt4ud0s1 t34;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t34 + 0), t33);
        F.l4 = t34;
    }
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb3;
            }
            {
                {
                    gm_b16_8 t35 = gm_l1;
                    gm_f_sem_comparator_qf8qea0((void *)&gm_l12, (void *)NULL, cx, NULL, t35);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                if (t36) {
                    memmove(((uint8_t *)&F.l9), (((uint8_t *)&gm_l12) + 8), 88);
                } else {
                    gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                    gm_l6 = t37;
                    {
                        {
                            {
                                gm_b16_8 t38 = gm_l1;
                                gm_f_sem_wildcard_15qmfol((void *)&gm_l13, t38);
                            }
                            uint8_t t39 = (uint8_t)((((uint8_t *)&gm_l13))[24] != 0);
                            if (!t39) {
                                goto gm_lb7;
                            }
                            {
                                memmove(((uint8_t *)&gm_l7), ((uint8_t *)&gm_l13), 24);
                            }
                            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
                            gm_b16_8 t41 = gm_f_trim_spaces_i65urmt(t40);
                            gm_l8 = t41;
                            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                            uint8_t t43 = ((t42 == ((uint64_t)0ULL)));
                            if (t43) {
                                gm_l14 = ((uint8_t)1);
                            } else {
                                gm_b16_8 t44 = gm_l8;
                                gm_b16_8 t45;
                                GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sc8o4oaph9gbr));
                                GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(1));
                                uint8_t t46 = gm_f_starts_with_6vvt01e(t44, t45);
                                gm_l14 = t46;
                            }
                            uint8_t t47 = gm_l14;
                            if (t47) {
                                {
                                    void * t48 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                    uint64_t t49 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                    uint64_t t50 = (uintptr_t)((uint64_t)0ULL);
                                    uint64_t t51 = (uintptr_t)((uint64_t)1ULL);
                                    if (t50 > t51 || t51 > (uintptr_t)t49) gm_trap_slice(t50, t51, t49, gm_srcij34jbkb4qlc, 277, 108);
                                    uint64_t t52 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                                    gm_b16_8 t53;
                                    GM_ST(void *, (uint8_t *)&t53, (void *)(((uint8_t *)t48 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(t52));
                                    gm_tnpifv20irnph t54;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t54 + 0), t53);
                                    gm_f_format_23lhd7n((void *)&gm_l15, (void *)NULL, cx, NULL, t54);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcij34jbkb4qlc, 277, 29);
                                {
                                    gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                                    gm_b16_8 t57 = GM_LD(gm_b16_8, ((uint8_t *)&t56));
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 32, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 40, 0);
                                    ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8), t57);
                                    goto gm_fdone;
                                }
                            }
                            goto gm_lb6;
                        }
gm_lb7:;
                    }
gm_lb6:;
                    gm_b16_8 t58 = gm_l6;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 40, 0);
                    ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8), t58);
                    goto gm_fdone;
                }
                {
                    memmove(((uint8_t *)&F.l9), ((uint8_t *)&F.l9), 88);
                }
                {
                    void * t59 = (void *)((uint8_t *)&F.l4);
                    gm_tftb1odc739t5 t60 = gm_f_push_qavl6tf(t59, &(*(gm_t2pjhvnad3fjn *)((uint8_t *)&F.l9)));
                    gm_l16 = t60;
                }
                uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcij34jbkb4qlc, 282, 9);
                uint64_t t62 = GM_LD(uint64_t, ((((uint8_t *)&F.l9) + 72) + 8));
                uint8_t t63 = ((t62 == ((uint64_t)0ULL)));
                if (t63) {
                    goto gm_lb3;
                }
                gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 72));
                gm_b16_8 t65;
                GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sc8o4oaph9gbr));
                GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(1));
                uint8_t t66 = gm_f_starts_with_6vvt01e(t64, t65);
                uint8_t t67 = (uint8_t)!t66;
                if (t67) {
                    {
                        gm_t9qkh0rsooc8d t68 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l9) + 64));
                        gm_b16_8 t69 = gm_f_sem_pos_name_bpi6shr(t68);
                        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 72));
                        gm_b16_8 t71 = gm_f_quoted_char_jpbulib((void *)NULL, cx, NULL, t70);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t6j6smj7qoctu t72;
                        GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t69);
                        GM_ST(gm_b16_8, ((uint8_t *)&t72 + 16), t71);
                        gm_f_format_6gn7o2j((void *)&gm_l17, (void *)NULL, cx, NULL, &t72);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t73 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                    if (!t73) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcij34jbkb4qlc, 285, 21);
                    {
                        gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 40, 0);
                        ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8), t75);
                        goto gm_fdone;
                    }
                }
                void * t76 = GM_LD(void *, (((uint8_t *)&F.l9) + 72));
                uint64_t t77 = GM_LD(uint64_t, ((((uint8_t *)&F.l9) + 72) + 8));
                uint64_t t78 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t79 = (uintptr_t)t77;
                if (t78 > t79 || t79 > (uintptr_t)t77) gm_trap_slice(t78, t79, t77, gm_srcij34jbkb4qlc, 287, 28);
                uint64_t t80 = ((uintptr_t)t77) - (uintptr_t)((uint64_t)1ULL);
                gm_b16_8 t81;
                GM_ST(void *, (uint8_t *)&t81, (void *)(((uint8_t *)t76 + (uintptr_t)((uint64_t)1ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(t80));
                gm_b16_8 t82 = gm_f_trim_spaces_i65urmt(t81);
                gm_l1 = t82;
                uint64_t t83 = gm_l5;
                uint64_t r84; bool o85 = __builtin_add_overflow(t83, ((uint64_t)1ULL), &r84);
                if (o85) gm_trap("integer overflow in `+`", gm_srcij34jbkb4qlc, 288, 12);
                uint64_t t86 = r84;
                uint8_t t87 = ((t86 == ((uint64_t)31ULL)));
                if (t87) {
                    gm_b16_8 t88;
                    GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sua0n2tdbtos7));
                    GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(39));
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 24, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 40, 0);
                    ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8), t88);
                    goto gm_fdone;
                }
                uint64_t t89 = gm_l5;
                uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                if (o91) gm_trap("integer overflow in `+`", gm_srcij34jbkb4qlc, 289, 9);
                uint64_t t92 = r90;
                gm_l5 = t92;
            }
gm_lb5:;
        }
    }
gm_lb3:;
    {
        gm_tin97tt4ud0s1 t93 = GM_LD(gm_tin97tt4ud0s1, ((uint8_t *)&F.l4));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out))[0] = 0;
        GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&(*(gm_t62efos6ssciq *)gm_out) + 8), t93);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_quoted_char_jpbulib(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_quoted_char_jpbulib F;
    gm_b16_8 gm_l0; /* text */
    uint8_t gm_l1; /* c */
    GM_STACK_CHECK(&F, 359u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l2;
    gm_tabb710d9mq2e gm_l3;
    gm_b16_8 t10 = gm_l0;
    uint8_t t11 = gm_f_first_char_2uom2hg(t10);
    gm_l1 = t11;
    {
        {
            uint8_t t12 = gm_l1;
            uint8_t t13 = ((t12 == ((uint8_t)0ULL)));
            if (!t13) {
                goto gm_lb2;
            }
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sc1nadatuanhc));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(4));
            gm_l2 = t14;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t15 = gm_l1;
            uint8_t t16 = ((t15 == ((uint8_t)39ULL)));
            if (!t16) {
                goto gm_lb3;
            }
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s2nmq6ak9nl9d));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
            gm_l2 = t17;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t18 = gm_l1;
            uint8_t t19 = ((t18 == ((uint8_t)92ULL)));
            if (!t19) {
                goto gm_lb4;
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s4b0873bt7c1q));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(4));
            gm_l2 = t20;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t21 = gm_l1;
            uint8_t t22 = ((t21 == ((uint8_t)10ULL)));
            if (!t22) {
                goto gm_lb5;
            }
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s460n81rl2c9c));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(4));
            gm_l2 = t23;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t24 = gm_l1;
            uint8_t t25 = ((t24 == ((uint8_t)9ULL)));
            if (!t25) {
                goto gm_lb6;
            }
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sh4f06c2ecgj1));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(4));
            gm_l2 = t26;
            goto gm_lb1;
        }
gm_lb6:;
        {
            uint8_t t27 = gm_l1;
            uint8_t t28 = ((t27 == ((uint8_t)13ULL)));
            if (!t28) {
                goto gm_lb7;
            }
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(gm_setdfq42nhg1q));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(4));
            gm_l2 = t29;
            goto gm_lb1;
        }
gm_lb7:;
        {
            void * t30 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            gm_b16_8 t32 = gm_l0;
            uint64_t t33 = gm_f_char_len_9cgkjuk(t32);
            uint64_t t34 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t35 = (uintptr_t)t33;
            if (t34 > t35 || t35 > (uintptr_t)t31) gm_trap_slice(t34, t35, t31, gm_srcij34jbkb4qlc, 61, 33);
            uint64_t t36 = ((uintptr_t)t33) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(((uint8_t *)t30 + (uintptr_t)((uint64_t)0ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(t36));
            gm_tnpifv20irnph t38;
            GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t37);
            gm_f_format_3aass3a((void *)&gm_l3, (void *)NULL, cx, NULL, t38);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
            if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcij34jbkb4qlc, 61, 14);
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
            gm_l2 = t41;
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcij34jbkb4qlc, 54, 12);
    }
gm_lb1:;
    gm_b16_8 t42 = gm_l2;
    F.ret = t42;
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
inline __attribute__((aligned(4))) uint8_t gm_f_req_equal_7ia8ip6(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_req_equal_7ia8ip6 F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 492u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tlqibld3f7ga5 gm_l7;
    uint8_t gm_l8;
    uint8_t gm_l9;
    uint8_t gm_l10;
    uint8_t gm_l11;
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    void * t9 = gm_l1;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 8));
    uint8_t t11 = (!(t8 == t10));
    if (t11) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t12 = gm_l0;
    gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)t12));
    gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
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
                {
                    uint64_t t18 = gm_l3;
                    void * t19 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t21 = gm_l3;
                    gm_t2pjhvnad3fjn t22 = GM_LD(gm_t2pjhvnad3fjn, (((uint8_t *)t19) + (uintptr_t)t21 * 64));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l7 + 0), t18);
                    GM_ST(gm_t2pjhvnad3fjn, ((uint8_t *)&gm_l7 + 8), t22);
                }
                {
                    {
                        uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&gm_l7));
                        gm_l4 = t23;
                        {
                            memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l7) + 8), 64);
                        }
                        {
                            void * t24 = gm_l1;
                            void * t25 = GM_LD(void *, ((uint8_t *)t24));
                            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t24) + 8));
                            uint64_t t27 = gm_l4;
                            if ((uintptr_t)t27 >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)t27, t26, gm_srcij34jbkb4qlc, 520, 14);
                            memmove(((uint8_t *)&F.l6), (((uint8_t *)t25) + (uintptr_t)t27 * 64), 64);
                        }
                        gm_t9qkh0rsooc8d t28 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l5));
                        gm_t9qkh0rsooc8d t29 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l6));
                        uint8_t t30 = (gm_eqmqffvrbc2a(((uint8_t *)&t28), ((uint8_t *)&t29)));
                        if (t30) {
                            uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                            uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
                            uint8_t t33 = ((t31 == t32));
                            gm_l11 = t33;
                        } else {
                            gm_l11 = ((uint8_t)0);
                        }
                        uint8_t t34 = gm_l11;
                        if (t34) {
                            gm_tf5srh4mp4b3i t35 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l5) + 16));
                            gm_tf5srh4mp4b3i t36 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l6) + 16));
                            uint8_t t37 = (gm_eqmpqhhfmb90(((uint8_t *)&t35), ((uint8_t *)&t36)));
                            gm_l10 = t37;
                        } else {
                            gm_l10 = ((uint8_t)0);
                        }
                        uint8_t t38 = gm_l10;
                        if (t38) {
                            gm_tf5srh4mp4b3i t39 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l5) + 32));
                            gm_tf5srh4mp4b3i t40 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l6) + 32));
                            uint8_t t41 = (gm_eqmpqhhfmb90(((uint8_t *)&t39), ((uint8_t *)&t40)));
                            gm_l9 = t41;
                        } else {
                            gm_l9 = ((uint8_t)0);
                        }
                        uint8_t t42 = gm_l9;
                        if (t42) {
                            gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 48));
                            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 48));
                            uint8_t t45 = (gm_eq0iqiubfm7e(((uint8_t *)&t43), ((uint8_t *)&t44)));
                            gm_l8 = t45;
                        } else {
                            gm_l8 = ((uint8_t)0);
                        }
                        uint8_t t46 = gm_l8;
                        uint8_t t47 = (uint8_t)!t46;
                        if (t47) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcij34jbkb4qlc, 519, 5);
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
    F.ret = ((uint8_t)1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_req_matches_dflcrek(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_req_matches_dflcrek F;
    void * gm_l0; /* req */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 347u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t14 = gm_l0;
    gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)t14));
    gm_l2 = t15;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t16 = gm_l3;
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t18 = (t16 < t17);
            if (!t18) {
                goto gm_lb1;
            }
            {
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t21 = gm_l3;
                {
                    {
                        {
                            memmove(((uint8_t *)&F.l4), (((uint8_t *)t19) + (uintptr_t)t21 * 64), 64);
                        }
                        {
                            void * t22 = (void *)((uint8_t *)&F.l4);
                            uint8_t t23 = gm_f_cmp_matches_h8hjnru(t22, &(*(gm_tu1089hhijadf *)((uint8_t *)a1)));
                            uint8_t t24 = (uint8_t)!t23;
                            if (t24) {
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcij34jbkb4qlc, 506, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t25 = gm_l3;
            uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
            uint64_t t28 = r26;
            gm_l3 = t28;
        }
    }
gm_lb1:;
    uint64_t t29 = GM_LD(uint64_t, ((((uint8_t *)a1) + 24) + 8));
    uint8_t t30 = ((t29 == ((uint64_t)0ULL)));
    if (t30) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    void * t31 = gm_l0;
    gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)t31));
    gm_l5 = t32;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t33 = gm_l6;
            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t35 = (t33 < t34);
            if (!t35) {
                goto gm_lb6;
            }
            {
                void * t36 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t38 = gm_l6;
                {
                    {
                        memmove(((uint8_t *)&F.l7), (((uint8_t *)t36) + (uintptr_t)t38 * 64), 64);
                        {
                            {
                                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 8));
                                uint64_t t40 = GM_LD(uint64_t, ((uint8_t *)a1));
                                uint8_t t41 = ((t39 == t40));
                                if (!t41) {
                                    goto gm_lb12;
                                }
                                gm_tf5srh4mp4b3i t42 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l7) + 16));
                                uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
                                gm_tf5srh4mp4b3i t44;
                                GM_ST(uint8_t, (uint8_t *)&t44 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t44 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t44 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t44), t43);
                                ((uint8_t *)&t44)[8] = 1;
                                uint8_t t45 = (gm_eqmpqhhfmb90(((uint8_t *)&t42), ((uint8_t *)&t44)));
                                if (!t45) {
                                    goto gm_lb12;
                                }
                                gm_tf5srh4mp4b3i t46 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l7) + 32));
                                uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
                                gm_tf5srh4mp4b3i t48;
                                GM_ST(uint8_t, (uint8_t *)&t48 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t48 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t48 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t48), t47);
                                ((uint8_t *)&t48)[8] = 1;
                                uint8_t t49 = (gm_eqmpqhhfmb90(((uint8_t *)&t46), ((uint8_t *)&t48)));
                                if (!t49) {
                                    goto gm_lb12;
                                }
                                uint64_t t50 = GM_LD(uint64_t, ((((uint8_t *)&F.l7) + 48) + 8));
                                uint8_t t51 = (t50 > ((uint64_t)0ULL));
                                if (!t51) {
                                    goto gm_lb12;
                                }
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
gm_lb12:;
                        }
gm_lb11:;
                        goto gm_lb9;
                    }
gm_lb10:;
                    gm_trap("no match arm matched the scrutinee", gm_srcij34jbkb4qlc, 510, 5);
                }
gm_lb9:;
            }
gm_lb8:;
            uint64_t t52 = gm_l6;
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            uint64_t t55 = r53;
            gm_l6 = t55;
        }
    }
gm_lb6:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_req_matches_dflcrek_c(void * a0, gm_tu1089hhijadf a1) { return gm_f_req_matches_dflcrek(a0, &a1); }
__attribute__((aligned(4))) gm_b16_8 gm_f_req_string_fk059jr(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_req_string_fk059jr F;
    void * gm_l0; /* req */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint64_t gm_l6; /* m */
    uint64_t gm_l7; /* p */
    GM_STACK_CHECK(&F, 894u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tlqibld3f7ga5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_b16_8 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    void * t20 = gm_l0;
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)t20) + 8));
    uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
    if (t22) {
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s6s96daqmhcs9));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(1));
        F.ret = t23;
        goto gm_fret;
    }
    gm_trqfvmjtt4sij t24;
    gm_f_list_uvfu1k1((void *)&t24, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t24;
    void * t25 = gm_l0;
    gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)t25));
    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
    gm_l2 = t27;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t28 = gm_l3;
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t30 = (t28 < t29);
            if (!t30) {
                goto gm_lb1;
            }
            {
                {
                    uint64_t t31 = gm_l3;
                    void * t32 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t34 = gm_l3;
                    gm_t2pjhvnad3fjn t35 = GM_LD(gm_t2pjhvnad3fjn, (((uint8_t *)t32) + (uintptr_t)t34 * 64));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l8 + 0), t31);
                    GM_ST(gm_t2pjhvnad3fjn, ((uint8_t *)&gm_l8 + 8), t35);
                }
                {
                    {
                        uint64_t t36 = GM_LD(uint64_t, ((uint8_t *)&gm_l8));
                        gm_l4 = t36;
                        memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l8) + 8), 64);
                        uint64_t t37 = gm_l4;
                        uint8_t t38 = (t37 > ((uint64_t)0ULL));
                        if (t38) {
                            void * t39 = (void *)((uint8_t *)&F.l1);
                            gm_b16_8 t40;
                            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t41 = gm_f_extend_0cs85gs(t39, t40);
                            gm_l9 = t41;
                            uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                            if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcij34jbkb4qlc, 312, 20);
                        }
                        void * t43 = (void *)((uint8_t *)&F.l1);
                        {
                            {
                                uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t45 = (uint8_t)(t44 == ((uint8_t)0ULL));
                                if (!t45) {
                                    goto gm_lb7;
                                }
                                gm_b16_8 t46;
                                GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sh0j2ggs1vvoa));
                                GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(1));
                                gm_l10 = t46;
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t48 = (uint8_t)(t47 == ((uint8_t)1ULL));
                                if (!t48) {
                                    goto gm_lb8;
                                }
                                gm_b16_8 t49;
                                GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sm5be4659ulci));
                                GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(1));
                                gm_l10 = t49;
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t51 = (uint8_t)(t50 == ((uint8_t)2ULL));
                                if (!t51) {
                                    goto gm_lb9;
                                }
                                gm_b16_8 t52;
                                GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s02ru83e3fffc));
                                GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(2));
                                gm_l10 = t52;
                                goto gm_lb6;
                            }
gm_lb9:;
                            {
                                uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t54 = (uint8_t)(t53 == ((uint8_t)3ULL));
                                if (!t54) {
                                    goto gm_lb10;
                                }
                                gm_b16_8 t55;
                                GM_ST(void *, (uint8_t *)&t55, (void *)(gm_s1f440st4mnkp));
                                GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(1));
                                gm_l10 = t55;
                                goto gm_lb6;
                            }
gm_lb10:;
                            {
                                uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t57 = (uint8_t)(t56 == ((uint8_t)4ULL));
                                if (!t57) {
                                    goto gm_lb11;
                                }
                                gm_b16_8 t58;
                                GM_ST(void *, (uint8_t *)&t58, (void *)(gm_s4g3cajt15dmi));
                                GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(2));
                                gm_l10 = t58;
                                goto gm_lb6;
                            }
gm_lb11:;
                            {
                                uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t60 = (uint8_t)(t59 == ((uint8_t)5ULL));
                                if (!t60) {
                                    goto gm_lb12;
                                }
                                gm_b16_8 t61;
                                GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sa831sfid0hf3));
                                GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(1));
                                gm_l10 = t61;
                                goto gm_lb6;
                            }
gm_lb12:;
                            {
                                uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t63 = (uint8_t)(t62 == ((uint8_t)6ULL));
                                if (!t63) {
                                    goto gm_lb13;
                                }
                                gm_b16_8 t64;
                                GM_ST(void *, (uint8_t *)&t64, (void *)(gm_srs1vrn2uaams));
                                GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(1));
                                gm_l10 = t64;
                                goto gm_lb6;
                            }
gm_lb13:;
                            {
                                uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                                uint8_t t66 = (uint8_t)(t65 == ((uint8_t)7ULL));
                                if (!t66) {
                                    goto gm_lb14;
                                }
                                gm_b16_8 t67;
                                GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(0));
                                gm_l10 = t67;
                                goto gm_lb6;
                            }
gm_lb14:;
                            gm_trap("no match arm matched the scrutinee", gm_srcij34jbkb4qlc, 313, 20);
                        }
gm_lb6:;
                        gm_b16_8 t68 = gm_l10;
                        gm_tftb1odc739t5 t69 = gm_f_extend_0cs85gs(t43, t68);
                        gm_l11 = t69;
                        uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcij34jbkb4qlc, 313, 9);
                        void * t71 = (void *)((uint8_t *)&F.l1);
                        uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                        gm_t4ce0jd3tdro4 t73;
                        GM_ST(uint64_t, ((uint8_t *)&t73 + 0), t72);
                        gm_tftb1odc739t5 t74 = gm_f_write_apgl18i(t71, t73);
                        gm_l12 = t74;
                        uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t75) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcij34jbkb4qlc, 323, 9);
                        {
                            {
                                uint8_t t76 = (uint8_t)(((((uint8_t *)&F.l5) + 16))[8] != 0);
                                if (!t76) {
                                    goto gm_lb16;
                                }
                                uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 16));
                                gm_l6 = t77;
                                void * t78 = (void *)((uint8_t *)&F.l1);
                                uint64_t t79 = gm_l6;
                                gm_t4ce0jd3tdro4 t80;
                                GM_ST(uint64_t, ((uint8_t *)&t80 + 0), t79);
                                gm_tftb1odc739t5 t81 = gm_f_write_mlkl1uj(t78, t80);
                                gm_l13 = t81;
                                uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcij34jbkb4qlc, 325, 13);
                                {
                                    {
                                        uint8_t t83 = (uint8_t)(((((uint8_t *)&F.l5) + 32))[8] != 0);
                                        if (!t83) {
                                            goto gm_lb18;
                                        }
                                        uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 32));
                                        gm_l7 = t84;
                                        void * t85 = (void *)((uint8_t *)&F.l1);
                                        uint64_t t86 = gm_l7;
                                        gm_t4ce0jd3tdro4 t87;
                                        GM_ST(uint64_t, ((uint8_t *)&t87 + 0), t86);
                                        gm_tftb1odc739t5 t88 = gm_f_write_mlkl1uj(t85, t87);
                                        gm_l14 = t88;
                                        uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                                        if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_srcij34jbkb4qlc, 327, 17);
                                        uint64_t t90 = GM_LD(uint64_t, ((((uint8_t *)&F.l5) + 48) + 8));
                                        uint8_t t91 = (t90 > ((uint64_t)0ULL));
                                        if (t91) {
                                            void * t92 = (void *)((uint8_t *)&F.l1);
                                            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 48));
                                            gm_tnpifv20irnph t94;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t94 + 0), t93);
                                            gm_tftb1odc739t5 t95 = gm_f_write_tmm3c98(t92, t94);
                                            gm_l15 = t95;
                                            uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                            if (!t96) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcij34jbkb4qlc, 328, 36);
                                        }
                                        goto gm_lb17;
                                    }
gm_lb18:;
                                    gm_t9qkh0rsooc8d t97 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l5));
                                    uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&t97) + 0));
                                    uint8_t t99 = (uint8_t)((t98 == ((uint8_t)7ULL)));
                                    gm_t9qkh0rsooc8d t100;
                                    GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)7ULL));
                                    if (t99) {
                                        void * t101 = (void *)((uint8_t *)&F.l1);
                                        gm_b16_8 t102;
                                        GM_ST(void *, (uint8_t *)&t102, (void *)(gm_sn7tl9jolqoj1));
                                        GM_ST(uintptr_t, (uint8_t *)&t102 + 8, (uintptr_t)(2));
                                        gm_tftb1odc739t5 t103 = gm_f_extend_0cs85gs(t101, t102);
                                        gm_l16 = t103;
                                        uint8_t t104 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                                        if (!t104) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcij34jbkb4qlc, 330, 17);
                                    }
                                }
gm_lb17:;
                                goto gm_lb15;
                            }
gm_lb16:;
                            gm_t9qkh0rsooc8d t105 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l5));
                            uint8_t t106 = GM_LD(uint8_t, (((uint8_t *)&t105) + 0));
                            uint8_t t107 = (uint8_t)((t106 == ((uint8_t)7ULL)));
                            gm_t9qkh0rsooc8d t108;
                            GM_ST(uint8_t, ((uint8_t *)&t108 + 0), ((uint8_t)7ULL));
                            if (t107) {
                                void * t109 = (void *)((uint8_t *)&F.l1);
                                gm_b16_8 t110;
                                GM_ST(void *, (uint8_t *)&t110, (void *)(gm_sn7tl9jolqoj1));
                                GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t111 = gm_f_extend_0cs85gs(t109, t110);
                                gm_l17 = t111;
                                uint8_t t112 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                if (!t112) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcij34jbkb4qlc, 333, 13);
                            }
                        }
gm_lb15:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcij34jbkb4qlc, 311, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t113 = gm_l3;
            uint64_t r114; bool o115 = __builtin_add_overflow(t113, ((uint64_t)1ULL), &r114);
            uint64_t t116 = r114;
            gm_l3 = t116;
        }
    }
gm_lb1:;
    gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
    F.ret = t118;
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
__attribute__((aligned(4))) void gm_f_sem_comparator_qf8qea0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_sem_comparator_qf8qea0 F;
    gm_b16_8 gm_l0; /* input */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    gm_b16_8 gm_l2; /* text */
    uint8_t gm_l3; /* default_op */
    gm_t9qkh0rsooc8d gm_l4; /* pos */
    gm_tf1bjg81u0glv gm_l5; /* major */
    uint8_t gm_l6; /* has_wildcard */
    gm_tf5srh4mp4b3i gm_l7; /* minor */
    gm_tf5srh4mp4b3i gm_l8; /* patch */
    gm_thervm8j23qet gm_l9; /* w */
    gm_tf1bjg81u0glv gm_l10; /* m */
    gm_thervm8j23qet gm_l11; /* w */
    gm_tf1bjg81u0glv gm_l12; /* p */
    gm_b16_8 gm_l13; /* pre */
    gm_t6j6smj7qoctu gm_l14; /* id */
    gm_t6j6smj7qoctu gm_l15; /* id */
    GM_STACK_CHECK(&F, 2295u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_trto8sim72asr gm_l16;
    gm_tadokt2of3tma gm_l17;
    gm_trto8sim72asr gm_l18;
    gm_tadokt2of3tma gm_l19;
    gm_trto8sim72asr gm_l20;
    uint8_t gm_l21;
    gm_t22b1ebhvur5a gm_l22;
    uint8_t gm_l23;
    gm_t22b1ebhvur5a gm_l24;
    {
        gm_t9qkh0rsooc8d t16;
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)6ULL));
        gm_l1 = t16;
        gm_b16_8 t17 = gm_l0;
        gm_l2 = t17;
        gm_b16_8 t18 = gm_l0;
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sh0j2ggs1vvoa));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(1));
        uint8_t t20 = gm_f_starts_with_6vvt01e(t18, t19);
        if (t20) {
            gm_t9qkh0rsooc8d t21;
            GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)0ULL));
            gm_l1 = t21;
            void * t22 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t24 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t25 = (uintptr_t)t23;
            if (t24 > t25 || t25 > (uintptr_t)t23) gm_trap_slice(t24, t25, t23, gm_srcij34jbkb4qlc, 187, 16);
            uint64_t t26 = ((uintptr_t)t23) - (uintptr_t)((uint64_t)1ULL);
            gm_b16_8 t27;
            GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t22 + (uintptr_t)((uint64_t)1ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26));
            gm_l2 = t27;
        } else {
            gm_b16_8 t28 = gm_l0;
            gm_b16_8 t29;
            GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s02ru83e3fffc));
            GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(2));
            uint8_t t30 = gm_f_starts_with_6vvt01e(t28, t29);
            if (t30) {
                gm_t9qkh0rsooc8d t31;
                GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)2ULL));
                gm_l1 = t31;
                void * t32 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t34 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t35 = (uintptr_t)t33;
                if (t34 > t35 || t35 > (uintptr_t)t33) gm_trap_slice(t34, t35, t33, gm_srcij34jbkb4qlc, 190, 16);
                uint64_t t36 = ((uintptr_t)t33) - (uintptr_t)((uint64_t)2ULL);
                gm_b16_8 t37;
                GM_ST(void *, (uint8_t *)&t37, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)2ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(t36));
                gm_l2 = t37;
            } else {
                gm_b16_8 t38 = gm_l0;
                gm_b16_8 t39;
                GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sm5be4659ulci));
                GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(1));
                uint8_t t40 = gm_f_starts_with_6vvt01e(t38, t39);
                if (t40) {
                    gm_t9qkh0rsooc8d t41;
                    GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)1ULL));
                    gm_l1 = t41;
                    void * t42 = GM_LD(void *, ((uint8_t *)&gm_l0));
                    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                    uint64_t t44 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t45 = (uintptr_t)t43;
                    if (t44 > t45 || t45 > (uintptr_t)t43) gm_trap_slice(t44, t45, t43, gm_srcij34jbkb4qlc, 193, 16);
                    uint64_t t46 = ((uintptr_t)t43) - (uintptr_t)((uint64_t)1ULL);
                    gm_b16_8 t47;
                    GM_ST(void *, (uint8_t *)&t47, (void *)(((uint8_t *)t42 + (uintptr_t)((uint64_t)1ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(t46));
                    gm_l2 = t47;
                } else {
                    gm_b16_8 t48 = gm_l0;
                    gm_b16_8 t49;
                    GM_ST(void *, (uint8_t *)&t49, (void *)(gm_s4g3cajt15dmi));
                    GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(2));
                    uint8_t t50 = gm_f_starts_with_6vvt01e(t48, t49);
                    if (t50) {
                        gm_t9qkh0rsooc8d t51;
                        GM_ST(uint8_t, ((uint8_t *)&t51 + 0), ((uint8_t)4ULL));
                        gm_l1 = t51;
                        void * t52 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t54 = (uintptr_t)((uint64_t)2ULL);
                        uint64_t t55 = (uintptr_t)t53;
                        if (t54 > t55 || t55 > (uintptr_t)t53) gm_trap_slice(t54, t55, t53, gm_srcij34jbkb4qlc, 196, 16);
                        uint64_t t56 = ((uintptr_t)t53) - (uintptr_t)((uint64_t)2ULL);
                        gm_b16_8 t57;
                        GM_ST(void *, (uint8_t *)&t57, (void *)(((uint8_t *)t52 + (uintptr_t)((uint64_t)2ULL) * 1)));
                        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(t56));
                        gm_l2 = t57;
                    } else {
                        gm_b16_8 t58 = gm_l0;
                        gm_b16_8 t59;
                        GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s1f440st4mnkp));
                        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(1));
                        uint8_t t60 = gm_f_starts_with_6vvt01e(t58, t59);
                        if (t60) {
                            gm_t9qkh0rsooc8d t61;
                            GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)3ULL));
                            gm_l1 = t61;
                            void * t62 = GM_LD(void *, ((uint8_t *)&gm_l0));
                            uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint64_t t64 = (uintptr_t)((uint64_t)1ULL);
                            uint64_t t65 = (uintptr_t)t63;
                            if (t64 > t65 || t65 > (uintptr_t)t63) gm_trap_slice(t64, t65, t63, gm_srcij34jbkb4qlc, 199, 16);
                            uint64_t t66 = ((uintptr_t)t63) - (uintptr_t)((uint64_t)1ULL);
                            gm_b16_8 t67;
                            GM_ST(void *, (uint8_t *)&t67, (void *)(((uint8_t *)t62 + (uintptr_t)((uint64_t)1ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(t66));
                            gm_l2 = t67;
                        } else {
                            gm_b16_8 t68 = gm_l0;
                            gm_b16_8 t69;
                            GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sa831sfid0hf3));
                            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(1));
                            uint8_t t70 = gm_f_starts_with_6vvt01e(t68, t69);
                            if (t70) {
                                gm_t9qkh0rsooc8d t71;
                                GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)5ULL));
                                gm_l1 = t71;
                                void * t72 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t74 = (uintptr_t)((uint64_t)1ULL);
                                uint64_t t75 = (uintptr_t)t73;
                                if (t74 > t75 || t75 > (uintptr_t)t73) gm_trap_slice(t74, t75, t73, gm_srcij34jbkb4qlc, 202, 16);
                                uint64_t t76 = ((uintptr_t)t73) - (uintptr_t)((uint64_t)1ULL);
                                gm_b16_8 t77;
                                GM_ST(void *, (uint8_t *)&t77, (void *)(((uint8_t *)t72 + (uintptr_t)((uint64_t)1ULL) * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(t76));
                                gm_l2 = t77;
                            } else {
                                gm_b16_8 t78 = gm_l0;
                                gm_b16_8 t79;
                                GM_ST(void *, (uint8_t *)&t79, (void *)(gm_srs1vrn2uaams));
                                GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(1));
                                uint8_t t80 = gm_f_starts_with_6vvt01e(t78, t79);
                                if (t80) {
                                    void * t81 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                    uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                    uint64_t t83 = (uintptr_t)((uint64_t)1ULL);
                                    uint64_t t84 = (uintptr_t)t82;
                                    if (t83 > t84 || t84 > (uintptr_t)t82) gm_trap_slice(t83, t84, t82, gm_srcij34jbkb4qlc, 204, 16);
                                    uint64_t t85 = ((uintptr_t)t82) - (uintptr_t)((uint64_t)1ULL);
                                    gm_b16_8 t86;
                                    GM_ST(void *, (uint8_t *)&t86, (void *)(((uint8_t *)t81 + (uintptr_t)((uint64_t)1ULL) * 1)));
                                    GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(t85));
                                    gm_l2 = t86;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    {
        uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint8_t t89 = ((t87 == t88));
        gm_l3 = t89;
        gm_b16_8 t90 = gm_l2;
        gm_b16_8 t91 = gm_f_trim_spaces_i65urmt(t90);
        gm_l2 = t91;
        gm_t9qkh0rsooc8d t92;
        GM_ST(uint8_t, ((uint8_t *)&t92 + 0), ((uint8_t)0ULL));
        gm_l4 = t92;
        gm_b16_8 t93 = gm_l2;
        gm_t9qkh0rsooc8d t94 = gm_l4;
        gm_f_sem_numeric_oik4dle((void *)&gm_l16, (void *)NULL, cx, NULL, t93, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (t95) {
        } else {
            gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
            memset(&(*(gm_t88cvv9hlbqql *)gm_out), 0, sizeof (*(gm_t88cvv9hlbqql *)gm_out));
            ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out) + 8), t96);
            goto gm_fdone;
        }
        memmove(((uint8_t *)&gm_l5), (((uint8_t *)&gm_l16) + 8), 24);
        gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
        gm_l2 = t97;
        gm_l6 = ((uint8_t)0);
        gm_tf5srh4mp4b3i t98;
        memset(&t98, 0, sizeof t98);
        gm_l7 = t98;
        gm_tf5srh4mp4b3i t99;
        memset(&t99, 0, sizeof t99);
        gm_l8 = t99;
        gm_b16_8 t100 = gm_l2;
        gm_b16_8 t101;
        GM_ST(void *, (uint8_t *)&t101, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(1));
        uint8_t t102 = gm_f_starts_with_6vvt01e(t100, t101);
        if (t102) {
            void * t103 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t105 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t106 = (uintptr_t)t104;
            if (t105 > t106 || t106 > (uintptr_t)t104) gm_trap_slice(t105, t106, t104, gm_srcij34jbkb4qlc, 215, 16);
            uint64_t t107 = ((uintptr_t)t104) - (uintptr_t)((uint64_t)1ULL);
            gm_b16_8 t108;
            GM_ST(void *, (uint8_t *)&t108, (void *)(((uint8_t *)t103 + (uintptr_t)((uint64_t)1ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(t107));
            gm_l2 = t108;
            gm_t9qkh0rsooc8d t109;
            GM_ST(uint8_t, ((uint8_t *)&t109 + 0), ((uint8_t)1ULL));
            gm_l4 = t109;
            {
                {
                    {
                        gm_b16_8 t110 = gm_l2;
                        gm_f_sem_wildcard_15qmfol((void *)&gm_l17, t110);
                    }
                    uint8_t t111 = (uint8_t)((((uint8_t *)&gm_l17))[24] != 0);
                    if (!t111) {
                        goto gm_lb2;
                    }
                    {
                        memmove(((uint8_t *)&gm_l9), ((uint8_t *)&gm_l17), 24);
                    }
                    gm_l6 = ((uint8_t)1);
                    uint8_t t112 = gm_l3;
                    if (t112) {
                        gm_t9qkh0rsooc8d t113;
                        GM_ST(uint8_t, ((uint8_t *)&t113 + 0), ((uint8_t)7ULL));
                        gm_l1 = t113;
                    }
                    gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
                    gm_l2 = t114;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    gm_b16_8 t115 = gm_l2;
                    gm_t9qkh0rsooc8d t116 = gm_l4;
                    gm_f_sem_numeric_oik4dle((void *)&gm_l18, (void *)NULL, cx, NULL, t115, t116);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t117 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                if (t117) {
                } else {
                    gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
                    memset(&(*(gm_t88cvv9hlbqql *)gm_out), 0, sizeof (*(gm_t88cvv9hlbqql *)gm_out));
                    ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out) + 8), t118);
                    goto gm_fdone;
                }
                {
                    memmove(((uint8_t *)&gm_l10), (((uint8_t *)&gm_l18) + 8), 24);
                }
                uint64_t t119 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                gm_tf5srh4mp4b3i t120;
                GM_ST(uint8_t, (uint8_t *)&t120 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t120 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t120 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t120), t119);
                ((uint8_t *)&t120)[8] = 1;
                gm_l7 = t120;
                gm_b16_8 t121 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                gm_l2 = t121;
            }
gm_lb1:;
        }
    }
    {
        gm_b16_8 t122 = gm_l2;
        gm_b16_8 t123;
        GM_ST(void *, (uint8_t *)&t123, (void *)(gm_sl3q52ep6aa53));
        GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(1));
        uint8_t t124 = gm_f_starts_with_6vvt01e(t122, t123);
        if (t124) {
            void * t125 = GM_LD(void *, ((uint8_t *)&gm_l2));
            uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint64_t t127 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t128 = (uintptr_t)t126;
            if (t127 > t128 || t128 > (uintptr_t)t126) gm_trap_slice(t127, t128, t126, gm_srcij34jbkb4qlc, 228, 16);
            uint64_t t129 = ((uintptr_t)t126) - (uintptr_t)((uint64_t)1ULL);
            gm_b16_8 t130;
            GM_ST(void *, (uint8_t *)&t130, (void *)(((uint8_t *)t125 + (uintptr_t)((uint64_t)1ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(t129));
            gm_l2 = t130;
            gm_t9qkh0rsooc8d t131;
            GM_ST(uint8_t, ((uint8_t *)&t131 + 0), ((uint8_t)2ULL));
            gm_l4 = t131;
            {
                {
                    {
                        gm_b16_8 t132 = gm_l2;
                        gm_f_sem_wildcard_15qmfol((void *)&gm_l19, t132);
                    }
                    uint8_t t133 = (uint8_t)((((uint8_t *)&gm_l19))[24] != 0);
                    if (!t133) {
                        goto gm_lb4;
                    }
                    {
                        memmove(((uint8_t *)&gm_l11), ((uint8_t *)&gm_l19), 24);
                    }
                    uint8_t t134 = gm_l3;
                    if (t134) {
                        gm_t9qkh0rsooc8d t135;
                        GM_ST(uint8_t, ((uint8_t *)&t135 + 0), ((uint8_t)7ULL));
                        gm_l1 = t135;
                    }
                    gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                    gm_l2 = t136;
                    goto gm_lb3;
                }
gm_lb4:;
                uint8_t t137 = gm_l6;
                if (t137) {
                    gm_b16_8 t138;
                    GM_ST(void *, (uint8_t *)&t138, (void *)(gm_sjm2vqdj9hddi));
                    GM_ST(uintptr_t, (uint8_t *)&t138 + 8, (uintptr_t)(50));
                    memset(&(*(gm_t88cvv9hlbqql *)gm_out), 0, sizeof (*(gm_t88cvv9hlbqql *)gm_out));
                    ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out) + 8), t138);
                    goto gm_fdone;
                } else {
                    {
                        gm_b16_8 t139 = gm_l2;
                        gm_t9qkh0rsooc8d t140 = gm_l4;
                        gm_f_sem_numeric_oik4dle((void *)&gm_l20, (void *)NULL, cx, NULL, t139, t140);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t141 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                    if (t141) {
                    } else {
                        gm_b16_8 t142 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                        memset(&(*(gm_t88cvv9hlbqql *)gm_out), 0, sizeof (*(gm_t88cvv9hlbqql *)gm_out));
                        ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out))[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out) + 8), t142);
                        goto gm_fdone;
                    }
                    {
                        memmove(((uint8_t *)&gm_l12), (((uint8_t *)&gm_l20) + 8), 24);
                    }
                    uint64_t t143 = GM_LD(uint64_t, ((uint8_t *)&gm_l12));
                    gm_tf5srh4mp4b3i t144;
                    GM_ST(uint8_t, (uint8_t *)&t144 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t144 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t144 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t144), t143);
                    ((uint8_t *)&t144)[8] = 1;
                    gm_l8 = t144;
                    gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                    gm_l2 = t145;
                }
            }
gm_lb3:;
        }
        gm_b16_8 t146;
        GM_ST(void *, (uint8_t *)&t146, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(0));
        gm_l13 = t146;
        {
            {
                {
                    {
                        uint8_t t147 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
                        if (t147) {
                            goto gm_lb8;
                        }
                        gm_l21 = ((uint8_t)0);
                        goto gm_lb7;
                    }
gm_lb8:;
                    {
                        gm_l21 = ((uint8_t)1);
                        goto gm_lb7;
                    }
gm_lb9:;
                    gm_trap("no match arm matched the scrutinee", gm_srcij34jbkb4qlc, 242, 8);
                }
gm_lb7:;
                uint8_t t148 = gm_l21;
                if (!t148) {
                    goto gm_lb6;
                }
                gm_b16_8 t149 = gm_l2;
                gm_b16_8 t150;
                GM_ST(void *, (uint8_t *)&t150, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(1));
                uint8_t t151 = gm_f_starts_with_6vvt01e(t149, t150);
                if (!t151) {
                    goto gm_lb6;
                }
                gm_t9qkh0rsooc8d t152;
                GM_ST(uint8_t, ((uint8_t *)&t152 + 0), ((uint8_t)3ULL));
                gm_l4 = t152;
                {
                    void * t153 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t155 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t156 = (uintptr_t)t154;
                    if (t155 > t156 || t156 > (uintptr_t)t154) gm_trap_slice(t155, t156, t154, gm_srcij34jbkb4qlc, 244, 34);
                    uint64_t t157 = ((uintptr_t)t154) - (uintptr_t)((uint64_t)1ULL);
                    gm_b16_8 t158;
                    GM_ST(void *, (uint8_t *)&t158, (void *)(((uint8_t *)t153 + (uintptr_t)((uint64_t)1ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t158 + 8, (uintptr_t)(t157));
                    gm_t9qkh0rsooc8d t159;
                    GM_ST(uint8_t, ((uint8_t *)&t159 + 0), ((uint8_t)3ULL));
                    gm_f_sem_identifier_dte5vh0((void *)&gm_l22, (void *)NULL, cx, NULL, t158, t159);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t160 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                if (t160) {
                } else {
                    gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                    memset(&(*(gm_t88cvv9hlbqql *)gm_out), 0, sizeof (*(gm_t88cvv9hlbqql *)gm_out));
                    ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out) + 8), t161);
                    goto gm_fdone;
                }
                {
                    memmove(((uint8_t *)&gm_l14), (((uint8_t *)&gm_l22) + 8), 32);
                }
                uint64_t t162 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint8_t t163 = ((t162 == ((uint64_t)0ULL)));
                if (t163) {
                    gm_b16_8 t164;
                    GM_ST(void *, (uint8_t *)&t164, (void *)(gm_s3b32la11650d));
                    GM_ST(uintptr_t, (uint8_t *)&t164 + 8, (uintptr_t)(50));
                    memset(&(*(gm_t88cvv9hlbqql *)gm_out), 0, sizeof (*(gm_t88cvv9hlbqql *)gm_out));
                    ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out) + 8), t164);
                    goto gm_fdone;
                }
                gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
                gm_l13 = t165;
                gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 16));
                gm_l2 = t166;
                goto gm_lb5;
            }
gm_lb6:;
        }
gm_lb5:;
    }
    {
        {
            {
                {
                    {
                        uint8_t t167 = (uint8_t)((((uint8_t *)&gm_l8))[8] != 0);
                        if (t167) {
                            goto gm_lb13;
                        }
                        gm_l23 = ((uint8_t)0);
                        goto gm_lb12;
                    }
gm_lb13:;
                    {
                        gm_l23 = ((uint8_t)1);
                        goto gm_lb12;
                    }
gm_lb14:;
                    gm_trap("no match arm matched the scrutinee", gm_srcij34jbkb4qlc, 249, 8);
                }
gm_lb12:;
                uint8_t t168 = gm_l23;
                if (!t168) {
                    goto gm_lb11;
                }
                gm_b16_8 t169 = gm_l2;
                gm_b16_8 t170;
                GM_ST(void *, (uint8_t *)&t170, (void *)(gm_sajhum9b4nr96));
                GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(1));
                uint8_t t171 = gm_f_starts_with_6vvt01e(t169, t170);
                if (!t171) {
                    goto gm_lb11;
                }
                gm_t9qkh0rsooc8d t172;
                GM_ST(uint8_t, ((uint8_t *)&t172 + 0), ((uint8_t)4ULL));
                gm_l4 = t172;
                {
                    void * t173 = GM_LD(void *, ((uint8_t *)&gm_l2));
                    uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    uint64_t t175 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t176 = (uintptr_t)t174;
                    if (t175 > t176 || t176 > (uintptr_t)t174) gm_trap_slice(t175, t176, t174, gm_srcij34jbkb4qlc, 251, 34);
                    uint64_t t177 = ((uintptr_t)t174) - (uintptr_t)((uint64_t)1ULL);
                    gm_b16_8 t178;
                    GM_ST(void *, (uint8_t *)&t178, (void *)(((uint8_t *)t173 + (uintptr_t)((uint64_t)1ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(t177));
                    gm_t9qkh0rsooc8d t179;
                    GM_ST(uint8_t, ((uint8_t *)&t179 + 0), ((uint8_t)4ULL));
                    gm_f_sem_identifier_dte5vh0((void *)&gm_l24, (void *)NULL, cx, NULL, t178, t179);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t180 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                if (t180) {
                } else {
                    gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
                    memset(&(*(gm_t88cvv9hlbqql *)gm_out), 0, sizeof (*(gm_t88cvv9hlbqql *)gm_out));
                    ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out) + 8), t181);
                    goto gm_fdone;
                }
                {
                    memmove(((uint8_t *)&gm_l15), (((uint8_t *)&gm_l24) + 8), 32);
                }
                uint64_t t182 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                uint8_t t183 = ((t182 == ((uint64_t)0ULL)));
                if (t183) {
                    gm_b16_8 t184;
                    GM_ST(void *, (uint8_t *)&t184, (void *)(gm_sviu03g0f5s30));
                    GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(42));
                    memset(&(*(gm_t88cvv9hlbqql *)gm_out), 0, sizeof (*(gm_t88cvv9hlbqql *)gm_out));
                    ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out))[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out) + 8), t184);
                    goto gm_fdone;
                }
                gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 16));
                gm_l2 = t185;
                goto gm_lb10;
            }
gm_lb11:;
        }
gm_lb10:;
        gm_b16_8 t186 = gm_l2;
        gm_b16_8 t187 = gm_f_trim_spaces_i65urmt(t186);
        gm_l2 = t187;
        gm_t9qkh0rsooc8d t188 = gm_l1;
        uint64_t t189 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
        gm_tf5srh4mp4b3i t190 = gm_l7;
        gm_tf5srh4mp4b3i t191 = gm_l8;
        gm_b16_8 t192 = gm_l13;
        gm_t2pjhvnad3fjn t193;
        GM_ST(uint8_t, (uint8_t *)&t193 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t193 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t193 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t193 + 0), t188);
        GM_ST(uint64_t, ((uint8_t *)&t193 + 8), t189);
        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t193 + 16), t190);
        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t193 + 32), t191);
        GM_ST(gm_b16_8, ((uint8_t *)&t193 + 48), t192);
        gm_t9qkh0rsooc8d t194 = gm_l4;
        gm_b16_8 t195 = gm_l2;
        gm_t3dk6o644dajt t196;
        GM_ST(uint8_t, (uint8_t *)&t196 + 65, 0);
        GM_ST(uint16_t, (uint8_t *)&t196 + 66, 0);
        GM_ST(uint32_t, (uint8_t *)&t196 + 68, 0);
        GM_ST(gm_t2pjhvnad3fjn, ((uint8_t *)&t196 + 0), t193);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t196 + 64), t194);
        GM_ST(gm_b16_8, ((uint8_t *)&t196 + 72), t195);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out))[0] = 0;
        GM_ST(gm_t3dk6o644dajt, ((uint8_t *)&(*(gm_t88cvv9hlbqql *)gm_out) + 8), t196);
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
inline __attribute__((aligned(4))) void gm_f_sem_dot_4gikq3f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_sem_dot_4gikq3f F;
    gm_b16_8 gm_l0; /* input */
    gm_t9qkh0rsooc8d gm_l1; /* pos */
    GM_STACK_CHECK(&F, 343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tabb710d9mq2e gm_l2;
    gm_tabb710d9mq2e gm_l3;
    gm_b16_8 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl3q52ep6aa53));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(1));
    uint8_t t4 = gm_f_starts_with_6vvt01e(t2, t3);
    if (t4) {
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t7 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t8 = (uintptr_t)t6;
        if (t7 > t8 || t8 > (uintptr_t)t6) gm_trap_slice(t7, t8, t6, gm_srcij34jbkb4qlc, 98, 40);
        uint64_t t9 = ((uintptr_t)t6) - (uintptr_t)((uint64_t)1ULL);
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)1ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(t9));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t12 = (t11 > ((uint64_t)0ULL));
    if (t12) {
        {
            gm_b16_8 t13 = gm_l0;
            gm_b16_8 t14 = gm_f_quoted_char_jpbulib((void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t15 = gm_l1;
            gm_b16_8 t16 = gm_f_sem_pos_name_bpi6shr(t15);
            gm_t6j6smj7qoctu t17;
            GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t14);
            GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t16);
            gm_f_format_744g2li((void *)&gm_l2, (void *)NULL, cx, NULL, &t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srcij34jbkb4qlc, 99, 32);
        {
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t20);
            goto gm_fdone;
        }
    }
    {
        gm_t9qkh0rsooc8d t21 = gm_l1;
        gm_b16_8 t22 = gm_f_sem_pos_name_bpi6shr(t21);
        gm_tnpifv20irnph t23;
        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t22);
        gm_f_format_9c3j64r((void *)&gm_l3, (void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 8, gm_srcij34jbkb4qlc, 100, 13);
    {
        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
        gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t3ujhu6qph7de *)gm_out) + 8), t26);
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
__attribute__((aligned(4))) void gm_f_sem_identifier_dte5vh0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_sem_identifier_dte5vh0 F;
    gm_b16_8 gm_l0; /* input */
    gm_t9qkh0rsooc8d gm_l1; /* pos */
    uint64_t gm_l2; /* acc */
    uint64_t gm_l3; /* seg */
    uint8_t gm_l4; /* nondigit */
    uint8_t gm_l5; /* c */
    uint8_t gm_l6; /* <tmp> */
    uint8_t gm_l7; /* <tmp> */
    uint8_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 657u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqs5p4n2tq19v gm_l9;
    gm_tqs5p4n2tq19v gm_l10;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_l2 = ((uint64_t)0ULL);
    gm_l3 = ((uint64_t)0ULL);
    gm_l4 = ((uint8_t)0);
    {
        for (;;) {
            if (!((uint8_t)1)) {
                goto gm_lb1;
            }
            {
                void * t9 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t11 = gm_l2;
                uint64_t t12 = gm_l3;
                uint64_t r13; bool o14 = __builtin_add_overflow(t11, t12, &r13);
                if (o14) gm_trap("integer overflow in `+`", gm_srcij34jbkb4qlc, 110, 27);
                uint64_t t15 = r13;
                uint8_t t16 = (t15 < t10);
                if (t16) {
                    uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)t9) + (uintptr_t)t15 * 1));
                    gm_tqs5p4n2tq19v t18;
                    GM_ST(uint8_t, ((uint8_t *)&t18), t17);
                    ((uint8_t *)&t18)[1] = 1;
                    gm_l9 = t18;
                } else {
                    gm_tqs5p4n2tq19v t19;
                    memset(&t19, 0, sizeof t19);
                    gm_l9 = t19;
                }
                gm_tqs5p4n2tq19v t20 = gm_l9;
                gm_l10 = t20;
                uint8_t t21 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
                if (t21) {
                    uint8_t t22 = GM_LD(uint8_t, ((uint8_t *)&gm_l10));
                    gm_l5 = t22;
                } else {
                    gm_l5 = ((uint8_t)0ULL);
                }
                uint8_t t23 = gm_l5;
                gm_l6 = t23;
                uint8_t t24 = gm_l6;
                uint8_t t25 = (((uint8_t)97ULL) <= t24);
                if (t25) {
                    uint8_t t26 = gm_l6;
                    uint8_t t27 = (t26 <= ((uint8_t)122ULL));
                    gm_l13 = t27;
                } else {
                    gm_l13 = ((uint8_t)0);
                }
                uint8_t t28 = gm_l13;
                if (t28) {
                    gm_l12 = ((uint8_t)1);
                } else {
                    uint8_t t29 = gm_l5;
                    gm_l7 = t29;
                    uint8_t t30 = gm_l7;
                    uint8_t t31 = (((uint8_t)65ULL) <= t30);
                    if (t31) {
                        uint8_t t32 = gm_l7;
                        uint8_t t33 = (t32 <= ((uint8_t)90ULL));
                        gm_l14 = t33;
                    } else {
                        gm_l14 = ((uint8_t)0);
                    }
                    uint8_t t34 = gm_l14;
                    gm_l12 = t34;
                }
                uint8_t t35 = gm_l12;
                if (t35) {
                    gm_l11 = ((uint8_t)1);
                } else {
                    uint8_t t36 = gm_l5;
                    uint8_t t37 = ((t36 == ((uint8_t)45ULL)));
                    gm_l11 = t37;
                }
                uint8_t t38 = gm_l11;
                if (t38) {
                    uint64_t t39 = gm_l3;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    if (o41) gm_trap("integer overflow in `+`", gm_srcij34jbkb4qlc, 112, 13);
                    uint64_t t42 = r40;
                    gm_l3 = t42;
                    gm_l4 = ((uint8_t)1);
                } else {
                    uint8_t t43 = gm_l5;
                    gm_l8 = t43;
                    uint8_t t44 = gm_l8;
                    uint8_t t45 = (((uint8_t)48ULL) <= t44);
                    if (t45) {
                        uint8_t t46 = gm_l8;
                        uint8_t t47 = (t46 <= ((uint8_t)57ULL));
                        gm_l15 = t47;
                    } else {
                        gm_l15 = ((uint8_t)0);
                    }
                    uint8_t t48 = gm_l15;
                    if (t48) {
                        uint64_t t49 = gm_l3;
                        uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                        if (o51) gm_trap("integer overflow in `+`", gm_srcij34jbkb4qlc, 115, 13);
                        uint64_t t52 = r50;
                        gm_l3 = t52;
                    } else {
                        uint64_t t53 = gm_l3;
                        uint8_t t54 = ((t53 == ((uint64_t)0ULL)));
                        if (t54) {
                            {
                                {
                                    uint64_t t55 = gm_l2;
                                    uint8_t t56 = ((t55 == ((uint64_t)0ULL)));
                                    if (!t56) {
                                        goto gm_lb5;
                                    }
                                    uint8_t t57 = gm_l5;
                                    uint8_t t58 = (!(t57 == ((uint8_t)46ULL)));
                                    if (!t58) {
                                        goto gm_lb5;
                                    }
                                    {
                                        gm_b16_8 t59;
                                        GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sr4qbrk6tlgaa));
                                        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(0));
                                        gm_b16_8 t60 = gm_l0;
                                        gm_t6j6smj7qoctu t61;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t61 + 0), t59);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t61 + 16), t60);
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out))[0] = 0;
                                        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 8), t61);
                                        goto gm_fdone;
                                    }
                                }
gm_lb5:;
                            }
gm_lb4:;
                            {
                                gm_t9qkh0rsooc8d t62 = gm_l1;
                                gm_b16_8 t63 = gm_f_sem_pos_name_bpi6shr(t62);
                                gm_tnpifv20irnph t64;
                                GM_ST(gm_b16_8, ((uint8_t *)&t64 + 0), t63);
                                gm_f_format_lac0oi2((void *)&gm_l16, (void *)NULL, cx, NULL, t64);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                            if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcij34jbkb4qlc, 119, 25);
                            {
                                gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                                gm_b16_8 t67 = GM_LD(gm_b16_8, ((uint8_t *)&t66));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 24, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 32, 0);
                                ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out))[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 8), t67);
                                goto gm_fdone;
                            }
                        }
                        {
                            {
                                gm_t9qkh0rsooc8d t68 = gm_l1;
                                uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&t68) + 0));
                                uint8_t t70 = (uint8_t)((t69 == ((uint8_t)3ULL)));
                                if (!t70) {
                                    goto gm_lb7;
                                }
                                uint64_t t71 = gm_l3;
                                uint8_t t72 = (t71 > ((uint64_t)1ULL));
                                if (!t72) {
                                    goto gm_lb7;
                                }
                                uint8_t t73 = gm_l4;
                                uint8_t t74 = (uint8_t)!t73;
                                if (!t74) {
                                    goto gm_lb7;
                                }
                                void * t75 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t77 = gm_l2;
                                if ((uintptr_t)t77 >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)t77, t76, gm_srcij34jbkb4qlc, 121, 61);
                                uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)t75) + (uintptr_t)t77 * 1));
                                uint8_t t79 = ((t78 == ((uint8_t)48ULL)));
                                if (!t79) {
                                    goto gm_lb7;
                                }
                                {
                                    gm_t9qkh0rsooc8d t80 = gm_l1;
                                    gm_b16_8 t81 = gm_f_sem_pos_name_bpi6shr(t80);
                                    gm_tnpifv20irnph t82;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t82 + 0), t81);
                                    gm_f_format_fjhi9ui((void *)&gm_l17, (void *)NULL, cx, NULL, t82);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                                if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcij34jbkb4qlc, 122, 25);
                                {
                                    gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
                                    gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)&t84));
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 24, 0);
                                    GM_ST(uint64_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 32, 0);
                                    ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out))[0] = 1;
                                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 8), t85);
                                    goto gm_fdone;
                                }
                            }
gm_lb7:;
                        }
gm_lb6:;
                        uint64_t t86 = gm_l2;
                        uint64_t t87 = gm_l3;
                        uint64_t r88; bool o89 = __builtin_add_overflow(t86, t87, &r88);
                        if (o89) gm_trap("integer overflow in `+`", gm_srcij34jbkb4qlc, 124, 13);
                        uint64_t t90 = r88;
                        gm_l2 = t90;
                        uint8_t t91 = gm_l5;
                        uint8_t t92 = ((t91 == ((uint8_t)46ULL)));
                        if (t92) {
                            uint64_t t93 = gm_l2;
                            uint64_t r94; bool o95 = __builtin_add_overflow(t93, ((uint64_t)1ULL), &r94);
                            if (o95) gm_trap("integer overflow in `+`", gm_srcij34jbkb4qlc, 126, 17);
                            uint64_t t96 = r94;
                            gm_l2 = t96;
                            gm_l3 = ((uint64_t)0ULL);
                            gm_l4 = ((uint8_t)0);
                        } else {
                            void * t97 = GM_LD(void *, ((uint8_t *)&gm_l0));
                            uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint64_t t99 = gm_l2;
                            uint64_t t100 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t101 = (uintptr_t)t99;
                            if (t100 > t101 || t101 > (uintptr_t)t98) gm_trap_slice(t100, t101, t98, gm_srcij34jbkb4qlc, 130, 32);
                            uint64_t t102 = ((uintptr_t)t99) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t103;
                            GM_ST(void *, (uint8_t *)&t103, (void *)(((uint8_t *)t97 + (uintptr_t)((uint64_t)0ULL) * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(t102));
                            void * t104 = GM_LD(void *, ((uint8_t *)&gm_l0));
                            uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint64_t t106 = gm_l2;
                            uint64_t t107 = (uintptr_t)t106;
                            uint64_t t108 = (uintptr_t)t105;
                            if (t107 > t108 || t108 > (uintptr_t)t105) gm_trap_slice(t107, t108, t105, gm_srcij34jbkb4qlc, 130, 53);
                            uint64_t t109 = ((uintptr_t)t105) - (uintptr_t)t106;
                            gm_b16_8 t110;
                            GM_ST(void *, (uint8_t *)&t110, (void *)(((uint8_t *)t104 + (uintptr_t)t106 * 1)));
                            GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(t109));
                            gm_t6j6smj7qoctu t111;
                            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 0), t103);
                            GM_ST(gm_b16_8, ((uint8_t *)&t111 + 16), t110);
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out))[0] = 0;
                            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 8), t111);
                            goto gm_fdone;
                        }
                    }
                }
            }
gm_lb3:;
        }
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
__attribute__((aligned(4))) void gm_f_sem_numeric_oik4dle(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_t9qkh0rsooc8d a1) {
    struct gm_fr_sem_numeric_oik4dle F;
    gm_b16_8 gm_l0; /* input */
    gm_t9qkh0rsooc8d gm_l1; /* pos */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* value */
    uint8_t gm_l4; /* d */
    gm_tf5srh4mp4b3i gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    GM_STACK_CHECK(&F, 910u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tqs5p4n2tq19v gm_l8;
    gm_tqs5p4n2tq19v gm_l9;
    uint8_t gm_l10;
    gm_tabb710d9mq2e gm_l11;
    uint64_t gm_l12;
    gm_tf5srh4mp4b3i gm_l13;
    gm_tf5srh4mp4b3i gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_l2 = ((uint64_t)0ULL);
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            void * t9 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t11 = gm_l2;
            uint8_t t12 = (t11 < t10);
            if (t12) {
                uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t9) + (uintptr_t)t11 * 1));
                gm_tqs5p4n2tq19v t14;
                GM_ST(uint8_t, ((uint8_t *)&t14), t13);
                ((uint8_t *)&t14)[1] = 1;
                gm_l8 = t14;
            } else {
                gm_tqs5p4n2tq19v t15;
                memset(&t15, 0, sizeof t15);
                gm_l8 = t15;
            }
            gm_tqs5p4n2tq19v t16 = gm_l8;
            gm_l9 = t16;
            uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l9))[1] != 0);
            if (!t17) {
                goto gm_lb1;
            }
            uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)&gm_l9));
            gm_l4 = t18;
            {
                uint8_t t19 = gm_l4;
                uint8_t t20 = (t19 < ((uint8_t)48ULL));
                if (t20) {
                    gm_l10 = ((uint8_t)1);
                } else {
                    uint8_t t21 = gm_l4;
                    uint8_t t22 = (t21 > ((uint8_t)57ULL));
                    gm_l10 = t22;
                }
                uint8_t t23 = gm_l10;
                if (t23) {
                    goto gm_lb1;
                }
                {
                    {
                        uint64_t t24 = gm_l3;
                        uint8_t t25 = ((t24 == ((uint64_t)0ULL)));
                        if (!t25) {
                            goto gm_lb5;
                        }
                        uint64_t t26 = gm_l2;
                        uint8_t t27 = (t26 > ((uint64_t)0ULL));
                        if (!t27) {
                            goto gm_lb5;
                        }
                        {
                            gm_t9qkh0rsooc8d t28 = gm_l1;
                            gm_b16_8 t29 = gm_f_sem_pos_name_bpi6shr(t28);
                            gm_tnpifv20irnph t30;
                            GM_ST(gm_b16_8, ((uint8_t *)&t30 + 0), t29);
                            gm_f_format_fjhi9ui((void *)&gm_l11, (void *)NULL, cx, NULL, t30);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcij34jbkb4qlc, 81, 45);
                        {
                            gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 8));
                            gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 24, 0);
                            ((uint8_t *)&(*(gm_trto8sim72asr *)gm_out))[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 8), t33);
                            goto gm_fdone;
                        }
                    }
gm_lb5:;
                }
gm_lb4:;
                uint64_t t34 = gm_l3;
                uint64_t r35; bool o36 = __builtin_mul_overflow(t34, ((uint64_t)10ULL), &r35);
                gm_tf5srh4mp4b3i t37;
                if (o36) {
                gm_tf5srh4mp4b3i t38;
                memset(&t38, 0, sizeof t38);
                    t37 = t38;
                } else {
                uint64_t t39 = r35;
                gm_tf5srh4mp4b3i t40;
                GM_ST(uint8_t, (uint8_t *)&t40 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t40 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t40 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t40), t39);
                ((uint8_t *)&t40)[8] = 1;
                    t37 = t40;
                }
                gm_l5 = t37;
                uint8_t t41 = gm_l4;
                uint8_t r42; bool o43 = __builtin_sub_overflow(t41, ((uint8_t)48ULL), &r42);
                if (o43) gm_trap("integer overflow in `-`", gm_srcij34jbkb4qlc, 82, 36);
                uint8_t t44 = r42;
                uint64_t t45 = (uint64_t)t44;
                gm_l7 = t45;
                {
                    {
                        uint8_t t46 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
                        if (!t46) {
                            goto gm_lb7;
                        }
                        uint64_t t47 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
                        gm_l6 = t47;
                        uint64_t t48 = gm_l6;
                        uint64_t t49 = gm_l7;
                        uint64_t r50; bool o51 = __builtin_add_overflow(t48, t49, &r50);
                        gm_tf5srh4mp4b3i t52;
                        if (o51) {
                        gm_tf5srh4mp4b3i t53;
                        memset(&t53, 0, sizeof t53);
                            t52 = t53;
                        } else {
                        uint64_t t54 = r50;
                        gm_tf5srh4mp4b3i t55;
                        GM_ST(uint8_t, (uint8_t *)&t55 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t55 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t55 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t55), t54);
                        ((uint8_t *)&t55)[8] = 1;
                            t52 = t55;
                        }
                        gm_l13 = t52;
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_tf5srh4mp4b3i t56;
                    memset(&t56, 0, sizeof t56);
                    gm_l13 = t56;
                }
gm_lb6:;
                gm_tf5srh4mp4b3i t57 = gm_l13;
                gm_l14 = t57;
                uint8_t t58 = (uint8_t)((((uint8_t *)&gm_l14))[8] != 0);
                if (t58) {
                    uint64_t t59 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                    gm_l12 = t59;
                } else {
                    {
                        gm_t9qkh0rsooc8d t60 = gm_l1;
                        gm_b16_8 t61 = gm_f_sem_pos_name_bpi6shr(t60);
                        gm_tnpifv20irnph t62;
                        GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t61);
                        gm_f_format_dbdhrrf((void *)&gm_l15, (void *)NULL, cx, NULL, t62);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                    if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcij34jbkb4qlc, 82, 58);
                    {
                        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_trto8sim72asr *)gm_out))[0] = 1;
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 8), t65);
                        goto gm_fdone;
                    }
                }
                uint64_t t66 = gm_l12;
                gm_l3 = t66;
                uint64_t t67 = gm_l2;
                uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                if (o69) gm_trap("integer overflow in `+`", gm_srcij34jbkb4qlc, 83, 9);
                uint64_t t70 = r68;
                gm_l2 = t70;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t71 = gm_l2;
    uint8_t t72 = (t71 > ((uint64_t)0ULL));
    if (t72) {
        uint64_t t73 = gm_l3;
        void * t74 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t76 = gm_l2;
        uint64_t t77 = (uintptr_t)t76;
        uint64_t t78 = (uintptr_t)t75;
        if (t77 > t78 || t78 > (uintptr_t)t75) gm_trap_slice(t77, t78, t75, gm_srcij34jbkb4qlc, 85, 53);
        uint64_t t79 = ((uintptr_t)t75) - (uintptr_t)t76;
        gm_b16_8 t80;
        GM_ST(void *, (uint8_t *)&t80, (void *)(((uint8_t *)t74 + (uintptr_t)t76 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(t79));
        gm_tf1bjg81u0glv t81;
        GM_ST(uint64_t, ((uint8_t *)&t81 + 0), t73);
        GM_ST(gm_b16_8, ((uint8_t *)&t81 + 8), t80);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_trto8sim72asr *)gm_out))[0] = 0;
        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 8), t81);
        goto gm_fdone;
    }
    uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t83 = (t82 > ((uint64_t)0ULL));
    if (t83) {
        {
            gm_b16_8 t84 = gm_l0;
            gm_b16_8 t85 = gm_f_quoted_char_jpbulib((void *)NULL, cx, NULL, t84);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t86 = gm_l1;
            gm_b16_8 t87 = gm_f_sem_pos_name_bpi6shr(t86);
            gm_t6j6smj7qoctu t88;
            GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t85);
            GM_ST(gm_b16_8, ((uint8_t *)&t88 + 16), t87);
            gm_f_format_qner803((void *)&gm_l16, (void *)NULL, cx, NULL, &t88);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t89 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (!t89) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcij34jbkb4qlc, 86, 32);
        {
            gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 24, 0);
            ((uint8_t *)&(*(gm_trto8sim72asr *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 8), t91);
            goto gm_fdone;
        }
    }
    {
        gm_t9qkh0rsooc8d t92 = gm_l1;
        gm_b16_8 t93 = gm_f_sem_pos_name_bpi6shr(t92);
        gm_tnpifv20irnph t94;
        GM_ST(gm_b16_8, ((uint8_t *)&t94 + 0), t93);
        gm_f_format_9c3j64r((void *)&gm_l17, (void *)NULL, cx, NULL, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t95 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (!t95) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcij34jbkb4qlc, 87, 13);
    {
        gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
        gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_trto8sim72asr *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_trto8sim72asr *)gm_out) + 8), t97);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_sem_pos_name_bpi6shr(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sem_pos_name_bpi6shr F;
    gm_t9qkh0rsooc8d gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t9 = (uint8_t)(t8 == ((uint8_t)0ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sfd6isdlu0d13));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(20));
            gm_l1 = t10;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)1ULL));
            if (!t12) {
                goto gm_lb3;
            }
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s3n43rret3i7v));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(20));
            gm_l1 = t13;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)2ULL));
            if (!t15) {
                goto gm_lb4;
            }
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s5ocg5vi2qib3));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(20));
            gm_l1 = t16;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)3ULL));
            if (!t18) {
                goto gm_lb5;
            }
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sr499ju9p7ks2));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(22));
            gm_l1 = t19;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)4ULL));
            if (!t21) {
                goto gm_lb6;
            }
            gm_b16_8 t22;
            GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sclthecvcggbv));
            GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(14));
            gm_l1 = t22;
            goto gm_lb1;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcij34jbkb4qlc, 37, 44);
    }
gm_lb1:;
    gm_b16_8 t23 = gm_l1;
    F.ret = t23;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_sem_wildcard_15qmfol(void *gm_out, gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sem_wildcard_15qmfol F;
    gm_b16_8 gm_l0; /* input */
    uint8_t gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 134u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l2;
    gm_tqs5p4n2tq19v gm_l3;
    void * t2 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t4 = (((uint64_t)0ULL) < t3);
    if (t4) {
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t2) + (uintptr_t)((uint64_t)0ULL) * 1));
        gm_tqs5p4n2tq19v t6;
        GM_ST(uint8_t, ((uint8_t *)&t6), t5);
        ((uint8_t *)&t6)[1] = 1;
        gm_l2 = t6;
    } else {
        gm_tqs5p4n2tq19v t7;
        memset(&t7, 0, sizeof t7);
        gm_l2 = t7;
    }
    gm_tqs5p4n2tq19v t8 = gm_l2;
    gm_l3 = t8;
    uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l3))[1] != 0);
    if (t9) {
        uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)&gm_l3));
        gm_l1 = t10;
    } else {
        memset(&(*(gm_tadokt2of3tma *)gm_out), 0, sizeof (*(gm_tadokt2of3tma *)gm_out));
        goto gm_fdone;
    }
    uint8_t t11 = gm_l1;
    gm_toh0ootodhjue t12;
    GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)42ULL));
    GM_ST(uint8_t, ((uint8_t *)&t12 + 1), ((uint8_t)120ULL));
    GM_ST(uint8_t, ((uint8_t *)&t12 + 2), ((uint8_t)88ULL));
    uint8_t t13 = 0;
    if (gm_eqjmccqdb90f(((uint8_t *)&t12) + 0, ((uint8_t *)&t11))) t13 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t12) + 1, ((uint8_t *)&t11))) t13 = 1;
    if (gm_eqjmccqdb90f(((uint8_t *)&t12) + 2, ((uint8_t *)&t11))) t13 = 1;
    uint8_t t14 = (uint8_t)(t13);
    if (t14) {
        uint8_t t15 = gm_l1;
        void * t16 = GM_LD(void *, ((uint8_t *)&gm_l0));
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t18 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t19 = (uintptr_t)t17;
        if (t18 > t19 || t19 > (uintptr_t)t17) gm_trap_slice(t18, t19, t17, gm_srcij34jbkb4qlc, 93, 52);
        uint64_t t20 = ((uintptr_t)t17) - (uintptr_t)((uint64_t)1ULL);
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(((uint8_t *)t16 + (uintptr_t)((uint64_t)1ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(t20));
        gm_thervm8j23qet t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), t15);
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 8), t21);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tadokt2of3tma *)gm_out) + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tadokt2of3tma *)gm_out) + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tadokt2of3tma *)gm_out) + 28, 0);
        GM_ST(gm_thervm8j23qet, ((uint8_t *)&(*(gm_tadokt2of3tma *)gm_out)), t22);
        ((uint8_t *)&(*(gm_tadokt2of3tma *)gm_out))[24] = 1;
        goto gm_fdone;
    }
    memset(&(*(gm_tadokt2of3tma *)gm_out), 0, sizeof (*(gm_tadokt2of3tma *)gm_out));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tadokt2of3tma gm_f_sem_wildcard_15qmfol_c(gm_b16_8 a0) { gm_tadokt2of3tma gm_r; gm_f_sem_wildcard_15qmfol(&gm_r, a0); return gm_r; }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_spaces_i65urmt(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_spaces_i65urmt F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l2;
    gm_l1 = ((uint64_t)0ULL);
    {
        for (;;) {
            void * t5 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t7 = gm_l1;
            uint8_t t8 = (t7 < t6);
            if (t8) {
                uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t5) + (uintptr_t)t7 * 1));
                gm_tqs5p4n2tq19v t10;
                GM_ST(uint8_t, ((uint8_t *)&t10), t9);
                ((uint8_t *)&t10)[1] = 1;
                gm_l2 = t10;
            } else {
                gm_tqs5p4n2tq19v t11;
                memset(&t11, 0, sizeof t11);
                gm_l2 = t11;
            }
            gm_tqs5p4n2tq19v t12 = gm_l2;
            gm_tqs5p4n2tq19v t13;
            GM_ST(uint8_t, ((uint8_t *)&t13), ((uint8_t)32ULL));
            ((uint8_t *)&t13)[1] = 1;
            uint8_t t14 = (gm_eqb44cb3cf1n(((uint8_t *)&t12), ((uint8_t *)&t13)));
            if (!t14) {
                goto gm_lb1;
            }
            {
                uint64_t t15 = gm_l1;
                uint64_t r16; bool o17 = __builtin_add_overflow(t15, ((uint64_t)1ULL), &r16);
                if (o17) gm_trap("integer overflow in `+`", gm_srcij34jbkb4qlc, 178, 29);
                uint64_t t18 = r16;
                gm_l1 = t18;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t19 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t21 = gm_l1;
    uint64_t t22 = (uintptr_t)t21;
    uint64_t t23 = (uintptr_t)t20;
    if (t22 > t23 || t23 > (uintptr_t)t20) gm_trap_slice(t22, t23, t20, gm_srcij34jbkb4qlc, 179, 12);
    uint64_t t24 = ((uintptr_t)t20) - (uintptr_t)t21;
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t19 + (uintptr_t)t21 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    F.ret = t25;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_zeros_q2fhlfl(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_zeros_q2fhlfl F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 173u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tqs5p4n2tq19v gm_l2;
    gm_l1 = ((uint64_t)0ULL);
    {
        for (;;) {
            void * t5 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t7 = gm_l1;
            uint8_t t8 = (t7 < t6);
            if (t8) {
                uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t5) + (uintptr_t)t7 * 1));
                gm_tqs5p4n2tq19v t10;
                GM_ST(uint8_t, ((uint8_t *)&t10), t9);
                ((uint8_t *)&t10)[1] = 1;
                gm_l2 = t10;
            } else {
                gm_tqs5p4n2tq19v t11;
                memset(&t11, 0, sizeof t11);
                gm_l2 = t11;
            }
            gm_tqs5p4n2tq19v t12 = gm_l2;
            gm_tqs5p4n2tq19v t13;
            GM_ST(uint8_t, ((uint8_t *)&t13), ((uint8_t)48ULL));
            ((uint8_t *)&t13)[1] = 1;
            uint8_t t14 = (gm_eqb44cb3cf1n(((uint8_t *)&t12), ((uint8_t *)&t13)));
            if (!t14) {
                goto gm_lb1;
            }
            {
                uint64_t t15 = gm_l1;
                uint64_t r16; bool o17 = __builtin_add_overflow(t15, ((uint64_t)1ULL), &r16);
                if (o17) gm_trap("integer overflow in `+`", gm_srcij34jbkb4qlc, 409, 29);
                uint64_t t18 = r16;
                gm_l1 = t18;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t19 = GM_LD(void *, ((uint8_t *)&gm_l0));
    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t21 = gm_l1;
    uint64_t t22 = (uintptr_t)t21;
    uint64_t t23 = (uintptr_t)t20;
    if (t22 > t23 || t23 > (uintptr_t)t20) gm_trap_slice(t22, t23, t20, gm_srcij34jbkb4qlc, 410, 12);
    uint64_t t24 = ((uintptr_t)t20) - (uintptr_t)t21;
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(((uint8_t *)t19 + (uintptr_t)t21 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(t24));
    F.ret = t25;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_version_string_k02v4te(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_version_string_k02v4te F;
    GM_STACK_CHECK(&F, 273u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tftb1odc739t5 gm_l2;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
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
        uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)a0));
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)a0) + 16));
        gm_tkpb4kplftgaa t7;
        GM_ST(uint64_t, ((uint8_t *)&t7 + 0), t4);
        GM_ST(uint64_t, ((uint8_t *)&t7 + 8), t5);
        GM_ST(uint64_t, ((uint8_t *)&t7 + 16), t6);
        gm_tftb1odc739t5 t8 = gm_f_write_7arl25u(t3, &t7);
        gm_l2 = t8;
    }
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srcij34jbkb4qlc, 301, 5);
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)a0) + 24) + 8));
    uint8_t t11 = (t10 > ((uint64_t)0ULL));
    if (t11) {
        void * t12 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 24));
        gm_tnpifv20irnph t14;
        GM_ST(gm_b16_8, ((uint8_t *)&t14 + 0), t13);
        gm_tftb1odc739t5 t15 = gm_f_write_tmm3c98(t12, t14);
        gm_l3 = t15;
        uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
        if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcij34jbkb4qlc, 302, 24);
    }
    uint64_t t17 = GM_LD(uint64_t, ((((uint8_t *)a0) + 40) + 8));
    uint8_t t18 = (t17 > ((uint64_t)0ULL));
    if (t18) {
        void * t19 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 40));
        gm_tnpifv20irnph t21;
        GM_ST(gm_b16_8, ((uint8_t *)&t21 + 0), t20);
        gm_tftb1odc739t5 t22 = gm_f_write_frfjpi8(t19, t21);
        gm_l4 = t22;
        uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
        if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcij34jbkb4qlc, 303, 26);
    }
    gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
    F.ret = t25;
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

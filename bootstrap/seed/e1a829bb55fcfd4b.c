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
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; uint8_t f3; uint8_t f4; uint8_t f5; } gm_t4q2b5ibsrdik;
typedef struct { uint8_t a[2]; } gm_t5liq177ejcou;
typedef struct { uint8_t a[32]; } gm_t7mlrlibdq1ja;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t a[8]; } gm_tg8e8rhc3qio9;
typedef struct { int64_t f0; } gm_tm98ap4112hee;
typedef struct { uint8_t f0; uint8_t p1[3]; union { int32_t v0; } f1; } gm_toe5fmr3vvf4d;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { uint8_t a[64]; } gm_tsn02dhhitgs4;
typedef struct { gm_b16_8 a[23]; } gm_t0ut772p7ffln;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_b16_8 v1; } f1; } gm_t3ujhu6qph7de;
_Static_assert(sizeof(gm_t4q2b5ibsrdik) == 6 && _Alignof(gm_t4q2b5ibsrdik) == 1 && offsetof(gm_t4q2b5ibsrdik, f0) == 0 && offsetof(gm_t4q2b5ibsrdik, f1) == 1 && offsetof(gm_t4q2b5ibsrdik, f2) == 2 && offsetof(gm_t4q2b5ibsrdik, f3) == 3 && offsetof(gm_t4q2b5ibsrdik, f4) == 4 && offsetof(gm_t4q2b5ibsrdik, f5) == 5, "layout of `OpenOptions`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; int64_t f2; uint32_t f3; uint8_t pe[4]; } gm_t5j14m8ru132p;
_Static_assert(sizeof(gm_t5liq177ejcou) == 2 && _Alignof(gm_t5liq177ejcou) == 1 && offsetof(gm_t5liq177ejcou, a) == 0, "layout of `[2]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tm98ap4112hee v1; } f1; } gm_t62ebbi2i5rkd;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t7mlrlibdq1ja) == 32 && _Alignof(gm_t7mlrlibdq1ja) == 1 && offsetof(gm_t7mlrlibdq1ja, a) == 0, "layout of `[32]u8`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_b16_8 a[7]; } gm_tdv3dkjvo1l7t;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tg8e8rhc3qio9) == 32 && _Alignof(gm_tg8e8rhc3qio9) == 4 && offsetof(gm_tg8e8rhc3qio9, a) == 0, "layout of `[8]u32`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; uint64_t v1; } f1; } gm_thf5kr0is9u9i;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tipro5npl6rd8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; } f1; } gm_tl572d7579hns;
_Static_assert(sizeof(gm_tm98ap4112hee) == 8 && _Alignof(gm_tm98ap4112hee) == 8 && offsetof(gm_tm98ap4112hee, f0) == 0, "layout of `File`");
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toe5fmr3vvf4d) == 8 && _Alignof(gm_toe5fmr3vvf4d) == 4 && offsetof(gm_toe5fmr3vvf4d, f0) == 0 && offsetof(gm_toe5fmr3vvf4d, f1) == 4, "layout of `IoError`");
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_tg8e8rhc3qio9 f0; gm_tsn02dhhitgs4 f1; uint64_t f2; uint64_t f3; } gm_tq14qvifgdvpm;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tsn02dhhitgs4) == 64 && _Alignof(gm_tsn02dhhitgs4) == 1 && offsetof(gm_tsn02dhhitgs4, a) == 0, "layout of `[64]u8`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_toe5fmr3vvf4d v0; } f1; } gm_ttln0d0hd70en;
_Static_assert(sizeof(gm_t0ut772p7ffln) == 368 && _Alignof(gm_t0ut772p7ffln) == 8 && offsetof(gm_t0ut772p7ffln, a) == 0, "layout of `[23]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_t6j6smj7qoctu v1; } f1; } gm_t22b1ebhvur5a;
_Static_assert(sizeof(gm_t3ujhu6qph7de) == 24 && _Alignof(gm_t3ujhu6qph7de) == 8 && offsetof(gm_t3ujhu6qph7de, f0) == 0 && offsetof(gm_t3ujhu6qph7de, f1) == 8, "layout of `string!string`");
_Static_assert(sizeof(gm_t5j14m8ru132p) == 32 && _Alignof(gm_t5j14m8ru132p) == 8 && offsetof(gm_t5j14m8ru132p, f0) == 0 && offsetof(gm_t5j14m8ru132p, f1) == 8 && offsetof(gm_t5j14m8ru132p, f2) == 16 && offsetof(gm_t5j14m8ru132p, f3) == 24, "layout of `FileInfo`");
_Static_assert(sizeof(gm_t62ebbi2i5rkd) == 16 && _Alignof(gm_t62ebbi2i5rkd) == 8 && offsetof(gm_t62ebbi2i5rkd, f0) == 0 && offsetof(gm_t62ebbi2i5rkd, f1) == 8, "layout of `IoError!File`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tdv3dkjvo1l7t) == 112 && _Alignof(gm_tdv3dkjvo1l7t) == 8 && offsetof(gm_tdv3dkjvo1l7t, a) == 0, "layout of `[7]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_tibupt4dmcqai v1; } f1; } gm_te2n4j2vtm53d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thf5kr0is9u9i) == 16 && _Alignof(gm_thf5kr0is9u9i) == 8 && offsetof(gm_thf5kr0is9u9i, f0) == 0 && offsetof(gm_thf5kr0is9u9i, f1) == 8, "layout of `IoError!usize`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_toe5fmr3vvf4d v0; gm_t5j14m8ru132p v1; } f1; } gm_tih6h4avv5nrb;
_Static_assert(sizeof(gm_tipro5npl6rd8) == 40 && _Alignof(gm_tipro5npl6rd8) == 8 && offsetof(gm_tipro5npl6rd8, f0) == 0 && offsetof(gm_tipro5npl6rd8, f1) == 4 && offsetof(gm_tipro5npl6rd8, f2) == 8 && offsetof(gm_tipro5npl6rd8, f3) == 24, "layout of `Ran`");
_Static_assert(sizeof(gm_tl572d7579hns) == 24 && _Alignof(gm_tl572d7579hns) == 8 && offsetof(gm_tl572d7579hns, f0) == 0 && offsetof(gm_tl572d7579hns, f1) == 8, "layout of `string!()`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tq14qvifgdvpm) == 112 && _Alignof(gm_tq14qvifgdvpm) == 8 && offsetof(gm_tq14qvifgdvpm, f0) == 0 && offsetof(gm_tq14qvifgdvpm, f1) == 32 && offsetof(gm_tq14qvifgdvpm, f2) == 96 && offsetof(gm_tq14qvifgdvpm, f3) == 104, "layout of `Sha256`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ttln0d0hd70en) == 12 && _Alignof(gm_ttln0d0hd70en) == 4 && offsetof(gm_ttln0d0hd70en, f0) == 0 && offsetof(gm_ttln0d0hd70en, f1) == 4, "layout of `IoError!()`");
_Static_assert(sizeof(gm_t22b1ebhvur5a) == 40 && _Alignof(gm_t22b1ebhvur5a) == 8 && offsetof(gm_t22b1ebhvur5a, f0) == 0 && offsetof(gm_t22b1ebhvur5a, f1) == 8, "layout of `string!(ident: string, rest: string)`");
_Static_assert(sizeof(gm_te2n4j2vtm53d) == 32 && _Alignof(gm_te2n4j2vtm53d) == 8 && offsetof(gm_te2n4j2vtm53d, f0) == 0 && offsetof(gm_te2n4j2vtm53d, f1) == 8, "layout of `IoError!Entry`");
_Static_assert(sizeof(gm_tih6h4avv5nrb) == 40 && _Alignof(gm_tih6h4avv5nrb) == 8 && offsetof(gm_tih6h4avv5nrb, f0) == 0 && offsetof(gm_tih6h4avv5nrb, f1) == 8, "layout of `IoError!FileInfo`");
struct gm_fr_contains_gkki4kq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_discard_bdpju80 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_download_eek737p {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* args */
    gm_t0ut772p7ffln l5; /* <tmp> */
    gm_tipro5npl6rd8 l9; /* r */
    gm_t22b1ebhvur5a ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_file_sha256_llree8t {
    gm_frame h;
    gm_tq14qvifgdvpm l3; /* h */
    gm_t3ujhu6qph7de ret;
};
struct gm_fr_for_body_1avb45k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_gp0er0a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_nnvr1n3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_body_tv3u4f0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_04pispc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_0rhberc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5pl0mkc {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5uun4vl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_8q6hdku {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ee96hnp {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ep7kj4f {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_l0gkulr {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_m1bjvb9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nk3ghj0 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_init_s32ien0 {
    gm_frame h;
    gm_tq14qvifgdvpm ret;
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
struct gm_fr_join_sb1m5es {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_leaves_directory_kq5llq4 {
    gm_frame h;
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_parent_4i83qet {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_place_snpjvsu {
    gm_frame h;
    gm_b16_8 l0; /* tmp */
    gm_trqfvmjtt4sij l2; /* names */
    gm_tl572d7579hns ret;
    gm_b0_1 w0;
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
struct gm_fr_sanitize_modes_mvteloh {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_starts_with_6vvt01e {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unpack_eb2aboi {
    gm_frame h;
    gm_tl572d7579hns ret;
};
struct gm_fr_unpack_into_kqij1d2 {
    gm_frame h;
    gm_t9bmh80edou9c l6; /* names_args */
    gm_tipro5npl6rd8 l7; /* names_run */
    gm_t9bmh80edou9c l8; /* types_args */
    gm_tipro5npl6rd8 l9; /* types_run */
    gm_trqfvmjtt4sij l10; /* names */
    gm_trqfvmjtt4sij l12; /* types */
    gm_tdv3dkjvo1l7t l21; /* args */
    gm_tipro5npl6rd8 l22; /* r */
    gm_tl572d7579hns ret;
    gm_b0_1 w0;
    gm_b0_1 w1;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b0_1 gm_f_chmod_tree_sj4qk6r(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_discard_bdpju80(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_download_eek737p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_dupe_g9iqpvn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_entries_44o5n9f_s40i17b(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_file_sha256_llree8t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_final_rg5ko3p(void *gm_out, void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1avb45k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_gp0er0a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_nnvr1n3(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tv3u4f0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_format_04pispc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_0rhberc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5pl0mkc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_5uun4vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_8q6hdku(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ee96hnp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_ep7kj4f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_l0gkulr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_m1bjvb9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
inline __attribute__((aligned(4))) void gm_f_format_nk3ghj0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_sc59up9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_hex_euih242(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_init_s32ien0(void *gm_out);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_io_message_p7jbrea(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_2kkkskr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_dir_6sd3mnh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_join_qveh5b2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_join_sb1m5es(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_leaves_directory_kq5llq4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s84ppfm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_lines_9uns0k2_s9kjq5m(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) void gm_f_make_dirs_gbp8nfr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_os_error_text_319eh6h(void *env, gm_node *cx, gm_node *inst, gm_toe5fmr3vvf4d a0);
inline __attribute__((aligned(4))) void gm_f_parent_4i83qet(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parent_lk0iero(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_place_snpjvsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_private_name_83ij38e(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_ttln0d0hd70en gm_f_remove_all_h0k1qkh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_run_mj5dobu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_sanitize_modes_mvteloh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s8m2rsv(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_unpack_eb2aboi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_unpack_into_kqij1d2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_update_pn3fq6a(void * a0, gm_b16_8 a1);
extern void *const gm_hlh176qr0nti[];
extern void *const gm_hlhtabotcjhg[];
extern void *const gm_hlq1reae6pf9[];
extern void *const gm_hlucu919n5n4[];
extern uint8_t gm_k0kae0be6jj[40] __attribute__((aligned(8)));
extern uint8_t gm_kh1ssdqq46n[40] __attribute__((aligned(8)));
extern uint8_t gm_kr7fqi9m0ih[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0c2823k4ul3o[17];
extern const uint8_t gm_s104g112fsi3t[3];
extern const uint8_t gm_s1ktljc2oac64[4];
extern const uint8_t gm_s24qpbg6rctcd[17];
extern const uint8_t gm_s2dssfe3261t2[2];
extern const uint8_t gm_s46khuklhbvqd[21];
extern const uint8_t gm_s4k36hl9k22np[63];
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s4ufkrqv2ag7g[7];
extern const uint8_t gm_s5v7o1bkddp5s[7];
extern const uint8_t gm_s7cn098jq5gvg[9];
extern const uint8_t gm_s8nekj984lthm[15];
extern const uint8_t gm_s9pknf9g05okt[3];
extern const uint8_t gm_sa5v7oq7un6c6[84];
extern const uint8_t gm_sarlhchtvv77m[13];
extern const uint8_t gm_sasgv00j2p5nu[18];
extern const uint8_t gm_sbtoph8phkseu[5];
extern const uint8_t gm_scueeuhqhtotc[30];
extern const uint8_t gm_sdaknqn35hokt[9];
extern const uint8_t gm_sfpeqbitcbq42[6];
extern const uint8_t gm_sg91clgj6l88m[6];
extern const uint8_t gm_sgs65cm0mtgm4[14];
extern const uint8_t gm_sh7dbt4gdrljc[19];
extern const uint8_t gm_shsisdh16t023[28];
extern const uint8_t gm_skpl66e1qvhab[8];
extern const uint8_t gm_slahfmk66v1v4[15];
extern const uint8_t gm_slqtudd4gk04i[9];
extern const uint8_t gm_smaguor10so8u[12];
extern const uint8_t gm_snauhge6bheis[14];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snhr2rebnnkc2[22];
extern const uint8_t gm_so48b6sdoerku[18];
extern const uint8_t gm_so52klh2mgoce[23];
extern const uint8_t gm_sobnp9i68hvdj[16];
extern const uint8_t gm_sos62dipmhb8k[6];
extern const uint8_t gm_spmhk8jcpdrb0[41];
extern const uint8_t gm_sppohbjp90mol[3];
extern const uint8_t gm_sqvb94amh2nu2[66];
extern const uint8_t gm_sqviis18fpvj7[5];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_src3s76fqo1a9jk[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srrq1irp1jfep[12];
extern const uint8_t gm_ss6cscjfm4jfc[11];
extern const uint8_t gm_ss9vj0o06n3e8[8];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_stjlu12rqiie7[13];
extern const uint8_t gm_suq2iqmhi931e[3];
extern const uint8_t gm_sutk9gh8f6gmt[53];
extern const uint8_t gm_svubtddeidu1d[2];
extern const gm_tmpl gm_tpl1lvejpvpq0qh;
extern const gm_tmpl gm_tpl3dghmndqn708;
extern const gm_tmpl gm_tpl8in37qe3omcj;
extern const gm_tmpl gm_tplh7ffbsfnitkh;
extern const gm_tmpl gm_tplj8iadiucj6so;
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tpllp788bsiqs1r;
extern const gm_tmpl gm_tplnr9pmjldkl35;
extern const gm_tmpl gm_tplqf2cda88og0i;
extern const gm_tmpl gm_tplt1555ki49he5;
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_gkki4kq(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_gkki4kq F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* needle */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    gm_b16_8 t5 = gm_l0;
    gm_b16_8 t6 = gm_l1;
    gm_tf5srh4mp4b3i t7 = gm_f_find_nab29co(t5, t6);
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
        gm_trap("no match arm matched the scrutinee", gm_src3d0v42kavhv7, 84, 50);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_discard_bdpju80(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_discard_bdpju80 F;
    gm_t6j6smj7qoctu gm_l0; /* d */
    GM_STACK_CHECK(&F, 60u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((uint8_t *)a0));
    gm_ttln0d0hd70en t3 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1avb45k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_1avb45k F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 82u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l1 = t4;
            gm_b16_8 t5 = gm_l1;
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_snenkhfev166s));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(2));
            uint8_t t7 = (gm_eq0iqiubfm7e(((uint8_t *)&t5), ((uint8_t *)&t6)));
            if (t7) {
                uint8_t t8 = 0;
                ((struct gm_fr_leaves_directory_kq5llq4 *)((&F.h)->up))->ret = ((uint8_t)1);
                gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t8;
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 131, 5);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tv3u4f0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_tv3u4f0 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
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
            void * t5 = (void *)((uint8_t *)&((struct gm_fr_unpack_into_kqij1d2 *)((&F.h)->up))->l10);
            gm_b16_8 t6 = gm_l1;
            gm_tftb1odc739t5 t7 = gm_f_push_3ef746b(t5, t6);
            gm_l2 = t7;
            uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
            if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_src3s76fqo1a9jk, 102, 44);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 102, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_04pispc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_04pispc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sh7dbt4gdrljc));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(18));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqf2cda88og0i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_0rhberc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_0rhberc F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scueeuhqhtotc));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplj8iadiucj6so), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5pl0mkc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_5pl0mkc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sutk9gh8f6gmt));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(52));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplnr9pmjldkl35), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_8q6hdku(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_8q6hdku F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shsisdh16t023));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(27));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpllp788bsiqs1r), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ee96hnp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ee96hnp F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spmhk8jcpdrb0));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl8in37qe3omcj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ep7kj4f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0) {
    struct gm_fr_format_ep7kj4f F;
    gm_t19enj90vg8bd gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t19enj90vg8bd t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqvb94amh2nu2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(65));
    gm_f_format_sc59up9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplt1555ki49he5), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_l0gkulr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_l0gkulr F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_so52klh2mgoce));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(22));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplh7ffbsfnitkh), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_m1bjvb9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0) {
    struct gm_fr_format_m1bjvb9 F;
    gm_t19enj90vg8bd gm_l0; /* args */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t19enj90vg8bd t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(2));
    gm_f_format_sc59up9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplk34ojqiaho84), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_nk3ghj0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_nk3ghj0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4k36hl9k22np));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(62));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl3dghmndqn708), t3);
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
inline __attribute__((aligned(4))) void gm_f_init_s32ien0(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_init_s32ien0 F;
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tg8e8rhc3qio9 t2;
    GM_ST(uint32_t, ((uint8_t *)&t2 + 0), ((uint32_t)1779033703ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 4), ((uint32_t)3144134277ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 8), ((uint32_t)1013904242ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 12), ((uint32_t)2773480762ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 16), ((uint32_t)1359893119ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 20), ((uint32_t)2600822924ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 24), ((uint32_t)528734635ULL));
    GM_ST(uint32_t, ((uint8_t *)&t2 + 28), ((uint32_t)1541459225ULL));
    gm_tsn02dhhitgs4 t3;
    for (size_t i4 = 0; i4 < 64; i4++) GM_ST(uint8_t, (uint8_t *)&t3 + i4 * 1, ((uint8_t)0ULL));
    GM_ST(gm_tg8e8rhc3qio9, ((uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 0), t2);
    GM_ST(gm_tsn02dhhitgs4, ((uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 32), t3);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 96), ((uint64_t)0ULL));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_tq14qvifgdvpm *)gm_out) + 104), ((uint64_t)0ULL));
    goto gm_fdone;
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
inline __attribute__((aligned(4))) void gm_f_parent_4i83qet(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parent_4i83qet F;
    gm_b16_8 gm_l0; /* p */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2 = gm_l0;
    gm_f_parent_lk0iero((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t2);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_sanitize_modes_mvteloh(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_sanitize_modes_mvteloh F;
    gm_b16_8 gm_l0; /* root */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_so48b6sdoerku));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(17));
    gm_b16_8 t3 = gm_l0;
    gm_b0_1 t4 = gm_f_chmod_tree_sj4qk6r((void *)NULL, cx, NULL, t2, t3);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_discard_bdpju80(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_download_eek737p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_download_eek737p F;
    gm_b16_8 gm_l0; /* url */
    gm_b16_8 gm_l1; /* dir */
    gm_b16_8 gm_l2; /* path */
    gm_b16_8 gm_l3; /* max */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_b16_8 gm_l8; /* a */
    gm_b16_8 gm_l10; /* e */
    gm_b16_8 gm_l11; /* sum */
    GM_STACK_CHECK(&F, 1731u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tl572d7579hns gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_t3ujhu6qph7de gm_l17;
    {
        gm_b16_8 t9 = gm_l1;
        gm_f_make_dirs_gbp8nfr((void *)&gm_l12, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (t10) {
    } else {
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 8), t11);
        goto gm_fdone;
    }
    gm_b16_8 t12 = gm_l1;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sdaknqn35hokt));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(8));
    gm_b16_8 t14 = gm_f_private_name_83ij38e((void *)NULL, cx, NULL, t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l2 = t14;
    {
        gm_t19enj90vg8bd t15;
        GM_ST(int32_t, ((uint8_t *)&t15 + 0), ((int32_t)1073741824LL));
        gm_f_format_m1bjvb9((void *)&gm_l13, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t16 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t16) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_src3s76fqo1a9jk, 21, 12);
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
    gm_l3 = t17;
    {
        gm_trqfvmjtt4sij t18;
        gm_f_list_epvducg((void *)&t18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t18;
    }
    {
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sbtoph8phkseu));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(4));
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_s4ufkrqv2ag7g));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(6));
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_slqtudd4gk04i));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(8));
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sarlhchtvv77m));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(12));
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_ss6cscjfm4jfc));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(10));
        gm_b16_8 t24;
        GM_ST(void *, (uint8_t *)&t24, (void *)(gm_skpl66e1qvhab));
        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(7));
        gm_b16_8 t25;
        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s2dssfe3261t2));
        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(1));
        gm_b16_8 t26;
        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sasgv00j2p5nu));
        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(17));
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(gm_suq2iqmhi931e));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(2));
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_snauhge6bheis));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(13));
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_svubtddeidu1d));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(1));
        gm_b16_8 t30;
        GM_ST(void *, (uint8_t *)&t30, (void *)(gm_stjlu12rqiie7));
        GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)(12));
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_s104g112fsi3t));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(2));
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_slahfmk66v1v4));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(14));
        gm_b16_8 t33 = gm_l3;
        gm_b16_8 t34 = GM_LD(gm_b16_8, ((uint8_t *)&t33));
        gm_b16_8 t35;
        GM_ST(void *, (uint8_t *)&t35, (void *)(gm_ss9vj0o06n3e8));
        GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(7));
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s24qpbg6rctcd));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(16));
        gm_b16_8 t37;
        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_sgs65cm0mtgm4));
        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(13));
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_smaguor10so8u));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(11));
        gm_b16_8 t39;
        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s7cn098jq5gvg));
        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(8));
        gm_b16_8 t40 = gm_l2;
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s9pknf9g05okt));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(2));
        gm_b16_8 t42 = gm_l0;
        gm_t0ut772p7ffln t43;
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t19);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 16), t20);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 32), t21);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 48), t22);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 64), t23);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 80), t24);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 96), t25);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 112), t26);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 128), t27);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 144), t28);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 160), t29);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 176), t30);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 192), t31);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 208), t32);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 224), t34);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 240), t35);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 256), t36);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 272), t37);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 288), t38);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 304), t39);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 320), t40);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 336), t41);
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 352), t42);
        F.l5 = t43;
    }
    void * t44 = (void *)((uint8_t *)&F.l5);
    uint64_t t45 = ((uintptr_t)((uint64_t)23ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t46;
    GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t44 + (uintptr_t)((uint64_t)0ULL) * 16)));
    GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45));
    gm_l6 = t46;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t47 = gm_l7;
            uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t49 = (t47 < t48);
            if (!t49) {
                goto gm_lb1;
            }
            {
                void * t50 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t52 = gm_l7;
                {
                    {
                        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)t50) + (uintptr_t)t52 * 16));
                        gm_l8 = t53;
                        void * t54 = (void *)((uint8_t *)&F.l4);
                        gm_b16_8 t55 = gm_l8;
                        gm_tftb1odc739t5 t56 = gm_f_push_3ef746b(t54, t55);
                        gm_l14 = t56;
                        uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                        if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_src3s76fqo1a9jk, 30, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 23, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t58 = gm_l7;
            uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
            uint64_t t61 = r59;
            gm_l7 = t61;
        }
    }
gm_lb1:;
    {
        gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
        gm_f_run_mj5dobu((void *)&F.l9, (void *)NULL, cx, NULL, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint32_t t64 = GM_LD(uint32_t, (((uint8_t *)&F.l9) + 4));
            uint8_t t65 = ((t64 == ((uint32_t)127ULL)));
            if (!t65) {
                goto gm_lb7;
            }
            uint64_t t66 = GM_LD(uint64_t, ((((uint8_t *)&F.l9) + 24) + 8));
            uint8_t t67 = (t66 > ((uint64_t)0ULL));
            if (!t67) {
                goto gm_lb7;
            }
            uint8_t t68 = GM_LD(uint8_t, ((uint8_t *)&F.l9));
            uint8_t t69 = (uint8_t)!t68;
            if (!t69) {
                goto gm_lb7;
            }
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 24));
            gm_b16_8 t71;
            GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sg91clgj6l88m));
            GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(5));
            uint8_t t72 = gm_f_contains_gkki4kq(t70, t71);
            uint8_t t73 = (uint8_t)!t72;
            if (!t73) {
                goto gm_lb7;
            }
            {
                gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 24));
                gm_tnpifv20irnph t75;
                GM_ST(gm_b16_8, ((uint8_t *)&t75 + 0), t74);
                gm_f_format_ee96hnp((void *)&gm_l15, (void *)NULL, cx, NULL, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
            if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src3s76fqo1a9jk, 33, 17);
            {
                gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 32, 0);
                ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 8), t78);
                goto gm_fdone;
            }
        }
gm_lb7:;
    }
gm_lb6:;
    uint8_t t79 = GM_LD(uint8_t, ((uint8_t *)&F.l9));
    uint8_t t80 = (uint8_t)!t79;
    if (t80) {
        gm_b16_8 t81 = gm_l2;
        gm_ttln0d0hd70en t82 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t81);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        {
            gm_b16_8 t83 = gm_l0;
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 24));
            gm_b16_8 t85 = gm_f_trim_fdrfe6i(t84);
            gm_t6j6smj7qoctu t86;
            GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t83);
            GM_ST(gm_b16_8, ((uint8_t *)&t86 + 16), t85);
            gm_f_format_l0gkulr((void *)&gm_l16, (void *)NULL, cx, NULL, &t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t87 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
        if (!t87) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_src3s76fqo1a9jk, 37, 17);
        {
            gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
            gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 32, 0);
            ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 8), t89);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t90 = gm_l2;
        gm_f_file_sha256_llree8t((void *)&gm_l17, (void *)NULL, cx, NULL, t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
    if (t91) {
        gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
        gm_l11 = t92;
    } else {
        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
        gm_l10 = t93;
        gm_b16_8 t94 = gm_l2;
        gm_ttln0d0hd70en t95 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t94);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t96 = gm_l10;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 32, 0);
        ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 8), t96);
        goto gm_fdone;
    }
    {
        gm_b16_8 t97 = gm_l2;
        gm_b16_8 t98 = gm_l11;
        gm_t6j6smj7qoctu t99;
        GM_ST(gm_b16_8, ((uint8_t *)&t99 + 0), t97);
        GM_ST(gm_b16_8, ((uint8_t *)&t99 + 16), t98);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out))[0] = 0;
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&(*(gm_t22b1ebhvur5a *)gm_out) + 8), t99);
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
__attribute__((aligned(4))) void gm_f_file_sha256_llree8t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_file_sha256_llree8t F;
    gm_b16_8 gm_l0; /* path */
    gm_toe5fmr3vvf4d gm_l1; /* e */
    gm_tm98ap4112hee gm_l2; /* f */
    gm_b16_8 gm_l4; /* buf */
    gm_toe5fmr3vvf4d gm_l5; /* e */
    uint64_t gm_l6; /* n */
    gm_t7mlrlibdq1ja gm_l7; /* d */
    GM_STACK_CHECK(&F, 940u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t62ebbi2i5rkd gm_l8;
    gm_tabb710d9mq2e gm_l9;
    gm_tabb710d9mq2e gm_l10;
    gm_thf5kr0is9u9i gm_l11;
    gm_tabb710d9mq2e gm_l12;
    {
        gm_b16_8 t7 = gm_l0;
        gm_t4q2b5ibsrdik t8;
        GM_ST(uint8_t, ((uint8_t *)&t8 + 0), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&t8 + 1), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t8 + 2), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t8 + 3), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t8 + 4), ((uint8_t)0));
        GM_ST(uint8_t, ((uint8_t *)&t8 + 5), ((uint8_t)0));
        gm_node *pn9 = gm_find(cx, 2126215853);
        if (!pn9) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 53, 10);
        gm_t62ebbi2i5rkd t10;
        t10 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn9->clauses[0])(pn9->env, pn9->hctx, pn9, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t10;
        uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (t11) {
            gm_tm98ap4112hee t12 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l8) + 8));
            gm_l2 = t12;
        } else {
            gm_toe5fmr3vvf4d t13 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l8) + 8));
            gm_l1 = t13;
            {
                gm_b16_8 t14 = gm_l0;
                gm_toe5fmr3vvf4d t15 = gm_l1;
                gm_b16_8 t16 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t15);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6j6smj7qoctu t17;
                GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t14);
                GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t16);
                gm_f_format_04pispc((void *)&gm_l9, (void *)NULL, cx, NULL, &t17);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
            if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src3s76fqo1a9jk, 53, 62);
            {
                gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
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
            gm_tq14qvifgdvpm t21;
            gm_f_init_s32ien0((void *)&t21);
            F.l3 = t21;
        }
        {
            unsigned __int128 n22 = (unsigned __int128)((uint64_t)16ULL);
            if (n22 >= 64) gm_trap_shift((uint64_t)n22, "usize", gm_src3s76fqo1a9jk, 56, 21);
            uint64_t r23 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n22);
            bool o24 = (uint64_t)(r23 >> (int)n22) != ((uint64_t)1ULL);
            if (o24) gm_trap("integer overflow in `<<`", gm_src3s76fqo1a9jk, 56, 21);
            uint64_t t25 = r23;
            gm_f_make_2ip78bj((void *)&gm_l10, (void *)NULL, cx, NULL, t25, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kh1ssdqq46n)));
            if (gm_unw.kind) {
                goto gm_ld6_1;
            }
        }
        uint8_t t26 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
        if (!t26) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src3s76fqo1a9jk, 56, 12);
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
        gm_l4 = t27;
        {
            for (;;) {
                if (!((uint8_t)1)) {
                    goto gm_lb2;
                }
                {
                    gm_tm98ap4112hee t28 = gm_l2;
                    gm_b16_8 t29 = gm_l4;
                    gm_node *pn30 = gm_find(cx, 2126215853);
                    if (!pn30) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 59, 14);
                    gm_thf5kr0is9u9i t31;
                    t31 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn30->clauses[2])(pn30->env, pn30->hctx, pn30, t28, t29);
                    if (gm_unw.kind) {
                        goto gm_ld6_2;
                    }
                    gm_l11 = t31;
                    uint8_t t32 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                    if (t32) {
                        uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                        gm_l6 = t33;
                    } else {
                        gm_toe5fmr3vvf4d t34 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l11) + 8));
                        gm_l5 = t34;
                        {
                            gm_b16_8 t35 = gm_l0;
                            gm_toe5fmr3vvf4d t36 = gm_l5;
                            gm_b16_8 t37 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t36);
                            if (gm_unw.kind) {
                                goto gm_ld6_2;
                            }
                            gm_t6j6smj7qoctu t38;
                            GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t35);
                            GM_ST(gm_b16_8, ((uint8_t *)&t38 + 16), t37);
                            gm_f_format_04pispc((void *)&gm_l12, (void *)NULL, cx, NULL, &t38);
                            if (gm_unw.kind) {
                                goto gm_ld6_2;
                            }
                        }
                        uint8_t t39 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t39) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src3s76fqo1a9jk, 59, 46);
                        {
                            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                            GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
                            ((uint8_t *)&F.ret)[0] = 1;
                            GM_ST(gm_b16_8, ((uint8_t *)&F.ret + 8), t41);
                            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
                            goto gm_ld6_2;
                        }
                    }
                    uint64_t t42 = gm_l6;
                    uint8_t t43 = ((t42 == ((uint64_t)0ULL)));
                    if (t43) {
                        goto gm_lb2;
                    }
                    void * t44 = (void *)((uint8_t *)&F.l3);
                    void * t45 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t47 = gm_l6;
                    uint64_t t48 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t49 = (uintptr_t)t47;
                    if (t48 > t49 || t49 > (uintptr_t)t46) gm_trap_slice(t48, t49, t46, gm_src3s76fqo1a9jk, 61, 18);
                    uint64_t t50 = ((uintptr_t)t47) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t51;
                    GM_ST(void *, (uint8_t *)&t51, (void *)(((uint8_t *)t45 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(t50));
                    gm_b16_8 t52 = GM_LD(gm_b16_8, ((uint8_t *)&t51));
                    gm_f_update_pn3fq6a(t44, t52);
                }
gm_lb4:;
            }
        }
gm_lb2:;
        {
            void * t53 = (void *)((uint8_t *)&F.l3);
            gm_t7mlrlibdq1ja t54;
            gm_f_final_rg5ko3p((void *)&t54, t53);
            gm_l7 = t54;
        }
        {
            void * t55 = (void *)((uint8_t *)&gm_l7);
            uint64_t t56 = ((uintptr_t)((uint64_t)32ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t57;
            GM_ST(void *, (uint8_t *)&t57, (void *)(((uint8_t *)t55 + (uintptr_t)((uint64_t)0ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(t56));
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
            gm_b16_8 t59 = gm_f_hex_euih242((void *)NULL, cx, NULL, t58);
            if (gm_unw.kind) {
                goto gm_ld6_2;
            }
            GM_ST(uint8_t, (uint8_t *)&F.ret + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&F.ret + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&F.ret + 4, 0);
            ((uint8_t *)&F.ret)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&F.ret + 8), t59);
            gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = ((((uint8_t *)&F.ret))[0] != 0);
            goto gm_ld6_2;
        }
    }
    {
        gm_b16_8 t60 = gm_l4;
        gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
        gm_b0_1 t62 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t61, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0kae0be6jj)));
    }
    {
        gm_tm98ap4112hee t63 = gm_l2;
        gm_node *pn64 = gm_find(cx, 2126215853);
        if (!pn64) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 54, 11);
        gm_b0_1 t65;
        t65 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn64->clauses[1])(pn64->env, pn64->hctx, pn64, t63);
    }
    goto gm_ls66;
gm_ld6_2:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_b16_8 t67 = gm_l4;
        gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
        gm_b0_1 t69 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t68, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0kae0be6jj)));
        gm_unw = gm_saved;
    }
gm_ld6_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        gm_tm98ap4112hee t70 = gm_l2;
        gm_node *pn71 = gm_find(cx, 2126215853);
        if (!pn71) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 54, 11);
        gm_b0_1 t72;
        t72 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn71->clauses[1])(pn71->env, pn71->hctx, pn71, t70);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls66:;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_1avb45k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_gp0er0a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_gp0er0a F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* line */
    GM_STACK_CHECK(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    gm_tqs5p4n2tq19v gm_l3;
    gm_tqs5p4n2tq19v gm_l4;
    gm_tftb1odc739t5 gm_l5;
    {
        {
            gm_b16_8 t4 = gm_l0;
            gm_l1 = t4;
            void * t5 = (void *)((uint8_t *)&((struct gm_fr_unpack_into_kqij1d2 *)((&F.h)->up))->l12);
            void * t6 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            uint8_t t8 = (((uint64_t)0ULL) < t7);
            if (t8) {
                uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 1));
                gm_tqs5p4n2tq19v t10;
                GM_ST(uint8_t, ((uint8_t *)&t10), t9);
                ((uint8_t *)&t10)[1] = 1;
                gm_l3 = t10;
            } else {
                gm_tqs5p4n2tq19v t11;
                memset(&t11, 0, sizeof t11);
                gm_l3 = t11;
            }
            gm_tqs5p4n2tq19v t12 = gm_l3;
            gm_l4 = t12;
            uint8_t t13 = (uint8_t)((((uint8_t *)&gm_l4))[1] != 0);
            if (t13) {
                uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&gm_l4));
                gm_l2 = t14;
            } else {
                gm_l2 = ((uint8_t)45ULL);
            }
            uint8_t t15 = gm_l2;
            gm_tftb1odc739t5 t16 = gm_f_push_t83rg2m(t5, t15);
            gm_l5 = t16;
            uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_src3s76fqo1a9jk, 104, 44);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 104, 5);
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_nnvr1n3(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_for_body_nnvr1n3 F;
    gm_te2n4j2vtm53d gm_l0; /* <item> */
    gm_te2n4j2vtm53d gm_l1; /* r */
    gm_toe5fmr3vvf4d gm_l2; /* err */
    gm_tibupt4dmcqai gm_l3; /* e */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    gm_tabb710d9mq2e gm_l4;
    gm_tabb710d9mq2e gm_l5;
    gm_tftb1odc739t5 gm_l6;
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
                {
                    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_place_snpjvsu *)((&F.h)->up))->l0));
                    gm_toe5fmr3vvf4d t7 = gm_l2;
                    gm_b16_8 t8 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t7);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t9;
                    GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t6);
                    GM_ST(gm_b16_8, ((uint8_t *)&t9 + 16), t8);
                    gm_f_format_04pispc((void *)&gm_l4, (void *)NULL, cx, NULL, &t9);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                if (!t10) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 8, gm_src3s76fqo1a9jk, 141, 38);
                {
                    gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
                    gm_tl572d7579hns t13;
                    GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
                    ((uint8_t *)&t13)[0] = 1;
                    GM_ST(gm_b16_8, ((uint8_t *)&t13 + 8), t12);
                    uint8_t t14 = ((((uint8_t *)&t13))[0] != 0);
                    ((struct gm_fr_place_snpjvsu *)((&F.h)->up))->ret = t13;
                    gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t14;
                    goto gm_lad;
                }
            }
            {
                void * t15 = (void *)((uint8_t *)&((struct gm_fr_place_snpjvsu *)((&F.h)->up))->l2);
                gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l3));
                gm_f_dupe_g9iqpvn((void *)&gm_l5, (void *)NULL, cx, NULL, t16, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kr7fqi9m0ih)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t17 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                if (!t17) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 8, gm_src3s76fqo1a9jk, 142, 20);
                gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
                gm_tftb1odc739t5 t20 = gm_f_push_3ef746b(t15, t19);
                gm_l6 = t20;
            }
            uint8_t t21 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
            if (!t21) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src3s76fqo1a9jk, 142, 9);
            goto gm_lb1;
        }
gm_lb2:;
        gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 140, 5);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tv3u4f0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_leaves_directory_kq5llq4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_leaves_directory_kq5llq4 F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* <tmp> */
    GM_STACK_CHECK(&F, 115u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = gm_l0;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_st3ja0nqi37bv));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
    uint8_t t5 = gm_f_starts_with_6vvt01e(t3, t4);
    if (t5) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    gm_b16_8 t6 = gm_l0;
    gm_l1 = t6;
    {
        gm_node wn7 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlucu919n5n4, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t10 = gm_l1;
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(1));
        gm_b0_1 t12 = gm_f_split_obtinjv_s8m2rsv((void *)NULL, (&wn7), NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_ll9;
        }
    }
    goto gm_ls13;
gm_ll9:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we8; }
    goto gm_lad;
gm_ls13:;
gm_lb1:;
we8:;
    F.ret = ((uint8_t)0);
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
inline __attribute__((aligned(4))) void gm_f_place_snpjvsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_place_snpjvsu F;
    gm_b16_8 gm_l1; /* dest */
    gm_b16_8 gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* src */
    gm_b16_8 gm_l5; /* one */
    gm_t5j14m8ru132p gm_l6; /* info */
    gm_toe5fmr3vvf4d gm_l7; /* e */
    GM_STACK_CHECK(&F, 594u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_tih6h4avv5nrb gm_l8;
    gm_ttln0d0hd70en gm_l9;
    gm_tabb710d9mq2e gm_l10;
    {
        gm_trqfvmjtt4sij t5;
        gm_f_list_epvducg((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t5;
    }
    gm_b16_8 t6 = F.l0;
    gm_l3 = t6;
    {
        gm_node wn7 = {.eff = 2076700068, .kind = GM_LIT, .clauses = gm_hlh176qr0nti, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t10 = gm_l3;
        gm_b0_1 t11 = gm_f_entries_44o5n9f_s40i17b((void *)NULL, (&wn7), NULL, t10);
        if (gm_unw.kind) {
            goto gm_ll9;
        }
    }
    goto gm_ls12;
gm_ll9:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we8; }
    goto gm_lad;
gm_ls12:;
gm_lb1:;
we8:;
    gm_b16_8 t13 = F.l0;
    gm_l4 = t13;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
    uint8_t t15 = ((t14 == ((uint64_t)1ULL)));
    if (t15) {
        gm_b16_8 t16 = F.l0;
        void * t17 = GM_LD(void *, ((uint8_t *)&F.l2));
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t18, gm_src3s76fqo1a9jk, 146, 26);
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)t17) + (uintptr_t)((uint64_t)0ULL) * 16));
        gm_b16_8 t20 = gm_f_join_sb1m5es((void *)NULL, cx, NULL, t16, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t20;
        {
            {
                {
                    gm_b16_8 t21 = gm_l5;
                    gm_node *pn22 = gm_find(cx, 2126215853);
                    if (!pn22) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 147, 20);
                    gm_tih6h4avv5nrb t23;
                    ((void (*)(void *, void *, gm_node *, gm_node *, gm_b16_8))pn22->clauses[13])((void *)&t23, pn22->env, pn22->hctx, pn22, t21);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l8 = t23;
                }
                uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                if (!t24) {
                    goto gm_lb3;
                }
                {
                    memmove(((uint8_t *)&gm_l6), (((uint8_t *)&gm_l8) + 8), 32);
                }
                gm_t9qkh0rsooc8d t25 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l6) + 8));
                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
                uint8_t t27 = (uint8_t)((t26 == ((uint8_t)1ULL)));
                gm_t9qkh0rsooc8d t28;
                GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)1ULL));
                if (t27) {
                    gm_b16_8 t29 = gm_l5;
                    gm_l4 = t29;
                }
                goto gm_lb2;
            }
gm_lb3:;
        }
gm_lb2:;
    }
    gm_b16_8 t30 = gm_l4;
    gm_b16_8 t31 = gm_l1;
    gm_node *pn32 = gm_find(cx, 2126215853);
    if (!pn32) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 151, 5);
    gm_ttln0d0hd70en t33;
    t33 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_b16_8))pn32->clauses[7])(pn32->env, pn32->hctx, pn32, t30, t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t33;
    uint8_t t34 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (t34) {
    } else {
        gm_toe5fmr3vvf4d t35 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l9) + 4));
        gm_l7 = t35;
        gm_b16_8 t36 = gm_l1;
        uint8_t t37 = gm_f_is_dir_2kkkskr((void *)NULL, cx, NULL, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t38 = (uint8_t)!t37;
        if (t38) {
            {
                gm_b16_8 t39 = gm_l1;
                gm_toe5fmr3vvf4d t40 = gm_l7;
                gm_b16_8 t41 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6j6smj7qoctu t42;
                GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t39);
                GM_ST(gm_b16_8, ((uint8_t *)&t42 + 16), t41);
                gm_f_format_5uun4vl((void *)&gm_l10, (void *)NULL, cx, NULL, &t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 8, gm_src3s76fqo1a9jk, 153, 39);
            {
                gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 8));
                gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&t44));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t45);
                goto gm_fdone;
            }
        }
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
inline __attribute__((aligned(4))) void gm_f_unpack_eb2aboi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_unpack_eb2aboi F;
    gm_b16_8 gm_l0; /* archive */
    gm_b16_8 gm_l1; /* dest */
    gm_b16_8 gm_l2; /* parent_dir */
    gm_b16_8 gm_l3; /* tmp */
    gm_toe5fmr3vvf4d gm_l4; /* e */
    gm_tl572d7579hns gm_l5; /* result */
    GM_STACK_CHECK(&F, 547u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l6;
    gm_tl572d7579hns gm_l7;
    gm_ttln0d0hd70en gm_l8;
    gm_tabb710d9mq2e gm_l9;
    uint8_t gm_l10;
    {
        gm_b16_8 t5 = gm_l1;
        gm_f_parent_4i83qet((void *)&gm_l6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t6 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
    if (t6) {
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l6));
        gm_l2 = t7;
    } else {
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s0c2823k4ul3o));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(16));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t8);
        goto gm_fdone;
    }
    {
        gm_b16_8 t9 = gm_l2;
        gm_f_make_dirs_gbp8nfr((void *)&gm_l7, (void *)NULL, cx, NULL, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t10 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
    if (t10) {
    } else {
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l7) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t11);
        goto gm_fdone;
    }
    gm_b16_8 t12 = gm_l2;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_s5v7o1bkddp5s));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(6));
    gm_b16_8 t14 = gm_f_private_name_83ij38e((void *)NULL, cx, NULL, t12, t13);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t14;
    gm_b16_8 t15 = gm_l3;
    gm_node *pn16 = gm_find(cx, 2126215853);
    if (!pn16) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 79, 5);
    gm_ttln0d0hd70en t17;
    t17 = ((gm_ttln0d0hd70en (*)(void *, gm_node *, gm_node *, gm_b16_8))pn16->clauses[8])(pn16->env, pn16->hctx, pn16, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t17;
    uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (t18) {
    } else {
        gm_toe5fmr3vvf4d t19 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l8) + 4));
        gm_l4 = t19;
        {
            gm_b16_8 t20 = gm_l3;
            gm_toe5fmr3vvf4d t21 = gm_l4;
            gm_b16_8 t22 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t23;
            GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t20);
            GM_ST(gm_b16_8, ((uint8_t *)&t23 + 16), t22);
            gm_f_format_5uun4vl((void *)&gm_l9, (void *)NULL, cx, NULL, &t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_src3s76fqo1a9jk, 79, 42);
        {
            gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t26);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t27 = gm_l0;
        gm_b16_8 t28 = gm_l3;
        gm_f_unpack_into_kqij1d2((void *)&gm_l5, (void *)NULL, cx, NULL, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
            if (t29) {
                goto gm_lb2;
            }
            gm_l10 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb2:;
        {
            gm_l10 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 81, 8);
    }
gm_lb1:;
    uint8_t t30 = gm_l10;
    if (t30) {
        gm_b16_8 t31 = gm_l3;
        gm_b16_8 t32 = gm_l1;
        gm_f_place_snpjvsu((void *)&gm_l5, (void *)NULL, cx, NULL, t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_b16_8 t33 = gm_l3;
    gm_ttln0d0hd70en t34 = gm_f_remove_all_h0k1qkh((void *)NULL, cx, NULL, t33);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        memmove(gm_out, ((uint8_t *)&gm_l5), 24);
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
__attribute__((aligned(4))) void gm_f_unpack_into_kqij1d2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_unpack_into_kqij1d2 F;
    gm_b16_8 gm_l0; /* archive */
    gm_b16_8 gm_l1; /* dir */
    gm_toe5fmr3vvf4d gm_l2; /* e */
    gm_tm98ap4112hee gm_l3; /* f */
    gm_t5liq177ejcou gm_l4; /* magic */
    uint64_t gm_l5; /* n */
    gm_b16_8 gm_l11; /* <tmp> */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* count */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    gm_b16_8 gm_l18; /* name */
    uint8_t gm_l19; /* kind */
    gm_b16_8 gm_l20; /* what */
    GM_STACK_CHECK(&F, 2078u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t62ebbi2i5rkd gm_l23;
    gm_tabb710d9mq2e gm_l24;
    gm_thf5kr0is9u9i gm_l25;
    uint8_t gm_l26;
    uint8_t gm_l27;
    gm_tabb710d9mq2e gm_l28;
    gm_tabb710d9mq2e gm_l29;
    gm_tf1bjg81u0glv gm_l30;
    gm_tabb710d9mq2e gm_l31;
    gm_tqs5p4n2tq19v gm_l32;
    gm_tqs5p4n2tq19v gm_l33;
    gm_tabb710d9mq2e gm_l34;
    gm_tabb710d9mq2e gm_l35;
    gm_tabb710d9mq2e gm_l36;
    gm_b16_8 t9 = gm_l0;
    gm_t4q2b5ibsrdik t10;
    GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)1));
    GM_ST(uint8_t, ((uint8_t *)&t10 + 1), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t10 + 2), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t10 + 3), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t10 + 4), ((uint8_t)0));
    GM_ST(uint8_t, ((uint8_t *)&t10 + 5), ((uint8_t)0));
    gm_node *pn11 = gm_find(cx, 2126215853);
    if (!pn11) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 87, 10);
    gm_t62ebbi2i5rkd t12;
    t12 = ((gm_t62ebbi2i5rkd (*)(void *, gm_node *, gm_node *, gm_b16_8, gm_t4q2b5ibsrdik))pn11->clauses[0])(pn11->env, pn11->hctx, pn11, t9, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l23 = t12;
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
    if (t13) {
        gm_tm98ap4112hee t14 = GM_LD(gm_tm98ap4112hee, (((uint8_t *)&gm_l23) + 8));
        gm_l3 = t14;
    } else {
        gm_toe5fmr3vvf4d t15 = GM_LD(gm_toe5fmr3vvf4d, (((uint8_t *)&gm_l23) + 8));
        gm_l2 = t15;
        {
            gm_b16_8 t16 = gm_l0;
            gm_toe5fmr3vvf4d t17 = gm_l2;
            gm_b16_8 t18 = gm_f_io_message_p7jbrea((void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6j6smj7qoctu t19;
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t16);
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 16), t18);
            gm_f_format_04pispc((void *)&gm_l24, (void *)NULL, cx, NULL, &t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 8, gm_src3s76fqo1a9jk, 87, 65);
        {
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l24) + 8));
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t22);
            goto gm_fdone;
        }
    }
    gm_tm98ap4112hee t23 = gm_l3;
    void * t24 = (void *)((uint8_t *)&gm_l4);
    uint64_t t25 = ((uintptr_t)((uint64_t)2ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t24 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    gm_node *pn27 = gm_find(cx, 2126215853);
    if (!pn27) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 89, 10);
    gm_thf5kr0is9u9i t28;
    t28 = ((gm_thf5kr0is9u9i (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee, gm_b16_8))pn27->clauses[2])(pn27->env, pn27->hctx, pn27, t23, t26);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l25 = t28;
    uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
    if (t29) {
        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
        gm_l5 = t30;
    } else {
        gm_l5 = ((uint64_t)0ULL);
    }
    gm_tm98ap4112hee t31 = gm_l3;
    gm_node *pn32 = gm_find(cx, 2126215853);
    if (!pn32) gm_trap("no handler installed for effect `Fs`", gm_src3s76fqo1a9jk, 90, 5);
    gm_b0_1 t33;
    t33 = ((gm_b0_1 (*)(void *, gm_node *, gm_node *, gm_tm98ap4112hee))pn32->clauses[1])(pn32->env, pn32->hctx, pn32, t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t34 = gm_l5;
    uint8_t t35 = (t34 < ((uint64_t)2ULL));
    if (t35) {
        gm_l27 = ((uint8_t)1);
    } else {
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + (uintptr_t)((uint64_t)0ULL) * 1));
        uint8_t t37 = (!(t36 == ((uint8_t)31ULL)));
        gm_l27 = t37;
    }
    uint8_t t38 = gm_l27;
    if (t38) {
        gm_l26 = ((uint8_t)1);
    } else {
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + (uintptr_t)((uint64_t)1ULL) * 1));
        uint8_t t40 = (!(t39 == ((uint8_t)139ULL)));
        gm_l26 = t40;
    }
    uint8_t t41 = gm_l26;
    if (t41) {
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sa5v7oq7un6c6));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(83));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t42);
        goto gm_fdone;
    }
    {
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s1ktljc2oac64));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(3));
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sqviis18fpvj7));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(4));
        gm_b16_8 t45 = gm_l0;
        gm_t9bmh80edou9c t46;
        GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t43);
        GM_ST(gm_b16_8, ((uint8_t *)&t46 + 16), t44);
        GM_ST(gm_b16_8, ((uint8_t *)&t46 + 32), t45);
        F.l6 = t46;
    }
    {
        void * t47 = (void *)((uint8_t *)&F.l6);
        uint64_t t48 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(((uint8_t *)t47 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(t48));
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        gm_f_run_mj5dobu((void *)&F.l7, (void *)NULL, cx, NULL, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)&F.l7));
    uint8_t t52 = (uint8_t)!t51;
    if (t52) {
        {
            gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 24));
            gm_b16_8 t54 = gm_f_trim_fdrfe6i(t53);
            gm_tnpifv20irnph t55;
            GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
            gm_f_format_8q6hdku((void *)&gm_l28, (void *)NULL, cx, NULL, t55);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t56 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
        if (!t56) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 8, gm_src3s76fqo1a9jk, 97, 35);
        {
            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l28) + 8));
            gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t58);
            goto gm_fdone;
        }
    }
    {
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s1ktljc2oac64));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(3));
        gm_b16_8 t60;
        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_sfpeqbitcbq42));
        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(5));
        gm_b16_8 t61 = gm_l0;
        gm_t9bmh80edou9c t62;
        GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t59);
        GM_ST(gm_b16_8, ((uint8_t *)&t62 + 16), t60);
        GM_ST(gm_b16_8, ((uint8_t *)&t62 + 32), t61);
        F.l8 = t62;
    }
    {
        void * t63 = (void *)((uint8_t *)&F.l8);
        uint64_t t64 = ((uintptr_t)((uint64_t)3ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t65;
        GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t63 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(t64));
        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
        gm_f_run_mj5dobu((void *)&F.l9, (void *)NULL, cx, NULL, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t67 = GM_LD(uint8_t, ((uint8_t *)&F.l9));
    uint8_t t68 = (uint8_t)!t67;
    if (t68) {
        {
            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 24));
            gm_b16_8 t70 = gm_f_trim_fdrfe6i(t69);
            gm_tnpifv20irnph t71;
            GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t70);
            gm_f_format_8q6hdku((void *)&gm_l29, (void *)NULL, cx, NULL, t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t72 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
        if (!t72) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_src3s76fqo1a9jk, 100, 35);
        {
            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l29) + 8));
            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t74);
            goto gm_fdone;
        }
    }
    {
        gm_trqfvmjtt4sij t75;
        gm_f_list_epvducg((void *)&t75, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t75;
    }
    gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 8));
    gm_l11 = t76;
    {
        gm_node wn77 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlq1reae6pf9, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t80 = gm_l11;
        gm_b0_1 t81 = gm_f_lines_9uns0k2_s9kjq5m((void *)NULL, (&wn77), NULL, t80);
        if (gm_unw.kind) {
            goto gm_ll79;
        }
    }
    goto gm_ls82;
gm_ll79:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we78; }
    goto gm_lad;
gm_ls82:;
gm_lb1:;
we78:;
    {
        gm_trqfvmjtt4sij t83;
        gm_f_list_uvfu1k1((void *)&t83, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l12 = t83;
    }
    gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 8));
    gm_l13 = t84;
    {
        gm_node wn85 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hlhtabotcjhg, .env = (void *)&F.h, .hctx = cx, .with_frame = &F.h, .with_label = 1, .result = (void *)&F.w1, .fw = NULL, .next = cx};
        gm_b16_8 t88 = gm_l13;
        gm_b0_1 t89 = gm_f_lines_9uns0k2_s84ppfm((void *)NULL, (&wn85), NULL, t88);
        if (gm_unw.kind) {
            goto gm_ll87;
        }
    }
    goto gm_ls90;
gm_ll87:;
    if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 1) { gm_unw.kind = GM_NONE; goto we86; }
    goto gm_lad;
gm_ls90:;
gm_lb2:;
we86:;
    gm_l14 = ((uint64_t)0ULL);
    gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
    gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
    gm_l15 = t92;
    gm_l16 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t94 = gm_l16;
            uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
            uint8_t t96 = (t94 < t95);
            if (!t96) {
                goto gm_lb3;
            }
            {
                uint64_t t97 = gm_l16;
                void * t98 = GM_LD(void *, ((uint8_t *)&gm_l15));
                uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                uint64_t t100 = gm_l16;
                gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)t98) + (uintptr_t)t100 * 16));
                GM_ST(uint64_t, ((uint8_t *)&gm_l30 + 0), t97);
                GM_ST(gm_b16_8, ((uint8_t *)&gm_l30 + 8), t101);
                {
                    {
                        uint64_t t102 = GM_LD(uint64_t, ((uint8_t *)&gm_l30));
                        gm_l17 = t102;
                        gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l30) + 8));
                        gm_l18 = t103;
                        uint64_t t104 = gm_l14;
                        uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                        if (o106) gm_trap("integer overflow in `+`", gm_src3s76fqo1a9jk, 107, 9);
                        uint64_t t107 = r105;
                        gm_l14 = t107;
                        uint64_t t108 = gm_l14;
                        uint8_t t109 = (t108 > ((uint64_t)1048576ULL));
                        if (t109) {
                            {
                                gm_t19enj90vg8bd t110;
                                GM_ST(int32_t, ((uint8_t *)&t110 + 0), ((int32_t)1048576LL));
                                gm_f_format_ep7kj4f((void *)&gm_l31, (void *)NULL, cx, NULL, t110);
                                if (gm_unw.kind) {
                                    goto gm_ll93;
                                }
                            }
                            uint8_t t111 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                            if (!t111) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_src3s76fqo1a9jk, 109, 21);
                            {
                                gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l31) + 8));
                                gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t113);
                                goto gm_fdone;
                            }
                        }
                        void * t114 = GM_LD(void *, ((uint8_t *)&F.l12));
                        uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&F.l12) + 8));
                        uint64_t t116 = gm_l17;
                        uint8_t t117 = (t116 < t115);
                        if (t117) {
                            uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)t114) + (uintptr_t)t116 * 1));
                            gm_tqs5p4n2tq19v t119;
                            GM_ST(uint8_t, ((uint8_t *)&t119), t118);
                            ((uint8_t *)&t119)[1] = 1;
                            gm_l32 = t119;
                        } else {
                            gm_tqs5p4n2tq19v t120;
                            memset(&t120, 0, sizeof t120);
                            gm_l32 = t120;
                        }
                        gm_tqs5p4n2tq19v t121 = gm_l32;
                        gm_l33 = t121;
                        uint8_t t122 = (uint8_t)((((uint8_t *)&gm_l33))[1] != 0);
                        if (t122) {
                            uint8_t t123 = GM_LD(uint8_t, ((uint8_t *)&gm_l33));
                            gm_l19 = t123;
                        } else {
                            gm_l19 = ((uint8_t)45ULL);
                        }
                        uint8_t t124 = gm_l19;
                        gm_toh0ootodhjue t125;
                        GM_ST(uint8_t, ((uint8_t *)&t125 + 0), ((uint8_t)45ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t125 + 1), ((uint8_t)100ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t125 + 2), ((uint8_t)108ULL));
                        uint8_t t126 = 0;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t125) + 0, ((uint8_t *)&t124))) t126 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t125) + 1, ((uint8_t *)&t124))) t126 = 1;
                        if (gm_eqjmccqdb90f(((uint8_t *)&t125) + 2, ((uint8_t *)&t124))) t126 = 1;
                        uint8_t t127 = (uint8_t)(!t126);
                        if (t127) {
                            uint8_t t128 = gm_l19;
                            uint8_t t129 = ((t128 == ((uint8_t)104ULL)));
                            if (t129) {
                                gm_b16_8 t130;
                                GM_ST(void *, (uint8_t *)&t130, (void *)(gm_srrq1irp1jfep));
                                GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(11));
                                gm_l20 = t130;
                            } else {
                                gm_b16_8 t131;
                                GM_ST(void *, (uint8_t *)&t131, (void *)(gm_s8nekj984lthm));
                                GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(14));
                                gm_l20 = t131;
                            }
                            {
                                gm_b16_8 t132 = gm_l20;
                                gm_b16_8 t133 = gm_l18;
                                gm_t6j6smj7qoctu t134;
                                GM_ST(gm_b16_8, ((uint8_t *)&t134 + 0), t132);
                                GM_ST(gm_b16_8, ((uint8_t *)&t134 + 16), t133);
                                gm_f_format_5pl0mkc((void *)&gm_l34, (void *)NULL, cx, NULL, &t134);
                                if (gm_unw.kind) {
                                    goto gm_ll93;
                                }
                            }
                            uint8_t t135 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                            if (!t135) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_src3s76fqo1a9jk, 114, 21);
                            {
                                gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l34) + 8));
                                gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t137);
                                goto gm_fdone;
                            }
                        }
                        gm_b16_8 t138 = gm_l18;
                        uint8_t t139 = gm_f_leaves_directory_kq5llq4((void *)NULL, cx, NULL, t138);
                        if (gm_unw.kind) {
                            goto gm_ll93;
                        }
                        if (t139) {
                            {
                                gm_b16_8 t140 = gm_l18;
                                gm_tnpifv20irnph t141;
                                GM_ST(gm_b16_8, ((uint8_t *)&t141 + 0), t140);
                                gm_f_format_nk3ghj0((void *)&gm_l35, (void *)NULL, cx, NULL, t141);
                                if (gm_unw.kind) {
                                    goto gm_ll93;
                                }
                            }
                            uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                            if (!t142) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 8, gm_src3s76fqo1a9jk, 117, 21);
                            {
                                gm_b16_8 t143 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                                gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
                                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t144);
                                goto gm_fdone;
                            }
                        }
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_trap("no match arm matched the scrutinee", gm_src3s76fqo1a9jk, 106, 5);
                }
gm_lb6:;
            }
gm_lb5:;
            uint64_t t145 = gm_l16;
            uint64_t r146; bool o147 = __builtin_add_overflow(t145, ((uint64_t)1ULL), &r146);
            uint64_t t148 = r146;
            gm_l16 = t148;
        }
    }
    goto gm_ls149;
gm_ll93:;
    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb3; }
        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb5; }
    }
    goto gm_lad;
gm_ls149:;
gm_lb3:;
    {
        gm_b16_8 t150;
        GM_ST(void *, (uint8_t *)&t150, (void *)(gm_s1ktljc2oac64));
        GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(3));
        gm_b16_8 t151;
        GM_ST(void *, (uint8_t *)&t151, (void *)(gm_sos62dipmhb8k));
        GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(5));
        gm_b16_8 t152 = gm_l0;
        gm_b16_8 t153;
        GM_ST(void *, (uint8_t *)&t153, (void *)(gm_sppohbjp90mol));
        GM_ST(uintptr_t, (uint8_t *)&t153 + 8, (uintptr_t)(2));
        gm_b16_8 t154 = gm_l1;
        gm_b16_8 t155;
        GM_ST(void *, (uint8_t *)&t155, (void *)(gm_sobnp9i68hvdj));
        GM_ST(uintptr_t, (uint8_t *)&t155 + 8, (uintptr_t)(15));
        gm_b16_8 t156;
        GM_ST(void *, (uint8_t *)&t156, (void *)(gm_snhr2rebnnkc2));
        GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(21));
        gm_tdv3dkjvo1l7t t157;
        GM_ST(gm_b16_8, ((uint8_t *)&t157 + 0), t150);
        GM_ST(gm_b16_8, ((uint8_t *)&t157 + 16), t151);
        GM_ST(gm_b16_8, ((uint8_t *)&t157 + 32), t152);
        GM_ST(gm_b16_8, ((uint8_t *)&t157 + 48), t153);
        GM_ST(gm_b16_8, ((uint8_t *)&t157 + 64), t154);
        GM_ST(gm_b16_8, ((uint8_t *)&t157 + 80), t155);
        GM_ST(gm_b16_8, ((uint8_t *)&t157 + 96), t156);
        F.l21 = t157;
    }
    {
        void * t158 = (void *)((uint8_t *)&F.l21);
        uint64_t t159 = ((uintptr_t)((uint64_t)7ULL)) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t160;
        GM_ST(void *, (uint8_t *)&t160, (void *)(((uint8_t *)t158 + (uintptr_t)((uint64_t)0ULL) * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(t159));
        gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
        gm_f_run_mj5dobu((void *)&F.l22, (void *)NULL, cx, NULL, t161);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t162 = GM_LD(uint8_t, ((uint8_t *)&F.l22));
    uint8_t t163 = (uint8_t)!t162;
    if (t163) {
        {
            gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 24));
            gm_b16_8 t165 = gm_f_trim_fdrfe6i(t164);
            gm_tnpifv20irnph t166;
            GM_ST(gm_b16_8, ((uint8_t *)&t166 + 0), t165);
            gm_f_format_0rhberc((void *)&gm_l36, (void *)NULL, cx, NULL, t166);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        uint8_t t167 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
        if (!t167) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_src3s76fqo1a9jk, 123, 27);
        {
            gm_b16_8 t168 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
            gm_b16_8 t169 = GM_LD(gm_b16_8, ((uint8_t *)&t168));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), t169);
            goto gm_fdone;
        }
    }
    gm_b16_8 t170 = gm_l1;
    gm_b0_1 t171 = gm_f_sanitize_modes_mvteloh((void *)NULL, cx, NULL, t170);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 16, 0);
        ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tl572d7579hns *)gm_out) + 8), GM_ZERO(gm_b0_1));
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

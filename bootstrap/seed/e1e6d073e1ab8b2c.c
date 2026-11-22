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
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_b16_8 f3; } gm_t26d2uc07m5o2;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; } gm_t9d4sf0vqn7dt;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; gm_tl874t2atnlh3 f3; } gm_tis7ttivmj935;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t f3; uint8_t pe[3]; } gm_tkrkmsnb6m1b7;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tllos8achgacq;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t f3; uint8_t pe[2]; } gm_tppdoi1slt2pr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_tl874t2atnlh3 f2; gm_b16_8 f3; } gm_tt384v6n8ojib;
typedef struct { gm_tl874t2atnlh3 f0; gm_tl874t2atnlh3 f1; uint8_t f2; uint8_t pe[3]; } gm_tt7dbdq4n1egc;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_t9805qtonbsag f0; uint8_t f1; uint8_t pe[3]; } gm_tve56d3t8b8jf;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t26d2uc07m5o2) == 48 && _Alignof(gm_t26d2uc07m5o2) == 8 && offsetof(gm_t26d2uc07m5o2, f0) == 0 && offsetof(gm_t26d2uc07m5o2, f1) == 16 && offsetof(gm_t26d2uc07m5o2, f2) == 28 && offsetof(gm_t26d2uc07m5o2, f3) == 32, "layout of `(name: string, span: Span, vis: Visibility, what: string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9d4sf0vqn7dt) == 16 && _Alignof(gm_t9d4sf0vqn7dt) == 4 && offsetof(gm_t9d4sf0vqn7dt, f0) == 0 && offsetof(gm_t9d4sf0vqn7dt, f1) == 4, "layout of `TraceCall`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { void * f0; gm_t4c7ipjj9s3fp f1; gm_trqfvmjtt4sij f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_t88rcucg1b7uk f5; uint8_t pe[4]; } gm_tfcpkukfav158;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tis7ttivmj935) == 56 && _Alignof(gm_tis7ttivmj935) == 8 && offsetof(gm_tis7ttivmj935, f0) == 0 && offsetof(gm_tis7ttivmj935, f1) == 16 && offsetof(gm_tis7ttivmj935, f2) == 32 && offsetof(gm_tis7ttivmj935, f3) == 44, "layout of `(span: Span, name: string, local: Span, origin: Span)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tppdoi1slt2pr v0; gm_t08olq97rscn3 v1; gm_tt384v6n8ojib v2; gm_tt7dbdq4n1egc v3; gm_toceffb697p0u v4; gm_tl874t2atnlh3 v5; gm_t26d2uc07m5o2 v6; gm_tllos8achgacq v7; gm_tkrkmsnb6m1b7 v8; gm_tis7ttivmj935 v9; gm_tis7ttivmj935 v10; } f1; } gm_tj5f8vajf35o7;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tkrkmsnb6m1b7) == 48 && _Alignof(gm_tkrkmsnb6m1b7) == 8 && offsetof(gm_tkrkmsnb6m1b7, f0) == 0 && offsetof(gm_tkrkmsnb6m1b7, f1) == 16 && offsetof(gm_tkrkmsnb6m1b7, f2) == 32 && offsetof(gm_tkrkmsnb6m1b7, f3) == 44, "layout of `(span: Span, name: string, catch_all: Span, is_result: bool)`");
_Static_assert(sizeof(gm_tllos8achgacq) == 64 && _Alignof(gm_tllos8achgacq) == 8 && offsetof(gm_tllos8achgacq, f0) == 0 && offsetof(gm_tllos8achgacq, f1) == 16 && offsetof(gm_tllos8achgacq, f2) == 32 && offsetof(gm_tllos8achgacq, f3) == 48, "layout of `(span: Span, name: string, local: Span, why: string)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tppdoi1slt2pr) == 32 && _Alignof(gm_tppdoi1slt2pr) == 8 && offsetof(gm_tppdoi1slt2pr, f0) == 0 && offsetof(gm_tppdoi1slt2pr, f1) == 16 && offsetof(gm_tppdoi1slt2pr, f2) == 28 && offsetof(gm_tppdoi1slt2pr, f3) == 29, "layout of `(name: string, span: Span, param: bool, assigned: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
_Static_assert(sizeof(gm_tt384v6n8ojib) == 56 && _Alignof(gm_tt384v6n8ojib) == 8 && offsetof(gm_tt384v6n8ojib, f0) == 0 && offsetof(gm_tt384v6n8ojib, f1) == 16 && offsetof(gm_tt384v6n8ojib, f2) == 28 && offsetof(gm_tt384v6n8ojib, f3) == 40, "layout of `(name: string, span: Span, item: Span, what: string)`");
_Static_assert(sizeof(gm_tt7dbdq4n1egc) == 28 && _Alignof(gm_tt7dbdq4n1egc) == 4 && offsetof(gm_tt7dbdq4n1egc, f0) == 0 && offsetof(gm_tt7dbdq4n1egc, f1) == 12 && offsetof(gm_tt7dbdq4n1egc, f2) == 24, "layout of `(span: Span, operand: Span, same: bool)`");
_Static_assert(sizeof(gm_tve56d3t8b8jf) == 12 && _Alignof(gm_tve56d3t8b8jf) == 4 && offsetof(gm_tve56d3t8b8jf, f0) == 0 && offsetof(gm_tve56d3t8b8jf, f1) == 8, "layout of `?Rooted`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tfcpkukfav158) == 224 && _Alignof(gm_tfcpkukfav158) == 8 && offsetof(gm_tfcpkukfav158, f0) == 0 && offsetof(gm_tfcpkukfav158, f1) == 8 && offsetof(gm_tfcpkukfav158, f2) == 64 && offsetof(gm_tfcpkukfav158, f3) == 104 && offsetof(gm_tfcpkukfav158, f4) == 160 && offsetof(gm_tfcpkukfav158, f5) == 216, "layout of `CopyScan`");
_Static_assert(sizeof(gm_tj5f8vajf35o7) == 72 && _Alignof(gm_tj5f8vajf35o7) == 8 && offsetof(gm_tj5f8vajf35o7, f0) == 0 && offsetof(gm_tj5f8vajf35o7, f1) == 8, "layout of `Finding`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
struct gm_fr_allocator_ty_osls0uv {
    gm_frame h;
    gm_t6qooom8qa2do l3; /* v */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_assign_target_cpdrvfk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_captured_b6iape2 {
    gm_frame h;
    void * l1; /* out */
    gm_b0_1 ret;
};
struct gm_fr_change_o4oom1c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_contains_57n7jqd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_contains_r1r3too {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_copy_block_ii23udv {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* seen */
    gm_b0_1 ret;
};
struct gm_fr_copy_expr_vvegfhs {
    gm_frame h;
    void * l0; /* sc */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_00ubuj1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_behvl9q {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_chipc0h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_k21v01c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n44tb4r {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_2i3asst {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_em1fvro {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gj9gcf0 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_fn_vun6ohp {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_get_espvf96 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_get_l84pf3h {
    gm_frame h;
    gm_toceffb697p0u ret;
};
struct gm_fr_hash_map_5905oeo {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_70470jk {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_hash_map_cuh4fs9 {
    gm_frame h;
    gm_t4c7ipjj9s3fp ret;
};
struct gm_fr_holds_buffer_bjim9pk {
    gm_frame h;
    gm_tpautr4br7h3l l9; /* sd */
    gm_t0mglksrkfa5e l14; /* f */
    gm_t0mglksrkfa5e l18; /* f */
    uint8_t ret;
};
struct gm_fr_is_place_4ugj4mr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_lbm57pe {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_lookup_59juqoo {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_mutated_copies_vc6lpss {
    gm_frame h;
    gm_tfcpkukfav158 l6; /* sc */
    gm_trqfvmjtt4sij l7; /* found */
    gm_tsoie1ppgdvcg l12; /* local */
    gm_b0_1 ret;
};
struct gm_fr_place_reads_si5djsr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ml25ln1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_rqlbv1e {
    gm_frame h;
    gm_tj5f8vajf35o7 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3n5udep {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_s3s9ef4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rooted_local_cq8gk6j {
    gm_frame h;
    gm_tve56d3t8b8jf ret;
};
struct gm_fr_sort_by_key_u6kj3vl {
    gm_frame h;
    gm_closure l1; /* key */
    gm_b0_1 ret;
};
struct gm_fr_text_of_lbn39it {
    gm_frame h;
    gm_b16_8 ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_allocator_ty_osls0uv(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_assign_target_cpdrvfk(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_captured_b6iape2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_change_o4oom1c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint8_t gm_f_contains_r1r3too(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_copy_block_ii23udv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_copy_expr_vvegfhs(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_00ubuj1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_behvl9q(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_chipc0h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n44tb4r(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_722uua4(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_hb6lt4h(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_ovslcai(void * a0, void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_shfuv0t(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2i3asst(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_em1fvro(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fn_gj9gcf0(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_vun6ohp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_get_espvf96(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_toceffb697p0u gm_f_get_l84pf3h(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_hash_map_5905oeo(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_70470jk(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_holds_buffer_bjim9pk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, void * a3);
__attribute__((aligned(4))) void gm_f_introsort_tfgce8v(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) uint8_t gm_f_is_place_4ugj4mr(void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_lbm57pe(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_59juqoo(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mutated_copies_vc6lpss(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_place_reads_si5djsr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ml25ln1(void * a0, gm_t9d4sf0vqn7dt a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rqlbv1e(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_5c6qgui(void * a0, gm_b16_8 a1, uint8_t a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_fv8gs1b(void * a0, uint32_t a1, gm_cseahvg8v7p0vs a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_v6g5pup(void * a0, uint32_t a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3n5udep(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s3s9ef4(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tve56d3t8b8jf gm_f_rooted_local_cq8gk6j(void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_sort_by_key_u6kj3vl(gm_b16_8 a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_text_of_lbn39it(void * a0, gm_cseahvg8v7p0vs a1);
extern const uint8_t gm_s4eilu97sooh1[10];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src5329n2jhsntp[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcc2qutubarto1[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuktvltncsesc[];
extern const char gm_srcv1nri2247d2r[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) uint8_t gm_f_contains_57n7jqd(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_57n7jqd F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* k */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    void * t5 = gm_l0;
    void * t6 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t7 = gm_f_find_722uua4(t5, t6);
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
inline __attribute__((aligned(4))) uint8_t gm_f_contains_r1r3too(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_r1r3too F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l2;
    gm_tf5srh4mp4b3i gm_l3;
    void * t5 = gm_l0;
    void * t6 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t7 = gm_f_find_hb6lt4h(t5, t6);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_00ubuj1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_00ubuj1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_behvl9q(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_behvl9q F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_chipc0h(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_chipc0h F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_k21v01c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_k21v01c F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n44tb4r(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n44tb4r F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2i3asst(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_2i3asst F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_copy_expr_vvegfhs *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    gm_b0_1 t4 = gm_f_copy_expr_vvegfhs((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_em1fvro(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_em1fvro F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 24u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_captured_b6iape2 *)((&F.h)->up))->l1));
    gm_b0_1 t4 = gm_f_captured_b6iape2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint32_t gm_f_fn_gj9gcf0(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fn_gj9gcf0 F;
    void * gm_l0; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)t2));
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_fn_vun6ohp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_fn_vun6ohp F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    GM_STACK_CHECK(&F, 155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_closure t2 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_sort_by_key_u6kj3vl *)((&F.h)->up))->l1));
    void * t3 = gm_l0;
    gm_closure t4 = t2;
    gm_node *ccx5 = t4.cx;
    void * t6 = t4.code;
    uint32_t t7;
    if (GM_PTR_TAGGED(t6)) {
        t7 = ((uint32_t (*)(void *))GM_PTR_UNTAG(t6))(t3);
    } else {
        t7 = ((uint32_t (*)(void *, gm_node *, gm_node *, void *))t6)(t4.env, ccx5, NULL, t3);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_closure t8 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_sort_by_key_u6kj3vl *)((&F.h)->up))->l1));
    void * t9 = gm_l1;
    gm_closure t10 = t8;
    gm_node *ccx11 = t10.cx;
    void * t12 = t10.code;
    uint32_t t13;
    if (GM_PTR_TAGGED(t12)) {
        t13 = ((uint32_t (*)(void *))GM_PTR_UNTAG(t12))(t9);
    } else {
        t13 = ((uint32_t (*)(void *, gm_node *, gm_node *, void *))t12)(t10.env, ccx11, NULL, t9);
    }
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t14 = (t7 < t13);
    F.ret = t14;
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
inline __attribute__((aligned(4))) gm_toceffb697p0u gm_f_get_l84pf3h(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_l84pf3h F;
    void * gm_l0; /* m */
    uint32_t gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 185u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_shfuv0t(t2, t3);
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
    gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t9) + (uintptr_t)t11 * 20) + 8));
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
inline __attribute__((aligned(4))) void gm_f_hash_map_5905oeo(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_5905oeo F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_00ubuj1();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_70470jk(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_70470jk F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_n44tb4r();
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
inline __attribute__((aligned(4))) void gm_f_hash_map_cuh4fs9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_hash_map_cuh4fs9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_k21v01c();
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
inline __attribute__((aligned(4))) void gm_f_list_lbm57pe(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_lbm57pe F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_chipc0h();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ml25ln1(void * a0, gm_t9d4sf0vqn7dt a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ml25ln1 F;
    void * gm_l0; /* l */
    gm_t9d4sf0vqn7dt gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_chipc0h();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_s3s9ef4(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_rqlbv1e(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_rqlbv1e F;
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
        gm_b16_8 t9 = gm_f_empty_slice_behvl9q();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_3n5udep(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 72), ((uint8_t *)&F.l1), 72);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 72)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3n5udep(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_3n5udep F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)72ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s3s9ef4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_s3s9ef4 F;
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
inline __attribute__((aligned(4))) void gm_f_sort_by_key_u6kj3vl(gm_b16_8 a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_sort_by_key_u6kj3vl F;
    gm_b16_8 gm_l0; /* s */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_b16_8 t2 = gm_l0;
    gm_closure t3 = ((gm_closure){(void *)gm_f_fn_vun6ohp, (void *)&F.h, cx});
    gm_f_introsort_tfgce8v(t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_text_of_lbn39it(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_text_of_lbn39it F;
    void * gm_l0; /* sm */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK_SHALLOW(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&gm_l1));
    void * t4 = gm_f_file_o15fqku(t2, t3);
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 16));
    uint64_t t6 = GM_LD(uint64_t, ((((uint8_t *)t4) + 16) + 8));
    uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
    uint64_t t8 = (uint64_t)t7;
    uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t10 = (uint64_t)t9;
    uint64_t t11 = (uintptr_t)t8;
    uint64_t t12 = (uintptr_t)t10;
    if (t11 > t12 || t12 > (uintptr_t)t6) gm_trap_slice(t11, t12, t6, gm_srcuktvltncsesc, 145, 76);
    uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)t8;
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t5 + (uintptr_t)t8 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_allocator_ty_osls0uv(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_allocator_ty_osls0uv F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* p */
    uint64_t gm_l2; /* d */
    gm_t88rcucg1b7uk gm_l4; /* t */
    GM_STACK_CHECK(&F, 426u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tf5srh4mp4b3i gm_l5;
    gm_tagrkbqh054h7 gm_l6;
    void * t6 = gm_l0;
    void * t7 = GM_LD(void *, ((uint8_t *)t6));
    uint8_t t8 = (uint8_t)(((((uint8_t *)t7) + 320))[8] != 0);
    if (t8) {
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 320));
        gm_l1 = t9;
    } else {
        gm_tkn42u7na3t81 t10;
        memset(&t10, 0, sizeof t10);
        F.ret = t10;
        goto gm_fret;
    }
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    uint64_t t13 = gm_l1;
    gm_b16_8 t14;
    GM_ST(void *, (uint8_t *)&t14, (void *)(gm_s4eilu97sooh1));
    GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(9));
    gm_tf5srh4mp4b3i t15 = gm_f_lookup_59juqoo(t12, t13, t14);
    gm_l5 = t15;
    uint8_t t16 = (uint8_t)((((uint8_t *)&gm_l5))[8] != 0);
    if (t16) {
        uint64_t t17 = GM_LD(uint64_t, ((uint8_t *)&gm_l5));
        gm_l2 = t17;
    } else {
        gm_tkn42u7na3t81 t18;
        memset(&t18, 0, sizeof t18);
        F.ret = t18;
        goto gm_fret;
    }
    {
        {
            {
                void * t19 = gm_l0;
                uint64_t t20 = gm_l2;
                gm_f_state_a5bnsjb((void *)&gm_l6, (void *)NULL, cx, NULL, t19, t20);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)2ULL));
            if (!t22) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l6) + 8), 208);
            }
            {
                {
                    uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)0ULL));
                    if (!t24) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l3) + 8));
                    gm_l4 = t25;
                    gm_t88rcucg1b7uk t26 = gm_l4;
                    gm_tkn42u7na3t81 t27;
                    GM_ST(uint8_t, (uint8_t *)&t27 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t27 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t27), t26);
                    ((uint8_t *)&t27)[4] = 1;
                    F.ret = t27;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    gm_tkn42u7na3t81 t28;
    memset(&t28, 0, sizeof t28);
    F.ret = t28;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_assign_target_cpdrvfk(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_assign_target_cpdrvfk F;
    void * gm_l0; /* sc */
    void * gm_l1; /* place */
    gm_t9805qtonbsag gm_l2; /* r */
    GM_STACK_CHECK(&F, 155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tve56d3t8b8jf gm_l3;
    gm_tftb1odc739t5 gm_l4;
    {
        {
            void * t4 = gm_l0;
            void * t5 = GM_LD(void *, ((uint8_t *)t4));
            void * t6 = gm_l1;
            gm_tve56d3t8b8jf t7 = gm_f_rooted_local_cq8gk6j(t5, t6);
            gm_l3 = t7;
            uint8_t t8 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
            if (!t8) {
                goto gm_lb2;
            }
            gm_t9805qtonbsag t9 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&gm_l3));
            gm_l2 = t9;
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 4));
            if (t10) {
                void * t11 = gm_l0;
                void * t12 = (void *)(((uint8_t *)t11) + 104);
                uint32_t t13 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
                gm_tftb1odc739t5 t14 = gm_f_put_v6g5pup(t12, t13, ((uint8_t)1));
                gm_l4 = t14;
                uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_src5329n2jhsntp, 97, 13);
            } else {
                void * t16 = gm_l0;
                uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)&gm_l2));
                void * t18 = gm_l1;
                gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t18) + 72));
                gm_b0_1 t20 = gm_f_change_o4oom1c((void *)NULL, cx, NULL, t16, t17, t19);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb2:;
        void * t21 = gm_l0;
        void * t22 = gm_l1;
        gm_b0_1 t23 = gm_f_copy_expr_vvegfhs((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_captured_b6iape2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_captured_b6iape2 F;
    void * gm_l0; /* e */
    uint32_t gm_l2; /* levels */
    uint32_t gm_l3; /* local */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    gm_tftb1odc739t5 gm_l4;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)16ULL));
            if (!t6) {
                goto gm_lb2;
            }
            uint32_t t7 = GM_LD(uint32_t, ((((uint8_t *)t4) + 8) + 8));
            gm_l2 = t7;
            uint32_t t8 = GM_LD(uint32_t, (((((uint8_t *)t4) + 8) + 8) + 4));
            gm_l3 = t8;
            uint32_t t9 = gm_l2;
            uint8_t t10 = ((t9 == ((uint32_t)1ULL)));
            if (t10) {
                void * t11 = F.l1;
                uint32_t t12 = gm_l3;
                gm_tftb1odc739t5 t13 = gm_f_put_v6g5pup(t11, t12, ((uint8_t)1));
                gm_l4 = t13;
                uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
                if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_src5329n2jhsntp, 173, 26);
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t15 = gm_l0;
    gm_closure t16 = ((gm_closure){(void *)gm_f_fn_em1fvro, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t15, t16);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_change_o4oom1c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_change_o4oom1c F;
    void * gm_l0; /* sc */
    uint32_t gm_l1; /* l */
    gm_tl874t2atnlh3 gm_l2; /* at */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t9d4sf0vqn7dt gm_l5; /* c */
    GM_STACK_CHECK(&F, 223u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l6;
    void * t7 = gm_l0;
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)t7) + 64));
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
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t14 = gm_l4;
                {
                    {
                        gm_t9d4sf0vqn7dt t15 = GM_LD(gm_t9d4sf0vqn7dt, (((uint8_t *)t12) + (uintptr_t)t14 * 16));
                        gm_l5 = t15;
                        uint32_t t16 = GM_LD(uint32_t, ((uint8_t *)&gm_l5));
                        uint32_t t17 = gm_l1;
                        uint8_t t18 = ((t16 == t17));
                        if (t18) {
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 108, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t19 = gm_l4;
            uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
            uint64_t t22 = r20;
            gm_l4 = t22;
        }
    }
gm_lb1:;
    void * t23 = gm_l0;
    void * t24 = (void *)(((uint8_t *)t23) + 64);
    uint32_t t25 = gm_l1;
    gm_tl874t2atnlh3 t26 = gm_l2;
    gm_t9d4sf0vqn7dt t27;
    GM_ST(uint32_t, ((uint8_t *)&t27 + 0), t25);
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t27 + 4), t26);
    gm_tftb1odc739t5 t28 = gm_f_push_ml25ln1(t24, t27);
    gm_l6 = t28;
    uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
    if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_src5329n2jhsntp, 111, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_copy_block_ii23udv(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_copy_block_ii23udv F;
    void * gm_l0; /* sc */
    void * gm_l1; /* b */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_trcmmd12cefb1 gm_l4; /* st */
    uint32_t gm_l5; /* local */
    void * gm_l6; /* init */
    void * gm_l7; /* x */
    void * gm_l9; /* place */
    void * gm_l10; /* value */
    gm_b16_8 gm_l11; /* places */
    void * gm_l12; /* value */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    void * gm_l15; /* p */
    void * gm_l16; /* x */
    void * gm_l17; /* x */
    void * gm_l18; /* x */
    void * gm_l19; /* x */
    void * gm_l20; /* t */
    GM_STACK_CHECK(&F, 865u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    void * t27 = gm_l1;
    gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)t27));
    gm_l2 = t28;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t29 = gm_l3;
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t31 = (t29 < t30);
            if (!t31) {
                goto gm_lb1;
            }
            {
                void * t32 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t34 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&gm_l4), (((uint8_t *)t32) + (uintptr_t)t34 * 32), 32);
                        {
                            {
                                uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t36 = (uint8_t)(t35 == ((uint8_t)0ULL));
                                if (!t36) {
                                    goto gm_lb7;
                                }
                                uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
                                gm_l5 = t37;
                                void * t38 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l6 = t38;
                                {
                                    {
                                        uint8_t t39 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l6)) != 0);
                                        if (!t39) {
                                            goto gm_lb9;
                                        }
                                        void * t40 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                        gm_l7 = t40;
                                        gm_trqfvmjtt4sij t41;
                                        gm_f_list_nof7vfj((void *)&t41, (void *)NULL, cx, NULL);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        F.l8 = t41;
                                        {
                                            {
                                                void * t42 = gm_l7;
                                                uint8_t t43 = gm_f_is_place_4ugj4mr(t42);
                                                if (!t43) {
                                                    goto gm_lb11;
                                                }
                                                void * t44 = gm_l0;
                                                void * t45 = GM_LD(void *, ((uint8_t *)t44));
                                                void * t46 = gm_l0;
                                                gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t46) + 216));
                                                void * t48 = gm_l7;
                                                gm_t88rcucg1b7uk t49 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t48));
                                                void * t50 = (void *)((uint8_t *)&F.l8);
                                                uint8_t t51 = gm_f_holds_buffer_bjim9pk((void *)NULL, cx, NULL, t45, t47, t49, t50);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                if (!t51) {
                                                    goto gm_lb11;
                                                }
                                                void * t52 = gm_l0;
                                                void * t53 = (void *)(((uint8_t *)t52) + 8);
                                                uint32_t t54 = gm_l5;
                                                void * t55 = gm_l7;
                                                gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t55) + 72));
                                                gm_tftb1odc739t5 t57 = gm_f_put_fv8gs1b(t53, t54, GM_CONV(gm_cseahvg8v7p0vs, t56));
                                                gm_l21 = t57;
                                                uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
                                                if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_src5329n2jhsntp, 71, 84);
                                                goto gm_lb10;
                                            }
gm_lb11:;
                                        }
gm_lb10:;
                                        void * t59 = gm_l0;
                                        void * t60 = gm_l7;
                                        gm_b0_1 t61 = gm_f_copy_expr_vvegfhs((void *)NULL, cx, NULL, t59, t60);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t63 = (uint8_t)(t62 == ((uint8_t)2ULL));
                                if (!t63) {
                                    goto gm_lb12;
                                }
                                void * t64 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l9 = t64;
                                void * t65 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l10 = t65;
                                void * t66 = gm_l0;
                                void * t67 = (void *)(((uint8_t *)t66) + 160);
                                void * t68 = gm_l0;
                                void * t69 = GM_LD(void *, ((uint8_t *)t68));
                                void * t70 = GM_LD(void *, ((uint8_t *)t69));
                                void * t71 = GM_LD(void *, ((uint8_t *)t70));
                                void * t72 = gm_l9;
                                gm_tl874t2atnlh3 t73 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t72) + 72));
                                gm_b16_8 t74 = gm_f_text_of_lbn39it(t71, GM_CONV(gm_cseahvg8v7p0vs, t73));
                                gm_tftb1odc739t5 t75 = gm_f_put_5c6qgui(t67, t74, ((uint8_t)1));
                                gm_l22 = t75;
                                uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                                if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_src5329n2jhsntp, 75, 17);
                                void * t77 = gm_l0;
                                void * t78 = gm_l9;
                                gm_b0_1 t79 = gm_f_assign_target_cpdrvfk((void *)NULL, cx, NULL, t77, t78);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t80 = gm_l0;
                                void * t81 = gm_l10;
                                gm_b0_1 t82 = gm_f_copy_expr_vvegfhs((void *)NULL, cx, NULL, t80, t81);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb12:;
                            {
                                uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t84 = (uint8_t)(t83 == ((uint8_t)3ULL));
                                if (!t84) {
                                    goto gm_lb13;
                                }
                                gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
                                gm_l11 = t85;
                                void * t86 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                                gm_l12 = t86;
                                gm_b16_8 t87 = gm_l11;
                                gm_l13 = t87;
                                gm_l14 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t88 = gm_l14;
                                        uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                        uint8_t t90 = (t88 < t89);
                                        if (!t90) {
                                            goto gm_lb14;
                                        }
                                        {
                                            void * t91 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                            uint64_t t92 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                            uint64_t t93 = gm_l14;
                                            {
                                                {
                                                    void * t94 = GM_LD(void *, (((uint8_t *)t91) + (uintptr_t)t93 * 8));
                                                    gm_l15 = t94;
                                                    {
                                                        {
                                                            uint8_t t95 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l15)) != 0);
                                                            if (!t95) {
                                                                goto gm_lb20;
                                                            }
                                                            void * t96 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                                            gm_l16 = t96;
                                                            void * t97 = gm_l0;
                                                            void * t98 = gm_l16;
                                                            gm_b0_1 t99 = gm_f_assign_target_cpdrvfk((void *)NULL, cx, NULL, t97, t98);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            goto gm_lb19;
                                                        }
gm_lb20:;
                                                    }
gm_lb19:;
                                                    goto gm_lb17;
                                                }
gm_lb18:;
                                                gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 80, 17);
                                            }
gm_lb17:;
                                        }
gm_lb16:;
                                        uint64_t t100 = gm_l14;
                                        uint64_t r101; bool o102 = __builtin_add_overflow(t100, ((uint64_t)1ULL), &r101);
                                        uint64_t t103 = r101;
                                        gm_l14 = t103;
                                    }
                                }
gm_lb14:;
                                void * t104 = gm_l0;
                                void * t105 = gm_l12;
                                gm_b0_1 t106 = gm_f_copy_expr_vvegfhs((void *)NULL, cx, NULL, t104, t105);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb13:;
                            {
                                uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t108 = (uint8_t)(t107 == ((uint8_t)1ULL));
                                if (!t108) {
                                    goto gm_lb21;
                                }
                                void * t109 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 16));
                                gm_l17 = t109;
                                void * t110 = gm_l0;
                                void * t111 = gm_l17;
                                gm_b0_1 t112 = gm_f_copy_expr_vvegfhs((void *)NULL, cx, NULL, t110, t111);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb21:;
                            {
                                uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t114 = (uint8_t)(t113 == ((uint8_t)4ULL));
                                if (!t114) {
                                    goto gm_lb22;
                                }
                                void * t115 = GM_LD(void *, (((uint8_t *)&gm_l4) + 8));
                                gm_l18 = t115;
                                void * t116 = gm_l0;
                                void * t117 = gm_l18;
                                gm_b0_1 t118 = gm_f_copy_expr_vvegfhs((void *)NULL, cx, NULL, t116, t117);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb22:;
                            {
                                uint8_t t119 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                                uint8_t t120 = (uint8_t)(t119 == ((uint8_t)5ULL));
                                if (!t120) {
                                    goto gm_lb23;
                                }
                                void * t121 = GM_LD(void *, ((((uint8_t *)&gm_l4) + 8) + 8));
                                gm_l19 = t121;
                                void * t122 = gm_l0;
                                void * t123 = gm_l19;
                                gm_b0_1 t124 = gm_f_copy_expr_vvegfhs((void *)NULL, cx, NULL, t122, t123);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb6;
                            }
gm_lb23:;
                            gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 68, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 67, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t125 = gm_l3;
            uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
            uint64_t t128 = r126;
            gm_l3 = t128;
        }
    }
gm_lb1:;
    {
        {
            void * t129 = gm_l1;
            uint8_t t130 = (uint8_t)(GM_LD(void *, (((uint8_t *)t129) + 16)) != 0);
            if (!t130) {
                goto gm_lb25;
            }
            void * t131 = GM_LD(void *, (((uint8_t *)t129) + 16));
            gm_l20 = t131;
            void * t132 = gm_l0;
            void * t133 = gm_l20;
            gm_b0_1 t134 = gm_f_copy_expr_vvegfhs((void *)NULL, cx, NULL, t132, t133);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb24;
        }
gm_lb25:;
    }
gm_lb24:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_copy_expr_vvegfhs(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_copy_expr_vvegfhs F;
    void * gm_l1; /* e */
    void * gm_l2; /* tys */
    void * gm_l3; /* b */
    uint32_t gm_l4; /* l */
    void * gm_l5; /* inner */
    uint8_t gm_l6; /* c */
    void * gm_l7; /* p */
    void * gm_l8; /* p */
    gm_t9805qtonbsag gm_l9; /* r */
    uint8_t gm_l10; /* c */
    uint32_t gm_l11; /* f */
    uint32_t gm_l12; /* f */
    void * gm_l13; /* b */
    GM_STACK_CHECK(&F, 728u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l14;
    gm_t53at80pfdus3 gm_l15;
    uint8_t gm_l16;
    gm_tve56d3t8b8jf gm_l17;
    gm_tve56d3t8b8jf gm_l18;
    gm_t53at80pfdus3 gm_l19;
    uint8_t gm_l20;
    gm_tve56d3t8b8jf gm_l21;
    void * t29 = F.l0;
    void * t30 = GM_LD(void *, ((uint8_t *)t29));
    void * t31 = (void *)(((uint8_t *)t30) + 8);
    gm_l2 = t31;
    void * t32 = gm_l1;
    {
        {
            uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)46ULL));
            if (!t34) {
                goto gm_lb2;
            }
            void * t35 = GM_LD(void *, ((((uint8_t *)t32) + 8) + 8));
            gm_l3 = t35;
            void * t36 = F.l0;
            void * t37 = gm_l3;
            gm_b0_1 t38 = gm_f_copy_block_ii23udv((void *)NULL, cx, NULL, t36, t37);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)15ULL));
            if (!t40) {
                goto gm_lb3;
            }
            uint32_t t41 = GM_LD(uint32_t, ((((uint8_t *)t32) + 8) + 8));
            gm_l4 = t41;
            void * t42 = F.l0;
            void * t43 = (void *)(((uint8_t *)t42) + 104);
            uint32_t t44 = gm_l4;
            gm_tftb1odc739t5 t45 = gm_f_put_v6g5pup(t43, t44, ((uint8_t)1));
            gm_l14 = t45;
            uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_src5329n2jhsntp, 119, 13);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t47 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
            uint8_t t48 = (uint8_t)(t47 == ((uint8_t)32ULL));
            if (!t48) {
                goto gm_lb4;
            }
            void * t49 = GM_LD(void *, (((((uint8_t *)t32) + 8) + 8) + 8));
            gm_l5 = t49;
            {
                {
                    void * t50 = gm_l2;
                    void * t51 = gm_l1;
                    gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t51));
                    gm_f_kind_jcnt4hm((void *)&gm_l15, t50, GM_CONV(gm_csguankko4lmhd, t52));
                    uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 0));
                    uint8_t t54 = (uint8_t)(t53 == ((uint8_t)8ULL));
                    if (!t54) {
                        goto gm_lb6;
                    }
                    uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 8));
                    gm_l6 = t55;
                    uint8_t t56 = gm_l6;
                    if (t56) {
                        {
                            {
                                void * t57 = gm_l5;
                                uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t57) + 8) + 0));
                                uint8_t t59 = (uint8_t)(t58 == ((uint8_t)24ULL));
                                if (!t59) {
                                    goto gm_lb8;
                                }
                                void * t60 = GM_LD(void *, ((((uint8_t *)t57) + 8) + 8));
                                gm_l7 = t60;
                                void * t61 = F.l0;
                                void * t62 = GM_LD(void *, ((uint8_t *)t61));
                                void * t63 = gm_l7;
                                gm_tve56d3t8b8jf t64 = gm_f_rooted_local_cq8gk6j(t62, t63);
                                gm_l17 = t64;
                                {
                                    {
                                        uint8_t t65 = (uint8_t)((((uint8_t *)&gm_l17))[8] != 0);
                                        if (t65) {
                                            goto gm_lb10;
                                        }
                                        gm_l16 = ((uint8_t)0);
                                        goto gm_lb9;
                                    }
gm_lb10:;
                                    {
                                        gm_l16 = ((uint8_t)1);
                                        goto gm_lb9;
                                    }
gm_lb11:;
                                    gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 127, 28);
                                }
gm_lb9:;
                                uint8_t t66 = gm_l16;
                                if (t66) {
                                    void * t67 = F.l0;
                                    void * t68 = gm_l7;
                                    gm_b0_1 t69 = gm_f_place_reads_si5djsr((void *)NULL, cx, NULL, t67, t68);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.ret = GM_ZERO(gm_b0_1);
                                    goto gm_fret;
                                }
                                goto gm_lb7;
                            }
gm_lb8:;
                        }
gm_lb7:;
                    }
                    goto gm_lb5;
                }
gm_lb6:;
            }
gm_lb5:;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
            uint8_t t71 = (uint8_t)(t70 == ((uint8_t)24ULL));
            if (!t71) {
                goto gm_lb12;
            }
            void * t72 = GM_LD(void *, ((((uint8_t *)t32) + 8) + 8));
            gm_l8 = t72;
            {
                {
                    void * t73 = F.l0;
                    void * t74 = GM_LD(void *, ((uint8_t *)t73));
                    void * t75 = gm_l8;
                    gm_tve56d3t8b8jf t76 = gm_f_rooted_local_cq8gk6j(t74, t75);
                    gm_l18 = t76;
                    uint8_t t77 = (uint8_t)((((uint8_t *)&gm_l18))[8] != 0);
                    if (!t77) {
                        goto gm_lb14;
                    }
                    gm_t9805qtonbsag t78 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&gm_l18));
                    gm_l9 = t78;
                    {
                        {
                            void * t79 = gm_l2;
                            void * t80 = gm_l1;
                            gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t80));
                            gm_f_kind_jcnt4hm((void *)&gm_l19, t79, GM_CONV(gm_csguankko4lmhd, t81));
                            uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                            uint8_t t83 = (uint8_t)(t82 == ((uint8_t)8ULL));
                            if (!t83) {
                                goto gm_lb16;
                            }
                            uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 8));
                            gm_l10 = t84;
                            uint8_t t85 = gm_l10;
                            uint8_t t86 = (uint8_t)!t85;
                            if (t86) {
                                void * t87 = F.l0;
                                uint32_t t88 = GM_LD(uint32_t, ((uint8_t *)&gm_l9));
                                void * t89 = gm_l1;
                                gm_tl874t2atnlh3 t90 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t89) + 72));
                                gm_b0_1 t91 = gm_f_change_o4oom1c((void *)NULL, cx, NULL, t87, t88, t90);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            goto gm_lb15;
                        }
gm_lb16:;
                    }
gm_lb15:;
                    void * t92 = F.l0;
                    void * t93 = gm_l8;
                    gm_b0_1 t94 = gm_f_place_reads_si5djsr((void *)NULL, cx, NULL, t92, t93);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = GM_ZERO(gm_b0_1);
                    goto gm_fret;
                }
gm_lb14:;
            }
gm_lb13:;
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
            uint8_t t96 = (uint8_t)(t95 == ((uint8_t)19ULL));
            if (!t96) {
                goto gm_lb17;
            }
            void * t97 = F.l0;
            void * t98 = GM_LD(void *, ((uint8_t *)t97));
            void * t99 = gm_l1;
            gm_tve56d3t8b8jf t100 = gm_f_rooted_local_cq8gk6j(t98, t99);
            gm_l21 = t100;
            {
                {
                    uint8_t t101 = (uint8_t)((((uint8_t *)&gm_l21))[8] != 0);
                    if (t101) {
                        goto gm_lb19;
                    }
                    gm_l20 = ((uint8_t)0);
                    goto gm_lb18;
                }
gm_lb19:;
                {
                    gm_l20 = ((uint8_t)1);
                    goto gm_lb18;
                }
gm_lb20:;
                gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 147, 16);
            }
gm_lb18:;
            uint8_t t102 = gm_l20;
            if (t102) {
                void * t103 = F.l0;
                void * t104 = gm_l1;
                gm_b0_1 t105 = gm_f_place_reads_si5djsr((void *)NULL, cx, NULL, t103, t104);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = GM_ZERO(gm_b0_1);
                goto gm_fret;
            }
            goto gm_lb1;
        }
gm_lb17:;
        {
            {
                {
                    uint8_t t106 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                    uint8_t t107 = (uint8_t)(t106 == ((uint8_t)27ULL));
                    if (!t107) {
                        goto gm_lb23;
                    }
                    uint32_t t108 = GM_LD(uint32_t, ((((uint8_t *)t32) + 8) + 8));
                    gm_l11 = t108;
                    goto gm_lb22;
                }
gm_lb23:;
                {
                    uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t32) + 8) + 0));
                    uint8_t t110 = (uint8_t)(t109 == ((uint8_t)26ULL));
                    if (!t110) {
                        goto gm_lb24;
                    }
                    uint32_t t111 = GM_LD(uint32_t, ((((uint8_t *)t32) + 8) + 8));
                    gm_l11 = t111;
                    goto gm_lb22;
                }
gm_lb24:;
                goto gm_lb21;
            }
gm_lb22:;
            {
                {
                    void * t112 = F.l0;
                    void * t113 = GM_LD(void *, ((uint8_t *)t112));
                    void * t114 = GM_LD(void *, (((uint8_t *)t113) + 384));
                    uint64_t t115 = GM_LD(uint64_t, ((((uint8_t *)t113) + 384) + 8));
                    uint32_t t116 = gm_l11;
                    uint64_t t117 = (uint64_t)t116;
                    if ((uintptr_t)t117 >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)t117, t115, gm_src5329n2jhsntp, 151, 21);
                    uint8_t t118 = (uint8_t)(GM_LD(void *, ((((uint8_t *)t114) + (uintptr_t)t117 * 64) + 40)) != 0);
                    if (!t118) {
                        goto gm_lb26;
                    }
                    void * t119 = GM_LD(void *, ((((uint8_t *)t114) + (uintptr_t)t117 * 64) + 40));
                    gm_l13 = t119;
                    void * t120 = gm_l13;
                    void * t121 = F.l0;
                    void * t122 = (void *)(((uint8_t *)t121) + 104);
                    gm_b0_1 t123 = gm_f_captured_b6iape2((void *)NULL, cx, NULL, t120, t122);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb25;
                }
gm_lb26:;
            }
gm_lb25:;
            goto gm_lb1;
        }
gm_lb21:;
        {
            goto gm_lb1;
        }
gm_lb27:;
        gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 116, 5);
    }
gm_lb1:;
    void * t124 = gm_l1;
    gm_closure t125 = ((gm_closure){(void *)gm_f_fn_2i3asst, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t124, t125);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2i3asst(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_em1fvro(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint32_t gm_f_fn_gj9gcf0(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_holds_buffer_bjim9pk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, void * a3) {
    struct gm_fr_holds_buffer_bjim9pk F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* allocator */
    gm_t88rcucg1b7uk gm_l2; /* t */
    void * gm_l3; /* seen */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* x */
    void * gm_l7; /* tys */
    uint32_t gm_l8; /* id */
    uint8_t gm_l10; /* has_slice */
    uint8_t gm_l11; /* has_alloc */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    uint8_t gm_l15; /* c */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l19; /* elems */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_t88rcucg1b7uk gm_l22; /* x */
    gm_t88rcucg1b7uk gm_l23; /* elem */
    GM_STACK_CHECK(&F, 842u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l24;
    gm_t53at80pfdus3 gm_l25;
    uint8_t gm_l26;
    gm_t53at80pfdus3 gm_l27;
    void * t36 = gm_l3;
    gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)t36));
    gm_l4 = t37;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t38 = gm_l5;
            uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t40 = (t38 < t39);
            if (!t40) {
                goto gm_lb1;
            }
            {
                void * t41 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t43 = gm_l5;
                {
                    {
                        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t41) + (uintptr_t)t43 * 4));
                        gm_l6 = t44;
                        gm_t88rcucg1b7uk t45 = gm_l6;
                        gm_t88rcucg1b7uk t46 = gm_l2;
                        uint8_t t47 = (gm_eq9m6mvtmgde(((uint8_t *)&t45), ((uint8_t *)&t46)));
                        if (t47) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 218, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t48 = gm_l5;
            uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
            uint64_t t51 = r49;
            gm_l5 = t51;
        }
    }
gm_lb1:;
    void * t52 = gm_l3;
    gm_t88rcucg1b7uk t53 = gm_l2;
    gm_tftb1odc739t5 t54 = gm_f_push_k3l9nic(t52, GM_CONV(gm_csguankko4lmhd, t53));
    gm_l24 = t54;
    uint8_t t55 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
    if (!t55) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_src5329n2jhsntp, 221, 5);
    void * t56 = gm_l0;
    void * t57 = (void *)(((uint8_t *)t56) + 8);
    gm_l7 = t57;
    void * t58 = gm_l7;
    gm_t88rcucg1b7uk t59 = gm_l2;
    gm_f_kind_jcnt4hm((void *)&gm_l25, t58, GM_CONV(gm_csguankko4lmhd, t59));
    {
        {
            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t61 = (uint8_t)(t60 == ((uint8_t)17ULL));
            if (!t61) {
                goto gm_lb7;
            }
            uint32_t t62 = GM_LD(uint32_t, (((uint8_t *)&gm_l25) + 8));
            gm_l8 = t62;
            uint32_t t63 = gm_l8;
            uint64_t t64 = (uint64_t)t63;
            void * t65 = gm_l0;
            uint64_t t66 = GM_LD(uint64_t, ((((uint8_t *)t65) + 1088) + 8));
            uint8_t t67 = (t64 >= t66);
            if (t67) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            {
                {
                    void * t68 = gm_l0;
                    void * t69 = GM_LD(void *, (((uint8_t *)t68) + 1088));
                    uint64_t t70 = GM_LD(uint64_t, ((((uint8_t *)t68) + 1088) + 8));
                    uint32_t t71 = gm_l8;
                    uint64_t t72 = (uint64_t)t71;
                    if ((uintptr_t)t72 >= (uintptr_t)(t70)) gm_trap_index((uintptr_t)t72, t70, gm_src5329n2jhsntp, 226, 28);
                    uint8_t t73 = GM_LD(uint8_t, (((((uint8_t *)t69) + (uintptr_t)t72 * 184) + 24) + 0));
                    uint8_t t74 = (uint8_t)(t73 == ((uint8_t)0ULL));
                    if (!t74) {
                        goto gm_lb9;
                    }
                    memmove(((uint8_t *)&F.l9), (((((uint8_t *)t69) + (uintptr_t)t72 * 184) + 24) + 8), 48);
                    {
                        {
                            uint8_t t75 = (uint8_t)(((((uint8_t *)&F.l9) + 40))[4] != 0);
                            if (t75) {
                                goto gm_lb11;
                            }
                            gm_l26 = ((uint8_t)0);
                            goto gm_lb10;
                        }
gm_lb11:;
                        {
                            gm_l26 = ((uint8_t)1);
                            goto gm_lb10;
                        }
gm_lb12:;
                        gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 227, 16);
                    }
gm_lb10:;
                    uint8_t t76 = gm_l26;
                    if (t76) {
                        F.ret = ((uint8_t)0);
                        goto gm_fret;
                    }
                    gm_l10 = ((uint8_t)0);
                    gm_l11 = ((uint8_t)0);
                    gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                    gm_l12 = t77;
                    gm_l13 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t78 = gm_l13;
                            uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                            uint8_t t80 = (t78 < t79);
                            if (!t80) {
                                goto gm_lb13;
                            }
                            {
                                void * t81 = GM_LD(void *, ((uint8_t *)&gm_l12));
                                uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                uint64_t t83 = gm_l13;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l14), (((uint8_t *)t81) + (uintptr_t)t83 * 40), 40);
                                        {
                                            {
                                                void * t84 = gm_l7;
                                                gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 16));
                                                gm_f_kind_jcnt4hm((void *)&gm_l27, t84, GM_CONV(gm_csguankko4lmhd, t85));
                                                uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
                                                uint8_t t87 = (uint8_t)(t86 == ((uint8_t)10ULL));
                                                if (!t87) {
                                                    goto gm_lb19;
                                                }
                                                uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 8));
                                                gm_l15 = t88;
                                                uint8_t t89 = gm_l15;
                                                uint8_t t90 = (uint8_t)!t89;
                                                if (t90) {
                                                    gm_l10 = ((uint8_t)1);
                                                }
                                                goto gm_lb18;
                                            }
gm_lb19:;
                                        }
gm_lb18:;
                                        gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 16));
                                        gm_t88rcucg1b7uk t92 = gm_l1;
                                        uint8_t t93 = (gm_eq9m6mvtmgde(((uint8_t *)&t91), ((uint8_t *)&t92)));
                                        if (t93) {
                                            gm_l11 = ((uint8_t)1);
                                        }
                                        goto gm_lb16;
                                    }
gm_lb17:;
                                    gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 230, 13);
                                }
gm_lb16:;
                            }
gm_lb15:;
                            uint64_t t94 = gm_l13;
                            uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                            uint64_t t97 = r95;
                            gm_l13 = t97;
                        }
                    }
gm_lb13:;
                    {
                        {
                            uint8_t t98 = gm_l10;
                            if (!t98) {
                                goto gm_lb21;
                            }
                            uint8_t t99 = gm_l11;
                            if (!t99) {
                                goto gm_lb21;
                            }
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
gm_lb21:;
                    }
gm_lb20:;
                    gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                    gm_l16 = t100;
                    gm_l17 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t101 = gm_l17;
                            uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                            uint8_t t103 = (t101 < t102);
                            if (!t103) {
                                goto gm_lb22;
                            }
                            {
                                void * t104 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                uint64_t t105 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                uint64_t t106 = gm_l17;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l18), (((uint8_t *)t104) + (uintptr_t)t106 * 40), 40);
                                        void * t107 = gm_l0;
                                        gm_t88rcucg1b7uk t108 = gm_l1;
                                        gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l18) + 16));
                                        void * t110 = gm_l3;
                                        uint8_t t111 = gm_f_holds_buffer_bjim9pk((void *)NULL, cx, NULL, t107, t108, t109, t110);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        if (t111) {
                                            F.ret = ((uint8_t)1);
                                            goto gm_fret;
                                        }
                                        goto gm_lb25;
                                    }
gm_lb26:;
                                    gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 237, 13);
                                }
gm_lb25:;
                            }
gm_lb24:;
                            uint64_t t112 = gm_l17;
                            uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                            uint64_t t115 = r113;
                            gm_l17 = t115;
                        }
                    }
gm_lb22:;
                    F.ret = ((uint8_t)0);
                    goto gm_fret;
                }
gm_lb9:;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
gm_lb8:;
        }
gm_lb7:;
        {
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t117 = (uint8_t)(t116 == ((uint8_t)12ULL));
            if (!t117) {
                goto gm_lb27;
            }
            gm_b16_8 t118 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
            gm_l19 = t118;
            gm_b16_8 t119 = gm_l19;
            gm_l20 = t119;
            gm_l21 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t120 = gm_l21;
                    uint64_t t121 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                    uint8_t t122 = (t120 < t121);
                    if (!t122) {
                        goto gm_lb28;
                    }
                    {
                        void * t123 = GM_LD(void *, ((uint8_t *)&gm_l20));
                        uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&gm_l20) + 8));
                        uint64_t t125 = gm_l21;
                        {
                            {
                                gm_t88rcucg1b7uk t126 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t123) + (uintptr_t)t125 * 4));
                                gm_l22 = t126;
                                void * t127 = gm_l0;
                                gm_t88rcucg1b7uk t128 = gm_l1;
                                gm_t88rcucg1b7uk t129 = gm_l22;
                                void * t130 = gm_l3;
                                uint8_t t131 = gm_f_holds_buffer_bjim9pk((void *)NULL, cx, NULL, t127, t128, t129, t130);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t131) {
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto gm_lb31;
                            }
gm_lb32:;
                            gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 243, 13);
                        }
gm_lb31:;
                    }
gm_lb30:;
                    uint64_t t132 = gm_l21;
                    uint64_t r133; bool o134 = __builtin_add_overflow(t132, ((uint64_t)1ULL), &r133);
                    uint64_t t135 = r133;
                    gm_l21 = t135;
                }
            }
gm_lb28:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb27:;
        {
            uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t137 = (uint8_t)(t136 == ((uint8_t)11ULL));
            if (!t137) {
                goto gm_lb33;
            }
            gm_t88rcucg1b7uk t138 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l25) + 8) + 8));
            gm_l23 = t138;
            void * t139 = gm_l0;
            gm_t88rcucg1b7uk t140 = gm_l1;
            gm_t88rcucg1b7uk t141 = gm_l23;
            void * t142 = gm_l3;
            uint8_t t143 = gm_f_holds_buffer_bjim9pk((void *)NULL, cx, NULL, t139, t140, t141, t142);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t143;
            goto gm_fret;
        }
gm_lb33:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb34:;
        gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 223, 5);
    }
gm_lb6:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_place_4ugj4mr(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_place_4ugj4mr F;
    void * gm_l0; /* e */
    void * gm_l1; /* base */
    GM_STACK_CHECK(&F, 69u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t gm_l2;
    uint8_t gm_l3;
    void * t15 = gm_l0;
    {
        {
            {
                {
                    uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
                    uint8_t t17 = (uint8_t)(t16 == ((uint8_t)15ULL));
                    if (!t17) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
                    uint8_t t19 = (uint8_t)(t18 == ((uint8_t)16ULL));
                    if (!t19) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
                    uint8_t t21 = (uint8_t)(t20 == ((uint8_t)18ULL));
                    if (!t21) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
                    uint8_t t23 = (uint8_t)(t22 == ((uint8_t)17ULL));
                    if (!t23) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                goto gm_lb2;
            }
gm_lb3:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)19ULL));
            if (!t25) {
                goto gm_lb8;
            }
            void * t26 = GM_LD(void *, ((((uint8_t *)t15) + 8) + 8));
            gm_l1 = t26;
            void * t27 = gm_l1;
            uint8_t t28 = gm_f_is_place_4ugj4mr(t27);
            if (t28) {
                gm_l2 = ((uint8_t)1);
            } else {
                void * t29 = gm_l1;
                {
                    {
                        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t29) + 8) + 0));
                        uint8_t t31 = (uint8_t)(t30 == ((uint8_t)34ULL));
                        if (!t31) {
                            goto gm_lb10;
                        }
                        gm_l3 = ((uint8_t)1);
                        goto gm_lb9;
                    }
gm_lb10:;
                    {
                        gm_l3 = ((uint8_t)0);
                        goto gm_lb9;
                    }
gm_lb11:;
                    gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 182, 53);
                }
gm_lb9:;
                uint8_t t32 = gm_l3;
                gm_l2 = t32;
            }
            uint8_t t33 = gm_l2;
            F.ret = t33;
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)20ULL));
            if (!t35) {
                goto gm_lb12;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb12:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb13:;
        gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 180, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_mutated_copies_vc6lpss(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_mutated_copies_vc6lpss F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* f */
    void * gm_l2; /* out */
    void * gm_l3; /* def */
    void * gm_l4; /* body */
    gm_t88rcucg1b7uk gm_l5; /* allocator */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t9d4sf0vqn7dt gm_l10; /* c */
    gm_tl874t2atnlh3 gm_l11; /* origin */
    GM_STACK_CHECK(&F, 1133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tkn42u7na3t81 gm_l13;
    gm_toceffb697p0u gm_l14;
    uint8_t gm_l15;
    gm_tftb1odc739t5 gm_l16;
    void * t7 = gm_l0;
    void * t8 = GM_LD(void *, (((uint8_t *)t7) + 384));
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t7) + 384) + 8));
    uint64_t t10 = gm_l1;
    if ((uintptr_t)t10 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t10, t9, gm_src5329n2jhsntp, 34, 13);
    void * t11 = (void *)(((uint8_t *)t8) + (uintptr_t)t10 * 64);
    gm_l3 = t11;
    void * t12 = gm_l3;
    uint8_t t13 = (uint8_t)(GM_LD(void *, (((uint8_t *)t12) + 40)) != 0);
    if (t13) {
        void * t14 = GM_LD(void *, (((uint8_t *)t12) + 40));
        gm_l4 = t14;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t15 = gm_l0;
    gm_tkn42u7na3t81 t16 = gm_f_allocator_ty_osls0uv((void *)NULL, cx, NULL, t15);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l13 = t16;
    uint8_t t17 = (uint8_t)((((uint8_t *)&gm_l13))[4] != 0);
    if (t17) {
        gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l13));
        gm_l5 = t18;
    } else {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        void * t19 = gm_l0;
        gm_t4c7ipjj9s3fp t20;
        gm_f_hash_map_5905oeo((void *)&t20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t21;
        gm_f_list_lbm57pe((void *)&t21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t22;
        gm_f_hash_map_70470jk((void *)&t22, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4c7ipjj9s3fp t23;
        gm_f_hash_map_cuh4fs9((void *)&t23, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t24 = gm_l5;
        gm_tfcpkukfav158 t25;
        GM_ST(uint32_t, (uint8_t *)&t25 + 220, 0);
        GM_ST(void *, ((uint8_t *)&t25 + 0), t19);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t25 + 8), t20);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t25 + 64), t21);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t25 + 104), t22);
        GM_ST(gm_t4c7ipjj9s3fp, ((uint8_t *)&t25 + 160), t23);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t25 + 216), t24);
        F.l6 = t25;
    }
    void * t26 = (void *)((uint8_t *)&F.l6);
    void * t27 = gm_l4;
    gm_b0_1 t28 = gm_f_copy_expr_vvegfhs((void *)NULL, cx, NULL, t26, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    {
        memmove(((uint8_t *)&F.l7), (((uint8_t *)&F.l6) + 64), 40);
    }
    gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
    gm_closure t30 = ((gm_closure){GM_PTR_TAG(gm_f_fn_gj9gcf0), NULL, cx});
    gm_f_sort_by_key_u6kj3vl(t29, t30);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
    gm_l8 = t31;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t32 = gm_l9;
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t34 = (t32 < t33);
            if (!t34) {
                goto gm_lb1;
            }
            {
                void * t35 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t37 = gm_l9;
                {
                    {
                        gm_t9d4sf0vqn7dt t38 = GM_LD(gm_t9d4sf0vqn7dt, (((uint8_t *)t35) + (uintptr_t)t37 * 16));
                        gm_l10 = t38;
                        void * t39 = (void *)(((uint8_t *)&F.l6) + 8);
                        uint32_t t40 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                        gm_toceffb697p0u t41 = gm_f_get_l84pf3h(t39, t40);
                        gm_l14 = t41;
                        uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l14))[12] != 0);
                        if (t42) {
                            gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l14));
                            gm_l11 = t43;
                        } else {
                            goto gm_lb3;
                        }
                        void * t44 = (void *)(((uint8_t *)&F.l6) + 104);
                        uint32_t t45 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                        uint8_t t46 = gm_f_contains_r1r3too(t44, t45);
                        if (t46) {
                            gm_l15 = ((uint8_t)1);
                        } else {
                            void * t47 = (void *)(((uint8_t *)&F.l6) + 160);
                            void * t48 = gm_l0;
                            void * t49 = GM_LD(void *, ((uint8_t *)t48));
                            void * t50 = GM_LD(void *, ((uint8_t *)t49));
                            gm_tl874t2atnlh3 t51 = gm_l11;
                            gm_b16_8 t52 = gm_f_text_of_lbn39it(t50, GM_CONV(gm_cseahvg8v7p0vs, t51));
                            uint8_t t53 = gm_f_contains_57n7jqd(t47, t52);
                            gm_l15 = t53;
                        }
                        uint8_t t54 = gm_l15;
                        if (t54) {
                            goto gm_lb3;
                        }
                        {
                            void * t55 = gm_l3;
                            void * t56 = GM_LD(void *, (((uint8_t *)t55) + 24));
                            uint64_t t57 = GM_LD(uint64_t, ((((uint8_t *)t55) + 24) + 8));
                            uint32_t t58 = GM_LD(uint32_t, ((uint8_t *)&gm_l10));
                            uint64_t t59 = (uint64_t)t58;
                            if ((uintptr_t)t59 >= (uintptr_t)(t57)) gm_trap_index((uintptr_t)t59, t57, gm_src5329n2jhsntp, 51, 18);
                            memmove(((uint8_t *)&F.l12), (((uint8_t *)t56) + (uintptr_t)t59 * 40), 40);
                        }
                        {
                            void * t60 = gm_l2;
                            gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l10) + 4));
                            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                            gm_tl874t2atnlh3 t63 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 24));
                            gm_tl874t2atnlh3 t64 = gm_l11;
                            gm_tis7ttivmj935 t65;
                            GM_ST(uint32_t, (uint8_t *)&t65 + 12, 0);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t65 + 0), t61);
                            GM_ST(gm_b16_8, ((uint8_t *)&t65 + 16), t62);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t65 + 32), t63);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t65 + 44), t64);
                            gm_tj5f8vajf35o7 t66;
                            GM_ST(uint8_t, (uint8_t *)&t66 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t66 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t66 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t66 + 64, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)9ULL));
                            GM_ST(gm_tis7ttivmj935, ((uint8_t *)&t66 + 8), t65);
                            gm_tftb1odc739t5 t67 = gm_f_push_rqlbv1e(t60, &t66);
                            gm_l16 = t67;
                        }
                        uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                        if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_src5329n2jhsntp, 52, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 48, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t69 = gm_l9;
            uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
            uint64_t t72 = r70;
            gm_l9 = t72;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_place_reads_si5djsr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_place_reads_si5djsr F;
    void * gm_l0; /* sc */
    void * gm_l1; /* p */
    void * gm_l2; /* base */
    void * gm_l3; /* base */
    void * gm_l4; /* index */
    GM_STACK_CHECK(&F, 124u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t6 = gm_l1;
    {
        {
            uint8_t t7 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t8 = (uint8_t)(t7 == ((uint8_t)19ULL));
            if (!t8) {
                goto gm_lb2;
            }
            void * t9 = GM_LD(void *, ((((uint8_t *)t6) + 8) + 8));
            gm_l2 = t9;
            void * t10 = gm_l0;
            void * t11 = gm_l2;
            gm_b0_1 t12 = gm_f_place_reads_si5djsr((void *)NULL, cx, NULL, t10, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)20ULL));
            if (!t14) {
                goto gm_lb3;
            }
            void * t15 = GM_LD(void *, ((((uint8_t *)t6) + 8) + 8));
            gm_l3 = t15;
            void * t16 = GM_LD(void *, (((((uint8_t *)t6) + 8) + 8) + 8));
            gm_l4 = t16;
            void * t17 = gm_l0;
            void * t18 = gm_l3;
            gm_b0_1 t19 = gm_f_place_reads_si5djsr((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t20 = gm_l0;
            void * t21 = gm_l4;
            gm_b0_1 t22 = gm_f_copy_expr_vvegfhs((void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb3:;
        {
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 160, 5);
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
inline __attribute__((aligned(4))) gm_tve56d3t8b8jf gm_f_rooted_local_cq8gk6j(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rooted_local_cq8gk6j F;
    void * gm_l0; /* s */
    void * gm_l1; /* p */
    uint32_t gm_l2; /* l */
    void * gm_l3; /* base */
    gm_t9805qtonbsag gm_l4; /* r */
    void * gm_l5; /* base */
    gm_t9805qtonbsag gm_l6; /* r */
    GM_STACK_CHECK(&F, 416u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tve56d3t8b8jf gm_l7;
    uint8_t gm_l8;
    gm_t53at80pfdus3 gm_l9;
    gm_tve56d3t8b8jf gm_l10;
    void * t10 = gm_l1;
    {
        {
            uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)15ULL));
            if (!t12) {
                goto gm_lb2;
            }
            uint32_t t13 = GM_LD(uint32_t, ((((uint8_t *)t10) + 8) + 8));
            gm_l2 = t13;
            uint32_t t14 = gm_l2;
            gm_t9805qtonbsag t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t15 + 0), t14);
            GM_ST(uint8_t, ((uint8_t *)&t15 + 4), ((uint8_t)1));
            gm_tve56d3t8b8jf t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 10, 0);
            GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[8] = 1;
            F.ret = t16;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)19ULL));
            if (!t18) {
                goto gm_lb3;
            }
            void * t19 = GM_LD(void *, ((((uint8_t *)t10) + 8) + 8));
            gm_l3 = t19;
            void * t20 = gm_l0;
            void * t21 = gm_l3;
            gm_tve56d3t8b8jf t22 = gm_f_rooted_local_cq8gk6j(t20, t21);
            gm_l7 = t22;
            uint8_t t23 = (uint8_t)((((uint8_t *)&gm_l7))[8] != 0);
            if (t23) {
                gm_t9805qtonbsag t24 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&gm_l7));
                gm_l4 = t24;
            } else {
                gm_tve56d3t8b8jf t25;
                memset(&t25, 0, sizeof t25);
                F.ret = t25;
                goto gm_fret;
            }
            uint32_t t26 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
            gm_t9805qtonbsag t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t27 + 0), t26);
            GM_ST(uint8_t, ((uint8_t *)&t27 + 4), ((uint8_t)0));
            gm_tve56d3t8b8jf t28;
            GM_ST(uint8_t, (uint8_t *)&t28 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t28 + 10, 0);
            GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t28), t27);
            ((uint8_t *)&t28)[8] = 1;
            F.ret = t28;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)20ULL));
            if (!t30) {
                goto gm_lb4;
            }
            void * t31 = GM_LD(void *, ((((uint8_t *)t10) + 8) + 8));
            gm_l5 = t31;
            void * t32 = gm_l0;
            void * t33 = (void *)(((uint8_t *)t32) + 8);
            void * t34 = gm_l5;
            gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t34));
            gm_f_kind_jcnt4hm((void *)&gm_l9, t33, GM_CONV(gm_csguankko4lmhd, t35));
            {
                {
                    uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)11ULL));
                    if (!t37) {
                        goto gm_lb6;
                    }
                    gm_l8 = ((uint8_t)1);
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    gm_l8 = ((uint8_t)0);
                    goto gm_lb5;
                }
gm_lb7:;
                gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 204, 16);
            }
gm_lb5:;
            uint8_t t38 = gm_l8;
            if (t38) {
                void * t39 = gm_l0;
                void * t40 = gm_l5;
                gm_tve56d3t8b8jf t41 = gm_f_rooted_local_cq8gk6j(t39, t40);
                gm_l10 = t41;
                uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
                if (t42) {
                    gm_t9805qtonbsag t43 = GM_LD(gm_t9805qtonbsag, ((uint8_t *)&gm_l10));
                    gm_l6 = t43;
                } else {
                    gm_tve56d3t8b8jf t44;
                    memset(&t44, 0, sizeof t44);
                    F.ret = t44;
                    goto gm_fret;
                }
                uint32_t t45 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
                gm_t9805qtonbsag t46;
                GM_ST(uint8_t, (uint8_t *)&t46 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t46 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t46 + 0), t45);
                GM_ST(uint8_t, ((uint8_t *)&t46 + 4), ((uint8_t)0));
                gm_tve56d3t8b8jf t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 10, 0);
                GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t47), t46);
                ((uint8_t *)&t47)[8] = 1;
                F.ret = t47;
                goto gm_fret;
            }
            gm_tve56d3t8b8jf t48;
            memset(&t48, 0, sizeof t48);
            F.ret = t48;
            goto gm_fret;
        }
gm_lb4:;
        {
            gm_tve56d3t8b8jf t49;
            memset(&t49, 0, sizeof t49);
            F.ret = t49;
            goto gm_fret;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_src5329n2jhsntp, 197, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}

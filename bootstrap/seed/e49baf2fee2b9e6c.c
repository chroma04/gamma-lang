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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_tmn7n0n9raou5 f0; uint8_t f1; uint8_t pe[3]; } gm_tbgiuvo2j5vcs;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(e: ^const Expr, l: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; } gm_thsh8t5u55gps;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { gm_tmn7n0n9raou5 f0; gm_t88rcucg1b7uk f1; } gm_tm7p68clol9c3;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_b16_8 v1; } f1; } gm_tvbp5g7fh5fh0;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t8588ma2cr9d7 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_t9q8lhljes74c;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgiuvo2j5vcs) == 12 && _Alignof(gm_tbgiuvo2j5vcs) == 4 && offsetof(gm_tbgiuvo2j5vcs, f0) == 0 && offsetof(gm_tbgiuvo2j5vcs, f1) == 8, "layout of `?Coercion`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thsh8t5u55gps) == 20 && _Alignof(gm_thsh8t5u55gps) == 4 && offsetof(gm_thsh8t5u55gps, f0) == 0 && offsetof(gm_thsh8t5u55gps, f1) == 4 && offsetof(gm_thsh8t5u55gps, f2) == 8, "layout of `(from: Ty, to: Ty, span: Span)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_tiilmfd16v3fc f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tj44oivvogvtl;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tm7p68clol9c3) == 12 && _Alignof(gm_tm7p68clol9c3) == 4 && offsetof(gm_tm7p68clol9c3, f0) == 0 && offsetof(gm_tm7p68clol9c3, f1) == 8, "layout of `Step`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tvbp5g7fh5fh0) == 24 && _Alignof(gm_tvbp5g7fh5fh0) == 8 && offsetof(gm_tvbp5g7fh5fh0, f0) == 0 && offsetof(gm_tvbp5g7fh5fh0, f1) == 8, "layout of `LiftResult`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9q8lhljes74c) == 32 && _Alignof(gm_t9q8lhljes74c) == 8 && offsetof(gm_t9q8lhljes74c, f0) == 0 && offsetof(gm_t9q8lhljes74c, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tj44oivvogvtl) == 40 && _Alignof(gm_tj44oivvogvtl) == 8 && offsetof(gm_tj44oivvogvtl, f0) == 0 && offsetof(gm_tj44oivvogvtl, f1) == 24, "layout of `Performed`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_t9q8lhljes74c v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_to0f5qn75cgg8;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_to0f5qn75cgg8) == 64 && _Alignof(gm_to0f5qn75cgg8) == 8 && offsetof(gm_to0f5qn75cgg8, f0) == 0 && offsetof(gm_to0f5qn75cgg8, f1) == 8, "layout of `ExprKind`");
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adds_const_e6d4bkb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_apply_steps_fd8cr97 {
    gm_frame h;
    void * ret;
};
struct gm_fr_base_coercion_t3tgsf0 {
    gm_frame h;
    gm_tddtjfggdat0d l25; /* a */
    gm_tddtjfggdat0d l26; /* b */
    gm_tbgiuvo2j5vcs ret;
};
struct gm_fr_coerce_sudc6c1 {
    gm_frame h;
    gm_t4gut2c9fj1kt l11; /* d */
    gm_tddtjfggdat0d l25; /* b */
    gm_tddtjfggdat0d l26; /* a */
    gm_tddtjfggdat0d l30; /* a */
    void * ret;
};
struct gm_fr_coercion_steps_ibqvhta {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_complex_of_kh32tib {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
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
struct gm_fr_empty_slice_c3p9jvl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_mcu2r7e {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_vhibpt8 {
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
struct gm_fr_format_dr0nbiu {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ns6dben {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_6canco6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_pointer_coercion_a03b7p1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3onuaru {
    gm_frame h;
    gm_tj44oivvogvtl l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_f21bjke {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_joldpsp {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2rchp81 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_dhs3lgm {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_s9eh08e {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_same_shape_m0o6aok {
    gm_frame h;
    uint8_t ret;
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
struct gm_fr_src_array_ok_og10umr {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_steps_for_768gb3b {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* out */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_steps_into_vts72l5 {
    gm_frame h;
    uint8_t ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqq39g9duc08(const uint8_t *a, const uint8_t *b);
inline bool gm_eqs65r2l0loh(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_adds_const_e6d4bkb(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void * gm_f_apply_steps_fd8cr97(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_array_as_slice_gvflfg2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tbgiuvo2j5vcs gm_f_base_coercion_t3tgsf0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_complex_of_kh32tib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_embeds_2jpqm45(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3p9jvl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mcu2r7e(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vhibpt8(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_format_dr0nbiu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_ns6dben(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_held_temp_d00dm1p(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_positional_array_6sufsk8(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_lift_variant_ptko7np(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_list_6canco6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) uint8_t gm_f_pointer_coercion_a03b7p1(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_prelude_instance_arg_kh32dmf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_promote_real_constant_v0nhili(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3onuaru(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f21bjke(void * a0, gm_tm7p68clol9c3 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_joldpsp(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2rchp81(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_dhs3lgm(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s9eh08e(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_same_shape_m0o6aok(void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_set_contains_8v9ep9m(void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_soa_coerce_c57qnm7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) uint8_t gm_f_src_array_ok_og10umr(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_steps_for_768gb3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3);
__attribute__((aligned(4))) uint8_t gm_f_steps_into_vts72l5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3, void * a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t9qkh0rsooc8d gm_f_unify_t2qjv2p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
extern const uint8_t gm_s0rphlcbno91v[8];
extern const uint8_t gm_sgpahsg79gte4[26];
extern const uint8_t gm_sn1h26ucppu4i[110];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4vq75vtnd3bq[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_ste817enr6egu[95];
extern const gm_tmpl gm_tpl5st0uuf0quab;
extern const gm_tmpl gm_tpl9v8qcr7jilen;
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
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqq39g9duc08(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    for (uintptr_t i = 0; i < n; i++) if (!gm_eq0iqiubfm7e(pa + i * 16, pb + i * 16)) return false;
    return true;
}
inline bool gm_eqs65r2l0loh(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq9m6mvtmgde(a, b);
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
inline __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_coercion_steps_ibqvhta F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_f_steps_for_768gb3b((void *)&(*(gm_th2lqbo77pvsc *)gm_out), (void *)NULL, cx, NULL, t2, t3, t4, ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3p9jvl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c3p9jvl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mcu2r7e(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_mcu2r7e F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vhibpt8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_vhibpt8 F;
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
inline __attribute__((aligned(4))) void gm_f_format_dr0nbiu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dr0nbiu F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ste817enr6egu));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(94));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5st0uuf0quab), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_ns6dben(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ns6dben F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgpahsg79gte4));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(25));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl9v8qcr7jilen), t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_infer_3o82a0b F;
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
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 252, 57);
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)t3) + (uintptr_t)t6 * 1));
    uint8_t t8 = (uint8_t)(t7 & ((uint8_t)2ULL));
    uint8_t t9 = (!(t8 == ((uint8_t)0ULL)));
    F.ret = t9;
    goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_list_6canco6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6canco6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_mcu2r7e();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3onuaru(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3onuaru F;
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
        gm_b16_8 t9 = gm_f_empty_slice_vhibpt8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_s9eh08e(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 40), ((uint8_t *)&F.l1), 40);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f21bjke(void * a0, gm_tm7p68clol9c3 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_f21bjke F;
    void * gm_l0; /* l */
    gm_tm7p68clol9c3 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_mcu2r7e();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_2rchp81(t8, ((uint64_t)1ULL), t10);
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
    gm_tm7p68clol9c3 t18 = gm_l1;
    GM_ST(gm_tm7p68clol9c3, (((uint8_t *)t16) + (uintptr_t)t17 * 12), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_joldpsp(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_joldpsp F;
    void * gm_l0; /* l */
    gm_thsh8t5u55gps gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_c3p9jvl();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_dhs3lgm(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 20), ((uint8_t *)a1), 20);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 20)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2rchp81(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2rchp81 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_dhs3lgm(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_dhs3lgm F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)20ULL), ((uint64_t)4ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s9eh08e(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_s9eh08e F;
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
__attribute__((aligned(4))) uint8_t gm_f_adds_const_e6d4bkb(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adds_const_e6d4bkb F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    void * gm_l3; /* tys */
    uint8_t gm_l4; /* sc */
    gm_t88rcucg1b7uk gm_l5; /* p */
    uint8_t gm_l6; /* tc */
    gm_t88rcucg1b7uk gm_l7; /* q */
    uint8_t gm_l8; /* sc */
    gm_t88rcucg1b7uk gm_l9; /* p */
    uint8_t gm_l10; /* tc */
    gm_t88rcucg1b7uk gm_l11; /* q */
    uint8_t gm_l12; /* sc */
    gm_t88rcucg1b7uk gm_l13; /* p */
    uint8_t gm_l14; /* tc */
    gm_t88rcucg1b7uk gm_l15; /* q */
    gm_t88rcucg1b7uk gm_l16; /* a */
    gm_t88rcucg1b7uk gm_l17; /* b */
    gm_t88rcucg1b7uk gm_l18; /* e1 */
    gm_t88rcucg1b7uk gm_l19; /* a */
    gm_t88rcucg1b7uk gm_l20; /* e2 */
    gm_t88rcucg1b7uk gm_l21; /* b */
    GM_STACK_CHECK(&F, 671u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    gm_t53at80pfdus3 gm_l22;
    gm_t53at80pfdus3 gm_l23;
    uint8_t gm_l24;
    uint8_t gm_l25;
    gm_t53at80pfdus3 gm_l26;
    uint8_t gm_l27;
    uint8_t gm_l28;
    gm_t53at80pfdus3 gm_l29;
    uint8_t gm_l30;
    uint8_t gm_l31;
    gm_t53at80pfdus3 gm_l32;
    gm_t53at80pfdus3 gm_l33;
    uint8_t gm_l34;
    void * t19 = gm_l0;
    void * t20 = (void *)(((uint8_t *)t19) + 8);
    gm_l3 = t20;
    void * t21 = gm_l3;
    gm_t88rcucg1b7uk t22 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l22, t21, GM_CONV(gm_csguankko4lmhd, t22));
    {
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)8ULL));
            if (!t24) {
                goto gm_lb2;
            }
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 8));
            gm_l4 = t25;
            gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l22) + 8) + 4));
            gm_l5 = t26;
            {
                {
                    void * t27 = gm_l3;
                    gm_t88rcucg1b7uk t28 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l23, t27, GM_CONV(gm_csguankko4lmhd, t28));
                    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                    uint8_t t30 = (uint8_t)(t29 == ((uint8_t)8ULL));
                    if (!t30) {
                        goto gm_lb4;
                    }
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 8));
                    gm_l6 = t31;
                    gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l23) + 8) + 4));
                    gm_l7 = t32;
                    gm_t88rcucg1b7uk t33 = gm_l5;
                    gm_t88rcucg1b7uk t34 = gm_l7;
                    uint8_t t35 = (gm_eq9m6mvtmgde(((uint8_t *)&t33), ((uint8_t *)&t34)));
                    if (t35) {
                        uint8_t t36 = gm_l6;
                        gm_l25 = t36;
                    } else {
                        gm_l25 = ((uint8_t)0);
                    }
                    uint8_t t37 = gm_l25;
                    if (t37) {
                        uint8_t t38 = gm_l4;
                        uint8_t t39 = (uint8_t)!t38;
                        gm_l24 = t39;
                    } else {
                        gm_l24 = ((uint8_t)0);
                    }
                    uint8_t t40 = gm_l24;
                    F.ret = t40;
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            uint8_t t42 = (uint8_t)(t41 == ((uint8_t)9ULL));
            if (!t42) {
                goto gm_lb5;
            }
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 8));
            gm_l8 = t43;
            gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l22) + 8) + 4));
            gm_l9 = t44;
            {
                {
                    void * t45 = gm_l3;
                    gm_t88rcucg1b7uk t46 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l26, t45, GM_CONV(gm_csguankko4lmhd, t46));
                    uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
                    uint8_t t48 = (uint8_t)(t47 == ((uint8_t)9ULL));
                    if (!t48) {
                        goto gm_lb7;
                    }
                    uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 8));
                    gm_l10 = t49;
                    gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l26) + 8) + 4));
                    gm_l11 = t50;
                    gm_t88rcucg1b7uk t51 = gm_l9;
                    gm_t88rcucg1b7uk t52 = gm_l11;
                    uint8_t t53 = (gm_eq9m6mvtmgde(((uint8_t *)&t51), ((uint8_t *)&t52)));
                    if (t53) {
                        uint8_t t54 = gm_l10;
                        gm_l28 = t54;
                    } else {
                        gm_l28 = ((uint8_t)0);
                    }
                    uint8_t t55 = gm_l28;
                    if (t55) {
                        uint8_t t56 = gm_l8;
                        uint8_t t57 = (uint8_t)!t56;
                        gm_l27 = t57;
                    } else {
                        gm_l27 = ((uint8_t)0);
                    }
                    uint8_t t58 = gm_l27;
                    F.ret = t58;
                    goto gm_fret;
                }
gm_lb7:;
            }
gm_lb6:;
            goto gm_lb1;
        }
gm_lb5:;
        {
            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            uint8_t t60 = (uint8_t)(t59 == ((uint8_t)10ULL));
            if (!t60) {
                goto gm_lb8;
            }
            uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 8));
            gm_l12 = t61;
            gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l22) + 8) + 4));
            gm_l13 = t62;
            {
                {
                    void * t63 = gm_l3;
                    gm_t88rcucg1b7uk t64 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l29, t63, GM_CONV(gm_csguankko4lmhd, t64));
                    uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                    uint8_t t66 = (uint8_t)(t65 == ((uint8_t)10ULL));
                    if (!t66) {
                        goto gm_lb10;
                    }
                    uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 8));
                    gm_l14 = t67;
                    gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l29) + 8) + 4));
                    gm_l15 = t68;
                    gm_t88rcucg1b7uk t69 = gm_l13;
                    gm_t88rcucg1b7uk t70 = gm_l15;
                    uint8_t t71 = (gm_eq9m6mvtmgde(((uint8_t *)&t69), ((uint8_t *)&t70)));
                    if (t71) {
                        uint8_t t72 = gm_l14;
                        gm_l31 = t72;
                    } else {
                        gm_l31 = ((uint8_t)0);
                    }
                    uint8_t t73 = gm_l31;
                    if (t73) {
                        uint8_t t74 = gm_l12;
                        uint8_t t75 = (uint8_t)!t74;
                        gm_l30 = t75;
                    } else {
                        gm_l30 = ((uint8_t)0);
                    }
                    uint8_t t76 = gm_l30;
                    F.ret = t76;
                    goto gm_fret;
                }
gm_lb10:;
            }
gm_lb9:;
            goto gm_lb1;
        }
gm_lb8:;
        {
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            uint8_t t78 = (uint8_t)(t77 == ((uint8_t)13ULL));
            if (!t78) {
                goto gm_lb11;
            }
            gm_t88rcucg1b7uk t79 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 8));
            gm_l16 = t79;
            {
                {
                    void * t80 = gm_l3;
                    gm_t88rcucg1b7uk t81 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l32, t80, GM_CONV(gm_csguankko4lmhd, t81));
                    uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l32) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)13ULL));
                    if (!t83) {
                        goto gm_lb13;
                    }
                    gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l32) + 8));
                    gm_l17 = t84;
                    void * t85 = gm_l0;
                    gm_t88rcucg1b7uk t86 = gm_l16;
                    gm_t88rcucg1b7uk t87 = gm_l17;
                    uint8_t t88 = gm_f_adds_const_e6d4bkb(t85, GM_CONV(gm_csguankko4lmhd, t86), GM_CONV(gm_csguankko4lmhd, t87));
                    F.ret = t88;
                    goto gm_fret;
                }
gm_lb13:;
            }
gm_lb12:;
            goto gm_lb1;
        }
gm_lb11:;
        {
            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l22) + 0));
            uint8_t t90 = (uint8_t)(t89 == ((uint8_t)14ULL));
            if (!t90) {
                goto gm_lb14;
            }
            gm_t88rcucg1b7uk t91 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l22) + 8));
            gm_l18 = t91;
            gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l22) + 8) + 4));
            gm_l19 = t92;
            {
                {
                    void * t93 = gm_l3;
                    gm_t88rcucg1b7uk t94 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l33, t93, GM_CONV(gm_csguankko4lmhd, t94));
                    uint8_t t95 = GM_LD(uint8_t, (((uint8_t *)&gm_l33) + 0));
                    uint8_t t96 = (uint8_t)(t95 == ((uint8_t)14ULL));
                    if (!t96) {
                        goto gm_lb16;
                    }
                    gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l33) + 8));
                    gm_l20 = t97;
                    gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l33) + 8) + 4));
                    gm_l21 = t98;
                    gm_t88rcucg1b7uk t99 = gm_l18;
                    gm_t88rcucg1b7uk t100 = gm_l20;
                    uint8_t t101 = (gm_eq9m6mvtmgde(((uint8_t *)&t99), ((uint8_t *)&t100)));
                    if (t101) {
                        void * t102 = gm_l0;
                        gm_t88rcucg1b7uk t103 = gm_l19;
                        gm_t88rcucg1b7uk t104 = gm_l21;
                        uint8_t t105 = gm_f_adds_const_e6d4bkb(t102, GM_CONV(gm_csguankko4lmhd, t103), GM_CONV(gm_csguankko4lmhd, t104));
                        gm_l34 = t105;
                    } else {
                        gm_l34 = ((uint8_t)0);
                    }
                    uint8_t t106 = gm_l34;
                    F.ret = t106;
                    goto gm_fret;
                }
gm_lb16:;
            }
gm_lb15:;
            goto gm_lb1;
        }
gm_lb14:;
        {
            goto gm_lb1;
        }
gm_lb17:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 198, 5);
    }
gm_lb1:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_apply_steps_fd8cr97(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_apply_steps_fd8cr97 F;
    void * gm_l0; /* e0 */
    gm_b16_8 gm_l1; /* steps */
    void * gm_l2; /* e */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tm7p68clol9c3 gm_l5; /* s */
    GM_STACK_CHECK(&F, 313u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = gm_l0;
    gm_l2 = t7;
    gm_b16_8 t8 = gm_l1;
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
                        gm_tm7p68clol9c3 t15 = GM_LD(gm_tm7p68clol9c3, (((uint8_t *)t12) + (uintptr_t)t14 * 12));
                        gm_l5 = t15;
                        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l5) + 8));
                        gm_tmn7n0n9raou5 t17 = GM_LD(gm_tmn7n0n9raou5, ((uint8_t *)&gm_l5));
                        void * t18 = gm_l2;
                        gm_t8jsc4n9vp2ib t19;
                        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t19 + 0), t17);
                        GM_ST(void *, ((uint8_t *)&t19 + 8), t18);
                        void * t20 = gm_l2;
                        gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 72));
                        gm_to0f5qn75cgg8 t22;
                        memset(&t22, 0, sizeof t22);
                        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)32ULL));
                        GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t22 + 8), t19);
                        void * t23 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t16, &t22, t21);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l2 = t23;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 136, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t24 = gm_l4;
            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
            uint64_t t27 = r25;
            gm_l4 = t27;
        }
    }
gm_lb1:;
    void * t28 = gm_l2;
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
__attribute__((aligned(4))) gm_tbgiuvo2j5vcs gm_f_base_coercion_t3tgsf0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_base_coercion_t3tgsf0 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    void * gm_l3; /* tys */
    uint8_t gm_l4; /* sa */
    uint16_t gm_l5; /* ba */
    uint8_t gm_l6; /* sb */
    uint16_t gm_l7; /* bb */
    uint8_t gm_l8; /* ok */
    uint16_t gm_l9; /* a */
    uint16_t gm_l10; /* b */
    gm_b16_8 gm_l11; /* a */
    gm_b16_8 gm_l12; /* na */
    gm_b16_8 gm_l13; /* b */
    gm_b16_8 gm_l14; /* nb */
    uint8_t gm_l15; /* same */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint64_t gm_l18; /* i */
    gm_t88rcucg1b7uk gm_l19; /* a */
    gm_t88rcucg1b7uk gm_l20; /* b */
    gm_t88rcucg1b7uk gm_l21; /* e1 */
    gm_t88rcucg1b7uk gm_l22; /* a */
    gm_t88rcucg1b7uk gm_l23; /* e2 */
    gm_t88rcucg1b7uk gm_l24; /* b */
    uint8_t gm_l27; /* same */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* i */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    gm_tiilmfd16v3fc gm_l33; /* x */
    gm_t88rcucg1b7uk gm_l34; /* pointee */
    GM_STACK_CHECK(&F, 2144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l35;
    gm_t53at80pfdus3 gm_l36;
    uint8_t gm_l37;
    gm_tbgiuvo2j5vcs gm_l38;
    gm_tbgiuvo2j5vcs gm_l39;
    gm_tbgiuvo2j5vcs gm_l40;
    uint8_t gm_l41;
    uint8_t gm_l42;
    uint8_t gm_l43;
    gm_t53at80pfdus3 gm_l44;
    gm_tbgiuvo2j5vcs gm_l45;
    gm_t53at80pfdus3 gm_l46;
    gm_tbgiuvo2j5vcs gm_l47;
    uint8_t gm_l48;
    gm_t53at80pfdus3 gm_l49;
    gm_tbgiuvo2j5vcs gm_l50;
    gm_t53at80pfdus3 gm_l51;
    gm_t53at80pfdus3 gm_l52;
    uint8_t gm_l53;
    uint8_t gm_l54;
    uint8_t gm_l55;
    gm_t53at80pfdus3 gm_l56;
    uint8_t gm_l57;
    gm_t53at80pfdus3 gm_l58;
    uint8_t gm_l59;
    gm_t53at80pfdus3 gm_l60;
    {
        void * t54 = gm_l0;
        void * t55 = (void *)(((uint8_t *)t54) + 8);
        gm_l3 = t55;
        void * t56 = gm_l3;
        gm_t88rcucg1b7uk t57 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&gm_l35, t56, GM_CONV(gm_csguankko4lmhd, t57));
        {
            {
                {
                    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                    uint8_t t59 = (uint8_t)(t58 == ((uint8_t)5ULL));
                    if (!t59) {
                        goto gm_lb2;
                    }
                    gm_tmn7n0n9raou5 t60;
                    GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t60 + 0), ((uint8_t)5ULL));
                    gm_tbgiuvo2j5vcs t61;
                    GM_ST(uint8_t, (uint8_t *)&t61 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t61 + 10, 0);
                    GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t61), t60);
                    ((uint8_t *)&t61)[8] = 1;
                    F.ret = t61;
                    goto gm_fret;
                }
gm_lb2:;
                {
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                    uint8_t t63 = (uint8_t)(t62 == ((uint8_t)0ULL));
                    if (!t63) {
                        goto gm_lb3;
                    }
                    uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 8));
                    gm_l4 = t64;
                    uint16_t t65 = GM_LD(uint16_t, ((((uint8_t *)&gm_l35) + 8) + 2));
                    gm_l5 = t65;
                    void * t66 = gm_l3;
                    gm_t88rcucg1b7uk t67 = gm_l2;
                    gm_f_kind_jcnt4hm((void *)&gm_l36, t66, GM_CONV(gm_csguankko4lmhd, t67));
                    {
                        {
                            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l36) + 0));
                            uint8_t t69 = (uint8_t)(t68 == ((uint8_t)0ULL));
                            if (!t69) {
                                goto gm_lb5;
                            }
                            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l36) + 8));
                            gm_l6 = t70;
                            uint16_t t71 = GM_LD(uint16_t, ((((uint8_t *)&gm_l36) + 8) + 2));
                            gm_l7 = t71;
                            uint8_t t72 = gm_l4;
                            uint8_t t73 = gm_l6;
                            uint8_t t74 = ((t72 == t73));
                            if (t74) {
                                uint16_t t75 = gm_l7;
                                uint16_t t76 = gm_l5;
                                uint8_t t77 = (t75 >= t76);
                                gm_l8 = t77;
                            } else {
                                uint8_t t78 = gm_l4;
                                uint8_t t79 = (uint8_t)!t78;
                                if (t79) {
                                    uint16_t t80 = gm_l7;
                                    uint16_t t81 = gm_l5;
                                    uint8_t t82 = (t80 > t81);
                                    gm_l37 = t82;
                                } else {
                                    gm_l37 = ((uint8_t)0);
                                }
                                uint8_t t83 = gm_l37;
                                gm_l8 = t83;
                            }
                            uint8_t t84 = gm_l8;
                            if (t84) {
                                gm_tmn7n0n9raou5 t85;
                                GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)0ULL));
                                gm_tbgiuvo2j5vcs t86;
                                GM_ST(uint8_t, (uint8_t *)&t86 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t86 + 10, 0);
                                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t86), t85);
                                ((uint8_t *)&t86)[8] = 1;
                                gm_l38 = t86;
                            } else {
                                gm_tbgiuvo2j5vcs t87;
                                memset(&t87, 0, sizeof t87);
                                gm_l38 = t87;
                            }
                            gm_tbgiuvo2j5vcs t88 = gm_l38;
                            F.ret = t88;
                            goto gm_fret;
                        }
gm_lb5:;
                        {
                            uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&gm_l36) + 0));
                            uint8_t t90 = (uint8_t)(t89 == ((uint8_t)1ULL));
                            if (!t90) {
                                goto gm_lb6;
                            }
                            {
                                {
                                    uint8_t t91 = gm_l4;
                                    uint8_t t92 = (uint8_t)!t91;
                                    if (!t92) {
                                        goto gm_lb8;
                                    }
                                    uint16_t t93 = gm_l5;
                                    uint8_t t94 = (t93 <= ((uint16_t)32ULL));
                                    if (!t94) {
                                        goto gm_lb8;
                                    }
                                    gm_tmn7n0n9raou5 t95;
                                    GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)0ULL));
                                    gm_tbgiuvo2j5vcs t96;
                                    GM_ST(uint8_t, (uint8_t *)&t96 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t96 + 10, 0);
                                    GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t96), t95);
                                    ((uint8_t *)&t96)[8] = 1;
                                    gm_l39 = t96;
                                    goto gm_lb7;
                                }
gm_lb8:;
                                gm_tbgiuvo2j5vcs t97;
                                memset(&t97, 0, sizeof t97);
                                gm_l39 = t97;
                            }
gm_lb7:;
                            gm_tbgiuvo2j5vcs t98 = gm_l39;
                            F.ret = t98;
                            goto gm_fret;
                        }
gm_lb6:;
                        {
                            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&gm_l36) + 0));
                            uint8_t t100 = (uint8_t)(t99 == ((uint8_t)2ULL));
                            if (!t100) {
                                goto gm_lb9;
                            }
                            uint8_t t101 = gm_l4;
                            uint8_t t102 = (uint8_t)!t101;
                            if (t102) {
                                uint16_t t103 = gm_l5;
                                uint8_t t104 = (t103 <= ((uint16_t)31ULL));
                                gm_l42 = t104;
                            } else {
                                gm_l42 = ((uint8_t)0);
                            }
                            uint8_t t105 = gm_l42;
                            if (t105) {
                                gm_l41 = ((uint8_t)1);
                            } else {
                                uint8_t t106 = gm_l4;
                                if (t106) {
                                    uint16_t t107 = gm_l5;
                                    uint8_t t108 = (t107 <= ((uint16_t)32ULL));
                                    gm_l43 = t108;
                                } else {
                                    gm_l43 = ((uint8_t)0);
                                }
                                uint8_t t109 = gm_l43;
                                gm_l41 = t109;
                            }
                            uint8_t t110 = gm_l41;
                            if (t110) {
                                gm_tmn7n0n9raou5 t111;
                                GM_ST(uint8_t, (uint8_t *)&t111 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t111 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t111 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)0ULL));
                                gm_tbgiuvo2j5vcs t112;
                                GM_ST(uint8_t, (uint8_t *)&t112 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t112 + 10, 0);
                                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t112), t111);
                                ((uint8_t *)&t112)[8] = 1;
                                gm_l40 = t112;
                            } else {
                                gm_tbgiuvo2j5vcs t113;
                                memset(&t113, 0, sizeof t113);
                                gm_l40 = t113;
                            }
                            gm_tbgiuvo2j5vcs t114 = gm_l40;
                            F.ret = t114;
                            goto gm_fret;
                        }
gm_lb9:;
                        {
                            goto gm_lb4;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 145, 25);
                    }
gm_lb4:;
                    goto gm_lb1;
                }
gm_lb3:;
            }
            {
                {
                    uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                    uint8_t t116 = (uint8_t)(t115 == ((uint8_t)3ULL));
                    if (!t116) {
                        goto gm_lb11;
                    }
                    uint16_t t117 = GM_LD(uint16_t, (((uint8_t *)&gm_l35) + 8));
                    gm_l9 = t117;
                    {
                        {
                            void * t118 = gm_l3;
                            gm_t88rcucg1b7uk t119 = gm_l2;
                            gm_f_kind_jcnt4hm((void *)&gm_l44, t118, GM_CONV(gm_csguankko4lmhd, t119));
                            uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&gm_l44) + 0));
                            uint8_t t121 = (uint8_t)(t120 == ((uint8_t)3ULL));
                            if (!t121) {
                                goto gm_lb13;
                            }
                            uint16_t t122 = GM_LD(uint16_t, (((uint8_t *)&gm_l44) + 8));
                            gm_l10 = t122;
                            uint16_t t123 = gm_l10;
                            uint16_t t124 = gm_l9;
                            uint8_t t125 = (t123 > t124);
                            if (t125) {
                                gm_tmn7n0n9raou5 t126;
                                GM_ST(uint8_t, (uint8_t *)&t126 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t126 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t126 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)1ULL));
                                gm_tbgiuvo2j5vcs t127;
                                GM_ST(uint8_t, (uint8_t *)&t127 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t127 + 10, 0);
                                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t127), t126);
                                ((uint8_t *)&t127)[8] = 1;
                                gm_l45 = t127;
                            } else {
                                gm_tbgiuvo2j5vcs t128;
                                memset(&t128, 0, sizeof t128);
                                gm_l45 = t128;
                            }
                            gm_tbgiuvo2j5vcs t129 = gm_l45;
                            F.ret = t129;
                            goto gm_fret;
                        }
gm_lb13:;
                    }
gm_lb12:;
                    goto gm_lb1;
                }
gm_lb11:;
                {
                    uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                    uint8_t t131 = (uint8_t)(t130 == ((uint8_t)12ULL));
                    if (!t131) {
                        goto gm_lb14;
                    }
                    gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l35) + 8));
                    gm_l11 = t132;
                    gm_b16_8 t133 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l35) + 8) + 16));
                    gm_l12 = t133;
                    {
                        {
                            void * t134 = gm_l3;
                            gm_t88rcucg1b7uk t135 = gm_l2;
                            gm_f_kind_jcnt4hm((void *)&gm_l46, t134, GM_CONV(gm_csguankko4lmhd, t135));
                            uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)&gm_l46) + 0));
                            uint8_t t137 = (uint8_t)(t136 == ((uint8_t)12ULL));
                            if (!t137) {
                                goto gm_lb16;
                            }
                            gm_b16_8 t138 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                            gm_l13 = t138;
                            gm_b16_8 t139 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l46) + 8) + 16));
                            gm_l14 = t139;
                            uint64_t t140 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                            uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                            uint8_t t142 = ((t140 == t141));
                            gm_l15 = t142;
                            uint8_t t143 = gm_l15;
                            if (t143) {
                                gm_l17 = ((uint64_t)0ULL);
                                uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                gm_l16 = t144;
                                {
                                    for (;;) {
                                        uint64_t t145 = gm_l17;
                                        uint64_t t146 = gm_l16;
                                        uint8_t t147 = (t145 < t146);
                                        if (!t147) {
                                            goto gm_lb17;
                                        }
                                        {
                                            {
                                                {
                                                    uint64_t t148 = gm_l17;
                                                    gm_l18 = t148;
                                                    void * t149 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                                    uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                                    uint64_t t151 = gm_l18;
                                                    gm_t88rcucg1b7uk t152 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t149) + (uintptr_t)t151 * 4));
                                                    void * t153 = GM_LD(void *, ((uint8_t *)&gm_l13));
                                                    uint64_t t154 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                                                    uint64_t t155 = gm_l18;
                                                    if ((uintptr_t)t155 >= (uintptr_t)(t154)) gm_trap_index((uintptr_t)t155, t154, gm_src4vq75vtnd3bq, 159, 36);
                                                    gm_t88rcucg1b7uk t156 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t153) + (uintptr_t)t155 * 4));
                                                    uint8_t t157 = (!gm_eq9m6mvtmgde(((uint8_t *)&t152), ((uint8_t *)&t156)));
                                                    if (t157) {
                                                        gm_l15 = ((uint8_t)0);
                                                    }
                                                    goto gm_lb20;
                                                }
gm_lb21:;
                                                gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 158, 17);
                                            }
gm_lb20:;
                                        }
gm_lb19:;
                                        uint64_t t158 = gm_l17;
                                        uint64_t r159; bool o160 = __builtin_add_overflow(t158, ((uint64_t)1ULL), &r159);
                                        uint64_t t161 = r159;
                                        gm_l17 = t161;
                                    }
                                }
gm_lb17:;
                            }
                            {
                                {
                                    uint8_t t162 = gm_l15;
                                    if (!t162) {
                                        goto gm_lb23;
                                    }
                                    gm_b16_8 t163 = gm_l12;
                                    gm_b16_8 t164 = gm_l14;
                                    uint8_t t165 = (!gm_eqq39g9duc08(((uint8_t *)&t163), ((uint8_t *)&t164)));
                                    if (!t165) {
                                        goto gm_lb23;
                                    }
                                    uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)&gm_l12) + 8));
                                    uint8_t t167 = ((t166 == ((uint64_t)0ULL)));
                                    if (t167) {
                                        gm_l48 = ((uint8_t)1);
                                    } else {
                                        uint64_t t168 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                                        uint8_t t169 = ((t168 == ((uint64_t)0ULL)));
                                        gm_l48 = t169;
                                    }
                                    uint8_t t170 = gm_l48;
                                    if (!t170) {
                                        goto gm_lb23;
                                    }
                                    gm_tmn7n0n9raou5 t171;
                                    GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t171 + 0), ((uint8_t)8ULL));
                                    gm_tbgiuvo2j5vcs t172;
                                    GM_ST(uint8_t, (uint8_t *)&t172 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t172 + 10, 0);
                                    GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t172), t171);
                                    ((uint8_t *)&t172)[8] = 1;
                                    gm_l47 = t172;
                                    goto gm_lb22;
                                }
gm_lb23:;
                                gm_tbgiuvo2j5vcs t173;
                                memset(&t173, 0, sizeof t173);
                                gm_l47 = t173;
                            }
gm_lb22:;
                            gm_tbgiuvo2j5vcs t174 = gm_l47;
                            F.ret = t174;
                            goto gm_fret;
                        }
gm_lb16:;
                    }
gm_lb15:;
                    goto gm_lb1;
                }
gm_lb14:;
            }
            {
                {
                    uint8_t t175 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                    uint8_t t176 = (uint8_t)(t175 == ((uint8_t)13ULL));
                    if (!t176) {
                        goto gm_lb24;
                    }
                    gm_t88rcucg1b7uk t177 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l35) + 8));
                    gm_l19 = t177;
                    {
                        {
                            void * t178 = gm_l3;
                            gm_t88rcucg1b7uk t179 = gm_l2;
                            gm_f_kind_jcnt4hm((void *)&gm_l49, t178, GM_CONV(gm_csguankko4lmhd, t179));
                            uint8_t t180 = GM_LD(uint8_t, (((uint8_t *)&gm_l49) + 0));
                            uint8_t t181 = (uint8_t)(t180 == ((uint8_t)13ULL));
                            if (!t181) {
                                goto gm_lb26;
                            }
                            gm_t88rcucg1b7uk t182 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l49) + 8));
                            gm_l20 = t182;
                            void * t183 = gm_l0;
                            gm_t88rcucg1b7uk t184 = gm_l19;
                            uint8_t t185 = gm_f_is_nullable_pointer_nkie06s(t183, GM_CONV(gm_csguankko4lmhd, t184));
                            if (t185) {
                                void * t186 = gm_l0;
                                gm_t88rcucg1b7uk t187 = gm_l19;
                                gm_t88rcucg1b7uk t188 = gm_l20;
                                uint8_t t189 = gm_f_pointer_coercion_a03b7p1(t186, GM_CONV(gm_csguankko4lmhd, t187), GM_CONV(gm_csguankko4lmhd, t188));
                                if (t189) {
                                    gm_tmn7n0n9raou5 t190;
                                    GM_ST(uint8_t, (uint8_t *)&t190 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t190 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t190 + 4, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t190 + 0), ((uint8_t)11ULL));
                                    gm_tbgiuvo2j5vcs t191;
                                    GM_ST(uint8_t, (uint8_t *)&t191 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t191 + 10, 0);
                                    GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t191), t190);
                                    ((uint8_t *)&t191)[8] = 1;
                                    gm_l50 = t191;
                                } else {
                                    gm_tbgiuvo2j5vcs t192;
                                    memset(&t192, 0, sizeof t192);
                                    gm_l50 = t192;
                                }
                                gm_tbgiuvo2j5vcs t193 = gm_l50;
                                F.ret = t193;
                                goto gm_fret;
                            }
                            void * t194 = gm_l0;
                            gm_t88rcucg1b7uk t195 = gm_l19;
                            gm_t88rcucg1b7uk t196 = gm_l20;
                            uint8_t t197 = gm_f_adds_const_e6d4bkb(t194, GM_CONV(gm_csguankko4lmhd, t195), GM_CONV(gm_csguankko4lmhd, t196));
                            if (t197) {
                                gm_tmn7n0n9raou5 t198;
                                GM_ST(uint8_t, (uint8_t *)&t198 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t198 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t198 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)6ULL));
                                gm_tbgiuvo2j5vcs t199;
                                GM_ST(uint8_t, (uint8_t *)&t199 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t199 + 10, 0);
                                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t199), t198);
                                ((uint8_t *)&t199)[8] = 1;
                                F.ret = t199;
                                goto gm_fret;
                            }
                            goto gm_lb25;
                        }
gm_lb26:;
                    }
gm_lb25:;
                    goto gm_lb1;
                }
gm_lb24:;
                {
                    uint8_t t200 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                    uint8_t t201 = (uint8_t)(t200 == ((uint8_t)14ULL));
                    if (!t201) {
                        goto gm_lb27;
                    }
                    gm_t88rcucg1b7uk t202 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l35) + 8));
                    gm_l21 = t202;
                    gm_t88rcucg1b7uk t203 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l35) + 8) + 4));
                    gm_l22 = t203;
                    {
                        {
                            void * t204 = gm_l3;
                            gm_t88rcucg1b7uk t205 = gm_l2;
                            gm_f_kind_jcnt4hm((void *)&gm_l51, t204, GM_CONV(gm_csguankko4lmhd, t205));
                            uint8_t t206 = GM_LD(uint8_t, (((uint8_t *)&gm_l51) + 0));
                            uint8_t t207 = (uint8_t)(t206 == ((uint8_t)14ULL));
                            if (!t207) {
                                goto gm_lb29;
                            }
                            gm_t88rcucg1b7uk t208 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l51) + 8));
                            gm_l23 = t208;
                            gm_t88rcucg1b7uk t209 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l51) + 8) + 4));
                            gm_l24 = t209;
                            gm_t88rcucg1b7uk t210 = gm_l21;
                            gm_t88rcucg1b7uk t211 = gm_l23;
                            uint8_t t212 = (gm_eq9m6mvtmgde(((uint8_t *)&t210), ((uint8_t *)&t211)));
                            if (!t212) {
                                goto gm_lb29;
                            }
                            void * t213 = gm_l0;
                            gm_t88rcucg1b7uk t214 = gm_l22;
                            gm_t88rcucg1b7uk t215 = gm_l24;
                            uint8_t t216 = gm_f_adds_const_e6d4bkb(t213, GM_CONV(gm_csguankko4lmhd, t214), GM_CONV(gm_csguankko4lmhd, t215));
                            if (!t216) {
                                goto gm_lb29;
                            }
                            gm_tmn7n0n9raou5 t217;
                            GM_ST(uint8_t, (uint8_t *)&t217 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t217 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t217 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t217 + 0), ((uint8_t)6ULL));
                            gm_tbgiuvo2j5vcs t218;
                            GM_ST(uint8_t, (uint8_t *)&t218 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t218 + 10, 0);
                            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t218), t217);
                            ((uint8_t *)&t218)[8] = 1;
                            F.ret = t218;
                            goto gm_fret;
                        }
gm_lb29:;
                    }
gm_lb28:;
                    goto gm_lb1;
                }
gm_lb27:;
            }
            {
                {
                    uint8_t t219 = GM_LD(uint8_t, (((uint8_t *)&gm_l35) + 0));
                    uint8_t t220 = (uint8_t)(t219 == ((uint8_t)16ULL));
                    if (!t220) {
                        goto gm_lb30;
                    }
                    memmove(((uint8_t *)&F.l25), (((uint8_t *)&gm_l35) + 8), 48);
                    {
                        {
                            {
                                void * t221 = gm_l3;
                                gm_t88rcucg1b7uk t222 = gm_l2;
                                gm_f_kind_jcnt4hm((void *)&gm_l52, t221, GM_CONV(gm_csguankko4lmhd, t222));
                            }
                            uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&gm_l52) + 0));
                            uint8_t t224 = (uint8_t)(t223 == ((uint8_t)16ULL));
                            if (!t224) {
                                goto gm_lb32;
                            }
                            {
                                memmove(((uint8_t *)&F.l26), (((uint8_t *)&gm_l52) + 8), 48);
                            }
                            uint64_t t225 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 8));
                            uint64_t t226 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 8));
                            uint8_t t227 = ((t225 == t226));
                            if (t227) {
                                gm_t88rcucg1b7uk t228 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l25) + 16));
                                gm_t88rcucg1b7uk t229 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l26) + 16));
                                uint8_t t230 = (gm_eq9m6mvtmgde(((uint8_t *)&t228), ((uint8_t *)&t229)));
                                gm_l54 = t230;
                            } else {
                                gm_l54 = ((uint8_t)0);
                            }
                            uint8_t t231 = gm_l54;
                            if (t231) {
                                uint8_t t232 = GM_LD(uint8_t, (((uint8_t *)&F.l25) + 40));
                                uint8_t t233 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 40));
                                uint8_t t234 = ((t232 == t233));
                                gm_l53 = t234;
                            } else {
                                gm_l53 = ((uint8_t)0);
                            }
                            uint8_t t235 = gm_l53;
                            gm_l27 = t235;
                            uint8_t t236 = gm_l27;
                            if (t236) {
                                gm_l29 = ((uint64_t)0ULL);
                                uint64_t t237 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 8));
                                gm_l28 = t237;
                                {
                                    for (;;) {
                                        uint64_t t238 = gm_l29;
                                        uint64_t t239 = gm_l28;
                                        uint8_t t240 = (t238 < t239);
                                        if (!t240) {
                                            goto gm_lb33;
                                        }
                                        {
                                            {
                                                {
                                                    uint64_t t241 = gm_l29;
                                                    gm_l30 = t241;
                                                    void * t242 = GM_LD(void *, ((uint8_t *)&F.l25));
                                                    uint64_t t243 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 8));
                                                    uint64_t t244 = gm_l30;
                                                    gm_t88rcucg1b7uk t245 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t242) + (uintptr_t)t244 * 4));
                                                    void * t246 = GM_LD(void *, ((uint8_t *)&F.l26));
                                                    uint64_t t247 = GM_LD(uint64_t, (((uint8_t *)&F.l26) + 8));
                                                    uint64_t t248 = gm_l30;
                                                    if ((uintptr_t)t248 >= (uintptr_t)(t247)) gm_trap_index((uintptr_t)t248, t247, gm_src4vq75vtnd3bq, 173, 43);
                                                    gm_t88rcucg1b7uk t249 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t246) + (uintptr_t)t248 * 4));
                                                    uint8_t t250 = (!gm_eq9m6mvtmgde(((uint8_t *)&t245), ((uint8_t *)&t249)));
                                                    if (t250) {
                                                        gm_l27 = ((uint8_t)0);
                                                    }
                                                    goto gm_lb36;
                                                }
gm_lb37:;
                                                gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 172, 17);
                                            }
gm_lb36:;
                                        }
gm_lb35:;
                                        uint64_t t251 = gm_l29;
                                        uint64_t r252; bool o253 = __builtin_add_overflow(t251, ((uint64_t)1ULL), &r252);
                                        uint64_t t254 = r252;
                                        gm_l29 = t254;
                                    }
                                }
gm_lb33:;
                            }
                            uint8_t t255 = gm_l27;
                            uint8_t t256 = (uint8_t)!t255;
                            if (t256) {
                                gm_l55 = ((uint8_t)1);
                            } else {
                                uint64_t t257 = GM_LD(uint64_t, ((((uint8_t *)&F.l25) + 24) + 8));
                                uint64_t t258 = GM_LD(uint64_t, ((((uint8_t *)&F.l26) + 24) + 8));
                                uint8_t t259 = (t257 >= t258);
                                gm_l55 = t259;
                            }
                            uint8_t t260 = gm_l55;
                            if (t260) {
                                gm_tbgiuvo2j5vcs t261;
                                memset(&t261, 0, sizeof t261);
                                F.ret = t261;
                                goto gm_fret;
                            }
                            gm_b16_8 t262 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 24));
                            gm_l31 = t262;
                            gm_l32 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t263 = gm_l32;
                                    uint64_t t264 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                    uint8_t t265 = (t263 < t264);
                                    if (!t265) {
                                        goto gm_lb38;
                                    }
                                    {
                                        void * t266 = GM_LD(void *, ((uint8_t *)&gm_l31));
                                        uint64_t t267 = GM_LD(uint64_t, (((uint8_t *)&gm_l31) + 8));
                                        uint64_t t268 = gm_l32;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l33), (((uint8_t *)t266) + (uintptr_t)t268 * 24), 24);
                                                void * t269 = (void *)(((uint8_t *)&F.l26) + 24);
                                                void * t270 = (void *)((uint8_t *)&gm_l33);
                                                uint8_t t271 = gm_f_set_contains_8v9ep9m(t269, t270);
                                                uint8_t t272 = (uint8_t)!t271;
                                                if (t272) {
                                                    gm_tbgiuvo2j5vcs t273;
                                                    memset(&t273, 0, sizeof t273);
                                                    F.ret = t273;
                                                    goto gm_fret;
                                                }
                                                goto gm_lb41;
                                            }
gm_lb42:;
                                            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 177, 13);
                                        }
gm_lb41:;
                                    }
gm_lb40:;
                                    uint64_t t274 = gm_l32;
                                    uint64_t r275; bool o276 = __builtin_add_overflow(t274, ((uint64_t)1ULL), &r275);
                                    uint64_t t277 = r275;
                                    gm_l32 = t277;
                                }
                            }
gm_lb38:;
                            gm_tmn7n0n9raou5 t278;
                            GM_ST(uint8_t, (uint8_t *)&t278 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t278 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t278 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t278 + 0), ((uint8_t)10ULL));
                            gm_tbgiuvo2j5vcs t279;
                            GM_ST(uint8_t, (uint8_t *)&t279 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t279 + 10, 0);
                            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t279), t278);
                            ((uint8_t *)&t279)[8] = 1;
                            F.ret = t279;
                            goto gm_fret;
                        }
gm_lb32:;
                    }
gm_lb31:;
                    goto gm_lb1;
                }
gm_lb30:;
            }
            {
                {
                    goto gm_lb1;
                }
gm_lb43:;
                gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 143, 5);
            }
        }
gm_lb1:;
    }
    {
        void * t280 = gm_l0;
        gm_t88rcucg1b7uk t281 = gm_l1;
        gm_t88rcucg1b7uk t282 = gm_l2;
        uint8_t t283 = gm_f_pointer_coercion_a03b7p1(t280, GM_CONV(gm_csguankko4lmhd, t281), GM_CONV(gm_csguankko4lmhd, t282));
        if (t283) {
            {
                {
                    {
                        void * t284 = gm_l3;
                        gm_t88rcucg1b7uk t285 = gm_l1;
                        gm_f_kind_jcnt4hm((void *)&gm_l56, t284, GM_CONV(gm_csguankko4lmhd, t285));
                    }
                    uint8_t t286 = GM_LD(uint8_t, (((uint8_t *)&gm_l56) + 0));
                    uint8_t t287 = (uint8_t)(t286 == ((uint8_t)8ULL));
                    if (!t287) {
                        goto gm_lb45;
                    }
                    gm_t88rcucg1b7uk t288 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l56) + 8) + 4));
                    gm_l34 = t288;
                    {
                        void * t289 = gm_l3;
                        gm_t88rcucg1b7uk t290 = gm_l2;
                        gm_f_kind_jcnt4hm((void *)&gm_l58, t289, GM_CONV(gm_csguankko4lmhd, t290));
                    }
                    {
                        {
                            uint8_t t291 = GM_LD(uint8_t, (((uint8_t *)&gm_l58) + 0));
                            uint8_t t292 = (uint8_t)(t291 == ((uint8_t)10ULL));
                            if (!t292) {
                                goto gm_lb47;
                            }
                            gm_l57 = ((uint8_t)1);
                            goto gm_lb46;
                        }
gm_lb47:;
                        {
                            gm_l57 = ((uint8_t)0);
                            goto gm_lb46;
                        }
gm_lb48:;
                        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 185, 50);
                    }
gm_lb46:;
                    uint8_t t293 = gm_l57;
                    if (!t293) {
                        goto gm_lb45;
                    }
                    {
                        void * t294 = gm_l3;
                        gm_t88rcucg1b7uk t295 = gm_l34;
                        gm_f_kind_jcnt4hm((void *)&gm_l60, t294, GM_CONV(gm_csguankko4lmhd, t295));
                    }
                    {
                        {
                            uint8_t t296 = GM_LD(uint8_t, (((uint8_t *)&gm_l60) + 0));
                            uint8_t t297 = (uint8_t)(t296 == ((uint8_t)11ULL));
                            if (!t297) {
                                goto gm_lb50;
                            }
                            gm_l59 = ((uint8_t)1);
                            goto gm_lb49;
                        }
gm_lb50:;
                        {
                            uint8_t t298 = GM_LD(uint8_t, (((uint8_t *)&gm_l60) + 0));
                            uint8_t t299 = (uint8_t)(t298 == ((uint8_t)20ULL));
                            if (!t299) {
                                goto gm_lb51;
                            }
                            gm_l59 = ((uint8_t)1);
                            goto gm_lb49;
                        }
gm_lb51:;
                        {
                            gm_l59 = ((uint8_t)0);
                            goto gm_lb49;
                        }
gm_lb52:;
                        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 185, 84);
                    }
gm_lb49:;
                    uint8_t t300 = gm_l59;
                    if (!t300) {
                        goto gm_lb45;
                    }
                    gm_tmn7n0n9raou5 t301;
                    GM_ST(uint8_t, (uint8_t *)&t301 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t301 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t301 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t301 + 0), ((uint8_t)7ULL));
                    gm_tbgiuvo2j5vcs t302;
                    GM_ST(uint8_t, (uint8_t *)&t302 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t302 + 10, 0);
                    GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t302), t301);
                    ((uint8_t *)&t302)[8] = 1;
                    F.ret = t302;
                    goto gm_fret;
                }
gm_lb45:;
            }
gm_lb44:;
            gm_tmn7n0n9raou5 t303;
            GM_ST(uint8_t, (uint8_t *)&t303 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t303 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t303 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t303 + 0), ((uint8_t)6ULL));
            gm_tbgiuvo2j5vcs t304;
            GM_ST(uint8_t, (uint8_t *)&t304 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t304 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t304), t303);
            ((uint8_t *)&t304)[8] = 1;
            F.ret = t304;
            goto gm_fret;
        }
        gm_tbgiuvo2j5vcs t305;
        memset(&t305, 0, sizeof t305);
        F.ret = t305;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_coerce_sudc6c1 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* target0 */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* src */
    gm_t88rcucg1b7uk gm_l5; /* target */
    gm_t88rcucg1b7uk gm_l6; /* a */
    uint8_t gm_l7; /* c */
    gm_t88rcucg1b7uk gm_l8; /* b */
    uint8_t gm_l9; /* sc */
    uint8_t gm_l10; /* tc */
    void * gm_l12; /* c */
    gm_t88rcucg1b7uk gm_l13; /* elem */
    void * gm_l14; /* p */
    gm_b16_8 gm_l15; /* a */
    gm_b16_8 gm_l16; /* na */
    gm_b16_8 gm_l17; /* b */
    gm_b16_8 gm_l18; /* nb */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    gm_t88rcucg1b7uk gm_l22; /* found */
    gm_t88rcucg1b7uk gm_l23; /* s */
    gm_t88rcucg1b7uk gm_l24; /* t */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_tiilmfd16v3fc gm_l29; /* x */
    uint8_t gm_l31; /* subset */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_tiilmfd16v3fc gm_l34; /* x */
    gm_b16_8 gm_l35; /* steps */
    GM_STACK_CHECK(&F, 4332u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l36;
    uint8_t gm_l37;
    gm_t53at80pfdus3 gm_l38;
    gm_t53at80pfdus3 gm_l39;
    uint8_t gm_l40;
    gm_t9qkh0rsooc8d gm_l41;
    gm_t53at80pfdus3 gm_l42;
    gm_t53at80pfdus3 gm_l43;
    gm_tabb710d9mq2e gm_l44;
    void * gm_l45;
    gm_tkn42u7na3t81 gm_l46;
    void * gm_l47;
    uint8_t gm_l48;
    gm_t53at80pfdus3 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_t53at80pfdus3 gm_l51;
    gm_t53at80pfdus3 gm_l52;
    uint8_t gm_l53;
    uint8_t gm_l54;
    uint8_t gm_l55;
    gm_t9qkh0rsooc8d gm_l56;
    gm_tabb710d9mq2e gm_l57;
    uint8_t gm_l58;
    uint8_t gm_l59;
    gm_t9qkh0rsooc8d gm_l60;
    gm_tabb710d9mq2e gm_l61;
    gm_t53at80pfdus3 gm_l62;
    gm_t53at80pfdus3 gm_l63;
    gm_tabb710d9mq2e gm_l64;
    gm_tftb1odc739t5 gm_l65;
    gm_th2lqbo77pvsc gm_l66;
    gm_tabb710d9mq2e gm_l67;
    {
        void * t49 = gm_l0;
        void * t50 = GM_LD(void *, ((uint8_t *)t49));
        void * t51 = (void *)(((uint8_t *)t50) + 8);
        gm_l3 = t51;
        void * t52 = gm_l0;
        void * t53 = (void *)(((uint8_t *)t52) + 96);
        void * t54 = gm_l0;
        void * t55 = GM_LD(void *, ((uint8_t *)t54));
        void * t56 = gm_l1;
        gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t56));
        gm_t88rcucg1b7uk t58 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t53, t55, t57);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t58;
        void * t59 = gm_l0;
        void * t60 = (void *)(((uint8_t *)t59) + 96);
        void * t61 = gm_l0;
        void * t62 = GM_LD(void *, ((uint8_t *)t61));
        gm_t88rcucg1b7uk t63 = gm_l2;
        gm_t88rcucg1b7uk t64 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t60, t62, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t64;
        gm_t88rcucg1b7uk t65 = gm_l4;
        gm_t88rcucg1b7uk t66 = gm_l5;
        uint8_t t67 = (gm_eq9m6mvtmgde(((uint8_t *)&t65), ((uint8_t *)&t66)));
        if (t67) {
            gm_l37 = ((uint8_t)1);
        } else {
            void * t68 = gm_l3;
            gm_t88rcucg1b7uk t69 = gm_l4;
            uint8_t t70 = gm_f_is_error_8k27gd1(t68, GM_CONV(gm_csguankko4lmhd, t69));
            gm_l37 = t70;
        }
        uint8_t t71 = gm_l37;
        if (t71) {
            gm_l36 = ((uint8_t)1);
        } else {
            void * t72 = gm_l3;
            gm_t88rcucg1b7uk t73 = gm_l5;
            uint8_t t74 = gm_f_is_error_8k27gd1(t72, GM_CONV(gm_csguankko4lmhd, t73));
            gm_l36 = t74;
        }
        uint8_t t75 = gm_l36;
        if (t75) {
            void * t76 = gm_l1;
            F.ret = t76;
            goto gm_fret;
        }
        {
            {
                {
                    void * t77 = gm_l3;
                    gm_t88rcucg1b7uk t78 = gm_l4;
                    gm_f_kind_jcnt4hm((void *)&gm_l38, t77, GM_CONV(gm_csguankko4lmhd, t78));
                }
                uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l38) + 0));
                uint8_t t80 = (uint8_t)(t79 == ((uint8_t)11ULL));
                if (!t80) {
                    goto gm_lb2;
                }
                gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l38) + 8) + 8));
                gm_l6 = t81;
                {
                    void * t82 = gm_l3;
                    gm_t88rcucg1b7uk t83 = gm_l5;
                    gm_f_kind_jcnt4hm((void *)&gm_l39, t82, GM_CONV(gm_csguankko4lmhd, t83));
                }
                uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l39) + 0));
                uint8_t t85 = (uint8_t)(t84 == ((uint8_t)10ULL));
                if (!t85) {
                    goto gm_lb2;
                }
                uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l39) + 8));
                gm_l7 = t86;
                gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l39) + 8) + 4));
                gm_l8 = t87;
                uint8_t t88 = gm_l7;
                if (!t88) {
                    goto gm_lb2;
                }
                void * t89 = gm_l3;
                gm_t88rcucg1b7uk t90 = gm_l4;
                uint8_t t91 = gm_f_is_positional_array_6sufsk8(t89, GM_CONV(gm_csguankko4lmhd, t90));
                if (!t91) {
                    goto gm_lb2;
                }
                void * t92 = gm_l0;
                void * t93 = (void *)(((uint8_t *)t92) + 96);
                void * t94 = gm_l3;
                gm_t88rcucg1b7uk t95 = gm_l6;
                gm_t88rcucg1b7uk t96 = gm_l8;
                gm_t9qkh0rsooc8d t97 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t93, t94, t95, t96);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l41 = t97;
                {
                    {
                        uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
                        if (t98) {
                            goto gm_lb4;
                        }
                        gm_l40 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        gm_l40 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 10, 121);
                }
gm_lb3:;
                uint8_t t99 = gm_l40;
                if (!t99) {
                    goto gm_lb2;
                }
                void * t100 = gm_l0;
                void * t101 = gm_l0;
                void * t102 = gm_l1;
                gm_t88rcucg1b7uk t103 = gm_l5;
                void * t104 = gm_f_array_as_slice_gvflfg2((void *)NULL, cx, NULL, t101, t102, t103);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t88rcucg1b7uk t105 = gm_l5;
                void * t106 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t100, t104, t105);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t106;
                goto gm_fret;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        {
            {
                {
                    void * t107 = gm_l3;
                    gm_t88rcucg1b7uk t108 = gm_l4;
                    gm_f_kind_jcnt4hm((void *)&gm_l42, t107, GM_CONV(gm_csguankko4lmhd, t108));
                }
                uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 0));
                uint8_t t110 = (uint8_t)(t109 == ((uint8_t)10ULL));
                if (!t110) {
                    goto gm_lb7;
                }
                uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&gm_l42) + 8));
                gm_l9 = t111;
                {
                    void * t112 = gm_l3;
                    gm_t88rcucg1b7uk t113 = gm_l5;
                    gm_f_kind_jcnt4hm((void *)&gm_l43, t112, GM_CONV(gm_csguankko4lmhd, t113));
                }
                uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l43) + 0));
                uint8_t t115 = (uint8_t)(t114 == ((uint8_t)10ULL));
                if (!t115) {
                    goto gm_lb7;
                }
                uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l43) + 8));
                gm_l10 = t116;
                uint8_t t117 = gm_l9;
                if (!t117) {
                    goto gm_lb7;
                }
                uint8_t t118 = gm_l10;
                uint8_t t119 = (uint8_t)!t118;
                if (!t119) {
                    goto gm_lb7;
                }
                void * t120 = gm_l1;
                uint8_t t121 = gm_f_is_held_temp_d00dm1p(t120);
                if (!t121) {
                    goto gm_lb7;
                }
                {
                    void * t122 = gm_l1;
                    gm_tl874t2atnlh3 t123 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t122) + 72));
                    void * t124 = gm_l0;
                    gm_t88rcucg1b7uk t125 = gm_l5;
                    gm_b16_8 t126 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t124, t125);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t127 = gm_l0;
                    gm_t88rcucg1b7uk t128 = gm_l4;
                    gm_b16_8 t129 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t127, t128);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t130;
                    GM_ST(gm_b16_8, ((uint8_t *)&t130 + 0), t126);
                    GM_ST(gm_b16_8, ((uint8_t *)&t130 + 16), t129);
                    gm_f_format_ns6dben((void *)&gm_l44, (void *)NULL, cx, NULL, &t130);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t131 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                    if (!t131) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 8, gm_src4vq75vtnd3bq, 14, 33);
                    gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 8));
                    gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&t132));
                    gm_t4gut2c9fj1kt t134;
                    gm_f_error_s6hn871((void *)&t134, (void *)NULL, cx, NULL, t123, t133);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l11 = t134;
                }
                void * t135 = (void *)((uint8_t *)&F.l11);
                gm_b16_8 t136;
                GM_ST(void *, (uint8_t *)&t136, (void *)(gm_sn1h26ucppu4i));
                GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(109));
                gm_f_add_note_46v502s(t135, t136);
                {
                    void * t137 = gm_l0;
                    void * t138 = GM_LD(void *, ((uint8_t *)t137));
                    gm_b0_1 t139 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t138, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l11)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                void * t140 = gm_l1;
                F.ret = t140;
                goto gm_fret;
            }
gm_lb7:;
        }
gm_lb6:;
        {
            {
                void * t141 = gm_l0;
                void * t142 = gm_l1;
                gm_t88rcucg1b7uk t143 = gm_l4;
                gm_t88rcucg1b7uk t144 = gm_l5;
                void * t145 = gm_f_soa_coerce_c57qnm7((void *)NULL, cx, NULL, t141, t142, t143, t144);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l45 = t145;
                uint8_t t146 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l45)) != 0);
                if (!t146) {
                    goto gm_lb9;
                }
                void * t147 = GM_LD(void *, ((uint8_t *)&gm_l45));
                gm_l12 = t147;
                void * t148 = gm_l12;
                F.ret = t148;
                goto gm_fret;
            }
gm_lb9:;
        }
gm_lb8:;
    }
    {
        {
            {
                void * t149 = gm_l0;
                void * t150 = GM_LD(void *, ((uint8_t *)t149));
                gm_t88rcucg1b7uk t151 = gm_l5;
                gm_tkn42u7na3t81 t152 = gm_f_complex_of_kh32tib((void *)NULL, cx, NULL, t150, t151);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l46 = t152;
                uint8_t t153 = (uint8_t)((((uint8_t *)&gm_l46))[4] != 0);
                if (!t153) {
                    goto gm_lb11;
                }
                gm_t88rcucg1b7uk t154 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l46));
                gm_l13 = t154;
                {
                    {
                        void * t155 = gm_l0;
                        void * t156 = gm_l1;
                        gm_t88rcucg1b7uk t157 = gm_l5;
                        gm_t88rcucg1b7uk t158 = gm_l13;
                        void * t159 = gm_f_promote_real_constant_v0nhili((void *)NULL, cx, NULL, t155, t156, t157, t158);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l47 = t159;
                        uint8_t t160 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l47)) != 0);
                        if (!t160) {
                            goto gm_lb13;
                        }
                        void * t161 = GM_LD(void *, ((uint8_t *)&gm_l47));
                        gm_l14 = t161;
                        void * t162 = gm_l14;
                        F.ret = t162;
                        goto gm_fret;
                    }
gm_lb13:;
                }
gm_lb12:;
                goto gm_lb10;
            }
gm_lb11:;
        }
gm_lb10:;
        void * t163 = gm_l3;
        gm_t88rcucg1b7uk t164 = gm_l4;
        gm_t53at80pfdus3 t165;
        gm_f_kind_jcnt4hm((void *)&t165, t163, GM_CONV(gm_csguankko4lmhd, t164));
        uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&t165) + 0));
        uint8_t t167 = (uint8_t)((t166 == ((uint8_t)5ULL)));
        gm_t53at80pfdus3 t168;
        memset(&t168, 0, sizeof t168);
        GM_ST(uint8_t, ((uint8_t *)&t168 + 0), ((uint8_t)5ULL));
        if (t167) {
            gm_t88rcucg1b7uk t169 = gm_l5;
            void * t170 = gm_l1;
            gm_tmn7n0n9raou5 t171;
            GM_ST(uint8_t, (uint8_t *)&t171 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t171 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t171 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t171 + 0), ((uint8_t)5ULL));
            gm_t8jsc4n9vp2ib t172;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t172 + 0), t171);
            GM_ST(void *, ((uint8_t *)&t172 + 8), t170);
            void * t173 = gm_l1;
            gm_tl874t2atnlh3 t174 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t173) + 72));
            gm_to0f5qn75cgg8 t175;
            memset(&t175, 0, sizeof t175);
            GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t175 + 8), t172);
            void * t176 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t169, &t175, t174);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t176;
            goto gm_fret;
        }
        void * t177 = gm_l3;
        gm_t88rcucg1b7uk t178 = gm_l4;
        gm_f_kind_jcnt4hm((void *)&gm_l49, t177, GM_CONV(gm_csguankko4lmhd, t178));
        {
            {
                uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l49) + 0));
                uint8_t t180 = (uint8_t)(t179 == ((uint8_t)23ULL));
                if (!t180) {
                    goto gm_lb15;
                }
                gm_l48 = ((uint8_t)1);
                goto gm_lb14;
            }
gm_lb15:;
            {
                gm_l48 = ((uint8_t)0);
                goto gm_lb14;
            }
gm_lb16:;
            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 25, 8);
        }
gm_lb14:;
        uint8_t t181 = gm_l48;
        if (t181) {
            {
                void * t182 = gm_l0;
                void * t183 = (void *)(((uint8_t *)t182) + 280);
                gm_t88rcucg1b7uk t184 = gm_l4;
                gm_t88rcucg1b7uk t185 = gm_l5;
                void * t186 = gm_l1;
                gm_tl874t2atnlh3 t187 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t186) + 72));
                gm_thsh8t5u55gps t188;
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t188 + 0), t184);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t188 + 4), t185);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t188 + 8), t187);
                gm_tftb1odc739t5 t189 = gm_f_push_joldpsp(t183, &t188);
                gm_l50 = t189;
            }
            uint8_t t190 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
            if (!t190) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_src4vq75vtnd3bq, 28, 9);
            {
                gm_t88rcucg1b7uk t191 = gm_l5;
                void * t192 = gm_l1;
                gm_tmn7n0n9raou5 t193;
                GM_ST(uint8_t, (uint8_t *)&t193 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t193 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t193 + 4, 0);
                GM_ST(uint8_t, ((uint8_t *)&t193 + 0), ((uint8_t)12ULL));
                gm_t8jsc4n9vp2ib t194;
                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t194 + 0), t193);
                GM_ST(void *, ((uint8_t *)&t194 + 8), t192);
                void * t195 = gm_l1;
                gm_tl874t2atnlh3 t196 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t195) + 72));
                gm_to0f5qn75cgg8 t197;
                memset(&t197, 0, sizeof t197);
                GM_ST(uint8_t, ((uint8_t *)&t197 + 0), ((uint8_t)32ULL));
                GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t197 + 8), t194);
                void * t198 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t191, &t197, t196);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t198;
                goto gm_fret;
            }
        }
    }
    {
        {
            {
                {
                    void * t199 = gm_l3;
                    gm_t88rcucg1b7uk t200 = gm_l4;
                    gm_f_kind_jcnt4hm((void *)&gm_l51, t199, GM_CONV(gm_csguankko4lmhd, t200));
                }
                uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l51) + 0));
                uint8_t t202 = (uint8_t)(t201 == ((uint8_t)12ULL));
                if (!t202) {
                    goto gm_lb18;
                }
                gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l51) + 8));
                gm_l15 = t203;
                gm_b16_8 t204 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l51) + 8) + 16));
                gm_l16 = t204;
                {
                    void * t205 = gm_l3;
                    gm_t88rcucg1b7uk t206 = gm_l5;
                    gm_f_kind_jcnt4hm((void *)&gm_l52, t205, GM_CONV(gm_csguankko4lmhd, t206));
                }
                uint8_t t207 = GM_LD(uint8_t, (((uint8_t *)&gm_l52) + 0));
                uint8_t t208 = (uint8_t)(t207 == ((uint8_t)12ULL));
                if (!t208) {
                    goto gm_lb18;
                }
                gm_b16_8 t209 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l52) + 8));
                gm_l17 = t209;
                gm_b16_8 t210 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l52) + 8) + 16));
                gm_l18 = t210;
                {
                    {
                        void * t211 = gm_l3;
                        gm_t88rcucg1b7uk t212 = gm_l4;
                        uint8_t t213 = gm_f_has_infer_3o82a0b(t211, GM_CONV(gm_csguankko4lmhd, t212));
                        if (t213) {
                            gm_l53 = ((uint8_t)1);
                        } else {
                            void * t214 = gm_l3;
                            gm_t88rcucg1b7uk t215 = gm_l5;
                            uint8_t t216 = gm_f_has_infer_3o82a0b(t214, GM_CONV(gm_csguankko4lmhd, t215));
                            gm_l53 = t216;
                        }
                        uint8_t t217 = gm_l53;
                        if (!t217) {
                            goto gm_lb20;
                        }
                        uint64_t t218 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                        uint64_t t219 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                        uint8_t t220 = ((t218 == t219));
                        if (!t220) {
                            goto gm_lb20;
                        }
                        gm_b16_8 t221 = gm_l16;
                        gm_b16_8 t222 = gm_l18;
                        uint8_t t223 = (!gm_eqq39g9duc08(((uint8_t *)&t221), ((uint8_t *)&t222)));
                        if (!t223) {
                            goto gm_lb20;
                        }
                        uint64_t t224 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                        uint8_t t225 = ((t224 == ((uint64_t)0ULL)));
                        if (t225) {
                            gm_l54 = ((uint8_t)1);
                        } else {
                            uint64_t t226 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                            uint8_t t227 = ((t226 == ((uint64_t)0ULL)));
                            gm_l54 = t227;
                        }
                        uint8_t t228 = gm_l54;
                        if (!t228) {
                            goto gm_lb20;
                        }
                        gm_l20 = ((uint64_t)0ULL);
                        uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                        gm_l19 = t229;
                        {
                            for (;;) {
                                uint64_t t230 = gm_l20;
                                uint64_t t231 = gm_l19;
                                uint8_t t232 = (t230 < t231);
                                if (!t232) {
                                    goto gm_lb21;
                                }
                                {
                                    {
                                        {
                                            uint64_t t233 = gm_l20;
                                            gm_l21 = t233;
                                            void * t234 = gm_l0;
                                            void * t235 = (void *)(((uint8_t *)t234) + 96);
                                            void * t236 = gm_l3;
                                            void * t237 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                            uint64_t t238 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                            uint64_t t239 = gm_l21;
                                            gm_t88rcucg1b7uk t240 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t237) + (uintptr_t)t239 * 4));
                                            void * t241 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                            uint64_t t242 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                            uint64_t t243 = gm_l21;
                                            if ((uintptr_t)t243 >= (uintptr_t)(t242)) gm_trap_index((uintptr_t)t243, t242, gm_src4vq75vtnd3bq, 35, 46);
                                            gm_t88rcucg1b7uk t244 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t241) + (uintptr_t)t243 * 4));
                                            gm_t9qkh0rsooc8d t245 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t235, t236, t240, t244);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_l56 = t245;
                                            {
                                                {
                                                    uint8_t t246 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                                    if (t246) {
                                                        goto gm_lb27;
                                                    }
                                                    gm_l55 = ((uint8_t)1);
                                                    goto gm_lb26;
                                                }
gm_lb27:;
                                                {
                                                    gm_l55 = ((uint8_t)0);
                                                    goto gm_lb26;
                                                }
gm_lb28:;
                                                gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 35, 20);
                                            }
gm_lb26:;
                                            uint8_t t247 = gm_l55;
                                            if (t247) {
                                                void * t248 = gm_l0;
                                                void * t249 = (void *)(((uint8_t *)t248) + 96);
                                                void * t250 = gm_l0;
                                                void * t251 = GM_LD(void *, ((uint8_t *)t250));
                                                gm_t88rcucg1b7uk t252 = gm_l4;
                                                gm_t88rcucg1b7uk t253 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t249, t251, t252);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l22 = t253;
                                                {
                                                    void * t254 = gm_l0;
                                                    void * t255 = gm_l1;
                                                    gm_tl874t2atnlh3 t256 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t255) + 72));
                                                    void * t257 = gm_l0;
                                                    gm_t88rcucg1b7uk t258 = gm_l5;
                                                    gm_b16_8 t259 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t257, t258);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    void * t260 = gm_l0;
                                                    gm_t88rcucg1b7uk t261 = gm_l22;
                                                    gm_b16_8 t262 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t260, t261);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_t6j6smj7qoctu t263;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t263 + 0), t259);
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t263 + 16), t262);
                                                    gm_f_format_ns6dben((void *)&gm_l57, (void *)NULL, cx, NULL, &t263);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t264 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                                                    if (!t264) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 8, gm_src4vq75vtnd3bq, 37, 38);
                                                    gm_b16_8 t265 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l57) + 8));
                                                    gm_b16_8 t266 = GM_LD(gm_b16_8, ((uint8_t *)&t265));
                                                    gm_b0_1 t267 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t254, t256, t266);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
                                                void * t268 = gm_l1;
                                                F.ret = t268;
                                                goto gm_fret;
                                            }
                                            goto gm_lb24;
                                        }
gm_lb25:;
                                        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 34, 13);
                                    }
gm_lb24:;
                                }
gm_lb23:;
                                uint64_t t269 = gm_l20;
                                uint64_t r270; bool o271 = __builtin_add_overflow(t269, ((uint64_t)1ULL), &r270);
                                uint64_t t272 = r270;
                                gm_l20 = t272;
                            }
                        }
gm_lb21:;
                        gm_t88rcucg1b7uk t273 = gm_l5;
                        void * t274 = gm_l1;
                        gm_tmn7n0n9raou5 t275;
                        GM_ST(uint8_t, (uint8_t *)&t275 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t275 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t275 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t275 + 0), ((uint8_t)8ULL));
                        gm_t8jsc4n9vp2ib t276;
                        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t276 + 0), t275);
                        GM_ST(void *, ((uint8_t *)&t276 + 8), t274);
                        void * t277 = gm_l1;
                        gm_tl874t2atnlh3 t278 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t277) + 72));
                        gm_to0f5qn75cgg8 t279;
                        memset(&t279, 0, sizeof t279);
                        GM_ST(uint8_t, ((uint8_t *)&t279 + 0), ((uint8_t)32ULL));
                        GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t279 + 8), t276);
                        void * t280 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t273, &t279, t278);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t280;
                        goto gm_fret;
                    }
gm_lb20:;
                }
gm_lb19:;
                goto gm_lb17;
            }
gm_lb18:;
        }
gm_lb17:;
    }
    {
        void * t281 = gm_l3;
        gm_t88rcucg1b7uk t282 = gm_l4;
        uint8_t t283 = gm_f_has_infer_3o82a0b(t281, GM_CONV(gm_csguankko4lmhd, t282));
        if (t283) {
            gm_l58 = ((uint8_t)1);
        } else {
            void * t284 = gm_l3;
            gm_t88rcucg1b7uk t285 = gm_l5;
            uint8_t t286 = gm_f_has_infer_3o82a0b(t284, GM_CONV(gm_csguankko4lmhd, t285));
            gm_l58 = t286;
        }
        uint8_t t287 = gm_l58;
        if (t287) {
            void * t288 = gm_l0;
            void * t289 = (void *)(((uint8_t *)t288) + 96);
            void * t290 = gm_l3;
            gm_t88rcucg1b7uk t291 = gm_l4;
            gm_t88rcucg1b7uk t292 = gm_l5;
            gm_t9qkh0rsooc8d t293 = gm_f_unify_t2qjv2p((void *)NULL, cx, NULL, t289, t290, t291, t292);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l60 = t293;
            {
                {
                    uint8_t t294 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                    if (t294) {
                        goto gm_lb30;
                    }
                    gm_l59 = ((uint8_t)1);
                    goto gm_lb29;
                }
gm_lb30:;
                {
                    gm_l59 = ((uint8_t)0);
                    goto gm_lb29;
                }
gm_lb31:;
                gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 45, 12);
            }
gm_lb29:;
            uint8_t t295 = gm_l59;
            if (t295) {
                void * t296 = gm_l0;
                void * t297 = (void *)(((uint8_t *)t296) + 96);
                void * t298 = gm_l0;
                void * t299 = GM_LD(void *, ((uint8_t *)t298));
                gm_t88rcucg1b7uk t300 = gm_l4;
                gm_t88rcucg1b7uk t301 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t297, t299, t300);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l23 = t301;
                void * t302 = gm_l0;
                void * t303 = (void *)(((uint8_t *)t302) + 96);
                void * t304 = gm_l0;
                void * t305 = GM_LD(void *, ((uint8_t *)t304));
                gm_t88rcucg1b7uk t306 = gm_l5;
                gm_t88rcucg1b7uk t307 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t303, t305, t306);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t307;
                {
                    void * t308 = gm_l0;
                    void * t309 = gm_l1;
                    gm_tl874t2atnlh3 t310 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t309) + 72));
                    void * t311 = gm_l0;
                    gm_t88rcucg1b7uk t312 = gm_l24;
                    gm_b16_8 t313 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t311, t312);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t314 = gm_l0;
                    gm_t88rcucg1b7uk t315 = gm_l23;
                    gm_b16_8 t316 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t314, t315);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6j6smj7qoctu t317;
                    GM_ST(gm_b16_8, ((uint8_t *)&t317 + 0), t313);
                    GM_ST(gm_b16_8, ((uint8_t *)&t317 + 16), t316);
                    gm_f_format_ns6dben((void *)&gm_l61, (void *)NULL, cx, NULL, &t317);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t318 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                    if (!t318) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 8, gm_src4vq75vtnd3bq, 48, 30);
                    gm_b16_8 t319 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l61) + 8));
                    gm_b16_8 t320 = GM_LD(gm_b16_8, ((uint8_t *)&t319));
                    gm_b0_1 t321 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t308, t310, t320);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            void * t322 = gm_l1;
            F.ret = t322;
            goto gm_fret;
        }
    }
    {
        {
            {
                {
                    void * t323 = gm_l3;
                    gm_t88rcucg1b7uk t324 = gm_l5;
                    gm_f_kind_jcnt4hm((void *)&gm_l62, t323, GM_CONV(gm_csguankko4lmhd, t324));
                }
                uint8_t t325 = GM_LD(uint8_t, (((uint8_t *)&gm_l62) + 0));
                uint8_t t326 = (uint8_t)(t325 == ((uint8_t)15ULL));
                if (!t326) {
                    goto gm_lb33;
                }
                {
                    memmove(((uint8_t *)&F.l25), (((uint8_t *)&gm_l62) + 8), 48);
                }
                {
                    void * t327 = gm_l3;
                    gm_t88rcucg1b7uk t328 = gm_l4;
                    gm_f_kind_jcnt4hm((void *)&gm_l63, t327, GM_CONV(gm_csguankko4lmhd, t328));
                }
                {
                    {
                        uint8_t t329 = GM_LD(uint8_t, (((uint8_t *)&gm_l63) + 0));
                        uint8_t t330 = (uint8_t)(t329 == ((uint8_t)16ULL));
                        if (!t330) {
                            goto gm_lb35;
                        }
                        memmove(((uint8_t *)&F.l26), (((uint8_t *)&gm_l63) + 8), 48);
                        void * t331 = (void *)((uint8_t *)&F.l26);
                        void * t332 = (void *)((uint8_t *)&F.l25);
                        uint8_t t333 = gm_f_same_shape_m0o6aok(t331, t332);
                        if (t333) {
                            uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 40));
                            if (t334) {
                                void * t335 = gm_l0;
                                void * t336 = gm_l1;
                                gm_tl874t2atnlh3 t337 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t336) + 72));
                                void * t338 = gm_l0;
                                gm_t88rcucg1b7uk t339 = gm_l4;
                                gm_b16_8 t340 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t338, t339);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tnpifv20irnph t341;
                                GM_ST(gm_b16_8, ((uint8_t *)&t341 + 0), t340);
                                gm_f_format_dr0nbiu((void *)&gm_l64, (void *)NULL, cx, NULL, t341);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t342 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                                if (!t342) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 8, gm_src4vq75vtnd3bq, 59, 38);
                                gm_b16_8 t343 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l64) + 8));
                                gm_b16_8 t344 = GM_LD(gm_b16_8, ((uint8_t *)&t343));
                                gm_b0_1 t345 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t335, t337, t344);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t346 = gm_l1;
                                F.ret = t346;
                                goto gm_fret;
                            }
                            gm_b16_8 t347 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 24));
                            gm_l27 = t347;
                            gm_l28 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t348 = gm_l28;
                                    uint64_t t349 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                                    uint8_t t350 = (t348 < t349);
                                    if (!t350) {
                                        goto gm_lb36;
                                    }
                                    {
                                        void * t351 = GM_LD(void *, ((uint8_t *)&gm_l27));
                                        uint64_t t352 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                                        uint64_t t353 = gm_l28;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l29), (((uint8_t *)t351) + (uintptr_t)t353 * 24), 24);
                                                void * t354 = (void *)(((uint8_t *)&F.l25) + 24);
                                                void * t355 = (void *)((uint8_t *)&gm_l29);
                                                uint8_t t356 = gm_f_set_contains_8v9ep9m(t354, t355);
                                                uint8_t t357 = (uint8_t)!t356;
                                                if (t357) {
                                                    {
                                                        void * t358 = gm_l0;
                                                        void * t359 = (void *)(((uint8_t *)t358) + 192);
                                                        void * t360 = gm_l1;
                                                        gm_tl874t2atnlh3 t361 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t360) + 72));
                                                        gm_tiilmfd16v3fc t362 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l29));
                                                        gm_tj44oivvogvtl t363;
                                                        GM_ST(uint32_t, (uint8_t *)&t363 + 36, 0);
                                                        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t363 + 0), t362);
                                                        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t363 + 24), t361);
                                                        gm_tftb1odc739t5 t364 = gm_f_push_3onuaru(t359, &t363);
                                                        gm_l65 = t364;
                                                    }
                                                    uint8_t t365 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                                                    if (!t365) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 1, gm_src4vq75vtnd3bq, 63, 59);
                                                }
                                                goto gm_lb39;
                                            }
gm_lb40:;
                                            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 62, 17);
                                        }
gm_lb39:;
                                    }
gm_lb38:;
                                    uint64_t t366 = gm_l28;
                                    uint64_t r367; bool o368 = __builtin_add_overflow(t366, ((uint64_t)1ULL), &r367);
                                    uint64_t t369 = r367;
                                    gm_l28 = t369;
                                }
                            }
gm_lb36:;
                            gm_t88rcucg1b7uk t370 = gm_l5;
                            void * t371 = gm_l1;
                            gm_tmn7n0n9raou5 t372;
                            GM_ST(uint8_t, (uint8_t *)&t372 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t372 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t372 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t372 + 0), ((uint8_t)9ULL));
                            gm_t8jsc4n9vp2ib t373;
                            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t373 + 0), t372);
                            GM_ST(void *, ((uint8_t *)&t373 + 8), t371);
                            void * t374 = gm_l1;
                            gm_tl874t2atnlh3 t375 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t374) + 72));
                            gm_to0f5qn75cgg8 t376;
                            memset(&t376, 0, sizeof t376);
                            GM_ST(uint8_t, ((uint8_t *)&t376 + 0), ((uint8_t)32ULL));
                            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t376 + 8), t373);
                            void * t377 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t370, &t376, t375);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t377;
                            goto gm_fret;
                        }
                        goto gm_lb34;
                    }
gm_lb35:;
                    {
                        uint8_t t378 = GM_LD(uint8_t, (((uint8_t *)&gm_l63) + 0));
                        uint8_t t379 = (uint8_t)(t378 == ((uint8_t)15ULL));
                        if (!t379) {
                            goto gm_lb41;
                        }
                        memmove(((uint8_t *)&F.l30), (((uint8_t *)&gm_l63) + 8), 48);
                        void * t380 = (void *)((uint8_t *)&F.l30);
                        void * t381 = (void *)((uint8_t *)&F.l25);
                        uint8_t t382 = gm_f_same_shape_m0o6aok(t380, t381);
                        if (t382) {
                            gm_l31 = ((uint8_t)1);
                            gm_b16_8 t383 = GM_LD(gm_b16_8, (((uint8_t *)&F.l30) + 24));
                            gm_l32 = t383;
                            gm_l33 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t384 = gm_l33;
                                    uint64_t t385 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                    uint8_t t386 = (t384 < t385);
                                    if (!t386) {
                                        goto gm_lb42;
                                    }
                                    {
                                        void * t387 = GM_LD(void *, ((uint8_t *)&gm_l32));
                                        uint64_t t388 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                        uint64_t t389 = gm_l33;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l34), (((uint8_t *)t387) + (uintptr_t)t389 * 24), 24);
                                                void * t390 = (void *)(((uint8_t *)&F.l25) + 24);
                                                void * t391 = (void *)((uint8_t *)&gm_l34);
                                                uint8_t t392 = gm_f_set_contains_8v9ep9m(t390, t391);
                                                uint8_t t393 = (uint8_t)!t392;
                                                if (t393) {
                                                    gm_l31 = ((uint8_t)0);
                                                }
                                                goto gm_lb45;
                                            }
gm_lb46:;
                                            gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 69, 17);
                                        }
gm_lb45:;
                                    }
gm_lb44:;
                                    uint64_t t394 = gm_l33;
                                    uint64_t r395; bool o396 = __builtin_add_overflow(t394, ((uint64_t)1ULL), &r395);
                                    uint64_t t397 = r395;
                                    gm_l33 = t397;
                                }
                            }
gm_lb42:;
                            uint8_t t398 = gm_l31;
                            if (t398) {
                                gm_t88rcucg1b7uk t399 = gm_l5;
                                void * t400 = gm_l1;
                                gm_tmn7n0n9raou5 t401;
                                GM_ST(uint8_t, (uint8_t *)&t401 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t401 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t401 + 4, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t401 + 0), ((uint8_t)9ULL));
                                gm_t8jsc4n9vp2ib t402;
                                GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t402 + 0), t401);
                                GM_ST(void *, ((uint8_t *)&t402 + 8), t400);
                                void * t403 = gm_l1;
                                gm_tl874t2atnlh3 t404 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t403) + 72));
                                gm_to0f5qn75cgg8 t405;
                                memset(&t405, 0, sizeof t405);
                                GM_ST(uint8_t, ((uint8_t *)&t405 + 0), ((uint8_t)32ULL));
                                GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t405 + 8), t402);
                                void * t406 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t399, &t405, t404);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t406;
                                goto gm_fret;
                            }
                        }
                        goto gm_lb34;
                    }
gm_lb41:;
                    {
                        goto gm_lb34;
                    }
gm_lb47:;
                    gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 56, 9);
                }
gm_lb34:;
                goto gm_lb32;
            }
gm_lb33:;
        }
gm_lb32:;
    }
    {
        void * t407 = gm_l0;
        gm_t88rcucg1b7uk t408 = gm_l4;
        gm_t88rcucg1b7uk t409 = gm_l5;
        uint8_t t410 = gm_f_embeds_2jpqm45(t407, GM_CONV(gm_csguankko4lmhd, t408), GM_CONV(gm_csguankko4lmhd, t409));
        if (t410) {
            gm_t88rcucg1b7uk t411 = gm_l5;
            void * t412 = gm_l1;
            gm_tmn7n0n9raou5 t413;
            GM_ST(uint8_t, (uint8_t *)&t413 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t413 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t413 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t413 + 0), ((uint8_t)6ULL));
            gm_t8jsc4n9vp2ib t414;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t414 + 0), t413);
            GM_ST(void *, ((uint8_t *)&t414 + 8), t412);
            void * t415 = gm_l1;
            gm_tl874t2atnlh3 t416 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t415) + 72));
            gm_to0f5qn75cgg8 t417;
            memset(&t417, 0, sizeof t417);
            GM_ST(uint8_t, ((uint8_t *)&t417 + 0), ((uint8_t)32ULL));
            GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t417 + 8), t414);
            void * t418 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t411, &t417, t416);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t418;
            goto gm_fret;
        }
        void * t419 = gm_l0;
        gm_t88rcucg1b7uk t420 = gm_l4;
        gm_t88rcucg1b7uk t421 = gm_l5;
        gm_f_coercion_steps_ibqvhta((void *)&gm_l66, (void *)NULL, cx, NULL, t419, t420, t421);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t422 = (uint8_t)((((uint8_t *)&gm_l66))[16] != 0);
        if (t422) {
            gm_b16_8 t423 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l66));
            gm_l35 = t423;
        } else {
            {
                void * t424 = gm_l0;
                void * t425 = gm_l1;
                gm_tl874t2atnlh3 t426 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t425) + 72));
                void * t427 = gm_l0;
                gm_t88rcucg1b7uk t428 = gm_l5;
                gm_b16_8 t429 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t427, t428);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t430 = gm_l0;
                gm_t88rcucg1b7uk t431 = gm_l4;
                gm_b16_8 t432 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t430, t431);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t6j6smj7qoctu t433;
                GM_ST(gm_b16_8, ((uint8_t *)&t433 + 0), t429);
                GM_ST(gm_b16_8, ((uint8_t *)&t433 + 16), t432);
                gm_f_format_ns6dben((void *)&gm_l67, (void *)NULL, cx, NULL, &t433);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t434 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                if (!t434) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 8, gm_src4vq75vtnd3bq, 81, 26);
                gm_b16_8 t435 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l67) + 8));
                gm_b16_8 t436 = GM_LD(gm_b16_8, ((uint8_t *)&t435));
                gm_b0_1 t437 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t424, t426, t436);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t438 = gm_l1;
            F.ret = t438;
            goto gm_fret;
        }
        void * t439 = gm_l1;
        gm_b16_8 t440 = gm_l35;
        gm_b16_8 t441 = GM_LD(gm_b16_8, ((uint8_t *)&t440));
        void * t442 = gm_f_apply_steps_fd8cr97((void *)NULL, cx, NULL, t439, t441);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t442;
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
extern __attribute__((aligned(4))) void gm_f_coercion_steps_ibqvhta(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_pointer_coercion_a03b7p1(void * a0, gm_csguankko4lmhd a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pointer_coercion_a03b7p1 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    void * gm_l3; /* tys */
    uint8_t gm_l4; /* sc */
    gm_t88rcucg1b7uk gm_l5; /* se */
    uint8_t gm_l6; /* sk */
    uint8_t gm_l7; /* c */
    gm_t88rcucg1b7uk gm_l8; /* p */
    uint8_t gm_l9; /* c */
    gm_t88rcucg1b7uk gm_l10; /* p */
    uint8_t gm_l11; /* c */
    gm_t88rcucg1b7uk gm_l12; /* p */
    uint8_t gm_l13; /* tc */
    gm_t88rcucg1b7uk gm_l14; /* te */
    uint8_t gm_l15; /* tk */
    uint8_t gm_l16; /* c */
    gm_t88rcucg1b7uk gm_l17; /* p */
    uint8_t gm_l18; /* c */
    gm_t88rcucg1b7uk gm_l19; /* p */
    uint8_t gm_l20; /* c */
    gm_t88rcucg1b7uk gm_l21; /* p */
    gm_tkn42u7na3t81 gm_l22; /* array_elem */
    gm_t88rcucg1b7uk gm_l23; /* elem */
    gm_t88rcucg1b7uk gm_l24; /* elem */
    GM_STACK_CHECK_SHALLOW(&F, 645u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    gm_t53at80pfdus3 gm_l25;
    gm_t53at80pfdus3 gm_l26;
    gm_t53at80pfdus3 gm_l27;
    uint8_t gm_l28;
    void * t22 = gm_l0;
    void * t23 = (void *)(((uint8_t *)t22) + 8);
    gm_l3 = t23;
    gm_l4 = ((uint8_t)0);
    gm_t88rcucg1b7uk t24 = gm_l1;
    gm_l5 = t24;
    gm_l6 = ((uint8_t)0ULL);
    {
        void * t25 = gm_l3;
        gm_t88rcucg1b7uk t26 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&gm_l25, t25, GM_CONV(gm_csguankko4lmhd, t26));
    }
    {
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)8ULL));
            if (!t28) {
                goto gm_lb2;
            }
            uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 8));
            gm_l7 = t29;
            gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l25) + 8) + 4));
            gm_l8 = t30;
            uint8_t t31 = gm_l7;
            gm_l4 = t31;
            gm_t88rcucg1b7uk t32 = gm_l8;
            gm_l5 = t32;
            gm_l6 = ((uint8_t)0ULL);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t34 = (uint8_t)(t33 == ((uint8_t)9ULL));
            if (!t34) {
                goto gm_lb3;
            }
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 8));
            gm_l9 = t35;
            gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l25) + 8) + 4));
            gm_l10 = t36;
            uint8_t t37 = gm_l9;
            gm_l4 = t37;
            gm_t88rcucg1b7uk t38 = gm_l10;
            gm_l5 = t38;
            gm_l6 = ((uint8_t)1ULL);
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 0));
            uint8_t t40 = (uint8_t)(t39 == ((uint8_t)10ULL));
            if (!t40) {
                goto gm_lb4;
            }
            uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 8));
            gm_l11 = t41;
            gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l25) + 8) + 4));
            gm_l12 = t42;
            uint8_t t43 = gm_l11;
            gm_l4 = t43;
            gm_t88rcucg1b7uk t44 = gm_l12;
            gm_l5 = t44;
            gm_l6 = ((uint8_t)2ULL);
            goto gm_lb1;
        }
gm_lb4:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 215, 5);
    }
gm_lb1:;
    {
        void * t45 = gm_l3;
        gm_t88rcucg1b7uk t46 = gm_l2;
        gm_t53at80pfdus3 t47;
        gm_f_kind_jcnt4hm((void *)&t47, t45, GM_CONV(gm_csguankko4lmhd, t46));
        uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)&t47) + 0));
        uint8_t t49 = (uint8_t)((t48 == ((uint8_t)7ULL)));
        gm_t53at80pfdus3 t50;
        memset(&t50, 0, sizeof t50);
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)7ULL));
        if (t49) {
            uint8_t t51 = gm_l6;
            uint8_t t52 = (!(t51 == ((uint8_t)2ULL)));
            F.ret = t52;
            goto gm_fret;
        }
    }
    gm_l13 = ((uint8_t)0);
    gm_t88rcucg1b7uk t53 = gm_l2;
    gm_l14 = t53;
    gm_l15 = ((uint8_t)0ULL);
    {
        void * t54 = gm_l3;
        gm_t88rcucg1b7uk t55 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&gm_l26, t54, GM_CONV(gm_csguankko4lmhd, t55));
    }
    {
        {
            uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
            uint8_t t57 = (uint8_t)(t56 == ((uint8_t)8ULL));
            if (!t57) {
                goto gm_lb7;
            }
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 8));
            gm_l16 = t58;
            gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l26) + 8) + 4));
            gm_l17 = t59;
            uint8_t t60 = gm_l16;
            gm_l13 = t60;
            gm_t88rcucg1b7uk t61 = gm_l17;
            gm_l14 = t61;
            gm_l15 = ((uint8_t)0ULL);
            goto gm_lb6;
        }
gm_lb7:;
        {
            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)9ULL));
            if (!t63) {
                goto gm_lb8;
            }
            uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 8));
            gm_l18 = t64;
            gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l26) + 8) + 4));
            gm_l19 = t65;
            uint8_t t66 = gm_l18;
            gm_l13 = t66;
            gm_t88rcucg1b7uk t67 = gm_l19;
            gm_l14 = t67;
            gm_l15 = ((uint8_t)1ULL);
            goto gm_lb6;
        }
gm_lb8:;
        {
            uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 0));
            uint8_t t69 = (uint8_t)(t68 == ((uint8_t)10ULL));
            if (!t69) {
                goto gm_lb9;
            }
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&gm_l26) + 8));
            gm_l20 = t70;
            gm_t88rcucg1b7uk t71 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l26) + 8) + 4));
            gm_l21 = t71;
            uint8_t t72 = gm_l20;
            gm_l13 = t72;
            gm_t88rcucg1b7uk t73 = gm_l21;
            gm_l14 = t73;
            gm_l15 = ((uint8_t)2ULL);
            goto gm_lb6;
        }
gm_lb9:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 237, 5);
    }
gm_lb6:;
    {
        {
            uint8_t t74 = gm_l4;
            if (!t74) {
                goto gm_lb12;
            }
            uint8_t t75 = gm_l13;
            uint8_t t76 = (uint8_t)!t75;
            if (!t76) {
                goto gm_lb12;
            }
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb12:;
    }
gm_lb11:;
    uint8_t t77 = gm_l6;
    uint8_t t78 = gm_l15;
    uint8_t t79 = ((t77 == t78));
    if (t79) {
        gm_t88rcucg1b7uk t80 = gm_l5;
        gm_t88rcucg1b7uk t81 = gm_l14;
        uint8_t t82 = (gm_eq9m6mvtmgde(((uint8_t *)&t80), ((uint8_t *)&t81)));
        F.ret = t82;
        goto gm_fret;
    }
    gm_tkn42u7na3t81 t83;
    memset(&t83, 0, sizeof t83);
    gm_l22 = t83;
    {
        void * t84 = gm_l3;
        gm_t88rcucg1b7uk t85 = gm_l5;
        gm_f_kind_jcnt4hm((void *)&gm_l27, t84, GM_CONV(gm_csguankko4lmhd, t85));
    }
    {
        {
            uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
            uint8_t t87 = (uint8_t)(t86 == ((uint8_t)11ULL));
            if (!t87) {
                goto gm_lb14;
            }
            gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l27) + 8) + 8));
            gm_l23 = t88;
            void * t89 = gm_l3;
            gm_t88rcucg1b7uk t90 = gm_l5;
            uint8_t t91 = gm_f_src_array_ok_og10umr(t89, GM_CONV(gm_csguankko4lmhd, t90));
            if (t91) {
                gm_t88rcucg1b7uk t92 = gm_l23;
                gm_tkn42u7na3t81 t93;
                GM_ST(uint8_t, (uint8_t *)&t93 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t93 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t93), t92);
                ((uint8_t *)&t93)[4] = 1;
                gm_l22 = t93;
            }
            goto gm_lb13;
        }
gm_lb14:;
        {
            uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l27) + 0));
            uint8_t t95 = (uint8_t)(t94 == ((uint8_t)20ULL));
            if (!t95) {
                goto gm_lb15;
            }
            gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l27) + 8) + 4));
            gm_l24 = t96;
            gm_t88rcucg1b7uk t97 = gm_l24;
            gm_tkn42u7na3t81 t98;
            GM_ST(uint8_t, (uint8_t *)&t98 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t98 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t98), t97);
            ((uint8_t *)&t98)[4] = 1;
            gm_l22 = t98;
            goto gm_lb13;
        }
gm_lb15:;
        {
            goto gm_lb13;
        }
gm_lb16:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 259, 5);
    }
gm_lb13:;
    {
        {
            uint8_t t99 = gm_l6;
            uint8_t t100 = ((t99 == ((uint8_t)0ULL)));
            if (!t100) {
                goto gm_lb18;
            }
            uint8_t t101 = gm_l15;
            uint8_t t102 = ((t101 == ((uint8_t)1ULL)));
            if (!t102) {
                goto gm_lb18;
            }
            gm_t88rcucg1b7uk t103 = gm_l5;
            gm_t88rcucg1b7uk t104 = gm_l14;
            uint8_t t105 = (gm_eq9m6mvtmgde(((uint8_t *)&t103), ((uint8_t *)&t104)));
            if (t105) {
                gm_l28 = ((uint8_t)1);
            } else {
                gm_tkn42u7na3t81 t106 = gm_l22;
                gm_t88rcucg1b7uk t107 = gm_l14;
                gm_tkn42u7na3t81 t108;
                GM_ST(uint8_t, (uint8_t *)&t108 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t108 + 6, 0);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t108), t107);
                ((uint8_t *)&t108)[4] = 1;
                uint8_t t109 = (gm_eqs65r2l0loh(((uint8_t *)&t106), ((uint8_t *)&t108)));
                gm_l28 = t109;
            }
            uint8_t t110 = gm_l28;
            F.ret = t110;
            goto gm_fret;
        }
gm_lb18:;
    }
gm_lb17:;
    {
        {
            uint8_t t111 = gm_l6;
            uint8_t t112 = ((t111 == ((uint8_t)0ULL)));
            if (!t112) {
                goto gm_lb20;
            }
            uint8_t t113 = gm_l15;
            uint8_t t114 = ((t113 == ((uint8_t)2ULL)));
            if (!t114) {
                goto gm_lb20;
            }
            gm_tkn42u7na3t81 t115 = gm_l22;
            gm_t88rcucg1b7uk t116 = gm_l14;
            gm_tkn42u7na3t81 t117;
            GM_ST(uint8_t, (uint8_t *)&t117 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t117 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t117), t116);
            ((uint8_t *)&t117)[4] = 1;
            uint8_t t118 = (gm_eqs65r2l0loh(((uint8_t *)&t115), ((uint8_t *)&t117)));
            F.ret = t118;
            goto gm_fret;
        }
gm_lb20:;
    }
gm_lb19:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_same_shape_m0o6aok(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_shape_m0o6aok F;
    void * gm_l0; /* a */
    void * gm_l1; /* b */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 232u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l5;
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    void * t9 = gm_l1;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)t9) + 8));
    uint8_t t11 = (!(t8 == t10));
    if (t11) {
        gm_l5 = ((uint8_t)1);
    } else {
        void * t12 = gm_l0;
        gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t12) + 16));
        void * t14 = gm_l1;
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t14) + 16));
        uint8_t t16 = (!gm_eq9m6mvtmgde(((uint8_t *)&t13), ((uint8_t *)&t15)));
        gm_l5 = t16;
    }
    uint8_t t17 = gm_l5;
    if (t17) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    gm_l3 = ((uint64_t)0ULL);
    void * t18 = gm_l0;
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 8));
    gm_l2 = t19;
    {
        for (;;) {
            uint64_t t20 = gm_l3;
            uint64_t t21 = gm_l2;
            uint8_t t22 = (t20 < t21);
            if (!t22) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t23 = gm_l3;
                        gm_l4 = t23;
                        void * t24 = gm_l0;
                        void * t25 = GM_LD(void *, ((uint8_t *)t24));
                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t24) + 8));
                        uint64_t t27 = gm_l4;
                        if ((uintptr_t)t27 >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)t27, t26, gm_src4vq75vtnd3bq, 280, 12);
                        gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t25) + (uintptr_t)t27 * 4));
                        void * t29 = gm_l1;
                        void * t30 = GM_LD(void *, ((uint8_t *)t29));
                        uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t29) + 8));
                        uint64_t t32 = gm_l4;
                        if ((uintptr_t)t32 >= (uintptr_t)(t31)) gm_trap_index((uintptr_t)t32, t31, gm_src4vq75vtnd3bq, 280, 31);
                        gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t30) + (uintptr_t)t32 * 4));
                        uint8_t t34 = (!gm_eq9m6mvtmgde(((uint8_t *)&t28), ((uint8_t *)&t33)));
                        if (t34) {
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 279, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t35 = gm_l3;
            uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
            uint64_t t38 = r36;
            gm_l3 = t38;
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
inline __attribute__((aligned(4))) uint8_t gm_f_src_array_ok_og10umr(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_src_array_ok_og10umr F;
    void * gm_l0; /* tys */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_tnnjeerolos0a gm_l2; /* kind */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l3;
    uint8_t gm_l4;
    {
        {
            void * t7 = gm_l0;
            gm_t88rcucg1b7uk t8 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l3, t7, GM_CONV(gm_csguankko4lmhd, t8));
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)11ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_tnnjeerolos0a t11 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&gm_l3) + 8) + 12));
            gm_l2 = t11;
            {
                {
                    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t13 = (uint8_t)(t12 == ((uint8_t)1ULL));
                    if (!t13) {
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
                gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 273, 12);
            }
gm_lb3:;
            uint8_t t14 = gm_l4;
            F.ret = t14;
            goto gm_fret;
        }
gm_lb2:;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_steps_for_768gb3b(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3) {
    struct gm_fr_steps_for_768gb3b F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    uint8_t gm_l3; /* lift */
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_6canco6((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t2;
    }
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_t88rcucg1b7uk t5 = gm_l2;
    uint8_t t6 = gm_l3;
    void * t7 = (void *)((uint8_t *)&F.l4);
    uint8_t t8 = gm_f_steps_into_vts72l5((void *)NULL, cx, NULL, t3, t4, t5, t6, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (t8) {
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t9);
        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
        goto gm_fdone;
    }
    {
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
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
__attribute__((aligned(4))) uint8_t gm_f_steps_into_vts72l5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2, uint8_t a3, void * a4) {
    struct gm_fr_steps_into_vts72l5 F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* src */
    gm_t88rcucg1b7uk gm_l2; /* target */
    uint8_t gm_l3; /* lift */
    void * gm_l4; /* out */
    gm_tmn7n0n9raou5 gm_l5; /* k */
    void * gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* e */
    gm_t88rcucg1b7uk gm_l8; /* o1 */
    gm_t88rcucg1b7uk gm_l9; /* f */
    gm_t88rcucg1b7uk gm_l10; /* o2 */
    uint32_t gm_l11; /* i */
    gm_t88rcucg1b7uk gm_l12; /* inner */
    gm_t88rcucg1b7uk gm_l13; /* ok */
    GM_STACK_CHECK(&F, 659u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tbgiuvo2j5vcs gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_t53at80pfdus3 gm_l16;
    gm_t53at80pfdus3 gm_l17;
    gm_tvbp5g7fh5fh0 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_t53at80pfdus3 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_t88rcucg1b7uk t12 = gm_l1;
    gm_t88rcucg1b7uk t13 = gm_l2;
    uint8_t t14 = (gm_eq9m6mvtmgde(((uint8_t *)&t12), ((uint8_t *)&t13)));
    if (t14) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    {
        {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, ((uint8_t *)t15));
            gm_t88rcucg1b7uk t17 = gm_l1;
            gm_t88rcucg1b7uk t18 = gm_l2;
            gm_tbgiuvo2j5vcs t19 = gm_f_base_coercion_t3tgsf0((void *)NULL, cx, NULL, t16, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l14 = t19;
            uint8_t t20 = (uint8_t)((((uint8_t *)&gm_l14))[8] != 0);
            if (!t20) {
                goto gm_lb2;
            }
            gm_tmn7n0n9raou5 t21 = GM_LD(gm_tmn7n0n9raou5, ((uint8_t *)&gm_l14));
            gm_l5 = t21;
            void * t22 = gm_l4;
            gm_tmn7n0n9raou5 t23 = gm_l5;
            gm_t88rcucg1b7uk t24 = gm_l2;
            gm_tm7p68clol9c3 t25;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t25 + 0), t23);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t25 + 8), t24);
            gm_tftb1odc739t5 t26 = gm_f_push_f21bjke(t22, t25);
            gm_l15 = t26;
            uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
            if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_src4vq75vtnd3bq, 108, 9);
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t28 = gm_l0;
    void * t29 = GM_LD(void *, ((uint8_t *)t28));
    void * t30 = (void *)(((uint8_t *)t29) + 8);
    gm_l6 = t30;
    {
        {
            uint8_t t31 = gm_l3;
            if (!t31) {
                goto gm_lb4;
            }
            {
                void * t32 = gm_l6;
                gm_t88rcucg1b7uk t33 = gm_l1;
                gm_f_kind_jcnt4hm((void *)&gm_l16, t32, GM_CONV(gm_csguankko4lmhd, t33));
            }
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l16) + 0));
            uint8_t t35 = (uint8_t)(t34 == ((uint8_t)14ULL));
            if (!t35) {
                goto gm_lb4;
            }
            gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l16) + 8));
            gm_l7 = t36;
            gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l16) + 8) + 4));
            gm_l8 = t37;
            {
                void * t38 = gm_l6;
                gm_t88rcucg1b7uk t39 = gm_l2;
                gm_f_kind_jcnt4hm((void *)&gm_l17, t38, GM_CONV(gm_csguankko4lmhd, t39));
            }
            uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l17) + 0));
            uint8_t t41 = (uint8_t)(t40 == ((uint8_t)14ULL));
            if (!t41) {
                goto gm_lb4;
            }
            gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 8));
            gm_l9 = t42;
            gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l17) + 8) + 4));
            gm_l10 = t43;
            gm_t88rcucg1b7uk t44 = gm_l8;
            gm_t88rcucg1b7uk t45 = gm_l10;
            uint8_t t46 = (gm_eq9m6mvtmgde(((uint8_t *)&t44), ((uint8_t *)&t45)));
            if (!t46) {
                goto gm_lb4;
            }
            {
                {
                    void * t47 = gm_l0;
                    gm_t88rcucg1b7uk t48 = gm_l7;
                    gm_t88rcucg1b7uk t49 = gm_l9;
                    gm_f_lift_variant_ptko7np((void *)&gm_l18, (void *)NULL, cx, NULL, t47, t48, t49);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&gm_l18) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)0ULL));
                    if (!t51) {
                        goto gm_lb6;
                    }
                    uint32_t t52 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 8));
                    gm_l11 = t52;
                    void * t53 = gm_l4;
                    uint32_t t54 = gm_l11;
                    gm_t88rcucg1b7uk t55 = gm_l2;
                    gm_tmn7n0n9raou5 t56;
                    GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)4ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t56 + 4), t54);
                    gm_tm7p68clol9c3 t57;
                    GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t57 + 0), t56);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t57 + 8), t55);
                    gm_tftb1odc739t5 t58 = gm_f_push_f21bjke(t53, t57);
                    gm_l19 = t58;
                    uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                    if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_src4vq75vtnd3bq, 115, 13);
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
gm_lb6:;
            }
gm_lb5:;
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t60 = gm_l6;
    gm_t88rcucg1b7uk t61 = gm_l2;
    gm_f_kind_jcnt4hm((void *)&gm_l20, t60, GM_CONV(gm_csguankko4lmhd, t61));
    {
        {
            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
            uint8_t t63 = (uint8_t)(t62 == ((uint8_t)13ULL));
            if (!t63) {
                goto gm_lb8;
            }
            gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l20) + 8));
            gm_l12 = t64;
            void * t65 = gm_l0;
            gm_t88rcucg1b7uk t66 = gm_l1;
            gm_t88rcucg1b7uk t67 = gm_l12;
            uint8_t t68 = gm_l3;
            void * t69 = gm_l4;
            uint8_t t70 = gm_f_steps_into_vts72l5((void *)NULL, cx, NULL, t65, t66, t67, t68, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t71 = (uint8_t)!t70;
            if (t71) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t72 = gm_l4;
            gm_t88rcucg1b7uk t73 = gm_l2;
            gm_tmn7n0n9raou5 t74;
            GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)2ULL));
            gm_tm7p68clol9c3 t75;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t75 + 0), t74);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t75 + 8), t73);
            gm_tftb1odc739t5 t76 = gm_f_push_f21bjke(t72, t75);
            gm_l21 = t76;
            uint8_t t77 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
            if (!t77) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_src4vq75vtnd3bq, 122, 13);
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)&gm_l20) + 0));
            uint8_t t79 = (uint8_t)(t78 == ((uint8_t)14ULL));
            if (!t79) {
                goto gm_lb9;
            }
            gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l20) + 8) + 4));
            gm_l13 = t80;
            void * t81 = gm_l0;
            gm_t88rcucg1b7uk t82 = gm_l1;
            gm_t88rcucg1b7uk t83 = gm_l13;
            uint8_t t84 = gm_l3;
            void * t85 = gm_l4;
            uint8_t t86 = gm_f_steps_into_vts72l5((void *)NULL, cx, NULL, t81, t82, t83, t84, t85);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t87 = (uint8_t)!t86;
            if (t87) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t88 = gm_l4;
            gm_t88rcucg1b7uk t89 = gm_l2;
            gm_tmn7n0n9raou5 t90;
            GM_ST(uint8_t, (uint8_t *)&t90 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t90 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t90 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)3ULL));
            gm_tm7p68clol9c3 t91;
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t91 + 0), t90);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t91 + 8), t89);
            gm_tftb1odc739t5 t92 = gm_f_push_f21bjke(t88, t91);
            gm_l22 = t92;
            uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
            if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_src4vq75vtnd3bq, 127, 13);
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb9:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb10:;
        gm_trap("no match arm matched the scrutinee", gm_src4vq75vtnd3bq, 119, 5);
    }
gm_lb7:;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}

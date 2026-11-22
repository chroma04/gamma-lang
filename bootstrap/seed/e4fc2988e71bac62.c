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
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct gm_csv4kcdnuj265c { uint32_t f0; uint32_t f1; gm_csguankko4lmhd f2; } gm_csv4kcdnuj265c;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; int32_t f1; uint8_t pe[4]; } gm_t0bf4lga76ktk;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t p3[6]; gm_b16_8 f3; void * f4; void * f5; gm_tl874t2atnlh3 f6; uint8_t pe[4]; } gm_t24301fas1b2a;
typedef struct { uint64_t f0; gm_b16_8 f1; uint16_t f2; uint8_t pe[6]; } gm_t3568dh2osse0;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_t5g5vgung6h6s;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_tf5srh4mp4b3i f0; uint8_t f1; uint8_t pe[7]; } gm_th3eueiipivq6;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { uint32_t f0; uint32_t f1; gm_t88rcucg1b7uk f2; } gm_tov4mtrfp5e7v;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_csv4kcdnuj265c) == 12 && _Alignof(gm_csv4kcdnuj265c) == 4 && offsetof(gm_csv4kcdnuj265c, f0) == 0 && offsetof(gm_csv4kcdnuj265c, f1) == 4 && offsetof(gm_csv4kcdnuj265c, f2) == 8, "layout of `BitField`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0bf4lga76ktk) == 24 && _Alignof(gm_t0bf4lga76ktk) == 8 && offsetof(gm_t0bf4lga76ktk, f0) == 0 && offsetof(gm_t0bf4lga76ktk, f1) == 16, "layout of `(string, i32)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t24301fas1b2a) == 72 && _Alignof(gm_t24301fas1b2a) == 8 && offsetof(gm_t24301fas1b2a, f0) == 0 && offsetof(gm_t24301fas1b2a, f1) == 16 && offsetof(gm_t24301fas1b2a, f2) == 17 && offsetof(gm_t24301fas1b2a, f3) == 24 && offsetof(gm_t24301fas1b2a, f4) == 40 && offsetof(gm_t24301fas1b2a, f5) == 48 && offsetof(gm_t24301fas1b2a, f6) == 56, "layout of `FieldDecl`");
_Static_assert(sizeof(gm_t3568dh2osse0) == 32 && _Alignof(gm_t3568dh2osse0) == 8 && offsetof(gm_t3568dh2osse0, f0) == 0 && offsetof(gm_t3568dh2osse0, f1) == 8 && offsetof(gm_t3568dh2osse0, f2) == 24, "layout of `(u64, string, u16)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t5g5vgung6h6s) == 32 && _Alignof(gm_t5g5vgung6h6s) == 8 && offsetof(gm_t5g5vgung6h6s, f0) == 0 && offsetof(gm_t5g5vgung6h6s, f1) == 8 && offsetof(gm_t5g5vgung6h6s, f2) == 24, "layout of `(u32, []u8, u32)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t24301fas1b2a f1; } gm_t8uai8b3boj78;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
typedef struct { gm_th3eueiipivq6 f0; uint8_t f1; uint8_t pe[7]; } gm_tbm0n9ujhrlhf;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
typedef struct { gm_tnnjeerolos0a f0; gm_trqfvmjtt4sij f1; } gm_tdtrcq4ju76rh;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `(addr: string, t: Ty)`");
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th3eueiipivq6) == 24 && _Alignof(gm_th3eueiipivq6) == 8 && offsetof(gm_th3eueiipivq6, f0) == 0 && offsetof(gm_th3eueiipivq6, f1) == 16, "layout of `(len: ?u64, is_const: bool)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tnljbma3s3h16;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tov4mtrfp5e7v) == 12 && _Alignof(gm_tov4mtrfp5e7v) == 4 && offsetof(gm_tov4mtrfp5e7v, f0) == 0 && offsetof(gm_tov4mtrfp5e7v, f1) == 4 && offsetof(gm_tov4mtrfp5e7v, f2) == 8, "layout of `BitField`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tpv68m88f9lcr;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnljbma3s3h16 v0; gm_t7bjbmukv26e7 v1; } f1; } gm_t03a3imodqi32;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8uai8b3boj78) == 80 && _Alignof(gm_t8uai8b3boj78) == 8 && offsetof(gm_t8uai8b3boj78, f0) == 0 && offsetof(gm_t8uai8b3boj78, f1) == 8, "layout of `(i: usize, x: FieldDecl)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbm0n9ujhrlhf) == 32 && _Alignof(gm_tbm0n9ujhrlhf) == 8 && offsetof(gm_tbm0n9ujhrlhf, f0) == 0 && offsetof(gm_tbm0n9ujhrlhf, f1) == 24, "layout of `?(len: ?u64, is_const: bool)`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?(variants: []VariantDef, tag: Ty)`");
_Static_assert(sizeof(gm_tdtrcq4ju76rh) == 48 && _Alignof(gm_tdtrcq4ju76rh) == 8 && offsetof(gm_tdtrcq4ju76rh, f0) == 0 && offsetof(gm_tdtrcq4ju76rh, f1) == 8, "layout of `ParamData`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t24301fas1b2a v0; gm_t7bjbmukv26e7 v1; } f1; } gm_tfc3oljlofib6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpautr4br7h3l v0; gm_tcmkompbl09il v1; gm_b16_8 v2; } f1; } gm_ti0kgh3pc3in2;
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tnljbma3s3h16) == 64 && _Alignof(gm_tnljbma3s3h16) == 8 && offsetof(gm_tnljbma3s3h16, f0) == 0 && offsetof(gm_tnljbma3s3h16, f1) == 32 && offsetof(gm_tnljbma3s3h16, f2) == 40 && offsetof(gm_tnljbma3s3h16, f3) == 48, "layout of `Variant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpv68m88f9lcr) == 56 && _Alignof(gm_tpv68m88f9lcr) == 8 && offsetof(gm_tpv68m88f9lcr, f0) == 0 && offsetof(gm_tpv68m88f9lcr, f1) == 32 && offsetof(gm_tpv68m88f9lcr, f2) == 40, "layout of `Constraint`");
_Static_assert(sizeof(gm_t03a3imodqi32) == 72 && _Alignof(gm_t03a3imodqi32) == 8 && offsetof(gm_t03a3imodqi32, f0) == 0 && offsetof(gm_t03a3imodqi32, f1) == 8, "layout of `UnionMember`");
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_ti0kgh3pc3in2 f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t p6[7]; gm_b16_8 f6; gm_b16_8 f7; uint8_t f8; uint8_t p9[3]; gm_tl874t2atnlh3 f9; gm_tf5srh4mp4b3i f10; } gm_t6art7bprjiqb;
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tfc3oljlofib6) == 80 && _Alignof(gm_tfc3oljlofib6) == 8 && offsetof(gm_tfc3oljlofib6, f0) == 0 && offsetof(gm_tfc3oljlofib6, f1) == 8, "layout of `StructMember`");
_Static_assert(sizeof(gm_ti0kgh3pc3in2) == 56 && _Alignof(gm_ti0kgh3pc3in2) == 8 && offsetof(gm_ti0kgh3pc3in2, f0) == 0 && offsetof(gm_ti0kgh3pc3in2, f1) == 8, "layout of `AdtKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
_Static_assert(sizeof(gm_t6art7bprjiqb) == 184 && _Alignof(gm_t6art7bprjiqb) == 8 && offsetof(gm_t6art7bprjiqb, f0) == 0 && offsetof(gm_t6art7bprjiqb, f1) == 16 && offsetof(gm_t6art7bprjiqb, f2) == 24 && offsetof(gm_t6art7bprjiqb, f3) == 80 && offsetof(gm_t6art7bprjiqb, f4) == 96 && offsetof(gm_t6art7bprjiqb, f5) == 112 && offsetof(gm_t6art7bprjiqb, f6) == 120 && offsetof(gm_t6art7bprjiqb, f7) == 136 && offsetof(gm_t6art7bprjiqb, f8) == 152 && offsetof(gm_t6art7bprjiqb, f9) == 156 && offsetof(gm_t6art7bprjiqb, f10) == 168, "layout of `AdtInfo`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t0ialelq3ql1k v0; } f1; } gm_t5vbkdq81qfs7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t5vbkdq81qfs7) == 208 && _Alignof(gm_t5vbkdq81qfs7) == 8 && offsetof(gm_t5vbkdq81qfs7, f0) == 0 && offsetof(gm_t5vbkdq81qfs7, f1) == 8, "layout of `UntypedResult`");
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
typedef struct { gm_t5vbkdq81qfs7 f0; uint8_t f1; uint8_t pe[7]; } gm_tkv1vap90vht2;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
_Static_assert(sizeof(gm_tkv1vap90vht2) == 216 && _Alignof(gm_tkv1vap90vht2) == 8 && offsetof(gm_tkv1vap90vht2, f0) == 0 && offsetof(gm_tkv1vap90vht2, f1) == 208, "layout of `?UntypedResult`");
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_adt_luat93d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_adt_mentions_param_0jsjgma {
    gm_frame h;
    gm_ti0kgh3pc3in2 l2; /* kind */
    uint8_t ret;
};
struct gm_fr_attr_power_of_two_gj02n2t {
    gm_frame h;
    gm_t5vbkdq81qfs7 l5; /* r */
    gm_t0ialelq3ql1k l6; /* v */
    gm_tbci5ospk5nnn l7; /* i */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_bit_width_b7ofh2h {
    gm_frame h;
    gm_tpautr4br7h3l l5; /* def */
    gm_tckdobunaulee l11; /* v */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_bits_layout_u6kkccp {
    gm_frame h;
    gm_tpautr4br7h3l l3; /* sd */
    gm_trqfvmjtt4sij l6; /* out */
    gm_t0mglksrkfa5e l9; /* fd */
    gm_t57euh612ligr ret;
};
struct gm_fr_c_union_fields_btdvc8b {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_check_bits_struct_ophej5b {
    gm_frame h;
    gm_t0mglksrkfa5e l10; /* f */
    gm_b0_1 ret;
};
struct gm_fr_contains_by_value_pcdi4kq {
    gm_frame h;
    gm_ti0kgh3pc3in2 l5; /* kind */
    uint8_t ret;
};
struct gm_fr_declare_adt_iefkbkk {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_declare_adt_in_74tov03 {
    gm_frame h;
    gm_trqfvmjtt4sij l16; /* generics */
    gm_trqfvmjtt4sij l17; /* defaults */
    gm_tf2orqcrphmek l20; /* p */
    gm_t6qooom8qa2do l27; /* v */
    gm_ti0kgh3pc3in2 l28; /* kind */
    gm_tpv68m88f9lcr l34; /* c */
    gm_trqfvmjtt4sij l35; /* stack */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_declare_local_adt_6j4pdai {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_adt_tdp3hro {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_2k4643t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3jt8vgk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3jt8vgk_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8fnsr5h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8q3kspo_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_atlcs4j {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c06andg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d0lhrvs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d53jqcq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dijpe9t {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_evi8iki {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_f1ngfj0 {
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
struct gm_fr_empty_slice_hu049sk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ito0pap {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
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
struct gm_fr_empty_slice_ose1ba9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_u4e74ha {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_struct_b5i94nj {
    gm_frame h;
    gm_ti0kgh3pc3in2 ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_extend_74ac4hb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_lfqasgq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_field_bits_2ktfm1k {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_flatten_2js7j92 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_tnpifv20irnph ret;
};
struct gm_fr_format_0au88qf {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_45bh632 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_5iu9hua {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_72n0e07 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_9kb6o1u {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_a5p8et1 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_atintr3 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bc9gr0b {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bsjq7um {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_c5ago2c {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_fiq5e0p {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_nag8m5u {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_otdvqpt {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_slub3h9 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_uaa99rp {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_fresh_adt_u9rnf9f {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_fresh_param_442g4m9 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_get_6qnso5a {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_has_param_u9t0efs {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_instance_args_15ek7cf {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_instantiate_adt_lbjcui1 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* gargs */
    gm_t6art7bprjiqb l8; /* info */
    gm_trqfvmjtt4sij l10; /* copy */
    gm_t6art7bprjiqb l12; /* inst_info */
    gm_trqfvmjtt4sij l13; /* map */
    gm_tpautr4br7h3l l18; /* def */
    gm_trqfvmjtt4sij l21; /* vs */
    gm_tckdobunaulee l24; /* v */
    gm_ti0kgh3pc3in2 l28; /* kind */
    gm_trqfvmjtt4sij l33; /* all */
    gm_trqfvmjtt4sij l34; /* infinite */
    gm_trqfvmjtt4sij l38; /* seen */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_int_fits_imbqv0c {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* v */
    uint8_t ret;
};
struct gm_fr_is_effects_kind_uutbf9u {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_infinitely_sized_mhhcucl {
    gm_frame h;
    gm_ti0kgh3pc3in2 l5; /* kind */
    uint8_t ret;
};
struct gm_fr_is_negative_k87pp3n {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_5kg5cjm_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_au01ire {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_g6uul2f {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_gst7tuh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ja7tv3o {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_oalq03d {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ol3ji66 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pmp06kq_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_spqn36o {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vmvl81b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_many_ptr_l4v11f8 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_member_count_otu9tc0 {
    gm_frame h;
    gm_ti0kgh3pc3in2 l0; /* kind */
    gm_tpautr4br7h3l l1; /* def */
    uint64_t ret;
};
struct gm_fr_member_ty_esvuguv {
    gm_frame h;
    gm_ti0kgh3pc3in2 l0; /* kind */
    gm_tpautr4br7h3l l2; /* def */
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_new_adt_info_5pl2srl {
    gm_frame h;
    gm_ti0kgh3pc3in2 l2; /* kind */
    gm_t6art7bprjiqb ret;
};
struct gm_fr_new_param_7010dt4 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_optional_rh6okj3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_param_kind_of_1jngakt {
    gm_frame h;
    gm_tnnjeerolos0a ret;
};
struct gm_fr_param_n8kgsdm {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_pending_layout_ikknuv1 {
    gm_frame h;
    gm_ti0kgh3pc3in2 l3; /* kind */
    gm_t9805qtonbsag ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_ptr_t1h2m2d {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_push_0mk7udh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_1j8vbii {
    gm_frame h;
    gm_tckdobunaulee l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_83inprj {
    gm_frame h;
    gm_tnljbma3s3h16 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bth3krn {
    gm_frame h;
    gm_tdtrcq4ju76rh l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d1r4h19 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ghqse9f {
    gm_frame h;
    gm_t24301fas1b2a l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_h69n5fi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_hv483nm {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_j84ih6c {
    gm_frame h;
    gm_t6art7bprjiqb l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_p52mcnt {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_r2k6rjf_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_s5vr2a2 {
    gm_frame h;
    gm_toa2vhe3l4hfr l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_sp4si0f {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_type_alias_rr9mg3i {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_push_u4kbe6d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vrmo7c5 {
    gm_frame h;
    gm_t0mglksrkfa5e l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_12ta4fr {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_1sqb3ir {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_24gkq1e {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_4mbcm9u {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_6hpdpt2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_cmj1khh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_coblsev {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_f2uhbto {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_ht2em0b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j7fb9h1_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_lm3950p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_lq604tk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_m244brh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_q0a57c8 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_v0p8ic9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_remove_in_progress_lrie478 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_result_o9ras10 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_scope_mark_i3b2mvo {
    gm_frame h;
    gm_t66j1im9ip128 ret;
};
struct gm_fr_set_state_3j300fi {
    gm_frame h;
    gm_tagrkbqh054h7 l2; /* v */
    gm_b0_1 ret;
};
struct gm_fr_set_template_o09q96o {
    gm_frame h;
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
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_struct_fields_chqu3it {
    gm_frame h;
    gm_tpautr4br7h3l l3; /* def */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_struct_kind_5l1hs46 {
    gm_frame h;
    gm_tkacitepg5fk0 l10; /* a */
    gm_trqfvmjtt4sij l13; /* fields */
    gm_trqfvmjtt4sij l14; /* defaults */
    gm_trqfvmjtt4sij l15; /* default_exprs */
    gm_trqfvmjtt4sij l17; /* members */
    gm_t24301fas1b2a l21; /* f */
    gm_tkacitepg5fk0 l26; /* a */
    gm_t0mglksrkfa5e l34; /* x */
    gm_ti0kgh3pc3in2 ret;
};
struct gm_fr_subst_a2s1mb3 {
    gm_frame h;
    gm_t53at80pfdus3 l4; /* k */
    gm_trqfvmjtt4sij l14; /* new_args */
    gm_toa2vhe3l4hfr l17; /* a */
    gm_tddtjfggdat0d l19; /* sig */
    gm_tddtjfggdat0d l20; /* sig */
    gm_trqfvmjtt4sij l51; /* out */
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_subst_effects_jnlbu5m {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* refs */
    gm_trqfvmjtt4sij l7; /* args */
    gm_toa2vhe3l4hfr l10; /* a */
    gm_toa2vhe3l4hfr l12; /* v */
    gm_tnpifv20irnph ret;
};
struct gm_fr_subst_fields_pqf9lb0 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_t0mglksrkfa5e l6; /* f */
    gm_b16_8 ret;
};
struct gm_fr_subst_sig_9ngh936 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* params */
    gm_tddtjfggdat0d ret;
};
struct gm_fr_taken_struct_members_1ltbb18 {
    gm_frame h;
    gm_tfc3oljlofib6 l6; /* m */
    gm_t24301fas1b2a l7; /* f */
    gm_t7bjbmukv26e7 l8; /* w */
    gm_b0_1 ret;
};
struct gm_fr_taken_union_members_pavth81 {
    gm_frame h;
    gm_t03a3imodqi32 l6; /* m */
    gm_tnljbma3s3h16 l7; /* v */
    gm_t7bjbmukv26e7 l8; /* w */
    gm_b0_1 ret;
};
struct gm_fr_template_9o92cvv {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
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
struct gm_fr_truncate_7r782uc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_daelje0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_dsb08pk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_union_kind_j2gcmto {
    gm_frame h;
    gm_tkacitepg5fk0 l7; /* a */
    gm_trqfvmjtt4sij l12; /* variants */
    gm_trqfvmjtt4sij l13; /* spans */
    gm_tbci5ospk5nnn l14; /* next */
    gm_trqfvmjtt4sij l15; /* members */
    gm_tnljbma3s3h16 l18; /* v */
    gm_tckdobunaulee l21; /* x */
    gm_tbci5ospk5nnn l24; /* tag */
    gm_t0ialelq3ql1k l26; /* c */
    gm_tbci5ospk5nnn l27; /* i */
    gm_trqfvmjtt4sij l28; /* fields */
    gm_tckdobunaulee l32; /* v */
    gm_tckdobunaulee l39; /* v */
    gm_tckdobunaulee l43; /* w */
    gm_t4gut2c9fj1kt l44; /* d */
    gm_ti0kgh3pc3in2 ret;
};
struct gm_fr_union_variants_a7khaqp {
    gm_frame h;
    gm_tdlpv0q69lov1 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_adt_mentions_param_0jsjgma(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_array_th8sdi3(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_attr_power_of_two_gj02n2t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_bit_width_b7ofh2h(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
__attribute__((aligned(4))) void gm_f_c_union_fields_btdvc8b(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_c_union_fields_btdvc8b_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_bits_struct_ophej5b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_default_4bf68q8(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2);
__attribute__((aligned(4))) uint8_t gm_f_contains_by_value_pcdi4kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2, void * a3);
__attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_in_74tov03(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4, uint8_t a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_constraint_s1juggf(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_local_adt_6j4pdai(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_effect_set_vtcpipr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_empty_adt_tdp3hro(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_2k4643t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c06andg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d0lhrvs(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d53jqcq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dijpe9t(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_evi8iki(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_f1ngfj0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fh7svmj(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hu049sk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oqjc4ke(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_osbc35j(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u4e74ha(void);
inline __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out);
__attribute__((aligned(4))) gm_ti0kgh3pc3in2 gm_f_empty_struct_b5i94nj_c(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_constraint_defer_52jt3m9(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_74ac4hb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_lfqasgq(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_uarsnut(void * a0, void * a1);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_flatten_2js7j92(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) void gm_f_format_0au88qf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_15426cv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_35467gd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_45bh632(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_5iu9hua(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_72n0e07(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_9kb6o1u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_a5p8et1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_atintr3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_bc9gr0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_bsjq7um(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_c5ago2c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_fiq5e0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h5s5tmr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_h92mgv5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_nag8m5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_otdvqpt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_slub3h9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) void gm_f_format_uaa99rp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_param_442g4m9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_6qnso5a(void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_has_param_u9t0efs(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_41b084m(gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_instance_args_15ek7cf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_instantiate_adt_lbjcui1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_infinitely_sized_mhhcucl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_key_u64_ljklb56(const void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_len_expr_i134fvg(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_5kg5cjm_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_au01ire(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_g6uul2f(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_gst7tuh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ja7tv3o(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ol3ji66(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_spqn36o(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint64_t gm_f_member_count_otu9tc0(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_member_count_otu9tc0_c(gm_ti0kgh3pc3in2 a0);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_member_ty_esvuguv(const void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_member_ty_esvuguv_c(gm_ti0kgh3pc3in2 a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_new_adt_info_5pl2srl(void *gm_out, gm_b16_8 a0, uint64_t a1, const void * a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4);
__attribute__((aligned(4))) gm_t6art7bprjiqb gm_f_new_adt_info_5pl2srl_c(gm_b16_8 a0, uint64_t a1, gm_ti0kgh3pc3in2 a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4);
__attribute__((aligned(4))) uint32_t gm_f_new_param_7010dt4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tnnjeerolos0a a2);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_of_1jngakt(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_pending_layout_ikknuv1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1j8vbii(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_83inprj(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bth3krn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ghqse9f(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_j84ih6c(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_p52mcnt(void * a0, gm_csv4kcdnuj265c a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf_2(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_sp4si0f(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u4kbe6d(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vrmo7c5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_f1iksvi(void * a0, const void * a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_12ta4fr(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_24gkq1e(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4mbcm9u(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_6hpdpt2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_cmj1khh(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_coblsev(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_f2uhbto(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ht2em0b(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_lm3950p(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_lq604tk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_m244brh(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q0a57c8(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint32_t gm_f_remove_10muhi8(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_remove_in_progress_lrie478(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_type_jjodbei(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_state_3j300fi(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_soa_shape_of_ohenvjc(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_struct_fields_chqu3it_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_struct_kind_5l1hs46(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, void * a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_subst_fields_pqf9lb0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_subst_sig_9ngh936(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_soa_2chqte3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_taken_struct_members_1ltbb18(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_taken_union_members_pavth81(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3);
inline __attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_try_untyped_fj0h8i5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint8_t gm_f_type_size_ok_tr1llg4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_union_kind_j2gcmto(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tdlpv0q69lov1 gm_f_union_variants_a7khaqp_c(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tqs5p4n2tq19v gm_f_when_cond_9a46a5h(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s2ph9f020upd2[7];
extern const uint8_t gm_s3lik4tvvjana[65];
extern const uint8_t gm_s4ko774bb2ea9[56];
extern const uint8_t gm_s62an8gcfpfbj[53];
extern const uint8_t gm_s6uch3l6osunv[50];
extern const uint8_t gm_s6vvtrmtnt3j1[5];
extern const uint8_t gm_s70g70juoj4nh[42];
extern const uint8_t gm_s86fb5obo4kb8[154];
extern const uint8_t gm_s8h39pil8lgh0[41];
extern const uint8_t gm_s8u6lv8hq3sn2[23];
extern const uint8_t gm_sacmu7hr5dknu[88];
extern const uint8_t gm_sbe7dr7t400bh[16];
extern const uint8_t gm_sbim95dtkmqmv[49];
extern const uint8_t gm_sbse0oko82bp9[42];
extern const uint8_t gm_sebk4v8jac58q[43];
extern const uint8_t gm_seoeuc6nik756[40];
extern const uint8_t gm_sevm6b5otfta5[21];
extern const uint8_t gm_sf3irv2ans6de[57];
extern const uint8_t gm_sf7l7ca9coehi[85];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_shda201svb9eg[119];
extern const uint8_t gm_shncvaenjonsu[41];
extern const uint8_t gm_si1b582k23708[50];
extern const uint8_t gm_sisjr6ep7j5j0[79];
extern const uint8_t gm_sj1k4t2de5144[6];
extern const uint8_t gm_slpp558cs8j3v[36];
extern const uint8_t gm_sp46pm3sna4bl[86];
extern const uint8_t gm_sqen1asili489[52];
extern const uint8_t gm_sqis5ba78j7cv[42];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3k278vcmqgdr[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_srcbsc23is03ttm[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcsffver18si54[];
extern const uint8_t gm_sshop8kf5qfhm[63];
extern const uint8_t gm_su38t05utsk53[67];
extern const uint8_t gm_suuv0qpi4v4e4[8];
extern const uint8_t gm_svocs6aii9rqg[75];
extern const gm_tmpl gm_tpl372mhqlcchb3;
extern const gm_tmpl gm_tpl40m4ah8dc3ae;
extern const gm_tmpl gm_tpl4e4n6ul1t53g;
extern const gm_tmpl gm_tpl5oph1bqhjo1e;
extern const gm_tmpl gm_tpl6md5hu7rmp82;
extern const gm_tmpl gm_tpl953juq2iaklp;
extern const gm_tmpl gm_tpl9d5e93nnc8t1;
extern const gm_tmpl gm_tpl9d622ubds6jn;
extern const gm_tmpl gm_tpl9fn8oj7nnnsq;
extern const gm_tmpl gm_tpl9kt3nn7p3a92;
extern const gm_tmpl gm_tplc8kkdoja48ii;
extern const gm_tmpl gm_tplkji0sd9phcb9;
extern const gm_tmpl gm_tplmr9p697ovoo7;
extern const gm_tmpl gm_tplpntqck8i8f5b;
extern const gm_tmpl gm_tplut4ib9ov72oe;
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
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a1));
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t2, t4, t5, t7);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_adt_luat93d(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_adt_luat93d F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    gm_b16_8 gm_l2; /* args */
    GM_STACK_CHECK(&F, 148u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    gm_b16_8 t4 = gm_l2;
    gm_tiilmfd16v3fc t5;
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint32_t, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_t53at80pfdus3 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 32, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)17ULL));
    GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t6 + 8), t5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3jt8vgk_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8fnsr5h F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8q3kspo_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_8q3kspo_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_92arfe8 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_atlcs4j(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_atlcs4j F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c06andg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c06andg F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d53jqcq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d53jqcq F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 80)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dijpe9t(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dijpe9t F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_evi8iki(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_evi8iki F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hu049sk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hu049sk F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ito0pap F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ose1ba9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ose1ba9 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_u4e74ha(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_u4e74ha F;
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
inline __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_struct_b5i94nj F;
    GM_STACK_CHECK_SHALLOW(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ose1ba9();
    gm_tf5srh4mp4b3i t3;
    memset(&t3, 0, sizeof t3);
    gm_tkn42u7na3t81 t4;
    memset(&t4, 0, sizeof t4);
    gm_tpautr4br7h3l t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t2);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 16), ((uint8_t)0));
    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t5 + 24), t3);
    GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t5 + 40), t4);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 0), ((uint8_t)0ULL));
    GM_ST(gm_tpautr4br7h3l, ((uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 8), t5);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l2;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        int32_t t9 = gm_f_mag_cmp_s39veq6(t6, t8);
        uint8_t t10 = ((t9 == ((int32_t)0LL)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_74ac4hb(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_74ac4hb F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_m244brh(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 24);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 24)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_lfqasgq(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_lfqasgq F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_j4iu2lk(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 4);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 4)));
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
inline __attribute__((aligned(4))) void gm_f_format_0au88qf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_0au88qf F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svocs6aii9rqg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(74));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6md5hu7rmp82), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_45bh632(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_45bh632 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqis5ba78j7cv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplkji0sd9phcb9), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_5iu9hua(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_5iu9hua F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sebk4v8jac58q));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(42));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9d622ubds6jn), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_72n0e07(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_72n0e07 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8u6lv8hq3sn2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9kt3nn7p3a92), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_9kb6o1u(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_9kb6o1u F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s62an8gcfpfbj));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5oph1bqhjo1e), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_a5p8et1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_a5p8et1 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbim95dtkmqmv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(48));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplut4ib9ov72oe), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_atintr3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_atintr3 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sevm6b5otfta5));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(20));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplpntqck8i8f5b), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_bc9gr0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_bc9gr0b F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_shda201svb9eg));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(118));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9fn8oj7nnnsq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_bsjq7um(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bsjq7um F;
    gm_t0bf4lga76ktk gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sp46pm3sna4bl));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(85));
    gm_t0bf4lga76ktk t3 = GM_LD(gm_t0bf4lga76ktk, ((uint8_t *)a0));
    gm_f_format_h92mgv5((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl953juq2iaklp), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_c5ago2c(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_c5ago2c F;
    gm_t3568dh2osse0 gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sshop8kf5qfhm));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(62));
    gm_t3568dh2osse0 t3 = GM_LD(gm_t3568dh2osse0, ((uint8_t *)a0));
    gm_f_format_35467gd((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplmr9p697ovoo7), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_fiq5e0p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_fiq5e0p F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6uch3l6osunv));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl372mhqlcchb3), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_nag8m5u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_nag8m5u F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sqen1asili489));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(51));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_h5s5tmr((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplc8kkdoja48ii), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_otdvqpt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_otdvqpt F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s86fb5obo4kb8));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(153));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl9d5e93nnc8t1), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_slub3h9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_slub3h9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s70g70juoj4nh));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(41));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl40m4ah8dc3ae), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_uaa99rp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_uaa99rp F;
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_si1b582k23708));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(49));
    gm_tfeab279c16rr t3 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
    gm_f_format_15426cv((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4e4n6ul1t53g), t2);
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
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_adt_u9rnf9f(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_adt_u9rnf9f F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 136) + 8));
    uint32_t t4 = (uint32_t)t3;
    gm_l2 = t4;
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 136);
    gm_b16_8 t7 = gm_l1;
    gm_tftb1odc739t5 t8 = gm_f_push_3ef746b(t6, t7);
    gm_l3 = t8;
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_src0ih77f43mhf2, 263, 5);
    void * t10 = gm_l0;
    void * t11 = (void *)(((uint8_t *)t10) + 176);
    uint32_t t12 = gm_l2;
    gm_tftb1odc739t5 t13 = gm_f_push_im9d4ro(t11, t12);
    gm_l4 = t13;
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_src0ih77f43mhf2, 264, 5);
    uint32_t t15 = gm_l2;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_param_442g4m9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fresh_param_442g4m9 F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l2;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 256);
    gm_b16_8 t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
    gm_l2 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_src0ih77f43mhf2, 286, 5);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, ((((uint8_t *)t7) + 256) + 8));
    uint64_t r9; bool o10 = __builtin_sub_overflow(t8, ((uint64_t)1ULL), &r9);
    if (o10) gm_trap("integer overflow in `-`", gm_src0ih77f43mhf2, 287, 13);
    uint64_t t11 = r9;
    uint32_t t12 = (uint32_t)t11;
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_6qnso5a(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_6qnso5a F;
    void * gm_l0; /* m */
    gm_tiilmfd16v3fc gm_l1; /* k */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK(&F, 165u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_tf5srh4mp4b3i gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)((uint8_t *)&gm_l1);
    gm_tf5srh4mp4b3i t4 = gm_f_find_uarsnut(t2, t3);
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
    uint32_t t12 = GM_LD(uint32_t, ((((uint8_t *)t9) + (uintptr_t)t11 * 40) + 32));
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_effects_kind_uutbf9u(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_effects_kind_uutbf9u F;
    void * gm_l0; /* t */
    gm_t08olq97rscn3 gm_l1; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 85u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)6ULL));
            if (!t6) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l1), ((((uint8_t *)t4) + 8) + 8), 32);
            gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sgbumttiou4de));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(7));
            uint8_t t9 = (gm_eq0iqiubfm7e(((uint8_t *)&t7), ((uint8_t *)&t8)));
            F.ret = t9;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_negative_k87pp3n F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    F.ret = t2;
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
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_084fcd6 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_92arfe8();
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
inline __attribute__((aligned(4))) void gm_f_list_5kg5cjm_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_5kg5cjm_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_3jt8vgk_2();
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
inline __attribute__((aligned(4))) void gm_f_list_g6uul2f(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_g6uul2f F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_hu049sk();
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
inline __attribute__((aligned(4))) void gm_f_list_gst7tuh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_gst7tuh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_c06andg();
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
inline __attribute__((aligned(4))) void gm_f_list_ja7tv3o(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ja7tv3o F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_dijpe9t();
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
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_oalq03d F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8fnsr5h();
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
inline __attribute__((aligned(4))) void gm_f_list_ol3ji66(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ol3ji66 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_evi8iki();
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
inline __attribute__((aligned(4))) void gm_f_list_pmp06kq_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pmp06kq_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_8q3kspo_2();
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
inline __attribute__((aligned(4))) void gm_f_list_spqn36o(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_spqn36o F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ose1ba9();
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
inline __attribute__((aligned(4))) void gm_f_list_vmvl81b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_vmvl81b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fh7svmj();
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_many_ptr_l4v11f8(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_many_ptr_l4v11f8 F;
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
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)9ULL));
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
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_agd5c5k F;
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t gm_l2;
    {
        gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l0));
        gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    uint8_t t4 = (uint8_t)!t3;
    if (t4) {
        uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)&F.l0) + 8) + 8));
        uint8_t t6 = (t5 > ((uint64_t)0ULL));
        gm_l2 = t6;
    } else {
        gm_l2 = ((uint8_t)0);
    }
    uint8_t t7 = gm_l2;
    GM_ST(uint8_t, ((uint8_t *)&F.l1), t7);
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_one_08qg1op F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_from_u64_ud13q5v((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, ((uint64_t)1ULL));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_optional_rh6okj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_optional_rh6okj3 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* inner */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)13ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 8), t3);
    gm_t88rcucg1b7uk t5 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_param_n8kgsdm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_param_n8kgsdm F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint32_t t3 = gm_l1;
    gm_t53at80pfdus3 t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)19ULL));
    GM_ST(uint32_t, ((uint8_t *)&t4 + 8), t3);
    gm_t88rcucg1b7uk t5 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t2, &t4);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_570g79n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint32_t gm_l2; /* x */
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
        gm_t9805qtonbsag t6;
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
    uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t8) + (uintptr_t)t13 * 4));
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
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t17 + (uintptr_t)((uint64_t)0ULL) * 4)));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(t25));
    GM_ST(gm_b16_8, ((uint8_t *)t15), t26);
    uint32_t t27 = gm_l2;
    gm_t9805qtonbsag t28;
    GM_ST(uint8_t, (uint8_t *)&t28 + 5, 0);
    GM_ST(uint16_t, (uint8_t *)&t28 + 6, 0);
    GM_ST(uint32_t, ((uint8_t *)&t28), t27);
    ((uint8_t *)&t28)[4] = 1;
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
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bits_glps5i7 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((uint8_t *)t2));
    uint64_t r4; bool o5 = __builtin_mul_overflow(t3, ((uint64_t)8ULL), &r4);
    if (o5) gm_trap("integer overflow in `*`", gm_src4o48ksu43djd, 91, 49);
    uint64_t t6 = r4;
    uint32_t t7 = (uint32_t)t6;
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_ptr_t1h2m2d(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_ptr_t1h2m2d F;
    void * gm_l0; /* t */
    uint8_t gm_l1; /* is_const */
    gm_t88rcucg1b7uk gm_l2; /* pointee */
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
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)8ULL));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0mk7udh F;
    void * gm_l0; /* l */
    gm_tl874t2atnlh3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_r81t436();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_v0p8ic9(t8, ((uint64_t)1ULL), t10);
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
    gm_tl874t2atnlh3 t18 = gm_l1;
    GM_ST(gm_tl874t2atnlh3, (((uint8_t *)t16) + (uintptr_t)t17 * 12), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1j8vbii(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1j8vbii F;
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
        gm_b16_8 t9 = gm_f_empty_slice_hu049sk();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_6hpdpt2(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 72), ((uint8_t *)a1), 72);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_83inprj(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_83inprj F;
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
        gm_b16_8 t9 = gm_f_empty_slice_dijpe9t();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_cmj1khh(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 64), ((uint8_t *)a1), 64);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bth3krn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bth3krn F;
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
        gm_b16_8 t9 = gm_f_empty_slice_2k4643t();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_lq604tk(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 48), ((uint8_t *)a1), 48);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 48)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_d1r4h19 F;
    void * gm_l0; /* l */
    gm_tiilmfd16v3fc gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_92arfe8();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_m244brh(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 24), ((uint8_t *)&gm_l1), 24);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ghqse9f(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ghqse9f F;
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
        gm_b16_8 t9 = gm_f_empty_slice_c06andg();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_24gkq1e(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 72), ((uint8_t *)a1), 72);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_h69n5fi F;
    void * gm_l0; /* l */
    gm_tplpbqf3duvn3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tplpbqf3duvn3, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_8fnsr5h();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_1sqb3ir(t8, ((uint64_t)1ULL), t10);
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
    gm_tplpbqf3duvn3 t18 = gm_l1;
    GM_ST(gm_tplpbqf3duvn3, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hv483nm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_hv483nm F;
    void * gm_l0; /* l */
    gm_tep2drsl9408b gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_osbc35j();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_12ta4fr(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_im9d4ro F;
    void * gm_l0; /* l */
    uint32_t gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_d914i5u();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_2onqfca(t8, ((uint64_t)1ULL), t10);
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
    uint32_t t18 = gm_l1;
    GM_ST(uint32_t, (((uint8_t *)t16) + (uintptr_t)t17 * 4), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_j84ih6c(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_j84ih6c F;
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
        gm_b16_8 t9 = gm_f_empty_slice_oqjc4ke();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_q0a57c8(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 184), ((uint8_t *)&F.l1), 184);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 184)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_p52mcnt(void * a0, gm_csv4kcdnuj265c a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_p52mcnt F;
    void * gm_l0; /* l */
    gm_tov4mtrfp5e7v gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tov4mtrfp5e7v, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_evi8iki();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_coblsev(t8, ((uint64_t)1ULL), t10);
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
    gm_tov4mtrfp5e7v t18 = gm_l1;
    GM_ST(gm_tov4mtrfp5e7v, (((uint8_t *)t16) + (uintptr_t)t17 * 12), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_r2k6rjf_2(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_r2k6rjf_2 F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_8q3kspo_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_j7fb9h1_2(t8, ((uint64_t)1ULL), t10);
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
    void * t18 = gm_l1;
    GM_ST(void *, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_s5vr2a2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_s5vr2a2 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_fh7svmj();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_4mbcm9u(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_sp4si0f(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_sp4si0f F;
    void * gm_l0; /* l */
    gm_tommf4j9upomc gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_d0lhrvs();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_lm3950p(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_type_alias_rr9mg3i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_push_type_alias_rr9mg3i F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 93u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 1608);
    gm_b16_8 t4 = gm_l1;
    gm_t88rcucg1b7uk t5 = gm_l2;
    gm_tep2drsl9408b t6;
    GM_ST(uint32_t, (uint8_t *)&t6 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t6 + 0), t4);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t6 + 16), t5);
    gm_tftb1odc739t5 t7 = gm_f_push_hv483nm(t3, &t6);
    gm_l3 = t7;
    uint8_t t8 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t8) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcsffver18si54, 63, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u4kbe6d(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u4kbe6d F;
    void * gm_l0; /* l */
    gm_t5g5vgung6h6s gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_f1ngfj0();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_f2uhbto(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vrmo7c5(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vrmo7c5 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_ose1ba9();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_ht2em0b(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_12ta4fr(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_12ta4fr F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_1sqb3ir F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_24gkq1e(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_24gkq1e F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_2onqfca F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_4mbcm9u(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_4mbcm9u F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_6hpdpt2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_6hpdpt2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_cmj1khh(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_cmj1khh F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_coblsev(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_coblsev F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_f2uhbto(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_f2uhbto F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_ht2em0b(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_ht2em0b F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j7fb9h1_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_j7fb9h1_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_lm3950p(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_lm3950p F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_lq604tk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_lq604tk F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)48ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_m244brh(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_m244brh F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q0a57c8(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_q0a57c8 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)184ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_v0p8ic9 F;
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
inline __attribute__((aligned(4))) void gm_f_remove_in_progress_lrie478(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_remove_in_progress_lrie478 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    {
        {
            void * t4 = gm_l0;
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 1848));
            gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
            uint32_t t7 = gm_l1;
            gm_tf5srh4mp4b3i t8 = gm_f_index_of_41b084m(t6, t7);
            gm_l3 = t8;
            uint8_t t9 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
            if (!t9) {
                goto gm_lb2;
            }
            uint64_t t10 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
            gm_l2 = t10;
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 1848);
            uint64_t t13 = gm_l2;
            uint32_t t14 = gm_f_remove_10muhi8(t12, t13);
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scope_edqq1v8 F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* mark */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 1608);
    uint64_t t4 = GM_LD(uint64_t, ((uint8_t *)a1));
    gm_f_truncate_7r782uc(t3, t4);
    void * t5 = gm_l0;
    void * t6 = (void *)(((uint8_t *)t5) + 1648);
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
    gm_f_truncate_daelje0(t6, t7);
    void * t8 = gm_l0;
    void * t9 = (void *)(((uint8_t *)t8) + 1728);
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
    gm_f_truncate_dsb08pk(t9, t10);
    void * t11 = gm_l0;
    void * t12 = (void *)(((uint8_t *)t11) + 1688);
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
    gm_f_truncate_upeumbt(t12, t13);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_result_o9ras10(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_result_o9ras10 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* err */
    gm_t88rcucg1b7uk gm_l2; /* ok */
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_tgglga8ro3isc t5;
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 4), t4);
    gm_t53at80pfdus3 t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)14ULL));
    GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t6 + 8), t5);
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
inline __attribute__((aligned(4))) void gm_f_scope_mark_i3b2mvo(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_scope_mark_i3b2mvo F;
    void * gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1608) + 8));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)t4) + 1648) + 8));
    void * t6 = gm_l0;
    uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)t6) + 1728) + 8));
    void * t8 = gm_l0;
    uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t8) + 1688) + 8));
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 0), t3);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 8), t5);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 16), t7);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t66j1im9ip128 *)gm_out) + 24), t9);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_set_template_o09q96o(void * a0, uint32_t a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_set_template_o09q96o F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    uint32_t gm_l2; /* template */
    GM_STACK_CHECK_SHALLOW(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 176));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 176) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 272, 5);
    uint32_t t7 = gm_l2;
    GM_ST(uint32_t, (((uint8_t *)t3) + (uintptr_t)t6 * 4), t7);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tabb710d9mq2e gm_l2;
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
        gm_b0_1 t4 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&gm_l2, t5);
    }
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 8, gm_srclv3pj5es0f8o, 624, 12);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 8));
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
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_7r782uc F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_daelje0 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_dsb08pk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_upeumbt F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 8));
    uint8_t t5 = (t2 < t4);
    if (t5) {
        void * t6 = gm_l0;
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, ((uint8_t *)t7));
        uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        uint64_t t10 = gm_l1;
        uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t12 = (uintptr_t)t10;
        if (t11 > t12 || t12 > (uintptr_t)t9) gm_trap_slice(t11, t12, t9, gm_srcrgc9bkquf6nr, 125, 36);
        uint64_t t13 = ((uintptr_t)t10) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 224)));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
        GM_ST(gm_b16_8, ((uint8_t *)t6), t14);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_s25l990 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t2;
    gm_f_list_go2qg0b((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 4, 0);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0), ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8), t2);
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
inline __attribute__((aligned(4))) uint8_t gm_f_adt_mentions_param_0jsjgma(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_mentions_param_0jsjgma F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_t88rcucg1b7uk gm_l6; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 177u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tkn42u7na3t81 gm_l7;
    {
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 1088));
        uint64_t t9 = GM_LD(uint64_t, ((((uint8_t *)t7) + 1088) + 8));
        uint32_t t10 = gm_l1;
        uint64_t t11 = (uint64_t)t10;
        if ((uintptr_t)t11 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t11, t9, gm_src3k278vcmqgdr, 73, 13);
        memmove(((uint8_t *)&F.l2), ((((uint8_t *)t8) + (uintptr_t)t11 * 184) + 24), 56);
    }
    gm_l4 = ((uint64_t)0ULL);
    {
        uint64_t t12 = gm_f_member_count_otu9tc0(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l2)));
        gm_l3 = t12;
    }
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
                        uint64_t t17 = gm_l5;
                        gm_tkn42u7na3t81 t18 = gm_f_member_ty_esvuguv(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l2)), t17);
                        gm_l7 = t18;
                        uint8_t t19 = (uint8_t)((((uint8_t *)&gm_l7))[4] != 0);
                        if (t19) {
                            gm_t88rcucg1b7uk t20 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l7));
                            gm_l6 = t20;
                        } else {
                            goto gm_lb3;
                        }
                        void * t21 = gm_l0;
                        void * t22 = (void *)(((uint8_t *)t21) + 8);
                        gm_t88rcucg1b7uk t23 = gm_l6;
                        uint8_t t24 = gm_f_has_param_u9t0efs(t22, GM_CONV(gm_csguankko4lmhd, t23));
                        if (t24) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 74, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t25 = gm_l4;
            uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
            uint64_t t28 = r26;
            gm_l4 = t28;
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_attr_power_of_two_gj02n2t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_attr_power_of_two_gj02n2t F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* a */
    gm_b16_8 gm_l3; /* args */
    void * gm_l4; /* arg */
    uint64_t gm_l8; /* n */
    GM_STACK_CHECK(&F, 694u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tkv1vap90vht2 gm_l9;
    gm_tf5srh4mp4b3i gm_l10;
    void * t10 = gm_l2;
    uint8_t t11 = (uint8_t)(((((uint8_t *)t10) + 32))[16] != 0);
    if (t11) {
        gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 32));
        gm_l3 = t12;
    } else {
        gm_b16_8 t13 = gm_f_empty_slice_g3tddrb();
        gm_l3 = t13;
    }
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t15 = (!(t14 == ((uint64_t)1ULL)));
    if (t15) {
        void * t16 = gm_l0;
        void * t17 = gm_l2;
        gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t17) + 56));
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sbse0oko82bp9));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(41));
        gm_b0_1 t20 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t16, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tf5srh4mp4b3i t21;
        memset(&t21, 0, sizeof t21);
        F.ret = t21;
        goto gm_fret;
    }
    void * t22 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t23, gm_src3k278vcmqgdr, 449, 13);
    void * t24 = (void *)(((uint8_t *)t22) + (uintptr_t)((uint64_t)0ULL) * 88);
    gm_l4 = t24;
    {
        {
            {
                void * t25 = gm_l0;
                uint64_t t26 = gm_l1;
                void * t27 = gm_l4;
                gm_f_try_untyped_fj0h8i5((void *)&gm_l9, (void *)NULL, cx, NULL, t25, t26, t27);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t28 = (uint8_t)((((uint8_t *)&gm_l9))[208] != 0);
            if (!t28) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&F.l5), ((uint8_t *)&gm_l9), 208);
            }
            {
                {
                    uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                    uint8_t t30 = (uint8_t)(t29 == ((uint8_t)0ULL));
                    if (!t30) {
                        goto gm_lb4;
                    }
                    {
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)&F.l5) + 8), 200);
                    }
                    uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                    uint8_t t32 = (uint8_t)(t31 == ((uint8_t)0ULL));
                    if (!t32) {
                        goto gm_lb4;
                    }
                    {
                        memmove(((uint8_t *)&F.l7), (((uint8_t *)&F.l6) + 8), 48);
                    }
                    {
                        {
                            gm_tf5srh4mp4b3i t33 = gm_f_to_u64_m01t1da(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)));
                            gm_l10 = t33;
                            uint8_t t34 = (uint8_t)((((uint8_t *)&gm_l10))[8] != 0);
                            if (!t34) {
                                goto gm_lb6;
                            }
                            uint64_t t35 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                            gm_l8 = t35;
                            {
                                {
                                    uint64_t t36 = gm_l8;
                                    uint8_t t37 = (t36 > ((uint64_t)0ULL));
                                    if (!t37) {
                                        goto gm_lb8;
                                    }
                                    uint64_t t38 = gm_l8;
                                    uint64_t t39 = gm_l8;
                                    uint64_t r40; bool o41 = __builtin_sub_overflow(t39, ((uint64_t)1ULL), &r40);
                                    if (o41) gm_trap("integer overflow in `-`", gm_src3k278vcmqgdr, 453, 35);
                                    uint64_t t42 = r40;
                                    uint64_t t43 = (uint64_t)(t38 & t42);
                                    uint8_t t44 = ((t43 == ((uint64_t)0ULL)));
                                    if (!t44) {
                                        goto gm_lb8;
                                    }
                                    uint64_t t45 = gm_l8;
                                    gm_tf5srh4mp4b3i t46;
                                    GM_ST(uint8_t, (uint8_t *)&t46 + 9, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t46 + 10, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t46 + 12, 0);
                                    GM_ST(uint64_t, ((uint8_t *)&t46), t45);
                                    ((uint8_t *)&t46)[8] = 1;
                                    F.ret = t46;
                                    goto gm_fret;
                                }
gm_lb8:;
                            }
gm_lb7:;
                            goto gm_lb5;
                        }
gm_lb6:;
                    }
gm_lb5:;
                    void * t47 = gm_l0;
                    void * t48 = gm_l4;
                    gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t48) + 72));
                    gm_b16_8 t50;
                    GM_ST(void *, (uint8_t *)&t50, (void *)(gm_slpp558cs8j3v));
                    GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(35));
                    gm_b0_1 t51 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t47, t49, t50);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tf5srh4mp4b3i t52;
                    memset(&t52, 0, sizeof t52);
                    F.ret = t52;
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
    void * t53 = gm_l0;
    void * t54 = gm_l4;
    gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t54) + 72));
    gm_b16_8 t56;
    GM_ST(void *, (uint8_t *)&t56, (void *)(gm_shncvaenjonsu));
    GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(40));
    gm_b0_1 t57 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t53, t55, t56);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tf5srh4mp4b3i t58;
    memset(&t58, 0, sizeof t58);
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
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_bit_width_b7ofh2h(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bit_width_b7ofh2h F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint16_t gm_l2; /* bits */
    uint32_t gm_l3; /* id */
    void * gm_l4; /* info */
    gm_t88rcucg1b7uk gm_l6; /* u */
    gm_t88rcucg1b7uk gm_l7; /* tag */
    gm_b16_8 gm_l8; /* variants */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    GM_STACK_CHECK(&F, 539u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l12;
    uint8_t gm_l13;
    void * t19 = gm_l0;
    void * t20 = (void *)(((uint8_t *)t19) + 8);
    gm_t88rcucg1b7uk t21 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l12, t20, GM_CONV(gm_csguankko4lmhd, t21));
    {
        {
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
            uint8_t t23 = (uint8_t)(t22 == ((uint8_t)4ULL));
            if (!t23) {
                goto gm_lb2;
            }
            gm_tf5srh4mp4b3i t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t24), ((uint64_t)1ULL));
            ((uint8_t *)&t24)[8] = 1;
            F.ret = t24;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)0ULL));
            if (!t26) {
                goto gm_lb3;
            }
            uint16_t t27 = GM_LD(uint16_t, ((((uint8_t *)&gm_l12) + 8) + 2));
            gm_l2 = t27;
            uint16_t t28 = gm_l2;
            uint64_t t29 = (uint64_t)t28;
            gm_tf5srh4mp4b3i t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t30), t29);
            ((uint8_t *)&t30)[8] = 1;
            F.ret = t30;
            goto gm_fret;
        }
gm_lb3:;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)17ULL));
            if (!t32) {
                goto gm_lb4;
            }
            uint32_t t33 = GM_LD(uint32_t, (((uint8_t *)&gm_l12) + 8));
            gm_l3 = t33;
            void * t34 = gm_l0;
            void * t35 = GM_LD(void *, (((uint8_t *)t34) + 1088));
            uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)t34) + 1088) + 8));
            uint32_t t37 = gm_l3;
            uint64_t t38 = (uint64_t)t37;
            if ((uintptr_t)t38 >= (uintptr_t)(t36)) gm_trap_index((uintptr_t)t38, t36, gm_src3k278vcmqgdr, 283, 22);
            void * t39 = (void *)(((uint8_t *)t35) + (uintptr_t)t38 * 184);
            gm_l4 = t39;
            void * t40 = gm_l4;
            {
                {
                    uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t40) + 24) + 0));
                    uint8_t t42 = (uint8_t)(t41 == ((uint8_t)0ULL));
                    if (!t42) {
                        goto gm_lb6;
                    }
                    memmove(((uint8_t *)&F.l5), ((((uint8_t *)t40) + 24) + 8), 48);
                    uint8_t t43 = (uint8_t)(((((uint8_t *)&F.l5) + 40))[4] != 0);
                    if (t43) {
                        gm_t88rcucg1b7uk t44 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 40));
                        gm_l6 = t44;
                    } else {
                        gm_tf5srh4mp4b3i t45;
                        memset(&t45, 0, sizeof t45);
                        F.ret = t45;
                        goto gm_fret;
                    }
                    void * t46 = gm_l0;
                    gm_t88rcucg1b7uk t47 = gm_l6;
                    gm_tf5srh4mp4b3i t48 = gm_f_bit_width_b7ofh2h(t46, GM_CONV(gm_csguankko4lmhd, t47));
                    F.ret = t48;
                    goto gm_fret;
                }
gm_lb6:;
                {
                    uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t40) + 24) + 0));
                    uint8_t t50 = (uint8_t)(t49 == ((uint8_t)1ULL));
                    if (!t50) {
                        goto gm_lb7;
                    }
                    gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t40) + 24) + 8));
                    gm_l7 = t51;
                    gm_b16_8 t52 = GM_LD(gm_b16_8, (((((uint8_t *)t40) + 24) + 8) + 8));
                    gm_l8 = t52;
                    void * t53 = gm_l4;
                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)t53) + 152));
                    uint8_t t55 = (uint8_t)!t54;
                    if (t55) {
                        gm_tf5srh4mp4b3i t56;
                        memset(&t56, 0, sizeof t56);
                        F.ret = t56;
                        goto gm_fret;
                    }
                    gm_b16_8 t57 = gm_l8;
                    gm_l9 = t57;
                    gm_l10 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t58 = gm_l10;
                            uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                            uint8_t t60 = (t58 < t59);
                            if (!t60) {
                                goto gm_lb8;
                            }
                            {
                                void * t61 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                uint64_t t63 = gm_l10;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l11), (((uint8_t *)t61) + (uintptr_t)t63 * 72), 72);
                                        {
                                            {
                                                uint8_t t64 = (uint8_t)(((((uint8_t *)&F.l11) + 16))[4] != 0);
                                                if (t64) {
                                                    goto gm_lb14;
                                                }
                                                gm_l13 = ((uint8_t)0);
                                                goto gm_lb13;
                                            }
gm_lb14:;
                                            {
                                                gm_l13 = ((uint8_t)1);
                                                goto gm_lb13;
                                            }
gm_lb15:;
                                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 293, 28);
                                        }
gm_lb13:;
                                        uint8_t t65 = gm_l13;
                                        if (t65) {
                                            gm_tf5srh4mp4b3i t66;
                                            memset(&t66, 0, sizeof t66);
                                            F.ret = t66;
                                            goto gm_fret;
                                        }
                                        goto gm_lb11;
                                    }
gm_lb12:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 292, 21);
                                }
gm_lb11:;
                            }
gm_lb10:;
                            uint64_t t67 = gm_l10;
                            uint64_t r68; bool o69 = __builtin_add_overflow(t67, ((uint64_t)1ULL), &r68);
                            uint64_t t70 = r68;
                            gm_l10 = t70;
                        }
                    }
gm_lb8:;
                    void * t71 = gm_l0;
                    gm_t88rcucg1b7uk t72 = gm_l7;
                    gm_tf5srh4mp4b3i t73 = gm_f_bit_width_b7ofh2h(t71, GM_CONV(gm_csguankko4lmhd, t72));
                    F.ret = t73;
                    goto gm_fret;
                }
gm_lb7:;
                {
                    gm_tf5srh4mp4b3i t74;
                    memset(&t74, 0, sizeof t74);
                    F.ret = t74;
                    goto gm_fret;
                }
gm_lb16:;
                gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 284, 13);
            }
gm_lb5:;
        }
gm_lb4:;
        {
            gm_tf5srh4mp4b3i t75;
            memset(&t75, 0, sizeof t75);
            F.ret = t75;
            goto gm_fret;
        }
gm_lb17:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 279, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_layout_u6kkccp F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* id */
    gm_t88rcucg1b7uk gm_l4; /* u */
    uint32_t gm_l5; /* off */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l10; /* w */
    GM_STACK_CHECK(&F, 415u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    {
        {
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 8);
            gm_t88rcucg1b7uk t13 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l11, t12, GM_CONV(gm_csguankko4lmhd, t13));
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
            uint8_t t15 = (uint8_t)(t14 == ((uint8_t)17ULL));
            if (!t15) {
                goto gm_lb2;
            }
            uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)&gm_l11) + 8));
            gm_l2 = t16;
            {
                {
                    {
                        void * t17 = gm_l0;
                        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 1088));
                        uint64_t t19 = GM_LD(uint64_t, ((((uint8_t *)t17) + 1088) + 8));
                        uint32_t t20 = gm_l2;
                        uint64_t t21 = (uint64_t)t20;
                        if ((uintptr_t)t21 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t21, t19, gm_src3k278vcmqgdr, 328, 20);
                        uint8_t t22 = GM_LD(uint8_t, (((((uint8_t *)t18) + (uintptr_t)t21 * 184) + 24) + 0));
                        uint8_t t23 = (uint8_t)(t22 == ((uint8_t)0ULL));
                        if (!t23) {
                            goto gm_lb4;
                        }
                        memmove(((uint8_t *)&F.l3), (((((uint8_t *)t18) + (uintptr_t)t21 * 184) + 24) + 8), 48);
                    }
                    uint8_t t24 = (uint8_t)(((((uint8_t *)&F.l3) + 40))[4] != 0);
                    if (t24) {
                        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l3) + 40));
                        gm_l4 = t25;
                    } else {
                        memset(&(*(gm_t57euh612ligr *)gm_out), 0, sizeof (*(gm_t57euh612ligr *)gm_out));
                        goto gm_fdone;
                    }
                    gm_l5 = ((uint32_t)0ULL);
                    {
                        gm_trqfvmjtt4sij t26;
                        gm_f_list_ol3ji66((void *)&t26, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l6 = t26;
                    }
                    gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                    gm_l7 = t27;
                    gm_l8 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t28 = gm_l8;
                            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            uint8_t t30 = (t28 < t29);
                            if (!t30) {
                                goto gm_lb5;
                            }
                            {
                                void * t31 = GM_LD(void *, ((uint8_t *)&gm_l7));
                                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                                uint64_t t33 = gm_l8;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l9), (((uint8_t *)t31) + (uintptr_t)t33 * 40), 40);
                                        void * t34 = gm_l0;
                                        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                                        uint32_t t36 = gm_f_field_bits_2ktfm1k((void *)NULL, cx, NULL, t34, t35);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l10 = t36;
                                        void * t37 = (void *)((uint8_t *)&F.l6);
                                        uint32_t t38 = gm_l5;
                                        uint32_t t39 = gm_l10;
                                        gm_t88rcucg1b7uk t40 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l9) + 16));
                                        gm_tov4mtrfp5e7v t41;
                                        GM_ST(uint32_t, ((uint8_t *)&t41 + 0), t38);
                                        GM_ST(uint32_t, ((uint8_t *)&t41 + 4), t39);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t41 + 8), t40);
                                        gm_tftb1odc739t5 t42 = gm_f_push_p52mcnt(t37, GM_CONV(gm_csv4kcdnuj265c, t41));
                                        gm_l12 = t42;
                                        uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                        if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_src3k278vcmqgdr, 334, 9);
                                        uint32_t t44 = gm_l5;
                                        uint32_t t45 = gm_l10;
                                        uint32_t r46; bool o47 = __builtin_add_overflow(t44, t45, &r46);
                                        if (o47) gm_trap("integer overflow in `+`", gm_src3k278vcmqgdr, 335, 9);
                                        uint32_t t48 = r46;
                                        gm_l5 = t48;
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 332, 5);
                                }
gm_lb8:;
                            }
gm_lb7:;
                            uint64_t t49 = gm_l8;
                            uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                            uint64_t t52 = r50;
                            gm_l8 = t52;
                        }
                    }
gm_lb5:;
                    {
                        gm_t88rcucg1b7uk t53 = gm_l4;
                        gm_b16_8 t54 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                        gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                        gm_tcmkompbl09il t56;
                        GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t56 + 0), t53);
                        GM_ST(gm_b16_8, ((uint8_t *)&t56 + 8), t55);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t57euh612ligr *)gm_out) + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t57euh612ligr *)gm_out) + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t57euh612ligr *)gm_out) + 28, 0);
                        GM_ST(gm_tcmkompbl09il, ((uint8_t *)&(*(gm_t57euh612ligr *)gm_out)), t56);
                        ((uint8_t *)&(*(gm_t57euh612ligr *)gm_out))[24] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb4:;
                memset(&(*(gm_t57euh612ligr *)gm_out), 0, sizeof (*(gm_t57euh612ligr *)gm_out));
                goto gm_fdone;
            }
gm_lb3:;
        }
gm_lb2:;
        memset(&(*(gm_t57euh612ligr *)gm_out), 0, sizeof (*(gm_t57euh612ligr *)gm_out));
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
inline __attribute__((aligned(4))) void gm_f_c_union_fields_btdvc8b(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_c_union_fields_btdvc8b F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* id */
    gm_b16_8 gm_l3; /* fields */
    GM_STACK_CHECK_SHALLOW(&F, 184u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l4;
    {
        {
            void * t6 = gm_l0;
            void * t7 = (void *)(((uint8_t *)t6) + 8);
            gm_t88rcucg1b7uk t8 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l4, t7, GM_CONV(gm_csguankko4lmhd, t8));
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)17ULL));
            if (!t10) {
                goto gm_lb2;
            }
            uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
            gm_l2 = t11;
            {
                {
                    void * t12 = gm_l0;
                    void * t13 = GM_LD(void *, (((uint8_t *)t12) + 1088));
                    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 1088) + 8));
                    uint32_t t15 = gm_l2;
                    uint64_t t16 = (uint64_t)t15;
                    if ((uintptr_t)t16 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t16, t14, gm_src3k278vcmqgdr, 503, 24);
                    uint8_t t17 = GM_LD(uint8_t, (((((uint8_t *)t13) + (uintptr_t)t16 * 184) + 24) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)2ULL));
                    if (!t18) {
                        goto gm_lb4;
                    }
                    gm_b16_8 t19 = GM_LD(gm_b16_8, (((((uint8_t *)t13) + (uintptr_t)t16 * 184) + 24) + 8));
                    gm_l3 = t19;
                    gm_b16_8 t20 = gm_l3;
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t20);
                    ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                    goto gm_fdone;
                }
gm_lb4:;
                memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                goto gm_fdone;
            }
gm_lb3:;
        }
gm_lb2:;
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
gm_lb1:;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_c_union_fields_btdvc8b_c(void * a0, gm_csguankko4lmhd a1) { gm_th2lqbo77pvsc gm_r; gm_f_c_union_fields_btdvc8b(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_check_bits_struct_ophej5b(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_check_bits_struct_ophej5b F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* underlying */
    gm_b16_8 gm_l3; /* fields */
    gm_b16_8 gm_l4; /* msg */
    uint8_t gm_l5; /* signed */
    uint16_t gm_l6; /* width */
    uint64_t gm_l7; /* total */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l11; /* w */
    GM_STACK_CHECK(&F, 795u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l12;
    gm_t53at80pfdus3 gm_l13;
    gm_tf5srh4mp4b3i gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tabb710d9mq2e gm_l16;
    void * t11 = gm_l0;
    gm_t88rcucg1b7uk t12 = gm_l2;
    gm_b16_8 t13 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t14;
    GM_ST(gm_b16_8, ((uint8_t *)&t14 + 0), t13);
    gm_f_format_a5p8et1((void *)&gm_l12, (void *)NULL, cx, NULL, t14);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 8, gm_src3k278vcmqgdr, 259, 12);
    gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
    gm_l4 = t16;
    {
        {
            void * t17 = gm_l0;
            void * t18 = (void *)(((uint8_t *)t17) + 8);
            gm_t88rcucg1b7uk t19 = gm_l2;
            gm_f_kind_jcnt4hm((void *)&gm_l13, t18, GM_CONV(gm_csguankko4lmhd, t19));
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)0ULL));
            if (!t21) {
                goto gm_lb2;
            }
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 8));
            gm_l5 = t22;
            uint16_t t23 = GM_LD(uint16_t, ((((uint8_t *)&gm_l13) + 8) + 2));
            gm_l6 = t23;
            uint8_t t24 = gm_l5;
            if (t24) {
                void * t25 = gm_l0;
                gm_tl874t2atnlh3 t26 = gm_l1;
                gm_b16_8 t27 = gm_l4;
                gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
                gm_b0_1 t29 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t25, t26, t28);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            gm_l7 = ((uint64_t)0ULL);
            gm_b16_8 t30 = gm_l3;
            gm_l8 = t30;
            gm_l9 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t31 = gm_l9;
                    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint8_t t33 = (t31 < t32);
                    if (!t33) {
                        goto gm_lb3;
                    }
                    {
                        void * t34 = GM_LD(void *, ((uint8_t *)&gm_l8));
                        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                        uint64_t t36 = gm_l9;
                        {
                            {
                                memmove(((uint8_t *)&F.l10), (((uint8_t *)t34) + (uintptr_t)t36 * 40), 40);
                                {
                                    {
                                        void * t37 = gm_l0;
                                        gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 16));
                                        gm_tf5srh4mp4b3i t39 = gm_f_bit_width_b7ofh2h(t37, GM_CONV(gm_csguankko4lmhd, t38));
                                        gm_l14 = t39;
                                        uint8_t t40 = (uint8_t)((((uint8_t *)&gm_l14))[8] != 0);
                                        if (!t40) {
                                            goto gm_lb9;
                                        }
                                        uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&gm_l14));
                                        gm_l11 = t41;
                                        uint64_t t42 = gm_l7;
                                        uint64_t t43 = gm_l11;
                                        uint64_t r44; bool o45 = __builtin_add_overflow(t42, t43, &r44);
                                        if (o45) gm_trap("integer overflow in `+`", gm_src3k278vcmqgdr, 268, 13);
                                        uint64_t t46 = r44;
                                        gm_l7 = t46;
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    void * t47 = gm_l0;
                                    gm_tl874t2atnlh3 t48 = gm_l1;
                                    gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                    gm_tnpifv20irnph t50;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t50 + 0), t49);
                                    gm_f_format_bc9gr0b((void *)&gm_l15, (void *)NULL, cx, NULL, t50);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                                    if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_src3k278vcmqgdr, 270, 27);
                                    gm_b16_8 t52 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                                    gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
                                    gm_b0_1 t54 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t47, t48, t53);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 266, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t55 = gm_l9;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l9 = t58;
                }
            }
gm_lb3:;
            uint64_t t59 = gm_l7;
            uint16_t t60 = gm_l6;
            uint64_t t61 = (uint64_t)t60;
            uint8_t t62 = (!(t59 == t61));
            if (t62) {
                void * t63 = gm_l0;
                gm_tl874t2atnlh3 t64 = gm_l1;
                uint64_t t65 = gm_l7;
                void * t66 = gm_l0;
                gm_t88rcucg1b7uk t67 = gm_l2;
                gm_b16_8 t68 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t66, t67);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint16_t t69 = gm_l6;
                gm_t3568dh2osse0 t70;
                GM_ST(uint16_t, (uint8_t *)&t70 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t70 + 28, 0);
                GM_ST(uint64_t, ((uint8_t *)&t70 + 0), t65);
                GM_ST(gm_b16_8, ((uint8_t *)&t70 + 8), t68);
                GM_ST(uint16_t, ((uint8_t *)&t70 + 24), t69);
                gm_f_format_c5ago2c((void *)&gm_l16, (void *)NULL, cx, NULL, &t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_src3k278vcmqgdr, 274, 23);
                gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                gm_b0_1 t74 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t63, t64, t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto gm_lb1;
        }
gm_lb2:;
        void * t75 = gm_l0;
        gm_tl874t2atnlh3 t76 = gm_l1;
        gm_b16_8 t77 = gm_l4;
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
        gm_b0_1 t79 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t75, t76, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) uint8_t gm_f_contains_by_value_pcdi4kq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2, void * a3) {
    struct gm_fr_contains_by_value_pcdi4kq F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* target_ */
    void * gm_l3; /* seen */
    uint32_t gm_l4; /* id */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_t88rcucg1b7uk gm_l10; /* elem */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    gm_b16_8 gm_l13; /* elems */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t88rcucg1b7uk gm_l16; /* e */
    gm_t88rcucg1b7uk gm_l17; /* err */
    gm_t88rcucg1b7uk gm_l18; /* ok */
    GM_STACK_CHECK(&F, 641u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tkn42u7na3t81 gm_l21;
    uint8_t gm_l22;
    void * t24 = gm_l0;
    void * t25 = (void *)(((uint8_t *)t24) + 8);
    gm_t88rcucg1b7uk t26 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l19, t25, GM_CONV(gm_csguankko4lmhd, t26));
    {
        {
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t28 = (uint8_t)(t27 == ((uint8_t)17ULL));
            if (!t28) {
                goto gm_lb2;
            }
            uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)&gm_l19) + 8));
            gm_l4 = t29;
            {
                {
                    uint32_t t30 = gm_l4;
                    uint32_t t31 = gm_l2;
                    uint8_t t32 = ((t30 == t31));
                    if (!t32) {
                        goto gm_lb4;
                    }
                    void * t33 = gm_l3;
                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 8));
                    uint8_t t35 = (t34 > ((uint64_t)0ULL));
                    if (!t35) {
                        goto gm_lb4;
                    }
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
gm_lb4:;
            }
gm_lb3:;
            uint32_t t36 = gm_l4;
            void * t37 = gm_l3;
            gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)t37));
            uint8_t t39 = 0;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t38) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t38))) + i * 4, ((uint8_t *)&t36))) { t39 = 1; break; }
            uint8_t t40 = (uint8_t)(t39);
            if (t40) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            void * t41 = gm_l3;
            uint32_t t42 = gm_l4;
            gm_tftb1odc739t5 t43 = gm_f_push_im9d4ro(t41, t42);
            gm_l20 = t43;
            uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_src3k278vcmqgdr, 650, 13);
            {
                void * t45 = gm_l0;
                void * t46 = GM_LD(void *, (((uint8_t *)t45) + 1088));
                uint64_t t47 = GM_LD(uint64_t, ((((uint8_t *)t45) + 1088) + 8));
                uint32_t t48 = gm_l4;
                uint64_t t49 = (uint64_t)t48;
                if ((uintptr_t)t49 >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)t49, t47, gm_src3k278vcmqgdr, 651, 21);
                memmove(((uint8_t *)&F.l5), ((((uint8_t *)t46) + (uintptr_t)t49 * 184) + 24), 56);
            }
            gm_l7 = ((uint64_t)0ULL);
            {
                uint64_t t50 = gm_f_member_count_otu9tc0(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l5)));
                gm_l6 = t50;
            }
            {
                for (;;) {
                    uint64_t t51 = gm_l7;
                    uint64_t t52 = gm_l6;
                    uint8_t t53 = (t51 < t52);
                    if (!t53) {
                        goto gm_lb5;
                    }
                    {
                        {
                            {
                                uint64_t t54 = gm_l7;
                                gm_l8 = t54;
                                uint64_t t55 = gm_l8;
                                gm_tkn42u7na3t81 t56 = gm_f_member_ty_esvuguv(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l5)), t55);
                                gm_l21 = t56;
                                uint8_t t57 = (uint8_t)((((uint8_t *)&gm_l21))[4] != 0);
                                if (t57) {
                                    gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l21));
                                    gm_l9 = t58;
                                } else {
                                    goto gm_lb7;
                                }
                                void * t59 = gm_l0;
                                gm_t88rcucg1b7uk t60 = gm_l9;
                                uint32_t t61 = gm_l2;
                                void * t62 = gm_l3;
                                uint8_t t63 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t59, t60, t61, t62);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t63) {
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto gm_lb8;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 652, 13);
                        }
gm_lb8:;
                    }
gm_lb7:;
                    uint64_t t64 = gm_l7;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    uint64_t t67 = r65;
                    gm_l7 = t67;
                }
            }
gm_lb5:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                    uint8_t t69 = (uint8_t)(t68 == ((uint8_t)11ULL));
                    if (!t69) {
                        goto gm_lb12;
                    }
                    gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l19) + 8) + 8));
                    gm_l10 = t70;
                    goto gm_lb11;
                }
gm_lb12:;
                {
                    uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                    uint8_t t72 = (uint8_t)(t71 == ((uint8_t)20ULL));
                    if (!t72) {
                        goto gm_lb13;
                    }
                    gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l19) + 8) + 4));
                    gm_l10 = t73;
                    goto gm_lb11;
                }
gm_lb13:;
                {
                    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                    uint8_t t75 = (uint8_t)(t74 == ((uint8_t)13ULL));
                    if (!t75) {
                        goto gm_lb14;
                    }
                    gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l19) + 8));
                    gm_l10 = t76;
                    goto gm_lb11;
                }
gm_lb14:;
                goto gm_lb10;
            }
gm_lb11:;
            void * t77 = gm_l0;
            gm_t88rcucg1b7uk t78 = gm_l10;
            uint32_t t79 = gm_l2;
            void * t80 = gm_l3;
            uint8_t t81 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t77, t78, t79, t80);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t81;
            goto gm_fret;
        }
gm_lb10:;
        {
            uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t83 = (uint8_t)(t82 == ((uint8_t)12ULL));
            if (!t83) {
                goto gm_lb15;
            }
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
            gm_l13 = t84;
            gm_b16_8 t85 = gm_l13;
            gm_l14 = t85;
            gm_l15 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t86 = gm_l15;
                    uint64_t t87 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint8_t t88 = (t86 < t87);
                    if (!t88) {
                        goto gm_lb16;
                    }
                    {
                        void * t89 = GM_LD(void *, ((uint8_t *)&gm_l14));
                        uint64_t t90 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                        uint64_t t91 = gm_l15;
                        {
                            {
                                gm_t88rcucg1b7uk t92 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t89) + (uintptr_t)t91 * 4));
                                gm_l16 = t92;
                                void * t93 = gm_l0;
                                gm_t88rcucg1b7uk t94 = gm_l16;
                                uint32_t t95 = gm_l2;
                                void * t96 = gm_l3;
                                uint8_t t97 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t93, t94, t95, t96);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t97) {
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto gm_lb19;
                            }
gm_lb20:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 660, 13);
                        }
gm_lb19:;
                    }
gm_lb18:;
                    uint64_t t98 = gm_l15;
                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                    uint64_t t101 = r99;
                    gm_l15 = t101;
                }
            }
gm_lb16:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb15:;
        {
            uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t103 = (uint8_t)(t102 == ((uint8_t)14ULL));
            if (!t103) {
                goto gm_lb21;
            }
            gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l19) + 8));
            gm_l17 = t104;
            gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l19) + 8) + 4));
            gm_l18 = t105;
            void * t106 = gm_l0;
            gm_t88rcucg1b7uk t107 = gm_l17;
            uint32_t t108 = gm_l2;
            void * t109 = gm_l3;
            uint8_t t110 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t106, t107, t108, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t110) {
                gm_l22 = ((uint8_t)1);
            } else {
                void * t111 = gm_l0;
                gm_t88rcucg1b7uk t112 = gm_l18;
                uint32_t t113 = gm_l2;
                void * t114 = gm_l3;
                uint8_t t115 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t111, t112, t113, t114);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t115;
            }
            uint8_t t116 = gm_l22;
            F.ret = t116;
            goto gm_fret;
        }
gm_lb21:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb22:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 646, 5);
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
extern __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_iefkbkk(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_adt_in_74tov03(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_tf5srh4mp4b3i a2, gm_b16_8 a3, void * a4, uint8_t a5) {
    struct gm_fr_declare_adt_in_74tov03 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_tf5srh4mp4b3i gm_l2; /* decl */
    gm_b16_8 gm_l3; /* name */
    void * gm_l4; /* lit */
    uint8_t gm_l5; /* local */
    gm_b16_8 gm_l6; /* attrs */
    void * gm_l7; /* inner */
    gm_b16_8 gm_l8; /* a */
    void * gm_l9; /* i */
    uint32_t gm_l10; /* id */
    gm_t88rcucg1b7uk gm_l11; /* ty */
    void * gm_l12; /* st */
    void * gm_l13; /* u */
    gm_th2lqbo77pvsc gm_l14; /* params */
    gm_t66j1im9ip128 gm_l15; /* mark */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    gm_tnnjeerolos0a gm_l21; /* kind */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_t08olq97rscn3 gm_l24; /* n */
    uint32_t gm_l25; /* pid */
    uint64_t gm_l26; /* d */
    gm_b16_8 gm_l29; /* wheres */
    void * gm_l30; /* st */
    void * gm_l31; /* u */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    GM_STACK_CHECK(&F, 2838u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_tftb1odc739t5 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    gm_tftb1odc739t5 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tftb1odc739t5 gm_l41;
    uint8_t gm_l42;
    gm_tabb710d9mq2e gm_l43;
    {
        gm_b16_8 t35 = gm_f_empty_slice_atlcs4j();
        gm_l6 = t35;
        void * t36 = gm_l4;
        gm_l7 = t36;
        {
            {
                void * t37 = gm_l4;
                uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t37) + 8) + 0));
                uint8_t t39 = (uint8_t)(t38 == ((uint8_t)35ULL));
                if (!t39) {
                    goto gm_lb2;
                }
                gm_b16_8 t40 = GM_LD(gm_b16_8, ((((uint8_t *)t37) + 8) + 8));
                gm_l8 = t40;
                void * t41 = GM_LD(void *, (((((uint8_t *)t37) + 8) + 8) + 16));
                gm_l9 = t41;
                gm_b16_8 t42 = gm_l8;
                gm_l6 = t42;
                void * t43 = gm_l9;
                gm_l7 = t43;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        void * t44 = gm_l0;
        void * t45 = (void *)(((uint8_t *)t44) + 8);
        gm_b16_8 t46 = gm_l3;
        uint32_t t47 = gm_f_fresh_adt_u9rnf9f((void *)NULL, cx, NULL, t45, t46);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t47;
        void * t48 = gm_l0;
        void * t49 = (void *)(((uint8_t *)t48) + 1088);
        gm_b16_8 t50 = gm_l3;
        uint64_t t51 = gm_l1;
        gm_ti0kgh3pc3in2 t52;
        gm_f_empty_struct_b5i94nj((void *)&t52);
        void * t53 = gm_l4;
        gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t53) + 72));
        gm_tf5srh4mp4b3i t55 = gm_l2;
        gm_t6art7bprjiqb t56;
        gm_f_new_adt_info_5pl2srl((void *)&t56, t50, t51, &t52, GM_CONV(gm_cseahvg8v7p0vs, t54), t55);
        gm_tftb1odc739t5 t57 = gm_f_push_j84ih6c(t49, &t56);
        gm_l36 = t57;
        uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
        if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_src3k278vcmqgdr, 103, 5);
        void * t59 = gm_l0;
        void * t60 = (void *)(((uint8_t *)t59) + 8);
        uint32_t t61 = gm_l10;
        gm_b16_8 t62 = gm_f_empty_slice_fh7svmj();
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&t62));
        gm_t88rcucg1b7uk t64 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t60, t61, t63);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t64;
        uint8_t t65 = gm_l5;
        if (t65) {
            void * t66 = gm_l0;
            gm_b16_8 t67 = gm_l3;
            gm_t88rcucg1b7uk t68 = gm_l11;
            gm_b0_1 t69 = gm_f_push_type_alias_rr9mg3i((void *)NULL, cx, NULL, t66, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        void * t70 = gm_l7;
        {
            {
                uint8_t t71 = GM_LD(uint8_t, ((((uint8_t *)t70) + 8) + 0));
                uint8_t t72 = (uint8_t)(t71 == ((uint8_t)32ULL));
                if (!t72) {
                    goto gm_lb4;
                }
                void * t73 = GM_LD(void *, ((((uint8_t *)t70) + 8) + 8));
                gm_l12 = t73;
                void * t74 = gm_l12;
                memmove(((uint8_t *)&gm_l14), ((uint8_t *)t74), 24);
                goto gm_lb3;
            }
gm_lb4:;
            {
                uint8_t t75 = GM_LD(uint8_t, ((((uint8_t *)t70) + 8) + 0));
                uint8_t t76 = (uint8_t)(t75 == ((uint8_t)33ULL));
                if (!t76) {
                    goto gm_lb5;
                }
                void * t77 = GM_LD(void *, ((((uint8_t *)t70) + 8) + 8));
                gm_l13 = t77;
                void * t78 = gm_l13;
                memmove(((uint8_t *)&gm_l14), (((uint8_t *)t78) + 8), 24);
                goto gm_lb3;
            }
gm_lb5:;
            {
                memset(&gm_l14, 0, sizeof gm_l14);
                goto gm_lb3;
            }
gm_lb6:;
            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 108, 28);
        }
gm_lb3:;
    }
    {
        void * t79 = gm_l0;
        gm_f_scope_mark_i3b2mvo((void *)&gm_l15, t79);
        gm_trqfvmjtt4sij t80;
        gm_f_list_go2qg0b((void *)&t80, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l16 = t80;
        gm_trqfvmjtt4sij t81;
        gm_f_list_pmp06kq_2((void *)&t81, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l17 = t81;
        uint8_t t82 = (uint8_t)((((uint8_t *)&gm_l14))[16] != 0);
        if (t82) {
            gm_b16_8 t83 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
            gm_l18 = t83;
        } else {
            gm_b16_8 t84 = gm_f_empty_slice_ito0pap();
            gm_l18 = t84;
        }
        gm_l19 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t85 = gm_l19;
                uint64_t t86 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                uint8_t t87 = (t85 < t86);
                if (!t87) {
                    goto gm_lb7;
                }
                {
                    void * t88 = GM_LD(void *, ((uint8_t *)&gm_l18));
                    uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint64_t t90 = gm_l19;
                    {
                        {
                            memmove(((uint8_t *)&F.l20), (((uint8_t *)t88) + (uintptr_t)t90 * 56), 56);
                            void * t91 = gm_l0;
                            uint64_t t92 = gm_l1;
                            void * t93 = (void *)((uint8_t *)&F.l20);
                            gm_tnnjeerolos0a t94 = gm_f_param_kind_of_1jngakt((void *)NULL, cx, NULL, t91, t92, t93);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l21 = t94;
                            gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                            gm_l22 = t95;
                            gm_l23 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t96 = gm_l23;
                                    uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                    uint8_t t98 = (t96 < t97);
                                    if (!t98) {
                                        goto gm_lb12;
                                    }
                                    {
                                        void * t99 = GM_LD(void *, ((uint8_t *)&gm_l22));
                                        uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                                        uint64_t t101 = gm_l23;
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l24), (((uint8_t *)t99) + (uintptr_t)t101 * 32), 32);
                                                void * t102 = gm_l0;
                                                gm_b16_8 t103 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l24));
                                                gm_tnnjeerolos0a t104 = gm_l21;
                                                uint32_t t105 = gm_f_new_param_7010dt4((void *)NULL, cx, NULL, t102, t103, t104);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l25 = t105;
                                                void * t106 = (void *)((uint8_t *)&F.l16);
                                                uint32_t t107 = gm_l25;
                                                gm_tftb1odc739t5 t108 = gm_f_push_im9d4ro(t106, t107);
                                                gm_l37 = t108;
                                                uint8_t t109 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                                if (!t109) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_src3k278vcmqgdr, 120, 13);
                                                void * t110 = (void *)((uint8_t *)&F.l17);
                                                void * t111 = GM_LD(void *, (((uint8_t *)&F.l20) + 32));
                                                gm_tftb1odc739t5 t112 = gm_f_push_r2k6rjf_2(t110, t111);
                                                gm_l38 = t112;
                                                uint8_t t113 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                                                if (!t113) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_src3k278vcmqgdr, 121, 13);
                                                {
                                                    {
                                                        uint8_t t114 = GM_LD(uint8_t, (((uint8_t *)&gm_l21) + 0));
                                                        uint8_t t115 = (uint8_t)(t114 == ((uint8_t)2ULL));
                                                        if (!t115) {
                                                            goto gm_lb18;
                                                        }
                                                        void * t116 = gm_l0;
                                                        void * t117 = (void *)(((uint8_t *)t116) + 1648);
                                                        gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l24));
                                                        uint32_t t119 = gm_l25;
                                                        gm_tommf4j9upomc t120;
                                                        GM_ST(uint32_t, (uint8_t *)&t120 + 20, 0);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t120 + 0), t118);
                                                        GM_ST(uint32_t, ((uint8_t *)&t120 + 16), t119);
                                                        gm_tftb1odc739t5 t121 = gm_f_push_sp4si0f(t117, &t120);
                                                        gm_l39 = t121;
                                                        uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                                                        if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_src3k278vcmqgdr, 123, 30);
                                                        goto gm_lb17;
                                                    }
gm_lb18:;
                                                    {
                                                        void * t123 = gm_l0;
                                                        void * t124 = (void *)(((uint8_t *)t123) + 1608);
                                                        gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l24));
                                                        void * t126 = gm_l0;
                                                        void * t127 = (void *)(((uint8_t *)t126) + 8);
                                                        uint32_t t128 = gm_l25;
                                                        gm_t88rcucg1b7uk t129 = gm_f_param_n8kgsdm((void *)NULL, cx, NULL, t127, t128);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_tep2drsl9408b t130;
                                                        GM_ST(uint32_t, (uint8_t *)&t130 + 20, 0);
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t130 + 0), t125);
                                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t130 + 16), t129);
                                                        gm_tftb1odc739t5 t131 = gm_f_push_hv483nm(t124, &t130);
                                                        gm_l40 = t131;
                                                        uint8_t t132 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                                        if (!t132) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_src3k278vcmqgdr, 124, 22);
                                                        goto gm_lb17;
                                                    }
gm_lb19:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 122, 13);
                                                }
gm_lb17:;
                                                goto gm_lb15;
                                            }
gm_lb16:;
                                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 118, 9);
                                        }
gm_lb15:;
                                    }
gm_lb14:;
                                    uint64_t t133 = gm_l23;
                                    uint64_t r134; bool o135 = __builtin_add_overflow(t133, ((uint64_t)1ULL), &r134);
                                    uint64_t t136 = r134;
                                    gm_l23 = t136;
                                }
                            }
gm_lb12:;
                            goto gm_lb10;
                        }
gm_lb11:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 116, 5);
                    }
gm_lb10:;
                }
gm_lb9:;
                uint64_t t137 = gm_l19;
                uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                uint64_t t140 = r138;
                gm_l19 = t140;
            }
        }
gm_lb7:;
    }
    {
        void * t141 = gm_l0;
        void * t142 = GM_LD(void *, (((uint8_t *)t141) + 1088));
        uint64_t t143 = GM_LD(uint64_t, ((((uint8_t *)t141) + 1088) + 8));
        uint32_t t144 = gm_l10;
        uint64_t t145 = (uint64_t)t144;
        if ((uintptr_t)t145 >= (uintptr_t)(t143)) gm_trap_index((uintptr_t)t145, t143, gm_src3k278vcmqgdr, 128, 5);
        gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&F.l16));
        GM_ST(gm_b16_8, ((((uint8_t *)t142) + (uintptr_t)t145 * 184) + 120), t146);
        void * t147 = gm_l0;
        void * t148 = GM_LD(void *, (((uint8_t *)t147) + 1088));
        uint64_t t149 = GM_LD(uint64_t, ((((uint8_t *)t147) + 1088) + 8));
        uint32_t t150 = gm_l10;
        uint64_t t151 = (uint64_t)t150;
        if ((uintptr_t)t151 >= (uintptr_t)(t149)) gm_trap_index((uintptr_t)t151, t149, gm_src3k278vcmqgdr, 129, 5);
        gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
        GM_ST(gm_b16_8, ((((uint8_t *)t148) + (uintptr_t)t151 * 184) + 136), t152);
        {
            {
                uint8_t t153 = (uint8_t)((((uint8_t *)&gm_l2))[8] != 0);
                if (!t153) {
                    goto gm_lb21;
                }
                uint64_t t154 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
                gm_l26 = t154;
                uint64_t t155 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
                uint8_t t156 = ((t155 == ((uint64_t)0ULL)));
                if (t156) {
                    gm_t88rcucg1b7uk t157 = gm_l11;
                    memset(&F.l27, 0, sizeof F.l27);
                    GM_ST(uint8_t, ((uint8_t *)&F.l27 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&F.l27 + 8), t157);
                } else {
                    uint32_t t158 = gm_l10;
                    memset(&F.l27, 0, sizeof F.l27);
                    GM_ST(uint8_t, ((uint8_t *)&F.l27 + 0), ((uint8_t)1ULL));
                    GM_ST(uint32_t, ((uint8_t *)&F.l27 + 8), t158);
                }
                {
                    void * t159 = gm_l0;
                    uint64_t t160 = gm_l26;
                    gm_t6qooom8qa2do t161 = GM_LD(gm_t6qooom8qa2do, ((uint8_t *)&F.l27));
                    gm_tagrkbqh054h7 t162;
                    GM_ST(uint8_t, (uint8_t *)&t162 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t162 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)2ULL));
                    GM_ST(gm_t6qooom8qa2do, ((uint8_t *)&t162 + 8), t161);
                    gm_b0_1 t163 = gm_f_set_state_3j300fi((void *)NULL, cx, NULL, t159, t160, &t162);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto gm_lb20;
            }
gm_lb21:;
        }
gm_lb20:;
        void * t164 = gm_l0;
        void * t165 = (void *)(((uint8_t *)t164) + 1848);
        uint32_t t166 = gm_l10;
        gm_tftb1odc739t5 t167 = gm_f_push_im9d4ro(t165, t166);
        gm_l41 = t167;
        uint8_t t168 = (uint8_t)!((((uint8_t *)&gm_l41))[0] != 0);
        if (!t168) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l41) + 1, gm_src3k278vcmqgdr, 135, 5);
        void * t169 = gm_l0;
        void * t170 = gm_l0;
        uint32_t t171 = GM_LD(uint32_t, (((uint8_t *)t170) + 1888));
        uint32_t r172; bool o173 = __builtin_add_overflow(t171, ((uint32_t)1ULL), &r172);
        if (o173) gm_trap("integer overflow in `+`", gm_src3k278vcmqgdr, 136, 5);
        uint32_t t174 = r172;
        GM_ST(uint32_t, (((uint8_t *)t169) + 1888), t174);
        gm_f_empty_struct_b5i94nj((void *)&F.l28);
        gm_b16_8 t175 = gm_f_empty_slice_3jt8vgk();
        gm_l29 = t175;
        void * t176 = gm_l7;
        {
            {
                uint8_t t177 = GM_LD(uint8_t, ((((uint8_t *)t176) + 8) + 0));
                uint8_t t178 = (uint8_t)(t177 == ((uint8_t)32ULL));
                if (!t178) {
                    goto gm_lb23;
                }
                void * t179 = GM_LD(void *, ((((uint8_t *)t176) + 8) + 8));
                gm_l30 = t179;
                void * t180 = gm_l0;
                uint64_t t181 = gm_l1;
                uint32_t t182 = gm_l10;
                void * t183 = gm_l30;
                gm_b16_8 t184 = gm_l6;
                gm_b16_8 t185 = GM_LD(gm_b16_8, ((uint8_t *)&t184));
                gm_f_struct_kind_5l1hs46((void *)&F.l28, (void *)NULL, cx, NULL, t180, t181, t182, t183, t185);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t186 = gm_l30;
                gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)t186) + 24));
                gm_l29 = t187;
                goto gm_lb22;
            }
gm_lb23:;
            {
                uint8_t t188 = GM_LD(uint8_t, ((((uint8_t *)t176) + 8) + 0));
                uint8_t t189 = (uint8_t)(t188 == ((uint8_t)33ULL));
                if (!t189) {
                    goto gm_lb24;
                }
                void * t190 = GM_LD(void *, ((((uint8_t *)t176) + 8) + 8));
                gm_l31 = t190;
                void * t191 = gm_l31;
                {
                    {
                        uint8_t t192 = (uint8_t)(GM_LD(void *, ((uint8_t *)t191)) != 0);
                        if (t192) {
                            goto gm_lb26;
                        }
                        gm_l42 = ((uint8_t)0);
                        goto gm_lb25;
                    }
gm_lb26:;
                    {
                        gm_l42 = ((uint8_t)1);
                        goto gm_lb25;
                    }
gm_lb27:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 145, 16);
                }
gm_lb25:;
                uint8_t t193 = gm_l42;
                if (t193) {
                    void * t194 = gm_l0;
                    void * t195 = GM_LD(void *, (((uint8_t *)t194) + 1088));
                    uint64_t t196 = GM_LD(uint64_t, ((((uint8_t *)t194) + 1088) + 8));
                    uint32_t t197 = gm_l10;
                    uint64_t t198 = (uint64_t)t197;
                    if ((uintptr_t)t198 >= (uintptr_t)(t196)) gm_trap_index((uintptr_t)t198, t196, gm_src3k278vcmqgdr, 145, 41);
                    GM_ST(uint8_t, ((((uint8_t *)t195) + (uintptr_t)t198 * 184) + 152), ((uint8_t)1));
                }
                void * t199 = gm_l0;
                uint64_t t200 = gm_l1;
                void * t201 = gm_l31;
                gm_b16_8 t202 = gm_l6;
                gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)&t202));
                gm_f_union_kind_j2gcmto((void *)&F.l28, (void *)NULL, cx, NULL, t199, t200, t201, t203);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t204 = gm_l31;
                gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)t204) + 32));
                gm_l29 = t205;
                goto gm_lb22;
            }
gm_lb24:;
            {
                void * t206 = gm_l0;
                void * t207 = gm_l4;
                gm_tl874t2atnlh3 t208 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t207) + 72));
                gm_b16_8 t209;
                GM_ST(void *, (uint8_t *)&t209, (void *)(gm_s4ko774bb2ea9));
                GM_ST(uintptr_t, (uint8_t *)&t209 + 8, (uintptr_t)(55));
                gm_b0_1 t210 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t206, t208, t209);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t211 = gm_l0;
                uint32_t t212 = gm_l10;
                gm_f_remove_in_progress_lrie478(t211, t212);
                void * t213 = gm_l0;
                gm_b0_1 t214 = gm_f_end_constraint_defer_52jt3m9((void *)NULL, cx, NULL, t213);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t215 = gm_l0;
                void * t216 = (void *)(((uint8_t *)t215) + 8);
                gm_t88rcucg1b7uk t217 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t216);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t217;
                goto gm_fret;
            }
gm_lb28:;
            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 139, 5);
        }
gm_lb22:;
    }
    {
        void * t218 = gm_l0;
        uint32_t t219 = gm_l10;
        gm_f_remove_in_progress_lrie478(t218, t219);
        void * t220 = gm_l0;
        void * t221 = GM_LD(void *, (((uint8_t *)t220) + 1088));
        uint64_t t222 = GM_LD(uint64_t, ((((uint8_t *)t220) + 1088) + 8));
        uint32_t t223 = gm_l10;
        uint64_t t224 = (uint64_t)t223;
        if ((uintptr_t)t224 >= (uintptr_t)(t222)) gm_trap_index((uintptr_t)t224, t222, gm_src3k278vcmqgdr, 157, 5);
        memmove(((((uint8_t *)t221) + (uintptr_t)t224 * 184) + 24), ((uint8_t *)&F.l28), 56);
        gm_b16_8 t225 = gm_l29;
        gm_l32 = t225;
        gm_l33 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t226 = gm_l33;
                uint64_t t227 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                uint8_t t228 = (t226 < t227);
                if (!t228) {
                    goto gm_lb29;
                }
                {
                    void * t229 = GM_LD(void *, ((uint8_t *)&gm_l32));
                    uint64_t t230 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                    uint64_t t231 = gm_l33;
                    {
                        {
                            memmove(((uint8_t *)&F.l34), (((uint8_t *)t229) + (uintptr_t)t231 * 56), 56);
                            void * t232 = gm_l0;
                            uint64_t t233 = gm_l1;
                            void * t234 = (void *)((uint8_t *)&F.l34);
                            gm_b0_1 t235 = gm_f_declare_constraint_s1juggf((void *)NULL, cx, NULL, t232, t233, t234);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb32;
                        }
gm_lb33:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 158, 5);
                    }
gm_lb32:;
                }
gm_lb31:;
                uint64_t t236 = gm_l33;
                uint64_t r237; bool o238 = __builtin_add_overflow(t236, ((uint64_t)1ULL), &r237);
                uint64_t t239 = r237;
                gm_l33 = t239;
            }
        }
gm_lb29:;
        void * t240 = gm_l0;
        gm_b0_1 t241 = gm_f_end_constraint_defer_52jt3m9((void *)NULL, cx, NULL, t240);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t242 = gm_l0;
        gm_f_restore_scope_edqq1v8(t242, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l15)));
        uint64_t t243 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
        uint8_t t244 = (t243 > ((uint64_t)0ULL));
        if (t244) {
            gm_t88rcucg1b7uk t245 = gm_l11;
            F.ret = t245;
            goto gm_fret;
        }
        gm_trqfvmjtt4sij t246;
        gm_f_list_go2qg0b((void *)&t246, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l35 = t246;
        void * t247 = gm_l0;
        gm_t88rcucg1b7uk t248 = gm_l11;
        void * t249 = (void *)((uint8_t *)&F.l35);
        uint8_t t250 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t247, t248, t249);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t250) {
            void * t251 = gm_l0;
            void * t252 = gm_l4;
            gm_tl874t2atnlh3 t253 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t252) + 72));
            gm_b16_8 t254 = gm_l3;
            gm_tnpifv20irnph t255;
            GM_ST(gm_b16_8, ((uint8_t *)&t255 + 0), t254);
            gm_f_format_0au88qf((void *)&gm_l43, (void *)NULL, cx, NULL, t255);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t256 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
            if (!t256) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 8, gm_src3k278vcmqgdr, 164, 27);
            gm_b16_8 t257 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l43) + 8));
            gm_b16_8 t258 = GM_LD(gm_b16_8, ((uint8_t *)&t257));
            gm_b0_1 t259 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t251, t253, t258);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t260 = gm_l0;
            uint32_t t261 = gm_l10;
            gm_f_empty_adt_tdp3hro(t260, t261);
            void * t262 = gm_l0;
            void * t263 = (void *)(((uint8_t *)t262) + 8);
            gm_t88rcucg1b7uk t264 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t263);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t264;
            goto gm_fret;
        }
    }
    {
        void * t265 = gm_l0;
        gm_t88rcucg1b7uk t266 = gm_l11;
        void * t267 = gm_l4;
        gm_tl874t2atnlh3 t268 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t267) + 72));
        uint8_t t269 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t265, t266, t268);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t270 = (uint8_t)!t269;
        if (t270) {
            void * t271 = gm_l0;
            uint32_t t272 = gm_l10;
            gm_f_empty_adt_tdp3hro(t271, t272);
            void * t273 = gm_l0;
            void * t274 = (void *)(((uint8_t *)t273) + 8);
            gm_t88rcucg1b7uk t275 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t274);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t275;
            goto gm_fret;
        }
        gm_t88rcucg1b7uk t276 = gm_l11;
        F.ret = t276;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_declare_local_adt_6j4pdai(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_declare_local_adt_6j4pdai F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* name */
    void * gm_l3; /* lit */
    void * gm_l4; /* inner */
    void * gm_l5; /* i */
    void * gm_l6; /* st */
    void * gm_l7; /* u */
    uint8_t gm_l8; /* generic */
    gm_t88rcucg1b7uk gm_l9; /* ty */
    uint32_t gm_l10; /* id */
    GM_STACK_CHECK(&F, 590u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l11;
    uint8_t gm_l12;
    gm_t53at80pfdus3 gm_l13;
    gm_tabb710d9mq2e gm_l14;
    void * t16 = gm_l3;
    gm_l4 = t16;
    {
        {
            void * t17 = gm_l3;
            uint8_t t18 = GM_LD(uint8_t, ((((uint8_t *)t17) + 8) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)35ULL));
            if (!t19) {
                goto gm_lb2;
            }
            void * t20 = GM_LD(void *, (((((uint8_t *)t17) + 8) + 8) + 16));
            gm_l5 = t20;
            void * t21 = gm_l5;
            gm_l4 = t21;
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t22 = gm_l4;
    {
        {
            uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t24 = (uint8_t)(t23 == ((uint8_t)32ULL));
            if (!t24) {
                goto gm_lb4;
            }
            void * t25 = GM_LD(void *, ((((uint8_t *)t22) + 8) + 8));
            gm_l6 = t25;
            void * t26 = gm_l6;
            {
                {
                    uint8_t t27 = (uint8_t)((((uint8_t *)t26))[16] != 0);
                    if (t27) {
                        goto gm_lb6;
                    }
                    gm_l11 = ((uint8_t)0);
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    gm_l11 = ((uint8_t)1);
                    goto gm_lb5;
                }
gm_lb7:;
                gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 53, 24);
            }
gm_lb5:;
            uint8_t t28 = gm_l11;
            gm_l8 = t28;
            goto gm_lb3;
        }
gm_lb4:;
        {
            uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
            uint8_t t30 = (uint8_t)(t29 == ((uint8_t)33ULL));
            if (!t30) {
                goto gm_lb8;
            }
            void * t31 = GM_LD(void *, ((((uint8_t *)t22) + 8) + 8));
            gm_l7 = t31;
            void * t32 = gm_l7;
            {
                {
                    uint8_t t33 = (uint8_t)(((((uint8_t *)t32) + 8))[16] != 0);
                    if (t33) {
                        goto gm_lb10;
                    }
                    gm_l12 = ((uint8_t)0);
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    gm_l12 = ((uint8_t)1);
                    goto gm_lb9;
                }
gm_lb11:;
                gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 54, 22);
            }
gm_lb9:;
            uint8_t t34 = gm_l12;
            gm_l8 = t34;
            goto gm_lb3;
        }
gm_lb8:;
        {
            gm_l8 = ((uint8_t)0);
            goto gm_lb3;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 52, 16);
    }
gm_lb3:;
    uint8_t t35 = gm_l8;
    if (t35) {
        void * t36 = gm_l0;
        void * t37 = gm_l3;
        gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t37) + 72));
        gm_b16_8 t39;
        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sf7l7ca9coehi));
        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(84));
        gm_b0_1 t40 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t36, t38, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t41 = gm_l0;
        gm_b16_8 t42 = gm_l2;
        void * t43 = gm_l0;
        void * t44 = (void *)(((uint8_t *)t43) + 8);
        gm_t88rcucg1b7uk t45 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b0_1 t46 = gm_f_push_type_alias_rr9mg3i((void *)NULL, cx, NULL, t41, t42, t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t47 = gm_l0;
        void * t48 = (void *)(((uint8_t *)t47) + 8);
        gm_t88rcucg1b7uk t49 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t49;
        goto gm_fret;
    }
    void * t50 = gm_l0;
    uint64_t t51 = gm_l1;
    gm_tf5srh4mp4b3i t52;
    memset(&t52, 0, sizeof t52);
    gm_b16_8 t53 = gm_l2;
    void * t54 = gm_l3;
    gm_t88rcucg1b7uk t55 = gm_f_declare_adt_in_74tov03((void *)NULL, cx, NULL, t50, t51, t52, t53, t54, ((uint8_t)1));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l9 = t55;
    {
        {
            {
                void * t56 = gm_l0;
                void * t57 = (void *)(((uint8_t *)t56) + 8);
                gm_t88rcucg1b7uk t58 = gm_l9;
                gm_f_kind_jcnt4hm((void *)&gm_l13, t57, GM_CONV(gm_csguankko4lmhd, t58));
            }
            uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&gm_l13) + 0));
            uint8_t t60 = (uint8_t)(t59 == ((uint8_t)17ULL));
            if (!t60) {
                goto gm_lb14;
            }
            uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 8));
            gm_l10 = t61;
            void * t62 = gm_l0;
            uint32_t t63 = gm_l10;
            uint8_t t64 = gm_f_adt_mentions_param_0jsjgma(t62, t63);
            if (!t64) {
                goto gm_lb14;
            }
            {
                void * t65 = gm_l0;
                void * t66 = gm_l3;
                gm_tl874t2atnlh3 t67 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t66) + 72));
                gm_b16_8 t68 = gm_l2;
                gm_tnpifv20irnph t69;
                GM_ST(gm_b16_8, ((uint8_t *)&t69 + 0), t68);
                gm_f_format_otdvqpt((void *)&gm_l14, (void *)NULL, cx, NULL, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_src3k278vcmqgdr, 64, 27);
                gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
                gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                gm_b0_1 t73 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t65, t67, t72);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            void * t74 = gm_l0;
            uint32_t t75 = gm_l10;
            gm_f_empty_adt_tdp3hro(t74, t75);
            void * t76 = gm_l0;
            void * t77 = (void *)(((uint8_t *)t76) + 8);
            gm_t88rcucg1b7uk t78 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t78;
            goto gm_fret;
        }
gm_lb14:;
    }
gm_lb13:;
    gm_t88rcucg1b7uk t79 = gm_l9;
    F.ret = t79;
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
inline __attribute__((aligned(4))) void gm_f_empty_adt_tdp3hro(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_adt_tdp3hro F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 244u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 1088));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 1088) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src3k278vcmqgdr, 182, 5);
    gm_ti0kgh3pc3in2 t7;
    gm_f_empty_struct_b5i94nj((void *)&t7);
    GM_ST(gm_ti0kgh3pc3in2, ((((uint8_t *)t3) + (uintptr_t)t6 * 184) + 24), t7);
    void * t8 = gm_l0;
    void * t9 = GM_LD(void *, (((uint8_t *)t8) + 1088));
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t8) + 1088) + 8));
    uint32_t t11 = gm_l1;
    uint64_t t12 = (uint64_t)t11;
    if ((uintptr_t)t12 >= (uintptr_t)(t10)) gm_trap_index((uintptr_t)t12, t10, gm_src3k278vcmqgdr, 183, 5);
    GM_ST(uint8_t, ((((uint8_t *)t9) + (uintptr_t)t12 * 184) + 112), ((uint8_t)0));
    void * t13 = gm_l0;
    void * t14 = GM_LD(void *, (((uint8_t *)t13) + 1088));
    uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)t13) + 1088) + 8));
    uint32_t t16 = gm_l1;
    uint64_t t17 = (uint64_t)t16;
    if ((uintptr_t)t17 >= (uintptr_t)(t15)) gm_trap_index((uintptr_t)t17, t15, gm_src3k278vcmqgdr, 184, 5);
    gm_b16_8 t18 = gm_f_empty_slice_s1lbe5s();
    GM_ST(gm_b16_8, ((((uint8_t *)t14) + (uintptr_t)t17 * 184) + 80), t18);
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, (((uint8_t *)t19) + 1088));
    uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)t19) + 1088) + 8));
    uint32_t t22 = gm_l1;
    uint64_t t23 = (uint64_t)t22;
    if ((uintptr_t)t23 >= (uintptr_t)(t21)) gm_trap_index((uintptr_t)t23, t21, gm_src3k278vcmqgdr, 185, 5);
    gm_b16_8 t24 = gm_f_empty_slice_8q3kspo_2();
    GM_ST(gm_b16_8, ((((uint8_t *)t20) + (uintptr_t)t23 * 184) + 96), t24);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
extern __attribute__((aligned(4))) void gm_f_empty_struct_b5i94nj(void *gm_out);
__attribute__((aligned(4))) gm_ti0kgh3pc3in2 gm_f_empty_struct_b5i94nj_c(void) { gm_ti0kgh3pc3in2 gm_r; gm_f_empty_struct_b5i94nj(&gm_r); return gm_r; }
inline __attribute__((aligned(4))) uint32_t gm_f_field_bits_2ktfm1k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_field_bits_2ktfm1k F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* w */
    GM_STACK_CHECK(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tf5srh4mp4b3i gm_l3;
    gm_tbccvikg6b96e gm_l4;
    {
        {
            void * t4 = gm_l0;
            gm_t88rcucg1b7uk t5 = gm_l1;
            gm_tf5srh4mp4b3i t6 = gm_f_bit_width_b7ofh2h(t4, GM_CONV(gm_csguankko4lmhd, t5));
            gm_l3 = t6;
            uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l3))[8] != 0);
            if (!t7) {
                goto gm_lb2;
            }
            uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)&gm_l3));
            gm_l2 = t8;
            uint64_t t9 = gm_l2;
            uint32_t t10 = (uint32_t)t9;
            F.ret = t10;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t11 = gm_l0;
    gm_t88rcucg1b7uk t12 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&gm_l4, (void *)NULL, cx, NULL, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&gm_l4));
    uint64_t r14; bool o15 = __builtin_mul_overflow(((uint64_t)8ULL), t13, &r14);
    if (o15) gm_trap("integer overflow in `*`", gm_src3k278vcmqgdr, 308, 13);
    uint64_t t16 = r14;
    uint32_t t17 = (uint32_t)t16;
    F.ret = t17;
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_flatten_2js7j92(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1) {
    struct gm_fr_flatten_2js7j92 F;
    void * gm_l0; /* s */
    gm_tnpifv20irnph gm_l1; /* set */
    uint8_t gm_l2; /* spliced */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tiilmfd16v3fc gm_l6; /* e */
    gm_t88rcucg1b7uk gm_l7; /* t */
    gm_tnpifv20irnph gm_l8; /* inner */
    GM_STACK_CHECK(&F, 476u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t53at80pfdus3 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_l2 = ((uint8_t)0);
    gm_trqfvmjtt4sij t11;
    gm_f_list_084fcd6((void *)&t11, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t11;
    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
    gm_l4 = t12;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t13 = gm_l5;
            uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                void * t16 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t18 = gm_l5;
                {
                    {
                        {
                            memmove(((uint8_t *)&gm_l6), (((uint8_t *)t16) + (uintptr_t)t18 * 24), 24);
                        }
                        {
                            {
                                uint32_t t19 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
                                uint8_t t20 = ((t19 == ((uint32_t)0ULL)));
                                if (!t20) {
                                    goto gm_lb7;
                                }
                                uint64_t t21 = GM_LD(uint64_t, ((((uint8_t *)&gm_l6) + 8) + 8));
                                uint8_t t22 = (t21 > ((uint64_t)0ULL));
                                if (!t22) {
                                    goto gm_lb7;
                                }
                                {
                                    {
                                        void * t23 = GM_LD(void *, (((uint8_t *)&gm_l6) + 8));
                                        uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)&gm_l6) + 8) + 8));
                                        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t23) + (uintptr_t)((uint64_t)0ULL) * 40) + 0));
                                        uint8_t t26 = (uint8_t)(t25 == ((uint8_t)0ULL));
                                        if (!t26) {
                                            goto gm_lb9;
                                        }
                                        gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t23) + (uintptr_t)((uint64_t)0ULL) * 40) + 8));
                                        gm_l7 = t27;
                                        void * t28 = gm_l0;
                                        void * t29 = (void *)(((uint8_t *)t28) + 8);
                                        gm_t88rcucg1b7uk t30 = gm_l7;
                                        gm_f_kind_jcnt4hm((void *)&gm_l9, t29, GM_CONV(gm_csguankko4lmhd, t30));
                                        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l9) + 0));
                                        uint8_t t32 = (uint8_t)(t31 == ((uint8_t)24ULL));
                                        if (!t32) {
                                            goto gm_lb9;
                                        }
                                        gm_tnpifv20irnph t33 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&gm_l9) + 8));
                                        gm_l8 = t33;
                                        void * t34 = (void *)((uint8_t *)&F.l3);
                                        gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l8));
                                        gm_tftb1odc739t5 t36 = gm_f_extend_74ac4hb(t34, t35);
                                        gm_l10 = t36;
                                        uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                                        if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_src3k278vcmqgdr, 781, 17);
                                        gm_l2 = ((uint8_t)1);
                                        goto gm_lb3;
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        {
                            void * t38 = (void *)((uint8_t *)&F.l3);
                            gm_tftb1odc739t5 t39 = gm_f_push_d1r4h19(t38, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l6)));
                            gm_l11 = t39;
                        }
                        uint8_t t40 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t40) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_src3k278vcmqgdr, 786, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 778, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t41 = gm_l5;
            uint64_t r42; bool o43 = __builtin_add_overflow(t41, ((uint64_t)1ULL), &r42);
            uint64_t t44 = r42;
            gm_l5 = t44;
        }
    }
gm_lb1:;
    uint8_t t45 = gm_l2;
    uint8_t t46 = (uint8_t)!t45;
    if (t46) {
        gm_tnpifv20irnph t47 = gm_l1;
        F.ret = t47;
        goto gm_fret;
    }
    gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
    gm_tnpifv20irnph t50 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t49);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t50;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_instance_args_15ek7cf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_instance_args_15ek7cf F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* inst */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t5g5vgung6h6s gm_l5; /* i */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l8; /* a */
    GM_STACK_CHECK(&F, 408u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l9;
    gm_trqfvmjtt4sij t12;
    gm_f_list_vmvl81b((void *)&t12, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l2 = t12;
    void * t13 = gm_l0;
    gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 1168));
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
                        memmove(((uint8_t *)&gm_l5), (((uint8_t *)t18) + (uintptr_t)t20 * 32), 32);
                        uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 24));
                        uint32_t t22 = gm_l1;
                        uint8_t t23 = ((t21 == t22));
                        if (t23) {
                            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                            gm_l6 = t24;
                            gm_l7 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t25 = gm_l7;
                                    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                    uint8_t t27 = (t25 < t26);
                                    if (!t27) {
                                        goto gm_lb6;
                                    }
                                    {
                                        void * t28 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                        uint64_t t30 = gm_l7;
                                        {
                                            {
                                                gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t28) + (uintptr_t)t30 * 4));
                                                gm_l8 = t31;
                                                void * t32 = (void *)((uint8_t *)&F.l2);
                                                gm_t88rcucg1b7uk t33 = gm_l8;
                                                gm_toa2vhe3l4hfr t34;
                                                memset(&t34, 0, sizeof t34);
                                                GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t34 + 8), t33);
                                                gm_tftb1odc739t5 t35 = gm_f_push_s5vr2a2(t32, &t34);
                                                gm_l9 = t35;
                                                uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                                if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_src3k278vcmqgdr, 631, 31);
                                                goto gm_lb9;
                                            }
gm_lb10:;
                                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 631, 13);
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
                            goto gm_lb1;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 629, 5);
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
    gm_b16_8 t45 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
    F.ret = t45;
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_instantiate_adt_lbjcui1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2) {
    struct gm_fr_instantiate_adt_lbjcui1 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* template */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* a */
    uint32_t gm_l7; /* inst */
    uint32_t gm_l9; /* inst */
    gm_t88rcucg1b7uk gm_l11; /* ty */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint32_t gm_l17; /* p */
    gm_t88rcucg1b7uk gm_l19; /* tag */
    gm_b16_8 gm_l20; /* variants */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_tkn42u7na3t81 gm_l25; /* payload */
    gm_t88rcucg1b7uk gm_l26; /* p */
    gm_b16_8 gm_l27; /* fields */
    uint8_t gm_l29; /* concrete */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    gm_t88rcucg1b7uk gm_l32; /* a */
    gm_b16_8 gm_l35; /* <tmp> */
    uint64_t gm_l36; /* <tmp> */
    gm_t5g5vgung6h6s gm_l37; /* i */
    gm_t88rcucg1b7uk gm_l39; /* t */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    gm_t5g5vgung6h6s gm_l42; /* i */
    gm_t88rcucg1b7uk gm_l43; /* t */
    gm_b16_8 gm_l44; /* <tmp> */
    uint64_t gm_l45; /* <tmp> */
    gm_t5g5vgung6h6s gm_l46; /* i */
    gm_t88rcucg1b7uk gm_l47; /* t */
    GM_STACK_CHECK(&F, 3640u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l48;
    gm_t9805qtonbsag gm_l49;
    gm_tabb710d9mq2e gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tftb1odc739t5 gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_tftb1odc739t5 gm_l54;
    gm_tn68el8d14vku gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_tftb1odc739t5 gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_tftb1odc739t5 gm_l59;
    gm_tabb710d9mq2e gm_l60;
    {
        gm_trqfvmjtt4sij t45;
        gm_f_list_vmvl81b((void *)&t45, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t45;
        gm_b16_8 t46 = gm_l2;
        gm_l4 = t46;
        gm_l5 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t47 = gm_l5;
                uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint8_t t49 = (t47 < t48);
                if (!t49) {
                    goto gm_lb1;
                }
                {
                    void * t50 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t52 = gm_l5;
                    {
                        {
                            gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t50) + (uintptr_t)t52 * 4));
                            gm_l6 = t53;
                            void * t54 = (void *)((uint8_t *)&F.l3);
                            gm_t88rcucg1b7uk t55 = gm_l6;
                            gm_toa2vhe3l4hfr t56;
                            memset(&t56, 0, sizeof t56);
                            GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)0ULL));
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t56 + 8), t55);
                            gm_tftb1odc739t5 t57 = gm_f_push_s5vr2a2(t54, &t56);
                            gm_l48 = t57;
                            uint8_t t58 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                            if (!t58) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_src3k278vcmqgdr, 549, 21);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 549, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t59 = gm_l5;
                uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                uint64_t t62 = r60;
                gm_l5 = t62;
            }
        }
gm_lb1:;
        {
            {
                void * t63 = gm_l0;
                void * t64 = (void *)(((uint8_t *)t63) + 1208);
                uint32_t t65 = gm_l1;
                gm_b16_8 t66 = gm_l2;
                gm_tiilmfd16v3fc t67;
                GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t67 + 0), t65);
                GM_ST(gm_b16_8, ((uint8_t *)&t67 + 8), t66);
                gm_t9805qtonbsag t68 = gm_f_get_6qnso5a(t64, &t67);
                gm_l49 = t68;
                uint8_t t69 = (uint8_t)((((uint8_t *)&gm_l49))[4] != 0);
                if (!t69) {
                    goto gm_lb7;
                }
                uint32_t t70 = GM_LD(uint32_t, ((uint8_t *)&gm_l49));
                gm_l7 = t70;
                void * t71 = gm_l0;
                void * t72 = (void *)(((uint8_t *)t71) + 8);
                uint32_t t73 = gm_l7;
                gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                gm_t88rcucg1b7uk t76 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t72, t73, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t76;
                goto gm_fret;
            }
gm_lb7:;
        }
gm_lb6:;
        void * t77 = gm_l0;
        uint32_t t78 = GM_LD(uint32_t, (((uint8_t *)t77) + 1936));
        uint8_t t79 = (t78 >= ((uint32_t)64ULL));
        if (t79) {
            void * t80 = gm_l0;
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t80) + 1984));
            uint8_t t82 = (uint8_t)!t81;
            if (t82) {
                void * t83 = gm_l0;
                GM_ST(uint8_t, (((uint8_t *)t83) + 1984), ((uint8_t)1));
                {
                    void * t84 = gm_l0;
                    void * t85 = gm_l0;
                    void * t86 = GM_LD(void *, (((uint8_t *)t85) + 1088));
                    uint64_t t87 = GM_LD(uint64_t, ((((uint8_t *)t85) + 1088) + 8));
                    uint32_t t88 = gm_l1;
                    uint64_t t89 = (uint64_t)t88;
                    if ((uintptr_t)t89 >= (uintptr_t)(t87)) gm_trap_index((uintptr_t)t89, t87, gm_src3k278vcmqgdr, 556, 21);
                    gm_tl874t2atnlh3 t90 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t86) + (uintptr_t)t89 * 184) + 156));
                    void * t91 = gm_l0;
                    void * t92 = GM_LD(void *, (((uint8_t *)t91) + 1088));
                    uint64_t t93 = GM_LD(uint64_t, ((((uint8_t *)t91) + 1088) + 8));
                    uint32_t t94 = gm_l1;
                    uint64_t t95 = (uint64_t)t94;
                    if ((uintptr_t)t95 >= (uintptr_t)(t93)) gm_trap_index((uintptr_t)t95, t93, gm_src3k278vcmqgdr, 556, 156);
                    gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)t92) + (uintptr_t)t95 * 184));
                    gm_t0bf4lga76ktk t97;
                    GM_ST(uint32_t, (uint8_t *)&t97 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t97 + 0), t96);
                    GM_ST(int32_t, ((uint8_t *)&t97 + 16), ((int32_t)64LL));
                    gm_f_format_bsjq7um((void *)&gm_l50, (void *)NULL, cx, NULL, &t97);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                    if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 8, gm_src3k278vcmqgdr, 556, 57);
                    gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l50) + 8));
                    gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
                    gm_b0_1 t101 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t84, t90, t100);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
            void * t102 = gm_l0;
            void * t103 = (void *)(((uint8_t *)t102) + 8);
            gm_t88rcucg1b7uk t104 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t103);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t104;
            goto gm_fret;
        }
    }
    {
        void * t105 = gm_l0;
        void * t106 = GM_LD(void *, (((uint8_t *)t105) + 1088));
        uint64_t t107 = GM_LD(uint64_t, ((((uint8_t *)t105) + 1088) + 8));
        uint32_t t108 = gm_l1;
        uint64_t t109 = (uint64_t)t108;
        if ((uintptr_t)t109 >= (uintptr_t)(t107)) gm_trap_index((uintptr_t)t109, t107, gm_src3k278vcmqgdr, 560, 13);
        memmove(((uint8_t *)&F.l8), (((uint8_t *)t106) + (uintptr_t)t109 * 184), 184);
        void * t110 = gm_l0;
        void * t111 = (void *)(((uint8_t *)t110) + 8);
        gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
        uint32_t t113 = gm_f_fresh_adt_u9rnf9f((void *)NULL, cx, NULL, t111, t112);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t113;
        void * t114 = gm_l0;
        void * t115 = (void *)(((uint8_t *)t114) + 8);
        uint32_t t116 = gm_l9;
        uint32_t t117 = gm_l1;
        gm_f_set_template_o09q96o(t115, t116, t117);
        gm_trqfvmjtt4sij t118;
        gm_f_list_nof7vfj((void *)&t118, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t118;
        void * t119 = (void *)((uint8_t *)&F.l10);
        gm_b16_8 t120 = gm_l2;
        gm_tftb1odc739t5 t121 = gm_f_extend_lfqasgq(t119, t120);
        gm_l51 = t121;
        uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
        if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_src3k278vcmqgdr, 564, 5);
        void * t123 = gm_l0;
        void * t124 = (void *)(((uint8_t *)t123) + 1168);
        uint32_t t125 = gm_l1;
        gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
        uint32_t t127 = gm_l9;
        gm_t5g5vgung6h6s t128;
        GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t128 + 28, 0);
        GM_ST(uint32_t, ((uint8_t *)&t128 + 0), t125);
        GM_ST(gm_b16_8, ((uint8_t *)&t128 + 8), t126);
        GM_ST(uint32_t, ((uint8_t *)&t128 + 24), t127);
        gm_tftb1odc739t5 t129 = gm_f_push_u4kbe6d(t124, &t128);
        gm_l52 = t129;
        uint8_t t130 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
        if (!t130) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_src3k278vcmqgdr, 565, 5);
        void * t131 = gm_l0;
        void * t132 = (void *)(((uint8_t *)t131) + 1208);
        uint32_t t133 = gm_l1;
        gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
        gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
        gm_tiilmfd16v3fc t136;
        GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t136 + 0), t133);
        GM_ST(gm_b16_8, ((uint8_t *)&t136 + 8), t135);
        uint32_t t137 = gm_l9;
        gm_tftb1odc739t5 t138 = gm_f_put_f1iksvi(t132, &t136, t137);
        gm_l53 = t138;
        uint8_t t139 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
        if (!t139) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_src3k278vcmqgdr, 566, 5);
        void * t140 = gm_l0;
        void * t141 = (void *)(((uint8_t *)t140) + 8);
        uint32_t t142 = gm_l9;
        gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&t143));
        gm_t88rcucg1b7uk t145 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t141, t142, t144);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t145;
        gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
        uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&F.l8) + 16));
        gm_ti0kgh3pc3in2 t148;
        gm_f_empty_struct_b5i94nj((void *)&t148);
        gm_tl874t2atnlh3 t149 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l8) + 156));
        gm_tf5srh4mp4b3i t150;
        memset(&t150, 0, sizeof t150);
        gm_f_new_adt_info_5pl2srl((void *)&F.l12, t146, t147, &t148, GM_CONV(gm_cseahvg8v7p0vs, t149), t150);
        gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 80));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l12) + 80), t151);
        gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 96));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l12) + 96), t152);
    }
    {
        uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 112));
        GM_ST(uint8_t, (((uint8_t *)&F.l12) + 112), t153);
        uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 152));
        GM_ST(uint8_t, (((uint8_t *)&F.l12) + 152), t154);
        void * t155 = gm_l0;
        void * t156 = (void *)(((uint8_t *)t155) + 1088);
        gm_tftb1odc739t5 t157 = gm_f_push_j84ih6c(t156, &(*(gm_t6art7bprjiqb *)((uint8_t *)&F.l12)));
        gm_l54 = t157;
        uint8_t t158 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
        if (!t158) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_src3k278vcmqgdr, 573, 5);
        gm_trqfvmjtt4sij t159;
        gm_f_list_oalq03d((void *)&t159, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t159;
        gm_b16_8 t160 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 120));
        gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&t160));
        gm_l14 = t161;
        gm_l15 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t162 = gm_l15;
                uint64_t t163 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint8_t t164 = (t162 < t163);
                if (!t164) {
                    goto gm_lb8;
                }
                {
                    uint64_t t165 = gm_l15;
                    void * t166 = GM_LD(void *, ((uint8_t *)&gm_l14));
                    uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint64_t t168 = gm_l15;
                    uint32_t t169 = GM_LD(uint32_t, (((uint8_t *)t166) + (uintptr_t)t168 * 4));
                    gm_tn68el8d14vku t170;
                    GM_ST(uint32_t, (uint8_t *)&t170 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t170 + 0), t165);
                    GM_ST(uint32_t, ((uint8_t *)&t170 + 8), t169);
                    gm_l55 = t170;
                    {
                        {
                            uint64_t t171 = GM_LD(uint64_t, ((uint8_t *)&gm_l55));
                            gm_l16 = t171;
                            uint32_t t172 = GM_LD(uint32_t, (((uint8_t *)&gm_l55) + 8));
                            gm_l17 = t172;
                            uint64_t t173 = gm_l16;
                            uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                            uint8_t t175 = (t173 < t174);
                            if (t175) {
                                void * t176 = (void *)((uint8_t *)&F.l13);
                                uint32_t t177 = gm_l17;
                                void * t178 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                uint64_t t179 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint64_t t180 = gm_l16;
                                gm_t88rcucg1b7uk t181 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t178) + (uintptr_t)t180 * 4));
                                gm_tplpbqf3duvn3 t182;
                                GM_ST(uint32_t, ((uint8_t *)&t182 + 0), t177);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t182 + 4), t181);
                                gm_tftb1odc739t5 t183 = gm_f_push_h69n5fi(t176, GM_CONV(gm_cs7e9ppsd384bj, t182));
                                gm_l56 = t183;
                                uint8_t t184 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                                if (!t184) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_src3k278vcmqgdr, 576, 27);
                            }
                            goto gm_lb11;
                        }
gm_lb12:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 575, 5);
                    }
gm_lb11:;
                }
gm_lb10:;
                uint64_t t185 = gm_l15;
                uint64_t r186; bool o187 = __builtin_add_overflow(t185, ((uint64_t)1ULL), &r186);
                uint64_t t188 = r186;
                gm_l15 = t188;
            }
        }
gm_lb8:;
        void * t189 = gm_l0;
        gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
        gm_b16_8 t191 = GM_LD(gm_b16_8, ((uint8_t *)&t190));
        gm_toceffb697p0u t192;
        memset(&t192, 0, sizeof t192);
        gm_b0_1 t193 = gm_f_check_len_exprs_m8s6m0h((void *)NULL, cx, NULL, t189, t191, t192);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t194 = gm_l0;
        void * t195 = gm_l0;
        uint32_t t196 = GM_LD(uint32_t, (((uint8_t *)t195) + 1936));
        uint32_t r197; bool o198 = __builtin_add_overflow(t196, ((uint32_t)1ULL), &r197);
        if (o198) gm_trap("integer overflow in `+`", gm_src3k278vcmqgdr, 579, 5);
        uint32_t t199 = r197;
        GM_ST(uint32_t, (((uint8_t *)t194) + 1936), t199);
    }
    {
        {
            {
                uint8_t t200 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 24) + 0));
                uint8_t t201 = (uint8_t)(t200 == ((uint8_t)0ULL));
                if (!t201) {
                    goto gm_lb14;
                }
                {
                    memmove(((uint8_t *)&F.l18), ((((uint8_t *)&F.l8) + 24) + 8), 48);
                }
                {
                    void * t202 = gm_l0;
                    gm_b16_8 t203 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                    gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&t203));
                    gm_b16_8 t205 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                    gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&t205));
                    gm_b16_8 t207 = gm_f_subst_fields_pqf9lb0((void *)NULL, cx, NULL, t202, t204, t206);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t208 = GM_LD(uint8_t, (((uint8_t *)&F.l18) + 16));
                    gm_tf5srh4mp4b3i t209 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l18) + 24));
                    gm_tkn42u7na3t81 t210 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l18) + 40));
                    gm_tpautr4br7h3l t211;
                    GM_ST(uint8_t, (uint8_t *)&t211 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t211 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t211 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t211 + 0), t207);
                    GM_ST(uint8_t, ((uint8_t *)&t211 + 16), t208);
                    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t211 + 24), t209);
                    GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t211 + 40), t210);
                    GM_ST(uint8_t, (uint8_t *)&F.l28 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.l28 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.l28 + 4, 0);
                    GM_ST(uint8_t, ((uint8_t *)&F.l28 + 0), ((uint8_t)0ULL));
                    GM_ST(gm_tpautr4br7h3l, ((uint8_t *)&F.l28 + 8), t211);
                }
                goto gm_lb13;
            }
gm_lb14:;
            {
                uint8_t t212 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 24) + 0));
                uint8_t t213 = (uint8_t)(t212 == ((uint8_t)1ULL));
                if (!t213) {
                    goto gm_lb15;
                }
                gm_t88rcucg1b7uk t214 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l8) + 24) + 8));
                gm_l19 = t214;
                gm_b16_8 t215 = GM_LD(gm_b16_8, (((((uint8_t *)&F.l8) + 24) + 8) + 8));
                gm_l20 = t215;
                {
                    gm_trqfvmjtt4sij t216;
                    gm_f_list_g6uul2f((void *)&t216, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l21 = t216;
                }
                gm_b16_8 t217 = gm_l20;
                gm_l22 = t217;
                gm_l23 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t218 = gm_l23;
                        uint64_t t219 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                        uint8_t t220 = (t218 < t219);
                        if (!t220) {
                            goto gm_lb16;
                        }
                        {
                            void * t221 = GM_LD(void *, ((uint8_t *)&gm_l22));
                            uint64_t t222 = GM_LD(uint64_t, (((uint8_t *)&gm_l22) + 8));
                            uint64_t t223 = gm_l23;
                            {
                                {
                                    {
                                        memmove(((uint8_t *)&F.l24), (((uint8_t *)t221) + (uintptr_t)t223 * 72), 72);
                                    }
                                    gm_tkn42u7na3t81 t224;
                                    memset(&t224, 0, sizeof t224);
                                    gm_l25 = t224;
                                    {
                                        {
                                            uint8_t t225 = (uint8_t)(((((uint8_t *)&F.l24) + 16))[4] != 0);
                                            if (!t225) {
                                                goto gm_lb22;
                                            }
                                            gm_t88rcucg1b7uk t226 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l24) + 16));
                                            gm_l26 = t226;
                                            void * t227 = gm_l0;
                                            gm_t88rcucg1b7uk t228 = gm_l26;
                                            gm_b16_8 t229 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                                            gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&t229));
                                            gm_t88rcucg1b7uk t231 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t227, t228, t230);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_tkn42u7na3t81 t232;
                                            GM_ST(uint8_t, (uint8_t *)&t232 + 5, 0);
                                            GM_ST(uint16_t, (uint8_t *)&t232 + 6, 0);
                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t232), t231);
                                            ((uint8_t *)&t232)[4] = 1;
                                            gm_l25 = t232;
                                            goto gm_lb21;
                                        }
gm_lb22:;
                                    }
gm_lb21:;
                                    {
                                        void * t233 = (void *)((uint8_t *)&F.l21);
                                        gm_b16_8 t234 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
                                        gm_tkn42u7na3t81 t235 = gm_l25;
                                        gm_tbci5ospk5nnn t236 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l24) + 24));
                                        gm_tckdobunaulee t237;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t237 + 0), t234);
                                        GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t237 + 16), t235);
                                        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t237 + 24), t236);
                                        gm_tftb1odc739t5 t238 = gm_f_push_1j8vbii(t233, &t237);
                                        gm_l57 = t238;
                                    }
                                    uint8_t t239 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                                    if (!t239) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_src3k278vcmqgdr, 587, 17);
                                    goto gm_lb19;
                                }
gm_lb20:;
                                gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 584, 13);
                            }
gm_lb19:;
                        }
gm_lb18:;
                        uint64_t t240 = gm_l23;
                        uint64_t r241; bool o242 = __builtin_add_overflow(t240, ((uint64_t)1ULL), &r241);
                        uint64_t t243 = r241;
                        gm_l23 = t243;
                    }
                }
gm_lb16:;
                {
                    gm_t88rcucg1b7uk t244 = gm_l19;
                    gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
                    gm_tcmkompbl09il t246;
                    GM_ST(uint32_t, (uint8_t *)&t246 + 4, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t246 + 0), t244);
                    GM_ST(gm_b16_8, ((uint8_t *)&t246 + 8), t245);
                    GM_ST(uint8_t, (uint8_t *)&F.l28 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.l28 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.l28 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&F.l28 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&F.l28 + 40, 0);
                    GM_ST(uint64_t, (uint8_t *)&F.l28 + 48, 0);
                    GM_ST(uint8_t, ((uint8_t *)&F.l28 + 0), ((uint8_t)1ULL));
                    GM_ST(gm_tcmkompbl09il, ((uint8_t *)&F.l28 + 8), t246);
                }
                goto gm_lb13;
            }
gm_lb15:;
            {
                uint8_t t247 = GM_LD(uint8_t, ((((uint8_t *)&F.l8) + 24) + 0));
                uint8_t t248 = (uint8_t)(t247 == ((uint8_t)2ULL));
                if (!t248) {
                    goto gm_lb23;
                }
                gm_b16_8 t249 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l8) + 24) + 8));
                gm_l27 = t249;
                void * t250 = gm_l0;
                gm_b16_8 t251 = gm_l27;
                gm_b16_8 t252 = GM_LD(gm_b16_8, ((uint8_t *)&t251));
                gm_b16_8 t253 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
                gm_b16_8 t255 = gm_f_subst_fields_pqf9lb0((void *)NULL, cx, NULL, t250, t252, t254);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                memset(&F.l28, 0, sizeof F.l28);
                GM_ST(uint8_t, ((uint8_t *)&F.l28 + 0), ((uint8_t)2ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&F.l28 + 8), t255);
                goto gm_lb13;
            }
gm_lb23:;
            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 580, 21);
        }
gm_lb13:;
    }
    {
        memmove(((uint8_t *)&F.l28), ((uint8_t *)&F.l28), 56);
        void * t256 = gm_l0;
        void * t257 = GM_LD(void *, (((uint8_t *)t256) + 1088));
        uint64_t t258 = GM_LD(uint64_t, ((((uint8_t *)t256) + 1088) + 8));
        uint32_t t259 = gm_l9;
        uint64_t t260 = (uint64_t)t259;
        if ((uintptr_t)t260 >= (uintptr_t)(t258)) gm_trap_index((uintptr_t)t260, t258, gm_src3k278vcmqgdr, 593, 5);
        memmove(((((uint8_t *)t257) + (uintptr_t)t260 * 184) + 24), ((uint8_t *)&F.l28), 56);
        void * t261 = gm_l0;
        void * t262 = gm_l0;
        uint32_t t263 = GM_LD(uint32_t, (((uint8_t *)t262) + 1936));
        uint32_t r264; bool o265 = __builtin_sub_overflow(t263, ((uint32_t)1ULL), &r264);
        if (o265) gm_trap("integer overflow in `-`", gm_src3k278vcmqgdr, 594, 5);
        uint32_t t266 = r264;
        GM_ST(uint32_t, (((uint8_t *)t261) + 1936), t266);
        gm_l29 = ((uint8_t)1);
        gm_b16_8 t267 = gm_l2;
        gm_l30 = t267;
        gm_l31 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t268 = gm_l31;
                uint64_t t269 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                uint8_t t270 = (t268 < t269);
                if (!t270) {
                    goto gm_lb24;
                }
                {
                    void * t271 = GM_LD(void *, ((uint8_t *)&gm_l30));
                    uint64_t t272 = GM_LD(uint64_t, (((uint8_t *)&gm_l30) + 8));
                    uint64_t t273 = gm_l31;
                    {
                        {
                            gm_t88rcucg1b7uk t274 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t271) + (uintptr_t)t273 * 4));
                            gm_l32 = t274;
                            void * t275 = gm_l0;
                            void * t276 = (void *)(((uint8_t *)t275) + 8);
                            gm_t88rcucg1b7uk t277 = gm_l32;
                            uint8_t t278 = gm_f_has_param_u9t0efs(t276, GM_CONV(gm_csguankko4lmhd, t277));
                            if (t278) {
                                gm_l29 = ((uint8_t)0);
                            }
                            goto gm_lb27;
                        }
gm_lb28:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 596, 5);
                    }
gm_lb27:;
                }
gm_lb26:;
                uint64_t t279 = gm_l31;
                uint64_t r280; bool o281 = __builtin_add_overflow(t279, ((uint64_t)1ULL), &r280);
                uint64_t t282 = r280;
                gm_l31 = t282;
            }
        }
gm_lb24:;
        uint8_t t283 = gm_l29;
        if (t283) {
            void * t284 = gm_l0;
            void * t285 = (void *)(((uint8_t *)t284) + 1944);
            uint32_t t286 = gm_l1;
            gm_b16_8 t287 = gm_f_empty_slice_rcd2jln();
            uint32_t t288 = gm_l9;
            gm_t5g5vgung6h6s t289;
            GM_ST(uint32_t, (uint8_t *)&t289 + 4, 0);
            GM_ST(uint32_t, (uint8_t *)&t289 + 28, 0);
            GM_ST(uint32_t, ((uint8_t *)&t289 + 0), t286);
            GM_ST(gm_b16_8, ((uint8_t *)&t289 + 8), t287);
            GM_ST(uint32_t, ((uint8_t *)&t289 + 24), t288);
            gm_tftb1odc739t5 t290 = gm_f_push_u4kbe6d(t285, &t289);
            gm_l58 = t290;
            uint8_t t291 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
            if (!t291) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_src3k278vcmqgdr, 599, 19);
        }
        void * t292 = gm_l0;
        uint32_t t293 = GM_LD(uint32_t, (((uint8_t *)t292) + 1936));
        uint8_t t294 = ((t293 == ((uint32_t)0ULL)));
        if (t294) {
            {
                void * t295 = gm_l0;
                memmove(((uint8_t *)&F.l33), (((uint8_t *)t295) + 1944), 40);
            }
            {
                void * t296 = gm_l0;
                gm_trqfvmjtt4sij t297;
                gm_f_list_au01ire((void *)&t297, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t296) + 1944), t297);
            }
            {
                gm_trqfvmjtt4sij t298;
                gm_f_list_go2qg0b((void *)&t298, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l34 = t298;
            }
            gm_b16_8 t299 = GM_LD(gm_b16_8, ((uint8_t *)&F.l33));
            gm_l35 = t299;
            gm_l36 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t300 = gm_l36;
                    uint64_t t301 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                    uint8_t t302 = (t300 < t301);
                    if (!t302) {
                        goto gm_lb29;
                    }
                    {
                        void * t303 = GM_LD(void *, ((uint8_t *)&gm_l35));
                        uint64_t t304 = GM_LD(uint64_t, (((uint8_t *)&gm_l35) + 8));
                        uint64_t t305 = gm_l36;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&gm_l37), (((uint8_t *)t303) + (uintptr_t)t305 * 32), 32);
                                }
                                {
                                    gm_trqfvmjtt4sij t306;
                                    gm_f_list_go2qg0b((void *)&t306, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    F.l38 = t306;
                                }
                                void * t307 = gm_l0;
                                void * t308 = (void *)(((uint8_t *)t307) + 8);
                                uint32_t t309 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 24));
                                void * t310 = gm_l0;
                                uint32_t t311 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 24));
                                gm_b16_8 t312 = gm_f_instance_args_15ek7cf((void *)NULL, cx, NULL, t310, t311);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t313 = GM_LD(gm_b16_8, ((uint8_t *)&t312));
                                gm_t88rcucg1b7uk t314 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t308, t309, t313);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l39 = t314;
                                void * t315 = gm_l0;
                                gm_t88rcucg1b7uk t316 = gm_l39;
                                uint32_t t317 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 24));
                                void * t318 = (void *)((uint8_t *)&F.l38);
                                uint8_t t319 = gm_f_contains_by_value_pcdi4kq((void *)NULL, cx, NULL, t315, t316, t317, t318);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t319) {
                                    void * t320 = (void *)((uint8_t *)&F.l34);
                                    uint32_t t321 = GM_LD(uint32_t, (((uint8_t *)&gm_l37) + 24));
                                    gm_tftb1odc739t5 t322 = gm_f_push_im9d4ro(t320, t321);
                                    gm_l59 = t322;
                                    uint8_t t323 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                    if (!t323) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_src3k278vcmqgdr, 609, 56);
                                }
                                goto gm_lb32;
                            }
gm_lb33:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 606, 9);
                        }
gm_lb32:;
                    }
gm_lb31:;
                    uint64_t t324 = gm_l36;
                    uint64_t r325; bool o326 = __builtin_add_overflow(t324, ((uint64_t)1ULL), &r325);
                    uint64_t t327 = r325;
                    gm_l36 = t327;
                }
            }
gm_lb29:;
            gm_b16_8 t328 = GM_LD(gm_b16_8, ((uint8_t *)&F.l33));
            gm_l40 = t328;
            gm_l41 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t329 = gm_l41;
                    uint64_t t330 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                    uint8_t t331 = (t329 < t330);
                    if (!t331) {
                        goto gm_lb34;
                    }
                    {
                        void * t332 = GM_LD(void *, ((uint8_t *)&gm_l40));
                        uint64_t t333 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                        uint64_t t334 = gm_l41;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&gm_l42), (((uint8_t *)t332) + (uintptr_t)t334 * 32), 32);
                                }
                                uint32_t t335 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 24));
                                gm_b16_8 t336 = GM_LD(gm_b16_8, ((uint8_t *)&F.l34));
                                uint8_t t337 = 0;
                                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t336) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t336))) + i * 4, ((uint8_t *)&t335))) { t337 = 1; break; }
                                uint8_t t338 = (uint8_t)(!t337);
                                if (t338) {
                                    goto gm_lb36;
                                }
                                void * t339 = gm_l0;
                                void * t340 = (void *)(((uint8_t *)t339) + 8);
                                uint32_t t341 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 24));
                                void * t342 = gm_l0;
                                uint32_t t343 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 24));
                                gm_b16_8 t344 = gm_f_instance_args_15ek7cf((void *)NULL, cx, NULL, t342, t343);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t345 = GM_LD(gm_b16_8, ((uint8_t *)&t344));
                                gm_t88rcucg1b7uk t346 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t340, t341, t345);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l43 = t346;
                                {
                                    void * t347 = gm_l0;
                                    void * t348 = gm_l0;
                                    void * t349 = GM_LD(void *, (((uint8_t *)t348) + 1088));
                                    uint64_t t350 = GM_LD(uint64_t, ((((uint8_t *)t348) + 1088) + 8));
                                    uint32_t t351 = GM_LD(uint32_t, ((uint8_t *)&gm_l42));
                                    uint64_t t352 = (uint64_t)t351;
                                    if ((uintptr_t)t352 >= (uintptr_t)(t350)) gm_trap_index((uintptr_t)t352, t350, gm_src3k278vcmqgdr, 614, 21);
                                    gm_tl874t2atnlh3 t353 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t349) + (uintptr_t)t352 * 184) + 156));
                                    void * t354 = gm_l0;
                                    gm_t88rcucg1b7uk t355 = gm_l43;
                                    gm_b16_8 t356 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t354, t355);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tnpifv20irnph t357;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t357 + 0), t356);
                                    gm_f_format_0au88qf((void *)&gm_l60, (void *)NULL, cx, NULL, t357);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t358 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                    if (!t358) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 8, gm_src3k278vcmqgdr, 614, 59);
                                    gm_b16_8 t359 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l60) + 8));
                                    gm_b16_8 t360 = GM_LD(gm_b16_8, ((uint8_t *)&t359));
                                    gm_b0_1 t361 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t347, t353, t360);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
                                void * t362 = gm_l0;
                                uint32_t t363 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 24));
                                gm_f_empty_adt_tdp3hro(t362, t363);
                                goto gm_lb37;
                            }
gm_lb38:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 611, 9);
                        }
gm_lb37:;
                    }
gm_lb36:;
                    uint64_t t364 = gm_l41;
                    uint64_t r365; bool o366 = __builtin_add_overflow(t364, ((uint64_t)1ULL), &r365);
                    uint64_t t367 = r365;
                    gm_l41 = t367;
                }
            }
gm_lb34:;
            gm_b16_8 t368 = GM_LD(gm_b16_8, ((uint8_t *)&F.l33));
            gm_l44 = t368;
            gm_l45 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t369 = gm_l45;
                    uint64_t t370 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                    uint8_t t371 = (t369 < t370);
                    if (!t371) {
                        goto gm_lb39;
                    }
                    {
                        void * t372 = GM_LD(void *, ((uint8_t *)&gm_l44));
                        uint64_t t373 = GM_LD(uint64_t, (((uint8_t *)&gm_l44) + 8));
                        uint64_t t374 = gm_l45;
                        {
                            {
                                memmove(((uint8_t *)&gm_l46), (((uint8_t *)t372) + (uintptr_t)t374 * 32), 32);
                                uint32_t t375 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 24));
                                gm_b16_8 t376 = GM_LD(gm_b16_8, ((uint8_t *)&F.l34));
                                uint8_t t377 = 0;
                                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t376) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t376))) + i * 4, ((uint8_t *)&t375))) { t377 = 1; break; }
                                uint8_t t378 = (uint8_t)(t377);
                                if (t378) {
                                    goto gm_lb41;
                                }
                                void * t379 = gm_l0;
                                void * t380 = (void *)(((uint8_t *)t379) + 8);
                                uint32_t t381 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 24));
                                void * t382 = gm_l0;
                                uint32_t t383 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 24));
                                gm_b16_8 t384 = gm_f_instance_args_15ek7cf((void *)NULL, cx, NULL, t382, t383);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t385 = GM_LD(gm_b16_8, ((uint8_t *)&t384));
                                gm_t88rcucg1b7uk t386 = gm_f_adt_luat93d((void *)NULL, cx, NULL, t380, t381, t385);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l47 = t386;
                                void * t387 = gm_l0;
                                gm_t88rcucg1b7uk t388 = gm_l47;
                                void * t389 = gm_l0;
                                void * t390 = GM_LD(void *, (((uint8_t *)t389) + 1088));
                                uint64_t t391 = GM_LD(uint64_t, ((((uint8_t *)t389) + 1088) + 8));
                                uint32_t t392 = GM_LD(uint32_t, ((uint8_t *)&gm_l46));
                                uint64_t t393 = (uint64_t)t392;
                                if ((uintptr_t)t393 >= (uintptr_t)(t391)) gm_trap_index((uintptr_t)t393, t391, gm_src3k278vcmqgdr, 620, 38);
                                gm_tl874t2atnlh3 t394 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t390) + (uintptr_t)t393 * 184) + 156));
                                uint8_t t395 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t387, t388, t394);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t396 = (uint8_t)!t395;
                                if (t396) {
                                    void * t397 = gm_l0;
                                    uint32_t t398 = GM_LD(uint32_t, (((uint8_t *)&gm_l46) + 24));
                                    gm_f_empty_adt_tdp3hro(t397, t398);
                                }
                                goto gm_lb42;
                            }
gm_lb43:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 617, 9);
                        }
gm_lb42:;
                    }
gm_lb41:;
                    uint64_t t399 = gm_l45;
                    uint64_t r400; bool o401 = __builtin_add_overflow(t399, ((uint64_t)1ULL), &r400);
                    uint64_t t402 = r400;
                    gm_l45 = t402;
                }
            }
gm_lb39:;
        }
    }
    {
        gm_t88rcucg1b7uk t403 = gm_l11;
        F.ret = t403;
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
inline __attribute__((aligned(4))) uint8_t gm_f_int_fits_imbqv0c(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_int_fits_imbqv0c F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint8_t gm_l3; /* signed */
    uint64_t gm_l4; /* bits */
    uint8_t gm_l5; /* sg */
    uint16_t gm_l6; /* b */
    GM_STACK_CHECK(&F, 464u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l7;
    uint64_t gm_l8;
    gm_l3 = ((uint8_t)0);
    gm_l4 = ((uint64_t)0ULL);
    {
        void * t7 = gm_l0;
        void * t8 = (void *)(((uint8_t *)t7) + 8);
        gm_t88rcucg1b7uk t9 = gm_l2;
        gm_f_kind_jcnt4hm((void *)&gm_l7, t8, GM_CONV(gm_csguankko4lmhd, t9));
    }
    {
        {
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t11 = (uint8_t)(t10 == ((uint8_t)0ULL));
            if (!t11) {
                goto gm_lb2;
            }
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 8));
            gm_l5 = t12;
            uint16_t t13 = GM_LD(uint16_t, ((((uint8_t *)&gm_l7) + 8) + 2));
            gm_l6 = t13;
            uint8_t t14 = gm_l5;
            gm_l3 = t14;
            uint16_t t15 = gm_l6;
            uint64_t t16 = (uint64_t)t15;
            gm_l4 = t16;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)1ULL));
            if (!t18) {
                goto gm_lb3;
            }
            void * t19 = gm_l0;
            void * t20 = GM_LD(void *, ((uint8_t *)t19));
            void * t21 = (void *)(((uint8_t *)t20) + 88);
            uint32_t t22 = gm_f_ptr_bits_glps5i7(t21);
            uint64_t t23 = (uint64_t)t22;
            gm_l4 = t23;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            uint8_t t25 = (uint8_t)(t24 == ((uint8_t)2ULL));
            if (!t25) {
                goto gm_lb4;
            }
            gm_l3 = ((uint8_t)1);
            void * t26 = gm_l0;
            void * t27 = GM_LD(void *, ((uint8_t *)t26));
            void * t28 = (void *)(((uint8_t *)t27) + 88);
            uint32_t t29 = gm_f_ptr_bits_glps5i7(t28);
            uint64_t t30 = (uint64_t)t29;
            gm_l4 = t30;
            goto gm_lb1;
        }
gm_lb4:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 423, 5);
    }
gm_lb1:;
    {
        uint8_t t31 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
        if (t31) {
            uint8_t t32 = gm_l3;
            uint8_t t33 = (uint8_t)!t32;
            if (t33) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            {
                gm_tbci5ospk5nnn t34 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l1));
                gm_tbci5ospk5nnn t35;
                gm_f_one_08qg1op((void *)&t35, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbci5ospk5nnn t36;
                gm_f_add_420lj6o((void *)&t36, (void *)NULL, cx, NULL, &t34, &t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbci5ospk5nnn t37;
                gm_f_neg_agd5c5k((void *)&t37, (void *)NULL, cx, NULL, &t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint64_t t38 = gm_f_bits_lr3of4i(&t37);
                uint64_t t39 = gm_l4;
                uint64_t r40; bool o41 = __builtin_sub_overflow(t39, ((uint64_t)1ULL), &r40);
                if (o41) gm_trap("integer overflow in `-`", gm_src3k278vcmqgdr, 438, 52);
                uint64_t t42 = r40;
                uint8_t t43 = (t38 <= t42);
                F.ret = t43;
                goto gm_fret;
            }
        }
    }
    {
        uint64_t t44 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
        uint8_t t45 = gm_l3;
        if (t45) {
            uint64_t t46 = gm_l4;
            uint64_t r47; bool o48 = __builtin_sub_overflow(t46, ((uint64_t)1ULL), &r47);
            if (o48) gm_trap("integer overflow in `-`", gm_src3k278vcmqgdr, 440, 37);
            uint64_t t49 = r47;
            gm_l8 = t49;
        } else {
            uint64_t t50 = gm_l4;
            gm_l8 = t50;
        }
        uint64_t t51 = gm_l8;
        uint8_t t52 = (t44 <= t51);
        F.ret = t52;
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
__attribute__((aligned(4))) uint8_t gm_f_is_infinitely_sized_mhhcucl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, void * a2) {
    struct gm_fr_is_infinitely_sized_mhhcucl F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    void * gm_l2; /* stack */
    uint32_t gm_l3; /* id */
    uint8_t gm_l4; /* r */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_t88rcucg1b7uk gm_l10; /* elem */
    gm_t88rcucg1b7uk gm_l11; /* elem */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    gm_b16_8 gm_l13; /* elems */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_t88rcucg1b7uk gm_l16; /* e */
    gm_t88rcucg1b7uk gm_l17; /* err */
    gm_t88rcucg1b7uk gm_l18; /* ok */
    GM_STACK_CHECK(&F, 609u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tkn42u7na3t81 gm_l21;
    uint8_t gm_l22;
    void * t22 = gm_l0;
    void * t23 = (void *)(((uint8_t *)t22) + 8);
    gm_t88rcucg1b7uk t24 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l19, t23, GM_CONV(gm_csguankko4lmhd, t24));
    {
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)17ULL));
            if (!t26) {
                goto gm_lb2;
            }
            uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)&gm_l19) + 8));
            gm_l3 = t27;
            uint32_t t28 = gm_l3;
            void * t29 = gm_l2;
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)t29));
            uint8_t t31 = 0;
            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t30) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t30))) + i * 4, ((uint8_t *)&t28))) { t31 = 1; break; }
            uint8_t t32 = (uint8_t)(t31);
            if (t32) {
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            void * t33 = gm_l2;
            uint32_t t34 = gm_l3;
            gm_tftb1odc739t5 t35 = gm_f_push_im9d4ro(t33, t34);
            gm_l20 = t35;
            uint8_t t36 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t36) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_src3k278vcmqgdr, 468, 13);
            gm_l4 = ((uint8_t)0);
            {
                void * t37 = gm_l0;
                void * t38 = GM_LD(void *, (((uint8_t *)t37) + 1088));
                uint64_t t39 = GM_LD(uint64_t, ((((uint8_t *)t37) + 1088) + 8));
                uint32_t t40 = gm_l3;
                uint64_t t41 = (uint64_t)t40;
                if ((uintptr_t)t41 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t41, t39, gm_src3k278vcmqgdr, 470, 21);
                memmove(((uint8_t *)&F.l5), ((((uint8_t *)t38) + (uintptr_t)t41 * 184) + 24), 56);
            }
            gm_l7 = ((uint64_t)0ULL);
            {
                uint64_t t42 = gm_f_member_count_otu9tc0(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l5)));
                gm_l6 = t42;
            }
            {
                for (;;) {
                    uint64_t t43 = gm_l7;
                    uint64_t t44 = gm_l6;
                    uint8_t t45 = (t43 < t44);
                    if (!t45) {
                        goto gm_lb3;
                    }
                    {
                        {
                            {
                                uint64_t t46 = gm_l7;
                                gm_l8 = t46;
                                uint64_t t47 = gm_l8;
                                gm_tkn42u7na3t81 t48 = gm_f_member_ty_esvuguv(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l5)), t47);
                                gm_l21 = t48;
                                uint8_t t49 = (uint8_t)((((uint8_t *)&gm_l21))[4] != 0);
                                if (t49) {
                                    gm_t88rcucg1b7uk t50 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l21));
                                    gm_l9 = t50;
                                } else {
                                    goto gm_lb5;
                                }
                                void * t51 = gm_l0;
                                gm_t88rcucg1b7uk t52 = gm_l9;
                                void * t53 = gm_l2;
                                uint8_t t54 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t51, t52, t53);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t54) {
                                    gm_l4 = ((uint8_t)1);
                                    goto gm_lb3;
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 471, 13);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t55 = gm_l7;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l7 = t58;
                }
            }
gm_lb3:;
            void * t59 = gm_l2;
            gm_t9805qtonbsag t60 = gm_f_pop_570g79n(t59);
            uint8_t t61 = gm_l4;
            F.ret = t61;
            goto gm_fret;
        }
gm_lb2:;
        {
            {
                {
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                    uint8_t t63 = (uint8_t)(t62 == ((uint8_t)11ULL));
                    if (!t63) {
                        goto gm_lb10;
                    }
                    gm_t88rcucg1b7uk t64 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l19) + 8) + 8));
                    gm_l10 = t64;
                    goto gm_lb9;
                }
gm_lb10:;
                {
                    uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                    uint8_t t66 = (uint8_t)(t65 == ((uint8_t)20ULL));
                    if (!t66) {
                        goto gm_lb11;
                    }
                    gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l19) + 8) + 4));
                    gm_l10 = t67;
                    goto gm_lb9;
                }
gm_lb11:;
                {
                    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
                    uint8_t t69 = (uint8_t)(t68 == ((uint8_t)13ULL));
                    if (!t69) {
                        goto gm_lb12;
                    }
                    gm_t88rcucg1b7uk t70 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l19) + 8));
                    gm_l10 = t70;
                    goto gm_lb9;
                }
gm_lb12:;
                goto gm_lb8;
            }
gm_lb9:;
            void * t71 = gm_l0;
            gm_t88rcucg1b7uk t72 = gm_l10;
            void * t73 = gm_l2;
            uint8_t t74 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t71, t72, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t74;
            goto gm_fret;
        }
gm_lb8:;
        {
            uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t76 = (uint8_t)(t75 == ((uint8_t)12ULL));
            if (!t76) {
                goto gm_lb13;
            }
            gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
            gm_l13 = t77;
            gm_b16_8 t78 = gm_l13;
            gm_l14 = t78;
            gm_l15 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t79 = gm_l15;
                    uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint8_t t81 = (t79 < t80);
                    if (!t81) {
                        goto gm_lb14;
                    }
                    {
                        void * t82 = GM_LD(void *, ((uint8_t *)&gm_l14));
                        uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                        uint64_t t84 = gm_l15;
                        {
                            {
                                gm_t88rcucg1b7uk t85 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t82) + (uintptr_t)t84 * 4));
                                gm_l16 = t85;
                                void * t86 = gm_l0;
                                gm_t88rcucg1b7uk t87 = gm_l16;
                                void * t88 = gm_l2;
                                uint8_t t89 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t86, t87, t88);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t89) {
                                    F.ret = ((uint8_t)1);
                                    goto gm_fret;
                                }
                                goto gm_lb17;
                            }
gm_lb18:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 483, 13);
                        }
gm_lb17:;
                    }
gm_lb16:;
                    uint64_t t90 = gm_l15;
                    uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                    uint64_t t93 = r91;
                    gm_l15 = t93;
                }
            }
gm_lb14:;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb13:;
        {
            uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t95 = (uint8_t)(t94 == ((uint8_t)14ULL));
            if (!t95) {
                goto gm_lb19;
            }
            gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l19) + 8));
            gm_l17 = t96;
            gm_t88rcucg1b7uk t97 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l19) + 8) + 4));
            gm_l18 = t97;
            void * t98 = gm_l0;
            gm_t88rcucg1b7uk t99 = gm_l17;
            void * t100 = gm_l2;
            uint8_t t101 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t98, t99, t100);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t101) {
                gm_l22 = ((uint8_t)1);
            } else {
                void * t102 = gm_l0;
                gm_t88rcucg1b7uk t103 = gm_l18;
                void * t104 = gm_l2;
                uint8_t t105 = gm_f_is_infinitely_sized_mhhcucl((void *)NULL, cx, NULL, t102, t103, t104);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l22 = t105;
            }
            uint8_t t106 = gm_l22;
            F.ret = t106;
            goto gm_fret;
        }
gm_lb19:;
        {
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
gm_lb20:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 465, 5);
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
inline __attribute__((aligned(4))) uint64_t gm_f_member_count_otu9tc0(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_member_count_otu9tc0 F;
    gm_b16_8 gm_l2; /* fields */
    gm_b16_8 gm_l3; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t gm_l4;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l1), (((uint8_t *)a0) + 8), 48);
            uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 8));
            gm_l4 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)2ULL));
            if (!t10) {
                goto gm_lb3;
            }
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l2 = t11;
            uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            gm_l4 = t12;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)1ULL));
            if (!t14) {
                goto gm_lb4;
            }
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 8));
            gm_l3 = t15;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            gm_l4 = t16;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 82, 47);
    }
gm_lb1:;
    uint64_t t17 = gm_l4;
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint64_t gm_f_member_count_otu9tc0_c(gm_ti0kgh3pc3in2 a0) { return gm_f_member_count_otu9tc0(&a0); }
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_member_ty_esvuguv(const void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_member_ty_esvuguv F;
    uint64_t gm_l1; /* i */
    gm_b16_8 gm_l3; /* fields */
    gm_b16_8 gm_l4; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 214u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    gm_tkn42u7na3t81 gm_l5;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)0ULL));
            if (!t7) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l2), (((uint8_t *)a0) + 8), 48);
            void * t8 = GM_LD(void *, ((uint8_t *)&F.l2));
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
            uint64_t t10 = gm_l1;
            if ((uintptr_t)t10 >= (uintptr_t)(t9)) gm_trap_index((uintptr_t)t10, t9, gm_src3k278vcmqgdr, 90, 21);
            gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t8) + (uintptr_t)t10 * 40) + 16));
            gm_tkn42u7na3t81 t12;
            GM_ST(uint8_t, (uint8_t *)&t12 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t12 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t12), t11);
            ((uint8_t *)&t12)[4] = 1;
            gm_l5 = t12;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t14 = (uint8_t)(t13 == ((uint8_t)2ULL));
            if (!t14) {
                goto gm_lb3;
            }
            gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l3 = t15;
            void * t16 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint64_t t18 = gm_l1;
            if ((uintptr_t)t18 >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)t18, t17, gm_src3k278vcmqgdr, 91, 24);
            gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t16) + (uintptr_t)t18 * 40) + 16));
            gm_tkn42u7na3t81 t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 6, 0);
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t20), t19);
            ((uint8_t *)&t20)[4] = 1;
            gm_l5 = t20;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)1ULL));
            if (!t22) {
                goto gm_lb4;
            }
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 8));
            gm_l4 = t23;
            void * t24 = GM_LD(void *, ((uint8_t *)&gm_l4));
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint64_t t26 = gm_l1;
            if ((uintptr_t)t26 >= (uintptr_t)(t25)) gm_trap_index((uintptr_t)t26, t25, gm_src3k278vcmqgdr, 92, 28);
            gm_tkn42u7na3t81 t27 = GM_LD(gm_tkn42u7na3t81, ((((uint8_t *)t24) + (uintptr_t)t26 * 72) + 16));
            gm_l5 = t27;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 89, 52);
    }
gm_lb1:;
    gm_tkn42u7na3t81 t28 = gm_l5;
    F.ret = t28;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_member_ty_esvuguv_c(gm_ti0kgh3pc3in2 a0, uint64_t a1) { return gm_f_member_ty_esvuguv(&a0, a1); }
inline __attribute__((aligned(4))) void gm_f_new_adt_info_5pl2srl(void *gm_out, gm_b16_8 a0, uint64_t a1, const void * a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_new_adt_info_5pl2srl F;
    gm_b16_8 gm_l0; /* name */
    uint64_t gm_l1; /* module */
    gm_tl874t2atnlh3 gm_l3; /* span */
    gm_tf5srh4mp4b3i gm_l4; /* decl */
    GM_STACK_CHECK_SHALLOW(&F, 224u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = GM_CONV(gm_tl874t2atnlh3, a3);
    gm_l4 = a4;
    gm_b16_8 t2 = gm_l0;
    uint64_t t3 = gm_l1;
    gm_ti0kgh3pc3in2 t4 = GM_LD(gm_ti0kgh3pc3in2, ((uint8_t *)a2));
    gm_b16_8 t5 = gm_f_empty_slice_s1lbe5s();
    gm_b16_8 t6 = gm_f_empty_slice_8q3kspo_2();
    gm_b16_8 t7 = gm_f_empty_slice_d914i5u();
    gm_b16_8 t8 = gm_f_empty_slice_8q3kspo_2();
    gm_tl874t2atnlh3 t9 = gm_l3;
    gm_tf5srh4mp4b3i t10 = gm_l4;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 113, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 114, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 116, 0);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 153, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 154, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 0), t2);
    GM_ST(uint64_t, ((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 16), t3);
    GM_ST(gm_ti0kgh3pc3in2, ((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 24), t4);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 80), t5);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 96), t6);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 112), ((uint8_t)0));
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 120), t7);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 136), t8);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 152), ((uint8_t)0));
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 156), t9);
    GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&(*(gm_t6art7bprjiqb *)gm_out) + 168), t10);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t6art7bprjiqb gm_f_new_adt_info_5pl2srl_c(gm_b16_8 a0, uint64_t a1, gm_ti0kgh3pc3in2 a2, gm_cseahvg8v7p0vs a3, gm_tf5srh4mp4b3i a4) { gm_t6art7bprjiqb gm_r; gm_f_new_adt_info_5pl2srl(&gm_r, a0, a1, &a2, a3, a4); return gm_r; }
inline __attribute__((aligned(4))) uint32_t gm_f_new_param_7010dt4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tnnjeerolos0a a2) {
    struct gm_fr_new_param_7010dt4 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* name */
    gm_tnnjeerolos0a gm_l2; /* kind */
    uint32_t gm_l3; /* id */
    GM_STACK_CHECK(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l4;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_b16_8 t4 = gm_l1;
    uint32_t t5 = gm_f_fresh_param_442g4m9((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l3 = t5;
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 1128);
        gm_tnnjeerolos0a t8 = gm_l2;
        gm_trqfvmjtt4sij t9;
        gm_f_list_5kg5cjm_2((void *)&t9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tdtrcq4ju76rh t10;
        GM_ST(gm_tnnjeerolos0a, ((uint8_t *)&t10 + 0), t8);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t10 + 8), t9);
        gm_tftb1odc739t5 t11 = gm_f_push_bth3krn(t7, &t10);
        gm_l4 = t11;
    }
    uint8_t t12 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t12) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_src3k278vcmqgdr, 37, 5);
    uint32_t t13 = gm_l3;
    F.ret = t13;
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
inline __attribute__((aligned(4))) gm_tnnjeerolos0a gm_f_param_kind_of_1jngakt(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2) {
    struct gm_fr_param_kind_of_1jngakt F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* p */
    void * gm_l3; /* t */
    gm_t08olq97rscn3 gm_l4; /* i */
    GM_STACK_CHECK(&F, 273u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t4 = gm_l2;
    uint8_t t5 = (uint8_t)(GM_LD(void *, (((uint8_t *)t4) + 24)) != 0);
    if (t5) {
        void * t6 = GM_LD(void *, (((uint8_t *)t4) + 24));
        gm_l3 = t6;
    } else {
        void * t7 = gm_l0;
        void * t8 = gm_l2;
        gm_tl874t2atnlh3 t9 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t8) + 40));
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sf3irv2ans6de));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(56));
        gm_b0_1 t11 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t7, t9, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t12 = gm_l0;
        void * t13 = (void *)(((uint8_t *)t12) + 8);
        gm_t88rcucg1b7uk t14 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnnjeerolos0a t15;
        GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)2ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t15 + 4), t14);
        F.ret = t15;
        goto gm_fret;
    }
    void * t16 = gm_l3;
    uint8_t t17 = gm_f_is_effects_kind_uutbf9u(t16);
    if (t17) {
        gm_tnnjeerolos0a t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)1ULL));
        F.ret = t18;
        goto gm_fret;
    }
    {
        {
            void * t19 = gm_l3;
            uint8_t t20 = GM_LD(uint8_t, ((((uint8_t *)t19) + 8) + 0));
            uint8_t t21 = (uint8_t)(t20 == ((uint8_t)6ULL));
            if (!t21) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t19) + 8) + 8), 32);
            gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l4));
            gm_b16_8 t23;
            GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s2737i1iq57nf));
            GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(4));
            uint8_t t24 = (gm_eq0iqiubfm7e(((uint8_t *)&t22), ((uint8_t *)&t23)));
            if (!t24) {
                goto gm_lb2;
            }
            gm_tnnjeerolos0a t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
            F.ret = t25;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t26 = gm_l0;
    uint64_t t27 = gm_l1;
    void * t28 = gm_l3;
    gm_t88rcucg1b7uk t29 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t26, t27, t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnnjeerolos0a t30;
    GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)2ULL));
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t30 + 4), t29);
    F.ret = t30;
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
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_pending_layout_ikknuv1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pending_layout_ikknuv1 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* id */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_t88rcucg1b7uk gm_l7; /* t */
    uint32_t gm_l8; /* p */
    gm_t8nckeqfenl8a gm_l9; /* a */
    gm_tgglga8ro3isc gm_l10; /* a */
    gm_t88rcucg1b7uk gm_l11; /* t */
    gm_t6j6smj7qoctu gm_l12; /* t */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    gm_t88rcucg1b7uk gm_l15; /* e */
    uint32_t gm_l16; /* p */
    gm_tgglga8ro3isc gm_l17; /* r */
    uint32_t gm_l18; /* p */
    GM_STACK_CHECK(&F, 834u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l19;
    uint8_t gm_l20;
    gm_tf5srh4mp4b3i gm_l21;
    gm_tkn42u7na3t81 gm_l22;
    gm_t9805qtonbsag gm_l23;
    gm_t9805qtonbsag gm_l24;
    gm_t9805qtonbsag gm_l25;
    void * t29 = gm_l0;
    uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t29) + 1848) + 8));
    uint8_t t31 = ((t30 == ((uint64_t)0ULL)));
    if (t31) {
        gm_t9805qtonbsag t32;
        memset(&t32, 0, sizeof t32);
        F.ret = t32;
        goto gm_fret;
    }
    void * t33 = gm_l0;
    void * t34 = (void *)(((uint8_t *)t33) + 8);
    gm_t88rcucg1b7uk t35 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&gm_l19, t34, GM_CONV(gm_csguankko4lmhd, t35));
    {
        {
            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t37 = (uint8_t)(t36 == ((uint8_t)17ULL));
            if (!t37) {
                goto gm_lb2;
            }
            uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)&gm_l19) + 8));
            gm_l2 = t38;
            void * t39 = gm_l0;
            gm_b16_8 t40 = GM_LD(gm_b16_8, (((uint8_t *)t39) + 1848));
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
            uint32_t t42 = gm_l2;
            gm_tf5srh4mp4b3i t43 = gm_f_index_of_41b084m(t41, t42);
            gm_l21 = t43;
            {
                {
                    uint8_t t44 = (uint8_t)((((uint8_t *)&gm_l21))[8] != 0);
                    if (t44) {
                        goto gm_lb4;
                    }
                    gm_l20 = ((uint8_t)0);
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    gm_l20 = ((uint8_t)1);
                    goto gm_lb3;
                }
gm_lb5:;
                gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 797, 16);
            }
gm_lb3:;
            uint8_t t45 = gm_l20;
            if (t45) {
                uint32_t t46 = gm_l2;
                gm_t9805qtonbsag t47;
                GM_ST(uint8_t, (uint8_t *)&t47 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t47 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t47), t46);
                ((uint8_t *)&t47)[4] = 1;
                F.ret = t47;
                goto gm_fret;
            }
            {
                void * t48 = gm_l0;
                void * t49 = GM_LD(void *, (((uint8_t *)t48) + 1088));
                uint64_t t50 = GM_LD(uint64_t, ((((uint8_t *)t48) + 1088) + 8));
                uint32_t t51 = gm_l2;
                uint64_t t52 = (uint64_t)t51;
                if ((uintptr_t)t52 >= (uintptr_t)(t50)) gm_trap_index((uintptr_t)t52, t50, gm_src3k278vcmqgdr, 798, 21);
                memmove(((uint8_t *)&F.l3), ((((uint8_t *)t49) + (uintptr_t)t52 * 184) + 24), 56);
            }
            gm_l5 = ((uint64_t)0ULL);
            {
                uint64_t t53 = gm_f_member_count_otu9tc0(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l3)));
                gm_l4 = t53;
            }
            {
                for (;;) {
                    uint64_t t54 = gm_l5;
                    uint64_t t55 = gm_l4;
                    uint8_t t56 = (t54 < t55);
                    if (!t56) {
                        goto gm_lb6;
                    }
                    {
                        {
                            {
                                uint64_t t57 = gm_l5;
                                gm_l6 = t57;
                                uint64_t t58 = gm_l6;
                                gm_tkn42u7na3t81 t59 = gm_f_member_ty_esvuguv(&(*(gm_ti0kgh3pc3in2 *)((uint8_t *)&F.l3)), t58);
                                gm_l22 = t59;
                                uint8_t t60 = (uint8_t)((((uint8_t *)&gm_l22))[4] != 0);
                                if (t60) {
                                    gm_t88rcucg1b7uk t61 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l22));
                                    gm_l7 = t61;
                                } else {
                                    goto gm_lb8;
                                }
                                {
                                    {
                                        void * t62 = gm_l0;
                                        gm_t88rcucg1b7uk t63 = gm_l7;
                                        gm_t9805qtonbsag t64 = gm_f_pending_layout_ikknuv1(t62, GM_CONV(gm_csguankko4lmhd, t63));
                                        gm_l23 = t64;
                                        uint8_t t65 = (uint8_t)((((uint8_t *)&gm_l23))[4] != 0);
                                        if (!t65) {
                                            goto gm_lb12;
                                        }
                                        uint32_t t66 = GM_LD(uint32_t, ((uint8_t *)&gm_l23));
                                        gm_l8 = t66;
                                        uint32_t t67 = gm_l8;
                                        gm_t9805qtonbsag t68;
                                        GM_ST(uint8_t, (uint8_t *)&t68 + 5, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t68 + 6, 0);
                                        GM_ST(uint32_t, ((uint8_t *)&t68), t67);
                                        ((uint8_t *)&t68)[4] = 1;
                                        F.ret = t68;
                                        goto gm_fret;
                                    }
gm_lb12:;
                                }
gm_lb11:;
                                goto gm_lb9;
                            }
gm_lb10:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 799, 13);
                        }
gm_lb9:;
                    }
gm_lb8:;
                    uint64_t t69 = gm_l5;
                    uint64_t r70; bool o71 = __builtin_add_overflow(t69, ((uint64_t)1ULL), &r70);
                    uint64_t t72 = r70;
                    gm_l5 = t72;
                }
            }
gm_lb6:;
            gm_t9805qtonbsag t73;
            memset(&t73, 0, sizeof t73);
            F.ret = t73;
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t75 = (uint8_t)(t74 == ((uint8_t)11ULL));
            if (!t75) {
                goto gm_lb13;
            }
            memmove(((uint8_t *)&gm_l9), (((uint8_t *)&gm_l19) + 8), 24);
            void * t76 = gm_l0;
            gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l9) + 8));
            gm_t9805qtonbsag t78 = gm_f_pending_layout_ikknuv1(t76, GM_CONV(gm_csguankko4lmhd, t77));
            F.ret = t78;
            goto gm_fret;
        }
gm_lb13:;
        {
            uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t80 = (uint8_t)(t79 == ((uint8_t)20ULL));
            if (!t80) {
                goto gm_lb14;
            }
            gm_tgglga8ro3isc t81 = GM_LD(gm_tgglga8ro3isc, (((uint8_t *)&gm_l19) + 8));
            gm_l10 = t81;
            void * t82 = gm_l0;
            gm_t88rcucg1b7uk t83 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l10) + 4));
            gm_t9805qtonbsag t84 = gm_f_pending_layout_ikknuv1(t82, GM_CONV(gm_csguankko4lmhd, t83));
            F.ret = t84;
            goto gm_fret;
        }
gm_lb14:;
        {
            uint8_t t85 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t86 = (uint8_t)(t85 == ((uint8_t)13ULL));
            if (!t86) {
                goto gm_lb15;
            }
            gm_t88rcucg1b7uk t87 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l19) + 8));
            gm_l11 = t87;
            void * t88 = gm_l0;
            gm_t88rcucg1b7uk t89 = gm_l11;
            gm_t9805qtonbsag t90 = gm_f_pending_layout_ikknuv1(t88, GM_CONV(gm_csguankko4lmhd, t89));
            F.ret = t90;
            goto gm_fret;
        }
gm_lb15:;
        {
            uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t92 = (uint8_t)(t91 == ((uint8_t)12ULL));
            if (!t92) {
                goto gm_lb16;
            }
            memmove(((uint8_t *)&gm_l12), (((uint8_t *)&gm_l19) + 8), 32);
            gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l12));
            gm_l13 = t93;
            gm_l14 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t94 = gm_l14;
                    uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                    uint8_t t96 = (t94 < t95);
                    if (!t96) {
                        goto gm_lb17;
                    }
                    {
                        void * t97 = GM_LD(void *, ((uint8_t *)&gm_l13));
                        uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l13) + 8));
                        uint64_t t99 = gm_l14;
                        {
                            {
                                gm_t88rcucg1b7uk t100 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t97) + (uintptr_t)t99 * 4));
                                gm_l15 = t100;
                                {
                                    {
                                        void * t101 = gm_l0;
                                        gm_t88rcucg1b7uk t102 = gm_l15;
                                        gm_t9805qtonbsag t103 = gm_f_pending_layout_ikknuv1(t101, GM_CONV(gm_csguankko4lmhd, t102));
                                        gm_l24 = t103;
                                        uint8_t t104 = (uint8_t)((((uint8_t *)&gm_l24))[4] != 0);
                                        if (!t104) {
                                            goto gm_lb23;
                                        }
                                        uint32_t t105 = GM_LD(uint32_t, ((uint8_t *)&gm_l24));
                                        gm_l16 = t105;
                                        uint32_t t106 = gm_l16;
                                        gm_t9805qtonbsag t107;
                                        GM_ST(uint8_t, (uint8_t *)&t107 + 5, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t107 + 6, 0);
                                        GM_ST(uint32_t, ((uint8_t *)&t107), t106);
                                        ((uint8_t *)&t107)[4] = 1;
                                        F.ret = t107;
                                        goto gm_fret;
                                    }
gm_lb23:;
                                }
gm_lb22:;
                                goto gm_lb20;
                            }
gm_lb21:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 809, 13);
                        }
gm_lb20:;
                    }
gm_lb19:;
                    uint64_t t108 = gm_l14;
                    uint64_t r109; bool o110 = __builtin_add_overflow(t108, ((uint64_t)1ULL), &r109);
                    uint64_t t111 = r109;
                    gm_l14 = t111;
                }
            }
gm_lb17:;
            gm_t9805qtonbsag t112;
            memset(&t112, 0, sizeof t112);
            F.ret = t112;
            goto gm_fret;
        }
gm_lb16:;
        {
            uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&gm_l19) + 0));
            uint8_t t114 = (uint8_t)(t113 == ((uint8_t)14ULL));
            if (!t114) {
                goto gm_lb24;
            }
            gm_tgglga8ro3isc t115 = GM_LD(gm_tgglga8ro3isc, (((uint8_t *)&gm_l19) + 8));
            gm_l17 = t115;
            {
                {
                    void * t116 = gm_l0;
                    gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l17));
                    gm_t9805qtonbsag t118 = gm_f_pending_layout_ikknuv1(t116, GM_CONV(gm_csguankko4lmhd, t117));
                    gm_l25 = t118;
                    uint8_t t119 = (uint8_t)((((uint8_t *)&gm_l25))[4] != 0);
                    if (!t119) {
                        goto gm_lb26;
                    }
                    uint32_t t120 = GM_LD(uint32_t, ((uint8_t *)&gm_l25));
                    gm_l18 = t120;
                    uint32_t t121 = gm_l18;
                    gm_t9805qtonbsag t122;
                    GM_ST(uint8_t, (uint8_t *)&t122 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t122 + 6, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t122), t121);
                    ((uint8_t *)&t122)[4] = 1;
                    F.ret = t122;
                    goto gm_fret;
                }
gm_lb26:;
            }
gm_lb25:;
            void * t123 = gm_l0;
            gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l17) + 4));
            gm_t9805qtonbsag t125 = gm_f_pending_layout_ikknuv1(t123, GM_CONV(gm_csguankko4lmhd, t124));
            F.ret = t125;
            goto gm_fret;
        }
gm_lb24:;
        {
            gm_t9805qtonbsag t126;
            memset(&t126, 0, sizeof t126);
            F.ret = t126;
            goto gm_fret;
        }
gm_lb27:;
        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 795, 5);
    }
gm_lb1:;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_remove_in_progress_lrie478(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_struct_fields_chqu3it(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_struct_fields_chqu3it F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l4;
    {
        {
            void * t6 = gm_l0;
            void * t7 = (void *)(((uint8_t *)t6) + 8);
            gm_t88rcucg1b7uk t8 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l4, t7, GM_CONV(gm_csguankko4lmhd, t8));
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)17ULL));
            if (!t10) {
                goto gm_lb2;
            }
            uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 8));
            gm_l2 = t11;
            {
                {
                    {
                        void * t12 = gm_l0;
                        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 1088));
                        uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 1088) + 8));
                        uint32_t t15 = gm_l2;
                        uint64_t t16 = (uint64_t)t15;
                        if ((uintptr_t)t16 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t16, t14, gm_src3k278vcmqgdr, 496, 21);
                        uint8_t t17 = GM_LD(uint8_t, (((((uint8_t *)t13) + (uintptr_t)t16 * 184) + 24) + 0));
                        uint8_t t18 = (uint8_t)(t17 == ((uint8_t)0ULL));
                        if (!t18) {
                            goto gm_lb4;
                        }
                        memmove(((uint8_t *)&F.l3), (((((uint8_t *)t13) + (uintptr_t)t16 * 184) + 24) + 8), 48);
                    }
                    {
                        gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 17, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 18, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out) + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out)), t19);
                        ((uint8_t *)&(*(gm_th2lqbo77pvsc *)gm_out))[16] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb4:;
                memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
                goto gm_fdone;
            }
gm_lb3:;
        }
gm_lb2:;
        memset(&(*(gm_th2lqbo77pvsc *)gm_out), 0, sizeof (*(gm_th2lqbo77pvsc *)gm_out));
        goto gm_fdone;
    }
gm_lb1:;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_struct_fields_chqu3it_c(void * a0, gm_csguankko4lmhd a1) { gm_th2lqbo77pvsc gm_r; gm_f_struct_fields_chqu3it(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_struct_kind_5l1hs46(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint32_t a2, void * a3, gm_b16_8 a4) {
    struct gm_fr_struct_kind_5l1hs46 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    uint32_t gm_l2; /* id */
    void * gm_l3; /* st */
    gm_b16_8 gm_l4; /* attrs */
    uint8_t gm_l5; /* packed */
    gm_tf5srh4mp4b3i gm_l6; /* align */
    gm_tkn42u7na3t81 gm_l7; /* bits */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l11; /* args */
    gm_b16_8 gm_l12; /* other */
    uint8_t gm_l16; /* has_using */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t88rcucg1b7uk gm_l22; /* ty */
    gm_tf5srh4mp4b3i gm_l23; /* field_align */
    gm_b16_8 gm_l24; /* <tmp> */
    uint64_t gm_l25; /* <tmp> */
    gm_b16_8 gm_l27; /* other */
    void * gm_l28; /* d */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    gm_t08olq97rscn3 gm_l31; /* n */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    gm_t88rcucg1b7uk gm_l35; /* b */
    GM_STACK_CHECK(&F, 2785u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tabb710d9mq2e gm_l36;
    gm_t8uai8b3boj78 gm_l37;
    uint8_t gm_l38;
    gm_t53at80pfdus3 gm_l39;
    gm_tabb710d9mq2e gm_l40;
    uint8_t gm_l41;
    uint8_t gm_l42;
    gm_t53at80pfdus3 gm_l43;
    gm_tabb710d9mq2e gm_l44;
    gm_tabb710d9mq2e gm_l45;
    gm_tftb1odc739t5 gm_l46;
    uint8_t gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    {
        gm_l5 = ((uint8_t)0);
        gm_tf5srh4mp4b3i t50;
        memset(&t50, 0, sizeof t50);
        gm_l6 = t50;
        gm_tkn42u7na3t81 t51;
        memset(&t51, 0, sizeof t51);
        gm_l7 = t51;
        gm_b16_8 t52 = gm_l4;
        gm_l8 = t52;
        gm_l9 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t53 = gm_l9;
                uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint8_t t55 = (t53 < t54);
                if (!t55) {
                    goto gm_lb1;
                }
                {
                    void * t56 = GM_LD(void *, ((uint8_t *)&gm_l8));
                    uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint64_t t58 = gm_l9;
                    {
                        {
                            memmove(((uint8_t *)&F.l10), (((uint8_t *)t56) + (uintptr_t)t58 * 72), 72);
                            {
                                {
                                    gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                    gm_b16_8 t60;
                                    GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s2ph9f020upd2));
                                    GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(6));
                                    uint8_t t61 = (gm_eq0iqiubfm7e(((uint8_t *)&t59), ((uint8_t *)&t60)));
                                    if (!t61) {
                                        goto gm_lb7;
                                    }
                                    gm_l5 = ((uint8_t)1);
                                    goto gm_lb6;
                                }
gm_lb7:;
                                {
                                    gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                    gm_b16_8 t63;
                                    GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sj1k4t2de5144));
                                    GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(5));
                                    uint8_t t64 = (gm_eq0iqiubfm7e(((uint8_t *)&t62), ((uint8_t *)&t63)));
                                    if (!t64) {
                                        goto gm_lb8;
                                    }
                                    void * t65 = gm_l0;
                                    uint64_t t66 = gm_l1;
                                    void * t67 = (void *)((uint8_t *)&F.l10);
                                    gm_tf5srh4mp4b3i t68 = gm_f_attr_power_of_two_gj02n2t((void *)NULL, cx, NULL, t65, t66, t67);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l6 = t68;
                                    goto gm_lb6;
                                }
gm_lb8:;
                                {
                                    gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                    gm_b16_8 t70;
                                    GM_ST(void *, (uint8_t *)&t70, (void *)(gm_s6vvtrmtnt3j1));
                                    GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(4));
                                    uint8_t t71 = (gm_eq0iqiubfm7e(((uint8_t *)&t69), ((uint8_t *)&t70)));
                                    if (!t71) {
                                        goto gm_lb9;
                                    }
                                    uint8_t t72 = (uint8_t)(((((uint8_t *)&F.l10) + 32))[16] != 0);
                                    if (t72) {
                                        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 32));
                                        gm_l11 = t73;
                                    } else {
                                        gm_b16_8 t74 = gm_f_empty_slice_g3tddrb();
                                        gm_l11 = t74;
                                    }
                                    uint64_t t75 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                    uint8_t t76 = ((t75 == ((uint64_t)1ULL)));
                                    if (t76) {
                                        void * t77 = gm_l0;
                                        uint64_t t78 = gm_l1;
                                        void * t79 = GM_LD(void *, ((uint8_t *)&gm_l11));
                                        uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t80)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t80, gm_src3k278vcmqgdr, 199, 52);
                                        void * t81 = (void *)(((uint8_t *)t79) + (uintptr_t)((uint64_t)0ULL) * 88);
                                        gm_t88rcucg1b7uk t82 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t77, t78, t81);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tkn42u7na3t81 t83;
                                        GM_ST(uint8_t, (uint8_t *)&t83 + 5, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t83 + 6, 0);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t83), t82);
                                        ((uint8_t *)&t83)[4] = 1;
                                        gm_l7 = t83;
                                    } else {
                                        void * t84 = gm_l0;
                                        gm_tl874t2atnlh3 t85 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 56));
                                        gm_b16_8 t86;
                                        GM_ST(void *, (uint8_t *)&t86, (void *)(gm_s3lik4tvvjana));
                                        GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(64));
                                        gm_b0_1 t87 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t84, t85, t86);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    goto gm_lb6;
                                }
gm_lb9:;
                                {
                                    gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                                    gm_l12 = t88;
                                    void * t89 = gm_l0;
                                    gm_tl874t2atnlh3 t90 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l10) + 56));
                                    gm_b16_8 t91 = gm_l12;
                                    gm_tnpifv20irnph t92;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t92 + 0), t91);
                                    gm_f_format_5iu9hua((void *)&gm_l36, (void *)NULL, cx, NULL, t92);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                                    if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 8, gm_src3k278vcmqgdr, 204, 38);
                                    gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l36) + 8));
                                    gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
                                    gm_b0_1 t96 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t89, t90, t95);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb6;
                                }
gm_lb10:;
                                gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 193, 9);
                            }
gm_lb6:;
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 192, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t97 = gm_l9;
                uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                uint64_t t100 = r98;
                gm_l9 = t100;
            }
        }
gm_lb1:;
    }
    {
        gm_trqfvmjtt4sij t101;
        gm_f_list_spqn36o((void *)&t101, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t101;
        gm_trqfvmjtt4sij t102;
        gm_f_list_s83jn2q((void *)&t102, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l14 = t102;
        gm_trqfvmjtt4sij t103;
        gm_f_list_pmp06kq_2((void *)&t103, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l15 = t103;
        gm_l16 = ((uint8_t)0);
        gm_trqfvmjtt4sij t104;
        gm_f_list_gst7tuh((void *)&t104, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l17 = t104;
        void * t105 = gm_l0;
        uint64_t t106 = gm_l1;
        void * t107 = gm_l3;
        gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)t107) + 40));
        gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
        void * t110 = (void *)((uint8_t *)&F.l17);
        gm_b0_1 t111 = gm_f_taken_struct_members_1ltbb18((void *)NULL, cx, NULL, t105, t106, t109, t110);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
        gm_b16_8 t113 = GM_LD(gm_b16_8, ((uint8_t *)&t112));
        gm_l18 = t113;
        gm_l19 = ((uint64_t)0ULL);
        {
            for (;;) {
                {
                    uint64_t t114 = gm_l19;
                    uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint8_t t116 = (t114 < t115);
                    if (!t116) {
                        goto gm_lb11;
                    }
                    {
                        {
                            uint64_t t117 = gm_l19;
                            void * t118 = GM_LD(void *, ((uint8_t *)&gm_l18));
                            uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                            uint64_t t120 = gm_l19;
                            gm_t24301fas1b2a t121 = GM_LD(gm_t24301fas1b2a, (((uint8_t *)t118) + (uintptr_t)t120 * 72));
                            GM_ST(uint64_t, ((uint8_t *)&gm_l37 + 0), t117);
                            GM_ST(gm_t24301fas1b2a, ((uint8_t *)&gm_l37 + 8), t121);
                            {
                                {
                                    uint64_t t122 = GM_LD(uint64_t, ((uint8_t *)&gm_l37));
                                    gm_l20 = t122;
                                    {
                                        memmove(((uint8_t *)&F.l21), (((uint8_t *)&gm_l37) + 8), 72);
                                    }
                                    void * t123 = gm_l0;
                                    uint64_t t124 = gm_l1;
                                    void * t125 = GM_LD(void *, (((uint8_t *)&F.l21) + 40));
                                    gm_t88rcucg1b7uk t126 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t123, t124, t125);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l22 = t126;
                                    {
                                        void * t127 = gm_l0;
                                        void * t128 = (void *)(((uint8_t *)t127) + 8);
                                        gm_t88rcucg1b7uk t129 = gm_l22;
                                        gm_f_kind_jcnt4hm((void *)&gm_l39, t128, GM_CONV(gm_csguankko4lmhd, t129));
                                    }
                                    {
                                        {
                                            uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&gm_l39) + 0));
                                            uint8_t t131 = (uint8_t)(t130 == ((uint8_t)15ULL));
                                            if (!t131) {
                                                goto gm_lb17;
                                            }
                                            gm_l38 = ((uint8_t)1);
                                            goto gm_lb16;
                                        }
gm_lb17:;
                                        {
                                            gm_l38 = ((uint8_t)0);
                                            goto gm_lb16;
                                        }
gm_lb18:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 215, 12);
                                    }
gm_lb16:;
                                    uint8_t t132 = gm_l38;
                                    if (t132) {
                                        void * t133 = gm_l0;
                                        void * t134 = GM_LD(void *, (((uint8_t *)&F.l21) + 40));
                                        gm_tl874t2atnlh3 t135 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t134) + 72));
                                        gm_b16_8 t136;
                                        GM_ST(void *, (uint8_t *)&t136, (void *)(gm_sacmu7hr5dknu));
                                        GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(87));
                                        gm_b0_1 t137 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t133, t135, t136);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    gm_tf5srh4mp4b3i t138;
                                    memset(&t138, 0, sizeof t138);
                                    gm_l23 = t138;
                                    gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
                                    gm_l24 = t139;
                                    gm_l25 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t140 = gm_l25;
                                            uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                            uint8_t t142 = (t140 < t141);
                                            if (!t142) {
                                                goto gm_lb19;
                                            }
                                            {
                                                void * t143 = GM_LD(void *, ((uint8_t *)&gm_l24));
                                                uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
                                                uint64_t t145 = gm_l25;
                                                {
                                                    {
                                                        memmove(((uint8_t *)&F.l26), (((uint8_t *)t143) + (uintptr_t)t145 * 72), 72);
                                                        {
                                                            {
                                                                gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
                                                                gm_b16_8 t147;
                                                                GM_ST(void *, (uint8_t *)&t147, (void *)(gm_sj1k4t2de5144));
                                                                GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(5));
                                                                uint8_t t148 = (gm_eq0iqiubfm7e(((uint8_t *)&t146), ((uint8_t *)&t147)));
                                                                if (!t148) {
                                                                    goto gm_lb25;
                                                                }
                                                                void * t149 = gm_l0;
                                                                uint64_t t150 = gm_l1;
                                                                void * t151 = (void *)((uint8_t *)&F.l26);
                                                                gm_tf5srh4mp4b3i t152 = gm_f_attr_power_of_two_gj02n2t((void *)NULL, cx, NULL, t149, t150, t151);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                gm_l23 = t152;
                                                                goto gm_lb24;
                                                            }
gm_lb25:;
                                                            {
                                                                gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&F.l26));
                                                                gm_l27 = t153;
                                                                void * t154 = gm_l0;
                                                                gm_tl874t2atnlh3 t155 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l26) + 56));
                                                                gm_b16_8 t156 = gm_l27;
                                                                gm_tnpifv20irnph t157;
                                                                GM_ST(gm_b16_8, ((uint8_t *)&t157 + 0), t156);
                                                                gm_f_format_slub3h9((void *)&gm_l40, (void *)NULL, cx, NULL, t157);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                uint8_t t158 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                                                                if (!t158) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 8, gm_src3k278vcmqgdr, 222, 42);
                                                                gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l40) + 8));
                                                                gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
                                                                gm_b0_1 t161 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t154, t155, t160);
                                                                if (gm_unw.kind) {
                                                                    goto gm_lad;
                                                                }
                                                                goto gm_lb24;
                                                            }
gm_lb26:;
                                                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 220, 13);
                                                        }
gm_lb24:;
                                                        goto gm_lb22;
                                                    }
gm_lb23:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 219, 9);
                                                }
gm_lb22:;
                                            }
gm_lb21:;
                                            uint64_t t162 = gm_l25;
                                            uint64_t r163; bool o164 = __builtin_add_overflow(t162, ((uint64_t)1ULL), &r163);
                                            uint64_t t165 = r163;
                                            gm_l25 = t165;
                                        }
                                    }
gm_lb19:;
                                    uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&F.l21) + 17));
                                    if (t166) {
                                        uint64_t t167 = gm_l20;
                                        uint8_t t168 = (!(t167 == ((uint64_t)0ULL)));
                                        if (t168) {
                                            gm_l41 = ((uint8_t)1);
                                        } else {
                                            uint64_t t169 = GM_LD(uint64_t, ((((uint8_t *)&F.l21) + 24) + 8));
                                            uint8_t t170 = (!(t169 == ((uint64_t)1ULL)));
                                            gm_l41 = t170;
                                        }
                                        uint8_t t171 = gm_l41;
                                        if (t171) {
                                            void * t172 = gm_l0;
                                            gm_tl874t2atnlh3 t173 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l21) + 56));
                                            gm_b16_8 t174;
                                            GM_ST(void *, (uint8_t *)&t174, (void *)(gm_sisjr6ep7j5j0));
                                            GM_ST(uintptr_t, (uint8_t *)&t174 + 8, (uintptr_t)(78));
                                            gm_b0_1 t175 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t172, t173, t174);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                        } else {
                                            {
                                                {
                                                    {
                                                        void * t176 = gm_l0;
                                                        void * t177 = (void *)(((uint8_t *)t176) + 8);
                                                        gm_t88rcucg1b7uk t178 = gm_l22;
                                                        gm_f_kind_jcnt4hm((void *)&gm_l43, t177, GM_CONV(gm_csguankko4lmhd, t178));
                                                    }
                                                    {
                                                        {
                                                            uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l43) + 0));
                                                            uint8_t t180 = (uint8_t)(t179 == ((uint8_t)17ULL));
                                                            if (!t180) {
                                                                goto gm_lb30;
                                                            }
                                                            gm_l42 = ((uint8_t)0);
                                                            goto gm_lb29;
                                                        }
gm_lb30:;
                                                        {
                                                            gm_l42 = ((uint8_t)1);
                                                            goto gm_lb29;
                                                        }
gm_lb31:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 228, 23);
                                                    }
gm_lb29:;
                                                    uint8_t t181 = gm_l42;
                                                    if (!t181) {
                                                        goto gm_lb28;
                                                    }
                                                    void * t182 = gm_l0;
                                                    void * t183 = (void *)(((uint8_t *)t182) + 8);
                                                    gm_t88rcucg1b7uk t184 = gm_l22;
                                                    uint8_t t185 = gm_f_is_error_8k27gd1(t183, GM_CONV(gm_csguankko4lmhd, t184));
                                                    uint8_t t186 = (uint8_t)!t185;
                                                    if (!t186) {
                                                        goto gm_lb28;
                                                    }
                                                    {
                                                        void * t187 = gm_l0;
                                                        gm_tl874t2atnlh3 t188 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l21) + 56));
                                                        void * t189 = gm_l0;
                                                        gm_t88rcucg1b7uk t190 = gm_l22;
                                                        gm_b16_8 t191 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t189, t190);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        gm_tnpifv20irnph t192;
                                                        GM_ST(gm_b16_8, ((uint8_t *)&t192 + 0), t191);
                                                        gm_f_format_fiq5e0p((void *)&gm_l44, (void *)NULL, cx, NULL, t192);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t193 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                                                        if (!t193) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 8, gm_src3k278vcmqgdr, 229, 33);
                                                        gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l44) + 8));
                                                        gm_b16_8 t195 = GM_LD(gm_b16_8, ((uint8_t *)&t194));
                                                        gm_b0_1 t196 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t187, t188, t195);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                    }
                                                    goto gm_lb27;
                                                }
gm_lb28:;
                                                gm_l16 = ((uint8_t)1);
                                            }
gm_lb27:;
                                        }
                                    }
                                    {
                                        {
                                            uint8_t t197 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l21) + 48)) != 0);
                                            if (!t197) {
                                                goto gm_lb33;
                                            }
                                            void * t198 = GM_LD(void *, (((uint8_t *)&F.l21) + 48));
                                            gm_l28 = t198;
                                            void * t199 = gm_l0;
                                            uint64_t t200 = gm_l1;
                                            void * t201 = gm_l28;
                                            gm_t88rcucg1b7uk t202 = gm_l22;
                                            gm_b0_1 t203 = gm_f_check_default_4bf68q8((void *)NULL, cx, NULL, t199, t200, t201, t202);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_lb32;
                                        }
gm_lb33:;
                                    }
gm_lb32:;
                                    gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 24));
                                    gm_l29 = t204;
                                    gm_l30 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t205 = gm_l30;
                                            uint64_t t206 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                                            uint8_t t207 = (t205 < t206);
                                            if (!t207) {
                                                goto gm_lb34;
                                            }
                                            {
                                                void * t208 = GM_LD(void *, ((uint8_t *)&gm_l29));
                                                uint64_t t209 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                                                uint64_t t210 = gm_l30;
                                                {
                                                    {
                                                        {
                                                            memmove(((uint8_t *)&gm_l31), (((uint8_t *)t208) + (uintptr_t)t210 * 32), 32);
                                                        }
                                                        gm_b16_8 t211 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
                                                        gm_b16_8 t212;
                                                        GM_ST(void *, (uint8_t *)&t212, (void *)(gm_sfdbfrv46lihd));
                                                        GM_ST(uintptr_t, (uint8_t *)&t212 + 8, (uintptr_t)(1));
                                                        uint8_t t213 = (!gm_eq0iqiubfm7e(((uint8_t *)&t211), ((uint8_t *)&t212)));
                                                        if (t213) {
                                                            gm_b16_8 t214 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                                                            gm_l32 = t214;
                                                            gm_l33 = ((uint64_t)0ULL);
                                                            {
                                                                for (;;) {
                                                                    uint64_t t215 = gm_l33;
                                                                    uint64_t t216 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                                                    uint8_t t217 = (t215 < t216);
                                                                    if (!t217) {
                                                                        goto gm_lb39;
                                                                    }
                                                                    {
                                                                        void * t218 = GM_LD(void *, ((uint8_t *)&gm_l32));
                                                                        uint64_t t219 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                                                                        uint64_t t220 = gm_l33;
                                                                        {
                                                                            {
                                                                                memmove(((uint8_t *)&F.l34), (((uint8_t *)t218) + (uintptr_t)t220 * 40), 40);
                                                                                gm_b16_8 t221 = GM_LD(gm_b16_8, ((uint8_t *)&F.l34));
                                                                                gm_b16_8 t222 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
                                                                                uint8_t t223 = (gm_eq0iqiubfm7e(((uint8_t *)&t221), ((uint8_t *)&t222)));
                                                                                if (t223) {
                                                                                    void * t224 = gm_l0;
                                                                                    gm_tl874t2atnlh3 t225 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l31) + 16));
                                                                                    gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
                                                                                    gm_tnpifv20irnph t227;
                                                                                    GM_ST(gm_b16_8, ((uint8_t *)&t227 + 0), t226);
                                                                                    gm_f_format_atintr3((void *)&gm_l45, (void *)NULL, cx, NULL, t227);
                                                                                    if (gm_unw.kind) {
                                                                                        goto gm_lad;
                                                                                    }
                                                                                    uint8_t t228 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                                                                    if (!t228) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 8, gm_src3k278vcmqgdr, 241, 41);
                                                                                    gm_b16_8 t229 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                                                                                    gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&t229));
                                                                                    gm_b0_1 t231 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t224, t225, t230);
                                                                                    if (gm_unw.kind) {
                                                                                        goto gm_lad;
                                                                                    }
                                                                                    goto gm_lb39;
                                                                                }
                                                                                goto gm_lb42;
                                                                            }
gm_lb43:;
                                                                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 239, 17);
                                                                        }
gm_lb42:;
                                                                    }
gm_lb41:;
                                                                    uint64_t t232 = gm_l33;
                                                                    uint64_t r233; bool o234 = __builtin_add_overflow(t232, ((uint64_t)1ULL), &r233);
                                                                    uint64_t t235 = r233;
                                                                    gm_l33 = t235;
                                                                }
                                                            }
gm_lb39:;
                                                        }
                                                        {
                                                            void * t236 = (void *)((uint8_t *)&F.l13);
                                                            gm_b16_8 t237 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
                                                            gm_t88rcucg1b7uk t238 = gm_l22;
                                                            gm_t9qkh0rsooc8d t239 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l21) + 16));
                                                            gm_tf5srh4mp4b3i t240 = gm_l23;
                                                            gm_t0mglksrkfa5e t241;
                                                            GM_ST(uint8_t, (uint8_t *)&t241 + 21, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&t241 + 22, 0);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t241 + 0), t237);
                                                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t241 + 16), t238);
                                                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t241 + 20), t239);
                                                            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t241 + 24), t240);
                                                            gm_tftb1odc739t5 t242 = gm_f_push_vrmo7c5(t236, &t241);
                                                            gm_l46 = t242;
                                                        }
                                                        uint8_t t243 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                                                        if (!t243) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_src3k278vcmqgdr, 246, 13);
                                                        void * t244 = (void *)((uint8_t *)&F.l14);
                                                        {
                                                            {
                                                                uint8_t t245 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l21) + 48)) != 0);
                                                                if (t245) {
                                                                    goto gm_lb45;
                                                                }
                                                                gm_l47 = ((uint8_t)0);
                                                                goto gm_lb44;
                                                            }
gm_lb45:;
                                                            {
                                                                gm_l47 = ((uint8_t)1);
                                                                goto gm_lb44;
                                                            }
gm_lb46:;
                                                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 247, 27);
                                                        }
gm_lb44:;
                                                        uint8_t t246 = gm_l47;
                                                        gm_tftb1odc739t5 t247 = gm_f_push_vd1290p(t244, t246);
                                                        gm_l48 = t247;
                                                        uint8_t t248 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                                        if (!t248) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_src3k278vcmqgdr, 247, 13);
                                                        void * t249 = (void *)((uint8_t *)&F.l15);
                                                        void * t250 = GM_LD(void *, (((uint8_t *)&F.l21) + 48));
                                                        gm_tftb1odc739t5 t251 = gm_f_push_r2k6rjf_2(t249, t250);
                                                        gm_l49 = t251;
                                                        uint8_t t252 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
                                                        if (!t252) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_src3k278vcmqgdr, 248, 13);
                                                        goto gm_lb37;
                                                    }
gm_lb38:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 237, 9);
                                                }
gm_lb37:;
                                            }
gm_lb36:;
                                            uint64_t t253 = gm_l30;
                                            uint64_t r254; bool o255 = __builtin_add_overflow(t253, ((uint64_t)1ULL), &r254);
                                            uint64_t t256 = r254;
                                            gm_l30 = t256;
                                        }
                                    }
gm_lb34:;
                                    goto gm_lb14;
                                }
gm_lb15:;
                                gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 213, 5);
                            }
gm_lb14:;
                        }
                    }
gm_lb13:;
                }
                {
                    uint64_t t257 = gm_l19;
                    uint64_t r258; bool o259 = __builtin_add_overflow(t257, ((uint64_t)1ULL), &r258);
                    uint64_t t260 = r258;
                    gm_l19 = t260;
                }
            }
        }
gm_lb11:;
    }
    {
        {
            {
                uint8_t t261 = (uint8_t)((((uint8_t *)&gm_l7))[4] != 0);
                if (!t261) {
                    goto gm_lb48;
                }
                gm_t88rcucg1b7uk t262 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l7));
                gm_l35 = t262;
                void * t263 = gm_l0;
                void * t264 = gm_l3;
                gm_tl874t2atnlh3 t265 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t264) + 56));
                gm_t88rcucg1b7uk t266 = gm_l35;
                gm_b16_8 t267 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&t267));
                gm_b0_1 t269 = gm_f_check_bits_struct_ophej5b((void *)NULL, cx, NULL, t263, t265, t266, t268);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb47;
            }
gm_lb48:;
        }
gm_lb47:;
        void * t270 = gm_l0;
        void * t271 = GM_LD(void *, (((uint8_t *)t270) + 1088));
        uint64_t t272 = GM_LD(uint64_t, ((((uint8_t *)t270) + 1088) + 8));
        uint32_t t273 = gm_l2;
        uint64_t t274 = (uint64_t)t273;
        if ((uintptr_t)t274 >= (uintptr_t)(t272)) gm_trap_index((uintptr_t)t274, t272, gm_src3k278vcmqgdr, 252, 5);
        gm_b16_8 t275 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
        GM_ST(gm_b16_8, ((((uint8_t *)t271) + (uintptr_t)t274 * 184) + 80), t275);
        void * t276 = gm_l0;
        void * t277 = GM_LD(void *, (((uint8_t *)t276) + 1088));
        uint64_t t278 = GM_LD(uint64_t, ((((uint8_t *)t276) + 1088) + 8));
        uint32_t t279 = gm_l2;
        uint64_t t280 = (uint64_t)t279;
        if ((uintptr_t)t280 >= (uintptr_t)(t278)) gm_trap_index((uintptr_t)t280, t278, gm_src3k278vcmqgdr, 253, 5);
        gm_b16_8 t281 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
        GM_ST(gm_b16_8, ((((uint8_t *)t277) + (uintptr_t)t280 * 184) + 96), t281);
        void * t282 = gm_l0;
        void * t283 = GM_LD(void *, (((uint8_t *)t282) + 1088));
        uint64_t t284 = GM_LD(uint64_t, ((((uint8_t *)t282) + 1088) + 8));
        uint32_t t285 = gm_l2;
        uint64_t t286 = (uint64_t)t285;
        if ((uintptr_t)t286 >= (uintptr_t)(t284)) gm_trap_index((uintptr_t)t286, t284, gm_src3k278vcmqgdr, 254, 5);
        uint8_t t287 = gm_l16;
        GM_ST(uint8_t, ((((uint8_t *)t283) + (uintptr_t)t286 * 184) + 112), t287);
        gm_b16_8 t288 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
        uint8_t t289 = gm_l5;
        gm_tf5srh4mp4b3i t290 = gm_l6;
        gm_tkn42u7na3t81 t291 = gm_l7;
        gm_tpautr4br7h3l t292;
        GM_ST(uint8_t, (uint8_t *)&t292 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t292 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t292 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t292 + 0), t288);
        GM_ST(uint8_t, ((uint8_t *)&t292 + 16), t289);
        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t292 + 24), t290);
        GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t292 + 40), t291);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 0), ((uint8_t)0ULL));
        GM_ST(gm_tpautr4br7h3l, ((uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 8), t292);
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
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_subst_a2s1mb3 F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* map */
    void * gm_l3; /* tys */
    uint32_t gm_l5; /* p */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tplpbqf3duvn3 gm_l8; /* m */
    uint32_t gm_l9; /* id */
    gm_b16_8 gm_l10; /* args */
    gm_th3eueiipivq6 gm_l11; /* shape */
    gm_t88rcucg1b7uk gm_l12; /* elem */
    gm_t88rcucg1b7uk gm_l13; /* x */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_t88rcucg1b7uk gm_l18; /* x */
    uint8_t gm_l21; /* c */
    gm_t88rcucg1b7uk gm_l22; /* x */
    uint8_t gm_l23; /* c */
    gm_t88rcucg1b7uk gm_l24; /* x */
    uint8_t gm_l25; /* c */
    gm_t88rcucg1b7uk gm_l26; /* x */
    uint64_t gm_l27; /* len */
    gm_t88rcucg1b7uk gm_l28; /* elem */
    gm_tnnjeerolos0a gm_l29; /* kind */
    gm_tnnjeerolos0a gm_l30; /* ak */
    gm_t88rcucg1b7uk gm_l31; /* e */
    gm_t88rcucg1b7uk gm_l32; /* len */
    gm_t88rcucg1b7uk gm_l33; /* elem */
    gm_t88rcucg1b7uk gm_l34; /* l */
    gm_t88rcucg1b7uk gm_l35; /* el */
    gm_t428ue2u3hf5g gm_l36; /* key */
    uint64_t gm_l37; /* n */
    gm_t9qkh0rsooc8d gm_l38; /* op */
    gm_t88rcucg1b7uk gm_l39; /* lhs */
    gm_t88rcucg1b7uk gm_l40; /* rhs */
    gm_t88rcucg1b7uk gm_l41; /* a */
    gm_t88rcucg1b7uk gm_l42; /* b */
    gm_t88rcucg1b7uk gm_l43; /* x */
    gm_t88rcucg1b7uk gm_l44; /* x */
    gm_t88rcucg1b7uk gm_l45; /* err */
    gm_t88rcucg1b7uk gm_l46; /* ok */
    gm_t88rcucg1b7uk gm_l47; /* e */
    gm_t88rcucg1b7uk gm_l48; /* o */
    gm_b16_8 gm_l49; /* elems */
    gm_b16_8 gm_l50; /* names */
    gm_b16_8 gm_l52; /* <tmp> */
    uint64_t gm_l53; /* <tmp> */
    gm_t88rcucg1b7uk gm_l54; /* e */
    gm_tnpifv20irnph gm_l55; /* set */
    GM_STACK_CHECK(&F, 2940u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l56;
    gm_tbm0n9ujhrlhf gm_l57;
    gm_t88rcucg1b7uk gm_l58;
    gm_tftb1odc739t5 gm_l59;
    gm_t53at80pfdus3 gm_l60;
    gm_tf5srh4mp4b3i gm_l61;
    gm_tftb1odc739t5 gm_l62;
    {
        uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint8_t t48 = ((t47 == ((uint64_t)0ULL)));
        if (t48) {
            gm_l56 = ((uint8_t)1);
        } else {
            void * t49 = gm_l0;
            void * t50 = (void *)(((uint8_t *)t49) + 8);
            gm_t88rcucg1b7uk t51 = gm_l1;
            uint8_t t52 = gm_f_has_param_u9t0efs(t50, GM_CONV(gm_csguankko4lmhd, t51));
            uint8_t t53 = (uint8_t)!t52;
            gm_l56 = t53;
        }
        uint8_t t54 = gm_l56;
        if (t54) {
            gm_t88rcucg1b7uk t55 = gm_l1;
            F.ret = t55;
            goto gm_fret;
        }
        void * t56 = gm_l0;
        void * t57 = (void *)(((uint8_t *)t56) + 8);
        gm_l3 = t57;
        void * t58 = gm_l3;
        gm_t88rcucg1b7uk t59 = gm_l1;
        gm_f_kind_jcnt4hm((void *)&F.l4, t58, GM_CONV(gm_csguankko4lmhd, t59));
        {
            {
                {
                    uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t61 = (uint8_t)(t60 == ((uint8_t)19ULL));
                    if (!t61) {
                        goto gm_lb2;
                    }
                    uint32_t t62 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 8));
                    gm_l5 = t62;
                    gm_b16_8 t63 = gm_l2;
                    gm_l6 = t63;
                    gm_l7 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t64 = gm_l7;
                            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint8_t t66 = (t64 < t65);
                            if (!t66) {
                                goto gm_lb3;
                            }
                            {
                                void * t67 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                uint64_t t69 = gm_l7;
                                {
                                    {
                                        gm_tplpbqf3duvn3 t70 = GM_LD(gm_tplpbqf3duvn3, (((uint8_t *)t67) + (uintptr_t)t69 * 8));
                                        gm_l8 = t70;
                                        uint32_t t71 = GM_LD(uint32_t, ((uint8_t *)&gm_l8));
                                        uint32_t t72 = gm_l5;
                                        uint8_t t73 = ((t71 == t72));
                                        if (t73) {
                                            gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l8) + 4));
                                            F.ret = t74;
                                            goto gm_fret;
                                        }
                                        goto gm_lb6;
                                    }
gm_lb7:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 683, 13);
                                }
gm_lb6:;
                            }
gm_lb5:;
                            uint64_t t75 = gm_l7;
                            uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                            uint64_t t78 = r76;
                            gm_l7 = t78;
                        }
                    }
gm_lb3:;
                    gm_t88rcucg1b7uk t79 = gm_l1;
                    F.ret = t79;
                    goto gm_fret;
                }
gm_lb2:;
                {
                    uint8_t t80 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t81 = (uint8_t)(t80 == ((uint8_t)17ULL));
                    if (!t81) {
                        goto gm_lb8;
                    }
                    uint32_t t82 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 8));
                    gm_l9 = t82;
                    gm_b16_8 t83 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 8));
                    gm_l10 = t83;
                    uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint8_t t85 = ((t84 == ((uint64_t)0ULL)));
                    if (t85) {
                        gm_t88rcucg1b7uk t86 = gm_l1;
                        F.ret = t86;
                        goto gm_fret;
                    }
                    {
                        {
                            {
                                void * t87 = gm_l0;
                                void * t88 = gm_l3;
                                uint32_t t89 = gm_l9;
                                uint32_t t90 = gm_f_template_9o92cvv(t88, t89);
                                gm_f_soa_shape_of_ohenvjc((void *)&gm_l57, t87, t90);
                            }
                            uint8_t t91 = (uint8_t)((((uint8_t *)&gm_l57))[24] != 0);
                            if (!t91) {
                                goto gm_lb10;
                            }
                            {
                                memmove(((uint8_t *)&gm_l11), ((uint8_t *)&gm_l57), 24);
                            }
                            void * t92 = gm_l3;
                            gm_t88rcucg1b7uk t93 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t92);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l12 = t93;
                            {
                                {
                                    void * t94 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                    uint64_t t95 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t95)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t95, gm_src3k278vcmqgdr, 693, 32);
                                    uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)t94) + (uintptr_t)((uint64_t)0ULL) * 40) + 0));
                                    uint8_t t97 = (uint8_t)(t96 == ((uint8_t)0ULL));
                                    if (!t97) {
                                        goto gm_lb12;
                                    }
                                    gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t94) + (uintptr_t)((uint64_t)0ULL) * 40) + 8));
                                    gm_l13 = t98;
                                    void * t99 = gm_l0;
                                    gm_t88rcucg1b7uk t100 = gm_l13;
                                    gm_b16_8 t101 = gm_l2;
                                    gm_t88rcucg1b7uk t102 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t99, t100, t101);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l12 = t102;
                                    goto gm_lb11;
                                }
gm_lb12:;
                            }
gm_lb11:;
                            {
                                void * t103 = gm_l0;
                                gm_t88rcucg1b7uk t104 = gm_l12;
                                gm_t88rcucg1b7uk t105 = gm_f_subst_soa_2chqte3((void *)NULL, cx, NULL, t103, t104, &(*(gm_th3eueiipivq6 *)((uint8_t *)&gm_l11)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t105;
                                goto gm_fret;
                            }
                        }
gm_lb10:;
                    }
gm_lb9:;
                    gm_trqfvmjtt4sij t106;
                    gm_f_list_nof7vfj((void *)&t106, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.l14 = t106;
                    gm_b16_8 t107 = gm_l10;
                    gm_l15 = t107;
                    gm_l16 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t108 = gm_l16;
                            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            uint8_t t110 = (t108 < t109);
                            if (!t110) {
                                goto gm_lb13;
                            }
                            {
                                void * t111 = GM_LD(void *, ((uint8_t *)&gm_l15));
                                uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                uint64_t t113 = gm_l16;
                                {
                                    {
                                        memmove(((uint8_t *)&F.l17), (((uint8_t *)t111) + (uintptr_t)t113 * 40), 40);
                                        void * t114 = (void *)((uint8_t *)&F.l14);
                                        {
                                            {
                                                uint8_t t115 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                                                uint8_t t116 = (uint8_t)(t115 == ((uint8_t)0ULL));
                                                if (!t116) {
                                                    goto gm_lb19;
                                                }
                                                gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 8));
                                                gm_l18 = t117;
                                                void * t118 = gm_l0;
                                                gm_t88rcucg1b7uk t119 = gm_l18;
                                                gm_b16_8 t120 = gm_l2;
                                                gm_t88rcucg1b7uk t121 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t118, t119, t120);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l58 = t121;
                                                goto gm_lb18;
                                            }
gm_lb19:;
                                            {
                                                uint8_t t122 = GM_LD(uint8_t, (((uint8_t *)&F.l17) + 0));
                                                uint8_t t123 = (uint8_t)(t122 == ((uint8_t)1ULL));
                                                if (!t123) {
                                                    goto gm_lb20;
                                                }
                                                void * t124 = gm_l0;
                                                void * t125 = (void *)(((uint8_t *)t124) + 8);
                                                gm_t88rcucg1b7uk t126 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t125);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_l58 = t126;
                                                goto gm_lb18;
                                            }
gm_lb20:;
                                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 698, 31);
                                        }
gm_lb18:;
                                        gm_t88rcucg1b7uk t127 = gm_l58;
                                        gm_tftb1odc739t5 t128 = gm_f_push_k3l9nic(t114, GM_CONV(gm_csguankko4lmhd, t127));
                                        gm_l59 = t128;
                                        uint8_t t129 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                        if (!t129) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_src3k278vcmqgdr, 698, 17);
                                        goto gm_lb16;
                                    }
gm_lb17:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 697, 13);
                                }
gm_lb16:;
                            }
gm_lb15:;
                            uint64_t t130 = gm_l16;
                            uint64_t r131; bool o132 = __builtin_add_overflow(t130, ((uint64_t)1ULL), &r131);
                            uint64_t t133 = r131;
                            gm_l16 = t133;
                        }
                    }
gm_lb13:;
                    void * t134 = gm_l0;
                    void * t135 = gm_l0;
                    void * t136 = (void *)(((uint8_t *)t135) + 8);
                    uint32_t t137 = gm_l9;
                    uint32_t t138 = gm_f_template_9o92cvv(t136, t137);
                    gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)&F.l14));
                    gm_b16_8 t140 = GM_LD(gm_b16_8, ((uint8_t *)&t139));
                    gm_t88rcucg1b7uk t141 = gm_f_instantiate_adt_lbjcui1((void *)NULL, cx, NULL, t134, t138, t140);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t141;
                    goto gm_fret;
                }
gm_lb8:;
            }
            {
                {
                    uint8_t t142 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t143 = (uint8_t)(t142 == ((uint8_t)15ULL));
                    if (!t143) {
                        goto gm_lb21;
                    }
                    {
                        memmove(((uint8_t *)&F.l19), (((uint8_t *)&F.l4) + 8), 48);
                    }
                    {
                        void * t144 = gm_l0;
                        void * t145 = (void *)(((uint8_t *)t144) + 8);
                        void * t146 = gm_l0;
                        void * t147 = (void *)((uint8_t *)&F.l19);
                        gm_b16_8 t148 = gm_l2;
                        gm_tddtjfggdat0d t149;
                        gm_f_subst_sig_9ngh936((void *)&t149, (void *)NULL, cx, NULL, t146, t147, t148);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t53at80pfdus3 t150;
                        GM_ST(uint8_t, (uint8_t *)&t150 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t150 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t150 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t150 + 0), ((uint8_t)15ULL));
                        GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t150 + 8), t149);
                        gm_t88rcucg1b7uk t151 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t145, &t150);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t151;
                        goto gm_fret;
                    }
                }
gm_lb21:;
                {
                    uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t153 = (uint8_t)(t152 == ((uint8_t)16ULL));
                    if (!t153) {
                        goto gm_lb22;
                    }
                    {
                        memmove(((uint8_t *)&F.l20), (((uint8_t *)&F.l4) + 8), 48);
                    }
                    {
                        void * t154 = gm_l0;
                        void * t155 = (void *)(((uint8_t *)t154) + 8);
                        void * t156 = gm_l0;
                        void * t157 = (void *)((uint8_t *)&F.l20);
                        gm_b16_8 t158 = gm_l2;
                        gm_tddtjfggdat0d t159;
                        gm_f_subst_sig_9ngh936((void *)&t159, (void *)NULL, cx, NULL, t156, t157, t158);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_t53at80pfdus3 t160;
                        GM_ST(uint8_t, (uint8_t *)&t160 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t160 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t160 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t160 + 0), ((uint8_t)16ULL));
                        GM_ST(gm_tddtjfggdat0d, ((uint8_t *)&t160 + 8), t159);
                        gm_t88rcucg1b7uk t161 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t155, &t160);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t161;
                        goto gm_fret;
                    }
                }
gm_lb22:;
                {
                    uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t163 = (uint8_t)(t162 == ((uint8_t)8ULL));
                    if (!t163) {
                        goto gm_lb23;
                    }
                    uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 8));
                    gm_l21 = t164;
                    gm_t88rcucg1b7uk t165 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                    gm_l22 = t165;
                    void * t166 = gm_l0;
                    void * t167 = (void *)(((uint8_t *)t166) + 8);
                    uint8_t t168 = gm_l21;
                    void * t169 = gm_l0;
                    gm_t88rcucg1b7uk t170 = gm_l22;
                    gm_b16_8 t171 = gm_l2;
                    gm_t88rcucg1b7uk t172 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t169, t170, t171);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t173 = gm_f_ptr_t1h2m2d((void *)NULL, cx, NULL, t167, t168, t172);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t173;
                    goto gm_fret;
                }
gm_lb23:;
                {
                    uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t175 = (uint8_t)(t174 == ((uint8_t)9ULL));
                    if (!t175) {
                        goto gm_lb24;
                    }
                    uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 8));
                    gm_l23 = t176;
                    gm_t88rcucg1b7uk t177 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                    gm_l24 = t177;
                    void * t178 = gm_l0;
                    void * t179 = (void *)(((uint8_t *)t178) + 8);
                    uint8_t t180 = gm_l23;
                    void * t181 = gm_l0;
                    gm_t88rcucg1b7uk t182 = gm_l24;
                    gm_b16_8 t183 = gm_l2;
                    gm_t88rcucg1b7uk t184 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t181, t182, t183);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t185 = gm_f_many_ptr_l4v11f8((void *)NULL, cx, NULL, t179, t180, t184);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t185;
                    goto gm_fret;
                }
gm_lb24:;
                {
                    uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t187 = (uint8_t)(t186 == ((uint8_t)10ULL));
                    if (!t187) {
                        goto gm_lb25;
                    }
                    uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 8));
                    gm_l25 = t188;
                    gm_t88rcucg1b7uk t189 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                    gm_l26 = t189;
                    void * t190 = gm_l0;
                    void * t191 = (void *)(((uint8_t *)t190) + 8);
                    uint8_t t192 = gm_l25;
                    void * t193 = gm_l0;
                    gm_t88rcucg1b7uk t194 = gm_l26;
                    gm_b16_8 t195 = gm_l2;
                    gm_t88rcucg1b7uk t196 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t193, t194, t195);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t197 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t191, t192, t196);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t197;
                    goto gm_fret;
                }
gm_lb25:;
            }
            {
                {
                    uint8_t t198 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t199 = (uint8_t)(t198 == ((uint8_t)11ULL));
                    if (!t199) {
                        goto gm_lb26;
                    }
                    uint64_t t200 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                    gm_l27 = t200;
                    gm_t88rcucg1b7uk t201 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 8));
                    gm_l28 = t201;
                    gm_tnnjeerolos0a t202 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&F.l4) + 8) + 12));
                    gm_l29 = t202;
                    gm_tnnjeerolos0a t203 = gm_l29;
                    gm_l30 = t203;
                    {
                        {
                            uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                            uint8_t t205 = (uint8_t)(t204 == ((uint8_t)1ULL));
                            if (!t205) {
                                goto gm_lb28;
                            }
                            gm_t88rcucg1b7uk t206 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l29) + 4));
                            gm_l31 = t206;
                            void * t207 = gm_l0;
                            gm_t88rcucg1b7uk t208 = gm_l31;
                            gm_b16_8 t209 = gm_l2;
                            gm_t88rcucg1b7uk t210 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t207, t208, t209);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tnnjeerolos0a t211;
                            GM_ST(uint8_t, (uint8_t *)&t211 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t211 + 2, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t211 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t211 + 4), t210);
                            gm_l30 = t211;
                            goto gm_lb27;
                        }
gm_lb28:;
                    }
gm_lb27:;
                    {
                        void * t212 = gm_l0;
                        void * t213 = (void *)(((uint8_t *)t212) + 8);
                        uint64_t t214 = gm_l27;
                        void * t215 = gm_l0;
                        gm_t88rcucg1b7uk t216 = gm_l28;
                        gm_b16_8 t217 = gm_l2;
                        gm_t88rcucg1b7uk t218 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t215, t216, t217);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnnjeerolos0a t219 = gm_l30;
                        gm_t8nckeqfenl8a t220;
                        GM_ST(uint32_t, (uint8_t *)&t220 + 20, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t220 + 0), t214);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t220 + 8), t218);
                        GM_ST(gm_tnnjeerolos0a, ((uint8_t *)&t220 + 12), t219);
                        gm_t53at80pfdus3 t221;
                        GM_ST(uint8_t, (uint8_t *)&t221 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t221 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t221 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t221 + 32, 0);
                        GM_ST(uint64_t, (uint8_t *)&t221 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t221 + 48, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t221 + 0), ((uint8_t)11ULL));
                        GM_ST(gm_t8nckeqfenl8a, ((uint8_t *)&t221 + 8), t220);
                        gm_t88rcucg1b7uk t222 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t213, &t221);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t222;
                        goto gm_fret;
                    }
                }
gm_lb26:;
                {
                    uint8_t t223 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t224 = (uint8_t)(t223 == ((uint8_t)20ULL));
                    if (!t224) {
                        goto gm_lb29;
                    }
                    gm_t88rcucg1b7uk t225 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
                    gm_l32 = t225;
                    gm_t88rcucg1b7uk t226 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                    gm_l33 = t226;
                    void * t227 = gm_l0;
                    gm_t88rcucg1b7uk t228 = gm_l32;
                    gm_b16_8 t229 = gm_l2;
                    gm_t88rcucg1b7uk t230 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t227, t228, t229);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l34 = t230;
                    void * t231 = gm_l0;
                    gm_t88rcucg1b7uk t232 = gm_l33;
                    gm_b16_8 t233 = gm_l2;
                    gm_t88rcucg1b7uk t234 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t231, t232, t233);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l35 = t234;
                    void * t235 = gm_l0;
                    void * t236 = (void *)(((uint8_t *)t235) + 8);
                    gm_t88rcucg1b7uk t237 = gm_l34;
                    gm_f_kind_jcnt4hm((void *)&gm_l60, t236, GM_CONV(gm_csguankko4lmhd, t237));
                    {
                        {
                            uint8_t t238 = GM_LD(uint8_t, (((uint8_t *)&gm_l60) + 0));
                            uint8_t t239 = (uint8_t)(t238 == ((uint8_t)21ULL));
                            if (!t239) {
                                goto gm_lb31;
                            }
                            {
                                memmove(((uint8_t *)&gm_l36), (((uint8_t *)&gm_l60) + 8), 32);
                            }
                            {
                                gm_tf5srh4mp4b3i t240 = gm_f_key_u64_ljklb56(&(*(gm_t428ue2u3hf5g *)((uint8_t *)&gm_l36)));
                                gm_l61 = t240;
                            }
                            uint8_t t241 = (uint8_t)((((uint8_t *)&gm_l61))[8] != 0);
                            if (t241) {
                                uint64_t t242 = GM_LD(uint64_t, ((uint8_t *)&gm_l61));
                                gm_l37 = t242;
                            } else {
                                void * t243 = gm_l0;
                                void * t244 = (void *)(((uint8_t *)t243) + 8);
                                gm_t88rcucg1b7uk t245 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t244);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.ret = t245;
                                goto gm_fret;
                            }
                            void * t246 = gm_l0;
                            void * t247 = (void *)(((uint8_t *)t246) + 8);
                            uint64_t t248 = gm_l37;
                            gm_t88rcucg1b7uk t249 = gm_l35;
                            gm_t88rcucg1b7uk t250 = gm_f_array_th8sdi3((void *)NULL, cx, NULL, t247, t248, t249);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t250;
                            goto gm_fret;
                        }
gm_lb31:;
                        {
                            uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&gm_l60) + 0));
                            uint8_t t252 = (uint8_t)(t251 == ((uint8_t)26ULL));
                            if (!t252) {
                                goto gm_lb32;
                            }
                            void * t253 = gm_l0;
                            void * t254 = (void *)(((uint8_t *)t253) + 8);
                            gm_t88rcucg1b7uk t255 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t254);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t255;
                            goto gm_fret;
                        }
gm_lb32:;
                        {
                            void * t256 = gm_l0;
                            void * t257 = (void *)(((uint8_t *)t256) + 8);
                            gm_t88rcucg1b7uk t258 = gm_l34;
                            gm_t88rcucg1b7uk t259 = gm_l35;
                            gm_tgglga8ro3isc t260;
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t260 + 0), t258);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t260 + 4), t259);
                            gm_t53at80pfdus3 t261;
                            memset(&t261, 0, sizeof t261);
                            GM_ST(uint8_t, ((uint8_t *)&t261 + 0), ((uint8_t)20ULL));
                            GM_ST(gm_tgglga8ro3isc, ((uint8_t *)&t261 + 8), t260);
                            gm_t88rcucg1b7uk t262 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t257, &t261);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t262;
                            goto gm_fret;
                        }
gm_lb33:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 718, 13);
                    }
gm_lb30:;
                }
gm_lb29:;
            }
            {
                {
                    uint8_t t263 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t264 = (uint8_t)(t263 == ((uint8_t)22ULL));
                    if (!t264) {
                        goto gm_lb34;
                    }
                    gm_t9qkh0rsooc8d t265 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l4) + 8));
                    gm_l38 = t265;
                    gm_t88rcucg1b7uk t266 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                    gm_l39 = t266;
                    gm_t88rcucg1b7uk t267 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 8));
                    gm_l40 = t267;
                    void * t268 = gm_l0;
                    gm_t88rcucg1b7uk t269 = gm_l39;
                    gm_b16_8 t270 = gm_l2;
                    gm_t88rcucg1b7uk t271 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t268, t269, t270);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l41 = t271;
                    void * t272 = gm_l0;
                    gm_t88rcucg1b7uk t273 = gm_l40;
                    gm_b16_8 t274 = gm_l2;
                    gm_t88rcucg1b7uk t275 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t272, t273, t274);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l42 = t275;
                    void * t276 = gm_l0;
                    void * t277 = (void *)(((uint8_t *)t276) + 8);
                    gm_t9qkh0rsooc8d t278 = gm_l38;
                    gm_t88rcucg1b7uk t279 = gm_l41;
                    gm_t88rcucg1b7uk t280 = gm_l42;
                    gm_t88rcucg1b7uk t281 = gm_f_len_expr_i134fvg((void *)NULL, cx, NULL, t277, t278, t279, t280);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t281;
                    goto gm_fret;
                }
gm_lb34:;
                {
                    uint8_t t282 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t283 = (uint8_t)(t282 == ((uint8_t)13ULL));
                    if (!t283) {
                        goto gm_lb35;
                    }
                    gm_t88rcucg1b7uk t284 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
                    gm_l43 = t284;
                    void * t285 = gm_l0;
                    void * t286 = (void *)(((uint8_t *)t285) + 8);
                    void * t287 = gm_l0;
                    gm_t88rcucg1b7uk t288 = gm_l43;
                    gm_b16_8 t289 = gm_l2;
                    gm_t88rcucg1b7uk t290 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t287, t288, t289);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t88rcucg1b7uk t291 = gm_f_optional_rh6okj3((void *)NULL, cx, NULL, t286, t290);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t291;
                    goto gm_fret;
                }
gm_lb35:;
                {
                    uint8_t t292 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t293 = (uint8_t)(t292 == ((uint8_t)18ULL));
                    if (!t293) {
                        goto gm_lb36;
                    }
                    gm_t88rcucg1b7uk t294 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
                    gm_l44 = t294;
                    void * t295 = gm_l0;
                    void * t296 = (void *)(((uint8_t *)t295) + 8);
                    void * t297 = gm_l0;
                    gm_t88rcucg1b7uk t298 = gm_l44;
                    gm_b16_8 t299 = gm_l2;
                    gm_t88rcucg1b7uk t300 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t297, t298, t299);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t53at80pfdus3 t301;
                    memset(&t301, 0, sizeof t301);
                    GM_ST(uint8_t, ((uint8_t *)&t301 + 0), ((uint8_t)18ULL));
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t301 + 8), t300);
                    gm_t88rcucg1b7uk t302 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t296, &t301);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t302;
                    goto gm_fret;
                }
gm_lb36:;
                {
                    uint8_t t303 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t304 = (uint8_t)(t303 == ((uint8_t)14ULL));
                    if (!t304) {
                        goto gm_lb37;
                    }
                    gm_t88rcucg1b7uk t305 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 8));
                    gm_l45 = t305;
                    gm_t88rcucg1b7uk t306 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&F.l4) + 8) + 4));
                    gm_l46 = t306;
                    void * t307 = gm_l0;
                    gm_t88rcucg1b7uk t308 = gm_l45;
                    gm_b16_8 t309 = gm_l2;
                    gm_t88rcucg1b7uk t310 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t307, t308, t309);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l47 = t310;
                    void * t311 = gm_l0;
                    gm_t88rcucg1b7uk t312 = gm_l46;
                    gm_b16_8 t313 = gm_l2;
                    gm_t88rcucg1b7uk t314 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t311, t312, t313);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l48 = t314;
                    void * t315 = gm_l0;
                    void * t316 = (void *)(((uint8_t *)t315) + 8);
                    gm_t88rcucg1b7uk t317 = gm_l47;
                    gm_t88rcucg1b7uk t318 = gm_l48;
                    gm_t88rcucg1b7uk t319 = gm_f_result_o9ras10((void *)NULL, cx, NULL, t316, t317, t318);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t319;
                    goto gm_fret;
                }
gm_lb37:;
            }
            {
                {
                    uint8_t t320 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t321 = (uint8_t)(t320 == ((uint8_t)12ULL));
                    if (!t321) {
                        goto gm_lb38;
                    }
                    gm_b16_8 t322 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 8));
                    gm_l49 = t322;
                    gm_b16_8 t323 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l4) + 8) + 16));
                    gm_l50 = t323;
                    {
                        gm_trqfvmjtt4sij t324;
                        gm_f_list_nof7vfj((void *)&t324, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.l51 = t324;
                    }
                    gm_b16_8 t325 = gm_l49;
                    gm_l52 = t325;
                    gm_l53 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t326 = gm_l53;
                            uint64_t t327 = GM_LD(uint64_t, (((uint8_t *)&gm_l52) + 8));
                            uint8_t t328 = (t326 < t327);
                            if (!t328) {
                                goto gm_lb39;
                            }
                            {
                                void * t329 = GM_LD(void *, ((uint8_t *)&gm_l52));
                                uint64_t t330 = GM_LD(uint64_t, (((uint8_t *)&gm_l52) + 8));
                                uint64_t t331 = gm_l53;
                                {
                                    {
                                        gm_t88rcucg1b7uk t332 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t329) + (uintptr_t)t331 * 4));
                                        gm_l54 = t332;
                                        void * t333 = (void *)((uint8_t *)&F.l51);
                                        void * t334 = gm_l0;
                                        gm_t88rcucg1b7uk t335 = gm_l54;
                                        gm_b16_8 t336 = gm_l2;
                                        gm_t88rcucg1b7uk t337 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t334, t335, t336);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t338 = gm_f_push_k3l9nic(t333, GM_CONV(gm_csguankko4lmhd, t337));
                                        gm_l62 = t338;
                                        uint8_t t339 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                        if (!t339) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 1, gm_src3k278vcmqgdr, 741, 30);
                                        goto gm_lb42;
                                    }
gm_lb43:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 741, 13);
                                }
gm_lb42:;
                            }
gm_lb41:;
                            uint64_t t340 = gm_l53;
                            uint64_t r341; bool o342 = __builtin_add_overflow(t340, ((uint64_t)1ULL), &r341);
                            uint64_t t343 = r341;
                            gm_l53 = t343;
                        }
                    }
gm_lb39:;
                    {
                        void * t344 = gm_l0;
                        void * t345 = (void *)(((uint8_t *)t344) + 8);
                        gm_b16_8 t346 = GM_LD(gm_b16_8, ((uint8_t *)&F.l51));
                        gm_b16_8 t347 = GM_LD(gm_b16_8, ((uint8_t *)&t346));
                        gm_b16_8 t348 = gm_l50;
                        gm_t6j6smj7qoctu t349;
                        GM_ST(gm_b16_8, ((uint8_t *)&t349 + 0), t347);
                        GM_ST(gm_b16_8, ((uint8_t *)&t349 + 16), t348);
                        gm_t53at80pfdus3 t350;
                        GM_ST(uint8_t, (uint8_t *)&t350 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t350 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t350 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t350 + 40, 0);
                        GM_ST(uint64_t, (uint8_t *)&t350 + 48, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t350 + 0), ((uint8_t)12ULL));
                        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t350 + 8), t349);
                        gm_t88rcucg1b7uk t351 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t345, &t350);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t351;
                        goto gm_fret;
                    }
                }
gm_lb38:;
                {
                    uint8_t t352 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                    uint8_t t353 = (uint8_t)(t352 == ((uint8_t)24ULL));
                    if (!t353) {
                        goto gm_lb44;
                    }
                    gm_tnpifv20irnph t354 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l4) + 8));
                    gm_l55 = t354;
                    void * t355 = gm_l0;
                    void * t356 = (void *)(((uint8_t *)t355) + 8);
                    void * t357 = gm_l0;
                    void * t358 = (void *)((uint8_t *)&gm_l55);
                    gm_b16_8 t359 = gm_l2;
                    gm_tnpifv20irnph t360 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t357, t358, t359);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t53at80pfdus3 t361;
                    memset(&t361, 0, sizeof t361);
                    GM_ST(uint8_t, ((uint8_t *)&t361 + 0), ((uint8_t)24ULL));
                    GM_ST(gm_tnpifv20irnph, ((uint8_t *)&t361 + 8), t360);
                    gm_t88rcucg1b7uk t362 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t356, &t361);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    F.ret = t362;
                    goto gm_fret;
                }
gm_lb44:;
                {
                    gm_t88rcucg1b7uk t363 = gm_l1;
                    F.ret = t363;
                    goto gm_fret;
                }
gm_lb45:;
            }
            {
                gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 681, 5);
            }
        }
gm_lb1:;
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
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_subst_effects_jnlbu5m F;
    void * gm_l0; /* s */
    void * gm_l1; /* set */
    gm_b16_8 gm_l2; /* map */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_tiilmfd16v3fc gm_l6; /* e */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_t88rcucg1b7uk gm_l11; /* t */
    GM_STACK_CHECK(&F, 610u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_toa2vhe3l4hfr gm_l13;
    gm_tftb1odc739t5 gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_trqfvmjtt4sij t15;
    gm_f_list_084fcd6((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t15;
    void * t16 = gm_l1;
    gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)t16));
    gm_l4 = t17;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t18 = gm_l5;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t20 = (t18 < t19);
            if (!t20) {
                goto gm_lb1;
            }
            {
                void * t21 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t23 = gm_l5;
                {
                    {
                        {
                            memmove(((uint8_t *)&gm_l6), (((uint8_t *)t21) + (uintptr_t)t23 * 24), 24);
                        }
                        {
                            gm_trqfvmjtt4sij t24;
                            gm_f_list_vmvl81b((void *)&t24, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.l7 = t24;
                        }
                        gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l6) + 8));
                        gm_l8 = t25;
                        gm_l9 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t26 = gm_l9;
                                uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                uint8_t t28 = (t26 < t27);
                                if (!t28) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t29 = GM_LD(void *, ((uint8_t *)&gm_l8));
                                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                                    uint64_t t31 = gm_l9;
                                    {
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l10), (((uint8_t *)t29) + (uintptr_t)t31 * 40), 40);
                                            }
                                            {
                                                void * t32 = (void *)((uint8_t *)&F.l7);
                                                {
                                                    {
                                                        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                                                        uint8_t t34 = (uint8_t)(t33 == ((uint8_t)0ULL));
                                                        if (!t34) {
                                                            goto gm_lb12;
                                                        }
                                                        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l10) + 8));
                                                        gm_l11 = t35;
                                                        void * t36 = gm_l0;
                                                        gm_t88rcucg1b7uk t37 = gm_l11;
                                                        gm_b16_8 t38 = gm_l2;
                                                        gm_t88rcucg1b7uk t39 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t36, t37, t38);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        memset(&gm_l13, 0, sizeof gm_l13);
                                                        GM_ST(uint8_t, ((uint8_t *)&gm_l13 + 0), ((uint8_t)0ULL));
                                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l13 + 8), t39);
                                                        goto gm_lb11;
                                                    }
gm_lb12:;
                                                    {
                                                        {
                                                            memmove(((uint8_t *)&F.l12), ((uint8_t *)&F.l10), 40);
                                                        }
                                                        {
                                                            memmove(((uint8_t *)&gm_l13), ((uint8_t *)&F.l12), 40);
                                                        }
                                                        goto gm_lb11;
                                                    }
gm_lb13:;
                                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 764, 23);
                                                }
gm_lb11:;
                                                gm_tftb1odc739t5 t40 = gm_f_push_s5vr2a2(t32, &(*(gm_toa2vhe3l4hfr *)((uint8_t *)&gm_l13)));
                                                gm_l14 = t40;
                                            }
                                            uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
                                            if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 1, gm_src3k278vcmqgdr, 764, 13);
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 763, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t42 = gm_l9;
                                uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                                uint64_t t45 = r43;
                                gm_l9 = t45;
                            }
                        }
gm_lb6:;
                        {
                            void * t46 = (void *)((uint8_t *)&F.l3);
                            uint32_t t47 = GM_LD(uint32_t, ((uint8_t *)&gm_l6));
                            gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
                            gm_tiilmfd16v3fc t50;
                            GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t50 + 0), t47);
                            GM_ST(gm_b16_8, ((uint8_t *)&t50 + 8), t49);
                            gm_tftb1odc739t5 t51 = gm_f_push_d1r4h19(t46, &t50);
                            gm_l15 = t51;
                        }
                        uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                        if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_src3k278vcmqgdr, 769, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 761, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t53 = gm_l5;
            uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
            uint64_t t56 = r54;
            gm_l5 = t56;
        }
    }
gm_lb1:;
    void * t57 = gm_l0;
    gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
    gm_tnpifv20irnph t60 = gm_f_effect_set_vtcpipr((void *)NULL, cx, NULL, t59);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t61 = gm_f_flatten_2js7j92((void *)NULL, cx, NULL, t57, t60);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t61;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_subst_fields_pqf9lb0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_subst_fields_pqf9lb0 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* fields */
    gm_b16_8 gm_l2; /* map */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l7;
    gm_trqfvmjtt4sij t7;
    gm_f_list_spqn36o((void *)&t7, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l3 = t7;
    gm_b16_8 t8 = gm_l1;
    gm_l4 = t8;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t9 = gm_l5;
            uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                void * t12 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t14 = gm_l5;
                {
                    {
                        {
                            memmove(((uint8_t *)&F.l6), (((uint8_t *)t12) + (uintptr_t)t14 * 40), 40);
                        }
                        {
                            void * t15 = (void *)((uint8_t *)&F.l3);
                            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
                            void * t17 = gm_l0;
                            gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 16));
                            gm_b16_8 t19 = gm_l2;
                            gm_t88rcucg1b7uk t20 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t17, t18, t19);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_t9qkh0rsooc8d t21 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l6) + 20));
                            gm_tf5srh4mp4b3i t22 = GM_LD(gm_tf5srh4mp4b3i, (((uint8_t *)&F.l6) + 24));
                            gm_t0mglksrkfa5e t23;
                            GM_ST(uint8_t, (uint8_t *)&t23 + 21, 0);
                            GM_ST(uint16_t, (uint8_t *)&t23 + 22, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t16);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t23 + 16), t20);
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t23 + 20), t21);
                            GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t23 + 24), t22);
                            gm_tftb1odc739t5 t24 = gm_f_push_vrmo7c5(t15, &t23);
                            gm_l7 = t24;
                        }
                        uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
                        if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_src3k278vcmqgdr, 640, 23);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 640, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t26 = gm_l5;
            uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
            uint64_t t29 = r27;
            gm_l5 = t29;
        }
    }
gm_lb1:;
    gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    F.ret = t30;
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
inline __attribute__((aligned(4))) void gm_f_subst_sig_9ngh936(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_subst_sig_9ngh936 F;
    void * gm_l0; /* s */
    void * gm_l1; /* sig */
    gm_b16_8 gm_l2; /* map */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    gm_t88rcucg1b7uk gm_l6; /* p */
    gm_t88rcucg1b7uk gm_l7; /* ret */
    gm_tnpifv20irnph gm_l8; /* effects */
    GM_STACK_CHECK(&F, 407u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l9;
    {
        gm_trqfvmjtt4sij t7;
        gm_f_list_nof7vfj((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t7;
    }
    void * t8 = gm_l1;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)t8));
    gm_l4 = t9;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t10 = gm_l5;
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t12 = (t10 < t11);
            if (!t12) {
                goto gm_lb1;
            }
            {
                void * t13 = GM_LD(void *, ((uint8_t *)&gm_l4));
                uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint64_t t15 = gm_l5;
                {
                    {
                        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + (uintptr_t)t15 * 4));
                        gm_l6 = t16;
                        void * t17 = (void *)((uint8_t *)&F.l3);
                        void * t18 = gm_l0;
                        gm_t88rcucg1b7uk t19 = gm_l6;
                        gm_b16_8 t20 = gm_l2;
                        gm_t88rcucg1b7uk t21 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t18, t19, t20);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tftb1odc739t5 t22 = gm_f_push_k3l9nic(t17, GM_CONV(gm_csguankko4lmhd, t21));
                        gm_l9 = t22;
                        uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                        if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_src3k278vcmqgdr, 751, 27);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 751, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t24 = gm_l5;
            uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
            uint64_t t27 = r25;
            gm_l5 = t27;
        }
    }
gm_lb1:;
    void * t28 = gm_l0;
    void * t29 = gm_l1;
    gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t29) + 16));
    gm_b16_8 t31 = gm_l2;
    gm_t88rcucg1b7uk t32 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t28, t30, t31);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t32;
    void * t33 = gm_l0;
    void * t34 = gm_l1;
    void * t35 = (void *)(((uint8_t *)t34) + 24);
    gm_b16_8 t36 = gm_l2;
    gm_tnpifv20irnph t37 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t33, t35, t36);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l8 = t37;
    {
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
        gm_t88rcucg1b7uk t40 = gm_l7;
        gm_tnpifv20irnph t41 = gm_l8;
        void * t42 = gm_l1;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)t42) + 40));
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 20, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 44, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 0), t39);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 16), t40);
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 24), t41);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tddtjfggdat0d *)gm_out) + 40), t43);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_taken_struct_members_1ltbb18(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_taken_struct_members_1ltbb18 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* members */
    void * gm_l3; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 358u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l9;
    gm_tqs5p4n2tq19v gm_l10;
    gm_b16_8 gm_l11;
    gm_b16_8 t14 = gm_l2;
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
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t18) + (uintptr_t)t20 * 80), 80);
                        {
                            {
                                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                uint8_t t22 = (uint8_t)(t21 == ((uint8_t)0ULL));
                                if (!t22) {
                                    goto gm_lb7;
                                }
                                {
                                    memmove(((uint8_t *)&F.l7), (((uint8_t *)&F.l6) + 8), 72);
                                }
                                {
                                    void * t23 = gm_l3;
                                    gm_tftb1odc739t5 t24 = gm_f_push_ghqse9f(t23, &(*(gm_t24301fas1b2a *)((uint8_t *)&F.l7)));
                                    gm_l9 = t24;
                                }
                                uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_src3k278vcmqgdr, 518, 26);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                uint8_t t27 = (uint8_t)(t26 == ((uint8_t)1ULL));
                                if (!t27) {
                                    goto gm_lb8;
                                }
                                memmove(((uint8_t *)&F.l8), (((uint8_t *)&F.l6) + 8), 64);
                                void * t28 = gm_l0;
                                void * t29 = GM_LD(void *, ((uint8_t *)t28));
                                uint64_t t30 = gm_l1;
                                void * t31 = GM_LD(void *, ((uint8_t *)&F.l8));
                                gm_tqs5p4n2tq19v t32 = gm_f_when_cond_9a46a5h((void *)NULL, cx, NULL, t29, t30, t31);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l10 = t32;
                                {
                                    {
                                        uint8_t t33 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
                                        if (!t33) {
                                            goto gm_lb10;
                                        }
                                        uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&gm_l10));
                                        uint8_t t35 = ((t34 == ((uint8_t)1)));
                                        if (!t35) {
                                            goto gm_lb10;
                                        }
                                        void * t36 = gm_l0;
                                        uint64_t t37 = gm_l1;
                                        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
                                        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                                        void * t40 = gm_l3;
                                        gm_b0_1 t41 = gm_f_taken_struct_members_1ltbb18((void *)NULL, cx, NULL, t36, t37, t39, t40);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb9;
                                    }
gm_lb10:;
                                    {
                                        uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
                                        if (!t42) {
                                            goto gm_lb11;
                                        }
                                        uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)&gm_l10));
                                        uint8_t t44 = ((t43 == ((uint8_t)0)));
                                        if (!t44) {
                                            goto gm_lb11;
                                        }
                                        void * t45 = gm_l0;
                                        uint64_t t46 = gm_l1;
                                        uint8_t t47 = (uint8_t)(((((uint8_t *)&F.l8) + 24))[16] != 0);
                                        if (t47) {
                                            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 24));
                                            gm_l11 = t48;
                                        } else {
                                            gm_b16_8 t49 = gm_f_empty_slice_d53jqcq();
                                            gm_l11 = t49;
                                        }
                                        gm_b16_8 t50 = gm_l11;
                                        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                                        void * t52 = gm_l3;
                                        gm_b0_1 t53 = gm_f_taken_struct_members_1ltbb18((void *)NULL, cx, NULL, t45, t46, t51, t52);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb9;
                                    }
gm_lb11:;
                                    {
                                        uint8_t t54 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
                                        if (t54) {
                                            goto gm_lb12;
                                        }
                                        goto gm_lb9;
                                    }
gm_lb12:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 519, 25);
                                }
gm_lb9:;
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 517, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 516, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t55 = gm_l5;
            uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
            uint64_t t58 = r56;
            gm_l5 = t58;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_taken_union_members_pavth81(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_taken_union_members_pavth81 F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    gm_b16_8 gm_l2; /* members */
    void * gm_l3; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 358u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l9;
    gm_tqs5p4n2tq19v gm_l10;
    gm_b16_8 gm_l11;
    gm_b16_8 t14 = gm_l2;
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
                        memmove(((uint8_t *)&F.l6), (((uint8_t *)t18) + (uintptr_t)t20 * 72), 72);
                        {
                            {
                                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                uint8_t t22 = (uint8_t)(t21 == ((uint8_t)0ULL));
                                if (!t22) {
                                    goto gm_lb7;
                                }
                                {
                                    memmove(((uint8_t *)&F.l7), (((uint8_t *)&F.l6) + 8), 64);
                                }
                                {
                                    void * t23 = gm_l3;
                                    gm_tftb1odc739t5 t24 = gm_f_push_83inprj(t23, &(*(gm_tnljbma3s3h16 *)((uint8_t *)&F.l7)));
                                    gm_l9 = t24;
                                }
                                uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
                                if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_src3k278vcmqgdr, 532, 28);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                                uint8_t t27 = (uint8_t)(t26 == ((uint8_t)1ULL));
                                if (!t27) {
                                    goto gm_lb8;
                                }
                                memmove(((uint8_t *)&F.l8), (((uint8_t *)&F.l6) + 8), 64);
                                void * t28 = gm_l0;
                                void * t29 = GM_LD(void *, ((uint8_t *)t28));
                                uint64_t t30 = gm_l1;
                                void * t31 = GM_LD(void *, ((uint8_t *)&F.l8));
                                gm_tqs5p4n2tq19v t32 = gm_f_when_cond_9a46a5h((void *)NULL, cx, NULL, t29, t30, t31);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l10 = t32;
                                {
                                    {
                                        uint8_t t33 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
                                        if (!t33) {
                                            goto gm_lb10;
                                        }
                                        uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&gm_l10));
                                        uint8_t t35 = ((t34 == ((uint8_t)1)));
                                        if (!t35) {
                                            goto gm_lb10;
                                        }
                                        void * t36 = gm_l0;
                                        uint64_t t37 = gm_l1;
                                        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 8));
                                        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
                                        void * t40 = gm_l3;
                                        gm_b0_1 t41 = gm_f_taken_union_members_pavth81((void *)NULL, cx, NULL, t36, t37, t39, t40);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb9;
                                    }
gm_lb10:;
                                    {
                                        uint8_t t42 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
                                        if (!t42) {
                                            goto gm_lb11;
                                        }
                                        uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)&gm_l10));
                                        uint8_t t44 = ((t43 == ((uint8_t)0)));
                                        if (!t44) {
                                            goto gm_lb11;
                                        }
                                        void * t45 = gm_l0;
                                        uint64_t t46 = gm_l1;
                                        uint8_t t47 = (uint8_t)(((((uint8_t *)&F.l8) + 24))[16] != 0);
                                        if (t47) {
                                            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 24));
                                            gm_l11 = t48;
                                        } else {
                                            gm_b16_8 t49 = gm_f_empty_slice_u4e74ha();
                                            gm_l11 = t49;
                                        }
                                        gm_b16_8 t50 = gm_l11;
                                        gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
                                        void * t52 = gm_l3;
                                        gm_b0_1 t53 = gm_f_taken_union_members_pavth81((void *)NULL, cx, NULL, t45, t46, t51, t52);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        goto gm_lb9;
                                    }
gm_lb11:;
                                    {
                                        uint8_t t54 = (uint8_t)((((uint8_t *)&gm_l10))[1] != 0);
                                        if (t54) {
                                            goto gm_lb12;
                                        }
                                        goto gm_lb9;
                                    }
gm_lb12:;
                                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 533, 25);
                                }
gm_lb9:;
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 531, 9);
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 530, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t55 = gm_l5;
            uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
            uint64_t t58 = r56;
            gm_l5 = t58;
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
__attribute__((aligned(4))) void gm_f_union_kind_j2gcmto(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, void * a2, gm_b16_8 a3) {
    struct gm_fr_union_kind_j2gcmto F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* module */
    void * gm_l2; /* u */
    gm_b16_8 gm_l3; /* attrs */
    uint8_t gm_l4; /* c_union */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l8; /* other */
    gm_tkn42u7na3t81 gm_l9; /* explicit_tag */
    void * gm_l10; /* t */
    gm_t88rcucg1b7uk gm_l11; /* tag */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_tkn42u7na3t81 gm_l22; /* payload */
    void * gm_l23; /* p */
    void * gm_l25; /* val */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* i */
    gm_t88rcucg1b7uk gm_l33; /* p */
    uint64_t gm_l34; /* n */
    gm_t88rcucg1b7uk gm_l35; /* tag */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* i */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    uint64_t gm_l42; /* j */
    GM_STACK_CHECK(&F, 3884u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tabb710d9mq2e gm_l45;
    gm_tabb710d9mq2e gm_l46;
    gm_tkn42u7na3t81 gm_l47;
    gm_tabb710d9mq2e gm_l48;
    uint8_t gm_l49;
    gm_tkv1vap90vht2 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tftb1odc739t5 gm_l52;
    gm_t3l8idb2m6c1r gm_l53;
    gm_tftb1odc739t5 gm_l54;
    gm_t88rcucg1b7uk gm_l55;
    gm_t88rcucg1b7uk gm_l56;
    gm_t3l8idb2m6c1r gm_l57;
    gm_tabb710d9mq2e gm_l58;
    gm_t3l8idb2m6c1r gm_l59;
    gm_tabb710d9mq2e gm_l60;
    {
        gm_l4 = ((uint8_t)0);
        gm_b16_8 t56 = gm_l3;
        gm_l5 = t56;
        gm_l6 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t57 = gm_l6;
                uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint8_t t59 = (t57 < t58);
                if (!t59) {
                    goto gm_lb1;
                }
                {
                    void * t60 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint64_t t62 = gm_l6;
                    {
                        {
                            memmove(((uint8_t *)&F.l7), (((uint8_t *)t60) + (uintptr_t)t62 * 72), 72);
                            {
                                {
                                    gm_b16_8 t63 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                                    gm_b16_8 t64;
                                    GM_ST(void *, (uint8_t *)&t64, (void *)(gm_suuv0qpi4v4e4));
                                    GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(7));
                                    uint8_t t65 = (gm_eq0iqiubfm7e(((uint8_t *)&t63), ((uint8_t *)&t64)));
                                    if (!t65) {
                                        goto gm_lb7;
                                    }
                                    gm_l4 = ((uint8_t)1);
                                    goto gm_lb6;
                                }
gm_lb7:;
                                {
                                    gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                                    gm_l8 = t66;
                                    void * t67 = gm_l0;
                                    gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l7) + 56));
                                    gm_b16_8 t69 = gm_l8;
                                    gm_tnpifv20irnph t70;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t70 + 0), t69);
                                    gm_f_format_45bh632((void *)&gm_l45, (void *)NULL, cx, NULL, t70);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    uint8_t t71 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                                    if (!t71) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 8, gm_src3k278vcmqgdr, 345, 38);
                                    gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l45) + 8));
                                    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                                    gm_b0_1 t74 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t67, t68, t73);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb6;
                                }
gm_lb8:;
                                gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 343, 9);
                            }
gm_lb6:;
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 342, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t75 = gm_l6;
                uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                uint64_t t78 = r76;
                gm_l6 = t78;
            }
        }
gm_lb1:;
        gm_tkn42u7na3t81 t79;
        memset(&t79, 0, sizeof t79);
        gm_l9 = t79;
        {
            {
                void * t80 = gm_l2;
                uint8_t t81 = (uint8_t)(GM_LD(void *, ((uint8_t *)t80)) != 0);
                if (!t81) {
                    goto gm_lb10;
                }
                void * t82 = GM_LD(void *, ((uint8_t *)t80));
                gm_l10 = t82;
                void * t83 = gm_l0;
                uint64_t t84 = gm_l1;
                void * t85 = gm_l10;
                gm_t88rcucg1b7uk t86 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t83, t84, t85);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l11 = t86;
                {
                    {
                        void * t87 = gm_l0;
                        void * t88 = (void *)(((uint8_t *)t87) + 8);
                        gm_t88rcucg1b7uk t89 = gm_l11;
                        uint8_t t90 = gm_f_is_integer_8d4f9ek(t88, GM_CONV(gm_csguankko4lmhd, t89));
                        uint8_t t91 = (uint8_t)!t90;
                        if (!t91) {
                            goto gm_lb12;
                        }
                        void * t92 = gm_l0;
                        void * t93 = (void *)(((uint8_t *)t92) + 8);
                        gm_t88rcucg1b7uk t94 = gm_l11;
                        uint8_t t95 = gm_f_is_error_8k27gd1(t93, GM_CONV(gm_csguankko4lmhd, t94));
                        uint8_t t96 = (uint8_t)!t95;
                        if (!t96) {
                            goto gm_lb12;
                        }
                        void * t97 = gm_l0;
                        void * t98 = gm_l2;
                        gm_tl874t2atnlh3 t99 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t98) + 64));
                        void * t100 = gm_l0;
                        gm_t88rcucg1b7uk t101 = gm_l11;
                        gm_b16_8 t102 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t100, t101);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t103;
                        GM_ST(gm_b16_8, ((uint8_t *)&t103 + 0), t102);
                        gm_f_format_9kb6o1u((void *)&gm_l46, (void *)NULL, cx, NULL, t103);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t104 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                        if (!t104) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 8, gm_src3k278vcmqgdr, 352, 29);
                        gm_b16_8 t105 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
                        gm_b16_8 t106 = GM_LD(gm_b16_8, ((uint8_t *)&t105));
                        gm_b0_1 t107 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t97, t99, t106);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb11;
                    }
gm_lb12:;
                }
gm_lb11:;
                void * t108 = gm_l0;
                void * t109 = (void *)(((uint8_t *)t108) + 8);
                gm_t88rcucg1b7uk t110 = gm_l11;
                uint8_t t111 = gm_f_is_integer_8d4f9ek(t109, GM_CONV(gm_csguankko4lmhd, t110));
                if (t111) {
                    gm_t88rcucg1b7uk t112 = gm_l11;
                    gm_tkn42u7na3t81 t113;
                    GM_ST(uint8_t, (uint8_t *)&t113 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t113 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t113), t112);
                    ((uint8_t *)&t113)[4] = 1;
                    gm_l47 = t113;
                } else {
                    void * t114 = gm_l0;
                    void * t115 = (void *)(((uint8_t *)t114) + 8);
                    gm_t88rcucg1b7uk t116 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t115);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tkn42u7na3t81 t117;
                    GM_ST(uint8_t, (uint8_t *)&t117 + 5, 0);
                    GM_ST(uint16_t, (uint8_t *)&t117 + 6, 0);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t117), t116);
                    ((uint8_t *)&t117)[4] = 1;
                    gm_l47 = t117;
                }
                gm_tkn42u7na3t81 t118 = gm_l47;
                gm_l9 = t118;
                goto gm_lb9;
            }
gm_lb10:;
        }
gm_lb9:;
    }
    {
        gm_trqfvmjtt4sij t119;
        gm_f_list_g6uul2f((void *)&t119, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l12 = t119;
        gm_trqfvmjtt4sij t120;
        gm_f_list_7s4om10((void *)&t120, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t120;
        gm_f_zero_s25l990((void *)&F.l14, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t121;
        gm_f_list_ja7tv3o((void *)&t121, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l15 = t121;
        void * t122 = gm_l0;
        uint64_t t123 = gm_l1;
        void * t124 = gm_l2;
        gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)t124) + 48));
        gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
        void * t127 = (void *)((uint8_t *)&F.l15);
        gm_b0_1 t128 = gm_f_taken_union_members_pavth81((void *)NULL, cx, NULL, t122, t123, t126, t127);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&F.l15));
        gm_l16 = t129;
        gm_l17 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t130 = gm_l17;
                uint64_t t131 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                uint8_t t132 = (t130 < t131);
                if (!t132) {
                    goto gm_lb13;
                }
                {
                    void * t133 = GM_LD(void *, ((uint8_t *)&gm_l16));
                    uint64_t t134 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                    uint64_t t135 = gm_l17;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l18), (((uint8_t *)t133) + (uintptr_t)t135 * 64), 64);
                            }
                            gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                            gm_l19 = t136;
                            gm_l20 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t137 = gm_l20;
                                    uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                    uint8_t t139 = (t137 < t138);
                                    if (!t139) {
                                        goto gm_lb18;
                                    }
                                    {
                                        void * t140 = GM_LD(void *, ((uint8_t *)&gm_l19));
                                        uint64_t t141 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                                        uint64_t t142 = gm_l20;
                                        {
                                            {
                                                memmove(((uint8_t *)&F.l21), (((uint8_t *)t140) + (uintptr_t)t142 * 72), 72);
                                                gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&F.l21));
                                                gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                                                uint8_t t145 = (gm_eq0iqiubfm7e(((uint8_t *)&t143), ((uint8_t *)&t144)));
                                                if (t145) {
                                                    void * t146 = gm_l0;
                                                    gm_tl874t2atnlh3 t147 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 16));
                                                    gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                                                    gm_tnpifv20irnph t149;
                                                    GM_ST(gm_b16_8, ((uint8_t *)&t149 + 0), t148);
                                                    gm_f_format_72n0e07((void *)&gm_l48, (void *)NULL, cx, NULL, t149);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t150 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                                                    if (!t150) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 8, gm_src3k278vcmqgdr, 365, 38);
                                                    gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l48) + 8));
                                                    gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
                                                    gm_b0_1 t153 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t146, t147, t152);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    goto gm_lb18;
                                                }
                                                goto gm_lb21;
                                            }
gm_lb22:;
                                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 363, 9);
                                        }
gm_lb21:;
                                    }
gm_lb20:;
                                    uint64_t t154 = gm_l20;
                                    uint64_t r155; bool o156 = __builtin_add_overflow(t154, ((uint64_t)1ULL), &r155);
                                    uint64_t t157 = r155;
                                    gm_l20 = t157;
                                }
                            }
gm_lb18:;
                            gm_tkn42u7na3t81 t158;
                            memset(&t158, 0, sizeof t158);
                            gm_l22 = t158;
                            {
                                {
                                    uint8_t t159 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l18) + 32)) != 0);
                                    if (!t159) {
                                        goto gm_lb24;
                                    }
                                    void * t160 = GM_LD(void *, (((uint8_t *)&F.l18) + 32));
                                    gm_l23 = t160;
                                    void * t161 = gm_l0;
                                    uint64_t t162 = gm_l1;
                                    void * t163 = gm_l23;
                                    gm_t88rcucg1b7uk t164 = gm_f_resolve_type_jjodbei((void *)NULL, cx, NULL, t161, t162, t163);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tkn42u7na3t81 t165;
                                    GM_ST(uint8_t, (uint8_t *)&t165 + 5, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t165 + 6, 0);
                                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t165), t164);
                                    ((uint8_t *)&t165)[4] = 1;
                                    gm_l22 = t165;
                                    goto gm_lb23;
                                }
gm_lb24:;
                            }
gm_lb23:;
                            {
                                memmove(((uint8_t *)&F.l24), ((uint8_t *)&F.l14), 48);
                            }
                            {
                                {
                                    uint8_t t166 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l18) + 40)) != 0);
                                    if (!t166) {
                                        goto gm_lb26;
                                    }
                                    void * t167 = GM_LD(void *, (((uint8_t *)&F.l18) + 40));
                                    gm_l25 = t167;
                                    {
                                        {
                                            uint8_t t168 = (uint8_t)((((uint8_t *)&gm_l9))[4] != 0);
                                            if (t168) {
                                                goto gm_lb28;
                                            }
                                            gm_l49 = ((uint8_t)1);
                                            goto gm_lb27;
                                        }
gm_lb28:;
                                        {
                                            gm_l49 = ((uint8_t)0);
                                            goto gm_lb27;
                                        }
gm_lb29:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 373, 16);
                                    }
gm_lb27:;
                                    uint8_t t169 = gm_l49;
                                    if (t169) {
                                        void * t170 = gm_l0;
                                        void * t171 = gm_l25;
                                        gm_tl874t2atnlh3 t172 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t171) + 72));
                                        gm_b16_8 t173;
                                        GM_ST(void *, (uint8_t *)&t173, (void *)(gm_su38t05utsk53));
                                        GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(66));
                                        gm_b0_1 t174 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t170, t172, t173);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    void * t175 = gm_l0;
                                    uint64_t t176 = gm_l1;
                                    void * t177 = gm_l25;
                                    gm_f_try_untyped_fj0h8i5((void *)&gm_l50, (void *)NULL, cx, NULL, t175, t176, t177);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    {
                                        {
                                            uint8_t t178 = (uint8_t)((((uint8_t *)&gm_l50))[208] != 0);
                                            if (!t178) {
                                                goto gm_lb31;
                                            }
                                            uint8_t t179 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                                            uint8_t t180 = (uint8_t)(t179 == ((uint8_t)0ULL));
                                            if (!t180) {
                                                goto gm_lb31;
                                            }
                                            memmove(((uint8_t *)&F.l26), (((uint8_t *)&gm_l50) + 8), 200);
                                            {
                                                {
                                                    uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&F.l26) + 0));
                                                    uint8_t t182 = (uint8_t)(t181 == ((uint8_t)0ULL));
                                                    if (!t182) {
                                                        goto gm_lb33;
                                                    }
                                                    {
                                                        memmove(((uint8_t *)&F.l27), (((uint8_t *)&F.l26) + 8), 48);
                                                    }
                                                    {
                                                        memmove(((uint8_t *)&F.l24), ((uint8_t *)&F.l27), 48);
                                                    }
                                                    goto gm_lb32;
                                                }
gm_lb33:;
                                                void * t183 = gm_l0;
                                                void * t184 = gm_l25;
                                                gm_tl874t2atnlh3 t185 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t184) + 72));
                                                gm_b16_8 t186;
                                                GM_ST(void *, (uint8_t *)&t186, (void *)(gm_seoeuc6nik756));
                                                GM_ST(uintptr_t, (uint8_t *)&t186 + 8, (uintptr_t)(39));
                                                gm_b0_1 t187 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t183, t185, t186);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
gm_lb32:;
                                            goto gm_lb30;
                                        }
gm_lb31:;
                                        {
                                            uint8_t t188 = (uint8_t)((((uint8_t *)&gm_l50))[208] != 0);
                                            if (!t188) {
                                                goto gm_lb34;
                                            }
                                            uint8_t t189 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                                            uint8_t t190 = (uint8_t)(t189 == ((uint8_t)1ULL));
                                            if (!t190) {
                                                goto gm_lb34;
                                            }
                                            goto gm_lb30;
                                        }
gm_lb34:;
                                        {
                                            uint8_t t191 = (uint8_t)((((uint8_t *)&gm_l50))[208] != 0);
                                            if (t191) {
                                                goto gm_lb35;
                                            }
                                            void * t192 = gm_l0;
                                            void * t193 = gm_l25;
                                            gm_tl874t2atnlh3 t194 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t193) + 72));
                                            gm_b16_8 t195;
                                            GM_ST(void *, (uint8_t *)&t195, (void *)(gm_seoeuc6nik756));
                                            GM_ST(uintptr_t, (uint8_t *)&t195 + 8, (uintptr_t)(39));
                                            gm_b0_1 t196 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t192, t194, t195);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            goto gm_lb30;
                                        }
gm_lb35:;
                                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 374, 13);
                                    }
gm_lb30:;
                                    goto gm_lb25;
                                }
gm_lb26:;
                            }
gm_lb25:;
                            {
                                gm_tbci5ospk5nnn t197 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l24));
                                gm_tbci5ospk5nnn t198;
                                gm_f_one_08qg1op((void *)&t198, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_f_add_420lj6o((void *)&F.l14, (void *)NULL, cx, NULL, &t197, &t198);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            {
                                void * t199 = (void *)((uint8_t *)&F.l12);
                                gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
                                gm_tkn42u7na3t81 t201 = gm_l22;
                                gm_tbci5ospk5nnn t202 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l24));
                                gm_tckdobunaulee t203;
                                GM_ST(gm_b16_8, ((uint8_t *)&t203 + 0), t200);
                                GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t203 + 16), t201);
                                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t203 + 24), t202);
                                gm_tftb1odc739t5 t204 = gm_f_push_1j8vbii(t199, &t203);
                                gm_l51 = t204;
                            }
                            uint8_t t205 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
                            if (!t205) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_src3k278vcmqgdr, 387, 9);
                            void * t206 = (void *)((uint8_t *)&F.l13);
                            gm_tl874t2atnlh3 t207 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l18) + 48));
                            gm_tftb1odc739t5 t208 = gm_f_push_0mk7udh(t206, GM_CONV(gm_cseahvg8v7p0vs, t207));
                            gm_l52 = t208;
                            uint8_t t209 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                            if (!t209) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_src3k278vcmqgdr, 388, 9);
                            goto gm_lb16;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 362, 5);
                    }
gm_lb16:;
                }
gm_lb15:;
                uint64_t t210 = gm_l17;
                uint64_t r211; bool o212 = __builtin_add_overflow(t210, ((uint64_t)1ULL), &r211);
                uint64_t t213 = r211;
                gm_l17 = t213;
            }
        }
gm_lb13:;
    }
    {
        uint8_t t214 = gm_l4;
        if (t214) {
            {
                gm_trqfvmjtt4sij t215;
                gm_f_list_spqn36o((void *)&t215, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l28 = t215;
            }
            gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
            gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&t216));
            gm_l29 = t217;
            gm_l30 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t218 = gm_l30;
                    uint64_t t219 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                    uint8_t t220 = (t218 < t219);
                    if (!t220) {
                        goto gm_lb36;
                    }
                    {
                        {
                            uint64_t t221 = gm_l30;
                            void * t222 = GM_LD(void *, ((uint8_t *)&gm_l29));
                            uint64_t t223 = GM_LD(uint64_t, (((uint8_t *)&gm_l29) + 8));
                            uint64_t t224 = gm_l30;
                            gm_tckdobunaulee t225 = GM_LD(gm_tckdobunaulee, (((uint8_t *)t222) + (uintptr_t)t224 * 72));
                            GM_ST(uint64_t, ((uint8_t *)&gm_l53 + 0), t221);
                            GM_ST(gm_tckdobunaulee, ((uint8_t *)&gm_l53 + 8), t225);
                        }
                        {
                            {
                                uint64_t t226 = GM_LD(uint64_t, ((uint8_t *)&gm_l53));
                                gm_l31 = t226;
                                memmove(((uint8_t *)&F.l32), (((uint8_t *)&gm_l53) + 8), 72);
                                {
                                    {
                                        uint8_t t227 = (uint8_t)(((((uint8_t *)&F.l32) + 16))[4] != 0);
                                        if (!t227) {
                                            goto gm_lb42;
                                        }
                                        gm_t88rcucg1b7uk t228 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l32) + 16));
                                        gm_l33 = t228;
                                        void * t229 = (void *)((uint8_t *)&F.l28);
                                        gm_b16_8 t230 = GM_LD(gm_b16_8, ((uint8_t *)&F.l32));
                                        gm_t88rcucg1b7uk t231 = gm_l33;
                                        gm_tf5srh4mp4b3i t232;
                                        memset(&t232, 0, sizeof t232);
                                        gm_t9qkh0rsooc8d t233;
                                        GM_ST(uint8_t, ((uint8_t *)&t233 + 0), ((uint8_t)2ULL));
                                        gm_t0mglksrkfa5e t234;
                                        GM_ST(uint8_t, (uint8_t *)&t234 + 21, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t234 + 22, 0);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t234 + 0), t230);
                                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t234 + 16), t231);
                                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t234 + 20), t233);
                                        GM_ST(gm_tf5srh4mp4b3i, ((uint8_t *)&t234 + 24), t232);
                                        gm_tftb1odc739t5 t235 = gm_f_push_vrmo7c5(t229, &t234);
                                        gm_l54 = t235;
                                        uint8_t t236 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                                        if (!t236) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_src3k278vcmqgdr, 394, 17);
                                        goto gm_lb41;
                                    }
gm_lb42:;
                                    void * t237 = gm_l0;
                                    void * t238 = GM_LD(void *, ((uint8_t *)&F.l13));
                                    uint64_t t239 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 8));
                                    uint64_t t240 = gm_l31;
                                    if ((uintptr_t)t240 >= (uintptr_t)(t239)) gm_trap_index((uintptr_t)t240, t239, gm_src3k278vcmqgdr, 396, 25);
                                    gm_tl874t2atnlh3 t241 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t238) + (uintptr_t)t240 * 12));
                                    gm_b16_8 t242;
                                    GM_ST(void *, (uint8_t *)&t242, (void *)(gm_s8h39pil8lgh0));
                                    GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(40));
                                    gm_b0_1 t243 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t237, t241, t242);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                }
gm_lb41:;
                                goto gm_lb39;
                            }
gm_lb40:;
                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 392, 9);
                        }
gm_lb39:;
                    }
gm_lb38:;
                    uint64_t t244 = gm_l30;
                    uint64_t r245; bool o246 = __builtin_add_overflow(t244, ((uint64_t)1ULL), &r245);
                    uint64_t t247 = r245;
                    gm_l30 = t247;
                }
            }
gm_lb36:;
            {
                gm_b16_8 t248 = GM_LD(gm_b16_8, ((uint8_t *)&F.l28));
                memset(&(*(gm_ti0kgh3pc3in2 *)gm_out), 0, sizeof (*(gm_ti0kgh3pc3in2 *)gm_out));
                GM_ST(uint8_t, ((uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 0), ((uint8_t)2ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 8), t248);
                goto gm_fdone;
            }
        }
        uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)&F.l12) + 8));
        gm_l34 = t249;
        uint8_t t250 = (uint8_t)((((uint8_t *)&gm_l9))[4] != 0);
        if (t250) {
            gm_t88rcucg1b7uk t251 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l9));
            gm_l35 = t251;
        } else {
            uint64_t t252 = gm_l34;
            uint8_t t253 = (t252 <= ((uint64_t)256ULL));
            if (t253) {
                void * t254 = gm_l0;
                void * t255 = (void *)(((uint8_t *)t254) + 8);
                gm_t88rcucg1b7uk t256 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t255);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l55 = t256;
            } else {
                uint64_t t257 = gm_l34;
                uint8_t t258 = (t257 <= ((uint64_t)65536ULL));
                if (t258) {
                    void * t259 = gm_l0;
                    void * t260 = (void *)(((uint8_t *)t259) + 8);
                    gm_t88rcucg1b7uk t261 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t260, ((uint8_t)0), ((uint16_t)16ULL));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l56 = t261;
                } else {
                    void * t262 = gm_l0;
                    void * t263 = (void *)(((uint8_t *)t262) + 8);
                    gm_t88rcucg1b7uk t264 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t263, ((uint8_t)0), ((uint16_t)32ULL));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l56 = t264;
                }
                gm_t88rcucg1b7uk t265 = gm_l56;
                gm_l55 = t265;
            }
            gm_t88rcucg1b7uk t266 = gm_l55;
            gm_l35 = t266;
        }
    }
    {
        gm_b16_8 t267 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
        gm_b16_8 t268 = GM_LD(gm_b16_8, ((uint8_t *)&t267));
        gm_l36 = t268;
        gm_l37 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t269 = gm_l37;
                uint64_t t270 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                uint8_t t271 = (t269 < t270);
                if (!t271) {
                    goto gm_lb43;
                }
                {
                    {
                        uint64_t t272 = gm_l37;
                        void * t273 = GM_LD(void *, ((uint8_t *)&gm_l36));
                        uint64_t t274 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                        uint64_t t275 = gm_l37;
                        gm_tckdobunaulee t276 = GM_LD(gm_tckdobunaulee, (((uint8_t *)t273) + (uintptr_t)t275 * 72));
                        GM_ST(uint64_t, ((uint8_t *)&gm_l57 + 0), t272);
                        GM_ST(gm_tckdobunaulee, ((uint8_t *)&gm_l57 + 8), t276);
                    }
                    {
                        {
                            uint64_t t277 = GM_LD(uint64_t, ((uint8_t *)&gm_l57));
                            gm_l38 = t277;
                            memmove(((uint8_t *)&F.l39), (((uint8_t *)&gm_l57) + 8), 72);
                            {
                                {
                                    void * t278 = gm_l0;
                                    void * t279 = (void *)(((uint8_t *)t278) + 8);
                                    gm_t88rcucg1b7uk t280 = gm_l35;
                                    uint8_t t281 = gm_f_is_error_8k27gd1(t279, GM_CONV(gm_csguankko4lmhd, t280));
                                    uint8_t t282 = (uint8_t)!t281;
                                    if (!t282) {
                                        goto gm_lb49;
                                    }
                                    {
                                        void * t283 = gm_l0;
                                        gm_t88rcucg1b7uk t284 = gm_l35;
                                        uint8_t t285 = gm_f_int_fits_imbqv0c((void *)NULL, cx, NULL, t283, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l39) + 24)), t284);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t286 = (uint8_t)!t285;
                                        if (!t286) {
                                            goto gm_lb49;
                                        }
                                    }
                                    {
                                        void * t287 = gm_l0;
                                        void * t288 = GM_LD(void *, ((uint8_t *)&F.l13));
                                        uint64_t t289 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 8));
                                        uint64_t t290 = gm_l38;
                                        if ((uintptr_t)t290 >= (uintptr_t)(t289)) gm_trap_index((uintptr_t)t290, t289, gm_src3k278vcmqgdr, 405, 21);
                                        gm_tl874t2atnlh3 t291 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t288) + (uintptr_t)t290 * 12));
                                        gm_b16_8 t292 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l39) + 24)));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t293 = GM_LD(gm_b16_8, ((uint8_t *)&F.l39));
                                        void * t294 = gm_l0;
                                        gm_t88rcucg1b7uk t295 = gm_l35;
                                        gm_b16_8 t296 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t294, t295);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tfeab279c16rr t297;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t297 + 0), t292);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t297 + 16), t293);
                                        GM_ST(gm_b16_8, ((uint8_t *)&t297 + 32), t296);
                                        gm_f_format_nag8m5u((void *)&gm_l58, (void *)NULL, cx, NULL, &t297);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t298 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                        if (!t298) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 8, gm_src3k278vcmqgdr, 405, 37);
                                        gm_b16_8 t299 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                                        gm_b16_8 t300 = GM_LD(gm_b16_8, ((uint8_t *)&t299));
                                        gm_b0_1 t301 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t287, t291, t300);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    goto gm_lb48;
                                }
gm_lb49:;
                            }
gm_lb48:;
                            void * t302 = GM_LD(void *, ((uint8_t *)&F.l12));
                            uint64_t t303 = GM_LD(uint64_t, (((uint8_t *)&F.l12) + 8));
                            uint64_t t304 = gm_l38;
                            uint64_t t305 = (uintptr_t)((uint64_t)0ULL);
                            uint64_t t306 = (uintptr_t)t304;
                            if (t305 > t306 || t306 > (uintptr_t)t303) gm_trap_slice(t305, t306, t303, gm_src3k278vcmqgdr, 407, 31);
                            uint64_t t307 = ((uintptr_t)t304) - (uintptr_t)((uint64_t)0ULL);
                            gm_b16_8 t308;
                            GM_ST(void *, (uint8_t *)&t308, (void *)(((uint8_t *)t302 + (uintptr_t)((uint64_t)0ULL) * 72)));
                            GM_ST(uintptr_t, (uint8_t *)&t308 + 8, (uintptr_t)(t307));
                            gm_b16_8 t309 = GM_LD(gm_b16_8, ((uint8_t *)&t308));
                            gm_l40 = t309;
                            gm_l41 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t310 = gm_l41;
                                    uint64_t t311 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                                    uint8_t t312 = (t310 < t311);
                                    if (!t312) {
                                        goto gm_lb50;
                                    }
                                    {
                                        {
                                            uint64_t t313 = gm_l41;
                                            void * t314 = GM_LD(void *, ((uint8_t *)&gm_l40));
                                            uint64_t t315 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                                            uint64_t t316 = gm_l41;
                                            gm_tckdobunaulee t317 = GM_LD(gm_tckdobunaulee, (((uint8_t *)t314) + (uintptr_t)t316 * 72));
                                            GM_ST(uint64_t, ((uint8_t *)&gm_l59 + 0), t313);
                                            GM_ST(gm_tckdobunaulee, ((uint8_t *)&gm_l59 + 8), t317);
                                        }
                                        {
                                            {
                                                uint64_t t318 = GM_LD(uint64_t, ((uint8_t *)&gm_l59));
                                                gm_l42 = t318;
                                                {
                                                    memmove(((uint8_t *)&F.l43), (((uint8_t *)&gm_l59) + 8), 72);
                                                }
                                                {
                                                    uint8_t t319 = gm_f_eq_60fjn21(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l43) + 24)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l39) + 24)));
                                                    if (t319) {
                                                        {
                                                            void * t320 = GM_LD(void *, ((uint8_t *)&F.l13));
                                                            uint64_t t321 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 8));
                                                            uint64_t t322 = gm_l38;
                                                            if ((uintptr_t)t322 >= (uintptr_t)(t321)) gm_trap_index((uintptr_t)t322, t321, gm_src3k278vcmqgdr, 409, 33);
                                                            gm_tl874t2atnlh3 t323 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t320) + (uintptr_t)t322 * 12));
                                                            gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&F.l43));
                                                            gm_b16_8 t325 = GM_LD(gm_b16_8, ((uint8_t *)&F.l39));
                                                            gm_b16_8 t326 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l39) + 24)));
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            gm_tfeab279c16rr t327;
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t327 + 0), t324);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t327 + 16), t325);
                                                            GM_ST(gm_b16_8, ((uint8_t *)&t327 + 32), t326);
                                                            gm_f_format_uaa99rp((void *)&gm_l60, (void *)NULL, cx, NULL, &t327);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            uint8_t t328 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                                            if (!t328) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 8, gm_src3k278vcmqgdr, 409, 49);
                                                            gm_b16_8 t329 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l60) + 8));
                                                            gm_b16_8 t330 = GM_LD(gm_b16_8, ((uint8_t *)&t329));
                                                            gm_t4gut2c9fj1kt t331;
                                                            gm_f_error_s6hn871((void *)&t331, (void *)NULL, cx, NULL, t323, t330);
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                            F.l44 = t331;
                                                        }
                                                        void * t332 = (void *)((uint8_t *)&F.l44);
                                                        void * t333 = GM_LD(void *, ((uint8_t *)&F.l13));
                                                        uint64_t t334 = GM_LD(uint64_t, (((uint8_t *)&F.l13) + 8));
                                                        uint64_t t335 = gm_l42;
                                                        if ((uintptr_t)t335 >= (uintptr_t)(t334)) gm_trap_index((uintptr_t)t335, t334, gm_src3k278vcmqgdr, 410, 29);
                                                        gm_tl874t2atnlh3 t336 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t333) + (uintptr_t)t335 * 12));
                                                        gm_b16_8 t337;
                                                        GM_ST(void *, (uint8_t *)&t337, (void *)(gm_sbe7dr7t400bh));
                                                        GM_ST(uintptr_t, (uint8_t *)&t337 + 8, (uintptr_t)(15));
                                                        gm_f_add_label_50fj7vv(t332, GM_CONV(gm_cseahvg8v7p0vs, t336), t337);
                                                        {
                                                            void * t338 = gm_l0;
                                                            gm_b0_1 t339 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t338, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l44)));
                                                            if (gm_unw.kind) {
                                                                goto gm_lad;
                                                            }
                                                        }
                                                        goto gm_lb50;
                                                    }
                                                }
                                                goto gm_lb53;
                                            }
gm_lb54:;
                                            gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 407, 9);
                                        }
gm_lb53:;
                                    }
gm_lb52:;
                                    uint64_t t340 = gm_l41;
                                    uint64_t r341; bool o342 = __builtin_add_overflow(t340, ((uint64_t)1ULL), &r341);
                                    uint64_t t343 = r341;
                                    gm_l41 = t343;
                                }
                            }
gm_lb50:;
                            goto gm_lb46;
                        }
gm_lb47:;
                        gm_trap("no match arm matched the scrutinee", gm_src3k278vcmqgdr, 403, 5);
                    }
gm_lb46:;
                }
gm_lb45:;
                uint64_t t344 = gm_l37;
                uint64_t r345; bool o346 = __builtin_add_overflow(t344, ((uint64_t)1ULL), &r345);
                uint64_t t347 = r345;
                gm_l37 = t347;
            }
        }
gm_lb43:;
    }
    {
        gm_t88rcucg1b7uk t348 = gm_l35;
        gm_b16_8 t349 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
        gm_tcmkompbl09il t350;
        GM_ST(uint32_t, (uint8_t *)&t350 + 4, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t350 + 0), t348);
        GM_ST(gm_b16_8, ((uint8_t *)&t350 + 8), t349);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 48, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 0), ((uint8_t)1ULL));
        GM_ST(gm_tcmkompbl09il, ((uint8_t *)&(*(gm_ti0kgh3pc3in2 *)gm_out) + 8), t350);
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
inline __attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_union_variants_a7khaqp F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* id */
    gm_t88rcucg1b7uk gm_l3; /* tag */
    gm_b16_8 gm_l4; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 220u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l5;
    {
        {
            void * t6 = gm_l0;
            void * t7 = (void *)(((uint8_t *)t6) + 8);
            gm_t88rcucg1b7uk t8 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l5, t7, GM_CONV(gm_csguankko4lmhd, t8));
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)17ULL));
            if (!t10) {
                goto gm_lb2;
            }
            uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 8));
            gm_l2 = t11;
            {
                {
                    void * t12 = gm_l0;
                    void * t13 = GM_LD(void *, (((uint8_t *)t12) + 1088));
                    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)t12) + 1088) + 8));
                    uint32_t t15 = gm_l2;
                    uint64_t t16 = (uint64_t)t15;
                    if ((uintptr_t)t16 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t16, t14, gm_src3k278vcmqgdr, 510, 30);
                    uint8_t t17 = GM_LD(uint8_t, (((((uint8_t *)t13) + (uintptr_t)t16 * 184) + 24) + 0));
                    uint8_t t18 = (uint8_t)(t17 == ((uint8_t)1ULL));
                    if (!t18) {
                        goto gm_lb4;
                    }
                    gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t13) + (uintptr_t)t16 * 184) + 24) + 8));
                    gm_l3 = t19;
                    gm_b16_8 t20 = GM_LD(gm_b16_8, ((((((uint8_t *)t13) + (uintptr_t)t16 * 184) + 24) + 8) + 8));
                    gm_l4 = t20;
                    {
                        gm_b16_8 t21 = gm_l4;
                        gm_t88rcucg1b7uk t22 = gm_l3;
                        gm_tep2drsl9408b t23;
                        GM_ST(uint32_t, (uint8_t *)&t23 + 20, 0);
                        GM_ST(gm_b16_8, ((uint8_t *)&t23 + 0), t21);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t23 + 16), t22);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)gm_out) + 25, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)gm_out) + 26, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tdlpv0q69lov1 *)gm_out) + 28, 0);
                        GM_ST(gm_tep2drsl9408b, ((uint8_t *)&(*(gm_tdlpv0q69lov1 *)gm_out)), t23);
                        ((uint8_t *)&(*(gm_tdlpv0q69lov1 *)gm_out))[24] = 1;
                        goto gm_fdone;
                    }
                }
gm_lb4:;
                memset(&(*(gm_tdlpv0q69lov1 *)gm_out), 0, sizeof (*(gm_tdlpv0q69lov1 *)gm_out));
                goto gm_fdone;
            }
gm_lb3:;
        }
gm_lb2:;
        memset(&(*(gm_tdlpv0q69lov1 *)gm_out), 0, sizeof (*(gm_tdlpv0q69lov1 *)gm_out));
        goto gm_fdone;
    }
gm_lb1:;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tdlpv0q69lov1 gm_f_union_variants_a7khaqp_c(void * a0, gm_csguankko4lmhd a1) { gm_tdlpv0q69lov1 gm_r; gm_f_union_variants_a7khaqp(&gm_r, a0, a1); return gm_r; }

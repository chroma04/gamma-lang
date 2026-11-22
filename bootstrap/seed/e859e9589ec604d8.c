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
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_keqa9hq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ui0bpsf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern uint8_t gm_c07srrruo2c[11] __attribute__((aligned(1)));
extern uint8_t gm_c19scujv4gq[6] __attribute__((aligned(1)));
extern uint8_t gm_c1s0khhquf5[7] __attribute__((aligned(1)));
extern uint8_t gm_c2rl097cgcd[16] __attribute__((aligned(1)));
extern uint8_t gm_c2t15afgpnn[8] __attribute__((aligned(8)));
extern uint8_t gm_c3hm1rk1ipa[10] __attribute__((aligned(1)));
extern uint8_t gm_c423srue241[9] __attribute__((aligned(1)));
extern uint8_t gm_c42ctv8im24[920] __attribute__((aligned(8)));
extern uint8_t gm_c4qfm55t8rc[3] __attribute__((aligned(1)));
extern uint8_t gm_c4rs7sohibn[5] __attribute__((aligned(1)));
extern uint8_t gm_c50isunhcrj[16] __attribute__((aligned(8)));
extern uint8_t gm_c5cvc3ab6qo[150] __attribute__((aligned(1)));
extern uint8_t gm_c5d9memamu5[6] __attribute__((aligned(1)));
extern uint8_t gm_c699d0m66pa[1] __attribute__((aligned(1)));
extern uint8_t gm_c6fgtimd3il[6] __attribute__((aligned(1)));
extern uint8_t gm_c82i6abqvvu[4] __attribute__((aligned(1)));
extern uint8_t gm_c886bolkgpe[9] __attribute__((aligned(1)));
extern uint8_t gm_c9la6b8qclf[10] __attribute__((aligned(1)));
extern uint8_t gm_c9pef0g9fng[4] __attribute__((aligned(4)));
extern uint8_t gm_cbocru68uam[11] __attribute__((aligned(1)));
extern uint8_t gm_ccvvl33tsdt[12] __attribute__((aligned(1)));
extern uint8_t gm_cdlh2b67kra[8] __attribute__((aligned(8)));
extern uint8_t gm_celc7l631oc[13] __attribute__((aligned(1)));
extern uint8_t gm_cen8idnq6iv[6] __attribute__((aligned(1)));
extern uint8_t gm_cenuluelo82[3] __attribute__((aligned(1)));
extern uint8_t gm_cepccru8ka8[6] __attribute__((aligned(1)));
extern uint8_t gm_cfql7t1kn72[14] __attribute__((aligned(1)));
extern uint8_t gm_cfslp7hoejg[3] __attribute__((aligned(1)));
extern uint8_t gm_cg6c770qgrk[11] __attribute__((aligned(1)));
extern uint8_t gm_cg7qavecf9k[3] __attribute__((aligned(1)));
extern uint8_t gm_cgdfdu1u5gd[7] __attribute__((aligned(1)));
extern uint8_t gm_cgsatlrqc4j[3] __attribute__((aligned(1)));
extern uint8_t gm_chtdbvfshqk[10] __attribute__((aligned(1)));
extern uint8_t gm_cits27rfpla[17] __attribute__((aligned(1)));
extern uint8_t gm_ckbflbrril1[1] __attribute__((aligned(1)));
extern uint8_t gm_ckmj20kunc0[4] __attribute__((aligned(1)));
extern uint8_t gm_cl17cr2em63[9] __attribute__((aligned(1)));
extern uint8_t gm_cl1d82mrtnv[40] __attribute__((aligned(1)));
extern uint8_t gm_clb2b60g1qs[13] __attribute__((aligned(1)));
extern uint8_t gm_clg4nkf458e[8] __attribute__((aligned(1)));
extern uint8_t gm_cm0ptlu1qd7[25] __attribute__((aligned(1)));
extern uint8_t gm_cm8gajkonri[6] __attribute__((aligned(1)));
extern uint8_t gm_cn824b4bhc1[8] __attribute__((aligned(1)));
extern uint8_t gm_cnj6gqa99nb[14] __attribute__((aligned(1)));
extern uint8_t gm_cnna9skotcp[9] __attribute__((aligned(1)));
extern uint8_t gm_cntjgiv42f7[23] __attribute__((aligned(1)));
extern uint8_t gm_co987o3ird4[8] __attribute__((aligned(1)));
extern uint8_t gm_cot5n1cq694[16] __attribute__((aligned(1)));
extern uint8_t gm_cpkd7s1srcc[4933] __attribute__((aligned(1)));
extern uint8_t gm_cpma9mav80s[3] __attribute__((aligned(1)));
extern uint8_t gm_cpn2np70vcf[8] __attribute__((aligned(8)));
extern uint8_t gm_cpupn61v7jf[12] __attribute__((aligned(1)));
extern uint8_t gm_cr5mkoa1b3c[10] __attribute__((aligned(1)));
extern uint8_t gm_crbso58uocb[10] __attribute__((aligned(1)));
extern uint8_t gm_cri2dckkbh8[9] __attribute__((aligned(1)));
extern uint8_t gm_crpb2rekieb[1] __attribute__((aligned(1)));
extern uint8_t gm_csjir7b1m9j[14] __attribute__((aligned(1)));
extern uint8_t gm_csp8mutqj4e[13] __attribute__((aligned(1)));
extern uint8_t gm_csp8t1dbqmf[13] __attribute__((aligned(1)));
extern uint8_t gm_csu9mkbsfpo[16] __attribute__((aligned(8)));
extern uint8_t gm_csuv6tb6jnj[38] __attribute__((aligned(1)));
extern uint8_t gm_csvkj3mk95q[11] __attribute__((aligned(1)));
extern uint8_t gm_ct6psi7af52[17] __attribute__((aligned(1)));
extern uint8_t gm_ct8d43r6fpj[13] __attribute__((aligned(1)));
extern uint8_t gm_ctcdekq6f2q[11] __attribute__((aligned(1)));
extern uint8_t gm_ctgi64l27km[15] __attribute__((aligned(1)));
extern uint8_t gm_ctq6t8frtor[4] __attribute__((aligned(4)));
extern uint8_t gm_cu0b9l65c0v[1] __attribute__((aligned(1)));
extern uint8_t gm_cu6pb2rap57[3] __attribute__((aligned(1)));
extern uint8_t gm_cuqbj62gqhf_2[16] __attribute__((aligned(8)));
extern uint8_t gm_cvb3726geh8[3] __attribute__((aligned(1)));
extern void *const gm_hlc7jtja1te5[];
extern void *const gm_hlvtf175jpmc[];
extern uint8_t gm_k0nv110r163[40] __attribute__((aligned(8)));
extern uint8_t gm_k48l3rluocl[40] __attribute__((aligned(8)));
extern uint8_t gm_k65c70p8jkg[40] __attribute__((aligned(8)));
extern uint8_t gm_k6be0mlmbp5[40] __attribute__((aligned(8)));
extern uint8_t gm_k72nh3emov2[40] __attribute__((aligned(8)));
extern uint8_t gm_k7bkj4i7gae[40] __attribute__((aligned(8)));
extern uint8_t gm_ka73u23j5nf[40] __attribute__((aligned(8)));
extern uint8_t gm_kdmnul2f0o4[40] __attribute__((aligned(8)));
extern uint8_t gm_kdpqmkpiene[40] __attribute__((aligned(8)));
extern uint8_t gm_kgj4rhjmudg[40] __attribute__((aligned(8)));
extern uint8_t gm_ki39jj5pj1o[40] __attribute__((aligned(8)));
extern uint8_t gm_kjn1j0gvnde[40] __attribute__((aligned(8)));
extern uint8_t gm_km7a24is5mf[40] __attribute__((aligned(8)));
extern uint8_t gm_ko53fduln7u[40] __attribute__((aligned(8)));
extern uint8_t gm_kq6te48ps2f[40] __attribute__((aligned(8)));
extern uint8_t gm_ks677q1halv[40] __attribute__((aligned(8)));
extern uint8_t gm_kt2nhjs8rv5[40] __attribute__((aligned(8)));
extern uint8_t gm_ktjao6lvic2[40] __attribute__((aligned(8)));
extern uint8_t gm_ku5cbgje11b[40] __attribute__((aligned(8)));
extern uint8_t gm_kue41kf0tbk[40] __attribute__((aligned(8)));
extern uint8_t gm_kv89j160jqp[40] __attribute__((aligned(8)));
extern const uint8_t gm_s00046u02oar6[41];
extern const uint8_t gm_s00989rdgst9u[20];
extern const uint8_t gm_s01ofvj3chukm[10];
extern const uint8_t gm_s02aodrl2fpra[12];
extern const uint8_t gm_s03c24qg7i06q[13];
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s05336tfii06a[18];
extern const uint8_t gm_s078u1763sqk3[11];
extern const uint8_t gm_s07uhsrd798kj[9];
extern const uint8_t gm_s08dcqdp58r5f[3];
extern const uint8_t gm_s0adesbq24e9u[6];
extern const uint8_t gm_s0cf71ouneh5m[12];
extern const uint8_t gm_s0cpkv7s4ef85[24];
extern const uint8_t gm_s0doambt0iqhe[4];
extern const uint8_t gm_s0e9ru2ha5ibt[103];
extern const uint8_t gm_s0ggo8ek7kfrt[34];
extern const uint8_t gm_s0ghv8aql4j5t[19];
extern const uint8_t gm_s0iditmthmvpu[21];
extern const uint8_t gm_s0j2j9kdc2obh[17];
extern const uint8_t gm_s0jtk73jm5ft9[16];
extern const uint8_t gm_s0lrfrtvffhu7[4];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0m9d22aen6f8[2];
extern const uint8_t gm_s0mph2pjoa3np[17];
extern const uint8_t gm_s0notp50gncsl[12];
extern const uint8_t gm_s0odi4lbspe7f[25];
extern const uint8_t gm_s0pi0emdqd3vb[2];
extern const uint8_t gm_s0qbjm15aot6s[11];
extern const uint8_t gm_s0rsc8ficqfcm[78];
extern const uint8_t gm_s0se3p14t5670[14];
extern const uint8_t gm_s108thc59vc7u[37];
extern const uint8_t gm_s11lbv9jc177b[4];
extern const uint8_t gm_s129prqlnb2kt[41];
extern const uint8_t gm_s148evlodt315[18];
extern const uint8_t gm_s14bn21gpuj3j[3];
extern const uint8_t gm_s15c9j2vv1d2a[39];
extern const uint8_t gm_s15p9kmrsfogp[2];
extern const uint8_t gm_s16mf4cvajh59[13];
extern const uint8_t gm_s1af5e7vkvjv9[8];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1bpuqd44lur7[7];
extern const uint8_t gm_s1ctr86inn5iu[24];
extern const uint8_t gm_s1dc9boe5unhg[32];
extern const uint8_t gm_s1e7k26kdaoj3[31];
extern const uint8_t gm_s1gm43jp1jqr0[89];
extern const uint8_t gm_s1isj7vodtbvg[37];
extern const uint8_t gm_s1jdtclvlju06[9];
extern const uint8_t gm_s1leke9tn7lrm[8];
extern const uint8_t gm_s1livc5021kui[9];
extern const uint8_t gm_s1lj31vo6lvqe[26];
extern const uint8_t gm_s1lnh4indnr62[4];
extern const uint8_t gm_s1rdbiku62o8o[41];
extern const uint8_t gm_s1smed2pig5ts[35];
extern const uint8_t gm_s1spbnihet3ou[25];
extern const uint8_t gm_s1suv7j29n0ea[21];
extern const uint8_t gm_s1t5armttgi3e[41];
extern const uint8_t gm_s1tdqlhdsnu5l[14];
extern const uint8_t gm_s1tqe3mtj6vb1[8];
extern const uint8_t gm_s211k6darac91[10];
extern const uint8_t gm_s23t712hkqlmp[5];
extern const uint8_t gm_s25h5btu4ug6a[12];
extern const uint8_t gm_s26crtotu14pn[3];
extern const uint8_t gm_s29jv7rttsnpq[52];
extern const uint8_t gm_s2cartb32o2of[10];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s2hi6cc9n75l5[7];
extern const uint8_t gm_s2hut6059od59[42];
extern const uint8_t gm_s2j8t1jqamft8[5];
extern const uint8_t gm_s2js1cb9eproh[41];
extern const uint8_t gm_s2k3mi7crs28d[5];
extern const uint8_t gm_s2nf76g2su2k3[15];
extern const uint8_t gm_s2nn3moc8tfri[17];
extern const uint8_t gm_s2nq2i5ut1dni[13];
extern const uint8_t gm_s2of8ntot1on0[81];
extern const uint8_t gm_s2tnbv3fg2r22[13];
extern const uint8_t gm_s2u46tincrqmv[25];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s2u7e0bju5qb8[28];
extern const uint8_t gm_s332n2u4obj9h[49];
extern const uint8_t gm_s336v1oga58d4[41];
extern const uint8_t gm_s33pl04oikskr[5];
extern const uint8_t gm_s34hhp1ucrags[12];
extern const uint8_t gm_s35u3nlsc2qa0[27];
extern const uint8_t gm_s38tgoanp9o78[66];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3c7ikv4cpqg1[13];
extern const uint8_t gm_s3e9133b3pasn[3];
extern const uint8_t gm_s3hb2q98sfsao[5];
extern const uint8_t gm_s3l29dojrpo6a[12];
extern const uint8_t gm_s3loluab89lqt[11];
extern const uint8_t gm_s3lovgpoph0mh[9];
extern const uint8_t gm_s3o8vm6p6efhi[21];
extern const uint8_t gm_s3on77ql0ndko[17];
extern const uint8_t gm_s3op5s1agh0ju[35];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3qu37je1jc8f[46];
extern const uint8_t gm_s3rph20cdvn0u[16];
extern const uint8_t gm_s3sb6rrf98gpi[20];
extern const uint8_t gm_s3v312rvfthpf[43];
extern const uint8_t gm_s44ek1gecbajs[7];
extern const uint8_t gm_s4769el4glhs1[4];
extern const uint8_t gm_s487q6bd4pfu3[50];
extern const uint8_t gm_s498ag88l373e[14];
extern const uint8_t gm_s4a4j4epvkeso[11];
extern const uint8_t gm_s4a980fahtblo[3];
extern const uint8_t gm_s4b8airp3rh69[50];
extern const uint8_t gm_s4chbmodjkinv[7];
extern const uint8_t gm_s4dg5a71bcfsh[24];
extern const uint8_t gm_s4emnk8r9sirq[11];
extern const uint8_t gm_s4gh6uvc8i8um[24];
extern const uint8_t gm_s4gilalcmdvu3[8];
extern const uint8_t gm_s4ifh5kc7nc0d[62];
extern const uint8_t gm_s4jh2uktkqkro[22];
extern const uint8_t gm_s4k57chtk2mkc[88];
extern const uint8_t gm_s4kt3mhlp0or9[11];
extern const uint8_t gm_s4m9nol66dh0a[25];
extern const uint8_t gm_s4nj79mj9jk57[21];
extern const uint8_t gm_s4o70d89183c2[4];
extern const uint8_t gm_s4ou57tmjn6c7[39];
extern const uint8_t gm_s4r2qm241merr[12];
extern const uint8_t gm_s4rmlksbchusu[11];
extern const uint8_t gm_s4sak9mcv04d9[25];
extern const uint8_t gm_s4ubro9t1tgos[17];
extern const uint8_t gm_s512m3mlvu0cv[106];
extern const uint8_t gm_s519l85f26pmp[81];
extern const uint8_t gm_s51lfi73niqcd[62];
extern const uint8_t gm_s52n7ji3kulf2[29];
extern const uint8_t gm_s53c18pcajigs[51];
extern const uint8_t gm_s55edm0in8jpg[19];
extern const uint8_t gm_s588gr73ph5f5[45];
extern const uint8_t gm_s58b2tdsrfkg1[85];
extern const uint8_t gm_s58ndvsqjhesf[6];
extern const uint8_t gm_s59a022mrijic[9];
extern const uint8_t gm_s5bq0p1ohhqqe[15];
extern const uint8_t gm_s5gbgbdradbg1[25];
extern const uint8_t gm_s5i05gtdgdefb[42];
extern const uint8_t gm_s5ivi0sts5foi[9];
extern const uint8_t gm_s5lqhiu2bn6of[8];
extern const uint8_t gm_s5n5e484glelv[5];
extern const uint8_t gm_s5q33up034m64[37];
extern const uint8_t gm_s5r5k5b2k7bru[20];
extern const uint8_t gm_s5ucn4crtdrq8[11];
extern const uint8_t gm_s632oftf9nssu[43];
extern const uint8_t gm_s63o5blu3j46d[16];
extern const uint8_t gm_s650q383sn58v[41];
extern const uint8_t gm_s65rai75evpi7[14];
extern const uint8_t gm_s66fbu3uhl900[20];
extern const uint8_t gm_s66fk044n1n0k[9];
extern const uint8_t gm_s66ph0gfa95db[14];
extern const uint8_t gm_s68rktnu8slfa[25];
extern const uint8_t gm_s69kv55uiaqoj[10];
extern const uint8_t gm_s6clc0pmaeksj[18];
extern const uint8_t gm_s6d960pgtv3l9[26];
extern const uint8_t gm_s6db4pm3k5olv[3];
extern const uint8_t gm_s6dh2ca0ubali[7];
extern const uint8_t gm_s6hk4348mssfv[41];
extern const uint8_t gm_s6jld2vf184ip[28];
extern const uint8_t gm_s6khfdbr5aof9[34];
extern const uint8_t gm_s6m55kvvtug5k[12];
extern const uint8_t gm_s6ocl6n3lno8m[8];
extern const uint8_t gm_s6rivmf4qclvb[71];
extern const uint8_t gm_s6sv4vhunlp6l[9];
extern const uint8_t gm_s6t4r831mukdr[3];
extern const uint8_t gm_s6vmoui2ivq06[10];
extern const uint8_t gm_s70dhiaqijjg0[24];
extern const uint8_t gm_s721l5hj93rdl[50];
extern const uint8_t gm_s74v1q63ilf9i[122];
extern const uint8_t gm_s75m3dt9015lh[15];
extern const uint8_t gm_s76ovr591an92[4];
extern const uint8_t gm_s77fi68iu5q6n[5];
extern const uint8_t gm_s7826b3jmrqb0[10];
extern const uint8_t gm_s794tb9qjlq1h[42];
extern const uint8_t gm_s7atji10aaq5k[7];
extern const uint8_t gm_s7b0d890pj8dv[14];
extern const uint8_t gm_s7b7j2m1f6fle[71];
extern const uint8_t gm_s7bj0jma5l2mg[39];
extern const uint8_t gm_s7cn2q9qfeafa[79];
extern const uint8_t gm_s7euumoo24h31[3];
extern const uint8_t gm_s7f47bs0i7q74[21];
extern const uint8_t gm_s7i7uukpl183c[4];
extern const uint8_t gm_s7ilf06vf0fi1[22];
extern const uint8_t gm_s7ipvt5onv6ht[4];
extern const uint8_t gm_s7mmjn8cgbhv8[67];
extern const uint8_t gm_s7nnecs6i9eki[41];
extern const uint8_t gm_s7pnhdrn5iusl[53];
extern const uint8_t gm_s7rl9plavbrfo[17];
extern const uint8_t gm_s7s8viq00e4g2[15];
extern const uint8_t gm_s7ti0q30v2iq7[8];
extern const uint8_t gm_s80t4sc5bf88n[82];
extern const uint8_t gm_s81trlqpvp2do[11];
extern const uint8_t gm_s83uh7m5068c7[14];
extern const uint8_t gm_s85m9c4pt0ikp[6];
extern const uint8_t gm_s872048h9ttis[26];
extern const uint8_t gm_s873vbpojuh8v[5];
extern const uint8_t gm_s87h4fof1n9ou[11];
extern const uint8_t gm_s87isqt5t284q[9];
extern const uint8_t gm_s885vckaljhqv[3];
extern const uint8_t gm_s894fmp947sfb[22];
extern const uint8_t gm_s8952afsouaih[21];
extern const uint8_t gm_s8a7jvbqoerpn[21];
extern const uint8_t gm_s8b49h6svih4r[82];
extern const uint8_t gm_s8c43u4dovbrb[17];
extern const uint8_t gm_s8e4n8lgtb0fh[41];
extern const uint8_t gm_s8kki44ucv73v[11];
extern const uint8_t gm_s8kr43nig4sns[8];
extern const uint8_t gm_s8m850dccae8f[88];
extern const uint8_t gm_s8olahjn69at6[21];
extern const uint8_t gm_s8oln6mi5b5qr[46];
extern const uint8_t gm_s8om3gihgkru8[8];
extern const uint8_t gm_s8pt0g4bme9og[30];
extern const uint8_t gm_s8q575q0mjt4k[6];
extern const uint8_t gm_s8qn6r2vquv2d[24];
extern const uint8_t gm_s8srbeac8og16[29];
extern const uint8_t gm_s8u1980tbab9f[15];
extern const uint8_t gm_s8vhc5kn5i8tv[38];
extern const uint8_t gm_s8vthjuk0el8j[63];
extern const uint8_t gm_s927f9s6klgc5[4];
extern const uint8_t gm_s966nqk7bt67q[19];
extern const uint8_t gm_s967emiec5f3a[9];
extern const uint8_t gm_s967vts9f19o3[11];
extern const uint8_t gm_s988kcbq5r7tp[14];
extern const uint8_t gm_s98a5ud3vqhvr[85];
extern const uint8_t gm_s99fe9sgbdt37[14];
extern const uint8_t gm_s9co2b6h6nhos[16];
extern const uint8_t gm_s9jmloct2mk5m[11];
extern const uint8_t gm_s9k2jkfctp1g5[72];
extern const uint8_t gm_s9kj8vgi6430a[6];
extern const uint8_t gm_s9oqmmiu8qcsh[4];
extern const uint8_t gm_s9pcjvamrhbn7[25];
extern const uint8_t gm_s9po3krkcue72[4];
extern const uint8_t gm_s9pqiujjjjsta[55];
extern const uint8_t gm_s9tdiht68h68o[4];
extern const uint8_t gm_s9tnrhv72lohp[8];
extern const uint8_t gm_s9ug0q3248l8e[44];
extern const uint8_t gm_sa132smnv97ed[4];
extern const uint8_t gm_sa16a135q4am6[13];
extern const uint8_t gm_sa23fj423hlk4[6];
extern const uint8_t gm_sa7fm83sqt8aa[16];
extern const uint8_t gm_sa7gjgqof9s56[44];
extern const uint8_t gm_sa831sfid0hf3[2];
extern const uint8_t gm_sa98jv6l3j8el[6];
extern const uint8_t gm_sa9ehjurfkhlj[17];
extern const uint8_t gm_sabcps3pq8iv7[15];
extern const uint8_t gm_sag4ku07t2k8t[25];
extern const uint8_t gm_sah6kaenul8kp[41];
extern const uint8_t gm_saiq60vdu8ckv[10];
extern const uint8_t gm_sajv8cd7k86pe[3];
extern const uint8_t gm_sakaoktvp5alh[52];
extern const uint8_t gm_sam8dbnfp1kb4[34];
extern const uint8_t gm_samqd3pmlgqmg[14];
extern const uint8_t gm_sankagtb3ecgn[58];
extern const uint8_t gm_sao4mumvadoct[4];
extern const uint8_t gm_sao9h06bhkdja[26];
extern const uint8_t gm_saturamfhig5d[45];
extern const uint8_t gm_sauf6ibu7mnba[5];
extern const uint8_t gm_saufuk3rs2i2m[15];
extern const uint8_t gm_savnf0pcaokd0[24];
extern const uint8_t gm_sb0ri502n42ab[16];
extern const uint8_t gm_sb2sr4t6stbn1[16];
extern const uint8_t gm_sb3o36rmuvj28[56];
extern const uint8_t gm_sb54ui2amic32[2];
extern const uint8_t gm_sb7hastj3mkf0[36];
extern const uint8_t gm_sb8jvk2vbau9p[25];
extern const uint8_t gm_sba0715plrn4t[22];
extern const uint8_t gm_sbabklk1lp75f[30];
extern const uint8_t gm_sbbre965bpo5f[77];
extern const uint8_t gm_sbcpd47bv0ru9[63];
extern const uint8_t gm_sbeptfoja7ub2[37];
extern const uint8_t gm_sbg03hl2obk72[10];
extern const uint8_t gm_sbg28ee7l5gjs[6];
extern const uint8_t gm_sbgdbpkf33nsc[30];
extern const uint8_t gm_sbhq27eoeitmb[24];
extern const uint8_t gm_sblodmj4vs4nd[24];
extern const uint8_t gm_sbmedt8vmt5ve[5];
extern const uint8_t gm_sbnms71r2jqr5[7];
extern const uint8_t gm_sbp061q0urkl4[4865];
extern const uint8_t gm_sbp7rkf7ng8nb[33];
extern const uint8_t gm_sbtq9nj91tefv[10];
extern const uint8_t gm_sbu7knoujlojo[56];
extern const uint8_t gm_sc103nruhmor7[3];
extern const uint8_t gm_sc149ns1jm5on[9];
extern const uint8_t gm_sc21u906vi2gp[72];
extern const uint8_t gm_sc4r4l9k6f577[3];
extern const uint8_t gm_sc5bk1hdqs61j[22];
extern const uint8_t gm_sc7bkbe71it8c[10];
extern const uint8_t gm_sc8spe93fl51n[45];
extern const uint8_t gm_sc90o4moufk96[41];
extern const uint8_t gm_scavc1uf2hs8l[7];
extern const uint8_t gm_scc8p0a9eachg[4];
extern const uint8_t gm_scch7vqldh7aj[13];
extern const uint8_t gm_scd7cqpmpn0p9[78];
extern const uint8_t gm_sci406m9tkvjp[32];
extern const uint8_t gm_scitmdqfeod1m[6];
extern const uint8_t gm_scjfd9b9vg99c[12];
extern const uint8_t gm_scongfv16i8kh[21];
extern const uint8_t gm_scrnhm4ha22vj[18];
extern const uint8_t gm_scstobchrdgiu[67];
extern const uint8_t gm_sctlci97unb16[36];
extern const uint8_t gm_scuaf7ud4smlt[123];
extern const uint8_t gm_sd1uj6gf5v8qh[8];
extern const uint8_t gm_sd2mrmucl6ngt[7];
extern const uint8_t gm_sd66vi8qjocp9[5];
extern const uint8_t gm_sd80su3vlvhjt[27];
extern const uint8_t gm_sd98uehpeed1c[11];
extern const uint8_t gm_sd9r0l17bbuoe[5];
extern const uint8_t gm_sd9ri1vmgds82[9];
extern const uint8_t gm_sdark9dtsgt1q[41];
extern const uint8_t gm_sdbo9gngsfqdj[8];
extern const uint8_t gm_sdckdu8fe6iog[7];
extern const uint8_t gm_sdes3rh8ctb7v[21];
extern const uint8_t gm_sdgcpos3mblup[37];
extern const uint8_t gm_sdib10lkj65nm[45];
extern const uint8_t gm_sdl8mgtvebfcs[3];
extern const uint8_t gm_sdpkj0pb6qtc0[33];
extern const uint8_t gm_sdpl6uk3q4cch[6];
extern const uint8_t gm_sdstli6hlsa1e[7];
extern const uint8_t gm_se1krm9tn2oaa[3];
extern const uint8_t gm_se39cci44d9rt[35];
extern const uint8_t gm_se47039ecu2b8[10];
extern const uint8_t gm_seb50ruktgoun[14];
extern const uint8_t gm_sebidnsgqg13d[45];
extern const uint8_t gm_sebojpbfjfgaf[3];
extern const uint8_t gm_seccpktgmrevt[2];
extern const uint8_t gm_secneq49c8dtf[36];
extern const uint8_t gm_seh12nehvgmhj[12];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sehokd57s045g[11];
extern const uint8_t gm_seleu2adn5qnd[18];
extern const uint8_t gm_selmorb0539e5[5];
extern const uint8_t gm_seoeuc6nik756[40];
extern const uint8_t gm_seoiboio4n733[5];
extern const uint8_t gm_ses9qtp6kdd22[8];
extern const uint8_t gm_setftr5leknrn[23];
extern const uint8_t gm_sf0p9o7g7fr6l[7];
extern const uint8_t gm_sf18e6o1dss49[6];
extern const uint8_t gm_sf46mekb18qm8[14];
extern const uint8_t gm_sf4j7f4kg765o[41];
extern const uint8_t gm_sf4qi8ut6u3p1[5];
extern const uint8_t gm_sf7ldnukv79cp[24];
extern const uint8_t gm_sfa86erpod86s[4];
extern const uint8_t gm_sfc9avb7sceba[21];
extern const uint8_t gm_sfhcr4tad3e77[85];
extern const uint8_t gm_sfltedqje18on[9];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sfn5aos9cps1h[13];
extern const uint8_t gm_sfp78bn17ei97[60];
extern const uint8_t gm_sfpg6jsb3cfp6[23];
extern const uint8_t gm_sfss9c30cdmun[8];
extern const uint8_t gm_sfvc4ovr5hl6h[23];
extern const uint8_t gm_sg2tq59po62eq[41];
extern const uint8_t gm_sg47f6qrh6jrs[32];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sg5h7btla5ksa[29];
extern const uint8_t gm_sgaj4glha8q4e[3];
extern const uint8_t gm_sgdk0atob5av0[6];
extern const uint8_t gm_sgdrhdkbtd5gr[19];
extern const uint8_t gm_sgfbh9novfe35[7];
extern const uint8_t gm_sgfmudgd82uhr[24];
extern const uint8_t gm_sgfqei77bsia8[4];
extern const uint8_t gm_sgga2ijjm9cqd[7];
extern const uint8_t gm_sgib96mf5meqv[11];
extern const uint8_t gm_sgj6i64633q0l[11];
extern const uint8_t gm_sglu5epjv38rl[92];
extern const uint8_t gm_sgmhc3klfmn46[5];
extern const uint8_t gm_sgmttpp6cmh41[14];
extern const uint8_t gm_sgqsd7lfikrak[41];
extern const uint8_t gm_sgurqv8kb2rs7[32];
extern const uint8_t gm_sh0j2ggs1vvoa[2];
extern const uint8_t gm_sh1to1i0u67lu[9];
extern const uint8_t gm_sh2urbm3ndjnn[31];
extern const uint8_t gm_sh4sp5o4s4bj1[9];
extern const uint8_t gm_sh4ufq9bak51r[13];
extern const uint8_t gm_sh5rvkuevnqlk[7];
extern const uint8_t gm_sh8l62ma11hq5[70];
extern const uint8_t gm_sha7302s2fme2[18];
extern const uint8_t gm_shakeh6dfb84i[17];
extern const uint8_t gm_shbfu7hj4uhkt[16];
extern const uint8_t gm_shbtdicfd8lcc[12];
extern const uint8_t gm_shde4bsckk9mt[4];
extern const uint8_t gm_shdies4nuf4tc[8];
extern const uint8_t gm_she92i1u1a4v5[30];
extern const uint8_t gm_shgptuodlhrcq[61];
extern const uint8_t gm_shhugap3t1rkg[41];
extern const uint8_t gm_shi7nidbc64qf[4];
extern const uint8_t gm_shkq4botdoed7[6];
extern const uint8_t gm_shlk3g0tk7m4f[31];
extern const uint8_t gm_shmdne5cle16i[8];
extern const uint8_t gm_shnmjdmsj431f[17];
extern const uint8_t gm_shnpduonp99d0[12];
extern const uint8_t gm_sho5nuaja1vvf[5];
extern const uint8_t gm_shq89iblo3j6k[51];
extern const uint8_t gm_shrtuk3jka1au[16];
extern const uint8_t gm_shsifme8hcenm[17];
extern const uint8_t gm_shtvnkmjp804q[23];
extern const uint8_t gm_shvak64vhokft[26];
extern const uint8_t gm_si0b5q45ot754[101];
extern const uint8_t gm_si0mnqg6j1eu8[19];
extern const uint8_t gm_si23lpqv3htdl[27];
extern const uint8_t gm_si6sr5gqkcui7[6];
extern const uint8_t gm_si7fe9i76cdgb[13];
extern const uint8_t gm_siamquc19uikr[2];
extern const uint8_t gm_sibmp9mck7osv[13];
extern const uint8_t gm_sicq1bvp13t5d[23];
extern const uint8_t gm_sigac1ehmei1f[69];
extern const uint8_t gm_siiqktevj2s2h[25];
extern const uint8_t gm_sij2r6jhpquej[12];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sikspprj7od7l[43];
extern const uint8_t gm_sildigrhtv45e[18];
extern const uint8_t gm_sio8t6i3tf974[11];
extern const uint8_t gm_siot2vlveunn8[18];
extern const uint8_t gm_sirg36m1q4cjm[55];
extern const uint8_t gm_sisb3n56u9bmt[15];
extern const uint8_t gm_siscc5ella8ma[4];
extern const uint8_t gm_siselrhsqskvi[6];
extern const uint8_t gm_sisp52otedii5[23];
extern const uint8_t gm_sithisfj5qgfv[67];
extern const uint8_t gm_siv3d9he09v3r[44];
extern const uint8_t gm_sivuq9c7rjrh5[16];
extern const uint8_t gm_sj2tf1ift8sga[21];
extern const uint8_t gm_sj36eemfvep3a[56];
extern const uint8_t gm_sj3ehailc2pgo[52];
extern const uint8_t gm_sj486prdcn2go[14];
extern const uint8_t gm_sj7i3vhsq2ifl[38];
extern const uint8_t gm_sjdn5gs7lu39i[57];
extern const uint8_t gm_sjh6cl9hs66k6[37];
extern const uint8_t gm_sjjc564tiep5s[13];
extern const uint8_t gm_sjk23rfp9022d[14];
extern const uint8_t gm_sjkj5g78lejrj[4];
extern const uint8_t gm_sjm54r6h90i7e[63];
extern const uint8_t gm_sjnkguo0atpm5[3];
extern const uint8_t gm_sjnv3p8t49clu[9];
extern const uint8_t gm_sjqs7q3ostqbm[4];
extern const uint8_t gm_sjrj6qdrion32[35];
extern const uint8_t gm_sju4s3ndpnsge[107];
extern const uint8_t gm_sk09uplg1v4qc[16];
extern const uint8_t gm_sk0h761p8pjoe[43];
extern const uint8_t gm_sk4u34fs8qq60[38];
extern const uint8_t gm_sk5qo3et4heam[40];
extern const uint8_t gm_sk88l68smd6kt[23];
extern const uint8_t gm_skat03ddcievj[39];
extern const uint8_t gm_skbblqa84g6pp[57];
extern const uint8_t gm_skbmmrtbskpao[8];
extern const uint8_t gm_skem4050p6ec1[13];
extern const uint8_t gm_skhcthij717g7[11];
extern const uint8_t gm_skiv4voi8bqij[20];
extern const uint8_t gm_skkqaektdhkn1[29];
extern const uint8_t gm_skl3cimaihkgu[26];
extern const uint8_t gm_skl8iadfmm1va[44];
extern const uint8_t gm_skmg81d57fq1r[31];
extern const uint8_t gm_skmsgkj4jeo7n[6];
extern const uint8_t gm_skpsvuvlfj0ep[68];
extern const uint8_t gm_skq2b15b87en9[63];
extern const uint8_t gm_skqf89fdi6p8e[12];
extern const uint8_t gm_skqko8fn9asq3[97];
extern const uint8_t gm_skr2kn3pd8es7[11];
extern const uint8_t gm_skrdl5pdcve8g[43];
extern const uint8_t gm_sktf16uersaon[9];
extern const uint8_t gm_skv01gaujln2s[41];
extern const uint8_t gm_skvqkn3gte5mk[11];
extern const uint8_t gm_sl23n0qkt3j4u[28];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sl439hggqojfk[15];
extern const uint8_t gm_sl69n28ucapfg[21];
extern const uint8_t gm_sl7p035rs001b[27];
extern const uint8_t gm_sl8of2tnl835f[6];
extern const uint8_t gm_sla366gd7nphd[58];
extern const uint8_t gm_slbqo2q5mg776[107];
extern const uint8_t gm_slc58j6a4e39a[56];
extern const uint8_t gm_slgmqgkioa4u6[3];
extern const uint8_t gm_sligbvojecf44[15];
extern const uint8_t gm_sll3452l5oa19[6];
extern const uint8_t gm_slp60pbhoot2j[15];
extern const uint8_t gm_slpo22sjs83b1[5];
extern const uint8_t gm_slq58vfha8k9h[2];
extern const uint8_t gm_slrtg04n254p4[19];
extern const uint8_t gm_sltjg0sbhbo73[13];
extern const uint8_t gm_sluiboi2c0d5n[58];
extern const uint8_t gm_slvvpj912a5bg[4];
extern const uint8_t gm_sm3pe8sptk65k[24];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_sm8m4kp0f7r62[13];
extern const uint8_t gm_sm8t3g6c72ol4[62];
extern const uint8_t gm_sma6ellkcmke6[26];
extern const uint8_t gm_smc854k364mj6[21];
extern const uint8_t gm_smdv8hgj9kr3n[21];
extern const uint8_t gm_smf5jnr5ps1bl[5];
extern const uint8_t gm_smf6b2r7npomm[41];
extern const uint8_t gm_smohsnbvmf059[10];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smptjqchq7bt0[47];
extern const uint8_t gm_smq2niiusplb0[40];
extern const uint8_t gm_smu48daue2qai[6];
extern const uint8_t gm_sn0peadng5i31[3];
extern const uint8_t gm_sn2gg3b98t635[21];
extern const uint8_t gm_sn5sjb95plsev[27];
extern const uint8_t gm_sn790bmojc3gl[33];
extern const uint8_t gm_sn7k3frnltojm[8];
extern const uint8_t gm_sn7tl9jolqoj1[3];
extern const uint8_t gm_sn8alsh9na9ne[12];
extern const uint8_t gm_sn9gq20tga4vu[35];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snf16gr70fhka[11];
extern const uint8_t gm_snh0sl2cdagdn[13];
extern const uint8_t gm_snh5ve0osdp08[7];
extern const uint8_t gm_snikjq3vkoue5[14];
extern const uint8_t gm_snivf25e5fme8[6];
extern const uint8_t gm_snja4gioias3h[19];
extern const uint8_t gm_snkldl7hr4htt[4];
extern const uint8_t gm_snmep73bl34j8[7];
extern const uint8_t gm_snmm68ee1rq48[41];
extern const uint8_t gm_sno069vrt5g9q[4];
extern const uint8_t gm_snp0oqa4m79hk[6];
extern const uint8_t gm_snps96dj7ecq0[17];
extern const uint8_t gm_snqevmbeuqcmu[10];
extern const uint8_t gm_snr5o5tak6405[4];
extern const uint8_t gm_sns1hl83hdlcn[5];
extern const uint8_t gm_sns75reqq32d2[4];
extern const uint8_t gm_snu49maifa670[41];
extern const uint8_t gm_snukl6jqep6jf[4];
extern const uint8_t gm_snvodad5t43r7[11];
extern const uint8_t gm_so2sjlodq81qn[13];
extern const uint8_t gm_so514oj5nbhcu[2];
extern const uint8_t gm_so5c2bcepn6c6[15];
extern const uint8_t gm_so5l2ibq9ia3i[65];
extern const uint8_t gm_so6e0kl9gkq21[46];
extern const uint8_t gm_so77ks9ivg559[9];
extern const uint8_t gm_so7v1cppe0bu8[41];
extern const uint8_t gm_so9fi6r28770a[3];
extern const uint8_t gm_soa9mnl4ahjud[8];
extern const uint8_t gm_soaukr1udufqk[19];
extern const uint8_t gm_sobspv56mhf83[15];
extern const uint8_t gm_sodq0gu838ini[139];
extern const uint8_t gm_sog9gpimub31i[7];
extern const uint8_t gm_soiad5r9c65ru[37];
extern const uint8_t gm_sojts6dcvra33[35];
extern const uint8_t gm_som0q53jqvjh3[5];
extern const uint8_t gm_somon4mdq0gpj[24];
extern const uint8_t gm_son0j0k0t10ro[8];
extern const uint8_t gm_soo5q2bffam33[33];
extern const uint8_t gm_soqt3jnmovq5l[4];
extern const uint8_t gm_sovv48653dnju[40];
extern const uint8_t gm_sp02qqgucmpt7[3];
extern const uint8_t gm_sp0iqkuu2lvpl[14];
extern const uint8_t gm_sp3k6emq42nh4[10];
extern const uint8_t gm_sp4acsbhsp3do[35];
extern const uint8_t gm_sp4skfj2bn2ut[7];
extern const uint8_t gm_sp51dovg85ku9[40];
extern const uint8_t gm_sp598n426d61b[40];
extern const uint8_t gm_sp7cdd0n91vli[8];
extern const uint8_t gm_sp9acfc9ddurj[10];
extern const uint8_t gm_spaa4sseck2fg[45];
extern const uint8_t gm_spdm0kqdda6co[25];
extern const uint8_t gm_spdpobhso0dbc[10];
extern const uint8_t gm_speojrsjsem8a[50];
extern const uint8_t gm_spf5krhuq011v[11];
extern const uint8_t gm_spgnoteg4vcil[11];
extern const uint8_t gm_sphl918t1t6qp[11];
extern const uint8_t gm_spj0hqsg585et[14];
extern const uint8_t gm_spkcu8lv7g8jc[5];
extern const uint8_t gm_spmgsv94458ho[59];
extern const uint8_t gm_spmkn6krbjnjh[10];
extern const uint8_t gm_spni6j7splqto[8];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_spu4ur06pm76g[17];
extern const uint8_t gm_sq05ai6ts8qlk[17];
extern const uint8_t gm_sq0h7jjc6dtk5[4];
extern const uint8_t gm_sq3jr80otu7tj[3];
extern const uint8_t gm_sq4jcg2uujqmj[16];
extern const uint8_t gm_sq7d77si6ibqp[41];
extern const uint8_t gm_sq7ohh8h77bn7[26];
extern const uint8_t gm_sq9i3vfk7nqs8[10];
extern const uint8_t gm_sqa29afvhov50[6];
extern const uint8_t gm_sqbstqdssta1a[48];
extern const uint8_t gm_sqc2or5f1575b[31];
extern const uint8_t gm_sqdfr4k3vea5p[4];
extern const uint8_t gm_sqfe8a5cd6b60[7];
extern const uint8_t gm_sqh0568fl8oao[14];
extern const uint8_t gm_sqhe7365v82gi[16];
extern const uint8_t gm_sqhqlj2oc6h5k[20];
extern const uint8_t gm_sqic2de8m2pet[61];
extern const uint8_t gm_sqj99shb29i55[11];
extern const uint8_t gm_sqkk84ckt4rm6[46];
extern const uint8_t gm_sqlk0h0kae56e[8];
extern const uint8_t gm_sqmr87iegdcns[7];
extern const uint8_t gm_sqn4031c223rk[13];
extern const uint8_t gm_sqqfe3o1gh1ab[55];
extern const uint8_t gm_sqr5tipcqpi8v[41];
extern const uint8_t gm_sqssndasqkc4l[17];
extern const uint8_t gm_sqtojn396s09d[11];
extern const uint8_t gm_squ7ob8ltdkn4[20];
extern const uint8_t gm_sqvb27hiolbh8[11];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const uint8_t gm_sr0u787bpqkqg[28];
extern const uint8_t gm_sr23o038u76nd[13];
extern const uint8_t gm_sr2ptnspelf8s[98];
extern const uint8_t gm_sr3s1oi48i6m6[16];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr5j1k0g236qh[68];
extern const uint8_t gm_sr5s632lvio16[62];
extern const uint8_t gm_sr7ga013iei9a[4];
extern const uint8_t gm_sr8dujibvqj2g[82];
extern const uint8_t gm_sr9i7j9bpmaec[11];
extern const uint8_t gm_sragmmlrm20js[3];
extern const uint8_t gm_srb0ps8mir4li[9];
extern const uint8_t gm_srbp7aar5n2sa[14];
extern const char gm_src0dtioet3p4f6[];
extern const uint8_t gm_src0qvon2jl38[28];
extern const char gm_src27ouot20tc5m[];
extern const char gm_src4hj0beu8p6ao[];
extern const char gm_src4rbevsr9gjng[];
extern const char gm_src5eu5l4a7gv6s[];
extern const char gm_src9pvgnpibl2nm[];
extern const char gm_srcb4m5qm2daehm[];
extern const char gm_srcfk7rg8t5apoh[];
extern const char gm_srck5orfpjti60c[];
extern const uint8_t gm_srcl5693p5ru5[17];
extern const char gm_srcl8pn2pjtmj6k[];
extern const char gm_srcm8m4ndqum1bp[];
extern const char gm_srcmhupam4se3h5[];
extern const char gm_srcuj9anpi7p1mq[];
extern const char gm_srcvmmksi0829pa[];
extern const uint8_t gm_srefpimq1sas7[4];
extern const uint8_t gm_sreie3avjjb9l[64];
extern const uint8_t gm_srge6muj16eos[31];
extern const uint8_t gm_srhs1fn12neea[33];
extern const uint8_t gm_srhtnnu7r6ead[20];
extern const uint8_t gm_sri9mnfjmc5ma[11];
extern const uint8_t gm_srkvn420e4huu[26];
extern const uint8_t gm_srm32inqe4h6s[10];
extern const uint8_t gm_srne5d503fcjg[5];
extern const uint8_t gm_srrkjtivok8h4[9];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_srtlfdmpif4qk[9];
extern const uint8_t gm_srv5rbhjb8ns5[15];
extern const uint8_t gm_ss41pl5vtg7un[72];
extern const uint8_t gm_ss6dev5u7tl99[12];
extern const uint8_t gm_ss7iv2kssvf80[7];
extern const uint8_t gm_ss9vj0o06n3e8[8];
extern const uint8_t gm_ssa0pabeurppq[20];
extern const uint8_t gm_ssd7gce6jed13[7];
extern const uint8_t gm_sse9dgkusbtnf[5];
extern const uint8_t gm_ssgeom7733jik[5];
extern const uint8_t gm_sshop8kf5qfhm[63];
extern const uint8_t gm_ssj1mfqvo884a[62];
extern const uint8_t gm_ssjhh9bdn23jf[17];
extern const uint8_t gm_ssr6vk8erbj18[13];
extern const uint8_t gm_sss3bmvruos1t[9];
extern const uint8_t gm_sss60if2anvc8[5];
extern const uint8_t gm_sss94v5r4qh46[13];
extern const uint8_t gm_sst2khrvr4nus[10];
extern const uint8_t gm_ssvvtpgta681j[19];
extern const uint8_t gm_st1bq6djjin2k[17];
extern const uint8_t gm_st2e24ck2icsi[26];
extern const uint8_t gm_st2ertk6tjva7[14];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_st3m5olipe6ov[17];
extern const uint8_t gm_st3mnofi7dhrs[19];
extern const uint8_t gm_st4gkaq24fvj9[32];
extern const uint8_t gm_st547kc8a6rg5[16];
extern const uint8_t gm_st5bsv3b3beg9[22];
extern const uint8_t gm_st5ngsmk07040[8];
extern const uint8_t gm_st78509mboaa4[22];
extern const uint8_t gm_st943craifob3[31];
extern const uint8_t gm_statjrbfbnhar[11];
extern const uint8_t gm_stbjrsoqc456r[12];
extern const uint8_t gm_stddp43pjd9r1[51];
extern const uint8_t gm_stm36rdb2vbg4[13];
extern const uint8_t gm_stm8v8ngvll3q[16];
extern const uint8_t gm_stmsadvf54k73[3];
extern const uint8_t gm_stobu0djed6ck[10];
extern const uint8_t gm_stou2bl2ross2[70];
extern const uint8_t gm_stpboephdcpba[14];
extern const uint8_t gm_stqvtvp1hc371[12];
extern const uint8_t gm_strcbt1c3upss[4];
extern const uint8_t gm_stsvc1ssgs83i[62];
extern const uint8_t gm_sttasaa2bp05i[22];
extern const uint8_t gm_su37g0462a9t0[41];
extern const uint8_t gm_su50gais1htu0[38];
extern const uint8_t gm_su5bsj1nioctr[22];
extern const uint8_t gm_su66bdcms0hot[28];
extern const uint8_t gm_su6ol10q7la3d[7];
extern const uint8_t gm_su7tg2occn1k0[15];
extern const uint8_t gm_su8mkvs9pi0v1[18];
extern const uint8_t gm_su9fl4d8e88sc[49];
extern const uint8_t gm_subcm6amchfvv[5];
extern const uint8_t gm_sucip17pohakb[22];
extern const uint8_t gm_sucpkqgliu39h[41];
extern const uint8_t gm_sueo2nge7h013[14];
extern const uint8_t gm_sujipgkhb2fih[8];
extern const uint8_t gm_sujs43rrijg57[102];
extern const uint8_t gm_sukriak12588g[5];
extern const uint8_t gm_sunidvof1bsgr[33];
extern const uint8_t gm_sup5ecv5jiqbn[119];
extern const uint8_t gm_sup9bfr0np428[9];
extern const uint8_t gm_surt6bemh510v[31];
extern const uint8_t gm_sv150rl18b4qa[4];
extern const uint8_t gm_sv3gqbo7rudsj[13];
extern const uint8_t gm_sv732pptj1lih[10];
extern const uint8_t gm_svaaloo7n72id[9];
extern const uint8_t gm_svb4l7qho0a9o[16];
extern const uint8_t gm_svbftsm94r0bv[7];
extern const uint8_t gm_svd9icup57bmm[27];
extern const uint8_t gm_svhavo13qha5k[4];
extern const uint8_t gm_svirf1u8t89n0[47];
extern const uint8_t gm_svk1fq3llqdta[10];
extern const uint8_t gm_svl5ddjf64ul7[18];
extern const uint8_t gm_svlbiokurll04[12];
extern const uint8_t gm_svmtk9tcvmupq[1051];
extern const uint8_t gm_svnpflkpvd513[5];
extern const uint8_t gm_svr80l31985v4[7];
extern const uint8_t gm_svsf0pu3t8u6m[7];
extern const uint8_t gm_svth7kg659g2i[13];
extern const uint8_t gm_svutc5hf6cgiv[2];
extern const uint8_t gm_svv3t2si5df2q[15];
extern const gm_tmpl_piece gm_tpl005eoei82778_p[];
extern const gm_spec *const gm_tpl005eoei82778_s[];
extern const gm_spec *const gm_tpl00b7kgp37ng7_s[];
extern const gm_tmpl gm_tpl00eps1l57c5a;
extern const uint32_t gm_tpl00eps1l57c5a_i[];
extern const gm_tmpl_piece gm_tpl00eps1l57c5a_p[];
extern const gm_spec *const gm_tpl00eps1l57c5a_s[];
extern const uint32_t gm_tpl07mnbdjjl9l9_i[];
extern const gm_tmpl_piece gm_tpl083ga8282tol_p[];
extern const uint32_t gm_tpl086m1ajg247c_i[];
extern const uint32_t gm_tpl0gp8c85b2lf5_i[];
extern const gm_tmpl_piece gm_tpl0h84r2u2heg0_p[];
extern const gm_tmpl gm_tpl0j5i33et5u62;
extern const uint32_t gm_tpl0j5i33et5u62_i[];
extern const gm_tmpl_piece gm_tpl0j5i33et5u62_p[];
extern const gm_spec *const gm_tpl0j5i33et5u62_s[];
extern const gm_tmpl gm_tpl0rhldcdavmcg;
extern const uint32_t gm_tpl0rhldcdavmcg_i[];
extern const gm_tmpl_piece gm_tpl0rhldcdavmcg_p[];
extern const gm_spec *const gm_tpl0rhldcdavmcg_s[];
extern const gm_spec *const gm_tpl0scna75sco5f_s[];
extern const gm_tmpl gm_tpl0tljhf3n7k2p;
extern const uint32_t gm_tpl0tljhf3n7k2p_i[];
extern const gm_tmpl_piece gm_tpl0tljhf3n7k2p_p[];
extern const gm_spec *const gm_tpl0tljhf3n7k2p_s[];
extern const gm_spec *const gm_tpl0vock46lp3ec_s[];
extern const gm_tmpl_piece gm_tpl149k5asebpko_p[];
extern const gm_tmpl gm_tpl150n44984e0j;
extern const uint32_t gm_tpl150n44984e0j_i[];
extern const gm_tmpl_piece gm_tpl150n44984e0j_p[];
extern const gm_spec *const gm_tpl150n44984e0j_s[];
extern const uint32_t gm_tpl16elqfq3d1nr_i[];
extern const gm_spec *const gm_tpl16petnrgdcd0_s[];
extern const gm_tmpl gm_tpl17t0pmu1kh1u;
extern const uint32_t gm_tpl17t0pmu1kh1u_i[];
extern const gm_tmpl_piece gm_tpl17t0pmu1kh1u_p[];
extern const gm_spec *const gm_tpl17t0pmu1kh1u_s[];
extern const gm_tmpl gm_tpl1avjsgn54rqb;
extern const uint32_t gm_tpl1avjsgn54rqb_i[];
extern const gm_tmpl_piece gm_tpl1avjsgn54rqb_p[];
extern const gm_spec *const gm_tpl1avjsgn54rqb_s[];
extern const uint32_t gm_tpl1c0b1s109fa4_i[];
extern const gm_tmpl gm_tpl1c171lb88cdd;
extern const uint32_t gm_tpl1c171lb88cdd_i[];
extern const gm_tmpl_piece gm_tpl1c171lb88cdd_p[];
extern const gm_spec *const gm_tpl1c171lb88cdd_s[];
extern const gm_spec *const gm_tpl1dleoebfmvhp_s[];
extern const uint32_t gm_tpl1f7c1o805fpf_i[];
extern const gm_tmpl_piece gm_tpl1f7c1o805fpf_p[];
extern const gm_tmpl_piece gm_tpl1gtefnja08ps_p[];
extern const gm_tmpl_piece gm_tpl1jgrbdjt2sm3_p[];
extern const gm_tmpl_piece gm_tpl1o2hk14gjjj0_p[];
extern const gm_tmpl gm_tpl1oodsdaf08oj;
extern const uint32_t gm_tpl1oodsdaf08oj_i[];
extern const gm_tmpl_piece gm_tpl1oodsdaf08oj_p[];
extern const gm_spec *const gm_tpl1oodsdaf08oj_s[];
extern const gm_tmpl_piece gm_tpl1u3j3qlmtjj2_p[];
extern const gm_tmpl gm_tpl1uf3f46uq3oj;
extern const uint32_t gm_tpl1uf3f46uq3oj_i[];
extern const gm_tmpl_piece gm_tpl1uf3f46uq3oj_p[];
extern const gm_spec *const gm_tpl1uf3f46uq3oj_s[];
extern const gm_spec *const gm_tpl233lm3m83499_s[];
extern const uint32_t gm_tpl29spsi1atllj_i[];
extern const gm_tmpl gm_tpl2aca96qot4p8;
extern const uint32_t gm_tpl2aca96qot4p8_i[];
extern const gm_tmpl_piece gm_tpl2aca96qot4p8_p[];
extern const gm_spec *const gm_tpl2aca96qot4p8_s[];
extern const gm_tmpl gm_tpl2d7ij9b2sa2u;
extern const uint32_t gm_tpl2d7ij9b2sa2u_i[];
extern const gm_tmpl_piece gm_tpl2d7ij9b2sa2u_p[];
extern const gm_spec *const gm_tpl2d7ij9b2sa2u_s[];
extern const gm_spec *const gm_tpl2egb012obd94_s[];
extern const uint32_t gm_tpl2kgmgv2ueqo4_i[];
extern const uint32_t gm_tpl2lqr5lnbpsvm_i[];
extern const gm_spec *const gm_tpl2n8jqp2rhn52_s[];
extern const gm_tmpl_piece gm_tpl2qal2i0dmdum_p[];
extern const gm_spec *const gm_tpl2re1r0fre48m_s[];
extern const gm_tmpl_piece gm_tpl31lgghh3mal3_p[];
extern const uint32_t gm_tpl328oas8hh4jg_i[];
extern const gm_tmpl gm_tpl33em4maossan;
extern const uint32_t gm_tpl33em4maossan_i[];
extern const gm_tmpl_piece gm_tpl33em4maossan_p[];
extern const gm_spec *const gm_tpl33em4maossan_s[];
extern const uint32_t gm_tpl355ags9581ob_i[];
extern const gm_tmpl gm_tpl3cldid2v21cr;
extern const uint32_t gm_tpl3cldid2v21cr_i[];
extern const gm_tmpl_piece gm_tpl3cldid2v21cr_p[];
extern const gm_spec *const gm_tpl3cldid2v21cr_s[];
extern const gm_tmpl_piece gm_tpl3melp777ubs8_p[];
extern const uint32_t gm_tpl3t5edr01qp5c_i[];
extern const gm_spec *const gm_tpl3t5edr01qp5c_s[];
extern const gm_tmpl gm_tpl3tkekujl97kv;
extern const uint32_t gm_tpl3tkekujl97kv_i[];
extern const gm_tmpl_piece gm_tpl3tkekujl97kv_p[];
extern const gm_spec *const gm_tpl3tkekujl97kv_s[];
extern const gm_tmpl_piece gm_tpl3ttr7njhqa54_p[];
extern const uint32_t gm_tpl409di1gnsfit_i[];
extern const uint32_t gm_tpl44cjgoji4tlu_i[];
extern const gm_tmpl_piece gm_tpl44cjgoji4tlu_p[];
extern const gm_spec *const gm_tpl4524ekle00ud_s[];
extern const gm_tmpl gm_tpl470f2c4pe1su;
extern const uint32_t gm_tpl470f2c4pe1su_i[];
extern const gm_tmpl_piece gm_tpl470f2c4pe1su_p[];
extern const gm_spec *const gm_tpl470f2c4pe1su_s[];
extern const gm_spec *const gm_tpl48h8br68c917_s[];
extern const uint32_t gm_tpl499l2fj2qs8e_i[];
extern const uint32_t gm_tpl4bpbmadpmu1i_i[];
extern const gm_spec *const gm_tpl4c2mfb3cngak_s[];
extern const gm_spec *const gm_tpl4dkmnviaa508_s[];
extern const gm_spec *const gm_tpl4fm7lnhj31fn_s[];
extern const gm_spec *const gm_tpl4g6bpqlb10dt_s[];
extern const gm_tmpl_piece gm_tpl4mli0f9rjihf_p[];
extern const gm_spec *const gm_tpl4r3nb3bvll23_s[];
extern const uint32_t gm_tpl4tk9leh8nue4_i[];
extern const gm_tmpl gm_tpl54tnh29jjfju;
extern const uint32_t gm_tpl54tnh29jjfju_i[];
extern const gm_tmpl_piece gm_tpl54tnh29jjfju_p[];
extern const gm_spec *const gm_tpl54tnh29jjfju_s[];
extern const gm_spec *const gm_tpl5a8og2hcltt3_s[];
extern const gm_tmpl_piece gm_tpl5cnv021ebod7_p[];
extern const gm_spec *const gm_tpl5cnv021ebod7_s[];
extern const gm_tmpl gm_tpl5hraomijgptv;
extern const uint32_t gm_tpl5hraomijgptv_i[];
extern const gm_tmpl_piece gm_tpl5hraomijgptv_p[];
extern const gm_spec *const gm_tpl5hraomijgptv_s[];
extern const gm_tmpl_piece gm_tpl5i8vi7ph4hfi_p[];
extern const gm_tmpl_piece gm_tpl5jb9rn2u95g6_p[];
extern const gm_tmpl gm_tpl5oph1bqhjo1e;
extern const uint32_t gm_tpl5oph1bqhjo1e_i[];
extern const gm_tmpl_piece gm_tpl5oph1bqhjo1e_p[];
extern const gm_spec *const gm_tpl5oph1bqhjo1e_s[];
extern const uint32_t gm_tpl5q9oi9aqgs43_i[];
extern const gm_spec *const gm_tpl5r25bpo8hobt_s[];
extern const uint32_t gm_tpl5utcgga3g5km_i[];
extern const gm_tmpl_piece gm_tpl5vbp9fc0mpc1_p[];
extern const gm_tmpl gm_tpl642cf7k81lop;
extern const uint32_t gm_tpl642cf7k81lop_i[];
extern const gm_tmpl_piece gm_tpl642cf7k81lop_p[];
extern const gm_spec *const gm_tpl642cf7k81lop_s[];
extern const uint32_t gm_tpl64qrh372lslh_i[];
extern const gm_tmpl gm_tpl65nnuqtjd24o;
extern const uint32_t gm_tpl65nnuqtjd24o_i[];
extern const gm_tmpl_piece gm_tpl65nnuqtjd24o_p[];
extern const gm_spec *const gm_tpl65nnuqtjd24o_s[];
extern const uint32_t gm_tpl6boglt2rkmh4_i[];
extern const gm_tmpl_piece gm_tpl6fm59pne5ng7_p[];
extern const gm_spec *const gm_tpl6fm59pne5ng7_s[];
extern const gm_tmpl_piece gm_tpl6jo4mmkdq2l1_p[];
extern const gm_tmpl gm_tpl6md5hu7rmp82;
extern const uint32_t gm_tpl6md5hu7rmp82_i[];
extern const gm_tmpl_piece gm_tpl6md5hu7rmp82_p[];
extern const gm_spec *const gm_tpl6md5hu7rmp82_s[];
extern const gm_tmpl_piece gm_tpl6q4b2p6tude6_p[];
extern const gm_tmpl_piece gm_tpl6vfpiq020ehi_p[];
extern const gm_spec *const gm_tpl77hog51tnj6o_s[];
extern const gm_tmpl_piece gm_tpl7b1ibjg22p4i_p[];
extern const gm_tmpl gm_tpl7bbfeces83t7;
extern const uint32_t gm_tpl7bbfeces83t7_i[];
extern const gm_tmpl_piece gm_tpl7bbfeces83t7_p[];
extern const gm_spec *const gm_tpl7bbfeces83t7_s[];
extern const gm_tmpl gm_tpl7hvv6i3t3o0k;
extern const uint32_t gm_tpl7hvv6i3t3o0k_i[];
extern const gm_tmpl_piece gm_tpl7hvv6i3t3o0k_p[];
extern const gm_spec *const gm_tpl7hvv6i3t3o0k_s[];
extern const gm_tmpl gm_tpl7jau285qbbrq;
extern const uint32_t gm_tpl7jau285qbbrq_i[];
extern const gm_tmpl_piece gm_tpl7jau285qbbrq_p[];
extern const gm_spec *const gm_tpl7jau285qbbrq_s[];
extern const gm_tmpl gm_tpl7kdhh1egc9gc;
extern const uint32_t gm_tpl7kdhh1egc9gc_i[];
extern const gm_tmpl_piece gm_tpl7kdhh1egc9gc_p[];
extern const gm_spec *const gm_tpl7kdhh1egc9gc_s[];
extern const gm_spec *const gm_tpl7qfg883s3b97_s[];
extern const gm_tmpl gm_tpl7ufg84gs1lc7;
extern const uint32_t gm_tpl7ufg84gs1lc7_i[];
extern const gm_tmpl_piece gm_tpl7ufg84gs1lc7_p[];
extern const gm_spec *const gm_tpl7ufg84gs1lc7_s[];
extern const gm_spec *const gm_tpl7ut0r685hcaa_s[];
extern const gm_tmpl_piece gm_tpl80avf3gokjno_p[];
extern const gm_tmpl gm_tpl82nt5tav1er7;
extern const uint32_t gm_tpl82nt5tav1er7_i[];
extern const gm_tmpl_piece gm_tpl82nt5tav1er7_p[];
extern const gm_spec *const gm_tpl82nt5tav1er7_s[];
extern const gm_spec *const gm_tpl84r77qupuujj_s[];
extern const gm_spec *const gm_tpl85p2q6ermgga_s[];
extern const gm_tmpl_piece gm_tpl86cip41cbf58_p[];
extern const uint32_t gm_tpl86i0s811rrlp_i[];
extern const gm_tmpl gm_tpl86pqqgq5ch0o;
extern const uint32_t gm_tpl86pqqgq5ch0o_i[];
extern const gm_tmpl_piece gm_tpl86pqqgq5ch0o_p[];
extern const gm_spec *const gm_tpl86pqqgq5ch0o_s[];
extern const gm_tmpl gm_tpl88ij4e6nvk4u;
extern const uint32_t gm_tpl88ij4e6nvk4u_i[];
extern const gm_tmpl_piece gm_tpl88ij4e6nvk4u_p[];
extern const gm_spec *const gm_tpl88ij4e6nvk4u_s[];
extern const gm_tmpl gm_tpl896mu4np77il;
extern const uint32_t gm_tpl896mu4np77il_i[];
extern const gm_tmpl_piece gm_tpl896mu4np77il_p[];
extern const gm_spec *const gm_tpl896mu4np77il_s[];
extern const gm_tmpl gm_tpl8bar0du85qu4;
extern const uint32_t gm_tpl8bar0du85qu4_i[];
extern const gm_tmpl_piece gm_tpl8bar0du85qu4_p[];
extern const gm_spec *const gm_tpl8bar0du85qu4_s[];
extern const gm_tmpl gm_tpl8c0nr5c8mas7;
extern const uint32_t gm_tpl8c0nr5c8mas7_i[];
extern const gm_tmpl_piece gm_tpl8c0nr5c8mas7_p[];
extern const gm_spec *const gm_tpl8c0nr5c8mas7_s[];
extern const gm_tmpl gm_tpl8eucsm7tjmm0;
extern const uint32_t gm_tpl8eucsm7tjmm0_i[];
extern const gm_tmpl_piece gm_tpl8eucsm7tjmm0_p[];
extern const gm_spec *const gm_tpl8eucsm7tjmm0_s[];
extern const gm_tmpl gm_tpl8in37qe3omcj;
extern const uint32_t gm_tpl8in37qe3omcj_i[];
extern const gm_tmpl_piece gm_tpl8in37qe3omcj_p[];
extern const gm_spec *const gm_tpl8in37qe3omcj_s[];
extern const gm_spec *const gm_tpl8pvdc7qbgge6_s[];
extern const gm_spec *const gm_tpl8s8bnem30e1s_s[];
extern const gm_tmpl_piece gm_tpl91b7sjgnmine_p[];
extern const gm_spec *const gm_tpl94ei7rae9si2_s[];
extern const uint32_t gm_tpl97n87h3pcva1_i[];
extern const uint32_t gm_tpl9900stiqclaj_i[];
extern const uint32_t gm_tpl9bim66fl3b37_i[];
extern const gm_tmpl_piece gm_tpl9d1siq0c9pir_p[];
extern const uint32_t gm_tpl9e18q8to9md2_i[];
extern const gm_tmpl_piece gm_tpl9s09hqjpb581_p[];
extern const gm_spec *const gm_tpl9uslg4i3oo2s_s[];
extern const gm_tmpl_piece gm_tpl9va1ubk8bean_p[];
extern const gm_tmpl gm_tpla0ldfomuin4r;
extern const uint32_t gm_tpla0ldfomuin4r_i[];
extern const gm_tmpl_piece gm_tpla0ldfomuin4r_p[];
extern const gm_spec *const gm_tpla0ldfomuin4r_s[];
extern const gm_spec *const gm_tpla1i8rr6vfju7_s[];
extern const gm_spec *const gm_tpla843kfhqtvit_s[];
extern const uint32_t gm_tpla8nvlakostko_i[];
extern const gm_spec *const gm_tplablvqji1bpu7_s[];
extern const uint32_t gm_tplacb81m1n6g9e_i[];
extern const uint32_t gm_tplad75mid9f0qe_i[];
extern const gm_tmpl gm_tplagt25ql42kpb;
extern const uint32_t gm_tplagt25ql42kpb_i[];
extern const gm_tmpl_piece gm_tplagt25ql42kpb_p[];
extern const gm_spec *const gm_tplagt25ql42kpb_s[];
extern const gm_spec *const gm_tplak7vr9q0b05e_s[];
extern const gm_tmpl gm_tplao1juqpqjfv9;
extern const uint32_t gm_tplao1juqpqjfv9_i[];
extern const gm_tmpl_piece gm_tplao1juqpqjfv9_p[];
extern const gm_spec *const gm_tplao1juqpqjfv9_s[];
extern const gm_tmpl_piece gm_tplaoj5r4tauejk_p[];
extern const uint32_t gm_tplav8bdvvt8rem_i[];
extern const gm_tmpl_piece gm_tplav8bdvvt8rem_p[];
extern const uint32_t gm_tplb05kqqtmbu7v_i[];
extern const uint32_t gm_tplb86aokjj8d2b_i[];
extern const gm_tmpl gm_tplbdrnrpalngql;
extern const uint32_t gm_tplbdrnrpalngql_i[];
extern const gm_tmpl_piece gm_tplbdrnrpalngql_p[];
extern const gm_spec *const gm_tplbdrnrpalngql_s[];
extern const gm_tmpl gm_tplbgmktlahcqrn;
extern const uint32_t gm_tplbgmktlahcqrn_i[];
extern const gm_tmpl_piece gm_tplbgmktlahcqrn_p[];
extern const gm_spec *const gm_tplbgmktlahcqrn_s[];
extern const gm_spec *const gm_tplbh7305mc9stu_s[];
extern const gm_tmpl gm_tplbk128im3qlnl;
extern const uint32_t gm_tplbk128im3qlnl_i[];
extern const gm_tmpl_piece gm_tplbk128im3qlnl_p[];
extern const gm_spec *const gm_tplbk128im3qlnl_s[];
extern const gm_tmpl_piece gm_tplbmf4neusqbo5_p[];
extern const gm_tmpl_piece gm_tplbnt69qpq91i8_p[];
extern const uint32_t gm_tplbog2sc0rf3ot_i[];
extern const gm_tmpl gm_tplbr0fi0201tmv;
extern const uint32_t gm_tplbr0fi0201tmv_i[];
extern const gm_tmpl_piece gm_tplbr0fi0201tmv_p[];
extern const gm_spec *const gm_tplbr0fi0201tmv_s[];
extern const gm_tmpl gm_tplc0d2186db2jt;
extern const uint32_t gm_tplc0d2186db2jt_i[];
extern const gm_tmpl_piece gm_tplc0d2186db2jt_p[];
extern const gm_spec *const gm_tplc0d2186db2jt_s[];
extern const gm_tmpl_piece gm_tplc0jhs849b438_p[];
extern const gm_tmpl gm_tplc3kt858jk6jp;
extern const uint32_t gm_tplc3kt858jk6jp_i[];
extern const gm_tmpl_piece gm_tplc3kt858jk6jp_p[];
extern const gm_spec *const gm_tplc3kt858jk6jp_s[];
extern const gm_tmpl_piece gm_tplc5cre0uqposd_p[];
extern const gm_tmpl_piece gm_tplc6hhdd7vcmvk_p[];
extern const gm_spec *const gm_tplc9nrcb798jhl_s[];
extern const gm_tmpl_piece gm_tplc9tn3pkjdpf4_p[];
extern const uint32_t gm_tplceemmmm7iuq6_i[];
extern const uint32_t gm_tplck20jih4okbk_i[];
extern const uint32_t gm_tplcpp8u5i709mj_i[];
extern const uint32_t gm_tplcpt7hf5iscqd_i[];
extern const gm_spec *const gm_tplcq107msa8ktd_s[];
extern const gm_tmpl gm_tplcsb5vnabjejk;
extern const uint32_t gm_tplcsb5vnabjejk_i[];
extern const gm_tmpl_piece gm_tplcsb5vnabjejk_p[];
extern const gm_spec *const gm_tplcsb5vnabjejk_s[];
extern const gm_tmpl gm_tplcssa18c20t1h;
extern const uint32_t gm_tplcssa18c20t1h_i[];
extern const gm_tmpl_piece gm_tplcssa18c20t1h_p[];
extern const gm_spec *const gm_tplcssa18c20t1h_s[];
extern const gm_tmpl_piece gm_tplctspgtksm35f_p[];
extern const gm_spec *const gm_tplcttkqubjcaqf_s[];
extern const gm_tmpl_piece gm_tplcudcaj1iluqd_p[];
extern const uint32_t gm_tpldftt3jdrahlt_i[];
extern const uint32_t gm_tpldhmlf1n8qcdi_i[];
extern const gm_spec *const gm_tpldhmlf1n8qcdi_s[];
extern const gm_spec *const gm_tpldiv6dh0or1km_s[];
extern const gm_spec *const gm_tpldj14c3jgvlnb_s[];
extern const gm_spec *const gm_tpldknbfev12jrb_s[];
extern const gm_tmpl_piece gm_tpldofkl5m8hqug_p[];
extern const gm_spec *const gm_tpldp3bk13rlc44_s[];
extern const gm_spec *const gm_tpldpi521pvp06p_s[];
extern const uint32_t gm_tpldrlq7uo0cbrm_i[];
extern const gm_spec *const gm_tpldsg332pn89g6_s[];
extern const gm_tmpl gm_tpldtecn6n5hb86;
extern const uint32_t gm_tpldtecn6n5hb86_i[];
extern const gm_tmpl_piece gm_tpldtecn6n5hb86_p[];
extern const gm_spec *const gm_tpldtecn6n5hb86_s[];
extern const uint32_t gm_tpldtv4ooqtkvgm_i[];
extern const gm_tmpl_piece gm_tpldvbj2judb6g5_p[];
extern const gm_spec *const gm_tple0332mlih7si_s[];
extern const gm_tmpl gm_tple2rp5rj93mkj;
extern const uint32_t gm_tple2rp5rj93mkj_i[];
extern const gm_tmpl_piece gm_tple2rp5rj93mkj_p[];
extern const gm_spec *const gm_tple2rp5rj93mkj_s[];
extern const uint32_t gm_tple6e96jcr39fk_i[];
extern const uint32_t gm_tple6jpca094i2j_i[];
extern const gm_tmpl_piece gm_tple7msnncsq0hr_p[];
extern const gm_tmpl_piece gm_tpleb7ph7fjnlbm_p[];
extern const gm_spec *const gm_tplebqthjijvqt0_s[];
extern const gm_tmpl gm_tpleeuqgcr11ngn;
extern const uint32_t gm_tpleeuqgcr11ngn_i[];
extern const gm_tmpl_piece gm_tpleeuqgcr11ngn_p[];
extern const gm_spec *const gm_tpleeuqgcr11ngn_s[];
extern const gm_tmpl gm_tpleie0q8nvjqch;
extern const uint32_t gm_tpleie0q8nvjqch_i[];
extern const gm_tmpl_piece gm_tpleie0q8nvjqch_p[];
extern const gm_spec *const gm_tpleie0q8nvjqch_s[];
extern const uint32_t gm_tpleiobdo4rm4hs_i[];
extern const gm_spec *const gm_tpleiobdo4rm4hs_s[];
extern const uint32_t gm_tpleiompgmn2730_i[];
extern const gm_tmpl gm_tplem02qig8laoi;
extern const uint32_t gm_tplem02qig8laoi_i[];
extern const gm_tmpl_piece gm_tplem02qig8laoi_p[];
extern const gm_spec *const gm_tplem02qig8laoi_s[];
extern const gm_tmpl gm_tplemb9urr8jmq6;
extern const uint32_t gm_tplemb9urr8jmq6_i[];
extern const gm_tmpl_piece gm_tplemb9urr8jmq6_p[];
extern const gm_spec *const gm_tplemb9urr8jmq6_s[];
extern const gm_tmpl gm_tplenatqap1hjoj;
extern const uint32_t gm_tplenatqap1hjoj_i[];
extern const gm_tmpl_piece gm_tplenatqap1hjoj_p[];
extern const gm_spec *const gm_tplenatqap1hjoj_s[];
extern const uint32_t gm_tpleo7934k1qvrc_i[];
extern const gm_tmpl_piece gm_tplesi2enviniss_p[];
extern const gm_spec *const gm_tplet6obcbkjbht_s[];
extern const gm_spec *const gm_tpleth0e0pd50mr_s[];
extern const gm_spec *const gm_tplett0du8rhujl_s[];
extern const gm_spec *const gm_tpleugrd3m4ci89_s[];
extern const uint32_t gm_tplf2ivserdf4ic_i[];
extern const uint32_t gm_tplf3c2iulsht2e_i[];
extern const gm_spec *const gm_tplf4oacobin5qk_s[];
extern const gm_tmpl_piece gm_tplf9q60in6m019_p[];
extern const gm_tmpl gm_tplfb67h0nat3pe;
extern const uint32_t gm_tplfb67h0nat3pe_i[];
extern const gm_tmpl_piece gm_tplfb67h0nat3pe_p[];
extern const gm_spec *const gm_tplfb67h0nat3pe_s[];
extern const uint32_t gm_tplfe53mjoakni4_i[];
extern const uint32_t gm_tplff85tslj1ph5_i[];
extern const uint32_t gm_tplfjoq6lkhl94q_i[];
extern const gm_tmpl gm_tplfjua3t6jsoe6;
extern const uint32_t gm_tplfjua3t6jsoe6_i[];
extern const gm_tmpl_piece gm_tplfjua3t6jsoe6_p[];
extern const gm_spec *const gm_tplfjua3t6jsoe6_s[];
extern const gm_spec *const gm_tplfm1jmgsrn2ua_s[];
extern const gm_tmpl gm_tplfm527r729efa;
extern const uint32_t gm_tplfm527r729efa_i[];
extern const gm_tmpl_piece gm_tplfm527r729efa_p[];
extern const gm_spec *const gm_tplfm527r729efa_s[];
extern const uint32_t gm_tplfr9bfp6t7vb7_i[];
extern const gm_tmpl gm_tplfsj0kbueolvv;
extern const uint32_t gm_tplfsj0kbueolvv_i[];
extern const gm_tmpl_piece gm_tplfsj0kbueolvv_p[];
extern const gm_spec *const gm_tplfsj0kbueolvv_s[];
extern const gm_tmpl gm_tplfsk62pc25cdb;
extern const uint32_t gm_tplfsk62pc25cdb_i[];
extern const gm_tmpl_piece gm_tplfsk62pc25cdb_p[];
extern const gm_spec *const gm_tplfsk62pc25cdb_s[];
extern const gm_spec *const gm_tplfsmasr3tgalo_s[];
extern const uint32_t gm_tplfsrd1mgov7nd_i[];
extern const gm_spec *const gm_tplfsrd1mgov7nd_s[];
extern const gm_tmpl_piece gm_tplfsstr25ojml5_p[];
extern const gm_tmpl_piece gm_tplftchh41b473r_p[];
extern const uint32_t gm_tplfv85d3oogq60_i[];
extern const gm_tmpl_piece gm_tplfv85d3oogq60_p[];
extern const gm_spec *const gm_tplfvv1qe6os0le_s[];
extern const uint32_t gm_tplg05c24rh8peq_i[];
extern const gm_spec *const gm_tplg24lfmv0pffq_s[];
extern const uint32_t gm_tplg3qra7vq6h3a_i[];
extern const gm_spec *const gm_tplg91jq41nbbkt_s[];
extern const gm_spec *const gm_tplgclcdp539fpt_s[];
extern const gm_tmpl gm_tplgeqcgpvi6n7p;
extern const uint32_t gm_tplgeqcgpvi6n7p_i[];
extern const gm_tmpl_piece gm_tplgeqcgpvi6n7p_p[];
extern const gm_spec *const gm_tplgeqcgpvi6n7p_s[];
extern const gm_spec *const gm_tplgih8dfq5hun7_s[];
extern const gm_spec *const gm_tplgm9vv1vtdqi9_s[];
extern const gm_tmpl gm_tplgniq1bprrgpo;
extern const uint32_t gm_tplgniq1bprrgpo_i[];
extern const gm_tmpl_piece gm_tplgniq1bprrgpo_p[];
extern const gm_spec *const gm_tplgniq1bprrgpo_s[];
extern const gm_tmpl gm_tplgnp9u7qeb3h6;
extern const uint32_t gm_tplgnp9u7qeb3h6_i[];
extern const gm_tmpl_piece gm_tplgnp9u7qeb3h6_p[];
extern const gm_spec *const gm_tplgnp9u7qeb3h6_s[];
extern const uint32_t gm_tplgqr018o660ue_i[];
extern const gm_tmpl_piece gm_tplgqr018o660ue_p[];
extern const uint32_t gm_tplgt6vkfjdh8cd_i[];
extern const gm_spec *const gm_tplgt8u7d5aku5b_s[];
extern const gm_spec *const gm_tplh2em7r1dmffm_s[];
extern const gm_spec *const gm_tplh7ffbsfnitkh_s[];
extern const gm_tmpl gm_tplh8soes50a6ps;
extern const uint32_t gm_tplh8soes50a6ps_i[];
extern const gm_tmpl_piece gm_tplh8soes50a6ps_p[];
extern const gm_spec *const gm_tplh8soes50a6ps_s[];
extern const gm_spec *const gm_tplhkjcpttbsqt0_s[];
extern const gm_tmpl_piece gm_tplhksbien67lqe_p[];
extern const gm_tmpl gm_tplhon5g1o0vuqi;
extern const uint32_t gm_tplhon5g1o0vuqi_i[];
extern const gm_tmpl_piece gm_tplhon5g1o0vuqi_p[];
extern const gm_spec *const gm_tplhon5g1o0vuqi_s[];
extern const gm_tmpl gm_tplhp4k6h527j3k;
extern const uint32_t gm_tplhp4k6h527j3k_i[];
extern const gm_tmpl_piece gm_tplhp4k6h527j3k_p[];
extern const gm_spec *const gm_tplhp4k6h527j3k_s[];
extern const gm_tmpl_piece gm_tplhphg08jcb4qr_p[];
extern const uint32_t gm_tplhrko1gs9n6tk_i[];
extern const gm_tmpl gm_tplhrqv5p3pfbh5;
extern const uint32_t gm_tplhrqv5p3pfbh5_i[];
extern const gm_tmpl_piece gm_tplhrqv5p3pfbh5_p[];
extern const gm_spec *const gm_tplhrqv5p3pfbh5_s[];
extern const gm_tmpl gm_tplhu5cefj26aau;
extern const uint32_t gm_tplhu5cefj26aau_i[];
extern const gm_tmpl_piece gm_tplhu5cefj26aau_p[];
extern const gm_spec *const gm_tplhu5cefj26aau_s[];
extern const gm_tmpl_piece gm_tpli0tmonaamd45_p[];
extern const gm_tmpl gm_tpli2qk88n6tll4;
extern const uint32_t gm_tpli2qk88n6tll4_i[];
extern const gm_tmpl_piece gm_tpli2qk88n6tll4_p[];
extern const gm_spec *const gm_tpli2qk88n6tll4_s[];
extern const uint32_t gm_tpli824iv2hf4o6_i[];
extern const gm_tmpl gm_tplidajav2hoc6k;
extern const uint32_t gm_tplidajav2hoc6k_i[];
extern const gm_tmpl_piece gm_tplidajav2hoc6k_p[];
extern const gm_spec *const gm_tplidajav2hoc6k_s[];
extern const gm_tmpl gm_tplidcjkf79g8le;
extern const uint32_t gm_tplidcjkf79g8le_i[];
extern const gm_tmpl_piece gm_tplidcjkf79g8le_p[];
extern const gm_spec *const gm_tplidcjkf79g8le_s[];
extern const gm_tmpl gm_tplidl0tsmrpak6;
extern const uint32_t gm_tplidl0tsmrpak6_i[];
extern const gm_tmpl_piece gm_tplidl0tsmrpak6_p[];
extern const gm_spec *const gm_tplidl0tsmrpak6_s[];
extern const gm_spec *const gm_tpligtpf80arr7h_s[];
extern const gm_tmpl_piece gm_tplijibrlknrr0j_p[];
extern const gm_tmpl gm_tplims1qainr636;
extern const uint32_t gm_tplims1qainr636_i[];
extern const gm_tmpl_piece gm_tplims1qainr636_p[];
extern const gm_spec *const gm_tplims1qainr636_s[];
extern const gm_tmpl gm_tpliq0iop4ulk1g;
extern const uint32_t gm_tpliq0iop4ulk1g_i[];
extern const gm_tmpl_piece gm_tpliq0iop4ulk1g_p[];
extern const gm_spec *const gm_tpliq0iop4ulk1g_s[];
extern const uint32_t gm_tpliqpvgtpo05sn_i[];
extern const gm_tmpl_piece gm_tplj0vaqlfn7orc_p[];
extern const gm_tmpl gm_tplj16n1o03849r;
extern const uint32_t gm_tplj16n1o03849r_i[];
extern const gm_tmpl_piece gm_tplj16n1o03849r_p[];
extern const gm_spec *const gm_tplj16n1o03849r_s[];
extern const gm_tmpl gm_tplj6ss1j7sgouf;
extern const uint32_t gm_tplj6ss1j7sgouf_i[];
extern const gm_tmpl_piece gm_tplj6ss1j7sgouf_p[];
extern const gm_spec *const gm_tplj6ss1j7sgouf_s[];
extern const gm_tmpl_piece gm_tplj8iadiucj6so_p[];
extern const gm_spec *const gm_tplj8iadiucj6so_s[];
extern const uint32_t gm_tpljcgfng3gohsk_i[];
extern const uint32_t gm_tpljcso3h5g4o29_i[];
extern const gm_tmpl_piece gm_tpljdnrgbsps94k_p[];
extern const gm_spec *const gm_tpljicvo0ni8aba_s[];
extern const gm_spec *const gm_tpljkde9cibn2er_s[];
extern const gm_tmpl_piece gm_tpljm6mn1j3nrs6_p[];
extern const gm_spec *const gm_tpljqhtbatb6393_s[];
extern const gm_tmpl gm_tpljt2av4h7c132;
extern const uint32_t gm_tpljt2av4h7c132_i[];
extern const gm_tmpl_piece gm_tpljt2av4h7c132_p[];
extern const gm_spec *const gm_tpljt2av4h7c132_s[];
extern const gm_tmpl gm_tpljv32on6ui3vn;
extern const uint32_t gm_tpljv32on6ui3vn_i[];
extern const gm_tmpl_piece gm_tpljv32on6ui3vn_p[];
extern const gm_spec *const gm_tpljv32on6ui3vn_s[];
extern const uint32_t gm_tplk1keglepka61_i[];
extern const gm_tmpl gm_tplk3nkr2eh6don;
extern const uint32_t gm_tplk3nkr2eh6don_i[];
extern const gm_tmpl_piece gm_tplk3nkr2eh6don_p[];
extern const gm_spec *const gm_tplk3nkr2eh6don_s[];
extern const gm_tmpl gm_tplk83cf3bpuiqb;
extern const uint32_t gm_tplk83cf3bpuiqb_i[];
extern const gm_tmpl_piece gm_tplk83cf3bpuiqb_p[];
extern const gm_spec *const gm_tplk83cf3bpuiqb_s[];
extern const gm_tmpl_piece gm_tplkaran1m5lpqm_p[];
extern const gm_spec *const gm_tplkd4irlmplk3e_s[];
extern const gm_tmpl gm_tplkfag0fic60vv;
extern const uint32_t gm_tplkfag0fic60vv_i[];
extern const gm_tmpl_piece gm_tplkfag0fic60vv_p[];
extern const gm_spec *const gm_tplkfag0fic60vv_s[];
extern const gm_spec *const gm_tplkhnuvgfhr9v5_s[];
extern const uint32_t gm_tplkik9smkp1eb6_i[];
extern const gm_spec *const gm_tplkik9smkp1eb6_s[];
extern const gm_tmpl_piece gm_tplkp06s34ois53_p[];
extern const gm_tmpl_piece gm_tplkpmnsso79v4r_p[];
extern const uint32_t gm_tplkr4062p8a8bi_i[];
extern const gm_tmpl_piece gm_tplkte26oe5429d_p[];
extern const uint32_t gm_tplkun0h8smcrq9_i[];
extern const gm_tmpl_piece gm_tplkun0h8smcrq9_p[];
extern const gm_tmpl_piece gm_tplkvqpieru7v5v_p[];
extern const gm_spec *const gm_tpll1ot8bjie65s_s[];
extern const gm_spec *const gm_tpll487d6t52m4g_s[];
extern const uint32_t gm_tpllaurt6qemv8q_i[];
extern const gm_tmpl gm_tpllc6r78fq9cl8;
extern const uint32_t gm_tpllc6r78fq9cl8_i[];
extern const gm_tmpl_piece gm_tpllc6r78fq9cl8_p[];
extern const gm_spec *const gm_tpllc6r78fq9cl8_s[];
extern const uint32_t gm_tpllf7nufk7lvkm_i[];
extern const gm_spec *const gm_tpllfhth9mee4q8_s[];
extern const uint32_t gm_tpllihoeldkkkhv_i[];
extern const uint32_t gm_tpllinpkb0ktkcf_i[];
extern const gm_tmpl_piece gm_tpllinpkb0ktkcf_p[];
extern const gm_tmpl_piece gm_tpllovel7a2ips1_p[];
extern const gm_spec *const gm_tpllsre3itruefn_s[];
extern const gm_tmpl_piece gm_tpllv3q9mfan4rg_p[];
extern const gm_tmpl gm_tplm01msh3jahhc;
extern const uint32_t gm_tplm01msh3jahhc_i[];
extern const gm_tmpl_piece gm_tplm01msh3jahhc_p[];
extern const gm_spec *const gm_tplm01msh3jahhc_s[];
extern const uint32_t gm_tplm1fuloaqnfv4_i[];
extern const gm_tmpl_piece gm_tplm3qlt5n96af0_p[];
extern const gm_tmpl gm_tplm61d8mlvmepm;
extern const uint32_t gm_tplm61d8mlvmepm_i[];
extern const gm_tmpl_piece gm_tplm61d8mlvmepm_p[];
extern const gm_spec *const gm_tplm61d8mlvmepm_s[];
extern const gm_spec *const gm_tplm9t4pej720h2_s[];
extern const gm_tmpl_piece gm_tplmghq7vvcvnvi_p[];
extern const gm_tmpl gm_tplmj3d4h2pi7q3;
extern const uint32_t gm_tplmj3d4h2pi7q3_i[];
extern const gm_tmpl_piece gm_tplmj3d4h2pi7q3_p[];
extern const gm_spec *const gm_tplmj3d4h2pi7q3_s[];
extern const gm_tmpl_piece gm_tplmrpf5m9qkka0_p[];
extern const uint32_t gm_tplmsb1klg4n6ap_i[];
extern const uint32_t gm_tplmueh9mdvaqub_i[];
extern const uint32_t gm_tpln2i759e19cgt_i[];
extern const gm_tmpl_piece gm_tpln2j7s58m8uan_p[];
extern const gm_spec *const gm_tpln2j7s58m8uan_s[];
extern const gm_tmpl gm_tpln6vfieiab6l5;
extern const uint32_t gm_tpln6vfieiab6l5_i[];
extern const gm_tmpl_piece gm_tpln6vfieiab6l5_p[];
extern const gm_spec *const gm_tpln6vfieiab6l5_s[];
extern const gm_tmpl_piece gm_tpln8ctpo9hror7_p[];
extern const gm_spec *const gm_tpln92q8vfbc4aq_s[];
extern const gm_spec *const gm_tplncm14tpa4cdg_s[];
extern const uint32_t gm_tplncn0njcasr6v_i[];
extern const gm_tmpl gm_tplne0l0lciidvr;
extern const uint32_t gm_tplne0l0lciidvr_i[];
extern const gm_tmpl_piece gm_tplne0l0lciidvr_p[];
extern const gm_spec *const gm_tplne0l0lciidvr_s[];
extern const gm_tmpl_piece gm_tplneb8585d50a4_p[];
extern const gm_tmpl gm_tplng1u8s5kl5ir;
extern const uint32_t gm_tplng1u8s5kl5ir_i[];
extern const gm_tmpl_piece gm_tplng1u8s5kl5ir_p[];
extern const gm_spec *const gm_tplng1u8s5kl5ir_s[];
extern const uint32_t gm_tplnmm43n0jmcod_i[];
extern const gm_tmpl_piece gm_tplnoi44u4met07_p[];
extern const gm_tmpl gm_tplnokap11rv9jr;
extern const uint32_t gm_tplnokap11rv9jr_i[];
extern const gm_tmpl_piece gm_tplnokap11rv9jr_p[];
extern const gm_spec *const gm_tplnokap11rv9jr_s[];
extern const gm_tmpl_piece gm_tplnqad0r40bfej_p[];
extern const uint32_t gm_tplnr3ridveqobn_i[];
extern const gm_spec *const gm_tplnrfprs01gfmj_s[];
extern const gm_tmpl gm_tplns4e3al0r331;
extern const uint32_t gm_tplns4e3al0r331_i[];
extern const gm_tmpl_piece gm_tplns4e3al0r331_p[];
extern const gm_spec *const gm_tplns4e3al0r331_s[];
extern const gm_tmpl_piece gm_tplntci9ovg2hdj_p[];
extern const gm_tmpl_piece gm_tplnv17naljkeai_p[];
extern const gm_spec *const gm_tplnv6l3ecgreid_s[];
extern const gm_spec *const gm_tplo2fstigc8bcf_s[];
extern const gm_tmpl_piece gm_tplo2l8dmp437h2_p[];
extern const gm_spec *const gm_tplo66949vs5pfo_s[];
extern const uint32_t gm_tplo8vr0jo68llo_i[];
extern const gm_tmpl_piece gm_tplo8vr0jo68llo_p[];
extern const uint32_t gm_tploc41q7b5gurn_i[];
extern const gm_tmpl gm_tploc8iqi9f2el1;
extern const uint32_t gm_tploc8iqi9f2el1_i[];
extern const gm_tmpl_piece gm_tploc8iqi9f2el1_p[];
extern const gm_spec *const gm_tploc8iqi9f2el1_s[];
extern const gm_tmpl_piece gm_tplod1bbc3c9s5j_p[];
extern const gm_spec *const gm_tplod1bbc3c9s5j_s[];
extern const gm_tmpl_piece gm_tplodcipjau6eqr_p[];
extern const gm_spec *const gm_tplodcipjau6eqr_s[];
extern const gm_tmpl_piece gm_tplof36cm9vqs8e_p[];
extern const gm_tmpl gm_tploh2305pu3svo;
extern const uint32_t gm_tploh2305pu3svo_i[];
extern const gm_tmpl_piece gm_tploh2305pu3svo_p[];
extern const gm_spec *const gm_tploh2305pu3svo_s[];
extern const gm_spec *const gm_tploj8vgi84m11i_s[];
extern const uint32_t gm_tplok9pv33vcsg0_i[];
extern const uint32_t gm_tplokoueqaolfmt_i[];
extern const uint32_t gm_tplomrgvu31uqof_i[];
extern const gm_tmpl gm_tplorj2ck0ovl7t;
extern const uint32_t gm_tplorj2ck0ovl7t_i[];
extern const gm_tmpl_piece gm_tplorj2ck0ovl7t_p[];
extern const gm_spec *const gm_tplorj2ck0ovl7t_s[];
extern const gm_tmpl gm_tplosjvq6nju6rc;
extern const uint32_t gm_tplosjvq6nju6rc_i[];
extern const gm_tmpl_piece gm_tplosjvq6nju6rc_p[];
extern const gm_spec *const gm_tplosjvq6nju6rc_s[];
extern const gm_tmpl gm_tplotost4gd8bha;
extern const uint32_t gm_tplotost4gd8bha_i[];
extern const gm_tmpl_piece gm_tplotost4gd8bha_p[];
extern const gm_spec *const gm_tplotost4gd8bha_s[];
extern const gm_tmpl gm_tplp0kc3a3fekq4;
extern const uint32_t gm_tplp0kc3a3fekq4_i[];
extern const gm_tmpl_piece gm_tplp0kc3a3fekq4_p[];
extern const gm_spec *const gm_tplp0kc3a3fekq4_s[];
extern const gm_tmpl gm_tplp3ak0q2gramc;
extern const uint32_t gm_tplp3ak0q2gramc_i[];
extern const gm_tmpl_piece gm_tplp3ak0q2gramc_p[];
extern const gm_spec *const gm_tplp3ak0q2gramc_s[];
extern const uint32_t gm_tplp7gdl2lerimh_i[];
extern const gm_tmpl gm_tplp7srseh4nakh;
extern const uint32_t gm_tplp7srseh4nakh_i[];
extern const gm_tmpl_piece gm_tplp7srseh4nakh_p[];
extern const gm_spec *const gm_tplp7srseh4nakh_s[];
extern const gm_tmpl gm_tplpf3jtoa0oclt;
extern const uint32_t gm_tplpf3jtoa0oclt_i[];
extern const gm_tmpl_piece gm_tplpf3jtoa0oclt_p[];
extern const gm_spec *const gm_tplpf3jtoa0oclt_s[];
extern const gm_tmpl_piece gm_tplpfavv7btb1m5_p[];
extern const gm_spec *const gm_tplpfavv7btb1m5_s[];
extern const gm_tmpl gm_tplph8v0nbmofpg;
extern const uint32_t gm_tplph8v0nbmofpg_i[];
extern const gm_tmpl_piece gm_tplph8v0nbmofpg_p[];
extern const gm_spec *const gm_tplph8v0nbmofpg_s[];
extern const gm_tmpl_piece gm_tplplkqcb01ocrd_p[];
extern const gm_tmpl_piece gm_tplpnk24geo6qd8_p[];
extern const gm_tmpl gm_tplpodff9060oeo;
extern const uint32_t gm_tplpodff9060oeo_i[];
extern const gm_tmpl_piece gm_tplpodff9060oeo_p[];
extern const gm_spec *const gm_tplpodff9060oeo_s[];
extern const gm_tmpl_piece gm_tplptgaia3s19c9_p[];
extern const uint32_t gm_tplptk7v9d6qojh_i[];
extern const gm_tmpl gm_tplpvh6srii84sj;
extern const uint32_t gm_tplpvh6srii84sj_i[];
extern const gm_tmpl_piece gm_tplpvh6srii84sj_p[];
extern const gm_spec *const gm_tplpvh6srii84sj_s[];
extern const gm_tmpl gm_tplq74l4dgr0l9g;
extern const uint32_t gm_tplq74l4dgr0l9g_i[];
extern const gm_tmpl_piece gm_tplq74l4dgr0l9g_p[];
extern const gm_spec *const gm_tplq74l4dgr0l9g_s[];
extern const gm_tmpl_piece gm_tplq7kai16vns6p_p[];
extern const gm_spec *const gm_tplq7vstpm0lgda_s[];
extern const gm_tmpl gm_tplqf2cda88og0i;
extern const uint32_t gm_tplqf2cda88og0i_i[];
extern const gm_tmpl_piece gm_tplqf2cda88og0i_p[];
extern const gm_spec *const gm_tplqf2cda88og0i_s[];
extern const gm_spec *const gm_tplqgt00aqumuje_s[];
extern const gm_spec *const gm_tplqh3e2aojp71u_s[];
extern const gm_spec *const gm_tplqj931pe0994k_s[];
extern const gm_tmpl_piece gm_tplqjfk6t5rn06e_p[];
extern const gm_tmpl gm_tplqrah664loq9e;
extern const uint32_t gm_tplqrah664loq9e_i[];
extern const gm_tmpl_piece gm_tplqrah664loq9e_p[];
extern const gm_spec *const gm_tplqrah664loq9e_s[];
extern const gm_spec *const gm_tplqrbhvccemc9k_s[];
extern const gm_tmpl_piece gm_tplqtueduh1r3n9_p[];
extern const gm_spec *const gm_tplqtueduh1r3n9_s[];
extern const uint32_t gm_tplr56tn8lpufgs_i[];
extern const gm_tmpl gm_tplrcmh11f2ocju;
extern const uint32_t gm_tplrcmh11f2ocju_i[];
extern const gm_tmpl_piece gm_tplrcmh11f2ocju_p[];
extern const gm_spec *const gm_tplrcmh11f2ocju_s[];
extern const gm_tmpl gm_tplrcqo0t095bsv;
extern const uint32_t gm_tplrcqo0t095bsv_i[];
extern const gm_tmpl_piece gm_tplrcqo0t095bsv_p[];
extern const gm_spec *const gm_tplrcqo0t095bsv_s[];
extern const gm_tmpl gm_tplrd8stfk49gik;
extern const uint32_t gm_tplrd8stfk49gik_i[];
extern const gm_tmpl_piece gm_tplrd8stfk49gik_p[];
extern const gm_spec *const gm_tplrd8stfk49gik_s[];
extern const gm_spec *const gm_tplrffp652tkgo2_s[];
extern const gm_spec *const gm_tplrnntb2r7ns40_s[];
extern const gm_tmpl_piece gm_tplrp4haaemeqja_p[];
extern const uint32_t gm_tplrp7ukmgscavj_i[];
extern const uint32_t gm_tplrsu0pn6pbour_i[];
extern const uint32_t gm_tplru5t7oahrjah_i[];
extern const gm_tmpl_piece gm_tplrveq8m992ml7_p[];
extern const uint32_t gm_tpls4bcac20qnq6_i[];
extern const gm_spec *const gm_tpls4d1lh9jd2u8_s[];
extern const gm_tmpl gm_tpls4d220l28nlk;
extern const uint32_t gm_tpls4d220l28nlk_i[];
extern const gm_tmpl_piece gm_tpls4d220l28nlk_p[];
extern const gm_spec *const gm_tpls4d220l28nlk_s[];
extern const gm_tmpl gm_tpls5b11sf5gq9o;
extern const uint32_t gm_tpls5b11sf5gq9o_i[];
extern const gm_tmpl_piece gm_tpls5b11sf5gq9o_p[];
extern const gm_spec *const gm_tpls5b11sf5gq9o_s[];
extern const gm_spec *const gm_tpls7559askt562_s[];
extern const gm_tmpl_piece gm_tpls7s61lfbg9l6_p[];
extern const gm_tmpl gm_tplsbqgr9k4itq0;
extern const uint32_t gm_tplsbqgr9k4itq0_i[];
extern const gm_tmpl_piece gm_tplsbqgr9k4itq0_p[];
extern const gm_spec *const gm_tplsbqgr9k4itq0_s[];
extern const gm_tmpl gm_tplsdvt6aocmtuo;
extern const uint32_t gm_tplsdvt6aocmtuo_i[];
extern const gm_tmpl_piece gm_tplsdvt6aocmtuo_p[];
extern const gm_spec *const gm_tplsdvt6aocmtuo_s[];
extern const gm_tmpl gm_tplsg1jhb9vp948;
extern const uint32_t gm_tplsg1jhb9vp948_i[];
extern const gm_tmpl_piece gm_tplsg1jhb9vp948_p[];
extern const gm_spec *const gm_tplsg1jhb9vp948_s[];
extern const gm_tmpl gm_tplshqe0v39oc52;
extern const uint32_t gm_tplshqe0v39oc52_i[];
extern const gm_tmpl_piece gm_tplshqe0v39oc52_p[];
extern const gm_spec *const gm_tplshqe0v39oc52_s[];
extern const gm_tmpl gm_tplsiiifs0eedvi;
extern const uint32_t gm_tplsiiifs0eedvi_i[];
extern const gm_tmpl_piece gm_tplsiiifs0eedvi_p[];
extern const gm_spec *const gm_tplsiiifs0eedvi_s[];
extern const uint32_t gm_tplsleqso6g0tjq_i[];
extern const gm_tmpl gm_tplsnmu9a7htqov;
extern const uint32_t gm_tplsnmu9a7htqov_i[];
extern const gm_tmpl_piece gm_tplsnmu9a7htqov_p[];
extern const gm_spec *const gm_tplsnmu9a7htqov_s[];
extern const uint32_t gm_tplso413blsqnmm_i[];
extern const gm_tmpl_piece gm_tplsogg2sp0m9p9_p[];
extern const gm_tmpl_piece gm_tplspgpn4qu6rd4_p[];
extern const gm_tmpl_piece gm_tplss5b4q9est2j_p[];
extern const uint32_t gm_tplt0hjq4nb3jer_i[];
extern const gm_tmpl_piece gm_tplt1lu36t604pk_p[];
extern const gm_tmpl_piece gm_tplt320ru9bfpdd_p[];
extern const gm_spec *const gm_tplt392n8hqfhn9_s[];
extern const gm_spec *const gm_tplt3d4sltsd44m_s[];
extern const gm_spec *const gm_tplt8hcvvk31bg7_s[];
extern const gm_spec *const gm_tpltb9h1t5060dp_s[];
extern const uint32_t gm_tpltev7cklsieaq_i[];
extern const gm_tmpl_piece gm_tplthb0oue219bq_p[];
extern const gm_tmpl_piece gm_tpltktipp7m43tk_p[];
extern const gm_spec *const gm_tpltktipp7m43tk_s[];
extern const uint32_t gm_tpltl0rfkesf8ms_i[];
extern const gm_tmpl_piece gm_tpltm9r5n97je13_p[];
extern const gm_tmpl gm_tplto3kksb9sot8;
extern const uint32_t gm_tplto3kksb9sot8_i[];
extern const gm_tmpl_piece gm_tplto3kksb9sot8_p[];
extern const gm_spec *const gm_tplto3kksb9sot8_s[];
extern const gm_tmpl_piece gm_tpltue438lnncbq_p[];
extern const gm_tmpl gm_tpltvrl7dp7h9m1;
extern const uint32_t gm_tpltvrl7dp7h9m1_i[];
extern const gm_tmpl_piece gm_tpltvrl7dp7h9m1_p[];
extern const gm_spec *const gm_tpltvrl7dp7h9m1_s[];
extern const gm_tmpl gm_tplu0n2ra1s30o8;
extern const uint32_t gm_tplu0n2ra1s30o8_i[];
extern const gm_tmpl_piece gm_tplu0n2ra1s30o8_p[];
extern const gm_spec *const gm_tplu0n2ra1s30o8_s[];
extern const uint32_t gm_tplu3lqf0bugia3_i[];
extern const gm_tmpl_piece gm_tplu3lqf0bugia3_p[];
extern const uint32_t gm_tplu7gq2cm8aanh_i[];
extern const gm_tmpl_piece gm_tplubod389iot6h_p[];
extern const gm_tmpl gm_tpluc8r0n3peqad;
extern const uint32_t gm_tpluc8r0n3peqad_i[];
extern const gm_tmpl_piece gm_tpluc8r0n3peqad_p[];
extern const gm_spec *const gm_tpluc8r0n3peqad_s[];
extern const gm_tmpl_piece gm_tpluc9sbr66n218_p[];
extern const gm_tmpl gm_tpludgprff7jihr;
extern const uint32_t gm_tpludgprff7jihr_i[];
extern const gm_tmpl_piece gm_tpludgprff7jihr_p[];
extern const gm_spec *const gm_tpludgprff7jihr_s[];
extern const gm_tmpl_piece gm_tpludqpd0ngjfu6_p[];
extern const gm_tmpl_piece gm_tpluduaa40ad285_p[];
extern const uint32_t gm_tplue2kdcikgcof_i[];
extern const gm_spec *const gm_tplui184heavke2_s[];
extern const gm_spec *const gm_tplun02iesa1r5k_s[];
extern const gm_tmpl_piece gm_tplup0bf5d2eopf_p[];
extern const gm_tmpl gm_tplut4ib9ov72oe;
extern const uint32_t gm_tplut4ib9ov72oe_i[];
extern const gm_tmpl_piece gm_tplut4ib9ov72oe_p[];
extern const gm_spec *const gm_tplut4ib9ov72oe_s[];
extern const gm_spec *const gm_tplutkid6t0497p_s[];
extern const uint32_t gm_tpluuj281d42vhc_i[];
extern const gm_spec *const gm_tpluuj281d42vhc_s[];
extern const gm_tmpl_piece gm_tplv1i7e9gejv6a_p[];
extern const uint32_t gm_tplv1shkj7l87mc_i[];
extern const uint32_t gm_tplv23k7silrvnp_i[];
extern const uint32_t gm_tplv3jh8gpmh6o5_i[];
extern const gm_spec *const gm_tplv6dbvs9bjnes_s[];
extern const gm_tmpl gm_tplvdf3kbq8rjbi;
extern const uint32_t gm_tplvdf3kbq8rjbi_i[];
extern const gm_tmpl_piece gm_tplvdf3kbq8rjbi_p[];
extern const gm_spec *const gm_tplvdf3kbq8rjbi_s[];
extern const gm_tmpl_piece gm_tplvflfleui9qjm_p[];
extern const uint32_t gm_tplvftnuchaenfj_i[];
extern const gm_spec *const gm_tplvhp4k8a2f5k6_s[];
extern const gm_tmpl_piece gm_tplviqo6u6r0sr3_p[];
extern const gm_spec *const gm_tplvle6f1rmlmjh_s[];
extern const gm_tmpl gm_tplvq03d5skib28;
extern const uint32_t gm_tplvq03d5skib28_i[];
extern const gm_tmpl_piece gm_tplvq03d5skib28_p[];
extern const gm_spec *const gm_tplvq03d5skib28_s[];
extern const gm_tmpl gm_tplvqa9kv7qt4oc;
extern const uint32_t gm_tplvqa9kv7qt4oc_i[];
extern const gm_tmpl_piece gm_tplvqa9kv7qt4oc_p[];
extern const gm_spec *const gm_tplvqa9kv7qt4oc_s[];
extern const gm_spec *const gm_tplvqpq1chrhvno_s[];
extern const gm_spec *const gm_tplvrmulr6rg858_s[];
extern const uint32_t gm_tplvsndsjoh0e6u_i[];
extern const gm_tmpl_piece gm_tplvuahs93lrft8_p[];
extern const gm_tmpl gm_tplvvfpp9olf6cl;
extern const uint32_t gm_tplvvfpp9olf6cl_i[];
extern const gm_tmpl_piece gm_tplvvfpp9olf6cl_p[];
extern const gm_spec *const gm_tplvvfpp9olf6cl_s[];
extern const gm_tmpl gm_tplvvic2bq8a97l;
extern const uint32_t gm_tplvvic2bq8a97l_i[];
extern const gm_tmpl_piece gm_tplvvic2bq8a97l_p[];
extern const gm_spec *const gm_tplvvic2bq8a97l_s[];
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_tybqb4gb1d92;
extern const gm_variant gm_tybqb4gb1d92_v[];
extern const gm_variant gm_tycmo75g2ssd_v[];
extern const gm_type gm_tylv5i3f25jk;
extern const gm_variant gm_tylv5i3f25jk_v[];
extern const gm_variant gm_tytje7cegoml_v[];
const char gm_src0dtioet3p4f6[] = "compiler/parse/expr.gm";
const char gm_src27ouot20tc5m[] = "compiler/lint/present.gm";
const char gm_src4hj0beu8p6ao[] = "compiler/lsp/supervisor.gm";
const char gm_src4rbevsr9gjng[] = "compiler/driver/rtcache.gm";
const char gm_src5eu5l4a7gv6s[] = "compiler/check/program.gm";
const char gm_src9pvgnpibl2nm[] = "compiler/interp/num.gm";
const char gm_srcb4m5qm2daehm[] = "compiler/check/soa.gm";
const char gm_srcfk7rg8t5apoh[] = "compiler/lint/stale.gm";
const char gm_srck5orfpjti60c[] = "compiler/types/vectors.gm";
const char gm_srcl8pn2pjtmj6k[] = "compiler/interp/builtins.gm";
const char gm_srcm8m4ndqum1bp[] = "compiler/check/bindings.gm";
const char gm_srcmhupam4se3h5[] = "compiler/sema/fnsig.gm";
const char gm_srcuj9anpi7p1mq[] = "compiler/cgen/program.gm";
const char gm_srcvmmksi0829pa[] = "compiler/syntax/lexer.gm";
const gm_spec *const gm_tpl005eoei82778_s[] = {NULL};
const gm_spec *const gm_tpl00b7kgp37ng7_s[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl0scna75sco5f_s[] = {NULL, NULL};
const gm_spec *const gm_tpl0vock46lp3ec_s[] = {NULL, NULL};
const gm_spec *const gm_tpl16petnrgdcd0_s[] = {NULL};
const gm_spec *const gm_tpl1dleoebfmvhp_s[] = {NULL, NULL};
const gm_spec *const gm_tpl233lm3m83499_s[] = {NULL};
const gm_spec *const gm_tpl2egb012obd94_s[] = {NULL};
const gm_spec *const gm_tpl2n8jqp2rhn52_s[] = {NULL, NULL};
const gm_spec *const gm_tpl2re1r0fre48m_s[] = {NULL};
const gm_spec *const gm_tpl3t5edr01qp5c_s[] = {NULL, NULL};
const gm_spec *const gm_tpl4524ekle00ud_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpl48h8br68c917_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl4c2mfb3cngak_s[] = {NULL};
const gm_spec *const gm_tpl4dkmnviaa508_s[] = {NULL, NULL};
const gm_spec *const gm_tpl4fm7lnhj31fn_s[] = {NULL};
const gm_spec *const gm_tpl4g6bpqlb10dt_s[] = {NULL};
const gm_spec *const gm_tpl4r3nb3bvll23_s[] = {NULL};
const gm_spec *const gm_tpl5a8og2hcltt3_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpl5cnv021ebod7_s[] = {NULL};
const gm_spec *const gm_tpl5r25bpo8hobt_s[] = {NULL, NULL};
const gm_spec *const gm_tpl6fm59pne5ng7_s[] = {NULL};
const gm_spec *const gm_tpl77hog51tnj6o_s[] = {NULL};
const gm_spec *const gm_tpl7qfg883s3b97_s[] = {NULL};
const gm_spec *const gm_tpl7ut0r685hcaa_s[] = {NULL};
const gm_spec *const gm_tpl84r77qupuujj_s[] = {NULL};
const gm_spec *const gm_tpl85p2q6ermgga_s[] = {NULL};
const gm_spec *const gm_tpl86pqqgq5ch0o_s[] = {NULL};
const gm_spec *const gm_tpl8pvdc7qbgge6_s[] = {NULL};
const gm_spec *const gm_tpl8s8bnem30e1s_s[] = {NULL, NULL};
const gm_spec *const gm_tpl94ei7rae9si2_s[] = {NULL};
const gm_spec *const gm_tpl9uslg4i3oo2s_s[] = {&(gm_spec){32, 0, 0, 1, 0, 1, 0u, -1}};
const gm_spec *const gm_tpla1i8rr6vfju7_s[] = {NULL};
const gm_spec *const gm_tpla843kfhqtvit_s[] = {NULL, NULL};
const gm_spec *const gm_tplablvqji1bpu7_s[] = {NULL};
const gm_spec *const gm_tplak7vr9q0b05e_s[] = {NULL};
const gm_spec *const gm_tplbh7305mc9stu_s[] = {NULL, NULL};
const gm_spec *const gm_tplc9nrcb798jhl_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplcq107msa8ktd_s[] = {NULL};
const gm_spec *const gm_tplcttkqubjcaqf_s[] = {NULL};
const gm_spec *const gm_tpldhmlf1n8qcdi_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpldiv6dh0or1km_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpldj14c3jgvlnb_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpldknbfev12jrb_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpldp3bk13rlc44_s[] = {NULL};
const gm_spec *const gm_tpldpi521pvp06p_s[] = {NULL};
const gm_spec *const gm_tpldsg332pn89g6_s[] = {NULL};
const gm_spec *const gm_tple0332mlih7si_s[] = {NULL};
const gm_spec *const gm_tplebqthjijvqt0_s[] = {NULL, NULL};
const gm_spec *const gm_tpleiobdo4rm4hs_s[] = {NULL, NULL};
const gm_spec *const gm_tplet6obcbkjbht_s[] = {NULL};
const gm_spec *const gm_tpleth0e0pd50mr_s[] = {NULL};
const gm_spec *const gm_tplett0du8rhujl_s[] = {NULL, NULL};
const gm_spec *const gm_tpleugrd3m4ci89_s[] = {NULL};
const gm_spec *const gm_tplf4oacobin5qk_s[] = {NULL};
const gm_spec *const gm_tplfm1jmgsrn2ua_s[] = {NULL, NULL};
const gm_spec *const gm_tplfsmasr3tgalo_s[] = {NULL, NULL};
const gm_spec *const gm_tplfsrd1mgov7nd_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplfvv1qe6os0le_s[] = {NULL};
const gm_spec *const gm_tplg24lfmv0pffq_s[] = {NULL, NULL};
const gm_spec *const gm_tplg91jq41nbbkt_s[] = {NULL, NULL};
const gm_spec *const gm_tplgclcdp539fpt_s[] = {NULL, NULL};
const gm_spec *const gm_tplgih8dfq5hun7_s[] = {NULL, NULL};
const gm_spec *const gm_tplgm9vv1vtdqi9_s[] = {NULL};
const gm_spec *const gm_tplgnp9u7qeb3h6_s[] = {NULL, NULL};
const gm_spec *const gm_tplgt8u7d5aku5b_s[] = {NULL, NULL};
const gm_spec *const gm_tplh2em7r1dmffm_s[] = {NULL};
const gm_spec *const gm_tplh7ffbsfnitkh_s[] = {NULL, NULL};
const gm_spec *const gm_tplhkjcpttbsqt0_s[] = {NULL};
const gm_spec *const gm_tpligtpf80arr7h_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpliq0iop4ulk1g_s[] = {NULL, NULL};
const gm_spec *const gm_tplj8iadiucj6so_s[] = {NULL};
const gm_spec *const gm_tpljicvo0ni8aba_s[] = {NULL};
const gm_spec *const gm_tpljkde9cibn2er_s[] = {NULL, NULL};
const gm_spec *const gm_tpljqhtbatb6393_s[] = {NULL, NULL};
const gm_spec *const gm_tplkd4irlmplk3e_s[] = {NULL, NULL};
const gm_spec *const gm_tplkhnuvgfhr9v5_s[] = {NULL, NULL};
const gm_spec *const gm_tplkik9smkp1eb6_s[] = {NULL};
const gm_spec *const gm_tpll1ot8bjie65s_s[] = {NULL};
const gm_spec *const gm_tpll487d6t52m4g_s[] = {NULL};
const gm_spec *const gm_tpllfhth9mee4q8_s[] = {NULL, NULL};
const gm_spec *const gm_tpllsre3itruefn_s[] = {NULL, NULL};
const gm_spec *const gm_tplm9t4pej720h2_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tpln2j7s58m8uan_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tpln92q8vfbc4aq_s[] = {NULL};
const gm_spec *const gm_tplncm14tpa4cdg_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplnrfprs01gfmj_s[] = {NULL};
const gm_spec *const gm_tplnv6l3ecgreid_s[] = {NULL, NULL};
const gm_spec *const gm_tplo2fstigc8bcf_s[] = {NULL, NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplo66949vs5pfo_s[] = {NULL, NULL};
const gm_spec *const gm_tplod1bbc3c9s5j_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplodcipjau6eqr_s[] = {NULL, NULL};
const gm_spec *const gm_tploh2305pu3svo_s[] = {NULL};
const gm_spec *const gm_tploj8vgi84m11i_s[] = {NULL};
const gm_spec *const gm_tplpfavv7btb1m5_s[] = {NULL, NULL};
const gm_spec *const gm_tplpodff9060oeo_s[] = {NULL};
const gm_spec *const gm_tplq7vstpm0lgda_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplqgt00aqumuje_s[] = {NULL, NULL};
const gm_spec *const gm_tplqh3e2aojp71u_s[] = {NULL};
const gm_spec *const gm_tplqj931pe0994k_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplqrbhvccemc9k_s[] = {NULL};
const gm_spec *const gm_tplqtueduh1r3n9_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplrffp652tkgo2_s[] = {NULL, NULL, NULL, NULL};
const gm_spec *const gm_tplrnntb2r7ns40_s[] = {NULL};
const gm_spec *const gm_tpls4d1lh9jd2u8_s[] = {NULL};
const gm_spec *const gm_tpls7559askt562_s[] = {NULL, NULL};
const gm_spec *const gm_tplsbqgr9k4itq0_s[] = {NULL};
const gm_spec *const gm_tplsdvt6aocmtuo_s[] = {NULL};
const gm_spec *const gm_tplsnmu9a7htqov_s[] = {NULL};
const gm_spec *const gm_tplt392n8hqfhn9_s[] = {&(gm_spec){32, 0, 0, 0, 0, 0, 0u, 1}};
const gm_spec *const gm_tplt3d4sltsd44m_s[] = {NULL, NULL};
const gm_spec *const gm_tplt8hcvvk31bg7_s[] = {NULL};
const gm_spec *const gm_tpltb9h1t5060dp_s[] = {NULL, NULL};
const gm_spec *const gm_tpltktipp7m43tk_s[] = {NULL, NULL, NULL};
const gm_spec *const gm_tplui184heavke2_s[] = {NULL};
const gm_spec *const gm_tplun02iesa1r5k_s[] = {NULL};
const gm_spec *const gm_tplutkid6t0497p_s[] = {NULL};
const gm_spec *const gm_tpluuj281d42vhc_s[] = {NULL};
const gm_spec *const gm_tplv6dbvs9bjnes_s[] = {NULL, NULL};
const gm_spec *const gm_tplvhp4k8a2f5k6_s[] = {NULL, NULL};
const gm_spec *const gm_tplvle6f1rmlmjh_s[] = {NULL};
const gm_spec *const gm_tplvqpq1chrhvno_s[] = {NULL};
const gm_spec *const gm_tplvrmulr6rg858_s[] = {NULL};
const gm_tmpl gm_tpl00eps1l57c5a = {2u, gm_tpl00eps1l57c5a_p, gm_tpl00eps1l57c5a_i, gm_tpl00eps1l57c5a_s};
const gm_tmpl gm_tpl0j5i33et5u62 = {1u, gm_tpl0j5i33et5u62_p, gm_tpl0j5i33et5u62_i, gm_tpl0j5i33et5u62_s};
const gm_tmpl gm_tpl0rhldcdavmcg = {2u, gm_tpl0rhldcdavmcg_p, gm_tpl0rhldcdavmcg_i, gm_tpl0rhldcdavmcg_s};
const gm_tmpl gm_tpl0tljhf3n7k2p = {3u, gm_tpl0tljhf3n7k2p_p, gm_tpl0tljhf3n7k2p_i, gm_tpl0tljhf3n7k2p_s};
const gm_tmpl gm_tpl150n44984e0j = {3u, gm_tpl150n44984e0j_p, gm_tpl150n44984e0j_i, gm_tpl150n44984e0j_s};
const gm_tmpl gm_tpl17t0pmu1kh1u = {3u, gm_tpl17t0pmu1kh1u_p, gm_tpl17t0pmu1kh1u_i, gm_tpl17t0pmu1kh1u_s};
const gm_tmpl gm_tpl1avjsgn54rqb = {1u, gm_tpl1avjsgn54rqb_p, gm_tpl1avjsgn54rqb_i, gm_tpl1avjsgn54rqb_s};
const gm_tmpl gm_tpl1c171lb88cdd = {2u, gm_tpl1c171lb88cdd_p, gm_tpl1c171lb88cdd_i, gm_tpl1c171lb88cdd_s};
const gm_tmpl gm_tpl1oodsdaf08oj = {2u, gm_tpl1oodsdaf08oj_p, gm_tpl1oodsdaf08oj_i, gm_tpl1oodsdaf08oj_s};
const gm_tmpl gm_tpl1uf3f46uq3oj = {1u, gm_tpl1uf3f46uq3oj_p, gm_tpl1uf3f46uq3oj_i, gm_tpl1uf3f46uq3oj_s};
const gm_tmpl gm_tpl2aca96qot4p8 = {1u, gm_tpl2aca96qot4p8_p, gm_tpl2aca96qot4p8_i, gm_tpl2aca96qot4p8_s};
const gm_tmpl gm_tpl2d7ij9b2sa2u = {1u, gm_tpl2d7ij9b2sa2u_p, gm_tpl2d7ij9b2sa2u_i, gm_tpl2d7ij9b2sa2u_s};
const gm_tmpl gm_tpl33em4maossan = {1u, gm_tpl33em4maossan_p, gm_tpl33em4maossan_i, gm_tpl33em4maossan_s};
const gm_tmpl gm_tpl3cldid2v21cr = {2u, gm_tpl3cldid2v21cr_p, gm_tpl3cldid2v21cr_i, gm_tpl3cldid2v21cr_s};
const gm_tmpl gm_tpl3tkekujl97kv = {1u, gm_tpl3tkekujl97kv_p, gm_tpl3tkekujl97kv_i, gm_tpl3tkekujl97kv_s};
const gm_tmpl gm_tpl470f2c4pe1su = {1u, gm_tpl470f2c4pe1su_p, gm_tpl470f2c4pe1su_i, gm_tpl470f2c4pe1su_s};
const gm_tmpl gm_tpl54tnh29jjfju = {3u, gm_tpl54tnh29jjfju_p, gm_tpl54tnh29jjfju_i, gm_tpl54tnh29jjfju_s};
const gm_tmpl gm_tpl5hraomijgptv = {1u, gm_tpl5hraomijgptv_p, gm_tpl5hraomijgptv_i, gm_tpl5hraomijgptv_s};
const gm_tmpl gm_tpl5oph1bqhjo1e = {1u, gm_tpl5oph1bqhjo1e_p, gm_tpl5oph1bqhjo1e_i, gm_tpl5oph1bqhjo1e_s};
const gm_tmpl gm_tpl642cf7k81lop = {1u, gm_tpl642cf7k81lop_p, gm_tpl642cf7k81lop_i, gm_tpl642cf7k81lop_s};
const gm_tmpl gm_tpl65nnuqtjd24o = {1u, gm_tpl65nnuqtjd24o_p, gm_tpl65nnuqtjd24o_i, gm_tpl65nnuqtjd24o_s};
const gm_tmpl gm_tpl6md5hu7rmp82 = {1u, gm_tpl6md5hu7rmp82_p, gm_tpl6md5hu7rmp82_i, gm_tpl6md5hu7rmp82_s};
const gm_tmpl gm_tpl7bbfeces83t7 = {5u, gm_tpl7bbfeces83t7_p, gm_tpl7bbfeces83t7_i, gm_tpl7bbfeces83t7_s};
const gm_tmpl gm_tpl7hvv6i3t3o0k = {2u, gm_tpl7hvv6i3t3o0k_p, gm_tpl7hvv6i3t3o0k_i, gm_tpl7hvv6i3t3o0k_s};
const gm_tmpl gm_tpl7jau285qbbrq = {2u, gm_tpl7jau285qbbrq_p, gm_tpl7jau285qbbrq_i, gm_tpl7jau285qbbrq_s};
const gm_tmpl gm_tpl7kdhh1egc9gc = {3u, gm_tpl7kdhh1egc9gc_p, gm_tpl7kdhh1egc9gc_i, gm_tpl7kdhh1egc9gc_s};
const gm_tmpl gm_tpl7ufg84gs1lc7 = {2u, gm_tpl7ufg84gs1lc7_p, gm_tpl7ufg84gs1lc7_i, gm_tpl7ufg84gs1lc7_s};
const gm_tmpl gm_tpl82nt5tav1er7 = {1u, gm_tpl82nt5tav1er7_p, gm_tpl82nt5tav1er7_i, gm_tpl82nt5tav1er7_s};
const gm_tmpl gm_tpl86pqqgq5ch0o = {1u, gm_tpl86pqqgq5ch0o_p, gm_tpl86pqqgq5ch0o_i, gm_tpl86pqqgq5ch0o_s};
const gm_tmpl gm_tpl88ij4e6nvk4u = {1u, gm_tpl88ij4e6nvk4u_p, gm_tpl88ij4e6nvk4u_i, gm_tpl88ij4e6nvk4u_s};
const gm_tmpl gm_tpl896mu4np77il = {1u, gm_tpl896mu4np77il_p, gm_tpl896mu4np77il_i, gm_tpl896mu4np77il_s};
const gm_tmpl gm_tpl8bar0du85qu4 = {4u, gm_tpl8bar0du85qu4_p, gm_tpl8bar0du85qu4_i, gm_tpl8bar0du85qu4_s};
const gm_tmpl gm_tpl8c0nr5c8mas7 = {3u, gm_tpl8c0nr5c8mas7_p, gm_tpl8c0nr5c8mas7_i, gm_tpl8c0nr5c8mas7_s};
const gm_tmpl gm_tpl8eucsm7tjmm0 = {1u, gm_tpl8eucsm7tjmm0_p, gm_tpl8eucsm7tjmm0_i, gm_tpl8eucsm7tjmm0_s};
const gm_tmpl gm_tpl8in37qe3omcj = {1u, gm_tpl8in37qe3omcj_p, gm_tpl8in37qe3omcj_i, gm_tpl8in37qe3omcj_s};
const gm_tmpl gm_tpla0ldfomuin4r = {3u, gm_tpla0ldfomuin4r_p, gm_tpla0ldfomuin4r_i, gm_tpla0ldfomuin4r_s};
const gm_tmpl gm_tplagt25ql42kpb = {2u, gm_tplagt25ql42kpb_p, gm_tplagt25ql42kpb_i, gm_tplagt25ql42kpb_s};
const gm_tmpl gm_tplao1juqpqjfv9 = {1u, gm_tplao1juqpqjfv9_p, gm_tplao1juqpqjfv9_i, gm_tplao1juqpqjfv9_s};
const gm_tmpl gm_tplbdrnrpalngql = {1u, gm_tplbdrnrpalngql_p, gm_tplbdrnrpalngql_i, gm_tplbdrnrpalngql_s};
const gm_tmpl gm_tplbgmktlahcqrn = {1u, gm_tplbgmktlahcqrn_p, gm_tplbgmktlahcqrn_i, gm_tplbgmktlahcqrn_s};
const gm_tmpl gm_tplbk128im3qlnl = {2u, gm_tplbk128im3qlnl_p, gm_tplbk128im3qlnl_i, gm_tplbk128im3qlnl_s};
const gm_tmpl gm_tplbr0fi0201tmv = {1u, gm_tplbr0fi0201tmv_p, gm_tplbr0fi0201tmv_i, gm_tplbr0fi0201tmv_s};
const gm_tmpl gm_tplc0d2186db2jt = {1u, gm_tplc0d2186db2jt_p, gm_tplc0d2186db2jt_i, gm_tplc0d2186db2jt_s};
const gm_tmpl gm_tplc3kt858jk6jp = {2u, gm_tplc3kt858jk6jp_p, gm_tplc3kt858jk6jp_i, gm_tplc3kt858jk6jp_s};
const gm_tmpl gm_tplcsb5vnabjejk = {1u, gm_tplcsb5vnabjejk_p, gm_tplcsb5vnabjejk_i, gm_tplcsb5vnabjejk_s};
const gm_tmpl gm_tplcssa18c20t1h = {2u, gm_tplcssa18c20t1h_p, gm_tplcssa18c20t1h_i, gm_tplcssa18c20t1h_s};
const gm_tmpl gm_tpldtecn6n5hb86 = {2u, gm_tpldtecn6n5hb86_p, gm_tpldtecn6n5hb86_i, gm_tpldtecn6n5hb86_s};
const gm_tmpl gm_tple2rp5rj93mkj = {1u, gm_tple2rp5rj93mkj_p, gm_tple2rp5rj93mkj_i, gm_tple2rp5rj93mkj_s};
const gm_tmpl gm_tpleeuqgcr11ngn = {3u, gm_tpleeuqgcr11ngn_p, gm_tpleeuqgcr11ngn_i, gm_tpleeuqgcr11ngn_s};
const gm_tmpl gm_tpleie0q8nvjqch = {1u, gm_tpleie0q8nvjqch_p, gm_tpleie0q8nvjqch_i, gm_tpleie0q8nvjqch_s};
const gm_tmpl gm_tplem02qig8laoi = {1u, gm_tplem02qig8laoi_p, gm_tplem02qig8laoi_i, gm_tplem02qig8laoi_s};
const gm_tmpl gm_tplemb9urr8jmq6 = {2u, gm_tplemb9urr8jmq6_p, gm_tplemb9urr8jmq6_i, gm_tplemb9urr8jmq6_s};
const gm_tmpl gm_tplenatqap1hjoj = {1u, gm_tplenatqap1hjoj_p, gm_tplenatqap1hjoj_i, gm_tplenatqap1hjoj_s};
const gm_tmpl gm_tplfb67h0nat3pe = {1u, gm_tplfb67h0nat3pe_p, gm_tplfb67h0nat3pe_i, gm_tplfb67h0nat3pe_s};
const gm_tmpl gm_tplfjua3t6jsoe6 = {1u, gm_tplfjua3t6jsoe6_p, gm_tplfjua3t6jsoe6_i, gm_tplfjua3t6jsoe6_s};
const gm_tmpl gm_tplfm527r729efa = {1u, gm_tplfm527r729efa_p, gm_tplfm527r729efa_i, gm_tplfm527r729efa_s};
const gm_tmpl gm_tplfsj0kbueolvv = {2u, gm_tplfsj0kbueolvv_p, gm_tplfsj0kbueolvv_i, gm_tplfsj0kbueolvv_s};
const gm_tmpl gm_tplfsk62pc25cdb = {1u, gm_tplfsk62pc25cdb_p, gm_tplfsk62pc25cdb_i, gm_tplfsk62pc25cdb_s};
const gm_tmpl gm_tplgeqcgpvi6n7p = {1u, gm_tplgeqcgpvi6n7p_p, gm_tplgeqcgpvi6n7p_i, gm_tplgeqcgpvi6n7p_s};
const gm_tmpl gm_tplgniq1bprrgpo = {3u, gm_tplgniq1bprrgpo_p, gm_tplgniq1bprrgpo_i, gm_tplgniq1bprrgpo_s};
const gm_tmpl gm_tplgnp9u7qeb3h6 = {2u, gm_tplgnp9u7qeb3h6_p, gm_tplgnp9u7qeb3h6_i, gm_tplgnp9u7qeb3h6_s};
const gm_tmpl gm_tplh8soes50a6ps = {2u, gm_tplh8soes50a6ps_p, gm_tplh8soes50a6ps_i, gm_tplh8soes50a6ps_s};
const gm_tmpl gm_tplhon5g1o0vuqi = {2u, gm_tplhon5g1o0vuqi_p, gm_tplhon5g1o0vuqi_i, gm_tplhon5g1o0vuqi_s};
const gm_tmpl gm_tplhp4k6h527j3k = {3u, gm_tplhp4k6h527j3k_p, gm_tplhp4k6h527j3k_i, gm_tplhp4k6h527j3k_s};
const gm_tmpl gm_tplhrqv5p3pfbh5 = {3u, gm_tplhrqv5p3pfbh5_p, gm_tplhrqv5p3pfbh5_i, gm_tplhrqv5p3pfbh5_s};
const gm_tmpl gm_tplhu5cefj26aau = {1u, gm_tplhu5cefj26aau_p, gm_tplhu5cefj26aau_i, gm_tplhu5cefj26aau_s};
const gm_tmpl gm_tpli2qk88n6tll4 = {1u, gm_tpli2qk88n6tll4_p, gm_tpli2qk88n6tll4_i, gm_tpli2qk88n6tll4_s};
const gm_tmpl gm_tplidajav2hoc6k = {3u, gm_tplidajav2hoc6k_p, gm_tplidajav2hoc6k_i, gm_tplidajav2hoc6k_s};
const gm_tmpl gm_tplidcjkf79g8le = {1u, gm_tplidcjkf79g8le_p, gm_tplidcjkf79g8le_i, gm_tplidcjkf79g8le_s};
const gm_tmpl gm_tplidl0tsmrpak6 = {1u, gm_tplidl0tsmrpak6_p, gm_tplidl0tsmrpak6_i, gm_tplidl0tsmrpak6_s};
const gm_tmpl gm_tplims1qainr636 = {1u, gm_tplims1qainr636_p, gm_tplims1qainr636_i, gm_tplims1qainr636_s};
const gm_tmpl gm_tpliq0iop4ulk1g = {2u, gm_tpliq0iop4ulk1g_p, gm_tpliq0iop4ulk1g_i, gm_tpliq0iop4ulk1g_s};
const gm_tmpl gm_tplj16n1o03849r = {3u, gm_tplj16n1o03849r_p, gm_tplj16n1o03849r_i, gm_tplj16n1o03849r_s};
const gm_tmpl gm_tplj6ss1j7sgouf = {1u, gm_tplj6ss1j7sgouf_p, gm_tplj6ss1j7sgouf_i, gm_tplj6ss1j7sgouf_s};
const gm_tmpl gm_tpljt2av4h7c132 = {1u, gm_tpljt2av4h7c132_p, gm_tpljt2av4h7c132_i, gm_tpljt2av4h7c132_s};
const gm_tmpl gm_tpljv32on6ui3vn = {3u, gm_tpljv32on6ui3vn_p, gm_tpljv32on6ui3vn_i, gm_tpljv32on6ui3vn_s};
const gm_tmpl gm_tplk3nkr2eh6don = {1u, gm_tplk3nkr2eh6don_p, gm_tplk3nkr2eh6don_i, gm_tplk3nkr2eh6don_s};
const gm_tmpl gm_tplk83cf3bpuiqb = {1u, gm_tplk83cf3bpuiqb_p, gm_tplk83cf3bpuiqb_i, gm_tplk83cf3bpuiqb_s};
const gm_tmpl gm_tplkfag0fic60vv = {1u, gm_tplkfag0fic60vv_p, gm_tplkfag0fic60vv_i, gm_tplkfag0fic60vv_s};
const gm_tmpl gm_tpllc6r78fq9cl8 = {8u, gm_tpllc6r78fq9cl8_p, gm_tpllc6r78fq9cl8_i, gm_tpllc6r78fq9cl8_s};
const gm_tmpl gm_tplm01msh3jahhc = {1u, gm_tplm01msh3jahhc_p, gm_tplm01msh3jahhc_i, gm_tplm01msh3jahhc_s};
const gm_tmpl gm_tplm61d8mlvmepm = {1u, gm_tplm61d8mlvmepm_p, gm_tplm61d8mlvmepm_i, gm_tplm61d8mlvmepm_s};
const gm_tmpl gm_tplmj3d4h2pi7q3 = {4u, gm_tplmj3d4h2pi7q3_p, gm_tplmj3d4h2pi7q3_i, gm_tplmj3d4h2pi7q3_s};
const gm_tmpl gm_tpln6vfieiab6l5 = {1u, gm_tpln6vfieiab6l5_p, gm_tpln6vfieiab6l5_i, gm_tpln6vfieiab6l5_s};
const gm_tmpl gm_tplne0l0lciidvr = {2u, gm_tplne0l0lciidvr_p, gm_tplne0l0lciidvr_i, gm_tplne0l0lciidvr_s};
const gm_tmpl gm_tplng1u8s5kl5ir = {1u, gm_tplng1u8s5kl5ir_p, gm_tplng1u8s5kl5ir_i, gm_tplng1u8s5kl5ir_s};
const gm_tmpl gm_tplnokap11rv9jr = {1u, gm_tplnokap11rv9jr_p, gm_tplnokap11rv9jr_i, gm_tplnokap11rv9jr_s};
const gm_tmpl gm_tplns4e3al0r331 = {1u, gm_tplns4e3al0r331_p, gm_tplns4e3al0r331_i, gm_tplns4e3al0r331_s};
const gm_tmpl gm_tploc8iqi9f2el1 = {3u, gm_tploc8iqi9f2el1_p, gm_tploc8iqi9f2el1_i, gm_tploc8iqi9f2el1_s};
const gm_tmpl gm_tploh2305pu3svo = {1u, gm_tploh2305pu3svo_p, gm_tploh2305pu3svo_i, gm_tploh2305pu3svo_s};
const gm_tmpl gm_tplorj2ck0ovl7t = {1u, gm_tplorj2ck0ovl7t_p, gm_tplorj2ck0ovl7t_i, gm_tplorj2ck0ovl7t_s};
const gm_tmpl gm_tplosjvq6nju6rc = {2u, gm_tplosjvq6nju6rc_p, gm_tplosjvq6nju6rc_i, gm_tplosjvq6nju6rc_s};
const gm_tmpl gm_tplotost4gd8bha = {2u, gm_tplotost4gd8bha_p, gm_tplotost4gd8bha_i, gm_tplotost4gd8bha_s};
const gm_tmpl gm_tplp0kc3a3fekq4 = {1u, gm_tplp0kc3a3fekq4_p, gm_tplp0kc3a3fekq4_i, gm_tplp0kc3a3fekq4_s};
const gm_tmpl gm_tplp3ak0q2gramc = {4u, gm_tplp3ak0q2gramc_p, gm_tplp3ak0q2gramc_i, gm_tplp3ak0q2gramc_s};
const gm_tmpl gm_tplp7srseh4nakh = {4u, gm_tplp7srseh4nakh_p, gm_tplp7srseh4nakh_i, gm_tplp7srseh4nakh_s};
const gm_tmpl gm_tplpf3jtoa0oclt = {2u, gm_tplpf3jtoa0oclt_p, gm_tplpf3jtoa0oclt_i, gm_tplpf3jtoa0oclt_s};
const gm_tmpl gm_tplph8v0nbmofpg = {4u, gm_tplph8v0nbmofpg_p, gm_tplph8v0nbmofpg_i, gm_tplph8v0nbmofpg_s};
const gm_tmpl gm_tplpodff9060oeo = {1u, gm_tplpodff9060oeo_p, gm_tplpodff9060oeo_i, gm_tplpodff9060oeo_s};
const gm_tmpl gm_tplpvh6srii84sj = {1u, gm_tplpvh6srii84sj_p, gm_tplpvh6srii84sj_i, gm_tplpvh6srii84sj_s};
const gm_tmpl gm_tplq74l4dgr0l9g = {1u, gm_tplq74l4dgr0l9g_p, gm_tplq74l4dgr0l9g_i, gm_tplq74l4dgr0l9g_s};
const gm_tmpl gm_tplqf2cda88og0i = {2u, gm_tplqf2cda88og0i_p, gm_tplqf2cda88og0i_i, gm_tplqf2cda88og0i_s};
const gm_tmpl gm_tplqrah664loq9e = {2u, gm_tplqrah664loq9e_p, gm_tplqrah664loq9e_i, gm_tplqrah664loq9e_s};
const gm_tmpl gm_tplrcmh11f2ocju = {1u, gm_tplrcmh11f2ocju_p, gm_tplrcmh11f2ocju_i, gm_tplrcmh11f2ocju_s};
const gm_tmpl gm_tplrcqo0t095bsv = {2u, gm_tplrcqo0t095bsv_p, gm_tplrcqo0t095bsv_i, gm_tplrcqo0t095bsv_s};
const gm_tmpl gm_tplrd8stfk49gik = {1u, gm_tplrd8stfk49gik_p, gm_tplrd8stfk49gik_i, gm_tplrd8stfk49gik_s};
const gm_tmpl gm_tpls4d220l28nlk = {1u, gm_tpls4d220l28nlk_p, gm_tpls4d220l28nlk_i, gm_tpls4d220l28nlk_s};
const gm_tmpl gm_tpls5b11sf5gq9o = {1u, gm_tpls5b11sf5gq9o_p, gm_tpls5b11sf5gq9o_i, gm_tpls5b11sf5gq9o_s};
const gm_tmpl gm_tplsbqgr9k4itq0 = {1u, gm_tplsbqgr9k4itq0_p, gm_tplsbqgr9k4itq0_i, gm_tplsbqgr9k4itq0_s};
const gm_tmpl gm_tplsdvt6aocmtuo = {1u, gm_tplsdvt6aocmtuo_p, gm_tplsdvt6aocmtuo_i, gm_tplsdvt6aocmtuo_s};
const gm_tmpl gm_tplsg1jhb9vp948 = {2u, gm_tplsg1jhb9vp948_p, gm_tplsg1jhb9vp948_i, gm_tplsg1jhb9vp948_s};
const gm_tmpl gm_tplshqe0v39oc52 = {1u, gm_tplshqe0v39oc52_p, gm_tplshqe0v39oc52_i, gm_tplshqe0v39oc52_s};
const gm_tmpl gm_tplsiiifs0eedvi = {2u, gm_tplsiiifs0eedvi_p, gm_tplsiiifs0eedvi_i, gm_tplsiiifs0eedvi_s};
const gm_tmpl gm_tplsnmu9a7htqov = {1u, gm_tplsnmu9a7htqov_p, gm_tplsnmu9a7htqov_i, gm_tplsnmu9a7htqov_s};
const gm_tmpl gm_tplto3kksb9sot8 = {1u, gm_tplto3kksb9sot8_p, gm_tplto3kksb9sot8_i, gm_tplto3kksb9sot8_s};
const gm_tmpl gm_tpltvrl7dp7h9m1 = {3u, gm_tpltvrl7dp7h9m1_p, gm_tpltvrl7dp7h9m1_i, gm_tpltvrl7dp7h9m1_s};
const gm_tmpl gm_tplu0n2ra1s30o8 = {2u, gm_tplu0n2ra1s30o8_p, gm_tplu0n2ra1s30o8_i, gm_tplu0n2ra1s30o8_s};
const gm_tmpl gm_tpluc8r0n3peqad = {2u, gm_tpluc8r0n3peqad_p, gm_tpluc8r0n3peqad_i, gm_tpluc8r0n3peqad_s};
const gm_tmpl gm_tpludgprff7jihr = {1u, gm_tpludgprff7jihr_p, gm_tpludgprff7jihr_i, gm_tpludgprff7jihr_s};
const gm_tmpl gm_tplut4ib9ov72oe = {1u, gm_tplut4ib9ov72oe_p, gm_tplut4ib9ov72oe_i, gm_tplut4ib9ov72oe_s};
const gm_tmpl gm_tplvdf3kbq8rjbi = {2u, gm_tplvdf3kbq8rjbi_p, gm_tplvdf3kbq8rjbi_i, gm_tplvdf3kbq8rjbi_s};
const gm_tmpl gm_tplvq03d5skib28 = {1u, gm_tplvq03d5skib28_p, gm_tplvq03d5skib28_i, gm_tplvq03d5skib28_s};
const gm_tmpl gm_tplvqa9kv7qt4oc = {7u, gm_tplvqa9kv7qt4oc_p, gm_tplvqa9kv7qt4oc_i, gm_tplvqa9kv7qt4oc_s};
const gm_tmpl gm_tplvvfpp9olf6cl = {2u, gm_tplvvfpp9olf6cl_p, gm_tplvvfpp9olf6cl_i, gm_tplvvfpp9olf6cl_s};
const gm_tmpl gm_tplvvic2bq8a97l = {2u, gm_tplvvic2bq8a97l_p, gm_tplvvic2bq8a97l_i, gm_tplvvic2bq8a97l_s};
const gm_tmpl_piece gm_tpl005eoei82778_p[] = {{gm_sluiboi2c0d5n, 57}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl083ga8282tol_p[] = {{gm_svirf1u8t89n0, 46}, {gm_sp02qqgucmpt7, 2}, {gm_s6sv4vhunlp6l, 8}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl0h84r2u2heg0_p[] = {{gm_stpboephdcpba, 13}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tpl149k5asebpko_p[] = {{gm_skhcthij717g7, 10}, {gm_s5r5k5b2k7bru, 19}};
const gm_tmpl_piece gm_tpl1f7c1o805fpf_p[] = {{gm_smooaouqmm8dj, 1}, {gm_sgdk0atob5av0, 5}};
const gm_tmpl_piece gm_tpl1gtefnja08ps_p[] = {{gm_sc103nruhmor7, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl1jgrbdjt2sm3_p[] = {{gm_s80t4sc5bf88n, 81}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl1o2hk14gjjj0_p[] = {{gm_s0ghv8aql4j5t, 18}, {gm_s9co2b6h6nhos, 15}};
const gm_tmpl_piece gm_tpl1u3j3qlmtjj2_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s0pi0emdqd3vb, 1}, {gm_s0pi0emdqd3vb, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl2d7ij9b2sa2u_p[] = {{gm_s66fbu3uhl900, 19}, {gm_s3qu37je1jc8f, 45}};
const gm_tmpl_piece gm_tpl2qal2i0dmdum_p[] = {{gm_saiq60vdu8ckv, 9}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl31lgghh3mal3_p[] = {{gm_s1bpuqd44lur7, 6}, {gm_sg5gdnrith2i6, 2}, {gm_s0m0qaokk51g0, 3}, {gm_snkldl7hr4htt, 3}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpl3melp777ubs8_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sh0j2ggs1vvoa, 1}};
const gm_tmpl_piece gm_tpl3tkekujl97kv_p[] = {{gm_s1tqe3mtj6vb1, 7}, {gm_s8kr43nig4sns, 7}};
const gm_tmpl_piece gm_tpl3ttr7njhqa54_p[] = {{gm_s7ipvt5onv6ht, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl44cjgoji4tlu_p[] = {{gm_sc21u906vi2gp, 71}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl4mli0f9rjihf_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_scrnhm4ha22vj, 17}, {gm_sq4jcg2uujqmj, 15}, {gm_sfm9elkq6bn2e, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl5cnv021ebod7_p[] = {{gm_st943craifob3, 30}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpl5i8vi7ph4hfi_p[] = {{gm_s8a7jvbqoerpn, 20}, {gm_se47039ecu2b8, 9}, {gm_sikpqtja8pqpv, 2}};
const gm_tmpl_piece gm_tpl5jb9rn2u95g6_p[] = {{gm_siamquc19uikr, 1}, {gm_so77ks9ivg559, 8}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpl5vbp9fc0mpc1_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s7mmjn8cgbhv8, 66}};
const gm_tmpl_piece gm_tpl6fm59pne5ng7_p[] = {{gm_smooaouqmm8dj, 1}, {gm_se1krm9tn2oaa, 2}};
const gm_tmpl_piece gm_tpl6jo4mmkdq2l1_p[] = {{gm_sq9i3vfk7nqs8, 9}, {gm_so514oj5nbhcu, 1}};
const gm_tmpl_piece gm_tpl6q4b2p6tude6_p[] = {{gm_shakeh6dfb84i, 16}, {gm_sf4qi8ut6u3p1, 4}};
const gm_tmpl_piece gm_tpl6vfpiq020ehi_p[] = {{gm_sf46mekb18qm8, 13}, {gm_s3rph20cdvn0u, 15}, {gm_sv150rl18b4qa, 3}};
const gm_tmpl_piece gm_tpl7b1ibjg22p4i_p[] = {{gm_sltjg0sbhbo73, 12}, {gm_s4k57chtk2mkc, 87}};
const gm_tmpl_piece gm_tpl7hvv6i3t3o0k_p[] = {{gm_s1e7k26kdaoj3, 30}, {gm_s23t712hkqlmp, 4}, {gm_sh8l62ma11hq5, 69}};
const gm_tmpl_piece gm_tpl80avf3gokjno_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s0j2j9kdc2obh, 16}, {gm_s0mph2pjoa3np, 16}, {gm_sjnkguo0atpm5, 2}};
const gm_tmpl_piece gm_tpl86cip41cbf58_p[] = {{gm_snh0sl2cdagdn, 12}, {gm_s1isj7vodtbvg, 36}};
const gm_tmpl_piece gm_tpl91b7sjgnmine_p[] = {{gm_sn5sjb95plsev, 26}, {gm_sh4ufq9bak51r, 12}, {gm_s9kj8vgi6430a, 5}, {gm_s6t4r831mukdr, 2}};
const gm_tmpl_piece gm_tpl9d1siq0c9pir_p[] = {{gm_s0doambt0iqhe, 3}, {gm_s2cuflhqkkuaq, 1}};
const gm_tmpl_piece gm_tpl9s09hqjpb581_p[] = {{gm_skbblqa84g6pp, 56}, {gm_s967vts9f19o3, 10}, {gm_stbjrsoqc456r, 11}, {gm_snvodad5t43r7, 10}, {gm_s967vts9f19o3, 10}, {gm_stbjrsoqc456r, 11}, {gm_si6sr5gqkcui7, 5}};
const gm_tmpl_piece gm_tpl9va1ubk8bean_p[] = {{gm_s4gh6uvc8i8um, 23}, {gm_s9tnrhv72lohp, 7}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplagt25ql42kpb_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_sebojpbfjfgaf, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplaoj5r4tauejk_p[] = {{gm_s6clc0pmaeksj, 17}, {gm_s8m850dccae8f, 87}};
const gm_tmpl_piece gm_tplav8bdvvt8rem_p[] = {{gm_s9pqiujjjjsta, 54}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplbmf4neusqbo5_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s39mljh669qgl, 1}, {gm_slgmqgkioa4u6, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplbnt69qpq91i8_p[] = {{gm_s1tqe3mtj6vb1, 7}, {gm_skiv4voi8bqij, 19}};
const gm_tmpl_piece gm_tplc0jhs849b438_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_snqevmbeuqcmu, 9}};
const gm_tmpl_piece gm_tplc3kt858jk6jp_p[] = {{gm_srne5d503fcjg, 4}, {gm_s7atji10aaq5k, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplc5cre0uqposd_p[] = {{gm_s68rktnu8slfa, 24}, {gm_sg5gdnrith2i6, 2}, {gm_snr5o5tak6405, 3}};
const gm_tmpl_piece gm_tplc6hhdd7vcmvk_p[] = {{gm_sgmhc3klfmn46, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplc9tn3pkjdpf4_p[] = {{gm_st5bsv3b3beg9, 21}, {gm_sueo2nge7h013, 13}, {gm_subcm6amchfvv, 4}};
const gm_tmpl_piece gm_tplctspgtksm35f_p[] = {{gm_s885vckaljhqv, 2}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplcudcaj1iluqd_p[] = {{gm_smooaouqmm8dj, 1}, {gm_spmkn6krbjnjh, 9}, {gm_shnpduonp99d0, 11}};
const gm_tmpl_piece gm_tpldofkl5m8hqug_p[] = {{gm_slq58vfha8k9h, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpldvbj2judb6g5_p[] = {{gm_som0q53jqvjh3, 4}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tple7msnncsq0hr_p[] = {{gm_s03c24qg7i06q, 12}, {gm_sqdfr4k3vea5p, 3}, {gm_s00989rdgst9u, 19}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpleb7ph7fjnlbm_p[] = {{gm_sqc2or5f1575b, 30}, {gm_sg5gdnrith2i6, 2}, {gm_sqvepl1893o5g, 2}};
const gm_tmpl_piece gm_tplesi2enviniss_p[] = {{gm_sauf6ibu7mnba, 4}, {gm_s3e9133b3pasn, 2}, {gm_sp4skfj2bn2ut, 6}, {gm_sujipgkhb2fih, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplf9q60in6m019_p[] = {{gm_s16mf4cvajh59, 12}, {gm_s75m3dt9015lh, 14}, {gm_stddp43pjd9r1, 50}};
const gm_tmpl_piece gm_tplfjua3t6jsoe6_p[] = {{gm_sjkj5g78lejrj, 3}, {gm_s26crtotu14pn, 2}};
const gm_tmpl_piece gm_tplfsstr25ojml5_p[] = {{gm_s66fbu3uhl900, 19}, {gm_secneq49c8dtf, 35}};
const gm_tmpl_piece gm_tplftchh41b473r_p[] = {{gm_sd9ri1vmgds82, 8}, {gm_svth7kg659g2i, 12}, {gm_s6m55kvvtug5k, 11}, {gm_sg5gdnrith2i6, 2}, {gm_sbtq9nj91tefv, 9}, {gm_sgfqei77bsia8, 3}, {gm_s3loluab89lqt, 10}, {gm_snukl6jqep6jf, 3}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplfv85d3oogq60_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s4m9nol66dh0a, 24}};
const gm_tmpl_piece gm_tplgqr018o660ue_p[] = {{gm_s7ilf06vf0fi1, 21}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplhksbien67lqe_p[] = {{gm_s1spbnihet3ou, 24}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplhphg08jcb4qr_p[] = {{gm_s1smed2pig5ts, 34}, {gm_sb7hastj3mkf0, 35}};
const gm_tmpl_piece gm_tpli0tmonaamd45_p[] = {{gm_s0m9d22aen6f8, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplijibrlknrr0j_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sdib10lkj65nm, 44}};
const gm_tmpl_piece gm_tplj0vaqlfn7orc_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s4jh2uktkqkro, 21}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplj8iadiucj6so_p[] = {{gm_src0qvon2jl38, 27}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpljdnrgbsps94k_p[] = {{gm_sdark9dtsgt1q, 40}, {gm_sog9gpimub31i, 6}, {gm_sr8dujibvqj2g, 81}};
const gm_tmpl_piece gm_tpljm6mn1j3nrs6_p[] = {{gm_sb54ui2amic32, 1}, {gm_sdpl6uk3q4cch, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplkaran1m5lpqm_p[] = {{gm_stm8v8ngvll3q, 15}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplkp06s34ois53_p[] = {{gm_sltjg0sbhbo73, 12}, {gm_skrdl5pdcve8g, 42}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplkpmnsso79v4r_p[] = {{gm_sc4r4l9k6f577, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplkte26oe5429d_p[] = {{gm_sgaj4glha8q4e, 2}, {gm_sm5be4659ulci, 1}};
const gm_tmpl_piece gm_tplkun0h8smcrq9_p[] = {{gm_sj3ehailc2pgo, 51}, {gm_sp3k6emq42nh4, 9}};
const gm_tmpl_piece gm_tplkvqpieru7v5v_p[] = {{gm_stmsadvf54k73, 2}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpllinpkb0ktkcf_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_smooaouqmm8dj, 1}, {gm_soiad5r9c65ru, 36}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tpllovel7a2ips1_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sfhcr4tad3e77, 84}, {gm_s512m3mlvu0cv, 105}};
const gm_tmpl_piece gm_tpllv3q9mfan4rg_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s7cn2q9qfeafa, 78}};
const gm_tmpl_piece gm_tplm3qlt5n96af0_p[] = {{gm_svsf0pu3t8u6m, 6}, {gm_s3p0rbs9hc3lg, 1}, {gm_sp02qqgucmpt7, 2}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplmghq7vvcvnvi_p[] = {{gm_srcl5693p5ru5, 16}, {gm_sma6ellkcmke6, 25}};
const gm_tmpl_piece gm_tplmrpf5m9qkka0_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_svutc5hf6cgiv, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpln2j7s58m8uan_p[] = {{gm_sktf16uersaon, 8}, {gm_s3p0rbs9hc3lg, 1}, {gm_sr4qbrk6tlgaa, 0}, {gm_srefpimq1sas7, 3}, {gm_sno069vrt5g9q, 3}, {gm_s6t4r831mukdr, 2}};
const gm_tmpl_piece gm_tpln8ctpo9hror7_p[] = {{gm_s1af5e7vkvjv9, 7}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplneb8585d50a4_p[] = {{gm_s51lfi73niqcd, 61}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplnoi44u4met07_p[] = {{gm_s14bn21gpuj3j, 2}, {gm_slc58j6a4e39a, 55}, {gm_sphl918t1t6qp, 10}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplnqad0r40bfej_p[] = {{gm_s1bpuqd44lur7, 6}, {gm_sd2mrmucl6ngt, 6}};
const gm_tmpl_piece gm_tplntci9ovg2hdj_p[] = {{gm_svsf0pu3t8u6m, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplnv17naljkeai_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sjnv3p8t49clu, 8}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplo2l8dmp437h2_p[] = {{gm_sqssndasqkc4l, 16}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplo8vr0jo68llo_p[] = {{gm_sgfbh9novfe35, 6}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplod1bbc3c9s5j_p[] = {{gm_s03c24qg7i06q, 12}, {gm_sr23o038u76nd, 12}, {gm_sp7cdd0n91vli, 7}, {gm_spaa4sseck2fg, 44}};
const gm_tmpl_piece gm_tplodcipjau6eqr_p[] = {{gm_so9fi6r28770a, 2}, {gm_s08dcqdp58r5f, 2}, {gm_s6ocl6n3lno8m, 7}};
const gm_tmpl_piece gm_tplof36cm9vqs8e_p[] = {{gm_sk09uplg1v4qc, 15}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_sg5gdnrith2i6, 2}, {gm_s4a980fahtblo, 2}};
const gm_tmpl_piece gm_tplpfavv7btb1m5_p[] = {{gm_sns1hl83hdlcn, 4}, {gm_svnpflkpvd513, 4}, {gm_siscc5ella8ma, 3}};
const gm_tmpl_piece gm_tplplkqcb01ocrd_p[] = {{gm_s8kki44ucv73v, 10}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplpnk24geo6qd8_p[] = {{gm_si7fe9i76cdgb, 12}, {gm_sr4qbrk6tlgaa, 0}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplptgaia3s19c9_p[] = {{gm_sqa29afvhov50, 5}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplq7kai16vns6p_p[] = {{gm_s34hhp1ucrags, 11}, {gm_shi7nidbc64qf, 3}, {gm_sao4mumvadoct, 3}, {gm_spod3tmkh25up, 1}};
const gm_tmpl_piece gm_tplqjfk6t5rn06e_p[] = {{gm_shmdne5cle16i, 7}, {gm_sl3q52ep6aa53, 1}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplqtueduh1r3n9_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_s8b49h6svih4r, 81}, {gm_skl3cimaihkgu, 25}, {gm_skl8iadfmm1va, 43}, {gm_sq05ai6ts8qlk, 16}};
const gm_tmpl_piece gm_tplrp4haaemeqja_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_s2u5posfkbgnq, 1}, {gm_s2u5posfkbgnq, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplrveq8m992ml7_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_snukl6jqep6jf, 3}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpls7s61lfbg9l6_p[] = {{gm_s5lqhiu2bn6of, 7}, {gm_sl23n0qkt3j4u, 27}};
const gm_tmpl_piece gm_tplsogg2sp0m9p9_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_st3ja0nqi37bv, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplspgpn4qu6rd4_p[] = {{gm_shbtdicfd8lcc, 11}, {gm_srhs1fn12neea, 32}};
const gm_tmpl_piece gm_tplss5b4q9est2j_p[] = {{gm_su66bdcms0hot, 27}, {gm_shtvnkmjp804q, 22}, {gm_sjh6cl9hs66k6, 36}, {gm_sqhqlj2oc6h5k, 19}, {gm_s2cartb32o2of, 9}, {gm_slp60pbhoot2j, 14}, {gm_svl5ddjf64ul7, 17}, {gm_s3hb2q98sfsao, 4}};
const gm_tmpl_piece gm_tplt1lu36t604pk_p[] = {{gm_s66ph0gfa95db, 13}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tplt320ru9bfpdd_p[] = {{gm_shhugap3t1rkg, 40}, {gm_s87h4fof1n9ou, 10}};
const gm_tmpl_piece gm_tplthb0oue219bq_p[] = {{gm_sr4qbrk6tlgaa, 0}, {gm_seccpktgmrevt, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpltktipp7m43tk_p[] = {{gm_s1tdqlhdsnu5l, 13}, {gm_s8q575q0mjt4k, 5}, {gm_s3e9133b3pasn, 2}, {gm_s332n2u4obj9h, 48}};
const gm_tmpl_piece gm_tpltm9r5n97je13_p[] = {{gm_s6vmoui2ivq06, 9}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tpltue438lnncbq_p[] = {{gm_s0iditmthmvpu, 20}, {gm_sjm54r6h90i7e, 62}};
const gm_tmpl_piece gm_tplu3lqf0bugia3_p[] = {{gm_s02aodrl2fpra, 11}, {gm_s7nnecs6i9eki, 40}};
const gm_tmpl_piece gm_tplubod389iot6h_p[] = {{gm_sbu7knoujlojo, 55}, {gm_sfm9elkq6bn2e, 1}};
const gm_tmpl_piece gm_tpluc9sbr66n218_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_ssr6vk8erbj18, 12}, {gm_sv3gqbo7rudsj, 12}, {gm_s35u3nlsc2qa0, 26}};
const gm_tmpl_piece gm_tpludqpd0ngjfu6_p[] = {{gm_s1tqe3mtj6vb1, 7}, {gm_srefpimq1sas7, 3}, {gm_s77fi68iu5q6n, 4}};
const gm_tmpl_piece gm_tpluduaa40ad285_p[] = {{gm_siamquc19uikr, 1}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplup0bf5d2eopf_p[] = {{gm_st547kc8a6rg5, 15}, {gm_ssjhh9bdn23jf, 16}};
const gm_tmpl_piece gm_tplv1i7e9gejv6a_p[] = {{gm_smooaouqmm8dj, 1}, {gm_svhavo13qha5k, 3}, {gm_s1asl1e1tvb0k, 1}, {gm_s9tdiht68h68o, 3}, {gm_s1asl1e1tvb0k, 1}};
const gm_tmpl_piece gm_tplvflfleui9qjm_p[] = {{gm_so9fi6r28770a, 2}, {gm_slvvpj912a5bg, 3}};
const gm_tmpl_piece gm_tplviqo6u6r0sr3_p[] = {{gm_smooaouqmm8dj, 1}, {gm_sq3jr80otu7tj, 2}, {gm_sr4qbrk6tlgaa, 0}};
const gm_tmpl_piece gm_tplvuahs93lrft8_p[] = {{gm_sfm9elkq6bn2e, 1}, {gm_sj36eemfvep3a, 55}, {gm_svbftsm94r0bv, 6}};
const gm_tmpl_piece gm_tplvvic2bq8a97l_p[] = {{gm_sicq1bvp13t5d, 22}, {gm_s58ndvsqjhesf, 5}, {gm_s98a5ud3vqhvr, 84}};
const gm_type gm_tybqb4gb1d92 = {.size = 2, .align = 1, .name = "BinOp", .kind = GM_K_UNION, .nvariants = 14, .variants = gm_tybqb4gb1d92_v, .tag_offset = 0, .payload_offset = 1, .tag_size = 1, .bits = 8, .tag_signed = false};
const gm_type gm_tybqb4gb1d92;
const gm_type gm_tylv5i3f25jk = {.size = 1, .align = 1, .name = "UnOp", .kind = GM_K_UNION, .nvariants = 3, .variants = gm_tylv5i3f25jk_v, .tag_offset = 0, .payload_offset = 0, .tag_size = 1, .bits = 8, .tag_signed = false};
const gm_type gm_tylv5i3f25jk;
const gm_variant gm_tycmo75g2ssd_v[] = {{"IntWiden", 0, NULL}, {"FloatWiden", 1, NULL}, {"WrapOptional", 2, NULL}, {"WrapOk", 3, NULL}, {"LiftErr", 4, &gm_ty467d5cm1d5}, {"FromNever", 5, NULL}, {"PtrToPtr", 6, NULL}, {"ArrayPtrToSlice", 7, NULL}, {"TupleRename", 8, NULL}, {"FnToPtr", 9, NULL}, {"FnPtrWiden", 10, NULL}, {"InOptional", 11, NULL}, {"Unresolved", 12, NULL}};
const gm_variant gm_tytje7cegoml_v[] = {{"Read", 0, NULL}, {"ClockNow", 1, NULL}, {"ClockWall", 2, NULL}, {"Sleep", 3, NULL}, {"RandFill", 4, NULL}, {"FsOpen", 5, NULL}, {"FsClose", 6, NULL}, {"FsRead", 7, NULL}, {"FsWrite", 8, NULL}, {"FsSeek", 9, NULL}, {"FsStat", 10, NULL}, {"FsRemove", 11, NULL}, {"FsRename", 12, NULL}, {"FsMakeDir", 13, NULL}, {"FsRemoveDir", 14, NULL}, {"FsOpenDir", 15, NULL}, {"FsNextEntry", 16, NULL}, {"FsCloseDir", 17, NULL}, {"FsLstat", 18, NULL}, {"FsTruncate", 19, NULL}, {"FsSync", 20, NULL}, {"FsCwd", 21, NULL}, {"FsCanonical", 22, NULL}, {"FsReadLink", 23, NULL}, {"FsSymlink", 24, NULL}, {"FsSetMode", 25, NULL}, {"FsSetModified", 26, NULL}, {"NetConnect", 27, NULL}, {"NetListen", 28, NULL}, {"NetLocalPort", 29, NULL}, {"NetAccept", 30, NULL}, {"NetSend", 31, NULL}, {"NetRecv", 32, NULL}, {"NetClose", 33, NULL}, {"NetShutdown", 34, NULL}, {"NetSetTimeouts", 35, NULL}, {"NetPeer", 36, NULL}, {"ProcLaunch", 37, NULL}, {"ProcWriteIn", 38, NULL}, {"ProcCloseIn", 39, NULL}, {"ProcReadOut", 40, NULL}, {"ProcWait", 41, NULL}, {"ProcKill", 42, NULL}, {"ProcTryWait", 43, NULL}, {"EnvArgs", 44, NULL}, {"EnvVar", 45, NULL}, {"EnvExit", 46, NULL}, {"ThreadSpawn", 47, NULL}, {"ThreadJoin", 48, NULL}, {"ThreadDetach", 49, NULL}, {"ThreadYield", 50, NULL}, {"FutexWait", 51, NULL}, {"FutexWake", 52, NULL}, {"CpuCount", 53, NULL}, {"SysWrite", 54, NULL}};
const uint32_t gm_tpl07mnbdjjl9l9_i[] = {0u, 1u};
const uint32_t gm_tpl086m1ajg247c_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl0gp8c85b2lf5_i[] = {0u};
const uint32_t gm_tpl150n44984e0j_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl16elqfq3d1nr_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tpl1c0b1s109fa4_i[] = {0u, 1u};
const uint32_t gm_tpl1f7c1o805fpf_i[] = {0u};
const uint32_t gm_tpl29spsi1atllj_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl2kgmgv2ueqo4_i[] = {0u, 1u};
const uint32_t gm_tpl2lqr5lnbpsvm_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl328oas8hh4jg_i[] = {0u, 1u};
const uint32_t gm_tpl355ags9581ob_i[] = {0u, 1u};
const uint32_t gm_tpl3t5edr01qp5c_i[] = {0u, 1u};
const uint32_t gm_tpl409di1gnsfit_i[] = {0u, 1u};
const uint32_t gm_tpl44cjgoji4tlu_i[] = {0u};
const uint32_t gm_tpl499l2fj2qs8e_i[] = {0u};
const uint32_t gm_tpl4bpbmadpmu1i_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl4tk9leh8nue4_i[] = {0u, 1u};
const uint32_t gm_tpl5q9oi9aqgs43_i[] = {0u};
const uint32_t gm_tpl5utcgga3g5km_i[] = {0u, 1u};
const uint32_t gm_tpl64qrh372lslh_i[] = {0u, 1u, 2u};
const uint32_t gm_tpl6boglt2rkmh4_i[] = {0u};
const uint32_t gm_tpl7hvv6i3t3o0k_i[] = {0u, 1u};
const uint32_t gm_tpl86i0s811rrlp_i[] = {0u};
const uint32_t gm_tpl88ij4e6nvk4u_i[] = {0u};
const uint32_t gm_tpl97n87h3pcva1_i[] = {0u};
const uint32_t gm_tpl9900stiqclaj_i[] = {0u};
const uint32_t gm_tpl9bim66fl3b37_i[] = {0u, 1u};
const uint32_t gm_tpl9e18q8to9md2_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tpla8nvlakostko_i[] = {0u};
const uint32_t gm_tplacb81m1n6g9e_i[] = {0u, 1u};
const uint32_t gm_tplad75mid9f0qe_i[] = {0u};
const uint32_t gm_tplav8bdvvt8rem_i[] = {0u};
const uint32_t gm_tplb05kqqtmbu7v_i[] = {0u, 1u, 2u};
const uint32_t gm_tplb86aokjj8d2b_i[] = {0u};
const uint32_t gm_tplbog2sc0rf3ot_i[] = {0u};
const uint32_t gm_tplceemmmm7iuq6_i[] = {0u, 1u};
const uint32_t gm_tplck20jih4okbk_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u};
const uint32_t gm_tplcpp8u5i709mj_i[] = {0u, 1u, 2u};
const uint32_t gm_tplcpt7hf5iscqd_i[] = {0u};
const uint32_t gm_tplcsb5vnabjejk_i[] = {0u};
const uint32_t gm_tpldftt3jdrahlt_i[] = {0u, 1u};
const uint32_t gm_tpldhmlf1n8qcdi_i[] = {0u, 1u, 2u};
const uint32_t gm_tpldrlq7uo0cbrm_i[] = {0u};
const uint32_t gm_tpldtv4ooqtkvgm_i[] = {0u};
const uint32_t gm_tple6e96jcr39fk_i[] = {0u, 1u};
const uint32_t gm_tple6jpca094i2j_i[] = {0u, 1u};
const uint32_t gm_tpleiobdo4rm4hs_i[] = {0u, 1u};
const uint32_t gm_tpleiompgmn2730_i[] = {0u};
const uint32_t gm_tpleo7934k1qvrc_i[] = {0u, 1u};
const uint32_t gm_tplf2ivserdf4ic_i[] = {0u, 1u};
const uint32_t gm_tplf3c2iulsht2e_i[] = {0u};
const uint32_t gm_tplfe53mjoakni4_i[] = {0u};
const uint32_t gm_tplff85tslj1ph5_i[] = {0u};
const uint32_t gm_tplfjoq6lkhl94q_i[] = {0u, 1u, 2u};
const uint32_t gm_tplfr9bfp6t7vb7_i[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u};
const uint32_t gm_tplfsrd1mgov7nd_i[] = {0u, 1u, 2u};
const uint32_t gm_tplfv85d3oogq60_i[] = {0u};
const uint32_t gm_tplg05c24rh8peq_i[] = {0u};
const uint32_t gm_tplg3qra7vq6h3a_i[] = {0u};
const uint32_t gm_tplgeqcgpvi6n7p_i[] = {0u};
const uint32_t gm_tplgqr018o660ue_i[] = {0u};
const uint32_t gm_tplgt6vkfjdh8cd_i[] = {0u};
const uint32_t gm_tplhrko1gs9n6tk_i[] = {0u};
const uint32_t gm_tpli824iv2hf4o6_i[] = {0u, 1u, 2u, 3u, 4u};
const uint32_t gm_tpliqpvgtpo05sn_i[] = {0u, 1u, 2u};
const uint32_t gm_tpljcgfng3gohsk_i[] = {0u, 1u};
const uint32_t gm_tpljcso3h5g4o29_i[] = {0u};
const uint32_t gm_tplk1keglepka61_i[] = {0u, 1u, 2u};
const uint32_t gm_tplkik9smkp1eb6_i[] = {0u};
const uint32_t gm_tplkr4062p8a8bi_i[] = {0u, 1u, 2u};
const uint32_t gm_tplkun0h8smcrq9_i[] = {0u};
const uint32_t gm_tpllaurt6qemv8q_i[] = {0u};
const uint32_t gm_tpllf7nufk7lvkm_i[] = {0u, 1u};
const uint32_t gm_tpllihoeldkkkhv_i[] = {0u};
const uint32_t gm_tpllinpkb0ktkcf_i[] = {0u, 1u, 2u};
const uint32_t gm_tplm1fuloaqnfv4_i[] = {0u, 1u, 2u};
const uint32_t gm_tplmsb1klg4n6ap_i[] = {0u};
const uint32_t gm_tplmueh9mdvaqub_i[] = {0u};
const uint32_t gm_tpln2i759e19cgt_i[] = {0u};
const uint32_t gm_tplncn0njcasr6v_i[] = {0u, 1u, 2u};
const uint32_t gm_tplnmm43n0jmcod_i[] = {0u};
const uint32_t gm_tplnr3ridveqobn_i[] = {0u};
const uint32_t gm_tplo8vr0jo68llo_i[] = {0u};
const uint32_t gm_tploc41q7b5gurn_i[] = {0u, 1u, 2u};
const uint32_t gm_tplok9pv33vcsg0_i[] = {0u};
const uint32_t gm_tplokoueqaolfmt_i[] = {0u};
const uint32_t gm_tplomrgvu31uqof_i[] = {0u};
const uint32_t gm_tplp7gdl2lerimh_i[] = {0u};
const uint32_t gm_tplptk7v9d6qojh_i[] = {0u};
const uint32_t gm_tplq74l4dgr0l9g_i[] = {0u};
const uint32_t gm_tplr56tn8lpufgs_i[] = {0u, 1u};
const uint32_t gm_tplrcqo0t095bsv_i[] = {0u, 1u};
const uint32_t gm_tplrd8stfk49gik_i[] = {0u};
const uint32_t gm_tplrp7ukmgscavj_i[] = {0u, 1u, 2u};
const uint32_t gm_tplrsu0pn6pbour_i[] = {0u, 1u};
const uint32_t gm_tplru5t7oahrjah_i[] = {0u};
const uint32_t gm_tpls4bcac20qnq6_i[] = {0u};
const uint32_t gm_tplshqe0v39oc52_i[] = {0u};
const uint32_t gm_tplsleqso6g0tjq_i[] = {0u};
const uint32_t gm_tplso413blsqnmm_i[] = {0u, 1u, 2u};
const uint32_t gm_tplt0hjq4nb3jer_i[] = {0u};
const uint32_t gm_tpltev7cklsieaq_i[] = {0u, 1u, 2u};
const uint32_t gm_tpltl0rfkesf8ms_i[] = {0u, 1u};
const uint32_t gm_tplu3lqf0bugia3_i[] = {0u};
const uint32_t gm_tplu7gq2cm8aanh_i[] = {0u};
const uint32_t gm_tpluc8r0n3peqad_i[] = {0u, 1u};
const uint32_t gm_tplue2kdcikgcof_i[] = {0u, 1u, 2u, 3u};
const uint32_t gm_tpluuj281d42vhc_i[] = {0u};
const uint32_t gm_tplv1shkj7l87mc_i[] = {0u};
const uint32_t gm_tplv23k7silrvnp_i[] = {0u};
const uint32_t gm_tplv3jh8gpmh6o5_i[] = {0u};
const uint32_t gm_tplvftnuchaenfj_i[] = {0u};
const uint32_t gm_tplvsndsjoh0e6u_i[] = {0u};
const uint8_t gm_s00046u02oar6[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000Q\001\000\0005\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_s01ofvj3chukm[10] = " of `Env`";
const uint8_t gm_s04qus0luoga5[5] = "NULL";
const uint8_t gm_s05336tfii06a[18] = " exceeds u64::MAX";
const uint8_t gm_s078u1763sqk3[11] = ") has none";
const uint8_t gm_s07uhsrd798kj[9] = "effects ";
const uint8_t gm_s0adesbq24e9u[6] = ")) { ";
const uint8_t gm_s0cf71ouneh5m[12] = "gm_fs_close";
const uint8_t gm_s0cpkv7s4ef85[24] = "bswap of a partial byte";
const uint8_t gm_s0e9ru2ha5ibt[103] = "a pointer has no address at compile time: `@hash` of a value that holds one is not a constant (\302\24716.8)";
const uint8_t gm_s0ggo8ek7kfrt[34] = "a constant that was not evaluated";
const uint8_t gm_s0jtk73jm5ft9[16] = "registerOptions";
const uint8_t gm_s0lrfrtvffhu7[4] = "NaN";
const uint8_t gm_s0notp50gncsl[12] = "gm_net_init";
const uint8_t gm_s0odi4lbspe7f[25] = "EOF while parsing a list";
const uint8_t gm_s0qbjm15aot6s[11] = ", (void *)";
const uint8_t gm_s0rsc8ficqfcm[78] = "cannot rename `{}` to `{}`: `{}` is already declared in a scope that overlaps";
const uint8_t gm_s0se3p14t5670[14] = "lib/gamma/std";
const uint8_t gm_s108thc59vc7u[37] = "index {} out of bounds for length {}";
const uint8_t gm_s11lbv9jc177b[4] = "hex";
const uint8_t gm_s129prqlnb2kt[41] = "unexpected character {} while parsing {}";
const uint8_t gm_s148evlodt315[18] = ")` has no bound: ";
const uint8_t gm_s15c9j2vv1d2a[39] = "the fields of this `#bits` struct use ";
const uint8_t gm_s15p9kmrsfogp[2] = "p";
const uint8_t gm_s1ctr86inn5iu[24] = "the C compiler failed:\012";
const uint8_t gm_s1dc9boe5unhg[32] = "this `break` gives a `with` a `";
const uint8_t gm_s1gm43jp1jqr0[89] = " a;\012    GM_ST(void *, (uint8_t *)&a, (void *)(d));\012    GM_ST(uintptr_t, (uint8_t *)&a + ";
const uint8_t gm_s1jdtclvlju06[9] = "fn {} {}";
const uint8_t gm_s1leke9tn7lrm[8] = "`match`";
const uint8_t gm_s1livc5021kui[9] = "parse_if";
const uint8_t gm_s1lj31vo6lvqe[26] = "field `{}` is given twice";
const uint8_t gm_s1lnh4indnr62[4] = "{}.";
const uint8_t gm_s1rdbiku62o8o[41] = "\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000\300\000\000\0005\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_s1suv7j29n0ea[21] = " at a place of type ";
const uint8_t gm_s1t5armttgi3e[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\350\000\000\0006\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000";
const uint8_t gm_s211k6darac91[10] = "byte_text";
const uint8_t gm_s25h5btu4ug6a[12] = "{} {} = {};";
const uint8_t gm_s29jv7rttsnpq[52] = ".from_tag` needs an enum (a union without payloads)";
const uint8_t gm_s2hi6cc9n75l5[7] = "gm_lad";
const uint8_t gm_s2hut6059od59[42] = "cannot infer the union type of `.{}` here";
const uint8_t gm_s2j8t1jqamft8[5] = "url+";
const uint8_t gm_s2js1cb9eproh[41] = "\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000.\003\000\000\020\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_s2k3mi7crs28d[5] = "goto";
const uint8_t gm_s2nf76g2su2k3[15] = "builtin method";
const uint8_t gm_s2nn3moc8tfri[17] = "    gm_start();\012";
const uint8_t gm_s2nq2i5ut1dni[13] = "fmt_shortest";
const uint8_t gm_s2of8ntot1on0[81] = "a function with a rest parameter takes a spread only as its whole rest arguments";
const uint8_t gm_s2tnbv3fg2r22[13] = " tg = GM_LD(";
const uint8_t gm_s2u46tincrqmv[25] = "compiler/lsp/analysis.gm";
const uint8_t gm_s2u7e0bju5qb8[28] = " = __builtin_shufflevector(";
const uint8_t gm_s336v1oga58d4[41] = "`{}` does not apply to complex constants";
const uint8_t gm_s33pl04oikskr[5] = "<<|=";
const uint8_t gm_s38tgoanp9o78[66] = "bool evok{} = gm_env_var((const uint8_t *){}, {}, &ev{}, &evn{});";
const uint8_t gm_s3c7ikv4cpqg1[13] = "Slice { lo: ";
const uint8_t gm_s3l29dojrpo6a[12] = "write_table";
const uint8_t gm_s3lovgpoph0mh[9] = "prefetch";
const uint8_t gm_s3o8vm6p6efhi[21] = "dependencies = [{}]\012";
const uint8_t gm_s3on77ql0ndko[17] = "a float, for `e`";
const uint8_t gm_s3op5s1agh0ju[35] = "`--run-test` needs a number, not `";
const uint8_t gm_s3sb6rrf98gpi[20] = " is not valid UTF-8";
const uint8_t gm_s3v312rvfthpf[43] = "radix prefixes must be lowercase; write `0";
const uint8_t gm_s44ek1gecbajs[7] = "void *";
const uint8_t gm_s4769el4glhs1[4] = "end";
const uint8_t gm_s487q6bd4pfu3[50] = "`cast` converts between pointer types, not from `";
const uint8_t gm_s498ag88l373e[14] = "gm_table_take";
const uint8_t gm_s4a4j4epvkeso[11] = "nfields = ";
const uint8_t gm_s4b8airp3rh69[50] = "rest parameters and defaults of function literals";
const uint8_t gm_s4chbmodjkinv[7] = "--help";
const uint8_t gm_s4dg5a71bcfsh[24] = "GM_ST({}, {}->arg, {});";
const uint8_t gm_s4emnk8r9sirq[11] = "gm_x_{}_{}";
const uint8_t gm_s4gilalcmdvu3[8] = "NetRecv";
const uint8_t gm_s4ifh5kc7nc0d[62] = "` goes directly before a `struct` or `union` literal (\302\24720.2)";
const uint8_t gm_s4kt3mhlp0or9[11] = "formatting";
const uint8_t gm_s4nj79mj9jk57[21] = "compiler/sema/soa.gm";
const uint8_t gm_s4o70d89183c2[4] = "add";
const uint8_t gm_s4ou57tmjn6c7[39] = "bool evok = gm_env_var(GM_LD(void *, &";
const uint8_t gm_s4r2qm241merr[12] = "const {}@{}";
const uint8_t gm_s4rmlksbchusu[11] = "### `{}`\012\012";
const uint8_t gm_s4sak9mcv04d9[25] = "        h = gm_mix64(h, ";
const uint8_t gm_s4ubro9t1tgos[17] = "\000\000\000\000\000\000\000\000f\000\000\000\000\000\000\000";
const uint8_t gm_s519l85f26pmp[81] = "static void {}(gm_node *cx, {} a, const char *file, uint32_t line, uint32_t col)";
const uint8_t gm_s52n7ji3kulf2[29] = "` requires integer constants";
const uint8_t gm_s53c18pcajigs[51] = "`: integer types are at most 128 bits wide (\302\2475.2)";
const uint8_t gm_s55edm0in8jpg[19] = "connection refused";
const uint8_t gm_s588gr73ph5f5[45] = "internal error: an instruction needs a frame";
const uint8_t gm_s58b2tdsrfkg1[85] = "`{}` would have two methods named `{}`: a method call must find exactly one (\302\2477.10)";
const uint8_t gm_s59a022mrijic[9] = "net_recv";
const uint8_t gm_s5bq0p1ohhqqe[15] = "the builtin {}";
const uint8_t gm_s5gbgbdradbg1[25] = "bigint: division by zero";
const uint8_t gm_s5i05gtdgdefb[42] = "slice bounds {}..{} invalid for length {}";
const uint8_t gm_s5ivi0sts5foi[9] = "_v[] = {";
const uint8_t gm_s5n5e484glelv[5] = "Left";
const uint8_t gm_s5q33up034m64[37] = "the template writes value {}, but {}";
const uint8_t gm_s5r5k5b2k7bru[20] = "/share/wasi-sysroot";
const uint8_t gm_s5ucn4crtdrq8[11] = "gm_int_dec";
const uint8_t gm_s632oftf9nssu[43] = "internal error: control flow left a thread";
const uint8_t gm_s63o5blu3j46d[16] = "__builtin_sqrtf";
const uint8_t gm_s650q383sn58v[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\346\001\000\000'\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_s65rai75evpi7[14] = "always_inline";
const uint8_t gm_s66fk044n1n0k[9] = "from_tag";
const uint8_t gm_s69kv55uiaqoj[10] = "--timeout";
const uint8_t gm_s6d960pgtv3l9[26] = "`main` must be a function";
const uint8_t gm_s6db4pm3k5olv[3] = "&B";
const uint8_t gm_s6dh2ca0ubali[7] = "size_t";
const uint8_t gm_s6hk4348mssfv[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\021\001\000\000'\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_s6jld2vf184ip[28] = "constant `@asm` constraints";
const uint8_t gm_s6khfdbr5aof9[34] = "no handler installed for effect `";
const uint8_t gm_s6rivmf4qclvb[71] = "cannot rename `{}` to `{}`: `{}` is already declared in the same scope";
const uint8_t gm_s70dhiaqijjg0[24] = "Operation not permitted";
const uint8_t gm_s721l5hj93rdl[50] = "a placeholder's value number has no leading zeros";
const uint8_t gm_s74v1q63ilf9i[122] = "static _Noreturn void gm_trap_hook_call(const uint8_t *msg, size_t len, const char *file, uint32_t line, uint32_t col) {\012";
const uint8_t gm_s76ovr591an92[4] = "_c(";
const uint8_t gm_s7826b3jmrqb0[10] = "_Alignas(";
const uint8_t gm_s794tb9qjlq1h[42] = " (a literal at `{}` needs `where {}({})`)";
const uint8_t gm_s7b0d890pj8dv[14] = "gm_fiber_cont";
const uint8_t gm_s7b7j2m1f6fle[71] = "this operation never returns to its caller: its result type is `never`";
const uint8_t gm_s7bj0jma5l2mg[39] = "gamma: cannot start the reader thread\012";
const uint8_t gm_s7euumoo24h31[3] = "]\012";
const uint8_t gm_s7f47bs0i7q74[21] = "Connection timed out";
const uint8_t gm_s7i7uukpl183c[4] = "br-";
const uint8_t gm_s7ipvt5onv6ht[4] = "gm_";
const uint8_t gm_s7pnhdrn5iusl[53] = "expected a handler, but no handler is declared for `";
const uint8_t gm_s7rl9plavbrfo[17] = "missing argument";
const uint8_t gm_s7s8viq00e4g2[15] = "expected value";
const uint8_t gm_s7ti0q30v2iq7[8] = "gm_atag";
const uint8_t gm_s81trlqpvp2do[11] = "_overflow(";
const uint8_t gm_s83uh7m5068c7[14] = "for (size_t i";
const uint8_t gm_s85m9c4pt0ikp[6] = " len;";
const uint8_t gm_s872048h9ttis[26] = "` is given more than once";
const uint8_t gm_s873vbpojuh8v[5] = "Mul(";
const uint8_t gm_s87isqt5t284q[9] = "ull);\012}\012";
const uint8_t gm_s894fmp947sfb[22] = "((unsigned __int128)(";
const uint8_t gm_s8952afsouaih[21] = "local foreign blocks";
const uint8_t gm_s8c43u4dovbrb[17] = "\000\000\000\000\000\000\000\000 \000\000\000\000\000\000\000";
const uint8_t gm_s8e4n8lgtb0fh[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000:\001\000\000N\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_s8m850dccae8f[88] = "`: a path starts with `./`, `../`, `std/`, or a dependency listed in gamma.toml (\302\2473.2)";
const uint8_t gm_s8olahjn69at6[21] = "({})(({})0 - ({}){})";
const uint8_t gm_s8oln6mi5b5qr[46] = "`@black_box` takes one value: `@black_box(x)`";
const uint8_t gm_s8om3gihgkru8[8] = "lc_node";
const uint8_t gm_s8pt0g4bme9og[30] = "the `git` URL of dependency `";
const uint8_t gm_s8qn6r2vquv2d[24] = "to begin the union body";
const uint8_t gm_s8srbeac8og16[29] = "replace the `if` with `{}{}`";
const uint8_t gm_s8u1980tbab9f[15] = "` is not Fil-C";
const uint8_t gm_s8vhc5kn5i8tv[38] = "a field name or tuple index after `.`";
const uint8_t gm_s8vthjuk0el8j[63] = "`.to(F)` converts numeric lanes to `f32` or `f64` lanes, not `";
const uint8_t gm_s927f9s6klgc5[4] = "F.l";
const uint8_t gm_s966nqk7bt67q[19] = " does not fit in `";
const uint8_t gm_s967emiec5f3a[9] = "hash_map";
const uint8_t gm_s988kcbq5r7tp[14] = "call perform ";
const uint8_t gm_s99fe9sgbdt37[14] = "gm_find_outer";
const uint8_t gm_s9jmloct2mk5m[11] = "clone_expr";
const uint8_t gm_s9k2jkfctp1g5[72] = "`@hash` is defined only for types with `is`, and `{}` has none (\302\24716.8)";
const uint8_t gm_s9oqmmiu8qcsh[4] = "u16";
const uint8_t gm_s9pcjvamrhbn7[25] = "error: unknown command `";
const uint8_t gm_s9po3krkcue72[4] = "l{}";
const uint8_t gm_s9ug0q3248l8e[44] = "print how long each test took and the total";
const uint8_t gm_sa132smnv97ed[4] = " / ";
const uint8_t gm_sa16a135q4am6[13] = ", .result = ";
const uint8_t gm_sa23fj423hlk4[6] = "touch";
const uint8_t gm_sa7fm83sqt8aa[16] = "` is deprecated";
const uint8_t gm_sa7gjgqof9s56[44] = "a function type cannot have type parameters";
const uint8_t gm_sa831sfid0hf3[2] = "~";
const uint8_t gm_sa98jv6l3j8el[6] = "--fix";
const uint8_t gm_sa9ehjurfkhlj[17] = " __attribute__((";
const uint8_t gm_sabcps3pq8iv7[15] = "gm_heap_alloc(";
const uint8_t gm_sag4ku07t2k8t[25] = "` has no element named `";
const uint8_t gm_sah6kaenul8kp[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000t\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_saiq60vdu8ckv[10] = ".payload@";
const uint8_t gm_sajv8cd7k86pe[3] = "[j";
const uint8_t gm_sakaoktvp5alh[52] = "calls of functions whose bodies are not checked yet";
const uint8_t gm_sam8dbnfp1kb4[34] = "    print(\"{}\")\012    eprint(\"{}\")\012";
const uint8_t gm_samqd3pmlgqmg[14] = "tag_signed = ";
const uint8_t gm_sankagtb3ecgn[58] = "use only one of `--tag`, `--branch`, `--rev`, `--version`";
const uint8_t gm_sao9h06bhkdja[26] = "->cx = inst->fw->body_cx;";
const uint8_t gm_saturamfhig5d[45] = "__atomic_thread_fence(gm_ord[GM_LD(uint8_t, ";
const uint8_t gm_saufuk3rs2i2m[15] = "foreign-global";
const uint8_t gm_savnf0pcaokd0[24] = "a vector holds at most ";
const uint8_t gm_sb0ri502n42ab[16] = "reference = {}\012";
const uint8_t gm_sb2sr4t6stbn1[16] = "gm_pipe_cloexec";
const uint8_t gm_sb3o36rmuvj28[56] = " levels deep, the limit of this implementation (\302\24723.5)";
const uint8_t gm_sb8jvk2vbau9p[25] = "this compile-time result";
const uint8_t gm_sba0715plrn4t[22] = "the function declares";
const uint8_t gm_sbabklk1lp75f[30] = "    for (uint64_t i = 0; i < ";
const uint8_t gm_sbbre965bpo5f[77] = "`{}` applies only to `#bits` structs whose fields are all `bool` or integers";
const uint8_t gm_sbcpd47bv0ru9[63] = "` is not a valid package name; use `--name` to choose another\012";
const uint8_t gm_sbeptfoja7ub2[37] = "{}({}.env, {}, NULL, GM_LD({}, {}));";
const uint8_t gm_sbg03hl2obk72[10] = "{}:{}: {}";
const uint8_t gm_sbg28ee7l5gjs[6] = "core/";
const uint8_t gm_sbgdbpkf33nsc[30] = " cannot be compared with `is`";
const uint8_t gm_sbhq27eoeitmb[24] = "for (size_t i = 0; i < ";
const uint8_t gm_sblodmj4vs4nd[24] = "a core library function";
const uint8_t gm_sbmedt8vmt5ve[5] = "gm_w";
const uint8_t gm_sbnms71r2jqr5[7] = "LaneLe";
const uint8_t gm_sbp061q0urkl4[4865] = "\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\023\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_sbp7rkf7ng8nb[33] = "` would have two methods named `";
const uint8_t gm_sc149ns1jm5on[9] = "lib{}.{}";
const uint8_t gm_sc5bk1hdqs61j[22] = "exited with status {}";
const uint8_t gm_sc7bkbe71it8c[10] = "continue ";
const uint8_t gm_sc8spe93fl51n[45] = "also print the output of the tests that pass";
const uint8_t gm_sc90o4moufk96[41] = "    {}(m, {});\012    __builtin_trap();\012}}\012";
const uint8_t gm_scavc1uf2hs8l[7] = "Cover(";
const uint8_t gm_scc8p0a9eachg[4] = "for";
const uint8_t gm_scch7vqldh7aj[13] = "the integer ";
const uint8_t gm_scd7cqpmpn0p9[78] = "this slice of a temporary array is returned, but the array ends with the call";
const uint8_t gm_sci406m9tkvjp[32] = "    return gm_unw.kind != 0;\012}\012";
const uint8_t gm_scitmdqfeod1m[6] = "ended";
const uint8_t gm_scjfd9b9vg99c[12] = " (os error ";
const uint8_t gm_scongfv16i8kh[21] = "slicing this pointer";
const uint8_t gm_scstobchrdgiu[67] = "` is also a name of the compiled program's runtime; choose another";
const uint8_t gm_sctlci97unb16[36] = "conflicting requirements for {}: {}";
const uint8_t gm_scuaf7ud4smlt[123] = "an element of a `#soa` has no address, since its fields are stored apart (\302\2475.6.4); take the address of a field: `&a[i].f`";
const uint8_t gm_sd1uj6gf5v8qh[8] = "inline ";
const uint8_t gm_sd66vi8qjocp9[5] = "{}: ";
const uint8_t gm_sd80su3vlvhjt[27] = "documentFormattingProvider";
const uint8_t gm_sd98uehpeed1c[11] = "gm_fs_stat";
const uint8_t gm_sd9r0l17bbuoe[5] = ": {}";
const uint8_t gm_sdark9dtsgt1q[41] = "the package contains the symbolic link `";
const uint8_t gm_sdbo9gngsfqdj[8] = "gm_type";
const uint8_t gm_sdckdu8fe6iog[7] = "label ";
const uint8_t gm_sdes3rh8ctb7v[21] = "#define GM_RUN_MAIN ";
const uint8_t gm_sdgcpos3mblup[37] = "no handler installed for effect `{}`";
const uint8_t gm_sdl8mgtvebfcs[3] = "on";
const uint8_t gm_sdpkj0pb6qtc0[33] = "the builtin `{}` at compile time";
const uint8_t gm_sdpl6uk3q4cch[6] = "\" in ";
const uint8_t gm_sdstli6hlsa1e[7] = "{}; {}";
const uint8_t gm_se39cci44d9rt[35] = "     Initialized a git repository\012";
const uint8_t gm_seb50ruktgoun[14] = "syntax_errors";
const uint8_t gm_sebidnsgqg13d[45] = "package `{}`: `rev` must be a full commit id";
const uint8_t gm_seh12nehvgmhj[12] = "checksum = ";
const uint8_t gm_sehko5bb96s6p[5] = "true";
const uint8_t gm_sehokd57s045g[11] = "gamma-run-";
const uint8_t gm_seleu2adn5qnd[18] = "` at compile time";
const uint8_t gm_selmorb0539e5[5] = "(m, ";
const uint8_t gm_seoeuc6nik756[40] = "a tag value must be an integer constant";
const uint8_t gm_seoiboio4n733[5] = "\003\000\000\000";
const uint8_t gm_ses9qtp6kdd22[8] = "default";
const uint8_t gm_setftr5leknrn[23] = "unmatched closing `{}`";
const uint8_t gm_sf0p9o7g7fr6l[7] = "[] = {";
const uint8_t gm_sf18e6o1dss49[6] = "; };\012";
const uint8_t gm_sf4j7f4kg765o[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\302\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\013\000\000\000\000\000\000\000";
const uint8_t gm_sf7ldnukv79cp[24] = "write it: `{}({}, \342\200\246)`";
const uint8_t gm_sfa86erpod86s[4] = "nan";
const uint8_t gm_sfc9avb7sceba[21] = "key must be a string";
const uint8_t gm_sfltedqje18on[9] = "({} < 0)";
const uint8_t gm_sfn5aos9cps1h[13] = "{}return {};";
const uint8_t gm_sfp78bn17ei97[60] = "`---` requires a plain-data type, but `{}` is not (\302\2474.4){}";
const uint8_t gm_sfpg6jsb3cfp6[23] = "compiler/lsp/server.gm";
const uint8_t gm_sfss9c30cdmun[8] = "error: ";
const uint8_t gm_sfvc4ovr5hl6h[23] = "`@{}` takes one `bool`";
const uint8_t gm_sg2tq59po62eq[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000#\002\000\000*\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_sg47f6qrh6jrs[32] = "write it by name: `{}({}: \342\200\246)`";
const uint8_t gm_sg5h7btla5ksa[29] = "` is assigned but never read";
const uint8_t gm_sgdrhdkbtd5gr[19] = ", .env = (void *)&";
const uint8_t gm_sgfmudgd82uhr[24] = "this kind of expression";
const uint8_t gm_sgga2ijjm9cqd[7] = "gm_ord";
const uint8_t gm_sgib96mf5meqv[11] = "gamma.lock";
const uint8_t gm_sgj6i64633q0l[11] = "gm_fs{}_{}";
const uint8_t gm_sglu5epjv38rl[92] = "` has a calling convention other than C, so it cannot be a second-class `fn` value (\302\24719.2)";
const uint8_t gm_sgmttpp6cmh41[14] = "gm_trap_index";
const uint8_t gm_sgqsd7lfikrak[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000u\001\000\000\035\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_sgurqv8kb2rs7[32] = "the receiver and argument of `.";
const uint8_t gm_sh1to1i0u67lu[9] = "lanes_lt";
const uint8_t gm_sh2urbm3ndjnn[31] = "cannot index a value of type `";
const uint8_t gm_sh4sp5o4s4bj1[9] = "generic ";
const uint8_t gm_sh5rvkuevnqlk[7] = "gm_ibp";
const uint8_t gm_sha7302s2fme2[18] = "GIT_INDEX_FILE={}";
const uint8_t gm_shbfu7hj4uhkt[16] = "` needs a value";
const uint8_t gm_shde4bsckk9mt[4] = " | ";
const uint8_t gm_shdies4nuf4tc[8] = "lc_with";
const uint8_t gm_she92i1u1a4v5[30] = " (there are no truthy values)";
const uint8_t gm_shgptuodlhrcq[61] = "# This file is generated by `gamma`. Do not edit it by hand.";
const uint8_t gm_shkq4botdoed7[6] = "First";
const uint8_t gm_shlk3g0tk7m4f[31] = "__attribute__((noreturn)) void";
const uint8_t gm_shnmjdmsj431f[17] = "`-` on this type";
const uint8_t gm_sho5nuaja1vvf[5] = "self";
const uint8_t gm_shq89iblo3j6k[51] = "this pattern has {} elements, but the value has {}";
const uint8_t gm_shrtuk3jka1au[16] = "    memcpy(gm_g";
const uint8_t gm_shsifme8hcenm[17] = "cannot download ";
const uint8_t gm_shvak64vhokft[26] = "#define GM_RT_SEPARATE 1\012";
const uint8_t gm_si0b5q45ot754[101] = "` is a `#c_union`: it has no tag, so it cannot be matched; read one of its fields instead (\302\2475.11.1)";
const uint8_t gm_si0mnqg6j1eu8[19] = "__atomic_fetch_add";
const uint8_t gm_si23lpqv3htdl[27] = " (a `#c_union` has no tag)";
const uint8_t gm_sibmp9mck7osv[13] = ".len != 0) {";
const uint8_t gm_sigac1ehmei1f[69] = "` names a type; a local type may not have the name of a type (\302\2474.6)";
const uint8_t gm_siiqktevj2s2h[25] = "gm_fmin(gm_fmax((double)";
const uint8_t gm_sij2r6jhpquej[12] = "LaneConvert";
const uint8_t gm_sikspprj7od7l[43] = "destructuring needs a tuple, but this is `";
const uint8_t gm_sildigrhtv45e[18] = "const gm_type *vd";
const uint8_t gm_sio8t6i3tf974[11] = "AtomicRmw(";
const uint8_t gm_siot2vlveunn8[18] = "{} run, {} failed";
const uint8_t gm_sirg36m1q4cjm[55] = "cannot rename `{}`: a use at {}:{} is not a plain name";
const uint8_t gm_sisb3n56u9bmt[15] = "gm_fs_set_mode";
const uint8_t gm_siselrhsqskvi[6] = "<li>\012";
const uint8_t gm_sisp52otedii5[23] = "-endian machine, but `";
const uint8_t gm_sithisfj5qgfv[67] = "unquoted keys cannot be empty, expected letters, numbers, `-`, `_`";
const uint8_t gm_siv3d9he09v3r[44] = "`) is unused; write `_ = \342\200\246` to discard it";
const uint8_t gm_sivuq9c7rjrh5[16] = "--deny-warnings";
const uint8_t gm_sj2tf1ift8sga[21] = "the directory name `";
const uint8_t gm_sj36eemfvep3a[56] = "` is a built-in method of slices and strings: write `s.";
const uint8_t gm_sj486prdcn2go[14] = "the match arm";
const uint8_t gm_sj7i3vhsq2ifl[38] = "a compound assignment takes one place";
const uint8_t gm_sjdn5gs7lu39i[57] = "a tuple type must be either fully named or fully unnamed";
const uint8_t gm_sjjc564tiep5s[13] = "else-capture";
const uint8_t gm_sjk23rfp9022d[14] = ", precision: ";
const uint8_t gm_sjnv3p8t49clu[9] = "` needs ";
const uint8_t gm_sjqs7q3ostqbm[4] = "pub";
const uint8_t gm_sjrj6qdrion32[35] = "a defer is not directly in a block";
const uint8_t gm_sju4s3ndpnsge[107] = "the `else` of a pattern binding must leave the block: `return`, `break`, `continue` or `trap(\342\200\246)` (\302\2474.5)";
const uint8_t gm_sk0h761p8pjoe[43] = "` needs an array variable, not a temporary";
const uint8_t gm_sk4u34fs8qq60[38] = "__asm__ volatile ({} : {} : {} : {});";
const uint8_t gm_sk5qo3et4heam[40] = "list the rules and their default levels";
const uint8_t gm_sk88l68smd6kt[23] = "gamma: no input file\012\012";
const uint8_t gm_skat03ddcievj[39] = "`!` unwraps a result, but this is `{}`";
const uint8_t gm_skbmmrtbskpao[8] = "-static";
const uint8_t gm_skem4050p6ec1[13] = "#define gm_g";
const uint8_t gm_skkqaektdhkn1[29] = "` leaves the package (\302\2473.2)";
const uint8_t gm_skmg81d57fq1r[31] = "element `{}` is supplied twice";
const uint8_t gm_skmsgkj4jeo7n[6] = "ccx{}";
const uint8_t gm_skpsvuvlfj0ep[68] = "the array length `{}` overflows or divides by zero with {} (\302\24716.5)";
const uint8_t gm_skq2b15b87en9[63] = "cannot use the path {}: No such file or directory (os error 2)";
const uint8_t gm_skqf89fdi6p8e[12] = "u << 64) | ";
const uint8_t gm_skqko8fn9asq3[97] = "`--backend=cranelift` is not supported: gamma has the C back end and, for `run`, the interpreter";
const uint8_t gm_skr2kn3pd8es7[11] = "extern {};";
const uint8_t gm_skv01gaujln2s[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000:\000\000\000$\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_skvqkn3gte5mk[11] = "variant {}";
const uint8_t gm_sl439hggqojfk[15] = "pack_rest_args";
const uint8_t gm_sl69n28ucapfg[21] = "((void *)(uintptr_t)";
const uint8_t gm_sl7p035rs001b[27] = "+refs/heads/*:refs/heads/*";
const uint8_t gm_sl8of2tnl835f[6] = "gm_pf";
const uint8_t gm_sla366gd7nphd[58] = "this points into a field of a `#packed` struct, where a `";
const uint8_t gm_slbqo2q5mg776[107] = "when a member of an inline tuple of `in` is a pattern, every member must be a pattern or a literal (\302\2477.7)";
const uint8_t gm_sligbvojecf44[15] = "write {} table";
const uint8_t gm_sll3452l5oa19[6] = "imag ";
const uint8_t gm_slpo22sjs83b1[5] = "when";
const uint8_t gm_slrtg04n254p4[19] = " (already exists)\012";
const uint8_t gm_sm3pe8sptk65k[24] = "` does not convert to `";
const uint8_t gm_sm8m4kp0f7r62[13] = "the builtin ";
const uint8_t gm_sm8t3g6c72ol4[62] = "the parameter `{}` must appear in the type the handler is for";
const uint8_t gm_smc854k364mj6[21] = " is a `usize`, but `";
const uint8_t gm_smdv8hgj9kr3n[21] = "GIT_OBJECT_DIRECTORY";
const uint8_t gm_smf5jnr5ps1bl[5] = "gm_b";
const uint8_t gm_smf6b2r7npomm[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\000\030\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_smohsnbvmf059[10] = "heap_free";
const uint8_t gm_smptjqchq7bt0[47] = "((gm_closure){{(void *){}, (void *)&F.h, {}}})";
const uint8_t gm_smq2niiusplb0[40] = "void *{} = gm_resume(inst->fw, {}, {});";
const uint8_t gm_smu48daue2qai[6] = "using";
const uint8_t gm_sn0peadng5i31[3] = "-g";
const uint8_t gm_sn2gg3b98t635[21] = "gm_bt_top = B.prev; ";
const uint8_t gm_sn790bmojc3gl[33] = "`{}` has no parameter named `{}`";
const uint8_t gm_sn7k3frnltojm[8] = "`@hash`";
const uint8_t gm_sn7tl9jolqoj1[3] = ".*";
const uint8_t gm_sn8alsh9na9ne[12] = "this atomic";
const uint8_t gm_sn9gq20tga4vu[35] = "{} {}; memcpy(&{}, {}, sizeof {});";
const uint8_t gm_snenkhfev166s[3] = "..";
const uint8_t gm_snf16gr70fhka[11] = "this guard";
const uint8_t gm_snh5ve0osdp08[7] = "`@asm`";
const uint8_t gm_snikjq3vkoue5[14] = "static_assert";
const uint8_t gm_snivf25e5fme8[6] = "first";
const uint8_t gm_snja4gioias3h[19] = "` is not an effect";
const uint8_t gm_snmep73bl34j8[7] = "</li>\012";
const uint8_t gm_snmm68ee1rq48[41] = " (the existing gamma.toml is unchanged)\012";
const uint8_t gm_snp0oqa4m79hk[6] = ":load";
const uint8_t gm_snps96dj7ecq0[17] = "the archive of `";
const uint8_t gm_sns75reqq32d2[4] = "inf";
const uint8_t gm_snu49maifa670[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\335\000\000\000\032\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_so2sjlodq81qn[13] = "NetLocalPort";
const uint8_t gm_so5c2bcepn6c6[15] = "XDG_CACHE_HOME";
const uint8_t gm_so5l2ibq9ia3i[65] = "`@variant_name` takes a value of a tagged union type, not `{}`{}";
const uint8_t gm_so6e0kl9gkq21[46] = "use only one of `--path`, `--git` and `--url`";
const uint8_t gm_so7v1cppe0bu8[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\321\001\000\000\031\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_so9fi6r28770a[3] = "((";
const uint8_t gm_soa9mnl4ahjud[8] = "static ";
const uint8_t gm_soaukr1udufqk[19] = "` has no method `.";
const uint8_t gm_sobspv56mhf83[15] = "GIT_INDEX_FILE";
const uint8_t gm_sodq0gu838ini[139] = "`{}(x)` would be a conversion to a type parameter, which is not lossless for every type; use `x.trunc({})`, `x.sat({})` or `x.checked({})`";
const uint8_t gm_sog9gpimub31i[7] = "` -> `";
const uint8_t gm_sojts6dcvra33[35] = "` takes one value per element, so ";
const uint8_t gm_somon4mdq0gpj[24] = ", cx->env, cx->hctx, cx";
const uint8_t gm_son0j0k0t10ro[8] = "sha256:";
const uint8_t gm_soo5q2bffam33[33] = "unknown attribute `#{}` (\302\24720.2)";
const uint8_t gm_soqt3jnmovq5l[4] = "...";
const uint8_t gm_sovv48653dnju[40] = " must be parenthesized in this position";
const uint8_t gm_sp0iqkuu2lvpl[14] = "{}; otherwise";
const uint8_t gm_sp4acsbhsp3do[35] = "{} run, {} failed, {} filtered out";
const uint8_t gm_sp51dovg85ku9[40] = "empty string, expected a semver version";
const uint8_t gm_sp598n426d61b[40] = "` is already declared in the same scope";
const uint8_t gm_sp9acfc9ddurj[10] = "(void){};";
const uint8_t gm_spdm0kqdda6co[25] = "`; its dependencies are ";
const uint8_t gm_spdpobhso0dbc[10] = "GM_RESUME";
const uint8_t gm_speojrsjsem8a[50] = "internal error: formatted output does not lex: {}";
const uint8_t gm_spf5krhuq011v[11] = "`, not by ";
const uint8_t gm_spgnoteg4vcil[11] = "{}{} {}{};";
const uint8_t gm_sphl918t1t6qp[11] = "`; write `";
const uint8_t gm_spj0hqsg585et[14] = "integer lanes";
const uint8_t gm_spkcu8lv7g8jc[5] = "pn{}";
const uint8_t gm_spmgsv94458ho[59] = "memcpy((uint8_t *)&{} + {}, (uint8_t *)&{} + {} * {}, {});";
const uint8_t gm_spni6j7splqto[8] = " takes ";
const uint8_t gm_spu4ur06pm76g[17] = "\000\000\000\000\000\000\000\0001\000\000\000\000\000\000\000";
const uint8_t gm_sq0h7jjc6dtk5[4] = " lo";
const uint8_t gm_sq7d77si6ibqp[41] = "\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000+\002\000\000\032\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
const uint8_t gm_sq7ohh8h77bn7[26] = "compiler/interp/memory.gm";
const uint8_t gm_sqbstqdssta1a[48] = "an install is not in the install list of a with";
const uint8_t gm_sqfe8a5cd6b60[7] = "needs ";
const uint8_t gm_sqh0568fl8oao[14] = "this coercion";
const uint8_t gm_sqhe7365v82gi[16] = "this assignment";
const uint8_t gm_sqic2de8m2pet[61] = "    return a[{}] \077 {}(a + {}, b + {}) : {}(a + {}, b + {});\012";
const uint8_t gm_sqj99shb29i55[11] = "gm_hb_used";
const uint8_t gm_sqkk84ckt4rm6[46] = "`@prefetch` takes one pointer: `@prefetch(p)`";
const uint8_t gm_sqlk0h0kae56e[8] = "NetSend";
const uint8_t gm_sqmr87iegdcns[7] = "naming";
const uint8_t gm_sqn4031c223rk[13] = "; size_t evn";
const uint8_t gm_sqqfe3o1gh1ab[55] = "foreign function `{}` cannot be called at compile time";
const uint8_t gm_sqr5tipcqpi8v[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\370\001\000\000\022\000\000\000\000\000\000\000\000\000\000\000\022\000\000\000\000\000\000\000";
const uint8_t gm_sqtojn396s09d[11] = "copy_elems";
const uint8_t gm_squ7ob8ltdkn4[20] = "`git {}` failed: {}";
const uint8_t gm_sqvb27hiolbh8[11] = "soa_expand";
const uint8_t gm_sr0u787bpqkqg[28] = "`@fiber` with this function";
const uint8_t gm_sr2ptnspelf8s[98] = "dependency cycle: `{}` is constructed while the default of one of its fields is evaluated (\302\2473.4)";
const uint8_t gm_sr3s1oi48i6m6[16] = "%{} = wrap {}{}";
const uint8_t gm_sr5j1k0g236qh[68] = "`.round` converts to an integer; for the nearest `{}` use `.to({})`";
const uint8_t gm_sr5s632lvio16[62] = "`{}` is a function, which is not a value to format (\302\24721.6.1)";
const uint8_t gm_sr7ga013iei9a[4] = "))(";
const uint8_t gm_sr9i7j9bpmaec[11] = "` (\302\24714.6)";
const uint8_t gm_sragmmlrm20js[3] = "}`";
const uint8_t gm_srb0ps8mir4li[9] = "();\012    ";
const uint8_t gm_srbp7aar5n2sa[14] = "replace_alloc";
const uint8_t gm_sreie3avjjb9l[64] = "        gm_buf b = {0};\012        gm_buf_push(&b, \"error: \", 7);\012";
const uint8_t gm_srge6muj16eos[31] = "typedef struct{} {{ {} }} {};\012";
const uint8_t gm_srhtnnu7r6ead[20] = "check_import_cycles";
const uint8_t gm_sri9mnfjmc5ma[11] = "gm_threads";
const uint8_t gm_srkvn420e4huu[26] = "this is nested more than ";
const uint8_t gm_srm32inqe4h6s[10] = "  import ";
const uint8_t gm_srrkjtivok8h4[9] = " literal";
const uint8_t gm_srs1vrn2uaams[2] = "^";
const uint8_t gm_srtlfdmpif4qk[9] = "quickfix";
const uint8_t gm_srv5rbhjb8ns5[15] = "{{{}, {}, {}}}";
const uint8_t gm_ss41pl5vtg7un[72] = "    if (gm_unw.kind == GM_RESUME) { gm_unw.kind = GM_NONE; gm_with_go(&";
const uint8_t gm_ss6dev5u7tl99[12] = "ThreadYield";
const uint8_t gm_ss7iv2kssvf80[7] = "rev = ";
const uint8_t gm_ss9vj0o06n3e8[8] = "--proto";
const uint8_t gm_ssa0pabeurppq[20] = "static {}int {}({})";
const uint8_t gm_ssd7gce6jed13[7] = " -> {}";
const uint8_t gm_sse9dgkusbtnf[5] = "Last";
const uint8_t gm_ssgeom7733jik[5] = "Wrap";
const uint8_t gm_sshop8kf5qfhm[63] = "the fields of this `#bits` struct use {} bits, but `{}` has {}";
const uint8_t gm_ssj1mfqvo884a[62] = "this address of `{}` is returned, but `{}` ends with the call";
const uint8_t gm_ssr6vk8erbj18[13] = "` has type `";
const uint8_t gm_sss3bmvruos1t[9] = "  {} :: ";
const uint8_t gm_sss60if2anvc8[5] = "PATH";
const uint8_t gm_sss94v5r4qh46[13] = "gm_clock_now";
const uint8_t gm_sst2khrvr4nus[10] = "argument ";
const uint8_t gm_ssvvtpgta681j[19] = "<h3 id=\"{}\"><code>";
const uint8_t gm_st1bq6djjin2k[17] = "\000\000\000\000\000\000\000\000\001\000\000\000\000\000\000\000";
const uint8_t gm_st2e24ck2icsi[26] = "{}({}.env, {}, NULL, {});";
const uint8_t gm_st2ertk6tjva7[14] = "` points into";
const uint8_t gm_st3m5olipe6ov[17] = "missing element ";
const uint8_t gm_st3mnofi7dhrs[19] = "includeDeclaration";
const uint8_t gm_st4gkaq24fvj9[32] = " here goes to the catch-all arm";
const uint8_t gm_st5ngsmk07040[8] = "->arg[k";
const uint8_t gm_st78509mboaa4[22] = "gm_bitreverse({}, {})";
const uint8_t gm_statjrbfbnhar[11] = "(({}){}LL)";
const uint8_t gm_stm36rdb2vbg4[13] = "gm_child_env";
const uint8_t gm_stobu0djed6ck[10] = " elements";
const uint8_t gm_stou2bl2ross2[70] = "this slice of a temporary array is stored where it outlives the array";
const uint8_t gm_stqvtvp1hc371[12] = "\012  package ";
const uint8_t gm_strcbt1c3upss[4] = "../";
const uint8_t gm_stsvc1ssgs83i[62] = "assigning to {} places needs a tuple of {} elements, not `{}`";
const uint8_t gm_sttasaa2bp05i[22] = "signatureHelpProvider";
const uint8_t gm_su37g0462a9t0[41] = "\000\000\000\000\000\000\000\000\026\000\000\000\000\000\000\0001\000\000\000\036\000\000\000\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000";
const uint8_t gm_su50gais1htu0[38] = "` is not a C-compatible type (\302\24719.1)";
const uint8_t gm_su5bsj1nioctr[22] = "`#soa` cannot store `";
const uint8_t gm_su66bdcms0hot[28] = "static GM_TLS uint8_t gm_gi";
const uint8_t gm_su6ol10q7la3d[7] = "; use ";
const uint8_t gm_su7tg2occn1k0[15] = "install decls ";
const uint8_t gm_su8mkvs9pi0v1[18] = "unknown name `{}`";
const uint8_t gm_su9fl4d8e88sc[49] = "\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000";
const uint8_t gm_sucip17pohakb[22] = "{}{} {}({}) {{ {} }}\012";
const uint8_t gm_sucpkqgliu39h[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000\030\001\000\000Z\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sujs43rrijg57[102] = "static bool {}(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s)";
const uint8_t gm_sukriak12588g[5] = "ev{}";
const uint8_t gm_sunidvof1bsgr[33] = "`return` needs a value of type `";
const uint8_t gm_sup5ecv5jiqbn[119] = "    uintptr_t m = na < nb \077 na : nb;\012    int c = m == 0 \077 0 : memcmp(pa, pb, m);\012    return c != 0 \077 c < 0 : na < nb;\012";
const uint8_t gm_sup9bfr0np428[9] = "{} in {}";
const uint8_t gm_surt6bemh510v[31] = "`{}` needs {} type argument(s)";
const uint8_t gm_sv732pptj1lih[10] = "item_json";
const uint8_t gm_svaaloo7n72id[9] = "{} = {}\012";
const uint8_t gm_svb4l7qho0a9o[16] = " is not a local";
const uint8_t gm_svd9icup57bmm[27] = "lane {} does not exist: {}";
const uint8_t gm_svk1fq3llqdta[10] = "refs/tags";
const uint8_t gm_svlbiokurll04[12] = "parameter `";
const uint8_t gm_svmtk9tcvmupq[1051] = "\012environment:\012  GAMMA_STD      the standard library (default: lib/gamma/std next to bin/)\012  GAMMA_CORE     the core library (default: core next to the standard library)\012  GAMMA_RUNTIME  the runtime's C sources (default: runtime two levels above the standard library)\012  CC             the C compiler (default: cc; clang for checked and fast builds if found), with its arguments\012  CFLAGS         flags for every C compile, LDFLAGS flags for the link (after those of gamma.toml)\012  GAMMA_CACHE_DIR\012                 the package store (default: the user's cache directory; empty turns it off)\012  GAMMA_C_UNITS  the number of C units of a Debug build (default: by size and processors)\012  GAMMA_UNIT_OBJECTS\012                 a directory of objects compiled ahead for the C units of a build\012  GAMMA_RUNNER   the program that runs the executables of another target (e.g. qemu-s390x)\012  WASI_SDK_PATH  the WASI SDK (for wasm32-wasi; its clang is the default C compiler there)\012  WASM_OPT       the wasm-opt program (default: wasm-opt) for programs that use fibers\012\012";
const uint8_t gm_svr80l31985v4[7] = "v + {}";
const uint8_t gm_svv3t2si5df2q[15] = ", a) == GM_LD(";
uint8_t gm_c07srrruo2c[11] __attribute__((aligned(1)));
uint8_t gm_c19scujv4gq[6] __attribute__((aligned(1)));
uint8_t gm_c1s0khhquf5[7] __attribute__((aligned(1)));
uint8_t gm_c2rl097cgcd[16] __attribute__((aligned(1)));
uint8_t gm_c2t15afgpnn[8] __attribute__((aligned(8)));
uint8_t gm_c3hm1rk1ipa[10] __attribute__((aligned(1)));
uint8_t gm_c423srue241[9] __attribute__((aligned(1)));
uint8_t gm_c42ctv8im24[920] __attribute__((aligned(8)));
uint8_t gm_c4qfm55t8rc[3] __attribute__((aligned(1)));
uint8_t gm_c4rs7sohibn[5] __attribute__((aligned(1)));
uint8_t gm_c50isunhcrj[16] __attribute__((aligned(8)));
uint8_t gm_c5cvc3ab6qo[150] __attribute__((aligned(1)));
uint8_t gm_c5d9memamu5[6] __attribute__((aligned(1)));
uint8_t gm_c699d0m66pa[1] __attribute__((aligned(1)));
uint8_t gm_c6fgtimd3il[6] __attribute__((aligned(1)));
uint8_t gm_c82i6abqvvu[4] __attribute__((aligned(1)));
uint8_t gm_c886bolkgpe[9] __attribute__((aligned(1)));
uint8_t gm_c9la6b8qclf[10] __attribute__((aligned(1)));
uint8_t gm_c9pef0g9fng[4] __attribute__((aligned(4)));
uint8_t gm_cbocru68uam[11] __attribute__((aligned(1)));
uint8_t gm_ccvvl33tsdt[12] __attribute__((aligned(1)));
uint8_t gm_cdlh2b67kra[8] __attribute__((aligned(8)));
uint8_t gm_celc7l631oc[13] __attribute__((aligned(1)));
uint8_t gm_cen8idnq6iv[6] __attribute__((aligned(1)));
uint8_t gm_cenuluelo82[3] __attribute__((aligned(1)));
uint8_t gm_cepccru8ka8[6] __attribute__((aligned(1)));
uint8_t gm_cfql7t1kn72[14] __attribute__((aligned(1)));
uint8_t gm_cfslp7hoejg[3] __attribute__((aligned(1)));
uint8_t gm_cg6c770qgrk[11] __attribute__((aligned(1)));
uint8_t gm_cg7qavecf9k[3] __attribute__((aligned(1)));
uint8_t gm_cgdfdu1u5gd[7] __attribute__((aligned(1)));
uint8_t gm_cgsatlrqc4j[3] __attribute__((aligned(1)));
uint8_t gm_chtdbvfshqk[10] __attribute__((aligned(1)));
uint8_t gm_cits27rfpla[17] __attribute__((aligned(1)));
uint8_t gm_ckbflbrril1[1] __attribute__((aligned(1)));
uint8_t gm_ckmj20kunc0[4] __attribute__((aligned(1)));
uint8_t gm_cl17cr2em63[9] __attribute__((aligned(1)));
uint8_t gm_cl1d82mrtnv[40] __attribute__((aligned(1)));
uint8_t gm_clb2b60g1qs[13] __attribute__((aligned(1)));
uint8_t gm_clg4nkf458e[8] __attribute__((aligned(1)));
uint8_t gm_cm0ptlu1qd7[25] __attribute__((aligned(1)));
uint8_t gm_cm8gajkonri[6] __attribute__((aligned(1)));
uint8_t gm_cn824b4bhc1[8] __attribute__((aligned(1)));
uint8_t gm_cnj6gqa99nb[14] __attribute__((aligned(1)));
uint8_t gm_cnna9skotcp[9] __attribute__((aligned(1)));
uint8_t gm_cntjgiv42f7[23] __attribute__((aligned(1)));
uint8_t gm_co987o3ird4[8] __attribute__((aligned(1)));
uint8_t gm_cot5n1cq694[16] __attribute__((aligned(1)));
uint8_t gm_cpkd7s1srcc[4933] __attribute__((aligned(1)));
uint8_t gm_cpma9mav80s[3] __attribute__((aligned(1)));
uint8_t gm_cpn2np70vcf[8] __attribute__((aligned(8)));
uint8_t gm_cpupn61v7jf[12] __attribute__((aligned(1)));
uint8_t gm_cr5mkoa1b3c[10] __attribute__((aligned(1)));
uint8_t gm_crbso58uocb[10] __attribute__((aligned(1)));
uint8_t gm_cri2dckkbh8[9] __attribute__((aligned(1)));
uint8_t gm_crpb2rekieb[1] __attribute__((aligned(1)));
uint8_t gm_csjir7b1m9j[14] __attribute__((aligned(1)));
uint8_t gm_csp8mutqj4e[13] __attribute__((aligned(1)));
uint8_t gm_csp8t1dbqmf[13] __attribute__((aligned(1)));
uint8_t gm_csu9mkbsfpo[16] __attribute__((aligned(8)));
uint8_t gm_csuv6tb6jnj[38] __attribute__((aligned(1)));
uint8_t gm_csvkj3mk95q[11] __attribute__((aligned(1)));
uint8_t gm_ct6psi7af52[17] __attribute__((aligned(1)));
uint8_t gm_ct8d43r6fpj[13] __attribute__((aligned(1)));
uint8_t gm_ctcdekq6f2q[11] __attribute__((aligned(1)));
uint8_t gm_ctgi64l27km[15] __attribute__((aligned(1)));
uint8_t gm_ctq6t8frtor[4] __attribute__((aligned(4)));
uint8_t gm_cu0b9l65c0v[1] __attribute__((aligned(1)));
uint8_t gm_cu6pb2rap57[3] __attribute__((aligned(1)));
uint8_t gm_cuqbj62gqhf_2[16] __attribute__((aligned(8)));
uint8_t gm_cvb3726geh8[3] __attribute__((aligned(1)));
uint8_t gm_k0nv110r163[40] __attribute__((aligned(8)));
uint8_t gm_k48l3rluocl[40] __attribute__((aligned(8)));
uint8_t gm_k65c70p8jkg[40] __attribute__((aligned(8)));
uint8_t gm_k6be0mlmbp5[40] __attribute__((aligned(8)));
uint8_t gm_k72nh3emov2[40] __attribute__((aligned(8)));
uint8_t gm_k7bkj4i7gae[40] __attribute__((aligned(8)));
uint8_t gm_ka73u23j5nf[40] __attribute__((aligned(8)));
uint8_t gm_kdmnul2f0o4[40] __attribute__((aligned(8)));
uint8_t gm_kdpqmkpiene[40] __attribute__((aligned(8)));
uint8_t gm_kgj4rhjmudg[40] __attribute__((aligned(8)));
uint8_t gm_ki39jj5pj1o[40] __attribute__((aligned(8)));
uint8_t gm_kjn1j0gvnde[40] __attribute__((aligned(8)));
uint8_t gm_km7a24is5mf[40] __attribute__((aligned(8)));
uint8_t gm_ko53fduln7u[40] __attribute__((aligned(8)));
uint8_t gm_kq6te48ps2f[40] __attribute__((aligned(8)));
uint8_t gm_ks677q1halv[40] __attribute__((aligned(8)));
uint8_t gm_kt2nhjs8rv5[40] __attribute__((aligned(8)));
uint8_t gm_ktjao6lvic2[40] __attribute__((aligned(8)));
uint8_t gm_ku5cbgje11b[40] __attribute__((aligned(8)));
uint8_t gm_kue41kf0tbk[40] __attribute__((aligned(8)));
uint8_t gm_kv89j160jqp[40] __attribute__((aligned(8)));
void *const gm_hlc7jtja1te5[] = {(void *)gm_f_for_body_ui0bpsf};
void *const gm_hlvtf175jpmc[] = {(void *)gm_f_for_body_keqa9hq};

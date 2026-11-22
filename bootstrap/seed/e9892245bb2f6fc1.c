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
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tv0k2vg3054a6;
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { uint64_t f0; gm_t5v0c8ia6a5mo f1; } gm_tgsvmddj3e85e;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; uint8_t f3; uint8_t f4; uint8_t p5[6]; gm_b16_8 f5; uint64_t f6; gm_trqfvmjtt4sij f7; } gm_tt0nlfbhb3hu6;
_Static_assert(sizeof(gm_tv0k2vg3054a6) == 48 && _Alignof(gm_tv0k2vg3054a6) == 8 && offsetof(gm_tv0k2vg3054a6, f0) == 0 && offsetof(gm_tv0k2vg3054a6, f1) == 16 && offsetof(gm_tv0k2vg3054a6, f2) == 24 && offsetof(gm_tv0k2vg3054a6, f3) == 40, "layout of `OpInfo`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tgsvmddj3e85e) == 64 && _Alignof(gm_tgsvmddj3e85e) == 8 && offsetof(gm_tgsvmddj3e85e, f0) == 0 && offsetof(gm_tgsvmddj3e85e, f1) == 8, "layout of `(i: usize, x: ParamInfo)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tt0nlfbhb3hu6) == 184 && _Alignof(gm_tt0nlfbhb3hu6) == 8 && offsetof(gm_tt0nlfbhb3hu6, f0) == 0 && offsetof(gm_tt0nlfbhb3hu6, f1) == 16 && offsetof(gm_tt0nlfbhb3hu6, f2) == 56 && offsetof(gm_tt0nlfbhb3hu6, f3) == 112 && offsetof(gm_tt0nlfbhb3hu6, f4) == 113 && offsetof(gm_tt0nlfbhb3hu6, f5) == 120 && offsetof(gm_tt0nlfbhb3hu6, f6) == 136 && offsetof(gm_tt0nlfbhb3hu6, f7) == 144, "layout of `Module`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; uint32_t v1; uint32_t v2; gm_tiilmfd16v3fc v3; gm_tnpifv20irnph v4; uint32_t v5; gm_tcc1rq4n1avb2 v6; gm_t88rcucg1b7uk v7; gm_t0ialelq3ql1k v8; uint64_t v9; gm_t88rcucg1b7uk v10; } f1; } gm_t6qooom8qa2do;
_Static_assert(sizeof(gm_t6qooom8qa2do) == 208 && _Alignof(gm_t6qooom8qa2do) == 8 && offsetof(gm_t6qooom8qa2do, f0) == 0 && offsetof(gm_t6qooom8qa2do, f1) == 8, "layout of `DeclValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t6qooom8qa2do v0; } f1; } gm_tagrkbqh054h7;
_Static_assert(sizeof(gm_tagrkbqh054h7) == 216 && _Alignof(gm_tagrkbqh054h7) == 8 && offsetof(gm_tagrkbqh054h7, f0) == 0 && offsetof(gm_tagrkbqh054h7, f1) == 8, "layout of `DeclState`");
struct gm_fr_decl_dump_iah0p30 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_tt0nlfbhb3hu6 l4; /* m */
    gm_t6qooom8qa2do l9; /* v */
    gm_b16_8 ret;
};
struct gm_fr_dump_adt_lmpn1o2 {
    gm_frame h;
    gm_tpautr4br7h3l l4; /* def */
    gm_tpautr4br7h3l l8; /* def */
    gm_tckdobunaulee l13; /* v */
    gm_b0_1 ret;
};
struct gm_fr_dump_fields_tli1mqp {
    gm_frame h;
    gm_t0mglksrkfa5e l7; /* f */
    gm_b0_1 ret;
};
struct gm_fr_dump_params_gh24g4q {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_dump_value_fcdgoej {
    gm_frame h;
    gm_t6qooom8qa2do l2; /* v */
    gm_tv0k2vg3054a6 l12; /* op */
    gm_t0ialelq3ql1k l21; /* u */
    gm_b0_1 ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_92arfe8 {
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
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_param_name_rbnop5l {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_effects_vieja9u {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_sig_text_i53adp7 {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* out */
    gm_t5v0c8ia6a5mo l8; /* p */
    gm_b16_8 ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_visibility_prefix_pbt2jhd {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_write_0g0ps7j {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_2gsrm6r {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_3j0395t {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_3qu59rs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_7bug452 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_7edu08o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_7ega8pl {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_8jvglca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_98kuve9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_9tu0dke {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_aisrlpj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_ajbca29 {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_clopc8o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_dgsisu6 {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_dqbiqk0 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_dte2pnq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_f4a3la5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_fhjt0ac {
    gm_frame h;
    gm_tfeab279c16rr l1; /* args */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_h9k6ni9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_p018n9l {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_rc0tik1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_rkgcjv5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_tjlhtea {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_u5ur1qg {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b);
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_decl_dump_iah0p30(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_dump_adt_lmpn1o2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_dump_fields_tli1mqp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_dump_params_gh24g4q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_dump_untyped_o8mt4q8(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_dump_value_fcdgoej(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effects_mg3vf2a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_effects_vieja9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_sig_text_i53adp7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
__attribute__((aligned(4))) void gm_f_state_a5bnsjb(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_visibility_prefix_pbt2jhd(gm_t9qkh0rsooc8d a0);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0g0ps7j(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2gsrm6r(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2venbiq(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3j0395t(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3qu59rs(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7bug452(void * a0, gm_t4ce0jd3tdro4 a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7edu08o(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7ega8pl(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8jvglca(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_98kuve9(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_9tu0dke(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_aisrlpj(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ajbca29(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_clopc8o(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dgsisu6(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dqbiqk0(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dte2pnq(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f4a3la5(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fhjt0ac(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_h9k6ni9(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_hqo1u7h(void * a0, gm_t4ce0jd3tdro4 a1, void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jog5i62(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_p018n9l(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rc0tik1(void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rkgcjv5(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_s3v41br(void * a0, const void * a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_tjlhtea(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u5ur1qg(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vq1ifl4(void * a0, gm_tnpifv20irnph a1, void * a2, gm_b16_8 a3);
extern const uint8_t gm_s1vduqt3gpvdd[10];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s2bo5kbkkvas2[11];
extern const uint8_t gm_s3bfo4sq48nsb[5];
extern const uint8_t gm_s4238sojpefbb[11];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s5cpmumv7vkcr[7];
extern const uint8_t gm_s5k1m00kjtc17[14];
extern const uint8_t gm_s66uvortgrtsg[4];
extern const uint8_t gm_s68avpqal8nbc[12];
extern const uint8_t gm_s8gf7bnv27fi9[7];
extern const uint8_t gm_sb71rivinn7pn[10];
extern const uint8_t gm_sb88rajuutaep[4];
extern const uint8_t gm_sbabl2k3pvk5p[11];
extern const uint8_t gm_sbf97lnc5053k[10];
extern const uint8_t gm_sbrr14r7dj3rt[9];
extern const uint8_t gm_sc49g68j2cekh[7];
extern const uint8_t gm_sc7ivrb8t36mb[12];
extern const uint8_t gm_sch2ljca2kt47[12];
extern const uint8_t gm_sd9r0l17bbuoe[5];
extern const uint8_t gm_sdt9q53qgnu87[7];
extern const uint8_t gm_sf4cn9nvrjhp1[15];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_sh4sp5o4s4bj1[9];
extern const uint8_t gm_shn6qvu5196go[10];
extern const uint8_t gm_sjdenof9qo6nf[12];
extern const uint8_t gm_sjeopa747clpb[4];
extern const uint8_t gm_sjjehkhgpkn3v[10];
extern const uint8_t gm_sk3qij0lrr43j[7];
extern const uint8_t gm_sl80cge5ts0aj[14];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_sp8jfj95g4f47[7];
extern const uint8_t gm_sqhj00be09q5v[9];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_sr54681eccerj[11];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcush70170kjg3[];
extern const uint8_t gm_ssd7gce6jed13[7];
extern const uint8_t gm_sss3bmvruos1t[9];
extern const uint8_t gm_stgfndqo1l714[13];
extern const uint8_t gm_su0l53i30raa8[6];
extern const uint8_t gm_svk3u470blc4u[9];
extern const gm_tmpl gm_tpl0e2i60vm7gtr;
extern const gm_tmpl gm_tpl33em4maossan;
extern const gm_tmpl gm_tpl37uc86rluksa;
extern const gm_tmpl gm_tpl4476c5bksft1;
extern const gm_tmpl gm_tpl65jqebqot15g;
extern const gm_tmpl gm_tplcte0qqjaa0q0;
extern const gm_tmpl gm_tpldqrk6ll2jb23;
extern const gm_tmpl gm_tpldvbj2judb6g5;
extern const gm_tmpl gm_tplg0lh6vujmm6o;
extern const gm_tmpl gm_tplgp15dehsbtkk;
extern const gm_tmpl gm_tpliufjdlri3svp;
extern const gm_tmpl gm_tplk51k04l6756s;
extern const gm_tmpl gm_tpll487d6t52m4g;
extern const gm_tmpl gm_tplls606vhvqojp;
extern const gm_tmpl gm_tploblcdf1g38ca;
extern const gm_tmpl gm_tploj5r79v5kirr;
extern const gm_tmpl gm_tplop2uhngiv88m;
extern const gm_tmpl gm_tplpgeniqejbt43;
extern const gm_tmpl gm_tplu7d9ofnf77qa;
extern const gm_tmpl gm_tplubjng587610o;
extern const gm_tmpl gm_tplvdft4iatcp17;
extern const gm_tmpl gm_tplvkpfj3qq0aig;
extern const gm_tmpl gm_tplvpehcchgo971;
extern const gm_tmpl gm_tplvrmulr6rg858;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq6groq37ram(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint64_t, a) == GM_LD(uint64_t, b);
}
inline bool gm_eq7mri15hk78(const uint8_t *a, const uint8_t *b) {
    if (a[8] != b[8]) return false;
    return !a[8] || gm_eq6groq37ram(a, b);
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_name_prpp0ip F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 216));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 216) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 283, 62);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t3) + (uintptr_t)t6 * 16));
    F.ret = t7;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_effects_r0iv3pv F;
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_92arfe8();
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&t2));
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    F.ret = t4;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_n83ttnb F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* elems */
    GM_STACK_CHECK_SHALLOW(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_t53at80pfdus3 gm_l3;
    {
        {
            void * t4 = gm_l0;
            gm_t88rcucg1b7uk t5 = gm_l1;
            gm_f_kind_jcnt4hm((void *)&gm_l3, t4, GM_CONV(gm_csguankko4lmhd, t5));
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)12ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_l2 = t8;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
            F.ret = t10;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_param_name_rbnop5l F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 256));
    uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)t2) + 256) + 8));
    uint32_t t5 = gm_l1;
    uint64_t t6 = (uint64_t)t5;
    if ((uintptr_t)t6 >= (uintptr_t)(t4)) gm_trap_index((uintptr_t)t6, t4, gm_src0ih77f43mhf2, 290, 61);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)t3) + (uintptr_t)t6 * 16));
    F.ret = t7;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_effects_vieja9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_effects_vieja9u F;
    void * gm_l0; /* t */
    void * gm_l1; /* set */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 200u);
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
    void * t7 = gm_l1;
    gm_b0_1 t8 = gm_f_effects_mg3vf2a((void *)NULL, cx, NULL, t6, t7);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_ref_aqt1t7c F;
    void * gm_l0; /* t */
    void * gm_l1; /* e */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 200u);
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
    void * t7 = gm_l1;
    gm_b0_1 t8 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t6, t7);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0g0ps7j(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_0g0ps7j F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s4238sojpefbb));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tploj5r79v5kirr), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2gsrm6r(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_2gsrm6r F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_su0l53i30raa8));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplls606vhvqojp), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3j0395t(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_3j0395t F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sc7ivrb8t36mb));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(11));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl33em4maossan), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_3qu59rs(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_3qu59rs F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sp8jfj95g4f47));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(6));
    gm_tftb1odc739t5 t5 = gm_f_write_vq1ifl4(t2, t3, ((void *)&gm_tpl0e2i60vm7gtr), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7bug452(void * a0, gm_t4ce0jd3tdro4 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_7bug452 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sch2ljca2kt47));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(11));
    gm_tftb1odc739t5 t5 = gm_f_write_hqo1u7h(t2, t3, ((void *)&gm_tplgp15dehsbtkk), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7edu08o(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_7edu08o F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_shn6qvu5196go));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(9));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplop2uhngiv88m), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7ega8pl(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_7ega8pl F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sd9r0l17bbuoe));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(4));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplvpehcchgo971), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8jvglca(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_8jvglca F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8gf7bnv27fi9));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpl4476c5bksft1), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_98kuve9(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_98kuve9 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s2bo5kbkkvas2));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplg0lh6vujmm6o), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_9tu0dke(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_9tu0dke F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sbabl2k3pvk5p));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpldqrk6ll2jb23), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_aisrlpj(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_aisrlpj F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sjjehkhgpkn3v));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(9));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpl37uc86rluksa), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ajbca29(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_ajbca29 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svk3u470blc4u));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_tftb1odc739t5 t4 = gm_f_write_s3v41br(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplubjng587610o), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_clopc8o(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_clopc8o F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_ssd7gce6jed13));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(6));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplvkpfj3qq0aig), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dgsisu6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_dgsisu6 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5k1m00kjtc17));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
    gm_tftb1odc739t5 t4 = gm_f_write_s3v41br(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tpl65jqebqot15g), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dqbiqk0(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_dqbiqk0 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sjeopa747clpb));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(3));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplvdft4iatcp17), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_dte2pnq(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_dte2pnq F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sr54681eccerj));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpll487d6t52m4g), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f4a3la5(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_f4a3la5 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sk3qij0lrr43j));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(6));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tpldvbj2judb6g5), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fhjt0ac(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_fhjt0ac F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stgfndqo1l714));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
    gm_tftb1odc739t5 t4 = gm_f_write_s3v41br(t2, &(*(gm_tfeab279c16rr *)((uint8_t *)a1)), ((void *)&gm_tplpgeniqejbt43), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_h9k6ni9(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_h9k6ni9 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sbf97lnc5053k));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(9));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplcte0qqjaa0q0), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_p018n9l(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_p018n9l F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s1vduqt3gpvdd));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(9));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplu7d9ofnf77qa), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rc0tik1(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_rc0tik1 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sss3bmvruos1t));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(8));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplk51k04l6756s), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_rkgcjv5(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_rkgcjv5 F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sjdenof9qo6nf));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(11));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tplvrmulr6rg858), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_tjlhtea(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_tjlhtea F;
    void * gm_l0; /* l */
    gm_t6j6smj7qoctu gm_l1; /* args */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb71rivinn7pn));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
    gm_tftb1odc739t5 t4 = gm_f_write_2venbiq(t2, &(*(gm_t6j6smj7qoctu *)((uint8_t *)a1)), ((void *)&gm_tpliufjdlri3svp), t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u5ur1qg(void * a0, gm_tnpifv20irnph a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_u5ur1qg F;
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
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sbrr14r7dj3rt));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(8));
    gm_tftb1odc739t5 t5 = gm_f_write_jog5i62(t2, t3, ((void *)&gm_tploblcdf1g38ca), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_decl_dump_iah0p30(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_decl_dump_iah0p30 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t3877pk7lv748 gm_l7; /* n */
    void * gm_l8; /* d */
    GM_STACK_CHECK(&F, 788u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tagrkbqh054h7 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_trqfvmjtt4sij t15;
    gm_f_list_uvfu1k1((void *)&t15, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l1 = t15;
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t17) + 200));
    gm_l2 = t18;
    gm_l3 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t19 = gm_l3;
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t21 = (t19 < t20);
            if (!t21) {
                goto gm_lb1;
            }
            {
                void * t22 = GM_LD(void *, ((uint8_t *)&gm_l2));
                uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint64_t t24 = gm_l3;
                {
                    {
                        memmove(((uint8_t *)&F.l4), (((uint8_t *)t22) + (uintptr_t)t24 * 184), 184);
                        uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 112));
                        if (t25) {
                            goto gm_lb3;
                        }
                        void * t26 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                        gm_tnpifv20irnph t28;
                        GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t27);
                        gm_tftb1odc739t5 t29 = gm_f_write_dte2pnq(t26, t28);
                        gm_l10 = t29;
                        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcush70170kjg3, 19, 9);
                        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 16));
                        gm_l5 = t31;
                        gm_l6 = ((uint64_t)0ULL);
                        {
                            for (;;) {
                                uint64_t t32 = gm_l6;
                                uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                uint8_t t34 = (t32 < t33);
                                if (!t34) {
                                    goto gm_lb6;
                                }
                                {
                                    void * t35 = GM_LD(void *, ((uint8_t *)&gm_l5));
                                    uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                                    uint64_t t37 = gm_l6;
                                    {
                                        {
                                            {
                                                memmove(((uint8_t *)&gm_l7), (((uint8_t *)t35) + (uintptr_t)t37 * 24), 24);
                                            }
                                            void * t38 = gm_l0;
                                            void * t39 = GM_LD(void *, ((uint8_t *)t38));
                                            void * t40 = GM_LD(void *, (((uint8_t *)t39) + 240));
                                            uint64_t t41 = GM_LD(uint64_t, ((((uint8_t *)t39) + 240) + 8));
                                            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 16));
                                            if ((uintptr_t)t42 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t42, t41, gm_srcush70170kjg3, 21, 19);
                                            void * t43 = (void *)(((uint8_t *)t40) + (uintptr_t)t42 * 216);
                                            gm_l8 = t43;
                                            void * t44 = (void *)((uint8_t *)&F.l1);
                                            void * t45 = gm_l8;
                                            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)t45));
                                            gm_tnpifv20irnph t47;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t47 + 0), t46);
                                            gm_tftb1odc739t5 t48 = gm_f_write_rc0tik1(t44, t47);
                                            gm_l11 = t48;
                                            uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                            if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcush70170kjg3, 22, 13);
                                            {
                                                void * t50 = gm_l0;
                                                uint64_t t51 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 16));
                                                gm_f_state_a5bnsjb((void *)&gm_l12, (void *)NULL, cx, NULL, t50, t51);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                            }
                                            {
                                                {
                                                    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l12) + 0));
                                                    uint8_t t53 = (uint8_t)(t52 == ((uint8_t)2ULL));
                                                    if (!t53) {
                                                        goto gm_lb12;
                                                    }
                                                    {
                                                        memmove(((uint8_t *)&F.l9), (((uint8_t *)&gm_l12) + 8), 208);
                                                    }
                                                    {
                                                        void * t54 = gm_l0;
                                                        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 16));
                                                        void * t56 = (void *)((uint8_t *)&F.l1);
                                                        gm_b0_1 t57 = gm_f_dump_value_fcdgoej((void *)NULL, cx, NULL, t54, t55, &(*(gm_t6qooom8qa2do *)((uint8_t *)&F.l9)), t56);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                    }
                                                    goto gm_lb11;
                                                }
gm_lb12:;
                                                {
                                                    void * t58 = (void *)((uint8_t *)&F.l1);
                                                    gm_b16_8 t59;
                                                    GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s68avpqal8nbc));
                                                    GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(11));
                                                    gm_tftb1odc739t5 t60 = gm_f_extend_0cs85gs(t58, t59);
                                                    gm_l13 = t60;
                                                    uint8_t t61 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                                                    if (!t61) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcush70170kjg3, 25, 22);
                                                    goto gm_lb11;
                                                }
gm_lb13:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 23, 13);
                                            }
gm_lb11:;
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 20, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t62 = gm_l6;
                                uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                                uint64_t t65 = r63;
                                gm_l6 = t65;
                            }
                        }
gm_lb6:;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 17, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t66 = gm_l3;
            uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
            uint64_t t69 = r67;
            gm_l3 = t69;
        }
    }
gm_lb1:;
    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&F.l1));
    gm_b16_8 t71 = GM_LD(gm_b16_8, ((uint8_t *)&t70));
    F.ret = t71;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_dump_adt_lmpn1o2(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2) {
    struct gm_fr_dump_adt_lmpn1o2 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* a */
    void * gm_l2; /* out */
    void * gm_l3; /* info */
    uint64_t gm_l5; /* n */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_t88rcucg1b7uk gm_l7; /* tag */
    gm_b16_8 gm_l9; /* fields */
    gm_b16_8 gm_l10; /* variants */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t88rcucg1b7uk gm_l14; /* p */
    GM_STACK_CHECK(&F, 980u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tftb1odc739t5 gm_l20;
    gm_tftb1odc739t5 gm_l21;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    void * t21 = gm_l0;
    void * t22 = GM_LD(void *, (((uint8_t *)t21) + 1088));
    uint64_t t23 = GM_LD(uint64_t, ((((uint8_t *)t21) + 1088) + 8));
    uint32_t t24 = gm_l1;
    uint64_t t25 = (uint64_t)t24;
    if ((uintptr_t)t25 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t25, t23, gm_srcush70170kjg3, 76, 14);
    void * t26 = (void *)(((uint8_t *)t22) + (uintptr_t)t25 * 184);
    gm_l3 = t26;
    void * t27 = gm_l3;
    {
        {
            uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t27) + 24) + 0));
            uint8_t t29 = (uint8_t)(t28 == ((uint8_t)0ULL));
            if (!t29) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l4), ((((uint8_t *)t27) + 24) + 8), 48);
            void * t30 = gm_l2;
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sc49g68j2cekh));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(6));
            gm_tftb1odc739t5 t32 = gm_f_extend_0cs85gs(t30, t31);
            gm_l15 = t32;
            uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
            if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcush70170kjg3, 79, 13);
            uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 16));
            if (t34) {
                void * t35 = gm_l2;
                gm_b16_8 t36;
                GM_ST(void *, (uint8_t *)&t36, (void *)(gm_sqhj00be09q5v));
                GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(8));
                gm_tftb1odc739t5 t37 = gm_f_extend_0cs85gs(t35, t36);
                gm_l16 = t37;
                uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcush70170kjg3, 80, 29);
            }
            {
                {
                    uint8_t t39 = (uint8_t)(((((uint8_t *)&F.l4) + 24))[8] != 0);
                    if (!t39) {
                        goto gm_lb4;
                    }
                    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 24));
                    gm_l5 = t40;
                    void * t41 = gm_l2;
                    uint64_t t42 = gm_l5;
                    gm_t4ce0jd3tdro4 t43;
                    GM_ST(uint64_t, ((uint8_t *)&t43 + 0), t42);
                    gm_tftb1odc739t5 t44 = gm_f_write_7bug452(t41, t43);
                    gm_l17 = t44;
                    uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
                    if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcush70170kjg3, 81, 33);
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            {
                {
                    uint8_t t46 = (uint8_t)(((((uint8_t *)&F.l4) + 40))[4] != 0);
                    if (!t46) {
                        goto gm_lb6;
                    }
                    gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l4) + 40));
                    gm_l6 = t47;
                    void * t48 = gm_l2;
                    void * t49 = gm_l0;
                    gm_t88rcucg1b7uk t50 = gm_l6;
                    gm_b16_8 t51 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t49, t50);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t52;
                    GM_ST(gm_b16_8, ((uint8_t *)&t52 + 0), t51);
                    gm_tftb1odc739t5 t53 = gm_f_write_0g0ps7j(t48, t52);
                    gm_l18 = t53;
                    uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                    if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcush70170kjg3, 82, 32);
                    goto gm_lb5;
                }
gm_lb6:;
            }
gm_lb5:;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)t27) + 24) + 0));
            uint8_t t56 = (uint8_t)(t55 == ((uint8_t)1ULL));
            if (!t56) {
                goto gm_lb7;
            }
            gm_t88rcucg1b7uk t57 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t27) + 24) + 8));
            gm_l7 = t57;
            void * t58 = gm_l2;
            void * t59 = gm_l0;
            gm_t88rcucg1b7uk t60 = gm_l7;
            gm_b16_8 t61 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t59, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t62;
            GM_ST(gm_b16_8, ((uint8_t *)&t62 + 0), t61);
            gm_tftb1odc739t5 t63 = gm_f_write_p018n9l(t58, t62);
            gm_l19 = t63;
            uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
            if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcush70170kjg3, 84, 27);
            goto gm_lb1;
        }
gm_lb7:;
        {
            uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)t27) + 24) + 0));
            uint8_t t66 = (uint8_t)(t65 == ((uint8_t)2ULL));
            if (!t66) {
                goto gm_lb8;
            }
            void * t67 = gm_l2;
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sf4cn9nvrjhp1));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(14));
            gm_tftb1odc739t5 t69 = gm_f_extend_0cs85gs(t67, t68);
            gm_l20 = t69;
            uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
            if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 1, gm_srcush70170kjg3, 85, 23);
            goto gm_lb1;
        }
gm_lb8:;
        gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 77, 5);
    }
gm_lb1:;
    void * t71 = gm_l2;
    void * t72 = gm_l3;
    gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)t72));
    gm_tnpifv20irnph t74;
    GM_ST(gm_b16_8, ((uint8_t *)&t74 + 0), t73);
    gm_tftb1odc739t5 t75 = gm_f_write_dqbiqk0(t71, t74);
    gm_l21 = t75;
    uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l21))[0] != 0);
    if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l21) + 1, gm_srcush70170kjg3, 87, 5);
    void * t77 = gm_l0;
    void * t78 = gm_l3;
    gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)t78) + 120));
    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
    void * t81 = gm_l2;
    gm_b0_1 t82 = gm_f_dump_params_gh24g4q((void *)NULL, cx, NULL, t77, t80, t81);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t83 = gm_l2;
    gm_tftb1odc739t5 t84 = gm_f_push_t83rg2m(t83, ((uint8_t)10ULL));
    gm_l22 = t84;
    uint8_t t85 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
    if (!t85) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srcush70170kjg3, 89, 5);
    void * t86 = gm_l3;
    {
        {
            uint8_t t87 = GM_LD(uint8_t, ((((uint8_t *)t86) + 24) + 0));
            uint8_t t88 = (uint8_t)(t87 == ((uint8_t)0ULL));
            if (!t88) {
                goto gm_lb10;
            }
            memmove(((uint8_t *)&F.l8), ((((uint8_t *)t86) + 24) + 8), 48);
            void * t89 = gm_l0;
            gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
            void * t92 = gm_l3;
            gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)t92) + 80));
            gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
            void * t95 = gm_l2;
            gm_b0_1 t96 = gm_f_dump_fields_tli1mqp((void *)NULL, cx, NULL, t89, t91, t94, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb9;
        }
gm_lb10:;
        {
            uint8_t t97 = GM_LD(uint8_t, ((((uint8_t *)t86) + 24) + 0));
            uint8_t t98 = (uint8_t)(t97 == ((uint8_t)2ULL));
            if (!t98) {
                goto gm_lb11;
            }
            gm_b16_8 t99 = GM_LD(gm_b16_8, ((((uint8_t *)t86) + 24) + 8));
            gm_l9 = t99;
            void * t100 = gm_l0;
            gm_b16_8 t101 = gm_l9;
            gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
            void * t103 = gm_l3;
            gm_b16_8 t104 = GM_LD(gm_b16_8, (((uint8_t *)t103) + 80));
            gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
            void * t106 = gm_l2;
            gm_b0_1 t107 = gm_f_dump_fields_tli1mqp((void *)NULL, cx, NULL, t100, t102, t105, t106);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb9;
        }
gm_lb11:;
        {
            uint8_t t108 = GM_LD(uint8_t, ((((uint8_t *)t86) + 24) + 0));
            uint8_t t109 = (uint8_t)(t108 == ((uint8_t)1ULL));
            if (!t109) {
                goto gm_lb12;
            }
            gm_b16_8 t110 = GM_LD(gm_b16_8, (((((uint8_t *)t86) + 24) + 8) + 8));
            gm_l10 = t110;
            gm_b16_8 t111 = gm_l10;
            gm_l11 = t111;
            gm_l12 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t112 = gm_l12;
                    uint64_t t113 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint8_t t114 = (t112 < t113);
                    if (!t114) {
                        goto gm_lb13;
                    }
                    {
                        void * t115 = GM_LD(void *, ((uint8_t *)&gm_l11));
                        uint64_t t116 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                        uint64_t t117 = gm_l12;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&F.l13), (((uint8_t *)t115) + (uintptr_t)t117 * 72), 72);
                                }
                                void * t118 = gm_l2;
                                gm_b16_8 t119 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                                gm_tnpifv20irnph t120;
                                GM_ST(gm_b16_8, ((uint8_t *)&t120 + 0), t119);
                                gm_tftb1odc739t5 t121 = gm_f_write_f4a3la5(t118, t120);
                                gm_l23 = t121;
                                uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                                if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcush70170kjg3, 94, 13);
                                {
                                    {
                                        uint8_t t123 = (uint8_t)(((((uint8_t *)&F.l13) + 16))[4] != 0);
                                        if (!t123) {
                                            goto gm_lb19;
                                        }
                                        gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l13) + 16));
                                        gm_l14 = t124;
                                        void * t125 = gm_l2;
                                        void * t126 = gm_l0;
                                        gm_t88rcucg1b7uk t127 = gm_l14;
                                        gm_b16_8 t128 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t126, t127);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tnpifv20irnph t129;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t129 + 0), t128);
                                        gm_tftb1odc739t5 t130 = gm_f_write_7ega8pl(t125, t129);
                                        gm_l24 = t130;
                                        uint8_t t131 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                                        if (!t131) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcush70170kjg3, 95, 33);
                                        goto gm_lb18;
                                    }
gm_lb19:;
                                }
gm_lb18:;
                                {
                                    void * t132 = gm_l2;
                                    gm_b16_8 t133 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l13) + 24)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_tnpifv20irnph t134;
                                    GM_ST(gm_b16_8, ((uint8_t *)&t134 + 0), t133);
                                    gm_tftb1odc739t5 t135 = gm_f_write_3qu59rs(t132, t134);
                                    gm_l25 = t135;
                                }
                                uint8_t t136 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                                if (!t136) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcush70170kjg3, 96, 13);
                                goto gm_lb16;
                            }
gm_lb17:;
                            gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 93, 32);
                        }
gm_lb16:;
                    }
gm_lb15:;
                    uint64_t t137 = gm_l12;
                    uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                    uint64_t t140 = r138;
                    gm_l12 = t140;
                }
            }
gm_lb13:;
            goto gm_lb9;
        }
gm_lb12:;
        gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 90, 5);
    }
gm_lb9:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_dump_fields_tli1mqp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_dump_fields_tli1mqp F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* fields */
    gm_b16_8 gm_l2; /* defaults */
    void * gm_l3; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    uint64_t gm_l8; /* n */
    GM_STACK_CHECK(&F, 509u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t8ta482bl6be0 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_b16_8 t11 = gm_l1;
    gm_l4 = t11;
    gm_l5 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t12 = gm_l5;
            uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
            uint8_t t14 = (t12 < t13);
            if (!t14) {
                goto gm_lb1;
            }
            {
                {
                    uint64_t t15 = gm_l5;
                    void * t16 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t18 = gm_l5;
                    gm_t0mglksrkfa5e t19 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)t16) + (uintptr_t)t18 * 40));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l9 + 0), t15);
                    GM_ST(gm_t0mglksrkfa5e, ((uint8_t *)&gm_l9 + 8), t19);
                }
                {
                    {
                        uint64_t t20 = GM_LD(uint64_t, ((uint8_t *)&gm_l9));
                        gm_l6 = t20;
                        {
                            memmove(((uint8_t *)&F.l7), (((uint8_t *)&gm_l9) + 8), 40);
                        }
                        {
                            void * t21 = gm_l3;
                            gm_t9qkh0rsooc8d t22 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l7) + 20));
                            gm_b16_8 t23 = gm_f_visibility_prefix_pbt2jhd(t22);
                            gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                            void * t25 = gm_l0;
                            gm_t88rcucg1b7uk t26 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l7) + 16));
                            gm_b16_8 t27 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t25, t26);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tfeab279c16rr t28;
                            GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t23);
                            GM_ST(gm_b16_8, ((uint8_t *)&t28 + 16), t24);
                            GM_ST(gm_b16_8, ((uint8_t *)&t28 + 32), t27);
                            gm_tftb1odc739t5 t29 = gm_f_write_fhjt0ac(t21, &t28);
                            gm_l10 = t29;
                        }
                        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcush70170kjg3, 103, 9);
                        {
                            {
                                uint8_t t31 = (uint8_t)(((((uint8_t *)&F.l7) + 24))[8] != 0);
                                if (!t31) {
                                    goto gm_lb7;
                                }
                                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&F.l7) + 24));
                                gm_l8 = t32;
                                void * t33 = gm_l3;
                                uint64_t t34 = gm_l8;
                                gm_t4ce0jd3tdro4 t35;
                                GM_ST(uint64_t, ((uint8_t *)&t35 + 0), t34);
                                gm_tftb1odc739t5 t36 = gm_f_write_7bug452(t33, t35);
                                gm_l11 = t36;
                                uint8_t t37 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                if (!t37) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcush70170kjg3, 104, 27);
                                goto gm_lb6;
                            }
gm_lb7:;
                        }
gm_lb6:;
                        {
                            {
                                uint64_t t38 = gm_l6;
                                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint8_t t40 = (t38 < t39);
                                if (!t40) {
                                    goto gm_lb9;
                                }
                                void * t41 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint64_t t43 = gm_l6;
                                uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)t41) + (uintptr_t)t43 * 1));
                                if (!t44) {
                                    goto gm_lb9;
                                }
                                void * t45 = gm_l3;
                                gm_b16_8 t46;
                                GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sdt9q53qgnu87));
                                GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(6));
                                gm_tftb1odc739t5 t47 = gm_f_extend_0cs85gs(t45, t46);
                                gm_l12 = t47;
                                uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcush70170kjg3, 105, 47);
                                goto gm_lb8;
                            }
gm_lb9:;
                        }
gm_lb8:;
                        void * t49 = gm_l3;
                        gm_tftb1odc739t5 t50 = gm_f_push_t83rg2m(t49, ((uint8_t)10ULL));
                        gm_l13 = t50;
                        uint8_t t51 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t51) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcush70170kjg3, 106, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 102, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t52 = gm_l5;
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            uint64_t t55 = r53;
            gm_l5 = t55;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_dump_params_gh24g4q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_dump_params_gh24g4q F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* params */
    void * gm_l2; /* out */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint32_t gm_l6; /* p */
    gm_t88rcucg1b7uk gm_l7; /* t */
    gm_b16_8 gm_l8; /* kind */
    GM_STACK_CHECK(&F, 493u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l9;
    gm_tn68el8d14vku gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t12 = ((t11 == ((uint64_t)0ULL)));
    if (t12) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    void * t13 = gm_l2;
    gm_tftb1odc739t5 t14 = gm_f_push_t83rg2m(t13, ((uint8_t)40ULL));
    gm_l9 = t14;
    uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcush70170kjg3, 113, 5);
    gm_b16_8 t16 = gm_l1;
    gm_l3 = t16;
    gm_l4 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t17 = gm_l4;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t19 = (t17 < t18);
            if (!t19) {
                goto gm_lb1;
            }
            {
                uint64_t t20 = gm_l4;
                void * t21 = GM_LD(void *, ((uint8_t *)&gm_l3));
                uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                uint64_t t23 = gm_l4;
                uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)t21) + (uintptr_t)t23 * 4));
                gm_tn68el8d14vku t25;
                GM_ST(uint32_t, (uint8_t *)&t25 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t25 + 0), t20);
                GM_ST(uint32_t, ((uint8_t *)&t25 + 8), t24);
                gm_l10 = t25;
                {
                    {
                        uint64_t t26 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                        gm_l5 = t26;
                        uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 8));
                        gm_l6 = t27;
                        uint64_t t28 = gm_l5;
                        uint8_t t29 = (t28 > ((uint64_t)0ULL));
                        if (t29) {
                            void * t30 = gm_l2;
                            gm_b16_8 t31;
                            GM_ST(void *, (uint8_t *)&t31, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t32 = gm_f_extend_0cs85gs(t30, t31);
                            gm_l11 = t32;
                            uint8_t t33 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                            if (!t33) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcush70170kjg3, 115, 20);
                        }
                        void * t34 = gm_l0;
                        void * t35 = GM_LD(void *, (((uint8_t *)t34) + 1128));
                        uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)t34) + 1128) + 8));
                        uint32_t t37 = gm_l6;
                        uint64_t t38 = (uint64_t)t37;
                        if ((uintptr_t)t38 >= (uintptr_t)(t36)) gm_trap_index((uintptr_t)t38, t36, gm_srcush70170kjg3, 116, 23);
                        {
                            {
                                uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t35) + (uintptr_t)t38 * 48) + 0));
                                uint8_t t40 = (uint8_t)(t39 == ((uint8_t)1ULL));
                                if (!t40) {
                                    goto gm_lb7;
                                }
                                gm_b16_8 t41;
                                GM_ST(void *, (uint8_t *)&t41, (void *)(gm_sgbumttiou4de));
                                GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(7));
                                gm_l8 = t41;
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t35) + (uintptr_t)t38 * 48) + 0));
                                uint8_t t43 = (uint8_t)(t42 == ((uint8_t)0ULL));
                                if (!t43) {
                                    goto gm_lb8;
                                }
                                gm_b16_8 t44;
                                GM_ST(void *, (uint8_t *)&t44, (void *)(gm_s2737i1iq57nf));
                                GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(4));
                                gm_l8 = t44;
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t35) + (uintptr_t)t38 * 48) + 0));
                                uint8_t t46 = (uint8_t)(t45 == ((uint8_t)2ULL));
                                if (!t46) {
                                    goto gm_lb9;
                                }
                                gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t35) + (uintptr_t)t38 * 48) + 4));
                                gm_l7 = t47;
                                void * t48 = gm_l0;
                                gm_t88rcucg1b7uk t49 = gm_l7;
                                gm_b16_8 t50 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t48, t49);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l8 = t50;
                                goto gm_lb6;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 116, 17);
                        }
gm_lb6:;
                        void * t51 = gm_l2;
                        void * t52 = gm_l0;
                        void * t53 = (void *)(((uint8_t *)t52) + 8);
                        uint32_t t54 = gm_l6;
                        gm_b16_8 t55 = gm_f_param_name_rbnop5l(t53, t54);
                        gm_b16_8 t56 = gm_l8;
                        gm_t6j6smj7qoctu t57;
                        GM_ST(gm_b16_8, ((uint8_t *)&t57 + 0), t55);
                        GM_ST(gm_b16_8, ((uint8_t *)&t57 + 16), t56);
                        gm_tftb1odc739t5 t58 = gm_f_write_8jvglca(t51, &t57);
                        gm_l12 = t58;
                        uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcush70170kjg3, 121, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 114, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t60 = gm_l4;
            uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
            uint64_t t63 = r61;
            gm_l4 = t63;
        }
    }
gm_lb1:;
    void * t64 = gm_l2;
    gm_tftb1odc739t5 t65 = gm_f_push_t83rg2m(t64, ((uint8_t)41ULL));
    gm_l13 = t65;
    uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
    if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcush70170kjg3, 123, 5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_dump_value_fcdgoej(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, const void * a2, void * a3) {
    struct gm_fr_dump_value_fcdgoej F;
    void * gm_l0; /* s */
    uint64_t gm_l1; /* id */
    void * gm_l3; /* out */
    gm_t88rcucg1b7uk gm_l4; /* t */
    uint32_t gm_l5; /* a */
    gm_b16_8 gm_l6; /* args */
    uint32_t gm_l7; /* a */
    uint32_t gm_l8; /* e */
    void * gm_l9; /* info */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_b16_8 gm_l13; /* kind */
    gm_tiilmfd16v3fc gm_l14; /* r */
    gm_tnpifv20irnph gm_l15; /* set */
    uint32_t gm_l16; /* f */
    void * gm_l17; /* sig */
    uint32_t gm_l18; /* e */
    uint32_t gm_l19; /* i */
    gm_t88rcucg1b7uk gm_l20; /* t */
    uint64_t gm_l22; /* m */
    gm_t88rcucg1b7uk gm_l23; /* t */
    GM_STACK_CHECK(&F, 1793u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_t53at80pfdus3 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tftb1odc739t5 gm_l30;
    gm_tftb1odc739t5 gm_l31;
    gm_tftb1odc739t5 gm_l32;
    gm_tftb1odc739t5 gm_l33;
    gm_tftb1odc739t5 gm_l34;
    gm_tftb1odc739t5 gm_l35;
    gm_tftb1odc739t5 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tftb1odc739t5 gm_l38;
    {
        {
            {
                {
                    uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t27 = (uint8_t)(t26 == ((uint8_t)0ULL));
                    if (!t27) {
                        goto gm_lb2;
                    }
                    gm_t88rcucg1b7uk t28 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 8));
                    gm_l4 = t28;
                    {
                        {
                            void * t29 = gm_l0;
                            void * t30 = (void *)(((uint8_t *)t29) + 8);
                            gm_t88rcucg1b7uk t31 = gm_l4;
                            gm_f_kind_jcnt4hm((void *)&gm_l24, t30, GM_CONV(gm_csguankko4lmhd, t31));
                            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l24) + 0));
                            uint8_t t33 = (uint8_t)(t32 == ((uint8_t)17ULL));
                            if (!t33) {
                                goto gm_lb4;
                            }
                            uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 8));
                            gm_l5 = t34;
                            gm_b16_8 t35 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l24) + 8) + 8));
                            gm_l6 = t35;
                            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                            uint8_t t37 = ((t36 == ((uint64_t)0ULL)));
                            if (!t37) {
                                goto gm_lb4;
                            }
                            void * t38 = gm_l0;
                            void * t39 = GM_LD(void *, (((uint8_t *)t38) + 1088));
                            uint64_t t40 = GM_LD(uint64_t, ((((uint8_t *)t38) + 1088) + 8));
                            uint32_t t41 = gm_l5;
                            uint64_t t42 = (uint64_t)t41;
                            if ((uintptr_t)t42 >= (uintptr_t)(t40)) gm_trap_index((uintptr_t)t42, t40, gm_srcush70170kjg3, 35, 69);
                            gm_tf5srh4mp4b3i t43 = GM_LD(gm_tf5srh4mp4b3i, ((((uint8_t *)t39) + (uintptr_t)t42 * 184) + 168));
                            uint64_t t44 = gm_l1;
                            gm_tf5srh4mp4b3i t45;
                            GM_ST(uint8_t, (uint8_t *)&t45 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t45 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t45 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t45), t44);
                            ((uint8_t *)&t45)[8] = 1;
                            uint8_t t46 = (gm_eq7mri15hk78(((uint8_t *)&t43), ((uint8_t *)&t45)));
                            if (!t46) {
                                goto gm_lb4;
                            }
                            void * t47 = gm_l0;
                            uint32_t t48 = gm_l5;
                            void * t49 = gm_l3;
                            gm_b0_1 t50 = gm_f_dump_adt_lmpn1o2((void *)NULL, cx, NULL, t47, t48, t49);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            goto gm_lb3;
                        }
gm_lb4:;
                        void * t51 = gm_l3;
                        void * t52 = gm_l0;
                        gm_t88rcucg1b7uk t53 = gm_l4;
                        gm_b16_8 t54 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t52, t53);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tnpifv20irnph t55;
                        GM_ST(gm_b16_8, ((uint8_t *)&t55 + 0), t54);
                        gm_tftb1odc739t5 t56 = gm_f_write_u5ur1qg(t51, t55);
                        gm_l25 = t56;
                        uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                        if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcush70170kjg3, 38, 17);
                    }
gm_lb3:;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t59 = (uint8_t)(t58 == ((uint8_t)1ULL));
                    if (!t59) {
                        goto gm_lb5;
                    }
                    uint32_t t60 = GM_LD(uint32_t, (((uint8_t *)a2) + 8));
                    gm_l7 = t60;
                    void * t61 = gm_l3;
                    gm_b16_8 t62;
                    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sh4sp5o4s4bj1));
                    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(8));
                    gm_tftb1odc739t5 t63 = gm_f_extend_0cs85gs(t61, t62);
                    gm_l26 = t63;
                    uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                    if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcush70170kjg3, 42, 13);
                    void * t65 = gm_l0;
                    uint32_t t66 = gm_l7;
                    void * t67 = gm_l3;
                    gm_b0_1 t68 = gm_f_dump_adt_lmpn1o2((void *)NULL, cx, NULL, t65, t66, t67);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb5:;
            }
            {
                {
                    uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t70 = (uint8_t)(t69 == ((uint8_t)2ULL));
                    if (!t70) {
                        goto gm_lb6;
                    }
                    uint32_t t71 = GM_LD(uint32_t, (((uint8_t *)a2) + 8));
                    gm_l8 = t71;
                    void * t72 = gm_l0;
                    void * t73 = GM_LD(void *, (((uint8_t *)t72) + 1048));
                    uint64_t t74 = GM_LD(uint64_t, ((((uint8_t *)t72) + 1048) + 8));
                    uint32_t t75 = gm_l8;
                    uint64_t t76 = (uint64_t)t75;
                    if ((uintptr_t)t76 >= (uintptr_t)(t74)) gm_trap_index((uintptr_t)t76, t74, gm_srcush70170kjg3, 46, 22);
                    void * t77 = (void *)(((uint8_t *)t73) + (uintptr_t)t76 * 56);
                    gm_l9 = t77;
                    void * t78 = gm_l3;
                    void * t79 = gm_l9;
                    gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)t79));
                    gm_tnpifv20irnph t81;
                    GM_ST(gm_b16_8, ((uint8_t *)&t81 + 0), t80);
                    gm_tftb1odc739t5 t82 = gm_f_write_7edu08o(t78, t81);
                    gm_l27 = t82;
                    uint8_t t83 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                    if (!t83) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcush70170kjg3, 47, 13);
                    void * t84 = gm_l0;
                    void * t85 = gm_l9;
                    gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)t85) + 24));
                    gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
                    void * t88 = gm_l3;
                    gm_b0_1 t89 = gm_f_dump_params_gh24g4q((void *)NULL, cx, NULL, t84, t87, t88);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t90 = gm_l3;
                    gm_tftb1odc739t5 t91 = gm_f_push_t83rg2m(t90, ((uint8_t)10ULL));
                    gm_l28 = t91;
                    uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                    if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcush70170kjg3, 49, 13);
                    void * t93 = gm_l9;
                    gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)t93) + 40));
                    gm_l10 = t94;
                    gm_l11 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            uint64_t t95 = gm_l11;
                            uint64_t t96 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                            uint8_t t97 = (t95 < t96);
                            if (!t97) {
                                goto gm_lb7;
                            }
                            {
                                void * t98 = GM_LD(void *, ((uint8_t *)&gm_l10));
                                uint64_t t99 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                                uint64_t t100 = gm_l11;
                                {
                                    {
                                        {
                                            memmove(((uint8_t *)&F.l12), (((uint8_t *)t98) + (uintptr_t)t100 * 48), 48);
                                        }
                                        {
                                            {
                                                uint8_t t101 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 16) + 0));
                                                uint8_t t102 = (uint8_t)(t101 == ((uint8_t)0ULL));
                                                if (!t102) {
                                                    goto gm_lb13;
                                                }
                                                gm_b16_8 t103;
                                                GM_ST(void *, (uint8_t *)&t103, (void *)(gm_s579fb7l5l6df));
                                                GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(2));
                                                gm_l13 = t103;
                                                goto gm_lb12;
                                            }
gm_lb13:;
                                            {
                                                uint8_t t104 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 16) + 0));
                                                uint8_t t105 = (uint8_t)(t104 == ((uint8_t)1ULL));
                                                if (!t105) {
                                                    goto gm_lb14;
                                                }
                                                gm_b16_8 t106;
                                                GM_ST(void *, (uint8_t *)&t106, (void *)(gm_sb88rajuutaep));
                                                GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(3));
                                                gm_l13 = t106;
                                                goto gm_lb12;
                                            }
gm_lb14:;
                                            {
                                                uint8_t t107 = GM_LD(uint8_t, ((((uint8_t *)&F.l12) + 16) + 0));
                                                uint8_t t108 = (uint8_t)(t107 == ((uint8_t)2ULL));
                                                if (!t108) {
                                                    goto gm_lb15;
                                                }
                                                gm_b16_8 t109;
                                                GM_ST(void *, (uint8_t *)&t109, (void *)(gm_s66uvortgrtsg));
                                                GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(3));
                                                gm_l13 = t109;
                                                goto gm_lb12;
                                            }
gm_lb15:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 51, 25);
                                        }
gm_lb12:;
                                        {
                                            void * t110 = gm_l3;
                                            gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&F.l12));
                                            gm_b16_8 t112 = gm_l13;
                                            void * t113 = gm_l0;
                                            gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 24));
                                            gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
                                            gm_t88rcucg1b7uk t116 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l12) + 40));
                                            gm_tnpifv20irnph t117 = gm_f_empty_effects_r0iv3pv();
                                            gm_b16_8 t118 = gm_f_sig_text_i53adp7((void *)NULL, cx, NULL, t113, t115, t116, t117);
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_tfeab279c16rr t119;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t119 + 0), t111);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t119 + 16), t112);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t119 + 32), t118);
                                            gm_tftb1odc739t5 t120 = gm_f_write_dgsisu6(t110, &t119);
                                            gm_l29 = t120;
                                        }
                                        uint8_t t121 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                                        if (!t121) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcush70170kjg3, 56, 17);
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 50, 13);
                                }
gm_lb10:;
                            }
gm_lb9:;
                            uint64_t t122 = gm_l11;
                            uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                            uint64_t t125 = r123;
                            gm_l11 = t125;
                        }
                    }
gm_lb7:;
                    goto gm_lb1;
                }
gm_lb6:;
            }
            {
                {
                    uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t127 = (uint8_t)(t126 == ((uint8_t)3ULL));
                    if (!t127) {
                        goto gm_lb16;
                    }
                    memmove(((uint8_t *)&gm_l14), (((uint8_t *)a2) + 8), 24);
                    void * t128 = gm_l3;
                    void * t129 = gm_l0;
                    void * t130 = (void *)(((uint8_t *)t129) + 8);
                    void * t131 = (void *)((uint8_t *)&gm_l14);
                    gm_b16_8 t132 = gm_f_show_ref_aqt1t7c((void *)NULL, cx, NULL, t130, t131);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t133;
                    GM_ST(gm_b16_8, ((uint8_t *)&t133 + 0), t132);
                    gm_tftb1odc739t5 t134 = gm_f_write_9tu0dke(t128, t133);
                    gm_l30 = t134;
                    uint8_t t135 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                    if (!t135) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 1, gm_srcush70170kjg3, 59, 27);
                    goto gm_lb1;
                }
gm_lb16:;
                {
                    uint8_t t136 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t137 = (uint8_t)(t136 == ((uint8_t)4ULL));
                    if (!t137) {
                        goto gm_lb17;
                    }
                    gm_tnpifv20irnph t138 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)a2) + 8));
                    gm_l15 = t138;
                    void * t139 = gm_l3;
                    void * t140 = gm_l0;
                    void * t141 = (void *)(((uint8_t *)t140) + 8);
                    void * t142 = (void *)((uint8_t *)&gm_l15);
                    gm_b16_8 t143 = gm_f_show_effects_vieja9u((void *)NULL, cx, NULL, t141, t142);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t144;
                    GM_ST(gm_b16_8, ((uint8_t *)&t144 + 0), t143);
                    gm_tftb1odc739t5 t145 = gm_f_write_3j0395t(t139, t144);
                    gm_l31 = t145;
                    uint8_t t146 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                    if (!t146) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 1, gm_srcush70170kjg3, 60, 28);
                    goto gm_lb1;
                }
gm_lb17:;
                {
                    uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t148 = (uint8_t)(t147 == ((uint8_t)5ULL));
                    if (!t148) {
                        goto gm_lb18;
                    }
                    uint32_t t149 = GM_LD(uint32_t, (((uint8_t *)a2) + 8));
                    gm_l16 = t149;
                    void * t150 = gm_l0;
                    void * t151 = GM_LD(void *, (((uint8_t *)t150) + 344));
                    uint64_t t152 = GM_LD(uint64_t, ((((uint8_t *)t150) + 344) + 8));
                    uint32_t t153 = gm_l16;
                    uint64_t t154 = (uint64_t)t153;
                    if ((uintptr_t)t154 >= (uintptr_t)(t152)) gm_trap_index((uintptr_t)t154, t152, gm_srcush70170kjg3, 62, 21);
                    void * t155 = (void *)(((uint8_t *)t151) + (uintptr_t)t154 * 136);
                    gm_l17 = t155;
                    void * t156 = gm_l3;
                    void * t157 = gm_l0;
                    void * t158 = gm_l17;
                    gm_b16_8 t159 = GM_LD(gm_b16_8, (((uint8_t *)t158) + 40));
                    gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
                    void * t161 = gm_l17;
                    gm_t88rcucg1b7uk t162 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t161) + 56));
                    void * t163 = gm_l17;
                    gm_tnpifv20irnph t164 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)t163) + 64));
                    gm_b16_8 t165 = gm_f_sig_text_i53adp7((void *)NULL, cx, NULL, t157, t160, t162, t164);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t166;
                    GM_ST(gm_b16_8, ((uint8_t *)&t166 + 0), t165);
                    gm_tftb1odc739t5 t167 = gm_f_write_2gsrm6r(t156, t166);
                    gm_l32 = t167;
                    uint8_t t168 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                    if (!t168) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcush70170kjg3, 63, 13);
                    goto gm_lb1;
                }
gm_lb18:;
                {
                    uint8_t t169 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t170 = (uint8_t)(t169 == ((uint8_t)6ULL));
                    if (!t170) {
                        goto gm_lb19;
                    }
                    uint32_t t171 = GM_LD(uint32_t, (((uint8_t *)a2) + 8));
                    gm_l18 = t171;
                    uint32_t t172 = GM_LD(uint32_t, ((((uint8_t *)a2) + 8) + 4));
                    gm_l19 = t172;
                    void * t173 = gm_l3;
                    void * t174 = gm_l0;
                    void * t175 = (void *)(((uint8_t *)t174) + 8);
                    uint32_t t176 = gm_l18;
                    gm_b16_8 t177 = gm_f_effect_name_prpp0ip(t175, t176);
                    void * t178 = gm_l0;
                    void * t179 = GM_LD(void *, (((uint8_t *)t178) + 1048));
                    uint64_t t180 = GM_LD(uint64_t, ((((uint8_t *)t178) + 1048) + 8));
                    uint32_t t181 = gm_l18;
                    uint64_t t182 = (uint64_t)t181;
                    if ((uintptr_t)t182 >= (uintptr_t)(t180)) gm_trap_index((uintptr_t)t182, t180, gm_srcush70170kjg3, 65, 68);
                    void * t183 = GM_LD(void *, ((((uint8_t *)t179) + (uintptr_t)t182 * 56) + 40));
                    uint64_t t184 = GM_LD(uint64_t, (((((uint8_t *)t179) + (uintptr_t)t182 * 56) + 40) + 8));
                    uint32_t t185 = gm_l19;
                    uint64_t t186 = (uint64_t)t185;
                    if ((uintptr_t)t186 >= (uintptr_t)(t184)) gm_trap_index((uintptr_t)t186, t184, gm_srcush70170kjg3, 65, 68);
                    gm_b16_8 t187 = GM_LD(gm_b16_8, (((uint8_t *)t183) + (uintptr_t)t186 * 48));
                    gm_t6j6smj7qoctu t188;
                    GM_ST(gm_b16_8, ((uint8_t *)&t188 + 0), t177);
                    GM_ST(gm_b16_8, ((uint8_t *)&t188 + 16), t187);
                    gm_tftb1odc739t5 t189 = gm_f_write_tjlhtea(t173, &t188);
                    gm_l33 = t189;
                    uint8_t t190 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                    if (!t190) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 1, gm_srcush70170kjg3, 65, 22);
                    goto gm_lb1;
                }
gm_lb19:;
            }
            {
                {
                    uint8_t t191 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t192 = (uint8_t)(t191 == ((uint8_t)7ULL));
                    if (!t192) {
                        goto gm_lb20;
                    }
                    gm_t88rcucg1b7uk t193 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 8));
                    gm_l20 = t193;
                    void * t194 = gm_l3;
                    void * t195 = gm_l0;
                    gm_t88rcucg1b7uk t196 = gm_l20;
                    gm_b16_8 t197 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t195, t196);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t198;
                    GM_ST(gm_b16_8, ((uint8_t *)&t198 + 0), t197);
                    gm_tftb1odc739t5 t199 = gm_f_write_h9k6ni9(t194, t198);
                    gm_l34 = t199;
                    uint8_t t200 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                    if (!t200) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 1, gm_srcush70170kjg3, 66, 22);
                    goto gm_lb1;
                }
gm_lb20:;
                {
                    uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t202 = (uint8_t)(t201 == ((uint8_t)8ULL));
                    if (!t202) {
                        goto gm_lb21;
                    }
                    memmove(((uint8_t *)&F.l21), (((uint8_t *)a2) + 8), 200);
                    void * t203 = gm_l3;
                    void * t204 = (void *)((uint8_t *)&F.l21);
                    gm_b16_8 t205 = gm_f_dump_untyped_o8mt4q8((void *)NULL, cx, NULL, t204);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t206;
                    GM_ST(gm_b16_8, ((uint8_t *)&t206 + 0), t205);
                    gm_tftb1odc739t5 t207 = gm_f_write_rkgcjv5(t203, t206);
                    gm_l35 = t207;
                    uint8_t t208 = (uint8_t)!((((uint8_t *)&gm_l35))[0] != 0);
                    if (!t208) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l35) + 1, gm_srcush70170kjg3, 67, 24);
                    goto gm_lb1;
                }
gm_lb21:;
                {
                    uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t210 = (uint8_t)(t209 == ((uint8_t)9ULL));
                    if (!t210) {
                        goto gm_lb22;
                    }
                    uint64_t t211 = GM_LD(uint64_t, (((uint8_t *)a2) + 8));
                    gm_l22 = t211;
                    void * t212 = gm_l3;
                    void * t213 = gm_l0;
                    void * t214 = GM_LD(void *, ((uint8_t *)t213));
                    void * t215 = GM_LD(void *, (((uint8_t *)t214) + 200));
                    uint64_t t216 = GM_LD(uint64_t, ((((uint8_t *)t214) + 200) + 8));
                    uint64_t t217 = gm_l22;
                    if ((uintptr_t)t217 >= (uintptr_t)(t216)) gm_trap_index((uintptr_t)t217, t216, gm_srcush70170kjg3, 68, 48);
                    gm_b16_8 t218 = GM_LD(gm_b16_8, (((uint8_t *)t215) + (uintptr_t)t217 * 184));
                    gm_tnpifv20irnph t219;
                    GM_ST(gm_b16_8, ((uint8_t *)&t219 + 0), t218);
                    gm_tftb1odc739t5 t220 = gm_f_write_dte2pnq(t212, t219);
                    gm_l36 = t220;
                    uint8_t t221 = (uint8_t)!((((uint8_t *)&gm_l36))[0] != 0);
                    if (!t221) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l36) + 1, gm_srcush70170kjg3, 68, 23);
                    goto gm_lb1;
                }
gm_lb22:;
                {
                    uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t223 = (uint8_t)(t222 == ((uint8_t)10ULL));
                    if (!t223) {
                        goto gm_lb23;
                    }
                    gm_t88rcucg1b7uk t224 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a2) + 8));
                    gm_l23 = t224;
                    void * t225 = gm_l3;
                    void * t226 = gm_l0;
                    gm_t88rcucg1b7uk t227 = gm_l23;
                    gm_b16_8 t228 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t226, t227);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tnpifv20irnph t229;
                    GM_ST(gm_b16_8, ((uint8_t *)&t229 + 0), t228);
                    gm_tftb1odc739t5 t230 = gm_f_write_98kuve9(t225, t229);
                    gm_l37 = t230;
                    uint8_t t231 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                    if (!t231) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srcush70170kjg3, 69, 23);
                    goto gm_lb1;
                }
gm_lb23:;
            }
            {
                {
                    uint8_t t232 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
                    uint8_t t233 = (uint8_t)(t232 == ((uint8_t)11ULL));
                    if (!t233) {
                        goto gm_lb24;
                    }
                    void * t234 = gm_l3;
                    gm_b16_8 t235;
                    GM_ST(void *, (uint8_t *)&t235, (void *)(gm_s5cpmumv7vkcr));
                    GM_ST(uintptr_t, (uint8_t *)&t235 + 8, (uintptr_t)(6));
                    gm_tftb1odc739t5 t236 = gm_f_extend_0cs85gs(t234, t235);
                    gm_l38 = t236;
                    uint8_t t237 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
                    if (!t237) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srcush70170kjg3, 70, 19);
                    goto gm_lb1;
                }
gm_lb24:;
                gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 33, 5);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_sig_text_i53adp7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3) {
    struct gm_fr_sig_text_i53adp7 F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* params */
    gm_t88rcucg1b7uk gm_l2; /* ret */
    gm_tnpifv20irnph gm_l3; /* effects */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    GM_STACK_CHECK(&F, 812u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tftb1odc739t5 gm_l9;
    gm_tgsvmddj3e85e gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_b16_8 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    uint8_t gm_l14;
    gm_tftb1odc739t5 gm_l15;
    gm_tftb1odc739t5 gm_l16;
    gm_tftb1odc739t5 gm_l17;
    gm_tftb1odc739t5 gm_l18;
    gm_trqfvmjtt4sij t10;
    gm_f_list_uvfu1k1((void *)&t10, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.l4 = t10;
    void * t11 = (void *)((uint8_t *)&F.l4);
    gm_tftb1odc739t5 t12 = gm_f_push_t83rg2m(t11, ((uint8_t)40ULL));
    gm_l9 = t12;
    uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcush70170kjg3, 129, 5);
    gm_b16_8 t14 = gm_l1;
    gm_l5 = t14;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t15 = gm_l6;
            uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t17 = (t15 < t16);
            if (!t17) {
                goto gm_lb1;
            }
            {
                {
                    uint64_t t18 = gm_l6;
                    void * t19 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint64_t t21 = gm_l6;
                    gm_t5v0c8ia6a5mo t22 = GM_LD(gm_t5v0c8ia6a5mo, (((uint8_t *)t19) + (uintptr_t)t21 * 56));
                    GM_ST(uint64_t, ((uint8_t *)&gm_l10 + 0), t18);
                    GM_ST(gm_t5v0c8ia6a5mo, ((uint8_t *)&gm_l10 + 8), t22);
                }
                {
                    {
                        uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)&gm_l10));
                        gm_l7 = t23;
                        {
                            memmove(((uint8_t *)&F.l8), (((uint8_t *)&gm_l10) + 8), 56);
                        }
                        uint64_t t24 = gm_l7;
                        uint8_t t25 = (t24 > ((uint64_t)0ULL));
                        if (t25) {
                            void * t26 = (void *)((uint8_t *)&F.l4);
                            gm_b16_8 t27;
                            GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t28 = gm_f_extend_0cs85gs(t26, t27);
                            gm_l11 = t28;
                            uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                            if (!t29) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcush70170kjg3, 131, 20);
                        }
                        {
                            void * t30 = (void *)((uint8_t *)&F.l4);
                            gm_b16_8 t31 = GM_LD(gm_b16_8, ((uint8_t *)&F.l8));
                            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + 48));
                            if (t32) {
                                gm_b16_8 t33;
                                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_snenkhfev166s));
                                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(2));
                                gm_l12 = t33;
                            } else {
                                gm_b16_8 t34;
                                GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(0));
                                gm_l12 = t34;
                            }
                            gm_b16_8 t35 = gm_l12;
                            void * t36 = gm_l0;
                            gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l8) + 16));
                            gm_b16_8 t38 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t36, t37);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tfeab279c16rr t39;
                            GM_ST(gm_b16_8, ((uint8_t *)&t39 + 0), t31);
                            GM_ST(gm_b16_8, ((uint8_t *)&t39 + 16), t35);
                            GM_ST(gm_b16_8, ((uint8_t *)&t39 + 32), t38);
                            gm_tftb1odc739t5 t40 = gm_f_write_ajbca29(t30, &t39);
                            gm_l13 = t40;
                        }
                        uint8_t t41 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
                        if (!t41) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srcush70170kjg3, 132, 9);
                        {
                            {
                                uint8_t t42 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l8) + 32)) != 0);
                                if (t42) {
                                    goto gm_lb7;
                                }
                                gm_l14 = ((uint8_t)0);
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                gm_l14 = ((uint8_t)1);
                                goto gm_lb6;
                            }
gm_lb8:;
                            gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 133, 12);
                        }
gm_lb6:;
                        uint8_t t43 = gm_l14;
                        if (t43) {
                            void * t44 = (void *)((uint8_t *)&F.l4);
                            gm_b16_8 t45;
                            GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sdt9q53qgnu87));
                            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(6));
                            gm_tftb1odc739t5 t46 = gm_f_extend_0cs85gs(t44, t45);
                            gm_l15 = t46;
                            uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
                            if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 1, gm_srcush70170kjg3, 133, 36);
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 130, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t48 = gm_l6;
            uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
            uint64_t t51 = r49;
            gm_l6 = t51;
        }
    }
gm_lb1:;
    void * t52 = (void *)((uint8_t *)&F.l4);
    gm_tftb1odc739t5 t53 = gm_f_push_t83rg2m(t52, ((uint8_t)41ULL));
    gm_l16 = t53;
    uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
    if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 1, gm_srcush70170kjg3, 135, 5);
    void * t55 = gm_l0;
    void * t56 = (void *)(((uint8_t *)t55) + 8);
    gm_t88rcucg1b7uk t57 = gm_l2;
    uint8_t t58 = gm_f_is_unit_n83ttnb(t56, GM_CONV(gm_csguankko4lmhd, t57));
    uint8_t t59 = (uint8_t)!t58;
    if (t59) {
        void * t60 = (void *)((uint8_t *)&F.l4);
        void * t61 = gm_l0;
        gm_t88rcucg1b7uk t62 = gm_l2;
        gm_b16_8 t63 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t61, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t64;
        GM_ST(gm_b16_8, ((uint8_t *)&t64 + 0), t63);
        gm_tftb1odc739t5 t65 = gm_f_write_clopc8o(t60, t64);
        gm_l17 = t65;
        uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
        if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 1, gm_srcush70170kjg3, 136, 33);
    }
    uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t68 = (t67 > ((uint64_t)0ULL));
    if (t68) {
        void * t69 = (void *)((uint8_t *)&F.l4);
        void * t70 = gm_l0;
        void * t71 = (void *)(((uint8_t *)t70) + 8);
        void * t72 = (void *)((uint8_t *)&gm_l3);
        gm_b16_8 t73 = gm_f_show_effects_vieja9u((void *)NULL, cx, NULL, t71, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t74;
        GM_ST(gm_b16_8, ((uint8_t *)&t74 + 0), t73);
        gm_tftb1odc739t5 t75 = gm_f_write_aisrlpj(t69, t74);
        gm_l18 = t75;
        uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
        if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcush70170kjg3, 137, 34);
    }
    gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
    gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
    F.ret = t78;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_visibility_prefix_pbt2jhd(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visibility_prefix_pbt2jhd F;
    gm_t9qkh0rsooc8d gm_l0; /* vis */
    GM_STACK_CHECK_SHALLOW(&F, 119u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 gm_l1;
    {
        {
            uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t7 = (uint8_t)(t6 == ((uint8_t)2ULL));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s3bfo4sq48nsb));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(4));
            gm_l1 = t8;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)1ULL));
            if (!t10) {
                goto gm_lb3;
            }
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_sl80cge5ts0aj));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(13));
            gm_l1 = t11;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
            uint8_t t13 = (uint8_t)(t12 == ((uint8_t)0ULL));
            if (!t13) {
                goto gm_lb4;
            }
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(0));
            gm_l1 = t14;
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcush70170kjg3, 142, 59);
    }
gm_lb1:;
    gm_b16_8 t15 = gm_l1;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}

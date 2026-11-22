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
typedef struct { uint8_t a[8]; } gm_t2doi72kmmlil;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; } gm_tkpb4kplftgaa;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { uint64_t f0; uint64_t f1; gm_t2lqu8jsj511d f2; uint64_t f3; uint64_t f4; } gm_t1hfnlc9mhrjq;
_Static_assert(sizeof(gm_t2doi72kmmlil) == 8 && _Alignof(gm_t2doi72kmmlil) == 1 && offsetof(gm_t2doi72kmmlil, a) == 0, "layout of `[8]u8`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tn68el8d14vku v0; gm_tn68el8d14vku v1; gm_tf5srh4mp4b3i v2; uint8_t v3; void * v4; uint32_t v5; } f1; } gm_t76123gfet73g;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { uint64_t f0; uint64_t f1; gm_t9qkh0rsooc8d f2; uint8_t f3; uint8_t pe[6]; } gm_tilak255t5m34;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tkpb4kplftgaa) == 24 && _Alignof(gm_tkpb4kplftgaa) == 8 && offsetof(gm_tkpb4kplftgaa, f0) == 0 && offsetof(gm_tkpb4kplftgaa, f1) == 8 && offsetof(gm_tkpb4kplftgaa, f2) == 16, "layout of `(u64, u64, u64)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; uint8_t v1; } f1; } gm_t1ec8br9ddkav;
_Static_assert(sizeof(gm_t1hfnlc9mhrjq) == 48 && _Alignof(gm_t1hfnlc9mhrjq) == 8 && offsetof(gm_t1hfnlc9mhrjq, f0) == 0 && offsetof(gm_t1hfnlc9mhrjq, f1) == 8 && offsetof(gm_t1hfnlc9mhrjq, f2) == 16 && offsetof(gm_t1hfnlc9mhrjq, f3) == 32 && offsetof(gm_t1hfnlc9mhrjq, f4) == 40, "layout of `AllocTag`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t76123gfet73g) == 24 && _Alignof(gm_t76123gfet73g) == 8 && offsetof(gm_t76123gfet73g, f0) == 0 && offsetof(gm_t76123gfet73g, f1) == 8, "layout of `Flow`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; } f1; } gm_tcfa6l7reoosn;
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t76123gfet73g v0; gm_b16_8 v1; } f1; } gm_td0ept12e7q8b;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tilak255t5m34) == 24 && _Alignof(gm_tilak255t5m34) == 8 && offsetof(gm_tilak255t5m34, f0) == 0 && offsetof(gm_tilak255t5m34, f1) == 8 && offsetof(gm_tilak255t5m34, f2) == 16 && offsetof(gm_tilak255t5m34, f3) == 17, "layout of `(base: u64, size: u64, kind: AllocKind, live: bool)`");
typedef struct { gm_t1hfnlc9mhrjq f0; uint8_t f1; uint8_t pe[7]; } gm_tm7c53s68971m;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
typedef struct { gm_tilak255t5m34 f0; uint8_t f1; uint8_t pe[7]; } gm_tqvpud5umqqp1;
_Static_assert(sizeof(gm_t1ec8br9ddkav) == 32 && _Alignof(gm_t1ec8br9ddkav) == 8 && offsetof(gm_t1ec8br9ddkav, f0) == 0 && offsetof(gm_t1ec8br9ddkav, f1) == 8, "layout of `Flow!bool`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; uint64_t v1; } f1; } gm_t3j6egd6cfnk5;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
_Static_assert(sizeof(gm_tcfa6l7reoosn) == 32 && _Alignof(gm_tcfa6l7reoosn) == 8 && offsetof(gm_tcfa6l7reoosn, f0) == 0 && offsetof(gm_tcfa6l7reoosn, f1) == 8, "layout of `Flow!()`");
_Static_assert(sizeof(gm_td0ept12e7q8b) == 32 && _Alignof(gm_td0ept12e7q8b) == 8 && offsetof(gm_td0ept12e7q8b, f0) == 0 && offsetof(gm_td0ept12e7q8b, f1) == 8, "layout of `Flow!string`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; } f1; } gm_tgfdnufetdnfc;
_Static_assert(sizeof(gm_tm7c53s68971m) == 56 && _Alignof(gm_tm7c53s68971m) == 8 && offsetof(gm_tm7c53s68971m, f0) == 0 && offsetof(gm_tm7c53s68971m, f1) == 48, "layout of `?AllocTag`");
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tqvpud5umqqp1) == 32 && _Alignof(gm_tqvpud5umqqp1) == 8 && offsetof(gm_tqvpud5umqqp1, f0) == 0 && offsetof(gm_tqvpud5umqqp1, f1) == 24, "layout of `?(base: u64, size: u64, kind: AllocKind, live: bool)`");
_Static_assert(sizeof(gm_t3j6egd6cfnk5) == 48 && _Alignof(gm_t3j6egd6cfnk5) == 8 && offsetof(gm_t3j6egd6cfnk5, f0) == 0 && offsetof(gm_t3j6egd6cfnk5, f1) == 8, "layout of `MemError!u64`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
_Static_assert(sizeof(gm_tgfdnufetdnfc) == 48 && _Alignof(gm_tgfdnufetdnfc) == 8 && offsetof(gm_tgfdnufetdnfc, f0) == 0 && offsetof(gm_tgfdnufetdnfc, f1) == 8, "layout of `MemError!()`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fast_23em7sv {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_fill_abmf5g5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_37g0gks {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bv13gdl {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_e7344u2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_jc3ftg7 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_p0ej7hu {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_u3ioj90 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_unbcnc5 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_mem_trap_ecogko0 {
    gm_frame h;
    gm_tpekhfs32op0f l2; /* e */
    gm_t76123gfet73g ret;
};
struct gm_fr_memory_builtin_9s47dk8 {
    gm_frame h;
    gm_t1hfnlc9mhrjq l22; /* tag */
    gm_tpekhfs32op0f l39; /* e */
    gm_tpekhfs32op0f l42; /* e */
    gm_tpekhfs32op0f l45; /* e */
    gm_tcfa6l7reoosn ret;
};
struct gm_fr_ptr_of_b77b4c9 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_put_ptr_188p3mm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_put_u64_irp77r2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_tag_site_msl07u1 {
    gm_frame h;
    gm_tpekhfs32op0f l5; /* e */
    gm_td0ept12e7q8b ret;
};
struct gm_fr_tmp_8k6u9c9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_wr_word_npte9fs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_view_pebit6h {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_alloc_bfo8187(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3, uint8_t a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_describe_t62jhnt(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) void gm_f_equal_e5vf82h(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4);
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1);
inline __attribute__((aligned(4))) void gm_f_format_37g0gks(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_bv13gdl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_e7344u2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_fdoda7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_fja4j2u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_ig8f8pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_jc3ftg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_p0ej7hu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_q33vns6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_u3ioj90(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_unbcnc5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_get_hd4d6j0(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_int_pg7p4gc(void * a0, gm_csguankko4lmhd a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_kill_3kcavfd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_load_at_6u81jcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_make_some_tvb9ubk(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2);
__attribute__((aligned(4))) void gm_f_memory_builtin_9s47dk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t8588ma2cr9d7 a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_64ej4om(void * a0, uint64_t a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2);
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) void gm_f_remove_gjirbtf(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t2lqu8jsj511d gm_f_slice_parts_1se5hu4(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_store_at_ktabiji(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, uint64_t a2, uint64_t a3, gm_b16_8 a4);
__attribute__((aligned(4))) void gm_f_tag_site_msl07u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t2lqu8jsj511d a2, uint64_t a3, uint64_t a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((noinline)) gm_b16_8 gm_f_tmp_next_dalgo1f(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_trap_qh84l9d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_write_5gub0ul(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_ckh5e429gka[8] __attribute__((aligned(8)));
extern const uint8_t gm_s0ugop4l95o1j[6];
extern const uint8_t gm_s1gjt02j7um1i[37];
extern const uint8_t gm_s2qp8mqi0c2le[48];
extern const uint8_t gm_s76i8mdsri6em[70];
extern const uint8_t gm_s7j165k7b6shl[32];
extern const uint8_t gm_s8pst1ovjhtbk[71];
extern const uint8_t gm_sdv3b0kjlrdl2[46];
extern const uint8_t gm_sgooul7ab8sa1[65];
extern const uint8_t gm_sm884rqp4fp7u[68];
extern const uint8_t gm_so94vinsu4uib[50];
extern const uint8_t gm_sp0ag7curvl4o[42];
extern const uint8_t gm_sqfrv3g5v9m2n[5];
extern const uint8_t gm_sqofr58g480ri[39];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srccp2mbkjs1vn4[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcq7ohh8h77bn7[];
extern const uint8_t gm_sukbpodfceoe1[9];
extern const gm_tmpl gm_tpl4bpbmadpmu1i;
extern const gm_tmpl gm_tpl5prdhjdrg194;
extern const gm_tmpl gm_tplcein5d5pq2cs;
extern const gm_tmpl gm_tple7ukme0qchh6;
extern const gm_tmpl gm_tplf3e41f3siie3;
extern const gm_tmpl gm_tpllhk8pmjfso1j;
extern const gm_tmpl gm_tplrgo7romk0g20;
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_15fjgq1 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 16);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint64_t t5 = gm_f_align_of_k6eqqrn((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_align_of_k6eqqrn F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tbccvikg6b96e gm_l2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 64));
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&gm_l2, (void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_aligned_q6sor4h F;
    uint64_t gm_l0; /* addr */
    uint64_t gm_l1; /* align */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    uint8_t t3 = (t2 <= ((uint64_t)1ULL));
    if (t3) {
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
    uint64_t t4 = gm_l1;
    uint64_t t5 = gm_l1;
    uint64_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint64_t)1ULL), &r6);
    if (o7) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 17);
    uint64_t t8 = r6;
    uint64_t t9 = (uint64_t)(t4 & t8);
    uint8_t t10 = ((t9 == ((uint64_t)0ULL)));
    if (t10) {
        uint64_t t11 = gm_l0;
        uint64_t t12 = gm_l1;
        uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
        if (o14) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 197, 50);
        uint64_t t15 = r13;
        uint64_t t16 = (uint64_t)(t11 & t15);
        uint8_t t17 = ((t16 == ((uint64_t)0ULL)));
        F.ret = t17;
        goto gm_fret;
    }
    uint64_t t18 = gm_l0;
    uint64_t t19 = gm_l1;
    if (t19 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 198, 12);
    uint64_t t20 = (uint64_t)(t18 % t19);
    uint8_t t21 = ((t20 == ((uint64_t)0ULL)));
    F.ret = t21;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_copy_from_f7j6p05 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* src */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t4 = ((t2 == t3));
        if (t4) {
        } else {
            gm_b16_8 t5;
            GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sqofr58g480ri));
            GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(38));
            { gm_buf mb = {0}; size_t ml = GM_LD(uintptr_t, ((uint8_t *)&t5) + 8); const uint8_t *mp = GM_LD(void *, ((uint8_t *)&t5)); if (ml == 16 && memcmp(mp, "assertion failed", 16) == 0) gm_buf_push(&mb, mp, ml); else { gm_buf_push(&mb, "assertion failed: ", 18); gm_buf_push(&mb, mp, ml); } gm_trap_bytes(mb.data, mb.len, gm_srcc24pnihhgkug, 24, 5); }
        }
    }
    gm_b16_8 t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    if (GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t6)), GM_LD(void *, ((uint8_t *)&t7)), GM_LD(uintptr_t, ((uint8_t *)&t6) + 8) * 1);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) uint8_t gm_f_fast_23em7sv(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fast_23em7sv F;
    void * gm_l0; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 22u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t2) + 88));
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&t3) + 0));
    uint8_t t5 = (uint8_t)((t4 == ((uint8_t)3ULL)));
    gm_t9qkh0rsooc8d t6;
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)3ULL));
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_fill_abmf5g5(gm_b16_8 a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fill_abmf5g5 F;
    gm_b16_8 gm_l0; /* s */
    uint8_t gm_l1; /* x */
    void * gm_l2; /* p */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t7 = GM_LD(void *, ((uint8_t *)&gm_l0));
    gm_l2 = t7;
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l3 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l4;
            uint64_t t10 = gm_l3;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t12 = gm_l4;
                        gm_l5 = t12;
                        void * t13 = gm_l2;
                        uint64_t t14 = gm_l5;
                        uint8_t t15 = gm_l1;
                        GM_ST(uint8_t, (((uint8_t *)t13) + (uintptr_t)t14 * 1), t15);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcc24pnihhgkug, 18, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t16 = gm_l4;
            uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
            uint64_t t19 = r17;
            gm_l4 = t19;
        }
    }
gm_lb1:;
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_format_37g0gks(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_37g0gks F;
    gm_tgbgti82mf879 gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_so94vinsu4uib));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(49));
    gm_tgbgti82mf879 t3 = GM_LD(gm_tgbgti82mf879, ((uint8_t *)a0));
    gm_f_format_fja4j2u((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpllhk8pmjfso1j), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_bv13gdl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_bv13gdl F;
    gm_tgbgti82mf879 gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sukbpodfceoe1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
    gm_tgbgti82mf879 t3 = GM_LD(gm_tgbgti82mf879, ((uint8_t *)a0));
    gm_f_format_fja4j2u((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl4bpbmadpmu1i), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_e7344u2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_e7344u2 F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s76i8mdsri6em));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(69));
    gm_f_format_ig8f8pl((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl5prdhjdrg194), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_jc3ftg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_jc3ftg7 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdv3b0kjlrdl2));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(45));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplrgo7romk0g20), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_p0ej7hu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_p0ej7hu F;
    gm_tkpb4kplftgaa gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sgooul7ab8sa1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(64));
    gm_tkpb4kplftgaa t3 = GM_LD(gm_tkpb4kplftgaa, ((uint8_t *)a0));
    gm_f_format_q33vns6((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplf3e41f3siie3), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_u3ioj90(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_u3ioj90 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s7j165k7b6shl));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tple7ukme0qchh6), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_unbcnc5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_unbcnc5 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sm884rqp4fp7u));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(67));
    gm_t6j6smj7qoctu t3 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
    gm_f_format_fdoda7b((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplcein5d5pq2cs), t2);
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
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_get_u64_tgpb2vh F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK_SHALLOW(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t gm_l2;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t3 = (t2 <= ((uint64_t)8ULL));
    if (t3) {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        uint64_t t6 = gm_f_rd_word_8flcq6o(t4, t5);
        gm_l2 = t6;
    } else {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_l1;
        unsigned __int128 t9 = gm_f_get_uint_bq8c4b7(t7, t8);
        uint64_t t10 = (uint64_t)t9;
        gm_l2 = t10;
    }
    uint64_t t11 = gm_l2;
    F.ret = t11;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_htv6093 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&(*(gm_t53at80pfdus3 *)gm_out), t4, GM_CONV(gm_csguankko4lmhd, t5));
    goto gm_fdone;
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
inline __attribute__((aligned(4))) void gm_f_mem_trap_ecogko0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2) {
    struct gm_fr_mem_trap_ecogko0 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_b16_8 t4 = gm_f_mem_message_e0go901((void *)NULL, cx, NULL, &(*(gm_tpekhfs32op0f *)((uint8_t *)a2)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_trap_qh84l9d((void *)&(*(gm_t76123gfet73g *)gm_out), (void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_of_b77b4c9(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_of_b77b4c9 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 176));
    uint64_t t7 = gm_f_us_cuv01fp(t6);
    uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t9 = (uintptr_t)t7;
    if (t8 > t9 || t9 > (uintptr_t)t4) gm_trap_slice(t8, t9, t4, gm_srcnqtpfma6qe4a, 104, 68);
    uint64_t t10 = ((uintptr_t)t7) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t3 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
    uint64_t t12 = gm_f_get_u64_tgpb2vh(t2, t11);
    F.ret = t12;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_put_ptr_188p3mm(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_ptr_188p3mm F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* addr */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, ((uint8_t *)&gm_l1));
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 176));
    uint64_t t7 = gm_f_us_cuv01fp(t6);
    uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t9 = (uintptr_t)t7;
    if (t8 > t9 || t9 > (uintptr_t)t4) gm_trap_slice(t8, t9, t4, gm_srcnqtpfma6qe4a, 106, 67);
    uint64_t t10 = ((uintptr_t)t7) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t11;
    GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t3 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(t10));
    uint64_t t12 = gm_l2;
    gm_f_put_u64_irp77r2(t2, t11, t12);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_put_u64_irp77r2(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_put_u64_irp77r2 F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 121u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t3 = (t2 <= ((uint64_t)8ULL));
    if (t3) {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l1;
        uint64_t t6 = gm_l2;
        gm_f_wr_word_npte9fs(t4, t5, t6);
    } else {
        void * t7 = gm_l0;
        gm_b16_8 t8 = gm_l1;
        uint64_t t9 = gm_l2;
        unsigned __int128 t10 = (unsigned __int128)t9;
        gm_f_put_uint_cddin99(t7, t8, t10);
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_rd_word_8flcq6o F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 89));
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t5 = ((t4 == ((uint64_t)8ULL)));
        if (t5) {
            void * t6 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t7, gm_srcnqtpfma6qe4a, 316, 36);
            uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t9 = (uint64_t)t8;
            void * t10 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t11, gm_srcnqtpfma6qe4a, 316, 49);
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t10) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t13 = (uint64_t)t12;
            unsigned __int128 n14 = (unsigned __int128)((uint64_t)8ULL);
            if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t r15 = (uint64_t)((uint64_t)t13 << (int)n14);
            bool o16 = (uint64_t)(r15 >> (int)n14) != t13;
            if (o16) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
            uint64_t t17 = r15;
            uint64_t t18 = (uint64_t)(t9 | t17);
            void * t19 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t20, gm_srcnqtpfma6qe4a, 316, 68);
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t19) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t22 = (uint64_t)t21;
            unsigned __int128 n23 = (unsigned __int128)((uint64_t)16ULL);
            if (n23 >= 64) gm_trap_shift((uint64_t)n23, "u64", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t r24 = (uint64_t)((uint64_t)t22 << (int)n23);
            bool o25 = (uint64_t)(r24 >> (int)n23) != t22;
            if (o25) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
            uint64_t t26 = r24;
            uint64_t t27 = (uint64_t)(t18 | t26);
            void * t28 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t29, gm_srcnqtpfma6qe4a, 316, 88);
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)t28) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t31 = (uint64_t)t30;
            unsigned __int128 n32 = (unsigned __int128)((uint64_t)24ULL);
            if (n32 >= 64) gm_trap_shift((uint64_t)n32, "u64", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t r33 = (uint64_t)((uint64_t)t31 << (int)n32);
            bool o34 = (uint64_t)(r33 >> (int)n32) != t31;
            if (o34) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
            uint64_t t35 = r33;
            uint64_t t36 = (uint64_t)(t27 | t35);
            void * t37 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t38, gm_srcnqtpfma6qe4a, 316, 108);
            uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)t37) + (uintptr_t)((uint64_t)4ULL) * 1));
            uint64_t t40 = (uint64_t)t39;
            unsigned __int128 n41 = (unsigned __int128)((uint64_t)32ULL);
            if (n41 >= 64) gm_trap_shift((uint64_t)n41, "u64", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t r42 = (uint64_t)((uint64_t)t40 << (int)n41);
            bool o43 = (uint64_t)(r42 >> (int)n41) != t40;
            if (o43) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
            uint64_t t44 = r42;
            uint64_t t45 = (uint64_t)(t36 | t44);
            void * t46 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)5ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)5ULL), t47, gm_srcnqtpfma6qe4a, 316, 128);
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)t46) + (uintptr_t)((uint64_t)5ULL) * 1));
            uint64_t t49 = (uint64_t)t48;
            unsigned __int128 n50 = (unsigned __int128)((uint64_t)40ULL);
            if (n50 >= 64) gm_trap_shift((uint64_t)n50, "u64", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t r51 = (uint64_t)((uint64_t)t49 << (int)n50);
            bool o52 = (uint64_t)(r51 >> (int)n50) != t49;
            if (o52) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
            uint64_t t53 = r51;
            uint64_t t54 = (uint64_t)(t45 | t53);
            void * t55 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)6ULL) >= (uintptr_t)(t56)) gm_trap_index((uintptr_t)((uint64_t)6ULL), t56, gm_srcnqtpfma6qe4a, 316, 148);
            uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)t55) + (uintptr_t)((uint64_t)6ULL) * 1));
            uint64_t t58 = (uint64_t)t57;
            unsigned __int128 n59 = (unsigned __int128)((uint64_t)48ULL);
            if (n59 >= 64) gm_trap_shift((uint64_t)n59, "u64", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t r60 = (uint64_t)((uint64_t)t58 << (int)n59);
            bool o61 = (uint64_t)(r60 >> (int)n59) != t58;
            if (o61) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
            uint64_t t62 = r60;
            uint64_t t63 = (uint64_t)(t54 | t62);
            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)7ULL) >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)((uint64_t)7ULL), t65, gm_srcnqtpfma6qe4a, 316, 168);
            uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)t64) + (uintptr_t)((uint64_t)7ULL) * 1));
            uint64_t t67 = (uint64_t)t66;
            unsigned __int128 n68 = (unsigned __int128)((uint64_t)56ULL);
            if (n68 >= 64) gm_trap_shift((uint64_t)n68, "u64", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t r69 = (uint64_t)((uint64_t)t67 << (int)n68);
            bool o70 = (uint64_t)(r69 >> (int)n68) != t67;
            if (o70) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
            uint64_t t71 = r69;
            uint64_t t72 = (uint64_t)(t63 | t71);
            F.ret = t72;
            goto gm_fret;
        }
        uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t74 = ((t73 == ((uint64_t)4ULL)));
        if (t74) {
            void * t75 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t76, gm_srcnqtpfma6qe4a, 317, 36);
            uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)t75) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t78 = (uint64_t)t77;
            void * t79 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t80)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t80, gm_srcnqtpfma6qe4a, 317, 49);
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)t79) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t82 = (uint64_t)t81;
            unsigned __int128 n83 = (unsigned __int128)((uint64_t)8ULL);
            if (n83 >= 64) gm_trap_shift((uint64_t)n83, "u64", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t r84 = (uint64_t)((uint64_t)t82 << (int)n83);
            bool o85 = (uint64_t)(r84 >> (int)n83) != t82;
            if (o85) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
            uint64_t t86 = r84;
            uint64_t t87 = (uint64_t)(t78 | t86);
            void * t88 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t89 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t89)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t89, gm_srcnqtpfma6qe4a, 317, 68);
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)t88) + (uintptr_t)((uint64_t)2ULL) * 1));
            uint64_t t91 = (uint64_t)t90;
            unsigned __int128 n92 = (unsigned __int128)((uint64_t)16ULL);
            if (n92 >= 64) gm_trap_shift((uint64_t)n92, "u64", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t r93 = (uint64_t)((uint64_t)t91 << (int)n92);
            bool o94 = (uint64_t)(r93 >> (int)n92) != t91;
            if (o94) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
            uint64_t t95 = r93;
            uint64_t t96 = (uint64_t)(t87 | t95);
            void * t97 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t98)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t98, gm_srcnqtpfma6qe4a, 317, 88);
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)t97) + (uintptr_t)((uint64_t)3ULL) * 1));
            uint64_t t100 = (uint64_t)t99;
            unsigned __int128 n101 = (unsigned __int128)((uint64_t)24ULL);
            if (n101 >= 64) gm_trap_shift((uint64_t)n101, "u64", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t r102 = (uint64_t)((uint64_t)t100 << (int)n101);
            bool o103 = (uint64_t)(r102 >> (int)n101) != t100;
            if (o103) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
            uint64_t t104 = r102;
            uint64_t t105 = (uint64_t)(t96 | t104);
            F.ret = t105;
            goto gm_fret;
        }
        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t107 = ((t106 == ((uint64_t)1ULL)));
        if (t107) {
            void * t108 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t109)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t109, gm_srcnqtpfma6qe4a, 318, 36);
            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)t108) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t111 = (uint64_t)t110;
            F.ret = t111;
            goto gm_fret;
        }
        uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t113 = ((t112 == ((uint64_t)2ULL)));
        if (t113) {
            void * t114 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t115, gm_srcnqtpfma6qe4a, 319, 36);
            uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)t114) + (uintptr_t)((uint64_t)0ULL) * 1));
            uint64_t t117 = (uint64_t)t116;
            void * t118 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t119 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t119)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t119, gm_srcnqtpfma6qe4a, 319, 49);
            uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)t118) + (uintptr_t)((uint64_t)1ULL) * 1));
            uint64_t t121 = (uint64_t)t120;
            unsigned __int128 n122 = (unsigned __int128)((uint64_t)8ULL);
            if (n122 >= 64) gm_trap_shift((uint64_t)n122, "u64", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t r123 = (uint64_t)((uint64_t)t121 << (int)n122);
            bool o124 = (uint64_t)(r123 >> (int)n122) != t121;
            if (o124) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
            uint64_t t125 = r123;
            uint64_t t126 = (uint64_t)(t117 | t125);
            F.ret = t126;
            goto gm_fret;
        }
    }
    void * t127 = gm_l0;
    gm_b16_8 t128 = gm_l1;
    unsigned __int128 t129 = gm_f_get_uint_bq8c4b7(t127, t128);
    uint64_t t130 = (uint64_t)t129;
    F.ret = t130;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_read_u6r4vns F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l5;
    {
        {
            uint64_t t6 = gm_l2;
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            uint64_t t8 = gm_l3;
            uint8_t t9 = (t8 <= ((uint64_t)1ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = gm_f_empty_slice_ijtp9lo();
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t10);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    uint64_t t11 = gm_l1;
    uint64_t t12 = gm_l3;
    uint8_t t13 = gm_f_aligned_q6sor4h(t11, t12);
    if (t13) {
        {
            {
                void * t14 = gm_l0;
                uint64_t t15 = gm_l1;
                uint64_t t16 = gm_l2;
                void * t17 = gm_f_view_qalkbv1(t14, t15, t16, ((uint8_t)0));
                gm_l5 = t17;
                uint8_t t18 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
                if (!t18) {
                    goto gm_lb4;
                }
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l5));
                gm_l4 = t19;
                void * t20 = gm_l4;
                uint64_t t21 = gm_l2;
                uint64_t t22 = gm_f_us_cuv01fp(t21);
                uint64_t t23 = ((uintptr_t)t22) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t24;
                GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t24);
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
    }
    {
        void * t25 = gm_l0;
        uint64_t t26 = gm_l1;
        uint64_t t27 = gm_l2;
        uint64_t t28 = gm_l3;
        gm_tpekhfs32op0f t29;
        gm_f_fault_u31b92l((void *)&t29, t25, t26, t27, t28, ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 1;
        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t29);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_b16_8 gm_f_tmp_8k6u9c9(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_tmp_8k6u9c9 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    void * gm_l2; /* sc */
    gm_b16_8 gm_l3; /* chunk */
    uint64_t gm_l4; /* base */
    uint64_t gm_l5; /* top */
    uint64_t gm_l6; /* size */
    gm_b16_8 gm_l7; /* b */
    GM_STACK_CHECK(&F, 524u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l8;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 360);
    gm_l2 = t3;
    void * t4 = gm_l2;
    void * t5 = GM_LD(void *, ((uint8_t *)t4));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t4) + 8));
    void * t7 = gm_l2;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 40));
    if ((uintptr_t)t8 >= (uintptr_t)(t6)) gm_trap_index((uintptr_t)t8, t6, gm_srcpit8mdn0781q, 387, 14);
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t5) + (uintptr_t)t8 * 16));
    gm_l3 = t9;
    void * t10 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t11 = (uintptr_t)t10;
    gm_l4 = t11;
    uint64_t t12 = gm_l4;
    void * t13 = gm_l2;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 48));
    uint64_t r15; bool o16 = __builtin_add_overflow(t12, t14, &r15);
    if (o16) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 391, 14);
    uint64_t t17 = r15;
    uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)7ULL), &r18);
    if (o19) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 391, 14);
    uint64_t t20 = r18;
    uint64_t t21 = gm_f_us_cuv01fp(((uint64_t)7ULL));
    uint64_t t22 = (uint64_t)~t21;
    uint64_t t23 = (uint64_t)(t20 & t22);
    uint64_t t24 = gm_l4;
    uint64_t r25; bool o26 = __builtin_sub_overflow(t23, t24, &r25);
    if (o26) gm_trap("integer overflow in `-`", gm_srcpit8mdn0781q, 391, 12);
    uint64_t t27 = r25;
    gm_l5 = t27;
    uint64_t t28 = gm_l1;
    uint64_t t29 = gm_f_us_cuv01fp(t28);
    gm_l6 = t29;
    uint64_t t30 = gm_l1;
    uint8_t t31 = ((t30 == ((uint64_t)0ULL)));
    if (t31) {
        gm_l8 = ((uint8_t)1);
    } else {
        uint64_t t32 = gm_l5;
        uint64_t t33 = gm_l6;
        uint64_t r34; bool o35 = __builtin_add_overflow(t32, t33, &r34);
        if (o35) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 393, 18);
        uint64_t t36 = r34;
        uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t38 = (t36 > t37);
        gm_l8 = t38;
    }
    uint8_t t39 = gm_l8;
    if (t39) {
        void * t40 = gm_l0;
        uint64_t t41 = gm_l1;
        gm_b16_8 t42 = gm_f_tmp_next_dalgo1f((void *)NULL, cx, NULL, t40, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t42;
        goto gm_fret;
    }
    void * t43 = GM_LD(void *, ((uint8_t *)&gm_l3));
    uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint64_t t45 = gm_l5;
    uint64_t t46 = gm_l5;
    uint64_t t47 = gm_l6;
    uint64_t r48; bool o49 = __builtin_add_overflow(t46, t47, &r48);
    if (o49) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 394, 21);
    uint64_t t50 = r48;
    uint64_t t51 = (uintptr_t)t45;
    uint64_t t52 = (uintptr_t)t50;
    if (t51 > t52 || t52 > (uintptr_t)t44) gm_trap_slice(t51, t52, t44, gm_srcpit8mdn0781q, 394, 10);
    uint64_t t53 = ((uintptr_t)t50) - (uintptr_t)t45;
    gm_b16_8 t54;
    GM_ST(void *, (uint8_t *)&t54, (void *)(((uint8_t *)t43 + (uintptr_t)t45 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(t53));
    gm_l7 = t54;
    void * t55 = gm_l2;
    uint64_t t56 = gm_l5;
    uint64_t t57 = gm_l6;
    uint64_t r58; bool o59 = __builtin_add_overflow(t56, t57, &r58);
    if (o59) gm_trap("integer overflow in `+`", gm_srcpit8mdn0781q, 395, 14);
    uint64_t t60 = r58;
    GM_ST(uint64_t, (((uint8_t *)t55) + 48), t60);
    gm_b16_8 t61 = gm_l7;
    gm_f_fill_abmf5g5(t61, ((uint8_t)0ULL));
    gm_b16_8 t62 = gm_l7;
    F.ret = t62;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_us_cuv01fp F;
    uint64_t gm_l0; /* x */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t2 = gm_l0;
    uint64_t t3 = (uint64_t)t2;
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_view_qalkbv1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint8_t gm_l3; /* write */
    uint64_t gm_l4; /* p */
    void * gm_l5; /* pg */
    void * gm_l6; /* ss */
    uint64_t gm_l7; /* rel */
    uint64_t gm_l8; /* i */
    void * gm_l9; /* s */
    uint64_t gm_l10; /* off */
    GM_STACK_CHECK_SHALLOW(&F, 522u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l11;
    uint8_t gm_l12;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
    uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
    uint64_t t7 = r5;
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)gm_c6p4chcutca));
    unsigned __int128 n9 = (unsigned __int128)t8;
    if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 206, 10);
    uint64_t t10 = (uint64_t)(t7 >> (int)n9);
    gm_l4 = t10;
    uint64_t t11 = gm_l4;
    void * t12 = gm_l0;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 8));
    uint64_t t14 = (uint64_t)t13;
    uint8_t t15 = (t11 >= t14);
    if (t15) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, ((uint8_t *)t16));
    uint64_t t18 = gm_l4;
    uint64_t t19 = gm_f_us_cuv01fp(t18);
    void * t20 = (void *)(((uint8_t *)t17) + (uintptr_t)t19 * 32);
    gm_l5 = t20;
    void * t21 = gm_l5;
    uint8_t t22 = (uint8_t)(GM_LD(void *, ((uint8_t *)t21)) != 0);
    if (t22) {
        void * t23 = GM_LD(void *, ((uint8_t *)t21));
        gm_l6 = t23;
    } else {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    uint64_t t24 = gm_l1;
    void * t25 = gm_l5;
    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 8));
    uint64_t r27; bool o28 = __builtin_sub_overflow(t24, t26, &r27);
    if (o28) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 210, 12);
    uint64_t t29 = r27;
    gm_l7 = t29;
    uint64_t t30 = gm_l7;
    void * t31 = gm_l5;
    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 16));
    unsigned __int128 n33 = (unsigned __int128)t32;
    if (n33 >= 64) gm_trap_shift((uint64_t)n33, "u64", gm_srcq7ohh8h77bn7, 211, 10);
    uint64_t t34 = (uint64_t)(t30 >> (int)n33);
    gm_l8 = t34;
    void * t35 = gm_l6;
    uint64_t t36 = gm_l8;
    uint64_t t37 = gm_f_us_cuv01fp(t36);
    void * t38 = (void *)(((uint8_t *)t35) + (uintptr_t)t37 * 24);
    gm_l9 = t38;
    uint64_t t39 = gm_l7;
    uint64_t t40 = gm_l8;
    void * t41 = gm_l5;
    uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t41) + 16));
    unsigned __int128 n43 = (unsigned __int128)t42;
    if (n43 >= 64) gm_trap_shift((uint64_t)n43, "u64", gm_srcq7ohh8h77bn7, 213, 19);
    uint64_t r44 = (uint64_t)((uint64_t)t40 << (int)n43);
    bool o45 = (uint64_t)(r44 >> (int)n43) != t40;
    if (o45) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 213, 19);
    uint64_t t46 = r44;
    uint64_t r47; bool o48 = __builtin_sub_overflow(t39, t46, &r47);
    if (o48) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 213, 12);
    uint64_t t49 = r47;
    gm_l10 = t49;
    void * t50 = gm_l9;
    uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)t50) + 17));
    uint8_t t52 = (uint8_t)!t51;
    if (t52) {
        gm_l13 = ((uint8_t)1);
    } else {
        uint64_t t53 = gm_l2;
        void * t54 = gm_l9;
        uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t54) + 8));
        uint8_t t56 = (t53 > t55);
        gm_l13 = t56;
    }
    uint8_t t57 = gm_l13;
    if (t57) {
        gm_l12 = ((uint8_t)1);
    } else {
        uint64_t t58 = gm_l10;
        void * t59 = gm_l9;
        uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)t59) + 8));
        uint64_t t61 = gm_l2;
        uint64_t r62; bool o63 = __builtin_sub_overflow(t60, t61, &r62);
        if (o63) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 214, 44);
        uint64_t t64 = r62;
        uint8_t t65 = (t58 > t64);
        gm_l12 = t65;
    }
    uint8_t t66 = gm_l12;
    if (t66) {
        gm_l11 = ((uint8_t)1);
    } else {
        uint8_t t67 = gm_l3;
        if (t67) {
            void * t68 = gm_l9;
            uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)t68) + 18));
            uint8_t t70 = (uint8_t)!t69;
            gm_l14 = t70;
        } else {
            gm_l14 = ((uint8_t)0);
        }
        uint8_t t71 = gm_l14;
        gm_l11 = t71;
    }
    uint8_t t72 = gm_l11;
    if (t72) {
        F.ret = ((void *)0);
        goto gm_fret;
    }
    void * t73 = gm_l9;
    void * t74 = GM_LD(void *, ((uint8_t *)t73));
    uint64_t t75 = gm_l10;
    uint64_t t76 = gm_f_us_cuv01fp(t75);
    void * t77 = (void *)(((uint8_t *)t74) + (uintptr_t)t76 * 1);
    void * t78 = t77;
    F.ret = t78;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_wr_word_npte9fs(void * a0, gm_b16_8 a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wr_word_npte9fs F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* v */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)t2) + 89));
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t5 = ((t4 == ((uint64_t)8ULL)));
        if (t5) {
            void * t6 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t7)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t7, gm_srcnqtpfma6qe4a, 329, 13);
            uint64_t t8 = gm_l2;
            uint8_t t9 = (uint8_t)t8;
            GM_ST(uint8_t, (((uint8_t *)t6) + (uintptr_t)((uint64_t)0ULL) * 1), t9);
            void * t10 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t11, gm_srcnqtpfma6qe4a, 330, 13);
            uint64_t t12 = gm_l2;
            unsigned __int128 n13 = (unsigned __int128)((uint64_t)8ULL);
            if (n13 >= 64) gm_trap_shift((uint64_t)n13, "u64", gm_srcnqtpfma6qe4a, 330, 21);
            uint64_t t14 = (uint64_t)(t12 >> (int)n13);
            uint8_t t15 = (uint8_t)t14;
            GM_ST(uint8_t, (((uint8_t *)t10) + (uintptr_t)((uint64_t)1ULL) * 1), t15);
            void * t16 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t17)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t17, gm_srcnqtpfma6qe4a, 331, 13);
            uint64_t t18 = gm_l2;
            unsigned __int128 n19 = (unsigned __int128)((uint64_t)16ULL);
            if (n19 >= 64) gm_trap_shift((uint64_t)n19, "u64", gm_srcnqtpfma6qe4a, 331, 21);
            uint64_t t20 = (uint64_t)(t18 >> (int)n19);
            uint8_t t21 = (uint8_t)t20;
            GM_ST(uint8_t, (((uint8_t *)t16) + (uintptr_t)((uint64_t)2ULL) * 1), t21);
            void * t22 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t23, gm_srcnqtpfma6qe4a, 332, 13);
            uint64_t t24 = gm_l2;
            unsigned __int128 n25 = (unsigned __int128)((uint64_t)24ULL);
            if (n25 >= 64) gm_trap_shift((uint64_t)n25, "u64", gm_srcnqtpfma6qe4a, 332, 21);
            uint64_t t26 = (uint64_t)(t24 >> (int)n25);
            uint8_t t27 = (uint8_t)t26;
            GM_ST(uint8_t, (((uint8_t *)t22) + (uintptr_t)((uint64_t)3ULL) * 1), t27);
            void * t28 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t29, gm_srcnqtpfma6qe4a, 333, 13);
            uint64_t t30 = gm_l2;
            unsigned __int128 n31 = (unsigned __int128)((uint64_t)32ULL);
            if (n31 >= 64) gm_trap_shift((uint64_t)n31, "u64", gm_srcnqtpfma6qe4a, 333, 21);
            uint64_t t32 = (uint64_t)(t30 >> (int)n31);
            uint8_t t33 = (uint8_t)t32;
            GM_ST(uint8_t, (((uint8_t *)t28) + (uintptr_t)((uint64_t)4ULL) * 1), t33);
            void * t34 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)5ULL) >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)((uint64_t)5ULL), t35, gm_srcnqtpfma6qe4a, 334, 13);
            uint64_t t36 = gm_l2;
            unsigned __int128 n37 = (unsigned __int128)((uint64_t)40ULL);
            if (n37 >= 64) gm_trap_shift((uint64_t)n37, "u64", gm_srcnqtpfma6qe4a, 334, 21);
            uint64_t t38 = (uint64_t)(t36 >> (int)n37);
            uint8_t t39 = (uint8_t)t38;
            GM_ST(uint8_t, (((uint8_t *)t34) + (uintptr_t)((uint64_t)5ULL) * 1), t39);
            void * t40 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)6ULL) >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)((uint64_t)6ULL), t41, gm_srcnqtpfma6qe4a, 335, 13);
            uint64_t t42 = gm_l2;
            unsigned __int128 n43 = (unsigned __int128)((uint64_t)48ULL);
            if (n43 >= 64) gm_trap_shift((uint64_t)n43, "u64", gm_srcnqtpfma6qe4a, 335, 21);
            uint64_t t44 = (uint64_t)(t42 >> (int)n43);
            uint8_t t45 = (uint8_t)t44;
            GM_ST(uint8_t, (((uint8_t *)t40) + (uintptr_t)((uint64_t)6ULL) * 1), t45);
            void * t46 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)7ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)7ULL), t47, gm_srcnqtpfma6qe4a, 336, 13);
            uint64_t t48 = gm_l2;
            unsigned __int128 n49 = (unsigned __int128)((uint64_t)56ULL);
            if (n49 >= 64) gm_trap_shift((uint64_t)n49, "u64", gm_srcnqtpfma6qe4a, 336, 21);
            uint64_t t50 = (uint64_t)(t48 >> (int)n49);
            uint8_t t51 = (uint8_t)t50;
            GM_ST(uint8_t, (((uint8_t *)t46) + (uintptr_t)((uint64_t)7ULL) * 1), t51);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t53 = ((t52 == ((uint64_t)1ULL)));
        if (t53) {
            void * t54 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t55)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t55, gm_srcnqtpfma6qe4a, 340, 13);
            uint64_t t56 = gm_l2;
            uint8_t t57 = (uint8_t)t56;
            GM_ST(uint8_t, (((uint8_t *)t54) + (uintptr_t)((uint64_t)0ULL) * 1), t57);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        uint64_t t58 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t59 = ((t58 == ((uint64_t)4ULL)));
        if (t59) {
            void * t60 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t61)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t61, gm_srcnqtpfma6qe4a, 344, 13);
            uint64_t t62 = gm_l2;
            uint8_t t63 = (uint8_t)t62;
            GM_ST(uint8_t, (((uint8_t *)t60) + (uintptr_t)((uint64_t)0ULL) * 1), t63);
            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t65, gm_srcnqtpfma6qe4a, 345, 13);
            uint64_t t66 = gm_l2;
            unsigned __int128 n67 = (unsigned __int128)((uint64_t)8ULL);
            if (n67 >= 64) gm_trap_shift((uint64_t)n67, "u64", gm_srcnqtpfma6qe4a, 345, 21);
            uint64_t t68 = (uint64_t)(t66 >> (int)n67);
            uint8_t t69 = (uint8_t)t68;
            GM_ST(uint8_t, (((uint8_t *)t64) + (uintptr_t)((uint64_t)1ULL) * 1), t69);
            void * t70 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t71 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t71)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t71, gm_srcnqtpfma6qe4a, 346, 13);
            uint64_t t72 = gm_l2;
            unsigned __int128 n73 = (unsigned __int128)((uint64_t)16ULL);
            if (n73 >= 64) gm_trap_shift((uint64_t)n73, "u64", gm_srcnqtpfma6qe4a, 346, 21);
            uint64_t t74 = (uint64_t)(t72 >> (int)n73);
            uint8_t t75 = (uint8_t)t74;
            GM_ST(uint8_t, (((uint8_t *)t70) + (uintptr_t)((uint64_t)2ULL) * 1), t75);
            void * t76 = GM_LD(void *, ((uint8_t *)&gm_l1));
            uint64_t t77 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
            if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t77)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t77, gm_srcnqtpfma6qe4a, 347, 13);
            uint64_t t78 = gm_l2;
            unsigned __int128 n79 = (unsigned __int128)((uint64_t)24ULL);
            if (n79 >= 64) gm_trap_shift((uint64_t)n79, "u64", gm_srcnqtpfma6qe4a, 347, 21);
            uint64_t t80 = (uint64_t)(t78 >> (int)n79);
            uint8_t t81 = (uint8_t)t80;
            GM_ST(uint8_t, (((uint8_t *)t76) + (uintptr_t)((uint64_t)3ULL) * 1), t81);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
    }
    void * t82 = gm_l0;
    gm_b16_8 t83 = gm_l1;
    uint64_t t84 = gm_l2;
    unsigned __int128 t85 = (unsigned __int128)t84;
    gm_f_put_uint_cddin99(t82, t83, t85);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_view_pebit6h F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 260u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * gm_l5;
    {
        {
            uint64_t t6 = gm_l2;
            uint8_t t7 = ((t6 == ((uint64_t)0ULL)));
            if (!t7) {
                goto gm_lb2;
            }
            uint64_t t8 = gm_l3;
            uint8_t t9 = (t8 <= ((uint64_t)1ULL));
            if (!t9) {
                goto gm_lb2;
            }
            gm_b16_8 t10 = gm_f_empty_slice_ijtp9lo();
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
            ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t10);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    uint64_t t11 = gm_l1;
    uint64_t t12 = gm_l3;
    uint8_t t13 = gm_f_aligned_q6sor4h(t11, t12);
    if (t13) {
        {
            {
                void * t14 = gm_l0;
                uint64_t t15 = gm_l1;
                uint64_t t16 = gm_l2;
                void * t17 = gm_f_view_qalkbv1(t14, t15, t16, ((uint8_t)1));
                gm_l5 = t17;
                uint8_t t18 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l5)) != 0);
                if (!t18) {
                    goto gm_lb4;
                }
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l5));
                gm_l4 = t19;
                void * t20 = gm_l4;
                uint64_t t21 = gm_l2;
                uint64_t t22 = gm_f_us_cuv01fp(t21);
                uint64_t t23 = ((uintptr_t)t22) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t24;
                GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 40, 0);
                ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t24);
                goto gm_fdone;
            }
gm_lb4:;
        }
gm_lb3:;
    }
    {
        void * t25 = gm_l0;
        uint64_t t26 = gm_l1;
        uint64_t t27 = gm_l2;
        uint64_t t28 = gm_l3;
        gm_tpekhfs32op0f t29;
        gm_f_fault_u31b92l((void *)&t29, t25, t26, t27, t28, ((uint8_t)1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out))[0] = 1;
        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&(*(gm_t934k1j7u70ct *)gm_out) + 8), t29);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) void gm_f_memory_builtin_9s47dk8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t8588ma2cr9d7 a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5, gm_b16_8 a6) {
    struct gm_fr_memory_builtin_9s47dk8 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t8588ma2cr9d7 gm_l2; /* op */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* v */
    gm_t88rcucg1b7uk gm_l5; /* res_ty */
    gm_b16_8 gm_l6; /* dst */
    uint64_t gm_l7; /* size */
    uint64_t gm_l8; /* al */
    uint64_t gm_l9; /* addr */
    gm_b16_8 gm_l10; /* d */
    gm_t2doi72kmmlil gm_l11; /* pb */
    gm_b16_8 gm_l12; /* p */
    uint64_t gm_l13; /* p */
    uint64_t gm_l14; /* size */
    gm_tilak255t5m34 gm_l15; /* d */
    uint64_t gm_l16; /* p */
    uint64_t gm_l17; /* owner */
    uint64_t gm_l18; /* vt */
    gm_t2lqu8jsj511d gm_l19; /* file */
    uint64_t gm_l20; /* line */
    uint64_t gm_l21; /* column */
    gm_b16_8 gm_l23; /* at */
    gm_b16_8 gm_l24; /* here */
    uint64_t gm_l25; /* addr */
    gm_t88rcucg1b7uk gm_l26; /* ty */
    uint64_t gm_l27; /* al */
    gm_b16_8 gm_l28; /* old */
    gm_t9qkh0rsooc8d gm_l29; /* rmw */
    unsigned __int128 gm_l30; /* x */
    unsigned __int128 gm_l31; /* y */
    uint8_t gm_l32; /* signed */
    unsigned __int128 gm_l33; /* r */
    gm_b16_8 gm_l34; /* nb */
    gm_t88rcucg1b7uk gm_l35; /* elem */
    gm_t2lqu8jsj511d gm_l36; /* d */
    gm_t2lqu8jsj511d gm_l37; /* s */
    uint64_t gm_l38; /* n */
    gm_b16_8 gm_l40; /* src */
    gm_b16_8 gm_l41; /* tmp */
    uint64_t gm_l43; /* p */
    uint64_t gm_l44; /* n */
    gm_b16_8 gm_l46; /* b */
    gm_t88rcucg1b7uk gm_l47; /* vt */
    gm_t2lqu8jsj511d gm_l48; /* sp */
    uint64_t gm_l49; /* n */
    gm_t88rcucg1b7uk gm_l50; /* lane */
    gm_b16_8 gm_l51; /* what */
    uint64_t gm_l52; /* al */
    GM_STACK_CHECK(&F, 7492u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    gm_tabb710d9mq2e gm_l53;
    gm_t3j6egd6cfnk5 gm_l54;
    gm_t934k1j7u70ct gm_l55;
    gm_tqvpud5umqqp1 gm_l56;
    gm_tabb710d9mq2e gm_l57;
    gm_tabb710d9mq2e gm_l58;
    gm_tabb710d9mq2e gm_l59;
    gm_tabb710d9mq2e gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_tm7c53s68971m gm_l62;
    uint8_t gm_l63;
    gm_td0ept12e7q8b gm_l64;
    gm_td0ept12e7q8b gm_l65;
    gm_tabb710d9mq2e gm_l66;
    gm_t53at80pfdus3 gm_l67;
    gm_tcfa6l7reoosn gm_l68;
    gm_tcfa6l7reoosn gm_l69;
    gm_tcfa6l7reoosn gm_l70;
    gm_t1ec8br9ddkav gm_l71;
    gm_tcfa6l7reoosn gm_l72;
    gm_ttovhfee8mf28 gm_l73;
    gm_tg5lql6itufk7 gm_l74;
    gm_ttovhfee8mf28 gm_l75;
    unsigned __int128 gm_l76;
    uint8_t gm_l77;
    uint8_t gm_l78;
    uint8_t gm_l79;
    unsigned __int128 gm_l80;
    uint8_t gm_l81;
    uint8_t gm_l82;
    uint8_t gm_l83;
    gm_tcfa6l7reoosn gm_l84;
    gm_t53at80pfdus3 gm_l85;
    gm_t934k1j7u70ct gm_l86;
    gm_tgfdnufetdnfc gm_l87;
    gm_t934k1j7u70ct gm_l88;
    gm_b16_8 gm_l89;
    gm_t53at80pfdus3 gm_l90;
    gm_tabb710d9mq2e gm_l91;
    gm_tcfa6l7reoosn gm_l92;
    gm_tcfa6l7reoosn gm_l93;
    {
        {
            {
                {
                    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t67 = (uint8_t)(t66 == ((uint8_t)75ULL));
                    if (!t67) {
                        goto gm_lb2;
                    }
                    void * t68 = gm_l0;
                    void * t69 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t70 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t70)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t70, gm_srccp2mbkjs1vn4, 12, 31);
                    gm_b16_8 t71 = GM_LD(gm_b16_8, (((uint8_t *)t69) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t72 = GM_LD(gm_b16_8, ((uint8_t *)&t71));
                    uint64_t t73 = gm_f_get_u64_tgpb2vh(t68, t72);
                    gm_l7 = t73;
                    void * t74 = gm_l0;
                    void * t75 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t76, gm_srccp2mbkjs1vn4, 13, 29);
                    gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)t75) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
                    uint64_t t79 = gm_f_get_u64_tgpb2vh(t74, t78);
                    uint64_t t80 = (t79 > ((uint64_t)1ULL) ? t79 : ((uint64_t)1ULL));
                    gm_l8 = t80;
                    uint64_t t81 = gm_l8;
                    uint64_t t82 = gm_l8;
                    uint64_t r83; bool o84 = __builtin_sub_overflow(t82, ((uint64_t)1ULL), &r83);
                    if (o84) gm_trap("integer overflow in `-`", gm_srccp2mbkjs1vn4, 14, 22);
                    uint64_t t85 = r83;
                    uint64_t t86 = (uint64_t)(t81 & t85);
                    uint8_t t87 = (!(t86 == ((uint64_t)0ULL)));
                    if (t87) {
                        void * t88 = gm_l0;
                        gm_tl874t2atnlh3 t89 = gm_l1;
                        uint64_t t90 = gm_l8;
                        gm_t4ce0jd3tdro4 t91;
                        GM_ST(uint64_t, ((uint8_t *)&t91 + 0), t90);
                        gm_f_format_jc3ftg7((void *)&gm_l53, (void *)NULL, cx, NULL, t91);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t92 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                        if (!t92) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 8, gm_srccp2mbkjs1vn4, 14, 62);
                        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l53) + 8));
                        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
                        gm_t76123gfet73g t95;
                        gm_f_trap_qh84l9d((void *)&t95, (void *)NULL, cx, NULL, t88, t89, t94);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t95);
                        goto gm_fdone;
                    }
                    uint64_t t96 = gm_l8;
                    uint64_t t97 = GM_LD(uint64_t, ((uint8_t *)gm_ckh5e429gka));
                    uint8_t t98 = (t96 > t97);
                    if (t98) {
                        gm_b16_8 t99 = gm_l6;
                        gm_f_fill_abmf5g5(t99, ((uint8_t)0ULL));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
                        goto gm_fdone;
                    }
                    void * t100 = gm_l0;
                    void * t101 = (void *)(((uint8_t *)t100) + 184);
                    uint64_t t102 = gm_l7;
                    uint64_t t103 = gm_l8;
                    gm_t9qkh0rsooc8d t104;
                    GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)2ULL));
                    gm_f_alloc_bfo8187((void *)&gm_l54, (void *)NULL, cx, NULL, t101, t102, t103, t104, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t105 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                    if (t105) {
                        uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l54) + 8));
                        gm_l9 = t106;
                    } else {
                        gm_b16_8 t107 = gm_l6;
                        gm_f_fill_abmf5g5(t107, ((uint8_t)0ULL));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
                        goto gm_fdone;
                    }
                    {
                        {
                            void * t108 = gm_l0;
                            gm_t9qkh0rsooc8d t109 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t108) + 88));
                            uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&t109) + 0));
                            uint8_t t111 = (uint8_t)((t110 == ((uint8_t)0ULL)));
                            if (!t111) {
                                goto gm_lb4;
                            }
                            uint64_t t112 = gm_l7;
                            uint8_t t113 = (t112 > ((uint64_t)0ULL));
                            if (!t113) {
                                goto gm_lb4;
                            }
                            void * t114 = gm_l0;
                            void * t115 = (void *)(((uint8_t *)t114) + 184);
                            uint64_t t116 = gm_l9;
                            uint64_t t117 = gm_l7;
                            gm_t9qkh0rsooc8d t118;
                            GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)0ULL));
                            gm_f_write_view_pebit6h((void *)&gm_l55, t115, t116, t117, ((uint64_t)1ULL));
                            uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                            if (t119) {
                                gm_b16_8 t120 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l55) + 8));
                                gm_l10 = t120;
                            } else {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                                GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
                                goto gm_fdone;
                            }
                            gm_b16_8 t121 = gm_l10;
                            gm_f_fill_abmf5g5(t121, ((uint8_t)170ULL));
                            goto gm_lb3;
                        }
gm_lb4:;
                    }
gm_lb3:;
                    gm_t2doi72kmmlil t122;
                    GM_ST(uint8_t, ((uint8_t *)&t122 + 0), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t122 + 1), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t122 + 2), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t122 + 3), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t122 + 4), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t122 + 5), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t122 + 6), ((uint8_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t122 + 7), ((uint8_t)0ULL));
                    gm_l11 = t122;
                    void * t123 = (void *)((uint8_t *)&gm_l11);
                    void * t124 = gm_l0;
                    uint64_t t125 = GM_LD(uint64_t, (((uint8_t *)t124) + 176));
                    uint64_t t126 = gm_f_us_cuv01fp(t125);
                    uint64_t t127 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t128 = (uintptr_t)t126;
                    if (t127 > t128 || t128 > (uintptr_t)((uint64_t)8ULL)) gm_trap_slice(t127, t128, ((uint64_t)8ULL), gm_srccp2mbkjs1vn4, 32, 18);
                    uint64_t t129 = ((uintptr_t)t126) - (uintptr_t)((uint64_t)0ULL);
                    gm_b16_8 t130;
                    GM_ST(void *, (uint8_t *)&t130, (void *)(((uint8_t *)t123 + (uintptr_t)((uint64_t)0ULL) * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(t129));
                    gm_l12 = t130;
                    void * t131 = gm_l0;
                    gm_b16_8 t132 = gm_l12;
                    uint64_t t133 = gm_l9;
                    gm_f_put_ptr_188p3mm(t131, t132, t133);
                    void * t134 = gm_l0;
                    gm_t88rcucg1b7uk t135 = gm_l5;
                    gm_b16_8 t136 = gm_l6;
                    gm_b16_8 t137 = gm_l12;
                    gm_b16_8 t138 = GM_LD(gm_b16_8, ((uint8_t *)&t137));
                    gm_b0_1 t139 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t134, t135, t136, t138);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_lb1;
                }
gm_lb2:;
            }
            {
                {
                    uint8_t t140 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t141 = (uint8_t)(t140 == ((uint8_t)76ULL));
                    if (!t141) {
                        goto gm_lb5;
                    }
                    void * t142 = gm_l0;
                    void * t143 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t144 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t144)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t144, gm_srccp2mbkjs1vn4, 37, 27);
                    gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)t143) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&t145));
                    uint64_t t147 = gm_f_ptr_of_b77b4c9(t142, t146);
                    gm_l13 = t147;
                    void * t148 = gm_l0;
                    void * t149 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t150)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t150, gm_srccp2mbkjs1vn4, 38, 31);
                    gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)t149) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t152 = GM_LD(gm_b16_8, ((uint8_t *)&t151));
                    uint64_t t153 = gm_f_get_u64_tgpb2vh(t148, t152);
                    gm_l14 = t153;
                    {
                        void * t154 = gm_l0;
                        void * t155 = (void *)(((uint8_t *)t154) + 184);
                        uint64_t t156 = gm_l13;
                        gm_f_describe_t62jhnt((void *)&gm_l56, t155, t156);
                    }
                    uint8_t t157 = (uint8_t)((((uint8_t *)&gm_l56))[24] != 0);
                    if (t157) {
                        memmove(((uint8_t *)&gm_l15), ((uint8_t *)&gm_l56), 24);
                    } else {
                        void * t158 = gm_l0;
                        gm_tl874t2atnlh3 t159 = gm_l1;
                        uint64_t t160 = gm_l13;
                        uint64_t t161 = gm_l14;
                        gm_t2lqu8jsj511d t162;
                        GM_ST(uint64_t, ((uint8_t *)&t162 + 0), t160);
                        GM_ST(uint64_t, ((uint8_t *)&t162 + 8), t161);
                        gm_f_format_e7344u2((void *)&gm_l57, (void *)NULL, cx, NULL, t162);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t163 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
                        if (!t163) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 8, gm_srccp2mbkjs1vn4, 39, 62);
                        gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l57) + 8));
                        gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
                        gm_t76123gfet73g t166;
                        gm_f_trap_qh84l9d((void *)&t166, (void *)NULL, cx, NULL, t158, t159, t165);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t166);
                        goto gm_fdone;
                    }
                    {
                        {
                            gm_t9qkh0rsooc8d t167 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l15) + 16));
                            uint8_t t168 = GM_LD(uint8_t, (((uint8_t *)&t167) + 0));
                            uint8_t t169 = (uint8_t)((t168 == ((uint8_t)2ULL)));
                            if (!t169) {
                                goto gm_lb7;
                            }
                            uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 17));
                            if (!t170) {
                                goto gm_lb7;
                            }
                            uint64_t t171 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                            uint64_t t172 = gm_l13;
                            uint8_t t173 = ((t171 == t172));
                            if (!t173) {
                                goto gm_lb7;
                            }
                            uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                            uint64_t t175 = gm_l14;
                            uint8_t t176 = ((t174 == t175));
                            if (!t176) {
                                goto gm_lb7;
                            }
                            void * t177 = gm_l0;
                            void * t178 = (void *)(((uint8_t *)t177) + 184);
                            uint64_t t179 = gm_l13;
                            gm_t9qkh0rsooc8d t180;
                            GM_ST(uint8_t, ((uint8_t *)&t180 + 0), ((uint8_t)2ULL));
                            uint8_t t181 = gm_f_kill_3kcavfd((void *)NULL, cx, NULL, t178, t179);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
                            goto gm_fdone;
                        }
gm_lb7:;
                    }
gm_lb6:;
                    {
                        {
                            gm_t9qkh0rsooc8d t182 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l15) + 16));
                            uint8_t t183 = GM_LD(uint8_t, (((uint8_t *)&t182) + 0));
                            uint8_t t184 = (uint8_t)((t183 == ((uint8_t)2ULL)));
                            if (!t184) {
                                goto gm_lb9;
                            }
                            uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&gm_l15) + 17));
                            uint8_t t186 = (uint8_t)!t185;
                            if (!t186) {
                                goto gm_lb9;
                            }
                            {
                                void * t187 = gm_l0;
                                gm_tl874t2atnlh3 t188 = gm_l1;
                                uint64_t t189 = gm_l13;
                                gm_t4ce0jd3tdro4 t190;
                                GM_ST(uint64_t, ((uint8_t *)&t190 + 0), t189);
                                gm_f_format_u3ioj90((void *)&gm_l58, (void *)NULL, cx, NULL, t190);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t191 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                if (!t191) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 8, gm_srccp2mbkjs1vn4, 44, 70);
                                gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l58) + 8));
                                gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
                                gm_t76123gfet73g t194;
                                gm_f_trap_qh84l9d((void *)&t194, (void *)NULL, cx, NULL, t187, t188, t193);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t194);
                                goto gm_fdone;
                            }
                        }
gm_lb9:;
                    }
gm_lb8:;
                    {
                        {
                            gm_t9qkh0rsooc8d t195 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l15) + 16));
                            uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)&t195) + 0));
                            uint8_t t197 = (uint8_t)((t196 == ((uint8_t)2ULL)));
                            if (!t197) {
                                goto gm_lb11;
                            }
                            uint64_t t198 = GM_LD(uint64_t, ((uint8_t *)&gm_l15));
                            uint64_t t199 = gm_l13;
                            uint8_t t200 = ((t198 == t199));
                            if (!t200) {
                                goto gm_lb11;
                            }
                            {
                                void * t201 = gm_l0;
                                gm_tl874t2atnlh3 t202 = gm_l1;
                                uint64_t t203 = gm_l13;
                                uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)&gm_l15) + 8));
                                uint64_t t205 = gm_l14;
                                gm_tkpb4kplftgaa t206;
                                GM_ST(uint64_t, ((uint8_t *)&t206 + 0), t203);
                                GM_ST(uint64_t, ((uint8_t *)&t206 + 8), t204);
                                GM_ST(uint64_t, ((uint8_t *)&t206 + 16), t205);
                                gm_f_format_p0ej7hu((void *)&gm_l59, (void *)NULL, cx, NULL, &t206);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t207 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
                                if (!t207) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 8, gm_srccp2mbkjs1vn4, 46, 71);
                                gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l59) + 8));
                                gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                                gm_t76123gfet73g t210;
                                gm_f_trap_qh84l9d((void *)&t210, (void *)NULL, cx, NULL, t201, t202, t209);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t210);
                                goto gm_fdone;
                            }
                        }
gm_lb11:;
                    }
gm_lb10:;
                    {
                        void * t211 = gm_l0;
                        gm_tl874t2atnlh3 t212 = gm_l1;
                        uint64_t t213 = gm_l13;
                        uint64_t t214 = gm_l14;
                        gm_t2lqu8jsj511d t215;
                        GM_ST(uint64_t, ((uint8_t *)&t215 + 0), t213);
                        GM_ST(uint64_t, ((uint8_t *)&t215 + 8), t214);
                        gm_f_format_e7344u2((void *)&gm_l60, (void *)NULL, cx, NULL, t215);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t216 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                        if (!t216) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 8, gm_srccp2mbkjs1vn4, 47, 34);
                        gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l60) + 8));
                        gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&t217));
                        gm_t76123gfet73g t219;
                        gm_f_trap_qh84l9d((void *)&t219, (void *)NULL, cx, NULL, t211, t212, t218);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t219);
                        goto gm_fdone;
                    }
                }
gm_lb5:;
            }
            {
                {
                    {
                        {
                            uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t221 = (uint8_t)(t220 == ((uint8_t)77ULL));
                            if (!t221) {
                                goto gm_lb14;
                            }
                            goto gm_lb13;
                        }
gm_lb14:;
                        {
                            uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t223 = (uint8_t)(t222 == ((uint8_t)78ULL));
                            if (!t223) {
                                goto gm_lb15;
                            }
                            goto gm_lb13;
                        }
gm_lb15:;
                        goto gm_lb12;
                    }
gm_lb13:;
                    void * t224 = gm_l0;
                    gm_t9qkh0rsooc8d t225 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t224) + 88));
                    uint8_t t226 = GM_LD(uint8_t, (((uint8_t *)&t225) + 0));
                    uint8_t t227 = (uint8_t)(!(t226 == ((uint8_t)0ULL)));
                    gm_t9qkh0rsooc8d t228;
                    GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)0ULL));
                    if (t227) {
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
                        goto gm_fdone;
                    }
                    void * t229 = gm_l0;
                    void * t230 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t231 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t231)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t231, gm_srccp2mbkjs1vn4, 51, 27);
                    gm_b16_8 t232 = GM_LD(gm_b16_8, (((uint8_t *)t230) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t233 = GM_LD(gm_b16_8, ((uint8_t *)&t232));
                    uint64_t t234 = gm_f_ptr_of_b77b4c9(t229, t233);
                    gm_l16 = t234;
                    void * t235 = gm_l0;
                    void * t236 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t237 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t237)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t237, gm_srccp2mbkjs1vn4, 52, 31);
                    gm_b16_8 t238 = GM_LD(gm_b16_8, (((uint8_t *)t236) + (uintptr_t)((uint64_t)1ULL) * 16));
                    gm_b16_8 t239 = GM_LD(gm_b16_8, ((uint8_t *)&t238));
                    uint64_t t240 = gm_f_ptr_of_b77b4c9(t235, t239);
                    gm_l17 = t240;
                    void * t241 = gm_l0;
                    void * t242 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t243 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t243)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t243, gm_srccp2mbkjs1vn4, 53, 28);
                    gm_b16_8 t244 = GM_LD(gm_b16_8, (((uint8_t *)t242) + (uintptr_t)((uint64_t)2ULL) * 16));
                    gm_b16_8 t245 = GM_LD(gm_b16_8, ((uint8_t *)&t244));
                    uint64_t t246 = gm_f_ptr_of_b77b4c9(t241, t245);
                    gm_l18 = t246;
                    void * t247 = gm_l0;
                    void * t248 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t249 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)3ULL) >= (uintptr_t)(t249)) gm_trap_index((uintptr_t)((uint64_t)3ULL), t249, gm_srccp2mbkjs1vn4, 54, 35);
                    gm_b16_8 t250 = GM_LD(gm_b16_8, (((uint8_t *)t248) + (uintptr_t)((uint64_t)3ULL) * 16));
                    gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)&t250));
                    gm_t2lqu8jsj511d t252 = gm_f_slice_parts_1se5hu4(t247, t251);
                    gm_l19 = t252;
                    void * t253 = gm_l0;
                    void * t254 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t255 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)4ULL) >= (uintptr_t)(t255)) gm_trap_index((uintptr_t)((uint64_t)4ULL), t255, gm_srccp2mbkjs1vn4, 55, 31);
                    gm_b16_8 t256 = GM_LD(gm_b16_8, (((uint8_t *)t254) + (uintptr_t)((uint64_t)4ULL) * 16));
                    gm_b16_8 t257 = GM_LD(gm_b16_8, ((uint8_t *)&t256));
                    uint64_t t258 = gm_f_get_u64_tgpb2vh(t253, t257);
                    gm_l20 = t258;
                    void * t259 = gm_l0;
                    void * t260 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t261 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)5ULL) >= (uintptr_t)(t261)) gm_trap_index((uintptr_t)((uint64_t)5ULL), t261, gm_srccp2mbkjs1vn4, 56, 33);
                    gm_b16_8 t262 = GM_LD(gm_b16_8, (((uint8_t *)t260) + (uintptr_t)((uint64_t)5ULL) * 16));
                    gm_b16_8 t263 = GM_LD(gm_b16_8, ((uint8_t *)&t262));
                    uint64_t t264 = gm_f_get_u64_tgpb2vh(t259, t263);
                    gm_l21 = t264;
                    gm_t8588ma2cr9d7 t265 = gm_l2;
                    uint8_t t266 = GM_LD(uint8_t, (((uint8_t *)&t265) + 0));
                    uint8_t t267 = (uint8_t)((t266 == ((uint8_t)77ULL)));
                    gm_t8588ma2cr9d7 t268;
                    GM_ST(uint8_t, (uint8_t *)&t268 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t268 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t268 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t268 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t268 + 0), ((uint8_t)77ULL));
                    if (t267) {
                        {
                            void * t269 = gm_l0;
                            void * t270 = (void *)(((uint8_t *)t269) + 1272);
                            uint64_t t271 = gm_l16;
                            uint64_t t272 = gm_l17;
                            uint64_t t273 = gm_l18;
                            uint64_t t274 = GM_LD(uint64_t, ((uint8_t *)&gm_l19));
                            uint64_t t275 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                            gm_t2lqu8jsj511d t276;
                            GM_ST(uint64_t, ((uint8_t *)&t276 + 0), t274);
                            GM_ST(uint64_t, ((uint8_t *)&t276 + 8), t275);
                            uint64_t t277 = gm_l20;
                            uint64_t t278 = gm_l21;
                            gm_t1hfnlc9mhrjq t279;
                            GM_ST(uint64_t, ((uint8_t *)&t279 + 0), t272);
                            GM_ST(uint64_t, ((uint8_t *)&t279 + 8), t273);
                            GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&t279 + 16), t276);
                            GM_ST(uint64_t, ((uint8_t *)&t279 + 32), t277);
                            GM_ST(uint64_t, ((uint8_t *)&t279 + 40), t278);
                            gm_tftb1odc739t5 t280 = gm_f_put_64ej4om(t270, t271, &t279);
                            gm_l61 = t280;
                        }
                        uint8_t t281 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
                        if (!t281) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srccp2mbkjs1vn4, 58, 17);
                        {
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                            GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
                            goto gm_fdone;
                        }
                    }
                    {
                        void * t282 = gm_l0;
                        void * t283 = (void *)(((uint8_t *)t282) + 1272);
                        uint64_t t284 = gm_l16;
                        gm_f_get_hd4d6j0((void *)&gm_l62, t283, t284);
                    }
                    uint8_t t285 = (uint8_t)((((uint8_t *)&gm_l62))[48] != 0);
                    if (t285) {
                        memmove(((uint8_t *)&F.l22), ((uint8_t *)&gm_l62), 48);
                    } else {
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
                        goto gm_fdone;
                    }
                    {
                        void * t286 = gm_l0;
                        void * t287 = (void *)(((uint8_t *)t286) + 1272);
                        uint64_t t288 = gm_l16;
                        gm_tm7c53s68971m t289;
                        gm_f_remove_gjirbtf((void *)&t289, t287, t288);
                    }
                    uint64_t t290 = GM_LD(uint64_t, ((uint8_t *)&F.l22));
                    uint64_t t291 = gm_l17;
                    uint8_t t292 = (!(t290 == t291));
                    if (t292) {
                        gm_l63 = ((uint8_t)1);
                    } else {
                        uint64_t t293 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 8));
                        uint64_t t294 = gm_l18;
                        uint8_t t295 = (!(t293 == t294));
                        gm_l63 = t295;
                    }
                    uint8_t t296 = gm_l63;
                    if (t296) {
                        {
                            void * t297 = gm_l0;
                            gm_tl874t2atnlh3 t298 = gm_l1;
                            gm_t2lqu8jsj511d t299 = GM_LD(gm_t2lqu8jsj511d, (((uint8_t *)&F.l22) + 16));
                            uint64_t t300 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 32));
                            uint64_t t301 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 40));
                            gm_f_tag_site_msl07u1((void *)&gm_l64, (void *)NULL, cx, NULL, t297, t298, t299, t300, t301);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t302 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                        if (t302) {
                        } else {
                            gm_t76123gfet73g t303 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l64) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t303);
                            goto gm_fdone;
                        }
                        gm_b16_8 t304 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l64) + 8));
                        gm_l23 = t304;
                        {
                            void * t305 = gm_l0;
                            gm_tl874t2atnlh3 t306 = gm_l1;
                            uint64_t t307 = GM_LD(uint64_t, ((uint8_t *)&gm_l19));
                            uint64_t t308 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                            gm_t2lqu8jsj511d t309;
                            GM_ST(uint64_t, ((uint8_t *)&t309 + 0), t307);
                            GM_ST(uint64_t, ((uint8_t *)&t309 + 8), t308);
                            uint64_t t310 = gm_l20;
                            uint64_t t311 = gm_l21;
                            gm_f_tag_site_msl07u1((void *)&gm_l65, (void *)NULL, cx, NULL, t305, t306, t309, t310, t311);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        uint8_t t312 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                        if (t312) {
                        } else {
                            gm_t76123gfet73g t313 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l65) + 8));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t313);
                            goto gm_fdone;
                        }
                        gm_b16_8 t314 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l65) + 8));
                        gm_l24 = t314;
                        {
                            void * t315 = gm_l0;
                            gm_tl874t2atnlh3 t316 = gm_l1;
                            gm_b16_8 t317 = gm_l23;
                            gm_b16_8 t318 = gm_l24;
                            gm_t6j6smj7qoctu t319;
                            GM_ST(gm_b16_8, ((uint8_t *)&t319 + 0), t317);
                            GM_ST(gm_b16_8, ((uint8_t *)&t319 + 16), t318);
                            gm_f_format_unbcnc5((void *)&gm_l66, (void *)NULL, cx, NULL, &t319);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t320 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                            if (!t320) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 8, gm_srccp2mbkjs1vn4, 66, 38);
                            gm_b16_8 t321 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l66) + 8));
                            gm_b16_8 t322 = GM_LD(gm_b16_8, ((uint8_t *)&t321));
                            gm_t76123gfet73g t323;
                            gm_f_trap_qh84l9d((void *)&t323, (void *)NULL, cx, NULL, t315, t316, t322);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t323);
                            goto gm_fdone;
                        }
                    }
                    goto gm_lb1;
                }
gm_lb12:;
            }
            {
                {
                    {
                        {
                            {
                                uint8_t t324 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t325 = (uint8_t)(t324 == ((uint8_t)65ULL));
                                if (!t325) {
                                    goto gm_lb18;
                                }
                                goto gm_lb17;
                            }
gm_lb18:;
                            {
                                uint8_t t326 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t327 = (uint8_t)(t326 == ((uint8_t)66ULL));
                                if (!t327) {
                                    goto gm_lb19;
                                }
                                goto gm_lb17;
                            }
gm_lb19:;
                            {
                                uint8_t t328 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t329 = (uint8_t)(t328 == ((uint8_t)67ULL));
                                if (!t329) {
                                    goto gm_lb20;
                                }
                                goto gm_lb17;
                            }
gm_lb20:;
                            {
                                uint8_t t330 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t331 = (uint8_t)(t330 == ((uint8_t)68ULL));
                                if (!t331) {
                                    goto gm_lb21;
                                }
                                goto gm_lb17;
                            }
gm_lb21:;
                            {
                                uint8_t t332 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t333 = (uint8_t)(t332 == ((uint8_t)69ULL));
                                if (!t333) {
                                    goto gm_lb22;
                                }
                                goto gm_lb17;
                            }
gm_lb22:;
                            {
                                uint8_t t334 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t335 = (uint8_t)(t334 == ((uint8_t)71ULL));
                                if (!t335) {
                                    goto gm_lb23;
                                }
                                goto gm_lb17;
                            }
gm_lb23:;
                            {
                                uint8_t t336 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                uint8_t t337 = (uint8_t)(t336 == ((uint8_t)72ULL));
                                if (!t337) {
                                    goto gm_lb24;
                                }
                                goto gm_lb17;
                            }
gm_lb24:;
                            goto gm_lb16;
                        }
gm_lb17:;
                        void * t338 = gm_l0;
                        void * t339 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t340 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t340)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t340, gm_srccp2mbkjs1vn4, 70, 30);
                        gm_b16_8 t341 = GM_LD(gm_b16_8, (((uint8_t *)t339) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t342 = GM_LD(gm_b16_8, ((uint8_t *)&t341));
                        uint64_t t343 = gm_f_ptr_of_b77b4c9(t338, t342);
                        gm_l25 = t343;
                        {
                            {
                                {
                                    {
                                        void * t344 = gm_l0;
                                        void * t345 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                        uint64_t t346 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t346)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t346, gm_srccp2mbkjs1vn4, 71, 35);
                                        void * t347 = GM_LD(void *, (((uint8_t *)t345) + (uintptr_t)((uint64_t)0ULL) * 8));
                                        gm_t88rcucg1b7uk t348 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t347) + 56));
                                        gm_f_kind_htv6093((void *)&gm_l67, t344, GM_CONV(gm_csguankko4lmhd, t348));
                                        uint8_t t349 = GM_LD(uint8_t, (((uint8_t *)&gm_l67) + 0));
                                        uint8_t t350 = (uint8_t)(t349 == ((uint8_t)8ULL));
                                        if (!t350) {
                                            goto gm_lb26;
                                        }
                                        gm_t88rcucg1b7uk t351 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l67) + 8) + 4));
                                        gm_l26 = t351;
                                        void * t352 = gm_l0;
                                        gm_t88rcucg1b7uk t353 = gm_l26;
                                        uint64_t t354 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t352, t353);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l27 = t354;
                                        void * t355 = gm_l0;
                                        void * t356 = gm_l0;
                                        gm_t88rcucg1b7uk t357 = gm_l26;
                                        uint64_t t358 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t356, t357);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_b16_8 t359 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t355, t358);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_l28 = t359;
                                        void * t360 = gm_l0;
                                        gm_tl874t2atnlh3 t361 = gm_l1;
                                        uint64_t t362 = gm_l25;
                                        uint64_t t363 = gm_l27;
                                        gm_b16_8 t364 = gm_l28;
                                        gm_f_load_at_6u81jcc((void *)&gm_l68, (void *)NULL, cx, NULL, t360, t361, t362, t363, t364);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t365 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
                                        if (t365) {
                                        } else {
                                            gm_t76123gfet73g t366 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l68) + 8));
                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t366);
                                            goto gm_fdone;
                                        }
                                        {
                                            {
                                                {
                                                    {
                                                        {
                                                            uint8_t t367 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                            uint8_t t368 = (uint8_t)(t367 == ((uint8_t)65ULL));
                                                            if (!t368) {
                                                                goto gm_lb30;
                                                            }
                                                            goto gm_lb29;
                                                        }
gm_lb30:;
                                                        {
                                                            uint8_t t369 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                            uint8_t t370 = (uint8_t)(t369 == ((uint8_t)71ULL));
                                                            if (!t370) {
                                                                goto gm_lb31;
                                                            }
                                                            goto gm_lb29;
                                                        }
gm_lb31:;
                                                        goto gm_lb28;
                                                    }
gm_lb29:;
                                                    gm_b16_8 t371 = gm_l6;
                                                    gm_b16_8 t372 = gm_l28;
                                                    gm_b16_8 t373 = GM_LD(gm_b16_8, ((uint8_t *)&t372));
                                                    gm_f_copy_from_f7j6p05(t371, t373);
                                                    goto gm_lb27;
                                                }
gm_lb28:;
                                                {
                                                    {
                                                        {
                                                            uint8_t t374 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                            uint8_t t375 = (uint8_t)(t374 == ((uint8_t)66ULL));
                                                            if (!t375) {
                                                                goto gm_lb34;
                                                            }
                                                            goto gm_lb33;
                                                        }
gm_lb34:;
                                                        {
                                                            uint8_t t376 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                            uint8_t t377 = (uint8_t)(t376 == ((uint8_t)72ULL));
                                                            if (!t377) {
                                                                goto gm_lb35;
                                                            }
                                                            goto gm_lb33;
                                                        }
gm_lb35:;
                                                        goto gm_lb32;
                                                    }
gm_lb33:;
                                                    void * t378 = gm_l0;
                                                    gm_tl874t2atnlh3 t379 = gm_l1;
                                                    uint64_t t380 = gm_l25;
                                                    uint64_t t381 = gm_l27;
                                                    void * t382 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                    uint64_t t383 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t383)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t383, gm_srccp2mbkjs1vn4, 77, 80);
                                                    gm_b16_8 t384 = GM_LD(gm_b16_8, (((uint8_t *)t382) + (uintptr_t)((uint64_t)1ULL) * 16));
                                                    gm_b16_8 t385 = GM_LD(gm_b16_8, ((uint8_t *)&t384));
                                                    gm_f_store_at_ktabiji((void *)&gm_l69, (void *)NULL, cx, NULL, t378, t379, t380, t381, t385);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t386 = (uint8_t)!((((uint8_t *)&gm_l69))[0] != 0);
                                                    if (t386) {
                                                    } else {
                                                        gm_t76123gfet73g t387 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l69) + 8));
                                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t387);
                                                        goto gm_fdone;
                                                    }
                                                    goto gm_lb27;
                                                }
gm_lb32:;
                                                {
                                                    uint8_t t388 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                    uint8_t t389 = (uint8_t)(t388 == ((uint8_t)67ULL));
                                                    if (!t389) {
                                                        goto gm_lb36;
                                                    }
                                                    void * t390 = gm_l0;
                                                    gm_tl874t2atnlh3 t391 = gm_l1;
                                                    uint64_t t392 = gm_l25;
                                                    uint64_t t393 = gm_l27;
                                                    void * t394 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                    uint64_t t395 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t395)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t395, gm_srccp2mbkjs1vn4, 79, 52);
                                                    gm_b16_8 t396 = GM_LD(gm_b16_8, (((uint8_t *)t394) + (uintptr_t)((uint64_t)1ULL) * 16));
                                                    gm_b16_8 t397 = GM_LD(gm_b16_8, ((uint8_t *)&t396));
                                                    gm_f_store_at_ktabiji((void *)&gm_l70, (void *)NULL, cx, NULL, t390, t391, t392, t393, t397);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t398 = (uint8_t)!((((uint8_t *)&gm_l70))[0] != 0);
                                                    if (t398) {
                                                    } else {
                                                        gm_t76123gfet73g t399 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l70) + 8));
                                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t399);
                                                        goto gm_fdone;
                                                    }
                                                    gm_b16_8 t400 = gm_l6;
                                                    gm_b16_8 t401 = gm_l28;
                                                    gm_b16_8 t402 = GM_LD(gm_b16_8, ((uint8_t *)&t401));
                                                    gm_f_copy_from_f7j6p05(t400, t402);
                                                    goto gm_lb27;
                                                }
gm_lb36:;
                                            }
                                            {
                                                {
                                                    uint8_t t403 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                    uint8_t t404 = (uint8_t)(t403 == ((uint8_t)68ULL));
                                                    if (!t404) {
                                                        goto gm_lb37;
                                                    }
                                                    void * t405 = gm_l0;
                                                    gm_tl874t2atnlh3 t406 = gm_l1;
                                                    gm_t88rcucg1b7uk t407 = gm_l26;
                                                    gm_b16_8 t408 = gm_l28;
                                                    void * t409 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                    uint64_t t410 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t410)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t410, gm_srccp2mbkjs1vn4, 83, 51);
                                                    gm_b16_8 t411 = GM_LD(gm_b16_8, (((uint8_t *)t409) + (uintptr_t)((uint64_t)1ULL) * 16));
                                                    gm_f_equal_e5vf82h((void *)&gm_l71, (void *)NULL, cx, NULL, t405, t406, t407, t408, t411);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t412 = (uint8_t)!((((uint8_t *)&gm_l71))[0] != 0);
                                                    if (t412) {
                                                    } else {
                                                        gm_t76123gfet73g t413 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l71) + 8));
                                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t413);
                                                        goto gm_fdone;
                                                    }
                                                    uint8_t t414 = GM_LD(uint8_t, (((uint8_t *)&gm_l71) + 8));
                                                    if (t414) {
                                                        void * t415 = gm_l0;
                                                        gm_tl874t2atnlh3 t416 = gm_l1;
                                                        uint64_t t417 = gm_l25;
                                                        uint64_t t418 = gm_l27;
                                                        void * t419 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                        uint64_t t420 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                        if ((uintptr_t)((uint64_t)2ULL) >= (uintptr_t)(t420)) gm_trap_index((uintptr_t)((uint64_t)2ULL), t420, gm_srccp2mbkjs1vn4, 84, 56);
                                                        gm_b16_8 t421 = GM_LD(gm_b16_8, (((uint8_t *)t419) + (uintptr_t)((uint64_t)2ULL) * 16));
                                                        gm_b16_8 t422 = GM_LD(gm_b16_8, ((uint8_t *)&t421));
                                                        gm_f_store_at_ktabiji((void *)&gm_l72, (void *)NULL, cx, NULL, t415, t416, t417, t418, t422);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                        uint8_t t423 = (uint8_t)!((((uint8_t *)&gm_l72))[0] != 0);
                                                        if (t423) {
                                                        } else {
                                                            gm_t76123gfet73g t424 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l72) + 8));
                                                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t424);
                                                            goto gm_fdone;
                                                        }
                                                        gm_b16_8 t425 = gm_l6;
                                                        gm_f_fill_abmf5g5(t425, ((uint8_t)0ULL));
                                                    } else {
                                                        void * t426 = gm_l0;
                                                        gm_t88rcucg1b7uk t427 = gm_l5;
                                                        gm_b16_8 t428 = gm_l6;
                                                        gm_b16_8 t429 = gm_l28;
                                                        gm_b16_8 t430 = GM_LD(gm_b16_8, ((uint8_t *)&t429));
                                                        gm_b0_1 t431 = gm_f_make_some_tvb9ubk((void *)NULL, cx, NULL, t426, t427, t428, t430);
                                                        if (gm_unw.kind) {
                                                            goto gm_lad;
                                                        }
                                                    }
                                                    goto gm_lb27;
                                                }
gm_lb37:;
                                                {
                                                    uint8_t t432 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                                                    uint8_t t433 = (uint8_t)(t432 == ((uint8_t)69ULL));
                                                    if (!t433) {
                                                        goto gm_lb38;
                                                    }
                                                    gm_t9qkh0rsooc8d t434 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l2) + 4));
                                                    gm_l29 = t434;
                                                    void * t435 = gm_l0;
                                                    gm_t88rcucg1b7uk t436 = gm_l26;
                                                    gm_b16_8 t437 = gm_l28;
                                                    gm_b16_8 t438 = GM_LD(gm_b16_8, ((uint8_t *)&t437));
                                                    unsigned __int128 t439 = gm_f_get_int_pg7p4gc(t435, GM_CONV(gm_csguankko4lmhd, t436), t438);
                                                    gm_l30 = t439;
                                                    void * t440 = gm_l0;
                                                    gm_t88rcucg1b7uk t441 = gm_l26;
                                                    void * t442 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                                    uint64_t t443 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                                    if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t443)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t443, gm_srccp2mbkjs1vn4, 92, 40);
                                                    gm_b16_8 t444 = GM_LD(gm_b16_8, (((uint8_t *)t442) + (uintptr_t)((uint64_t)1ULL) * 16));
                                                    gm_b16_8 t445 = GM_LD(gm_b16_8, ((uint8_t *)&t444));
                                                    unsigned __int128 t446 = gm_f_get_int_pg7p4gc(t440, GM_CONV(gm_csguankko4lmhd, t441), t445);
                                                    gm_l31 = t446;
                                                    void * t447 = gm_l0;
                                                    gm_t88rcucg1b7uk t448 = gm_l26;
                                                    gm_tg5lql6itufk7 t449 = gm_f_int_info_ulcevdv(t447, GM_CONV(gm_csguankko4lmhd, t448));
                                                    gm_l74 = t449;
                                                    uint8_t t450 = (uint8_t)((((uint8_t *)&gm_l74))[8] != 0);
                                                    if (t450) {
                                                        gm_ttovhfee8mf28 t451 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&gm_l74));
                                                        gm_l73 = t451;
                                                    } else {
                                                        gm_ttovhfee8mf28 t452;
                                                        GM_ST(uint8_t, (uint8_t *)&t452 + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&t452 + 2, 0);
                                                        GM_ST(uint8_t, ((uint8_t *)&t452 + 0), ((uint8_t)0));
                                                        GM_ST(uint32_t, ((uint8_t *)&t452 + 4), ((uint32_t)0ULL));
                                                        gm_l73 = t452;
                                                    }
                                                    gm_ttovhfee8mf28 t453 = gm_l73;
                                                    gm_l75 = t453;
                                                    uint8_t t454 = GM_LD(uint8_t, ((uint8_t *)&gm_l75));
                                                    gm_l32 = t454;
                                                    {
                                                        {
                                                            uint8_t t455 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t456 = (uint8_t)(t455 == ((uint8_t)0ULL));
                                                            if (!t456) {
                                                                goto gm_lb40;
                                                            }
                                                            unsigned __int128 t457 = gm_l30;
                                                            unsigned __int128 t458 = gm_l31;
                                                            unsigned __int128 r459; bool o460 = __builtin_add_overflow(t457, t458, &r459);
                                                            unsigned __int128 t461 = r459;
                                                            gm_l33 = t461;
                                                            goto gm_lb39;
                                                        }
gm_lb40:;
                                                        {
                                                            uint8_t t462 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t463 = (uint8_t)(t462 == ((uint8_t)1ULL));
                                                            if (!t463) {
                                                                goto gm_lb41;
                                                            }
                                                            unsigned __int128 t464 = gm_l30;
                                                            unsigned __int128 t465 = gm_l31;
                                                            unsigned __int128 r466; bool o467 = __builtin_sub_overflow(t464, t465, &r466);
                                                            unsigned __int128 t468 = r466;
                                                            gm_l33 = t468;
                                                            goto gm_lb39;
                                                        }
gm_lb41:;
                                                        {
                                                            uint8_t t469 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t470 = (uint8_t)(t469 == ((uint8_t)2ULL));
                                                            if (!t470) {
                                                                goto gm_lb42;
                                                            }
                                                            unsigned __int128 t471 = gm_l30;
                                                            unsigned __int128 t472 = gm_l31;
                                                            unsigned __int128 t473 = (unsigned __int128)(t471 & t472);
                                                            gm_l33 = t473;
                                                            goto gm_lb39;
                                                        }
gm_lb42:;
                                                        {
                                                            uint8_t t474 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t475 = (uint8_t)(t474 == ((uint8_t)3ULL));
                                                            if (!t475) {
                                                                goto gm_lb43;
                                                            }
                                                            unsigned __int128 t476 = gm_l30;
                                                            unsigned __int128 t477 = gm_l31;
                                                            unsigned __int128 t478 = (unsigned __int128)(t476 | t477);
                                                            gm_l33 = t478;
                                                            goto gm_lb39;
                                                        }
gm_lb43:;
                                                        {
                                                            uint8_t t479 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t480 = (uint8_t)(t479 == ((uint8_t)4ULL));
                                                            if (!t480) {
                                                                goto gm_lb44;
                                                            }
                                                            unsigned __int128 t481 = gm_l30;
                                                            unsigned __int128 t482 = gm_l31;
                                                            unsigned __int128 t483 = (unsigned __int128)(t481 ^ t482);
                                                            gm_l33 = t483;
                                                            goto gm_lb39;
                                                        }
gm_lb44:;
                                                        {
                                                            uint8_t t484 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t485 = (uint8_t)(t484 == ((uint8_t)5ULL));
                                                            if (!t485) {
                                                                goto gm_lb45;
                                                            }
                                                            uint8_t t486 = gm_l32;
                                                            if (t486) {
                                                                unsigned __int128 t487 = gm_l31;
                                                                __int128 t488;
                                                                memset(&t488, 0, sizeof t488); memcpy(&t488, ((uint8_t *)&t487), sizeof t488 < 16 ? sizeof t488 : 16);
                                                                unsigned __int128 t489 = gm_l30;
                                                                __int128 t490;
                                                                memset(&t490, 0, sizeof t490); memcpy(&t490, ((uint8_t *)&t489), sizeof t490 < 16 ? sizeof t490 : 16);
                                                                uint8_t t491 = (t488 > t490);
                                                                gm_l78 = t491;
                                                            } else {
                                                                gm_l78 = ((uint8_t)0);
                                                            }
                                                            uint8_t t492 = gm_l78;
                                                            if (t492) {
                                                                gm_l77 = ((uint8_t)1);
                                                            } else {
                                                                uint8_t t493 = gm_l32;
                                                                uint8_t t494 = (uint8_t)!t493;
                                                                if (t494) {
                                                                    unsigned __int128 t495 = gm_l31;
                                                                    unsigned __int128 t496 = gm_l30;
                                                                    uint8_t t497 = (t495 > t496);
                                                                    gm_l79 = t497;
                                                                } else {
                                                                    gm_l79 = ((uint8_t)0);
                                                                }
                                                                uint8_t t498 = gm_l79;
                                                                gm_l77 = t498;
                                                            }
                                                            uint8_t t499 = gm_l77;
                                                            if (t499) {
                                                                unsigned __int128 t500 = gm_l31;
                                                                gm_l76 = t500;
                                                            } else {
                                                                unsigned __int128 t501 = gm_l30;
                                                                gm_l76 = t501;
                                                            }
                                                            unsigned __int128 t502 = gm_l76;
                                                            gm_l33 = t502;
                                                            goto gm_lb39;
                                                        }
gm_lb45:;
                                                        {
                                                            uint8_t t503 = GM_LD(uint8_t, (((uint8_t *)&gm_l29) + 0));
                                                            uint8_t t504 = (uint8_t)(t503 == ((uint8_t)6ULL));
                                                            if (!t504) {
                                                                goto gm_lb46;
                                                            }
                                                            uint8_t t505 = gm_l32;
                                                            if (t505) {
                                                                unsigned __int128 t506 = gm_l31;
                                                                __int128 t507;
                                                                memset(&t507, 0, sizeof t507); memcpy(&t507, ((uint8_t *)&t506), sizeof t507 < 16 ? sizeof t507 : 16);
                                                                unsigned __int128 t508 = gm_l30;
                                                                __int128 t509;
                                                                memset(&t509, 0, sizeof t509); memcpy(&t509, ((uint8_t *)&t508), sizeof t509 < 16 ? sizeof t509 : 16);
                                                                uint8_t t510 = (t507 < t509);
                                                                gm_l82 = t510;
                                                            } else {
                                                                gm_l82 = ((uint8_t)0);
                                                            }
                                                            uint8_t t511 = gm_l82;
                                                            if (t511) {
                                                                gm_l81 = ((uint8_t)1);
                                                            } else {
                                                                uint8_t t512 = gm_l32;
                                                                uint8_t t513 = (uint8_t)!t512;
                                                                if (t513) {
                                                                    unsigned __int128 t514 = gm_l31;
                                                                    unsigned __int128 t515 = gm_l30;
                                                                    uint8_t t516 = (t514 < t515);
                                                                    gm_l83 = t516;
                                                                } else {
                                                                    gm_l83 = ((uint8_t)0);
                                                                }
                                                                uint8_t t517 = gm_l83;
                                                                gm_l81 = t517;
                                                            }
                                                            uint8_t t518 = gm_l81;
                                                            if (t518) {
                                                                unsigned __int128 t519 = gm_l31;
                                                                gm_l80 = t519;
                                                            } else {
                                                                unsigned __int128 t520 = gm_l30;
                                                                gm_l80 = t520;
                                                            }
                                                            unsigned __int128 t521 = gm_l80;
                                                            gm_l33 = t521;
                                                            goto gm_lb39;
                                                        }
gm_lb46:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srccp2mbkjs1vn4, 94, 31);
                                                    }
gm_lb39:;
                                                    void * t522 = gm_l0;
                                                    void * t523 = gm_l0;
                                                    gm_t88rcucg1b7uk t524 = gm_l26;
                                                    uint64_t t525 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t523, t524);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_b16_8 t526 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t522, t525);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    gm_l34 = t526;
                                                    void * t527 = gm_l0;
                                                    gm_b16_8 t528 = gm_l34;
                                                    unsigned __int128 t529 = gm_l33;
                                                    gm_f_put_uint_cddin99(t527, t528, t529);
                                                    void * t530 = gm_l0;
                                                    gm_tl874t2atnlh3 t531 = gm_l1;
                                                    uint64_t t532 = gm_l25;
                                                    uint64_t t533 = gm_l27;
                                                    gm_b16_8 t534 = gm_l34;
                                                    gm_b16_8 t535 = GM_LD(gm_b16_8, ((uint8_t *)&t534));
                                                    gm_f_store_at_ktabiji((void *)&gm_l84, (void *)NULL, cx, NULL, t530, t531, t532, t533, t535);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                    uint8_t t536 = (uint8_t)!((((uint8_t *)&gm_l84))[0] != 0);
                                                    if (t536) {
                                                    } else {
                                                        gm_t76123gfet73g t537 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l84) + 8));
                                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t537);
                                                        goto gm_fdone;
                                                    }
                                                    gm_b16_8 t538 = gm_l6;
                                                    gm_b16_8 t539 = gm_l28;
                                                    gm_b16_8 t540 = GM_LD(gm_b16_8, ((uint8_t *)&t539));
                                                    gm_f_copy_from_f7j6p05(t538, t540);
                                                    goto gm_lb27;
                                                }
gm_lb38:;
                                            }
                                            {
                                                {
                                                    goto gm_lb27;
                                                }
gm_lb47:;
                                                gm_trap("no match arm matched the scrutinee", gm_srccp2mbkjs1vn4, 75, 13);
                                            }
                                        }
gm_lb27:;
                                    }
                                    {
                                        goto gm_lb25;
                                    }
                                }
gm_lb26:;
                            }
                            {
                                void * t541 = gm_l0;
                                gm_tl874t2atnlh3 t542 = gm_l1;
                                gm_b16_8 t543;
                                GM_ST(void *, (uint8_t *)&t543, (void *)(gm_s8pst1ovjhtbk));
                                GM_ST(uintptr_t, (uint8_t *)&t543 + 8, (uintptr_t)(70));
                                gm_t76123gfet73g t544;
                                gm_f_trap_qh84l9d((void *)&t544, (void *)NULL, cx, NULL, t541, t542, t543);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t544);
                                goto gm_fdone;
                            }
                        }
gm_lb25:;
                    }
                    {
                        goto gm_lb1;
                    }
                }
gm_lb16:;
            }
            {
                {
                    uint8_t t545 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t546 = (uint8_t)(t545 == ((uint8_t)70ULL));
                    if (!t546) {
                        goto gm_lb48;
                    }
                    goto gm_lb1;
                }
gm_lb48:;
                {
                    uint8_t t547 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t548 = (uint8_t)(t547 == ((uint8_t)79ULL));
                    if (!t548) {
                        goto gm_lb49;
                    }
                    {
                        {
                            {
                                void * t549 = gm_l0;
                                void * t550 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                uint64_t t551 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t551)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t551, gm_srccp2mbkjs1vn4, 113, 39);
                                void * t552 = GM_LD(void *, (((uint8_t *)t550) + (uintptr_t)((uint64_t)0ULL) * 8));
                                gm_t88rcucg1b7uk t553 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t552) + 56));
                                gm_f_kind_htv6093((void *)&gm_l85, t549, GM_CONV(gm_csguankko4lmhd, t553));
                            }
                            uint8_t t554 = GM_LD(uint8_t, (((uint8_t *)&gm_l85) + 0));
                            uint8_t t555 = (uint8_t)(t554 == ((uint8_t)10ULL));
                            if (!t555) {
                                goto gm_lb51;
                            }
                            gm_t88rcucg1b7uk t556 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l85) + 8) + 4));
                            gm_l35 = t556;
                            void * t557 = gm_l0;
                            void * t558 = GM_LD(void *, ((uint8_t *)&gm_l4));
                            uint64_t t559 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t559)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t559, gm_srccp2mbkjs1vn4, 114, 32);
                            gm_b16_8 t560 = GM_LD(gm_b16_8, (((uint8_t *)t558) + (uintptr_t)((uint64_t)0ULL) * 16));
                            gm_b16_8 t561 = GM_LD(gm_b16_8, ((uint8_t *)&t560));
                            gm_t2lqu8jsj511d t562 = gm_f_slice_parts_1se5hu4(t557, t561);
                            gm_l36 = t562;
                            void * t563 = gm_l0;
                            void * t564 = GM_LD(void *, ((uint8_t *)&gm_l4));
                            uint64_t t565 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                            if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t565)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t565, gm_srccp2mbkjs1vn4, 115, 32);
                            gm_b16_8 t566 = GM_LD(gm_b16_8, (((uint8_t *)t564) + (uintptr_t)((uint64_t)1ULL) * 16));
                            gm_b16_8 t567 = GM_LD(gm_b16_8, ((uint8_t *)&t566));
                            gm_t2lqu8jsj511d t568 = gm_f_slice_parts_1se5hu4(t563, t567);
                            gm_l37 = t568;
                            uint64_t t569 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                            void * t570 = gm_l0;
                            gm_t88rcucg1b7uk t571 = gm_l35;
                            uint64_t t572 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t570, t571);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint64_t r573; bool o574 = __builtin_mul_overflow(t569, t572, &r573);
                            if (o574) gm_trap("integer overflow in `*`", gm_srccp2mbkjs1vn4, 116, 18);
                            uint64_t t575 = r573;
                            gm_l38 = t575;
                            uint64_t t576 = gm_l38;
                            uint8_t t577 = ((t576 == ((uint64_t)0ULL)));
                            if (t577) {
                                GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
                                ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
                                GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
                                goto gm_fdone;
                            }
                            {
                                void * t578 = gm_l0;
                                void * t579 = (void *)(((uint8_t *)t578) + 184);
                                uint64_t t580 = GM_LD(uint64_t, ((uint8_t *)&gm_l37));
                                uint64_t t581 = gm_l38;
                                gm_f_read_u6r4vns((void *)&gm_l86, t579, t580, t581, ((uint64_t)1ULL));
                            }
                            uint8_t t582 = (uint8_t)!((((uint8_t *)&gm_l86))[0] != 0);
                            if (t582) {
                                gm_b16_8 t583 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l86) + 8));
                                gm_l40 = t583;
                            } else {
                                {
                                    memmove(((uint8_t *)&F.l39), (((uint8_t *)&gm_l86) + 8), 40);
                                }
                                {
                                    void * t584 = gm_l0;
                                    gm_tl874t2atnlh3 t585 = gm_l1;
                                    gm_t76123gfet73g t586;
                                    gm_f_mem_trap_ecogko0((void *)&t586, (void *)NULL, cx, NULL, t584, t585, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l39)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t586);
                                    goto gm_fdone;
                                }
                            }
                            void * t587 = gm_l0;
                            uint64_t t588 = gm_l38;
                            gm_b16_8 t589 = gm_f_tmp_8k6u9c9((void *)NULL, cx, NULL, t587, t588);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l41 = t589;
                            gm_b16_8 t590 = gm_l41;
                            gm_b16_8 t591 = gm_l40;
                            gm_b16_8 t592 = GM_LD(gm_b16_8, ((uint8_t *)&t591));
                            gm_f_copy_from_f7j6p05(t590, t592);
                            {
                                void * t593 = gm_l0;
                                void * t594 = (void *)(((uint8_t *)t593) + 184);
                                uint64_t t595 = GM_LD(uint64_t, ((uint8_t *)&gm_l36));
                                gm_b16_8 t596 = gm_l41;
                                gm_b16_8 t597 = GM_LD(gm_b16_8, ((uint8_t *)&t596));
                                gm_f_write_5gub0ul((void *)&gm_l87, t594, t595, t597, ((uint64_t)1ULL));
                            }
                            uint8_t t598 = (uint8_t)!((((uint8_t *)&gm_l87))[0] != 0);
                            if (t598) {
                            } else {
                                {
                                    memmove(((uint8_t *)&F.l42), (((uint8_t *)&gm_l87) + 8), 40);
                                }
                                {
                                    void * t599 = gm_l0;
                                    gm_tl874t2atnlh3 t600 = gm_l1;
                                    gm_t76123gfet73g t601;
                                    gm_f_mem_trap_ecogko0((void *)&t601, (void *)NULL, cx, NULL, t599, t600, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l42)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t601);
                                    goto gm_fdone;
                                }
                            }
                            goto gm_lb50;
                        }
gm_lb51:;
                        {
                            void * t602 = gm_l0;
                            gm_tl874t2atnlh3 t603 = gm_l1;
                            gm_b16_8 t604;
                            GM_ST(void *, (uint8_t *)&t604, (void *)(gm_sp0ag7curvl4o));
                            GM_ST(uintptr_t, (uint8_t *)&t604 + 8, (uintptr_t)(41));
                            gm_t76123gfet73g t605;
                            gm_f_trap_qh84l9d((void *)&t605, (void *)NULL, cx, NULL, t602, t603, t604);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t605);
                            goto gm_fdone;
                        }
                    }
gm_lb50:;
                    goto gm_lb1;
                }
gm_lb49:;
            }
            {
                {
                    uint8_t t606 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                    uint8_t t607 = (uint8_t)(t606 == ((uint8_t)61ULL));
                    if (!t607) {
                        goto gm_lb52;
                    }
                    void * t608 = gm_l0;
                    void * t609 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t610 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t610)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t610, gm_srccp2mbkjs1vn4, 125, 27);
                    gm_b16_8 t611 = GM_LD(gm_b16_8, (((uint8_t *)t609) + (uintptr_t)((uint64_t)0ULL) * 16));
                    gm_b16_8 t612 = GM_LD(gm_b16_8, ((uint8_t *)&t611));
                    uint64_t t613 = gm_f_ptr_of_b77b4c9(t608, t612);
                    gm_l43 = t613;
                    gm_l44 = ((uint64_t)0ULL);
                    {
                        for (;;) {
                            if (!((uint8_t)1)) {
                                goto gm_lb53;
                            }
                            {
                                void * t614 = gm_l0;
                                void * t615 = (void *)(((uint8_t *)t614) + 184);
                                uint64_t t616 = gm_l43;
                                uint64_t t617 = gm_l44;
                                uint64_t r618; bool o619 = __builtin_add_overflow(t616, t617, &r618);
                                if (o619) gm_trap("integer overflow in `+`", gm_srccp2mbkjs1vn4, 128, 33);
                                uint64_t t620 = r618;
                                gm_f_read_u6r4vns((void *)&gm_l88, t615, t620, ((uint64_t)1ULL), ((uint64_t)1ULL));
                                uint8_t t621 = (uint8_t)!((((uint8_t *)&gm_l88))[0] != 0);
                                if (t621) {
                                    gm_b16_8 t622 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l88) + 8));
                                    gm_l46 = t622;
                                } else {
                                    {
                                        memmove(((uint8_t *)&F.l45), (((uint8_t *)&gm_l88) + 8), 40);
                                    }
                                    {
                                        void * t623 = gm_l0;
                                        gm_tl874t2atnlh3 t624 = gm_l1;
                                        gm_t76123gfet73g t625;
                                        gm_f_mem_trap_ecogko0((void *)&t625, (void *)NULL, cx, NULL, t623, t624, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l45)));
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t625);
                                        goto gm_fdone;
                                    }
                                }
                                void * t626 = GM_LD(void *, ((uint8_t *)&gm_l46));
                                uint64_t t627 = GM_LD(uint64_t, (((uint8_t *)&gm_l46) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t627)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t627, gm_srccp2mbkjs1vn4, 129, 20);
                                uint8_t t628 = GM_LD(uint8_t, (((uint8_t *)t626) + (uintptr_t)((uint64_t)0ULL) * 1));
                                uint8_t t629 = ((t628 == ((uint8_t)0ULL)));
                                if (t629) {
                                    goto gm_lb53;
                                }
                                uint64_t t630 = gm_l44;
                                uint64_t r631; bool o632 = __builtin_add_overflow(t630, ((uint64_t)1ULL), &r631);
                                if (o632) gm_trap("integer overflow in `+`", gm_srccp2mbkjs1vn4, 130, 17);
                                uint64_t t633 = r631;
                                gm_l44 = t633;
                            }
gm_lb55:;
                        }
                    }
gm_lb53:;
                    void * t634 = gm_l0;
                    gm_b16_8 t635 = gm_l6;
                    uint64_t t636 = gm_l44;
                    gm_f_put_u64_irp77r2(t634, t635, t636);
                    goto gm_lb1;
                }
gm_lb52:;
                {
                    {
                        {
                            uint8_t t637 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t638 = (uint8_t)(t637 == ((uint8_t)59ULL));
                            if (!t638) {
                                goto gm_lb58;
                            }
                            goto gm_lb57;
                        }
gm_lb58:;
                        {
                            uint8_t t639 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
                            uint8_t t640 = (uint8_t)(t639 == ((uint8_t)60ULL));
                            if (!t640) {
                                goto gm_lb59;
                            }
                            goto gm_lb57;
                        }
gm_lb59:;
                        goto gm_lb56;
                    }
gm_lb57:;
                    gm_t8588ma2cr9d7 t641 = gm_l2;
                    uint8_t t642 = GM_LD(uint8_t, (((uint8_t *)&t641) + 0));
                    uint8_t t643 = (uint8_t)((t642 == ((uint8_t)59ULL)));
                    gm_t8588ma2cr9d7 t644;
                    GM_ST(uint8_t, (uint8_t *)&t644 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t644 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t644 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t644 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t644 + 0), ((uint8_t)59ULL));
                    if (t643) {
                        gm_t88rcucg1b7uk t645 = gm_l5;
                        gm_l47 = t645;
                    } else {
                        void * t646 = GM_LD(void *, ((uint8_t *)&gm_l3));
                        uint64_t t647 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t647)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t647, gm_srccp2mbkjs1vn4, 135, 55);
                        void * t648 = GM_LD(void *, (((uint8_t *)t646) + (uintptr_t)((uint64_t)0ULL) * 8));
                        gm_t88rcucg1b7uk t649 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t648) + 56));
                        gm_l47 = t649;
                    }
                    void * t650 = gm_l0;
                    gm_t8588ma2cr9d7 t651 = gm_l2;
                    uint8_t t652 = GM_LD(uint8_t, (((uint8_t *)&t651) + 0));
                    uint8_t t653 = (uint8_t)((t652 == ((uint8_t)59ULL)));
                    gm_t8588ma2cr9d7 t654;
                    GM_ST(uint8_t, (uint8_t *)&t654 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t654 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t654 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t654 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t654 + 0), ((uint8_t)59ULL));
                    if (t653) {
                        void * t655 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t656 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t656)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t656, gm_srccp2mbkjs1vn4, 136, 53);
                        gm_b16_8 t657 = GM_LD(gm_b16_8, (((uint8_t *)t655) + (uintptr_t)((uint64_t)0ULL) * 16));
                        gm_b16_8 t658 = GM_LD(gm_b16_8, ((uint8_t *)&t657));
                        gm_l89 = t658;
                    } else {
                        void * t659 = GM_LD(void *, ((uint8_t *)&gm_l4));
                        uint64_t t660 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                        if ((uintptr_t)((uint64_t)1ULL) >= (uintptr_t)(t660)) gm_trap_index((uintptr_t)((uint64_t)1ULL), t660, gm_srccp2mbkjs1vn4, 136, 67);
                        gm_b16_8 t661 = GM_LD(gm_b16_8, (((uint8_t *)t659) + (uintptr_t)((uint64_t)1ULL) * 16));
                        gm_b16_8 t662 = GM_LD(gm_b16_8, ((uint8_t *)&t661));
                        gm_l89 = t662;
                    }
                    gm_b16_8 t663 = gm_l89;
                    gm_t2lqu8jsj511d t664 = gm_f_slice_parts_1se5hu4(t650, t663);
                    gm_l48 = t664;
                    {
                        {
                            void * t665 = gm_l0;
                            gm_t88rcucg1b7uk t666 = gm_l47;
                            gm_f_kind_htv6093((void *)&gm_l90, t665, GM_CONV(gm_csguankko4lmhd, t666));
                            uint8_t t667 = GM_LD(uint8_t, (((uint8_t *)&gm_l90) + 0));
                            uint8_t t668 = (uint8_t)(t667 == ((uint8_t)11ULL));
                            if (!t668) {
                                goto gm_lb61;
                            }
                            uint64_t t669 = GM_LD(uint64_t, (((uint8_t *)&gm_l90) + 8));
                            gm_l49 = t669;
                            gm_t88rcucg1b7uk t670 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l90) + 8) + 8));
                            gm_l50 = t670;
                            {
                                {
                                    uint64_t t671 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                                    uint64_t t672 = gm_l49;
                                    uint8_t t673 = (t671 < t672);
                                    if (!t673) {
                                        goto gm_lb63;
                                    }
                                    void * t674 = gm_l0;
                                    uint8_t t675 = gm_f_fast_23em7sv(t674);
                                    uint8_t t676 = (uint8_t)!t675;
                                    if (!t676) {
                                        goto gm_lb63;
                                    }
                                    gm_t8588ma2cr9d7 t677 = gm_l2;
                                    uint8_t t678 = GM_LD(uint8_t, (((uint8_t *)&t677) + 0));
                                    uint8_t t679 = (uint8_t)((t678 == ((uint8_t)59ULL)));
                                    gm_t8588ma2cr9d7 t680;
                                    GM_ST(uint8_t, (uint8_t *)&t680 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t680 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t680 + 4, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t680 + 8, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t680 + 0), ((uint8_t)59ULL));
                                    if (t679) {
                                        gm_b16_8 t681;
                                        GM_ST(void *, (uint8_t *)&t681, (void *)(gm_sqfrv3g5v9m2n));
                                        GM_ST(uintptr_t, (uint8_t *)&t681 + 8, (uintptr_t)(4));
                                        gm_l51 = t681;
                                    } else {
                                        gm_b16_8 t682;
                                        GM_ST(void *, (uint8_t *)&t682, (void *)(gm_s0ugop4l95o1j));
                                        GM_ST(uintptr_t, (uint8_t *)&t682 + 8, (uintptr_t)(5));
                                        gm_l51 = t682;
                                    }
                                    {
                                        void * t683 = gm_l0;
                                        gm_tl874t2atnlh3 t684 = gm_l1;
                                        gm_b16_8 t685 = gm_l51;
                                        uint64_t t686 = gm_l49;
                                        uint64_t t687 = GM_LD(uint64_t, (((uint8_t *)&gm_l48) + 8));
                                        gm_tgbgti82mf879 t688;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t688 + 0), t685);
                                        GM_ST(uint64_t, ((uint8_t *)&t688 + 16), t686);
                                        GM_ST(uint64_t, ((uint8_t *)&t688 + 24), t687);
                                        gm_f_format_37g0gks((void *)&gm_l91, (void *)NULL, cx, NULL, &t688);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t689 = (uint8_t)!((((uint8_t *)&gm_l91))[0] != 0);
                                        if (!t689) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l91) + 8, gm_srccp2mbkjs1vn4, 140, 38);
                                        gm_b16_8 t690 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l91) + 8));
                                        gm_b16_8 t691 = GM_LD(gm_b16_8, ((uint8_t *)&t690));
                                        gm_t76123gfet73g t692;
                                        gm_f_trap_qh84l9d((void *)&t692, (void *)NULL, cx, NULL, t683, t684, t691);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                        GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t692);
                                        goto gm_fdone;
                                    }
                                }
gm_lb63:;
                            }
gm_lb62:;
                            void * t693 = gm_l0;
                            gm_t88rcucg1b7uk t694 = gm_l50;
                            uint64_t t695 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t693, t694);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l52 = t695;
                            gm_t8588ma2cr9d7 t696 = gm_l2;
                            uint8_t t697 = GM_LD(uint8_t, (((uint8_t *)&t696) + 0));
                            uint8_t t698 = (uint8_t)((t697 == ((uint8_t)59ULL)));
                            gm_t8588ma2cr9d7 t699;
                            GM_ST(uint8_t, (uint8_t *)&t699 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t699 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t699 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&t699 + 8, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t699 + 0), ((uint8_t)59ULL));
                            if (t698) {
                                void * t700 = gm_l0;
                                gm_tl874t2atnlh3 t701 = gm_l1;
                                uint64_t t702 = GM_LD(uint64_t, ((uint8_t *)&gm_l48));
                                uint64_t t703 = gm_l52;
                                gm_b16_8 t704 = gm_l6;
                                gm_f_load_at_6u81jcc((void *)&gm_l92, (void *)NULL, cx, NULL, t700, t701, t702, t703, t704);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t705 = (uint8_t)!((((uint8_t *)&gm_l92))[0] != 0);
                                if (t705) {
                                } else {
                                    gm_t76123gfet73g t706 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l92) + 8));
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t706);
                                    goto gm_fdone;
                                }
                            } else {
                                void * t707 = gm_l0;
                                gm_tl874t2atnlh3 t708 = gm_l1;
                                uint64_t t709 = GM_LD(uint64_t, ((uint8_t *)&gm_l48));
                                uint64_t t710 = gm_l52;
                                void * t711 = GM_LD(void *, ((uint8_t *)&gm_l4));
                                uint64_t t712 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t712)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t712, gm_srccp2mbkjs1vn4, 143, 112);
                                gm_b16_8 t713 = GM_LD(gm_b16_8, (((uint8_t *)t711) + (uintptr_t)((uint64_t)0ULL) * 16));
                                gm_b16_8 t714 = GM_LD(gm_b16_8, ((uint8_t *)&t713));
                                gm_f_store_at_ktabiji((void *)&gm_l93, (void *)NULL, cx, NULL, t707, t708, t709, t710, t714);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint8_t t715 = (uint8_t)!((((uint8_t *)&gm_l93))[0] != 0);
                                if (t715) {
                                } else {
                                    gm_t76123gfet73g t716 = GM_LD(gm_t76123gfet73g, (((uint8_t *)&gm_l93) + 8));
                                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t716);
                                    goto gm_fdone;
                                }
                            }
                            goto gm_lb60;
                        }
gm_lb61:;
                        {
                            void * t717 = gm_l0;
                            gm_tl874t2atnlh3 t718 = gm_l1;
                            gm_b16_8 t719;
                            GM_ST(void *, (uint8_t *)&t719, (void *)(gm_s2qp8mqi0c2le));
                            GM_ST(uintptr_t, (uint8_t *)&t719 + 8, (uintptr_t)(47));
                            gm_t76123gfet73g t720;
                            gm_f_trap_qh84l9d((void *)&t720, (void *)NULL, cx, NULL, t717, t718, t719);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                            GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t720);
                            goto gm_fdone;
                        }
                    }
gm_lb60:;
                    goto gm_lb1;
                }
gm_lb56:;
            }
            {
                {
                    void * t721 = gm_l0;
                    gm_tl874t2atnlh3 t722 = gm_l1;
                    gm_b16_8 t723;
                    GM_ST(void *, (uint8_t *)&t723, (void *)(gm_s1gjt02j7um1i));
                    GM_ST(uintptr_t, (uint8_t *)&t723 + 8, (uintptr_t)(36));
                    gm_t76123gfet73g t724;
                    gm_f_trap_qh84l9d((void *)&t724, (void *)NULL, cx, NULL, t721, t722, t723);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
                    ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 1;
                    GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), t724);
                    goto gm_fdone;
                }
gm_lb64:;
                gm_trap("no match arm matched the scrutinee", gm_srccp2mbkjs1vn4, 10, 5);
            }
        }
gm_lb1:;
    }
    {
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tcfa6l7reoosn *)gm_out) + 8), GM_ZERO(gm_b0_1));
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
inline __attribute__((aligned(4))) void gm_f_tag_site_msl07u1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t2lqu8jsj511d a2, uint64_t a3, uint64_t a4) {
    struct gm_fr_tag_site_msl07u1 F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t2lqu8jsj511d gm_l2; /* file */
    uint64_t gm_l3; /* line */
    uint64_t gm_l4; /* column */
    gm_b16_8 gm_l6; /* name */
    GM_STACK_CHECK(&F, 403u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_b16_8 gm_l7;
    gm_t934k1j7u70ct gm_l8;
    gm_tabb710d9mq2e gm_l9;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(0));
        gm_l6 = t4;
    } else {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 184);
        uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)&gm_l2));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        gm_f_read_u6r4vns((void *)&gm_l8, t6, t7, t8, ((uint64_t)1ULL));
        uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
        if (t9) {
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
            gm_l7 = t10;
        } else {
            {
                memmove(((uint8_t *)&F.l5), (((uint8_t *)&gm_l8) + 8), 40);
            }
            {
                void * t11 = gm_l0;
                gm_tl874t2atnlh3 t12 = gm_l1;
                gm_t76123gfet73g t13;
                gm_f_mem_trap_ecogko0((void *)&t13, (void *)NULL, cx, NULL, t11, t12, &(*(gm_tpekhfs32op0f *)((uint8_t *)&F.l5)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out))[0] = 1;
                GM_ST(gm_t76123gfet73g, ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 8), t13);
                goto gm_fdone;
            }
        }
        gm_b16_8 t14 = gm_l7;
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
        gm_l6 = t15;
    }
    {
        gm_b16_8 t16 = gm_l6;
        uint64_t t17 = gm_l3;
        uint64_t t18 = gm_l4;
        gm_tgbgti82mf879 t19;
        GM_ST(gm_b16_8, ((uint8_t *)&t19 + 0), t16);
        GM_ST(uint64_t, ((uint8_t *)&t19 + 16), t17);
        GM_ST(uint64_t, ((uint8_t *)&t19 + 24), t18);
        gm_f_format_bv13gdl((void *)&gm_l9, (void *)NULL, cx, NULL, &t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 8, gm_srccp2mbkjs1vn4, 152, 12);
    {
        gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 24, 0);
        ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out))[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_td0ept12e7q8b *)gm_out) + 8), t22);
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

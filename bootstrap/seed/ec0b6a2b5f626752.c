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
typedef struct { void * f0; uint64_t f1; uint64_t f2; uint32_t f3; uint32_t f4; } gm_t2i0cb8fcu1gb;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint64_t a[8]; } gm_tl4e4vqe807ge;
typedef struct { void * f0; uint64_t f1; } gm_ttf6i6v1da8cm;
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; } gm_t0am46iohlpol;
_Static_assert(sizeof(gm_t2i0cb8fcu1gb) == 32 && _Alignof(gm_t2i0cb8fcu1gb) == 8 && offsetof(gm_t2i0cb8fcu1gb, f0) == 0 && offsetof(gm_t2i0cb8fcu1gb, f1) == 8 && offsetof(gm_t2i0cb8fcu1gb, f2) == 16 && offsetof(gm_t2i0cb8fcu1gb, f3) == 24 && offsetof(gm_t2i0cb8fcu1gb, f4) == 28, "layout of `Page`");
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint64_t f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_t617vr72bd7b3;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_ttf6i6v1da8cm f0; uint8_t f1; uint8_t pe[7]; } gm_taordiq61jj46;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { void * f0; uint64_t f1; uint8_t f2; uint8_t f3; uint8_t f4; gm_t9qkh0rsooc8d f5; uint8_t pe[4]; } gm_tbls6nq5r2dk1;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { uint64_t f0; uint64_t f1; gm_t9qkh0rsooc8d f2; uint8_t f3; uint8_t pe[6]; } gm_tilak255t5m34;
_Static_assert(sizeof(gm_tl4e4vqe807ge) == 64 && _Alignof(gm_tl4e4vqe807ge) == 8 && offsetof(gm_tl4e4vqe807ge, a) == 0, "layout of `[8]usize`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttf6i6v1da8cm) == 16 && _Alignof(gm_ttf6i6v1da8cm) == 8 && offsetof(gm_ttf6i6v1da8cm, f0) == 0 && offsetof(gm_ttf6i6v1da8cm, f1) == 8, "layout of `(slot: ^Slot, base: u64)`");
_Static_assert(sizeof(gm_t0am46iohlpol) == 2 && _Alignof(gm_t0am46iohlpol) == 1 && offsetof(gm_t0am46iohlpol, f0) == 0 && offsetof(gm_t0am46iohlpol, f1) == 1, "layout of `(Command, Command)`");
typedef struct { gm_trqfvmjtt4sij f0; uint64_t f1; uint64_t f2; gm_tl4e4vqe807ge f3; gm_trqfvmjtt4sij f4; uint64_t f5; uint64_t f6; } gm_t4m55nu62fs91;
_Static_assert(sizeof(gm_t617vr72bd7b3) == 16 && _Alignof(gm_t617vr72bd7b3) == 8 && offsetof(gm_t617vr72bd7b3, f0) == 0 && offsetof(gm_t617vr72bd7b3, f1) == 8, "layout of `DirEntry`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_taordiq61jj46) == 24 && _Alignof(gm_taordiq61jj46) == 8 && offsetof(gm_taordiq61jj46, f0) == 0 && offsetof(gm_taordiq61jj46, f1) == 16, "layout of `?(slot: ^Slot, base: u64)`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbls6nq5r2dk1) == 24 && _Alignof(gm_tbls6nq5r2dk1) == 8 && offsetof(gm_tbls6nq5r2dk1, f0) == 0 && offsetof(gm_tbls6nq5r2dk1, f1) == 8 && offsetof(gm_tbls6nq5r2dk1, f2) == 16 && offsetof(gm_tbls6nq5r2dk1, f3) == 17 && offsetof(gm_tbls6nq5r2dk1, f4) == 18 && offsetof(gm_tbls6nq5r2dk1, f5) == 19, "layout of `Slot`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tilak255t5m34) == 24 && _Alignof(gm_tilak255t5m34) == 8 && offsetof(gm_tilak255t5m34, f0) == 0 && offsetof(gm_tilak255t5m34, f1) == 8 && offsetof(gm_tilak255t5m34, f2) == 16 && offsetof(gm_tilak255t5m34, f3) == 17, "layout of `(base: u64, size: u64, kind: AllocKind, live: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t2lqu8jsj511d v0; gm_t617vr72bd7b3 v1; gm_t66j1im9ip128 v2; gm_t2lqu8jsj511d v3; uint64_t v4; gm_t617vr72bd7b3 v5; } f1; } gm_tpekhfs32op0f;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tilak255t5m34 f0; uint8_t f1; uint8_t pe[7]; } gm_tqvpud5umqqp1;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; uint64_t v1; } f1; } gm_t3j6egd6cfnk5;
_Static_assert(sizeof(gm_t4m55nu62fs91) == 176 && _Alignof(gm_t4m55nu62fs91) == 8 && offsetof(gm_t4m55nu62fs91, f0) == 0 && offsetof(gm_t4m55nu62fs91, f1) == 40 && offsetof(gm_t4m55nu62fs91, f2) == 48 && offsetof(gm_t4m55nu62fs91, f3) == 56 && offsetof(gm_t4m55nu62fs91, f4) == 120 && offsetof(gm_t4m55nu62fs91, f5) == 160 && offsetof(gm_t4m55nu62fs91, f6) == 168, "layout of `Memory`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; gm_b16_8 v1; } f1; } gm_t934k1j7u70ct;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tpekhfs32op0f v0; } f1; } gm_tgfdnufetdnfc;
_Static_assert(sizeof(gm_tpekhfs32op0f) == 40 && _Alignof(gm_tpekhfs32op0f) == 8 && offsetof(gm_tpekhfs32op0f, f0) == 0 && offsetof(gm_tpekhfs32op0f, f1) == 8, "layout of `MemError`");
_Static_assert(sizeof(gm_tqvpud5umqqp1) == 32 && _Alignof(gm_tqvpud5umqqp1) == 8 && offsetof(gm_tqvpud5umqqp1, f0) == 0 && offsetof(gm_tqvpud5umqqp1, f1) == 24, "layout of `?(base: u64, size: u64, kind: AllocKind, live: bool)`");
_Static_assert(sizeof(gm_t3j6egd6cfnk5) == 48 && _Alignof(gm_t3j6egd6cfnk5) == 8 && offsetof(gm_t3j6egd6cfnk5, f0) == 0 && offsetof(gm_t3j6egd6cfnk5, f1) == 8, "layout of `MemError!u64`");
_Static_assert(sizeof(gm_t934k1j7u70ct) == 48 && _Alignof(gm_t934k1j7u70ct) == 8 && offsetof(gm_t934k1j7u70ct, f0) == 0 && offsetof(gm_t934k1j7u70ct, f1) == 8, "layout of `MemError![]u8`");
_Static_assert(sizeof(gm_tgfdnufetdnfc) == 48 && _Alignof(gm_tgfdnufetdnfc) == 8 && offsetof(gm_tgfdnufetdnfc, f0) == 0 && offsetof(gm_tgfdnufetdnfc, f1) == 8, "layout of `MemError!()`");
struct gm_fr_aligned_q6sor4h {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_alloc_bfo8187 {
    gm_frame h;
    gm_t3j6egd6cfnk5 ret;
};
struct gm_fr_copy_from_f7j6p05 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_d5p9g61 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_deinit_u4v62un {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_describe_t62jhnt {
    gm_frame h;
    gm_tqvpud5umqqp1 ret;
};
struct gm_fr_drop_oldest_o9uaetp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_m3oe7t3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fault_u31b92l {
    gm_frame h;
    gm_tpekhfs32op0f ret;
};
struct gm_fr_format_173gk69 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_bovj9ap {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_hvaqp8b {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_itetl3f {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_n0787fu {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_o05ssh3 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_q07lmui {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_init_9fuf99v {
    gm_frame h;
    gm_tgfdnufetdnfc ret;
};
struct gm_fr_kill_3kcavfd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_list_5dhgg4i {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_bcj49nd {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_locate_ciotffk {
    gm_frame h;
    gm_taordiq61jj46 ret;
};
struct gm_fr_mem_deinit_0088958 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_mem_message_e0go901 {
    gm_frame h;
    gm_tpekhfs32op0f l0; /* e */
    gm_b16_8 ret;
};
struct gm_fr_memory_dqom95e {
    gm_frame h;
    gm_t4m55nu62fs91 ret;
};
struct gm_fr_next_pages_p5tqfl1 {
    gm_frame h;
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_page_of_artfs32 {
    gm_frame h;
    void * ret;
};
struct gm_fr_push_20a7t0m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_read_u6r4vns {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
struct gm_fr_regrow_568t7p1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_release_page_66inn9t {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_truncate_ucmmehv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_view_qalkbv1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_write_5gub0ul {
    gm_frame h;
    gm_tgfdnufetdnfc ret;
};
struct gm_fr_write_view_pebit6h {
    gm_frame h;
    gm_t934k1j7u70ct ret;
};
inline bool gm_eqv4kh4qul78(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_alloc_bfo8187(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3, uint8_t a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_copy_from_f7j6p05(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_deinit_d5p9g61(void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_u4v62un(void * a0);
__attribute__((aligned(4))) void gm_f_describe_t62jhnt(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tqvpud5umqqp1 gm_f_describe_t62jhnt_c(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_destroy_l0inh8t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_drop_oldest_o9uaetp(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_m3oe7t3(void);
__attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
__attribute__((aligned(4))) gm_tpekhfs32op0f gm_f_fault_u31b92l_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4);
inline __attribute__((aligned(4))) void gm_f_format_173gk69(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_format_bovj9ap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_bqgl4hq(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_hvaqp8b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0);
__attribute__((aligned(4))) void gm_f_format_ig8f8pl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_itetl3f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_n0787fu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_n3u30k9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_format_nio7lfn(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_o05ssh3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) void gm_f_format_q07lmui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_oobi9fu(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_free_rkuh2rm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_init_9fuf99v(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tgfdnufetdnfc gm_f_init_9fuf99v_c(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_kill_3kcavfd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_list_5dhgg4i(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_bcj49nd(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_locate_ciotffk(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_taordiq61jj46 gm_f_locate_ciotffk_c(void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_make_fill_0p8dma0(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_make_fill_jpvm0kc(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint8_t a1, const void * a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mem_deinit_0088958(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_memory_dqom95e(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_skgqa77(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_pages_p5tqfl1(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_page_of_artfs32(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_20a7t0m(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_t934k1j7u70ct gm_f_read_u6r4vns_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_568t7p1(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) uint64_t gm_f_release_page_66inn9t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_ucmmehv(void * a0, uint64_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
__attribute__((aligned(4))) void gm_f_write_5gub0ul(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) gm_tgfdnufetdnfc gm_f_write_5gub0ul_c(void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_t934k1j7u70ct gm_f_write_view_pebit6h_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
extern uint8_t gm_c0n6vj9dmoi[8] __attribute__((aligned(8)));
extern uint8_t gm_c2ftoibr66a[8] __attribute__((aligned(8)));
extern uint8_t gm_c2t30gdg5i0[8] __attribute__((aligned(8)));
extern uint8_t gm_c6p4chcutca[8] __attribute__((aligned(8)));
extern uint8_t gm_ckasdtpni1c[8] __attribute__((aligned(8)));
extern uint8_t gm_cmml7o4if0q[8] __attribute__((aligned(8)));
extern uint8_t gm_k0ddgrnig17[40] __attribute__((aligned(8)));
extern uint8_t gm_k2afmplo64l[40] __attribute__((aligned(8)));
extern uint8_t gm_k3ho3jie8mm[40] __attribute__((aligned(8)));
extern uint8_t gm_ke9p7o91039[40] __attribute__((aligned(8)));
extern uint8_t gm_keudd4gct2b[40] __attribute__((aligned(8)));
extern uint8_t gm_kgubkej3sov[40] __attribute__((aligned(8)));
extern uint8_t gm_kib6nfi69s7[40] __attribute__((aligned(8)));
extern uint8_t gm_kjq4sbqctak[40] __attribute__((aligned(8)));
extern uint8_t gm_km03kohh9a4[40] __attribute__((aligned(8)));
extern uint8_t gm_koniosmdv9m[40] __attribute__((aligned(8)));
extern uint8_t gm_kpmakm2t9q3[40] __attribute__((aligned(8)));
extern uint8_t gm_krsnqpdcjf4[40] __attribute__((aligned(8)));
extern uint8_t gm_ks0d3nb0eoq[40] __attribute__((aligned(8)));
extern const uint8_t gm_s2reoulhu3o4k[49];
extern const uint8_t gm_s3a91i4ku76eo[81];
extern const uint8_t gm_sacs352aeakr8[18];
extern const uint8_t gm_sagd3osht35og[13];
extern const uint8_t gm_sddniufplfkku[66];
extern const uint8_t gm_sdgqla1nhnp8n[34];
extern const uint8_t gm_sdvkaj73n9goh[24];
extern const uint8_t gm_sisd1n433forg[52];
extern const uint8_t gm_skt7g4v99o4j0[42];
extern const uint8_t gm_sqofr58g480ri[39];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcq7ohh8h77bn7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_sroh2go4g3kcu[107];
extern const uint8_t gm_suklu1i9clb5l[48];
extern const uint8_t gm_svr1ohtrtj17p[64];
extern const gm_tmpl gm_tpl13c887f2o944;
extern const gm_tmpl gm_tpl2a8tmpvk11vt;
extern const gm_tmpl gm_tpl6m8ee83kb2vt;
extern const gm_tmpl gm_tplb2gsdj8iioqj;
extern const gm_tmpl gm_tpleiji4s5drk3o;
extern const gm_tmpl gm_tplmr0a02ecbaom;
extern const gm_tmpl gm_tplut5h6m1atjoq;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eqv4kh4qul78(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) void gm_f_deinit_d5p9g61(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_d5p9g61 F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    if (t4) {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, ((((uint8_t *)t5) + 24) + 8));
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 16));
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 24));
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, ((uint8_t *)t10));
        void * t12 = t11;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)8ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)8ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_73v4ttb();
    GM_ST(gm_b16_8, ((uint8_t *)t20), t21);
    void * t22 = gm_l0;
    GM_ST(uint64_t, (((uint8_t *)t22) + 16), ((uint64_t)0ULL));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_u4v62un(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_u4v62un F;
    void * gm_l0; /* l */
    GM_STACK_CHECK(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 16));
    uint8_t t4 = (t3 > ((uint64_t)0ULL));
    if (t4) {
        void * t5 = gm_l0;
        void * t6 = GM_LD(void *, ((((uint8_t *)t5) + 24) + 8));
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 16));
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 24));
        void * t10 = gm_l0;
        void * t11 = GM_LD(void *, ((uint8_t *)t10));
        void * t12 = t11;
        void * t13 = gm_l0;
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t13) + 16));
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)32ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)8ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_m3oe7t3();
    GM_ST(gm_b16_8, ((uint8_t *)t20), t21);
    void * t22 = gm_l0;
    GM_ST(uint64_t, (((uint8_t *)t22) + 16), ((uint64_t)0ULL));
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_m3oe7t3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_m3oe7t3 F;
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
inline __attribute__((aligned(4))) void gm_f_format_173gk69(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_173gk69 F;
    gm_t66j1im9ip128 gm_l0; /* args */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sroh2go4g3kcu));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(106));
    gm_t66j1im9ip128 t3 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)a0));
    gm_f_format_nio7lfn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpleiji4s5drk3o), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_bovj9ap(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_bovj9ap F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s3a91i4ku76eo));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(80));
    gm_f_format_ig8f8pl((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplb2gsdj8iioqj), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_hvaqp8b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t2lqu8jsj511d a0) {
    struct gm_fr_format_hvaqp8b F;
    gm_t2lqu8jsj511d gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t2lqu8jsj511d t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svr1ohtrtj17p));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(63));
    gm_f_format_ig8f8pl((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl6m8ee83kb2vt), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_itetl3f(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_itetl3f F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2reoulhu3o4k));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(48));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplut5h6m1atjoq), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_n0787fu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_n0787fu F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sddniufplfkku));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(65));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplmr0a02ecbaom), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_o05ssh3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_o05ssh3 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdgqla1nhnp8n));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
    gm_f_format_n3u30k9((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tpl13c887f2o944), t3);
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
inline __attribute__((aligned(4))) void gm_f_format_q07lmui(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_q07lmui F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    GM_STACK_CHECK(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sisd1n433forg));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(51));
    gm_tf1bjg81u0glv t3 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
    gm_f_format_bqgl4hq((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tpl2a8tmpvk11vt), t2);
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
inline __attribute__((aligned(4))) void gm_f_list_bcj49nd(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_bcj49nd F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_m3oe7t3();
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void * gm_f_page_of_artfs32(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_page_of_artfs32 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 161u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    void * t3 = gm_l0;
    uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)t3) + 40));
    uint64_t r5; bool o6 = __builtin_sub_overflow(t2, t4, &r5);
    uint64_t t7 = r5;
    uint64_t t8 = GM_LD(uint64_t, ((uint8_t *)gm_c6p4chcutca));
    unsigned __int128 n9 = (unsigned __int128)t8;
    if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 178, 10);
    uint64_t t10 = (uint64_t)(t7 >> (int)n9);
    gm_l2 = t10;
    uint64_t t11 = gm_l2;
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
    uint64_t t18 = gm_l2;
    uint64_t t19 = gm_f_us_cuv01fp(t18);
    void * t20 = (void *)(((uint8_t *)t17) + (uintptr_t)t19 * 32);
    F.ret = t20;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_20a7t0m(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_20a7t0m F;
    void * gm_l0; /* l */
    gm_t2i0cb8fcu1gb gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_m3oe7t3();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_568t7p1(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_568t7p1(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_568t7p1 F;
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
inline __attribute__((aligned(4))) void gm_f_truncate_ucmmehv(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_ucmmehv F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 8)));
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
extern __attribute__((aligned(4))) uint8_t gm_f_aligned_q6sor4h(uint64_t a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_alloc_bfo8187(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, uint64_t a2, gm_t9qkh0rsooc8d a3, uint8_t a4) {
    struct gm_fr_alloc_bfo8187 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* size */
    uint64_t gm_l2; /* align */
    gm_t9qkh0rsooc8d gm_l3; /* kind */
    uint8_t gm_l4; /* mutable */
    gm_b16_8 gm_l5; /* data */
    gm_tbls6nq5r2dk1 gm_l6; /* slot */
    uint64_t gm_l7; /* need */
    uint64_t gm_l8; /* shift */
    uint64_t gm_l9; /* c */
    uint32_t gm_l10; /* per */
    uint64_t gm_l11; /* addr */
    gm_b16_8 gm_l12; /* slots */
    void * gm_l13; /* pg */
    uint32_t gm_l14; /* i */
    void * gm_l15; /* ss */
    uint64_t gm_l16; /* n */
    uint64_t gm_l17; /* addr */
    void * gm_l18; /* one */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    GM_STACK_CHECK(&F, 2053u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_b16_8 gm_l21;
    gm_tabb710d9mq2e gm_l22;
    uint8_t gm_l23;
    gm_tf5srh4mp4b3i gm_l24;
    gm_tabb710d9mq2e gm_l25;
    gm_tftb1odc739t5 gm_l26;
    uint64_t gm_l27;
    gm_tf5srh4mp4b3i gm_l28;
    gm_tftb1odc739t5 gm_l29;
    gm_tf5srh4mp4b3i gm_l30;
    gm_tq7s98ihonkju gm_l31;
    gm_tftb1odc739t5 gm_l32;
    {
        uint64_t t15 = gm_l1;
        unsigned __int128 n16 = (unsigned __int128)((uint64_t)1ULL);
        if (n16 >= 64) gm_trap_shift((uint64_t)n16, "usize", gm_srcq7ohh8h77bn7, 125, 19);
        uint64_t t17 = (uint64_t)(((uint64_t)18446744073709551615ULL) >> (int)n16);
        uint64_t t18 = (uint64_t)t17;
        uint8_t t19 = (t15 > t18);
        if (t19) {
            uint64_t t20 = gm_l1;
            gm_t9qkh0rsooc8d t21 = gm_l3;
            gm_t617vr72bd7b3 t22;
            GM_ST(uint8_t, (uint8_t *)&t22 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t22 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t22 + 12, 0);
            GM_ST(uint64_t, ((uint8_t *)&t22 + 0), t20);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 8), t21);
            gm_tpekhfs32op0f t23;
            GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t23 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t23 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)7ULL));
            GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&t23 + 8), t22);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 4, 0);
            ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 1;
            GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8), t23);
            goto gm_fdone;
        }
        gm_b16_8 t24 = gm_f_empty_slice_ijtp9lo();
        gm_l5 = t24;
        uint64_t t25 = gm_l1;
        uint8_t t26 = (t25 > ((uint64_t)0ULL));
        if (t26) {
            {
                uint64_t t27 = gm_l1;
                uint64_t t28 = gm_f_us_cuv01fp(t27);
                gm_f_make_fill_jpvm0kc((void *)&gm_l22, (void *)NULL, cx, NULL, t28, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kib6nfi69s7)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t29 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
            if (t29) {
                gm_b16_8 t30 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l22) + 8));
                gm_l21 = t30;
            } else {
                uint64_t t31 = gm_l1;
                gm_t9qkh0rsooc8d t32 = gm_l3;
                gm_t617vr72bd7b3 t33;
                GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t33 + 0), t31);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
                gm_tpekhfs32op0f t34;
                GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t34 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t34 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t34 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t34 + 32, 0);
                GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)7ULL));
                GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&t34 + 8), t33);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 1;
                GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8), t34);
                goto gm_fdone;
            }
            gm_b16_8 t35 = gm_l21;
            gm_l5 = t35;
        }
        void * t36 = GM_LD(void *, ((uint8_t *)&gm_l5));
        uint64_t t37 = gm_l1;
        uint8_t t38 = gm_l4;
        gm_t9qkh0rsooc8d t39 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&gm_l6 + 20, 0);
        GM_ST(void *, ((uint8_t *)&gm_l6 + 0), t36);
        GM_ST(uint64_t, ((uint8_t *)&gm_l6 + 8), t37);
        GM_ST(uint8_t, ((uint8_t *)&gm_l6 + 16), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&gm_l6 + 17), ((uint8_t)1));
        GM_ST(uint8_t, ((uint8_t *)&gm_l6 + 18), t38);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&gm_l6 + 19), t39);
        uint64_t t40 = gm_l1;
        uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
        if (o42) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 130, 14);
        uint64_t t43 = r41;
        uint64_t t44 = gm_l2;
        uint64_t t45 = (t43 > t44 ? t43 : t44);
        gm_l7 = t45;
        uint64_t t46 = gm_l7;
        uint64_t t47 = GM_LD(uint64_t, ((uint8_t *)gm_c0n6vj9dmoi));
        unsigned __int128 n48 = (unsigned __int128)t47;
        if (n48 >= 64) gm_trap_shift((uint64_t)n48, "u64", gm_srcq7ohh8h77bn7, 131, 17);
        uint64_t r49 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n48);
        bool o50 = (uint64_t)(r49 >> (int)n48) != ((uint64_t)1ULL);
        if (o50) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 131, 17);
        uint64_t t51 = r49;
        uint8_t t52 = (t46 <= t51);
        if (t52) {
            uint64_t t53 = GM_LD(uint64_t, ((uint8_t *)gm_cmml7o4if0q));
            gm_l8 = t53;
            {
                for (;;) {
                    uint64_t t54 = gm_l8;
                    unsigned __int128 n55 = (unsigned __int128)t54;
                    if (n55 >= 64) gm_trap_shift((uint64_t)n55, "u64", gm_srcq7ohh8h77bn7, 133, 16);
                    uint64_t r56 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n55);
                    bool o57 = (uint64_t)(r56 >> (int)n55) != ((uint64_t)1ULL);
                    if (o57) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 133, 16);
                    uint64_t t58 = r56;
                    uint64_t t59 = gm_l7;
                    uint8_t t60 = (t58 < t59);
                    if (!t60) {
                        goto gm_lb1;
                    }
                    {
                        uint64_t t61 = gm_l8;
                        uint64_t r62; bool o63 = __builtin_add_overflow(t61, ((uint64_t)1ULL), &r62);
                        if (o63) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 133, 42);
                        uint64_t t64 = r62;
                        gm_l8 = t64;
                    }
gm_lb3:;
                }
            }
gm_lb1:;
            uint64_t t65 = gm_l8;
            uint64_t t66 = GM_LD(uint64_t, ((uint8_t *)gm_cmml7o4if0q));
            uint64_t r67; bool o68 = __builtin_sub_overflow(t65, t66, &r67);
            if (o68) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 134, 17);
            uint64_t t69 = r67;
            uint64_t t70 = gm_f_us_cuv01fp(t69);
            gm_l9 = t70;
            uint64_t t71 = GM_LD(uint64_t, ((uint8_t *)gm_ckasdtpni1c));
            uint64_t t72 = gm_l8;
            unsigned __int128 n73 = (unsigned __int128)t72;
            if (n73 >= 64) gm_trap_shift((uint64_t)n73, "u64", gm_srcq7ohh8h77bn7, 135, 17);
            uint64_t t74 = (uint64_t)(t71 >> (int)n73);
            uint32_t t75 = (uint32_t)t74;
            gm_l10 = t75;
            void * t76 = gm_l0;
            uint64_t t77 = gm_l9;
            if ((uintptr_t)t77 >= (uintptr_t)(((uint64_t)8ULL))) gm_trap_index((uintptr_t)t77, ((uint64_t)8ULL), gm_srcq7ohh8h77bn7, 136, 12);
            uint64_t t78 = GM_LD(uint64_t, ((((uint8_t *)t76) + 56) + (uintptr_t)t77 * 8));
            uint8_t t79 = ((t78 == ((uint64_t)0ULL)));
            if (t79) {
                gm_l23 = ((uint8_t)1);
            } else {
                void * t80 = gm_l0;
                void * t81 = GM_LD(void *, ((uint8_t *)t80));
                uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)t80) + 8));
                void * t83 = gm_l0;
                uint64_t t84 = gm_l9;
                if ((uintptr_t)t84 >= (uintptr_t)(((uint64_t)8ULL))) gm_trap_index((uintptr_t)t84, ((uint64_t)8ULL), gm_srcq7ohh8h77bn7, 136, 44);
                uint64_t t85 = GM_LD(uint64_t, ((((uint8_t *)t83) + 56) + (uintptr_t)t84 * 8));
                uint64_t r86; bool o87 = __builtin_sub_overflow(t85, ((uint64_t)1ULL), &r86);
                if (o87) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 136, 44);
                uint64_t t88 = r86;
                if ((uintptr_t)t88 >= (uintptr_t)(t82)) gm_trap_index((uintptr_t)t88, t82, gm_srcq7ohh8h77bn7, 136, 30);
                uint32_t t89 = GM_LD(uint32_t, ((((uint8_t *)t81) + (uintptr_t)t88 * 32) + 24));
                uint32_t t90 = gm_l10;
                uint8_t t91 = ((t89 == t90));
                gm_l23 = t91;
            }
            uint8_t t92 = gm_l23;
            if (t92) {
                void * t93 = gm_l0;
                gm_tf5srh4mp4b3i t94 = gm_f_next_pages_p5tqfl1(t93, ((uint64_t)1ULL));
                gm_l24 = t94;
                uint8_t t95 = (uint8_t)((((uint8_t *)&gm_l24))[8] != 0);
                if (t95) {
                    uint64_t t96 = GM_LD(uint64_t, ((uint8_t *)&gm_l24));
                    gm_l11 = t96;
                } else {
                    {
                        gm_b16_8 t97 = gm_l5;
                        gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
                        gm_b0_1 t99 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t98, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_koniosmdv9m)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_tpekhfs32op0f t100;
                        memset(&t100, 0, sizeof t100);
                        GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)6ULL));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 1;
                        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8), t100);
                        goto gm_fdone;
                    }
                }
                {
                    uint32_t t101 = gm_l10;
                    uint64_t t102 = (uint64_t)t101;
                    void * t103 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    gm_t9qkh0rsooc8d t104;
                    GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)2ULL));
                    gm_tbls6nq5r2dk1 t105;
                    GM_ST(uint32_t, (uint8_t *)&t105 + 20, 0);
                    GM_ST(void *, ((uint8_t *)&t105 + 0), t103);
                    GM_ST(uint64_t, ((uint8_t *)&t105 + 8), ((uint64_t)0ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t105 + 16), ((uint8_t)0));
                    GM_ST(uint8_t, ((uint8_t *)&t105 + 17), ((uint8_t)0));
                    GM_ST(uint8_t, ((uint8_t *)&t105 + 18), ((uint8_t)0));
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t105 + 19), t104);
                    gm_f_make_fill_0p8dma0((void *)&gm_l25, (void *)NULL, cx, NULL, t102, &t105, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjq4sbqctak)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint8_t t106 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                if (t106) {
                    gm_b16_8 t107 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l25) + 8));
                    gm_l12 = t107;
                } else {
                    {
                        gm_b16_8 t108 = gm_l5;
                        gm_b16_8 t109 = GM_LD(gm_b16_8, ((uint8_t *)&t108));
                        gm_b0_1 t110 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t109, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kpmakm2t9q3)));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        uint64_t t111 = gm_l1;
                        gm_t9qkh0rsooc8d t112 = gm_l3;
                        gm_t617vr72bd7b3 t113;
                        GM_ST(uint8_t, (uint8_t *)&t113 + 9, 0);
                        GM_ST(uint16_t, (uint8_t *)&t113 + 10, 0);
                        GM_ST(uint32_t, (uint8_t *)&t113 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t113 + 0), t111);
                        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t113 + 8), t112);
                        gm_tpekhfs32op0f t114;
                        GM_ST(uint8_t, (uint8_t *)&t114 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t114 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t114 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t114 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t114 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t114 + 0), ((uint8_t)7ULL));
                        GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&t114 + 8), t113);
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 4, 0);
                        ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 1;
                        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8), t114);
                        goto gm_fdone;
                    }
                }
                {
                    void * t115 = gm_l0;
                    void * t116 = (void *)((uint8_t *)t115);
                    void * t117 = GM_LD(void *, ((uint8_t *)&gm_l12));
                    uint64_t t118 = gm_l11;
                    uint64_t t119 = gm_l8;
                    gm_t2i0cb8fcu1gb t120;
                    GM_ST(void *, ((uint8_t *)&t120 + 0), t117);
                    GM_ST(uint64_t, ((uint8_t *)&t120 + 8), t118);
                    GM_ST(uint64_t, ((uint8_t *)&t120 + 16), t119);
                    GM_ST(uint32_t, ((uint8_t *)&t120 + 24), ((uint32_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t120 + 28), ((uint32_t)0ULL));
                    gm_tftb1odc739t5 t121 = gm_f_push_20a7t0m(t116, &t120);
                    gm_l26 = t121;
                }
                uint8_t t122 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                if (!t122) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcq7ohh8h77bn7, 145, 13);
                void * t123 = gm_l0;
                uint64_t t124 = gm_l9;
                if ((uintptr_t)t124 >= (uintptr_t)(((uint64_t)8ULL))) gm_trap_index((uintptr_t)t124, ((uint64_t)8ULL), gm_srcq7ohh8h77bn7, 146, 13);
                void * t125 = gm_l0;
                uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)t125) + 8));
                GM_ST(uint64_t, ((((uint8_t *)t123) + 56) + (uintptr_t)t124 * 8), t126);
            }
            void * t127 = gm_l0;
            void * t128 = GM_LD(void *, ((uint8_t *)t127));
            uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)t127) + 8));
            void * t130 = gm_l0;
            uint64_t t131 = gm_l9;
            if ((uintptr_t)t131 >= (uintptr_t)(((uint64_t)8ULL))) gm_trap_index((uintptr_t)t131, ((uint64_t)8ULL), gm_srcq7ohh8h77bn7, 148, 30);
            uint64_t t132 = GM_LD(uint64_t, ((((uint8_t *)t130) + 56) + (uintptr_t)t131 * 8));
            uint64_t r133; bool o134 = __builtin_sub_overflow(t132, ((uint64_t)1ULL), &r133);
            if (o134) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 148, 30);
            uint64_t t135 = r133;
            if ((uintptr_t)t135 >= (uintptr_t)(t129)) gm_trap_index((uintptr_t)t135, t129, gm_srcq7ohh8h77bn7, 148, 16);
            void * t136 = (void *)(((uint8_t *)t128) + (uintptr_t)t135 * 32);
            gm_l13 = t136;
            void * t137 = gm_l13;
            uint32_t t138 = GM_LD(uint32_t, (((uint8_t *)t137) + 24));
            gm_l14 = t138;
            void * t139 = gm_l13;
            void * t140 = gm_l13;
            uint32_t t141 = GM_LD(uint32_t, (((uint8_t *)t140) + 24));
            uint32_t r142; bool o143 = __builtin_add_overflow(t141, ((uint32_t)1ULL), &r142);
            if (o143) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 150, 9);
            uint32_t t144 = r142;
            GM_ST(uint32_t, (((uint8_t *)t139) + 24), t144);
            {
                {
                    void * t145 = gm_l13;
                    uint8_t t146 = (uint8_t)(GM_LD(void *, ((uint8_t *)t145)) != 0);
                    if (!t146) {
                        goto gm_lb5;
                    }
                    void * t147 = GM_LD(void *, ((uint8_t *)t145));
                    gm_l15 = t147;
                    void * t148 = gm_l15;
                    uint32_t t149 = gm_l14;
                    uint64_t t150 = (uint64_t)t149;
                    memmove((((uint8_t *)t148) + (uintptr_t)t150 * 24), ((uint8_t *)&gm_l6), 24);
                    goto gm_lb4;
                }
gm_lb5:;
            }
gm_lb4:;
            void * t151 = gm_l13;
            uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)t151) + 8));
            uint32_t t153 = gm_l14;
            uint64_t t154 = (uint64_t)t153;
            uint64_t t155 = gm_l8;
            unsigned __int128 n156 = (unsigned __int128)t155;
            if (n156 >= 64) gm_trap_shift((uint64_t)n156, "u64", gm_srcq7ohh8h77bn7, 152, 27);
            uint64_t r157 = (uint64_t)((uint64_t)t154 << (int)n156);
            bool o158 = (uint64_t)(r157 >> (int)n156) != t154;
            if (o158) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 152, 27);
            uint64_t t159 = r157;
            uint64_t r160; bool o161 = __builtin_add_overflow(t152, t159, &r160);
            if (o161) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 152, 16);
            uint64_t t162 = r160;
            memset(&(*(gm_t3j6egd6cfnk5 *)gm_out), 0, sizeof (*(gm_t3j6egd6cfnk5 *)gm_out));
            ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 0;
            GM_ST(uint64_t, ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8), t162);
            goto gm_fdone;
        }
    }
    {
        {
            for (;;) {
                void * t163 = gm_l0;
                uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)t163) + 40));
                void * t165 = gm_l0;
                uint64_t t166 = GM_LD(uint64_t, (((uint8_t *)t165) + 8));
                uint64_t t167 = (uint64_t)t166;
                uint64_t t168 = GM_LD(uint64_t, ((uint8_t *)gm_ckasdtpni1c));
                uint64_t r169; bool o170 = __builtin_mul_overflow(t167, t168, &r169);
                if (o170) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 155, 21);
                uint64_t t171 = r169;
                uint64_t r172; bool o173 = __builtin_add_overflow(t164, t171, &r172);
                if (o173) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 155, 12);
                uint64_t t174 = r172;
                uint64_t t175 = gm_l2;
                uint64_t t176 = (t175 > ((uint64_t)1ULL) ? t175 : ((uint64_t)1ULL));
                if (t176 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 155, 11);
                uint64_t t177 = (uint64_t)(t174 % t176);
                uint8_t t178 = (!(t177 == ((uint64_t)0ULL)));
                if (!t178) {
                    goto gm_lb6;
                }
                {
                    void * t179 = gm_l0;
                    gm_tf5srh4mp4b3i t180 = gm_f_next_pages_p5tqfl1(t179, ((uint64_t)1ULL));
                    gm_l28 = t180;
                    uint8_t t181 = (uint8_t)((((uint8_t *)&gm_l28))[8] != 0);
                    if (t181) {
                        uint64_t t182 = GM_LD(uint64_t, ((uint8_t *)&gm_l28));
                        gm_l27 = t182;
                    } else {
                        {
                            gm_b16_8 t183 = gm_l5;
                            gm_b16_8 t184 = GM_LD(gm_b16_8, ((uint8_t *)&t183));
                            gm_b0_1 t185 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t184, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3ho3jie8mm)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            gm_tpekhfs32op0f t186;
                            memset(&t186, 0, sizeof t186);
                            GM_ST(uint8_t, ((uint8_t *)&t186 + 0), ((uint8_t)6ULL));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 4, 0);
                            ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 1;
                            GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8), t186);
                            goto gm_fdone;
                        }
                    }
                    uint64_t t187 = gm_l27;
                    void * t188 = gm_l0;
                    void * t189 = (void *)((uint8_t *)t188);
                    uint64_t t190 = GM_LD(uint64_t, ((uint8_t *)gm_c2t30gdg5i0));
                    gm_t2i0cb8fcu1gb t191;
                    GM_ST(void *, ((uint8_t *)&t191 + 0), ((void *)0));
                    GM_ST(uint64_t, ((uint8_t *)&t191 + 8), ((uint64_t)0ULL));
                    GM_ST(uint64_t, ((uint8_t *)&t191 + 16), t190);
                    GM_ST(uint32_t, ((uint8_t *)&t191 + 24), ((uint32_t)0ULL));
                    GM_ST(uint32_t, ((uint8_t *)&t191 + 28), ((uint32_t)0ULL));
                    gm_tftb1odc739t5 t192 = gm_f_push_20a7t0m(t189, &t191);
                    gm_l29 = t192;
                    uint8_t t193 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                    if (!t193) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcq7ohh8h77bn7, 160, 9);
                }
gm_lb8:;
            }
        }
gm_lb6:;
        uint64_t t194 = gm_l1;
        uint64_t t195 = GM_LD(uint64_t, ((uint8_t *)gm_ckasdtpni1c));
        if (t195 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 162, 10);
        uint64_t t196 = (uint64_t)(t194 / t195);
        uint64_t r197; bool o198 = __builtin_add_overflow(t196, ((uint64_t)1ULL), &r197);
        if (o198) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 162, 10);
        uint64_t t199 = r197;
        gm_l16 = t199;
        void * t200 = gm_l0;
        uint64_t t201 = gm_l16;
        gm_tf5srh4mp4b3i t202 = gm_f_next_pages_p5tqfl1(t200, t201);
        gm_l30 = t202;
        uint8_t t203 = (uint8_t)((((uint8_t *)&gm_l30))[8] != 0);
        if (t203) {
            uint64_t t204 = GM_LD(uint64_t, ((uint8_t *)&gm_l30));
            gm_l17 = t204;
        } else {
            {
                gm_b16_8 t205 = gm_l5;
                gm_b16_8 t206 = GM_LD(gm_b16_8, ((uint8_t *)&t205));
                gm_b0_1 t207 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t206, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krsnqpdcjf4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_tpekhfs32op0f t208;
                memset(&t208, 0, sizeof t208);
                GM_ST(uint8_t, ((uint8_t *)&t208 + 0), ((uint8_t)6ULL));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 1;
                GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8), t208);
                goto gm_fdone;
            }
        }
    }
    {
        gm_tq7s98ihonkju t209 = gm_f_new_skgqa77((void *)NULL, cx, NULL, &(*(gm_tbls6nq5r2dk1 *)((uint8_t *)&gm_l6)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_keudd4gct2b)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l31 = t209;
        uint8_t t210 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
        if (t210) {
            void * t211 = GM_LD(void *, (((uint8_t *)&gm_l31) + 8));
            gm_l18 = t211;
        } else {
            {
                gm_b16_8 t212 = gm_l5;
                gm_b16_8 t213 = GM_LD(gm_b16_8, ((uint8_t *)&t212));
                gm_b0_1 t214 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t213, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_km03kohh9a4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                uint64_t t215 = gm_l1;
                gm_t9qkh0rsooc8d t216 = gm_l3;
                gm_t617vr72bd7b3 t217;
                GM_ST(uint8_t, (uint8_t *)&t217 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t217 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t217 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t217 + 0), t215);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t217 + 8), t216);
                gm_tpekhfs32op0f t218;
                GM_ST(uint8_t, (uint8_t *)&t218 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t218 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t218 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t218 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t218 + 32, 0);
                GM_ST(uint8_t, ((uint8_t *)&t218 + 0), ((uint8_t)7ULL));
                GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&t218 + 8), t217);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 4, 0);
                ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 1;
                GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8), t218);
                goto gm_fdone;
            }
        }
        gm_l20 = ((uint64_t)0ULL);
        uint64_t t219 = gm_l16;
        uint64_t t220 = gm_f_us_cuv01fp(t219);
        gm_l19 = t220;
        {
            for (;;) {
                uint64_t t221 = gm_l20;
                uint64_t t222 = gm_l19;
                uint8_t t223 = (t221 < t222);
                if (!t223) {
                    goto gm_lb9;
                }
                {
                    {
                        {
                            void * t224 = gm_l0;
                            void * t225 = (void *)((uint8_t *)t224);
                            void * t226 = gm_l18;
                            void * t227 = t226;
                            uint64_t t228 = gm_l17;
                            uint64_t t229 = GM_LD(uint64_t, ((uint8_t *)gm_c2t30gdg5i0));
                            gm_t2i0cb8fcu1gb t230;
                            GM_ST(void *, ((uint8_t *)&t230 + 0), t227);
                            GM_ST(uint64_t, ((uint8_t *)&t230 + 8), t228);
                            GM_ST(uint64_t, ((uint8_t *)&t230 + 16), t229);
                            GM_ST(uint32_t, ((uint8_t *)&t230 + 24), ((uint32_t)1ULL));
                            GM_ST(uint32_t, ((uint8_t *)&t230 + 28), ((uint32_t)0ULL));
                            gm_tftb1odc739t5 t231 = gm_f_push_20a7t0m(t225, &t230);
                            gm_l32 = t231;
                            uint8_t t232 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                            if (!t232) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 1, gm_srcq7ohh8h77bn7, 171, 25);
                            goto gm_lb12;
                        }
gm_lb13:;
                        gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 171, 5);
                    }
gm_lb12:;
                }
gm_lb11:;
                uint64_t t233 = gm_l20;
                uint64_t r234; bool o235 = __builtin_add_overflow(t233, ((uint64_t)1ULL), &r234);
                uint64_t t236 = r234;
                gm_l20 = t236;
            }
        }
gm_lb9:;
        uint64_t t237 = gm_l17;
        memset(&(*(gm_t3j6egd6cfnk5 *)gm_out), 0, sizeof (*(gm_t3j6egd6cfnk5 *)gm_out));
        ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out))[0] = 0;
        GM_ST(uint64_t, ((uint8_t *)&(*(gm_t3j6egd6cfnk5 *)gm_out) + 8), t237);
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
inline __attribute__((aligned(4))) void gm_f_describe_t62jhnt(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_describe_t62jhnt F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    gm_ttf6i6v1da8cm gm_l2; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_taordiq61jj46 gm_l3;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        gm_f_locate_ciotffk((void *)&gm_l3, t2, t3);
    }
    uint8_t t4 = (uint8_t)((((uint8_t *)&gm_l3))[16] != 0);
    if (t4) {
        gm_ttf6i6v1da8cm t5 = GM_LD(gm_ttf6i6v1da8cm, ((uint8_t *)&gm_l3));
        gm_l2 = t5;
    } else {
        memset(&(*(gm_tqvpud5umqqp1 *)gm_out), 0, sizeof (*(gm_tqvpud5umqqp1 *)gm_out));
        goto gm_fdone;
    }
    {
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        void * t7 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
        void * t9 = GM_LD(void *, ((uint8_t *)&gm_l2));
        gm_t9qkh0rsooc8d t10 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t9) + 19));
        void * t11 = GM_LD(void *, ((uint8_t *)&gm_l2));
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t11) + 17));
        gm_tilak255t5m34 t13;
        GM_ST(uint16_t, (uint8_t *)&t13 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 20, 0);
        GM_ST(uint64_t, ((uint8_t *)&t13 + 0), t6);
        GM_ST(uint64_t, ((uint8_t *)&t13 + 8), t8);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 16), t10);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 17), t12);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqvpud5umqqp1 *)gm_out) + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqvpud5umqqp1 *)gm_out) + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqvpud5umqqp1 *)gm_out) + 28, 0);
        GM_ST(gm_tilak255t5m34, ((uint8_t *)&(*(gm_tqvpud5umqqp1 *)gm_out)), t13);
        ((uint8_t *)&(*(gm_tqvpud5umqqp1 *)gm_out))[24] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tqvpud5umqqp1 gm_f_describe_t62jhnt_c(void * a0, uint64_t a1) { gm_tqvpud5umqqp1 gm_r; gm_f_describe_t62jhnt(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) gm_b0_1 gm_f_drop_oldest_o9uaetp(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_drop_oldest_o9uaetp F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* pi */
    gm_b16_8 gm_l2; /* rest */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* k */
    GM_STACK_CHECK(&F, 491u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t9 = gm_l0;
    void * t10 = GM_LD(void *, (((uint8_t *)t9) + 120));
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t9) + 120) + 8));
    void * t12 = gm_l0;
    uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)t12) + 160));
    if ((uintptr_t)t13 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t13, t11, gm_srcq7ohh8h77bn7, 287, 11);
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)t10) + (uintptr_t)t13 * 8));
    gm_l1 = t14;
    void * t15 = gm_l0;
    void * t16 = gm_l0;
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 160));
    uint64_t r18; bool o19 = __builtin_add_overflow(t17, ((uint64_t)1ULL), &r18);
    if (o19) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 288, 5);
    uint64_t t20 = r18;
    GM_ST(uint64_t, (((uint8_t *)t15) + 160), t20);
    {
        {
            void * t21 = gm_l0;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)t21) + 160));
            uint8_t t23 = (t22 > ((uint64_t)1024ULL));
            if (!t23) {
                goto gm_lb2;
            }
            void * t24 = gm_l0;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 160));
            uint64_t r26; bool o27 = __builtin_mul_overflow(t25, ((uint64_t)2ULL), &r26);
            if (o27) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 289, 34);
            uint64_t t28 = r26;
            void * t29 = gm_l0;
            uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t29) + 120) + 8));
            uint8_t t31 = (t28 > t30);
            if (!t31) {
                goto gm_lb2;
            }
            void * t32 = gm_l0;
            void * t33 = GM_LD(void *, (((uint8_t *)t32) + 120));
            uint64_t t34 = GM_LD(uint64_t, ((((uint8_t *)t32) + 120) + 8));
            void * t35 = gm_l0;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)t35) + 160));
            void * t37 = gm_l0;
            uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)t37) + 120) + 8));
            uint64_t t39 = (uintptr_t)t36;
            uint64_t t40 = (uintptr_t)t38;
            if (t39 > t40 || t40 > (uintptr_t)t34) gm_trap_slice(t39, t40, t34, gm_srcq7ohh8h77bn7, 290, 17);
            uint64_t t41 = ((uintptr_t)t38) - (uintptr_t)t36;
            gm_b16_8 t42;
            GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t33 + (uintptr_t)t36 * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(t41));
            gm_l2 = t42;
            gm_l4 = ((uint64_t)0ULL);
            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            gm_l3 = t43;
            {
                for (;;) {
                    uint64_t t44 = gm_l4;
                    uint64_t t45 = gm_l3;
                    uint8_t t46 = (t44 < t45);
                    if (!t46) {
                        goto gm_lb3;
                    }
                    {
                        {
                            {
                                uint64_t t47 = gm_l4;
                                gm_l5 = t47;
                                void * t48 = gm_l0;
                                void * t49 = GM_LD(void *, (((uint8_t *)t48) + 120));
                                uint64_t t50 = GM_LD(uint64_t, ((((uint8_t *)t48) + 120) + 8));
                                uint64_t t51 = gm_l5;
                                if ((uintptr_t)t51 >= (uintptr_t)(t50)) gm_trap_index((uintptr_t)t51, t50, gm_srcq7ohh8h77bn7, 291, 32);
                                void * t52 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                uint64_t t54 = gm_l5;
                                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)t52) + (uintptr_t)t54 * 8));
                                GM_ST(uint64_t, (((uint8_t *)t49) + (uintptr_t)t51 * 8), t55);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 291, 9);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t56 = gm_l4;
                    uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                    uint64_t t59 = r57;
                    gm_l4 = t59;
                }
            }
gm_lb3:;
            void * t60 = gm_l0;
            void * t61 = (void *)(((uint8_t *)t60) + 120);
            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            gm_f_truncate_ucmmehv(t61, t62);
            void * t63 = gm_l0;
            GM_ST(uint64_t, (((uint8_t *)t63) + 160), ((uint64_t)0ULL));
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t64 = gm_l0;
    void * t65 = gm_l0;
    uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)t65) + 168));
    void * t67 = gm_l0;
    uint64_t t68 = gm_l1;
    uint64_t t69 = gm_f_release_page_66inn9t((void *)NULL, cx, NULL, t67, t68);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint64_t r70; bool o71 = __builtin_sub_overflow(t66, t69, &r70);
    if (o71) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 295, 5);
    uint64_t t72 = r70;
    GM_ST(uint64_t, (((uint8_t *)t64) + 168), t72);
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
inline __attribute__((aligned(4))) void gm_f_fault_u31b92l(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fault_u31b92l F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* align */
    uint8_t gm_l4; /* write */
    gm_ttf6i6v1da8cm gm_l5; /* at */
    GM_STACK_CHECK_SHALLOW(&F, 420u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_taordiq61jj46 gm_l6;
    uint64_t t4 = gm_l1;
    uint64_t t5 = gm_l3;
    uint8_t t6 = gm_f_aligned_q6sor4h(t4, t5);
    uint8_t t7 = (uint8_t)!t6;
    if (t7) {
        uint64_t t8 = gm_l1;
        uint64_t t9 = gm_l3;
        gm_t2lqu8jsj511d t10;
        GM_ST(uint64_t, ((uint8_t *)&t10 + 0), t8);
        GM_ST(uint64_t, ((uint8_t *)&t10 + 8), t9);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 0), ((uint8_t)4ULL));
        GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 8), t10);
        goto gm_fdone;
    }
    uint64_t t11 = gm_l1;
    uint8_t t12 = ((t11 == ((uint64_t)0ULL)));
    if (t12) {
        memset(&(*(gm_tpekhfs32op0f *)gm_out), 0, sizeof (*(gm_tpekhfs32op0f *)gm_out));
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 0), ((uint8_t)0ULL));
        goto gm_fdone;
    }
    {
        void * t13 = gm_l0;
        uint64_t t14 = gm_l1;
        gm_f_locate_ciotffk((void *)&gm_l6, t13, t14);
    }
    uint8_t t15 = (uint8_t)((((uint8_t *)&gm_l6))[16] != 0);
    if (t15) {
        gm_ttf6i6v1da8cm t16 = GM_LD(gm_ttf6i6v1da8cm, ((uint8_t *)&gm_l6));
        gm_l5 = t16;
    } else {
        uint64_t t17 = gm_l1;
        uint64_t t18 = gm_l2;
        gm_t2lqu8jsj511d t19;
        GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t17);
        GM_ST(uint64_t, ((uint8_t *)&t19 + 8), t18);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 0), ((uint8_t)1ULL));
        GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 8), t19);
        goto gm_fdone;
    }
    void * t20 = GM_LD(void *, ((uint8_t *)&gm_l5));
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t20) + 17));
    uint8_t t22 = (uint8_t)!t21;
    if (t22) {
        uint64_t t23 = gm_l1;
        void * t24 = GM_LD(void *, ((uint8_t *)&gm_l5));
        gm_t9qkh0rsooc8d t25 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t24) + 19));
        gm_t617vr72bd7b3 t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t26 + 0), t23);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t26 + 8), t25);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 0), ((uint8_t)2ULL));
        GM_ST(gm_t617vr72bd7b3, ((uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 8), t26);
        goto gm_fdone;
    }
    uint64_t t27 = gm_l2;
    void * t28 = GM_LD(void *, ((uint8_t *)&gm_l5));
    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t28) + 8));
    uint64_t t30 = gm_l1;
    uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint64_t r32; bool o33 = __builtin_sub_overflow(t30, t31, &r32);
    if (o33) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 224, 30);
    uint64_t t34 = r32;
    uint64_t r35; bool o36 = __builtin_sub_overflow(t29, t34, &r35);
    if (o36) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 224, 14);
    uint64_t t37 = r35;
    uint8_t t38 = (t27 > t37);
    if (t38) {
        uint64_t t39 = gm_l1;
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
        void * t42 = GM_LD(void *, ((uint8_t *)&gm_l5));
        uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)t42) + 8));
        gm_t66j1im9ip128 t44;
        GM_ST(uint64_t, ((uint8_t *)&t44 + 0), t39);
        GM_ST(uint64_t, ((uint8_t *)&t44 + 8), t40);
        GM_ST(uint64_t, ((uint8_t *)&t44 + 16), t41);
        GM_ST(uint64_t, ((uint8_t *)&t44 + 24), t43);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 0), ((uint8_t)3ULL));
        GM_ST(gm_t66j1im9ip128, ((uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 8), t44);
        goto gm_fdone;
    }
    {
        {
            uint8_t t45 = gm_l4;
            if (!t45) {
                goto gm_lb2;
            }
            void * t46 = GM_LD(void *, ((uint8_t *)&gm_l5));
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)t46) + 18));
            uint8_t t48 = (uint8_t)!t47;
            if (!t48) {
                goto gm_lb2;
            }
            uint64_t t49 = gm_l1;
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 0), ((uint8_t)5ULL));
            GM_ST(uint64_t, ((uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 8), t49);
            goto gm_fdone;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        uint64_t t50 = gm_l1;
        uint64_t t51 = gm_l2;
        gm_t2lqu8jsj511d t52;
        GM_ST(uint64_t, ((uint8_t *)&t52 + 0), t50);
        GM_ST(uint64_t, ((uint8_t *)&t52 + 8), t51);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 32, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 0), ((uint8_t)1ULL));
        GM_ST(gm_t2lqu8jsj511d, ((uint8_t *)&(*(gm_tpekhfs32op0f *)gm_out) + 8), t52);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tpekhfs32op0f gm_f_fault_u31b92l_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3, uint8_t a4) { gm_tpekhfs32op0f gm_r; gm_f_fault_u31b92l(&gm_r, a0, a1, a2, a3, a4); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_init_9fuf99v(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_init_9fuf99v F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    gm_b16_8 gm_l2; /* data */
    void * gm_l3; /* p */
    GM_STACK_CHECK(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * gm_l4;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        memset(&(*(gm_tgfdnufetdnfc *)gm_out), 0, sizeof (*(gm_tgfdnufetdnfc *)gm_out));
        ((uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 8), GM_ZERO(gm_b0_1));
        goto gm_fdone;
    }
    void * t4 = gm_l0;
    uint64_t t5 = gm_l1;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t7 = (uint64_t)t6;
    void * t8 = gm_f_view_qalkbv1(t4, t5, t7, ((uint8_t)0));
    gm_l4 = t8;
    uint8_t t9 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l4)) != 0);
    if (t9) {
        void * t10 = GM_LD(void *, ((uint8_t *)&gm_l4));
        gm_l3 = t10;
    } else {
        void * t11 = gm_l0;
        uint64_t t12 = gm_l1;
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t14 = (uint64_t)t13;
        gm_tpekhfs32op0f t15;
        gm_f_fault_u31b92l((void *)&t15, t11, t12, t14, ((uint64_t)1ULL), ((uint8_t)0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out))[0] = 1;
        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 8), t15);
        goto gm_fdone;
    }
    void * t16 = gm_l3;
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t18 = ((uintptr_t)t17) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(((uint8_t *)t16 + (uintptr_t)((uint64_t)0ULL) * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(t18));
    gm_b16_8 t20 = gm_l2;
    gm_f_copy_from_f7j6p05(t19, t20);
    memset(&(*(gm_tgfdnufetdnfc *)gm_out), 0, sizeof (*(gm_tgfdnufetdnfc *)gm_out));
    ((uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out))[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 8), GM_ZERO(gm_b0_1));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tgfdnufetdnfc gm_f_init_9fuf99v_c(void * a0, uint64_t a1, gm_b16_8 a2) { gm_tgfdnufetdnfc gm_r; gm_f_init_9fuf99v(&gm_r, a0, a1, a2); return gm_r; }
__attribute__((aligned(4))) uint8_t gm_f_kill_3kcavfd(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_kill_3kcavfd F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* base */
    gm_ttf6i6v1da8cm gm_l2; /* at */
    void * gm_l3; /* s */
    uint64_t gm_l4; /* pi */
    void * gm_l5; /* pg */
    uint32_t gm_l6; /* full */
    GM_STACK_CHECK(&F, 565u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_taordiq61jj46 gm_l7;
    uint8_t gm_l8;
    gm_tftb1odc739t5 gm_l9;
    void * t5 = gm_l0;
    uint64_t t6 = gm_l1;
    gm_f_locate_ciotffk((void *)&gm_l7, t5, t6);
    uint8_t t7 = (uint8_t)((((uint8_t *)&gm_l7))[16] != 0);
    if (t7) {
        gm_ttf6i6v1da8cm t8 = GM_LD(gm_ttf6i6v1da8cm, ((uint8_t *)&gm_l7));
        gm_l2 = t8;
    } else {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t9 = GM_LD(void *, ((uint8_t *)&gm_l2));
    gm_l3 = t9;
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t11 = gm_l1;
    uint8_t t12 = (!(t10 == t11));
    if (t12) {
        gm_l8 = ((uint8_t)1);
    } else {
        void * t13 = gm_l3;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)t13) + 17));
        uint8_t t15 = (uint8_t)!t14;
        gm_l8 = t15;
    }
    uint8_t t16 = gm_l8;
    if (t16) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t17 = gm_l3;
    GM_ST(uint8_t, (((uint8_t *)t17) + 17), ((uint8_t)0));
    void * t18 = gm_l3;
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t18) + 8));
    uint8_t t20 = (t19 > ((uint64_t)0ULL));
    if (t20) {
        void * t21 = gm_l3;
        void * t22 = GM_LD(void *, ((uint8_t *)t21));
        void * t23 = gm_l3;
        uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)t23) + 8));
        uint64_t t25 = gm_f_us_cuv01fp(t24);
        uint64_t t26 = ((uintptr_t)t25) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(((uint8_t *)t22 + (uintptr_t)((uint64_t)0ULL) * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(t26));
        gm_b16_8 t28 = GM_LD(gm_b16_8, ((uint8_t *)&t27));
        gm_b0_1 t29 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t28, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ks0d3nb0eoq)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    uint64_t t30 = gm_l1;
    void * t31 = gm_l0;
    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 40));
    uint64_t r33; bool o34 = __builtin_sub_overflow(t30, t32, &r33);
    if (o34) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 272, 15);
    uint64_t t35 = r33;
    uint64_t t36 = GM_LD(uint64_t, ((uint8_t *)gm_c6p4chcutca));
    unsigned __int128 n37 = (unsigned __int128)t36;
    if (n37 >= 64) gm_trap_shift((uint64_t)n37, "u64", gm_srcq7ohh8h77bn7, 272, 14);
    uint64_t t38 = (uint64_t)(t35 >> (int)n37);
    uint64_t t39 = gm_f_us_cuv01fp(t38);
    gm_l4 = t39;
    void * t40 = gm_l0;
    void * t41 = GM_LD(void *, ((uint8_t *)t40));
    uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)t40) + 8));
    uint64_t t43 = gm_l4;
    if ((uintptr_t)t43 >= (uintptr_t)(t42)) gm_trap_index((uintptr_t)t43, t42, gm_srcq7ohh8h77bn7, 273, 12);
    void * t44 = (void *)(((uint8_t *)t41) + (uintptr_t)t43 * 32);
    gm_l5 = t44;
    void * t45 = gm_l5;
    void * t46 = gm_l5;
    uint32_t t47 = GM_LD(uint32_t, (((uint8_t *)t46) + 28));
    uint32_t r48; bool o49 = __builtin_add_overflow(t47, ((uint32_t)1ULL), &r48);
    if (o49) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 274, 5);
    uint32_t t50 = r48;
    GM_ST(uint32_t, (((uint8_t *)t45) + 28), t50);
    void * t51 = gm_l5;
    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t51) + 16));
    uint64_t t53 = GM_LD(uint64_t, ((uint8_t *)gm_c2t30gdg5i0));
    uint8_t t54 = ((t52 == t53));
    if (t54) {
        gm_l6 = ((uint32_t)1ULL);
    } else {
        uint64_t t55 = GM_LD(uint64_t, ((uint8_t *)gm_ckasdtpni1c));
        void * t56 = gm_l5;
        uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)t56) + 16));
        unsigned __int128 n58 = (unsigned __int128)t57;
        if (n58 >= 64) gm_trap_shift((uint64_t)n58, "u64", gm_srcq7ohh8h77bn7, 275, 48);
        uint64_t t59 = (uint64_t)(t55 >> (int)n58);
        uint32_t t60 = (uint32_t)t59;
        gm_l6 = t60;
    }
    void * t61 = gm_l5;
    uint32_t t62 = GM_LD(uint32_t, (((uint8_t *)t61) + 28));
    uint32_t t63 = gm_l6;
    uint8_t t64 = ((t62 == t63));
    if (t64) {
        void * t65 = gm_l0;
        void * t66 = (void *)(((uint8_t *)t65) + 120);
        uint64_t t67 = gm_l4;
        gm_tftb1odc739t5 t68 = gm_f_push_ucgmlo2(t66, t67);
        gm_l9 = t68;
        uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcq7ohh8h77bn7, 278, 9);
        void * t70 = gm_l0;
        void * t71 = gm_l0;
        uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)t71) + 168));
        uint32_t t73 = gm_l6;
        uint64_t t74 = (uint64_t)t73;
        uint64_t r75; bool o76 = __builtin_add_overflow(t72, t74, &r75);
        if (o76) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 279, 9);
        uint64_t t77 = r75;
        GM_ST(uint64_t, (((uint8_t *)t70) + 168), t77);
        {
            for (;;) {
                void * t78 = gm_l0;
                uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)t78) + 168));
                uint64_t t80 = GM_LD(uint64_t, ((uint8_t *)gm_c2ftoibr66a));
                uint8_t t81 = (t79 > t80);
                if (!t81) {
                    goto gm_lb1;
                }
                {
                    void * t82 = gm_l0;
                    gm_b0_1 t83 = gm_f_drop_oldest_o9uaetp((void *)NULL, cx, NULL, t82);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
gm_lb3:;
            }
        }
gm_lb1:;
    }
    F.ret = ((uint8_t)1);
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
inline __attribute__((aligned(4))) void gm_f_locate_ciotffk(void *gm_out, void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_locate_ciotffk F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    void * gm_l2; /* pg */
    void * gm_l3; /* ss */
    uint64_t gm_l4; /* i */
    void * gm_l5; /* s */
    uint64_t gm_l6; /* base */
    GM_STACK_CHECK_SHALLOW(&F, 343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * gm_l7;
    uint8_t gm_l8;
    void * t2 = gm_l0;
    uint64_t t3 = gm_l1;
    void * t4 = gm_f_page_of_artfs32(t2, t3);
    gm_l7 = t4;
    uint8_t t5 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l7)) != 0);
    if (t5) {
        void * t6 = GM_LD(void *, ((uint8_t *)&gm_l7));
        gm_l2 = t6;
    } else {
        memset(&(*(gm_taordiq61jj46 *)gm_out), 0, sizeof (*(gm_taordiq61jj46 *)gm_out));
        goto gm_fdone;
    }
    void * t7 = gm_l2;
    uint8_t t8 = (uint8_t)(GM_LD(void *, ((uint8_t *)t7)) != 0);
    if (t8) {
        void * t9 = GM_LD(void *, ((uint8_t *)t7));
        gm_l3 = t9;
    } else {
        memset(&(*(gm_taordiq61jj46 *)gm_out), 0, sizeof (*(gm_taordiq61jj46 *)gm_out));
        goto gm_fdone;
    }
    uint64_t t10 = gm_l1;
    void * t11 = gm_l2;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)t11) + 8));
    uint64_t r13; bool o14 = __builtin_sub_overflow(t10, t12, &r13);
    if (o14) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 187, 11);
    uint64_t t15 = r13;
    void * t16 = gm_l2;
    uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)t16) + 16));
    unsigned __int128 n18 = (unsigned __int128)t17;
    if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_srcq7ohh8h77bn7, 187, 10);
    uint64_t t19 = (uint64_t)(t15 >> (int)n18);
    gm_l4 = t19;
    void * t20 = gm_l3;
    uint64_t t21 = gm_l4;
    uint64_t t22 = gm_f_us_cuv01fp(t21);
    void * t23 = (void *)(((uint8_t *)t20) + (uintptr_t)t22 * 24);
    gm_l5 = t23;
    void * t24 = gm_l2;
    uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 8));
    uint64_t t26 = gm_l4;
    void * t27 = gm_l2;
    uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)t27) + 16));
    unsigned __int128 n29 = (unsigned __int128)t28;
    if (n29 >= 64) gm_trap_shift((uint64_t)n29, "u64", gm_srcq7ohh8h77bn7, 189, 24);
    uint64_t r30 = (uint64_t)((uint64_t)t26 << (int)n29);
    bool o31 = (uint64_t)(r30 >> (int)n29) != t26;
    if (o31) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 189, 24);
    uint64_t t32 = r30;
    uint64_t r33; bool o34 = __builtin_add_overflow(t25, t32, &r33);
    if (o34) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 189, 13);
    uint64_t t35 = r33;
    gm_l6 = t35;
    void * t36 = gm_l5;
    uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)t36) + 16));
    uint8_t t38 = (uint8_t)!t37;
    if (t38) {
        gm_l8 = ((uint8_t)1);
    } else {
        uint64_t t39 = gm_l1;
        uint64_t t40 = gm_l6;
        uint64_t r41; bool o42 = __builtin_sub_overflow(t39, t40, &r41);
        if (o42) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 190, 22);
        uint64_t t43 = r41;
        void * t44 = gm_l5;
        uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)t44) + 8));
        uint8_t t46 = (t43 > t45);
        gm_l8 = t46;
    }
    uint8_t t47 = gm_l8;
    if (t47) {
        memset(&(*(gm_taordiq61jj46 *)gm_out), 0, sizeof (*(gm_taordiq61jj46 *)gm_out));
        goto gm_fdone;
    }
    void * t48 = gm_l5;
    uint64_t t49 = gm_l6;
    gm_ttf6i6v1da8cm t50;
    GM_ST(void *, ((uint8_t *)&t50 + 0), t48);
    GM_ST(uint64_t, ((uint8_t *)&t50 + 8), t49);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_taordiq61jj46 *)gm_out) + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_taordiq61jj46 *)gm_out) + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_taordiq61jj46 *)gm_out) + 20, 0);
    GM_ST(gm_ttf6i6v1da8cm, ((uint8_t *)&(*(gm_taordiq61jj46 *)gm_out)), t50);
    ((uint8_t *)&(*(gm_taordiq61jj46 *)gm_out))[16] = 1;
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_taordiq61jj46 gm_f_locate_ciotffk_c(void * a0, uint64_t a1) { gm_taordiq61jj46 gm_r; gm_f_locate_ciotffk(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_b0_1 gm_f_mem_deinit_0088958(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_mem_deinit_0088958 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* pi */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = ((uint64_t)0ULL);
    void * t7 = gm_l0;
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t7) + 8));
    gm_l1 = t8;
    {
        for (;;) {
            uint64_t t9 = gm_l2;
            uint64_t t10 = gm_l1;
            uint8_t t11 = (t9 < t10);
            if (!t11) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t12 = gm_l2;
                        gm_l3 = t12;
                        void * t13 = gm_l0;
                        uint64_t t14 = gm_l3;
                        uint64_t t15 = gm_f_release_page_66inn9t((void *)NULL, cx, NULL, t13, t14);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 327, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t16 = gm_l2;
            uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
            uint64_t t19 = r17;
            gm_l2 = t19;
        }
    }
gm_lb1:;
    void * t20 = gm_l0;
    void * t21 = (void *)((uint8_t *)t20);
    gm_f_deinit_u4v62un(t21);
    void * t22 = gm_l0;
    void * t23 = (void *)(((uint8_t *)t22) + 120);
    gm_f_deinit_d5p9g61(t23);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_mem_message_e0go901(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_mem_message_e0go901 F;
    uint64_t gm_l1; /* addr */
    uint64_t gm_l2; /* len */
    uint64_t gm_l3; /* addr */
    gm_t9qkh0rsooc8d gm_l4; /* kind */
    gm_b16_8 gm_l5; /* what */
    gm_t66j1im9ip128 gm_l6; /* x */
    uint64_t gm_l7; /* addr */
    uint64_t gm_l8; /* align */
    uint64_t gm_l9; /* addr */
    uint64_t gm_l10; /* size */
    gm_t9qkh0rsooc8d gm_l11; /* kind */
    GM_STACK_CHECK(&F, 992u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 gm_l12;
    gm_tabb710d9mq2e gm_l13;
    gm_tabb710d9mq2e gm_l14;
    gm_tabb710d9mq2e gm_l15;
    gm_tabb710d9mq2e gm_l16;
    gm_tabb710d9mq2e gm_l17;
    gm_b16_8 gm_l18;
    gm_tabb710d9mq2e gm_l19;
    gm_tabb710d9mq2e gm_l20;
    {
        {
            uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t19 = (uint8_t)(t18 == ((uint8_t)0ULL));
            if (!t19) {
                goto gm_lb2;
            }
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_suklu1i9clb5l));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(47));
            gm_l12 = t20;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t22 = (uint8_t)(t21 == ((uint8_t)1ULL));
            if (!t22) {
                goto gm_lb3;
            }
            uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
            gm_l1 = t23;
            uint64_t t24 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
            gm_l2 = t24;
            uint64_t t25 = gm_l2;
            uint64_t t26 = gm_l1;
            gm_t2lqu8jsj511d t27;
            GM_ST(uint64_t, ((uint8_t *)&t27 + 0), t25);
            GM_ST(uint64_t, ((uint8_t *)&t27 + 8), t26);
            gm_f_format_bovj9ap((void *)&gm_l13, (void *)NULL, cx, NULL, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t28 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t28) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 8, gm_srcq7ohh8h77bn7, 78, 29);
            gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l13) + 8));
            gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
            gm_l12 = t30;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t32 = (uint8_t)(t31 == ((uint8_t)2ULL));
            if (!t32) {
                goto gm_lb4;
            }
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
            gm_l3 = t33;
            gm_t9qkh0rsooc8d t34 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)a0) + 8) + 8));
            gm_l4 = t34;
            {
                {
                    {
                        {
                            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                            uint8_t t36 = (uint8_t)(t35 == ((uint8_t)0ULL));
                            if (!t36) {
                                goto gm_lb8;
                            }
                            goto gm_lb7;
                        }
gm_lb8:;
                        {
                            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                            uint8_t t38 = (uint8_t)(t37 == ((uint8_t)1ULL));
                            if (!t38) {
                                goto gm_lb9;
                            }
                            goto gm_lb7;
                        }
gm_lb9:;
                        goto gm_lb6;
                    }
gm_lb7:;
                    gm_b16_8 t39;
                    GM_ST(void *, (uint8_t *)&t39, (void *)(gm_skt7g4v99o4j0));
                    GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(41));
                    gm_l5 = t39;
                    goto gm_lb5;
                }
gm_lb6:;
                {
                    uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&gm_l4) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)2ULL));
                    if (!t41) {
                        goto gm_lb10;
                    }
                    gm_b16_8 t42;
                    GM_ST(void *, (uint8_t *)&t42, (void *)(gm_sagd3osht35og));
                    GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(12));
                    gm_l5 = t42;
                    goto gm_lb5;
                }
gm_lb10:;
                {
                    gm_b16_8 t43;
                    GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sacs352aeakr8));
                    GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(17));
                    gm_l5 = t43;
                    goto gm_lb5;
                }
gm_lb11:;
                gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 80, 17);
            }
gm_lb5:;
            {
                uint64_t t44 = gm_l3;
                gm_b16_8 t45 = gm_l5;
                gm_tf1bjg81u0glv t46;
                GM_ST(uint64_t, ((uint8_t *)&t46 + 0), t44);
                GM_ST(gm_b16_8, ((uint8_t *)&t46 + 8), t45);
                gm_f_format_q07lmui((void *)&gm_l14, (void *)NULL, cx, NULL, &t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l14))[0] != 0);
            if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l14) + 8, gm_srcq7ohh8h77bn7, 85, 9);
            gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 8));
            gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
            gm_l12 = t49;
            goto gm_lb1;
        }
gm_lb4:;
        {
            uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t51 = (uint8_t)(t50 == ((uint8_t)3ULL));
            if (!t51) {
                goto gm_lb12;
            }
            {
                memmove(((uint8_t *)&gm_l6), (((uint8_t *)a0) + 8), 32);
            }
            {
                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t53 = GM_LD(uint64_t, ((uint8_t *)&gm_l6));
                uint64_t t54 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 16));
                uint64_t t55 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 24));
                gm_t66j1im9ip128 t56;
                GM_ST(uint64_t, ((uint8_t *)&t56 + 0), t52);
                GM_ST(uint64_t, ((uint8_t *)&t56 + 8), t53);
                GM_ST(uint64_t, ((uint8_t *)&t56 + 16), t54);
                GM_ST(uint64_t, ((uint8_t *)&t56 + 24), t55);
                gm_f_format_173gk69((void *)&gm_l15, (void *)NULL, cx, NULL, &t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l15))[0] != 0);
            if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l15) + 8, gm_srcq7ohh8h77bn7, 87, 24);
            gm_b16_8 t58 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
            gm_l12 = t59;
            goto gm_lb1;
        }
gm_lb12:;
        {
            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t61 = (uint8_t)(t60 == ((uint8_t)4ULL));
            if (!t61) {
                goto gm_lb13;
            }
            uint64_t t62 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
            gm_l7 = t62;
            uint64_t t63 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
            gm_l8 = t63;
            uint64_t t64 = gm_l7;
            uint64_t t65 = gm_l8;
            gm_t2lqu8jsj511d t66;
            GM_ST(uint64_t, ((uint8_t *)&t66 + 0), t64);
            GM_ST(uint64_t, ((uint8_t *)&t66 + 8), t65);
            gm_f_format_hvaqp8b((void *)&gm_l16, (void *)NULL, cx, NULL, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t67 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
            if (!t67) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcq7ohh8h77bn7, 88, 33);
            gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
            gm_l12 = t69;
            goto gm_lb1;
        }
gm_lb13:;
        {
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t71 = (uint8_t)(t70 == ((uint8_t)5ULL));
            if (!t71) {
                goto gm_lb14;
            }
            uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
            gm_l9 = t72;
            uint64_t t73 = gm_l9;
            gm_t4ce0jd3tdro4 t74;
            GM_ST(uint64_t, ((uint8_t *)&t74 + 0), t73);
            gm_f_format_n0787fu((void *)&gm_l17, (void *)NULL, cx, NULL, t74);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t75 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (!t75) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcq7ohh8h77bn7, 89, 24);
            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l17) + 8));
            gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
            gm_l12 = t77;
            goto gm_lb1;
        }
gm_lb14:;
        {
            uint8_t t78 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t79 = (uint8_t)(t78 == ((uint8_t)6ULL));
            if (!t79) {
                goto gm_lb15;
            }
            gm_b16_8 t80;
            GM_ST(void *, (uint8_t *)&t80, (void *)(gm_sdvkaj73n9goh));
            GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(23));
            gm_l12 = t80;
            goto gm_lb1;
        }
gm_lb15:;
        {
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
            uint8_t t82 = (uint8_t)(t81 == ((uint8_t)7ULL));
            if (!t82) {
                goto gm_lb16;
            }
            uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)a0) + 8));
            gm_l10 = t83;
            gm_t9qkh0rsooc8d t84 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)a0) + 8) + 8));
            gm_l11 = t84;
            gm_t9qkh0rsooc8d t85 = gm_l11;
            gm_t9qkh0rsooc8d t86;
            GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)0ULL));
            gm_t9qkh0rsooc8d t87;
            GM_ST(uint8_t, ((uint8_t *)&t87 + 0), ((uint8_t)1ULL));
            gm_t0am46iohlpol t88;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t88 + 0), t86);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t88 + 1), t87);
            uint8_t t89 = 0;
            if (gm_eqv4kh4qul78(((uint8_t *)&t88) + 0, ((uint8_t *)&t85))) t89 = 1;
            if (gm_eqv4kh4qul78(((uint8_t *)&t88) + 1, ((uint8_t *)&t85))) t89 = 1;
            uint8_t t90 = (uint8_t)(t89);
            if (t90) {
                uint64_t t91 = gm_l10;
                gm_t4ce0jd3tdro4 t92;
                GM_ST(uint64_t, ((uint8_t *)&t92 + 0), t91);
                gm_f_format_itetl3f((void *)&gm_l19, (void *)NULL, cx, NULL, t92);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t93 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                if (!t93) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 8, gm_srcq7ohh8h77bn7, 91, 77);
                gm_b16_8 t94 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l19) + 8));
                gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
                gm_l18 = t95;
            } else {
                uint64_t t96 = gm_l10;
                gm_t4ce0jd3tdro4 t97;
                GM_ST(uint64_t, ((uint8_t *)&t97 + 0), t96);
                gm_f_format_o05ssh3((void *)&gm_l20, (void *)NULL, cx, NULL, t97);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srcq7ohh8h77bn7, 91, 156);
                gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l20) + 8));
                gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
                gm_l18 = t100;
            }
            gm_b16_8 t101 = gm_l18;
            gm_l12 = t101;
            goto gm_lb1;
        }
gm_lb16:;
        gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 76, 57);
    }
gm_lb1:;
    gm_b16_8 t102 = gm_l12;
    F.ret = t102;
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
inline __attribute__((aligned(4))) void gm_f_memory_dqom95e(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint64_t a1) {
    struct gm_fr_memory_dqom95e F;
    uint64_t gm_l0; /* data_base */
    uint64_t gm_l1; /* ptr_bytes */
    uint64_t gm_l2; /* limit */
    uint64_t gm_l3; /* base */
    GM_STACK_CHECK(&F, 297u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t2 = gm_l1;
    uint8_t t3 = (t2 >= ((uint64_t)8ULL));
    if (t3) {
        uint64_t t4 = (uint64_t)~((uint64_t)0ULL);
        gm_l2 = t4;
    } else {
        uint64_t t5 = gm_l1;
        uint64_t r6; bool o7 = __builtin_mul_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 111, 62);
        uint64_t t8 = r6;
        unsigned __int128 n9 = (unsigned __int128)t8;
        if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srcq7ohh8h77bn7, 111, 51);
        uint64_t r10 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n9);
        bool o11 = (uint64_t)(r10 >> (int)n9) != ((uint64_t)1ULL);
        if (o11) gm_trap("integer overflow in `<<`", gm_srcq7ohh8h77bn7, 111, 51);
        uint64_t t12 = r10;
        gm_l2 = t12;
    }
    uint64_t t13 = gm_l0;
    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)gm_ckasdtpni1c));
    uint64_t r15; bool o16 = __builtin_add_overflow(t13, t14, &r15);
    if (o16) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 112, 14);
    uint64_t t17 = r15;
    uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
    if (o19) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 112, 14);
    uint64_t t20 = r18;
    uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)gm_ckasdtpni1c));
    if (t21 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 112, 13);
    uint64_t t22 = (uint64_t)(t20 / t21);
    uint64_t t23 = GM_LD(uint64_t, ((uint8_t *)gm_ckasdtpni1c));
    uint64_t r24; bool o25 = __builtin_mul_overflow(t22, t23, &r24);
    if (o25) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 112, 13);
    uint64_t t26 = r24;
    gm_l3 = t26;
    {
        gm_trqfvmjtt4sij t27;
        gm_f_list_bcj49nd((void *)&t27, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t28 = gm_l3;
        uint64_t t29 = gm_l2;
        gm_tl4e4vqe807ge t30;
        for (size_t i31 = 0; i31 < 8; i31++) GM_ST(uint64_t, (uint8_t *)&t30 + i31 * 8, ((uint64_t)0ULL));
        gm_trqfvmjtt4sij t32;
        gm_f_list_5dhgg4i((void *)&t32, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 0), t27);
        GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 40), t28);
        GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 48), t29);
        GM_ST(gm_tl4e4vqe807ge, ((uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 56), t30);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 120), t32);
        GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 160), ((uint64_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&(*(gm_t4m55nu62fs91 *)gm_out) + 168), ((uint64_t)0ULL));
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_next_pages_p5tqfl1(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_next_pages_p5tqfl1 F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* addr */
    GM_STACK_CHECK_SHALLOW(&F, 217u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 40));
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 8));
    uint64_t t6 = (uint64_t)t5;
    uint64_t t7 = GM_LD(uint64_t, ((uint8_t *)gm_ckasdtpni1c));
    uint64_t r8; bool o9 = __builtin_mul_overflow(t6, t7, &r8);
    if (o9) gm_trap("integer overflow in `*`", gm_srcq7ohh8h77bn7, 118, 22);
    uint64_t t10 = r8;
    uint64_t r11; bool o12 = __builtin_add_overflow(t3, t10, &r11);
    if (o12) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 118, 13);
    uint64_t t13 = r11;
    gm_l2 = t13;
    uint64_t t14 = gm_l1;
    void * t15 = gm_l0;
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t15) + 48));
    uint64_t t17 = gm_l2;
    uint64_t r18; bool o19 = __builtin_sub_overflow(t16, t17, &r18);
    if (o19) gm_trap("integer overflow in `-`", gm_srcq7ohh8h77bn7, 119, 13);
    uint64_t t20 = r18;
    uint64_t t21 = GM_LD(uint64_t, ((uint8_t *)gm_ckasdtpni1c));
    if (t21 == 0) gm_trap("division by zero", gm_srcq7ohh8h77bn7, 119, 12);
    uint64_t t22 = (uint64_t)(t20 / t21);
    uint8_t t23 = (t14 > t22);
    if (t23) {
        gm_tf5srh4mp4b3i t24;
        memset(&t24, 0, sizeof t24);
        F.ret = t24;
        goto gm_fret;
    }
    uint64_t t25 = gm_l2;
    gm_tf5srh4mp4b3i t26;
    GM_ST(uint8_t, (uint8_t *)&t26 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t26 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t26 + 12, 0);
    GM_ST(uint64_t, ((uint8_t *)&t26), t25);
    ((uint8_t *)&t26)[8] = 1;
    F.ret = t26;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void * gm_f_page_of_artfs32(void * a0, uint64_t a1);
extern __attribute__((aligned(4))) void gm_f_read_u6r4vns(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_t934k1j7u70ct gm_f_read_u6r4vns_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3) { gm_t934k1j7u70ct gm_r; gm_f_read_u6r4vns(&gm_r, a0, a1, a2, a3); return gm_r; }
__attribute__((aligned(4))) uint64_t gm_f_release_page_66inn9t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1) {
    struct gm_fr_release_page_66inn9t F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* pi */
    void * gm_l2; /* pg */
    void * gm_l3; /* ss */
    void * gm_l4; /* s */
    uint64_t gm_l5; /* k */
    uint64_t gm_l6; /* per */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    void * gm_l10; /* s */
    GM_STACK_CHECK(&F, 829u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l11;
    void * t17 = gm_l0;
    void * t18 = GM_LD(void *, ((uint8_t *)t17));
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)t17) + 8));
    uint64_t t20 = gm_l1;
    if ((uintptr_t)t20 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t20, t19, gm_srcq7ohh8h77bn7, 302, 12);
    void * t21 = (void *)(((uint8_t *)t18) + (uintptr_t)t20 * 32);
    gm_l2 = t21;
    void * t22 = gm_l2;
    uint8_t t23 = (uint8_t)(GM_LD(void *, ((uint8_t *)t22)) != 0);
    if (t23) {
        void * t24 = GM_LD(void *, ((uint8_t *)t22));
        gm_l3 = t24;
    } else {
        F.ret = ((uint64_t)0ULL);
        goto gm_fret;
    }
    void * t25 = gm_l2;
    uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)t25) + 16));
    uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)gm_c2t30gdg5i0));
    uint8_t t28 = ((t26 == t27));
    if (t28) {
        void * t29 = gm_l3;
        void * t30 = (void *)(((uint8_t *)t29) + (uintptr_t)((uint64_t)0ULL) * 24);
        gm_l4 = t30;
        {
            {
                void * t31 = gm_l4;
                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)t31) + 17));
                if (!t32) {
                    goto gm_lb2;
                }
                void * t33 = gm_l4;
                uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)t33) + 8));
                uint8_t t35 = (t34 > ((uint64_t)0ULL));
                if (!t35) {
                    goto gm_lb2;
                }
                void * t36 = gm_l4;
                void * t37 = GM_LD(void *, ((uint8_t *)t36));
                void * t38 = gm_l4;
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)t38) + 8));
                uint64_t t40 = gm_f_us_cuv01fp(t39);
                uint64_t t41 = ((uintptr_t)t40) - (uintptr_t)((uint64_t)0ULL);
                gm_b16_8 t42;
                GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t37 + (uintptr_t)((uint64_t)0ULL) * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(t41));
                gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
                gm_b0_1 t44 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t43, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0ddgrnig17)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
        uint64_t t45 = gm_l1;
        gm_l5 = t45;
        {
            for (;;) {
                uint64_t t46 = gm_l5;
                void * t47 = gm_l0;
                uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)t47) + 8));
                uint8_t t49 = (t46 < t48);
                if (!t49) {
                    goto gm_lb3;
                }
                void * t50 = gm_l0;
                void * t51 = GM_LD(void *, ((uint8_t *)t50));
                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t50) + 8));
                uint64_t t53 = gm_l5;
                if ((uintptr_t)t53 >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)t53, t52, gm_srcq7ohh8h77bn7, 308, 41);
                uint64_t t54 = GM_LD(uint64_t, ((((uint8_t *)t51) + (uintptr_t)t53 * 32) + 16));
                uint64_t t55 = GM_LD(uint64_t, ((uint8_t *)gm_c2t30gdg5i0));
                uint8_t t56 = ((t54 == t55));
                if (!t56) {
                    goto gm_lb3;
                }
                void * t57 = gm_l0;
                void * t58 = GM_LD(void *, ((uint8_t *)t57));
                uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)t57) + 8));
                uint64_t t60 = gm_l5;
                if ((uintptr_t)t60 >= (uintptr_t)(t59)) gm_trap_index((uintptr_t)t60, t59, gm_srcq7ohh8h77bn7, 308, 77);
                uint64_t t61 = GM_LD(uint64_t, ((((uint8_t *)t58) + (uintptr_t)t60 * 32) + 8));
                void * t62 = gm_l2;
                uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)t62) + 8));
                uint8_t t64 = ((t61 == t63));
                if (!t64) {
                    goto gm_lb3;
                }
                void * t65 = gm_l0;
                void * t66 = GM_LD(void *, ((uint8_t *)t65));
                uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)t65) + 8));
                uint64_t t68 = gm_l5;
                if ((uintptr_t)t68 >= (uintptr_t)(t67)) gm_trap_index((uintptr_t)t68, t67, gm_srcq7ohh8h77bn7, 308, 114);
                {
                    {
                        uint8_t t69 = (uint8_t)(GM_LD(void *, (((uint8_t *)t66) + (uintptr_t)t68 * 32)) != 0);
                        if (t69) {
                            goto gm_lb7;
                        }
                        gm_l11 = ((uint8_t)0);
                        goto gm_lb6;
                    }
gm_lb7:;
                    {
                        gm_l11 = ((uint8_t)1);
                        goto gm_lb6;
                    }
gm_lb8:;
                    gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 308, 114);
                }
gm_lb6:;
                uint8_t t70 = gm_l11;
                if (!t70) {
                    goto gm_lb3;
                }
                {
                    void * t71 = gm_l0;
                    void * t72 = GM_LD(void *, ((uint8_t *)t71));
                    uint64_t t73 = GM_LD(uint64_t, (((uint8_t *)t71) + 8));
                    uint64_t t74 = gm_l5;
                    if ((uintptr_t)t74 >= (uintptr_t)(t73)) gm_trap_index((uintptr_t)t74, t73, gm_srcq7ohh8h77bn7, 309, 13);
                    GM_ST(void *, (((uint8_t *)t72) + (uintptr_t)t74 * 32), ((void *)0));
                    uint64_t t75 = gm_l5;
                    uint64_t r76; bool o77 = __builtin_add_overflow(t75, ((uint64_t)1ULL), &r76);
                    if (o77) gm_trap("integer overflow in `+`", gm_srcq7ohh8h77bn7, 310, 13);
                    uint64_t t78 = r76;
                    gm_l5 = t78;
                }
gm_lb5:;
            }
        }
gm_lb3:;
        void * t79 = gm_l4;
        gm_b0_1 t80 = gm_f_destroy_l0inh8t((void *)NULL, cx, NULL, t79, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kgubkej3sov)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = ((uint64_t)1ULL);
        goto gm_fret;
    }
    uint64_t t81 = GM_LD(uint64_t, ((uint8_t *)gm_ckasdtpni1c));
    void * t82 = gm_l2;
    uint64_t t83 = GM_LD(uint64_t, (((uint8_t *)t82) + 16));
    unsigned __int128 n84 = (unsigned __int128)t83;
    if (n84 >= 64) gm_trap_shift((uint64_t)n84, "u64", gm_srcq7ohh8h77bn7, 315, 15);
    uint64_t t85 = (uint64_t)(t81 >> (int)n84);
    uint64_t t86 = gm_f_us_cuv01fp(t85);
    gm_l6 = t86;
    gm_l8 = ((uint64_t)0ULL);
    uint64_t t87 = gm_l6;
    gm_l7 = t87;
    {
        for (;;) {
            uint64_t t88 = gm_l8;
            uint64_t t89 = gm_l7;
            uint8_t t90 = (t88 < t89);
            if (!t90) {
                goto gm_lb9;
            }
            {
                {
                    {
                        uint64_t t91 = gm_l8;
                        gm_l9 = t91;
                        void * t92 = gm_l3;
                        uint64_t t93 = gm_l9;
                        void * t94 = (void *)(((uint8_t *)t92) + (uintptr_t)t93 * 24);
                        gm_l10 = t94;
                        {
                            {
                                void * t95 = gm_l10;
                                uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)t95) + 17));
                                if (!t96) {
                                    goto gm_lb15;
                                }
                                void * t97 = gm_l10;
                                uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)t97) + 8));
                                uint8_t t99 = (t98 > ((uint64_t)0ULL));
                                if (!t99) {
                                    goto gm_lb15;
                                }
                                void * t100 = gm_l10;
                                void * t101 = GM_LD(void *, ((uint8_t *)t100));
                                void * t102 = gm_l10;
                                uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)t102) + 8));
                                uint64_t t104 = gm_f_us_cuv01fp(t103);
                                uint64_t t105 = ((uintptr_t)t104) - (uintptr_t)((uint64_t)0ULL);
                                gm_b16_8 t106;
                                GM_ST(void *, (uint8_t *)&t106, (void *)(((uint8_t *)t101 + (uintptr_t)((uint64_t)0ULL) * 1)));
                                GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(t105));
                                gm_b16_8 t107 = GM_LD(gm_b16_8, ((uint8_t *)&t106));
                                gm_b0_1 t108 = gm_f_free_rkuh2rm((void *)NULL, cx, NULL, t107, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k2afmplo64l)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                goto gm_lb14;
                            }
gm_lb15:;
                        }
gm_lb14:;
                        goto gm_lb12;
                    }
gm_lb13:;
                    gm_trap("no match arm matched the scrutinee", gm_srcq7ohh8h77bn7, 316, 5);
                }
gm_lb12:;
            }
gm_lb11:;
            uint64_t t109 = gm_l8;
            uint64_t r110; bool o111 = __builtin_add_overflow(t109, ((uint64_t)1ULL), &r110);
            uint64_t t112 = r110;
            gm_l8 = t112;
        }
    }
gm_lb9:;
    void * t113 = gm_l3;
    uint64_t t114 = gm_l6;
    uint64_t t115 = ((uintptr_t)t114) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t116;
    GM_ST(void *, (uint8_t *)&t116, (void *)(((uint8_t *)t113 + (uintptr_t)((uint64_t)0ULL) * 24)));
    GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(t115));
    gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&t116));
    gm_b0_1 t118 = gm_f_free_oobi9fu((void *)NULL, cx, NULL, t117, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ke9p7o91039)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    void * t119 = gm_l2;
    GM_ST(void *, ((uint8_t *)t119), ((void *)0));
    uint64_t t120 = gm_l6;
    F.ret = t120;
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
extern __attribute__((aligned(4))) void * gm_f_view_qalkbv1(void * a0, uint64_t a1, uint64_t a2, uint8_t a3);
inline __attribute__((aligned(4))) void gm_f_write_5gub0ul(void *gm_out, void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_5gub0ul F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* addr */
    gm_b16_8 gm_l2; /* data */
    uint64_t gm_l3; /* align */
    gm_b16_8 gm_l4; /* d */
    GM_STACK_CHECK(&F, 233u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_t934k1j7u70ct gm_l5;
    {
        void * t2 = gm_l0;
        uint64_t t3 = gm_l1;
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
        uint64_t t5 = (uint64_t)t4;
        uint64_t t6 = gm_l3;
        gm_f_write_view_pebit6h((void *)&gm_l5, t2, t3, t5, t6);
    }
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
    if (t7) {
    } else {
        gm_tpekhfs32op0f t8 = GM_LD(gm_tpekhfs32op0f, (((uint8_t *)&gm_l5) + 8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 4, 0);
        ((uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out))[0] = 1;
        GM_ST(gm_tpekhfs32op0f, ((uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 8), t8);
        goto gm_fdone;
    }
    gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
    gm_l4 = t9;
    gm_b16_8 t10 = gm_l4;
    gm_b16_8 t11 = gm_l2;
    gm_f_copy_from_f7j6p05(t10, t11);
    {
        memset(&(*(gm_tgfdnufetdnfc *)gm_out), 0, sizeof (*(gm_tgfdnufetdnfc *)gm_out));
        ((uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out))[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&(*(gm_tgfdnufetdnfc *)gm_out) + 8), GM_ZERO(gm_b0_1));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tgfdnufetdnfc gm_f_write_5gub0ul_c(void * a0, uint64_t a1, gm_b16_8 a2, uint64_t a3) { gm_tgfdnufetdnfc gm_r; gm_f_write_5gub0ul(&gm_r, a0, a1, a2, a3); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_write_view_pebit6h(void *gm_out, void * a0, uint64_t a1, uint64_t a2, uint64_t a3);
__attribute__((aligned(4))) gm_t934k1j7u70ct gm_f_write_view_pebit6h_c(void * a0, uint64_t a1, uint64_t a2, uint64_t a3) { gm_t934k1j7u70ct gm_r; gm_f_write_view_pebit6h(&gm_r, a0, a1, a2, a3); return gm_r; }

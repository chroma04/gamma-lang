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
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
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
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; uint8_t v2; gm_t9qkh0rsooc8d v3; gm_t9qkh0rsooc8d v4; gm_tcc1rq4n1avb2 v5; uint32_t v6; uint32_t v7; } f1; } gm_t8588ma2cr9d7;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(e: ^const Expr, l: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t66j1im9ip128 f0; uint8_t f1; uint8_t pe[7]; } gm_td8hfgklo8dil;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t9qkh0rsooc8d f0; gm_t9qkh0rsooc8d f1; gm_t9qkh0rsooc8d f2; } gm_tk22flr9mh3ql;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; void * f3; } gm_tl93vjpcesj48;
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { gm_tiilmfd16v3fc f0; gm_b16_8 f1; } gm_t1cs2fjnrqap1;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; gm_tkn42u7na3t81 v1; } f1; } gm_t1qr0p6kln6tf;
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t8588ma2cr9d7 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_t9q8lhljes74c;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t79rbdv4rafe5 v0; void * v1; } f1; } gm_tapdr2ag17fh7;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_td8hfgklo8dil) == 40 && _Alignof(gm_td8hfgklo8dil) == 8 && offsetof(gm_td8hfgklo8dil, f0) == 0 && offsetof(gm_td8hfgklo8dil, f1) == 32, "layout of `?(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { gm_tiilmfd16v3fc f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_tj44oivvogvtl;
_Static_assert(sizeof(gm_tk22flr9mh3ql) == 3 && _Alignof(gm_tk22flr9mh3ql) == 1 && offsetof(gm_tk22flr9mh3ql, f0) == 0 && offsetof(gm_tk22flr9mh3ql, f1) == 1 && offsetof(gm_tk22flr9mh3ql, f2) == 2, "layout of `(Keyword, Keyword, Keyword)`");
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tl93vjpcesj48) == 48 && _Alignof(gm_tl93vjpcesj48) == 8 && offsetof(gm_tl93vjpcesj48, f0) == 0 && offsetof(gm_tl93vjpcesj48, f1) == 8 && offsetof(gm_tl93vjpcesj48, f2) == 24 && offsetof(gm_tl93vjpcesj48, f3) == 40, "layout of `NestedFn`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t pe[4]; } gm_tuanp0jbrlb6j;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
typedef struct { gm_tlivgedjs5ps3 f0; gm_b16_8 f1; gm_tapdr2ag17fh7 f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t0kv05e068jrn;
_Static_assert(sizeof(gm_t1cs2fjnrqap1) == 40 && _Alignof(gm_t1cs2fjnrqap1) == 8 && offsetof(gm_t1cs2fjnrqap1, f0) == 0 && offsetof(gm_t1cs2fjnrqap1, f1) == 24, "layout of `HandlerLit`");
_Static_assert(sizeof(gm_t1qr0p6kln6tf) == 12 && _Alignof(gm_t1qr0p6kln6tf) == 4 && offsetof(gm_t1qr0p6kln6tf, f0) == 0 && offsetof(gm_t1qr0p6kln6tf, f1) == 4, "layout of `LabelValue`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_th2lqbo77pvsc f0; uint32_t f1; uint8_t f2; uint8_t p3[3]; gm_t1qr0p6kln6tf f3; uint8_t pe[4]; } gm_t8enl5g7b22jb;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9q8lhljes74c) == 32 && _Alignof(gm_t9q8lhljes74c) == 8 && offsetof(gm_t9q8lhljes74c, f0) == 0 && offsetof(gm_t9q8lhljes74c, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tapdr2ag17fh7) == 48 && _Alignof(gm_tapdr2ag17fh7) == 8 && offsetof(gm_tapdr2ag17fh7, f0) == 0 && offsetof(gm_tapdr2ag17fh7, f1) == 8, "layout of `WhenElse`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tj44oivvogvtl) == 40 && _Alignof(gm_tj44oivvogvtl) == 8 && offsetof(gm_tj44oivvogvtl, f0) == 0 && offsetof(gm_tj44oivvogvtl, f1) == 24, "layout of `Performed`");
typedef struct { gm_tuanp0jbrlb6j f0; uint8_t f1; uint8_t pe[7]; } gm_tjvtgq0p0sehv;
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tuanp0jbrlb6j) == 32 && _Alignof(gm_tuanp0jbrlb6j) == 8 && offsetof(gm_tuanp0jbrlb6j, f0) == 0 && offsetof(gm_tuanp0jbrlb6j, f1) == 24, "layout of `ForLabel`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
_Static_assert(sizeof(gm_t0kv05e068jrn) == 152 && _Alignof(gm_t0kv05e068jrn) == 8 && offsetof(gm_t0kv05e068jrn, f0) == 0 && offsetof(gm_t0kv05e068jrn, f1) == 72 && offsetof(gm_t0kv05e068jrn, f2) == 88 && offsetof(gm_t0kv05e068jrn, f3) == 136, "layout of `FnLit`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t8enl5g7b22jb) == 48 && _Alignof(gm_t8enl5g7b22jb) == 8 && offsetof(gm_t8enl5g7b22jb, f0) == 0 && offsetof(gm_t8enl5g7b22jb, f1) == 24 && offsetof(gm_t8enl5g7b22jb, f2) == 28 && offsetof(gm_t8enl5g7b22jb, f3) == 32, "layout of `LabelInfo`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
typedef struct { gm_t8enl5g7b22jb f0; uint8_t f1; uint8_t pe[7]; } gm_te5gbhu3us3k4;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tjvtgq0p0sehv) == 40 && _Alignof(gm_tjvtgq0p0sehv) == 8 && offsetof(gm_tjvtgq0p0sehv, f0) == 0 && offsetof(gm_tjvtgq0p0sehv, f1) == 32, "layout of `?ForLabel`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_tkn42u7na3t81 f3; uint8_t p4[4]; gm_tjvtgq0p0sehv f4; gm_tkn42u7na3t81 f5; gm_tqu3m2l3n8mnc f6; } gm_tnqmlaad6uchl;
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_tnqmlaad6uchl f0; uint8_t f1; uint8_t pe[7]; } gm_t403eokj7ifg9;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_te5gbhu3us3k4) == 56 && _Alignof(gm_te5gbhu3us3k4) == 8 && offsetof(gm_te5gbhu3us3k4, f0) == 0 && offsetof(gm_te5gbhu3us3k4, f1) == 48, "layout of `?LabelInfo`");
_Static_assert(sizeof(gm_tnqmlaad6uchl) == 112 && _Alignof(gm_tnqmlaad6uchl) == 8 && offsetof(gm_tnqmlaad6uchl, f0) == 0 && offsetof(gm_tnqmlaad6uchl, f1) == 4 && offsetof(gm_tnqmlaad6uchl, f2) == 12 && offsetof(gm_tnqmlaad6uchl, f3) == 20 && offsetof(gm_tnqmlaad6uchl, f4) == 32 && offsetof(gm_tnqmlaad6uchl, f5) == 72 && offsetof(gm_tnqmlaad6uchl, f6) == 80, "layout of `ClauseInfo`");
typedef struct { gm_t1s75i08qtptd f0; gm_t0d02142k4tu8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tnrgict8sthcv;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_t9q8lhljes74c v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_to0f5qn75cgg8;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_tc3oht4ksth9d f0; uint8_t f1; uint8_t pe[7]; } gm_t0mfir12leg2g;
_Static_assert(sizeof(gm_t403eokj7ifg9) == 120 && _Alignof(gm_t403eokj7ifg9) == 8 && offsetof(gm_t403eokj7ifg9, f0) == 0 && offsetof(gm_t403eokj7ifg9, f1) == 112, "layout of `?ClauseInfo`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_tnrgict8sthcv) == 120 && _Alignof(gm_tnrgict8sthcv) == 8 && offsetof(gm_tnrgict8sthcv, f0) == 0 && offsetof(gm_tnrgict8sthcv, f1) == 40 && offsetof(gm_tnrgict8sthcv, f2) == 104, "layout of `PatArg`");
_Static_assert(sizeof(gm_to0f5qn75cgg8) == 64 && _Alignof(gm_to0f5qn75cgg8) == 8 && offsetof(gm_to0f5qn75cgg8, f0) == 0 && offsetof(gm_to0f5qn75cgg8, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_to0f5qn75cgg8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tsf6i2agfnpva;
_Static_assert(sizeof(gm_t0mfir12leg2g) == 96 && _Alignof(gm_t0mfir12leg2g) == 8 && offsetof(gm_t0mfir12leg2g, f0) == 0 && offsetof(gm_t0mfir12leg2g, f1) == 88, "layout of `?Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_tsf6i2agfnpva) == 88 && _Alignof(gm_tsf6i2agfnpva) == 8 && offsetof(gm_tsf6i2agfnpva, f0) == 0 && offsetof(gm_tsf6i2agfnpva, f1) == 8 && offsetof(gm_tsf6i2agfnpva, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_block_of_2gjl2i1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_block_q09sn1p {
    gm_frame h;
    void * ret;
};
struct gm_fr_empty_effects_r0iv3pv {
    gm_frame h;
    gm_tnpifv20irnph ret;
};
struct gm_fr_empty_slice_3jt8vgk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3v2gjd4_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73v4ttb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_92arfe8 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_c3qnk0a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_fa4sjvq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ilis91k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ito0pap {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jvoelso {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_qlbp545 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r9g5q6n {
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
struct gm_fr_enumerate_loop_5f6m89f {
    gm_frame h;
    gm_tsth9qu1dv0g3 l5; /* callee */
    gm_tl874t2atnlh3 l12; /* span */
    gm_trqfvmjtt4sij l17; /* fields */
    gm_trqfvmjtt4sij l20; /* stmts */
    void * ret;
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
struct gm_fr_extend_43vto3v {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_nkql463 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_t9suqla {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_for_iterator_cc5qo7n {
    gm_frame h;
    gm_tsth9qu1dv0g3 l11; /* callee */
    gm_tddtjfggdat0d l14; /* sig */
    gm_tddtjfggdat0d l15; /* sig */
    gm_trqfvmjtt4sij l16; /* yields */
    gm_trqfvmjtt4sij l27; /* pre */
    gm_trqfvmjtt4sij l32; /* params */
    gm_t0d02142k4tu8 l35; /* pat_ast */
    gm_trqfvmjtt4sij l36; /* args */
    gm_t0d02142k4tu8 l39; /* p */
    gm_trqfvmjtt4sij l40; /* binds */
    gm_trqfvmjtt4sij l43; /* arms */
    gm_tk9n3vlila9qg l45; /* frame */
    gm_trqfvmjtt4sij l46; /* ids */
    gm_trqfvmjtt4sij l47; /* clauses */
    gm_trqfvmjtt4sij l48; /* handlers */
    void * ret;
};
struct gm_fr_format_mpnc7rc {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t04dp7b {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_hoist_args_po0rnjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_hoist_iter_value_cs229ip {
    gm_frame h;
    gm_t0mfir12leg2g l3; /* place */
    gm_tc3oht4ksth9d l7; /* b */
    gm_tc3oht4ksth9d l11; /* b */
    gm_tc3oht4ksth9d l14; /* b */
    gm_tc3oht4ksth9d l15; /* i */
    gm_tc3oht4ksth9d l17; /* v */
    gm_tc3oht4ksth9d l19; /* o */
    gm_tc3oht4ksth9d l20; /* p */
    gm_tc3oht4ksth9d l21; /* expr */
    gm_trqfvmjtt4sij l26; /* pre */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_hoist_iterator_call_5rmgsce {
    gm_frame h;
    gm_tc3oht4ksth9d l5; /* new_callee */
    gm_tc3oht4ksth9d l8; /* b */
    gm_trqfvmjtt4sij l9; /* out */
    gm_t7te2tdpebbt4 l12; /* a */
    gm_tc3oht4ksth9d l13; /* e */
    gm_tc3oht4ksth9d l15; /* e */
    gm_tc3oht4ksth9d l16; /* e */
    gm_t22mtv3sd9prf l17; /* kind */
    gm_tc3oht4ksth9d ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_iter_stable_k9c87oo {
    gm_frame h;
    gm_t7te2tdpebbt4 l13; /* a */
    gm_tc3oht4ksth9d l14; /* x */
    gm_tc3oht4ksth9d l15; /* x */
    uint8_t ret;
};
struct gm_fr_iterator_arg_r0if28f {
    gm_frame h;
    gm_tddtjfggdat0d l3; /* sig */
    gm_tc3oht4ksth9d l11; /* call */
    gm_tlivgedjs5ps3 l12; /* fsig */
    gm_t0mfir12leg2g ret;
};
struct gm_fr_iterator_call_as_value_00i5mch {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_lift_temps_7c6gk6l {
    gm_frame h;
    void * l0; /* cx */
    void * l2; /* pre */
    gm_b0_1 ret;
};
struct gm_fr_list_084fcd6 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_58nr42v_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6l26q4a {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6oos54v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ir0h3tc_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_p0pjtrh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ttksl9g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_local_c3kjspo {
    gm_frame h;
    void * ret;
};
struct gm_fr_not_an_iterator_au10b3l {
    gm_frame h;
    void * ret;
};
struct gm_fr_place_iter_pre_qn5c5b5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_pop_8sodrk9 {
    gm_frame h;
    gm_td8hfgklo8dil ret;
};
struct gm_fr_pop_dt2bd2s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* x */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_pop_ruvva9t {
    gm_frame h;
    gm_t8enl5g7b22jb l2; /* x */
    gm_te5gbhu3us3k4 ret;
};
struct gm_fr_pop_scope_g5j7o4e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_0mk7udh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bu0mlkm {
    gm_frame h;
    gm_t1cs2fjnrqap1 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d1r4h19 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d6c76ud_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tsf6i2agfnpva l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_f5fsh94 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_jd7smj6 {
    gm_frame h;
    gm_tl93vjpcesj48 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_jis5lhu {
    gm_frame h;
    gm_t7te2tdpebbt4 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l74t9rq {
    gm_frame h;
    gm_tnrgict8sthcv l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o8u01sh {
    gm_frame h;
    gm_t5v0c8ia6a5mo l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ucgmlo2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_08uc6h5 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_61fb705_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_7sj41hq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8l7j2k6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h08har5_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h9tbucc_2 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k4j2gj3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_kqobl40 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_m244brh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_s9eh08e {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_sqitcho {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_t5pgl2t {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_v0p8ic9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
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
struct gm_fr_temp_ilbvj8q {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_trim_fdrfe6i {
    gm_frame h;
    gm_b16_8 ret;
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
struct gm_fr_truncate_ua7l21h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_usize_ty_qnrd30f {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqlv5i3f25jk(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_bind_temp_sa5rcbr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_block_expected_bv4ukm2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void * gm_f_block_of_2gjl2i1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) void * gm_f_block_typed_e7svg7n(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, gm_b16_8 a1, void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) int32_t gm_f_cmp_ref_8ntrqll(void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_counting_loop_s4u2ugd(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, uint32_t a3, gm_t88rcucg1b7uk a4, void * a5, void * a6);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_3l5mhoj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_binds_rk1ncsj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_denotes_type_jgfet92(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_tnpifv20irnph gm_f_empty_effects_r0iv3pv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3jt8vgk(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73v4ttb(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_92arfe8(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ilis91k(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ito0pap(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jvoelso(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qlbp545(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_vj3ckdj_2(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_enter_fn_vo5kel1(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2, gm_tnpifv20irnph a3);
__attribute__((aligned(4))) void * gm_f_enumerate_loop_5f6m89f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_exit_fn_kj6vbu8(void *gm_out, void * a0);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_43vto3v(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_find_nab29co(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t9suqla(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
__attribute__((aligned(4))) void * gm_f_for_each_value_5qklbg7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_for_iterator_cc5qo7n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_forget_label_breaks_vl4mn72(void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_format_lo2svpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_mpnc7rc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rqlkk21(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_format_t04dp7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_hoist_args_po0rnjr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_hoist_iter_value_cs229ip(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_hoist_iterator_call_5rmgsce(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_irrefutable_lpkmeii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_iter_stable_k9c87oo(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_iterator_arg_r0if28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_iterator_call_as_value_00i5mch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_lift_temps_7c6gk6l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) void gm_f_list_084fcd6(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6l26q4a(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ir0h3tc_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p0pjtrh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_local_c3kjspo(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_cnt7p5u(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint32_t gm_f_new_label_3207tcb(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2, gm_t1qr0p6kln6tf a3);
__attribute__((aligned(4))) uint32_t gm_f_new_local_003h9es(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) uint32_t gm_f_new_nested_fn_r5u4ioo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_tl874t2atnlh3 a3, uint64_t a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, gm_tnpifv20irnph a7);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void * gm_f_not_an_iterator_au10b3l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void * gm_f_pattern_5gn6kid(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, void * a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_place_iter_pre_qn5c5b5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint64_t a3, void * a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bu0mlkm(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d1r4h19(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d6c76ud_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f5fsh94(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jd7smj6(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ucgmlo2(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_08uc6h5(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8l7j2k6(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h08har5_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k4j2gj3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kqobl40(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_m244brh(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_s9eh08e(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_sqitcho(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t5pgl2t(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_remove_vrfdutr(void *gm_out, void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_rfind_ai5s2js(gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint32_t gm_f_temp_ilbvj8q(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_end_6sjv6rg(gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_fdrfe6i(gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_start_179crak(gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_ua7l21h(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_k0gn7sac5pt[40] __attribute__((aligned(8)));
extern uint8_t gm_k1umgjqrbkf[40] __attribute__((aligned(8)));
extern uint8_t gm_k8gk4puesad[40] __attribute__((aligned(8)));
extern uint8_t gm_k8upcoeidcs[40] __attribute__((aligned(8)));
extern uint8_t gm_kb8poqrtf7g[40] __attribute__((aligned(8)));
extern uint8_t gm_kbng43moaud[40] __attribute__((aligned(8)));
extern uint8_t gm_kl5b02juaoa[40] __attribute__((aligned(8)));
extern uint8_t gm_kmnip3ksasl[40] __attribute__((aligned(8)));
extern uint8_t gm_kq6mn01fhkq[40] __attribute__((aligned(8)));
extern uint8_t gm_kq6te48ps2f[40] __attribute__((aligned(8)));
extern uint8_t gm_krlms0mdefs[40] __attribute__((aligned(8)));
extern uint8_t gm_kukq3ep6ok2[40] __attribute__((aligned(8)));
extern const uint8_t gm_s4m4ttv1faeb1[175];
extern const uint8_t gm_sa39b9bt4jpna[36];
extern const uint8_t gm_sb8airrqr74u5[73];
extern const uint8_t gm_sbol12a8gjr17[9];
extern const uint8_t gm_sbr6raqk787l2[10];
extern const uint8_t gm_sc0gdocdik6sv[7];
extern const uint8_t gm_sfvnlkgl40moh[72];
extern const uint8_t gm_shv1k5m05egbe[6];
extern const uint8_t gm_sigq4ahgd0l2a[64];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_so4k586t2v79t[64];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcoimgd0jo62fr[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_ss7qhfi2s00na[11];
extern const gm_tmpl gm_tplfb67h0nat3pe;
extern const gm_tmpl gm_tplqtueduh1r3n9;
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
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eqlv5i3f25jk(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
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
inline __attribute__((aligned(4))) void * gm_f_block_q09sn1p(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_block_q09sn1p F;
    void * gm_l0; /* cx */
    void * gm_l1; /* b */
    uint8_t gm_l2; /* value */
    GM_STACK_CHECK(&F, 66u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t2 = gm_l0;
    void * t3 = gm_l1;
    uint8_t t4 = gm_l2;
    gm_tkn42u7na3t81 t5;
    memset(&t5, 0, sizeof t5);
    void * t6 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t2, t3, t4, t5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_c3qnk0a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_c3qnk0a F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_fa4sjvq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_fa4sjvq F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 144)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ilis91k(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ilis91k F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jvoelso(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jvoelso F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_qlbp545(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_qlbp545 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r9g5q6n F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 120)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_43vto3v(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_43vto3v F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_s9eh08e(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 40);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 40)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_nkql463(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_nkql463 F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_h08har5_2(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 32);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 32)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t9suqla(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_t9suqla F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_lift_temps_7c6gk6l *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_lift_temps_7c6gk6l *)((&F.h)->up))->l2));
    gm_b0_1 t5 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) void gm_f_format_mpnc7rc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_mpnc7rc F;
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2;
    GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s4m4ttv1faeb1));
    GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(174));
    gm_t0uuhaljsf410 t3 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
    gm_f_format_rqlkk21((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, &t3, ((void *)&gm_tplqtueduh1r3n9), t2);
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
inline __attribute__((aligned(4))) void gm_f_format_t04dp7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_t04dp7b F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t4ce0jd3tdro4 t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbol12a8gjr17));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
    gm_f_format_lo2svpj((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tplfb67h0nat3pe), t3);
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_3v2gjd4_2();
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
inline __attribute__((aligned(4))) void gm_f_list_6l26q4a(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6l26q4a F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_ilis91k();
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
inline __attribute__((aligned(4))) void gm_f_list_6oos54v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6oos54v F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_fa4sjvq();
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
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_af74s0q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_he774nl();
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
inline __attribute__((aligned(4))) void gm_f_list_p0pjtrh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_p0pjtrh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_qlbp545();
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
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ttksl9g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_r9g5q6n();
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
inline __attribute__((aligned(4))) void * gm_f_local_c3kjspo(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_local_c3kjspo F;
    uint32_t gm_l0; /* id */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t88rcucg1b7uk t2 = gm_l1;
    uint32_t t3 = gm_l0;
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&((struct gm_fr_enumerate_loop_5f6m89f *)((&F.h)->up))->l12));
    gm_to0f5qn75cgg8 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)15ULL));
    GM_ST(uint32_t, ((uint8_t *)&t5 + 8), t3);
    void * t6 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t2, &t5, t4);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_8sodrk9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t66j1im9ip128 gm_l2; /* x */
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
        memset(&(*(gm_td8hfgklo8dil *)gm_out), 0, sizeof (*(gm_td8hfgklo8dil *)gm_out));
        goto gm_fdone;
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, ((uint8_t *)t6));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t6) + 8));
        uint64_t t9 = gm_l1;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t12 = r10;
        if ((uintptr_t)t12 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t12, t8, gm_srcrgc9bkquf6nr, 133, 10);
        memmove(((uint8_t *)&gm_l2), (((uint8_t *)t7) + (uintptr_t)t12 * 32), 32);
    }
    void * t13 = gm_l0;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t14) + 8));
    uint64_t t17 = gm_l1;
    uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
    if (o19) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t20 = r18;
    uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t22 = (uintptr_t)t20;
    if (t21 > t22 || t22 > (uintptr_t)t16) gm_trap_slice(t21, t22, t16, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t23 = ((uintptr_t)t20) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t15 + (uintptr_t)((uint64_t)0ULL) * 32)));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
    GM_ST(gm_b16_8, ((uint8_t *)t13), t24);
    {
        gm_t66j1im9ip128 t25 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)&gm_l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 36, 0);
        GM_ST(gm_t66j1im9ip128, ((uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out)), t25);
        ((uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out))[32] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_dt2bd2s F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
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
        memset(&(*(gm_tqetrgo0q24b5 *)gm_out), 0, sizeof (*(gm_tqetrgo0q24b5 *)gm_out));
        goto gm_fdone;
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, ((uint8_t *)t6));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t6) + 8));
        uint64_t t9 = gm_l1;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t12 = r10;
        if ((uintptr_t)t12 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t12, t8, gm_srcrgc9bkquf6nr, 133, 10);
        memmove(((uint8_t *)&F.l2), (((uint8_t *)t7) + (uintptr_t)t12 * 40), 40);
    }
    void * t13 = gm_l0;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t14) + 8));
    uint64_t t17 = gm_l1;
    uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
    if (o19) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t20 = r18;
    uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t22 = (uintptr_t)t20;
    if (t21 > t22 || t22 > (uintptr_t)t16) gm_trap_slice(t21, t22, t16, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t23 = ((uintptr_t)t20) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t15 + (uintptr_t)((uint64_t)0ULL) * 40)));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
    GM_ST(gm_b16_8, ((uint8_t *)t13), t24);
    {
        gm_trqfvmjtt4sij t25 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 44, 0);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out)), t25);
        ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out))[40] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_ruvva9t(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_ruvva9t F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
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
        memset(&(*(gm_te5gbhu3us3k4 *)gm_out), 0, sizeof (*(gm_te5gbhu3us3k4 *)gm_out));
        goto gm_fdone;
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, ((uint8_t *)t6));
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)t6) + 8));
        uint64_t t9 = gm_l1;
        uint64_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint64_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t12 = r10;
        if ((uintptr_t)t12 >= (uintptr_t)(t8)) gm_trap_index((uintptr_t)t12, t8, gm_srcrgc9bkquf6nr, 133, 10);
        memmove(((uint8_t *)&F.l2), (((uint8_t *)t7) + (uintptr_t)t12 * 48), 48);
    }
    void * t13 = gm_l0;
    void * t14 = gm_l0;
    void * t15 = GM_LD(void *, ((uint8_t *)t14));
    uint64_t t16 = GM_LD(uint64_t, (((uint8_t *)t14) + 8));
    uint64_t t17 = gm_l1;
    uint64_t r18; bool o19 = __builtin_sub_overflow(t17, ((uint64_t)1ULL), &r18);
    if (o19) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
    uint64_t t20 = r18;
    uint64_t t21 = (uintptr_t)((uint64_t)0ULL);
    uint64_t t22 = (uintptr_t)t20;
    if (t21 > t22 || t22 > (uintptr_t)t16) gm_trap_slice(t21, t22, t16, gm_srcrgc9bkquf6nr, 134, 15);
    uint64_t t23 = ((uintptr_t)t20) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t24;
    GM_ST(void *, (uint8_t *)&t24, (void *)(((uint8_t *)t15 + (uintptr_t)((uint64_t)0ULL) * 48)));
    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(t23));
    GM_ST(gm_b16_8, ((uint8_t *)t13), t24);
    {
        gm_t8enl5g7b22jb t25 = GM_LD(gm_t8enl5g7b22jb, ((uint8_t *)&F.l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out) + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out) + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out) + 52, 0);
        GM_ST(gm_t8enl5g7b22jb, ((uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out)), t25);
        ((uint8_t *)&(*(gm_te5gbhu3us3k4 *)gm_out))[48] = 1;
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_scope_g5j7o4e F;
    void * gm_l0; /* cx */
    gm_t66j1im9ip128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 201u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_td8hfgklo8dil gm_l2;
    void * t4 = gm_l0;
    void * t5 = (void *)(((uint8_t *)t4) + 56);
    gm_tqetrgo0q24b5 t6;
    gm_f_pop_dt2bd2s((void *)&t6, t5);
    void * t7 = gm_l0;
    void * t8 = (void *)(((uint8_t *)t7) + 616);
    gm_t9805qtonbsag t9 = gm_f_pop_570g79n(t8);
    {
        {
            {
                void * t10 = gm_l0;
                void * t11 = (void *)(((uint8_t *)t10) + 656);
                gm_f_pop_8sodrk9((void *)&gm_l2, t11);
            }
            uint8_t t12 = (uint8_t)((((uint8_t *)&gm_l2))[32] != 0);
            if (!t12) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&gm_l1), ((uint8_t *)&gm_l2), 32);
            }
            {
                void * t13 = gm_l0;
                void * t14 = GM_LD(void *, ((uint8_t *)t13));
                gm_f_restore_scope_edqq1v8(t14, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l1)));
            }
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_2(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_2 F;
    void * gm_l0; /* l */
    gm_tbbqefuhv2r8h gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_3v2gjd4_2();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_h9tbucc_2(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bu0mlkm(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bu0mlkm F;
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
        gm_b16_8 t9 = gm_f_empty_slice_c3qnk0a();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8l7j2k6(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_f5fsh94(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_f5fsh94 F;
    void * gm_l0; /* l */
    gm_t4lnf71mruqpt gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_qlbp545();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_sqitcho(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jd7smj6(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_jd7smj6 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_jvoelso();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_kqobl40(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_jis5lhu(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_jis5lhu F;
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
        gm_b16_8 t9 = gm_f_empty_slice_fa4sjvq();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_08uc6h5(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 144), ((uint8_t *)&F.l1), 144);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 144)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l74t9rq F;
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
        gm_b16_8 t9 = gm_f_empty_slice_r9g5q6n();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_t5pgl2t(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 120), ((uint8_t *)&F.l1), 120);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 120)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_o8u01sh F;
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
        gm_b16_8 t9 = gm_f_empty_slice_he774nl();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_7sj41hq(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 56), ((uint8_t *)&F.l1), 56);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 56)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_08uc6h5(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_08uc6h5 F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)144ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_7sj41hq F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)56ULL), ((uint64_t)8ULL));
    F.ret = t8;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8l7j2k6(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8l7j2k6 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_2(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h9tbucc_2 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_kqobl40(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_kqobl40 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_sqitcho(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_sqitcho F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t5pgl2t(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_t5pgl2t F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)120ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) void gm_f_truncate_ua7l21h(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_ua7l21h F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 40)));
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_usize_ty_qnrd30f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_usize_ty_qnrd30f F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    gm_t53at80pfdus3 t3;
    memset(&t3, 0, sizeof t3);
    GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)1ULL));
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
__attribute__((aligned(4))) void * gm_f_enumerate_loop_5f6m89f(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4) {
    struct gm_fr_enumerate_loop_5f6m89f F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    void * gm_l2; /* call */
    gm_b16_8 gm_l3; /* bound */
    gm_t88rcucg1b7uk gm_l4; /* elem */
    gm_b16_8 gm_l6; /* args */
    uint32_t gm_l7; /* fid */
    void * gm_l8; /* sig */
    void * gm_l9; /* tys */
    gm_t88rcucg1b7uk gm_l10; /* sty */
    gm_t88rcucg1b7uk gm_l11; /* x_ty */
    gm_t88rcucg1b7uk gm_l13; /* usize_t */
    uint32_t gm_l14; /* s */
    uint32_t gm_l15; /* i */
    gm_closure gm_l16; /* local */
    void * gm_l18; /* item */
    void * gm_l19; /* lp */
    GM_STACK_CHECK(&F, 1800u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    uint8_t gm_l21;
    uint8_t gm_l22;
    gm_t53at80pfdus3 gm_l23;
    gm_tftb1odc739t5 gm_l24;
    gm_tftb1odc739t5 gm_l25;
    gm_tftb1odc739t5 gm_l26;
    gm_tftb1odc739t5 gm_l27;
    gm_tftb1odc739t5 gm_l28;
    gm_tftb1odc739t5 gm_l29;
    {
        {
            void * t8 = gm_l2;
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)34ULL));
            if (!t10) {
                goto gm_lb2;
            }
            memmove(((uint8_t *)&F.l5), ((((uint8_t *)t8) + 8) + 8), 40);
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((((uint8_t *)t8) + 8) + 8) + 40));
            gm_l6 = t11;
            {
                {
                    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 0));
                    uint8_t t13 = (uint8_t)(t12 == ((uint8_t)0ULL));
                    if (!t13) {
                        goto gm_lb4;
                    }
                    uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)&F.l5) + 8));
                    gm_l7 = t14;
                    void * t15 = gm_l0;
                    void * t16 = GM_LD(void *, ((uint8_t *)t15));
                    void * t17 = GM_LD(void *, (((uint8_t *)t16) + 344));
                    uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t16) + 344) + 8));
                    uint32_t t19 = gm_l7;
                    uint64_t t20 = (uint64_t)t19;
                    if ((uintptr_t)t20 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t20, t18, gm_srcoimgd0jo62fr, 123, 13);
                    void * t21 = (void *)(((uint8_t *)t17) + (uintptr_t)t20 * 136);
                    gm_l8 = t21;
                    void * t22 = gm_l8;
                    gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)t22));
                    gm_b16_8 t24;
                    GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sbr6raqk787l2));
                    GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(9));
                    uint8_t t25 = (!gm_eq0iqiubfm7e(((uint8_t *)&t23), ((uint8_t *)&t24)));
                    if (t25) {
                        gm_l22 = ((uint8_t)1);
                    } else {
                        void * t26 = gm_l0;
                        void * t27 = GM_LD(void *, ((uint8_t *)t26));
                        void * t28 = GM_LD(void *, ((uint8_t *)t27));
                        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 200));
                        uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t28) + 200) + 8));
                        void * t31 = gm_l8;
                        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)t31) + 32));
                        if ((uintptr_t)t32 >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)t32, t30, gm_srcoimgd0jo62fr, 124, 43);
                        uint8_t t33 = GM_LD(uint8_t, ((((uint8_t *)t29) + (uintptr_t)t32 * 184) + 112));
                        uint8_t t34 = (uint8_t)!t33;
                        gm_l22 = t34;
                    }
                    uint8_t t35 = gm_l22;
                    if (t35) {
                        gm_l21 = ((uint8_t)1);
                    } else {
                        uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint8_t t37 = (!(t36 == ((uint64_t)1ULL)));
                        gm_l21 = t37;
                    }
                    uint8_t t38 = gm_l21;
                    if (t38) {
                        F.ret = ((void *)0);
                        goto gm_fret;
                    }
                    void * t39 = gm_l0;
                    void * t40 = GM_LD(void *, ((uint8_t *)t39));
                    void * t41 = (void *)(((uint8_t *)t40) + 8);
                    gm_l9 = t41;
                    void * t42 = gm_l0;
                    void * t43 = (void *)(((uint8_t *)t42) + 96);
                    void * t44 = gm_l0;
                    void * t45 = GM_LD(void *, ((uint8_t *)t44));
                    void * t46 = GM_LD(void *, ((uint8_t *)&gm_l6));
                    uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t47, gm_srcoimgd0jo62fr, 126, 35);
                    gm_t88rcucg1b7uk t48 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t46) + (uintptr_t)((uint64_t)0ULL) * 88));
                    gm_t88rcucg1b7uk t49 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t43, t45, t48);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t49;
                    {
                        {
                            {
                                void * t50 = gm_l9;
                                gm_t88rcucg1b7uk t51 = gm_l10;
                                gm_f_kind_jcnt4hm((void *)&gm_l23, t50, GM_CONV(gm_csguankko4lmhd, t51));
                            }
                            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l23) + 0));
                            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)10ULL));
                            if (!t53) {
                                goto gm_lb6;
                            }
                            gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&gm_l23) + 8) + 4));
                            gm_l11 = t54;
                            void * t55 = gm_l1;
                            gm_tl874t2atnlh3 t56 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t55) + 104));
                            F.l12 = t56;
                            void * t57 = gm_l9;
                            gm_t88rcucg1b7uk t58 = gm_f_usize_ty_qnrd30f((void *)NULL, cx, NULL, t57);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l13 = t58;
                            void * t59 = gm_l0;
                            gm_t88rcucg1b7uk t60 = gm_l10;
                            gm_tl874t2atnlh3 t61 = F.l12;
                            uint32_t t62 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t59, t60, t61);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l14 = t62;
                            void * t63 = gm_l0;
                            gm_t88rcucg1b7uk t64 = gm_l13;
                            gm_tl874t2atnlh3 t65 = F.l12;
                            uint32_t t66 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t63, t64, t65);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l15 = t66;
                            gm_closure t67 = ((gm_closure){(void *)gm_f_local_c3kjspo, (void *)&F.h, cx});
                            gm_l16 = t67;
                            {
                                gm_trqfvmjtt4sij t68;
                                gm_f_list_vp1djv1_2((void *)&t68, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l17 = t68;
                            }
                            {
                                void * t69 = (void *)((uint8_t *)&F.l17);
                                gm_closure t70 = gm_l16;
                                uint32_t t71 = gm_l15;
                                gm_t88rcucg1b7uk t72 = gm_l13;
                                gm_closure t73 = t70;
                                gm_node *ccx74 = t73.cx;
                                gm_node cn75; { gm_node *f = gm_find(cx, 587034433); if (f) { cn75 = *f; cn75.next = ccx74; ccx74 = &cn75; } }
                                void * t76 = t73.code;
                                void * t77;
                                if (GM_PTR_TAGGED(t76)) {
                                    t77 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t76))(t71, GM_CONV(gm_csguankko4lmhd, t72));
                                } else {
                                    t77 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t76)(t73.env, ccx74, NULL, t71, t72);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t78 = gm_f_push_d943vgp_2(t69, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t77)));
                                gm_l24 = t78;
                            }
                            uint8_t t79 = (uint8_t)!((((uint8_t *)&gm_l24))[0] != 0);
                            if (!t79) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l24) + 1, gm_srcoimgd0jo62fr, 134, 5);
                            {
                                void * t80 = (void *)((uint8_t *)&F.l17);
                                gm_t88rcucg1b7uk t81 = gm_l11;
                                gm_closure t82 = gm_l16;
                                uint32_t t83 = gm_l14;
                                gm_t88rcucg1b7uk t84 = gm_l10;
                                gm_closure t85 = t82;
                                gm_node *ccx86 = t85.cx;
                                gm_node cn87; { gm_node *f = gm_find(cx, 587034433); if (f) { cn87 = *f; cn87.next = ccx86; ccx86 = &cn87; } }
                                void * t88 = t85.code;
                                void * t89;
                                if (GM_PTR_TAGGED(t88)) {
                                    t89 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t88))(t83, GM_CONV(gm_csguankko4lmhd, t84));
                                } else {
                                    t89 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t88)(t85.env, ccx86, NULL, t83, t84);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_closure t90 = gm_l16;
                                uint32_t t91 = gm_l15;
                                gm_t88rcucg1b7uk t92 = gm_l13;
                                gm_closure t93 = t90;
                                gm_node *ccx94 = t93.cx;
                                gm_node cn95; { gm_node *f = gm_find(cx, 587034433); if (f) { cn95 = *f; cn95.next = ccx94; ccx94 = &cn95; } }
                                void * t96 = t93.code;
                                void * t97;
                                if (GM_PTR_TAGGED(t96)) {
                                    t97 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t96))(t91, GM_CONV(gm_csguankko4lmhd, t92));
                                } else {
                                    t97 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t96)(t93.env, ccx94, NULL, t91, t92);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tlcl4u558l6p5 t98;
                                GM_ST(uint8_t, (uint8_t *)&t98 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t98 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t98 + 20, 0);
                                GM_ST(void *, ((uint8_t *)&t98 + 0), t89);
                                GM_ST(void *, ((uint8_t *)&t98 + 8), t97);
                                GM_ST(uint8_t, ((uint8_t *)&t98 + 16), ((uint8_t)0));
                                gm_tl874t2atnlh3 t99 = F.l12;
                                gm_to0f5qn75cgg8 t100;
                                memset(&t100, 0, sizeof t100);
                                GM_ST(uint8_t, ((uint8_t *)&t100 + 0), ((uint8_t)20ULL));
                                GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t100 + 8), t98);
                                void * t101 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t81, &t100, t99);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t102 = gm_f_push_d943vgp_2(t80, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t101)));
                                gm_l25 = t102;
                            }
                            uint8_t t103 = (uint8_t)!((((uint8_t *)&gm_l25))[0] != 0);
                            if (!t103) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l25) + 1, gm_srcoimgd0jo62fr, 135, 5);
                            {
                                gm_t88rcucg1b7uk t104 = gm_l4;
                                gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&F.l17));
                                gm_tl874t2atnlh3 t106 = F.l12;
                                gm_to0f5qn75cgg8 t107;
                                memset(&t107, 0, sizeof t107);
                                GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)35ULL));
                                GM_ST(gm_b16_8, ((uint8_t *)&t107 + 8), t105);
                                void * t108 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t104, &t107, t106);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l18 = t108;
                            }
                            {
                                void * t109 = gm_l0;
                                void * t110 = gm_l1;
                                gm_t88rcucg1b7uk t111 = gm_l4;
                                uint32_t t112 = gm_l15;
                                gm_t88rcucg1b7uk t113 = gm_l13;
                                gm_t88rcucg1b7uk t114 = gm_l13;
                                gm_closure t115 = gm_l16;
                                uint32_t t116 = gm_l14;
                                gm_t88rcucg1b7uk t117 = gm_l10;
                                gm_closure t118 = t115;
                                gm_node *ccx119 = t118.cx;
                                gm_node cn120; { gm_node *f = gm_find(cx, 587034433); if (f) { cn120 = *f; cn120.next = ccx119; ccx119 = &cn120; } }
                                void * t121 = t118.code;
                                void * t122;
                                if (GM_PTR_TAGGED(t121)) {
                                    t122 = ((void * (*)(uint32_t, gm_csguankko4lmhd))GM_PTR_UNTAG(t121))(t116, GM_CONV(gm_csguankko4lmhd, t117));
                                } else {
                                    t122 = ((void * (*)(void *, gm_node *, gm_node *, uint32_t, gm_t88rcucg1b7uk))t121)(t118.env, ccx119, NULL, t116, t117);
                                }
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tl874t2atnlh3 t123 = F.l12;
                                gm_to0f5qn75cgg8 t124;
                                memset(&t124, 0, sizeof t124);
                                GM_ST(uint8_t, ((uint8_t *)&t124 + 0), ((uint8_t)22ULL));
                                GM_ST(void *, ((uint8_t *)&t124 + 8), t122);
                                void * t125 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t114, &t124, t123);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t126 = gm_l18;
                                void * t127 = gm_f_counting_loop_s4u2ugd((void *)NULL, cx, NULL, t109, t110, t111, t112, t113, t125, t126);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l19 = t127;
                            }
                            {
                                gm_trqfvmjtt4sij t128;
                                gm_f_list_ir0h3tc_2((void *)&t128, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l20 = t128;
                            }
                            void * t129 = (void *)((uint8_t *)&F.l20);
                            gm_b16_8 t130 = gm_l3;
                            gm_b16_8 t131 = GM_LD(gm_b16_8, ((uint8_t *)&t130));
                            gm_tftb1odc739t5 t132 = gm_f_extend_nkql463(t129, t131);
                            gm_l26 = t132;
                            uint8_t t133 = (uint8_t)!((((uint8_t *)&gm_l26))[0] != 0);
                            if (!t133) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l26) + 1, gm_srcoimgd0jo62fr, 139, 5);
                            {
                                void * t134 = (void *)((uint8_t *)&F.l20);
                                uint32_t t135 = gm_l14;
                                void * t136 = GM_LD(void *, ((uint8_t *)&gm_l6));
                                uint64_t t137 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t137)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t137, gm_srcoimgd0jo62fr, 140, 25);
                                void * t138 = (void *)(((uint8_t *)t136) + (uintptr_t)((uint64_t)0ULL) * 88);
                                gm_ttggq7ng4kp3p t139;
                                GM_ST(uint32_t, (uint8_t *)&t139 + 4, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t139 + 0), t135);
                                GM_ST(void *, ((uint8_t *)&t139 + 8), t138);
                                gm_trcmmd12cefb1 t140;
                                GM_ST(uint8_t, (uint8_t *)&t140 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t140 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t140 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t140 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t140 + 8), t139);
                                gm_tftb1odc739t5 t141 = gm_f_push_d6c76ud_2(t134, &t140);
                                gm_l27 = t141;
                            }
                            uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l27))[0] != 0);
                            if (!t142) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l27) + 1, gm_srcoimgd0jo62fr, 140, 5);
                            {
                                void * t143 = (void *)((uint8_t *)&F.l20);
                                uint32_t t144 = gm_l15;
                                gm_t88rcucg1b7uk t145 = gm_l13;
                                gm_tbci5ospk5nnn t146;
                                gm_f_zero_s25l990((void *)&t146, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tl874t2atnlh3 t147 = F.l12;
                                gm_to0f5qn75cgg8 t148;
                                GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t148 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t148 + 56, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t148 + 8), t146);
                                void * t149 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t145, &t148, t147);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_ttggq7ng4kp3p t150;
                                GM_ST(uint32_t, (uint8_t *)&t150 + 4, 0);
                                GM_ST(uint32_t, ((uint8_t *)&t150 + 0), t144);
                                GM_ST(void *, ((uint8_t *)&t150 + 8), t149);
                                gm_trcmmd12cefb1 t151;
                                GM_ST(uint8_t, (uint8_t *)&t151 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t151 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t151 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t151 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t151 + 0), ((uint8_t)0ULL));
                                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t151 + 8), t150);
                                gm_tftb1odc739t5 t152 = gm_f_push_d6c76ud_2(t143, &t151);
                                gm_l28 = t152;
                            }
                            uint8_t t153 = (uint8_t)!((((uint8_t *)&gm_l28))[0] != 0);
                            if (!t153) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l28) + 1, gm_srcoimgd0jo62fr, 141, 5);
                            {
                                void * t154 = (void *)((uint8_t *)&F.l20);
                                void * t155 = gm_l19;
                                gm_trcmmd12cefb1 t156;
                                GM_ST(uint8_t, (uint8_t *)&t156 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t156 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t156 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t156 + 16, 0);
                                GM_ST(uint64_t, (uint8_t *)&t156 + 24, 0);
                                GM_ST(uint8_t, ((uint8_t *)&t156 + 0), ((uint8_t)4ULL));
                                GM_ST(void *, ((uint8_t *)&t156 + 8), t155);
                                gm_tftb1odc739t5 t157 = gm_f_push_d6c76ud_2(t154, &t156);
                                gm_l29 = t157;
                            }
                            uint8_t t158 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                            if (!t158) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 1, gm_srcoimgd0jo62fr, 142, 5);
                            void * t159 = gm_l0;
                            void * t160 = GM_LD(void *, ((uint8_t *)t159));
                            gm_b16_8 t161 = GM_LD(gm_b16_8, ((uint8_t *)&F.l20));
                            gm_tl874t2atnlh3 t162 = F.l12;
                            void * t163 = gm_f_block_of_2gjl2i1((void *)NULL, cx, NULL, t160, t161, t162);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            F.ret = t163;
                            goto gm_fret;
                        }
gm_lb6:;
                        F.ret = ((void *)0);
                        goto gm_fret;
                    }
gm_lb5:;
                }
gm_lb4:;
                F.ret = ((void *)0);
                goto gm_fret;
            }
gm_lb3:;
        }
gm_lb2:;
        F.ret = ((void *)0);
        goto gm_fret;
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t9suqla(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_for_iterator_cc5qo7n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_for_iterator_cc5qo7n F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    gm_tl874t2atnlh3 gm_l2; /* span */
    void * gm_l3; /* tys */
    uint32_t gm_l4; /* yield_eff */
    uint64_t gm_l5; /* start */
    void * gm_l6; /* call */
    gm_b16_8 gm_l7; /* bound */
    void * gm_l8; /* b */
    void * gm_l9; /* t */
    gm_tnpifv20irnph gm_l10; /* effects */
    uint32_t gm_l12; /* id */
    void * gm_l13; /* c */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_tiilmfd16v3fc gm_l19; /* e */
    void * gm_l20; /* e */
    gm_tiilmfd16v3fc gm_l21; /* eref */
    gm_t88rcucg1b7uk gm_l22; /* elem */
    gm_t88rcucg1b7uk gm_l23; /* t */
    uint64_t gm_l24; /* i */
    void * gm_l25; /* e */
    gm_t88rcucg1b7uk gm_l26; /* result_ty */
    gm_th2lqbo77pvsc gm_l28; /* label_name */
    gm_t08olq97rscn3 gm_l29; /* l */
    uint32_t gm_l30; /* label */
    uint32_t gm_l31; /* hid */
    uint32_t gm_l33; /* fn_id */
    uint32_t gm_l34; /* xid */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    void * gm_l41; /* pat */
    void * gm_l42; /* body */
    void * gm_l44; /* m */
    void * gm_l49; /* w */
    GM_STACK_CHECK(&F, 5072u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l50;
    gm_t53at80pfdus3 gm_l51;
    gm_tftb1odc739t5 gm_l52;
    void * gm_l53;
    void * gm_l54;
    gm_tftb1odc739t5 gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_tftb1odc739t5 gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_tftb1odc739t5 gm_l59;
    gm_tftb1odc739t5 gm_l60;
    gm_tftb1odc739t5 gm_l61;
    gm_tftb1odc739t5 gm_l62;
    gm_tftb1odc739t5 gm_l63;
    gm_tftb1odc739t5 gm_l64;
    {
        void * t43 = gm_l1;
        gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t43) + 104));
        gm_l2 = t44;
        void * t45 = gm_l0;
        void * t46 = GM_LD(void *, ((uint8_t *)t45));
        void * t47 = (void *)(((uint8_t *)t46) + 8);
        gm_l3 = t47;
        void * t48 = gm_l0;
        void * t49 = GM_LD(void *, ((uint8_t *)t48));
        uint8_t t50 = (uint8_t)(((((uint8_t *)t49) + 2076))[4] != 0);
        if (t50) {
            uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)t49) + 2076));
            gm_l4 = t51;
        } else {
            void * t52 = gm_l0;
            gm_tl874t2atnlh3 t53 = gm_l2;
            gm_b16_8 t54;
            GM_ST(void *, (uint8_t *)&t54, (void *)(gm_sa39b9bt4jpna));
            GM_ST(uintptr_t, (uint8_t *)&t54 + 8, (uintptr_t)(35));
            gm_b0_1 t55 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t52, t53, t54);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t56 = gm_l0;
            void * t57 = GM_LD(void *, ((uint8_t *)t56));
            gm_tl874t2atnlh3 t58 = gm_l2;
            void * t59 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t57, t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t59;
            goto gm_fret;
        }
        void * t60 = gm_l0;
        uint64_t t61 = GM_LD(uint64_t, ((((uint8_t *)t60) + 192) + 8));
        gm_l5 = t61;
        void * t62 = gm_l0;
        void * t63 = gm_l1;
        void * t64 = GM_LD(void *, (((uint8_t *)t63) + 56));
        gm_tkn42u7na3t81 t65;
        memset(&t65, 0, sizeof t65);
        void * t66 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t62, t64, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t66;
        gm_b16_8 t67 = gm_f_empty_slice_vj3ckdj_2();
        gm_l7 = t67;
        {
            {
                void * t68 = gm_l6;
                uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t68) + 8) + 0));
                uint8_t t70 = (uint8_t)(t69 == ((uint8_t)46ULL));
                if (!t70) {
                    goto gm_lb2;
                }
                void * t71 = GM_LD(void *, ((((uint8_t *)t68) + 8) + 8));
                gm_l8 = t71;
                void * t72 = gm_l8;
                uint8_t t73 = (uint8_t)(GM_LD(void *, (((uint8_t *)t72) + 16)) != 0);
                if (!t73) {
                    goto gm_lb2;
                }
                void * t74 = GM_LD(void *, (((uint8_t *)t72) + 16));
                gm_l9 = t74;
                void * t75 = gm_l9;
                {
                    {
                        uint8_t t76 = GM_LD(uint8_t, ((((uint8_t *)t75) + 8) + 0));
                        uint8_t t77 = (uint8_t)(t76 == ((uint8_t)34ULL));
                        if (!t77) {
                            goto gm_lb4;
                        }
                        gm_l50 = ((uint8_t)1);
                        goto gm_lb3;
                    }
gm_lb4:;
                    {
                        gm_l50 = ((uint8_t)0);
                        goto gm_lb3;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 17, 51);
                }
gm_lb3:;
                uint8_t t78 = gm_l50;
                if (!t78) {
                    goto gm_lb2;
                }
                void * t79 = gm_l8;
                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)t79));
                gm_l7 = t80;
                void * t81 = gm_l9;
                gm_l6 = t81;
                goto gm_lb1;
            }
gm_lb2:;
        }
gm_lb1:;
    }
    {
        gm_tnpifv20irnph t82 = gm_f_empty_effects_r0iv3pv();
        gm_l10 = t82;
        void * t83 = gm_l6;
        {
            {
                uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t83) + 8) + 0));
                uint8_t t85 = (uint8_t)(t84 == ((uint8_t)34ULL));
                if (!t85) {
                    goto gm_lb7;
                }
                memmove(((uint8_t *)&F.l11), ((((uint8_t *)t83) + 8) + 8), 40);
                {
                    {
                        uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                        uint8_t t87 = (uint8_t)(t86 == ((uint8_t)0ULL));
                        if (!t87) {
                            goto gm_lb9;
                        }
                        uint32_t t88 = GM_LD(uint32_t, (((uint8_t *)&F.l11) + 8));
                        gm_l12 = t88;
                        void * t89 = gm_l0;
                        void * t90 = GM_LD(void *, ((uint8_t *)t89));
                        void * t91 = GM_LD(void *, (((uint8_t *)t90) + 344));
                        uint64_t t92 = GM_LD(uint64_t, ((((uint8_t *)t90) + 344) + 8));
                        uint32_t t93 = gm_l12;
                        uint64_t t94 = (uint64_t)t93;
                        if ((uintptr_t)t94 >= (uintptr_t)(t92)) gm_trap_index((uintptr_t)t94, t92, gm_srcoimgd0jo62fr, 24, 36);
                        gm_tnpifv20irnph t95 = GM_LD(gm_tnpifv20irnph, ((((uint8_t *)t91) + (uintptr_t)t94 * 136) + 64));
                        gm_l10 = t95;
                        goto gm_lb8;
                    }
gm_lb9:;
                    {
                        uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&F.l11) + 0));
                        uint8_t t97 = (uint8_t)(t96 == ((uint8_t)2ULL));
                        if (!t97) {
                            goto gm_lb10;
                        }
                        void * t98 = GM_LD(void *, (((uint8_t *)&F.l11) + 8));
                        gm_l13 = t98;
                        void * t99 = gm_l3;
                        void * t100 = gm_l0;
                        void * t101 = (void *)(((uint8_t *)t100) + 96);
                        void * t102 = gm_l0;
                        void * t103 = GM_LD(void *, ((uint8_t *)t102));
                        void * t104 = gm_l13;
                        gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t104));
                        gm_t88rcucg1b7uk t106 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t101, t103, t105);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_f_kind_jcnt4hm((void *)&gm_l51, t99, GM_CONV(gm_csguankko4lmhd, t106));
                        {
                            {
                                {
                                    {
                                        uint8_t t107 = GM_LD(uint8_t, (((uint8_t *)&gm_l51) + 0));
                                        uint8_t t108 = (uint8_t)(t107 == ((uint8_t)15ULL));
                                        if (!t108) {
                                            goto gm_lb14;
                                        }
                                        memmove(((uint8_t *)&F.l14), (((uint8_t *)&gm_l51) + 8), 48);
                                        goto gm_lb13;
                                    }
gm_lb14:;
                                    {
                                        uint8_t t109 = GM_LD(uint8_t, (((uint8_t *)&gm_l51) + 0));
                                        uint8_t t110 = (uint8_t)(t109 == ((uint8_t)16ULL));
                                        if (!t110) {
                                            goto gm_lb15;
                                        }
                                        memmove(((uint8_t *)&F.l14), (((uint8_t *)&gm_l51) + 8), 48);
                                        goto gm_lb13;
                                    }
gm_lb15:;
                                    goto gm_lb12;
                                }
gm_lb13:;
                                gm_tnpifv20irnph t111 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&F.l14) + 24));
                                gm_l10 = t111;
                                goto gm_lb11;
                            }
gm_lb12:;
                            {
                                goto gm_lb11;
                            }
gm_lb16:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 25, 29);
                        }
gm_lb11:;
                        goto gm_lb8;
                    }
gm_lb10:;
                    {
                        void * t112 = gm_l0;
                        void * t113 = gm_l1;
                        void * t114 = gm_l6;
                        void * t115 = gm_f_not_an_iterator_au10b3l((void *)NULL, cx, NULL, t112, t113, t114);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        F.ret = t115;
                        goto gm_fret;
                    }
gm_lb17:;
                    gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 23, 29);
                }
gm_lb8:;
                goto gm_lb6;
            }
gm_lb7:;
            {
                void * t116 = gm_l0;
                void * t117 = gm_l1;
                void * t118 = gm_l6;
                void * t119 = gm_f_not_an_iterator_au10b3l((void *)NULL, cx, NULL, t116, t117, t118);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.ret = t119;
                goto gm_fret;
            }
gm_lb18:;
            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 22, 5);
        }
gm_lb6:;
    }
    {
        gm_trqfvmjtt4sij t120;
        gm_f_list_084fcd6((void *)&t120, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l16 = t120;
        gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
        gm_l17 = t121;
        gm_l18 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t122 = gm_l18;
                uint64_t t123 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                uint8_t t124 = (t122 < t123);
                if (!t124) {
                    goto gm_lb19;
                }
                {
                    void * t125 = GM_LD(void *, ((uint8_t *)&gm_l17));
                    uint64_t t126 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                    uint64_t t127 = gm_l18;
                    {
                        {
                            memmove(((uint8_t *)&gm_l19), (((uint8_t *)t125) + (uintptr_t)t127 * 24), 24);
                            uint32_t t128 = GM_LD(uint32_t, ((uint8_t *)&gm_l19));
                            uint32_t t129 = gm_l4;
                            uint8_t t130 = ((t128 == t129));
                            if (t130) {
                                void * t131 = (void *)((uint8_t *)&F.l16);
                                gm_tftb1odc739t5 t132 = gm_f_push_d1r4h19(t131, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l19)));
                                gm_l52 = t132;
                                uint8_t t133 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                                if (!t133) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_srcoimgd0jo62fr, 35, 33);
                            }
                            goto gm_lb22;
                        }
gm_lb23:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 34, 5);
                    }
gm_lb22:;
                }
gm_lb21:;
                uint64_t t134 = gm_l18;
                uint64_t r135; bool o136 = __builtin_add_overflow(t134, ((uint64_t)1ULL), &r135);
                uint64_t t137 = r135;
                gm_l18 = t137;
            }
        }
gm_lb19:;
        uint64_t t138 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
        uint8_t t139 = ((t138 == ((uint64_t)0ULL)));
        if (t139) {
            {
                {
                    void * t140 = gm_l0;
                    void * t141 = gm_l1;
                    void * t142 = gm_l6;
                    void * t143 = gm_f_for_each_value_5qklbg7((void *)NULL, cx, NULL, t140, t141, t142);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l53 = t143;
                    uint8_t t144 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l53)) != 0);
                    if (!t144) {
                        goto gm_lb25;
                    }
                    void * t145 = GM_LD(void *, ((uint8_t *)&gm_l53));
                    gm_l20 = t145;
                    void * t146 = gm_l20;
                    F.ret = t146;
                    goto gm_fret;
                }
gm_lb25:;
            }
gm_lb24:;
            void * t147 = gm_l0;
            void * t148 = gm_l1;
            void * t149 = GM_LD(void *, (((uint8_t *)t148) + 56));
            gm_tl874t2atnlh3 t150 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t149) + 72));
            gm_b16_8 t151;
            GM_ST(void *, (uint8_t *)&t151, (void *)(gm_sigq4ahgd0l2a));
            GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(63));
            gm_b0_1 t152 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t147, t150, t151);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t153 = gm_l0;
            void * t154 = GM_LD(void *, ((uint8_t *)t153));
            gm_tl874t2atnlh3 t155 = gm_l2;
            void * t156 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t154, t155);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t156;
            goto gm_fret;
        }
        uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
        uint8_t t158 = (t157 > ((uint64_t)1ULL));
        if (t158) {
            void * t159 = gm_l0;
            void * t160 = gm_l1;
            void * t161 = GM_LD(void *, (((uint8_t *)t160) + 56));
            gm_tl874t2atnlh3 t162 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t161) + 72));
            gm_b16_8 t163;
            GM_ST(void *, (uint8_t *)&t163, (void *)(gm_sfvnlkgl40moh));
            GM_ST(uintptr_t, (uint8_t *)&t163 + 8, (uintptr_t)(71));
            gm_b0_1 t164 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t159, t162, t163);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t165 = gm_l0;
            void * t166 = GM_LD(void *, ((uint8_t *)t165));
            gm_tl874t2atnlh3 t167 = gm_l2;
            void * t168 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t166, t167);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t168;
            goto gm_fret;
        }
    }
    {
        void * t169 = GM_LD(void *, ((uint8_t *)&F.l16));
        uint64_t t170 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t170)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t170, gm_srcoimgd0jo62fr, 47, 13);
        memmove(((uint8_t *)&gm_l21), (((uint8_t *)t169) + (uintptr_t)((uint64_t)0ULL) * 24), 24);
        void * t171 = gm_l3;
        gm_t88rcucg1b7uk t172 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t171);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l22 = t172;
        {
            {
                uint64_t t173 = GM_LD(uint64_t, ((((uint8_t *)&gm_l21) + 8) + 8));
                uint8_t t174 = (t173 > ((uint64_t)0ULL));
                if (!t174) {
                    goto gm_lb27;
                }
                void * t175 = GM_LD(void *, (((uint8_t *)&gm_l21) + 8));
                uint64_t t176 = GM_LD(uint64_t, ((((uint8_t *)&gm_l21) + 8) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t176)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t176, gm_srcoimgd0jo62fr, 49, 42);
                uint8_t t177 = GM_LD(uint8_t, ((((uint8_t *)t175) + (uintptr_t)((uint64_t)0ULL) * 40) + 0));
                uint8_t t178 = (uint8_t)(t177 == ((uint8_t)0ULL));
                if (!t178) {
                    goto gm_lb27;
                }
                gm_t88rcucg1b7uk t179 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t175) + (uintptr_t)((uint64_t)0ULL) * 40) + 8));
                gm_l23 = t179;
                gm_t88rcucg1b7uk t180 = gm_l23;
                gm_l22 = t180;
                goto gm_lb26;
            }
gm_lb27:;
        }
gm_lb26:;
        uint64_t t181 = gm_l5;
        gm_l24 = t181;
        {
            for (;;) {
                uint64_t t182 = gm_l24;
                void * t183 = gm_l0;
                uint64_t t184 = GM_LD(uint64_t, ((((uint8_t *)t183) + 192) + 8));
                uint8_t t185 = (t182 < t184);
                if (!t185) {
                    goto gm_lb28;
                }
                {
                    void * t186 = gm_l0;
                    void * t187 = GM_LD(void *, (((uint8_t *)t186) + 192));
                    uint64_t t188 = GM_LD(uint64_t, ((((uint8_t *)t186) + 192) + 8));
                    uint64_t t189 = gm_l24;
                    if ((uintptr_t)t189 >= (uintptr_t)(t188)) gm_trap_index((uintptr_t)t189, t188, gm_srcoimgd0jo62fr, 53, 27);
                    void * t190 = (void *)(((uint8_t *)t187) + (uintptr_t)t189 * 40);
                    void * t191 = (void *)((uint8_t *)&gm_l21);
                    int32_t t192 = gm_f_cmp_ref_8ntrqll(t190, t191);
                    uint8_t t193 = ((t192 == ((int32_t)0LL)));
                    if (t193) {
                        void * t194 = gm_l0;
                        void * t195 = (void *)(((uint8_t *)t194) + 192);
                        uint64_t t196 = gm_l24;
                        gm_tj44oivvogvtl t197;
                        gm_f_remove_vrfdutr((void *)&t197, t195, t196);
                    } else {
                        uint64_t t198 = gm_l24;
                        uint64_t r199; bool o200 = __builtin_add_overflow(t198, ((uint64_t)1ULL), &r199);
                        if (o200) gm_trap("integer overflow in `+`", gm_srcoimgd0jo62fr, 56, 13);
                        uint64_t t201 = r199;
                        gm_l24 = t201;
                    }
                }
gm_lb30:;
            }
        }
gm_lb28:;
        {
            {
                void * t202 = gm_l0;
                void * t203 = gm_l1;
                void * t204 = gm_l6;
                gm_b16_8 t205 = gm_l7;
                gm_t88rcucg1b7uk t206 = gm_l22;
                void * t207 = gm_f_enumerate_loop_5f6m89f((void *)NULL, cx, NULL, t202, t203, t204, t205, t206);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l54 = t207;
                uint8_t t208 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l54)) != 0);
                if (!t208) {
                    goto gm_lb32;
                }
                void * t209 = GM_LD(void *, ((uint8_t *)&gm_l54));
                gm_l25 = t209;
                void * t210 = gm_l25;
                F.ret = t210;
                goto gm_fret;
            }
gm_lb32:;
        }
gm_lb31:;
    }
    {
        void * t211 = gm_l6;
        gm_t88rcucg1b7uk t212 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t211));
        gm_l26 = t212;
        gm_trqfvmjtt4sij t213;
        gm_f_list_ir0h3tc_2((void *)&t213, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l27 = t213;
        void * t214 = (void *)((uint8_t *)&F.l27);
        gm_b16_8 t215 = gm_l7;
        gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
        gm_tftb1odc739t5 t217 = gm_f_extend_nkql463(t214, t216);
        gm_l55 = t217;
        uint8_t t218 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
        if (!t218) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 1, gm_srcoimgd0jo62fr, 63, 5);
        void * t219 = gm_l0;
        void * t220 = gm_l6;
        void * t221 = (void *)((uint8_t *)&F.l27);
        gm_b0_1 t222 = gm_f_hoist_args_po0rnjr((void *)NULL, cx, NULL, t219, t220, t221);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t223 = gm_l0;
        gm_b0_1 t224 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t223);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        memset(&gm_l28, 0, sizeof gm_l28);
        {
            {
                {
                    void * t225 = gm_l1;
                    uint8_t t226 = (uint8_t)((((uint8_t *)t225))[32] != 0);
                    if (!t226) {
                        goto gm_lb34;
                    }
                    memmove(((uint8_t *)&gm_l29), ((uint8_t *)t225), 32);
                }
                {
                    void * t227 = gm_l0;
                    gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l29));
                    gm_tl874t2atnlh3 t229 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l29) + 16));
                    gm_t930q5l38hdap t230;
                    memset(&t230, 0, sizeof t230);
                    GM_ST(uint8_t, ((uint8_t *)&t230 + 0), ((uint8_t)2ULL));
                    gm_b0_1 t231 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t227, t228, &t230, t229);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l29));
                    GM_ST(uint8_t, (uint8_t *)&gm_l28 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&gm_l28 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&gm_l28 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&gm_l28), t232);
                    ((uint8_t *)&gm_l28)[16] = 1;
                }
                goto gm_lb33;
            }
gm_lb34:;
        }
gm_lb33:;
        void * t233 = gm_l0;
        gm_t88rcucg1b7uk t234 = gm_l26;
        gm_t1qr0p6kln6tf t235;
        GM_ST(uint8_t, (uint8_t *)&t235 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t235 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t235 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t235 + 0), ((uint8_t)1ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t235 + 4), t234);
        uint32_t t236 = gm_f_new_label_3207tcb((void *)NULL, cx, NULL, t233, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l28)), ((uint8_t)1), t235);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l30 = t236;
        void * t237 = gm_l0;
        void * t238 = GM_LD(void *, ((uint8_t *)t237));
        uint64_t t239 = GM_LD(uint64_t, ((((uint8_t *)t238) + 504) + 8));
        uint32_t t240 = (uint32_t)t239;
        gm_l31 = t240;
        void * t241 = gm_l0;
        void * t242 = GM_LD(void *, ((uint8_t *)t241));
        void * t243 = (void *)(((uint8_t *)t242) + 504);
        gm_tiilmfd16v3fc t244 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l21));
        gm_b16_8 t245 = gm_f_empty_slice_d914i5u();
        gm_t1cs2fjnrqap1 t246;
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t246 + 0), t244);
        GM_ST(gm_b16_8, ((uint8_t *)&t246 + 24), t245);
        gm_tftb1odc739t5 t247 = gm_f_push_bu0mlkm(t243, &t246);
        gm_l56 = t247;
        uint8_t t248 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
        if (!t248) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_srcoimgd0jo62fr, 73, 5);
        gm_trqfvmjtt4sij t249;
        gm_f_list_af74s0q((void *)&t249, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l32 = t249;
        void * t250 = (void *)((uint8_t *)&F.l32);
        gm_b16_8 t251;
        GM_ST(void *, (uint8_t *)&t251, (void *)(gm_sc0gdocdik6sv));
        GM_ST(uintptr_t, (uint8_t *)&t251 + 8, (uintptr_t)(6));
        gm_t88rcucg1b7uk t252 = gm_l22;
        gm_tl874t2atnlh3 t253 = gm_l2;
        gm_t9805qtonbsag t254;
        memset(&t254, 0, sizeof t254);
        gm_t5v0c8ia6a5mo t255;
        GM_ST(uint8_t, (uint8_t *)&t255 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t255 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t255 + 52, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t255 + 0), t251);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t255 + 16), t252);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t255 + 20), t253);
        GM_ST(void *, ((uint8_t *)&t255 + 32), ((void *)0));
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t255 + 40), t254);
        GM_ST(uint8_t, ((uint8_t *)&t255 + 48), ((uint8_t)0));
        gm_tftb1odc739t5 t256 = gm_f_push_o8u01sh(t250, &t255);
        gm_l57 = t256;
    }
    {
        uint8_t t257 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
        if (!t257) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_srcoimgd0jo62fr, 76, 5);
        void * t258 = gm_l0;
        void * t259 = GM_LD(void *, ((uint8_t *)t258));
        gm_b16_8 t260;
        GM_ST(void *, (uint8_t *)&t260, (void *)(gm_ss7qhfi2s00na));
        GM_ST(uintptr_t, (uint8_t *)&t260 + 8, (uintptr_t)(10));
        gm_tl874t2atnlh3 t261 = gm_l2;
        void * t262 = gm_l0;
        uint64_t t263 = GM_LD(uint64_t, (((uint8_t *)t262) + 8));
        gm_b16_8 t264 = GM_LD(gm_b16_8, ((uint8_t *)&F.l32));
        void * t265 = gm_l3;
        gm_t9qkh0rsooc8d t266;
        GM_ST(uint8_t, ((uint8_t *)&t266 + 0), ((uint8_t)2ULL));
        gm_t88rcucg1b7uk t267 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t265);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t268 = gm_f_empty_effects_r0iv3pv();
        uint32_t t269 = gm_f_new_nested_fn_r5u4ioo((void *)NULL, cx, NULL, t259, t266, t260, t261, t263, t264, t267, t268);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l33 = t269;
        void * t270 = gm_l0;
        uint32_t t271 = gm_l33;
        void * t272 = gm_l3;
        gm_t88rcucg1b7uk t273 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t272);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t274 = gm_f_empty_effects_r0iv3pv();
        gm_b0_1 t275 = gm_f_enter_fn_vo5kel1((void *)NULL, cx, NULL, t270, t271, t273, t274);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t276 = gm_l0;
        uint32_t t277 = gm_l31;
        gm_t9805qtonbsag t278;
        GM_ST(uint8_t, (uint8_t *)&t278 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t278 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t278), t277);
        ((uint8_t *)&t278)[4] = 1;
        gm_t9805qtonbsag t279;
        memset(&t279, 0, sizeof t279);
        gm_tkn42u7na3t81 t280;
        memset(&t280, 0, sizeof t280);
        uint32_t t281 = gm_l30;
        gm_th2lqbo77pvsc t282 = GM_LD(gm_th2lqbo77pvsc, ((uint8_t *)&gm_l28));
        gm_tuanp0jbrlb6j t283;
        GM_ST(uint32_t, (uint8_t *)&t283 + 28, 0);
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t283 + 0), t282);
        GM_ST(uint32_t, ((uint8_t *)&t283 + 24), t281);
        gm_tjvtgq0p0sehv t284;
        GM_ST(uint8_t, (uint8_t *)&t284 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t284 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t284 + 36, 0);
        GM_ST(gm_tuanp0jbrlb6j, ((uint8_t *)&t284), t283);
        ((uint8_t *)&t284)[32] = 1;
        gm_t88rcucg1b7uk t285 = gm_l26;
        gm_tkn42u7na3t81 t286;
        GM_ST(uint8_t, (uint8_t *)&t286 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t286 + 6, 0);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t286), t285);
        ((uint8_t *)&t286)[4] = 1;
        gm_tiilmfd16v3fc t287 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)&gm_l21));
        gm_tqu3m2l3n8mnc t288;
        GM_ST(uint8_t, (uint8_t *)&t288 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t288 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t288 + 28, 0);
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t288), t287);
        ((uint8_t *)&t288)[24] = 1;
        gm_t9qkh0rsooc8d t289;
        GM_ST(uint8_t, ((uint8_t *)&t289 + 0), ((uint8_t)1ULL));
        gm_tnqmlaad6uchl t290;
        GM_ST(uint8_t, (uint8_t *)&t290 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t290 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t290 + 28, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t290 + 0), t289);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t290 + 4), t278);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t290 + 12), t279);
        GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t290 + 20), t280);
        GM_ST(gm_tjvtgq0p0sehv, ((uint8_t *)&t290 + 32), t284);
        GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t290 + 72), t286);
        GM_ST(gm_tqu3m2l3n8mnc, ((uint8_t *)&t290 + 80), t288);
        gm_t403eokj7ifg9 t291;
        GM_ST(uint8_t, (uint8_t *)&t291 + 113, 0);
        GM_ST(uint16_t, (uint8_t *)&t291 + 114, 0);
        GM_ST(uint32_t, (uint8_t *)&t291 + 116, 0);
        GM_ST(gm_tnqmlaad6uchl, ((uint8_t *)&t291), t290);
        ((uint8_t *)&t291)[112] = 1;
        GM_ST(gm_t403eokj7ifg9, (((uint8_t *)t276) + 800), t291);
        void * t292 = gm_l0;
        gm_b16_8 t293;
        GM_ST(void *, (uint8_t *)&t293, (void *)(gm_sc0gdocdik6sv));
        GM_ST(uintptr_t, (uint8_t *)&t293 + 8, (uintptr_t)(6));
        gm_t88rcucg1b7uk t294 = gm_l22;
        gm_tl874t2atnlh3 t295 = gm_l2;
        uint32_t t296 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t292, t293, t294, ((uint8_t)0), t295);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l34 = t296;
        void * t297 = gm_l1;
        void * t298 = GM_LD(void *, (((uint8_t *)t297) + 40));
        uint64_t t299 = GM_LD(uint64_t, ((((uint8_t *)t297) + 40) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t299)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t299, gm_srcoimgd0jo62fr, 82, 16);
        memmove(((uint8_t *)&F.l35), (((uint8_t *)t298) + (uintptr_t)((uint64_t)0ULL) * 64), 64);
        void * t300 = gm_l1;
        uint64_t t301 = GM_LD(uint64_t, ((((uint8_t *)t300) + 40) + 8));
        uint8_t t302 = (!(t301 == ((uint64_t)1ULL)));
        if (t302) {
            {
                gm_trqfvmjtt4sij t303;
                gm_f_list_ttksl9g((void *)&t303, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l36 = t303;
            }
            void * t304 = gm_l1;
            gm_b16_8 t305 = GM_LD(gm_b16_8, (((uint8_t *)t304) + 40));
            gm_l37 = t305;
            gm_l38 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t306 = gm_l38;
                    uint64_t t307 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                    uint8_t t308 = (t306 < t307);
                    if (!t308) {
                        goto gm_lb35;
                    }
                    {
                        void * t309 = GM_LD(void *, ((uint8_t *)&gm_l37));
                        uint64_t t310 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 8));
                        uint64_t t311 = gm_l38;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&F.l39), (((uint8_t *)t309) + (uintptr_t)t311 * 64), 64);
                                }
                                {
                                    void * t312 = (void *)((uint8_t *)&F.l36);
                                    gm_t1s75i08qtptd t313;
                                    memset(&t313, 0, sizeof t313);
                                    gm_tl874t2atnlh3 t314 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l39) + 48));
                                    gm_t0d02142k4tu8 t315 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)&F.l39));
                                    gm_tnrgict8sthcv t316;
                                    GM_ST(uint32_t, (uint8_t *)&t316 + 116, 0);
                                    GM_ST(gm_t1s75i08qtptd, ((uint8_t *)&t316 + 0), t313);
                                    GM_ST(gm_t0d02142k4tu8, ((uint8_t *)&t316 + 40), t315);
                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t316 + 104), t314);
                                    gm_tftb1odc739t5 t317 = gm_f_push_l74t9rq(t312, &t316);
                                    gm_l58 = t317;
                                }
                                uint8_t t318 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                                if (!t318) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_srcoimgd0jo62fr, 85, 27);
                                goto gm_lb38;
                            }
gm_lb39:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 85, 9);
                        }
gm_lb38:;
                    }
gm_lb37:;
                    uint64_t t319 = gm_l38;
                    uint64_t r320; bool o321 = __builtin_add_overflow(t319, ((uint64_t)1ULL), &r320);
                    uint64_t t322 = r320;
                    gm_l38 = t322;
                }
            }
gm_lb35:;
            {
                void * t323 = gm_l1;
                void * t324 = GM_LD(void *, (((uint8_t *)t323) + 40));
                uint64_t t325 = GM_LD(uint64_t, ((((uint8_t *)t323) + 40) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t325)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t325, gm_srcoimgd0jo62fr, 86, 35);
                uint32_t t326 = GM_LD(uint32_t, (((uint8_t *)t324) + (uintptr_t)((uint64_t)0ULL) * 64));
                gm_b16_8 t327 = GM_LD(gm_b16_8, ((uint8_t *)&F.l36));
                void * t328 = gm_l1;
                void * t329 = GM_LD(void *, (((uint8_t *)t328) + 40));
                uint64_t t330 = GM_LD(uint64_t, ((((uint8_t *)t328) + 40) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t330)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t330, gm_srcoimgd0jo62fr, 86, 81);
                gm_tl874t2atnlh3 t331 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t329) + (uintptr_t)((uint64_t)0ULL) * 64) + 48));
                gm_to361lgknsu69 t332;
                GM_ST(uint8_t, (uint8_t *)&t332 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t332 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t332 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t332 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t332 + 32, 0);
                GM_ST(uint8_t, ((uint8_t *)&t332 + 0), ((uint8_t)8ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t332 + 8), t327);
                gm_t0d02142k4tu8 t333;
                GM_ST(uint32_t, (uint8_t *)&t333 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t333 + 60, 0);
                GM_ST(uint32_t, ((uint8_t *)&t333 + 0), t326);
                GM_ST(gm_to361lgknsu69, ((uint8_t *)&t333 + 8), t332);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t333 + 48), t331);
                F.l35 = t333;
            }
        }
    }
    {
        gm_trqfvmjtt4sij t334;
        gm_f_list_6l26q4a((void *)&t334, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l40 = t334;
        void * t335 = gm_l0;
        void * t336 = (void *)((uint8_t *)&F.l35);
        gm_t88rcucg1b7uk t337 = gm_l22;
        void * t338 = (void *)((uint8_t *)&F.l40);
        gm_t9qkh0rsooc8d t339;
        GM_ST(uint8_t, ((uint8_t *)&t339 + 0), ((uint8_t)1ULL));
        void * t340 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t335, t336, t337, t339, t338);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l41 = t340;
        {
            {
                void * t341 = gm_l3;
                gm_t88rcucg1b7uk t342 = gm_l22;
                uint8_t t343 = gm_f_is_error_8k27gd1(t341, GM_CONV(gm_csguankko4lmhd, t342));
                uint8_t t344 = (uint8_t)!t343;
                if (!t344) {
                    goto gm_lb41;
                }
                void * t345 = gm_l0;
                void * t346 = GM_LD(void *, ((uint8_t *)t345));
                void * t347 = gm_l41;
                uint8_t t348 = gm_f_irrefutable_lpkmeii((void *)NULL, cx, NULL, t346, t347);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t349 = (uint8_t)!t348;
                if (!t349) {
                    goto gm_lb41;
                }
                void * t350 = gm_l0;
                gm_tl874t2atnlh3 t351 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l35) + 48));
                gm_b16_8 t352;
                GM_ST(void *, (uint8_t *)&t352, (void *)(gm_so4k586t2v79t));
                GM_ST(uintptr_t, (uint8_t *)&t352 + 8, (uintptr_t)(63));
                gm_b0_1 t353 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t350, t351, t352);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_lb40;
            }
gm_lb41:;
        }
gm_lb40:;
        void * t354 = gm_l0;
        void * t355 = (void *)((uint8_t *)&F.l40);
        gm_b0_1 t356 = gm_f_declare_binds_rk1ncsj((void *)NULL, cx, NULL, t354, t355);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t357 = gm_l0;
        void * t358 = gm_l1;
        void * t359 = (void *)(((uint8_t *)t358) + 64);
        void * t360 = gm_f_block_q09sn1p((void *)NULL, cx, NULL, t357, t359, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l42 = t360;
        gm_trqfvmjtt4sij t361;
        gm_f_list_58nr42v_2((void *)&t361, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l43 = t361;
        void * t362 = (void *)((uint8_t *)&F.l43);
        void * t363 = gm_l41;
        void * t364 = gm_l42;
        gm_tbbqefuhv2r8h t365;
        GM_ST(void *, ((uint8_t *)&t365 + 0), t363);
        GM_ST(void *, ((uint8_t *)&t365 + 8), ((void *)0));
        GM_ST(void *, ((uint8_t *)&t365 + 16), t364);
        gm_tftb1odc739t5 t366 = gm_f_push_bpovr2k_2(t362, &t365);
        gm_l59 = t366;
        uint8_t t367 = (uint8_t)!((((uint8_t *)&gm_l59))[0] != 0);
        if (!t367) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l59) + 1, gm_srcoimgd0jo62fr, 96, 5);
        void * t368 = gm_l3;
        gm_t88rcucg1b7uk t369 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t368);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t370 = gm_l22;
        uint32_t t371 = gm_l34;
        gm_tl874t2atnlh3 t372 = gm_l2;
        gm_to0f5qn75cgg8 t373;
        memset(&t373, 0, sizeof t373);
        GM_ST(uint8_t, ((uint8_t *)&t373 + 0), ((uint8_t)15ULL));
        GM_ST(uint32_t, ((uint8_t *)&t373 + 8), t371);
        void * t374 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t370, &t373, t372);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t375 = GM_LD(gm_b16_8, ((uint8_t *)&F.l43));
        gm_tkaapcii86udk t376;
        GM_ST(void *, ((uint8_t *)&t376 + 0), t374);
        GM_ST(gm_b16_8, ((uint8_t *)&t376 + 8), t375);
        gm_tl874t2atnlh3 t377 = gm_l2;
        gm_to0f5qn75cgg8 t378;
        memset(&t378, 0, sizeof t378);
        GM_ST(uint8_t, ((uint8_t *)&t378 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t378 + 8), t376);
        void * t379 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t369, &t378, t377);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l44 = t379;
        void * t380 = gm_l0;
        gm_f_exit_fn_kj6vbu8((void *)&F.l45, t380);
    }
    {
        void * t381 = gm_l0;
        void * t382 = (void *)(((uint8_t *)t381) + 192);
        gm_b16_8 t383 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 16));
        gm_b16_8 t384 = GM_LD(gm_b16_8, ((uint8_t *)&t383));
        gm_tftb1odc739t5 t385 = gm_f_extend_43vto3v(t382, t384);
        gm_l60 = t385;
        uint8_t t386 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
        if (!t386) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 1, gm_srcoimgd0jo62fr, 99, 5);
        void * t387 = gm_l0;
        void * t388 = GM_LD(void *, ((uint8_t *)t387));
        void * t389 = GM_LD(void *, (((uint8_t *)t388) + 384));
        uint64_t t390 = GM_LD(uint64_t, ((((uint8_t *)t388) + 384) + 8));
        uint32_t t391 = gm_l33;
        uint64_t t392 = (uint64_t)t391;
        if ((uintptr_t)t392 >= (uintptr_t)(t390)) gm_trap_index((uintptr_t)t392, t390, gm_srcoimgd0jo62fr, 100, 5);
        void * t393 = gm_l0;
        gm_t9805qtonbsag t394 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)t393) + 400));
        GM_ST(gm_t9805qtonbsag, ((((uint8_t *)t389) + (uintptr_t)t392 * 64) + 48), t394);
        gm_trqfvmjtt4sij t395;
        gm_f_list_go2qg0b((void *)&t395, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l46 = t395;
        void * t396 = (void *)((uint8_t *)&F.l46);
        uint32_t t397 = gm_l34;
        gm_tftb1odc739t5 t398 = gm_f_push_im9d4ro(t396, t397);
        gm_l61 = t398;
        uint8_t t399 = (uint8_t)!((((uint8_t *)&gm_l61))[0] != 0);
        if (!t399) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l61) + 1, gm_srcoimgd0jo62fr, 102, 5);
        void * t400 = gm_l0;
        void * t401 = (void *)(((uint8_t *)t400) + 704);
        uint32_t t402 = gm_l33;
        gm_b16_8 t403 = GM_LD(gm_b16_8, ((uint8_t *)&F.l46));
        gm_b16_8 t404 = GM_LD(gm_b16_8, ((uint8_t *)&F.l45));
        void * t405 = gm_l44;
        gm_tl93vjpcesj48 t406;
        GM_ST(uint32_t, (uint8_t *)&t406 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t406 + 0), t402);
        GM_ST(gm_b16_8, ((uint8_t *)&t406 + 8), t403);
        GM_ST(gm_b16_8, ((uint8_t *)&t406 + 24), t404);
        GM_ST(void *, ((uint8_t *)&t406 + 40), t405);
        gm_tftb1odc739t5 t407 = gm_f_push_jd7smj6(t401, &t406);
        gm_l62 = t407;
        uint8_t t408 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
        if (!t408) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 1, gm_srcoimgd0jo62fr, 103, 5);
        void * t409 = gm_l0;
        void * t410 = (void *)(((uint8_t *)t409) + 232);
        gm_te5gbhu3us3k4 t411;
        gm_f_pop_ruvva9t((void *)&t411, t410);
        void * t412 = gm_l0;
        uint32_t t413 = gm_l30;
        uint8_t t414 = gm_f_forget_label_breaks_vl4mn72(t412, t413);
        void * t415 = gm_l0;
        gm_f_pop_scope_g5j7o4e(t415);
        gm_trqfvmjtt4sij t416;
        gm_f_list_go2qg0b((void *)&t416, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l47 = t416;
        void * t417 = (void *)((uint8_t *)&F.l47);
        uint32_t t418 = gm_l33;
        gm_tftb1odc739t5 t419 = gm_f_push_im9d4ro(t417, t418);
        gm_l63 = t419;
        uint8_t t420 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
        if (!t420) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 1, gm_srcoimgd0jo62fr, 108, 5);
        void * t421 = gm_l0;
        void * t422 = GM_LD(void *, ((uint8_t *)t421));
        void * t423 = GM_LD(void *, (((uint8_t *)t422) + 504));
        uint64_t t424 = GM_LD(uint64_t, ((((uint8_t *)t422) + 504) + 8));
        uint32_t t425 = gm_l31;
        uint64_t t426 = (uint64_t)t425;
        if ((uintptr_t)t426 >= (uintptr_t)(t424)) gm_trap_index((uintptr_t)t426, t424, gm_srcoimgd0jo62fr, 109, 5);
        gm_b16_8 t427 = GM_LD(gm_b16_8, ((uint8_t *)&F.l47));
        GM_ST(gm_b16_8, ((((uint8_t *)t423) + (uintptr_t)t426 * 40) + 24), t427);
        gm_trqfvmjtt4sij t428;
        gm_f_list_p0pjtrh((void *)&t428, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l48 = t428;
    }
    {
        void * t429 = (void *)((uint8_t *)&F.l48);
        void * t430 = gm_l3;
        uint32_t t431 = gm_l31;
        gm_t53at80pfdus3 t432;
        memset(&t432, 0, sizeof t432);
        GM_ST(uint8_t, ((uint8_t *)&t432 + 0), ((uint8_t)25ULL));
        GM_ST(uint32_t, ((uint8_t *)&t432 + 8), t431);
        gm_t88rcucg1b7uk t433 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t430, &t432);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t434 = gm_l31;
        gm_tl874t2atnlh3 t435 = gm_l2;
        gm_to0f5qn75cgg8 t436;
        memset(&t436, 0, sizeof t436);
        GM_ST(uint8_t, ((uint8_t *)&t436 + 0), ((uint8_t)28ULL));
        GM_ST(uint32_t, ((uint8_t *)&t436 + 8), t434);
        void * t437 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t433, &t436, t435);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t4lnf71mruqpt t438;
        GM_ST(uint8_t, (uint8_t *)&t438 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t438 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t438 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t438 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t438 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t438 + 0), ((uint8_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t438 + 8), t437);
        gm_tftb1odc739t5 t439 = gm_f_push_f5fsh94(t429, &t438);
        gm_l64 = t439;
        uint8_t t440 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
        if (!t440) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_srcoimgd0jo62fr, 111, 5);
        gm_t88rcucg1b7uk t441 = gm_l26;
        uint32_t t442 = gm_l30;
        gm_b16_8 t443 = GM_LD(gm_b16_8, ((uint8_t *)&F.l48));
        void * t444 = gm_l6;
        gm_t710lsrq1m8gk t445;
        GM_ST(uint32_t, (uint8_t *)&t445 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t445 + 0), t442);
        GM_ST(gm_b16_8, ((uint8_t *)&t445 + 8), t443);
        GM_ST(void *, ((uint8_t *)&t445 + 24), ((void *)0));
        GM_ST(void *, ((uint8_t *)&t445 + 32), t444);
        gm_tl874t2atnlh3 t446 = gm_l2;
        gm_to0f5qn75cgg8 t447;
        GM_ST(uint8_t, (uint8_t *)&t447 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t447 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t447 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t447 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t447 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t447 + 0), ((uint8_t)59ULL));
        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t447 + 8), t445);
        void * t448 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t441, &t447, t446);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l49 = t448;
        uint64_t t449 = GM_LD(uint64_t, (((uint8_t *)&F.l27) + 8));
        uint8_t t450 = ((t449 == ((uint64_t)0ULL)));
        if (t450) {
            void * t451 = gm_l49;
            F.ret = t451;
            goto gm_fret;
        }
        gm_t88rcucg1b7uk t452 = gm_l26;
        gm_b16_8 t453 = GM_LD(gm_b16_8, ((uint8_t *)&F.l27));
        void * t454 = gm_l49;
        gm_tl874t2atnlh3 t455 = gm_l2;
        void * t456 = gm_f_block_typed_e7svg7n((void *)NULL, cx, NULL, t452, t453, t454, t455);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = t456;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_hoist_args_po0rnjr(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_hoist_args_po0rnjr F;
    void * gm_l0; /* cx */
    void * gm_l1; /* call */
    void * gm_l2; /* pre */
    gm_b16_8 gm_l3; /* args */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    void * gm_l7; /* a */
    uint32_t gm_l8; /* t */
    void * gm_l9; /* v */
    GM_STACK_CHECK(&F, 513u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint8_t gm_l10;
    gm_tq7s98ihonkju gm_l11;
    gm_tftb1odc739t5 gm_l12;
    {
        {
            void * t14 = gm_l1;
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)34ULL));
            if (!t16) {
                goto gm_lb2;
            }
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((((uint8_t *)t14) + 8) + 8) + 40));
            gm_l3 = t17;
            gm_l5 = ((uint64_t)0ULL);
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            gm_l4 = t18;
            {
                for (;;) {
                    uint64_t t19 = gm_l5;
                    uint64_t t20 = gm_l4;
                    uint8_t t21 = (t19 < t20);
                    if (!t21) {
                        goto gm_lb3;
                    }
                    {
                        {
                            {
                                uint64_t t22 = gm_l5;
                                gm_l6 = t22;
                                void * t23 = GM_LD(void *, ((uint8_t *)&gm_l3));
                                uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                                uint64_t t25 = gm_l6;
                                void * t26 = (void *)(((uint8_t *)t23) + (uintptr_t)t25 * 88);
                                gm_l7 = t26;
                                void * t27 = gm_l7;
                                {
                                    {
                                        uint8_t t28 = GM_LD(uint8_t, ((((uint8_t *)t27) + 8) + 0));
                                        uint8_t t29 = (uint8_t)(t28 == ((uint8_t)0ULL));
                                        if (!t29) {
                                            goto gm_lb9;
                                        }
                                        gm_l10 = ((uint8_t)1);
                                        goto gm_lb8;
                                    }
gm_lb9:;
                                    {
                                        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)t27) + 8) + 0));
                                        uint8_t t31 = (uint8_t)(t30 == ((uint8_t)5ULL));
                                        if (!t31) {
                                            goto gm_lb10;
                                        }
                                        gm_l10 = ((uint8_t)1);
                                        goto gm_lb8;
                                    }
gm_lb10:;
                                    {
                                        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t27) + 8) + 0));
                                        uint8_t t33 = (uint8_t)(t32 == ((uint8_t)4ULL));
                                        if (!t33) {
                                            goto gm_lb11;
                                        }
                                        gm_l10 = ((uint8_t)1);
                                        goto gm_lb8;
                                    }
gm_lb11:;
                                    {
                                        gm_l10 = ((uint8_t)0);
                                        goto gm_lb8;
                                    }
gm_lb12:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 160, 12);
                                }
gm_lb8:;
                                uint8_t t34 = gm_l10;
                                if (t34) {
                                    goto gm_lb5;
                                }
                                void * t35 = gm_l0;
                                void * t36 = gm_l7;
                                void * t37 = gm_l2;
                                gm_b0_1 t38 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t35, t36, t37);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t39 = gm_l0;
                                void * t40 = gm_l7;
                                gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t40));
                                void * t42 = gm_l7;
                                gm_tl874t2atnlh3 t43 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t42) + 72));
                                uint32_t t44 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t39, t41, t43);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l8 = t44;
                                {
                                    void * t45 = gm_l7;
                                    gm_tq7s98ihonkju t46 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t45)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k1umgjqrbkf)));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    gm_l11 = t46;
                                }
                                uint8_t t47 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                if (!t47) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 8, gm_srcoimgd0jo62fr, 166, 14);
                                void * t48 = GM_LD(void *, (((uint8_t *)&gm_l11) + 8));
                                gm_l9 = t48;
                                {
                                    void * t49 = gm_l7;
                                    void * t50 = gm_l7;
                                    gm_t88rcucg1b7uk t51 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t50));
                                    uint32_t t52 = gm_l8;
                                    void * t53 = gm_l7;
                                    gm_tl874t2atnlh3 t54 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t53) + 72));
                                    gm_to0f5qn75cgg8 t55;
                                    memset(&t55, 0, sizeof t55);
                                    GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)15ULL));
                                    GM_ST(uint32_t, ((uint8_t *)&t55 + 8), t52);
                                    void * t56 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t51, &t55, t54);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    memmove(((uint8_t *)t49), ((uint8_t *)t56), 88);
                                }
                                {
                                    void * t57 = gm_l2;
                                    uint32_t t58 = gm_l8;
                                    void * t59 = gm_l9;
                                    gm_ttggq7ng4kp3p t60;
                                    GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
                                    GM_ST(uint32_t, ((uint8_t *)&t60 + 0), t58);
                                    GM_ST(void *, ((uint8_t *)&t60 + 8), t59);
                                    gm_trcmmd12cefb1 t61;
                                    GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t61 + 24, 0);
                                    GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)0ULL));
                                    GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t61 + 8), t60);
                                    gm_tftb1odc739t5 t62 = gm_f_push_d6c76ud_2(t57, &t61);
                                    gm_l12 = t62;
                                }
                                uint8_t t63 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                                if (!t63) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcoimgd0jo62fr, 168, 9);
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 158, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t64 = gm_l5;
                    uint64_t r65; bool o66 = __builtin_add_overflow(t64, ((uint64_t)1ULL), &r65);
                    uint64_t t67 = r65;
                    gm_l5 = t67;
                }
            }
gm_lb3:;
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
__attribute__((aligned(4))) void gm_f_hoist_iter_value_cs229ip(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_hoist_iter_value_cs229ip F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    uint8_t gm_l2; /* receiver */
    void * gm_l4; /* x */
    void * gm_l5; /* base */
    gm_t08olq97rscn3 gm_l6; /* name */
    void * gm_l8; /* base */
    uint32_t gm_l9; /* index */
    gm_tl874t2atnlh3 gm_l10; /* index_span */
    void * gm_l12; /* base */
    void * gm_l13; /* index */
    void * gm_l16; /* x */
    void * gm_l18; /* operand */
    uint64_t gm_l22; /* mark */
    void * gm_l23; /* v */
    gm_t88rcucg1b7uk gm_l24; /* ty */
    uint8_t gm_l25; /* keep_place */
    gm_b16_8 gm_l27; /* name */
    uint32_t gm_l28; /* local */
    GM_STACK_CHECK(&F, 2793u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tq7s98ihonkju gm_l29;
    gm_tq7s98ihonkju gm_l30;
    gm_tq7s98ihonkju gm_l31;
    gm_tq7s98ihonkju gm_l32;
    gm_tq7s98ihonkju gm_l33;
    gm_tq7s98ihonkju gm_l34;
    uint8_t gm_l35;
    uint8_t gm_l36;
    uint8_t gm_l37;
    uint8_t gm_l38;
    uint8_t gm_l39;
    gm_t53at80pfdus3 gm_l40;
    uint8_t gm_l41;
    uint8_t gm_l42;
    uint8_t gm_l43;
    uint8_t gm_l44;
    gm_t53at80pfdus3 gm_l45;
    gm_tabb710d9mq2e gm_l46;
    gm_tftb1odc739t5 gm_l47;
    {
        void * t27 = gm_l0;
        void * t28 = gm_l1;
        uint8_t t29 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (t29) {
            void * t30 = gm_l1;
            memmove(gm_out, ((uint8_t *)t30), 88);
            goto gm_fdone;
        }
        memset(&F.l3, 0, sizeof F.l3);
        void * t31 = gm_l1;
        {
            {
                uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)t31) + 8) + 0));
                uint8_t t33 = (uint8_t)(t32 == ((uint8_t)9ULL));
                if (!t33) {
                    goto gm_lb2;
                }
                void * t34 = GM_LD(void *, ((((uint8_t *)t31) + 8) + 8));
                gm_l4 = t34;
                void * t35 = gm_l0;
                void * t36 = gm_l4;
                uint8_t t37 = gm_l2;
                gm_f_hoist_iter_value_cs229ip((void *)&(*(gm_tc3oht4ksth9d *)gm_out), (void *)NULL, cx, NULL, t35, t36, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
gm_lb2:;
            {
                uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t31) + 8) + 0));
                uint8_t t39 = (uint8_t)(t38 == ((uint8_t)6ULL));
                if (!t39) {
                    goto gm_lb3;
                }
                {
                    void * t40 = gm_l1;
                    gm_tc3oht4ksth9d t41 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)t40));
                    GM_ST(uint8_t, (uint8_t *)&F.l3 + 89, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.l3 + 90, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.l3 + 92, 0);
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&F.l3), t41);
                    ((uint8_t *)&F.l3)[88] = 1;
                }
                goto gm_lb1;
            }
gm_lb3:;
            {
                uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t31) + 8) + 0));
                uint8_t t43 = (uint8_t)(t42 == ((uint8_t)22ULL));
                if (!t43) {
                    goto gm_lb4;
                }
                void * t44 = GM_LD(void *, ((((uint8_t *)t31) + 8) + 8));
                gm_l5 = t44;
                {
                    memmove(((uint8_t *)&gm_l6), (((((uint8_t *)t31) + 8) + 8) + 8), 32);
                }
                {
                    void * t45 = gm_l0;
                    void * t46 = gm_l5;
                    gm_f_hoist_iter_value_cs229ip((void *)&F.l7, (void *)NULL, cx, NULL, t45, t46, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_tq7s98ihonkju t47 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l7)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kq6te48ps2f)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l29 = t47;
                }
                uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l29))[0] != 0);
                if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l29) + 8, gm_srcoimgd0jo62fr, 314, 78);
                {
                    void * t49 = GM_LD(void *, (((uint8_t *)&gm_l29) + 8));
                    gm_t08olq97rscn3 t50 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l6));
                    gm_tioie2ju3q4ul t51;
                    GM_ST(void *, ((uint8_t *)&t51 + 0), t49);
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t51 + 8), t50);
                    void * t52 = gm_l1;
                    gm_tl874t2atnlh3 t53 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t52) + 72));
                    gm_t2gtbh39ptekd t54;
                    GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t54 + 48, 0);
                    GM_ST(uint64_t, (uint8_t *)&t54 + 56, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t54 + 0), ((uint8_t)22ULL));
                    GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t54 + 8), t51);
                    gm_tc3oht4ksth9d t55;
                    GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t55 + 84, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t55 + 0), ((uint32_t)4294967295ULL));
                    GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&t55 + 8), t54);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t55 + 72), t53);
                    GM_ST(uint8_t, (uint8_t *)&F.l3 + 89, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.l3 + 90, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.l3 + 92, 0);
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&F.l3), t55);
                    ((uint8_t *)&F.l3)[88] = 1;
                }
                goto gm_lb1;
            }
gm_lb4:;
            {
                uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t31) + 8) + 0));
                uint8_t t57 = (uint8_t)(t56 == ((uint8_t)23ULL));
                if (!t57) {
                    goto gm_lb5;
                }
                void * t58 = GM_LD(void *, ((((uint8_t *)t31) + 8) + 8));
                gm_l8 = t58;
                uint32_t t59 = GM_LD(uint32_t, (((((uint8_t *)t31) + 8) + 8) + 8));
                gm_l9 = t59;
                gm_tl874t2atnlh3 t60 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t31) + 8) + 8) + 12));
                gm_l10 = t60;
                {
                    void * t61 = gm_l0;
                    void * t62 = gm_l8;
                    gm_f_hoist_iter_value_cs229ip((void *)&F.l11, (void *)NULL, cx, NULL, t61, t62, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_tq7s98ihonkju t63 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l11)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kukq3ep6ok2)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l30 = t63;
                }
                uint8_t t64 = (uint8_t)!((((uint8_t *)&gm_l30))[0] != 0);
                if (!t64) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l30) + 8, gm_srcoimgd0jo62fr, 318, 83);
                {
                    void * t65 = GM_LD(void *, (((uint8_t *)&gm_l30) + 8));
                    uint32_t t66 = gm_l9;
                    gm_tl874t2atnlh3 t67 = gm_l10;
                    gm_tupk62cr6lbdu t68;
                    GM_ST(void *, ((uint8_t *)&t68 + 0), t65);
                    GM_ST(uint32_t, ((uint8_t *)&t68 + 8), t66);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t68 + 12), t67);
                    void * t69 = gm_l1;
                    gm_tl874t2atnlh3 t70 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t69) + 72));
                    gm_t2gtbh39ptekd t71;
                    memset(&t71, 0, sizeof t71);
                    GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)23ULL));
                    GM_ST(gm_tupk62cr6lbdu, ((uint8_t *)&t71 + 8), t68);
                    gm_tc3oht4ksth9d t72;
                    GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t72 + 84, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t72 + 0), ((uint32_t)4294967295ULL));
                    GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&t72 + 8), t71);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t72 + 72), t70);
                    GM_ST(uint8_t, (uint8_t *)&F.l3 + 89, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.l3 + 90, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.l3 + 92, 0);
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&F.l3), t72);
                    ((uint8_t *)&F.l3)[88] = 1;
                }
                goto gm_lb1;
            }
gm_lb5:;
            {
                uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t31) + 8) + 0));
                uint8_t t74 = (uint8_t)(t73 == ((uint8_t)20ULL));
                if (!t74) {
                    goto gm_lb6;
                }
                void * t75 = GM_LD(void *, ((((uint8_t *)t31) + 8) + 8));
                gm_l12 = t75;
                void * t76 = GM_LD(void *, (((((uint8_t *)t31) + 8) + 8) + 8));
                gm_l13 = t76;
                {
                    void * t77 = gm_l0;
                    void * t78 = gm_l12;
                    gm_f_hoist_iter_value_cs229ip((void *)&F.l14, (void *)NULL, cx, NULL, t77, t78, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t79 = gm_l0;
                    void * t80 = gm_l13;
                    gm_f_hoist_iter_value_cs229ip((void *)&F.l15, (void *)NULL, cx, NULL, t79, t80, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_tq7s98ihonkju t81 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l14)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8gk4puesad)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l31 = t81;
                }
                uint8_t t82 = (uint8_t)!((((uint8_t *)&gm_l31))[0] != 0);
                if (!t82) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l31) + 8, gm_srcoimgd0jo62fr, 323, 78);
                {
                    void * t83 = GM_LD(void *, (((uint8_t *)&gm_l31) + 8));
                    gm_tq7s98ihonkju t84 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l15)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kbng43moaud)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l32 = t84;
                    uint8_t t85 = (uint8_t)!((((uint8_t *)&gm_l32))[0] != 0);
                    if (!t85) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l32) + 8, gm_srcoimgd0jo62fr, 323, 94);
                    void * t86 = GM_LD(void *, (((uint8_t *)&gm_l32) + 8));
                    gm_t3mf58gv9kf3n t87;
                    GM_ST(void *, ((uint8_t *)&t87 + 0), t83);
                    GM_ST(void *, ((uint8_t *)&t87 + 8), t86);
                    void * t88 = gm_l1;
                    gm_tl874t2atnlh3 t89 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t88) + 72));
                    gm_t2gtbh39ptekd t90;
                    memset(&t90, 0, sizeof t90);
                    GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)20ULL));
                    GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t90 + 8), t87);
                    gm_tc3oht4ksth9d t91;
                    GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t91 + 84, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t91 + 0), ((uint32_t)4294967295ULL));
                    GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&t91 + 8), t90);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t91 + 72), t89);
                    GM_ST(uint8_t, (uint8_t *)&F.l3 + 89, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.l3 + 90, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.l3 + 92, 0);
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&F.l3), t91);
                    ((uint8_t *)&F.l3)[88] = 1;
                }
                goto gm_lb1;
            }
gm_lb6:;
            {
                uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)t31) + 8) + 0));
                uint8_t t93 = (uint8_t)(t92 == ((uint8_t)18ULL));
                if (!t93) {
                    goto gm_lb7;
                }
                void * t94 = GM_LD(void *, ((((uint8_t *)t31) + 8) + 8));
                gm_l16 = t94;
                {
                    void * t95 = gm_l0;
                    void * t96 = gm_l16;
                    gm_f_hoist_iter_value_cs229ip((void *)&F.l17, (void *)NULL, cx, NULL, t95, t96, ((uint8_t)0));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_tq7s98ihonkju t97 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l17)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krlms0mdefs)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l33 = t97;
                }
                uint8_t t98 = (uint8_t)!((((uint8_t *)&gm_l33))[0] != 0);
                if (!t98) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l33) + 8, gm_srcoimgd0jo62fr, 327, 72);
                {
                    void * t99 = GM_LD(void *, (((uint8_t *)&gm_l33) + 8));
                    void * t100 = gm_l1;
                    gm_tl874t2atnlh3 t101 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t100) + 72));
                    gm_t2gtbh39ptekd t102;
                    memset(&t102, 0, sizeof t102);
                    GM_ST(uint8_t, ((uint8_t *)&t102 + 0), ((uint8_t)18ULL));
                    GM_ST(void *, ((uint8_t *)&t102 + 8), t99);
                    gm_tc3oht4ksth9d t103;
                    GM_ST(uint32_t, (uint8_t *)&t103 + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&t103 + 84, 0);
                    GM_ST(uint32_t, ((uint8_t *)&t103 + 0), ((uint32_t)4294967295ULL));
                    GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&t103 + 8), t102);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t103 + 72), t101);
                    GM_ST(uint8_t, (uint8_t *)&F.l3 + 89, 0);
                    GM_ST(uint16_t, (uint8_t *)&F.l3 + 90, 0);
                    GM_ST(uint32_t, (uint8_t *)&F.l3 + 92, 0);
                    GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&F.l3), t103);
                    ((uint8_t *)&F.l3)[88] = 1;
                }
                goto gm_lb1;
            }
gm_lb7:;
            {
                uint8_t t104 = GM_LD(uint8_t, ((((uint8_t *)t31) + 8) + 0));
                uint8_t t105 = (uint8_t)(t104 == ((uint8_t)12ULL));
                if (!t105) {
                    goto gm_lb8;
                }
                uint8_t t106 = GM_LD(uint8_t, (((((uint8_t *)t31) + 8) + 8) + 0));
                uint8_t t107 = (uint8_t)(t106 == ((uint8_t)2ULL));
                if (!t107) {
                    goto gm_lb8;
                }
                void * t108 = GM_LD(void *, (((((uint8_t *)t31) + 8) + 8) + 8));
                gm_l18 = t108;
                {
                    void * t109 = gm_l0;
                    void * t110 = gm_l18;
                    gm_f_hoist_iter_value_cs229ip((void *)&F.l19, (void *)NULL, cx, NULL, t109, t110, ((uint8_t)1));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_t9qkh0rsooc8d t111;
                    GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)2ULL));
                    gm_tq7s98ihonkju t112 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l19)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kb8poqrtf7g)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l34 = t112;
                    uint8_t t113 = (uint8_t)!((((uint8_t *)&gm_l34))[0] != 0);
                    if (!t113) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l34) + 8, gm_srcoimgd0jo62fr, 331, 93);
                    void * t114 = GM_LD(void *, (((uint8_t *)&gm_l34) + 8));
                    gm_t8ghgl2jj0qha t115;
                    GM_ST(uint8_t, (uint8_t *)&t115 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t115 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t115 + 4, 0);
                    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t115 + 0), t111);
                    GM_ST(void *, ((uint8_t *)&t115 + 8), t114);
                    void * t116 = gm_l1;
                    gm_tl874t2atnlh3 t117 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t116) + 72));
                    gm_t2gtbh39ptekd t118;
                    memset(&t118, 0, sizeof t118);
                    GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)12ULL));
                    GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t118 + 8), t115);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 4, 0);
                    GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 84, 0);
                    GM_ST(uint32_t, ((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 0), ((uint32_t)4294967295ULL));
                    GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 8), t118);
                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 72), t117);
                    goto gm_fdone;
                }
            }
gm_lb8:;
            {
                goto gm_lb1;
            }
gm_lb9:;
            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 309, 5);
        }
gm_lb1:;
    }
    {
        {
            {
                uint8_t t119 = (uint8_t)((((uint8_t *)&F.l3))[88] != 0);
                if (!t119) {
                    goto gm_lb11;
                }
                {
                    memmove(((uint8_t *)&F.l20), ((uint8_t *)&F.l3), 88);
                }
                {
                    memmove(((uint8_t *)&F.l21), ((uint8_t *)&F.l20), 88);
                }
                goto gm_lb10;
            }
gm_lb11:;
            void * t120 = gm_l1;
            memmove(((uint8_t *)&F.l21), ((uint8_t *)t120), 88);
        }
gm_lb10:;
        void * t121 = gm_l0;
        uint64_t t122 = GM_LD(uint64_t, ((((uint8_t *)t121) + 192) + 8));
        gm_l22 = t122;
        void * t123 = gm_l0;
        void * t124 = (void *)((uint8_t *)&F.l21);
        gm_tkn42u7na3t81 t125;
        memset(&t125, 0, sizeof t125);
        void * t126 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t123, t124, t125);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l23 = t126;
        {
            {
                {
                    {
                        uint8_t t127 = GM_LD(uint8_t, ((((uint8_t *)&F.l21) + 8) + 0));
                        uint8_t t128 = (uint8_t)(t127 == ((uint8_t)19ULL));
                        if (!t128) {
                            goto gm_lb15;
                        }
                        gm_l35 = ((uint8_t)1);
                        goto gm_lb14;
                    }
gm_lb15:;
                    {
                        gm_l35 = ((uint8_t)0);
                        goto gm_lb14;
                    }
gm_lb16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 340, 8);
                }
gm_lb14:;
                uint8_t t129 = gm_l35;
                if (!t129) {
                    goto gm_lb13;
                }
                gm_tl874t2atnlh3 t130 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l21) + 72));
                void * t131 = gm_l0;
                gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)t131) + 960));
                uint8_t t133 = 0;
                for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t132) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t132))) + i * 12, ((uint8_t *)&t130))) { t133 = 1; break; }
                uint8_t t134 = (uint8_t)(t133);
                if (!t134) {
                    goto gm_lb13;
                }
                void * t135 = gm_l0;
                void * t136 = (void *)(((uint8_t *)t135) + 192);
                uint64_t t137 = gm_l22;
                gm_f_truncate_ua7l21h(t136, t137);
                void * t138 = gm_l0;
                void * t139 = (void *)((uint8_t *)&F.l21);
                gm_f_hoist_iterator_call_5rmgsce((void *)&(*(gm_tc3oht4ksth9d *)gm_out), (void *)NULL, cx, NULL, t138, t139);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
gm_lb13:;
        }
gm_lb12:;
        void * t140 = gm_l0;
        void * t141 = (void *)(((uint8_t *)t140) + 96);
        void * t142 = gm_l0;
        void * t143 = GM_LD(void *, ((uint8_t *)t142));
        void * t144 = gm_l23;
        gm_t88rcucg1b7uk t145 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t144));
        gm_t88rcucg1b7uk t146 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t141, t143, t145);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l24 = t146;
        {
            {
                uint8_t t147 = (uint8_t)((((uint8_t *)&F.l3))[88] != 0);
                if (t147) {
                    goto gm_lb18;
                }
                gm_l37 = ((uint8_t)0);
                goto gm_lb17;
            }
gm_lb18:;
            {
                gm_l37 = ((uint8_t)1);
                goto gm_lb17;
            }
gm_lb19:;
            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 345, 19);
        }
gm_lb17:;
    }
    {
        uint8_t t148 = gm_l37;
        if (t148) {
            uint8_t t149 = gm_l2;
            if (t149) {
                gm_l38 = ((uint8_t)1);
            } else {
                void * t150 = gm_l0;
                void * t151 = GM_LD(void *, ((uint8_t *)t150));
                void * t152 = (void *)(((uint8_t *)t151) + 8);
                gm_t88rcucg1b7uk t153 = gm_l24;
                gm_f_kind_jcnt4hm((void *)&gm_l40, t152, GM_CONV(gm_csguankko4lmhd, t153));
                {
                    {
                        uint8_t t154 = GM_LD(uint8_t, (((uint8_t *)&gm_l40) + 0));
                        uint8_t t155 = (uint8_t)(t154 == ((uint8_t)11ULL));
                        if (!t155) {
                            goto gm_lb21;
                        }
                        gm_l39 = ((uint8_t)1);
                        goto gm_lb20;
                    }
gm_lb21:;
                    {
                        gm_l39 = ((uint8_t)0);
                        goto gm_lb20;
                    }
gm_lb22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 345, 54);
                }
gm_lb20:;
                uint8_t t156 = gm_l39;
                gm_l38 = t156;
            }
            uint8_t t157 = gm_l38;
            gm_l36 = t157;
        } else {
            gm_l36 = ((uint8_t)0);
        }
        uint8_t t158 = gm_l36;
        gm_l25 = t158;
        void * t159 = gm_l0;
        void * t160 = GM_LD(void *, ((uint8_t *)t159));
        void * t161 = (void *)(((uint8_t *)t160) + 8);
        gm_t88rcucg1b7uk t162 = gm_l24;
        uint8_t t163 = gm_f_is_error_8k27gd1(t161, GM_CONV(gm_csguankko4lmhd, t162));
        if (t163) {
            gm_l43 = ((uint8_t)1);
        } else {
            void * t164 = gm_l0;
            void * t165 = GM_LD(void *, ((uint8_t *)t164));
            void * t166 = (void *)(((uint8_t *)t165) + 8);
            gm_t88rcucg1b7uk t167 = gm_l24;
            uint8_t t168 = gm_f_has_infer_3o82a0b(t166, GM_CONV(gm_csguankko4lmhd, t167));
            gm_l43 = t168;
        }
        uint8_t t169 = gm_l43;
        if (t169) {
            gm_l42 = ((uint8_t)1);
        } else {
            void * t170 = gm_l0;
            void * t171 = GM_LD(void *, ((uint8_t *)t170));
            void * t172 = (void *)(((uint8_t *)t171) + 8);
            gm_t88rcucg1b7uk t173 = gm_l24;
            gm_f_kind_jcnt4hm((void *)&gm_l45, t172, GM_CONV(gm_csguankko4lmhd, t173));
            {
                {
                    uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&gm_l45) + 0));
                    uint8_t t175 = (uint8_t)(t174 == ((uint8_t)15ULL));
                    if (!t175) {
                        goto gm_lb24;
                    }
                    gm_l44 = ((uint8_t)1);
                    goto gm_lb23;
                }
gm_lb24:;
                {
                    gm_l44 = ((uint8_t)0);
                    goto gm_lb23;
                }
gm_lb25:;
                gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 348, 59);
            }
gm_lb23:;
            uint8_t t176 = gm_l44;
            gm_l42 = t176;
        }
        uint8_t t177 = gm_l42;
        if (t177) {
            gm_l41 = ((uint8_t)1);
        } else {
            uint8_t t178 = gm_l25;
            gm_l41 = t178;
        }
    }
    {
        uint8_t t179 = gm_l41;
        if (t179) {
            memmove(gm_out, ((uint8_t *)&F.l21), 88);
            goto gm_fdone;
        }
        void * t180 = gm_l0;
        memmove(((uint8_t *)&F.l26), (((uint8_t *)t180) + 1160), 40);
        void * t181 = gm_l0;
        gm_trqfvmjtt4sij t182;
        gm_f_list_ir0h3tc_2((void *)&t182, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t181) + 1160), t182);
        void * t183 = gm_l0;
        void * t184 = gm_l23;
        void * t185 = (void *)((uint8_t *)&F.l26);
        gm_b0_1 t186 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t183, t184, t185);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t187 = gm_l0;
        uint64_t t188 = GM_LD(uint64_t, ((((uint8_t *)t187) + 16) + 8));
        gm_t4ce0jd3tdro4 t189;
        GM_ST(uint64_t, ((uint8_t *)&t189 + 0), t188);
        gm_f_format_t04dp7b((void *)&gm_l46, (void *)NULL, cx, NULL, t189);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t190 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
        if (!t190) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 8, gm_srcoimgd0jo62fr, 352, 20);
        gm_b16_8 t191 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l46) + 8));
        gm_b16_8 t192 = GM_LD(gm_b16_8, ((uint8_t *)&t191));
        gm_l27 = t192;
        void * t193 = gm_l0;
        gm_b16_8 t194 = gm_l27;
        gm_t88rcucg1b7uk t195 = gm_l24;
        void * t196 = gm_l1;
        gm_tl874t2atnlh3 t197 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t196) + 72));
        uint32_t t198 = gm_f_new_local_003h9es((void *)NULL, cx, NULL, t193, t194, t195, ((uint8_t)1), t197);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l28 = t198;
        void * t199 = (void *)((uint8_t *)&F.l26);
        uint32_t t200 = gm_l28;
        void * t201 = gm_l23;
        gm_ttggq7ng4kp3p t202;
        GM_ST(uint32_t, (uint8_t *)&t202 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t202 + 0), t200);
        GM_ST(void *, ((uint8_t *)&t202 + 8), t201);
        gm_trcmmd12cefb1 t203;
        GM_ST(uint8_t, (uint8_t *)&t203 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t203 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t203 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t203 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t203 + 0), ((uint8_t)0ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t203 + 8), t202);
        gm_tftb1odc739t5 t204 = gm_f_push_d6c76ud_2(t199, &t203);
        gm_l47 = t204;
        uint8_t t205 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
        if (!t205) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_srcoimgd0jo62fr, 354, 5);
        void * t206 = gm_l0;
        memmove((((uint8_t *)t206) + 1160), ((uint8_t *)&F.l26), 40);
        void * t207 = gm_l0;
        gm_b16_8 t208 = gm_l27;
        uint32_t t209 = gm_l28;
        void * t210 = gm_l1;
        gm_tl874t2atnlh3 t211 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t210) + 72));
        gm_t930q5l38hdap t212;
        memset(&t212, 0, sizeof t212);
        GM_ST(uint8_t, ((uint8_t *)&t212 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t212 + 8), t209);
        gm_b0_1 t213 = gm_f_declare_3l5mhoj((void *)NULL, cx, NULL, t207, t208, &t212, t211);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t214 = gm_l27;
        void * t215 = gm_l1;
        gm_tl874t2atnlh3 t216 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t215) + 72));
        gm_t08olq97rscn3 t217;
        GM_ST(uint32_t, (uint8_t *)&t217 + 28, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t217 + 0), t214);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t217 + 16), t216);
        void * t218 = gm_l1;
        gm_tl874t2atnlh3 t219 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t218) + 72));
        gm_t2gtbh39ptekd t220;
        GM_ST(uint8_t, (uint8_t *)&t220 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t220 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t220 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t220 + 40, 0);
        GM_ST(uint64_t, (uint8_t *)&t220 + 48, 0);
        GM_ST(uint64_t, (uint8_t *)&t220 + 56, 0);
        GM_ST(uint8_t, ((uint8_t *)&t220 + 0), ((uint8_t)6ULL));
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t220 + 8), t217);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 84, 0);
        GM_ST(uint32_t, ((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 0), ((uint32_t)4294967295ULL));
        GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 8), t220);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 72), t219);
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
__attribute__((aligned(4))) void gm_f_hoist_iterator_call_5rmgsce(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_hoist_iterator_call_5rmgsce F;
    void * gm_l0; /* cx */
    void * gm_l1; /* call */
    void * gm_l2; /* callee */
    gm_b16_8 gm_l3; /* args */
    uint8_t gm_l4; /* trailing_comma */
    void * gm_l6; /* base */
    gm_t08olq97rscn3 gm_l7; /* name */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    gm_t08olq97rscn3 gm_l14; /* n */
    GM_STACK_CHECK(&F, 1499u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tq7s98ihonkju gm_l18;
    gm_tftb1odc739t5 gm_l19;
    gm_tq7s98ihonkju gm_l20;
    {
        {
            void * t15 = gm_l1;
            uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t15) + 8) + 0));
            uint8_t t17 = (uint8_t)(t16 == ((uint8_t)19ULL));
            if (!t17) {
                goto gm_lb2;
            }
            void * t18 = GM_LD(void *, ((((uint8_t *)t15) + 8) + 8));
            gm_l2 = t18;
            gm_b16_8 t19 = GM_LD(gm_b16_8, (((((uint8_t *)t15) + 8) + 8) + 8));
            gm_l3 = t19;
            uint8_t t20 = GM_LD(uint8_t, (((((uint8_t *)t15) + 8) + 8) + 24));
            gm_l4 = t20;
            {
                void * t21 = gm_l2;
                memmove(((uint8_t *)&F.l5), ((uint8_t *)t21), 88);
            }
            {
                {
                    void * t22 = gm_l2;
                    uint8_t t23 = GM_LD(uint8_t, ((((uint8_t *)t22) + 8) + 0));
                    uint8_t t24 = (uint8_t)(t23 == ((uint8_t)22ULL));
                    if (!t24) {
                        goto gm_lb4;
                    }
                    void * t25 = GM_LD(void *, ((((uint8_t *)t22) + 8) + 8));
                    gm_l6 = t25;
                    memmove(((uint8_t *)&gm_l7), (((((uint8_t *)t22) + 8) + 8) + 8), 32);
                    void * t26 = gm_l0;
                    void * t27 = gm_l6;
                    uint8_t t28 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t26, t27);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    uint8_t t29 = (uint8_t)!t28;
                    if (t29) {
                        {
                            void * t30 = gm_l0;
                            void * t31 = gm_l6;
                            gm_f_hoist_iter_value_cs229ip((void *)&F.l8, (void *)NULL, cx, NULL, t30, t31, ((uint8_t)1));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                        }
                        {
                            void * t32 = gm_l2;
                            uint32_t t33 = GM_LD(uint32_t, ((uint8_t *)t32));
                            gm_tq7s98ihonkju t34 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l8)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kl5b02juaoa)));
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_l18 = t34;
                            uint8_t t35 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
                            if (!t35) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcoimgd0jo62fr, 261, 69);
                            void * t36 = GM_LD(void *, (((uint8_t *)&gm_l18) + 8));
                            gm_t08olq97rscn3 t37 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l7));
                            gm_tioie2ju3q4ul t38;
                            GM_ST(void *, ((uint8_t *)&t38 + 0), t36);
                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t38 + 8), t37);
                            void * t39 = gm_l2;
                            gm_tl874t2atnlh3 t40 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t39) + 72));
                            gm_t2gtbh39ptekd t41;
                            GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t41 + 48, 0);
                            GM_ST(uint64_t, (uint8_t *)&t41 + 56, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)22ULL));
                            GM_ST(gm_tioie2ju3q4ul, ((uint8_t *)&t41 + 8), t38);
                            GM_ST(uint32_t, (uint8_t *)&F.l5 + 4, 0);
                            GM_ST(uint32_t, (uint8_t *)&F.l5 + 84, 0);
                            GM_ST(uint32_t, ((uint8_t *)&F.l5 + 0), t33);
                            GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&F.l5 + 8), t41);
                            GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&F.l5 + 72), t40);
                        }
                    }
                    goto gm_lb3;
                }
gm_lb4:;
            }
gm_lb3:;
            {
                gm_trqfvmjtt4sij t42;
                gm_f_list_6oos54v((void *)&t42, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l9 = t42;
            }
            gm_b16_8 t43 = gm_l3;
            gm_l10 = t43;
            gm_l11 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t44 = gm_l11;
                    uint64_t t45 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                    uint8_t t46 = (t44 < t45);
                    if (!t46) {
                        goto gm_lb5;
                    }
                    {
                        void * t47 = GM_LD(void *, ((uint8_t *)&gm_l10));
                        uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l10) + 8));
                        uint64_t t49 = gm_l11;
                        {
                            {
                                {
                                    memmove(((uint8_t *)&F.l12), (((uint8_t *)t47) + (uintptr_t)t49 * 144), 144);
                                }
                                {
                                    {
                                        uint8_t t50 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
                                        uint8_t t51 = (uint8_t)(t50 == ((uint8_t)0ULL));
                                        if (!t51) {
                                            goto gm_lb11;
                                        }
                                        {
                                            memmove(((uint8_t *)&F.l13), (((uint8_t *)&F.l12) + 8), 88);
                                        }
                                        {
                                            void * t52 = gm_l0;
                                            void * t53 = (void *)((uint8_t *)&F.l13);
                                            gm_tc3oht4ksth9d t54;
                                            gm_f_hoist_iter_value_cs229ip((void *)&t54, (void *)NULL, cx, NULL, t52, t53, ((uint8_t)0));
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            memset(&F.l17, 0, sizeof F.l17);
                                            GM_ST(uint8_t, ((uint8_t *)&F.l17 + 0), ((uint8_t)0ULL));
                                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&F.l17 + 8), t54);
                                        }
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                    {
                                        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
                                        uint8_t t56 = (uint8_t)(t55 == ((uint8_t)1ULL));
                                        if (!t56) {
                                            goto gm_lb12;
                                        }
                                        {
                                            memmove(((uint8_t *)&gm_l14), (((uint8_t *)&F.l12) + 8), 32);
                                        }
                                        {
                                            memmove(((uint8_t *)&F.l15), ((((uint8_t *)&F.l12) + 8) + 32), 88);
                                        }
                                        {
                                            void * t57 = gm_l0;
                                            void * t58 = (void *)((uint8_t *)&F.l15);
                                            gm_t08olq97rscn3 t59 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)&gm_l14));
                                            gm_tc3oht4ksth9d t60;
                                            gm_f_hoist_iter_value_cs229ip((void *)&t60, (void *)NULL, cx, NULL, t57, t58, ((uint8_t)0));
                                            if (gm_unw.kind) {
                                                goto gm_lad;
                                            }
                                            gm_t56mj5hnm2uc6 t61;
                                            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t61 + 0), t59);
                                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&t61 + 32), t60);
                                            GM_ST(uint8_t, (uint8_t *)&F.l17 + 1, 0);
                                            GM_ST(uint16_t, (uint8_t *)&F.l17 + 2, 0);
                                            GM_ST(uint32_t, (uint8_t *)&F.l17 + 4, 0);
                                            GM_ST(uint8_t, ((uint8_t *)&F.l17 + 0), ((uint8_t)1ULL));
                                            GM_ST(gm_t56mj5hnm2uc6, ((uint8_t *)&F.l17 + 8), t61);
                                        }
                                        goto gm_lb10;
                                    }
gm_lb12:;
                                    {
                                        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&F.l12) + 0));
                                        uint8_t t63 = (uint8_t)(t62 == ((uint8_t)2ULL));
                                        if (!t63) {
                                            goto gm_lb13;
                                        }
                                        {
                                            memmove(((uint8_t *)&F.l16), (((uint8_t *)&F.l12) + 8), 88);
                                        }
                                        {
                                            gm_tc3oht4ksth9d t64 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)&F.l16));
                                            memset(&F.l17, 0, sizeof F.l17);
                                            GM_ST(uint8_t, ((uint8_t *)&F.l17 + 0), ((uint8_t)2ULL));
                                            GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&F.l17 + 8), t64);
                                        }
                                        goto gm_lb10;
                                    }
gm_lb13:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 266, 29);
                                }
gm_lb10:;
                                {
                                    void * t65 = (void *)((uint8_t *)&F.l9);
                                    gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l12) + 128));
                                    gm_t22mtv3sd9prf t67 = GM_LD(gm_t22mtv3sd9prf, ((uint8_t *)&F.l17));
                                    gm_t7te2tdpebbt4 t68;
                                    GM_ST(uint32_t, (uint8_t *)&t68 + 140, 0);
                                    GM_ST(gm_t22mtv3sd9prf, ((uint8_t *)&t68 + 0), t67);
                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t68 + 128), t66);
                                    gm_tftb1odc739t5 t69 = gm_f_push_jis5lhu(t65, &t68);
                                    gm_l19 = t69;
                                }
                                uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l19))[0] != 0);
                                if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l19) + 1, gm_srcoimgd0jo62fr, 271, 9);
                                goto gm_lb8;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 265, 5);
                        }
gm_lb8:;
                    }
gm_lb7:;
                    uint64_t t71 = gm_l11;
                    uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                    uint64_t t74 = r72;
                    gm_l11 = t74;
                }
            }
gm_lb5:;
            {
                void * t75 = gm_l1;
                uint32_t t76 = GM_LD(uint32_t, ((uint8_t *)t75));
                gm_tq7s98ihonkju t77 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l5)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kq6mn01fhkq)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l20 = t77;
                uint8_t t78 = (uint8_t)!((((uint8_t *)&gm_l20))[0] != 0);
                if (!t78) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l20) + 8, gm_srcoimgd0jo62fr, 273, 54);
                void * t79 = GM_LD(void *, (((uint8_t *)&gm_l20) + 8));
                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
                uint8_t t81 = gm_l4;
                gm_tlljg61a2ctgq t82;
                GM_ST(uint8_t, (uint8_t *)&t82 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t82 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t82 + 28, 0);
                GM_ST(void *, ((uint8_t *)&t82 + 0), t79);
                GM_ST(gm_b16_8, ((uint8_t *)&t82 + 8), t80);
                GM_ST(uint8_t, ((uint8_t *)&t82 + 24), t81);
                void * t83 = gm_l1;
                gm_tl874t2atnlh3 t84 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t83) + 72));
                gm_t2gtbh39ptekd t85;
                GM_ST(uint8_t, (uint8_t *)&t85 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t85 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t85 + 40, 0);
                GM_ST(uint64_t, (uint8_t *)&t85 + 48, 0);
                GM_ST(uint64_t, (uint8_t *)&t85 + 56, 0);
                GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)19ULL));
                GM_ST(gm_tlljg61a2ctgq, ((uint8_t *)&t85 + 8), t82);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 84, 0);
                GM_ST(uint32_t, ((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 0), t76);
                GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 8), t85);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&(*(gm_tc3oht4ksth9d *)gm_out) + 72), t84);
                goto gm_fdone;
            }
        }
gm_lb2:;
        void * t86 = gm_l1;
        memmove(gm_out, ((uint8_t *)t86), 88);
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
__attribute__((aligned(4))) uint8_t gm_f_iter_stable_k9c87oo(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_iter_stable_k9c87oo F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t08olq97rscn3 gm_l2; /* id */
    void * gm_l3; /* x */
    void * gm_l4; /* base */
    gm_t9qkh0rsooc8d gm_l5; /* op */
    void * gm_l6; /* operand */
    void * gm_l7; /* lhs */
    void * gm_l8; /* rhs */
    void * gm_l9; /* callee */
    gm_b16_8 gm_l10; /* args */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    GM_STACK_CHECK(&F, 725u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l16;
    gm_tqvu78rpi6q1h gm_l17;
    uint8_t gm_l18;
    uint8_t gm_l19;
    uint8_t gm_l20;
    void * t35 = gm_l1;
    {
        {
            {
                {
                    uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t37 = (uint8_t)(t36 == ((uint8_t)0ULL));
                    if (!t37) {
                        goto gm_lb4;
                    }
                    goto gm_lb3;
                }
gm_lb4:;
                {
                    uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t39 = (uint8_t)(t38 == ((uint8_t)1ULL));
                    if (!t39) {
                        goto gm_lb5;
                    }
                    goto gm_lb3;
                }
gm_lb5:;
                {
                    uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t41 = (uint8_t)(t40 == ((uint8_t)2ULL));
                    if (!t41) {
                        goto gm_lb6;
                    }
                    goto gm_lb3;
                }
gm_lb6:;
                {
                    uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t43 = (uint8_t)(t42 == ((uint8_t)3ULL));
                    if (!t43) {
                        goto gm_lb7;
                    }
                    goto gm_lb3;
                }
gm_lb7:;
                {
                    uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t45 = (uint8_t)(t44 == ((uint8_t)4ULL));
                    if (!t45) {
                        goto gm_lb8;
                    }
                    goto gm_lb3;
                }
gm_lb8:;
                {
                    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)5ULL));
                    if (!t47) {
                        goto gm_lb9;
                    }
                    goto gm_lb3;
                }
gm_lb9:;
                {
                    uint8_t t48 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t49 = (uint8_t)(t48 == ((uint8_t)7ULL));
                    if (!t49) {
                        goto gm_lb10;
                    }
                    goto gm_lb3;
                }
gm_lb10:;
                {
                    uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)8ULL));
                    if (!t51) {
                        goto gm_lb11;
                    }
                    goto gm_lb3;
                }
gm_lb11:;
                goto gm_lb2;
            }
gm_lb3:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
        {
            uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
            uint8_t t53 = (uint8_t)(t52 == ((uint8_t)6ULL));
            if (!t53) {
                goto gm_lb12;
            }
            {
                memmove(((uint8_t *)&gm_l2), ((((uint8_t *)t35) + 8) + 8), 32);
            }
            {
                void * t54 = gm_l0;
                gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l2));
                gm_f_lookup_local_6f38t7p((void *)&gm_l17, t54, t55);
            }
            {
                {
                    uint8_t t56 = (uint8_t)((((uint8_t *)&gm_l17))[208] != 0);
                    if (t56) {
                        goto gm_lb14;
                    }
                    gm_l16 = ((uint8_t)1);
                    goto gm_lb13;
                }
gm_lb14:;
                {
                    gm_l16 = ((uint8_t)0);
                    goto gm_lb13;
                }
gm_lb15:;
                gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 281, 30);
            }
gm_lb13:;
            uint8_t t57 = gm_l16;
            F.ret = t57;
            goto gm_fret;
        }
gm_lb12:;
        {
            uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
            uint8_t t59 = (uint8_t)(t58 == ((uint8_t)9ULL));
            if (!t59) {
                goto gm_lb16;
            }
            void * t60 = GM_LD(void *, ((((uint8_t *)t35) + 8) + 8));
            gm_l3 = t60;
            void * t61 = gm_l0;
            void * t62 = gm_l3;
            uint8_t t63 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t63;
            goto gm_fret;
        }
gm_lb16:;
        {
            uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
            uint8_t t65 = (uint8_t)(t64 == ((uint8_t)22ULL));
            if (!t65) {
                goto gm_lb17;
            }
            void * t66 = GM_LD(void *, ((((uint8_t *)t35) + 8) + 8));
            gm_l4 = t66;
            void * t67 = gm_l0;
            void * t68 = gm_l4;
            uint8_t t69 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t69;
            goto gm_fret;
        }
gm_lb17:;
        {
            uint8_t t70 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
            uint8_t t71 = (uint8_t)(t70 == ((uint8_t)12ULL));
            if (!t71) {
                goto gm_lb18;
            }
            gm_t9qkh0rsooc8d t72 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)t35) + 8) + 8));
            gm_l5 = t72;
            void * t73 = GM_LD(void *, (((((uint8_t *)t35) + 8) + 8) + 8));
            gm_l6 = t73;
            gm_t9qkh0rsooc8d t74 = gm_l5;
            gm_t9qkh0rsooc8d t75;
            GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)0ULL));
            gm_t9qkh0rsooc8d t76;
            GM_ST(uint8_t, ((uint8_t *)&t76 + 0), ((uint8_t)1ULL));
            gm_t9qkh0rsooc8d t77;
            GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)5ULL));
            gm_tk22flr9mh3ql t78;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t78 + 0), t75);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t78 + 1), t76);
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t78 + 2), t77);
            uint8_t t79 = 0;
            if (gm_eqlv5i3f25jk(((uint8_t *)&t78) + 0, ((uint8_t *)&t74))) t79 = 1;
            if (gm_eqlv5i3f25jk(((uint8_t *)&t78) + 1, ((uint8_t *)&t74))) t79 = 1;
            if (gm_eqlv5i3f25jk(((uint8_t *)&t78) + 2, ((uint8_t *)&t74))) t79 = 1;
            uint8_t t80 = (uint8_t)(t79);
            if (t80) {
                void * t81 = gm_l0;
                void * t82 = gm_l6;
                uint8_t t83 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t81, t82);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t83;
            } else {
                gm_l18 = ((uint8_t)0);
            }
            uint8_t t84 = gm_l18;
            F.ret = t84;
            goto gm_fret;
        }
gm_lb18:;
        {
            uint8_t t85 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
            uint8_t t86 = (uint8_t)(t85 == ((uint8_t)13ULL));
            if (!t86) {
                goto gm_lb19;
            }
            void * t87 = GM_LD(void *, (((((uint8_t *)t35) + 8) + 8) + 8));
            gm_l7 = t87;
            void * t88 = GM_LD(void *, (((((uint8_t *)t35) + 8) + 8) + 16));
            gm_l8 = t88;
            void * t89 = gm_l0;
            void * t90 = gm_l7;
            uint8_t t91 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t89, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (t91) {
                void * t92 = gm_l0;
                void * t93 = gm_l8;
                uint8_t t94 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t92, t93);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t94;
            } else {
                gm_l19 = ((uint8_t)0);
            }
            uint8_t t95 = gm_l19;
            F.ret = t95;
            goto gm_fret;
        }
gm_lb19:;
        {
            uint8_t t96 = GM_LD(uint8_t, ((((uint8_t *)t35) + 8) + 0));
            uint8_t t97 = (uint8_t)(t96 == ((uint8_t)19ULL));
            if (!t97) {
                goto gm_lb20;
            }
            void * t98 = GM_LD(void *, ((((uint8_t *)t35) + 8) + 8));
            gm_l9 = t98;
            gm_b16_8 t99 = GM_LD(gm_b16_8, (((((uint8_t *)t35) + 8) + 8) + 8));
            gm_l10 = t99;
            void * t100 = gm_l9;
            {
                {
                    uint8_t t101 = GM_LD(uint8_t, ((((uint8_t *)t100) + 8) + 0));
                    uint8_t t102 = (uint8_t)(t101 == ((uint8_t)8ULL));
                    if (!t102) {
                        goto gm_lb22;
                    }
                    gm_l20 = ((uint8_t)0);
                    goto gm_lb21;
                }
gm_lb22:;
                {
                    gm_l20 = ((uint8_t)1);
                    goto gm_lb21;
                }
gm_lb23:;
                gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 288, 16);
            }
gm_lb21:;
            uint8_t t103 = gm_l20;
            if (t103) {
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_b16_8 t104 = gm_l10;
            gm_l11 = t104;
            gm_l12 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t105 = gm_l12;
                    uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint8_t t107 = (t105 < t106);
                    if (!t107) {
                        goto gm_lb24;
                    }
                    {
                        void * t108 = GM_LD(void *, ((uint8_t *)&gm_l11));
                        uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                        uint64_t t110 = gm_l12;
                        {
                            {
                                memmove(((uint8_t *)&F.l13), (((uint8_t *)t108) + (uintptr_t)t110 * 144), 144);
                                {
                                    {
                                        uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
                                        uint8_t t112 = (uint8_t)(t111 == ((uint8_t)0ULL));
                                        if (!t112) {
                                            goto gm_lb30;
                                        }
                                        memmove(((uint8_t *)&F.l14), (((uint8_t *)&F.l13) + 8), 88);
                                        void * t113 = gm_l0;
                                        void * t114 = (void *)((uint8_t *)&F.l14);
                                        uint8_t t115 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t113, t114);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t116 = (uint8_t)!t115;
                                        if (t116) {
                                            F.ret = ((uint8_t)0);
                                            goto gm_fret;
                                        }
                                        goto gm_lb29;
                                    }
gm_lb30:;
                                    {
                                        uint8_t t117 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
                                        uint8_t t118 = (uint8_t)(t117 == ((uint8_t)1ULL));
                                        if (!t118) {
                                            goto gm_lb31;
                                        }
                                        memmove(((uint8_t *)&F.l15), ((((uint8_t *)&F.l13) + 8) + 32), 88);
                                        void * t119 = gm_l0;
                                        void * t120 = (void *)((uint8_t *)&F.l15);
                                        uint8_t t121 = gm_f_iter_stable_k9c87oo((void *)NULL, cx, NULL, t119, t120);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t122 = (uint8_t)!t121;
                                        if (t122) {
                                            F.ret = ((uint8_t)0);
                                            goto gm_fret;
                                        }
                                        goto gm_lb29;
                                    }
gm_lb31:;
                                    {
                                        uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&F.l13) + 0));
                                        uint8_t t124 = (uint8_t)(t123 == ((uint8_t)2ULL));
                                        if (!t124) {
                                            goto gm_lb32;
                                        }
                                        F.ret = ((uint8_t)0);
                                        goto gm_fret;
                                    }
gm_lb32:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 290, 17);
                                }
gm_lb29:;
                                goto gm_lb27;
                            }
gm_lb28:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 289, 13);
                        }
gm_lb27:;
                    }
gm_lb26:;
                    uint64_t t125 = gm_l12;
                    uint64_t r126; bool o127 = __builtin_add_overflow(t125, ((uint64_t)1ULL), &r126);
                    uint64_t t128 = r126;
                    gm_l12 = t128;
                }
            }
gm_lb24:;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb20:;
        {
            void * t129 = gm_l0;
            void * t130 = gm_l1;
            uint8_t t131 = gm_f_denotes_type_jgfet92((void *)NULL, cx, NULL, t129, t130);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            F.ret = t131;
            goto gm_fret;
        }
gm_lb33:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 279, 5);
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
__attribute__((aligned(4))) void gm_f_iterator_arg_r0if28f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_iterator_arg_r0if28f F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint32_t gm_l4; /* y */
    uint8_t gm_l5; /* yields */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_tiilmfd16v3fc gm_l8; /* x */
    void * gm_l9; /* inner */
    void * gm_l10; /* x */
    void * gm_l13; /* lit */
    GM_STACK_CHECK(&F, 1042u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_t53at80pfdus3 gm_l14;
    uint8_t gm_l15;
    gm_tq7s98ihonkju gm_l16;
    gm_tq7s98ihonkju gm_l17;
    gm_tftb1odc739t5 gm_l18;
    {
        {
            {
                void * t15 = gm_l0;
                void * t16 = GM_LD(void *, ((uint8_t *)t15));
                void * t17 = (void *)(((uint8_t *)t16) + 8);
                gm_t88rcucg1b7uk t18 = gm_l2;
                gm_f_kind_jcnt4hm((void *)&gm_l14, t17, GM_CONV(gm_csguankko4lmhd, t18));
            }
            uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            uint8_t t20 = (uint8_t)(t19 == ((uint8_t)15ULL));
            if (!t20) {
                goto gm_lb2;
            }
            {
                memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l14) + 8), 48);
            }
            void * t21 = gm_l0;
            void * t22 = GM_LD(void *, ((uint8_t *)t21));
            uint8_t t23 = (uint8_t)(((((uint8_t *)t22) + 2076))[4] != 0);
            if (t23) {
                uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)t22) + 2076));
                gm_l4 = t24;
            } else {
                memset(&(*(gm_t0mfir12leg2g *)gm_out), 0, sizeof (*(gm_t0mfir12leg2g *)gm_out));
                goto gm_fdone;
            }
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
            uint8_t t26 = (t25 > ((uint64_t)0ULL));
            if (t26) {
                memset(&(*(gm_t0mfir12leg2g *)gm_out), 0, sizeof (*(gm_t0mfir12leg2g *)gm_out));
                goto gm_fdone;
            }
            gm_l5 = ((uint8_t)0);
            gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
            gm_l6 = t27;
            gm_l7 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t28 = gm_l7;
                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint8_t t30 = (t28 < t29);
                    if (!t30) {
                        goto gm_lb3;
                    }
                    {
                        void * t31 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t33 = gm_l7;
                        {
                            {
                                memmove(((uint8_t *)&gm_l8), (((uint8_t *)t31) + (uintptr_t)t33 * 24), 24);
                                uint32_t t34 = GM_LD(uint32_t, ((uint8_t *)&gm_l8));
                                uint32_t t35 = gm_l4;
                                uint8_t t36 = ((t34 == t35));
                                if (t36) {
                                    gm_l5 = ((uint8_t)1);
                                }
                                goto gm_lb6;
                            }
gm_lb7:;
                            gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 221, 5);
                        }
gm_lb6:;
                    }
gm_lb5:;
                    uint64_t t37 = gm_l7;
                    uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                    uint64_t t40 = r38;
                    gm_l7 = t40;
                }
            }
gm_lb3:;
            uint8_t t41 = gm_l5;
            uint8_t t42 = (uint8_t)!t41;
            if (t42) {
                memset(&(*(gm_t0mfir12leg2g *)gm_out), 0, sizeof (*(gm_t0mfir12leg2g *)gm_out));
                goto gm_fdone;
            }
            void * t43 = gm_l1;
            gm_l9 = t43;
            {
                for (;;) {
                    void * t44 = gm_l9;
                    uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t44) + 8) + 0));
                    uint8_t t46 = (uint8_t)(t45 == ((uint8_t)9ULL));
                    if (!t46) {
                        goto gm_lb8;
                    }
                    void * t47 = GM_LD(void *, ((((uint8_t *)t44) + 8) + 8));
                    gm_l10 = t47;
                    {
                        void * t48 = gm_l10;
                        gm_l9 = t48;
                    }
gm_lb10:;
                }
            }
gm_lb8:;
            void * t49 = gm_l9;
            {
                {
                    uint8_t t50 = GM_LD(uint8_t, ((((uint8_t *)t49) + 8) + 0));
                    uint8_t t51 = (uint8_t)(t50 == ((uint8_t)19ULL));
                    if (!t51) {
                        goto gm_lb12;
                    }
                    gm_l15 = ((uint8_t)0);
                    goto gm_lb11;
                }
gm_lb12:;
                {
                    gm_l15 = ((uint8_t)1);
                    goto gm_lb11;
                }
gm_lb13:;
                gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 227, 8);
            }
gm_lb11:;
            uint8_t t52 = gm_l15;
            if (t52) {
                memset(&(*(gm_t0mfir12leg2g *)gm_out), 0, sizeof (*(gm_t0mfir12leg2g *)gm_out));
                goto gm_fdone;
            }
            {
                void * t53 = gm_l0;
                void * t54 = gm_l9;
                gm_f_hoist_iterator_call_5rmgsce((void *)&F.l11, (void *)NULL, cx, NULL, t53, t54);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b16_8 t55 = gm_f_empty_slice_ito0pap();
                gm_th2lqbo77pvsc t56;
                memset(&t56, 0, sizeof t56);
                void * t57 = gm_l1;
                gm_tl874t2atnlh3 t58 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t57) + 72));
                GM_ST(uint8_t, (uint8_t *)&F.l12 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&F.l12 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&F.l12 + 20, 0);
                GM_ST(uint32_t, (uint8_t *)&F.l12 + 68, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&F.l12 + 0), t55);
                GM_ST(uint8_t, ((uint8_t *)&F.l12 + 16), ((uint8_t)0));
                GM_ST(void *, ((uint8_t *)&F.l12 + 24), ((void *)0));
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&F.l12 + 32), t56);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&F.l12 + 56), t58);
            }
            {
                gm_tlivgedjs5ps3 t59 = GM_LD(gm_tlivgedjs5ps3, ((uint8_t *)&F.l12));
                gm_b16_8 t60 = gm_f_empty_slice_3jt8vgk();
                gm_tq7s98ihonkju t61 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)&F.l11)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k0gn7sac5pt)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t61;
                uint8_t t62 = (uint8_t)!((((uint8_t *)&gm_l16))[0] != 0);
                if (!t62) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l16) + 8, gm_srcoimgd0jo62fr, 232, 86);
                void * t63 = GM_LD(void *, (((uint8_t *)&gm_l16) + 8));
                void * t64 = gm_l1;
                gm_tl874t2atnlh3 t65 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t64) + 72));
                gm_tapdr2ag17fh7 t66;
                memset(&t66, 0, sizeof t66);
                GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)1ULL));
                GM_ST(void *, ((uint8_t *)&t66 + 8), t63);
                gm_t0kv05e068jrn t67;
                GM_ST(uint32_t, (uint8_t *)&t67 + 148, 0);
                GM_ST(gm_tlivgedjs5ps3, ((uint8_t *)&t67 + 0), t59);
                GM_ST(gm_b16_8, ((uint8_t *)&t67 + 72), t60);
                GM_ST(gm_tapdr2ag17fh7, ((uint8_t *)&t67 + 88), t66);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t67 + 136), t65);
                gm_tq7s98ihonkju t68 = gm_f_new_cnt7p5u((void *)NULL, cx, NULL, &t67, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8upcoeidcs)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l17 = t68;
            }
            uint8_t t69 = (uint8_t)!((((uint8_t *)&gm_l17))[0] != 0);
            if (!t69) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l17) + 8, gm_srcoimgd0jo62fr, 232, 12);
            void * t70 = GM_LD(void *, (((uint8_t *)&gm_l17) + 8));
            gm_l13 = t70;
            void * t71 = gm_l0;
            void * t72 = (void *)(((uint8_t *)t71) + 1248);
            void * t73 = gm_l1;
            gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t73) + 72));
            gm_tftb1odc739t5 t75 = gm_f_push_0mk7udh(t72, GM_CONV(gm_cseahvg8v7p0vs, t74));
            gm_l18 = t75;
            uint8_t t76 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
            if (!t76) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 1, gm_srcoimgd0jo62fr, 233, 5);
            {
                void * t77 = gm_l1;
                uint32_t t78 = GM_LD(uint32_t, ((uint8_t *)t77));
                void * t79 = gm_l13;
                void * t80 = gm_l1;
                gm_tl874t2atnlh3 t81 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t80) + 72));
                gm_t2gtbh39ptekd t82;
                memset(&t82, 0, sizeof t82);
                GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)36ULL));
                GM_ST(void *, ((uint8_t *)&t82 + 8), t79);
                gm_tc3oht4ksth9d t83;
                GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t83 + 84, 0);
                GM_ST(uint32_t, ((uint8_t *)&t83 + 0), t78);
                GM_ST(gm_t2gtbh39ptekd, ((uint8_t *)&t83 + 8), t82);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t83 + 72), t81);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)gm_out) + 89, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)gm_out) + 90, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_t0mfir12leg2g *)gm_out) + 92, 0);
                GM_ST(gm_tc3oht4ksth9d, ((uint8_t *)&(*(gm_t0mfir12leg2g *)gm_out)), t83);
                ((uint8_t *)&(*(gm_t0mfir12leg2g *)gm_out))[88] = 1;
                goto gm_fdone;
            }
        }
gm_lb2:;
        memset(&(*(gm_t0mfir12leg2g *)gm_out), 0, sizeof (*(gm_t0mfir12leg2g *)gm_out));
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
__attribute__((aligned(4))) uint8_t gm_f_iterator_call_as_value_00i5mch(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_iterator_call_as_value_00i5mch F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e0 */
    void * gm_l2; /* v */
    gm_t88rcucg1b7uk gm_l3; /* param */
    void * gm_l4; /* e */
    void * gm_l5; /* x */
    void * gm_l6; /* tys */
    gm_b16_8 gm_l7; /* text */
    uint64_t gm_l8; /* i */
    uint64_t gm_l9; /* i */
    gm_b16_8 gm_l10; /* c */
    gm_b16_8 gm_l11; /* p */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 1262u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l13;
    uint8_t gm_l14;
    uint8_t gm_l15;
    gm_tf5srh4mp4b3i gm_l16;
    gm_tf5srh4mp4b3i gm_l17;
    gm_tabb710d9mq2e gm_l18;
    void * t15 = gm_l1;
    gm_l4 = t15;
    {
        for (;;) {
            void * t16 = gm_l4;
            uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t16) + 8) + 0));
            uint8_t t18 = (uint8_t)(t17 == ((uint8_t)9ULL));
            if (!t18) {
                goto gm_lb1;
            }
            void * t19 = GM_LD(void *, ((((uint8_t *)t16) + 8) + 8));
            gm_l5 = t19;
            {
                void * t20 = gm_l5;
                gm_l4 = t20;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t21 = gm_l0;
    void * t22 = GM_LD(void *, ((uint8_t *)t21));
    void * t23 = (void *)(((uint8_t *)t22) + 8);
    gm_l6 = t23;
    void * t24 = gm_l4;
    {
        {
            uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t24) + 8) + 0));
            uint8_t t26 = (uint8_t)(t25 == ((uint8_t)19ULL));
            if (!t26) {
                goto gm_lb5;
            }
            gm_l15 = ((uint8_t)0);
            goto gm_lb4;
        }
gm_lb5:;
        {
            gm_l15 = ((uint8_t)1);
            goto gm_lb4;
        }
gm_lb6:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 195, 8);
    }
gm_lb4:;
    uint8_t t27 = gm_l15;
    if (t27) {
        gm_l14 = ((uint8_t)1);
    } else {
        void * t28 = gm_l6;
        void * t29 = gm_l0;
        void * t30 = (void *)(((uint8_t *)t29) + 96);
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, ((uint8_t *)t31));
        void * t33 = gm_l2;
        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t33));
        gm_t88rcucg1b7uk t35 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t30, t32, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t36 = gm_f_is_unit_n83ttnb(t28, GM_CONV(gm_csguankko4lmhd, t35));
        uint8_t t37 = (uint8_t)!t36;
        gm_l14 = t37;
    }
    uint8_t t38 = gm_l14;
    if (t38) {
        gm_l13 = ((uint8_t)1);
    } else {
        void * t39 = gm_l6;
        void * t40 = gm_l0;
        void * t41 = (void *)(((uint8_t *)t40) + 96);
        void * t42 = gm_l0;
        void * t43 = GM_LD(void *, ((uint8_t *)t42));
        gm_t88rcucg1b7uk t44 = gm_l3;
        gm_t88rcucg1b7uk t45 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t41, t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t46 = gm_f_is_unit_n83ttnb(t39, GM_CONV(gm_csguankko4lmhd, t45));
        gm_l13 = t46;
    }
    uint8_t t47 = gm_l13;
    if (t47) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t48 = gm_l4;
    gm_tl874t2atnlh3 t49 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t48) + 72));
    void * t50 = gm_l0;
    gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)t50) + 960));
    uint8_t t52 = 0;
    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t51) + 8)); i++) if (gm_eqff85ac10o3(((uint8_t *)GM_LD(void *, ((uint8_t *)&t51))) + i * 12, ((uint8_t *)&t49))) { t52 = 1; break; }
    uint8_t t53 = (uint8_t)(!t52);
    if (t53) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t54 = gm_l0;
    void * t55 = GM_LD(void *, ((uint8_t *)t54));
    void * t56 = GM_LD(void *, ((uint8_t *)t55));
    void * t57 = GM_LD(void *, ((uint8_t *)t56));
    void * t58 = gm_l4;
    uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)t58) + 72));
    void * t60 = gm_f_file_o15fqku(t57, t59);
    void * t61 = GM_LD(void *, (((uint8_t *)t60) + 16));
    uint64_t t62 = GM_LD(uint64_t, ((((uint8_t *)t60) + 16) + 8));
    void * t63 = gm_l4;
    uint32_t t64 = GM_LD(uint32_t, ((((uint8_t *)t63) + 72) + 4));
    uint64_t t65 = (uint64_t)t64;
    void * t66 = gm_l4;
    uint32_t t67 = GM_LD(uint32_t, ((((uint8_t *)t66) + 72) + 8));
    uint64_t t68 = (uint64_t)t67;
    uint64_t t69 = (uintptr_t)t65;
    uint64_t t70 = (uintptr_t)t68;
    if (t69 > t70 || t70 > (uintptr_t)t62) gm_trap_slice(t69, t70, t62, gm_srcoimgd0jo62fr, 197, 13);
    uint64_t t71 = ((uintptr_t)t68) - (uintptr_t)t65;
    gm_b16_8 t72;
    GM_ST(void *, (uint8_t *)&t72, (void *)(((uint8_t *)t61 + (uintptr_t)t65 * 1)));
    GM_ST(uintptr_t, (uint8_t *)&t72 + 8, (uintptr_t)(t71));
    gm_l7 = t72;
    {
        {
            gm_b16_8 t73 = gm_l7;
            gm_b16_8 t74;
            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_smooaouqmm8dj));
            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(1));
            gm_tf5srh4mp4b3i t75 = gm_f_find_nab29co(t73, t74);
            gm_l16 = t75;
            uint8_t t76 = (uint8_t)((((uint8_t *)&gm_l16))[8] != 0);
            if (!t76) {
                goto gm_lb8;
            }
            uint64_t t77 = GM_LD(uint64_t, ((uint8_t *)&gm_l16));
            gm_l8 = t77;
            void * t78 = GM_LD(void *, ((uint8_t *)&gm_l7));
            uint64_t t79 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint64_t t80 = gm_l8;
            uint64_t t81 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t82 = (uintptr_t)t80;
            if (t81 > t82 || t82 > (uintptr_t)t79) gm_trap_slice(t81, t82, t79, gm_srcoimgd0jo62fr, 198, 42);
            uint64_t t83 = ((uintptr_t)t80) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t84;
            GM_ST(void *, (uint8_t *)&t84, (void *)(((uint8_t *)t78 + (uintptr_t)((uint64_t)0ULL) * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(t83));
            gm_l7 = t84;
            goto gm_lb7;
        }
gm_lb8:;
    }
gm_lb7:;
    {
        {
            gm_b16_8 t85 = gm_l7;
            gm_b16_8 t86;
            GM_ST(void *, (uint8_t *)&t86, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(1));
            gm_tf5srh4mp4b3i t87 = gm_f_rfind_ai5s2js(t85, t86);
            gm_l17 = t87;
            uint8_t t88 = (uint8_t)((((uint8_t *)&gm_l17))[8] != 0);
            if (!t88) {
                goto gm_lb10;
            }
            uint64_t t89 = GM_LD(uint64_t, ((uint8_t *)&gm_l17));
            gm_l9 = t89;
            void * t90 = GM_LD(void *, ((uint8_t *)&gm_l7));
            uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
            uint64_t t92 = gm_l9;
            uint64_t r93; bool o94 = __builtin_add_overflow(t92, ((uint64_t)1ULL), &r93);
            if (o94) gm_trap("integer overflow in `+`", gm_srcoimgd0jo62fr, 199, 48);
            uint64_t t95 = r93;
            uint64_t t96 = (uintptr_t)t95;
            uint64_t t97 = (uintptr_t)t91;
            if (t96 > t97 || t97 > (uintptr_t)t91) gm_trap_slice(t96, t97, t91, gm_srcoimgd0jo62fr, 199, 43);
            uint64_t t98 = ((uintptr_t)t91) - (uintptr_t)t95;
            gm_b16_8 t99;
            GM_ST(void *, (uint8_t *)&t99, (void *)(((uint8_t *)t90 + (uintptr_t)t95 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(t98));
            gm_l7 = t99;
            goto gm_lb9;
        }
gm_lb10:;
    }
gm_lb9:;
    gm_b16_8 t100 = gm_l7;
    gm_b16_8 t101 = gm_f_trim_fdrfe6i(t100);
    gm_l10 = t101;
    void * t102 = gm_l0;
    void * t103 = gm_l0;
    void * t104 = (void *)(((uint8_t *)t103) + 96);
    void * t105 = gm_l0;
    void * t106 = GM_LD(void *, ((uint8_t *)t105));
    gm_t88rcucg1b7uk t107 = gm_l3;
    gm_t88rcucg1b7uk t108 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t104, t106, t107);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t109 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t102, t108);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l11 = t109;
    {
        void * t110 = gm_l0;
        void * t111 = gm_l4;
        gm_tl874t2atnlh3 t112 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t111) + 72));
        gm_b16_8 t113 = gm_l10;
        gm_b16_8 t114 = gm_l11;
        gm_b16_8 t115 = gm_l10;
        gm_b16_8 t116 = gm_l10;
        gm_t0uuhaljsf410 t117;
        GM_ST(gm_b16_8, ((uint8_t *)&t117 + 0), t113);
        GM_ST(gm_b16_8, ((uint8_t *)&t117 + 16), t114);
        GM_ST(gm_b16_8, ((uint8_t *)&t117 + 32), t115);
        GM_ST(gm_b16_8, ((uint8_t *)&t117 + 48), t116);
        gm_f_format_mpnc7rc((void *)&gm_l18, (void *)NULL, cx, NULL, &t117);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t118 = (uint8_t)!((((uint8_t *)&gm_l18))[0] != 0);
        if (!t118) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l18) + 8, gm_srcoimgd0jo62fr, 202, 22);
        gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 8));
        gm_b16_8 t120 = GM_LD(gm_b16_8, ((uint8_t *)&t119));
        gm_b0_1 t121 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t110, t112, t120);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_l12 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t122 = gm_l12;
            void * t123 = gm_l0;
            uint64_t t124 = GM_LD(uint64_t, ((((uint8_t *)t123) + 192) + 8));
            uint8_t t125 = (t122 < t124);
            if (!t125) {
                goto gm_lb11;
            }
            {
                void * t126 = gm_l0;
                void * t127 = GM_LD(void *, (((uint8_t *)t126) + 192));
                uint64_t t128 = GM_LD(uint64_t, ((((uint8_t *)t126) + 192) + 8));
                uint64_t t129 = gm_l12;
                if ((uintptr_t)t129 >= (uintptr_t)(t128)) gm_trap_index((uintptr_t)t129, t128, gm_srcoimgd0jo62fr, 205, 12);
                gm_tl874t2atnlh3 t130 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t127) + (uintptr_t)t129 * 40) + 24));
                void * t131 = gm_l4;
                gm_tl874t2atnlh3 t132 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t131) + 72));
                uint8_t t133 = (gm_eqff85ac10o3(((uint8_t *)&t130), ((uint8_t *)&t132)));
                if (t133) {
                    void * t134 = gm_l0;
                    void * t135 = (void *)(((uint8_t *)t134) + 192);
                    uint64_t t136 = gm_l12;
                    gm_tj44oivvogvtl t137;
                    gm_f_remove_vrfdutr((void *)&t137, t135, t136);
                } else {
                    uint64_t t138 = gm_l12;
                    uint64_t r139; bool o140 = __builtin_add_overflow(t138, ((uint64_t)1ULL), &r139);
                    if (o140) gm_trap("integer overflow in `+`", gm_srcoimgd0jo62fr, 208, 13);
                    uint64_t t141 = r139;
                    gm_l12 = t141;
                }
            }
gm_lb13:;
        }
    }
gm_lb11:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_lift_temps_7c6gk6l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_lift_temps_7c6gk6l F;
    void * gm_l1; /* e */
    void * gm_l3; /* inner */
    uint32_t gm_l4; /* t */
    void * gm_l5; /* v */
    GM_STACK_CHECK(&F, 478u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    uint8_t gm_l6;
    gm_tq7s98ihonkju gm_l7;
    gm_tftb1odc739t5 gm_l8;
    void * t8 = gm_l1;
    {
        {
            uint8_t t9 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t10 = (uint8_t)(t9 == ((uint8_t)34ULL));
            if (!t10) {
                goto gm_lb2;
            }
            gm_l6 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t8) + 8) + 0));
            uint8_t t12 = (uint8_t)(t11 == ((uint8_t)27ULL));
            if (!t12) {
                goto gm_lb3;
            }
            gm_l6 = ((uint8_t)1);
            goto gm_lb1;
        }
gm_lb3:;
        {
            gm_l6 = ((uint8_t)0);
            goto gm_lb1;
        }
gm_lb4:;
        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 175, 8);
    }
gm_lb1:;
    uint8_t t13 = gm_l6;
    if (t13) {
        F.ret = GM_ZERO(gm_b0_1);
        goto gm_fret;
    }
    {
        {
            void * t14 = gm_l1;
            uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t14) + 8) + 0));
            uint8_t t16 = (uint8_t)(t15 == ((uint8_t)25ULL));
            if (!t16) {
                goto gm_lb6;
            }
            void * t17 = GM_LD(void *, ((((uint8_t *)t14) + 8) + 8));
            gm_l3 = t17;
            void * t18 = F.l0;
            void * t19 = gm_l3;
            void * t20 = F.l2;
            gm_b0_1 t21 = gm_f_lift_temps_7c6gk6l((void *)NULL, cx, NULL, t18, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t22 = F.l0;
            void * t23 = gm_l3;
            gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t23));
            void * t25 = gm_l3;
            gm_tl874t2atnlh3 t26 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t25) + 72));
            uint32_t t27 = gm_f_temp_ilbvj8q((void *)NULL, cx, NULL, t22, t24, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t27;
            {
                void * t28 = gm_l3;
                gm_tq7s98ihonkju t29 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t28)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kmnip3ksasl)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l7 = t29;
            }
            uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 8, gm_srcoimgd0jo62fr, 179, 14);
            void * t31 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
            gm_l5 = t31;
            {
                void * t32 = gm_l3;
                void * t33 = gm_l3;
                gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t33));
                uint32_t t35 = gm_l4;
                void * t36 = gm_l3;
                gm_tl874t2atnlh3 t37 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t36) + 72));
                gm_to0f5qn75cgg8 t38;
                memset(&t38, 0, sizeof t38);
                GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)15ULL));
                GM_ST(uint32_t, ((uint8_t *)&t38 + 8), t35);
                void * t39 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t34, &t38, t37);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                memmove(((uint8_t *)t32), ((uint8_t *)t39), 88);
            }
            {
                void * t40 = F.l2;
                uint32_t t41 = gm_l4;
                void * t42 = gm_l5;
                gm_ttggq7ng4kp3p t43;
                GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
                GM_ST(uint32_t, ((uint8_t *)&t43 + 0), t41);
                GM_ST(void *, ((uint8_t *)&t43 + 8), t42);
                gm_trcmmd12cefb1 t44;
                GM_ST(uint8_t, (uint8_t *)&t44 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t44 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t44 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t44 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t44 + 0), ((uint8_t)0ULL));
                GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t44 + 8), t43);
                gm_tftb1odc739t5 t45 = gm_f_push_d6c76ud_2(t40, &t44);
                gm_l8 = t45;
            }
            uint8_t t46 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
            if (!t46) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srcoimgd0jo62fr, 181, 9);
            {
                void * t47 = gm_l1;
                void * t48 = gm_l3;
                gm_to0f5qn75cgg8 t49;
                memset(&t49, 0, sizeof t49);
                GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)24ULL));
                GM_ST(void *, ((uint8_t *)&t49 + 8), t48);
                GM_ST(gm_to0f5qn75cgg8, (((uint8_t *)t47) + 8), t49);
            }
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
gm_lb6:;
    }
gm_lb5:;
    void * t50 = gm_l1;
    gm_closure t51 = ((gm_closure){(void *)gm_f_fn_t9suqla, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t50, t51);
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
extern __attribute__((aligned(4))) void * gm_f_local_c3kjspo(void *env, gm_node *cx, gm_node *inst, uint32_t a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void * gm_f_not_an_iterator_au10b3l(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_not_an_iterator_au10b3l F;
    void * gm_l0; /* cx */
    void * gm_l1; /* f */
    void * gm_l2; /* call */
    void * gm_l3; /* e */
    GM_STACK_CHECK(&F, 239u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * gm_l4;
    {
        {
            void * t4 = gm_l0;
            void * t5 = gm_l1;
            void * t6 = gm_l2;
            void * t7 = gm_f_for_each_value_5qklbg7((void *)NULL, cx, NULL, t4, t5, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t7;
            uint8_t t8 = (uint8_t)(GM_LD(void *, ((uint8_t *)&gm_l4)) != 0);
            if (!t8) {
                goto gm_lb2;
            }
            void * t9 = GM_LD(void *, ((uint8_t *)&gm_l4));
            gm_l3 = t9;
            void * t10 = gm_l3;
            F.ret = t10;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, ((uint8_t *)t11));
    void * t13 = (void *)(((uint8_t *)t12) + 8);
    void * t14 = gm_l2;
    gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)t14));
    uint8_t t16 = gm_f_is_error_8k27gd1(t13, GM_CONV(gm_csguankko4lmhd, t15));
    uint8_t t17 = (uint8_t)!t16;
    if (t17) {
        void * t18 = gm_l0;
        void * t19 = gm_l1;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 56));
        gm_tl874t2atnlh3 t21 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t20) + 72));
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sb8airrqr74u5));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(72));
        gm_b0_1 t23 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t18, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    void * t24 = gm_l0;
    void * t25 = GM_LD(void *, ((uint8_t *)t24));
    void * t26 = gm_l1;
    gm_tl874t2atnlh3 t27 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t26) + 104));
    void * t28 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t25, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_place_iter_pre_qn5c5b5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint64_t a3, void * a4) {
    struct gm_fr_place_iter_pre_qn5c5b5 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* bound */
    void * gm_l2; /* written */
    uint64_t gm_l3; /* i */
    void * gm_l4; /* stmts */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* j */
    void * gm_l8; /* v */
    GM_STACK_CHECK(&F, 413u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    void * t9 = gm_l0;
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)t9) + 1160) + 8));
    uint8_t t11 = (t10 > ((uint64_t)0ULL));
    if (t11) {
        void * t12 = gm_l2;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)t12));
        gm_l5 = t13;
        gm_l6 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t14 = gm_l6;
                uint64_t t15 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint8_t t16 = (t14 < t15);
                if (!t16) {
                    goto gm_lb1;
                }
                {
                    void * t17 = GM_LD(void *, ((uint8_t *)&gm_l5));
                    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                    uint64_t t19 = gm_l6;
                    {
                        {
                            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)t17) + (uintptr_t)t19 * 8));
                            gm_l7 = t20;
                            {
                                {
                                    void * t21 = gm_l1;
                                    void * t22 = GM_LD(void *, ((uint8_t *)t21));
                                    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)t21) + 8));
                                    uint64_t t24 = gm_l7;
                                    if ((uintptr_t)t24 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t24, t23, gm_srcoimgd0jo62fr, 244, 21);
                                    uint8_t t25 = (uint8_t)(GM_LD(void *, (((uint8_t *)t22) + (uintptr_t)t24 * 8)) != 0);
                                    if (!t25) {
                                        goto gm_lb7;
                                    }
                                    void * t26 = GM_LD(void *, (((uint8_t *)t22) + (uintptr_t)t24 * 8));
                                    gm_l8 = t26;
                                    void * t27 = gm_l1;
                                    void * t28 = GM_LD(void *, ((uint8_t *)t27));
                                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)t27) + 8));
                                    uint64_t t30 = gm_l7;
                                    if ((uintptr_t)t30 >= (uintptr_t)(t29)) gm_trap_index((uintptr_t)t30, t29, gm_srcoimgd0jo62fr, 244, 38);
                                    void * t31 = gm_l0;
                                    void * t32 = gm_l8;
                                    void * t33 = gm_l4;
                                    void * t34 = gm_f_bind_temp_sa5rcbr((void *)NULL, cx, NULL, t31, t32, t33);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    GM_ST(void *, (((uint8_t *)t28) + (uintptr_t)t30 * 8), t34);
                                    goto gm_lb6;
                                }
gm_lb7:;
                            }
gm_lb6:;
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcoimgd0jo62fr, 243, 9);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t35 = gm_l6;
                uint64_t r36; bool o37 = __builtin_add_overflow(t35, ((uint64_t)1ULL), &r36);
                uint64_t t38 = r36;
                gm_l6 = t38;
            }
        }
gm_lb1:;
        void * t39 = gm_l4;
        void * t40 = gm_l0;
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)t40) + 1160));
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
        gm_tftb1odc739t5 t43 = gm_f_extend_nkql463(t39, t42);
        gm_l9 = t43;
        uint8_t t44 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t44) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srcoimgd0jo62fr, 246, 9);
        void * t45 = gm_l0;
        gm_trqfvmjtt4sij t46;
        gm_f_list_ir0h3tc_2((void *)&t46, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t45) + 1160), t46);
    }
    void * t47 = gm_l2;
    uint64_t t48 = gm_l3;
    gm_tftb1odc739t5 t49 = gm_f_push_ucgmlo2(t47, t48);
    gm_l10 = t49;
    uint8_t t50 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t50) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcoimgd0jo62fr, 249, 5);
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

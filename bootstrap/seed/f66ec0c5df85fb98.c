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
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { int64_t f0; uint8_t f1; uint8_t pe[7]; } gm_t913bb735pvt5;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_ta24rd13gupia;
typedef struct { uint32_t a[1]; } gm_tdvq40fmif5j0;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t913bb735pvt5) == 16 && _Alignof(gm_t913bb735pvt5) == 8 && offsetof(gm_t913bb735pvt5, f0) == 0 && offsetof(gm_t913bb735pvt5, f1) == 8, "layout of `?i64`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta24rd13gupia) == 32 && _Alignof(gm_ta24rd13gupia) == 16 && offsetof(gm_ta24rd13gupia, f0) == 0 && offsetof(gm_ta24rd13gupia, f1) == 16, "layout of `?i128`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tdvq40fmif5j0) == 4 && _Alignof(gm_tdvq40fmif5j0) == 4 && offsetof(gm_tdvq40fmif5j0, a) == 0, "layout of `[1]u32`");
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_trqfvmjtt4sij f0; uint32_t f1; uint8_t pe[4]; } gm_tfqiukge1b9q0;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tfqiukge1b9q0) == 48 && _Alignof(gm_tfqiukge1b9q0) == 8 && offsetof(gm_tfqiukge1b9q0, f0) == 0 && offsetof(gm_tfqiukge1b9q0, f1) == 40, "layout of `(q: List(u32), r: u32)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
struct gm_fr_abs_v7cf214 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_bit_and_3tgam8m {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_bit_or_j1tdrhl {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_bit_xor_tmvdnad {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_bits128_jigkui4 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    unsigned __int128 ret;
};
struct gm_fr_bits_lr3of4i {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint64_t ret;
};
struct gm_fr_bitwise_f0f970e {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* a */
    gm_tbci5ospk5nnn l2; /* b */
    gm_trqfvmjtt4sij l4; /* x */
    gm_trqfvmjtt4sij l5; /* y */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_cmp_i1knrgu {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    int32_t ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_digit_value_8ogbrqp {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_div_f0qrfbr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* q */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_div_floor_u3mfihk {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* q */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_div_mod_floor_eqv1ila {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* q */
    gm_tbci5ospk5nnn l3; /* r */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_div_rem_sc09dcc {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_trqfvmjtt4sij l2; /* q */
    gm_trqfvmjtt4sij l3; /* r */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_extend_7hs67uj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_from_i128_1n0s49q {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_from_twos_hcopd4d {
    gm_frame h;
    gm_trqfvmjtt4sij l0; /* t */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_from_u128_gl5fk0t {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_from_u64_ud13q5v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_gcd_iabkui0 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_trqfvmjtt4sij l2; /* x */
    gm_trqfvmjtt4sij l3; /* y */
    gm_trqfvmjtt4sij l4; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_is_even_3kgcpjs {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_negative_k87pp3n {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_positive_su660ab {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_zero_8tcct9o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mag_add_eq1gkek {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* r */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mag_cmp_s39veq6 {
    gm_frame h;
    int32_t ret;
};
struct gm_fr_mag_divrem_09qfqaq {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* r */
    gm_trqfvmjtt4sij l3; /* q */
    gm_trqfvmjtt4sij l5; /* rl */
    gm_trqfvmjtt4sij l9; /* v */
    gm_trqfvmjtt4sij l10; /* u */
    gm_trqfvmjtt4sij l11; /* q */
    gm_trqfvmjtt4sij l32; /* r */
    gm_td7pkamr64l0c ret;
};
struct gm_fr_mag_divrem_small_1qs5pmr {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* q */
    gm_tfqiukge1b9q0 ret;
};
struct gm_fr_mag_inc_ku3vbu2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mag_low_bits_set_mr405qt {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_mag_mul_if5s38k {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* r */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mag_shl_5df643p {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* r */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mag_shr_lfje1ah {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* r */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mag_sub_ro7ahl7 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* r */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_mag_to_u128_f843dal {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t9p38b040grms ret;
};
struct gm_fr_make_r4pbu77 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* m */
    gm_trqfvmjtt4sij l2; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_mul_nfj5rjv {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_parse_fbta82r {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* kept */
    gm_trqfvmjtt4sij l9; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_pow_mhlb5fr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* base */
    gm_tbci5ospk5nnn l2; /* result */
    gm_tbci5ospk5nnn l3; /* sq */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_2onqfca {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_rem_ib4l1vl {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_reserve_8hbo1q9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_shl_small_pijgvpq {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* r */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_shr_4jp99a9 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l2; /* m */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_signed_add_ek0dpmo {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_sub_pktm5cr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_to_i128_o7ud1ue {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_ta24rd13gupia ret;
};
struct gm_fr_to_i64_4cgpvak {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t913bb735pvt5 ret;
};
struct gm_fr_to_twos_475ok28 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l2; /* r */
    gm_trqfvmjtt4sij ret;
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
struct gm_fr_trim_8rdppmk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_eoppkeo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_6r0n90c {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_cfkbroh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_write_decimal_ke367ab {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l2; /* chunks */
    gm_tfqiukge1b9q0 l4; /* qr */
    gm_b0_1 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_zeros_h0vjgnm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    gm_trqfvmjtt4sij ret;
};
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_abs_v7cf214(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_bit_and_3tgam8m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_bit_or_j1tdrhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_bit_xor_tmvdnad(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4_c(gm_tbci5ospk5nnn a0);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i_c(gm_tbci5ospk5nnn a0);
__attribute__((aligned(4))) void gm_f_bitwise_f0f970e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, const void * a1, const void * a2);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu_c(gm_tbci5ospk5nnn a0, gm_tbci5ospk5nnn a1);
__attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) uint32_t gm_f_digit_value_8ogbrqp(uint8_t a0);
inline __attribute__((aligned(4))) void gm_f_div_f0qrfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_div_floor_u3mfihk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_div_mod_floor_eqv1ila(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_div_rem_sc09dcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21_c(gm_tbci5ospk5nnn a0, gm_tbci5ospk5nnn a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_from_i128_1n0s49q(void *gm_out, void *env, gm_node *cx, gm_node *inst, __int128 a0);
__attribute__((aligned(4))) void gm_f_from_twos_hcopd4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u128_gl5fk0t(void *gm_out, void *env, gm_node *cx, gm_node *inst, unsigned __int128 a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void gm_f_gcd_iabkui0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_even_3kgcpjs(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_even_3kgcpjs_c(gm_tbci5ospk5nnn a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n_c(gm_tbci5ospk5nnn a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_positive_su660ab(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_positive_su660ab_c(gm_tbci5ospk5nnn a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o_c(gm_tbci5ospk5nnn a0);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_mag_add_eq1gkek(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_divrem_09qfqaq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_divrem_small_1qs5pmr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_mag_inc_ku3vbu2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_mag_low_bits_set_mr405qt(gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_mag_mul_if5s38k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_mag_shr_lfje1ah(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_mag_sub_ro7ahl7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_t9p38b040grms gm_f_mag_to_u128_f843dal_c(gm_tbci5ospk5nnn a0);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
__attribute__((aligned(4))) gm_tbci5ospk5nnn gm_f_make_r4pbu77_c(uint8_t a0, gm_trqfvmjtt4sij a1);
inline __attribute__((aligned(4))) void gm_f_mul_nfj5rjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_parse_fbta82r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_pow_mhlb5fr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) void gm_f_rem_ib4l1vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_8hbo1q9(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_shl_small_pijgvpq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_shr_4jp99a9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_to_i128_o7ud1ue(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_ta24rd13gupia gm_f_to_i128_o7ud1ue_c(gm_tbci5ospk5nnn a0);
inline __attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_to_i64_4cgpvak(const void * a0);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_to_i64_4cgpvak_c(gm_tbci5ospk5nnn a0);
__attribute__((aligned(4))) void gm_f_to_twos_475ok28(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_t9p38b040grms gm_f_to_u128_haka99r_c(gm_tbci5ospk5nnn a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da_c(gm_tbci5ospk5nnn a0);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
inline __attribute__((aligned(4))) void gm_f_truncate_eoppkeo(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6r0n90c(void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cfkbroh(void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e55tuqo(void * a0, gm_t88rcucg1b7uk a1, void * a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_zeros_h0vjgnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
extern const uint8_t gm_s4pru1na1k33s[3];
extern const uint8_t gm_s5gbgbdradbg1[25];
extern const uint8_t gm_s76hci78s6ds4[6];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_tmpl gm_tplk34ojqiaho84;
extern const gm_tmpl gm_tplp0kc3a3fekq4;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint8_t, a) == GM_LD(uint8_t, b);
}
inline __attribute__((aligned(4))) void gm_f_abs_v7cf214(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_abs_v7cf214 F;
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
        gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    GM_ST(uint8_t, ((uint8_t *)&F.l1), ((uint8_t)0));
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
inline __attribute__((aligned(4))) void gm_f_bit_and_3tgam8m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_bit_and_3tgam8m F;
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_t9qkh0rsooc8d t2;
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)0ULL));
    gm_tbci5ospk5nnn t3 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l0));
    gm_tbci5ospk5nnn t4 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
    gm_f_bitwise_f0f970e((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t2, &t3, &t4);
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
inline __attribute__((aligned(4))) void gm_f_bit_or_j1tdrhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_bit_or_j1tdrhl F;
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_t9qkh0rsooc8d t2;
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)1ULL));
    gm_tbci5ospk5nnn t3 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l0));
    gm_tbci5ospk5nnn t4 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
    gm_f_bitwise_f0f970e((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t2, &t3, &t4);
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
inline __attribute__((aligned(4))) void gm_f_bit_xor_tmvdnad(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_bit_xor_tmvdnad F;
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t9qkh0rsooc8d t2;
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)2ULL));
    gm_tbci5ospk5nnn t3 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
    gm_tbci5ospk5nnn t4 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
    gm_f_bitwise_f0f970e((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t2, &t3, &t4);
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
inline __attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_cmp_i1knrgu F;
    int32_t gm_l2; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t4 = (!(t2 == t3));
    if (t4) {
        uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a0));
        if (t5) {
            F.ret = ((int32_t)-1LL);
            goto gm_fret;
        }
        F.ret = ((int32_t)1LL);
        goto gm_fret;
    }
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    int32_t t10 = gm_f_mag_cmp_s39veq6(t7, t9);
    gm_l2 = t10;
    uint8_t t11 = GM_LD(uint8_t, ((uint8_t *)a0));
    if (t11) {
        int32_t t12 = gm_l2;
        if (t12 == ((int32_t)-2147483648LL)) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 360, 28);
        int32_t t13 = (int32_t)((uint32_t)0 - (uint32_t)t12);
        F.ret = t13;
        goto gm_fret;
    }
    int32_t t14 = gm_l2;
    F.ret = t14;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_div_f0qrfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_div_f0qrfbr F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tngtuiuskn938 gm_l3;
    {
        gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l0));
        gm_tbci5ospk5nnn t3 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
        gm_f_div_rem_sc09dcc((void *)&gm_l3, (void *)NULL, cx, NULL, &t2, &t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l3), 48);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 48);
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
inline __attribute__((aligned(4))) void gm_f_div_floor_u3mfihk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_div_floor_u3mfihk F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tngtuiuskn938 gm_l3;
    {
        gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
        gm_tbci5ospk5nnn t3 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
        gm_f_div_mod_floor_eqv1ila((void *)&gm_l3, (void *)NULL, cx, NULL, &t2, &t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l3), 48);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 48);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_7hs67uj F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_2onqfca(t12, t13, t14);
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
inline __attribute__((aligned(4))) void gm_f_from_i128_1n0s49q(void *gm_out, void *env, gm_node *cx, gm_node *inst, __int128 a0) {
    struct gm_fr_from_i128_1n0s49q F;
    __int128 gm_l0; /* v */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        __int128 t2 = gm_l0;
        unsigned __int128 t3 = (t2 > ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)) ? (unsigned __int128)t2 - (unsigned __int128)((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)) : (unsigned __int128)((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)) - (unsigned __int128)t2);
        gm_f_from_u128_gl5fk0t((void *)&F.l1, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    __int128 t4 = gm_l0;
    uint8_t t5 = (t4 < ((__int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
    GM_ST(uint8_t, ((uint8_t *)&F.l1), t5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_even_3kgcpjs(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_even_3kgcpjs F;
    GM_STACK_CHECK_SHALLOW(&F, 38u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l1;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (t3) {
        gm_l1 = ((uint8_t)1);
    } else {
        void * t4 = GM_LD(void *, (((uint8_t *)a0) + 8));
        uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
        if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t5)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t5, gm_srclv3pj5es0f8o, 344, 63);
        uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)t4) + (uintptr_t)((uint64_t)0ULL) * 4));
        uint32_t t7 = (uint32_t)(t6 & ((uint32_t)1ULL));
        uint8_t t8 = ((t7 == ((uint32_t)0ULL)));
        gm_l1 = t8;
    }
    uint8_t t9 = gm_l1;
    F.ret = t9;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_positive_su660ab(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_positive_su660ab F;
    GM_STACK_CHECK_SHALLOW(&F, 15u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)a0));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        gm_l1 = t5;
    } else {
        gm_l1 = ((uint8_t)0);
    }
    uint8_t t6 = gm_l1;
    F.ret = t6;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_8tcct9o F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    F.ret = t3;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void gm_f_mag_inc_ku3vbu2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_mag_inc_ku3vbu2 F;
    gm_b16_8 gm_l0; /* a */
    gm_tdvq40fmif5j0 gm_l1; /* one */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tdvq40fmif5j0 t2;
    GM_ST(uint32_t, ((uint8_t *)&t2 + 0), ((uint32_t)1ULL));
    gm_l1 = t2;
    gm_b16_8 t3 = gm_l0;
    void * t4 = (void *)((uint8_t *)&gm_l1);
    uint64_t t5 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t6;
    GM_ST(void *, (uint8_t *)&t6, (void *)(((uint8_t *)t4 + (uintptr_t)((uint64_t)0ULL) * 4)));
    GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(t5));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    gm_f_mag_add_eq1gkek((void *)&(*(gm_trqfvmjtt4sij *)gm_out), (void *)NULL, cx, NULL, t3, t7);
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
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_make_r4pbu77 F;
    uint8_t gm_l0; /* negative */
    GM_STACK_CHECK_SHALLOW(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    uint8_t gm_l3;
    {
        memmove(((uint8_t *)&F.l2), ((uint8_t *)&F.l1), 40);
    }
    void * t2 = (void *)((uint8_t *)&F.l2);
    gm_f_trim_8rdppmk(t2);
    uint8_t t3 = gm_l0;
    if (t3) {
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
        uint8_t t5 = (t4 > ((uint64_t)0ULL));
        gm_l3 = t5;
    } else {
        gm_l3 = ((uint8_t)0);
    }
    {
        uint8_t t6 = gm_l3;
        gm_trqfvmjtt4sij t7 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0), t6);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8), t7);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_mul_nfj5rjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_mul_nfj5rjv F;
    GM_STACK_CHECK(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    memcpy(&F.l1, a1, sizeof F.l1);
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&F.l1));
    uint8_t t4 = (!(t2 == t3));
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_trqfvmjtt4sij t9;
    gm_f_mag_mul_if5s38k((void *)&t9, (void *)NULL, cx, NULL, t6, t8);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t4, &t9);
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
inline __attribute__((aligned(4))) void gm_f_rem_ib4l1vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_rem_ib4l1vl F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tngtuiuskn938 gm_l3;
    {
        gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
        gm_tbci5ospk5nnn t3 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
        gm_f_div_rem_sc09dcc((void *)&gm_l3, (void *)NULL, cx, NULL, &t2, &t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l3) + 48), 48);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 48);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_8hbo1q9(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_reserve_8hbo1q9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK(&F, 137u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    uint64_t t3 = GM_LD(uint64_t, (((uint8_t *)t2) + 8));
    gm_l2 = t3;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)t4) + 16));
    uint64_t t6 = gm_l2;
    uint64_t r7; bool o8 = __builtin_sub_overflow(t5, t6, &r7);
    if (o8) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 18, 8);
    uint64_t t9 = r7;
    uint64_t t10 = gm_l1;
    uint8_t t11 = (t9 >= t10);
    if (t11) {
        gm_tftb1odc739t5 t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
        ((uint8_t *)&t12)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t12 + 1), GM_ZERO(gm_b0_1));
        F.ret = t12;
        goto gm_fret;
    }
    void * t13 = gm_l0;
    uint64_t t14 = gm_l1;
    gm_b16_8 t15 = gm_f_empty_slice_d914i5u();
    gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
    gm_tftb1odc739t5 t17 = gm_f_regrow_2onqfca(t13, t14, t16);
    F.ret = t17;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_shl_qqecl81 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint64_t t5 = gm_l1;
    gm_trqfvmjtt4sij t6;
    gm_f_mag_shl_5df643p((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t2, &t6);
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
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_pktm5cr F;
    GM_STACK_CHECK(&F, 67u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a1));
    uint8_t t6 = (uint8_t)!t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t2, t4, t6, t8);
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
inline __attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_to_i64_4cgpvak(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_i64_4cgpvak F;
    __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 233u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_ta24rd13gupia gm_l2;
    {
        gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
        gm_f_to_i128_o7ud1ue((void *)&gm_l2, &t2);
    }
    uint8_t t3 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
    if (t3) {
        __int128 t4 = GM_LD(__int128, ((uint8_t *)&gm_l2));
        gm_l1 = t4;
    } else {
        gm_t913bb735pvt5 t5;
        memset(&t5, 0, sizeof t5);
        F.ret = t5;
        goto gm_fret;
    }
    __int128 t6 = gm_l1;
    gm_t913bb735pvt5 t7;
    int64_t t8 = (int64_t)t6;
    gm_t913bb735pvt5 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 9, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 10, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 12, 0);
    GM_ST(int64_t, ((uint8_t *)&t9), t8);
    ((uint8_t *)&t9)[8] = 1;
    gm_t913bb735pvt5 t10;
    memset(&t10, 0, sizeof t10);
    t7 = ((__int128)(t6) >= ((__int128)(((unsigned __int128)18446744073709551615ULL << 64) | 9223372036854775808ULL)) && ((__int128)(t6) < 0 || (unsigned __int128)(t6) <= (((unsigned __int128)0ULL << 64) | 9223372036854775807ULL))) ? t9 : t10;
    F.ret = t7;
    goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_truncate_eoppkeo(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_eoppkeo F;
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
        GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t8 + (uintptr_t)((uint64_t)0ULL) * 4)));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6r0n90c(void * a0, gm_t88rcucg1b7uk a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_6r0n90c F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* args */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s76hci78s6ds4));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(5));
    gm_tftb1odc739t5 t5 = gm_f_write_e55tuqo(t2, t3, ((void *)&gm_tplp0kc3a3fekq4), t4);
    F.ret = t5;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cfkbroh(void * a0, gm_t88rcucg1b7uk a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_write_cfkbroh F;
    void * gm_l0; /* l */
    gm_t88rcucg1b7uk gm_l1; /* args */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_s4pru1na1k33s));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(2));
    gm_tftb1odc739t5 t5 = gm_f_write_e55tuqo(t2, t3, ((void *)&gm_tplk34ojqiaho84), t4);
    F.ret = t5;
    goto gm_fret;
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
extern __attribute__((aligned(4))) void gm_f_abs_v7cf214(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
extern __attribute__((aligned(4))) void gm_f_bit_and_3tgam8m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
extern __attribute__((aligned(4))) void gm_f_bit_or_j1tdrhl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
extern __attribute__((aligned(4))) void gm_f_bit_xor_tmvdnad(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bits128_jigkui4 F;
    gm_b16_8 gm_l1; /* l */
    unsigned __int128 gm_l2; /* v */
    uint64_t gm_l3; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 302u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    unsigned __int128 gm_l4;
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l1 = t5;
    gm_l2 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint64_t t7 = (t6 < ((uint64_t)4ULL) ? t6 : ((uint64_t)4ULL));
    gm_l3 = t7;
    {
        for (;;) {
            uint64_t t8 = gm_l3;
            uint8_t t9 = (t8 > ((uint64_t)0ULL));
            if (!t9) {
                goto gm_lb1;
            }
            {
                uint64_t t10 = gm_l3;
                uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
                if (o12) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 401, 9);
                uint64_t t13 = r11;
                gm_l3 = t13;
                unsigned __int128 t14 = gm_l2;
                unsigned __int128 n15 = (unsigned __int128)((uint64_t)32ULL);
                if (n15 >= 128) gm_trap_shift((uint64_t)n15, "u128", gm_srclv3pj5es0f8o, 402, 14);
                unsigned __int128 r16 = (unsigned __int128)((unsigned __int128)t14 << (int)n15);
                bool o17 = (unsigned __int128)(r16 >> (int)n15) != t14;
                unsigned __int128 t18 = r16;
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t21 = gm_l3;
                if ((uintptr_t)t21 >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)t21, t20, gm_srclv3pj5es0f8o, 402, 31);
                uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)t19) + (uintptr_t)t21 * 4));
                unsigned __int128 t23 = (unsigned __int128)t22;
                unsigned __int128 t24 = (unsigned __int128)(t18 | t23);
                gm_l2 = t24;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)a0));
    if (t25) {
        unsigned __int128 t26 = gm_l2;
        unsigned __int128 r27; bool o28 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)), t26, &r27);
        unsigned __int128 t29 = r27;
        gm_l4 = t29;
    } else {
        unsigned __int128 t30 = gm_l2;
        gm_l4 = t30;
    }
    unsigned __int128 t31 = gm_l4;
    F.ret = t31;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4_c(gm_tbci5ospk5nnn a0) { return gm_f_bits128_jigkui4(&a0); }
inline __attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bits_lr3of4i F;
    uint64_t gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t2 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    gm_l1 = t2;
    uint64_t t3 = gm_l1;
    uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
    if (t4) {
        F.ret = ((uint64_t)0ULL);
        goto gm_fret;
    }
    uint64_t t5 = gm_l1;
    uint64_t r6; bool o7 = __builtin_sub_overflow(t5, ((uint64_t)1ULL), &r6);
    if (o7) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 370, 16);
    uint64_t t8 = r6;
    uint64_t t9 = (uint64_t)t8;
    uint64_t r10; bool o11 = __builtin_mul_overflow(t9, ((uint64_t)32ULL), &r10);
    if (o11) gm_trap("integer overflow in `*`", gm_srclv3pj5es0f8o, 370, 12);
    uint64_t t12 = r10;
    void * t13 = GM_LD(void *, (((uint8_t *)a0) + 8));
    uint64_t t14 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    uint64_t t15 = gm_l1;
    uint64_t r16; bool o17 = __builtin_sub_overflow(t15, ((uint64_t)1ULL), &r16);
    if (o17) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 370, 56);
    uint64_t t18 = r16;
    if ((uintptr_t)t18 >= (uintptr_t)(t14)) gm_trap_index((uintptr_t)t18, t14, gm_srclv3pj5es0f8o, 370, 44);
    uint32_t t19 = GM_LD(uint32_t, (((uint8_t *)t13) + (uintptr_t)t18 * 4));
    uint32_t t20 = (uint32_t)gm_clz(((unsigned __int128)(uint32_t)(t19) & (((unsigned __int128)1 << 32) - 1)), 32);
    uint32_t r21; bool o22 = __builtin_sub_overflow(((uint32_t)32ULL), t20, &r21);
    if (o22) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 370, 34);
    uint32_t t23 = r21;
    uint64_t t24 = (uint64_t)t23;
    uint64_t r25; bool o26 = __builtin_add_overflow(t12, t24, &r25);
    if (o26) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 370, 12);
    uint64_t t27 = r25;
    F.ret = t27;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i_c(gm_tbci5ospk5nnn a0) { return gm_f_bits_lr3of4i(&a0); }
__attribute__((aligned(4))) void gm_f_bitwise_f0f970e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0, const void * a1, const void * a2) {
    struct gm_fr_bitwise_f0f970e F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    uint64_t gm_l3; /* len */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    GM_STACK_CHECK(&F, 501u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t gm_l9;
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 8));
    gm_l3 = t11;
    uint64_t t12 = GM_LD(uint64_t, ((((uint8_t *)a2) + 8) + 8));
    uint64_t t13 = gm_l3;
    uint8_t t14 = (t12 > t13);
    if (t14) {
        uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)a2) + 8) + 8));
        gm_l3 = t15;
    }
    uint64_t t16 = gm_l3;
    uint64_t r17; bool o18 = __builtin_add_overflow(t16, ((uint64_t)1ULL), &r17);
    if (o18) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 571, 5);
    uint64_t t19 = r17;
    gm_l3 = t19;
    {
        uint64_t t20 = gm_l3;
        gm_tbci5ospk5nnn t21 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
        gm_f_to_twos_475ok28((void *)&F.l4, (void *)NULL, cx, NULL, &t21, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t22 = gm_l3;
        gm_tbci5ospk5nnn t23 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a2));
        gm_f_to_twos_475ok28((void *)&F.l5, (void *)NULL, cx, NULL, &t23, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_l7 = ((uint64_t)0ULL);
    uint64_t t24 = gm_l3;
    gm_l6 = t24;
    {
        for (;;) {
            uint64_t t25 = gm_l7;
            uint64_t t26 = gm_l6;
            uint8_t t27 = (t25 < t26);
            if (!t27) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t28 = gm_l7;
                        gm_l8 = t28;
                        void * t29 = GM_LD(void *, ((uint8_t *)&F.l4));
                        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                        uint64_t t31 = gm_l8;
                        if ((uintptr_t)t31 >= (uintptr_t)(t30)) gm_trap_index((uintptr_t)t31, t30, gm_srclv3pj5es0f8o, 575, 9);
                        {
                            {
                                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                uint8_t t33 = (uint8_t)(t32 == ((uint8_t)0ULL));
                                if (!t33) {
                                    goto gm_lb7;
                                }
                                void * t34 = GM_LD(void *, ((uint8_t *)&F.l4));
                                uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                                uint64_t t36 = gm_l8;
                                if ((uintptr_t)t36 >= (uintptr_t)(t35)) gm_trap_index((uintptr_t)t36, t35, gm_srclv3pj5es0f8o, 576, 21);
                                uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)t34) + (uintptr_t)t36 * 4));
                                void * t38 = GM_LD(void *, ((uint8_t *)&F.l5));
                                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                uint64_t t40 = gm_l8;
                                if ((uintptr_t)t40 >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)t40, t39, gm_srclv3pj5es0f8o, 576, 34);
                                uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)t38) + (uintptr_t)t40 * 4));
                                uint32_t t42 = (uint32_t)(t37 & t41);
                                gm_l9 = t42;
                                goto gm_lb6;
                            }
gm_lb7:;
                            {
                                uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                uint8_t t44 = (uint8_t)(t43 == ((uint8_t)1ULL));
                                if (!t44) {
                                    goto gm_lb8;
                                }
                                void * t45 = GM_LD(void *, ((uint8_t *)&F.l4));
                                uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                                uint64_t t47 = gm_l8;
                                if ((uintptr_t)t47 >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)t47, t46, gm_srclv3pj5es0f8o, 577, 20);
                                uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)t45) + (uintptr_t)t47 * 4));
                                void * t49 = GM_LD(void *, ((uint8_t *)&F.l5));
                                uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                uint64_t t51 = gm_l8;
                                if ((uintptr_t)t51 >= (uintptr_t)(t50)) gm_trap_index((uintptr_t)t51, t50, gm_srclv3pj5es0f8o, 577, 33);
                                uint32_t t52 = GM_LD(uint32_t, (((uint8_t *)t49) + (uintptr_t)t51 * 4));
                                uint32_t t53 = (uint32_t)(t48 | t52);
                                gm_l9 = t53;
                                goto gm_lb6;
                            }
gm_lb8:;
                            {
                                uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
                                uint8_t t55 = (uint8_t)(t54 == ((uint8_t)2ULL));
                                if (!t55) {
                                    goto gm_lb9;
                                }
                                void * t56 = GM_LD(void *, ((uint8_t *)&F.l4));
                                uint64_t t57 = GM_LD(uint64_t, (((uint8_t *)&F.l4) + 8));
                                uint64_t t58 = gm_l8;
                                if ((uintptr_t)t58 >= (uintptr_t)(t57)) gm_trap_index((uintptr_t)t58, t57, gm_srclv3pj5es0f8o, 578, 21);
                                uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)t56) + (uintptr_t)t58 * 4));
                                void * t60 = GM_LD(void *, ((uint8_t *)&F.l5));
                                uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 8));
                                uint64_t t62 = gm_l8;
                                if ((uintptr_t)t62 >= (uintptr_t)(t61)) gm_trap_index((uintptr_t)t62, t61, gm_srclv3pj5es0f8o, 578, 34);
                                uint32_t t63 = GM_LD(uint32_t, (((uint8_t *)t60) + (uintptr_t)t62 * 4));
                                uint32_t t64 = (uint32_t)(t59 ^ t63);
                                gm_l9 = t64;
                                goto gm_lb6;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 575, 22);
                        }
gm_lb6:;
                        uint32_t t65 = gm_l9;
                        GM_ST(uint32_t, (((uint8_t *)t29) + (uintptr_t)t31 * 4), t65);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 574, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t66 = gm_l7;
            uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
            uint64_t t69 = r67;
            gm_l7 = t69;
        }
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t70 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l4));
        gm_f_from_twos_hcopd4d((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, &t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
extern __attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu_c(gm_tbci5ospk5nnn a0, gm_tbci5ospk5nnn a1) { return gm_f_cmp_i1knrgu(&a0, &a1); }
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_copy_pmkg67r F;
    GM_STACK_CHECK(&F, 126u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tftb1odc739t5 gm_l2;
    {
        gm_trqfvmjtt4sij t2;
        gm_f_list_go2qg0b((void *)&t2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t2;
    }
    void * t3 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    gm_tftb1odc739t5 t6 = gm_f_extend_7hs67uj(t3, t5);
    gm_l2 = t6;
    uint8_t t7 = (uint8_t)!((((uint8_t *)&gm_l2))[0] != 0);
    if (!t7) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l2) + 1, gm_srclv3pj5es0f8o, 330, 5);
    {
        uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)a0));
        gm_trqfvmjtt4sij t9 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0), t8);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8), t9);
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
inline __attribute__((aligned(4))) uint32_t gm_f_digit_value_8ogbrqp(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_digit_value_8ogbrqp F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK_SHALLOW(&F, 52u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint32_t gm_l1;
    {
        {
            uint8_t t7 = gm_l0;
            uint8_t t8 = (t7 >= ((uint8_t)48ULL));
            if (!t8) {
                goto gm_lb2;
            }
            uint8_t t9 = (t7 <= ((uint8_t)57ULL));
            if (!t9) {
                goto gm_lb2;
            }
            uint8_t t10 = gm_l0;
            uint8_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint8_t)48ULL), &r11);
            if (o12) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 629, 22);
            uint8_t t13 = r11;
            uint32_t t14 = (uint32_t)t13;
            gm_l1 = t14;
            goto gm_lb1;
        }
gm_lb2:;
        {
            uint8_t t15 = gm_l0;
            uint8_t t16 = (t15 >= ((uint8_t)97ULL));
            if (!t16) {
                goto gm_lb3;
            }
            uint8_t t17 = (t15 <= ((uint8_t)102ULL));
            if (!t17) {
                goto gm_lb3;
            }
            uint8_t t18 = gm_l0;
            uint8_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint8_t)97ULL), &r19);
            if (o20) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 630, 22);
            uint8_t t21 = r19;
            uint32_t t22 = (uint32_t)t21;
            uint32_t r23; bool o24 = __builtin_add_overflow(t22, ((uint32_t)10ULL), &r23);
            if (o24) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 630, 18);
            uint32_t t25 = r23;
            gm_l1 = t25;
            goto gm_lb1;
        }
gm_lb3:;
        {
            uint8_t t26 = gm_l0;
            uint8_t t27 = (t26 >= ((uint8_t)65ULL));
            if (!t27) {
                goto gm_lb4;
            }
            uint8_t t28 = (t26 <= ((uint8_t)70ULL));
            if (!t28) {
                goto gm_lb4;
            }
            uint8_t t29 = gm_l0;
            uint8_t r30; bool o31 = __builtin_sub_overflow(t29, ((uint8_t)65ULL), &r30);
            if (o31) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 631, 22);
            uint8_t t32 = r30;
            uint32_t t33 = (uint32_t)t32;
            uint32_t r34; bool o35 = __builtin_add_overflow(t33, ((uint32_t)10ULL), &r34);
            if (o35) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 631, 18);
            uint32_t t36 = r34;
            gm_l1 = t36;
            goto gm_lb1;
        }
gm_lb4:;
        {
            gm_l1 = ((uint32_t)0ULL);
            goto gm_lb1;
        }
gm_lb5:;
        gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 628, 40);
    }
gm_lb1:;
    uint32_t t37 = gm_l1;
    F.ret = t37;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_div_f0qrfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
extern __attribute__((aligned(4))) void gm_f_div_floor_u3mfihk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_div_mod_floor_eqv1ila(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_div_mod_floor_eqv1ila F;
    GM_STACK_CHECK(&F, 677u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tngtuiuskn938 gm_l4;
    {
        gm_tbci5ospk5nnn t4 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
        gm_tbci5ospk5nnn t5 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
        gm_f_div_rem_sc09dcc((void *)&gm_l4, (void *)NULL, cx, NULL, &t4, &t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l4), 48);
    }
    {
        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l4) + 48), 48);
    }
    {
        {
            {
                uint8_t t6 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
                uint8_t t7 = (uint8_t)!t6;
                if (!t7) {
                    goto gm_lb2;
                }
            }
            uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)&F.l3));
            uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)a1));
            uint8_t t10 = (!(t8 == t9));
            if (!t10) {
                goto gm_lb2;
            }
            {
                gm_tbci5ospk5nnn t11 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l2));
                gm_tbci5ospk5nnn t12;
                gm_f_one_08qg1op((void *)&t12, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbci5ospk5nnn t13;
                gm_f_sub_pktm5cr((void *)&t13, (void *)NULL, cx, NULL, &t11, &t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbci5ospk5nnn t14;
                gm_f_add_420lj6o((void *)&t14, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tngtuiuskn938 t15;
                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t15 + 0), t13);
                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t15 + 48), t14);
                memmove(gm_out, (((uint8_t *)&t15) + 0), 96);
                goto gm_fdone;
            }
        }
gm_lb2:;
    }
gm_lb1:;
    {
        gm_tbci5ospk5nnn t16 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l2));
        gm_tbci5ospk5nnn t17 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l3));
        gm_tngtuiuskn938 t18;
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t18 + 0), t16);
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t18 + 48), t17);
        memmove(gm_out, (((uint8_t *)&t18) + 0), 96);
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
inline __attribute__((aligned(4))) void gm_f_div_rem_sc09dcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_div_rem_sc09dcc F;
    GM_STACK_CHECK(&F, 357u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_td7pkamr64l0c gm_l4;
    {
        uint8_t t2 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l1)));
        if (t2) {
            gm_b16_8 t3;
            GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5gbgbdradbg1));
            GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
            gm_trap_bytes(GM_LD(void *, ((uint8_t *)&t3)), GM_LD(uintptr_t, ((uint8_t *)&t3) + 8), gm_srclv3pj5es0f8o, 457, 22);
        }
    }
    {
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 8));
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_f_mag_divrem_09qfqaq((void *)&gm_l4, (void *)NULL, cx, NULL, t5, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(((uint8_t *)&F.l2), ((uint8_t *)&gm_l4), 40);
    }
    {
        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l4) + 40), 40);
    }
    {
        uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
        uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)&F.l1));
        uint8_t t10 = (!(t8 == t9));
        gm_tbci5ospk5nnn t11;
        gm_f_make_r4pbu77((void *)&t11, t10, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l2)));
        uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
        gm_tbci5ospk5nnn t13;
        gm_f_make_r4pbu77((void *)&t13, t12, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3)));
        gm_tngtuiuskn938 t14;
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t14 + 0), t11);
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t14 + 48), t13);
        memmove(gm_out, (((uint8_t *)&t14) + 0), 96);
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
extern __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21_c(gm_tbci5ospk5nnn a0, gm_tbci5ospk5nnn a1) { return gm_f_eq_60fjn21(&a0, &a1); }
extern __attribute__((aligned(4))) void gm_f_from_i128_1n0s49q(void *gm_out, void *env, gm_node *cx, gm_node *inst, __int128 a0);
inline __attribute__((aligned(4))) void gm_f_from_twos_hcopd4d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_from_twos_hcopd4d F;
    uint64_t gm_l2; /* n */
    uint8_t gm_l3; /* negative */
    uint64_t gm_l4; /* carry */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    uint64_t gm_l8; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 308u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t gm_l9;
    {
        memmove(((uint8_t *)&F.l1), ((uint8_t *)a0), 40);
    }
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 8));
    gm_l2 = t7;
    uint64_t t8 = gm_l2;
    uint8_t t9 = (t8 > ((uint64_t)0ULL));
    if (t9) {
        void * t10 = GM_LD(void *, ((uint8_t *)&F.l1));
        uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 8));
        uint64_t t12 = gm_l2;
        uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
        if (o14) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 554, 36);
        uint64_t t15 = r13;
        if ((uintptr_t)t15 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t15, t11, gm_srclv3pj5es0f8o, 554, 28);
        uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)t10) + (uintptr_t)t15 * 4));
        unsigned __int128 n17 = (unsigned __int128)((uint64_t)31ULL);
        if (n17 >= 32) gm_trap_shift((uint64_t)n17, "u32", gm_srclv3pj5es0f8o, 554, 28);
        uint32_t t18 = (uint32_t)(t16 >> (int)n17);
        uint8_t t19 = ((t18 == ((uint32_t)1ULL)));
        gm_l9 = t19;
    } else {
        gm_l9 = ((uint8_t)0);
    }
    uint8_t t20 = gm_l9;
    gm_l3 = t20;
    uint8_t t21 = gm_l3;
    if (t21) {
        gm_l4 = ((uint64_t)1ULL);
        gm_l6 = ((uint64_t)0ULL);
        uint64_t t22 = gm_l2;
        gm_l5 = t22;
        {
            for (;;) {
                uint64_t t23 = gm_l6;
                uint64_t t24 = gm_l5;
                uint8_t t25 = (t23 < t24);
                if (!t25) {
                    goto gm_lb1;
                }
                {
                    {
                        {
                            uint64_t t26 = gm_l6;
                            gm_l7 = t26;
                            void * t27 = GM_LD(void *, ((uint8_t *)&F.l1));
                            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 8));
                            uint64_t t29 = gm_l7;
                            uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)t27) + (uintptr_t)t29 * 4));
                            uint32_t t31 = (uint32_t)~t30;
                            uint64_t t32 = (uint64_t)t31;
                            uint64_t t33 = gm_l4;
                            uint64_t r34; bool o35 = __builtin_add_overflow(t32, t33, &r34);
                            if (o35) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 558, 18);
                            uint64_t t36 = r34;
                            gm_l8 = t36;
                            void * t37 = GM_LD(void *, ((uint8_t *)&F.l1));
                            uint64_t t38 = GM_LD(uint64_t, (((uint8_t *)&F.l1) + 8));
                            uint64_t t39 = gm_l7;
                            uint64_t t40 = gm_l8;
                            uint32_t t41 = (uint32_t)t40;
                            GM_ST(uint32_t, (((uint8_t *)t37) + (uintptr_t)t39 * 4), t41);
                            uint64_t t42 = gm_l8;
                            unsigned __int128 n43 = (unsigned __int128)((uint64_t)32ULL);
                            if (n43 >= 64) gm_trap_shift((uint64_t)n43, "u64", gm_srclv3pj5es0f8o, 560, 21);
                            uint64_t t44 = (uint64_t)(t42 >> (int)n43);
                            gm_l4 = t44;
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 557, 9);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t45 = gm_l6;
                uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                uint64_t t48 = r46;
                gm_l6 = t48;
            }
        }
gm_lb1:;
    }
    {
        uint8_t t49 = gm_l3;
        gm_trqfvmjtt4sij t50 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
        gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t49, &t50);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_from_u128_gl5fk0t(void *gm_out, void *env, gm_node *cx, gm_node *inst, unsigned __int128 a0) {
    struct gm_fr_from_u128_gl5fk0t F;
    unsigned __int128 gm_l0; /* v */
    unsigned __int128 gm_l2; /* m */
    GM_STACK_CHECK(&F, 210u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l3;
    {
        gm_trqfvmjtt4sij t5;
        gm_f_list_go2qg0b((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t5;
    }
    unsigned __int128 t6 = gm_l0;
    gm_l2 = t6;
    {
        for (;;) {
            unsigned __int128 t7 = gm_l2;
            uint8_t t8 = (t7 > ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)));
            if (!t8) {
                goto gm_lb1;
            }
            {
                void * t9 = (void *)((uint8_t *)&F.l1);
                unsigned __int128 t10 = gm_l2;
                uint32_t t11 = (uint32_t)t10;
                gm_tftb1odc739t5 t12 = gm_f_push_im9d4ro(t9, t11);
                gm_l3 = t12;
                uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
                if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srclv3pj5es0f8o, 313, 9);
                unsigned __int128 t14 = gm_l2;
                unsigned __int128 n15 = (unsigned __int128)((uint64_t)32ULL);
                if (n15 >= 128) gm_trap_shift((uint64_t)n15, "u128", gm_srclv3pj5es0f8o, 314, 9);
                unsigned __int128 t16 = (unsigned __int128)(t14 >> (int)n15);
                gm_l2 = t16;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t17 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0), ((uint8_t)0));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8), t17);
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
inline __attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0) {
    struct gm_fr_from_u64_ud13q5v F;
    uint64_t gm_l0; /* v */
    uint64_t gm_l2; /* m */
    GM_STACK_CHECK(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l3;
    {
        gm_trqfvmjtt4sij t5;
        gm_f_list_go2qg0b((void *)&t5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t5;
    }
    uint64_t t6 = gm_l0;
    gm_l2 = t6;
    {
        for (;;) {
            uint64_t t7 = gm_l2;
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) {
                goto gm_lb1;
            }
            {
                void * t9 = (void *)((uint8_t *)&F.l1);
                uint64_t t10 = gm_l2;
                uint32_t t11 = (uint32_t)t10;
                gm_tftb1odc739t5 t12 = gm_f_push_im9d4ro(t9, t11);
                gm_l3 = t12;
                uint8_t t13 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
                if (!t13) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srclv3pj5es0f8o, 295, 9);
                uint64_t t14 = gm_l2;
                unsigned __int128 n15 = (unsigned __int128)((uint64_t)32ULL);
                if (n15 >= 64) gm_trap_shift((uint64_t)n15, "u64", gm_srclv3pj5es0f8o, 296, 9);
                uint64_t t16 = (uint64_t)(t14 >> (int)n15);
                gm_l2 = t16;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t17 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l1));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0), ((uint8_t)0));
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8), t17);
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
inline __attribute__((aligned(4))) void gm_f_gcd_iabkui0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_gcd_iabkui0 F;
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn gm_l5;
    gm_tbci5ospk5nnn gm_l6;
    gm_td7pkamr64l0c gm_l7;
    {
        gm_tbci5ospk5nnn t5 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
        gm_f_abs_v7cf214((void *)&gm_l5, (void *)NULL, cx, NULL, &t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(((uint8_t *)&F.l2), (((uint8_t *)&gm_l5) + 8), 40);
    }
    {
        gm_tbci5ospk5nnn t6 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a1));
        gm_f_abs_v7cf214((void *)&gm_l6, (void *)NULL, cx, NULL, &t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(((uint8_t *)&F.l3), (((uint8_t *)&gm_l6) + 8), 40);
    }
    {
        for (;;) {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 8));
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) {
                goto gm_lb1;
            }
            {
                {
                    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
                    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
                    gm_b16_8 t11 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
                    gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
                    gm_f_mag_divrem_09qfqaq((void *)&gm_l7, (void *)NULL, cx, NULL, t10, t12);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    memmove(((uint8_t *)&F.l4), (((uint8_t *)&gm_l7) + 40), 40);
                }
                {
                    memmove(((uint8_t *)&F.l2), ((uint8_t *)&F.l3), 40);
                }
                {
                    memmove(((uint8_t *)&F.l3), ((uint8_t *)&F.l4), 40);
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        gm_trqfvmjtt4sij t13 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
        gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), ((uint8_t)0), &t13);
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_even_3kgcpjs(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_even_3kgcpjs_c(gm_tbci5ospk5nnn a0) { return gm_f_is_even_3kgcpjs(&a0); }
extern __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n_c(gm_tbci5ospk5nnn a0) { return gm_f_is_negative_k87pp3n(&a0); }
extern __attribute__((aligned(4))) uint8_t gm_f_is_positive_su660ab(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_positive_su660ab_c(gm_tbci5ospk5nnn a0) { return gm_f_is_positive_su660ab(&a0); }
extern __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0);
__attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o_c(gm_tbci5ospk5nnn a0) { return gm_f_is_zero_8tcct9o(&a0); }
__attribute__((aligned(4))) void gm_f_mag_add_eq1gkek(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_mag_add_eq1gkek F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    gm_b16_8 gm_l2; /* x */
    gm_b16_8 gm_l3; /* y */
    uint64_t gm_l5; /* carry */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    uint64_t gm_l9; /* s */
    GM_STACK_CHECK(&F, 563u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t6j6smj7qoctu gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    gm_b16_8 t7 = gm_l0;
    gm_l2 = t7;
    gm_b16_8 t8 = gm_l1;
    gm_l3 = t8;
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
    uint8_t t11 = (t9 < t10);
    if (t11) {
        gm_b16_8 t12 = gm_l3;
        gm_b16_8 t13 = gm_l2;
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l10 + 0), t12);
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l10 + 16), t13);
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l10));
        gm_l2 = t14;
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 16));
        gm_l3 = t15;
    }
    {
        gm_trqfvmjtt4sij t16;
        gm_f_list_go2qg0b((void *)&t16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t16;
    }
    void * t17 = (void *)((uint8_t *)&F.l4);
    uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    uint64_t r19; bool o20 = __builtin_add_overflow(t18, ((uint64_t)1ULL), &r19);
    if (o20) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 63, 15);
    uint64_t t21 = r19;
    gm_tftb1odc739t5 t22 = gm_f_reserve_8hbo1q9(t17, t21);
    gm_l11 = t22;
    uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srclv3pj5es0f8o, 63, 5);
    gm_l5 = ((uint64_t)0ULL);
    gm_l7 = ((uint64_t)0ULL);
    uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
    gm_l6 = t24;
    {
        for (;;) {
            uint64_t t25 = gm_l7;
            uint64_t t26 = gm_l6;
            uint8_t t27 = (t25 < t26);
            if (!t27) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t28 = gm_l7;
                        gm_l8 = t28;
                        void * t29 = GM_LD(void *, ((uint8_t *)&gm_l2));
                        uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                        uint64_t t31 = gm_l8;
                        uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)t29) + (uintptr_t)t31 * 4));
                        uint64_t t33 = (uint64_t)t32;
                        uint64_t t34 = gm_l5;
                        uint64_t r35; bool o36 = __builtin_add_overflow(t33, t34, &r35);
                        if (o36) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 66, 14);
                        uint64_t t37 = r35;
                        gm_l9 = t37;
                        uint64_t t38 = gm_l8;
                        uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                        uint8_t t40 = (t38 < t39);
                        if (t40) {
                            uint64_t t41 = gm_l9;
                            void * t42 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t44 = gm_l8;
                            uint32_t t45 = GM_LD(uint32_t, (((uint8_t *)t42) + (uintptr_t)t44 * 4));
                            uint64_t t46 = (uint64_t)t45;
                            uint64_t r47; bool o48 = __builtin_add_overflow(t41, t46, &r47);
                            if (o48) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 67, 24);
                            uint64_t t49 = r47;
                            gm_l9 = t49;
                        }
                        void * t50 = (void *)((uint8_t *)&F.l4);
                        uint64_t t51 = gm_l9;
                        uint32_t t52 = (uint32_t)t51;
                        gm_tftb1odc739t5 t53 = gm_f_push_im9d4ro(t50, t52);
                        gm_l12 = t53;
                        uint8_t t54 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t54) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srclv3pj5es0f8o, 68, 9);
                        uint64_t t55 = gm_l9;
                        unsigned __int128 n56 = (unsigned __int128)((uint64_t)32ULL);
                        if (n56 >= 64) gm_trap_shift((uint64_t)n56, "u64", gm_srclv3pj5es0f8o, 69, 17);
                        uint64_t t57 = (uint64_t)(t55 >> (int)n56);
                        gm_l5 = t57;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 65, 5);
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
    uint64_t t62 = gm_l5;
    uint8_t t63 = (t62 > ((uint64_t)0ULL));
    if (t63) {
        void * t64 = (void *)((uint8_t *)&F.l4);
        uint64_t t65 = gm_l5;
        uint32_t t66 = (uint32_t)t65;
        gm_tftb1odc739t5 t67 = gm_f_push_im9d4ro(t64, t66);
        gm_l13 = t67;
        uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
        if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srclv3pj5es0f8o, 71, 20);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l4), 40);
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
inline __attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mag_cmp_s39veq6 F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 229u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t7 = (t5 < t6);
    if (t7) {
        F.ret = ((int32_t)-1LL);
        goto gm_fret;
    }
    uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
    uint8_t t10 = (t8 > t9);
    if (t10) {
        F.ret = ((int32_t)1LL);
        goto gm_fret;
    }
    uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l2 = t11;
    {
        for (;;) {
            uint64_t t12 = gm_l2;
            uint8_t t13 = (t12 > ((uint64_t)0ULL));
            if (!t13) {
                goto gm_lb1;
            }
            {
                uint64_t t14 = gm_l2;
                uint64_t r15; bool o16 = __builtin_sub_overflow(t14, ((uint64_t)1ULL), &r15);
                if (o16) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 51, 9);
                uint64_t t17 = r15;
                gm_l2 = t17;
                void * t18 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t20 = gm_l2;
                if ((uintptr_t)t20 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t20, t19, gm_srclv3pj5es0f8o, 52, 12);
                uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)t18) + (uintptr_t)t20 * 4));
                void * t22 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t24 = gm_l2;
                if ((uintptr_t)t24 >= (uintptr_t)(t23)) gm_trap_index((uintptr_t)t24, t23, gm_srclv3pj5es0f8o, 52, 19);
                uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)t22) + (uintptr_t)t24 * 4));
                uint8_t t26 = (t21 < t25);
                if (t26) {
                    F.ret = ((int32_t)-1LL);
                    goto gm_fret;
                }
                void * t27 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t29 = gm_l2;
                if ((uintptr_t)t29 >= (uintptr_t)(t28)) gm_trap_index((uintptr_t)t29, t28, gm_srclv3pj5es0f8o, 53, 12);
                uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)t27) + (uintptr_t)t29 * 4));
                void * t31 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                uint64_t t33 = gm_l2;
                if ((uintptr_t)t33 >= (uintptr_t)(t32)) gm_trap_index((uintptr_t)t33, t32, gm_srclv3pj5es0f8o, 53, 19);
                uint32_t t34 = GM_LD(uint32_t, (((uint8_t *)t31) + (uintptr_t)t33 * 4));
                uint8_t t35 = (t30 > t34);
                if (t35) {
                    F.ret = ((int32_t)1LL);
                    goto gm_fret;
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    F.ret = ((int32_t)0LL);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_mag_divrem_09qfqaq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_mag_divrem_09qfqaq F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint32_t gm_l4; /* r */
    uint64_t gm_l6; /* n */
    uint64_t gm_l7; /* m */
    uint32_t gm_l8; /* s */
    uint64_t gm_l12; /* base */
    uint64_t gm_l13; /* vtop */
    uint64_t gm_l14; /* vnext */
    uint64_t gm_l15; /* j */
    uint64_t gm_l16; /* num */
    uint64_t gm_l17; /* qhat */
    uint64_t gm_l18; /* rhat */
    uint64_t gm_l19; /* carry */
    uint64_t gm_l20; /* borrow */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* i */
    uint64_t gm_l24; /* p */
    uint64_t gm_l25; /* t */
    uint64_t gm_l26; /* t */
    uint64_t gm_l27; /* c */
    uint64_t gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* i */
    uint64_t gm_l31; /* sum */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* i */
    uint32_t gm_l36; /* x */
    uint32_t gm_l37; /* hi */
    GM_STACK_CHECK(&F, 2803u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l38;
    gm_tfqiukge1b9q0 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    uint8_t gm_l41;
    gm_tftb1odc739t5 gm_l42;
    gm_tftb1odc739t5 gm_l43;
    {
        gm_b16_8 t23 = gm_l0;
        gm_b16_8 t24 = gm_l1;
        int32_t t25 = gm_f_mag_cmp_s39veq6(t23, t24);
        uint8_t t26 = (t25 < ((int32_t)0LL));
        if (t26) {
            {
                gm_trqfvmjtt4sij t27;
                gm_f_list_go2qg0b((void *)&t27, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l2 = t27;
            }
            void * t28 = (void *)((uint8_t *)&F.l2);
            gm_b16_8 t29 = gm_l0;
            gm_tftb1odc739t5 t30 = gm_f_extend_7hs67uj(t28, t29);
            gm_l38 = t30;
            uint8_t t31 = (uint8_t)!((((uint8_t *)&gm_l38))[0] != 0);
            if (!t31) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l38) + 1, gm_srclv3pj5es0f8o, 146, 9);
            {
                gm_trqfvmjtt4sij t32;
                gm_f_list_go2qg0b((void *)&t32, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_trqfvmjtt4sij t33 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
                gm_td7pkamr64l0c t34;
                GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t34 + 0), t32);
                GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t34 + 40), t33);
                memmove(gm_out, (((uint8_t *)&t34) + 0), 80);
                goto gm_fdone;
            }
        }
        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t36 = ((t35 == ((uint64_t)1ULL)));
        if (t36) {
            {
                gm_b16_8 t37 = gm_l0;
                void * t38 = GM_LD(void *, ((uint8_t *)&gm_l1));
                uint64_t t39 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                if ((uintptr_t)((uint64_t)0ULL) >= (uintptr_t)(t39)) gm_trap_index((uintptr_t)((uint64_t)0ULL), t39, gm_srclv3pj5es0f8o, 150, 37);
                uint32_t t40 = GM_LD(uint32_t, (((uint8_t *)t38) + (uintptr_t)((uint64_t)0ULL) * 4));
                gm_f_mag_divrem_small_1qs5pmr((void *)&gm_l39, (void *)NULL, cx, NULL, t37, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                memmove(((uint8_t *)&F.l3), ((uint8_t *)&gm_l39), 40);
            }
            uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)&gm_l39) + 40));
            gm_l4 = t41;
            {
                gm_trqfvmjtt4sij t42;
                gm_f_list_go2qg0b((void *)&t42, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                F.l5 = t42;
            }
            uint32_t t43 = gm_l4;
            uint8_t t44 = (t43 > ((uint32_t)0ULL));
            if (t44) {
                void * t45 = (void *)((uint8_t *)&F.l5);
                uint32_t t46 = gm_l4;
                gm_tftb1odc739t5 t47 = gm_f_push_im9d4ro(t45, t46);
                gm_l40 = t47;
                uint8_t t48 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                if (!t48) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srclv3pj5es0f8o, 152, 20);
            }
            {
                gm_trqfvmjtt4sij t49 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l3));
                gm_trqfvmjtt4sij t50 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l5));
                gm_td7pkamr64l0c t51;
                GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t51 + 0), t49);
                GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t51 + 40), t50);
                memmove(gm_out, (((uint8_t *)&t51) + 0), 80);
                goto gm_fdone;
            }
        }
        uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        gm_l6 = t52;
        uint64_t t53 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t54 = gm_l6;
        uint64_t r55; bool o56 = __builtin_sub_overflow(t53, t54, &r55);
        if (o56) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 156, 10);
        uint64_t t57 = r55;
        gm_l7 = t57;
        void * t58 = GM_LD(void *, ((uint8_t *)&gm_l1));
        uint64_t t59 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t t60 = gm_l6;
        uint64_t r61; bool o62 = __builtin_sub_overflow(t60, ((uint64_t)1ULL), &r61);
        if (o62) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 157, 17);
        uint64_t t63 = r61;
        if ((uintptr_t)t63 >= (uintptr_t)(t59)) gm_trap_index((uintptr_t)t63, t59, gm_srclv3pj5es0f8o, 157, 15);
        uint32_t t64 = GM_LD(uint32_t, (((uint8_t *)t58) + (uintptr_t)t63 * 4));
        uint32_t t65 = (uint32_t)gm_clz(((unsigned __int128)(uint32_t)(t64) & (((unsigned __int128)1 << 32) - 1)), 32);
        gm_l8 = t65;
        gm_b16_8 t66 = gm_l1;
        uint32_t t67 = gm_l8;
        gm_f_shl_small_pijgvpq((void *)&F.l9, (void *)NULL, cx, NULL, t66, t67, ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t68 = gm_l0;
        uint32_t t69 = gm_l8;
        gm_f_shl_small_pijgvpq((void *)&F.l10, (void *)NULL, cx, NULL, t68, t69, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t70 = gm_l7;
        uint64_t r71; bool o72 = __builtin_add_overflow(t70, ((uint64_t)1ULL), &r71);
        if (o72) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 160, 16);
        uint64_t t73 = r71;
        gm_trqfvmjtt4sij t74;
        gm_f_zeros_h0vjgnm((void *)&t74, (void *)NULL, cx, NULL, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l11 = t74;
        gm_l12 = ((uint64_t)4294967296ULL);
        void * t75 = GM_LD(void *, ((uint8_t *)&F.l9));
        uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
        uint64_t t77 = gm_l6;
        uint64_t r78; bool o79 = __builtin_sub_overflow(t77, ((uint64_t)1ULL), &r78);
        if (o79) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 162, 25);
        uint64_t t80 = r78;
        if ((uintptr_t)t80 >= (uintptr_t)(t76)) gm_trap_index((uintptr_t)t80, t76, gm_srclv3pj5es0f8o, 162, 17);
        uint32_t t81 = GM_LD(uint32_t, (((uint8_t *)t75) + (uintptr_t)t80 * 4));
        uint64_t t82 = (uint64_t)t81;
        gm_l13 = t82;
        void * t83 = GM_LD(void *, ((uint8_t *)&F.l9));
        uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
        uint64_t t85 = gm_l6;
        uint64_t r86; bool o87 = __builtin_sub_overflow(t85, ((uint64_t)2ULL), &r86);
        if (o87) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 163, 26);
        uint64_t t88 = r86;
        if ((uintptr_t)t88 >= (uintptr_t)(t84)) gm_trap_index((uintptr_t)t88, t84, gm_srclv3pj5es0f8o, 163, 18);
        uint32_t t89 = GM_LD(uint32_t, (((uint8_t *)t83) + (uintptr_t)t88 * 4));
        uint64_t t90 = (uint64_t)t89;
        gm_l14 = t90;
        uint64_t t91 = gm_l7;
        uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
        if (o93) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 164, 10);
        uint64_t t94 = r92;
        gm_l15 = t94;
        {
            for (;;) {
                uint64_t t95 = gm_l15;
                uint8_t t96 = (t95 > ((uint64_t)0ULL));
                if (!t96) {
                    goto gm_lb1;
                }
                {
                    uint64_t t97 = gm_l15;
                    uint64_t r98; bool o99 = __builtin_sub_overflow(t97, ((uint64_t)1ULL), &r98);
                    if (o99) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 166, 9);
                    uint64_t t100 = r98;
                    gm_l15 = t100;
                    void * t101 = GM_LD(void *, ((uint8_t *)&F.l10));
                    uint64_t t102 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                    uint64_t t103 = gm_l15;
                    uint64_t t104 = gm_l6;
                    uint64_t r105; bool o106 = __builtin_add_overflow(t103, t104, &r105);
                    if (o106) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 167, 29);
                    uint64_t t107 = r105;
                    if ((uintptr_t)t107 >= (uintptr_t)(t102)) gm_trap_index((uintptr_t)t107, t102, gm_srclv3pj5es0f8o, 167, 21);
                    uint32_t t108 = GM_LD(uint32_t, (((uint8_t *)t101) + (uintptr_t)t107 * 4));
                    uint64_t t109 = (uint64_t)t108;
                    unsigned __int128 n110 = (unsigned __int128)((uint64_t)32ULL);
                    if (n110 >= 64) gm_trap_shift((uint64_t)n110, "u64", gm_srclv3pj5es0f8o, 167, 17);
                    uint64_t r111 = (uint64_t)((uint64_t)t109 << (int)n110);
                    bool o112 = (uint64_t)(r111 >> (int)n110) != t109;
                    uint64_t t113 = r111;
                    void * t114 = GM_LD(void *, ((uint8_t *)&F.l10));
                    uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                    uint64_t t116 = gm_l15;
                    uint64_t t117 = gm_l6;
                    uint64_t r118; bool o119 = __builtin_add_overflow(t116, t117, &r118);
                    if (o119) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 167, 59);
                    uint64_t t120 = r118;
                    uint64_t r121; bool o122 = __builtin_sub_overflow(t120, ((uint64_t)1ULL), &r121);
                    if (o122) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 167, 59);
                    uint64_t t123 = r121;
                    if ((uintptr_t)t123 >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)t123, t115, gm_srclv3pj5es0f8o, 167, 51);
                    uint32_t t124 = GM_LD(uint32_t, (((uint8_t *)t114) + (uintptr_t)t123 * 4));
                    uint64_t t125 = (uint64_t)t124;
                    uint64_t t126 = (uint64_t)(t113 | t125);
                    gm_l16 = t126;
                    uint64_t t127 = gm_l16;
                    uint64_t t128 = gm_l13;
                    if (t128 == 0) gm_trap("division by zero", gm_srclv3pj5es0f8o, 168, 17);
                    uint64_t t129 = (uint64_t)(t127 / t128);
                    gm_l17 = t129;
                    uint64_t t130 = gm_l16;
                    uint64_t t131 = gm_l13;
                    if (t131 == 0) gm_trap("division by zero", gm_srclv3pj5es0f8o, 169, 17);
                    uint64_t t132 = (uint64_t)(t130 % t131);
                    gm_l18 = t132;
                    {
                        for (;;) {
                            uint64_t t133 = gm_l17;
                            uint64_t t134 = gm_l12;
                            uint8_t t135 = (t133 >= t134);
                            if (t135) {
                                gm_l41 = ((uint8_t)1);
                            } else {
                                uint64_t t136 = gm_l17;
                                uint64_t t137 = gm_l14;
                                uint64_t r138; bool o139 = __builtin_mul_overflow(t136, t137, &r138);
                                if (o139) gm_trap("integer overflow in `*`", gm_srclv3pj5es0f8o, 170, 31);
                                uint64_t t140 = r138;
                                uint64_t t141 = gm_l18;
                                unsigned __int128 n142 = (unsigned __int128)((uint64_t)32ULL);
                                if (n142 >= 64) gm_trap_shift((uint64_t)n142, "u64", gm_srclv3pj5es0f8o, 170, 48);
                                uint64_t r143 = (uint64_t)((uint64_t)t141 << (int)n142);
                                bool o144 = (uint64_t)(r143 >> (int)n142) != t141;
                                uint64_t t145 = r143;
                                void * t146 = GM_LD(void *, ((uint8_t *)&F.l10));
                                uint64_t t147 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                uint64_t t148 = gm_l15;
                                uint64_t t149 = gm_l6;
                                uint64_t r150; bool o151 = __builtin_add_overflow(t148, t149, &r150);
                                if (o151) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 170, 75);
                                uint64_t t152 = r150;
                                uint64_t r153; bool o154 = __builtin_sub_overflow(t152, ((uint64_t)2ULL), &r153);
                                if (o154) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 170, 75);
                                uint64_t t155 = r153;
                                if ((uintptr_t)t155 >= (uintptr_t)(t147)) gm_trap_index((uintptr_t)t155, t147, gm_srclv3pj5es0f8o, 170, 67);
                                uint32_t t156 = GM_LD(uint32_t, (((uint8_t *)t146) + (uintptr_t)t155 * 4));
                                uint64_t t157 = (uint64_t)t156;
                                uint64_t t158 = (uint64_t)(t145 | t157);
                                uint8_t t159 = (t140 > t158);
                                gm_l41 = t159;
                            }
                            uint8_t t160 = gm_l41;
                            if (!t160) {
                                goto gm_lb4;
                            }
                            {
                                uint64_t t161 = gm_l17;
                                uint64_t r162; bool o163 = __builtin_sub_overflow(t161, ((uint64_t)1ULL), &r162);
                                if (o163) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 171, 13);
                                uint64_t t164 = r162;
                                gm_l17 = t164;
                                uint64_t t165 = gm_l18;
                                uint64_t t166 = gm_l13;
                                uint64_t r167; bool o168 = __builtin_add_overflow(t165, t166, &r167);
                                if (o168) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 172, 13);
                                uint64_t t169 = r167;
                                gm_l18 = t169;
                                uint64_t t170 = gm_l18;
                                uint64_t t171 = gm_l12;
                                uint8_t t172 = (t170 >= t171);
                                if (t172) {
                                    goto gm_lb4;
                                }
                            }
gm_lb6:;
                        }
                    }
gm_lb4:;
                    gm_l19 = ((uint64_t)0ULL);
                    gm_l20 = ((uint64_t)0ULL);
                    gm_l22 = ((uint64_t)0ULL);
                    uint64_t t173 = gm_l6;
                    gm_l21 = t173;
                    {
                        for (;;) {
                            uint64_t t174 = gm_l22;
                            uint64_t t175 = gm_l21;
                            uint8_t t176 = (t174 < t175);
                            if (!t176) {
                                goto gm_lb7;
                            }
                            {
                                {
                                    {
                                        uint64_t t177 = gm_l22;
                                        gm_l23 = t177;
                                        uint64_t t178 = gm_l17;
                                        void * t179 = GM_LD(void *, ((uint8_t *)&F.l9));
                                        uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                        uint64_t t181 = gm_l23;
                                        if ((uintptr_t)t181 >= (uintptr_t)(t180)) gm_trap_index((uintptr_t)t181, t180, gm_srclv3pj5es0f8o, 179, 29);
                                        uint32_t t182 = GM_LD(uint32_t, (((uint8_t *)t179) + (uintptr_t)t181 * 4));
                                        uint64_t t183 = (uint64_t)t182;
                                        uint64_t r184; bool o185 = __builtin_mul_overflow(t178, t183, &r184);
                                        if (o185) gm_trap("integer overflow in `*`", gm_srclv3pj5es0f8o, 179, 18);
                                        uint64_t t186 = r184;
                                        uint64_t t187 = gm_l19;
                                        uint64_t r188; bool o189 = __builtin_add_overflow(t186, t187, &r188);
                                        if (o189) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 179, 18);
                                        uint64_t t190 = r188;
                                        gm_l24 = t190;
                                        uint64_t t191 = gm_l24;
                                        unsigned __int128 n192 = (unsigned __int128)((uint64_t)32ULL);
                                        if (n192 >= 64) gm_trap_shift((uint64_t)n192, "u64", gm_srclv3pj5es0f8o, 180, 21);
                                        uint64_t t193 = (uint64_t)(t191 >> (int)n192);
                                        gm_l19 = t193;
                                        void * t194 = GM_LD(void *, ((uint8_t *)&F.l10));
                                        uint64_t t195 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                        uint64_t t196 = gm_l23;
                                        uint64_t t197 = gm_l15;
                                        uint64_t r198; bool o199 = __builtin_add_overflow(t196, t197, &r198);
                                        if (o199) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 181, 30);
                                        uint64_t t200 = r198;
                                        if ((uintptr_t)t200 >= (uintptr_t)(t195)) gm_trap_index((uintptr_t)t200, t195, gm_srclv3pj5es0f8o, 181, 22);
                                        uint32_t t201 = GM_LD(uint32_t, (((uint8_t *)t194) + (uintptr_t)t200 * 4));
                                        uint64_t t202 = (uint64_t)t201;
                                        uint64_t t203 = gm_l12;
                                        uint64_t r204; bool o205 = __builtin_add_overflow(t202, t203, &r204);
                                        if (o205) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 181, 18);
                                        uint64_t t206 = r204;
                                        uint64_t t207 = gm_l24;
                                        uint64_t t208 = (uint64_t)(t207 & ((uint64_t)4294967295ULL));
                                        uint64_t r209; bool o210 = __builtin_sub_overflow(t206, t208, &r209);
                                        if (o210) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 181, 18);
                                        uint64_t t211 = r209;
                                        uint64_t t212 = gm_l20;
                                        uint64_t r213; bool o214 = __builtin_sub_overflow(t211, t212, &r213);
                                        if (o214) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 181, 18);
                                        uint64_t t215 = r213;
                                        gm_l25 = t215;
                                        void * t216 = GM_LD(void *, ((uint8_t *)&F.l10));
                                        uint64_t t217 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                        uint64_t t218 = gm_l23;
                                        uint64_t t219 = gm_l15;
                                        uint64_t r220; bool o221 = __builtin_add_overflow(t218, t219, &r220);
                                        if (o221) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 182, 21);
                                        uint64_t t222 = r220;
                                        if ((uintptr_t)t222 >= (uintptr_t)(t217)) gm_trap_index((uintptr_t)t222, t217, gm_srclv3pj5es0f8o, 182, 13);
                                        uint64_t t223 = gm_l25;
                                        uint32_t t224 = (uint32_t)t223;
                                        GM_ST(uint32_t, (((uint8_t *)t216) + (uintptr_t)t222 * 4), t224);
                                        uint64_t t225 = gm_l25;
                                        unsigned __int128 n226 = (unsigned __int128)((uint64_t)32ULL);
                                        if (n226 >= 64) gm_trap_shift((uint64_t)n226, "u64", gm_srclv3pj5es0f8o, 183, 27);
                                        uint64_t t227 = (uint64_t)(t225 >> (int)n226);
                                        uint64_t r228; bool o229 = __builtin_sub_overflow(((uint64_t)1ULL), t227, &r228);
                                        if (o229) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 183, 22);
                                        uint64_t t230 = r228;
                                        gm_l20 = t230;
                                        goto gm_lb10;
                                    }
gm_lb11:;
                                    gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 178, 9);
                                }
gm_lb10:;
                            }
gm_lb9:;
                            uint64_t t231 = gm_l22;
                            uint64_t r232; bool o233 = __builtin_add_overflow(t231, ((uint64_t)1ULL), &r232);
                            uint64_t t234 = r232;
                            gm_l22 = t234;
                        }
                    }
gm_lb7:;
                    void * t235 = GM_LD(void *, ((uint8_t *)&F.l10));
                    uint64_t t236 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                    uint64_t t237 = gm_l15;
                    uint64_t t238 = gm_l6;
                    uint64_t r239; bool o240 = __builtin_add_overflow(t237, t238, &r239);
                    if (o240) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 185, 26);
                    uint64_t t241 = r239;
                    if ((uintptr_t)t241 >= (uintptr_t)(t236)) gm_trap_index((uintptr_t)t241, t236, gm_srclv3pj5es0f8o, 185, 18);
                    uint32_t t242 = GM_LD(uint32_t, (((uint8_t *)t235) + (uintptr_t)t241 * 4));
                    uint64_t t243 = (uint64_t)t242;
                    uint64_t t244 = gm_l12;
                    uint64_t r245; bool o246 = __builtin_add_overflow(t243, t244, &r245);
                    if (o246) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 185, 14);
                    uint64_t t247 = r245;
                    uint64_t t248 = gm_l19;
                    uint64_t r249; bool o250 = __builtin_sub_overflow(t247, t248, &r249);
                    if (o250) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 185, 14);
                    uint64_t t251 = r249;
                    uint64_t t252 = gm_l20;
                    uint64_t r253; bool o254 = __builtin_sub_overflow(t251, t252, &r253);
                    if (o254) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 185, 14);
                    uint64_t t255 = r253;
                    gm_l26 = t255;
                    void * t256 = GM_LD(void *, ((uint8_t *)&F.l10));
                    uint64_t t257 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                    uint64_t t258 = gm_l15;
                    uint64_t t259 = gm_l6;
                    uint64_t r260; bool o261 = __builtin_add_overflow(t258, t259, &r260);
                    if (o261) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 186, 17);
                    uint64_t t262 = r260;
                    if ((uintptr_t)t262 >= (uintptr_t)(t257)) gm_trap_index((uintptr_t)t262, t257, gm_srclv3pj5es0f8o, 186, 9);
                    uint64_t t263 = gm_l26;
                    uint32_t t264 = (uint32_t)t263;
                    GM_ST(uint32_t, (((uint8_t *)t256) + (uintptr_t)t262 * 4), t264);
                    uint64_t t265 = gm_l26;
                    unsigned __int128 n266 = (unsigned __int128)((uint64_t)32ULL);
                    if (n266 >= 64) gm_trap_shift((uint64_t)n266, "u64", gm_srclv3pj5es0f8o, 187, 23);
                    uint64_t t267 = (uint64_t)(t265 >> (int)n266);
                    uint64_t r268; bool o269 = __builtin_sub_overflow(((uint64_t)1ULL), t267, &r268);
                    if (o269) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 187, 18);
                    uint64_t t270 = r268;
                    gm_l20 = t270;
                    uint64_t t271 = gm_l20;
                    uint8_t t272 = (t271 > ((uint64_t)0ULL));
                    if (t272) {
                        uint64_t t273 = gm_l17;
                        uint64_t r274; bool o275 = __builtin_sub_overflow(t273, ((uint64_t)1ULL), &r274);
                        if (o275) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 190, 13);
                        uint64_t t276 = r274;
                        gm_l17 = t276;
                        gm_l27 = ((uint64_t)0ULL);
                        gm_l29 = ((uint64_t)0ULL);
                        uint64_t t277 = gm_l6;
                        gm_l28 = t277;
                        {
                            for (;;) {
                                uint64_t t278 = gm_l29;
                                uint64_t t279 = gm_l28;
                                uint8_t t280 = (t278 < t279);
                                if (!t280) {
                                    goto gm_lb12;
                                }
                                {
                                    {
                                        {
                                            uint64_t t281 = gm_l29;
                                            gm_l30 = t281;
                                            void * t282 = GM_LD(void *, ((uint8_t *)&F.l10));
                                            uint64_t t283 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                            uint64_t t284 = gm_l30;
                                            uint64_t t285 = gm_l15;
                                            uint64_t r286; bool o287 = __builtin_add_overflow(t284, t285, &r286);
                                            if (o287) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 193, 36);
                                            uint64_t t288 = r286;
                                            if ((uintptr_t)t288 >= (uintptr_t)(t283)) gm_trap_index((uintptr_t)t288, t283, gm_srclv3pj5es0f8o, 193, 28);
                                            uint32_t t289 = GM_LD(uint32_t, (((uint8_t *)t282) + (uintptr_t)t288 * 4));
                                            uint64_t t290 = (uint64_t)t289;
                                            void * t291 = GM_LD(void *, ((uint8_t *)&F.l9));
                                            uint64_t t292 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                            uint64_t t293 = gm_l30;
                                            if ((uintptr_t)t293 >= (uintptr_t)(t292)) gm_trap_index((uintptr_t)t293, t292, gm_srclv3pj5es0f8o, 193, 50);
                                            uint32_t t294 = GM_LD(uint32_t, (((uint8_t *)t291) + (uintptr_t)t293 * 4));
                                            uint64_t t295 = (uint64_t)t294;
                                            uint64_t r296; bool o297 = __builtin_add_overflow(t290, t295, &r296);
                                            if (o297) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 193, 24);
                                            uint64_t t298 = r296;
                                            uint64_t t299 = gm_l27;
                                            uint64_t r300; bool o301 = __builtin_add_overflow(t298, t299, &r300);
                                            if (o301) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 193, 24);
                                            uint64_t t302 = r300;
                                            gm_l31 = t302;
                                            void * t303 = GM_LD(void *, ((uint8_t *)&F.l10));
                                            uint64_t t304 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                            uint64_t t305 = gm_l30;
                                            uint64_t t306 = gm_l15;
                                            uint64_t r307; bool o308 = __builtin_add_overflow(t305, t306, &r307);
                                            if (o308) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 194, 25);
                                            uint64_t t309 = r307;
                                            if ((uintptr_t)t309 >= (uintptr_t)(t304)) gm_trap_index((uintptr_t)t309, t304, gm_srclv3pj5es0f8o, 194, 17);
                                            uint64_t t310 = gm_l31;
                                            uint32_t t311 = (uint32_t)t310;
                                            GM_ST(uint32_t, (((uint8_t *)t303) + (uintptr_t)t309 * 4), t311);
                                            uint64_t t312 = gm_l31;
                                            unsigned __int128 n313 = (unsigned __int128)((uint64_t)32ULL);
                                            if (n313 >= 64) gm_trap_shift((uint64_t)n313, "u64", gm_srclv3pj5es0f8o, 195, 21);
                                            uint64_t t314 = (uint64_t)(t312 >> (int)n313);
                                            gm_l27 = t314;
                                            goto gm_lb15;
                                        }
gm_lb16:;
                                        gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 192, 13);
                                    }
gm_lb15:;
                                }
gm_lb14:;
                                uint64_t t315 = gm_l29;
                                uint64_t r316; bool o317 = __builtin_add_overflow(t315, ((uint64_t)1ULL), &r316);
                                uint64_t t318 = r316;
                                gm_l29 = t318;
                            }
                        }
gm_lb12:;
                        void * t319 = GM_LD(void *, ((uint8_t *)&F.l10));
                        uint64_t t320 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                        uint64_t t321 = gm_l15;
                        uint64_t t322 = gm_l6;
                        uint64_t r323; bool o324 = __builtin_add_overflow(t321, t322, &r323);
                        if (o324) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 197, 21);
                        uint64_t t325 = r323;
                        if ((uintptr_t)t325 >= (uintptr_t)(t320)) gm_trap_index((uintptr_t)t325, t320, gm_srclv3pj5es0f8o, 197, 13);
                        void * t326 = GM_LD(void *, ((uint8_t *)&F.l10));
                        uint64_t t327 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                        uint64_t t328 = gm_l15;
                        uint64_t t329 = gm_l6;
                        uint64_t r330; bool o331 = __builtin_add_overflow(t328, t329, &r330);
                        if (o331) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 197, 38);
                        uint64_t t332 = r330;
                        if ((uintptr_t)t332 >= (uintptr_t)(t327)) gm_trap_index((uintptr_t)t332, t327, gm_srclv3pj5es0f8o, 197, 30);
                        uint32_t t333 = GM_LD(uint32_t, (((uint8_t *)t326) + (uintptr_t)t332 * 4));
                        uint64_t t334 = gm_l27;
                        uint32_t t335 = (uint32_t)t334;
                        uint32_t r336; bool o337 = __builtin_add_overflow(t333, t335, &r336);
                        uint32_t t338 = r336;
                        GM_ST(uint32_t, (((uint8_t *)t319) + (uintptr_t)t325 * 4), t338);
                    }
                    void * t339 = GM_LD(void *, ((uint8_t *)&F.l11));
                    uint64_t t340 = GM_LD(uint64_t, (((uint8_t *)&F.l11) + 8));
                    uint64_t t341 = gm_l15;
                    if ((uintptr_t)t341 >= (uintptr_t)(t340)) gm_trap_index((uintptr_t)t341, t340, gm_srclv3pj5es0f8o, 199, 9);
                    uint64_t t342 = gm_l17;
                    uint32_t t343 = (uint32_t)t342;
                    GM_ST(uint32_t, (((uint8_t *)t339) + (uintptr_t)t341 * 4), t343);
                }
gm_lb3:;
            }
        }
gm_lb1:;
    }
    {
        void * t344 = (void *)((uint8_t *)&F.l11);
        gm_f_trim_8rdppmk(t344);
        gm_trqfvmjtt4sij t345;
        gm_f_list_go2qg0b((void *)&t345, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l32 = t345;
        void * t346 = (void *)((uint8_t *)&F.l32);
        uint64_t t347 = gm_l6;
        gm_tftb1odc739t5 t348 = gm_f_reserve_8hbo1q9(t346, t347);
        gm_l42 = t348;
        uint8_t t349 = (uint8_t)!((((uint8_t *)&gm_l42))[0] != 0);
        if (!t349) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l42) + 1, gm_srclv3pj5es0f8o, 204, 5);
        gm_l34 = ((uint64_t)0ULL);
        uint64_t t350 = gm_l6;
        gm_l33 = t350;
        {
            for (;;) {
                uint64_t t351 = gm_l34;
                uint64_t t352 = gm_l33;
                uint8_t t353 = (t351 < t352);
                if (!t353) {
                    goto gm_lb17;
                }
                {
                    {
                        {
                            uint64_t t354 = gm_l34;
                            gm_l35 = t354;
                            void * t355 = GM_LD(void *, ((uint8_t *)&F.l10));
                            uint64_t t356 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                            uint64_t t357 = gm_l35;
                            if ((uintptr_t)t357 >= (uintptr_t)(t356)) gm_trap_index((uintptr_t)t357, t356, gm_srclv3pj5es0f8o, 206, 14);
                            uint32_t t358 = GM_LD(uint32_t, (((uint8_t *)t355) + (uintptr_t)t357 * 4));
                            gm_l36 = t358;
                            uint32_t t359 = gm_l8;
                            uint8_t t360 = (t359 > ((uint32_t)0ULL));
                            if (t360) {
                                void * t361 = GM_LD(void *, ((uint8_t *)&F.l10));
                                uint64_t t362 = GM_LD(uint64_t, (((uint8_t *)&F.l10) + 8));
                                uint64_t t363 = gm_l35;
                                uint64_t r364; bool o365 = __builtin_add_overflow(t363, ((uint64_t)1ULL), &r364);
                                if (o365) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 208, 27);
                                uint64_t t366 = r364;
                                if ((uintptr_t)t366 >= (uintptr_t)(t362)) gm_trap_index((uintptr_t)t366, t362, gm_srclv3pj5es0f8o, 208, 19);
                                uint32_t t367 = GM_LD(uint32_t, (((uint8_t *)t361) + (uintptr_t)t366 * 4));
                                gm_l37 = t367;
                                uint64_t t368 = gm_l35;
                                uint64_t r369; bool o370 = __builtin_add_overflow(t368, ((uint64_t)1ULL), &r369);
                                if (o370) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 209, 16);
                                uint64_t t371 = r369;
                                uint64_t t372 = gm_l6;
                                uint8_t t373 = ((t371 == t372));
                                if (t373) {
                                    gm_l37 = ((uint32_t)0ULL);
                                }
                                uint32_t t374 = gm_l36;
                                uint32_t t375 = gm_l8;
                                unsigned __int128 n376 = (unsigned __int128)t375;
                                if (n376 >= 32) gm_trap_shift((uint64_t)n376, "u32", gm_srclv3pj5es0f8o, 210, 18);
                                uint32_t t377 = (uint32_t)(t374 >> (int)n376);
                                uint32_t t378 = gm_l37;
                                uint32_t t379 = gm_l8;
                                uint32_t r380; bool o381 = __builtin_sub_overflow(((uint32_t)32ULL), t379, &r380);
                                if (o381) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 210, 37);
                                uint32_t t382 = r380;
                                unsigned __int128 n383 = (unsigned __int128)t382;
                                if (n383 >= 32) gm_trap_shift((uint64_t)n383, "u32", gm_srclv3pj5es0f8o, 210, 29);
                                uint32_t r384 = (uint32_t)((uint32_t)t378 << (int)n383);
                                bool o385 = (uint32_t)(r384 >> (int)n383) != t378;
                                uint32_t t386 = r384;
                                uint32_t t387 = (uint32_t)(t377 | t386);
                                gm_l36 = t387;
                            }
                            void * t388 = (void *)((uint8_t *)&F.l32);
                            uint32_t t389 = gm_l36;
                            gm_tftb1odc739t5 t390 = gm_f_push_im9d4ro(t388, t389);
                            gm_l43 = t390;
                            uint8_t t391 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                            if (!t391) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_srclv3pj5es0f8o, 212, 9);
                            goto gm_lb20;
                        }
gm_lb21:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 205, 5);
                    }
gm_lb20:;
                }
gm_lb19:;
                uint64_t t392 = gm_l34;
                uint64_t r393; bool o394 = __builtin_add_overflow(t392, ((uint64_t)1ULL), &r393);
                uint64_t t395 = r393;
                gm_l34 = t395;
            }
        }
gm_lb17:;
    }
    {
        void * t396 = (void *)((uint8_t *)&F.l32);
        gm_f_trim_8rdppmk(t396);
        gm_trqfvmjtt4sij t397 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l11));
        gm_trqfvmjtt4sij t398 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l32));
        gm_td7pkamr64l0c t399;
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t399 + 0), t397);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t399 + 40), t398);
        memmove(gm_out, (((uint8_t *)&t399) + 0), 80);
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
inline __attribute__((aligned(4))) void gm_f_mag_divrem_small_1qs5pmr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1) {
    struct gm_fr_mag_divrem_small_1qs5pmr F;
    gm_b16_8 gm_l0; /* a */
    uint32_t gm_l1; /* d */
    uint64_t gm_l3; /* rem */
    uint64_t gm_l4; /* i */
    uint64_t gm_l5; /* cur */
    GM_STACK_CHECK(&F, 377u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        gm_trqfvmjtt4sij t6;
        gm_f_zeros_h0vjgnm((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t6;
    }
    gm_l3 = ((uint64_t)0ULL);
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l4 = t7;
    {
        for (;;) {
            uint64_t t8 = gm_l4;
            uint8_t t9 = (t8 > ((uint64_t)0ULL));
            if (!t9) {
                goto gm_lb1;
            }
            {
                uint64_t t10 = gm_l4;
                uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
                if (o12) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 115, 9);
                uint64_t t13 = r11;
                gm_l4 = t13;
                uint64_t t14 = gm_l3;
                unsigned __int128 n15 = (unsigned __int128)((uint64_t)32ULL);
                if (n15 >= 64) gm_trap_shift((uint64_t)n15, "u64", gm_srclv3pj5es0f8o, 116, 17);
                uint64_t r16 = (uint64_t)((uint64_t)t14 << (int)n15);
                bool o17 = (uint64_t)(r16 >> (int)n15) != t14;
                uint64_t t18 = r16;
                void * t19 = GM_LD(void *, ((uint8_t *)&gm_l0));
                uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                uint64_t t21 = gm_l4;
                if ((uintptr_t)t21 >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)t21, t20, gm_srclv3pj5es0f8o, 116, 35);
                uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)t19) + (uintptr_t)t21 * 4));
                uint64_t t23 = (uint64_t)t22;
                uint64_t t24 = (uint64_t)(t18 | t23);
                gm_l5 = t24;
                void * t25 = GM_LD(void *, ((uint8_t *)&F.l2));
                uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                uint64_t t27 = gm_l4;
                if ((uintptr_t)t27 >= (uintptr_t)(t26)) gm_trap_index((uintptr_t)t27, t26, gm_srclv3pj5es0f8o, 117, 9);
                uint64_t t28 = gm_l5;
                uint32_t t29 = gm_l1;
                uint64_t t30 = (uint64_t)t29;
                if (t30 == 0) gm_trap("division by zero", gm_srclv3pj5es0f8o, 117, 23);
                uint64_t t31 = (uint64_t)(t28 / t30);
                uint32_t t32 = (uint32_t)t31;
                GM_ST(uint32_t, (((uint8_t *)t25) + (uintptr_t)t27 * 4), t32);
                uint64_t t33 = gm_l5;
                uint32_t t34 = gm_l1;
                uint64_t t35 = (uint64_t)t34;
                if (t35 == 0) gm_trap("division by zero", gm_srclv3pj5es0f8o, 118, 15);
                uint64_t t36 = (uint64_t)(t33 % t35);
                gm_l3 = t36;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t37 = (void *)((uint8_t *)&F.l2);
    gm_f_trim_8rdppmk(t37);
    {
        uint64_t t38 = gm_l3;
        gm_trqfvmjtt4sij t39 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
        uint32_t t40 = (uint32_t)t38;
        gm_tfqiukge1b9q0 t41;
        GM_ST(uint32_t, (uint8_t *)&t41 + 44, 0);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t41 + 0), t39);
        GM_ST(uint32_t, ((uint8_t *)&t41 + 40), t40);
        memmove(gm_out, (((uint8_t *)&t41) + 0), 48);
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
extern __attribute__((aligned(4))) void gm_f_mag_inc_ku3vbu2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_mag_low_bits_set_mr405qt(gm_b16_8 a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mag_low_bits_set_mr405qt F;
    gm_b16_8 gm_l0; /* a */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* whole */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint32_t gm_l6; /* bits */
    uint32_t gm_l7; /* mask */
    GM_STACK_CHECK_SHALLOW(&F, 292u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t9 = gm_l1;
    unsigned __int128 n10 = (unsigned __int128)((uint64_t)5ULL);
    if (n10 >= 64) gm_trap_shift((uint64_t)n10, "u64", gm_srclv3pj5es0f8o, 261, 14);
    uint64_t t11 = (uint64_t)(t9 >> (int)n10);
    gm_l2 = t11;
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
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
                        uint64_t t17 = gm_l5;
                        uint64_t t18 = (uint64_t)t17;
                        uint64_t t19 = gm_l2;
                        uint8_t t20 = (t18 >= t19);
                        if (t20) {
                            goto gm_lb1;
                        }
                        void * t21 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t23 = gm_l5;
                        uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)t21) + (uintptr_t)t23 * 4));
                        uint8_t t25 = (!(t24 == ((uint32_t)0ULL)));
                        if (t25) {
                            F.ret = ((uint8_t)1);
                            goto gm_fret;
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 262, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t26 = gm_l4;
            uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
            uint64_t t29 = r27;
            gm_l4 = t29;
        }
    }
gm_lb1:;
    uint64_t t30 = gm_l1;
    uint64_t t31 = (uint64_t)(t30 & ((uint64_t)31ULL));
    uint32_t t32 = (uint32_t)t31;
    gm_l6 = t32;
    {
        {
            uint32_t t33 = gm_l6;
            uint8_t t34 = (t33 > ((uint32_t)0ULL));
            if (!t34) {
                goto gm_lb7;
            }
            uint64_t t35 = gm_l2;
            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t37 = (uint64_t)t36;
            uint8_t t38 = (t35 < t37);
            if (!t38) {
                goto gm_lb7;
            }
            uint32_t t39 = gm_l6;
            unsigned __int128 n40 = (unsigned __int128)t39;
            if (n40 >= 32) gm_trap_shift((uint64_t)n40, "u32", gm_srclv3pj5es0f8o, 268, 18);
            uint32_t r41 = (uint32_t)((uint32_t)((uint32_t)1ULL) << (int)n40);
            bool o42 = (uint32_t)(r41 >> (int)n40) != ((uint32_t)1ULL);
            uint32_t t43 = r41;
            uint32_t r44; bool o45 = __builtin_sub_overflow(t43, ((uint32_t)1ULL), &r44);
            if (o45) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 268, 17);
            uint32_t t46 = r44;
            gm_l7 = t46;
            void * t47 = GM_LD(void *, ((uint8_t *)&gm_l0));
            uint64_t t48 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
            uint64_t t49 = gm_l2;
            uint64_t t50 = (uint64_t)t49;
            if ((uintptr_t)t50 >= (uintptr_t)(t48)) gm_trap_index((uintptr_t)t50, t48, gm_srclv3pj5es0f8o, 269, 13);
            uint32_t t51 = GM_LD(uint32_t, (((uint8_t *)t47) + (uintptr_t)t50 * 4));
            uint32_t t52 = gm_l7;
            uint32_t t53 = (uint32_t)(t51 & t52);
            uint8_t t54 = (!(t53 == ((uint32_t)0ULL)));
            if (t54) {
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto gm_lb6;
        }
gm_lb7:;
    }
gm_lb6:;
    F.ret = ((uint8_t)0);
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_mag_mul_if5s38k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_mag_mul_if5s38k F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* ai */
    uint64_t gm_l7; /* carry */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* j */
    uint64_t gm_l11; /* t */
    GM_STACK_CHECK(&F, 587u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t gm_l12;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t13 = ((t12 == ((uint64_t)0ULL)));
    if (t13) {
        gm_l12 = ((uint8_t)1);
    } else {
        uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
        gm_l12 = t15;
    }
    uint8_t t16 = gm_l12;
    if (t16) {
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)gm_out), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    {
        uint64_t t17 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
        uint64_t r19; bool o20 = __builtin_add_overflow(t17, t18, &r19);
        if (o20) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 93, 16);
        uint64_t t21 = r19;
        gm_trqfvmjtt4sij t22;
        gm_f_zeros_h0vjgnm((void *)&t22, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t22;
    }
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l3 = t23;
    {
        for (;;) {
            uint64_t t24 = gm_l4;
            uint64_t t25 = gm_l3;
            uint8_t t26 = (t24 < t25);
            if (!t26) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t27 = gm_l4;
                        gm_l5 = t27;
                        void * t28 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t30 = gm_l5;
                        uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)t28) + (uintptr_t)t30 * 4));
                        uint64_t t32 = (uint64_t)t31;
                        gm_l6 = t32;
                        uint64_t t33 = gm_l6;
                        uint8_t t34 = ((t33 == ((uint64_t)0ULL)));
                        if (t34) {
                            goto gm_lb3;
                        }
                        gm_l7 = ((uint64_t)0ULL);
                        gm_l9 = ((uint64_t)0ULL);
                        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        gm_l8 = t35;
                        {
                            for (;;) {
                                uint64_t t36 = gm_l9;
                                uint64_t t37 = gm_l8;
                                uint8_t t38 = (t36 < t37);
                                if (!t38) {
                                    goto gm_lb6;
                                }
                                {
                                    {
                                        {
                                            uint64_t t39 = gm_l9;
                                            gm_l10 = t39;
                                            uint64_t t40 = gm_l6;
                                            void * t41 = GM_LD(void *, ((uint8_t *)&gm_l1));
                                            uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                                            uint64_t t43 = gm_l10;
                                            uint32_t t44 = GM_LD(uint32_t, (((uint8_t *)t41) + (uintptr_t)t43 * 4));
                                            uint64_t t45 = (uint64_t)t44;
                                            uint64_t r46; bool o47 = __builtin_mul_overflow(t40, t45, &r46);
                                            if (o47) gm_trap("integer overflow in `*`", gm_srclv3pj5es0f8o, 99, 18);
                                            uint64_t t48 = r46;
                                            void * t49 = GM_LD(void *, ((uint8_t *)&F.l2));
                                            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                            uint64_t t51 = gm_l5;
                                            uint64_t t52 = gm_l10;
                                            uint64_t r53; bool o54 = __builtin_add_overflow(t51, t52, &r53);
                                            if (o54) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 99, 47);
                                            uint64_t t55 = r53;
                                            if ((uintptr_t)t55 >= (uintptr_t)(t50)) gm_trap_index((uintptr_t)t55, t50, gm_srclv3pj5es0f8o, 99, 39);
                                            uint32_t t56 = GM_LD(uint32_t, (((uint8_t *)t49) + (uintptr_t)t55 * 4));
                                            uint64_t t57 = (uint64_t)t56;
                                            uint64_t r58; bool o59 = __builtin_add_overflow(t48, t57, &r58);
                                            if (o59) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 99, 18);
                                            uint64_t t60 = r58;
                                            uint64_t t61 = gm_l7;
                                            uint64_t r62; bool o63 = __builtin_add_overflow(t60, t61, &r62);
                                            if (o63) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 99, 18);
                                            uint64_t t64 = r62;
                                            gm_l11 = t64;
                                            void * t65 = GM_LD(void *, ((uint8_t *)&F.l2));
                                            uint64_t t66 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                                            uint64_t t67 = gm_l5;
                                            uint64_t t68 = gm_l10;
                                            uint64_t r69; bool o70 = __builtin_add_overflow(t67, t68, &r69);
                                            if (o70) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 100, 21);
                                            uint64_t t71 = r69;
                                            if ((uintptr_t)t71 >= (uintptr_t)(t66)) gm_trap_index((uintptr_t)t71, t66, gm_srclv3pj5es0f8o, 100, 13);
                                            uint64_t t72 = gm_l11;
                                            uint32_t t73 = (uint32_t)t72;
                                            GM_ST(uint32_t, (((uint8_t *)t65) + (uintptr_t)t71 * 4), t73);
                                            uint64_t t74 = gm_l11;
                                            unsigned __int128 n75 = (unsigned __int128)((uint64_t)32ULL);
                                            if (n75 >= 64) gm_trap_shift((uint64_t)n75, "u64", gm_srclv3pj5es0f8o, 101, 21);
                                            uint64_t t76 = (uint64_t)(t74 >> (int)n75);
                                            gm_l7 = t76;
                                            goto gm_lb9;
                                        }
gm_lb10:;
                                        gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 98, 9);
                                    }
gm_lb9:;
                                }
gm_lb8:;
                                uint64_t t77 = gm_l9;
                                uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                                uint64_t t80 = r78;
                                gm_l9 = t80;
                            }
                        }
gm_lb6:;
                        void * t81 = GM_LD(void *, ((uint8_t *)&F.l2));
                        uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                        uint64_t t83 = gm_l5;
                        uint64_t t84 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint64_t r85; bool o86 = __builtin_add_overflow(t83, t84, &r85);
                        if (o86) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 103, 17);
                        uint64_t t87 = r85;
                        if ((uintptr_t)t87 >= (uintptr_t)(t82)) gm_trap_index((uintptr_t)t87, t82, gm_srclv3pj5es0f8o, 103, 9);
                        uint64_t t88 = gm_l7;
                        uint32_t t89 = (uint32_t)t88;
                        GM_ST(uint32_t, (((uint8_t *)t81) + (uintptr_t)t87 * 4), t89);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 94, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t90 = gm_l4;
            uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
            uint64_t t93 = r91;
            gm_l4 = t93;
        }
    }
gm_lb1:;
    void * t94 = (void *)((uint8_t *)&F.l2);
    gm_f_trim_8rdppmk(t94);
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_mag_shl_5df643p F;
    gm_b16_8 gm_l0; /* a */
    uint64_t gm_l1; /* n */
    uint64_t gm_l2; /* limbs */
    uint32_t gm_l3; /* bits */
    uint32_t gm_l5; /* carry */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint32_t gm_l8; /* x */
    GM_STACK_CHECK(&F, 380u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
    if (t8) {
        gm_f_list_go2qg0b((void *)&(*(gm_trqfvmjtt4sij *)gm_out), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    uint64_t t9 = gm_l1;
    unsigned __int128 n10 = (unsigned __int128)((uint64_t)5ULL);
    if (n10 >= 64) gm_trap_shift((uint64_t)n10, "u64", gm_srclv3pj5es0f8o, 221, 15);
    uint64_t t11 = (uint64_t)(t9 >> (int)n10);
    uint64_t t12 = (uint64_t)t11;
    gm_l2 = t12;
    uint64_t t13 = gm_l1;
    uint64_t t14 = (uint64_t)(t13 & ((uint64_t)31ULL));
    uint32_t t15 = (uint32_t)t14;
    gm_l3 = t15;
    {
        uint64_t t16 = gm_l2;
        gm_trqfvmjtt4sij t17;
        gm_f_zeros_h0vjgnm((void *)&t17, (void *)NULL, cx, NULL, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l4 = t17;
    }
    void * t18 = (void *)((uint8_t *)&F.l4);
    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
    if (o21) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 224, 15);
    uint64_t t22 = r20;
    gm_tftb1odc739t5 t23 = gm_f_reserve_8hbo1q9(t18, t22);
    gm_l9 = t23;
    uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srclv3pj5es0f8o, 224, 5);
    uint32_t t25 = gm_l3;
    uint8_t t26 = ((t25 == ((uint32_t)0ULL)));
    if (t26) {
        void * t27 = (void *)((uint8_t *)&F.l4);
        gm_b16_8 t28 = gm_l0;
        gm_tftb1odc739t5 t29 = gm_f_extend_7hs67uj(t27, t28);
        gm_l10 = t29;
        uint8_t t30 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
        if (!t30) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srclv3pj5es0f8o, 226, 9);
        memmove(gm_out, ((uint8_t *)&F.l4), 40);
        goto gm_fdone;
    }
    gm_l5 = ((uint32_t)0ULL);
    gm_b16_8 t31 = gm_l0;
    gm_l6 = t31;
    gm_l7 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t32 = gm_l7;
            uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
            uint8_t t34 = (t32 < t33);
            if (!t34) {
                goto gm_lb1;
            }
            {
                void * t35 = GM_LD(void *, ((uint8_t *)&gm_l6));
                uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                uint64_t t37 = gm_l7;
                {
                    {
                        uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)t35) + (uintptr_t)t37 * 4));
                        gm_l8 = t38;
                        void * t39 = (void *)((uint8_t *)&F.l4);
                        uint32_t t40 = gm_l8;
                        uint32_t t41 = gm_l3;
                        unsigned __int128 n42 = (unsigned __int128)t41;
                        if (n42 >= 32) gm_trap_shift((uint64_t)n42, "u32", gm_srclv3pj5es0f8o, 231, 17);
                        uint32_t r43 = (uint32_t)((uint32_t)t40 << (int)n42);
                        bool o44 = (uint32_t)(r43 >> (int)n42) != t40;
                        uint32_t t45 = r43;
                        uint32_t t46 = gm_l5;
                        uint32_t t47 = (uint32_t)(t45 | t46);
                        gm_tftb1odc739t5 t48 = gm_f_push_im9d4ro(t39, t47);
                        gm_l11 = t48;
                        uint8_t t49 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t49) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srclv3pj5es0f8o, 231, 9);
                        uint32_t t50 = gm_l8;
                        uint32_t t51 = gm_l3;
                        uint32_t r52; bool o53 = __builtin_sub_overflow(((uint32_t)32ULL), t51, &r52);
                        if (o53) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 232, 23);
                        uint32_t t54 = r52;
                        unsigned __int128 n55 = (unsigned __int128)t54;
                        if (n55 >= 32) gm_trap_shift((uint64_t)n55, "u32", gm_srclv3pj5es0f8o, 232, 17);
                        uint32_t t56 = (uint32_t)(t50 >> (int)n55);
                        gm_l5 = t56;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 230, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t57 = gm_l7;
            uint64_t r58; bool o59 = __builtin_add_overflow(t57, ((uint64_t)1ULL), &r58);
            uint64_t t60 = r58;
            gm_l7 = t60;
        }
    }
gm_lb1:;
    uint32_t t61 = gm_l5;
    uint8_t t62 = (t61 > ((uint32_t)0ULL));
    if (t62) {
        void * t63 = (void *)((uint8_t *)&F.l4);
        uint32_t t64 = gm_l5;
        gm_tftb1odc739t5 t65 = gm_f_push_im9d4ro(t63, t64);
        gm_l12 = t65;
        uint8_t t66 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t66) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srclv3pj5es0f8o, 234, 20);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l4), 40);
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
__attribute__((aligned(4))) void gm_f_mag_shr_lfje1ah(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1) {
    struct gm_fr_mag_shr_lfje1ah F;
    gm_b16_8 gm_l0; /* a */
    uint64_t gm_l1; /* n */
    uint64_t gm_l3; /* skip */
    uint64_t gm_l4; /* limbs */
    uint32_t gm_l5; /* bits */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    uint32_t gm_l9; /* x */
    uint32_t gm_l10; /* hi */
    GM_STACK_CHECK(&F, 438u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    {
        gm_trqfvmjtt4sij t7;
        gm_f_list_go2qg0b((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t7;
    }
    uint64_t t8 = gm_l1;
    unsigned __int128 n9 = (unsigned __int128)((uint64_t)5ULL);
    if (n9 >= 64) gm_trap_shift((uint64_t)n9, "u64", gm_srclv3pj5es0f8o, 241, 13);
    uint64_t t10 = (uint64_t)(t8 >> (int)n9);
    gm_l3 = t10;
    uint64_t t11 = gm_l3;
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t13 = (uint64_t)t12;
    uint8_t t14 = (t11 >= t13);
    if (t14) {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
        goto gm_fdone;
    }
    uint64_t t15 = gm_l3;
    uint64_t t16 = (uint64_t)t15;
    gm_l4 = t16;
    uint64_t t17 = gm_l1;
    uint64_t t18 = (uint64_t)(t17 & ((uint64_t)31ULL));
    uint32_t t19 = (uint32_t)t18;
    gm_l5 = t19;
    void * t20 = (void *)((uint8_t *)&F.l2);
    uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t t22 = gm_l4;
    uint64_t r23; bool o24 = __builtin_sub_overflow(t21, t22, &r23);
    if (o24) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 245, 15);
    uint64_t t25 = r23;
    gm_tftb1odc739t5 t26 = gm_f_reserve_8hbo1q9(t20, t25);
    gm_l11 = t26;
    uint8_t t27 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
    if (!t27) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srclv3pj5es0f8o, 245, 5);
    uint64_t t28 = gm_l4;
    gm_l7 = t28;
    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l6 = t29;
    {
        for (;;) {
            uint64_t t30 = gm_l7;
            uint64_t t31 = gm_l6;
            uint8_t t32 = (t30 < t31);
            if (!t32) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t33 = gm_l7;
                        gm_l8 = t33;
                        void * t34 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t35 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t36 = gm_l8;
                        uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)t34) + (uintptr_t)t36 * 4));
                        gm_l9 = t37;
                        uint32_t t38 = gm_l5;
                        uint8_t t39 = (t38 > ((uint32_t)0ULL));
                        if (t39) {
                            gm_l10 = ((uint32_t)0ULL);
                            uint64_t t40 = gm_l8;
                            uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                            if (o42) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 250, 16);
                            uint64_t t43 = r41;
                            uint64_t t44 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                            uint8_t t45 = (t43 < t44);
                            if (t45) {
                                void * t46 = GM_LD(void *, ((uint8_t *)&gm_l0));
                                uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                                uint64_t t48 = gm_l8;
                                uint64_t r49; bool o50 = __builtin_add_overflow(t48, ((uint64_t)1ULL), &r49);
                                if (o50) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 250, 39);
                                uint64_t t51 = r49;
                                if ((uintptr_t)t51 >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)t51, t47, gm_srclv3pj5es0f8o, 250, 37);
                                uint32_t t52 = GM_LD(uint32_t, (((uint8_t *)t46) + (uintptr_t)t51 * 4));
                                gm_l10 = t52;
                            }
                            uint32_t t53 = gm_l9;
                            uint32_t t54 = gm_l5;
                            unsigned __int128 n55 = (unsigned __int128)t54;
                            if (n55 >= 32) gm_trap_shift((uint64_t)n55, "u32", gm_srclv3pj5es0f8o, 251, 18);
                            uint32_t t56 = (uint32_t)(t53 >> (int)n55);
                            uint32_t t57 = gm_l10;
                            uint32_t t58 = gm_l5;
                            uint32_t r59; bool o60 = __builtin_sub_overflow(((uint32_t)32ULL), t58, &r59);
                            if (o60) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 251, 40);
                            uint32_t t61 = r59;
                            unsigned __int128 n62 = (unsigned __int128)t61;
                            if (n62 >= 32) gm_trap_shift((uint64_t)n62, "u32", gm_srclv3pj5es0f8o, 251, 32);
                            uint32_t r63 = (uint32_t)((uint32_t)t57 << (int)n62);
                            bool o64 = (uint32_t)(r63 >> (int)n62) != t57;
                            uint32_t t65 = r63;
                            uint32_t t66 = (uint32_t)(t56 | t65);
                            gm_l9 = t66;
                        }
                        void * t67 = (void *)((uint8_t *)&F.l2);
                        uint32_t t68 = gm_l9;
                        gm_tftb1odc739t5 t69 = gm_f_push_im9d4ro(t67, t68);
                        gm_l12 = t69;
                        uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
                        if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srclv3pj5es0f8o, 253, 9);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 246, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t71 = gm_l7;
            uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
            uint64_t t74 = r72;
            gm_l7 = t74;
        }
    }
gm_lb1:;
    void * t75 = (void *)((uint8_t *)&F.l2);
    gm_f_trim_8rdppmk(t75);
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
inline __attribute__((aligned(4))) void gm_f_mag_sub_ro7ahl7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_mag_sub_ro7ahl7 F;
    gm_b16_8 gm_l0; /* a */
    gm_b16_8 gm_l1; /* b */
    uint64_t gm_l3; /* borrow */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    uint64_t gm_l7; /* s */
    uint64_t gm_l8; /* t */
    GM_STACK_CHECK(&F, 376u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    {
        gm_trqfvmjtt4sij t7;
        gm_f_list_go2qg0b((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t7;
    }
    void * t8 = (void *)((uint8_t *)&F.l2);
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_tftb1odc739t5 t10 = gm_f_reserve_8hbo1q9(t8, t9);
    gm_l9 = t10;
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srclv3pj5es0f8o, 78, 5);
    gm_l3 = ((uint64_t)0ULL);
    gm_l5 = ((uint64_t)0ULL);
    uint64_t t12 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    gm_l4 = t12;
    {
        for (;;) {
            uint64_t t13 = gm_l5;
            uint64_t t14 = gm_l4;
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t16 = gm_l5;
                        gm_l6 = t16;
                        gm_l7 = ((uint64_t)0ULL);
                        uint64_t t17 = gm_l6;
                        uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                        uint8_t t19 = (t17 < t18);
                        if (t19) {
                            void * t20 = GM_LD(void *, ((uint8_t *)&gm_l1));
                            uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 8));
                            uint64_t t22 = gm_l6;
                            uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)t20) + (uintptr_t)t22 * 4));
                            uint64_t t24 = (uint64_t)t23;
                            gm_l7 = t24;
                        }
                        void * t25 = GM_LD(void *, ((uint8_t *)&gm_l0));
                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
                        uint64_t t27 = gm_l6;
                        uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)t25) + (uintptr_t)t27 * 4));
                        uint64_t t29 = (uint64_t)t28;
                        uint64_t r30; bool o31 = __builtin_add_overflow(t29, ((uint64_t)4294967296ULL), &r30);
                        if (o31) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 83, 14);
                        uint64_t t32 = r30;
                        uint64_t t33 = gm_l7;
                        uint64_t r34; bool o35 = __builtin_sub_overflow(t32, t33, &r34);
                        if (o35) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 83, 14);
                        uint64_t t36 = r34;
                        uint64_t t37 = gm_l3;
                        uint64_t r38; bool o39 = __builtin_sub_overflow(t36, t37, &r38);
                        if (o39) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 83, 14);
                        uint64_t t40 = r38;
                        gm_l8 = t40;
                        void * t41 = (void *)((uint8_t *)&F.l2);
                        uint64_t t42 = gm_l8;
                        uint32_t t43 = (uint32_t)t42;
                        gm_tftb1odc739t5 t44 = gm_f_push_im9d4ro(t41, t43);
                        gm_l10 = t44;
                        uint8_t t45 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                        if (!t45) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srclv3pj5es0f8o, 84, 9);
                        uint64_t t46 = gm_l8;
                        unsigned __int128 n47 = (unsigned __int128)((uint64_t)32ULL);
                        if (n47 >= 64) gm_trap_shift((uint64_t)n47, "u64", gm_srclv3pj5es0f8o, 85, 23);
                        uint64_t t48 = (uint64_t)(t46 >> (int)n47);
                        uint64_t r49; bool o50 = __builtin_sub_overflow(((uint64_t)1ULL), t48, &r49);
                        if (o50) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 85, 18);
                        uint64_t t51 = r49;
                        gm_l3 = t51;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 80, 5);
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
    void * t56 = (void *)((uint8_t *)&F.l2);
    gm_f_trim_8rdppmk(t56);
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
inline __attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mag_to_u128_f843dal F;
    unsigned __int128 gm_l1; /* r */
    uint64_t gm_l2; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 174u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t5 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    uint8_t t6 = (t5 > ((uint64_t)4ULL));
    if (t6) {
        memset(&(*(gm_t9p38b040grms *)gm_out), 0, sizeof (*(gm_t9p38b040grms *)gm_out));
        goto gm_fdone;
    }
    gm_l1 = ((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL));
    uint64_t t7 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    gm_l2 = t7;
    {
        for (;;) {
            uint64_t t8 = gm_l2;
            uint8_t t9 = (t8 > ((uint64_t)0ULL));
            if (!t9) {
                goto gm_lb1;
            }
            {
                uint64_t t10 = gm_l2;
                uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
                if (o12) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 379, 9);
                uint64_t t13 = r11;
                gm_l2 = t13;
                unsigned __int128 t14 = gm_l1;
                unsigned __int128 n15 = (unsigned __int128)((uint64_t)32ULL);
                if (n15 >= 128) gm_trap_shift((uint64_t)n15, "u128", gm_srclv3pj5es0f8o, 380, 14);
                unsigned __int128 r16 = (unsigned __int128)((unsigned __int128)t14 << (int)n15);
                bool o17 = (unsigned __int128)(r16 >> (int)n15) != t14;
                unsigned __int128 t18 = r16;
                void * t19 = GM_LD(void *, (((uint8_t *)a0) + 8));
                uint64_t t20 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
                uint64_t t21 = gm_l2;
                if ((uintptr_t)t21 >= (uintptr_t)(t20)) gm_trap_index((uintptr_t)t21, t20, gm_srclv3pj5es0f8o, 380, 31);
                uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)t19) + (uintptr_t)t21 * 4));
                unsigned __int128 t23 = (unsigned __int128)t22;
                unsigned __int128 t24 = (unsigned __int128)(t18 | t23);
                gm_l1 = t24;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    unsigned __int128 t25 = gm_l1;
    GM_ST(uint8_t, (uint8_t *)&(*(gm_t9p38b040grms *)gm_out) + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_t9p38b040grms *)gm_out) + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_t9p38b040grms *)gm_out) + 20, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_t9p38b040grms *)gm_out) + 24, 0);
    GM_ST(unsigned __int128, ((uint8_t *)&(*(gm_t9p38b040grms *)gm_out)), t25);
    ((uint8_t *)&(*(gm_t9p38b040grms *)gm_out))[16] = 1;
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_t9p38b040grms gm_f_mag_to_u128_f843dal_c(gm_tbci5ospk5nnn a0) { gm_t9p38b040grms gm_r; gm_f_mag_to_u128_f843dal(&gm_r, &a0); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
__attribute__((aligned(4))) gm_tbci5ospk5nnn gm_f_make_r4pbu77_c(uint8_t a0, gm_trqfvmjtt4sij a1) { gm_tbci5ospk5nnn gm_r; gm_f_make_r4pbu77(&gm_r, a0, &a1); return gm_r; }
extern __attribute__((aligned(4))) void gm_f_mul_nfj5rjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
extern __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
extern __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_parse_fbta82r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1) {
    struct gm_fr_parse_fbta82r F;
    gm_b16_8 gm_l0; /* text */
    uint32_t gm_l1; /* radix */
    gm_b16_8 gm_l2; /* digits */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* c */
    uint32_t gm_l7; /* group */
    uint64_t gm_l8; /* per */
    uint64_t gm_l10; /* start */
    uint64_t gm_l11; /* end */
    uint32_t gm_l12; /* chunk */
    uint32_t gm_l13; /* weight */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint64_t gm_l17; /* carry */
    uint64_t gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    uint64_t gm_l21; /* t */
    GM_STACK_CHECK(&F, 906u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l22;
    gm_tftb1odc739t5 gm_l23;
    gm_b16_8 t23 = gm_l0;
    gm_l2 = t23;
    gm_b16_8 t24 = gm_l0;
    uint8_t t25 = 0;
    uint8_t t26 = ((uint8_t)95ULL);
    for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t24) + 8)); i++) if (gm_eqjmccqdb90f(((uint8_t *)GM_LD(void *, ((uint8_t *)&t24))) + i * 1, ((uint8_t *)&t26))) { t25 = 1; break; }
    uint8_t t27 = (uint8_t)(t25);
    if (t27) {
        gm_trqfvmjtt4sij t28;
        gm_f_list_uvfu1k1((void *)&t28, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t28;
        gm_b16_8 t29 = gm_l0;
        gm_l4 = t29;
        gm_l5 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t30 = gm_l5;
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                uint8_t t32 = (t30 < t31);
                if (!t32) {
                    goto gm_lb1;
                }
                {
                    void * t33 = GM_LD(void *, ((uint8_t *)&gm_l4));
                    uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&gm_l4) + 8));
                    uint64_t t35 = gm_l5;
                    {
                        {
                            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t33) + (uintptr_t)t35 * 1));
                            gm_l6 = t36;
                            uint8_t t37 = gm_l6;
                            uint8_t t38 = (!(t37 == ((uint8_t)95ULL)));
                            if (t38) {
                                void * t39 = (void *)((uint8_t *)&F.l3);
                                uint8_t t40 = gm_l6;
                                gm_tftb1odc739t5 t41 = gm_f_push_t83rg2m(t39, t40);
                                gm_l22 = t41;
                                uint8_t t42 = (uint8_t)!((((uint8_t *)&gm_l22))[0] != 0);
                                if (!t42) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l22) + 1, gm_srclv3pj5es0f8o, 642, 31);
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 641, 9);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t43 = gm_l5;
                uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                uint64_t t46 = r44;
                gm_l5 = t46;
            }
        }
gm_lb1:;
        gm_b16_8 t47 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
        gm_l2 = t48;
    }
    gm_l7 = ((uint32_t)1ULL);
    gm_l8 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint32_t t49 = gm_l7;
            uint64_t t50 = (uint64_t)t49;
            uint32_t t51 = gm_l1;
            uint64_t t52 = (uint64_t)t51;
            uint64_t r53; bool o54 = __builtin_mul_overflow(t50, t52, &r53);
            if (o54) gm_trap("integer overflow in `*`", gm_srclv3pj5es0f8o, 649, 11);
            uint64_t t55 = r53;
            uint8_t t56 = (t55 <= ((uint64_t)4294967295ULL));
            if (!t56) {
                goto gm_lb6;
            }
            {
                uint32_t t57 = gm_l7;
                uint32_t t58 = gm_l1;
                uint32_t r59; bool o60 = __builtin_mul_overflow(t57, t58, &r59);
                if (o60) gm_trap("integer overflow in `*`", gm_srclv3pj5es0f8o, 650, 9);
                uint32_t t61 = r59;
                gm_l7 = t61;
                uint64_t t62 = gm_l8;
                uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                if (o64) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 651, 9);
                uint64_t t65 = r63;
                gm_l8 = t65;
            }
gm_lb8:;
        }
    }
gm_lb6:;
    {
        gm_trqfvmjtt4sij t66;
        gm_f_list_go2qg0b((void *)&t66, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l9 = t66;
    }
    gm_l10 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t67 = gm_l10;
            uint64_t t68 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
            uint8_t t69 = (t67 < t68);
            if (!t69) {
                goto gm_lb9;
            }
            {
                uint64_t t70 = gm_l10;
                uint64_t t71 = gm_l8;
                uint64_t r72; bool o73 = __builtin_add_overflow(t70, t71, &r72);
                if (o73) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 656, 16);
                uint64_t t74 = r72;
                gm_l11 = t74;
                uint64_t t75 = gm_l11;
                uint64_t t76 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                uint8_t t77 = (t75 > t76);
                if (t77) {
                    uint64_t t78 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                    gm_l11 = t78;
                }
                gm_l12 = ((uint32_t)0ULL);
                gm_l13 = ((uint32_t)1ULL);
                uint64_t t79 = gm_l10;
                gm_l15 = t79;
                uint64_t t80 = gm_l11;
                gm_l14 = t80;
                {
                    for (;;) {
                        uint64_t t81 = gm_l15;
                        uint64_t t82 = gm_l14;
                        uint8_t t83 = (t81 < t82);
                        if (!t83) {
                            goto gm_lb12;
                        }
                        {
                            {
                                {
                                    uint64_t t84 = gm_l15;
                                    gm_l16 = t84;
                                    uint32_t t85 = gm_l12;
                                    uint32_t t86 = gm_l1;
                                    uint32_t r87; bool o88 = __builtin_mul_overflow(t85, t86, &r87);
                                    if (o88) gm_trap("integer overflow in `*`", gm_srclv3pj5es0f8o, 661, 21);
                                    uint32_t t89 = r87;
                                    void * t90 = GM_LD(void *, ((uint8_t *)&gm_l2));
                                    uint64_t t91 = GM_LD(uint64_t, (((uint8_t *)&gm_l2) + 8));
                                    uint64_t t92 = gm_l16;
                                    if ((uintptr_t)t92 >= (uintptr_t)(t91)) gm_trap_index((uintptr_t)t92, t91, gm_srclv3pj5es0f8o, 661, 49);
                                    uint8_t t93 = GM_LD(uint8_t, (((uint8_t *)t90) + (uintptr_t)t92 * 1));
                                    uint32_t t94 = gm_f_digit_value_8ogbrqp(t93);
                                    uint32_t r95; bool o96 = __builtin_add_overflow(t89, t94, &r95);
                                    if (o96) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 661, 21);
                                    uint32_t t97 = r95;
                                    gm_l12 = t97;
                                    uint32_t t98 = gm_l13;
                                    uint32_t t99 = gm_l1;
                                    uint32_t r100; bool o101 = __builtin_mul_overflow(t98, t99, &r100);
                                    if (o101) gm_trap("integer overflow in `*`", gm_srclv3pj5es0f8o, 662, 13);
                                    uint32_t t102 = r100;
                                    gm_l13 = t102;
                                    goto gm_lb15;
                                }
gm_lb16:;
                                gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 660, 9);
                            }
gm_lb15:;
                        }
gm_lb14:;
                        uint64_t t103 = gm_l15;
                        uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                        uint64_t t106 = r104;
                        gm_l15 = t106;
                    }
                }
gm_lb12:;
                uint32_t t107 = gm_l12;
                uint64_t t108 = (uint64_t)t107;
                gm_l17 = t108;
                gm_l19 = ((uint64_t)0ULL);
                uint64_t t109 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                gm_l18 = t109;
                {
                    for (;;) {
                        uint64_t t110 = gm_l19;
                        uint64_t t111 = gm_l18;
                        uint8_t t112 = (t110 < t111);
                        if (!t112) {
                            goto gm_lb17;
                        }
                        {
                            {
                                {
                                    uint64_t t113 = gm_l19;
                                    gm_l20 = t113;
                                    void * t114 = GM_LD(void *, ((uint8_t *)&F.l9));
                                    uint64_t t115 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                    uint64_t t116 = gm_l20;
                                    if ((uintptr_t)t116 >= (uintptr_t)(t115)) gm_trap_index((uintptr_t)t116, t115, gm_srclv3pj5es0f8o, 667, 22);
                                    uint32_t t117 = GM_LD(uint32_t, (((uint8_t *)t114) + (uintptr_t)t116 * 4));
                                    uint64_t t118 = (uint64_t)t117;
                                    uint32_t t119 = gm_l13;
                                    uint64_t t120 = (uint64_t)t119;
                                    uint64_t r121; bool o122 = __builtin_mul_overflow(t118, t120, &r121);
                                    if (o122) gm_trap("integer overflow in `*`", gm_srclv3pj5es0f8o, 667, 18);
                                    uint64_t t123 = r121;
                                    uint64_t t124 = gm_l17;
                                    uint64_t r125; bool o126 = __builtin_add_overflow(t123, t124, &r125);
                                    if (o126) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 667, 18);
                                    uint64_t t127 = r125;
                                    gm_l21 = t127;
                                    void * t128 = GM_LD(void *, ((uint8_t *)&F.l9));
                                    uint64_t t129 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                    uint64_t t130 = gm_l20;
                                    if ((uintptr_t)t130 >= (uintptr_t)(t129)) gm_trap_index((uintptr_t)t130, t129, gm_srclv3pj5es0f8o, 668, 13);
                                    uint64_t t131 = gm_l21;
                                    uint32_t t132 = (uint32_t)t131;
                                    GM_ST(uint32_t, (((uint8_t *)t128) + (uintptr_t)t130 * 4), t132);
                                    uint64_t t133 = gm_l21;
                                    unsigned __int128 n134 = (unsigned __int128)((uint64_t)32ULL);
                                    if (n134 >= 64) gm_trap_shift((uint64_t)n134, "u64", gm_srclv3pj5es0f8o, 669, 21);
                                    uint64_t t135 = (uint64_t)(t133 >> (int)n134);
                                    gm_l17 = t135;
                                    goto gm_lb20;
                                }
gm_lb21:;
                                gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 666, 9);
                            }
gm_lb20:;
                        }
gm_lb19:;
                        uint64_t t136 = gm_l19;
                        uint64_t r137; bool o138 = __builtin_add_overflow(t136, ((uint64_t)1ULL), &r137);
                        uint64_t t139 = r137;
                        gm_l19 = t139;
                    }
                }
gm_lb17:;
                uint64_t t140 = gm_l17;
                uint8_t t141 = (t140 > ((uint64_t)0ULL));
                if (t141) {
                    void * t142 = (void *)((uint8_t *)&F.l9);
                    uint64_t t143 = gm_l17;
                    uint32_t t144 = (uint32_t)t143;
                    gm_tftb1odc739t5 t145 = gm_f_push_im9d4ro(t142, t144);
                    gm_l23 = t145;
                    uint8_t t146 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                    if (!t146) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srclv3pj5es0f8o, 671, 24);
                }
                uint64_t t147 = gm_l11;
                gm_l10 = t147;
            }
gm_lb11:;
        }
    }
gm_lb9:;
    {
        gm_trqfvmjtt4sij t148 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l9));
        gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), ((uint8_t)0), &t148);
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
inline __attribute__((aligned(4))) void gm_f_pow_mhlb5fr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint32_t a1) {
    struct gm_fr_pow_mhlb5fr F;
    uint32_t gm_l1; /* exp */
    uint32_t gm_l4; /* e */
    GM_STACK_CHECK(&F, 231u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    {
        gm_f_one_08qg1op((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        memmove(((uint8_t *)&F.l3), ((uint8_t *)a0), 48);
    }
    uint32_t t5 = gm_l1;
    gm_l4 = t5;
    {
        for (;;) {
            uint32_t t6 = gm_l4;
            uint8_t t7 = (t6 > ((uint32_t)0ULL));
            if (!t7) {
                goto gm_lb1;
            }
            {
                uint32_t t8 = gm_l4;
                uint32_t t9 = (uint32_t)(t8 & ((uint32_t)1ULL));
                uint8_t t10 = ((t9 == ((uint32_t)1ULL)));
                if (t10) {
                    gm_tbci5ospk5nnn t11 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l2));
                    gm_tbci5ospk5nnn t12 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l3));
                    gm_f_mul_nfj5rjv((void *)&F.l2, (void *)NULL, cx, NULL, &t11, &t12);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                uint32_t t13 = gm_l4;
                unsigned __int128 n14 = (unsigned __int128)((uint64_t)1ULL);
                if (n14 >= 32) gm_trap_shift((uint64_t)n14, "u32", gm_srclv3pj5es0f8o, 503, 9);
                uint32_t t15 = (uint32_t)(t13 >> (int)n14);
                gm_l4 = t15;
                uint32_t t16 = gm_l4;
                uint8_t t17 = (t16 > ((uint32_t)0ULL));
                if (t17) {
                    gm_tbci5ospk5nnn t18 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l3));
                    gm_tbci5ospk5nnn t19 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)&F.l3));
                    gm_f_mul_nfj5rjv((void *)&F.l3, (void *)NULL, cx, NULL, &t18, &t19);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
            }
gm_lb3:;
        }
    }
gm_lb1:;
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 48);
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
extern __attribute__((aligned(4))) void gm_f_rem_ib4l1vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
extern __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_shl_small_pijgvpq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint32_t a1, uint8_t a2) {
    struct gm_fr_shl_small_pijgvpq F;
    gm_b16_8 gm_l0; /* a */
    uint32_t gm_l1; /* s */
    uint8_t gm_l2; /* extra */
    uint32_t gm_l4; /* carry */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint32_t gm_l7; /* x */
    GM_STACK_CHECK(&F, 336u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    gm_tftb1odc739t5 gm_l13;
    {
        gm_trqfvmjtt4sij t7;
        gm_f_list_go2qg0b((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t7;
    }
    void * t8 = (void *)((uint8_t *)&F.l3);
    uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l0) + 8));
    uint64_t r10; bool o11 = __builtin_add_overflow(t9, ((uint64_t)1ULL), &r10);
    if (o11) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 127, 15);
    uint64_t t12 = r10;
    gm_tftb1odc739t5 t13 = gm_f_reserve_8hbo1q9(t8, t12);
    gm_l8 = t13;
    uint8_t t14 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
    if (!t14) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srclv3pj5es0f8o, 127, 5);
    uint32_t t15 = gm_l1;
    uint8_t t16 = ((t15 == ((uint32_t)0ULL)));
    if (t16) {
        void * t17 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t18 = gm_l0;
        gm_tftb1odc739t5 t19 = gm_f_extend_7hs67uj(t17, t18);
        gm_l9 = t19;
        uint8_t t20 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
        if (!t20) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srclv3pj5es0f8o, 129, 9);
        uint8_t t21 = gm_l2;
        if (t21) {
            void * t22 = (void *)((uint8_t *)&F.l3);
            gm_tftb1odc739t5 t23 = gm_f_push_im9d4ro(t22, ((uint32_t)0ULL));
            gm_l10 = t23;
            uint8_t t24 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
            if (!t24) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srclv3pj5es0f8o, 130, 20);
        }
        memmove(gm_out, ((uint8_t *)&F.l3), 40);
        goto gm_fdone;
    }
    gm_l4 = ((uint32_t)0ULL);
    gm_b16_8 t25 = gm_l0;
    gm_l5 = t25;
    gm_l6 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t26 = gm_l6;
            uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
            uint8_t t28 = (t26 < t27);
            if (!t28) {
                goto gm_lb1;
            }
            {
                void * t29 = GM_LD(void *, ((uint8_t *)&gm_l5));
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
                uint64_t t31 = gm_l6;
                {
                    {
                        uint32_t t32 = GM_LD(uint32_t, (((uint8_t *)t29) + (uintptr_t)t31 * 4));
                        gm_l7 = t32;
                        void * t33 = (void *)((uint8_t *)&F.l3);
                        uint32_t t34 = gm_l7;
                        uint32_t t35 = gm_l1;
                        unsigned __int128 n36 = (unsigned __int128)t35;
                        if (n36 >= 32) gm_trap_shift((uint64_t)n36, "u32", gm_srclv3pj5es0f8o, 135, 17);
                        uint32_t r37 = (uint32_t)((uint32_t)t34 << (int)n36);
                        bool o38 = (uint32_t)(r37 >> (int)n36) != t34;
                        uint32_t t39 = r37;
                        uint32_t t40 = gm_l4;
                        uint32_t t41 = (uint32_t)(t39 | t40);
                        gm_tftb1odc739t5 t42 = gm_f_push_im9d4ro(t33, t41);
                        gm_l11 = t42;
                        uint8_t t43 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                        if (!t43) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srclv3pj5es0f8o, 135, 9);
                        uint32_t t44 = gm_l7;
                        uint32_t t45 = gm_l1;
                        uint32_t r46; bool o47 = __builtin_sub_overflow(((uint32_t)32ULL), t45, &r46);
                        if (o47) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 136, 23);
                        uint32_t t48 = r46;
                        unsigned __int128 n49 = (unsigned __int128)t48;
                        if (n49 >= 32) gm_trap_shift((uint64_t)n49, "u32", gm_srclv3pj5es0f8o, 136, 17);
                        uint32_t t50 = (uint32_t)(t44 >> (int)n49);
                        gm_l4 = t50;
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 134, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t51 = gm_l6;
            uint64_t r52; bool o53 = __builtin_add_overflow(t51, ((uint64_t)1ULL), &r52);
            uint64_t t54 = r52;
            gm_l6 = t54;
        }
    }
gm_lb1:;
    uint8_t t55 = gm_l2;
    if (t55) {
        void * t56 = (void *)((uint8_t *)&F.l3);
        uint32_t t57 = gm_l4;
        gm_tftb1odc739t5 t58 = gm_f_push_im9d4ro(t56, t57);
        gm_l12 = t58;
        uint8_t t59 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
        if (!t59) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srclv3pj5es0f8o, 138, 16);
    } else {
        uint32_t t60 = gm_l4;
        uint8_t t61 = (t60 > ((uint32_t)0ULL));
        if (t61) {
            void * t62 = (void *)((uint8_t *)&F.l3);
            uint32_t t63 = gm_l4;
            gm_tftb1odc739t5 t64 = gm_f_push_im9d4ro(t62, t63);
            gm_l13 = t64;
            uint8_t t65 = (uint8_t)!((((uint8_t *)&gm_l13))[0] != 0);
            if (!t65) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l13) + 1, gm_srclv3pj5es0f8o, 138, 53);
        }
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l3), 40);
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
inline __attribute__((aligned(4))) void gm_f_shr_4jp99a9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_shr_4jp99a9 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 163u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    {
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        uint64_t t6 = gm_l1;
        gm_f_mag_shr_lfje1ah((void *)&F.l2, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
            if (!t7) {
                goto gm_lb2;
            }
            gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
            uint64_t t10 = gm_l1;
            uint8_t t11 = gm_f_mag_low_bits_set_mr405qt(t9, t10);
            if (!t11) {
                goto gm_lb2;
            }
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&F.l2));
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_f_mag_inc_ku3vbu2((void *)&F.l2, (void *)NULL, cx, NULL, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_lb1;
        }
gm_lb2:;
    }
gm_lb1:;
    {
        uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
        gm_trqfvmjtt4sij t15 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)&F.l2));
        gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t14, &t15);
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
inline __attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3) {
    struct gm_fr_signed_add_ek0dpmo F;
    uint8_t gm_l0; /* an */
    gm_b16_8 gm_l1; /* a */
    uint8_t gm_l2; /* bn */
    gm_b16_8 gm_l3; /* b */
    int32_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 306u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t t2 = gm_l0;
    uint8_t t3 = gm_l2;
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        uint8_t t5 = gm_l0;
        gm_b16_8 t6 = gm_l1;
        gm_b16_8 t7 = gm_l3;
        gm_trqfvmjtt4sij t8;
        gm_f_mag_add_eq1gkek((void *)&t8, (void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t5, &t8);
        goto gm_fdone;
    }
    gm_b16_8 t9 = gm_l1;
    gm_b16_8 t10 = gm_l3;
    int32_t t11 = gm_f_mag_cmp_s39veq6(t9, t10);
    gm_l4 = t11;
    int32_t t12 = gm_l4;
    uint8_t t13 = ((t12 == ((int32_t)0LL)));
    if (t13) {
        gm_f_zero_s25l990((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
    }
    int32_t t14 = gm_l4;
    uint8_t t15 = (t14 > ((int32_t)0LL));
    if (t15) {
        uint8_t t16 = gm_l0;
        gm_b16_8 t17 = gm_l1;
        gm_b16_8 t18 = gm_l3;
        gm_trqfvmjtt4sij t19;
        gm_f_mag_sub_ro7ahl7((void *)&t19, (void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t16, &t19);
        goto gm_fdone;
    }
    {
        uint8_t t20 = gm_l2;
        gm_b16_8 t21 = gm_l3;
        gm_b16_8 t22 = gm_l1;
        gm_trqfvmjtt4sij t23;
        gm_f_mag_sub_ro7ahl7((void *)&t23, (void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t20, &t23);
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
extern __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_i128_o7ud1ue(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_i128_o7ud1ue F;
    unsigned __int128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_t9p38b040grms gm_l2;
    {
        gm_tbci5ospk5nnn t2 = GM_LD(gm_tbci5ospk5nnn, ((uint8_t *)a0));
        gm_f_mag_to_u128_f843dal((void *)&gm_l2, &t2);
    }
    uint8_t t3 = (uint8_t)((((uint8_t *)&gm_l2))[16] != 0);
    if (t3) {
        unsigned __int128 t4 = GM_LD(unsigned __int128, ((uint8_t *)&gm_l2));
        gm_l1 = t4;
    } else {
        memset(&(*(gm_ta24rd13gupia *)gm_out), 0, sizeof (*(gm_ta24rd13gupia *)gm_out));
        goto gm_fdone;
    }
    uint8_t t5 = GM_LD(uint8_t, ((uint8_t *)a0));
    if (t5) {
        unsigned __int128 t6 = gm_l1;
        uint8_t t7 = (t6 > ((unsigned __int128)(((unsigned __int128)9223372036854775808ULL << 64) | 0ULL)));
        if (t7) {
            memset(&(*(gm_ta24rd13gupia *)gm_out), 0, sizeof (*(gm_ta24rd13gupia *)gm_out));
            goto gm_fdone;
        }
        unsigned __int128 t8 = gm_l1;
        unsigned __int128 r9; bool o10 = __builtin_sub_overflow(((unsigned __int128)(((unsigned __int128)0ULL << 64) | 0ULL)), t8, &r9);
        unsigned __int128 t11 = r9;
        __int128 t12;
        memset(&t12, 0, sizeof t12); memcpy(&t12, ((uint8_t *)&t11), sizeof t12 < 16 ? sizeof t12 : 16);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_ta24rd13gupia *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_ta24rd13gupia *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_ta24rd13gupia *)gm_out) + 20, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_ta24rd13gupia *)gm_out) + 24, 0);
        GM_ST(__int128, ((uint8_t *)&(*(gm_ta24rd13gupia *)gm_out)), t12);
        ((uint8_t *)&(*(gm_ta24rd13gupia *)gm_out))[16] = 1;
        goto gm_fdone;
    }
    {
        unsigned __int128 t13 = gm_l1;
        gm_ta24rd13gupia t14;
        __int128 t15 = (__int128)t13;
        gm_ta24rd13gupia t16;
        GM_ST(uint8_t, (uint8_t *)&t16 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t16 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t16 + 20, 0);
        GM_ST(uint64_t, (uint8_t *)&t16 + 24, 0);
        GM_ST(__int128, ((uint8_t *)&t16), t15);
        ((uint8_t *)&t16)[16] = 1;
        gm_ta24rd13gupia t17;
        memset(&t17, 0, sizeof t17);
        t14 = ((unsigned __int128)(t13) <= (((unsigned __int128)9223372036854775807ULL << 64) | 18446744073709551615ULL)) ? t16 : t17;
        memmove(gm_out, ((uint8_t *)&t14), 32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_ta24rd13gupia gm_f_to_i128_o7ud1ue_c(gm_tbci5ospk5nnn a0) { gm_ta24rd13gupia gm_r; gm_f_to_i128_o7ud1ue(&gm_r, &a0); return gm_r; }
extern __attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_to_i64_4cgpvak(const void * a0);
__attribute__((aligned(4))) gm_t913bb735pvt5 gm_f_to_i64_4cgpvak_c(gm_tbci5ospk5nnn a0) { return gm_f_to_i64_4cgpvak(&a0); }
inline __attribute__((aligned(4))) void gm_f_to_twos_475ok28(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_to_twos_475ok28 F;
    uint64_t gm_l1; /* len */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    uint64_t gm_l6; /* carry */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint64_t gm_l10; /* t */
    GM_STACK_CHECK(&F, 339u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    {
        uint64_t t12 = gm_l1;
        gm_f_zeros_h0vjgnm((void *)&F.l2, (void *)NULL, cx, NULL, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    gm_l4 = ((uint64_t)0ULL);
    uint64_t t13 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
    gm_l3 = t13;
    {
        for (;;) {
            uint64_t t14 = gm_l4;
            uint64_t t15 = gm_l3;
            uint8_t t16 = (t14 < t15);
            if (!t16) {
                goto gm_lb1;
            }
            {
                {
                    {
                        uint64_t t17 = gm_l4;
                        gm_l5 = t17;
                        void * t18 = GM_LD(void *, ((uint8_t *)&F.l2));
                        uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                        uint64_t t20 = gm_l5;
                        if ((uintptr_t)t20 >= (uintptr_t)(t19)) gm_trap_index((uintptr_t)t20, t19, gm_srclv3pj5es0f8o, 538, 35);
                        void * t21 = GM_LD(void *, (((uint8_t *)a0) + 8));
                        uint64_t t22 = GM_LD(uint64_t, ((((uint8_t *)a0) + 8) + 8));
                        uint64_t t23 = gm_l5;
                        uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)t21) + (uintptr_t)t23 * 4));
                        GM_ST(uint32_t, (((uint8_t *)t18) + (uintptr_t)t20 * 4), t24);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 538, 5);
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
    uint8_t t29 = GM_LD(uint8_t, ((uint8_t *)a0));
    if (t29) {
        gm_l6 = ((uint64_t)1ULL);
        gm_l8 = ((uint64_t)0ULL);
        uint64_t t30 = gm_l1;
        gm_l7 = t30;
        {
            for (;;) {
                uint64_t t31 = gm_l8;
                uint64_t t32 = gm_l7;
                uint8_t t33 = (t31 < t32);
                if (!t33) {
                    goto gm_lb6;
                }
                {
                    {
                        {
                            uint64_t t34 = gm_l8;
                            gm_l9 = t34;
                            void * t35 = GM_LD(void *, ((uint8_t *)&F.l2));
                            uint64_t t36 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                            uint64_t t37 = gm_l9;
                            if ((uintptr_t)t37 >= (uintptr_t)(t36)) gm_trap_index((uintptr_t)t37, t36, gm_srclv3pj5es0f8o, 542, 23);
                            uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)t35) + (uintptr_t)t37 * 4));
                            uint32_t t39 = (uint32_t)~t38;
                            uint64_t t40 = (uint64_t)t39;
                            uint64_t t41 = gm_l6;
                            uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
                            if (o43) gm_trap("integer overflow in `+`", gm_srclv3pj5es0f8o, 542, 18);
                            uint64_t t44 = r42;
                            gm_l10 = t44;
                            void * t45 = GM_LD(void *, ((uint8_t *)&F.l2));
                            uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                            uint64_t t47 = gm_l9;
                            if ((uintptr_t)t47 >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)t47, t46, gm_srclv3pj5es0f8o, 543, 13);
                            uint64_t t48 = gm_l10;
                            uint32_t t49 = (uint32_t)t48;
                            GM_ST(uint32_t, (((uint8_t *)t45) + (uintptr_t)t47 * 4), t49);
                            uint64_t t50 = gm_l10;
                            unsigned __int128 n51 = (unsigned __int128)((uint64_t)32ULL);
                            if (n51 >= 64) gm_trap_shift((uint64_t)n51, "u64", gm_srclv3pj5es0f8o, 544, 21);
                            uint64_t t52 = (uint64_t)(t50 >> (int)n51);
                            gm_l6 = t52;
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 541, 9);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t53 = gm_l8;
                uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                uint64_t t56 = r54;
                gm_l8 = t56;
            }
        }
gm_lb6:;
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 40);
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
extern __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_t9p38b040grms gm_f_to_u128_haka99r_c(gm_tbci5ospk5nnn a0) { gm_t9p38b040grms gm_r; gm_f_to_u128_haka99r(&gm_r, &a0); return gm_r; }
extern __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da_c(gm_tbci5ospk5nnn a0) { return gm_f_to_u64_m01t1da(&a0); }
inline __attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_8rdppmk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* k */
    GM_STACK_CHECK_SHALLOW(&F, 118u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t5 = gm_l0;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 8));
    gm_l1 = t6;
    {
        for (;;) {
            uint64_t t7 = gm_l1;
            uint8_t t8 = (t7 > ((uint64_t)0ULL));
            if (!t8) {
                goto gm_lb1;
            }
            void * t9 = gm_l0;
            void * t10 = GM_LD(void *, ((uint8_t *)t9));
            uint64_t t11 = GM_LD(uint64_t, (((uint8_t *)t9) + 8));
            uint64_t t12 = gm_l1;
            uint64_t r13; bool o14 = __builtin_sub_overflow(t12, ((uint64_t)1ULL), &r13);
            if (o14) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 26, 29);
            uint64_t t15 = r13;
            if ((uintptr_t)t15 >= (uintptr_t)(t11)) gm_trap_index((uintptr_t)t15, t11, gm_srclv3pj5es0f8o, 26, 21);
            uint32_t t16 = GM_LD(uint32_t, (((uint8_t *)t10) + (uintptr_t)t15 * 4));
            uint8_t t17 = ((t16 == ((uint32_t)0ULL)));
            if (!t17) {
                goto gm_lb1;
            }
            {
                uint64_t t18 = gm_l1;
                uint64_t r19; bool o20 = __builtin_sub_overflow(t18, ((uint64_t)1ULL), &r19);
                if (o20) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 26, 43);
                uint64_t t21 = r19;
                gm_l1 = t21;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    void * t22 = gm_l0;
    uint64_t t23 = gm_l1;
    gm_f_truncate_eoppkeo(t22, t23);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1) {
    struct gm_fr_write_decimal_ke367ab F;
    void * gm_l1; /* out */
    gm_b16_8 gm_l3; /* m */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 321u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l6;
    gm_tftb1odc739t5 gm_l7;
    gm_tftb1odc739t5 gm_l8;
    gm_tftb1odc739t5 gm_l9;
    gm_tftb1odc739t5 gm_l10;
    uint8_t t8 = GM_LD(uint8_t, ((uint8_t *)&F.l0));
    if (t8) {
        void * t9 = gm_l1;
        gm_tftb1odc739t5 t10 = gm_f_push_t83rg2m(t9, ((uint8_t)45ULL));
        gm_l6 = t10;
        uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l6))[0] != 0);
        if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l6) + 1, gm_srclv3pj5es0f8o, 599, 21);
    }
    {
        uint8_t t12 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        if (t12) {
            void * t13 = gm_l1;
            gm_tftb1odc739t5 t14 = gm_f_push_t83rg2m(t13, ((uint8_t)48ULL));
            gm_l7 = t14;
            uint8_t t15 = (uint8_t)!((((uint8_t *)&gm_l7))[0] != 0);
            if (!t15) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l7) + 1, gm_srclv3pj5es0f8o, 601, 9);
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
    }
    {
        gm_trqfvmjtt4sij t16;
        gm_f_list_go2qg0b((void *)&t16, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l2 = t16;
    }
    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&F.l0) + 8));
    gm_l3 = t17;
    {
        for (;;) {
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            uint8_t t19 = (t18 > ((uint64_t)0ULL));
            if (!t19) {
                goto gm_lb1;
            }
            {
                gm_b16_8 t20 = gm_l3;
                gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
                gm_f_mag_divrem_small_1qs5pmr((void *)&F.l4, (void *)NULL, cx, NULL, t21, ((uint32_t)1000000000ULL));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t22 = (void *)((uint8_t *)&F.l2);
                uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&F.l4) + 40));
                gm_tftb1odc739t5 t24 = gm_f_push_im9d4ro(t22, t23);
                gm_l8 = t24;
                uint8_t t25 = (uint8_t)!((((uint8_t *)&gm_l8))[0] != 0);
                if (!t25) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l8) + 1, gm_srclv3pj5es0f8o, 609, 9);
                gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&F.l4));
                gm_l3 = t26;
            }
gm_lb3:;
        }
    }
gm_lb1:;
    uint64_t t27 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
    uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)1ULL), &r28);
    if (o29) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 612, 10);
    uint64_t t30 = r28;
    gm_l5 = t30;
    void * t31 = gm_l1;
    void * t32 = GM_LD(void *, ((uint8_t *)&F.l2));
    uint64_t t33 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
    uint64_t t34 = gm_l5;
    if ((uintptr_t)t34 >= (uintptr_t)(t33)) gm_trap_index((uintptr_t)t34, t33, gm_srclv3pj5es0f8o, 613, 21);
    uint32_t t35 = GM_LD(uint32_t, (((uint8_t *)t32) + (uintptr_t)t34 * 4));
    gm_t88rcucg1b7uk t36;
    GM_ST(uint32_t, ((uint8_t *)&t36 + 0), t35);
    gm_tftb1odc739t5 t37 = gm_f_write_cfkbroh(t31, t36);
    gm_l9 = t37;
    uint8_t t38 = (uint8_t)!((((uint8_t *)&gm_l9))[0] != 0);
    if (!t38) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l9) + 1, gm_srclv3pj5es0f8o, 613, 5);
    {
        for (;;) {
            uint64_t t39 = gm_l5;
            uint8_t t40 = (t39 > ((uint64_t)0ULL));
            if (!t40) {
                goto gm_lb4;
            }
            {
                uint64_t t41 = gm_l5;
                uint64_t r42; bool o43 = __builtin_sub_overflow(t41, ((uint64_t)1ULL), &r42);
                if (o43) gm_trap("integer overflow in `-`", gm_srclv3pj5es0f8o, 615, 9);
                uint64_t t44 = r42;
                gm_l5 = t44;
                void * t45 = gm_l1;
                void * t46 = GM_LD(void *, ((uint8_t *)&F.l2));
                uint64_t t47 = GM_LD(uint64_t, (((uint8_t *)&F.l2) + 8));
                uint64_t t48 = gm_l5;
                if ((uintptr_t)t48 >= (uintptr_t)(t47)) gm_trap_index((uintptr_t)t48, t47, gm_srclv3pj5es0f8o, 616, 28);
                uint32_t t49 = GM_LD(uint32_t, (((uint8_t *)t46) + (uintptr_t)t48 * 4));
                gm_t88rcucg1b7uk t50;
                GM_ST(uint32_t, ((uint8_t *)&t50 + 0), t49);
                gm_tftb1odc739t5 t51 = gm_f_write_6r0n90c(t45, t50);
                gm_l10 = t51;
                uint8_t t52 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
                if (!t52) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srclv3pj5es0f8o, 616, 9);
            }
gm_lb6:;
        }
    }
gm_lb4:;
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
extern __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_zeros_h0vjgnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0) {
    struct gm_fr_zeros_h0vjgnm F;
    uint64_t gm_l0; /* n */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l4;
    gm_tftb1odc739t5 gm_l5;
    {
        gm_trqfvmjtt4sij t7;
        gm_f_list_go2qg0b((void *)&t7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l1 = t7;
    }
    void * t8 = (void *)((uint8_t *)&F.l1);
    uint64_t t9 = gm_l0;
    gm_tftb1odc739t5 t10 = gm_f_reserve_8hbo1q9(t8, t9);
    gm_l4 = t10;
    uint8_t t11 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t11) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srclv3pj5es0f8o, 40, 5);
    gm_l3 = ((uint64_t)0ULL);
    uint64_t t12 = gm_l0;
    gm_l2 = t12;
    {
        for (;;) {
            uint64_t t13 = gm_l3;
            uint64_t t14 = gm_l2;
            uint8_t t15 = (t13 < t14);
            if (!t15) {
                goto gm_lb1;
            }
            {
                {
                    {
                        void * t16 = (void *)((uint8_t *)&F.l1);
                        gm_tftb1odc739t5 t17 = gm_f_push_im9d4ro(t16, ((uint32_t)0ULL));
                        gm_l5 = t17;
                        uint8_t t18 = (uint8_t)!((((uint8_t *)&gm_l5))[0] != 0);
                        if (!t18) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l5) + 1, gm_srclv3pj5es0f8o, 41, 21);
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srclv3pj5es0f8o, 41, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t19 = gm_l3;
            uint64_t r20; bool o21 = __builtin_add_overflow(t19, ((uint64_t)1ULL), &r20);
            uint64_t t22 = r20;
            gm_l3 = t22;
        }
    }
gm_lb1:;
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 40);
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

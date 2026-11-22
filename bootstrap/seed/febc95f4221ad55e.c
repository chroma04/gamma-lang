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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint64_t f0; uint32_t f1; uint8_t pe[4]; } gm_tn68el8d14vku;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct gm_cs7e9ppsd384bj { uint32_t f0; gm_csguankko4lmhd f1; } gm_cs7e9ppsd384bj;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; gm_tl874t2atnlh3 f3; uint32_t f4; } gm_t0ut88vjdgrt1;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_tl874t2atnlh3 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_t5v0c8ia6a5mo;
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
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `(e: ^const Expr, l: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_b16_8 f2; void * f3; gm_t9805qtonbsag f4; uint8_t f5; uint8_t pe[7]; } gm_tdgfduqjheqdm;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_tf5srh4mp4b3i f1; } gm_tj64ui6kruni6;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
_Static_assert(sizeof(gm_tn68el8d14vku) == 16 && _Alignof(gm_tn68el8d14vku) == 8 && offsetof(gm_tn68el8d14vku, f0) == 0 && offsetof(gm_tn68el8d14vku, f1) == 8, "layout of `(i: usize, x: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t p3[3]; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tsoie1ppgdvcg;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_cs7e9ppsd384bj) == 8 && _Alignof(gm_cs7e9ppsd384bj) == 4 && offsetof(gm_cs7e9ppsd384bj, f0) == 0 && offsetof(gm_cs7e9ppsd384bj, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0ut88vjdgrt1) == 40 && _Alignof(gm_t0ut88vjdgrt1) == 8 && offsetof(gm_t0ut88vjdgrt1, f0) == 0 && offsetof(gm_t0ut88vjdgrt1, f1) == 4 && offsetof(gm_t0ut88vjdgrt1, f2) == 8 && offsetof(gm_t0ut88vjdgrt1, f3) == 24 && offsetof(gm_t0ut88vjdgrt1, f4) == 36, "layout of `MonoRequest`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tfeab279c16rr v0; gm_tj64ui6kruni6 v1; } f1; } gm_t1jll27j7ae34;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `(string, usize)`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t5v0c8ia6a5mo) == 56 && _Alignof(gm_t5v0c8ia6a5mo) == 8 && offsetof(gm_t5v0c8ia6a5mo, f0) == 0 && offsetof(gm_t5v0c8ia6a5mo, f1) == 16 && offsetof(gm_t5v0c8ia6a5mo, f2) == 20 && offsetof(gm_t5v0c8ia6a5mo, f3) == 32 && offsetof(gm_t5v0c8ia6a5mo, f4) == 40 && offsetof(gm_t5v0c8ia6a5mo, f5) == 48, "layout of `ParamInfo`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8588ma2cr9d7) == 12 && _Alignof(gm_t8588ma2cr9d7) == 4 && offsetof(gm_t8588ma2cr9d7, f0) == 0 && offsetof(gm_t8588ma2cr9d7, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_t8588ma2cr9d7 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_t9q8lhljes74c;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { gm_b16_8 f0; gm_trqfvmjtt4sij f1; gm_trqfvmjtt4sij f2; gm_tl874t2atnlh3 f3; uint8_t f4; uint8_t p5[3]; void * f5; gm_b16_8 f6; void * f7; } gm_tblkd491pk4cr;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdgfduqjheqdm) == 64 && _Alignof(gm_tdgfduqjheqdm) == 8 && offsetof(gm_tdgfduqjheqdm, f0) == 0 && offsetof(gm_tdgfduqjheqdm, f1) == 8 && offsetof(gm_tdgfduqjheqdm, f2) == 24 && offsetof(gm_tdgfduqjheqdm, f3) == 40 && offsetof(gm_tdgfduqjheqdm, f4) == 48 && offsetof(gm_tdgfduqjheqdm, f5) == 56, "layout of `FnDef`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `(string, []u8, string)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; uint64_t f4; uint8_t f5; uint8_t p6[7]; gm_trqfvmjtt4sij f6; } gm_tj607dkcelqbd;
_Static_assert(sizeof(gm_tj64ui6kruni6) == 32 && _Alignof(gm_tj64ui6kruni6) == 8 && offsetof(gm_tj64ui6kruni6, f0) == 0 && offsetof(gm_tj64ui6kruni6, f1) == 16, "layout of `(string, ?usize)`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
typedef struct { gm_tplpbqf3duvn3 a[1]; } gm_tmcj65fi8d3nd;
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tiilmfd16v3fc f0; uint8_t f1; uint8_t pe[7]; } gm_tqu3m2l3n8mnc;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tsoie1ppgdvcg) == 40 && _Alignof(gm_tsoie1ppgdvcg) == 8 && offsetof(gm_tsoie1ppgdvcg, f0) == 0 && offsetof(gm_tsoie1ppgdvcg, f1) == 16 && offsetof(gm_tsoie1ppgdvcg, f2) == 20 && offsetof(gm_tsoie1ppgdvcg, f3) == 24, "layout of `LocalDecl`");
typedef struct { uint32_t f0; gm_tkn42u7na3t81 f1; } gm_tuo61a5rdrbtf;
_Static_assert(sizeof(gm_t1jll27j7ae34) == 56 && _Alignof(gm_t1jll27j7ae34) == 8 && offsetof(gm_t1jll27j7ae34, f0) == 0 && offsetof(gm_t1jll27j7ae34, f1) == 8, "layout of `Parsed`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t9q8lhljes74c) == 32 && _Alignof(gm_t9q8lhljes74c) == 8 && offsetof(gm_t9q8lhljes74c, f0) == 0 && offsetof(gm_t9q8lhljes74c, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tblkd491pk4cr) == 144 && _Alignof(gm_tblkd491pk4cr) == 8 && offsetof(gm_tblkd491pk4cr, f0) == 0 && offsetof(gm_tblkd491pk4cr, f1) == 16 && offsetof(gm_tblkd491pk4cr, f2) == 56 && offsetof(gm_tblkd491pk4cr, f3) == 96 && offsetof(gm_tblkd491pk4cr, f4) == 108 && offsetof(gm_tblkd491pk4cr, f5) == 112 && offsetof(gm_tblkd491pk4cr, f6) == 120 && offsetof(gm_tblkd491pk4cr, f7) == 136, "layout of `MonoCx`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tj607dkcelqbd) == 88 && _Alignof(gm_tj607dkcelqbd) == 8 && offsetof(gm_tj607dkcelqbd, f0) == 0 && offsetof(gm_tj607dkcelqbd, f1) == 8 && offsetof(gm_tj607dkcelqbd, f2) == 12 && offsetof(gm_tj607dkcelqbd, f3) == 16 && offsetof(gm_tj607dkcelqbd, f4) == 32 && offsetof(gm_tj607dkcelqbd, f5) == 40 && offsetof(gm_tj607dkcelqbd, f6) == 48, "layout of `TemplateUses`");
_Static_assert(sizeof(gm_tmcj65fi8d3nd) == 8 && _Alignof(gm_tmcj65fi8d3nd) == 4 && offsetof(gm_tmcj65fi8d3nd, a) == 0, "layout of `[1]Subst`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; uint64_t f2; gm_b16_8 f3; gm_t88rcucg1b7uk f4; uint8_t p5[4]; gm_tnpifv20irnph f5; gm_b16_8 f6; gm_tqu3m2l3n8mnc f7; uint8_t f8; uint8_t pe[7]; } gm_tqqhoivahirr0;
_Static_assert(sizeof(gm_tqu3m2l3n8mnc) == 32 && _Alignof(gm_tqu3m2l3n8mnc) == 8 && offsetof(gm_tqu3m2l3n8mnc, f0) == 0 && offsetof(gm_tqu3m2l3n8mnc, f1) == 24, "layout of `?FnInstance`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tuo61a5rdrbtf) == 12 && _Alignof(gm_tuo61a5rdrbtf) == 4 && offsetof(gm_tuo61a5rdrbtf, f0) == 0 && offsetof(gm_tuo61a5rdrbtf, f1) == 4, "layout of `(param: u32, args: ?Ty)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tqqhoivahirr0) == 136 && _Alignof(gm_tqqhoivahirr0) == 8 && offsetof(gm_tqqhoivahirr0, f0) == 0 && offsetof(gm_tqqhoivahirr0, f1) == 16 && offsetof(gm_tqqhoivahirr0, f2) == 32 && offsetof(gm_tqqhoivahirr0, f3) == 40 && offsetof(gm_tqqhoivahirr0, f4) == 56 && offsetof(gm_tqqhoivahirr0, f5) == 64 && offsetof(gm_tqqhoivahirr0, f6) == 80 && offsetof(gm_tqqhoivahirr0, f7) == 96 && offsetof(gm_tqqhoivahirr0, f8) == 128, "layout of `FnSig`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_t9q8lhljes74c v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; void * v55; gm_tandvbt63uk7n v56; gm_t710lsrq1m8gk v57; } f1; } gm_to0f5qn75cgg8;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_to0f5qn75cgg8 f0; uint8_t f1; uint8_t pe[7]; } gm_t9f76dterbqj6;
_Static_assert(sizeof(gm_to0f5qn75cgg8) == 64 && _Alignof(gm_to0f5qn75cgg8) == 8 && offsetof(gm_to0f5qn75cgg8, f0) == 0 && offsetof(gm_to0f5qn75cgg8, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_to0f5qn75cgg8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tsf6i2agfnpva;
_Static_assert(sizeof(gm_t9f76dterbqj6) == 72 && _Alignof(gm_t9f76dterbqj6) == 8 && offsetof(gm_t9f76dterbqj6, f0) == 0 && offsetof(gm_t9f76dterbqj6, f1) == 64, "layout of `?ExprKind`");
_Static_assert(sizeof(gm_tsf6i2agfnpva) == 88 && _Alignof(gm_tsf6i2agfnpva) == 8 && offsetof(gm_tsf6i2agfnpva, f0) == 0 && offsetof(gm_tsf6i2agfnpva, f1) == 8 && offsetof(gm_tsf6i2agfnpva, f2) == 72, "layout of `Expr`");
struct gm_fr_add_fn_t4t83qh {
    gm_frame h;
    gm_tqqhoivahirr0 l1; /* sig */
    gm_tdgfduqjheqdm l2; /* def */
    uint32_t ret;
};
struct gm_fr_add_shared_fn_f8l99p1 {
    gm_frame h;
    gm_tqqhoivahirr0 l1; /* sig */
    uint32_t ret;
};
struct gm_fr_any_child_ffc8h1k {
    gm_frame h;
    gm_closure l1; /* pred */
    uint8_t l2; /* found */
    uint8_t ret;
};
struct gm_fr_bodiless_def_u9kqo09 {
    gm_frame h;
    gm_tdgfduqjheqdm ret;
};
struct gm_fr_deinit_padgogk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_1fu8oa9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_6trg8r7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_8fnsr5h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_crpekm1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
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
struct gm_fr_empty_slice_jviatgq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_n936iee {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ot5ttb7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s65n31g {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_extend_7hs67uj {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_gig5o6g {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fn_7u87bne {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9gjij2f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_m1ukc9a {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_format_jvf1ma2 {
    gm_frame h;
    gm_tabb710d9mq2e ret;
};
struct gm_fr_forward_call_qat9d2e {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* hidden */
    gm_trqfvmjtt4sij l10; /* erased */
    gm_t0ut88vjdgrt1 l20; /* req */
    gm_trqfvmjtt4sij l31; /* all */
    gm_t9f76dterbqj6 ret;
};
struct gm_fr_forwards_l1foetf {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_2plb4qp {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_31sqk5v {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7l73vjb {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_af74s0q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_allvvbh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
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
struct gm_fr_list_p9s2t99 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_vp1djv1_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_make_shared_r51o03d {
    gm_frame h;
    gm_t0ut88vjdgrt1 l1; /* req */
    gm_tqqhoivahirr0 l5; /* gsig */
    gm_tdgfduqjheqdm l6; /* def */
    gm_trqfvmjtt4sij l7; /* map */
    gm_trqfvmjtt4sij l13; /* params */
    gm_t5v0c8ia6a5mo l16; /* p */
    gm_t5v0c8ia6a5mo l17; /* q */
    gm_tqqhoivahirr0 l20; /* sig */
    gm_trqfvmjtt4sij l22; /* locals */
    gm_trqfvmjtt4sij l24; /* hidden */
    gm_trqfvmjtt4sij l25; /* sites */
    gm_tblkd491pk4cr l26; /* mc */
    gm_tsoie1ppgdvcg l29; /* l */
    gm_tsoie1ppgdvcg l30; /* x */
    gm_trqfvmjtt4sij l31; /* ps */
    gm_tsoie1ppgdvcg l41; /* l */
    gm_tiilmfd16v3fc ret;
};
struct gm_fr_mentions_value_2a40t7e {
    gm_frame h;
    uint32_t l1; /* g */
    uint8_t ret;
};
struct gm_fr_only_templates_8l3e5t6 {
    gm_frame h;
    gm_tj607dkcelqbd l5; /* u */
    uint8_t ret;
};
struct gm_fr_push_0p2fb2b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_1ncolkq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_472i7vl {
    gm_frame h;
    gm_tqqhoivahirr0 l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_am7bo0v {
    gm_frame h;
    gm_tsoie1ppgdvcg l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_d943vgp_2 {
    gm_frame h;
    gm_tsf6i2agfnpva l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_gtq94gd {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_h69n5fi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_hbhlgi6 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_im9d4ro {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_o8u01sh {
    gm_frame h;
    gm_t5v0c8ia6a5mo l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_roqi806 {
    gm_frame h;
    gm_tdgfduqjheqdm l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_1sqb3ir {
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
struct gm_fr_regrow_8r12kcb {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_9a4nica {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_icepq2q {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_jd8h83s {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_k8jaegk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_o08qncv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_thihbkv {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_scan_uses_iuphl4h {
    gm_frame h;
    void * l0; /* u */
    uint8_t l2; /* top */
    gm_tdgfduqjheqdm l20; /* d */
    gm_b0_1 ret;
};
struct gm_fr_set_inst_def_tcpabg0 {
    gm_frame h;
    gm_tdgfduqjheqdm l2; /* d */
    gm_b0_1 ret;
};
struct gm_fr_share_instance_1q66m8k {
    gm_frame h;
    gm_t0ut88vjdgrt1 l1; /* req */
    gm_trqfvmjtt4sij l5; /* erased */
    gm_trqfvmjtt4sij l6; /* texts */
    gm_trqfvmjtt4sij l13; /* map */
    gm_trqfvmjtt4sij l18; /* tables */
    gm_tfeab279c16rr l22; /* p */
    gm_tfeab279c16rr l24; /* parsed */
    gm_trqfvmjtt4sij l28; /* index */
    gm_tdgfduqjheqdm l33; /* def */
    gm_tqqhoivahirr0 l34; /* sig */
    gm_trqfvmjtt4sij l39; /* locals */
    gm_tsoie1ppgdvcg l42; /* l */
    gm_tsoie1ppgdvcg l43; /* x */
    gm_trqfvmjtt4sij l46; /* args */
    uint8_t ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_template_marker_heil9i9 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_template_params_6stj17b {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_tdgfduqjheqdm l4; /* def */
    gm_b16_8 ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2);
inline __attribute__((aligned(4))) uint32_t gm_f_add_shared_fn_f8l99p1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2);
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_len_exprs_m8s6m0h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_toceffb697p0u a2);
__attribute__((aligned(4))) void gm_f_check_template_brtb8t0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, const void * a2, gm_tkn42u7na3t81 a3);
__attribute__((aligned(4))) void * gm_f_clone_expr_af0u1qc(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_deinit_padgogk(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_8fnsr5h(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_crpekm1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jviatgq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n936iee(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ot5ttb7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s65n31g(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_gig5o6g(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9gjij2f(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_fn_m1ukc9a(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_child_fm49s73(void * a0, gm_closure a1);
inline __attribute__((aligned(4))) void gm_f_format_jvf1ma2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_roumpdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_forward_call_qat9d2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) uint8_t gm_f_forwards_l1foetf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_get_hi5nrra(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_get_s7blvas(void *gm_out, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_2plb4qp(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_31sqk5v(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7l73vjb(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_af74s0q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_allvvbh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_oalq03d(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_p9s2t99(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_vp1djv1_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_shared_r51o03d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_value_2a40t7e(void * a0, uint32_t a1);
__attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_mono_expr_p8qb7fe(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_mono_ty_atcu0jn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_4n5f3sc(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_only_templates_8l3e5t6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, uint32_t a3);
__attribute__((aligned(4))) void gm_f_parse_template_rlpl2k8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0p2fb2b(void * a0, gm_tuo61a5rdrbtf a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1ncolkq(void * a0, gm_cs9ufftihtbpo2 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_472i7vl(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_d943vgp_2(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_gtq94gd(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_h69n5fi(void * a0, gm_cs7e9ppsd384bj a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hbhlgi6(void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_im9d4ro(void * a0, uint32_t a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_o8u01sh(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_roqi806(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_3pvr70r(void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_6him49q(void * a0, const void * a1, const void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_i2clb58(void * a0, uint32_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_1sqb3ir(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_2onqfca(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_61fb705_2(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_7sj41hq(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9a4nica(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_icepq2q(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_jd8h83s(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k8jaegk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o08qncv(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_thihbkv(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_soa_errors_m1bqopj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_uses_iuphl4h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2);
__attribute__((aligned(4))) uint8_t gm_f_share_instance_1q66m8k(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_subst_a2s1mb3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tnpifv20irnph gm_f_subst_effects_jnlbu5m(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_template_marker_heil9i9(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_template_params_6stj17b(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) uint8_t gm_f_type_size_ok_tr1llg4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_kolm4drqk8i[40] __attribute__((aligned(8)));
extern const uint8_t gm_seb2o268bfiia[11];
extern const uint8_t gm_sj3cmvbmi7cm6[9];
extern const uint8_t gm_sqcvjnsub5bfs[53];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcb5s85si25i3o[];
extern const char gm_srclv0dcbbfhves[];
extern const char gm_srcmhupam4se3h5[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srkol0piugptj[14];
extern const gm_tmpl gm_tploj8vgi84m11i;
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) uint32_t gm_f_add_fn_t4t83qh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2) {
    struct gm_fr_add_fn_t4t83qh F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 86u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tftb1odc739t5 gm_l3;
    gm_tftb1odc739t5 gm_l4;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 344);
        gm_tftb1odc739t5 t4 = gm_f_push_472i7vl(t3, &(*(gm_tqqhoivahirr0 *)((uint8_t *)a1)));
        gm_l3 = t4;
    }
    uint8_t t5 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t5) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srcmhupam4se3h5, 466, 5);
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 384);
        gm_tftb1odc739t5 t8 = gm_f_push_roqi806(t7, &(*(gm_tdgfduqjheqdm *)((uint8_t *)a2)));
        gm_l4 = t8;
    }
    uint8_t t9 = (uint8_t)!((((uint8_t *)&gm_l4))[0] != 0);
    if (!t9) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l4) + 1, gm_srcmhupam4se3h5, 467, 5);
    void * t10 = gm_l0;
    uint64_t t11 = GM_LD(uint64_t, ((((uint8_t *)t10) + 344) + 8));
    uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
    if (o13) gm_trap("integer overflow in `-`", gm_srcmhupam4se3h5, 468, 13);
    uint64_t t14 = r12;
    uint32_t t15 = (uint32_t)t14;
    F.ret = t15;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_add_shared_fn_f8l99p1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2) {
    struct gm_fr_add_shared_fn_f8l99p1 F;
    void * gm_l0; /* s */
    gm_t9qkh0rsooc8d gm_l2; /* kind */
    GM_STACK_CHECK(&F, 230u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    void * t2 = gm_l0;
    gm_t9qkh0rsooc8d t3 = gm_l2;
    gm_tqqhoivahirr0 t4 = GM_LD(gm_tqqhoivahirr0, ((uint8_t *)a1));
    gm_tdgfduqjheqdm t5;
    gm_f_bodiless_def_u9kqo09((void *)&t5, t3);
    uint32_t t6 = gm_f_add_fn_t4t83qh((void *)NULL, cx, NULL, t2, &t4, &t5);
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
inline __attribute__((aligned(4))) uint8_t gm_f_any_child_ffc8h1k(void * a0, gm_closure a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_any_child_ffc8h1k F;
    void * gm_l0; /* e */
    GM_STACK_CHECK(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    F.l2 = ((uint8_t)0);
    void * t2 = gm_l0;
    gm_closure t3 = ((gm_closure){(void *)gm_f_fn_7u87bne, (void *)&F.h, cx});
    gm_f_for_each_child_fm49s73(t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    uint8_t t4 = F.l2;
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
inline __attribute__((aligned(4))) void gm_f_bodiless_def_u9kqo09(void *gm_out, gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bodiless_def_u9kqo09 F;
    gm_t9qkh0rsooc8d gm_l0; /* kind */
    GM_STACK_CHECK_SHALLOW(&F, 42u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_t9qkh0rsooc8d t2 = gm_l0;
    gm_b16_8 t3 = gm_f_empty_slice_d914i5u();
    gm_b16_8 t4 = gm_f_empty_slice_6trg8r7();
    gm_t9805qtonbsag t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 4, 0);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 57, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 58, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 60, 0);
    GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 0), t2);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 8), t3);
    GM_ST(gm_b16_8, ((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 24), t4);
    GM_ST(void *, ((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 40), ((void *)0));
    GM_ST(gm_t9805qtonbsag, ((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 48), t5);
    GM_ST(uint8_t, ((uint8_t *)&(*(gm_tdgfduqjheqdm *)gm_out) + 56), ((uint8_t)0));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_deinit_padgogk(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_deinit_padgogk F;
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
        uint64_t r15; bool o16 = __builtin_mul_overflow(((uint64_t)4ULL), t14, &r15);
        if (o16) gm_trap("integer overflow in `*`", gm_srcrgc9bkquf6nr, 146, 64);
        uint64_t t17 = r15;
        void * t18 = t7;
        gm_b0_1 t19;
        ((void (*)(void *, void *, uint64_t, uint64_t))GM_PTR_UNTAG(t18))(t9, t12, t17, ((uint64_t)4ULL));
    }
    void * t20 = gm_l0;
    gm_b16_8 t21 = gm_f_empty_slice_d914i5u();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1fu8oa9 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_6trg8r7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_6trg8r7 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_crpekm1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_crpekm1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jviatgq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jviatgq F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_n936iee(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_n936iee F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ot5ttb7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ot5ttb7 F;
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
    GM_ST(void *, (uint8_t *)&t7, (void *)(((uint8_t *)t5 + (uintptr_t)((uint64_t)0ULL) * 136)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s65n31g(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s65n31g F;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_gig5o6g(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_gig5o6g F;
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
        gm_tftb1odc739t5 t15 = gm_f_regrow_61fb705_2(t12, t13, t14);
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
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t20 + (uintptr_t)t21 * 88)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27));
        gm_b16_8 t29 = gm_l1;
        if (GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t28)), GM_LD(void *, ((uint8_t *)&t29)), GM_LD(uintptr_t, ((uint8_t *)&t28) + 8) * 88);
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
    GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t32 + (uintptr_t)((uint64_t)0ULL) * 88)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7u87bne(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_7u87bne F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t2 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2));
    uint8_t t3 = (uint8_t)!t2;
    if (t3) {
        gm_closure t4 = GM_LD(gm_closure, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l1));
        void * t5 = gm_l0;
        gm_closure t6 = t4;
        gm_node *ccx7 = t6.cx;
        void * t8 = t6.code;
        uint8_t t9;
        if (GM_PTR_TAGGED(t8)) {
            t9 = ((uint8_t (*)(void *))GM_PTR_UNTAG(t8))(t5);
        } else {
            t9 = ((uint8_t (*)(void *, gm_node *, gm_node *, void *))t8)(t6.env, ccx7, NULL, t5);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_any_child_ffc8h1k *)((&F.h)->up))->l2), t9);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9gjij2f(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_9gjij2f F;
    void * gm_l0; /* x */
    GM_STACK_CHECK(&F, 25u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_scan_uses_iuphl4h *)((&F.h)->up))->l0));
    void * t3 = gm_l0;
    uint8_t t4 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_scan_uses_iuphl4h *)((&F.h)->up))->l2));
    gm_b0_1 t5 = gm_f_scan_uses_iuphl4h((void *)NULL, cx, NULL, t2, t3, t4);
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
inline __attribute__((aligned(4))) uint8_t gm_f_fn_m1ukc9a(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_m1ukc9a F;
    void * gm_l0; /* c */
    GM_STACK_CHECK(&F, 23u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    uint32_t t3 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_mentions_value_2a40t7e *)((&F.h)->up))->l1));
    uint8_t t4 = gm_f_mentions_value_2a40t7e(t2, t3);
    F.ret = t4;
    goto gm_fret;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_format_jvf1ma2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_jvf1ma2 F;
    gm_tnpifv20irnph gm_l0; /* args */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tnpifv20irnph t2 = gm_l0;
    gm_b16_8 t3;
    GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqcvjnsub5bfs));
    GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
    gm_f_format_roumpdn((void *)&(*(gm_tabb710d9mq2e *)gm_out), (void *)NULL, cx, NULL, t2, ((void *)&gm_tploj8vgi84m11i), t3);
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
inline __attribute__((aligned(4))) void gm_f_list_2plb4qp(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_2plb4qp F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_n936iee();
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
inline __attribute__((aligned(4))) void gm_f_list_31sqk5v(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_31sqk5v F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_s65n31g();
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
inline __attribute__((aligned(4))) void gm_f_list_7l73vjb(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7l73vjb F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_crpekm1();
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
inline __attribute__((aligned(4))) void gm_f_list_allvvbh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_allvvbh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_jviatgq();
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
inline __attribute__((aligned(4))) void gm_f_list_p9s2t99(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_p9s2t99 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t2 = gm_f_empty_slice_6trg8r7();
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
inline __attribute__((aligned(4))) uint8_t gm_f_mentions_value_2a40t7e(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mentions_value_2a40t7e F;
    void * gm_l0; /* e */
    uint32_t gm_l2; /* p */
    GM_STACK_CHECK(&F, 70u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        {
            void * t4 = gm_l0;
            uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
            uint8_t t6 = (uint8_t)(t5 == ((uint8_t)9ULL));
            if (!t6) {
                goto gm_lb2;
            }
            uint32_t t7 = GM_LD(uint32_t, ((((uint8_t *)t4) + 8) + 8));
            gm_l2 = t7;
            uint32_t t8 = gm_l2;
            uint32_t t9 = F.l1;
            uint8_t t10 = ((t8 == t9));
            if (!t10) {
                goto gm_lb2;
            }
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t11 = gm_l0;
    gm_closure t12 = ((gm_closure){(void *)gm_f_fn_m1ukc9a, (void *)&F.h, cx});
    uint8_t t13 = gm_f_any_child_ffc8h1k(t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0p2fb2b(void * a0, gm_tuo61a5rdrbtf a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0p2fb2b F;
    void * gm_l0; /* l */
    gm_tuo61a5rdrbtf gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_n936iee();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_thihbkv(t8, ((uint64_t)1ULL), t10);
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
    gm_tuo61a5rdrbtf t18 = gm_l1;
    GM_ST(gm_tuo61a5rdrbtf, (((uint8_t *)t16) + (uintptr_t)t17 * 12), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_1ncolkq(void * a0, gm_cs9ufftihtbpo2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_1ncolkq F;
    void * gm_l0; /* l */
    gm_tcc1rq4n1avb2 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tcc1rq4n1avb2, a1);
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
        gm_b16_8 t9 = gm_f_empty_slice_crpekm1();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_o08qncv(t8, ((uint64_t)1ULL), t10);
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
    gm_tcc1rq4n1avb2 t18 = gm_l1;
    GM_ST(gm_tcc1rq4n1avb2, (((uint8_t *)t16) + (uintptr_t)t17 * 8), t18);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_472i7vl(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_472i7vl F;
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
        gm_b16_8 t9 = gm_f_empty_slice_ot5ttb7();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_icepq2q(t8, ((uint64_t)1ULL), t10);
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
    memmove((((uint8_t *)t16) + (uintptr_t)t17 * 136), ((uint8_t *)a1), 136);
    void * t18 = gm_l0;
    void * t19 = gm_l0;
    void * t20 = GM_LD(void *, ((uint8_t *)t19));
    uint64_t t21 = gm_l2;
    uint64_t r22; bool o23 = __builtin_add_overflow(t21, ((uint64_t)1ULL), &r22);
    uint64_t t24 = r22;
    uint64_t t25 = ((uintptr_t)t24) - (uintptr_t)((uint64_t)0ULL);
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t20 + (uintptr_t)((uint64_t)0ULL) * 136)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_am7bo0v(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_am7bo0v F;
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
        gm_b16_8 t9 = gm_f_empty_slice_6trg8r7();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_8r12kcb(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_gtq94gd(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_gtq94gd F;
    void * gm_l0; /* l */
    gm_tiilmfd16v3fc gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_jviatgq();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_jd8h83s(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_hbhlgi6(void * a0, gm_tl874t2atnlh3 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_hbhlgi6 F;
    void * gm_l0; /* l */
    gm_tl874t2atnlh3 gm_l1; /* x */
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
        gm_b16_8 t9 = gm_f_empty_slice_s65n31g();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_k8jaegk(t8, ((uint64_t)1ULL), t10);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_roqi806(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_roqi806 F;
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
        gm_b16_8 t9 = gm_f_empty_slice_1fu8oa9();
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_tftb1odc739t5 t11 = gm_f_regrow_9a4nica(t8, ((uint64_t)1ULL), t10);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8r12kcb(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8r12kcb F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_9a4nica(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_9a4nica F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_icepq2q(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_icepq2q F;
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
    gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)136ULL), ((uint64_t)8ULL));
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_jd8h83s(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_jd8h83s F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_k8jaegk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_k8jaegk F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_o08qncv(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_o08qncv F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_thihbkv(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_thihbkv F;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_set_inst_def_tcpabg0(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2) {
    struct gm_fr_set_inst_def_tcpabg0 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 37u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tftb1odc739t5 gm_l3;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 2184);
    uint32_t t4 = gm_l1;
    gm_tftb1odc739t5 t5 = gm_f_put_3pvr70r(t3, t4, &(*(gm_tdgfduqjheqdm *)((uint8_t *)a2)));
    gm_l3 = t5;
    uint8_t t6 = (uint8_t)!((((uint8_t *)&gm_l3))[0] != 0);
    if (!t6) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l3) + 1, gm_srclv0dcbbfhves, 615, 5);
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t5 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t2, ((uint8_t)1), t4);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_template_marker_heil9i9(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_template_marker_heil9i9 F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t2 = gm_l0;
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_b16_8 t4;
    GM_ST(void *, (uint8_t *)&t4, (void *)(gm_seb2o268bfiia));
    GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(10));
    gm_t428ue2u3hf5g t5;
    GM_ST(uint8_t, (uint8_t *)&t5 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t5 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t5 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t5 + 24, 0);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)3ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_t53at80pfdus3 t6;
    GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t6 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)21ULL));
    GM_ST(gm_t428ue2u3hf5g, ((uint8_t *)&t6 + 8), t5);
    gm_t88rcucg1b7uk t7 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t3, &t6);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9gjij2f(void *env, gm_node *cx, gm_node *inst, void * a0);
extern __attribute__((aligned(4))) uint8_t gm_f_fn_m1ukc9a(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_forward_call_qat9d2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3) {
    struct gm_fr_forward_call_qat9d2e F;
    void * gm_l0; /* s */
    void * gm_l1; /* mc */
    uint32_t gm_l2; /* c */
    gm_b16_8 gm_l3; /* args */
    gm_tiilmfd16v3fc gm_l4; /* inst */
    uint32_t gm_l5; /* t */
    gm_b16_8 gm_l6; /* tps */
    gm_b16_8 gm_l7; /* generics */
    gm_t88rcucg1b7uk gm_l8; /* marker */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    uint64_t gm_l13; /* i */
    gm_t88rcucg1b7uk gm_l14; /* a */
    gm_toceffb697p0u gm_l15; /* found */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_tl874t2atnlh3 gm_l18; /* x */
    gm_tmcj65fi8d3nd gm_l19; /* sub */
    gm_tiilmfd16v3fc gm_l21; /* shared */
    void * gm_l22; /* sites */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* k */
    uint32_t gm_l26; /* h */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_tuo61a5rdrbtf gm_l29; /* site */
    gm_tl874t2atnlh3 gm_l30; /* span */
    gm_t88rcucg1b7uk gm_l32; /* raw */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    gm_tl874t2atnlh3 gm_l35; /* x */
    GM_STACK_CHECK(&F, 2407u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tlcm4lvu3g1a8 gm_l36;
    gm_tftb1odc739t5 gm_l37;
    gm_tl874t2atnlh3 gm_l38;
    gm_tftb1odc739t5 gm_l39;
    gm_tftb1odc739t5 gm_l40;
    gm_tqu3m2l3n8mnc gm_l41;
    gm_tn68el8d14vku gm_l42;
    gm_tftb1odc739t5 gm_l43;
    gm_tftb1odc739t5 gm_l44;
    gm_tftb1odc739t5 gm_l45;
    gm_tftb1odc739t5 gm_l46;
    {
        void * t29 = gm_l1;
        uint64_t t30 = GM_LD(uint64_t, ((((uint8_t *)t29) + 120) + 8));
        uint8_t t31 = ((t30 == ((uint64_t)0ULL)));
        if (t31) {
            memset(&(*(gm_t9f76dterbqj6 *)gm_out), 0, sizeof (*(gm_t9f76dterbqj6 *)gm_out));
            goto gm_fdone;
        }
        void * t32 = gm_l0;
        void * t33 = GM_LD(void *, (((uint8_t *)t32) + 344));
        uint64_t t34 = GM_LD(uint64_t, ((((uint8_t *)t32) + 344) + 8));
        uint32_t t35 = gm_l2;
        uint64_t t36 = (uint64_t)t35;
        if ((uintptr_t)t36 >= (uintptr_t)(t34)) gm_trap_index((uintptr_t)t36, t34, gm_srcb5s85si25i3o, 132, 13);
        uint8_t t37 = (uint8_t)((((((uint8_t *)t33) + (uintptr_t)t36 * 136) + 96))[24] != 0);
        if (t37) {
            memmove(((uint8_t *)&gm_l4), ((((uint8_t *)t33) + (uintptr_t)t36 * 136) + 96), 24);
        } else {
            memset(&(*(gm_t9f76dterbqj6 *)gm_out), 0, sizeof (*(gm_t9f76dterbqj6 *)gm_out));
            goto gm_fdone;
        }
        uint32_t t38 = GM_LD(uint32_t, ((uint8_t *)&gm_l4));
        gm_l5 = t38;
        void * t39 = gm_l0;
        uint32_t t40 = gm_l5;
        gm_b16_8 t41 = gm_f_template_params_6stj17b((void *)NULL, cx, NULL, t39, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t41;
        uint64_t t42 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
        uint8_t t43 = ((t42 == ((uint64_t)0ULL)));
        if (t43) {
            memset(&(*(gm_t9f76dterbqj6 *)gm_out), 0, sizeof (*(gm_t9f76dterbqj6 *)gm_out));
            goto gm_fdone;
        }
        void * t44 = gm_l0;
        void * t45 = GM_LD(void *, (((uint8_t *)t44) + 344));
        uint64_t t46 = GM_LD(uint64_t, ((((uint8_t *)t44) + 344) + 8));
        uint32_t t47 = gm_l5;
        uint64_t t48 = (uint64_t)t47;
        if ((uintptr_t)t48 >= (uintptr_t)(t46)) gm_trap_index((uintptr_t)t48, t46, gm_srcb5s85si25i3o, 136, 17);
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((((uint8_t *)t45) + (uintptr_t)t48 * 136) + 80));
        gm_l7 = t49;
        void * t50 = gm_l0;
        gm_t88rcucg1b7uk t51 = gm_f_template_marker_heil9i9((void *)NULL, cx, NULL, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t51;
        gm_trqfvmjtt4sij t52;
        gm_f_list_31sqk5v((void *)&t52, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l9 = t52;
        gm_trqfvmjtt4sij t53;
        gm_f_list_nof7vfj((void *)&t53, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l10 = t53;
        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l4) + 8));
        gm_l11 = t54;
        gm_l12 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t55 = gm_l12;
                uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                uint8_t t57 = (t55 < t56);
                if (!t57) {
                    goto gm_lb1;
                }
                {
                    uint64_t t58 = gm_l12;
                    void * t59 = GM_LD(void *, ((uint8_t *)&gm_l11));
                    uint64_t t60 = GM_LD(uint64_t, (((uint8_t *)&gm_l11) + 8));
                    uint64_t t61 = gm_l12;
                    gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t59) + (uintptr_t)t61 * 4));
                    gm_tlcm4lvu3g1a8 t63;
                    GM_ST(uint32_t, (uint8_t *)&t63 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t63 + 0), t58);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t63 + 8), t62);
                    gm_l36 = t63;
                    {
                        {
                            uint64_t t64 = GM_LD(uint64_t, ((uint8_t *)&gm_l36));
                            gm_l13 = t64;
                            gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l36) + 8));
                            gm_l14 = t65;
                            void * t66 = GM_LD(void *, ((uint8_t *)&gm_l7));
                            uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                            uint64_t t68 = gm_l13;
                            if ((uintptr_t)t68 >= (uintptr_t)(t67)) gm_trap_index((uintptr_t)t68, t67, gm_srcb5s85si25i3o, 141, 12);
                            uint32_t t69 = GM_LD(uint32_t, (((uint8_t *)t66) + (uintptr_t)t68 * 4));
                            gm_b16_8 t70 = gm_l6;
                            uint8_t t71 = 0;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t70) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t70))) + i * 4, ((uint8_t *)&t69))) { t71 = 1; break; }
                            uint8_t t72 = (uint8_t)(!t71);
                            if (t72) {
                                void * t73 = (void *)((uint8_t *)&F.l10);
                                void * t74 = gm_l0;
                                gm_t88rcucg1b7uk t75 = gm_l14;
                                void * t76 = gm_l1;
                                gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)t76));
                                gm_t88rcucg1b7uk t78 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t74, t75, t77);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t79 = gm_f_push_k3l9nic(t73, GM_CONV(gm_csguankko4lmhd, t78));
                                gm_l37 = t79;
                                uint8_t t80 = (uint8_t)!((((uint8_t *)&gm_l37))[0] != 0);
                                if (!t80) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l37) + 1, gm_srcb5s85si25i3o, 142, 13);
                                goto gm_lb3;
                            }
                            gm_toceffb697p0u t81;
                            memset(&t81, 0, sizeof t81);
                            gm_l15 = t81;
                            void * t82 = gm_l1;
                            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)t82) + 120));
                            gm_l16 = t83;
                            gm_l17 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t84 = gm_l17;
                                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                    uint8_t t86 = (t84 < t85);
                                    if (!t86) {
                                        goto gm_lb6;
                                    }
                                    {
                                        void * t87 = GM_LD(void *, ((uint8_t *)&gm_l16));
                                        uint64_t t88 = GM_LD(uint64_t, (((uint8_t *)&gm_l16) + 8));
                                        uint64_t t89 = gm_l17;
                                        {
                                            {
                                                gm_tl874t2atnlh3 t90 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t87) + (uintptr_t)t89 * 12));
                                                gm_l18 = t90;
                                                uint32_t t91 = GM_LD(uint32_t, ((uint8_t *)&gm_l18));
                                                gm_t88rcucg1b7uk t92 = gm_l8;
                                                gm_tplpbqf3duvn3 t93;
                                                GM_ST(uint32_t, ((uint8_t *)&t93 + 0), t91);
                                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t93 + 4), t92);
                                                gm_tmcj65fi8d3nd t94;
                                                GM_ST(gm_tplpbqf3duvn3, ((uint8_t *)&t94 + 0), t93);
                                                gm_l19 = t94;
                                                void * t95 = gm_l0;
                                                gm_t88rcucg1b7uk t96 = gm_l14;
                                                void * t97 = (void *)((uint8_t *)&gm_l19);
                                                uint64_t t98 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
                                                gm_b16_8 t99;
                                                GM_ST(void *, (uint8_t *)&t99, (void *)(((uint8_t *)t97 + (uintptr_t)((uint64_t)0ULL) * 8)));
                                                GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(t98));
                                                gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
                                                gm_t88rcucg1b7uk t101 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t95, t96, t100);
                                                if (gm_unw.kind) {
                                                    goto gm_lad;
                                                }
                                                gm_t88rcucg1b7uk t102 = gm_l8;
                                                uint8_t t103 = (gm_eq9m6mvtmgde(((uint8_t *)&t101), ((uint8_t *)&t102)));
                                                if (t103) {
                                                    gm_tl874t2atnlh3 t104 = gm_l18;
                                                    gm_toceffb697p0u t105;
                                                    GM_ST(uint8_t, (uint8_t *)&t105 + 13, 0);
                                                    GM_ST(uint16_t, (uint8_t *)&t105 + 14, 0);
                                                    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t105), t104);
                                                    ((uint8_t *)&t105)[12] = 1;
                                                    gm_l15 = t105;
                                                }
                                                goto gm_lb9;
                                            }
gm_lb10:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 146, 9);
                                        }
gm_lb9:;
                                    }
gm_lb8:;
                                    uint64_t t106 = gm_l17;
                                    uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                                    uint64_t t109 = r107;
                                    gm_l17 = t109;
                                }
                            }
gm_lb6:;
                            void * t110 = (void *)((uint8_t *)&F.l9);
                            uint8_t t111 = (uint8_t)((((uint8_t *)&gm_l15))[12] != 0);
                            if (t111) {
                                gm_tl874t2atnlh3 t112 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)&gm_l15));
                                gm_l38 = t112;
                            } else {
                                memset(&(*(gm_t9f76dterbqj6 *)gm_out), 0, sizeof (*(gm_t9f76dterbqj6 *)gm_out));
                                goto gm_fdone;
                            }
                            gm_tl874t2atnlh3 t113 = gm_l38;
                            gm_tftb1odc739t5 t114 = gm_f_push_hbhlgi6(t110, t113);
                            gm_l39 = t114;
                            uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l39))[0] != 0);
                            if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l39) + 1, gm_srcb5s85si25i3o, 150, 9);
                            void * t116 = (void *)((uint8_t *)&F.l10);
                            gm_t88rcucg1b7uk t117 = gm_l8;
                            gm_tftb1odc739t5 t118 = gm_f_push_k3l9nic(t116, GM_CONV(gm_csguankko4lmhd, t117));
                            gm_l40 = t118;
                            uint8_t t119 = (uint8_t)!((((uint8_t *)&gm_l40))[0] != 0);
                            if (!t119) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l40) + 1, gm_srcb5s85si25i3o, 151, 9);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 140, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t120 = gm_l12;
                uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                uint64_t t123 = r121;
                gm_l12 = t123;
            }
        }
gm_lb1:;
    }
    {
        void * t124 = gm_l0;
        void * t125 = (void *)(((uint8_t *)t124) + 1424);
        uint32_t t126 = gm_l5;
        gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
        gm_b16_8 t128 = GM_LD(gm_b16_8, ((uint8_t *)&t127));
        gm_tiilmfd16v3fc t129;
        GM_ST(uint32_t, (uint8_t *)&t129 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t129 + 0), t126);
        GM_ST(gm_b16_8, ((uint8_t *)&t129 + 8), t128);
        gm_f_get_s7blvas((void *)&gm_l41, t125, &t129);
        uint8_t t130 = (uint8_t)((((uint8_t *)&gm_l41))[24] != 0);
        if (t130) {
            memmove(((uint8_t *)&gm_l21), ((uint8_t *)&gm_l41), 24);
        } else {
            {
                uint32_t t131 = gm_l5;
                uint32_t t132 = gm_l2;
                gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                gm_b16_8 t134 = GM_LD(gm_b16_8, ((uint8_t *)&t133));
                void * t135 = gm_l1;
                gm_tl874t2atnlh3 t136 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t135) + 96));
                void * t137 = gm_l0;
                uint32_t t138 = GM_LD(uint32_t, (((uint8_t *)t137) + 1360));
                uint32_t r139; bool o140 = __builtin_add_overflow(t138, ((uint32_t)1ULL), &r139);
                if (o140) gm_trap("integer overflow in `+`", gm_srcb5s85si25i3o, 154, 100);
                uint32_t t141 = r139;
                GM_ST(uint32_t, ((uint8_t *)&F.l20 + 0), t131);
                GM_ST(uint32_t, ((uint8_t *)&F.l20 + 4), t132);
                GM_ST(gm_b16_8, ((uint8_t *)&F.l20 + 8), t134);
                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&F.l20 + 24), t136);
                GM_ST(uint32_t, ((uint8_t *)&F.l20 + 36), t141);
            }
            {
                void * t142 = gm_l0;
                gm_b16_8 t143 = gm_l6;
                gm_b16_8 t144 = GM_LD(gm_b16_8, ((uint8_t *)&F.l10));
                gm_b16_8 t145 = GM_LD(gm_b16_8, ((uint8_t *)&t144));
                gm_t0ut88vjdgrt1 t146 = GM_LD(gm_t0ut88vjdgrt1, ((uint8_t *)&F.l20));
                gm_f_make_shared_r51o03d((void *)&gm_l21, (void *)NULL, cx, NULL, t142, &t146, t143, t145);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        memmove(((uint8_t *)&gm_l21), ((uint8_t *)&gm_l21), 24);
        {
            {
                void * t147 = gm_l1;
                uint8_t t148 = (uint8_t)(GM_LD(void *, (((uint8_t *)t147) + 136)) != 0);
                if (!t148) {
                    goto gm_lb12;
                }
                void * t149 = GM_LD(void *, (((uint8_t *)t147) + 136));
                gm_l22 = t149;
                gm_b16_8 t150 = gm_l6;
                gm_l23 = t150;
                gm_l24 = ((uint64_t)0ULL);
                {
                    for (;;) {
                        uint64_t t151 = gm_l24;
                        uint64_t t152 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                        uint8_t t153 = (t151 < t152);
                        if (!t153) {
                            goto gm_lb13;
                        }
                        {
                            uint64_t t154 = gm_l24;
                            void * t155 = GM_LD(void *, ((uint8_t *)&gm_l23));
                            uint64_t t156 = GM_LD(uint64_t, (((uint8_t *)&gm_l23) + 8));
                            uint64_t t157 = gm_l24;
                            uint32_t t158 = GM_LD(uint32_t, (((uint8_t *)t155) + (uintptr_t)t157 * 4));
                            gm_tn68el8d14vku t159;
                            GM_ST(uint32_t, (uint8_t *)&t159 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t159 + 0), t154);
                            GM_ST(uint32_t, ((uint8_t *)&t159 + 8), t158);
                            gm_l42 = t159;
                            {
                                {
                                    uint64_t t160 = GM_LD(uint64_t, ((uint8_t *)&gm_l42));
                                    gm_l25 = t160;
                                    uint32_t t161 = GM_LD(uint32_t, (((uint8_t *)&gm_l42) + 8));
                                    gm_l26 = t161;
                                    gm_b16_8 t162 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                                    gm_l27 = t162;
                                    gm_l28 = ((uint64_t)0ULL);
                                    {
                                        for (;;) {
                                            uint64_t t163 = gm_l28;
                                            uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                                            uint8_t t165 = (t163 < t164);
                                            if (!t165) {
                                                goto gm_lb18;
                                            }
                                            {
                                                void * t166 = GM_LD(void *, ((uint8_t *)&gm_l27));
                                                uint64_t t167 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                                                uint64_t t168 = gm_l28;
                                                {
                                                    {
                                                        gm_tuo61a5rdrbtf t169 = GM_LD(gm_tuo61a5rdrbtf, (((uint8_t *)t166) + (uintptr_t)t168 * 12));
                                                        gm_l29 = t169;
                                                        uint32_t t170 = GM_LD(uint32_t, ((uint8_t *)&gm_l29));
                                                        uint32_t t171 = gm_l26;
                                                        uint8_t t172 = ((t170 == t171));
                                                        if (t172) {
                                                            void * t173 = gm_l22;
                                                            void * t174 = GM_LD(void *, ((uint8_t *)&F.l9));
                                                            uint64_t t175 = GM_LD(uint64_t, (((uint8_t *)&F.l9) + 8));
                                                            uint64_t t176 = gm_l25;
                                                            if ((uintptr_t)t176 >= (uintptr_t)(t175)) gm_trap_index((uintptr_t)t176, t175, gm_srcb5s85si25i3o, 160, 57);
                                                            uint32_t t177 = GM_LD(uint32_t, (((uint8_t *)t174) + (uintptr_t)t176 * 12));
                                                            gm_tkn42u7na3t81 t178 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&gm_l29) + 4));
                                                            gm_tuo61a5rdrbtf t179;
                                                            GM_ST(uint32_t, ((uint8_t *)&t179 + 0), t177);
                                                            GM_ST(gm_tkn42u7na3t81, ((uint8_t *)&t179 + 4), t178);
                                                            gm_tftb1odc739t5 t180 = gm_f_push_0p2fb2b(t173, t179);
                                                            gm_l43 = t180;
                                                            uint8_t t181 = (uint8_t)!((((uint8_t *)&gm_l43))[0] != 0);
                                                            if (!t181) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l43) + 1, gm_srcb5s85si25i3o, 160, 38);
                                                        }
                                                        goto gm_lb21;
                                                    }
gm_lb22:;
                                                    gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 159, 13);
                                                }
gm_lb21:;
                                            }
gm_lb20:;
                                            uint64_t t182 = gm_l28;
                                            uint64_t r183; bool o184 = __builtin_add_overflow(t182, ((uint64_t)1ULL), &r183);
                                            uint64_t t185 = r183;
                                            gm_l28 = t185;
                                        }
                                    }
gm_lb18:;
                                    goto gm_lb16;
                                }
gm_lb17:;
                                gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 158, 9);
                            }
gm_lb16:;
                        }
gm_lb15:;
                        uint64_t t186 = gm_l24;
                        uint64_t r187; bool o188 = __builtin_add_overflow(t186, ((uint64_t)1ULL), &r187);
                        uint64_t t189 = r187;
                        gm_l24 = t189;
                    }
                }
gm_lb13:;
                goto gm_lb11;
            }
gm_lb12:;
        }
gm_lb11:;
    }
    {
        void * t190 = gm_l1;
        gm_tl874t2atnlh3 t191 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t190) + 96));
        gm_l30 = t191;
        uint64_t t192 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
        uint8_t t193 = (t192 > ((uint64_t)0ULL));
        if (t193) {
            void * t194 = GM_LD(void *, ((uint8_t *)&gm_l3));
            uint64_t t195 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
            gm_tl874t2atnlh3 t196 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t194) + (uintptr_t)((uint64_t)0ULL) * 88) + 72));
            gm_l30 = t196;
        }
        gm_trqfvmjtt4sij t197;
        gm_f_list_vp1djv1_2((void *)&t197, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l31 = t197;
        void * t198 = (void *)((uint8_t *)&F.l31);
        gm_b16_8 t199 = gm_l3;
        gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
        gm_tftb1odc739t5 t201 = gm_f_extend_gig5o6g(t198, t200);
        gm_l44 = t201;
        uint8_t t202 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
        if (!t202) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 1, gm_srcb5s85si25i3o, 167, 5);
        void * t203 = gm_l0;
        void * t204 = (void *)(((uint8_t *)t203) + 8);
        gm_t53at80pfdus3 t205;
        memset(&t205, 0, sizeof t205);
        GM_ST(uint8_t, ((uint8_t *)&t205 + 0), ((uint8_t)7ULL));
        gm_t88rcucg1b7uk t206 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t204, &t205);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l32 = t206;
        gm_b16_8 t207 = GM_LD(gm_b16_8, ((uint8_t *)&F.l9));
        gm_l33 = t207;
        gm_l34 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t208 = gm_l34;
                uint64_t t209 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                uint8_t t210 = (t208 < t209);
                if (!t210) {
                    goto gm_lb23;
                }
                {
                    void * t211 = GM_LD(void *, ((uint8_t *)&gm_l33));
                    uint64_t t212 = GM_LD(uint64_t, (((uint8_t *)&gm_l33) + 8));
                    uint64_t t213 = gm_l34;
                    {
                        {
                            gm_tl874t2atnlh3 t214 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t211) + (uintptr_t)t213 * 12));
                            gm_l35 = t214;
                            {
                                void * t215 = (void *)((uint8_t *)&F.l31);
                                gm_t88rcucg1b7uk t216 = gm_l32;
                                uint32_t t217 = GM_LD(uint32_t, (((uint8_t *)&gm_l35) + 4));
                                gm_tl874t2atnlh3 t218 = gm_l30;
                                gm_to0f5qn75cgg8 t219;
                                memset(&t219, 0, sizeof t219);
                                GM_ST(uint8_t, ((uint8_t *)&t219 + 0), ((uint8_t)15ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t219 + 8), t217);
                                void * t220 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t216, &t219, t218);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t221 = gm_f_push_d943vgp_2(t215, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t220)));
                                gm_l45 = t221;
                            }
                            uint8_t t222 = (uint8_t)!((((uint8_t *)&gm_l45))[0] != 0);
                            if (!t222) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l45) + 1, gm_srcb5s85si25i3o, 170, 9);
                            {
                                void * t223 = (void *)((uint8_t *)&F.l31);
                                void * t224 = gm_l0;
                                void * t225 = (void *)(((uint8_t *)t224) + 8);
                                gm_t88rcucg1b7uk t226 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t225);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                uint32_t t227 = GM_LD(uint32_t, (((uint8_t *)&gm_l35) + 8));
                                gm_tl874t2atnlh3 t228 = gm_l30;
                                gm_to0f5qn75cgg8 t229;
                                memset(&t229, 0, sizeof t229);
                                GM_ST(uint8_t, ((uint8_t *)&t229 + 0), ((uint8_t)15ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t229 + 8), t227);
                                void * t230 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t226, &t229, t228);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t231 = gm_f_push_d943vgp_2(t223, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t230)));
                                gm_l46 = t231;
                            }
                            uint8_t t232 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                            if (!t232) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_srcb5s85si25i3o, 171, 9);
                            goto gm_lb26;
                        }
gm_lb27:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 169, 5);
                    }
gm_lb26:;
                }
gm_lb25:;
                uint64_t t233 = gm_l34;
                uint64_t r234; bool o235 = __builtin_add_overflow(t233, ((uint64_t)1ULL), &r234);
                uint64_t t236 = r234;
                gm_l34 = t236;
            }
        }
gm_lb23:;
    }
    {
        uint32_t t237 = GM_LD(uint32_t, ((uint8_t *)&gm_l21));
        gm_b16_8 t238 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
        gm_tsth9qu1dv0g3 t239;
        memset(&t239, 0, sizeof t239);
        GM_ST(uint8_t, ((uint8_t *)&t239 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t239 + 8), t237);
        gm_to76jli3bqddq t240;
        GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t240 + 0), t239);
        GM_ST(gm_b16_8, ((uint8_t *)&t240 + 40), t238);
        gm_to0f5qn75cgg8 t241;
        GM_ST(uint8_t, (uint8_t *)&t241 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t241 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t241 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t241 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t241 + 8), t240);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t9f76dterbqj6 *)gm_out) + 65, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t9f76dterbqj6 *)gm_out) + 66, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t9f76dterbqj6 *)gm_out) + 68, 0);
        GM_ST(gm_to0f5qn75cgg8, ((uint8_t *)&(*(gm_t9f76dterbqj6 *)gm_out)), t241);
        ((uint8_t *)&(*(gm_t9f76dterbqj6 *)gm_out))[64] = 1;
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
inline __attribute__((aligned(4))) uint8_t gm_f_forwards_l1foetf(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, const void * a2, gm_b16_8 a3) {
    struct gm_fr_forwards_l1foetf F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_tiilmfd16v3fc gm_l2; /* inst */
    gm_b16_8 gm_l3; /* sub */
    uint32_t gm_l4; /* t */
    gm_b16_8 gm_l5; /* tps */
    gm_b16_8 gm_l6; /* generics */
    gm_t88rcucg1b7uk gm_l7; /* marker */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_t88rcucg1b7uk gm_l11; /* a */
    gm_t88rcucg1b7uk gm_l12; /* m */
    GM_STACK_CHECK(&F, 473u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_tlcm4lvu3g1a8 gm_l13;
    uint32_t t7 = GM_LD(uint32_t, ((uint8_t *)a2));
    gm_l4 = t7;
    uint32_t t8 = gm_l4;
    uint32_t t9 = gm_l1;
    uint8_t t10 = ((t8 == t9));
    if (t10) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t11 = gm_l0;
    uint32_t t12 = gm_l4;
    gm_b16_8 t13 = gm_f_template_params_6stj17b((void *)NULL, cx, NULL, t11, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l5 = t13;
    uint64_t t14 = GM_LD(uint64_t, (((uint8_t *)&gm_l5) + 8));
    uint8_t t15 = ((t14 == ((uint64_t)0ULL)));
    if (t15) {
        F.ret = ((uint8_t)0);
        goto gm_fret;
    }
    void * t16 = gm_l0;
    void * t17 = GM_LD(void *, (((uint8_t *)t16) + 344));
    uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)t16) + 344) + 8));
    uint32_t t19 = gm_l4;
    uint64_t t20 = (uint64_t)t19;
    if ((uintptr_t)t20 >= (uintptr_t)(t18)) gm_trap_index((uintptr_t)t20, t18, gm_srcb5s85si25i3o, 113, 17);
    gm_b16_8 t21 = GM_LD(gm_b16_8, ((((uint8_t *)t17) + (uintptr_t)t20 * 136) + 80));
    gm_l6 = t21;
    void * t22 = gm_l0;
    gm_t88rcucg1b7uk t23 = gm_f_template_marker_heil9i9((void *)NULL, cx, NULL, t22);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_l7 = t23;
    gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)a2) + 8));
    gm_l8 = t24;
    gm_l9 = ((uint64_t)0ULL);
    {
        for (;;) {
            uint64_t t25 = gm_l9;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
            uint8_t t27 = (t25 < t26);
            if (!t27) {
                goto gm_lb1;
            }
            {
                uint64_t t28 = gm_l9;
                void * t29 = GM_LD(void *, ((uint8_t *)&gm_l8));
                uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint64_t t31 = gm_l9;
                gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t29) + (uintptr_t)t31 * 4));
                gm_tlcm4lvu3g1a8 t33;
                GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t33 + 0), t28);
                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t33 + 8), t32);
                gm_l13 = t33;
                {
                    {
                        uint64_t t34 = GM_LD(uint64_t, ((uint8_t *)&gm_l13));
                        gm_l10 = t34;
                        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l13) + 8));
                        gm_l11 = t35;
                        void * t36 = gm_l0;
                        gm_t88rcucg1b7uk t37 = gm_l11;
                        gm_b16_8 t38 = gm_l3;
                        gm_t88rcucg1b7uk t39 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t36, t37, t38);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l12 = t39;
                        void * t40 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t41 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t42 = gm_l10;
                        if ((uintptr_t)t42 >= (uintptr_t)(t41)) gm_trap_index((uintptr_t)t42, t41, gm_srcb5s85si25i3o, 117, 12);
                        uint32_t t43 = GM_LD(uint32_t, (((uint8_t *)t40) + (uintptr_t)t42 * 4));
                        gm_b16_8 t44 = gm_l5;
                        uint8_t t45 = 0;
                        for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t44) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t44))) + i * 4, ((uint8_t *)&t43))) { t45 = 1; break; }
                        uint8_t t46 = (uint8_t)(t45);
                        if (t46) {
                            gm_t88rcucg1b7uk t47 = gm_l12;
                            gm_t88rcucg1b7uk t48 = gm_l7;
                            uint8_t t49 = (!gm_eq9m6mvtmgde(((uint8_t *)&t47), ((uint8_t *)&t48)));
                            if (t49) {
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                        } else {
                            gm_t88rcucg1b7uk t50 = gm_l12;
                            gm_t88rcucg1b7uk t51 = gm_l11;
                            uint8_t t52 = (!gm_eq9m6mvtmgde(((uint8_t *)&t50), ((uint8_t *)&t51)));
                            if (t52) {
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                        }
                        goto gm_lb4;
                    }
gm_lb5:;
                    gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 115, 5);
                }
gm_lb4:;
            }
gm_lb3:;
            uint64_t t53 = gm_l9;
            uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
            uint64_t t56 = r54;
            gm_l9 = t56;
        }
    }
gm_lb1:;
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
__attribute__((aligned(4))) void gm_f_make_shared_r51o03d(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_make_shared_r51o03d F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* tps */
    gm_b16_8 gm_l3; /* erased */
    uint32_t gm_l4; /* f */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    uint32_t gm_l11; /* g */
    gm_t88rcucg1b7uk gm_l12; /* raw */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint32_t gm_l21; /* id */
    uint32_t gm_l23; /* outer */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_b16_8 gm_l32; /* <tmp> */
    uint64_t gm_l33; /* <tmp> */
    uint32_t gm_l34; /* g */
    uint32_t gm_l35; /* l */
    void * gm_l36; /* body */
    void * gm_l37; /* b */
    void * gm_l38; /* copy */
    gm_b16_8 gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    gm_tiilmfd16v3fc gm_l42; /* shared */
    GM_STACK_CHECK(&F, 2940u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_tn68el8d14vku gm_l43;
    gm_tftb1odc739t5 gm_l44;
    uint8_t gm_l45;
    gm_tftb1odc739t5 gm_l46;
    gm_tftb1odc739t5 gm_l47;
    gm_tftb1odc739t5 gm_l48;
    gm_tftb1odc739t5 gm_l49;
    gm_tftb1odc739t5 gm_l50;
    gm_tftb1odc739t5 gm_l51;
    gm_tftb1odc739t5 gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_tftb1odc739t5 gm_l54;
    gm_tftb1odc739t5 gm_l55;
    gm_tftb1odc739t5 gm_l56;
    gm_tftb1odc739t5 gm_l57;
    {
        uint32_t t37 = GM_LD(uint32_t, ((uint8_t *)a1));
        gm_l4 = t37;
        void * t38 = gm_l0;
        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 344));
        uint64_t t40 = GM_LD(uint64_t, ((((uint8_t *)t38) + 344) + 8));
        uint32_t t41 = gm_l4;
        uint64_t t42 = (uint64_t)t41;
        if ((uintptr_t)t42 >= (uintptr_t)(t40)) gm_trap_index((uintptr_t)t42, t40, gm_srcb5s85si25i3o, 243, 13);
        memmove(((uint8_t *)&F.l5), (((uint8_t *)t39) + (uintptr_t)t42 * 136), 136);
        void * t43 = gm_l0;
        void * t44 = GM_LD(void *, (((uint8_t *)t43) + 384));
        uint64_t t45 = GM_LD(uint64_t, ((((uint8_t *)t43) + 384) + 8));
        uint32_t t46 = gm_l4;
        uint64_t t47 = (uint64_t)t46;
        if ((uintptr_t)t47 >= (uintptr_t)(t45)) gm_trap_index((uintptr_t)t47, t45, gm_srcb5s85si25i3o, 244, 12);
        memmove(((uint8_t *)&F.l6), (((uint8_t *)t44) + (uintptr_t)t47 * 64), 64);
        gm_trqfvmjtt4sij t48;
        gm_f_list_oalq03d((void *)&t48, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l7 = t48;
        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 80));
        gm_b16_8 t50 = GM_LD(gm_b16_8, ((uint8_t *)&t49));
        gm_l8 = t50;
        gm_l9 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t51 = gm_l9;
                uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                uint8_t t53 = (t51 < t52);
                if (!t53) {
                    goto gm_lb1;
                }
                {
                    uint64_t t54 = gm_l9;
                    void * t55 = GM_LD(void *, ((uint8_t *)&gm_l8));
                    uint64_t t56 = GM_LD(uint64_t, (((uint8_t *)&gm_l8) + 8));
                    uint64_t t57 = gm_l9;
                    uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)t55) + (uintptr_t)t57 * 4));
                    gm_tn68el8d14vku t59;
                    GM_ST(uint32_t, (uint8_t *)&t59 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t59 + 0), t54);
                    GM_ST(uint32_t, ((uint8_t *)&t59 + 8), t58);
                    gm_l43 = t59;
                    {
                        {
                            uint64_t t60 = GM_LD(uint64_t, ((uint8_t *)&gm_l43));
                            gm_l10 = t60;
                            uint32_t t61 = GM_LD(uint32_t, (((uint8_t *)&gm_l43) + 8));
                            gm_l11 = t61;
                            void * t62 = (void *)((uint8_t *)&F.l7);
                            uint32_t t63 = gm_l11;
                            void * t64 = GM_LD(void *, ((uint8_t *)&gm_l3));
                            uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 8));
                            uint64_t t66 = gm_l10;
                            if ((uintptr_t)t66 >= (uintptr_t)(t65)) gm_trap_index((uintptr_t)t66, t65, gm_srcb5s85si25i3o, 246, 78);
                            gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t64) + (uintptr_t)t66 * 4));
                            gm_tplpbqf3duvn3 t68;
                            GM_ST(uint32_t, ((uint8_t *)&t68 + 0), t63);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t68 + 4), t67);
                            gm_tftb1odc739t5 t69 = gm_f_push_h69n5fi(t62, GM_CONV(gm_cs7e9ppsd384bj, t68));
                            gm_l44 = t69;
                            uint8_t t70 = (uint8_t)!((((uint8_t *)&gm_l44))[0] != 0);
                            if (!t70) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l44) + 1, gm_srcb5s85si25i3o, 246, 44);
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 246, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t71 = gm_l9;
                uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                uint64_t t74 = r72;
                gm_l9 = t74;
            }
        }
gm_lb1:;
        void * t75 = gm_l0;
        void * t76 = (void *)(((uint8_t *)t75) + 8);
        gm_t53at80pfdus3 t77;
        memset(&t77, 0, sizeof t77);
        GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)7ULL));
        gm_t88rcucg1b7uk t78 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t76, &t77);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t78;
    }
    {
        gm_trqfvmjtt4sij t79;
        gm_f_list_af74s0q((void *)&t79, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t79;
        gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
        gm_l14 = t80;
        gm_l15 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t81 = gm_l15;
                uint64_t t82 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint8_t t83 = (t81 < t82);
                if (!t83) {
                    goto gm_lb6;
                }
                {
                    void * t84 = GM_LD(void *, ((uint8_t *)&gm_l14));
                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint64_t t86 = gm_l15;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l16), (((uint8_t *)t84) + (uintptr_t)t86 * 56), 56);
                            }
                            {
                                {
                                    uint8_t t87 = (uint8_t)(((((uint8_t *)&F.l16) + 40))[4] != 0);
                                    if (t87) {
                                        goto gm_lb12;
                                    }
                                    gm_l45 = ((uint8_t)0);
                                    goto gm_lb11;
                                }
gm_lb12:;
                                {
                                    gm_l45 = ((uint8_t)1);
                                    goto gm_lb11;
                                }
gm_lb13:;
                                gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 250, 12);
                            }
gm_lb11:;
                            uint8_t t88 = gm_l45;
                            if (t88) {
                                goto gm_lb8;
                            }
                            {
                                memmove(((uint8_t *)&F.l17), ((uint8_t *)&F.l16), 56);
                            }
                            void * t89 = gm_l0;
                            gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l16) + 16));
                            gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                            gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                            gm_t88rcucg1b7uk t93 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t89, t90, t92);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 16), t93);
                            {
                                void * t94 = (void *)((uint8_t *)&F.l13);
                                gm_tftb1odc739t5 t95 = gm_f_push_o8u01sh(t94, &(*(gm_t5v0c8ia6a5mo *)((uint8_t *)&F.l17)));
                                gm_l46 = t95;
                            }
                            uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l46))[0] != 0);
                            if (!t96) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l46) + 1, gm_srcb5s85si25i3o, 253, 9);
                            goto gm_lb9;
                        }
gm_lb10:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 249, 5);
                    }
gm_lb9:;
                }
gm_lb8:;
                uint64_t t97 = gm_l15;
                uint64_t r98; bool o99 = __builtin_add_overflow(t97, ((uint64_t)1ULL), &r98);
                uint64_t t100 = r98;
                gm_l15 = t100;
            }
        }
gm_lb6:;
        gm_b16_8 t101 = gm_l2;
        gm_l18 = t101;
        gm_l19 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t102 = gm_l19;
                uint64_t t103 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                uint8_t t104 = (t102 < t103);
                if (!t104) {
                    goto gm_lb14;
                }
                {
                    void * t105 = GM_LD(void *, ((uint8_t *)&gm_l18));
                    uint64_t t106 = GM_LD(uint64_t, (((uint8_t *)&gm_l18) + 8));
                    uint64_t t107 = gm_l19;
                    {
                        {
                            {
                                void * t108 = (void *)((uint8_t *)&F.l13);
                                gm_b16_8 t109;
                                GM_ST(void *, (uint8_t *)&t109, (void *)(gm_sj3cmvbmi7cm6));
                                GM_ST(uintptr_t, (uint8_t *)&t109 + 8, (uintptr_t)(8));
                                gm_t88rcucg1b7uk t110 = gm_l12;
                                gm_tl874t2atnlh3 t111 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 16));
                                gm_t9805qtonbsag t112;
                                memset(&t112, 0, sizeof t112);
                                gm_t5v0c8ia6a5mo t113;
                                GM_ST(uint8_t, (uint8_t *)&t113 + 49, 0);
                                GM_ST(uint16_t, (uint8_t *)&t113 + 50, 0);
                                GM_ST(uint32_t, (uint8_t *)&t113 + 52, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t113 + 0), t109);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t113 + 16), t110);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t113 + 20), t111);
                                GM_ST(void *, ((uint8_t *)&t113 + 32), ((void *)0));
                                GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t113 + 40), t112);
                                GM_ST(uint8_t, ((uint8_t *)&t113 + 48), ((uint8_t)0));
                                gm_tftb1odc739t5 t114 = gm_f_push_o8u01sh(t108, &t113);
                                gm_l47 = t114;
                            }
                            uint8_t t115 = (uint8_t)!((((uint8_t *)&gm_l47))[0] != 0);
                            if (!t115) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l47) + 1, gm_srcb5s85si25i3o, 256, 9);
                            {
                                void * t116 = (void *)((uint8_t *)&F.l13);
                                gm_b16_8 t117;
                                GM_ST(void *, (uint8_t *)&t117, (void *)(gm_srkol0piugptj));
                                GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(13));
                                void * t118 = gm_l0;
                                void * t119 = (void *)(((uint8_t *)t118) + 8);
                                gm_t88rcucg1b7uk t120 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t119);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tl874t2atnlh3 t121 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 16));
                                gm_t9805qtonbsag t122;
                                memset(&t122, 0, sizeof t122);
                                gm_t5v0c8ia6a5mo t123;
                                GM_ST(uint8_t, (uint8_t *)&t123 + 49, 0);
                                GM_ST(uint16_t, (uint8_t *)&t123 + 50, 0);
                                GM_ST(uint32_t, (uint8_t *)&t123 + 52, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t123 + 0), t117);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t123 + 16), t120);
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t123 + 20), t121);
                                GM_ST(void *, ((uint8_t *)&t123 + 32), ((void *)0));
                                GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t123 + 40), t122);
                                GM_ST(uint8_t, ((uint8_t *)&t123 + 48), ((uint8_t)0));
                                gm_tftb1odc739t5 t124 = gm_f_push_o8u01sh(t116, &t123);
                                gm_l48 = t124;
                            }
                            uint8_t t125 = (uint8_t)!((((uint8_t *)&gm_l48))[0] != 0);
                            if (!t125) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l48) + 1, gm_srcb5s85si25i3o, 257, 9);
                            goto gm_lb17;
                        }
gm_lb18:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 255, 5);
                    }
gm_lb17:;
                }
gm_lb16:;
                uint64_t t126 = gm_l19;
                uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                uint64_t t129 = r127;
                gm_l19 = t129;
            }
        }
gm_lb14:;
    }
    {
        gm_b16_8 t130 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_tl874t2atnlh3 t131 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 16));
        uint64_t t132 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 32));
        gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
        void * t134 = gm_l0;
        gm_t88rcucg1b7uk t135 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l5) + 56));
        gm_b16_8 t136 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_b16_8 t137 = GM_LD(gm_b16_8, ((uint8_t *)&t136));
        gm_t88rcucg1b7uk t138 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t134, t135, t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t139 = gm_l0;
        void * t140 = (void *)(((uint8_t *)&F.l5) + 64);
        gm_b16_8 t141 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
        gm_tnpifv20irnph t143 = gm_f_subst_effects_jnlbu5m((void *)NULL, cx, NULL, t139, t140, t142);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t144 = gm_f_empty_slice_d914i5u();
        uint32_t t145 = gm_l4;
        gm_b16_8 t146 = gm_l3;
        gm_tiilmfd16v3fc t147;
        GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t147 + 0), t145);
        GM_ST(gm_b16_8, ((uint8_t *)&t147 + 8), t146);
        gm_tqu3m2l3n8mnc t148;
        GM_ST(uint8_t, (uint8_t *)&t148 + 25, 0);
        GM_ST(uint16_t, (uint8_t *)&t148 + 26, 0);
        GM_ST(uint32_t, (uint8_t *)&t148 + 28, 0);
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t148), t147);
        ((uint8_t *)&t148)[24] = 1;
        GM_ST(uint32_t, (uint8_t *)&F.l20 + 28, 0);
        GM_ST(uint32_t, (uint8_t *)&F.l20 + 60, 0);
        GM_ST(uint8_t, (uint8_t *)&F.l20 + 129, 0);
        GM_ST(uint16_t, (uint8_t *)&F.l20 + 130, 0);
        GM_ST(uint32_t, (uint8_t *)&F.l20 + 132, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l20 + 0), t130);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&F.l20 + 16), t131);
        GM_ST(uint64_t, ((uint8_t *)&F.l20 + 32), t132);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l20 + 40), t133);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&F.l20 + 56), t138);
        GM_ST(gm_tnpifv20irnph, ((uint8_t *)&F.l20 + 64), t143);
        GM_ST(gm_b16_8, ((uint8_t *)&F.l20 + 80), t144);
        GM_ST(gm_tqu3m2l3n8mnc, ((uint8_t *)&F.l20 + 96), t148);
        GM_ST(uint8_t, ((uint8_t *)&F.l20 + 128), ((uint8_t)0));
        void * t149 = gm_l0;
        gm_t9qkh0rsooc8d t150 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l6));
        uint32_t t151 = gm_f_add_shared_fn_f8l99p1((void *)NULL, cx, NULL, t149, &(*(gm_tqqhoivahirr0 *)((uint8_t *)&F.l20)), t150);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l21 = t151;
        gm_trqfvmjtt4sij t152;
        gm_f_list_p9s2t99((void *)&t152, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l22 = t152;
        void * t153 = gm_l0;
        uint32_t t154 = GM_LD(uint32_t, (((uint8_t *)t153) + 1360));
        gm_l23 = t154;
        void * t155 = gm_l0;
        uint32_t t156 = GM_LD(uint32_t, (((uint8_t *)a1) + 36));
        GM_ST(uint32_t, (((uint8_t *)t155) + 1360), t156);
        gm_trqfvmjtt4sij t157;
        gm_f_list_31sqk5v((void *)&t157, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l24 = t157;
        gm_trqfvmjtt4sij t158;
        gm_f_list_2plb4qp((void *)&t158, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l25 = t158;
        gm_b16_8 t159 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
        gm_b16_8 t160 = GM_LD(gm_b16_8, ((uint8_t *)&t159));
        gm_trqfvmjtt4sij t161;
        gm_f_list_7l73vjb((void *)&t161, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trqfvmjtt4sij t162;
        gm_f_list_7l73vjb((void *)&t162, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tl874t2atnlh3 t163 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
        void * t164 = (void *)((uint8_t *)&F.l22);
        gm_b16_8 t165 = gm_f_empty_slice_s65n31g();
        gm_b16_8 t166 = GM_LD(gm_b16_8, ((uint8_t *)&t165));
        void * t167 = (void *)((uint8_t *)&F.l25);
        gm_tblkd491pk4cr t168;
        GM_ST(uint8_t, (uint8_t *)&t168 + 109, 0);
        GM_ST(uint16_t, (uint8_t *)&t168 + 110, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t168 + 0), t160);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t168 + 16), t161);
        GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t168 + 56), t162);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t168 + 96), t163);
        GM_ST(uint8_t, ((uint8_t *)&t168 + 108), ((uint8_t)0));
        GM_ST(void *, ((uint8_t *)&t168 + 112), t164);
        GM_ST(gm_b16_8, ((uint8_t *)&t168 + 120), t166);
        GM_ST(void *, ((uint8_t *)&t168 + 136), t167);
        F.l26 = t168;
        void * t169 = (void *)(((uint8_t *)&F.l26) + 16);
        uint32_t t170 = gm_l4;
        uint32_t t171 = gm_l21;
        gm_tcc1rq4n1avb2 t172;
        GM_ST(uint32_t, ((uint8_t *)&t172 + 0), t170);
        GM_ST(uint32_t, ((uint8_t *)&t172 + 4), t171);
        gm_tftb1odc739t5 t173 = gm_f_push_1ncolkq(t169, GM_CONV(gm_cs9ufftihtbpo2, t172));
        gm_l49 = t173;
        uint8_t t174 = (uint8_t)!((((uint8_t *)&gm_l49))[0] != 0);
        if (!t174) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l49) + 1, gm_srcb5s85si25i3o, 277, 5);
        gm_b16_8 t175 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 24));
        gm_l27 = t175;
        gm_l28 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t176 = gm_l28;
                uint64_t t177 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                uint8_t t178 = (t176 < t177);
                if (!t178) {
                    goto gm_lb19;
                }
                {
                    void * t179 = GM_LD(void *, ((uint8_t *)&gm_l27));
                    uint64_t t180 = GM_LD(uint64_t, (((uint8_t *)&gm_l27) + 8));
                    uint64_t t181 = gm_l28;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l29), (((uint8_t *)t179) + (uintptr_t)t181 * 40), 40);
                            }
                            {
                                memmove(((uint8_t *)&F.l30), ((uint8_t *)&F.l29), 40);
                            }
                            void * t182 = gm_l0;
                            void * t183 = (void *)((uint8_t *)&F.l26);
                            gm_t88rcucg1b7uk t184 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l29) + 16));
                            gm_t88rcucg1b7uk t185 = gm_f_mono_ty_atcu0jn((void *)NULL, cx, NULL, t182, t183, t184);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l30) + 16), t185);
                            {
                                void * t186 = (void *)((uint8_t *)&F.l22);
                                gm_tftb1odc739t5 t187 = gm_f_push_am7bo0v(t186, &(*(gm_tsoie1ppgdvcg *)((uint8_t *)&F.l30)));
                                gm_l50 = t187;
                            }
                            uint8_t t188 = (uint8_t)!((((uint8_t *)&gm_l50))[0] != 0);
                            if (!t188) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l50) + 1, gm_srcb5s85si25i3o, 281, 9);
                            goto gm_lb22;
                        }
gm_lb23:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 278, 5);
                    }
gm_lb22:;
                }
gm_lb21:;
                uint64_t t189 = gm_l28;
                uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                uint64_t t192 = r190;
                gm_l28 = t192;
            }
        }
gm_lb19:;
    }
    {
        gm_trqfvmjtt4sij t193;
        gm_f_list_go2qg0b((void *)&t193, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l31 = t193;
        void * t194 = (void *)((uint8_t *)&F.l31);
        gm_b16_8 t195 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 8));
        gm_b16_8 t196 = GM_LD(gm_b16_8, ((uint8_t *)&t195));
        gm_tftb1odc739t5 t197 = gm_f_extend_7hs67uj(t194, t196);
        gm_l51 = t197;
        uint8_t t198 = (uint8_t)!((((uint8_t *)&gm_l51))[0] != 0);
        if (!t198) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l51) + 1, gm_srcb5s85si25i3o, 284, 5);
        gm_b16_8 t199 = gm_l2;
        gm_l32 = t199;
        gm_l33 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t200 = gm_l33;
                uint64_t t201 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                uint8_t t202 = (t200 < t201);
                if (!t202) {
                    goto gm_lb24;
                }
                {
                    void * t203 = GM_LD(void *, ((uint8_t *)&gm_l32));
                    uint64_t t204 = GM_LD(uint64_t, (((uint8_t *)&gm_l32) + 8));
                    uint64_t t205 = gm_l33;
                    {
                        {
                            uint32_t t206 = GM_LD(uint32_t, (((uint8_t *)t203) + (uintptr_t)t205 * 4));
                            gm_l34 = t206;
                            uint64_t t207 = GM_LD(uint64_t, (((uint8_t *)&F.l22) + 8));
                            uint32_t t208 = (uint32_t)t207;
                            gm_l35 = t208;
                            {
                                void * t209 = (void *)((uint8_t *)&F.l22);
                                gm_b16_8 t210;
                                GM_ST(void *, (uint8_t *)&t210, (void *)(gm_sj3cmvbmi7cm6));
                                GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(8));
                                gm_t88rcucg1b7uk t211 = gm_l12;
                                gm_tl874t2atnlh3 t212 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 16));
                                gm_tsoie1ppgdvcg t213;
                                GM_ST(uint8_t, (uint8_t *)&t213 + 21, 0);
                                GM_ST(uint16_t, (uint8_t *)&t213 + 22, 0);
                                GM_ST(uint32_t, (uint8_t *)&t213 + 36, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t213 + 0), t210);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t213 + 16), t211);
                                GM_ST(uint8_t, ((uint8_t *)&t213 + 20), ((uint8_t)0));
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t213 + 24), t212);
                                gm_tftb1odc739t5 t214 = gm_f_push_am7bo0v(t209, &t213);
                                gm_l52 = t214;
                            }
                            uint8_t t215 = (uint8_t)!((((uint8_t *)&gm_l52))[0] != 0);
                            if (!t215) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l52) + 1, gm_srcb5s85si25i3o, 287, 9);
                            {
                                void * t216 = (void *)((uint8_t *)&F.l22);
                                gm_b16_8 t217;
                                GM_ST(void *, (uint8_t *)&t217, (void *)(gm_srkol0piugptj));
                                GM_ST(uintptr_t, (uint8_t *)&t217 + 8, (uintptr_t)(13));
                                void * t218 = gm_l0;
                                void * t219 = (void *)(((uint8_t *)t218) + 8);
                                gm_t88rcucg1b7uk t220 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t219);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tl874t2atnlh3 t221 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&F.l5) + 16));
                                gm_tsoie1ppgdvcg t222;
                                GM_ST(uint8_t, (uint8_t *)&t222 + 21, 0);
                                GM_ST(uint16_t, (uint8_t *)&t222 + 22, 0);
                                GM_ST(uint32_t, (uint8_t *)&t222 + 36, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t222 + 0), t217);
                                GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t222 + 16), t220);
                                GM_ST(uint8_t, ((uint8_t *)&t222 + 20), ((uint8_t)0));
                                GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t222 + 24), t221);
                                gm_tftb1odc739t5 t223 = gm_f_push_am7bo0v(t216, &t222);
                                gm_l53 = t223;
                            }
                            uint8_t t224 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                            if (!t224) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_srcb5s85si25i3o, 288, 9);
                            void * t225 = (void *)((uint8_t *)&F.l24);
                            uint32_t t226 = gm_l34;
                            uint32_t t227 = gm_l35;
                            uint32_t t228 = gm_l35;
                            uint32_t r229; bool o230 = __builtin_add_overflow(t228, ((uint32_t)1ULL), &r229);
                            if (o230) gm_trap("integer overflow in `+`", gm_srcb5s85si25i3o, 289, 48);
                            uint32_t t231 = r229;
                            gm_tl874t2atnlh3 t232;
                            GM_ST(uint32_t, ((uint8_t *)&t232 + 0), t226);
                            GM_ST(uint32_t, ((uint8_t *)&t232 + 4), t227);
                            GM_ST(uint32_t, ((uint8_t *)&t232 + 8), t231);
                            gm_tftb1odc739t5 t233 = gm_f_push_hbhlgi6(t225, t232);
                            gm_l54 = t233;
                            uint8_t t234 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                            if (!t234) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_srcb5s85si25i3o, 289, 9);
                            void * t235 = (void *)((uint8_t *)&F.l31);
                            uint32_t t236 = gm_l35;
                            gm_tftb1odc739t5 t237 = gm_f_push_im9d4ro(t235, t236);
                            gm_l55 = t237;
                            uint8_t t238 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                            if (!t238) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 1, gm_srcb5s85si25i3o, 290, 9);
                            void * t239 = (void *)((uint8_t *)&F.l31);
                            uint32_t t240 = gm_l35;
                            uint32_t r241; bool o242 = __builtin_add_overflow(t240, ((uint32_t)1ULL), &r241);
                            if (o242) gm_trap("integer overflow in `+`", gm_srcb5s85si25i3o, 291, 17);
                            uint32_t t243 = r241;
                            gm_tftb1odc739t5 t244 = gm_f_push_im9d4ro(t239, t243);
                            gm_l56 = t244;
                            uint8_t t245 = (uint8_t)!((((uint8_t *)&gm_l56))[0] != 0);
                            if (!t245) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l56) + 1, gm_srcb5s85si25i3o, 291, 9);
                            goto gm_lb27;
                        }
gm_lb28:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 285, 5);
                    }
gm_lb27:;
                }
gm_lb26:;
                uint64_t t246 = gm_l33;
                uint64_t r247; bool o248 = __builtin_add_overflow(t246, ((uint64_t)1ULL), &r247);
                uint64_t t249 = r247;
                gm_l33 = t249;
            }
        }
gm_lb24:;
    }
    {
        gm_b16_8 t250 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
        gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)&t250));
        GM_ST(gm_b16_8, (((uint8_t *)&F.l26) + 120), t251);
        gm_l36 = ((void *)0);
        {
            {
                uint8_t t252 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l6) + 40)) != 0);
                if (!t252) {
                    goto gm_lb30;
                }
                void * t253 = GM_LD(void *, (((uint8_t *)&F.l6) + 40));
                gm_l37 = t253;
                void * t254 = gm_l37;
                void * t255 = gm_f_clone_expr_af0u1qc((void *)NULL, cx, NULL, t254);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l38 = t255;
                void * t256 = gm_l0;
                void * t257 = (void *)((uint8_t *)&F.l26);
                void * t258 = gm_l38;
                gm_b0_1 t259 = gm_f_mono_expr_p8qb7fe((void *)NULL, cx, NULL, t256, t257, t258);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t260 = gm_l38;
                gm_l36 = t260;
                goto gm_lb29;
            }
gm_lb30:;
        }
gm_lb29:;
        void * t261 = gm_l0;
        gm_tl874t2atnlh3 t262 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
        gm_b0_1 t263 = gm_f_report_soa_errors_m1bqopj((void *)NULL, cx, NULL, t261, t262);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t264 = gm_l0;
        uint32_t t265 = gm_l23;
        GM_ST(uint32_t, (((uint8_t *)t264) + 1360), t265);
        void * t266 = gm_l0;
        uint32_t t267 = gm_l21;
        gm_t9qkh0rsooc8d t268 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l6));
        gm_b16_8 t269 = GM_LD(gm_b16_8, ((uint8_t *)&F.l31));
        gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&F.l22));
        void * t271 = gm_l36;
        gm_t9805qtonbsag t272 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l6) + 48));
        uint8_t t273 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 56));
        gm_tdgfduqjheqdm t274;
        GM_ST(uint8_t, (uint8_t *)&t274 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t274 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t274 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t274 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t274 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t274 + 60, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t274 + 0), t268);
        GM_ST(gm_b16_8, ((uint8_t *)&t274 + 8), t269);
        GM_ST(gm_b16_8, ((uint8_t *)&t274 + 24), t270);
        GM_ST(void *, ((uint8_t *)&t274 + 40), t271);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t274 + 48), t272);
        GM_ST(uint8_t, ((uint8_t *)&t274 + 56), t273);
        gm_b0_1 t275 = gm_f_set_inst_def_tcpabg0((void *)NULL, cx, NULL, t266, t267, &t274);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t276 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 24));
        gm_l39 = t276;
        gm_l40 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t277 = gm_l40;
                uint64_t t278 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                uint8_t t279 = (t277 < t278);
                if (!t279) {
                    goto gm_lb31;
                }
                {
                    void * t280 = GM_LD(void *, ((uint8_t *)&gm_l39));
                    uint64_t t281 = GM_LD(uint64_t, (((uint8_t *)&gm_l39) + 8));
                    uint64_t t282 = gm_l40;
                    {
                        {
                            memmove(((uint8_t *)&F.l41), (((uint8_t *)t280) + (uintptr_t)t282 * 40), 40);
                            void * t283 = gm_l0;
                            void * t284 = gm_l0;
                            gm_t88rcucg1b7uk t285 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l41) + 16));
                            gm_b16_8 t286 = GM_LD(gm_b16_8, ((uint8_t *)&F.l7));
                            gm_b16_8 t287 = GM_LD(gm_b16_8, ((uint8_t *)&t286));
                            gm_t88rcucg1b7uk t288 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t284, t285, t287);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tl874t2atnlh3 t289 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
                            uint8_t t290 = gm_f_type_size_ok_tr1llg4((void *)NULL, cx, NULL, t283, t288, t289);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            uint8_t t291 = (uint8_t)!t290;
                            if (t291) {
                                goto gm_lb31;
                            }
                            goto gm_lb34;
                        }
gm_lb35:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 303, 5);
                    }
gm_lb34:;
                }
gm_lb33:;
                uint64_t t292 = gm_l40;
                uint64_t r293; bool o294 = __builtin_add_overflow(t292, ((uint64_t)1ULL), &r293);
                uint64_t t295 = r293;
                gm_l40 = t295;
            }
        }
gm_lb31:;
    }
    {
        uint32_t t296 = gm_l21;
        gm_b16_8 t297 = GM_LD(gm_b16_8, ((uint8_t *)&F.l25));
        gm_b16_8 t298 = GM_LD(gm_b16_8, ((uint8_t *)&t297));
        GM_ST(uint32_t, (uint8_t *)&gm_l42 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&gm_l42 + 0), t296);
        GM_ST(gm_b16_8, ((uint8_t *)&gm_l42 + 8), t298);
        void * t299 = gm_l0;
        void * t300 = (void *)(((uint8_t *)t299) + 1424);
        uint32_t t301 = gm_l4;
        gm_b16_8 t302 = gm_l3;
        gm_tiilmfd16v3fc t303;
        GM_ST(uint32_t, (uint8_t *)&t303 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t303 + 0), t301);
        GM_ST(gm_b16_8, ((uint8_t *)&t303 + 8), t302);
        gm_tftb1odc739t5 t304 = gm_f_put_6him49q(t300, &t303, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l42)));
        gm_l57 = t304;
        uint8_t t305 = (uint8_t)!((((uint8_t *)&gm_l57))[0] != 0);
        if (!t305) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l57) + 1, gm_srcb5s85si25i3o, 307, 5);
        memmove(gm_out, ((uint8_t *)&gm_l42), 24);
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
extern __attribute__((aligned(4))) uint8_t gm_f_mentions_value_2a40t7e(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_only_templates_8l3e5t6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, uint32_t a3) {
    struct gm_fr_only_templates_8l3e5t6 F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    void * gm_l2; /* body */
    uint32_t gm_l3; /* g */
    gm_tmcj65fi8d3nd gm_l4; /* sub */
    GM_STACK_CHECK(&F, 303u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    uint8_t gm_l6;
    {
        uint32_t t4 = gm_l3;
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_f_template_marker_heil9i9((void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tplpbqf3duvn3 t7;
        GM_ST(uint32_t, ((uint8_t *)&t7 + 0), t4);
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t7 + 4), t6);
        gm_tmcj65fi8d3nd t8;
        GM_ST(gm_tplpbqf3duvn3, ((uint8_t *)&t8 + 0), t7);
        gm_l4 = t8;
        {
            void * t9 = gm_l0;
            uint32_t t10 = gm_l1;
            uint32_t t11 = gm_l3;
            void * t12 = (void *)((uint8_t *)&gm_l4);
            uint64_t t13 = ((uintptr_t)((uint64_t)1ULL)) - (uintptr_t)((uint64_t)0ULL);
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(((uint8_t *)t12 + (uintptr_t)((uint64_t)0ULL) * 8)));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(t13));
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            gm_trqfvmjtt4sij t16;
            gm_f_list_go2qg0b((void *)&t16, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tj607dkcelqbd t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 41, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 42, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 44, 0);
            GM_ST(void *, ((uint8_t *)&t17 + 0), t9);
            GM_ST(uint32_t, ((uint8_t *)&t17 + 8), t10);
            GM_ST(uint32_t, ((uint8_t *)&t17 + 12), t11);
            GM_ST(gm_b16_8, ((uint8_t *)&t17 + 16), t15);
            GM_ST(uint64_t, ((uint8_t *)&t17 + 32), ((uint64_t)0ULL));
            GM_ST(uint8_t, ((uint8_t *)&t17 + 40), ((uint8_t)0));
            GM_ST(gm_trqfvmjtt4sij, ((uint8_t *)&t17 + 48), t16);
            F.l5 = t17;
        }
        void * t18 = (void *)((uint8_t *)&F.l5);
        void * t19 = gm_l2;
        gm_b0_1 t20 = gm_f_scan_uses_iuphl4h((void *)NULL, cx, NULL, t18, t19, ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_ld3_1;
        }
        uint64_t t21 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 32));
        uint8_t t22 = (t21 > ((uint64_t)0ULL));
        if (t22) {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 40));
            uint8_t t24 = (uint8_t)!t23;
            gm_l6 = t24;
        } else {
            gm_l6 = ((uint8_t)0);
        }
        uint8_t t25 = gm_l6;
        F.ret = t25;
        gm_unw.kind = GM_RETURN; gm_unw.frame = (&F.h); gm_unw.label = 0; gm_unw.is_err = 0;
        goto gm_ld3_1;
    }
    {
        void * t26 = (void *)(((uint8_t *)&F.l5) + 48);
        gm_f_deinit_padgogk(t26);
    }
    goto gm_ls27;
gm_ld3_1:;
    {
        gm_unwind gm_saved = gm_unw; gm_unw.kind = GM_NONE;
        void * t28 = (void *)(((uint8_t *)&F.l5) + 48);
        gm_f_deinit_padgogk(t28);
        gm_unw = gm_saved;
    }
    goto gm_lad;
gm_ls27:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_scan_uses_iuphl4h(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2) {
    struct gm_fr_scan_uses_iuphl4h F;
    void * gm_l1; /* e */
    void * gm_l3; /* s */
    void * gm_l4; /* w */
    uint32_t gm_l5; /* p */
    void * gm_l6; /* x */
    void * gm_l7; /* a */
    uint32_t gm_l8; /* p */
    gm_b16_8 gm_l9; /* args */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    uint32_t gm_l13; /* c */
    uint32_t gm_l14; /* c */
    uint32_t gm_l15; /* c */
    gm_tiilmfd16v3fc gm_l16; /* inst */
    gm_b16_8 gm_l17; /* <tmp> */
    uint64_t gm_l18; /* <tmp> */
    gm_t88rcucg1b7uk gm_l19; /* a */
    void * gm_l21; /* nb */
    GM_STACK_CHECK(&F, 959u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    F.l2 = a2;
    uint8_t gm_l22;
    gm_tftb1odc739t5 gm_l23;
    {
        void * t41 = F.l0;
        uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)t41) + 40));
        if (t42) {
            F.ret = GM_ZERO(gm_b0_1);
            goto gm_fret;
        }
        void * t43 = F.l0;
        void * t44 = GM_LD(void *, ((uint8_t *)t43));
        gm_l3 = t44;
        void * t45 = gm_l1;
        {
            {
                {
                    uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                    uint8_t t47 = (uint8_t)(t46 == ((uint8_t)56ULL));
                    if (!t47) {
                        goto gm_lb2;
                    }
                    void * t48 = GM_LD(void *, ((((uint8_t *)t45) + 8) + 8));
                    gm_l4 = t48;
                    {
                        {
                            void * t49 = gm_l4;
                            void * t50 = GM_LD(void *, (((uint8_t *)t49) + 40));
                            uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t50) + 8) + 0));
                            uint8_t t52 = (uint8_t)(t51 == ((uint8_t)9ULL));
                            if (!t52) {
                                goto gm_lb4;
                            }
                            uint32_t t53 = GM_LD(uint32_t, ((((uint8_t *)t50) + 8) + 8));
                            gm_l5 = t53;
                            uint32_t t54 = gm_l5;
                            void * t55 = F.l0;
                            uint32_t t56 = GM_LD(uint32_t, (((uint8_t *)t55) + 12));
                            uint8_t t57 = ((t54 == t56));
                            if (!t57) {
                                goto gm_lb4;
                            }
                            uint8_t t58 = F.l2;
                            uint8_t t59 = (uint8_t)!t58;
                            if (t59) {
                                void * t60 = F.l0;
                                GM_ST(uint8_t, (((uint8_t *)t60) + 40), ((uint8_t)1));
                                F.ret = GM_ZERO(gm_b0_1);
                                goto gm_fret;
                            }
                            void * t61 = F.l0;
                            void * t62 = F.l0;
                            uint64_t t63 = GM_LD(uint64_t, (((uint8_t *)t62) + 32));
                            uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                            if (o65) gm_trap("integer overflow in `+`", gm_srcb5s85si25i3o, 71, 13);
                            uint64_t t66 = r64;
                            GM_ST(uint64_t, (((uint8_t *)t61) + 32), t66);
                            {
                                {
                                    void * t67 = gm_l4;
                                    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)t67) + 0));
                                    uint8_t t69 = (uint8_t)(t68 == ((uint8_t)1ULL));
                                    if (!t69) {
                                        goto gm_lb6;
                                    }
                                    void * t70 = GM_LD(void *, (((uint8_t *)t67) + 8));
                                    gm_l6 = t70;
                                    void * t71 = F.l0;
                                    void * t72 = gm_l6;
                                    uint8_t t73 = F.l2;
                                    gm_b0_1 t74 = gm_f_scan_uses_iuphl4h((void *)NULL, cx, NULL, t71, t72, t73);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb5;
                                }
gm_lb6:;
                            }
gm_lb5:;
                            {
                                {
                                    void * t75 = gm_l4;
                                    uint8_t t76 = (uint8_t)(GM_LD(void *, (((uint8_t *)t75) + 48)) != 0);
                                    if (!t76) {
                                        goto gm_lb8;
                                    }
                                    void * t77 = GM_LD(void *, (((uint8_t *)t75) + 48));
                                    gm_l7 = t77;
                                    void * t78 = F.l0;
                                    void * t79 = gm_l7;
                                    uint8_t t80 = F.l2;
                                    gm_b0_1 t81 = gm_f_scan_uses_iuphl4h((void *)NULL, cx, NULL, t78, t79, t80);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb7;
                                }
gm_lb8:;
                            }
gm_lb7:;
                            F.ret = GM_ZERO(gm_b0_1);
                            goto gm_fret;
                        }
gm_lb4:;
                    }
gm_lb3:;
                    goto gm_lb1;
                }
gm_lb2:;
                {
                    uint8_t t82 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                    uint8_t t83 = (uint8_t)(t82 == ((uint8_t)9ULL));
                    if (!t83) {
                        goto gm_lb9;
                    }
                    uint32_t t84 = GM_LD(uint32_t, ((((uint8_t *)t45) + 8) + 8));
                    gm_l8 = t84;
                    {
                        {
                            uint32_t t85 = gm_l8;
                            void * t86 = F.l0;
                            uint32_t t87 = GM_LD(uint32_t, (((uint8_t *)t86) + 12));
                            uint8_t t88 = ((t85 == t87));
                            if (!t88) {
                                goto gm_lb11;
                            }
                            uint8_t t89 = F.l2;
                            uint8_t t90 = (uint8_t)!t89;
                            if (!t90) {
                                goto gm_lb11;
                            }
                            void * t91 = F.l0;
                            GM_ST(uint8_t, (((uint8_t *)t91) + 40), ((uint8_t)1));
                            goto gm_lb10;
                        }
gm_lb11:;
                    }
gm_lb10:;
                    goto gm_lb1;
                }
gm_lb9:;
            }
            {
                {
                    uint8_t t92 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                    uint8_t t93 = (uint8_t)(t92 == ((uint8_t)33ULL));
                    if (!t93) {
                        goto gm_lb12;
                    }
                    uint8_t t94 = GM_LD(uint8_t, (((((uint8_t *)t45) + 8) + 8) + 0));
                    uint8_t t95 = (uint8_t)(t94 == ((uint8_t)81ULL));
                    if (!t95) {
                        goto gm_lb12;
                    }
                    gm_b16_8 t96 = GM_LD(gm_b16_8, (((((uint8_t *)t45) + 8) + 8) + 16));
                    gm_l9 = t96;
                    gm_l11 = ((uint64_t)0ULL);
                    uint64_t t97 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                    gm_l10 = t97;
                    {
                        for (;;) {
                            uint64_t t99 = gm_l11;
                            uint64_t t100 = gm_l10;
                            uint8_t t101 = (t99 < t100);
                            if (!t101) {
                                goto gm_lb13;
                            }
                            {
                                {
                                    {
                                        uint64_t t102 = gm_l11;
                                        gm_l12 = t102;
                                        void * t103 = GM_LD(void *, ((uint8_t *)&gm_l9));
                                        uint64_t t104 = GM_LD(uint64_t, (((uint8_t *)&gm_l9) + 8));
                                        uint64_t t105 = gm_l12;
                                        void * t106 = (void *)(((uint8_t *)t103) + (uintptr_t)t105 * 88);
                                        void * t107 = F.l0;
                                        uint32_t t108 = GM_LD(uint32_t, (((uint8_t *)t107) + 12));
                                        uint8_t t109 = gm_f_mentions_value_2a40t7e(t106, t108);
                                        if (t109) {
                                            void * t110 = F.l0;
                                            GM_ST(uint8_t, (((uint8_t *)t110) + 40), ((uint8_t)1));
                                        }
                                        goto gm_lb16;
                                    }
gm_lb17:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 77, 45);
                                }
gm_lb16:;
                            }
gm_lb15:;
                            uint64_t t111 = gm_l11;
                            uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                            uint64_t t114 = r112;
                            gm_l11 = t114;
                        }
                    }
gm_lb13:;
                    goto gm_lb1;
                }
gm_lb12:;
                {
                    {
                        {
                            uint8_t t115 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                            uint8_t t116 = (uint8_t)(t115 == ((uint8_t)34ULL));
                            if (!t116) {
                                goto gm_lb20;
                            }
                            uint8_t t117 = GM_LD(uint8_t, (((((uint8_t *)t45) + 8) + 8) + 0));
                            uint8_t t118 = (uint8_t)(t117 == ((uint8_t)0ULL));
                            if (!t118) {
                                goto gm_lb20;
                            }
                            uint32_t t119 = GM_LD(uint32_t, (((((uint8_t *)t45) + 8) + 8) + 8));
                            gm_l13 = t119;
                            goto gm_lb19;
                        }
gm_lb20:;
                        {
                            uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                            uint8_t t121 = (uint8_t)(t120 == ((uint8_t)26ULL));
                            if (!t121) {
                                goto gm_lb21;
                            }
                            uint32_t t122 = GM_LD(uint32_t, ((((uint8_t *)t45) + 8) + 8));
                            gm_l13 = t122;
                            goto gm_lb19;
                        }
gm_lb21:;
                        {
                            uint8_t t123 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
                            uint8_t t124 = (uint8_t)(t123 == ((uint8_t)27ULL));
                            if (!t124) {
                                goto gm_lb22;
                            }
                            uint32_t t125 = GM_LD(uint32_t, ((((uint8_t *)t45) + 8) + 8));
                            gm_l13 = t125;
                            goto gm_lb19;
                        }
gm_lb22:;
                        goto gm_lb18;
                    }
gm_lb19:;
                    uint32_t t126 = gm_l13;
                    void * t127 = F.l0;
                    uint32_t t128 = GM_LD(uint32_t, (((uint8_t *)t127) + 8));
                    uint8_t t129 = ((t126 == t128));
                    if (t129) {
                        void * t130 = F.l0;
                        GM_ST(uint8_t, (((uint8_t *)t130) + 40), ((uint8_t)1));
                    }
                    {
                        {
                            void * t131 = gm_l3;
                            void * t132 = GM_LD(void *, (((uint8_t *)t131) + 344));
                            uint64_t t133 = GM_LD(uint64_t, ((((uint8_t *)t131) + 344) + 8));
                            uint32_t t134 = gm_l13;
                            uint64_t t135 = (uint64_t)t134;
                            if ((uintptr_t)t135 >= (uintptr_t)(t133)) gm_trap_index((uintptr_t)t135, t133, gm_srcb5s85si25i3o, 83, 24);
                            uint8_t t136 = (uint8_t)((((((uint8_t *)t132) + (uintptr_t)t135 * 136) + 96))[24] != 0);
                            if (!t136) {
                                goto gm_lb24;
                            }
                            memmove(((uint8_t *)&gm_l16), ((((uint8_t *)t132) + (uintptr_t)t135 * 136) + 96), 24);
                            {
                                {
                                    uint8_t t137 = F.l2;
                                    if (!t137) {
                                        goto gm_lb26;
                                    }
                                    void * t138 = gm_l1;
                                    {
                                        {
                                            uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t138) + 8) + 0));
                                            uint8_t t140 = (uint8_t)(t139 == ((uint8_t)34ULL));
                                            if (!t140) {
                                                goto gm_lb28;
                                            }
                                            gm_l22 = ((uint8_t)1);
                                            goto gm_lb27;
                                        }
gm_lb28:;
                                        {
                                            gm_l22 = ((uint8_t)0);
                                            goto gm_lb27;
                                        }
gm_lb29:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 84, 28);
                                    }
gm_lb27:;
                                    uint8_t t141 = gm_l22;
                                    if (!t141) {
                                        goto gm_lb26;
                                    }
                                    void * t142 = gm_l3;
                                    void * t143 = F.l0;
                                    uint32_t t144 = GM_LD(uint32_t, (((uint8_t *)t143) + 8));
                                    void * t145 = F.l0;
                                    gm_b16_8 t146 = GM_LD(gm_b16_8, (((uint8_t *)t145) + 16));
                                    uint8_t t147 = gm_f_forwards_l1foetf((void *)NULL, cx, NULL, t142, t144, &(*(gm_tiilmfd16v3fc *)((uint8_t *)&gm_l16)), t146);
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    if (!t147) {
                                        goto gm_lb26;
                                    }
                                    void * t148 = F.l0;
                                    void * t149 = F.l0;
                                    uint64_t t150 = GM_LD(uint64_t, (((uint8_t *)t149) + 32));
                                    uint64_t r151; bool o152 = __builtin_add_overflow(t150, ((uint64_t)1ULL), &r151);
                                    if (o152) gm_trap("integer overflow in `+`", gm_srcb5s85si25i3o, 85, 21);
                                    uint64_t t153 = r151;
                                    GM_ST(uint64_t, (((uint8_t *)t148) + 32), t153);
                                    goto gm_lb25;
                                }
gm_lb26:;
                                gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 8));
                                gm_l17 = t154;
                                gm_l18 = ((uint64_t)0ULL);
                                {
                                    for (;;) {
                                        uint64_t t156 = gm_l18;
                                        uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                        uint8_t t158 = (t156 < t157);
                                        if (!t158) {
                                            goto gm_lb30;
                                        }
                                        {
                                            void * t159 = GM_LD(void *, ((uint8_t *)&gm_l17));
                                            uint64_t t160 = GM_LD(uint64_t, (((uint8_t *)&gm_l17) + 8));
                                            uint64_t t161 = gm_l18;
                                            {
                                                {
                                                    gm_t88rcucg1b7uk t162 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t159) + (uintptr_t)t161 * 4));
                                                    gm_l19 = t162;
                                                    void * t163 = gm_l3;
                                                    gm_t88rcucg1b7uk t164 = gm_l19;
                                                    void * t165 = F.l0;
                                                    gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)t165) + 16));
                                                    gm_t88rcucg1b7uk t167 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t163, t164, t166);
                                                    if (gm_unw.kind) {
                                                        goto gm_ll155;
                                                    }
                                                    gm_t88rcucg1b7uk t168 = gm_l19;
                                                    uint8_t t169 = (!gm_eq9m6mvtmgde(((uint8_t *)&t167), ((uint8_t *)&t168)));
                                                    if (t169) {
                                                        void * t170 = F.l0;
                                                        GM_ST(uint8_t, (((uint8_t *)t170) + 40), ((uint8_t)1));
                                                    }
                                                    goto gm_lb33;
                                                }
gm_lb34:;
                                                gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 87, 21);
                                            }
gm_lb33:;
                                        }
gm_lb32:;
                                        uint64_t t171 = gm_l18;
                                        uint64_t r172; bool o173 = __builtin_add_overflow(t171, ((uint64_t)1ULL), &r172);
                                        uint64_t t174 = r172;
                                        gm_l18 = t174;
                                    }
                                }
                                goto gm_ls175;
gm_ll155:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto gm_lb30; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto gm_lb32; }
                                }
                                goto gm_lad;
gm_ls175:;
gm_lb30:;
                            }
gm_lb25:;
                            goto gm_lb23;
                        }
gm_lb24:;
                    }
gm_lb23:;
                    void * t176 = gm_l3;
                    void * t177 = GM_LD(void *, (((uint8_t *)t176) + 384));
                    uint64_t t178 = GM_LD(uint64_t, ((((uint8_t *)t176) + 384) + 8));
                    uint32_t t179 = gm_l13;
                    uint64_t t180 = (uint64_t)t179;
                    if ((uintptr_t)t180 >= (uintptr_t)(t178)) gm_trap_index((uintptr_t)t180, t178, gm_srcb5s85si25i3o, 93, 18);
                    memmove(((uint8_t *)&F.l20), (((uint8_t *)t177) + (uintptr_t)t180 * 64), 64);
                    {
                        {
                            uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&F.l20) + 56));
                            if (!t181) {
                                goto gm_lb36;
                            }
                            uint32_t t182 = gm_l13;
                            void * t183 = F.l0;
                            gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)t183) + 48));
                            uint8_t t185 = 0;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t184) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t184))) + i * 4, ((uint8_t *)&t182))) { t185 = 1; break; }
                            uint8_t t186 = (uint8_t)(!t185);
                            if (!t186) {
                                goto gm_lb36;
                            }
                            void * t187 = F.l0;
                            void * t188 = (void *)(((uint8_t *)t187) + 48);
                            uint32_t t189 = gm_l13;
                            gm_tftb1odc739t5 t190 = gm_f_push_im9d4ro(t188, t189);
                            gm_l23 = t190;
                            uint8_t t191 = (uint8_t)!((((uint8_t *)&gm_l23))[0] != 0);
                            if (!t191) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l23) + 1, gm_srcb5s85si25i3o, 95, 17);
                            {
                                {
                                    uint8_t t192 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l20) + 40)) != 0);
                                    if (!t192) {
                                        goto gm_lb38;
                                    }
                                    void * t193 = GM_LD(void *, (((uint8_t *)&F.l20) + 40));
                                    gm_l21 = t193;
                                    void * t194 = F.l0;
                                    void * t195 = gm_l21;
                                    gm_b0_1 t196 = gm_f_scan_uses_iuphl4h((void *)NULL, cx, NULL, t194, t195, ((uint8_t)0));
                                    if (gm_unw.kind) {
                                        goto gm_lad;
                                    }
                                    goto gm_lb37;
                                }
gm_lb38:;
                            }
gm_lb37:;
                            goto gm_lb35;
                        }
gm_lb36:;
                    }
gm_lb35:;
                    goto gm_lb1;
                }
gm_lb18:;
            }
            {
                {
                    goto gm_lb1;
                }
gm_lb39:;
                gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 65, 5);
            }
        }
gm_lb1:;
    }
    {
        void * t197 = gm_l1;
        gm_closure t198 = ((gm_closure){(void *)gm_f_fn_9gjij2f, (void *)&F.h, cx});
        gm_f_for_each_child_fm49s73(t197, t198);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
__attribute__((aligned(4))) uint8_t gm_f_share_instance_1q66m8k(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2) {
    struct gm_fr_share_instance_1q66m8k F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* tps */
    uint32_t gm_l3; /* f */
    gm_b16_8 gm_l4; /* generics */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint32_t gm_l10; /* g */
    gm_b16_8 gm_l11; /* text */
    gm_tiilmfd16v3fc gm_l12; /* shared */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* i */
    uint32_t gm_l17; /* g */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_tiilmfd16v3fc gm_l21; /* t */
    gm_tj64ui6kruni6 gm_l23; /* err */
    gm_b16_8 gm_l25; /* <tmp> */
    uint64_t gm_l26; /* <tmp> */
    gm_tuo61a5rdrbtf gm_l27; /* site */
    uint64_t gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint64_t gm_l31; /* k */
    void * gm_l32; /* table */
    uint64_t gm_l35; /* used */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint32_t gm_l38; /* p */
    gm_b16_8 gm_l40; /* <tmp> */
    uint64_t gm_l41; /* <tmp> */
    void * gm_l44; /* b */
    gm_tl874t2atnlh3 gm_l45; /* span */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    uint32_t gm_l49; /* p */
    void * gm_l50; /* body */
    GM_STACK_CHECK(&F, 3655u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_tn68el8d14vku gm_l51;
    gm_t53at80pfdus3 gm_l52;
    gm_tftb1odc739t5 gm_l53;
    gm_tftb1odc739t5 gm_l54;
    gm_tftb1odc739t5 gm_l55;
    gm_tqu3m2l3n8mnc gm_l56;
    gm_tn68el8d14vku gm_l57;
    gm_tftb1odc739t5 gm_l58;
    gm_t1jll27j7ae34 gm_l59;
    gm_tabb710d9mq2e gm_l60;
    uint32_t gm_l61;
    gm_tftb1odc739t5 gm_l62;
    gm_tq7s98ihonkju gm_l63;
    gm_tftb1odc739t5 gm_l64;
    gm_tftb1odc739t5 gm_l65;
    gm_tftb1odc739t5 gm_l66;
    gm_tftb1odc739t5 gm_l67;
    gm_tftb1odc739t5 gm_l68;
    {
        uint32_t t49 = GM_LD(uint32_t, ((uint8_t *)a1));
        gm_l3 = t49;
        void * t50 = gm_l0;
        void * t51 = GM_LD(void *, (((uint8_t *)t50) + 344));
        uint64_t t52 = GM_LD(uint64_t, ((((uint8_t *)t50) + 344) + 8));
        uint32_t t53 = gm_l3;
        uint64_t t54 = (uint64_t)t53;
        if ((uintptr_t)t54 >= (uintptr_t)(t52)) gm_trap_index((uintptr_t)t54, t52, gm_srcb5s85si25i3o, 181, 17);
        gm_b16_8 t55 = GM_LD(gm_b16_8, ((((uint8_t *)t51) + (uintptr_t)t54 * 136) + 80));
        gm_l4 = t55;
        gm_trqfvmjtt4sij t56;
        gm_f_list_nof7vfj((void *)&t56, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l5 = t56;
        gm_trqfvmjtt4sij t57;
        gm_f_list_allvvbh((void *)&t57, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l6 = t57;
        gm_b16_8 t58 = gm_l4;
        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
        gm_l7 = t59;
        gm_l8 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t60 = gm_l8;
                uint64_t t61 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                uint8_t t62 = (t60 < t61);
                if (!t62) {
                    goto gm_lb1;
                }
                {
                    uint64_t t63 = gm_l8;
                    void * t64 = GM_LD(void *, ((uint8_t *)&gm_l7));
                    uint64_t t65 = GM_LD(uint64_t, (((uint8_t *)&gm_l7) + 8));
                    uint64_t t66 = gm_l8;
                    uint32_t t67 = GM_LD(uint32_t, (((uint8_t *)t64) + (uintptr_t)t66 * 4));
                    gm_tn68el8d14vku t68;
                    GM_ST(uint32_t, (uint8_t *)&t68 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t68 + 0), t63);
                    GM_ST(uint32_t, ((uint8_t *)&t68 + 8), t67);
                    gm_l51 = t68;
                    {
                        {
                            uint64_t t69 = GM_LD(uint64_t, ((uint8_t *)&gm_l51));
                            gm_l9 = t69;
                            uint32_t t70 = GM_LD(uint32_t, (((uint8_t *)&gm_l51) + 8));
                            gm_l10 = t70;
                            uint32_t t71 = gm_l10;
                            gm_b16_8 t72 = gm_l2;
                            uint8_t t73 = 0;
                            for (uintptr_t i = 0; i < (uintptr_t)(GM_LD(uintptr_t, ((uint8_t *)&t72) + 8)); i++) if (gm_eq467d5cm1d5(((uint8_t *)GM_LD(void *, ((uint8_t *)&t72))) + i * 4, ((uint8_t *)&t71))) { t73 = 1; break; }
                            uint8_t t74 = (uint8_t)(t73);
                            if (t74) {
                                {
                                    {
                                        {
                                            void * t75 = gm_l0;
                                            void * t76 = (void *)(((uint8_t *)t75) + 8);
                                            void * t77 = GM_LD(void *, (((uint8_t *)a1) + 8));
                                            uint64_t t78 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 8));
                                            uint64_t t79 = gm_l9;
                                            if ((uintptr_t)t79 >= (uintptr_t)(t78)) gm_trap_index((uintptr_t)t79, t78, gm_srcb5s85si25i3o, 186, 46);
                                            gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t77) + (uintptr_t)t79 * 4));
                                            gm_f_kind_jcnt4hm((void *)&gm_l52, t76, GM_CONV(gm_csguankko4lmhd, t80));
                                        }
                                        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&gm_l52) + 0));
                                        uint8_t t82 = (uint8_t)(t81 == ((uint8_t)21ULL));
                                        if (!t82) {
                                            goto gm_lb7;
                                        }
                                        uint8_t t83 = GM_LD(uint8_t, ((((uint8_t *)&gm_l52) + 8) + 0));
                                        uint8_t t84 = (uint8_t)(t83 == ((uint8_t)3ULL));
                                        if (!t84) {
                                            goto gm_lb7;
                                        }
                                        gm_b16_8 t85 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l52) + 8) + 8));
                                        gm_l11 = t85;
                                        {
                                            void * t86 = (void *)((uint8_t *)&F.l6);
                                            uint32_t t87 = gm_l10;
                                            gm_b16_8 t88 = gm_l11;
                                            gm_tiilmfd16v3fc t89;
                                            GM_ST(uint32_t, (uint8_t *)&t89 + 4, 0);
                                            GM_ST(uint32_t, ((uint8_t *)&t89 + 0), t87);
                                            GM_ST(gm_b16_8, ((uint8_t *)&t89 + 8), t88);
                                            gm_tftb1odc739t5 t90 = gm_f_push_gtq94gd(t86, &t89);
                                            gm_l53 = t90;
                                        }
                                        uint8_t t91 = (uint8_t)!((((uint8_t *)&gm_l53))[0] != 0);
                                        if (!t91) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l53) + 1, gm_srcb5s85si25i3o, 187, 13);
                                        void * t92 = (void *)((uint8_t *)&F.l5);
                                        void * t93 = gm_l0;
                                        gm_t88rcucg1b7uk t94 = gm_f_template_marker_heil9i9((void *)NULL, cx, NULL, t93);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        gm_tftb1odc739t5 t95 = gm_f_push_k3l9nic(t92, GM_CONV(gm_csguankko4lmhd, t94));
                                        gm_l54 = t95;
                                        uint8_t t96 = (uint8_t)!((((uint8_t *)&gm_l54))[0] != 0);
                                        if (!t96) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l54) + 1, gm_srcb5s85si25i3o, 188, 13);
                                        goto gm_lb6;
                                    }
gm_lb7:;
                                    F.ret = ((uint8_t)0);
                                    goto gm_fret;
                                }
gm_lb6:;
                            } else {
                                void * t97 = (void *)((uint8_t *)&F.l5);
                                void * t98 = GM_LD(void *, (((uint8_t *)a1) + 8));
                                uint64_t t99 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 8));
                                uint64_t t100 = gm_l9;
                                if ((uintptr_t)t100 >= (uintptr_t)(t99)) gm_trap_index((uintptr_t)t100, t99, gm_srcb5s85si25i3o, 190, 25);
                                gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t98) + (uintptr_t)t100 * 4));
                                gm_tftb1odc739t5 t102 = gm_f_push_k3l9nic(t97, GM_CONV(gm_csguankko4lmhd, t101));
                                gm_l55 = t102;
                                uint8_t t103 = (uint8_t)!((((uint8_t *)&gm_l55))[0] != 0);
                                if (!t103) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l55) + 1, gm_srcb5s85si25i3o, 190, 13);
                            }
                            goto gm_lb4;
                        }
gm_lb5:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 184, 5);
                    }
gm_lb4:;
                }
gm_lb3:;
                uint64_t t104 = gm_l8;
                uint64_t r105; bool o106 = __builtin_add_overflow(t104, ((uint64_t)1ULL), &r105);
                uint64_t t107 = r105;
                gm_l8 = t107;
            }
        }
gm_lb1:;
    }
    {
        void * t108 = gm_l0;
        void * t109 = (void *)(((uint8_t *)t108) + 1424);
        uint32_t t110 = gm_l3;
        gm_b16_8 t111 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
        gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
        gm_tiilmfd16v3fc t113;
        GM_ST(uint32_t, (uint8_t *)&t113 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t113 + 0), t110);
        GM_ST(gm_b16_8, ((uint8_t *)&t113 + 8), t112);
        gm_f_get_s7blvas((void *)&gm_l56, t109, &t113);
        uint8_t t114 = (uint8_t)((((uint8_t *)&gm_l56))[24] != 0);
        if (t114) {
            memmove(((uint8_t *)&gm_l12), ((uint8_t *)&gm_l56), 24);
        } else {
            void * t115 = gm_l0;
            gm_b16_8 t116 = gm_l2;
            gm_b16_8 t117 = GM_LD(gm_b16_8, ((uint8_t *)&F.l5));
            gm_b16_8 t118 = GM_LD(gm_b16_8, ((uint8_t *)&t117));
            gm_t0ut88vjdgrt1 t119 = GM_LD(gm_t0ut88vjdgrt1, ((uint8_t *)a1));
            gm_f_make_shared_r51o03d((void *)&gm_l12, (void *)NULL, cx, NULL, t115, &t119, t116, t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        gm_trqfvmjtt4sij t120;
        gm_f_list_oalq03d((void *)&t120, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t120;
        gm_b16_8 t121 = gm_l4;
        gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)&t121));
        gm_l14 = t122;
        gm_l15 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t123 = gm_l15;
                uint64_t t124 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                uint8_t t125 = (t123 < t124);
                if (!t125) {
                    goto gm_lb8;
                }
                {
                    uint64_t t126 = gm_l15;
                    void * t127 = GM_LD(void *, ((uint8_t *)&gm_l14));
                    uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&gm_l14) + 8));
                    uint64_t t129 = gm_l15;
                    uint32_t t130 = GM_LD(uint32_t, (((uint8_t *)t127) + (uintptr_t)t129 * 4));
                    gm_tn68el8d14vku t131;
                    GM_ST(uint32_t, (uint8_t *)&t131 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t131 + 0), t126);
                    GM_ST(uint32_t, ((uint8_t *)&t131 + 8), t130);
                    gm_l57 = t131;
                    {
                        {
                            uint64_t t132 = GM_LD(uint64_t, ((uint8_t *)&gm_l57));
                            gm_l16 = t132;
                            uint32_t t133 = GM_LD(uint32_t, (((uint8_t *)&gm_l57) + 8));
                            gm_l17 = t133;
                            void * t134 = (void *)((uint8_t *)&F.l13);
                            uint32_t t135 = gm_l17;
                            void * t136 = GM_LD(void *, (((uint8_t *)a1) + 8));
                            uint64_t t137 = GM_LD(uint64_t, ((((uint8_t *)a1) + 8) + 8));
                            uint64_t t138 = gm_l16;
                            if ((uintptr_t)t138 >= (uintptr_t)(t137)) gm_trap_index((uintptr_t)t138, t137, gm_srcb5s85si25i3o, 195, 73);
                            gm_t88rcucg1b7uk t139 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t136) + (uintptr_t)t138 * 4));
                            gm_tplpbqf3duvn3 t140;
                            GM_ST(uint32_t, ((uint8_t *)&t140 + 0), t135);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t140 + 4), t139);
                            gm_tftb1odc739t5 t141 = gm_f_push_h69n5fi(t134, GM_CONV(gm_cs7e9ppsd384bj, t140));
                            gm_l58 = t141;
                            uint8_t t142 = (uint8_t)!((((uint8_t *)&gm_l58))[0] != 0);
                            if (!t142) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l58) + 1, gm_srcb5s85si25i3o, 195, 39);
                            goto gm_lb11;
                        }
gm_lb12:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 195, 5);
                    }
gm_lb11:;
                }
gm_lb10:;
                uint64_t t143 = gm_l15;
                uint64_t r144; bool o145 = __builtin_add_overflow(t143, ((uint64_t)1ULL), &r144);
                uint64_t t146 = r144;
                gm_l15 = t146;
            }
        }
gm_lb8:;
        void * t147 = gm_l0;
        gm_b16_8 t148 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
        gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
        gm_tl874t2atnlh3 t150 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
        gm_toceffb697p0u t151;
        GM_ST(uint8_t, (uint8_t *)&t151 + 13, 0);
        GM_ST(uint16_t, (uint8_t *)&t151 + 14, 0);
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t151), t150);
        ((uint8_t *)&t151)[12] = 1;
        gm_b0_1 t152 = gm_f_check_len_exprs_m8s6m0h((void *)NULL, cx, NULL, t147, t149, t151);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_trqfvmjtt4sij t153;
        gm_f_list_vp1djv1_2((void *)&t153, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l18 = t153;
        gm_b16_8 t154 = GM_LD(gm_b16_8, ((uint8_t *)&F.l6));
        gm_l19 = t154;
        gm_l20 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t155 = gm_l20;
                uint64_t t156 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                uint8_t t157 = (t155 < t156);
                if (!t157) {
                    goto gm_lb13;
                }
                {
                    void * t158 = GM_LD(void *, ((uint8_t *)&gm_l19));
                    uint64_t t159 = GM_LD(uint64_t, (((uint8_t *)&gm_l19) + 8));
                    uint64_t t160 = gm_l20;
                    {
                        {
                            {
                                memmove(((uint8_t *)&gm_l21), (((uint8_t *)t158) + (uintptr_t)t160 * 24), 24);
                            }
                            {
                                gm_b16_8 t161 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                                gm_f_parse_template_rlpl2k8((void *)&gm_l59, (void *)NULL, cx, NULL, t161);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                            }
                            {
                                {
                                    uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l59) + 0));
                                    uint8_t t163 = (uint8_t)(t162 == ((uint8_t)0ULL));
                                    if (!t163) {
                                        goto gm_lb19;
                                    }
                                    {
                                        memmove(((uint8_t *)&F.l22), (((uint8_t *)&gm_l59) + 8), 48);
                                    }
                                    {
                                        memmove(((uint8_t *)&F.l24), ((uint8_t *)&F.l22), 48);
                                    }
                                    goto gm_lb18;
                                }
gm_lb19:;
                                {
                                    uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&gm_l59) + 0));
                                    uint8_t t165 = (uint8_t)(t164 == ((uint8_t)1ULL));
                                    if (!t165) {
                                        goto gm_lb20;
                                    }
                                    {
                                        memmove(((uint8_t *)&gm_l23), (((uint8_t *)&gm_l59) + 8), 32);
                                    }
                                    {
                                        void * t166 = gm_l0;
                                        gm_tl874t2atnlh3 t167 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
                                        gm_b16_8 t168 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l23));
                                        gm_tnpifv20irnph t169;
                                        GM_ST(gm_b16_8, ((uint8_t *)&t169 + 0), t168);
                                        gm_f_format_jvf1ma2((void *)&gm_l60, (void *)NULL, cx, NULL, t169);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                        uint8_t t170 = (uint8_t)!((((uint8_t *)&gm_l60))[0] != 0);
                                        if (!t170) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l60) + 8, gm_srcb5s85si25i3o, 203, 35);
                                        gm_b16_8 t171 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l60) + 8));
                                        gm_b16_8 t172 = GM_LD(gm_b16_8, ((uint8_t *)&t171));
                                        gm_b0_1 t173 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t166, t167, t172);
                                        if (gm_unw.kind) {
                                            goto gm_lad;
                                        }
                                    }
                                    goto gm_lb15;
                                }
gm_lb20:;
                                gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 200, 19);
                            }
gm_lb18:;
                            gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 8));
                            gm_l25 = t174;
                            gm_l26 = ((uint64_t)0ULL);
                            {
                                for (;;) {
                                    uint64_t t175 = gm_l26;
                                    uint64_t t176 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                    uint8_t t177 = (t175 < t176);
                                    if (!t177) {
                                        goto gm_lb21;
                                    }
                                    {
                                        void * t178 = GM_LD(void *, ((uint8_t *)&gm_l25));
                                        uint64_t t179 = GM_LD(uint64_t, (((uint8_t *)&gm_l25) + 8));
                                        uint64_t t180 = gm_l26;
                                        {
                                            {
                                                gm_tuo61a5rdrbtf t181 = GM_LD(gm_tuo61a5rdrbtf, (((uint8_t *)t178) + (uintptr_t)t180 * 12));
                                                gm_l27 = t181;
                                                uint32_t t182 = GM_LD(uint32_t, ((uint8_t *)&gm_l27));
                                                uint32_t t183 = GM_LD(uint32_t, ((uint8_t *)&gm_l21));
                                                uint8_t t184 = ((t182 == t183));
                                                if (t184) {
                                                    void * t185 = gm_l0;
                                                    gm_tl874t2atnlh3 t186 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
                                                    gm_tkn42u7na3t81 t187 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&gm_l27) + 4));
                                                    gm_th2lqbo77pvsc t188;
                                                    gm_f_check_template_brtb8t0((void *)&t188, (void *)NULL, cx, NULL, t185, t186, &(*(gm_tfeab279c16rr *)((uint8_t *)&F.l24)), t187);
                                                    if (gm_unw.kind) {
                                                        goto gm_lad;
                                                    }
                                                }
                                                goto gm_lb24;
                                            }
gm_lb25:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 207, 9);
                                        }
gm_lb24:;
                                    }
gm_lb23:;
                                    uint64_t t189 = gm_l26;
                                    uint64_t r190; bool o191 = __builtin_add_overflow(t189, ((uint64_t)1ULL), &r190);
                                    uint64_t t192 = r190;
                                    gm_l26 = t192;
                                }
                            }
gm_lb21:;
                            {
                                gm_trqfvmjtt4sij t193;
                                gm_f_list_go2qg0b((void *)&t193, (void *)NULL, cx, NULL);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                F.l28 = t193;
                            }
                            gm_l30 = ((uint64_t)0ULL);
                            uint64_t t194 = GM_LD(uint64_t, ((((uint8_t *)&F.l24) + 16) + 8));
                            gm_l29 = t194;
                            {
                                for (;;) {
                                    uint64_t t195 = gm_l30;
                                    uint64_t t196 = gm_l29;
                                    uint8_t t197 = (t195 < t196);
                                    if (!t197) {
                                        goto gm_lb26;
                                    }
                                    {
                                        {
                                            {
                                                uint64_t t198 = gm_l30;
                                                gm_l31 = t198;
                                                void * t199 = (void *)((uint8_t *)&F.l28);
                                                uint64_t t200 = GM_LD(uint64_t, ((((uint8_t *)&F.l24) + 32) + 8));
                                                uint8_t t201 = (t200 > ((uint64_t)0ULL));
                                                if (t201) {
                                                    void * t202 = GM_LD(void *, (((uint8_t *)&F.l24) + 32));
                                                    uint64_t t203 = GM_LD(uint64_t, ((((uint8_t *)&F.l24) + 32) + 8));
                                                    uint64_t t204 = gm_l31;
                                                    if ((uintptr_t)t204 >= (uintptr_t)(t203)) gm_trap_index((uintptr_t)t204, t203, gm_srcb5s85si25i3o, 211, 79);
                                                    uint64_t t205 = GM_LD(uint64_t, (((uint8_t *)t202) + (uintptr_t)t204 * 8));
                                                    uint32_t t206 = (uint32_t)t205;
                                                    gm_l61 = t206;
                                                } else {
                                                    uint64_t t207 = gm_l31;
                                                    uint32_t t208 = (uint32_t)t207;
                                                    gm_l61 = t208;
                                                }
                                                uint32_t t209 = gm_l61;
                                                gm_tftb1odc739t5 t210 = gm_f_push_im9d4ro(t199, t209);
                                                gm_l62 = t210;
                                                uint8_t t211 = (uint8_t)!((((uint8_t *)&gm_l62))[0] != 0);
                                                if (!t211) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l62) + 1, gm_srcb5s85si25i3o, 211, 40);
                                                goto gm_lb29;
                                            }
gm_lb30:;
                                            gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 211, 9);
                                        }
gm_lb29:;
                                    }
gm_lb28:;
                                    uint64_t t212 = gm_l30;
                                    uint64_t r213; bool o214 = __builtin_add_overflow(t212, ((uint64_t)1ULL), &r213);
                                    uint64_t t215 = r213;
                                    gm_l30 = t215;
                                }
                            }
gm_lb26:;
                            {
                                gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&F.l24));
                                gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&F.l24) + 16));
                                gm_b16_8 t218 = GM_LD(gm_b16_8, ((uint8_t *)&F.l28));
                                gm_b16_8 t219 = GM_LD(gm_b16_8, ((uint8_t *)&t218));
                                gm_tfeab279c16rr t220;
                                GM_ST(gm_b16_8, ((uint8_t *)&t220 + 0), t216);
                                GM_ST(gm_b16_8, ((uint8_t *)&t220 + 16), t217);
                                GM_ST(gm_b16_8, ((uint8_t *)&t220 + 32), t219);
                                gm_tq7s98ihonkju t221 = gm_f_new_4n5f3sc((void *)NULL, cx, NULL, &t220, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kolm4drqk8i)));
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_l63 = t221;
                            }
                            uint8_t t222 = (uint8_t)!((((uint8_t *)&gm_l63))[0] != 0);
                            if (!t222) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l63) + 8, gm_srcb5s85si25i3o, 212, 18);
                            void * t223 = GM_LD(void *, (((uint8_t *)&gm_l63) + 8));
                            gm_l32 = t223;
                            {
                                void * t224 = (void *)((uint8_t *)&F.l18);
                                void * t225 = gm_l0;
                                void * t226 = (void *)(((uint8_t *)t225) + 8);
                                gm_t53at80pfdus3 t227;
                                memset(&t227, 0, sizeof t227);
                                GM_ST(uint8_t, ((uint8_t *)&t227 + 0), ((uint8_t)7ULL));
                                gm_t88rcucg1b7uk t228 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t226, &t227);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                void * t229 = gm_l32;
                                gm_tl874t2atnlh3 t230 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
                                gm_to0f5qn75cgg8 t231;
                                memset(&t231, 0, sizeof t231);
                                GM_ST(uint8_t, ((uint8_t *)&t231 + 0), ((uint8_t)57ULL));
                                GM_ST(void *, ((uint8_t *)&t231 + 8), t229);
                                void * t232 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t228, &t231, t230);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t233 = gm_f_push_d943vgp_2(t224, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t232)));
                                gm_l64 = t233;
                            }
                            uint8_t t234 = (uint8_t)!((((uint8_t *)&gm_l64))[0] != 0);
                            if (!t234) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l64) + 1, gm_srcb5s85si25i3o, 213, 9);
                            {
                                void * t235 = (void *)((uint8_t *)&F.l18);
                                void * t236 = gm_l0;
                                void * t237 = (void *)(((uint8_t *)t236) + 8);
                                gm_t88rcucg1b7uk t238 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t237);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 8));
                                gm_tl874t2atnlh3 t240 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
                                gm_to0f5qn75cgg8 t241;
                                memset(&t241, 0, sizeof t241);
                                GM_ST(uint8_t, ((uint8_t *)&t241 + 0), ((uint8_t)5ULL));
                                GM_ST(gm_b16_8, ((uint8_t *)&t241 + 8), t239);
                                void * t242 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t238, &t241, t240);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t243 = gm_f_push_d943vgp_2(t235, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t242)));
                                gm_l65 = t243;
                            }
                            uint8_t t244 = (uint8_t)!((((uint8_t *)&gm_l65))[0] != 0);
                            if (!t244) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l65) + 1, gm_srcb5s85si25i3o, 214, 9);
                            goto gm_lb16;
                        }
gm_lb17:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 199, 5);
                    }
gm_lb16:;
                }
gm_lb15:;
                uint64_t t245 = gm_l20;
                uint64_t r246; bool o247 = __builtin_add_overflow(t245, ((uint64_t)1ULL), &r246);
                uint64_t t248 = r246;
                gm_l20 = t248;
            }
        }
gm_lb13:;
    }
    {
        void * t249 = gm_l0;
        void * t250 = GM_LD(void *, (((uint8_t *)t249) + 384));
        uint64_t t251 = GM_LD(uint64_t, ((((uint8_t *)t249) + 384) + 8));
        uint32_t t252 = gm_l3;
        uint64_t t253 = (uint64_t)t252;
        if ((uintptr_t)t253 >= (uintptr_t)(t251)) gm_trap_index((uintptr_t)t253, t251, gm_srcb5s85si25i3o, 217, 12);
        memmove(((uint8_t *)&F.l33), (((uint8_t *)t250) + (uintptr_t)t253 * 64), 64);
        void * t254 = gm_l0;
        void * t255 = GM_LD(void *, (((uint8_t *)t254) + 344));
        uint64_t t256 = GM_LD(uint64_t, ((((uint8_t *)t254) + 344) + 8));
        uint32_t t257 = GM_LD(uint32_t, (((uint8_t *)a1) + 4));
        uint64_t t258 = (uint64_t)t257;
        if ((uintptr_t)t258 >= (uintptr_t)(t256)) gm_trap_index((uintptr_t)t258, t256, gm_srcb5s85si25i3o, 218, 12);
        memmove(((uint8_t *)&F.l34), (((uint8_t *)t255) + (uintptr_t)t258 * 136), 136);
        gm_l35 = ((uint64_t)0ULL);
        gm_b16_8 t259 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 8));
        gm_l36 = t259;
        gm_l37 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t260 = gm_l37;
                uint64_t t261 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                uint8_t t262 = (t260 < t261);
                if (!t262) {
                    goto gm_lb31;
                }
                {
                    void * t263 = GM_LD(void *, ((uint8_t *)&gm_l36));
                    uint64_t t264 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 8));
                    uint64_t t265 = gm_l37;
                    {
                        {
                            uint32_t t266 = GM_LD(uint32_t, (((uint8_t *)t263) + (uintptr_t)t265 * 4));
                            gm_l38 = t266;
                            uint64_t t267 = gm_l35;
                            uint32_t t268 = gm_l38;
                            uint64_t t269 = (uint64_t)t268;
                            uint64_t r270; bool o271 = __builtin_add_overflow(t269, ((uint64_t)1ULL), &r270);
                            if (o271) gm_trap("integer overflow in `+`", gm_srcb5s85si25i3o, 221, 43);
                            uint64_t t272 = r270;
                            uint64_t t273 = (t267 > t272 ? t267 : t272);
                            gm_l35 = t273;
                            goto gm_lb34;
                        }
gm_lb35:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 221, 5);
                    }
gm_lb34:;
                }
gm_lb33:;
                uint64_t t274 = gm_l37;
                uint64_t r275; bool o276 = __builtin_add_overflow(t274, ((uint64_t)1ULL), &r275);
                uint64_t t277 = r275;
                gm_l37 = t277;
            }
        }
gm_lb31:;
        gm_trqfvmjtt4sij t278;
        gm_f_list_p9s2t99((void *)&t278, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l39 = t278;
        void * t279 = GM_LD(void *, (((uint8_t *)&F.l33) + 24));
        uint64_t t280 = GM_LD(uint64_t, ((((uint8_t *)&F.l33) + 24) + 8));
        uint64_t t281 = gm_l35;
        uint64_t t282 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t283 = (uintptr_t)t281;
        if (t282 > t283 || t283 > (uintptr_t)t280) gm_trap_slice(t282, t283, t280, gm_srcb5s85si25i3o, 223, 14);
        uint64_t t284 = ((uintptr_t)t281) - (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t285;
        GM_ST(void *, (uint8_t *)&t285, (void *)(((uint8_t *)t279 + (uintptr_t)((uint64_t)0ULL) * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)(t284));
        gm_l40 = t285;
        gm_l41 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t286 = gm_l41;
                uint64_t t287 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                uint8_t t288 = (t286 < t287);
                if (!t288) {
                    goto gm_lb36;
                }
                {
                    void * t289 = GM_LD(void *, ((uint8_t *)&gm_l40));
                    uint64_t t290 = GM_LD(uint64_t, (((uint8_t *)&gm_l40) + 8));
                    uint64_t t291 = gm_l41;
                    {
                        {
                            {
                                memmove(((uint8_t *)&F.l42), (((uint8_t *)t289) + (uintptr_t)t291 * 40), 40);
                            }
                            {
                                memmove(((uint8_t *)&F.l43), ((uint8_t *)&F.l42), 40);
                            }
                            void * t292 = gm_l0;
                            gm_t88rcucg1b7uk t293 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l42) + 16));
                            gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)&F.l13));
                            gm_b16_8 t295 = GM_LD(gm_b16_8, ((uint8_t *)&t294));
                            gm_t88rcucg1b7uk t296 = gm_f_subst_a2s1mb3((void *)NULL, cx, NULL, t292, t293, t295);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            GM_ST(gm_t88rcucg1b7uk, (((uint8_t *)&F.l43) + 16), t296);
                            {
                                void * t297 = (void *)((uint8_t *)&F.l39);
                                gm_tftb1odc739t5 t298 = gm_f_push_am7bo0v(t297, &(*(gm_tsoie1ppgdvcg *)((uint8_t *)&F.l43)));
                                gm_l66 = t298;
                            }
                            uint8_t t299 = (uint8_t)!((((uint8_t *)&gm_l66))[0] != 0);
                            if (!t299) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l66) + 1, gm_srcb5s85si25i3o, 226, 9);
                            goto gm_lb39;
                        }
gm_lb40:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 223, 5);
                    }
gm_lb39:;
                }
gm_lb38:;
                uint64_t t300 = gm_l41;
                uint64_t r301; bool o302 = __builtin_add_overflow(t300, ((uint64_t)1ULL), &r301);
                uint64_t t303 = r301;
                gm_l41 = t303;
            }
        }
gm_lb36:;
    }
    {
        {
            {
                uint8_t t304 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l33) + 40)) != 0);
                if (!t304) {
                    goto gm_lb42;
                }
                void * t305 = GM_LD(void *, (((uint8_t *)&F.l33) + 40));
                gm_l44 = t305;
                void * t306 = gm_l44;
                gm_tl874t2atnlh3 t307 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t306) + 72));
                gm_l45 = t307;
                goto gm_lb41;
            }
gm_lb42:;
            gm_tl874t2atnlh3 t308 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 24));
            gm_l45 = t308;
        }
gm_lb41:;
        gm_trqfvmjtt4sij t309;
        gm_f_list_vp1djv1_2((void *)&t309, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l46 = t309;
        gm_b16_8 t310 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 8));
        gm_l47 = t310;
        gm_l48 = ((uint64_t)0ULL);
        {
            for (;;) {
                uint64_t t311 = gm_l48;
                uint64_t t312 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 8));
                uint8_t t313 = (t311 < t312);
                if (!t313) {
                    goto gm_lb43;
                }
                {
                    void * t314 = GM_LD(void *, ((uint8_t *)&gm_l47));
                    uint64_t t315 = GM_LD(uint64_t, (((uint8_t *)&gm_l47) + 8));
                    uint64_t t316 = gm_l48;
                    {
                        {
                            uint32_t t317 = GM_LD(uint32_t, (((uint8_t *)t314) + (uintptr_t)t316 * 4));
                            gm_l49 = t317;
                            {
                                void * t318 = (void *)((uint8_t *)&F.l46);
                                void * t319 = GM_LD(void *, ((uint8_t *)&F.l39));
                                uint64_t t320 = GM_LD(uint64_t, (((uint8_t *)&F.l39) + 8));
                                uint32_t t321 = gm_l49;
                                uint64_t t322 = (uint64_t)t321;
                                if ((uintptr_t)t322 >= (uintptr_t)(t320)) gm_trap_index((uintptr_t)t322, t320, gm_srcb5s85si25i3o, 230, 44);
                                gm_t88rcucg1b7uk t323 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t319) + (uintptr_t)t322 * 40) + 16));
                                uint32_t t324 = gm_l49;
                                gm_tl874t2atnlh3 t325 = gm_l45;
                                gm_to0f5qn75cgg8 t326;
                                memset(&t326, 0, sizeof t326);
                                GM_ST(uint8_t, ((uint8_t *)&t326 + 0), ((uint8_t)15ULL));
                                GM_ST(uint32_t, ((uint8_t *)&t326 + 8), t324);
                                void * t327 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t323, &t326, t325);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                gm_tftb1odc739t5 t328 = gm_f_push_d943vgp_2(t318, &(*(gm_tsf6i2agfnpva *)((uint8_t *)t327)));
                                gm_l67 = t328;
                            }
                            uint8_t t329 = (uint8_t)!((((uint8_t *)&gm_l67))[0] != 0);
                            if (!t329) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l67) + 1, gm_srcb5s85si25i3o, 230, 27);
                            goto gm_lb46;
                        }
gm_lb47:;
                        gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 230, 5);
                    }
gm_lb46:;
                }
gm_lb45:;
                uint64_t t330 = gm_l48;
                uint64_t r331; bool o332 = __builtin_add_overflow(t330, ((uint64_t)1ULL), &r331);
                uint64_t t333 = r331;
                gm_l48 = t333;
            }
        }
gm_lb43:;
        uint64_t t334 = GM_LD(uint64_t, (((uint8_t *)&F.l18) + 8));
        uint64_t t335 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 8));
        uint64_t r336; bool o337 = __builtin_mul_overflow(((uint64_t)2ULL), t335, &r336);
        if (o337) gm_trap("integer overflow in `*`", gm_srcb5s85si25i3o, 231, 32);
        uint64_t t338 = r336;
        uint8_t t339 = (!(t334 == t338));
        if (t339) {
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        void * t340 = (void *)((uint8_t *)&F.l46);
        gm_b16_8 t341 = GM_LD(gm_b16_8, ((uint8_t *)&F.l18));
        gm_b16_8 t342 = GM_LD(gm_b16_8, ((uint8_t *)&t341));
        gm_tftb1odc739t5 t343 = gm_f_extend_gig5o6g(t340, t342);
        gm_l68 = t343;
        uint8_t t344 = (uint8_t)!((((uint8_t *)&gm_l68))[0] != 0);
        if (!t344) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l68) + 1, gm_srcb5s85si25i3o, 232, 5);
    }
    {
        gm_t88rcucg1b7uk t345 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l34) + 56));
        uint32_t t346 = GM_LD(uint32_t, ((uint8_t *)&gm_l12));
        gm_b16_8 t347 = GM_LD(gm_b16_8, ((uint8_t *)&F.l46));
        gm_tsth9qu1dv0g3 t348;
        memset(&t348, 0, sizeof t348);
        GM_ST(uint8_t, ((uint8_t *)&t348 + 0), ((uint8_t)0ULL));
        GM_ST(uint32_t, ((uint8_t *)&t348 + 8), t346);
        gm_to76jli3bqddq t349;
        GM_ST(gm_tsth9qu1dv0g3, ((uint8_t *)&t349 + 0), t348);
        GM_ST(gm_b16_8, ((uint8_t *)&t349 + 40), t347);
        gm_tl874t2atnlh3 t350 = gm_l45;
        gm_to0f5qn75cgg8 t351;
        GM_ST(uint8_t, (uint8_t *)&t351 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t351 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t351 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t351 + 0), ((uint8_t)34ULL));
        GM_ST(gm_to76jli3bqddq, ((uint8_t *)&t351 + 8), t349);
        void * t352 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t345, &t351, t350);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l50 = t352;
        void * t353 = gm_l0;
        uint32_t t354 = GM_LD(uint32_t, (((uint8_t *)a1) + 4));
        gm_t9qkh0rsooc8d t355 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l33));
        gm_b16_8 t356 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 8));
        gm_b16_8 t357 = GM_LD(gm_b16_8, ((uint8_t *)&F.l39));
        void * t358 = gm_l50;
        gm_t9805qtonbsag t359 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&F.l33) + 48));
        uint8_t t360 = GM_LD(uint8_t, (((uint8_t *)&F.l33) + 56));
        gm_tdgfduqjheqdm t361;
        GM_ST(uint8_t, (uint8_t *)&t361 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t361 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t361 + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&t361 + 57, 0);
        GM_ST(uint16_t, (uint8_t *)&t361 + 58, 0);
        GM_ST(uint32_t, (uint8_t *)&t361 + 60, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t361 + 0), t355);
        GM_ST(gm_b16_8, ((uint8_t *)&t361 + 8), t356);
        GM_ST(gm_b16_8, ((uint8_t *)&t361 + 24), t357);
        GM_ST(void *, ((uint8_t *)&t361 + 40), t358);
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t361 + 48), t359);
        GM_ST(uint8_t, ((uint8_t *)&t361 + 56), t360);
        gm_b0_1 t362 = gm_f_set_inst_def_tcpabg0((void *)NULL, cx, NULL, t353, t354, &t361);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.ret = ((uint8_t)1);
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
__attribute__((aligned(4))) gm_b16_8 gm_f_template_params_6stj17b(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_template_params_6stj17b F;
    void * gm_l0; /* s */
    uint32_t gm_l1; /* f */
    gm_b16_8 gm_l2; /* r */
    void * gm_l5; /* b */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint32_t gm_l8; /* g */
    GM_STACK_CHECK(&F, 563u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_th2lqbo77pvsc gm_l9;
    gm_tftb1odc739t5 gm_l10;
    gm_tftb1odc739t5 gm_l11;
    gm_tftb1odc739t5 gm_l12;
    {
        {
            void * t11 = gm_l0;
            void * t12 = (void *)(((uint8_t *)t11) + 1368);
            uint32_t t13 = gm_l1;
            gm_f_get_hi5nrra((void *)&gm_l9, t12, t13);
            uint8_t t14 = (uint8_t)((((uint8_t *)&gm_l9))[16] != 0);
            if (!t14) {
                goto gm_lb2;
            }
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l9));
            gm_l2 = t15;
            gm_b16_8 t16 = gm_l2;
            F.ret = t16;
            goto gm_fret;
        }
gm_lb2:;
    }
gm_lb1:;
    void * t17 = gm_l0;
    void * t18 = (void *)(((uint8_t *)t17) + 1368);
    uint32_t t19 = gm_l1;
    gm_b16_8 t20 = gm_f_empty_slice_d914i5u();
    gm_b16_8 t21 = GM_LD(gm_b16_8, ((uint8_t *)&t20));
    gm_tftb1odc739t5 t22 = gm_f_put_i2clb58(t18, t19, t21);
    gm_l10 = t22;
    uint8_t t23 = (uint8_t)!((((uint8_t *)&gm_l10))[0] != 0);
    if (!t23) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l10) + 1, gm_srcb5s85si25i3o, 21, 5);
    {
        gm_trqfvmjtt4sij t24;
        gm_f_list_go2qg0b((void *)&t24, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l3 = t24;
    }
    {
        void * t25 = gm_l0;
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 384));
        uint64_t t27 = GM_LD(uint64_t, ((((uint8_t *)t25) + 384) + 8));
        uint32_t t28 = gm_l1;
        uint64_t t29 = (uint64_t)t28;
        if ((uintptr_t)t29 >= (uintptr_t)(t27)) gm_trap_index((uintptr_t)t29, t27, gm_srcb5s85si25i3o, 23, 12);
        memmove(((uint8_t *)&F.l4), (((uint8_t *)t26) + (uintptr_t)t29 * 64), 64);
    }
    {
        {
            gm_t9qkh0rsooc8d t30 = GM_LD(gm_t9qkh0rsooc8d, ((uint8_t *)&F.l4));
            uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t30) + 0));
            uint8_t t32 = (uint8_t)((t31 == ((uint8_t)0ULL)));
            if (!t32) {
                goto gm_lb4;
            }
            uint8_t t33 = (uint8_t)(GM_LD(void *, (((uint8_t *)&F.l4) + 40)) != 0);
            if (!t33) {
                goto gm_lb4;
            }
            void * t34 = GM_LD(void *, (((uint8_t *)&F.l4) + 40));
            gm_t9qkh0rsooc8d t35;
            GM_ST(uint8_t, ((uint8_t *)&t35 + 0), ((uint8_t)0ULL));
            gm_l5 = t34;
            void * t36 = gm_l0;
            void * t37 = GM_LD(void *, (((uint8_t *)t36) + 344));
            uint64_t t38 = GM_LD(uint64_t, ((((uint8_t *)t36) + 344) + 8));
            uint32_t t39 = gm_l1;
            uint64_t t40 = (uint64_t)t39;
            if ((uintptr_t)t40 >= (uintptr_t)(t38)) gm_trap_index((uintptr_t)t40, t38, gm_srcb5s85si25i3o, 25, 18);
            gm_b16_8 t41 = GM_LD(gm_b16_8, ((((uint8_t *)t37) + (uintptr_t)t40 * 136) + 80));
            gm_l6 = t41;
            gm_l7 = ((uint64_t)0ULL);
            {
                for (;;) {
                    uint64_t t42 = gm_l7;
                    uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                    uint8_t t44 = (t42 < t43);
                    if (!t44) {
                        goto gm_lb5;
                    }
                    {
                        void * t45 = GM_LD(void *, ((uint8_t *)&gm_l6));
                        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&gm_l6) + 8));
                        uint64_t t47 = gm_l7;
                        {
                            {
                                uint32_t t48 = GM_LD(uint32_t, (((uint8_t *)t45) + (uintptr_t)t47 * 4));
                                gm_l8 = t48;
                                void * t49 = gm_l0;
                                uint32_t t50 = gm_l1;
                                void * t51 = gm_l5;
                                uint32_t t52 = gm_l8;
                                uint8_t t53 = gm_f_only_templates_8l3e5t6((void *)NULL, cx, NULL, t49, t50, t51, t52);
                                if (gm_unw.kind) {
                                    goto gm_lad;
                                }
                                if (t53) {
                                    void * t54 = (void *)((uint8_t *)&F.l3);
                                    uint32_t t55 = gm_l8;
                                    gm_tftb1odc739t5 t56 = gm_f_push_im9d4ro(t54, t55);
                                    gm_l11 = t56;
                                    uint8_t t57 = (uint8_t)!((((uint8_t *)&gm_l11))[0] != 0);
                                    if (!t57) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l11) + 1, gm_srcb5s85si25i3o, 26, 45);
                                }
                                goto gm_lb8;
                            }
gm_lb9:;
                            gm_trap("no match arm matched the scrutinee", gm_srcb5s85si25i3o, 25, 9);
                        }
gm_lb8:;
                    }
gm_lb7:;
                    uint64_t t58 = gm_l7;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l7 = t61;
                }
            }
gm_lb5:;
            goto gm_lb3;
        }
gm_lb4:;
    }
gm_lb3:;
    void * t62 = gm_l0;
    void * t63 = (void *)(((uint8_t *)t62) + 1368);
    uint32_t t64 = gm_l1;
    gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
    gm_tftb1odc739t5 t67 = gm_f_put_i2clb58(t63, t64, t66);
    gm_l12 = t67;
    uint8_t t68 = (uint8_t)!((((uint8_t *)&gm_l12))[0] != 0);
    if (!t68) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&gm_l12) + 1, gm_srcb5s85si25i3o, 29, 5);
    gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&F.l3));
    gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
    F.ret = t70;
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

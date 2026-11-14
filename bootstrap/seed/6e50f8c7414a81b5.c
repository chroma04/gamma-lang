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

#if defined(GM_RT_PROGRAM) && defined(GM_RT_SEPARATE) && !defined(_WIN32)
/* The generated code of a program that is linked with the separately compiled runtime
 * needs only the declarations below, which use nothing from the operating system's headers;
 * parsing those would take the C compiler about as long as a small program. (Windows
 * builds include them all.) */
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
   record at the top of the frame (x86-64, Clang on AArch64), and below them where it
   keeps it at the bottom (GCC on AArch64 without a stack protector): there the frame is
   above it already. The comparison with the frame `p` tells which, and folds away. */
#define GM_STACK_CHECK(p, n) do { \
    uintptr_t gm_fa = (uintptr_t)__builtin_frame_address(0); \
    if (gm_fa < gm_stack_limit + (gm_fa > (uintptr_t)(p) ? (uintptr_t)(n) + sizeof(*(p)) : 0)) gm_stack_overflow(); \
} while (0)
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
GM_POW_FN(gm_pow_i128, __int128)
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
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; uint64_t f2; uint8_t f3; uint8_t pe[7]; } gm_t1j70din2upc4;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; uint64_t f2; void * f3; } gm_tg0sdqqpruvqq;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; uint32_t f3; } gm_thpdbp3q7rdrc;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { void * f0; uint64_t f1; } gm_ttf6i6v1da8cm;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
typedef struct { uint16_t f0; uint16_t f1; } gm_tuqvtbj0p3v9c;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
_Static_assert(sizeof(gm_t1j70din2upc4) == 32 && _Alignof(gm_t1j70din2upc4) == 8 && offsetof(gm_t1j70din2upc4, f0) == 0 && offsetof(gm_t1j70din2upc4, f1) == 8 && offsetof(gm_t1j70din2upc4, f2) == 16 && offsetof(gm_t1j70din2upc4, f3) == 24, "layout of `(base: ^Node, index: u32, off: u64, packed: bool)`");
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_t4n3ujve5elie;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; uint64_t f2; unsigned __int128 f3; uint64_t f4; uint64_t f5; uint64_t f6; void * f7; } gm_t4nomjfe3hlif;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_ttovhfee8mf28 f1; uint8_t p2[4]; void * f2; } gm_t9s8r45p2ijj5;
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_ta6jn77i9rmqf;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { gm_tmn7n0n9raou5 f0; uint8_t f1; uint8_t pe[3]; } gm_tbgiuvo2j5vcs;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { void * f0; uint32_t f1; uint32_t f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tdvcgvrf81sbd;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; uint64_t f1; } gm_te0mgnjdphs16;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; uint64_t f1; } gm_tfbnciphf3261;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tg0sdqqpruvqq) == 32 && _Alignof(gm_tg0sdqqpruvqq) == 8 && offsetof(gm_tg0sdqqpruvqq, f0) == 0 && offsetof(gm_tg0sdqqpruvqq, f1) == 8 && offsetof(gm_tg0sdqqpruvqq, f2) == 16 && offsetof(gm_tg0sdqqpruvqq, f3) == 24, "layout of `(local: u32, off: u64, size: u64, init: ?^Node)`");
typedef struct { gm_ttovhfee8mf28 f0; uint8_t f1; uint8_t pe[3]; } gm_tg5lql6itufk7;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
_Static_assert(sizeof(gm_thpdbp3q7rdrc) == 16 && _Alignof(gm_thpdbp3q7rdrc) == 4 && offsetof(gm_thpdbp3q7rdrc, f0) == 0 && offsetof(gm_thpdbp3q7rdrc, f1) == 4 && offsetof(gm_thpdbp3q7rdrc, f2) == 8 && offsetof(gm_thpdbp3q7rdrc, f3) == 12, "layout of `(u32, u32, u32, u32)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { void * f0; void * f1; gm_t9qkh0rsooc8d f2; uint8_t p3[7]; uint64_t f3; uint64_t f4; } gm_tp0fa5qv7rqnn;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint32_t f0; gm_t88rcucg1b7uk f1; } gm_tplpbqf3duvn3;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; gm_t9qkh0rsooc8d f4; uint8_t p5[6]; uint64_t f5; uint64_t f6; } gm_tt49maei14cl6;
_Static_assert(sizeof(gm_ttf6i6v1da8cm) == 16 && _Alignof(gm_ttf6i6v1da8cm) == 8 && offsetof(gm_ttf6i6v1da8cm, f0) == 0 && offsetof(gm_ttf6i6v1da8cm, f1) == 8, "layout of `(slot: ^Slot, base: u64)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tuqvtbj0p3v9c) == 4 && _Alignof(gm_tuqvtbj0p3v9c) == 2 && offsetof(gm_tuqvtbj0p3v9c, f0) == 0 && offsetof(gm_tuqvtbj0p3v9c, f1) == 2, "layout of `(u16, u16)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_ttovhfee8mf28 f1; gm_ttovhfee8mf28 f2; uint8_t p3[4]; void * f3; void * f4; } gm_t0pc2c7dafsjt;
typedef struct { gm_tvdg94bk7aa22 f0; gm_tvdg94bk7aa22 f1; gm_tvdg94bk7aa22 f2; } gm_t2dgk4tdinqkd;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tg0sdqqpruvqq v0; gm_tpe1hv3fekn05 v1; gm_ttggq7ng4kp3p v2; gm_t3mf58gv9kf3n v3; gm_tpe1hv3fekn05 v4; void * v5; gm_tgtub0v94d5u5 v6; } f1; } gm_t2k9dbsv5ff58;
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
_Static_assert(sizeof(gm_t4n3ujve5elie) == 32 && _Alignof(gm_t4n3ujve5elie) == 8 && offsetof(gm_t4n3ujve5elie, f0) == 0 && offsetof(gm_t4n3ujve5elie, f1) == 16 && offsetof(gm_t4n3ujve5elie, f2) == 24, "layout of `Blk`");
_Static_assert(sizeof(gm_t4nomjfe3hlif) == 64 && _Alignof(gm_t4nomjfe3hlif) == 16 && offsetof(gm_t4nomjfe3hlif, f0) == 0 && offsetof(gm_t4nomjfe3hlif, f1) == 4 && offsetof(gm_t4nomjfe3hlif, f2) == 8 && offsetof(gm_t4nomjfe3hlif, f3) == 16 && offsetof(gm_t4nomjfe3hlif, f4) == 32 && offsetof(gm_t4nomjfe3hlif, f5) == 40 && offsetof(gm_t4nomjfe3hlif, f6) == 48 && offsetof(gm_t4nomjfe3hlif, f7) == 56, "layout of `(variant: u32, tag_ty: Ty, tag_size: u64, tag: u128, tag_w: u64, off: u64, size: u64, payload: ?^Pat)`");
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9s8r45p2ijj5) == 24 && _Alignof(gm_t9s8r45p2ijj5) == 8 && offsetof(gm_t9s8r45p2ijj5, f0) == 0 && offsetof(gm_t9s8r45p2ijj5, f1) == 4 && offsetof(gm_t9s8r45p2ijj5, f2) == 16, "layout of `(op: UnOp, t: IntW, x: ^Node)`");
_Static_assert(sizeof(gm_ta6jn77i9rmqf) == 32 && _Alignof(gm_ta6jn77i9rmqf) == 8 && offsetof(gm_ta6jn77i9rmqf, f0) == 0 && offsetof(gm_ta6jn77i9rmqf, f1) == 8 && offsetof(gm_ta6jn77i9rmqf, f2) == 16 && offsetof(gm_ta6jn77i9rmqf, f3) == 24, "layout of `(how: WordCmp, l: ^Node, r: ^Node, negated: bool)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgiuvo2j5vcs) == 12 && _Alignof(gm_tbgiuvo2j5vcs) == 4 && offsetof(gm_tbgiuvo2j5vcs, f0) == 0 && offsetof(gm_tbgiuvo2j5vcs, f1) == 8, "layout of `?Coercion`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tdvcgvrf81sbd) == 24 && _Alignof(gm_tdvcgvrf81sbd) == 8 && offsetof(gm_tdvcgvrf81sbd, f0) == 0 && offsetof(gm_tdvcgvrf81sbd, f1) == 8 && offsetof(gm_tdvcgvrf81sbd, f2) == 12 && offsetof(gm_tdvcgvrf81sbd, f3) == 16, "layout of `(base: ^Node, index: u32, bit_off: u32, fty: Ty)`");
_Static_assert(sizeof(gm_te0mgnjdphs16) == 16 && _Alignof(gm_te0mgnjdphs16) == 8 && offsetof(gm_te0mgnjdphs16, f0) == 0 && offsetof(gm_te0mgnjdphs16, f1) == 8, "layout of `Chunk`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; } gm_tf4r0415i49d1;
_Static_assert(sizeof(gm_tfbnciphf3261) == 16 && _Alignof(gm_tfbnciphf3261) == 8 && offsetof(gm_tfbnciphf3261, f0) == 0 && offsetof(gm_tfbnciphf3261, f1) == 8, "layout of `(ty: Ty, off: u64)`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tg5lql6itufk7) == 12 && _Alignof(gm_tg5lql6itufk7) == 4 && offsetof(gm_tg5lql6itufk7, f0) == 0 && offsetof(gm_tg5lql6itufk7, f1) == 8, "layout of `?(signed: bool, bits: u32)`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
typedef struct { gm_te0mgnjdphs16 f0; uint8_t f1; uint8_t pe[7]; } gm_thdrvp9q0u7b5;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_tmn7n0n9raou5 f1; uint8_t p2[4]; void * f2; void * f3; } gm_tkm39klvnn4cr;
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tp0fa5qv7rqnn) == 40 && _Alignof(gm_tp0fa5qv7rqnn) == 8 && offsetof(gm_tp0fa5qv7rqnn, f0) == 0 && offsetof(gm_tp0fa5qv7rqnn, f1) == 8 && offsetof(gm_tp0fa5qv7rqnn, f2) == 16 && offsetof(gm_tp0fa5qv7rqnn, f3) == 24 && offsetof(gm_tp0fa5qv7rqnn, f4) == 32, "layout of `(base: ^Node, index: ^Node, seq: SeqKind, esz: u64, len: u64)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tplpbqf3duvn3) == 8 && _Alignof(gm_tplpbqf3duvn3) == 4 && offsetof(gm_tplpbqf3duvn3, f0) == 0 && offsetof(gm_tplpbqf3duvn3, f1) == 4, "layout of `WithSlot`");
typedef struct { gm_tvdg94bk7aa22 f0; gm_tvdg94bk7aa22 f1; gm_tvdg94bk7aa22 f2; gm_tvdg94bk7aa22 f3; gm_tvdg94bk7aa22 f4; } gm_tpq3gshtivcif;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tvdg94bk7aa22 f0; gm_tvdg94bk7aa22 f1; } gm_tq9t4giin3pm0;
typedef struct { gm_tvdg94bk7aa22 f0; gm_ttovhfee8mf28 f1; gm_ttovhfee8mf28 f2; uint8_t p3[4]; void * f3; } gm_tr8okf1iilonm;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_ttggq7ng4kp3p v0; gm_tpe1hv3fekn05 v1; gm_t3mf58gv9kf3n v2; gm_tpe1hv3fekn05 v3; void * v4; gm_tgtub0v94d5u5 v5; } f1; } gm_trcmmd12cefb1;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_tvdg94bk7aa22 f0; gm_ttovhfee8mf28 f1; uint8_t p2[4]; void * f2; } gm_ts6ln23edn25a;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; gm_ttovhfee8mf28 f1; uint8_t p2[4]; void * f2; void * f3; } gm_tscalq56heofe;
_Static_assert(sizeof(gm_tt49maei14cl6) == 48 && _Alignof(gm_tt49maei14cl6) == 8 && offsetof(gm_tt49maei14cl6, f0) == 0 && offsetof(gm_tt49maei14cl6, f1) == 8 && offsetof(gm_tt49maei14cl6, f2) == 16 && offsetof(gm_tt49maei14cl6, f3) == 24 && offsetof(gm_tt49maei14cl6, f4) == 25 && offsetof(gm_tt49maei14cl6, f5) == 32 && offsetof(gm_tt49maei14cl6, f6) == 40, "layout of `(base: ^Node, lo: ?^Node, hi: ?^Node, inclusive: bool, seq: SeqKind, esz: u64, len: u64)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t f1; uint8_t p2[5]; void * f2; void * f3; } gm_tuggr5sml9o52;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t0pc2c7dafsjt) == 40 && _Alignof(gm_t0pc2c7dafsjt) == 8 && offsetof(gm_t0pc2c7dafsjt, f0) == 0 && offsetof(gm_t0pc2c7dafsjt, f1) == 4 && offsetof(gm_t0pc2c7dafsjt, f2) == 12 && offsetof(gm_t0pc2c7dafsjt, f3) == 24 && offsetof(gm_t0pc2c7dafsjt, f4) == 32, "layout of `(op: BinOp, t: IntW, shift: IntW, l: ^Node, r: ^Node)`");
_Static_assert(sizeof(gm_t2dgk4tdinqkd) == 36 && _Alignof(gm_t2dgk4tdinqkd) == 4 && offsetof(gm_t2dgk4tdinqkd, f0) == 0 && offsetof(gm_t2dgk4tdinqkd, f1) == 12 && offsetof(gm_t2dgk4tdinqkd, f2) == 24, "layout of `(Builtin, Builtin, Builtin)`");
_Static_assert(sizeof(gm_t2k9dbsv5ff58) == 40 && _Alignof(gm_t2k9dbsv5ff58) == 8 && offsetof(gm_t2k9dbsv5ff58, f0) == 0 && offsetof(gm_t2k9dbsv5ff58, f1) == 8, "layout of `St`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tf4r0415i49d1) == 48 && _Alignof(gm_tf4r0415i49d1) == 8 && offsetof(gm_tf4r0415i49d1, f0) == 0 && offsetof(gm_tf4r0415i49d1, f1) == 24 && offsetof(gm_tf4r0415i49d1, f2) == 32, "layout of `(effect_: EffectRef, op: u32, args: []^Node)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
_Static_assert(sizeof(gm_thdrvp9q0u7b5) == 24 && _Alignof(gm_thdrvp9q0u7b5) == 8 && offsetof(gm_thdrvp9q0u7b5, f0) == 0 && offsetof(gm_thdrvp9q0u7b5, f1) == 16, "layout of `?Chunk`");
_Static_assert(sizeof(gm_tkm39klvnn4cr) == 32 && _Alignof(gm_tkm39klvnn4cr) == 8 && offsetof(gm_tkm39klvnn4cr, f0) == 0 && offsetof(gm_tkm39klvnn4cr, f1) == 4 && offsetof(gm_tkm39klvnn4cr, f2) == 16 && offsetof(gm_tkm39klvnn4cr, f3) == 24, "layout of `(op: BinOp, how: WordCmp, l: ^Node, r: ^Node)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_tpq3gshtivcif) == 60 && _Alignof(gm_tpq3gshtivcif) == 4 && offsetof(gm_tpq3gshtivcif, f0) == 0 && offsetof(gm_tpq3gshtivcif, f1) == 12 && offsetof(gm_tpq3gshtivcif, f2) == 24 && offsetof(gm_tpq3gshtivcif, f3) == 36 && offsetof(gm_tpq3gshtivcif, f4) == 48, "layout of `(Builtin, Builtin, Builtin, Builtin, Builtin)`");
_Static_assert(sizeof(gm_tq9t4giin3pm0) == 24 && _Alignof(gm_tq9t4giin3pm0) == 4 && offsetof(gm_tq9t4giin3pm0, f0) == 0 && offsetof(gm_tq9t4giin3pm0, f1) == 12, "layout of `(Builtin, Builtin)`");
_Static_assert(sizeof(gm_tr8okf1iilonm) == 40 && _Alignof(gm_tr8okf1iilonm) == 8 && offsetof(gm_tr8okf1iilonm, f0) == 0 && offsetof(gm_tr8okf1iilonm, f1) == 12 && offsetof(gm_tr8okf1iilonm, f2) == 20 && offsetof(gm_tr8okf1iilonm, f3) == 32, "layout of `(op: Builtin, from: IntW, to: IntW, x: ^Node)`");
_Static_assert(sizeof(gm_trcmmd12cefb1) == 32 && _Alignof(gm_trcmmd12cefb1) == 8 && offsetof(gm_trcmmd12cefb1, f0) == 0 && offsetof(gm_trcmmd12cefb1, f1) == 8, "layout of `Stmt`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts6ln23edn25a) == 32 && _Alignof(gm_ts6ln23edn25a) == 8 && offsetof(gm_ts6ln23edn25a, f0) == 0 && offsetof(gm_ts6ln23edn25a, f1) == 12 && offsetof(gm_ts6ln23edn25a, f2) == 24, "layout of `(op: Builtin, t: IntW, x: ^Node)`");
_Static_assert(sizeof(gm_tscalq56heofe) == 40 && _Alignof(gm_tscalq56heofe) == 8 && offsetof(gm_tscalq56heofe, f0) == 0 && offsetof(gm_tscalq56heofe, f1) == 12 && offsetof(gm_tscalq56heofe, f2) == 24 && offsetof(gm_tscalq56heofe, f3) == 32, "layout of `(op: Builtin, t: IntW, x: ^Node, k: ^Node)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tuggr5sml9o52) == 24 && _Alignof(gm_tuggr5sml9o52) == 8 && offsetof(gm_tuggr5sml9o52, f0) == 0 && offsetof(gm_tuggr5sml9o52, f1) == 2 && offsetof(gm_tuggr5sml9o52, f2) == 8 && offsetof(gm_tuggr5sml9o52, f3) == 16, "layout of `(op: BinOp, wide: bool, l: ^Node, r: ^Node)`");
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_t4nomjfe3hlif v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tlpa5tka3taqb;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_b16_8 v1; gm_b16_8 v2; gm_b16_8 v3; gm_tl874t2atnlh3 v4; gm_tb31k5l794t7p v5; gm_tcc1rq4n1avb2 v6; uint64_t v7; gm_ttf6i6v1da8cm v8; gm_t1j70din2upc4 v9; gm_tdvcgvrf81sbd v10; gm_tp0fa5qv7rqnn v11; gm_tt49maei14cl6 v12; void * v13; void * v14; void * v15; gm_ttf6i6v1da8cm v16; gm_t9805qtonbsag v17; uint32_t v18; uint32_t v19; gm_t0pc2c7dafsjt v20; gm_tuggr5sml9o52 v21; gm_tkm39klvnn4cr v22; gm_ta6jn77i9rmqf v23; void * v24; gm_t9s8r45p2ijj5 v25; gm_tr8okf1iilonm v26; gm_tscalq56heofe v27; gm_ts6ln23edn25a v28; gm_t8ghgl2jj0qha v29; gm_trl9d7m1k5lbd v30; gm_ta612fil812pm v31; gm_t8jsc4n9vp2ib v32; gm_tt8o5kebou62r v33; gm_tiilmfd16v3fc v34; gm_tf4r0415i49d1 v35; gm_tkaapcii86udk v36; gm_t6j6smj7qoctu v37; void * v38; gm_b16_8 v39; gm_b16_8 v40; void * v41; gm_ttggq7ng4kp3p v42; void * v43; gm_tsplfr33dsvji v44; gm_t3dcoc7077t1q v45; gm_t083kd7b1kkag v46; gm_tlcl4u558l6p5 v47; gm_tlcl4u558l6p5 v48; gm_tkaapcii86udk v49; void * v50; gm_trl2khmuphncd v51; gm_t710lsrq1m8gk v52; gm_teo9d4mncjfdu v53; gm_te9hrk0u35rev v54; gm_ttggq7ng4kp3p v55; void * v56; gm_t3mf58gv9kf3n v57; void * v58; gm_tntn2tf15puib v59; gm_t710lsrq1m8gk v60; } f1; } gm_tbo4aa19b457h;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[12]; gm_tlpa5tka3taqb f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tdgedp3mu9p1a;
_Static_assert(sizeof(gm_tlpa5tka3taqb) == 128 && _Alignof(gm_tlpa5tka3taqb) == 16 && offsetof(gm_tlpa5tka3taqb, f0) == 0 && offsetof(gm_tlpa5tka3taqb, f1) == 16, "layout of `PatKind`");
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_tbo4aa19b457h) == 96 && _Alignof(gm_tbo4aa19b457h) == 8 && offsetof(gm_tbo4aa19b457h, f0) == 0 && offsetof(gm_tbo4aa19b457h, f1) == 8, "layout of `Op`");
_Static_assert(sizeof(gm_tdgedp3mu9p1a) == 160 && _Alignof(gm_tdgedp3mu9p1a) == 16 && offsetof(gm_tdgedp3mu9p1a, f0) == 0 && offsetof(gm_tdgedp3mu9p1a, f1) == 16 && offsetof(gm_tdgedp3mu9p1a, f2) == 144, "layout of `Pat`");
typedef struct { gm_tbo4aa19b457h f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; uint64_t f2; gm_tl874t2atnlh3 f3; uint8_t f4; uint8_t pe[3]; } gm_tv8a4cs3pcpgp;
_Static_assert(sizeof(gm_tv8a4cs3pcpgp) == 128 && _Alignof(gm_tv8a4cs3pcpgp) == 8 && offsetof(gm_tv8a4cs3pcpgp, f0) == 0 && offsetof(gm_tv8a4cs3pcpgp, f1) == 96 && offsetof(gm_tv8a4cs3pcpgp, f2) == 104 && offsetof(gm_tv8a4cs3pcpgp, f3) == 112 && offsetof(gm_tv8a4cs3pcpgp, f4) == 124, "layout of `Node`");
struct gm_fr_align_15fjgq1 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_align_of_k6eqqrn {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_big_float_lh5u3gu {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* i */
    double ret;
};
struct gm_fr_const_op_r0qgpnv {
    gm_frame h;
    gm_tbo4aa19b457h ret;
};
struct gm_fr_empty_slice_28d0d11 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_3v2gjd4_4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5rt2oa7 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_73gbl6u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hmpovae {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_mah65iv {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_oana0to {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rtces00_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_eq_word_itm79nq {
    gm_frame h;
    gm_tbgiuvo2j5vcs ret;
};
struct gm_fr_expr_a84t331 {
    gm_frame h;
    gm_tbci5ospk5nnn l3; /* i */
    gm_tsth9qu1dv0g3 l48; /* callee */
    gm_trqfvmjtt4sij l81; /* out */
    gm_t710lsrq1m8gk l87; /* x */
    gm_th1g69d7vpipg l100; /* sink */
    gm_t710lsrq1m8gk l101; /* w */
    gm_trqfvmjtt4sij l102; /* hs */
    gm_tbo4aa19b457h l110; /* op */
    void * ret;
};
struct gm_fr_exprs_musunrl {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_get_u64_tgpb2vh {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_int_w_a8oe7le {
    gm_frame h;
    gm_tg5lql6itufk7 ret;
};
struct gm_fr_is_cmp_r2i4hv9 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_float_op_6co8naj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_packed_8fhislp {
    gm_frame h;
    gm_tpautr4br7h3l l3; /* def */
    uint8_t ret;
};
struct gm_fr_is_shift_svl330f {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_htv6093 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
};
struct gm_fr_list_31hegji {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_58nr42v_4 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_kualkeq {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_mlkakgl {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nh3d5t2_2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_pp6ecn9 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_r36qhtc {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_sjspm77 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_low_binary_5dhmccv {
    gm_frame h;
    gm_tbo4aa19b457h ret;
};
struct gm_fr_low_block_klh2c88 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* stmts */
    gm_trqfvmjtt4sij l17; /* ps */
    gm_t2k9dbsv5ff58 l24; /* x */
    void * ret;
};
struct gm_fr_low_builtin_rbo6661 {
    gm_frame h;
    gm_tbo4aa19b457h ret;
};
struct gm_fr_low_coerce_rhvh5p0 {
    gm_frame h;
    gm_tbo4aa19b457h ret;
};
struct gm_fr_low_conds_1olabjo {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_low_pat_bba97qc {
    gm_frame h;
    gm_t59juu943t8sl l5; /* r */
    gm_trqfvmjtt4sij l21; /* out */
    gm_trqfvmjtt4sij l26; /* out */
    gm_tlpa5tka3taqb l30; /* kind */
    void * ret;
};
struct gm_fr_low_unary_3f5k6s5 {
    gm_frame h;
    gm_tbo4aa19b457h ret;
};
struct gm_fr_lower_body_g4bo546 {
    gm_frame h;
    void * ret;
};
struct gm_fr_mask64_vvoc1f6 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_mk_node_ndavg23 {
    gm_frame h;
    gm_tbo4aa19b457h l2; /* op */
    void * ret;
};
struct gm_fr_nullable_pointer_7qfmn6o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_offsets_qpt7c30 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_opt_expr_gr6e3ks {
    gm_frame h;
    void * ret;
};
struct gm_fr_payload_offset_la0c8ft {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_rd_word_8flcq6o {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_seq_kind_405ip0f {
    gm_frame h;
    gm_thdrvp9q0u7b5 ret;
};
struct gm_fr_us_cuv01fp {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_word_cmp_s2rbdbn {
    gm_frame h;
    gm_tbgiuvo2j5vcs ret;
};
inline bool gm_eq0lfe1s2pin(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b);
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b);
inline bool gm_eqg63o53ar52(const uint8_t *a, const uint8_t *b);
inline bool gm_eqov4e3mbi0f(const uint8_t *a, const uint8_t *b);
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_adt_of_8k5vqud(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_15fjgq1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_align_of_k6eqqrn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) double gm_f_big_float_lh5u3gu(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) gm_tplpbqf3duvn3 gm_f_bits_field_vqua3kd(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_bits_struct_ty_22ue20s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_const_op_r0qgpnv(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tbo4aa19b457h gm_f_const_op_r0qgpnv_c(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_28d0d11(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5rt2oa7(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73gbl6u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hmpovae(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mah65iv(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oana0to(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00_2(void);
__attribute__((aligned(4))) gm_tbgiuvo2j5vcs gm_f_eq_word_itm79nq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_expr_a84t331(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_exprs_musunrl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tfbnciphf3261 gm_f_field_layout_h34salb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint32_t a2);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
inline __attribute__((aligned(4))) uint64_t gm_f_get_u64_tgpb2vh(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) unsigned __int128 gm_f_get_uint_bq8c4b7(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_info_ulcevdv(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_w_a8oe7le(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_cmp_r2i4hv9(gm_ts6oqppjh0ba3 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_op_6co8naj(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_nullable_pointer_nkie06s(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_packed_8fhislp(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_place_expr_qlfi6ot(void * a0, void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_shift_svl330f(gm_ts6oqppjh0ba3 a0);
inline __attribute__((aligned(4))) void gm_f_kind_htv6093(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_31hegji(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_4(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_kualkeq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_mlkakgl(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nh3d5t2_2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_pp6ecn9(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_r36qhtc(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_sjspm77(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_low_binary_5dhmccv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, void * a4);
__attribute__((aligned(4))) void * gm_f_low_block_klh2c88(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_low_builtin_rbo6661(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tvdg94bk7aa22 a1, gm_b16_8 a2, void * a3);
__attribute__((aligned(4))) void gm_f_low_coerce_rhvh5p0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tmn7n0n9raou5 a1, void * a2, void * a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_low_conds_1olabjo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_low_pat_bba97qc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_low_unary_3f5k6s5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, void * a3);
inline __attribute__((aligned(4))) void * gm_f_lower_body_g4bo546(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_make_fill_jpvm0kc(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, uint8_t a1, const void * a2);
__attribute__((aligned(4))) void gm_f_make_phdnqpj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_mask64_vvoc1f6(uint32_t a0);
__attribute__((aligned(4))) void * gm_f_mk_node_ndavg23(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_her8si0(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_jg8stkn(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void * gm_f_opt_expr_gr6e3ks(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_69l8e45(void * a0, gm_ttggq7ng4kp3p a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_a0ftqhm(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_bpovr2k_4(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_esf6o94(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_h04leh4(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_lq94qhs(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_u2jka7o_2(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_uehemid(void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_put_float_fiad72d(void * a0, gm_csguankko4lmhd a1, double a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_put_uint_cddin99(void * a0, gm_b16_8 a1, unsigned __int128 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_rd_word_8flcq6o(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_seq_kind_405ip0f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_hr4b76n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_union_def_h8mvf4t(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_us_cuv01fp(uint64_t a0);
__attribute__((aligned(4))) gm_tbgiuvo2j5vcs gm_f_word_cmp_s2rbdbn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern uint8_t gm_k4j26j8fh8s[40] __attribute__((aligned(8)));
extern uint8_t gm_k8ao9fftopr[40] __attribute__((aligned(8)));
extern uint8_t gm_k8o4k1d1for[40] __attribute__((aligned(8)));
extern uint8_t gm_klsha2ujn8u[40] __attribute__((aligned(8)));
extern uint8_t gm_kpc4646t5v3[40] __attribute__((aligned(8)));
extern uint8_t gm_krt2fusbrlm[40] __attribute__((aligned(8)));
extern uint8_t gm_ktu15664o6a[40] __attribute__((aligned(8)));
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src7afujsggtlje[];
extern const char gm_srch12rfrebvus5[];
extern const char gm_srcj10g653a00lt[];
extern const char gm_srcnqtpfma6qe4a[];
extern const char gm_srcrgc9bkquf6nr[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0lfe1s2pin(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    return true;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9oqmmiu8qc(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint16_t, a) == GM_LD(uint16_t, b);
}
inline bool gm_eqehhdtpeti1(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqg63o53ar52(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    if (ta == (uint8_t)21) return gm_eqehhdtpeti1(a + 4, b + 4);
    if (ta == (uint8_t)68) return gm_eqov4e3mbi0f(a + 4, b + 4);
    if (ta == (uint8_t)72) return gm_eqtje7cegoml(a + 4, b + 4);
    if (ta == (uint8_t)79) return gm_eq0lfe1s2pin(a + 4, b + 4);
    if (ta == (uint8_t)80) return gm_eq467d5cm1d5(a + 4, b + 4);
    if (ta == (uint8_t)81) return gm_eq467d5cm1d5(a + 4, b + 4);
    return true;
}
inline bool gm_eqov4e3mbi0f(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
inline bool gm_eqtje7cegoml(const uint8_t *a, const uint8_t *b) {
    uint8_t ta = GM_LD(uint8_t, a + 0), tb = GM_LD(uint8_t, b + 0);
    if (ta != tb) return false;
    return true;
}
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
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 16);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_align_of_k6eqqrn((void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t4;
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
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_a5ovnrm((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbccvikg6b96e t5 = t4;
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 8));
    F.ret = t6;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) double gm_f_big_float_lh5u3gu(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_big_float_lh5u3gu F;
    void * gm_l0; /* _m */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_tngtuiuskn938 t1;
    gm_f_from_int_ofjuoqh((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    double t2 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t2;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_const_op_r0qgpnv(void *gm_out, void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_const_op_r0qgpnv F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 307u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t4 = (t3 <= ((uint64_t)8ULL));
        if (!t4) goto ie2;
        {
            void * t6 = gm_l0;
            gm_b16_8 t7 = gm_l1;
            gm_b16_8 t8 = t7;
            gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
            uint64_t t10 = gm_f_get_u64_tgpb2vh(t6, t9);
            gm_tbo4aa19b457h t11;
            memset(&t11, 0, sizeof t11);
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)0ULL));
            GM_ST(uint64_t, ((uint8_t *)&t11 + 8), t10);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t13 = gm_l1;
        gm_tbo4aa19b457h t14;
        memset(&t14, 0, sizeof t14);
        GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)1ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t14 + 8), t13);
        uint8_t t15 = 0;
        F.ret = t14;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_28d0d11(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_28d0d11 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_4 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5rt2oa7(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5rt2oa7 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_73gbl6u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_73gbl6u F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hmpovae(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hmpovae F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_mah65iv(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_mah65iv F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_oana0to(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_oana0to F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rtces00_2 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        void * t3 = t2;
        if (!(GM_LD(void *, ((uint8_t *)&t3)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t4 = GM_LD(void *, ((uint8_t *)&t3));
        gm_l0 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = ((uint8_t *)t5);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(t8 - t7));
        uint8_t t10 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_from_int_ofjuoqh F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_tbci5ospk5nnn t1 = F.l0;
    gm_tbci5ospk5nnn t2;
    gm_f_one_08qg1op((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0, t1);
    GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48, t2);
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
    GM_STACK_CHECK_SHALLOW(&F, 153u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1;
    uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
    uint8_t t4 = (t3 <= ((uint64_t)8ULL));
    if (!t4) goto ie2;
    void * t6 = gm_l0;
    gm_b16_8 t7 = gm_l1;
    uint64_t t8 = gm_f_rd_word_8flcq6o(t6, t7);
    uint64_t t9 = t8;
    t1 = t9;
    goto ix2;
ie2:;
    void * t11 = gm_l0;
    gm_b16_8 t12 = gm_l1;
    unsigned __int128 t13 = gm_f_get_uint_bq8c4b7(t11, t12);
    uint64_t t14 = (uint64_t)t13;
    uint64_t t15 = t14;
    t1 = t15;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_cmp_r2i4hv9(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_cmp_r2i4hv9 F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t6 != ((uint8_t)10ULL)) goto pa5;
    goto po4;
pa5:;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t8 != ((uint8_t)11ULL)) goto pa7;
    goto po4;
pa7:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)12ULL)) goto pa9;
    goto po4;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)13ULL)) goto pa11;
    goto po4;
pa11:;
    goto mn3;
po4:;
    t1 = ((uint8_t)1);
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn13:;
    gm_trap("no match arm matched the scrutinee", gm_srcj10g653a00lt, 26, 40);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_float_op_6co8naj(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_float_op_6co8naj F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 9u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t6 != ((uint8_t)0ULL)) goto pa5;
    goto po4;
pa5:;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t8 != ((uint8_t)1ULL)) goto pa7;
    goto po4;
pa7:;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t10 != ((uint8_t)2ULL)) goto pa9;
    goto po4;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)4ULL)) goto pa11;
    goto po4;
pa11:;
    goto mn3;
po4:;
    t1 = ((uint8_t)1);
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn13:;
    gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 449, 45);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_packed_8fhislp(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_packed_8fhislp F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    void * gm_l2; /* a */
    GM_STACK_CHECK_SHALLOW(&F, 127u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        void * t4 = gm_f_adt_of_8k5vqud(t2, GM_CONV(gm_csguankko4lmhd, t3));
        void * t5 = t4;
        void * t6;
        if ((GM_LD(void *, ((uint8_t *)&t5)) != 0)) {
            void * t7 = GM_LD(void *, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            uint8_t t8 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        void * t10 = gm_l2;
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t10) + 24) + 0));
        if (t11 != ((uint8_t)0ULL)) goto ie9;
        gm_tpautr4br7h3l t12 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t10) + 24) + 8));
        F.l3 = t12;
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 16));
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix9;
ie9:;
        {
            uint8_t t17 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix9:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_shift_svl330f(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_shift_svl330f F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 7u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1;
    uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t6 != ((uint8_t)3ULL)) goto pa5;
    goto po4;
pa5:;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t8 != ((uint8_t)6ULL)) goto pa7;
    goto po4;
pa7:;
    goto mn3;
po4:;
    t1 = ((uint8_t)1);
    goto me2;
mn3:;
    t1 = ((uint8_t)0);
    goto me2;
mn9:;
    gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 444, 42);
me2:;
    F.ret = t1;
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
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 8));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_f_kind_jcnt4hm((void *)&(*(gm_t53at80pfdus3 *)gm_out), t3, GM_CONV(gm_csguankko4lmhd, t4));
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
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 151, 54);
    gm_t53at80pfdus3 t6 = GM_LD(gm_t53at80pfdus3, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 56));
    F.ret = t6;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_layout_of_e6hfj1r F;
    void * gm_l0; /* lay */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 64));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_layout_a5ovnrm((void *)&(*(gm_tbccvikg6b96e *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_31hegji(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_31hegji F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_oana0to();
    gm_node *pn2 = gm_find(cx, 587034433);
    if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t3;
    t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16, ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_4(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_4 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3v2gjd4_4();
    gm_node *pn2 = gm_find(cx, 587034433);
    if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t3;
    t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16, ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_kualkeq(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_kualkeq F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_5rt2oa7();
    gm_node *pn2 = gm_find(cx, 587034433);
    if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t3;
    t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16, ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_mlkakgl(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_mlkakgl F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_mah65iv();
    gm_node *pn2 = gm_find(cx, 587034433);
    if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t3;
    t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16, ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_nh3d5t2_2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nh3d5t2_2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rtces00_2();
    gm_node *pn2 = gm_find(cx, 587034433);
    if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t3;
    t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16, ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_pp6ecn9(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_pp6ecn9 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_hmpovae();
    gm_node *pn2 = gm_find(cx, 587034433);
    if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t3;
    t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16, ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_r36qhtc(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_r36qhtc F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_28d0d11();
    gm_node *pn2 = gm_find(cx, 587034433);
    if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t3;
    t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16, ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24, t3);
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
inline __attribute__((aligned(4))) void gm_f_list_sjspm77(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_sjspm77 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_73gbl6u();
    gm_node *pn2 = gm_find(cx, 587034433);
    if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_srcrgc9bkquf6nr, 13, 97);
    gm_t3mf58gv9kf3n t3;
    t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 0, t1);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 16, ((uint64_t)0ULL));
    GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_trqfvmjtt4sij *)gm_out) + 24, t3);
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
inline __attribute__((aligned(4))) void * gm_f_lower_body_g4bo546(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_lower_body_g4bo546 F;
    void * gm_l0; /* m */
    void * gm_l1; /* info */
    void * gm_l2; /* e */
    gm_t3mf58gv9kf3n gm_l3; /* lw */
    GM_STACK_CHECK(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_t3mf58gv9kf3n t4;
        GM_ST(void *, ((uint8_t *)&t4 + 0), t2);
        GM_ST(void *, ((uint8_t *)&t4 + 8), t3);
        gm_l3 = t4;
    }
    {
        void * t5 = (void *)((uint8_t *)&gm_l3);
        void * t6 = gm_l2;
        void * t7 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t8 = 0;
        F.ret = t7;
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline uint64_t gm_f_mask64_vvoc1f6(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mask64_vvoc1f6 F;
    uint32_t gm_l0; /* bits */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint64_t t1;
    uint32_t t3 = gm_l0;
    uint8_t t4 = (t3 >= ((uint32_t)64ULL));
    if (!t4) goto ie2;
    uint64_t t6 = (uint64_t)~((uint64_t)0ULL);
    uint64_t t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    uint32_t t9 = gm_l0;
    uint64_t t10 = (uint64_t)t9;
    unsigned __int128 n11 = (unsigned __int128)t10;
    if (n11 >= 64) gm_trap_shift((uint64_t)n11, "u64", gm_srch12rfrebvus5, 7, 69);
    uint64_t r12 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n11);
    bool o13 = (uint64_t)(r12 >> (int)n11) != ((uint64_t)1ULL);
    uint64_t t14 = r12;
    uint64_t r15; bool o16 = __builtin_sub_overflow(t14, ((uint64_t)1ULL), &r15);
    uint64_t t17 = r15;
    uint64_t t18 = t17;
    t1 = t18;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_nullable_pointer_7qfmn6o F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 27u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    uint8_t t3 = gm_f_is_nullable_pointer_nkie06s(t1, GM_CONV(gm_csguankko4lmhd, t2));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_qpt7c30(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_qpt7c30 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 16);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_tbccvikg6b96e t4;
    gm_f_layout_of_e6hfj1r((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbccvikg6b96e t5 = t4;
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 16));
    F.ret = t6;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
inline __attribute__((aligned(4))) void * gm_f_opt_expr_gr6e3ks(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_opt_expr_gr6e3ks F;
    void * gm_l0; /* lw */
    void * gm_l1; /* x */
    void * gm_l2; /* e */
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        void * t3 = t2;
        void * t4;
        if ((GM_LD(void *, ((uint8_t *)&t3)) != 0)) {
            void * t5 = GM_LD(void *, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            uint8_t t6 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l2 = t4;
    }
    {
        void * t7 = gm_l0;
        void * t8 = gm_l2;
        void * t9 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = 0;
        F.ret = t9;
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
inline __attribute__((aligned(4))) uint64_t gm_f_payload_offset_la0c8ft(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_payload_offset_la0c8ft F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* offs */
    GM_STACK_CHECK(&F, 130u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_offsets_qpt7c30((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t4;
    }
    {
        uint64_t t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t8 = (t7 > ((uint64_t)1ULL));
        if (!t8) goto ie6;
        uint64_t t10 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 231, 30);
        uint64_t t12 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t10 * 8));
        uint64_t t13 = t12;
        t5 = t13;
        goto ix6;
ie6:;
        uint64_t t15 = ((uint64_t)0ULL);
        t5 = t15;
ix6:;
        uint8_t t16 = 0;
        F.ret = t5;
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
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 89));
        if (!t4) goto ie2;
        {
            uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t8 = ((t7 == ((uint64_t)8ULL)));
            if (!t8) goto ie6;
            {
                uint64_t t10 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t10 >= (uintptr_t)(t11)) gm_trap_index(t10, t11, gm_srcnqtpfma6qe4a, 316, 36);
                uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t10 * 1));
                uint64_t t13 = (uint64_t)t12;
                uint64_t t14 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t14 >= (uintptr_t)(t15)) gm_trap_index(t14, t15, gm_srcnqtpfma6qe4a, 316, 49);
                uint8_t t16 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 1));
                uint64_t t17 = (uint64_t)t16;
                unsigned __int128 n18 = (unsigned __int128)((uint64_t)8ULL);
                if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t r19 = (uint64_t)((uint64_t)t17 << (int)n18);
                bool o20 = (uint64_t)(r19 >> (int)n18) != t17;
                if (o20) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 45);
                uint64_t t21 = r19;
                uint64_t t22 = (uint64_t)(t13 | t21);
                uint64_t t23 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcnqtpfma6qe4a, 316, 68);
                uint8_t t25 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t23 * 1));
                uint64_t t26 = (uint64_t)t25;
                unsigned __int128 n27 = (unsigned __int128)((uint64_t)16ULL);
                if (n27 >= 64) gm_trap_shift((uint64_t)n27, "u64", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t r28 = (uint64_t)((uint64_t)t26 << (int)n27);
                bool o29 = (uint64_t)(r28 >> (int)n27) != t26;
                if (o29) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 64);
                uint64_t t30 = r28;
                uint64_t t31 = (uint64_t)(t22 | t30);
                uint64_t t32 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcnqtpfma6qe4a, 316, 88);
                uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t32 * 1));
                uint64_t t35 = (uint64_t)t34;
                unsigned __int128 n36 = (unsigned __int128)((uint64_t)24ULL);
                if (n36 >= 64) gm_trap_shift((uint64_t)n36, "u64", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t r37 = (uint64_t)((uint64_t)t35 << (int)n36);
                bool o38 = (uint64_t)(r37 >> (int)n36) != t35;
                if (o38) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 84);
                uint64_t t39 = r37;
                uint64_t t40 = (uint64_t)(t31 | t39);
                uint64_t t41 = (uintptr_t)((uint64_t)4ULL);
                uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_srcnqtpfma6qe4a, 316, 108);
                uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t41 * 1));
                uint64_t t44 = (uint64_t)t43;
                unsigned __int128 n45 = (unsigned __int128)((uint64_t)32ULL);
                if (n45 >= 64) gm_trap_shift((uint64_t)n45, "u64", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t r46 = (uint64_t)((uint64_t)t44 << (int)n45);
                bool o47 = (uint64_t)(r46 >> (int)n45) != t44;
                if (o47) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 104);
                uint64_t t48 = r46;
                uint64_t t49 = (uint64_t)(t40 | t48);
                uint64_t t50 = (uintptr_t)((uint64_t)5ULL);
                uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcnqtpfma6qe4a, 316, 128);
                uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t50 * 1));
                uint64_t t53 = (uint64_t)t52;
                unsigned __int128 n54 = (unsigned __int128)((uint64_t)40ULL);
                if (n54 >= 64) gm_trap_shift((uint64_t)n54, "u64", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t r55 = (uint64_t)((uint64_t)t53 << (int)n54);
                bool o56 = (uint64_t)(r55 >> (int)n54) != t53;
                if (o56) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 124);
                uint64_t t57 = r55;
                uint64_t t58 = (uint64_t)(t49 | t57);
                uint64_t t59 = (uintptr_t)((uint64_t)6ULL);
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_srcnqtpfma6qe4a, 316, 148);
                uint8_t t61 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t59 * 1));
                uint64_t t62 = (uint64_t)t61;
                unsigned __int128 n63 = (unsigned __int128)((uint64_t)48ULL);
                if (n63 >= 64) gm_trap_shift((uint64_t)n63, "u64", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t r64 = (uint64_t)((uint64_t)t62 << (int)n63);
                bool o65 = (uint64_t)(r64 >> (int)n63) != t62;
                if (o65) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 144);
                uint64_t t66 = r64;
                uint64_t t67 = (uint64_t)(t58 | t66);
                uint64_t t68 = (uintptr_t)((uint64_t)7ULL);
                uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_srcnqtpfma6qe4a, 316, 168);
                uint8_t t70 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t68 * 1));
                uint64_t t71 = (uint64_t)t70;
                unsigned __int128 n72 = (unsigned __int128)((uint64_t)56ULL);
                if (n72 >= 64) gm_trap_shift((uint64_t)n72, "u64", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t r73 = (uint64_t)((uint64_t)t71 << (int)n72);
                bool o74 = (uint64_t)(r73 >> (int)n72) != t71;
                if (o74) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 316, 164);
                uint64_t t75 = r73;
                uint64_t t76 = (uint64_t)(t67 | t75);
                uint8_t t77 = 0;
                F.ret = t76;
                goto gm_fret;
            }
            goto ix6;
ie6:;
ix6:;
        }
        {
            uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t80 = ((t79 == ((uint64_t)4ULL)));
            if (!t80) goto ie78;
            {
                uint64_t t82 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t82 >= (uintptr_t)(t83)) gm_trap_index(t82, t83, gm_srcnqtpfma6qe4a, 317, 36);
                uint8_t t84 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t82 * 1));
                uint64_t t85 = (uint64_t)t84;
                uint64_t t86 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcnqtpfma6qe4a, 317, 49);
                uint8_t t88 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t86 * 1));
                uint64_t t89 = (uint64_t)t88;
                unsigned __int128 n90 = (unsigned __int128)((uint64_t)8ULL);
                if (n90 >= 64) gm_trap_shift((uint64_t)n90, "u64", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t r91 = (uint64_t)((uint64_t)t89 << (int)n90);
                bool o92 = (uint64_t)(r91 >> (int)n90) != t89;
                if (o92) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 45);
                uint64_t t93 = r91;
                uint64_t t94 = (uint64_t)(t85 | t93);
                uint64_t t95 = (uintptr_t)((uint64_t)2ULL);
                uint64_t t96 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t95 >= (uintptr_t)(t96)) gm_trap_index(t95, t96, gm_srcnqtpfma6qe4a, 317, 68);
                uint8_t t97 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t95 * 1));
                uint64_t t98 = (uint64_t)t97;
                unsigned __int128 n99 = (unsigned __int128)((uint64_t)16ULL);
                if (n99 >= 64) gm_trap_shift((uint64_t)n99, "u64", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t r100 = (uint64_t)((uint64_t)t98 << (int)n99);
                bool o101 = (uint64_t)(r100 >> (int)n99) != t98;
                if (o101) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 64);
                uint64_t t102 = r100;
                uint64_t t103 = (uint64_t)(t94 | t102);
                uint64_t t104 = (uintptr_t)((uint64_t)3ULL);
                uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t104 >= (uintptr_t)(t105)) gm_trap_index(t104, t105, gm_srcnqtpfma6qe4a, 317, 88);
                uint8_t t106 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t104 * 1));
                uint64_t t107 = (uint64_t)t106;
                unsigned __int128 n108 = (unsigned __int128)((uint64_t)24ULL);
                if (n108 >= 64) gm_trap_shift((uint64_t)n108, "u64", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t r109 = (uint64_t)((uint64_t)t107 << (int)n108);
                bool o110 = (uint64_t)(r109 >> (int)n108) != t107;
                if (o110) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 317, 84);
                uint64_t t111 = r109;
                uint64_t t112 = (uint64_t)(t103 | t111);
                uint8_t t113 = 0;
                F.ret = t112;
                goto gm_fret;
            }
            goto ix78;
ie78:;
ix78:;
        }
        {
            uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t116 = ((t115 == ((uint64_t)1ULL)));
            if (!t116) goto ie114;
            {
                uint64_t t118 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t118 >= (uintptr_t)(t119)) gm_trap_index(t118, t119, gm_srcnqtpfma6qe4a, 318, 36);
                uint8_t t120 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t118 * 1));
                uint64_t t121 = (uint64_t)t120;
                uint8_t t122 = 0;
                F.ret = t121;
                goto gm_fret;
            }
            goto ix114;
ie114:;
ix114:;
        }
        {
            uint64_t t124 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t125 = ((t124 == ((uint64_t)2ULL)));
            if (!t125) goto ie123;
            {
                uint64_t t127 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t127 >= (uintptr_t)(t128)) gm_trap_index(t127, t128, gm_srcnqtpfma6qe4a, 319, 36);
                uint8_t t129 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t127 * 1));
                uint64_t t130 = (uint64_t)t129;
                uint64_t t131 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t132 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                if (t131 >= (uintptr_t)(t132)) gm_trap_index(t131, t132, gm_srcnqtpfma6qe4a, 319, 49);
                uint8_t t133 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t131 * 1));
                uint64_t t134 = (uint64_t)t133;
                unsigned __int128 n135 = (unsigned __int128)((uint64_t)8ULL);
                if (n135 >= 64) gm_trap_shift((uint64_t)n135, "u64", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t r136 = (uint64_t)((uint64_t)t134 << (int)n135);
                bool o137 = (uint64_t)(r136 >> (int)n135) != t134;
                if (o137) gm_trap("integer overflow in `<<`", gm_srcnqtpfma6qe4a, 319, 45);
                uint64_t t138 = r136;
                uint64_t t139 = (uint64_t)(t130 | t138);
                uint8_t t140 = 0;
                F.ret = t139;
                goto gm_fret;
            }
            goto ix123;
ie123:;
ix123:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t141 = gm_l0;
        gm_b16_8 t142 = gm_l1;
        unsigned __int128 t143 = gm_f_get_uint_bq8c4b7(t141, t142);
        uint64_t t144 = (uint64_t)t143;
        uint8_t t145 = 0;
        F.ret = t144;
        goto gm_fret;
    }
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
    uint64_t t1 = gm_l0;
    uint64_t t2 = (uint64_t)t1;
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_const_op_r0qgpnv(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tbo4aa19b457h gm_f_const_op_r0qgpnv_c(void * a0, gm_b16_8 a1) { gm_tbo4aa19b457h gm_r; gm_f_const_op_r0qgpnv(&gm_r, a0, a1); return gm_r; }
inline __attribute__((aligned(4))) gm_tbgiuvo2j5vcs gm_f_eq_word_itm79nq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_eq_word_itm79nq F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l2; /* inner */
    GM_STACK_CHECK(&F, 236u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_htv6093((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)13ULL)) goto ie2;
        gm_t88rcucg1b7uk t8 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t6) + 8));
        gm_l2 = t8;
        void * t9 = gm_l0;
        void * t10 = GM_LD(void *, (((uint8_t *)t9) + 8));
        gm_t88rcucg1b7uk t11 = gm_l2;
        uint8_t t12 = gm_f_nullable_pointer_7qfmn6o(t10, GM_CONV(gm_csguankko4lmhd, t11));
        if (!t12) goto ie2;
        {
            gm_tmn7n0n9raou5 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)0ULL));
            gm_tbgiuvo2j5vcs t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[8] = 1;
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t17 = gm_l0;
        gm_t88rcucg1b7uk t18 = gm_l1;
        gm_tbgiuvo2j5vcs t19 = gm_f_word_cmp_s2rbdbn((void *)NULL, cx, NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t20 = 0;
        F.ret = t19;
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
__attribute__((aligned(4))) void * gm_f_expr_a84t331(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_expr_a84t331 F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    void * gm_l2; /* m */
    uint64_t gm_l4; /* n */
    gm_b16_8 gm_l5; /* b */
    double gm_l6; /* x */
    uint64_t gm_l7; /* n */
    gm_b16_8 gm_l8; /* b */
    uint8_t gm_l9; /* b */
    gm_b16_8 gm_l10; /* bytes */
    gm_b16_8 gm_l11; /* bytes */
    uint32_t gm_l12; /* l */
    uint32_t gm_l13; /* levels */
    uint32_t gm_l14; /* l */
    uint64_t gm_l15; /* decl */
    gm_tl874t2atnlh3 gm_l16; /* span */
    void * gm_l17; /* x */
    void * gm_l18; /* base */
    uint32_t gm_l19; /* index */
    void * gm_l20; /* b */
    gm_tplpbqf3duvn3 gm_l21; /* bf */
    void * gm_l22; /* base */
    void * gm_l23; /* index */
    gm_te0mgnjdphs16 gm_l24; /* sk */
    gm_tao63pk3r18v5 gm_l25; /* sl */
    gm_te0mgnjdphs16 gm_l26; /* sk */
    uint64_t gm_l27; /* esz */
    gm_t88rcucg1b7uk gm_l28; /* elem */
    void * gm_l29; /* x */
    void * gm_l30; /* x */
    void * gm_l31; /* x */
    void * gm_l32; /* x */
    uint32_t gm_l33; /* f */
    uint32_t gm_l34; /* f */
    uint32_t gm_l35; /* h */
    gm_t9qkh0rsooc8d gm_l36; /* op */
    void * gm_l37; /* x */
    gm_ts6oqppjh0ba3 gm_l38; /* op */
    void * gm_l39; /* l */
    void * gm_l40; /* r */
    uint8_t gm_l41; /* is_and */
    void * gm_l42; /* l */
    void * gm_l43; /* r */
    gm_tmn7n0n9raou5 gm_l44; /* kind */
    void * gm_l45; /* x */
    gm_tvdg94bk7aa22 gm_l46; /* op */
    gm_b16_8 gm_l47; /* args */
    gm_b16_8 gm_l49; /* args */
    uint32_t gm_l50; /* f */
    gm_tiilmfd16v3fc gm_l51; /* effect_ */
    uint32_t gm_l52; /* op */
    void * gm_l53; /* c */
    void * gm_l54; /* cn */
    gm_b16_8 gm_l55; /* items */
    void * gm_l56; /* ad */
    gm_b16_8 gm_l57; /* offs */
    uint64_t gm_l58; /* <tmp> */
    uint64_t gm_l59; /* <tmp> */
    uint64_t gm_l60; /* i */
    gm_b16_8 gm_l61; /* items */
    void * gm_l62; /* x */
    gm_ttggq7ng4kp3p gm_l63; /* v */
    void * gm_l64; /* x */
    void * gm_l65; /* x */
    uint8_t gm_l66; /* is_result */
    gm_t3dcoc7077t1q gm_l67; /* x */
    gm_t083kd7b1kkag gm_l68; /* x */
    void * gm_l69; /* l */
    void * gm_l70; /* r */
    uint8_t gm_l71; /* negated */
    void * gm_l72; /* ln */
    void * gm_l73; /* rn */
    gm_tmn7n0n9raou5 gm_l74; /* how */
    void * gm_l75; /* elem */
    void * gm_l76; /* container */
    uint8_t gm_l77; /* negated */
    void * gm_l78; /* scrutinee */
    gm_b16_8 gm_l79; /* arms */
    void * gm_l80; /* s */
    gm_b16_8 gm_l82; /* <tmp> */
    uint64_t gm_l83; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l84; /* arm */
    void * gm_l85; /* b */
    gm_trl2khmuphncd gm_l86; /* x */
    gm_teo9d4mncjfdu gm_l88; /* x */
    gm_te9hrk0u35rev gm_l89; /* t */
    uint32_t gm_l90; /* levels */
    void * gm_l91; /* value */
    void * gm_l92; /* x */
    void * gm_l93; /* k */
    void * gm_l94; /* v */
    void * gm_l95; /* k */
    void * gm_l96; /* w */
    gm_tiilmfd16v3fc gm_l97; /* effect_ */
    uint32_t gm_l98; /* op */
    void * gm_l99; /* x */
    gm_b16_8 gm_l103; /* <tmp> */
    uint64_t gm_l104; /* <tmp> */
    gm_t4lnf71mruqpt gm_l105; /* h */
    void * gm_l106; /* x */
    gm_b16_8 gm_l107; /* decls */
    void * gm_l108; /* value */
    gm_t4lnf71mruqpt gm_l109; /* hv */
    GM_STACK_CHECK(&F, 15558u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l1;
        gm_tbo4aa19b457h t5;
        uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        switch (t59) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma57; case ((uint8_t)3ULL): goto ma57; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma57; case ((uint8_t)10ULL): goto ma57; case ((uint8_t)11ULL): goto ma57; case ((uint8_t)12ULL): goto ma57; case ((uint8_t)13ULL): goto ma57; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma14; case ((uint8_t)16ULL): goto ma15; case ((uint8_t)17ULL): goto ma16; case ((uint8_t)18ULL): goto ma18; case ((uint8_t)19ULL): goto ma19; case ((uint8_t)20ULL): goto ma20; case ((uint8_t)21ULL): goto ma21; case ((uint8_t)22ULL): goto ma22; case ((uint8_t)23ULL): goto ma23; case ((uint8_t)24ULL): goto ma24; case ((uint8_t)25ULL): goto ma25; case ((uint8_t)26ULL): goto ma26; case ((uint8_t)27ULL): goto ma27; case ((uint8_t)28ULL): goto ma28; case ((uint8_t)29ULL): goto ma29; case ((uint8_t)30ULL): goto ma30; case ((uint8_t)31ULL): goto ma31; case ((uint8_t)32ULL): goto ma32; case ((uint8_t)33ULL): goto ma33; case ((uint8_t)34ULL): goto ma34; case ((uint8_t)35ULL): goto ma35; case ((uint8_t)36ULL): goto ma36; case ((uint8_t)37ULL): goto ma37; case ((uint8_t)38ULL): goto ma38; case ((uint8_t)39ULL): goto ma39; case ((uint8_t)40ULL): goto ma40; case ((uint8_t)41ULL): goto ma41; case ((uint8_t)42ULL): goto ma42; case ((uint8_t)43ULL): goto ma43; case ((uint8_t)44ULL): goto ma45; case ((uint8_t)45ULL): goto ma44; case ((uint8_t)46ULL): goto ma46; case ((uint8_t)47ULL): goto ma47; case ((uint8_t)48ULL): goto ma48; case ((uint8_t)49ULL): goto ma49; case ((uint8_t)50ULL): goto ma50; case ((uint8_t)51ULL): goto ma51; case ((uint8_t)52ULL): goto ma52; case ((uint8_t)53ULL): goto ma53; case ((uint8_t)54ULL): goto ma54; case ((uint8_t)55ULL): goto ma55; case ((uint8_t)56ULL): goto ma57; case ((uint8_t)57ULL): goto ma57; case ((uint8_t)58ULL): goto ma56; default: goto ma58; }
ma7:;
        uint8_t t61 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t61 != ((uint8_t)0ULL)) goto mn60;
        gm_tbci5ospk5nnn t62 = GM_LD(gm_tbci5ospk5nnn, ((((uint8_t *)t4) + 8) + 8));
        F.l3 = t62;
        {
            void * t64 = gm_l2;
            void * t65 = gm_l1;
            gm_t88rcucg1b7uk t66 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t65) + 0));
            uint64_t t67 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t64, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t67;
        }
        {
            uint64_t t68 = gm_l4;
            uint64_t t69 = (t68 > ((uint64_t)1ULL) ? t68 : ((uint64_t)1ULL));
            uint64_t t70 = gm_f_us_cuv01fp(t69);
            gm_tabb710d9mq2e t71;
            gm_f_make_fill_jpvm0kc((void *)&t71, (void *)NULL, cx, NULL, t70, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kpc4646t5v3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t72 = t71;
            if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_src7afujsggtlje, 276, 18);
            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
            gm_l5 = t73;
        }
        {
            void * t75 = gm_l2;
            void * t76 = gm_l1;
            gm_t88rcucg1b7uk t77 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t76) + 0));
            gm_t53at80pfdus3 t78;
            gm_f_kind_htv6093((void *)&t78, t75, GM_CONV(gm_csguankko4lmhd, t77));
            gm_t53at80pfdus3 t79 = t78;
            uint8_t t80;
            uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t79) + 0));
            if (t83 != ((uint8_t)3ULL)) goto mn82;
            t80 = ((uint8_t)1);
            goto me81;
mn82:;
            t80 = ((uint8_t)0);
            goto me81;
mn84:;
            gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 277, 16);
me81:;
            if (!t80) goto ie74;
            {
                void * t86 = gm_l2;
                void * t87 = gm_l1;
                gm_t88rcucg1b7uk t88 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t87) + 0));
                void * t89 = gm_l2;
                double t90 = gm_f_big_float_lh5u3gu((void *)NULL, cx, NULL, t89, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t91 = gm_l5;
                gm_b16_8 t92 = t91;
                uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&t92) + 8);
                void * t94 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t92)));
                uint64_t t95 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t96 = gm_l4;
                uint64_t t97 = gm_f_us_cuv01fp(t96);
                uint64_t t98 = (uintptr_t)t97;
                if (t95 > t98 || t98 > (uintptr_t)t93) gm_trap_slice(t95, t98, t93, gm_src7afujsggtlje, 278, 51);
                gm_b16_8 t99;
                GM_ST(void *, (uint8_t *)&t99, (void *)(((uint8_t *)t94 + t95 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(t98 - t95));
                gm_f_put_float_fiad72d(t86, GM_CONV(gm_csguankko4lmhd, t88), t90, t99);
            }
            goto ix74;
ie74:;
            {
                void * t101 = gm_l2;
                gm_b16_8 t102 = gm_l5;
                gm_b16_8 t103 = t102;
                uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&t103) + 8);
                void * t105 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t103)));
                uint64_t t106 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t107 = gm_l4;
                uint64_t t108 = gm_f_us_cuv01fp(t107);
                uint64_t t109 = (uintptr_t)t108;
                if (t106 > t109 || t109 > (uintptr_t)t104) gm_trap_slice(t106, t109, t104, gm_src7afujsggtlje, 280, 28);
                gm_b16_8 t110;
                GM_ST(void *, (uint8_t *)&t110, (void *)(((uint8_t *)t105 + t106 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(t109 - t106));
                unsigned __int128 t111 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
                gm_f_put_uint_cddin99(t101, t110, t111);
            }
ix74:;
        }
        void * t112 = gm_l2;
        gm_b16_8 t113 = gm_l5;
        gm_b16_8 t114 = t113;
        uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&t114) + 8);
        void * t116 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t114)));
        uint64_t t117 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t118 = gm_l4;
        uint64_t t119 = gm_f_us_cuv01fp(t118);
        uint64_t t120 = (uintptr_t)t119;
        if (t117 > t120 || t120 > (uintptr_t)t115) gm_trap_slice(t117, t120, t115, gm_src7afujsggtlje, 282, 25);
        gm_b16_8 t121;
        GM_ST(void *, (uint8_t *)&t121, (void *)(((uint8_t *)t116 + t117 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t121 + 8, (uintptr_t)(t120 - t117));
        gm_tbo4aa19b457h t122;
        gm_f_const_op_r0qgpnv((void *)&t122, t112, t121);
        gm_tbo4aa19b457h t123 = t122;
        t5 = t123;
        goto me6;
mn60:;
ma8:;
        uint8_t t125 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t125 != ((uint8_t)1ULL)) goto mn124;
        double t126 = GM_LD(double, ((((uint8_t *)t4) + 8) + 8));
        gm_l6 = t126;
        {
            void * t128 = gm_l2;
            void * t129 = gm_l1;
            gm_t88rcucg1b7uk t130 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t129) + 0));
            uint64_t t131 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t128, t130);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t131;
        }
        {
            uint64_t t132 = gm_l7;
            uint64_t t133 = (t132 > ((uint64_t)1ULL) ? t132 : ((uint64_t)1ULL));
            uint64_t t134 = gm_f_us_cuv01fp(t133);
            gm_tabb710d9mq2e t135;
            gm_f_make_fill_jpvm0kc((void *)&t135, (void *)NULL, cx, NULL, t134, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klsha2ujn8u)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t136 = t135;
            if (((((uint8_t *)&t136))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t136) + 8, gm_src7afujsggtlje, 286, 18);
            gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&t136) + 8));
            gm_l8 = t137;
        }
        {
            void * t138 = gm_l2;
            void * t139 = gm_l1;
            gm_t88rcucg1b7uk t140 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t139) + 0));
            double t141 = gm_l6;
            gm_b16_8 t142 = gm_l8;
            gm_b16_8 t143 = t142;
            uint64_t t144 = GM_LD(uintptr_t, ((uint8_t *)&t143) + 8);
            void * t145 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t143)));
            uint64_t t146 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t147 = gm_l7;
            uint64_t t148 = gm_f_us_cuv01fp(t147);
            uint64_t t149 = (uintptr_t)t148;
            if (t146 > t149 || t149 > (uintptr_t)t144) gm_trap_slice(t146, t149, t144, gm_src7afujsggtlje, 287, 34);
            gm_b16_8 t150;
            GM_ST(void *, (uint8_t *)&t150, (void *)(((uint8_t *)t145 + t146 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t150 + 8, (uintptr_t)(t149 - t146));
            gm_f_put_float_fiad72d(t138, GM_CONV(gm_csguankko4lmhd, t140), t141, t150);
        }
        void * t151 = gm_l2;
        gm_b16_8 t152 = gm_l8;
        gm_b16_8 t153 = t152;
        uint64_t t154 = GM_LD(uintptr_t, ((uint8_t *)&t153) + 8);
        void * t155 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t153)));
        uint64_t t156 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t157 = gm_l7;
        uint64_t t158 = gm_f_us_cuv01fp(t157);
        uint64_t t159 = (uintptr_t)t158;
        if (t156 > t159 || t159 > (uintptr_t)t154) gm_trap_slice(t156, t159, t154, gm_src7afujsggtlje, 288, 25);
        gm_b16_8 t160;
        GM_ST(void *, (uint8_t *)&t160, (void *)(((uint8_t *)t155 + t156 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(t159 - t156));
        gm_tbo4aa19b457h t161;
        gm_f_const_op_r0qgpnv((void *)&t161, t151, t160);
        gm_tbo4aa19b457h t162 = t161;
        t5 = t162;
        goto me6;
mn124:;
ma9:;
        uint8_t t164 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t164 != ((uint8_t)4ULL)) goto mn163;
        uint8_t t165 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 8));
        gm_l9 = t165;
        uint64_t t166;
        uint8_t t168 = gm_l9;
        if (!t168) goto ie167;
        uint64_t t170 = ((uint64_t)1ULL);
        t166 = t170;
        goto ix167;
ie167:;
        uint64_t t172 = ((uint64_t)0ULL);
        t166 = t172;
ix167:;
        gm_tbo4aa19b457h t173;
        memset(&t173, 0, sizeof t173);
        GM_ST(uint8_t, ((uint8_t *)&t173 + 0), ((uint8_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&t173 + 8), t166);
        t5 = t173;
        goto me6;
mn163:;
ma10:;
        uint8_t t175 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t175 != ((uint8_t)5ULL)) goto mn174;
        gm_b16_8 t176 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l10 = t176;
        gm_b16_8 t177 = gm_l10;
        gm_tbo4aa19b457h t178;
        memset(&t178, 0, sizeof t178);
        GM_ST(uint8_t, ((uint8_t *)&t178 + 0), ((uint8_t)2ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t178 + 8), t177);
        t5 = t178;
        goto me6;
mn174:;
ma11:;
        uint8_t t180 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t180 != ((uint8_t)6ULL)) goto mn179;
        gm_b16_8 t181 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l11 = t181;
        gm_b16_8 t182 = gm_l11;
        gm_tbo4aa19b457h t183;
        memset(&t183, 0, sizeof t183);
        GM_ST(uint8_t, ((uint8_t *)&t183 + 0), ((uint8_t)3ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t183 + 8), t182);
        t5 = t183;
        goto me6;
mn179:;
ma12:;
        uint8_t t185 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t185 != ((uint8_t)7ULL)) goto mn184;
        void * t186 = gm_l2;
        void * t187 = gm_l2;
        void * t188 = gm_l1;
        gm_t88rcucg1b7uk t189 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t188) + 0));
        uint64_t t190 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t187, t189);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t191 = gm_f_us_cuv01fp(t190);
        gm_tabb710d9mq2e t192;
        gm_f_make_fill_jpvm0kc((void *)&t192, (void *)NULL, cx, NULL, t191, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8ao9fftopr)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t193 = t192;
        if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 8, gm_src7afujsggtlje, 293, 30);
        gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&t193) + 8));
        gm_tbo4aa19b457h t195;
        gm_f_const_op_r0qgpnv((void *)&t195, t186, t194);
        t5 = t195;
        goto me6;
mn184:;
ma13:;
        uint8_t t197 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t197 != ((uint8_t)8ULL)) goto mn196;
        gm_tbo4aa19b457h t198;
        memset(&t198, 0, sizeof t198);
        GM_ST(uint8_t, ((uint8_t *)&t198 + 0), ((uint8_t)5ULL));
        t5 = t198;
        goto me6;
mn196:;
ma14:;
        uint8_t t200 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t200 != ((uint8_t)15ULL)) goto mn199;
        uint32_t t201 = GM_LD(uint32_t, ((((uint8_t *)t4) + 8) + 8));
        gm_l12 = t201;
        uint32_t t202 = gm_l12;
        void * t203 = gm_l0;
        void * t204 = GM_LD(void *, (((uint8_t *)t203) + 8));
        gm_b16_8 t205 = GM_LD(gm_b16_8, (((uint8_t *)t204) + 72));
        gm_b16_8 t206 = t205;
        uint32_t t207 = gm_l12;
        uint64_t t208 = (uint64_t)t207;
        uint64_t t209 = gm_f_us_cuv01fp(t208);
        uint64_t t210 = (uintptr_t)t209;
        gm_b16_8 t211 = t206;
        uint64_t t212 = GM_LD(uintptr_t, ((uint8_t *)&t211) + 8);
        if (t210 >= (uintptr_t)(t212)) gm_trap_index(t210, t212, gm_src7afujsggtlje, 295, 32);
        uint64_t t213 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t211)) + t210 * 8));
        gm_tb31k5l794t7p t214;
        GM_ST(uint32_t, (uint8_t *)&t214 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t214 + 0), t202);
        GM_ST(uint64_t, ((uint8_t *)&t214 + 8), t213);
        gm_tbo4aa19b457h t215;
        memset(&t215, 0, sizeof t215);
        GM_ST(uint8_t, ((uint8_t *)&t215 + 0), ((uint8_t)6ULL));
        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t215 + 8), t214);
        t5 = t215;
        goto me6;
mn199:;
ma15:;
        uint8_t t217 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t217 != ((uint8_t)16ULL)) goto mn216;
        uint32_t t218 = GM_LD(uint32_t, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l13 = t218;
        uint32_t t219 = GM_LD(uint32_t, (((((uint8_t *)t4) + 8) + 8) + 4));
        gm_l14 = t219;
        uint32_t t220 = gm_l13;
        uint32_t t221 = gm_l14;
        gm_tcc1rq4n1avb2 t222;
        GM_ST(uint32_t, ((uint8_t *)&t222 + 0), t220);
        GM_ST(uint32_t, ((uint8_t *)&t222 + 4), t221);
        gm_tbo4aa19b457h t223;
        memset(&t223, 0, sizeof t223);
        GM_ST(uint8_t, ((uint8_t *)&t223 + 0), ((uint8_t)7ULL));
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t223 + 8), t222);
        t5 = t223;
        goto me6;
mn216:;
ma16:;
        uint8_t t225 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t225 != ((uint8_t)17ULL)) goto mn224;
        uint64_t t226 = GM_LD(uint64_t, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l15 = t226;
        uint64_t t227 = gm_l15;
        gm_tbo4aa19b457h t228;
        memset(&t228, 0, sizeof t228);
        GM_ST(uint8_t, ((uint8_t *)&t228 + 0), ((uint8_t)8ULL));
        GM_ST(uint64_t, ((uint8_t *)&t228 + 8), t227);
        t5 = t228;
        goto me6;
mn224:;
ma17:;
        uint8_t t230 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t230 != ((uint8_t)14ULL)) goto mn229;
        gm_tl874t2atnlh3 t231 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l16 = t231;
        gm_tl874t2atnlh3 t232 = gm_l16;
        gm_tbo4aa19b457h t233;
        memset(&t233, 0, sizeof t233);
        GM_ST(uint8_t, ((uint8_t *)&t233 + 0), ((uint8_t)4ULL));
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t233 + 8), t232);
        t5 = t233;
        goto me6;
mn229:;
ma18:;
        uint8_t t235 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t235 != ((uint8_t)18ULL)) goto mn234;
        void * t236 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l17 = t236;
        void * t237 = gm_l0;
        void * t238 = gm_l17;
        void * t239 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t237, t238);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t240 = gm_l2;
        void * t241 = gm_l1;
        gm_t88rcucg1b7uk t242 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t241) + 0));
        uint64_t t243 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t240, t242);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttf6i6v1da8cm t244;
        GM_ST(void *, ((uint8_t *)&t244 + 0), t239);
        GM_ST(uint64_t, ((uint8_t *)&t244 + 8), t243);
        gm_tbo4aa19b457h t245;
        memset(&t245, 0, sizeof t245);
        GM_ST(uint8_t, ((uint8_t *)&t245 + 0), ((uint8_t)9ULL));
        GM_ST(gm_ttf6i6v1da8cm, ((uint8_t *)&t245 + 8), t244);
        t5 = t245;
        goto me6;
mn234:;
ma19:;
        uint8_t t247 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t247 != ((uint8_t)19ULL)) goto mn246;
        void * t248 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l18 = t248;
        uint32_t t249 = GM_LD(uint32_t, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l19 = t249;
        {
            void * t251 = gm_l0;
            void * t252 = gm_l18;
            void * t253 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t251, t252);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t253;
        }
        gm_tbo4aa19b457h t254;
        void * t256 = gm_l2;
        void * t257 = gm_l18;
        gm_t88rcucg1b7uk t258 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t257) + 0));
        gm_tkn42u7na3t81 t259 = gm_f_bits_struct_ty_22ue20s(t256, GM_CONV(gm_csguankko4lmhd, t258));
        gm_tkn42u7na3t81 t260 = t259;
        uint8_t t261;
        if (((((uint8_t *)&t260))[4] != 0)) goto mn263;
        t261 = ((uint8_t)0);
        goto me262;
mn263:;
        t261 = ((uint8_t)1);
        goto me262;
mn264:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 302, 16);
me262:;
        if (!t261) goto ie255;
        {
            void * t266 = gm_l2;
            void * t267 = gm_l18;
            gm_t88rcucg1b7uk t268 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t267) + 0));
            uint32_t t269 = gm_l19;
            gm_tplpbqf3duvn3 t270 = gm_f_bits_field_vqua3kd((void *)NULL, cx, NULL, t266, t268, t269);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l21 = t270;
        }
        gm_tdvcgvrf81sbd t271;
        memset((uint8_t *)&t271 + 20, 0, 4);
        void * t272 = gm_l20;
        GM_ST(void *, (uint8_t *)&t271 + 0, t272);
        uint32_t t273 = gm_l19;
        GM_ST(uint32_t, (uint8_t *)&t271 + 8, t273);
        uint32_t t274 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 0));
        GM_ST(uint32_t, (uint8_t *)&t271 + 12, t274);
        gm_t88rcucg1b7uk t275 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l21) + 4));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t271 + 16, t275);
        gm_tbo4aa19b457h t276;
        memset(&t276, 0, sizeof t276);
        GM_ST(uint8_t, ((uint8_t *)&t276 + 0), ((uint8_t)11ULL));
        GM_ST(gm_tdvcgvrf81sbd, ((uint8_t *)&t276 + 8), t271);
        gm_tbo4aa19b457h t277 = t276;
        t254 = t277;
        goto ix255;
ie255:;
        gm_t1j70din2upc4 t279;
        memset((uint8_t *)&t279 + 12, 0, 4);
        memset((uint8_t *)&t279 + 25, 0, 7);
        void * t280 = gm_l20;
        GM_ST(void *, (uint8_t *)&t279 + 0, t280);
        uint32_t t281 = gm_l19;
        GM_ST(uint32_t, (uint8_t *)&t279 + 8, t281);
        void * t282 = gm_l2;
        void * t283 = gm_l18;
        gm_t88rcucg1b7uk t284 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t283) + 0));
        uint32_t t285 = gm_l19;
        gm_tfbnciphf3261 t286 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t282, t284, t285);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tfbnciphf3261 t287 = t286;
        uint64_t t288 = GM_LD(uint64_t, (((uint8_t *)&t287) + 8));
        GM_ST(uint64_t, (uint8_t *)&t279 + 16, t288);
        void * t289 = gm_l2;
        void * t290 = gm_l18;
        gm_t88rcucg1b7uk t291 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t290) + 0));
        uint8_t t292 = gm_f_is_packed_8fhislp(t289, GM_CONV(gm_csguankko4lmhd, t291));
        GM_ST(uint8_t, (uint8_t *)&t279 + 24, t292);
        gm_tbo4aa19b457h t293;
        memset(&t293, 0, sizeof t293);
        GM_ST(uint8_t, ((uint8_t *)&t293 + 0), ((uint8_t)10ULL));
        GM_ST(gm_t1j70din2upc4, ((uint8_t *)&t293 + 8), t279);
        gm_tbo4aa19b457h t294 = t293;
        t254 = t294;
ix255:;
        gm_tbo4aa19b457h t295 = t254;
        t5 = t295;
        goto me6;
mn246:;
ma20:;
        uint8_t t297 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t297 != ((uint8_t)20ULL)) goto mn296;
        void * t298 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l22 = t298;
        void * t299 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l23 = t299;
        {
            void * t301 = gm_l2;
            void * t302 = gm_l22;
            gm_t88rcucg1b7uk t303 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t302) + 0));
            gm_thdrvp9q0u7b5 t304;
            gm_f_seq_kind_405ip0f((void *)&t304, (void *)NULL, cx, NULL, t301, t303);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thdrvp9q0u7b5 t305 = t304;
            gm_te0mgnjdphs16 t306;
            if (((((uint8_t *)&t305))[16] != 0)) {
                gm_te0mgnjdphs16 t307 = GM_LD(gm_te0mgnjdphs16, (((uint8_t *)&t305) + 0));
                t306 = t307;
            } else {
                gm_t9qkh0rsooc8d t308;
                GM_ST(uint8_t, ((uint8_t *)&t308 + 0), ((uint8_t)1ULL));
                gm_te0mgnjdphs16 t309;
                GM_ST(uint8_t, (uint8_t *)&t309 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t309 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t309 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t309 + 0), t308);
                GM_ST(uint64_t, ((uint8_t *)&t309 + 8), ((uint64_t)0ULL));
                t306 = t309;
            }
            gm_l24 = t306;
        }
        gm_tp0fa5qv7rqnn t310;
        memset((uint8_t *)&t310 + 17, 0, 7);
        void * t311 = gm_l0;
        void * t312 = gm_l22;
        void * t313 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t311, t312);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t310 + 0, t313);
        void * t314 = gm_l0;
        void * t315 = gm_l23;
        void * t316 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t314, t315);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t310 + 8, t316);
        gm_t9qkh0rsooc8d t317 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l24) + 0));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t310 + 16, t317);
        void * t318 = gm_l2;
        void * t319 = gm_l1;
        gm_t88rcucg1b7uk t320 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t319) + 0));
        uint64_t t321 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t318, t320);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint64_t, (uint8_t *)&t310 + 24, t321);
        uint64_t t322 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
        GM_ST(uint64_t, (uint8_t *)&t310 + 32, t322);
        gm_tbo4aa19b457h t323;
        memset(&t323, 0, sizeof t323);
        GM_ST(uint8_t, ((uint8_t *)&t323 + 0), ((uint8_t)12ULL));
        GM_ST(gm_tp0fa5qv7rqnn, ((uint8_t *)&t323 + 8), t310);
        gm_tbo4aa19b457h t324 = t323;
        t5 = t324;
        goto me6;
mn296:;
ma21:;
        uint8_t t326 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t326 != ((uint8_t)21ULL)) goto mn325;
        gm_tao63pk3r18v5 t327 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t4) + 8) + 8));
        gm_l25 = t327;
        {
            void * t329 = gm_l2;
            void * t330 = GM_LD(void *, (((uint8_t *)&gm_l25) + 0));
            gm_t88rcucg1b7uk t331 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t330) + 0));
            gm_thdrvp9q0u7b5 t332;
            gm_f_seq_kind_405ip0f((void *)&t332, (void *)NULL, cx, NULL, t329, t331);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thdrvp9q0u7b5 t333 = t332;
            gm_te0mgnjdphs16 t334;
            if (((((uint8_t *)&t333))[16] != 0)) {
                gm_te0mgnjdphs16 t335 = GM_LD(gm_te0mgnjdphs16, (((uint8_t *)&t333) + 0));
                t334 = t335;
            } else {
                gm_t9qkh0rsooc8d t336;
                GM_ST(uint8_t, ((uint8_t *)&t336 + 0), ((uint8_t)1ULL));
                gm_te0mgnjdphs16 t337;
                GM_ST(uint8_t, (uint8_t *)&t337 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t337 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t337 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t337 + 0), t336);
                GM_ST(uint64_t, ((uint8_t *)&t337 + 8), ((uint64_t)0ULL));
                t334 = t337;
            }
            gm_l26 = t334;
        }
        {
            gm_l27 = ((uint64_t)0ULL);
        }
        {
            void * t339 = gm_l2;
            void * t340 = gm_l1;
            gm_t88rcucg1b7uk t341 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t340) + 0));
            gm_t53at80pfdus3 t342;
            gm_f_kind_htv6093((void *)&t342, t339, GM_CONV(gm_csguankko4lmhd, t341));
            gm_t53at80pfdus3 t343 = t342;
            uint8_t t344 = GM_LD(uint8_t, (((uint8_t *)&t343) + 0));
            if (t344 != ((uint8_t)10ULL)) goto ie338;
            gm_t88rcucg1b7uk t345 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t343) + 8) + 4));
            gm_l28 = t345;
            {
                void * t347 = gm_l2;
                gm_t88rcucg1b7uk t348 = gm_l28;
                uint64_t t349 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t347, t348);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l27 = t349;
            }
            goto ix338;
ie338:;
ix338:;
        }
        gm_tt49maei14cl6 t350;
        memset((uint8_t *)&t350 + 26, 0, 6);
        void * t351 = gm_l0;
        void * t352 = GM_LD(void *, (((uint8_t *)&gm_l25) + 0));
        void * t353 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t351, t352);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t350 + 0, t353);
        void * t354 = gm_l0;
        void * t355 = GM_LD(void *, (((uint8_t *)&gm_l25) + 8));
        void * t356 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t354, t355);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t350 + 8, t356);
        void * t357 = gm_l0;
        void * t358 = GM_LD(void *, (((uint8_t *)&gm_l25) + 16));
        void * t359 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t357, t358);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t350 + 16, t359);
        uint8_t t360 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 24));
        GM_ST(uint8_t, (uint8_t *)&t350 + 24, t360);
        gm_t9qkh0rsooc8d t361 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l26) + 0));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t350 + 25, t361);
        uint64_t t362 = gm_l27;
        GM_ST(uint64_t, (uint8_t *)&t350 + 32, t362);
        uint64_t t363 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
        GM_ST(uint64_t, (uint8_t *)&t350 + 40, t363);
        gm_tbo4aa19b457h t364;
        memset(&t364, 0, sizeof t364);
        GM_ST(uint8_t, ((uint8_t *)&t364 + 0), ((uint8_t)13ULL));
        GM_ST(gm_tt49maei14cl6, ((uint8_t *)&t364 + 8), t350);
        gm_tbo4aa19b457h t365 = t364;
        t5 = t365;
        goto me6;
mn325:;
ma22:;
        uint8_t t367 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t367 != ((uint8_t)22ULL)) goto mn366;
        void * t368 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l29 = t368;
        void * t369 = gm_l0;
        void * t370 = gm_l29;
        void * t371 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t369, t370);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t372;
        memset(&t372, 0, sizeof t372);
        GM_ST(uint8_t, ((uint8_t *)&t372 + 0), ((uint8_t)14ULL));
        GM_ST(void *, ((uint8_t *)&t372 + 8), t371);
        t5 = t372;
        goto me6;
mn366:;
ma23:;
        uint8_t t374 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t374 != ((uint8_t)23ULL)) goto mn373;
        void * t375 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l30 = t375;
        void * t376 = gm_l0;
        void * t377 = gm_l30;
        void * t378 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t376, t377);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t379;
        memset(&t379, 0, sizeof t379);
        GM_ST(uint8_t, ((uint8_t *)&t379 + 0), ((uint8_t)15ULL));
        GM_ST(void *, ((uint8_t *)&t379 + 8), t378);
        t5 = t379;
        goto me6;
mn373:;
ma24:;
        uint8_t t381 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t381 != ((uint8_t)24ULL)) goto mn380;
        void * t382 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l31 = t382;
        void * t383 = gm_l0;
        void * t384 = gm_l31;
        void * t385 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t383, t384);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t386;
        memset(&t386, 0, sizeof t386);
        GM_ST(uint8_t, ((uint8_t *)&t386 + 0), ((uint8_t)16ULL));
        GM_ST(void *, ((uint8_t *)&t386 + 8), t385);
        t5 = t386;
        goto me6;
mn380:;
ma25:;
        uint8_t t388 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t388 != ((uint8_t)25ULL)) goto mn387;
        void * t389 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l32 = t389;
        void * t390 = gm_l0;
        void * t391 = gm_l32;
        void * t392 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t390, t391);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t393 = gm_l2;
        void * t394 = gm_l32;
        gm_t88rcucg1b7uk t395 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t394) + 0));
        uint64_t t396 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t393, t395);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttf6i6v1da8cm t397;
        GM_ST(void *, ((uint8_t *)&t397 + 0), t392);
        GM_ST(uint64_t, ((uint8_t *)&t397 + 8), t396);
        gm_tbo4aa19b457h t398;
        memset(&t398, 0, sizeof t398);
        GM_ST(uint8_t, ((uint8_t *)&t398 + 0), ((uint8_t)17ULL));
        GM_ST(gm_ttf6i6v1da8cm, ((uint8_t *)&t398 + 8), t397);
        t5 = t398;
        goto me6;
mn387:;
ma26:;
        uint8_t t400 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t400 != ((uint8_t)26ULL)) goto mn399;
        uint32_t t401 = GM_LD(uint32_t, ((((uint8_t *)t4) + 8) + 8));
        gm_l33 = t401;
        uint32_t t402 = gm_l33;
        void * t403 = gm_l2;
        void * t404 = gm_l1;
        gm_t88rcucg1b7uk t405 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t404) + 0));
        gm_t53at80pfdus3 t406;
        gm_f_kind_htv6093((void *)&t406, t403, GM_CONV(gm_csguankko4lmhd, t405));
        gm_t53at80pfdus3 t407 = t406;
        uint8_t t408;
        uint8_t t411 = GM_LD(uint8_t, (((uint8_t *)&t407) + 0));
        if (t411 != ((uint8_t)15ULL)) goto mn410;
        t408 = ((uint8_t)1);
        goto me409;
mn410:;
        t408 = ((uint8_t)0);
        goto me409;
mn412:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 323, 32);
me409:;
        gm_t9805qtonbsag t413;
        GM_ST(uint8_t, (uint8_t *)&t413 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t413 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t413 + 0), t402);
        GM_ST(uint8_t, ((uint8_t *)&t413 + 4), t408);
        gm_tbo4aa19b457h t414;
        memset(&t414, 0, sizeof t414);
        GM_ST(uint8_t, ((uint8_t *)&t414 + 0), ((uint8_t)18ULL));
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t414 + 8), t413);
        t5 = t414;
        goto me6;
mn399:;
ma27:;
        uint8_t t416 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t416 != ((uint8_t)27ULL)) goto mn415;
        uint32_t t417 = GM_LD(uint32_t, ((((uint8_t *)t4) + 8) + 8));
        gm_l34 = t417;
        uint32_t t418 = gm_l34;
        gm_tbo4aa19b457h t419;
        memset(&t419, 0, sizeof t419);
        GM_ST(uint8_t, ((uint8_t *)&t419 + 0), ((uint8_t)19ULL));
        GM_ST(uint32_t, ((uint8_t *)&t419 + 8), t418);
        t5 = t419;
        goto me6;
mn415:;
ma28:;
        uint8_t t421 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t421 != ((uint8_t)28ULL)) goto mn420;
        uint32_t t422 = GM_LD(uint32_t, ((((uint8_t *)t4) + 8) + 8));
        gm_l35 = t422;
        uint32_t t423 = gm_l35;
        gm_tbo4aa19b457h t424;
        memset(&t424, 0, sizeof t424);
        GM_ST(uint8_t, ((uint8_t *)&t424 + 0), ((uint8_t)20ULL));
        GM_ST(uint32_t, ((uint8_t *)&t424 + 8), t423);
        t5 = t424;
        goto me6;
mn420:;
ma29:;
        uint8_t t426 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t426 != ((uint8_t)29ULL)) goto mn425;
        gm_t9qkh0rsooc8d t427 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l36 = t427;
        void * t428 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l37 = t428;
        void * t429 = gm_l0;
        gm_t9qkh0rsooc8d t430 = gm_l36;
        void * t431 = gm_l37;
        void * t432 = gm_l1;
        gm_tbo4aa19b457h t433;
        gm_f_low_unary_3f5k6s5((void *)&t433, (void *)NULL, cx, NULL, t429, t430, t431, t432);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t5 = t433;
        goto me6;
mn425:;
ma30:;
        uint8_t t435 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t435 != ((uint8_t)30ULL)) goto mn434;
        gm_ts6oqppjh0ba3 t436 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l38 = t436;
        void * t437 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l39 = t437;
        void * t438 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l40 = t438;
        void * t439 = gm_l0;
        gm_ts6oqppjh0ba3 t440 = gm_l38;
        void * t441 = gm_l39;
        void * t442 = gm_l40;
        void * t443 = gm_l1;
        gm_tbo4aa19b457h t444;
        gm_f_low_binary_5dhmccv((void *)&t444, (void *)NULL, cx, NULL, t439, t440, t441, t442, t443);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t5 = t444;
        goto me6;
mn434:;
ma31:;
        uint8_t t446 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t446 != ((uint8_t)31ULL)) goto mn445;
        uint8_t t447 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l41 = t447;
        void * t448 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l42 = t448;
        void * t449 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l43 = t449;
        gm_ta612fil812pm t450;
        memset((uint8_t *)&t450 + 1, 0, 7);
        uint8_t t451 = gm_l41;
        GM_ST(uint8_t, (uint8_t *)&t450 + 0, t451);
        void * t452 = gm_l0;
        void * t453 = gm_l42;
        void * t454 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t452, t453);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t450 + 8, t454);
        void * t455 = gm_l0;
        void * t456 = gm_l43;
        void * t457 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t455, t456);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t450 + 16, t457);
        gm_tbo4aa19b457h t458;
        memset(&t458, 0, sizeof t458);
        GM_ST(uint8_t, ((uint8_t *)&t458 + 0), ((uint8_t)32ULL));
        GM_ST(gm_ta612fil812pm, ((uint8_t *)&t458 + 8), t450);
        t5 = t458;
        goto me6;
mn445:;
ma32:;
        uint8_t t460 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t460 != ((uint8_t)32ULL)) goto mn459;
        gm_tmn7n0n9raou5 t461 = GM_LD(gm_tmn7n0n9raou5, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l44 = t461;
        void * t462 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l45 = t462;
        void * t463 = gm_l0;
        gm_tmn7n0n9raou5 t464 = gm_l44;
        void * t465 = gm_l45;
        void * t466 = gm_l1;
        gm_tbo4aa19b457h t467;
        gm_f_low_coerce_rhvh5p0((void *)&t467, (void *)NULL, cx, NULL, t463, t464, t465, t466);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t5 = t467;
        goto me6;
mn459:;
ma33:;
        uint8_t t469 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t469 != ((uint8_t)33ULL)) goto mn468;
        gm_tvdg94bk7aa22 t470 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l46 = t470;
        gm_b16_8 t471 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l47 = t471;
        void * t472 = gm_l0;
        gm_tvdg94bk7aa22 t473 = gm_l46;
        gm_b16_8 t474 = gm_l47;
        void * t475 = gm_l1;
        gm_tbo4aa19b457h t476;
        gm_f_low_builtin_rbo6661((void *)&t476, (void *)NULL, cx, NULL, t472, t473, t474, t475);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t5 = t476;
        goto me6;
mn468:;
ma34:;
        uint8_t t478 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t478 != ((uint8_t)34ULL)) goto mn477;
        gm_tsth9qu1dv0g3 t479 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t4) + 8) + 8) + 0));
        F.l48 = t479;
        gm_b16_8 t480 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 8) + 8) + 40));
        gm_l49 = t480;
        gm_tbo4aa19b457h t481;
        uint8_t t487 = GM_LD(uint8_t, (((uint8_t *)&F.l48) + 0));
        switch (t487) { case ((uint8_t)0ULL): goto ma483; case ((uint8_t)1ULL): goto ma484; case ((uint8_t)2ULL): goto ma485; default: goto ma486; }
ma483:;
        uint8_t t489 = GM_LD(uint8_t, (((uint8_t *)&F.l48) + 0));
        if (t489 != ((uint8_t)0ULL)) goto mn488;
        uint32_t t490 = GM_LD(uint32_t, (((uint8_t *)&F.l48) + 8));
        gm_l50 = t490;
        gm_tiilmfd16v3fc t491;
        memset((uint8_t *)&t491 + 4, 0, 4);
        uint32_t t492 = gm_l50;
        GM_ST(uint32_t, (uint8_t *)&t491 + 0, t492);
        void * t493 = gm_l0;
        gm_b16_8 t494 = gm_l49;
        gm_b16_8 t495 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t493, t494);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t491 + 8, t495);
        gm_tbo4aa19b457h t496;
        memset(&t496, 0, sizeof t496);
        GM_ST(uint8_t, ((uint8_t *)&t496 + 0), ((uint8_t)35ULL));
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t496 + 8), t491);
        t481 = t496;
        goto me482;
mn488:;
ma484:;
        uint8_t t498 = GM_LD(uint8_t, (((uint8_t *)&F.l48) + 0));
        if (t498 != ((uint8_t)1ULL)) goto mn497;
        gm_tiilmfd16v3fc t499 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)&F.l48) + 8) + 0));
        gm_l51 = t499;
        uint32_t t500 = GM_LD(uint32_t, ((((uint8_t *)&F.l48) + 8) + 24));
        gm_l52 = t500;
        gm_tf4r0415i49d1 t501;
        memset((uint8_t *)&t501 + 28, 0, 4);
        memcpy((uint8_t *)&t501 + 0, ((uint8_t *)&gm_l51), 24);
        uint32_t t502 = gm_l52;
        GM_ST(uint32_t, (uint8_t *)&t501 + 24, t502);
        void * t503 = gm_l0;
        gm_b16_8 t504 = gm_l49;
        gm_b16_8 t505 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t503, t504);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t501 + 32, t505);
        gm_tbo4aa19b457h t506;
        memset(&t506, 0, sizeof t506);
        GM_ST(uint8_t, ((uint8_t *)&t506 + 0), ((uint8_t)36ULL));
        GM_ST(gm_tf4r0415i49d1, ((uint8_t *)&t506 + 8), t501);
        t481 = t506;
        goto me482;
mn497:;
ma485:;
        uint8_t t508 = GM_LD(uint8_t, (((uint8_t *)&F.l48) + 0));
        if (t508 != ((uint8_t)2ULL)) goto mn507;
        void * t509 = GM_LD(void *, (((uint8_t *)&F.l48) + 8));
        gm_l53 = t509;
        {
            void * t511 = gm_l0;
            void * t512 = gm_l53;
            void * t513 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t511, t512);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l54 = t513;
        }
        gm_tkaapcii86udk t514;
        void * t515 = gm_l54;
        GM_ST(void *, (uint8_t *)&t514 + 0, t515);
        void * t516 = gm_l0;
        gm_b16_8 t517 = gm_l49;
        gm_b16_8 t518 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t516, t517);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t514 + 8, t518);
        gm_tbo4aa19b457h t519;
        memset(&t519, 0, sizeof t519);
        GM_ST(uint8_t, ((uint8_t *)&t519 + 0), ((uint8_t)37ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t519 + 8), t514);
        gm_tbo4aa19b457h t520 = t519;
        t481 = t520;
        goto me482;
mn507:;
ma486:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 331, 32);
me482:;
        t5 = t481;
        goto me6;
mn477:;
ma35:;
        uint8_t t522 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t522 != ((uint8_t)35ULL)) goto mn521;
        gm_b16_8 t523 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l55 = t523;
        gm_tbo4aa19b457h t525;
        void * t527 = gm_l2;
        void * t528 = gm_l1;
        gm_t88rcucg1b7uk t529 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t528) + 0));
        gm_tkn42u7na3t81 t530 = gm_f_bits_struct_ty_22ue20s(t527, GM_CONV(gm_csguankko4lmhd, t529));
        gm_tkn42u7na3t81 t531 = t530;
        uint8_t t532;
        if (((((uint8_t *)&t531))[4] != 0)) goto mn534;
        t532 = ((uint8_t)0);
        goto me533;
mn534:;
        t532 = ((uint8_t)1);
        goto me533;
mn535:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 340, 16);
me533:;
        if (!t532) goto ie526;
        void * t537 = gm_l0;
        gm_b16_8 t538 = gm_l55;
        gm_b16_8 t539 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t537, t538);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t540;
        memset(&t540, 0, sizeof t540);
        GM_ST(uint8_t, ((uint8_t *)&t540 + 0), ((uint8_t)40ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t540 + 8), t539);
        gm_tbo4aa19b457h t541 = t540;
        t525 = t541;
        goto ix526;
ie526:;
        gm_tbo4aa19b457h t542;
        void * t544 = gm_l2;
        void * t545 = gm_l1;
        gm_t88rcucg1b7uk t546 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t545) + 0));
        void * t547 = gm_f_adt_of_8k5vqud(t544, GM_CONV(gm_csguankko4lmhd, t546));
        void * t548 = t547;
        if (!(GM_LD(void *, ((uint8_t *)&t548)) != 0)) goto ie543;
        void * t549 = GM_LD(void *, ((uint8_t *)&t548));
        gm_l56 = t549;
        void * t550 = gm_l56;
        uint8_t t551;
        uint8_t t554 = GM_LD(uint8_t, ((((uint8_t *)t550) + 24) + 0));
        if (t554 != ((uint8_t)2ULL)) goto mn553;
        t551 = ((uint8_t)1);
        goto me552;
mn553:;
        t551 = ((uint8_t)0);
        goto me552;
mn555:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 342, 48);
me552:;
        if (!t551) goto ie543;
        void * t557;
        uint64_t t559 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
        uint8_t t560 = (t559 > ((uint64_t)0ULL));
        if (!t560) goto ie558;
        void * t562 = gm_l0;
        uint64_t t563 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t564 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
        if (t563 >= (uintptr_t)(t564)) gm_trap_index(t563, t564, gm_src7afujsggtlje, 343, 60);
        void * t565 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l55)) + t563 * 88);
        void * t566 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t562, t565);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t567 = t566;
        t557 = t567;
        goto ix558;
ie558:;
        void * t569 = ((void *)0);
        t557 = t569;
ix558:;
        gm_tbo4aa19b457h t570;
        memset(&t570, 0, sizeof t570);
        GM_ST(uint8_t, ((uint8_t *)&t570 + 0), ((uint8_t)39ULL));
        GM_ST(void *, ((uint8_t *)&t570 + 8), t557);
        gm_tbo4aa19b457h t571 = t570;
        t542 = t571;
        goto ix543;
ie543:;
        {
            uint64_t t573 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
            gm_tabb710d9mq2e t574;
            gm_f_make_phdnqpj((void *)&t574, (void *)NULL, cx, NULL, t573, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krt2fusbrlm)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t575 = t574;
            if (((((uint8_t *)&t575))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t575) + 8, gm_src7afujsggtlje, 345, 25);
            gm_b16_8 t576 = GM_LD(gm_b16_8, (((uint8_t *)&t575) + 8));
            gm_l57 = t576;
        }
        {
            {
                gm_l59 = ((uint64_t)0ULL);
            }
            {
                uint64_t t578 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                gm_l58 = t578;
            }
            {
                for (;;) {
                    uint64_t t580 = gm_l59;
                    uint64_t t581 = gm_l58;
                    uint8_t t582 = (t580 < t581);
                    if (!t582) goto lb579;
                    {
                        uint64_t t585 = gm_l59;
                        uint64_t t586 = t585;
                        uint64_t t589 = GM_LD(uint64_t, ((uint8_t *)&t586));
                        gm_l60 = t589;
                        {
                            uint64_t t591 = gm_l60;
                            uint64_t t592 = (uintptr_t)t591;
                            uint64_t t593 = GM_LD(uintptr_t, ((uint8_t *)&gm_l57) + 8);
                            if (t592 >= (uintptr_t)(t593)) gm_trap_index(t592, t593, gm_src7afujsggtlje, 346, 41);
                            uint8_t *t594 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l57)) + t592 * 8);
                            void * t595 = gm_l2;
                            void * t596 = gm_l1;
                            gm_t88rcucg1b7uk t597 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t596) + 0));
                            uint64_t t598 = gm_l60;
                            uint32_t t599 = (uint32_t)t598;
                            gm_tfbnciphf3261 t600 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t595, t597, t599);
                            if (gm_unw.kind) {
                                goto gm_ll583;
                            }
                            gm_tfbnciphf3261 t601 = t600;
                            uint64_t t602 = GM_LD(uint64_t, (((uint8_t *)&t601) + 8));
                            GM_ST(uint64_t, t594, t602);
                        }
                        goto me587;
mn588:;
                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 346, 17);
me587:;
                    }
lc579:;
                    {
                        uint64_t t604 = gm_l59;
                        uint64_t r605; bool o606 = __builtin_add_overflow(t604, ((uint64_t)1ULL), &r605);
                        uint64_t t607 = r605;
                        gm_l59 = t607;
                    }
                }
gm_ll583:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb579; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc579; }
                }
                goto gm_lad;
lb579:;
            }
        }
        gm_t6j6smj7qoctu t608;
        void * t609 = gm_l0;
        gm_b16_8 t610 = gm_l55;
        gm_b16_8 t611 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t609, t610);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t608 + 0, t611);
        gm_b16_8 t612 = gm_l57;
        GM_ST(gm_b16_8, (uint8_t *)&t608 + 16, t612);
        gm_tbo4aa19b457h t613;
        memset(&t613, 0, sizeof t613);
        GM_ST(uint8_t, ((uint8_t *)&t613 + 0), ((uint8_t)38ULL));
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t613 + 8), t608);
        gm_tbo4aa19b457h t614 = t613;
        t542 = t614;
ix543:;
        t525 = t542;
ix526:;
        gm_tbo4aa19b457h t615 = t525;
        t5 = t615;
        goto me6;
mn521:;
ma36:;
        uint8_t t617 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t617 != ((uint8_t)36ULL)) goto mn616;
        gm_b16_8 t618 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l61 = t618;
        void * t619 = gm_l0;
        gm_b16_8 t620 = gm_l61;
        gm_b16_8 t621 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t619, t620);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t622;
        memset(&t622, 0, sizeof t622);
        GM_ST(uint8_t, ((uint8_t *)&t622 + 0), ((uint8_t)41ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t622 + 8), t621);
        t5 = t622;
        goto me6;
mn616:;
ma37:;
        uint8_t t624 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t624 != ((uint8_t)37ULL)) goto mn623;
        void * t625 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l62 = t625;
        void * t626 = gm_l0;
        void * t627 = gm_l62;
        void * t628 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t626, t627);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t629;
        memset(&t629, 0, sizeof t629);
        GM_ST(uint8_t, ((uint8_t *)&t629 + 0), ((uint8_t)42ULL));
        GM_ST(void *, ((uint8_t *)&t629 + 8), t628);
        t5 = t629;
        goto me6;
mn623:;
ma38:;
        uint8_t t631 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t631 != ((uint8_t)38ULL)) goto mn630;
        gm_ttggq7ng4kp3p t632 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t4) + 8) + 8));
        gm_l63 = t632;
        uint32_t t633 = GM_LD(uint32_t, (((uint8_t *)&gm_l63) + 0));
        void * t634 = gm_l0;
        void * t635 = GM_LD(void *, (((uint8_t *)&gm_l63) + 8));
        void * t636 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t634, t635);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttggq7ng4kp3p t637;
        GM_ST(uint32_t, (uint8_t *)&t637 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t637 + 0), t633);
        GM_ST(void *, ((uint8_t *)&t637 + 8), t636);
        gm_tbo4aa19b457h t638;
        memset(&t638, 0, sizeof t638);
        GM_ST(uint8_t, ((uint8_t *)&t638 + 0), ((uint8_t)43ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t638 + 8), t637);
        t5 = t638;
        goto me6;
mn630:;
ma39:;
        uint8_t t640 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t640 != ((uint8_t)39ULL)) goto mn639;
        void * t641 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l64 = t641;
        void * t642 = gm_l0;
        void * t643 = gm_l64;
        void * t644 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t642, t643);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t645;
        memset(&t645, 0, sizeof t645);
        GM_ST(uint8_t, ((uint8_t *)&t645 + 0), ((uint8_t)44ULL));
        GM_ST(void *, ((uint8_t *)&t645 + 8), t644);
        t5 = t645;
        goto me6;
mn639:;
ma40:;
        uint8_t t647 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t647 != ((uint8_t)40ULL)) goto mn646;
        void * t648 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l65 = t648;
        uint8_t t649 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l66 = t649;
        void * t650 = gm_l0;
        void * t651 = gm_l65;
        void * t652 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t650, t651);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t653 = gm_l66;
        gm_tsplfr33dsvji t654;
        GM_ST(uint8_t, (uint8_t *)&t654 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t654 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t654 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t654 + 0), t652);
        GM_ST(uint8_t, ((uint8_t *)&t654 + 8), t653);
        gm_tbo4aa19b457h t655;
        memset(&t655, 0, sizeof t655);
        GM_ST(uint8_t, ((uint8_t *)&t655 + 0), ((uint8_t)45ULL));
        GM_ST(gm_tsplfr33dsvji, ((uint8_t *)&t655 + 8), t654);
        t5 = t655;
        goto me6;
mn646:;
ma41:;
        uint8_t t657 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t657 != ((uint8_t)41ULL)) goto mn656;
        gm_t3dcoc7077t1q t658 = GM_LD(gm_t3dcoc7077t1q, ((((uint8_t *)t4) + 8) + 8));
        gm_l67 = t658;
        gm_t3dcoc7077t1q t659;
        void * t660 = gm_l0;
        void * t661 = GM_LD(void *, (((uint8_t *)&gm_l67) + 0));
        void * t662 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t660, t661);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t659 + 0, t662);
        gm_t9805qtonbsag t663 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l67) + 8));
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t659 + 8, t663);
        void * t664 = gm_l0;
        void * t665 = GM_LD(void *, (((uint8_t *)&gm_l67) + 16));
        void * t666 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t664, t665);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t659 + 16, t666);
        gm_tbo4aa19b457h t667;
        memset(&t667, 0, sizeof t667);
        GM_ST(uint8_t, ((uint8_t *)&t667 + 0), ((uint8_t)46ULL));
        GM_ST(gm_t3dcoc7077t1q, ((uint8_t *)&t667 + 8), t659);
        t5 = t667;
        goto me6;
mn656:;
ma42:;
        uint8_t t669 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t669 != ((uint8_t)42ULL)) goto mn668;
        gm_t083kd7b1kkag t670 = GM_LD(gm_t083kd7b1kkag, ((((uint8_t *)t4) + 8) + 8));
        gm_l68 = t670;
        gm_t083kd7b1kkag t671;
        memset((uint8_t *)&t671 + 20, 0, 4);
        void * t672 = gm_l0;
        void * t673 = GM_LD(void *, (((uint8_t *)&gm_l68) + 0));
        void * t674 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t672, t673);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t671 + 0, t674);
        gm_t9805qtonbsag t675 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l68) + 8));
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t671 + 8, t675);
        uint32_t t676 = GM_LD(uint32_t, (((uint8_t *)&gm_l68) + 16));
        GM_ST(uint32_t, (uint8_t *)&t671 + 16, t676);
        gm_tbo4aa19b457h t677;
        memset(&t677, 0, sizeof t677);
        GM_ST(uint8_t, ((uint8_t *)&t677 + 0), ((uint8_t)47ULL));
        GM_ST(gm_t083kd7b1kkag, ((uint8_t *)&t677 + 8), t671);
        t5 = t677;
        goto me6;
mn668:;
ma43:;
        uint8_t t679 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t679 != ((uint8_t)43ULL)) goto mn678;
        void * t680 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l69 = t680;
        void * t681 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l70 = t681;
        uint8_t t682 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l71 = t682;
        {
            void * t684 = gm_l0;
            void * t685 = gm_l69;
            void * t686 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t684, t685);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l72 = t686;
        }
        {
            void * t687 = gm_l0;
            void * t688 = gm_l70;
            void * t689 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t687, t688);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l73 = t689;
        }
        gm_tbo4aa19b457h t690;
        void * t692 = gm_l2;
        void * t693 = gm_l69;
        gm_t88rcucg1b7uk t694 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t693) + 0));
        gm_tbgiuvo2j5vcs t695 = gm_f_eq_word_itm79nq((void *)NULL, cx, NULL, t692, t694);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbgiuvo2j5vcs t696 = t695;
        if (!((((uint8_t *)&t696))[8] != 0)) goto ie691;
        gm_tmn7n0n9raou5 t697 = GM_LD(gm_tmn7n0n9raou5, ((uint8_t *)&t696));
        gm_l74 = t697;
        void * t698 = gm_l2;
        void * t699 = gm_l69;
        gm_t88rcucg1b7uk t700 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t699) + 0));
        uint64_t t701 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t698, t700);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t702 = (t701 <= ((uint64_t)8ULL));
        if (!t702) goto ie691;
        gm_ta6jn77i9rmqf t704;
        memset((uint8_t *)&t704 + 25, 0, 7);
        gm_tmn7n0n9raou5 t705 = gm_l74;
        GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t704 + 0, t705);
        void * t706 = gm_l72;
        GM_ST(void *, (uint8_t *)&t704 + 8, t706);
        void * t707 = gm_l73;
        GM_ST(void *, (uint8_t *)&t704 + 16, t707);
        uint8_t t708 = gm_l71;
        GM_ST(uint8_t, (uint8_t *)&t704 + 24, t708);
        gm_tbo4aa19b457h t709;
        memset(&t709, 0, sizeof t709);
        GM_ST(uint8_t, ((uint8_t *)&t709 + 0), ((uint8_t)24ULL));
        GM_ST(gm_ta6jn77i9rmqf, ((uint8_t *)&t709 + 8), t704);
        gm_tbo4aa19b457h t710 = t709;
        t690 = t710;
        goto ix691;
ie691:;
        gm_tlcl4u558l6p5 t712;
        memset((uint8_t *)&t712 + 17, 0, 7);
        void * t713 = gm_l72;
        GM_ST(void *, (uint8_t *)&t712 + 0, t713);
        void * t714 = gm_l73;
        GM_ST(void *, (uint8_t *)&t712 + 8, t714);
        uint8_t t715 = gm_l71;
        GM_ST(uint8_t, (uint8_t *)&t712 + 16, t715);
        gm_tbo4aa19b457h t716;
        memset(&t716, 0, sizeof t716);
        GM_ST(uint8_t, ((uint8_t *)&t716 + 0), ((uint8_t)48ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t716 + 8), t712);
        gm_tbo4aa19b457h t717 = t716;
        t690 = t717;
ix691:;
        gm_tbo4aa19b457h t718 = t690;
        t5 = t718;
        goto me6;
mn678:;
ma44:;
        uint8_t t720 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t720 != ((uint8_t)45ULL)) goto mn719;
        void * t721 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l75 = t721;
        void * t722 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l76 = t722;
        uint8_t t723 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l77 = t723;
        gm_tlcl4u558l6p5 t724;
        memset((uint8_t *)&t724 + 17, 0, 7);
        void * t725 = gm_l0;
        void * t726 = gm_l75;
        void * t727 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t725, t726);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t724 + 0, t727);
        void * t728 = gm_l0;
        void * t729 = gm_l76;
        void * t730 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t728, t729);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t724 + 8, t730);
        uint8_t t731 = gm_l77;
        GM_ST(uint8_t, (uint8_t *)&t724 + 16, t731);
        gm_tbo4aa19b457h t732;
        memset(&t732, 0, sizeof t732);
        GM_ST(uint8_t, ((uint8_t *)&t732 + 0), ((uint8_t)49ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t732 + 8), t724);
        t5 = t732;
        goto me6;
mn719:;
ma45:;
        uint8_t t734 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t734 != ((uint8_t)44ULL)) goto mn733;
        void * t735 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l78 = t735;
        gm_b16_8 t736 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l79 = t736;
        {
            void * t738 = gm_l0;
            void * t739 = gm_l78;
            void * t740 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t738, t739);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l80 = t740;
        }
        {
            gm_f_list_58nr42v_4((void *)&F.l81, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t742 = gm_l79;
                gm_l82 = t742;
            }
            {
                gm_l83 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t744 = gm_l83;
                    uint64_t t745 = GM_LD(uintptr_t, ((uint8_t *)&gm_l82) + 8);
                    uint8_t t746 = (t744 < t745);
                    if (!t746) goto lb743;
                    {
                        uint64_t t749 = gm_l83;
                        uint64_t t750 = (uintptr_t)t749;
                        uint64_t t751 = GM_LD(uintptr_t, ((uint8_t *)&gm_l82) + 8);
                        gm_tbbqefuhv2r8h t754 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l82)) + t750 * 24));
                        gm_l84 = t754;
                        {
                            void * t756 = (void *)((uint8_t *)&F.l81);
                            gm_tbbqefuhv2r8h t757;
                            void * t758 = gm_l0;
                            void * t759 = GM_LD(void *, (((uint8_t *)&gm_l84) + 0));
                            void * t760 = gm_f_low_pat_bba97qc((void *)NULL, cx, NULL, t758, t759);
                            if (gm_unw.kind) {
                                goto gm_ll747;
                            }
                            GM_ST(void *, (uint8_t *)&t757 + 0, t760);
                            void * t761 = gm_l0;
                            void * t762 = GM_LD(void *, (((uint8_t *)&gm_l84) + 8));
                            void * t763 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t761, t762);
                            if (gm_unw.kind) {
                                goto gm_ll747;
                            }
                            GM_ST(void *, (uint8_t *)&t757 + 8, t763);
                            void * t764 = gm_l0;
                            void * t765 = GM_LD(void *, (((uint8_t *)&gm_l84) + 16));
                            void * t766 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t764, t765);
                            if (gm_unw.kind) {
                                goto gm_ll747;
                            }
                            GM_ST(void *, (uint8_t *)&t757 + 16, t766);
                            gm_tftb1odc739t5 t767 = gm_f_push_bpovr2k_4(t756, &t757);
                            gm_tftb1odc739t5 t768 = t767;
                            if (((((uint8_t *)&t768))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t768) + 1, gm_src7afujsggtlje, 366, 31);
                            gm_b0_1 t769 = GM_LD(gm_b0_1, (((uint8_t *)&t768) + 1));
                        }
                        goto me752;
mn753:;
                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 366, 13);
me752:;
                    }
lc743:;
                    {
                        uint64_t t771 = gm_l83;
                        uint64_t r772; bool o773 = __builtin_add_overflow(t771, ((uint64_t)1ULL), &r772);
                        uint64_t t774 = r772;
                        gm_l83 = t774;
                    }
                }
gm_ll747:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb743; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc743; }
                }
                goto gm_lad;
lb743:;
            }
        }
        gm_tkaapcii86udk t775;
        void * t776 = gm_l80;
        GM_ST(void *, (uint8_t *)&t775 + 0, t776);
        gm_b16_8 t777 = GM_LD(gm_b16_8, (((uint8_t *)&F.l81) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t775 + 8, t777);
        gm_tbo4aa19b457h t778;
        memset(&t778, 0, sizeof t778);
        GM_ST(uint8_t, ((uint8_t *)&t778 + 0), ((uint8_t)50ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t778 + 8), t775);
        gm_tbo4aa19b457h t779 = t778;
        t5 = t779;
        goto me6;
mn733:;
ma46:;
        uint8_t t781 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t781 != ((uint8_t)46ULL)) goto mn780;
        void * t782 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l85 = t782;
        void * t783 = gm_l0;
        void * t784 = gm_l85;
        void * t785 = gm_f_low_block_klh2c88((void *)NULL, cx, NULL, t783, t784);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t786;
        memset(&t786, 0, sizeof t786);
        GM_ST(uint8_t, ((uint8_t *)&t786 + 0), ((uint8_t)51ULL));
        GM_ST(void *, ((uint8_t *)&t786 + 8), t785);
        t5 = t786;
        goto me6;
mn780:;
ma47:;
        uint8_t t788 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t788 != ((uint8_t)47ULL)) goto mn787;
        gm_trl2khmuphncd t789 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t4) + 8) + 8));
        gm_l86 = t789;
        gm_trl2khmuphncd t790;
        void * t791 = gm_l0;
        gm_b16_8 t792 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l86) + 0));
        gm_b16_8 t793 = gm_f_low_conds_1olabjo((void *)NULL, cx, NULL, t791, t792);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t790 + 0, t793);
        void * t794 = gm_l0;
        void * t795 = GM_LD(void *, (((uint8_t *)&gm_l86) + 16));
        void * t796 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t794, t795);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t790 + 16, t796);
        void * t797 = gm_l0;
        void * t798 = GM_LD(void *, (((uint8_t *)&gm_l86) + 24));
        void * t799 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t797, t798);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t790 + 24, t799);
        gm_tbo4aa19b457h t800;
        memset(&t800, 0, sizeof t800);
        GM_ST(uint8_t, ((uint8_t *)&t800 + 0), ((uint8_t)52ULL));
        GM_ST(gm_trl2khmuphncd, ((uint8_t *)&t800 + 8), t790);
        t5 = t800;
        goto me6;
mn787:;
ma48:;
        uint8_t t802 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t802 != ((uint8_t)48ULL)) goto mn801;
        gm_t710lsrq1m8gk t803 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t4) + 8) + 8));
        F.l87 = t803;
        gm_t710lsrq1m8gk t804;
        memset((uint8_t *)&t804 + 4, 0, 4);
        uint32_t t805 = GM_LD(uint32_t, (((uint8_t *)&F.l87) + 0));
        GM_ST(uint32_t, (uint8_t *)&t804 + 0, t805);
        void * t806 = gm_l0;
        gm_b16_8 t807 = GM_LD(gm_b16_8, (((uint8_t *)&F.l87) + 8));
        gm_b16_8 t808 = gm_f_low_conds_1olabjo((void *)NULL, cx, NULL, t806, t807);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t804 + 8, t808);
        void * t809 = gm_l0;
        void * t810 = GM_LD(void *, (((uint8_t *)&F.l87) + 24));
        void * t811 = gm_f_low_block_klh2c88((void *)NULL, cx, NULL, t809, t810);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t804 + 24, t811);
        void * t812 = gm_l0;
        void * t813 = GM_LD(void *, (((uint8_t *)&F.l87) + 32));
        void * t814 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t812, t813);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t804 + 32, t814);
        gm_tbo4aa19b457h t815;
        memset(&t815, 0, sizeof t815);
        GM_ST(uint8_t, ((uint8_t *)&t815 + 0), ((uint8_t)53ULL));
        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t815 + 8), t804);
        t5 = t815;
        goto me6;
mn801:;
ma49:;
        uint8_t t817 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t817 != ((uint8_t)49ULL)) goto mn816;
        gm_teo9d4mncjfdu t818 = GM_LD(gm_teo9d4mncjfdu, ((((uint8_t *)t4) + 8) + 8));
        gm_l88 = t818;
        gm_teo9d4mncjfdu t819;
        memset((uint8_t *)&t819 + 12, 0, 4);
        gm_te9hrk0u35rev t820 = GM_LD(gm_te9hrk0u35rev, (((uint8_t *)&gm_l88) + 0));
        GM_ST(gm_te9hrk0u35rev, (uint8_t *)&t819 + 0, t820);
        void * t821 = gm_l0;
        void * t822 = GM_LD(void *, (((uint8_t *)&gm_l88) + 16));
        void * t823 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t821, t822);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t819 + 16, t823);
        gm_tbo4aa19b457h t824;
        memset(&t824, 0, sizeof t824);
        GM_ST(uint8_t, ((uint8_t *)&t824 + 0), ((uint8_t)54ULL));
        GM_ST(gm_teo9d4mncjfdu, ((uint8_t *)&t824 + 8), t819);
        t5 = t824;
        goto me6;
mn816:;
ma50:;
        uint8_t t826 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t826 != ((uint8_t)50ULL)) goto mn825;
        gm_te9hrk0u35rev t827 = GM_LD(gm_te9hrk0u35rev, ((((uint8_t *)t4) + 8) + 8));
        gm_l89 = t827;
        gm_te9hrk0u35rev t828 = gm_l89;
        gm_tbo4aa19b457h t829;
        memset(&t829, 0, sizeof t829);
        GM_ST(uint8_t, ((uint8_t *)&t829 + 0), ((uint8_t)55ULL));
        GM_ST(gm_te9hrk0u35rev, ((uint8_t *)&t829 + 8), t828);
        t5 = t829;
        goto me6;
mn825:;
ma51:;
        uint8_t t831 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t831 != ((uint8_t)51ULL)) goto mn830;
        uint32_t t832 = GM_LD(uint32_t, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l90 = t832;
        void * t833 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l91 = t833;
        uint32_t t834 = gm_l90;
        void * t835 = gm_l0;
        void * t836 = gm_l91;
        void * t837 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t835, t836);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttggq7ng4kp3p t838;
        GM_ST(uint32_t, (uint8_t *)&t838 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t838 + 0), t834);
        GM_ST(void *, ((uint8_t *)&t838 + 8), t837);
        gm_tbo4aa19b457h t839;
        memset(&t839, 0, sizeof t839);
        GM_ST(uint8_t, ((uint8_t *)&t839 + 0), ((uint8_t)56ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t839 + 8), t838);
        t5 = t839;
        goto me6;
mn830:;
ma52:;
        uint8_t t841 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t841 != ((uint8_t)52ULL)) goto mn840;
        void * t842 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l92 = t842;
        void * t843 = gm_l0;
        void * t844 = gm_l92;
        void * t845 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t843, t844);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t846;
        memset(&t846, 0, sizeof t846);
        GM_ST(uint8_t, ((uint8_t *)&t846 + 0), ((uint8_t)57ULL));
        GM_ST(void *, ((uint8_t *)&t846 + 8), t845);
        t5 = t846;
        goto me6;
mn840:;
ma53:;
        uint8_t t848 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t848 != ((uint8_t)53ULL)) goto mn847;
        void * t849 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l93 = t849;
        void * t850 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l94 = t850;
        void * t851 = gm_l0;
        void * t852 = gm_l93;
        void * t853 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t851, t852);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t854 = gm_l0;
        void * t855 = gm_l94;
        void * t856 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t854, t855);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3mf58gv9kf3n t857;
        GM_ST(void *, ((uint8_t *)&t857 + 0), t853);
        GM_ST(void *, ((uint8_t *)&t857 + 8), t856);
        gm_tbo4aa19b457h t858;
        memset(&t858, 0, sizeof t858);
        GM_ST(uint8_t, ((uint8_t *)&t858 + 0), ((uint8_t)58ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t858 + 8), t857);
        t5 = t858;
        goto me6;
mn847:;
ma54:;
        uint8_t t860 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t860 != ((uint8_t)54ULL)) goto mn859;
        void * t861 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l95 = t861;
        void * t862 = gm_l0;
        void * t863 = gm_l95;
        void * t864 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t862, t863);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t865;
        memset(&t865, 0, sizeof t865);
        GM_ST(uint8_t, ((uint8_t *)&t865 + 0), ((uint8_t)59ULL));
        GM_ST(void *, ((uint8_t *)&t865 + 8), t864);
        t5 = t865;
        goto me6;
mn859:;
ma55:;
        uint8_t t867 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t867 != ((uint8_t)55ULL)) goto mn866;
        void * t868 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l96 = t868;
        {
            void * t870 = gm_l96;
            gm_th1g69d7vpipg t871;
            uint8_t t874 = GM_LD(uint8_t, ((((uint8_t *)t870) + 0) + 0));
            if (t874 != ((uint8_t)0ULL)) goto mn873;
            gm_tiilmfd16v3fc t875 = GM_LD(gm_tiilmfd16v3fc, (((((uint8_t *)t870) + 0) + 8) + 0));
            gm_l97 = t875;
            uint32_t t876 = GM_LD(uint32_t, (((((uint8_t *)t870) + 0) + 8) + 24));
            gm_l98 = t876;
            gm_t419mo45g7j0a t877;
            memset((uint8_t *)&t877 + 28, 0, 4);
            memcpy((uint8_t *)&t877 + 0, ((uint8_t *)&gm_l97), 24);
            uint32_t t878 = gm_l98;
            GM_ST(uint32_t, (uint8_t *)&t877 + 24, t878);
            gm_th1g69d7vpipg t879;
            GM_ST(uint8_t, (uint8_t *)&t879 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t879 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t879 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t879 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t879 + 8), t877);
            t871 = t879;
            goto me872;
mn873:;
            uint8_t t881 = GM_LD(uint8_t, ((((uint8_t *)t870) + 0) + 0));
            if (t881 != ((uint8_t)1ULL)) goto mn880;
            void * t882 = GM_LD(void *, ((((uint8_t *)t870) + 0) + 8));
            gm_l99 = t882;
            void * t883 = gm_l0;
            void * t884 = gm_l99;
            void * t885 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t883, t884);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th1g69d7vpipg t886;
            GM_ST(uint8_t, (uint8_t *)&t886 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t886 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t886 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t886 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t886 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t886 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t886 + 0), ((uint8_t)1ULL));
            GM_ST(void *, ((uint8_t *)&t886 + 8), t885);
            t871 = t886;
            goto me872;
mn880:;
            gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 379, 26);
me872:;
            F.l100 = t871;
        }
        gm_tntn2tf15puib t887;
        memcpy((uint8_t *)&t887 + 0, ((uint8_t *)&F.l100), 40);
        void * t888 = gm_l96;
        gm_b16_8 t889 = GM_LD(gm_b16_8, (((uint8_t *)t888) + 40));
        GM_ST(gm_b16_8, (uint8_t *)&t887 + 40, t889);
        void * t890 = gm_l96;
        gm_b16_8 t891 = GM_LD(gm_b16_8, (((uint8_t *)t890) + 56));
        GM_ST(gm_b16_8, (uint8_t *)&t887 + 56, t891);
        void * t892 = gm_l0;
        void * t893 = gm_l96;
        gm_b16_8 t894 = GM_LD(gm_b16_8, (((uint8_t *)t893) + 72));
        gm_b16_8 t895 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t892, t894);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t887 + 72, t895);
        gm_tbo4aa19b457h t896;
        GM_ST(uint8_t, (uint8_t *)&t896 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t896 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t896 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t896 + 0), ((uint8_t)60ULL));
        GM_ST(gm_tntn2tf15puib, ((uint8_t *)&t896 + 8), t887);
        gm_tbo4aa19b457h t897 = t896;
        t5 = t897;
        goto me6;
mn866:;
ma56:;
        uint8_t t899 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t899 != ((uint8_t)58ULL)) goto mn898;
        gm_t710lsrq1m8gk t900 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t4) + 8) + 8));
        F.l101 = t900;
        {
            gm_f_list_kualkeq((void *)&F.l102, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t903 = GM_LD(gm_b16_8, (((uint8_t *)&F.l101) + 8));
                gm_l103 = t903;
            }
            {
                gm_l104 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t905 = gm_l104;
                    uint64_t t906 = GM_LD(uintptr_t, ((uint8_t *)&gm_l103) + 8);
                    uint8_t t907 = (t905 < t906);
                    if (!t907) goto lb904;
                    {
                        uint64_t t910 = gm_l104;
                        uint64_t t911 = (uintptr_t)t910;
                        uint64_t t912 = GM_LD(uintptr_t, ((uint8_t *)&gm_l103) + 8);
                        gm_t4lnf71mruqpt t915 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l103)) + t911 * 32));
                        gm_l105 = t915;
                        {
                            gm_t4lnf71mruqpt t917;
                            uint8_t t920 = GM_LD(uint8_t, (((uint8_t *)&gm_l105) + 0));
                            if (t920 != ((uint8_t)0ULL)) goto mn919;
                            void * t921 = GM_LD(void *, (((uint8_t *)&gm_l105) + 8));
                            gm_l106 = t921;
                            void * t922 = gm_l0;
                            void * t923 = gm_l106;
                            void * t924 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t922, t923);
                            if (gm_unw.kind) {
                                goto gm_ll908;
                            }
                            gm_t4lnf71mruqpt t925;
                            GM_ST(uint8_t, (uint8_t *)&t925 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t925 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t925 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t925 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t925 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t925 + 0), ((uint8_t)0ULL));
                            GM_ST(void *, ((uint8_t *)&t925 + 8), t924);
                            t917 = t925;
                            goto me918;
mn919:;
                            uint8_t t927 = GM_LD(uint8_t, (((uint8_t *)&gm_l105) + 0));
                            if (t927 != ((uint8_t)1ULL)) goto mn926;
                            gm_b16_8 t928 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l105) + 8) + 0));
                            gm_l107 = t928;
                            void * t929 = GM_LD(void *, ((((uint8_t *)&gm_l105) + 8) + 16));
                            gm_l108 = t929;
                            gm_tpe1hv3fekn05 t930;
                            gm_b16_8 t931 = gm_l107;
                            GM_ST(gm_b16_8, (uint8_t *)&t930 + 0, t931);
                            void * t932 = gm_l0;
                            void * t933 = gm_l108;
                            void * t934 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t932, t933);
                            if (gm_unw.kind) {
                                goto gm_ll908;
                            }
                            GM_ST(void *, (uint8_t *)&t930 + 16, t934);
                            gm_t4lnf71mruqpt t935;
                            GM_ST(uint8_t, (uint8_t *)&t935 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t935 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t935 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t935 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t935 + 8), t930);
                            t917 = t935;
                            goto me918;
mn926:;
                            gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 388, 31);
me918:;
                            gm_l109 = t917;
                        }
                        {
                            void * t936 = (void *)((uint8_t *)&F.l102);
                            gm_tftb1odc739t5 t937 = gm_f_push_a0ftqhm(t936, &(*(gm_t4lnf71mruqpt *)((uint8_t *)&gm_l109)));
                            gm_tftb1odc739t5 t938 = t937;
                            if (((((uint8_t *)&t938))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t938) + 1, gm_src7afujsggtlje, 392, 17);
                            gm_b0_1 t939 = GM_LD(gm_b0_1, (((uint8_t *)&t938) + 1));
                        }
                        goto me913;
mn914:;
                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 387, 13);
me913:;
                    }
lc904:;
                    {
                        uint64_t t941 = gm_l104;
                        uint64_t r942; bool o943 = __builtin_add_overflow(t941, ((uint64_t)1ULL), &r942);
                        uint64_t t944 = r942;
                        gm_l104 = t944;
                    }
                }
gm_ll908:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb904; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc904; }
                }
                goto gm_lad;
lb904:;
            }
        }
        gm_t710lsrq1m8gk t945;
        memset((uint8_t *)&t945 + 4, 0, 4);
        uint32_t t946 = GM_LD(uint32_t, (((uint8_t *)&F.l101) + 0));
        GM_ST(uint32_t, (uint8_t *)&t945 + 0, t946);
        gm_b16_8 t947 = GM_LD(gm_b16_8, (((uint8_t *)&F.l102) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t945 + 8, t947);
        void * t948 = gm_l0;
        void * t949 = GM_LD(void *, (((uint8_t *)&F.l101) + 24));
        void * t950 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t948, t949);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t945 + 24, t950);
        void * t951 = gm_l0;
        void * t952 = GM_LD(void *, (((uint8_t *)&F.l101) + 32));
        void * t953 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t951, t952);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t945 + 32, t953);
        gm_tbo4aa19b457h t954;
        memset(&t954, 0, sizeof t954);
        GM_ST(uint8_t, ((uint8_t *)&t954 + 0), ((uint8_t)61ULL));
        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t954 + 8), t945);
        gm_tbo4aa19b457h t955 = t954;
        t5 = t955;
        goto me6;
mn898:;
ma57:;
        uint8_t t959 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t959 != ((uint8_t)10ULL)) goto pa958;
        goto po957;
pa958:;
        uint8_t t961 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t961 != ((uint8_t)11ULL)) goto pa960;
        goto po957;
pa960:;
        uint8_t t963 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t963 != ((uint8_t)12ULL)) goto pa962;
        goto po957;
pa962:;
        uint8_t t965 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t965 != ((uint8_t)13ULL)) goto pa964;
        goto po957;
pa964:;
        uint8_t t967 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t967 != ((uint8_t)2ULL)) goto pa966;
        goto po957;
pa966:;
        uint8_t t969 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t969 != ((uint8_t)3ULL)) goto pa968;
        goto po957;
pa968:;
        uint8_t t971 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t971 != ((uint8_t)9ULL)) goto pa970;
        goto po957;
pa970:;
        uint8_t t973 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t973 != ((uint8_t)56ULL)) goto pa972;
        goto po957;
pa972:;
        uint8_t t975 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t975 != ((uint8_t)57ULL)) goto pa974;
        goto po957;
pa974:;
        goto mn956;
po957:;
        gm_tbo4aa19b457h t976;
        memset(&t976, 0, sizeof t976);
        GM_ST(uint8_t, ((uint8_t *)&t976 + 0), ((uint8_t)62ULL));
        t5 = t976;
        goto me6;
mn956:;
ma58:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 273, 14);
me6:;
        F.l110 = t5;
    }
    {
        void * t977 = gm_l0;
        void * t978 = gm_l1;
        void * t979 = gm_f_mk_node_ndavg23((void *)NULL, cx, NULL, t977, t978, &(*(gm_tbo4aa19b457h *)((uint8_t *)&F.l110)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t980 = 0;
        F.ret = t979;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_exprs_musunrl(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_exprs_musunrl F;
    void * gm_l0; /* lw */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    GM_STACK_CHECK(&F, 222u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_sjspm77((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l3 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = gm_l3;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = t10;
                    uint64_t t14 = GM_LD(uint64_t, ((uint8_t *)&t11));
                    gm_l5 = t14;
                    {
                        void * t16 = (void *)((uint8_t *)&F.l2);
                        void * t17 = gm_l0;
                        uint64_t t18 = gm_l5;
                        uint64_t t19 = (uintptr_t)t18;
                        uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t19 >= (uintptr_t)(t20)) gm_trap_index(t19, t20, gm_src7afujsggtlje, 247, 44);
                        void * t21 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t19 * 88);
                        void * t22 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t17, t21);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tftb1odc739t5 t23 = gm_f_push_uehemid(t16, t22);
                        gm_tftb1odc739t5 t24 = t23;
                        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_src7afujsggtlje, 247, 26);
                        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
                    }
                    goto me12;
mn13:;
                    gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 247, 5);
me12:;
                }
lc4:;
                {
                    uint64_t t27 = gm_l4;
                    uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
                    uint64_t t30 = r28;
                    gm_l4 = t30;
                }
            }
gm_ll8:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb4; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc4; }
            }
            goto gm_lad;
lb4:;
        }
    }
    {
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        uint8_t t32 = 0;
        F.ret = t31;
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
inline __attribute__((aligned(4))) gm_tg5lql6itufk7 gm_f_int_w_a8oe7le(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_int_w_a8oe7le F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_ttovhfee8mf28 gm_l2; /* info */
    GM_STACK_CHECK(&F, 232u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_tg5lql6itufk7 t4 = gm_f_int_info_ulcevdv(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_tg5lql6itufk7 t5 = t4;
        gm_ttovhfee8mf28 t6;
        if (((((uint8_t *)&t5))[8] != 0)) {
            gm_ttovhfee8mf28 t7 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_tg5lql6itufk7 t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t6;
    }
    {
        uint8_t t11 = 0;
        uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        gm_thpdbp3q7rdrc t13;
        GM_ST(uint32_t, ((uint8_t *)&t13 + 0), ((uint32_t)8ULL));
        GM_ST(uint32_t, ((uint8_t *)&t13 + 4), ((uint32_t)16ULL));
        GM_ST(uint32_t, ((uint8_t *)&t13 + 8), ((uint32_t)32ULL));
        GM_ST(uint32_t, ((uint8_t *)&t13 + 12), ((uint32_t)64ULL));
        gm_thpdbp3q7rdrc t14 = t13;
        uint8_t t15 = 0;
        uint32_t t16 = t12;
        if (gm_eq467d5cm1d5(((uint8_t *)&t14) + 0, ((uint8_t *)&t16))) t15 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t14) + 4, ((uint8_t *)&t16))) t15 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t14) + 8, ((uint8_t *)&t16))) t15 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t14) + 12, ((uint8_t *)&t16))) t15 = 1;
        uint8_t t17 = (uint8_t)(!t15);
        if (!t17) {
            uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            uint64_t t19 = (uint64_t)t18;
            void * t20 = gm_l0;
            gm_t88rcucg1b7uk t21 = gm_l1;
            uint64_t t22 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t r23; bool o24 = __builtin_mul_overflow(((uint64_t)8ULL), t22, &r23);
            if (o24) gm_trap("integer overflow in `*`", gm_src7afujsggtlje, 222, 66);
            uint64_t t25 = r23;
            uint8_t t26 = (!(t19 == t25));
            t11 = t26;
        } else t11 = 1;
        if (!t11) goto ie10;
        {
            gm_tg5lql6itufk7 t28;
            memset(&t28, 0, sizeof t28);
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        uint32_t t31 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        gm_ttovhfee8mf28 t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t32 + 0), t30);
        GM_ST(uint32_t, ((uint8_t *)&t32 + 4), t31);
        gm_tg5lql6itufk7 t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
        GM_ST(gm_ttovhfee8mf28, ((uint8_t *)&t33), t32);
        ((uint8_t *)&t33)[8] = 1;
        uint8_t t34 = 0;
        F.ret = t33;
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
extern __attribute__((aligned(4))) uint8_t gm_f_is_float_op_6co8naj(gm_ts6oqppjh0ba3 a0);
extern __attribute__((aligned(4))) uint8_t gm_f_is_shift_svl330f(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) void gm_f_low_binary_5dhmccv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, void * a2, void * a3, void * a4) {
    struct gm_fr_low_binary_5dhmccv F;
    void * gm_l0; /* lw */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    void * gm_l2; /* l */
    void * gm_l3; /* r */
    void * gm_l4; /* e */
    void * gm_l5; /* m */
    void * gm_l6; /* ln */
    void * gm_l7; /* rn */
    gm_tmn7n0n9raou5 gm_l8; /* how */
    gm_ttovhfee8mf28 gm_l9; /* t */
    gm_t9qkh0rsooc8d gm_l10; /* fl */
    gm_t9qkh0rsooc8d gm_l11; /* fl */
    gm_t9qkh0rsooc8d gm_l12; /* fl */
    gm_t9qkh0rsooc8d gm_l13; /* fl */
    uint8_t gm_l14; /* opt */
    gm_ttovhfee8mf28 gm_l15; /* shift */
    uint16_t gm_l16; /* w */
    GM_STACK_CHECK(&F, 1381u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l5 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l2;
        void * t6 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t6;
    }
    {
        void * t7 = gm_l0;
        void * t8 = gm_l3;
        void * t9 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t9;
    }
    {
        gm_ts6oqppjh0ba3 t11 = gm_l1;
        uint8_t t12 = gm_f_is_cmp_r2i4hv9(t11);
        if (!t12) goto ie10;
        {
            void * t15 = gm_l5;
            void * t16 = gm_l2;
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t16) + 0));
            gm_tbgiuvo2j5vcs t18 = gm_f_word_cmp_s2rbdbn((void *)NULL, cx, NULL, t15, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbgiuvo2j5vcs t19 = t18;
            if (!((((uint8_t *)&t19))[8] != 0)) goto ie14;
            gm_tmn7n0n9raou5 t20 = GM_LD(gm_tmn7n0n9raou5, ((uint8_t *)&t19));
            gm_l8 = t20;
            uint8_t t21 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l8) + 0) == ((uint8_t)4ULL)));
            if (!t21) goto ie14;
            {
                gm_tkm39klvnn4cr t23;
                memset((uint8_t *)&t23 + 2, 0, 2);
                memset((uint8_t *)&t23 + 12, 0, 4);
                gm_ts6oqppjh0ba3 t24 = gm_l1;
                GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t23 + 0, t24);
                gm_tmn7n0n9raou5 t25 = gm_l8;
                GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t23 + 4, t25);
                void * t26 = gm_l6;
                GM_ST(void *, (uint8_t *)&t23 + 16, t26);
                void * t27 = gm_l7;
                GM_ST(void *, (uint8_t *)&t23 + 24, t27);
                gm_tbo4aa19b457h t28;
                memset(&t28, 0, sizeof t28);
                GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)23ULL));
                GM_ST(gm_tkm39klvnn4cr, ((uint8_t *)&t28 + 8), t23);
                uint8_t t29 = 0;
                F.ret = t28;
                goto gm_fret;
            }
            goto ix14;
ie14:;
ix14:;
        }
        {
            gm_trl9d7m1k5lbd t30;
            memset((uint8_t *)&t30 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t31 = gm_l1;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t30 + 0, t31);
            void * t32 = gm_l6;
            GM_ST(void *, (uint8_t *)&t30 + 8, t32);
            void * t33 = gm_l7;
            GM_ST(void *, (uint8_t *)&t30 + 16, t33);
            gm_tbo4aa19b457h t34;
            memset(&t34, 0, sizeof t34);
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)31ULL));
            GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t34 + 8), t30);
            uint8_t t35 = 0;
            F.ret = t34;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t37 = gm_l5;
        void * t38 = gm_l2;
        gm_t88rcucg1b7uk t39 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t38) + 0));
        gm_tg5lql6itufk7 t40 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t37, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tg5lql6itufk7 t41 = t40;
        if (!((((uint8_t *)&t41))[8] != 0)) goto ie36;
        gm_ttovhfee8mf28 t42 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t41));
        gm_l9 = t42;
        {
            uint8_t t44;
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t49 != ((uint8_t)0ULL)) goto pa48;
            gm_t9qkh0rsooc8d t50 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
            gm_l10 = t50;
            goto po47;
pa48:;
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t52 != ((uint8_t)1ULL)) goto pa51;
            gm_t9qkh0rsooc8d t53 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
            gm_l10 = t53;
            goto po47;
pa51:;
            uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t55 != ((uint8_t)2ULL)) goto pa54;
            gm_t9qkh0rsooc8d t56 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
            gm_l10 = t56;
            goto po47;
pa54:;
            uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t58 != ((uint8_t)3ULL)) goto pa57;
            gm_t9qkh0rsooc8d t59 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
            gm_l10 = t59;
            goto po47;
pa57:;
            goto mn46;
po47:;
            uint8_t t60 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l10) + 0) == ((uint8_t)3ULL)));
            t44 = t60;
            goto me45;
mn46:;
            t44 = ((uint8_t)0);
            goto me45;
mn61:;
            gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 430, 16);
me45:;
            gm_l14 = t44;
        }
        {
            uint8_t t63 = gm_l14;
            uint8_t t64 = (uint8_t)!t63;
            if (!t64) goto ie62;
            {
                gm_ttovhfee8mf28 t66 = gm_l9;
                gm_l15 = t66;
            }
            {
                gm_ts6oqppjh0ba3 t68 = gm_l1;
                uint8_t t69 = gm_f_is_shift_svl330f(t68);
                if (!t69) goto ie67;
                {
                    void * t71 = gm_l5;
                    void * t72 = gm_l3;
                    gm_t88rcucg1b7uk t73 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t72) + 0));
                    gm_tg5lql6itufk7 t74 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t71, t73);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tg5lql6itufk7 t75 = t74;
                    gm_ttovhfee8mf28 t76;
                    if (((((uint8_t *)&t75))[8] != 0)) {
                        gm_ttovhfee8mf28 t77 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t75) + 0));
                        t76 = t77;
                    } else {
                        gm_trl9d7m1k5lbd t78;
                        memset((uint8_t *)&t78 + 2, 0, 6);
                        gm_ts6oqppjh0ba3 t79 = gm_l1;
                        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t78 + 0, t79);
                        void * t80 = gm_l6;
                        GM_ST(void *, (uint8_t *)&t78 + 8, t80);
                        void * t81 = gm_l7;
                        GM_ST(void *, (uint8_t *)&t78 + 16, t81);
                        gm_tbo4aa19b457h t82;
                        memset(&t82, 0, sizeof t82);
                        GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)31ULL));
                        GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t82 + 8), t78);
                        uint8_t t83 = 0;
                        F.ret = t82;
                        goto gm_fret;
                    }
                    gm_l15 = t76;
                }
                goto ix67;
ie67:;
ix67:;
            }
            {
                gm_t0pc2c7dafsjt t84;
                memset((uint8_t *)&t84 + 2, 0, 2);
                memset((uint8_t *)&t84 + 20, 0, 4);
                gm_ts6oqppjh0ba3 t85 = gm_l1;
                GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t84 + 0, t85);
                gm_ttovhfee8mf28 t86 = gm_l9;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t84 + 4, t86);
                gm_ttovhfee8mf28 t87 = gm_l15;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t84 + 12, t87);
                void * t88 = gm_l6;
                GM_ST(void *, (uint8_t *)&t84 + 24, t88);
                void * t89 = gm_l7;
                GM_ST(void *, (uint8_t *)&t84 + 32, t89);
                gm_tbo4aa19b457h t90;
                memset(&t90, 0, sizeof t90);
                GM_ST(uint8_t, ((uint8_t *)&t90 + 0), ((uint8_t)21ULL));
                GM_ST(gm_t0pc2c7dafsjt, ((uint8_t *)&t90 + 8), t84);
                uint8_t t91 = 0;
                F.ret = t90;
                goto gm_fret;
            }
            goto ix62;
ie62:;
ix62:;
        }
        goto ix36;
ie36:;
ix36:;
    }
    {
        void * t93 = gm_l5;
        void * t94 = gm_l2;
        gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t94) + 0));
        gm_t53at80pfdus3 t96;
        gm_f_kind_htv6093((void *)&t96, t93, GM_CONV(gm_csguankko4lmhd, t95));
        gm_t53at80pfdus3 t97 = t96;
        uint8_t t98 = GM_LD(uint8_t, (((uint8_t *)&t97) + 0));
        if (t98 != ((uint8_t)3ULL)) goto ie92;
        uint16_t t99 = GM_LD(uint16_t, (((uint8_t *)&t97) + 8));
        gm_l16 = t99;
        uint16_t t100 = gm_l16;
        gm_tuqvtbj0p3v9c t101;
        GM_ST(uint16_t, ((uint8_t *)&t101 + 0), ((uint16_t)32ULL));
        GM_ST(uint16_t, ((uint8_t *)&t101 + 2), ((uint16_t)64ULL));
        gm_tuqvtbj0p3v9c t102 = t101;
        uint8_t t103 = 0;
        uint16_t t104 = t100;
        if (gm_eq9oqmmiu8qc(((uint8_t *)&t102) + 0, ((uint8_t *)&t104))) t103 = 1;
        if (gm_eq9oqmmiu8qc(((uint8_t *)&t102) + 2, ((uint8_t *)&t104))) t103 = 1;
        uint8_t t105 = (uint8_t)(t103);
        if (!t105) goto ie92;
        gm_ts6oqppjh0ba3 t106 = gm_l1;
        uint8_t t107 = gm_f_is_float_op_6co8naj(t106);
        if (!t107) goto ie92;
        {
            gm_tuggr5sml9o52 t109;
            memset((uint8_t *)&t109 + 3, 0, 5);
            gm_ts6oqppjh0ba3 t110 = gm_l1;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t109 + 0, t110);
            uint16_t t111 = gm_l16;
            uint8_t t112 = ((t111 == ((uint16_t)64ULL)));
            GM_ST(uint8_t, (uint8_t *)&t109 + 2, t112);
            void * t113 = gm_l6;
            GM_ST(void *, (uint8_t *)&t109 + 8, t113);
            void * t114 = gm_l7;
            GM_ST(void *, (uint8_t *)&t109 + 16, t114);
            gm_tbo4aa19b457h t115;
            memset(&t115, 0, sizeof t115);
            GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)22ULL));
            GM_ST(gm_tuggr5sml9o52, ((uint8_t *)&t115 + 8), t109);
            uint8_t t116 = 0;
            F.ret = t115;
            goto gm_fret;
        }
        goto ix92;
ie92:;
ix92:;
    }
    {
        gm_trl9d7m1k5lbd t117;
        memset((uint8_t *)&t117 + 2, 0, 6);
        gm_ts6oqppjh0ba3 t118 = gm_l1;
        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t117 + 0, t118);
        void * t119 = gm_l6;
        GM_ST(void *, (uint8_t *)&t117 + 8, t119);
        void * t120 = gm_l7;
        GM_ST(void *, (uint8_t *)&t117 + 16, t120);
        gm_tbo4aa19b457h t121;
        memset(&t121, 0, sizeof t121);
        GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)31ULL));
        GM_ST(gm_trl9d7m1k5lbd, ((uint8_t *)&t121 + 8), t117);
        uint8_t t122 = 0;
        F.ret = t121;
        goto gm_fret;
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
__attribute__((aligned(4))) void * gm_f_low_block_klh2c88(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_low_block_klh2c88 F;
    void * gm_l0; /* lw */
    void * gm_l1; /* b */
    void * gm_l2; /* m */
    uint8_t gm_l4; /* defers */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_trcmmd12cefb1 gm_l7; /* st */
    uint32_t gm_l8; /* l */
    void * gm_l9; /* init */
    gm_b16_8 gm_l10; /* locals */
    void * gm_l11; /* init */
    void * gm_l12; /* place_ */
    void * gm_l13; /* value */
    uint32_t gm_l14; /* l */
    gm_b16_8 gm_l15; /* places */
    void * gm_l16; /* value */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    void * gm_l20; /* p */
    void * gm_l21; /* x */
    uint8_t gm_l22; /* is_err */
    void * gm_l23; /* body */
    GM_STACK_CHECK(&F, 1840u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l2 = t3;
    }
    {
        gm_f_list_pp6ecn9((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l4 = ((uint8_t)0);
    }
    {
        {
            void * t5 = gm_l1;
            gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t5) + 0));
            gm_l5 = t6;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t8 = gm_l6;
                uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t10 = (t8 < t9);
                if (!t10) goto lb7;
                {
                    uint64_t t13 = gm_l6;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_trcmmd12cefb1 t18 = GM_LD(gm_trcmmd12cefb1, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t14 * 32));
                    gm_l7 = t18;
                    {
                        gm_t2k9dbsv5ff58 t20;
                        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        switch (t29) { case ((uint8_t)0ULL): goto ma22; case ((uint8_t)1ULL): goto ma23; case ((uint8_t)2ULL): goto ma24; case ((uint8_t)3ULL): goto ma25; case ((uint8_t)4ULL): goto ma26; case ((uint8_t)5ULL): goto ma27; default: goto ma28; }
ma22:;
                        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t31 != ((uint8_t)0ULL)) goto mn30;
                        uint32_t t32 = GM_LD(uint32_t, ((((uint8_t *)&gm_l7) + 8) + 0));
                        gm_l8 = t32;
                        void * t33 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 8));
                        gm_l9 = t33;
                        gm_tg0sdqqpruvqq t34;
                        memset((uint8_t *)&t34 + 4, 0, 4);
                        uint32_t t35 = gm_l8;
                        GM_ST(uint32_t, (uint8_t *)&t34 + 0, t35);
                        void * t36 = gm_l0;
                        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 8));
                        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)t37) + 72));
                        gm_b16_8 t39 = t38;
                        uint32_t t40 = gm_l8;
                        uint64_t t41 = (uint64_t)t40;
                        uint64_t t42 = gm_f_us_cuv01fp(t41);
                        uint64_t t43 = (uintptr_t)t42;
                        gm_b16_8 t44 = t39;
                        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&t44) + 8);
                        if (t43 >= (uintptr_t)(t45)) gm_trap_index(t43, t45, gm_src7afujsggtlje, 484, 39);
                        uint64_t t46 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t44)) + t43 * 8));
                        GM_ST(uint64_t, (uint8_t *)&t34 + 8, t46);
                        void * t47 = gm_l0;
                        void * t48 = GM_LD(void *, (((uint8_t *)t47) + 8));
                        gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)t48) + 88));
                        gm_b16_8 t50 = t49;
                        uint32_t t51 = gm_l8;
                        uint64_t t52 = (uint64_t)t51;
                        uint64_t t53 = gm_f_us_cuv01fp(t52);
                        uint64_t t54 = (uintptr_t)t53;
                        gm_b16_8 t55 = t50;
                        uint64_t t56 = GM_LD(uintptr_t, ((uint8_t *)&t55) + 8);
                        if (t54 >= (uintptr_t)(t56)) gm_trap_index(t54, t56, gm_src7afujsggtlje, 484, 63);
                        uint64_t t57 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t55)) + t54 * 8));
                        GM_ST(uint64_t, (uint8_t *)&t34 + 16, t57);
                        void * t58 = gm_l0;
                        void * t59 = gm_l9;
                        void * t60 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t58, t59);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        GM_ST(void *, (uint8_t *)&t34 + 24, t60);
                        gm_t2k9dbsv5ff58 t61;
                        GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t61 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tg0sdqqpruvqq, ((uint8_t *)&t61 + 8), t34);
                        t20 = t61;
                        goto me21;
mn30:;
ma23:;
                        uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t63 != ((uint8_t)1ULL)) goto mn62;
                        gm_b16_8 t64 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l7) + 8) + 0));
                        gm_l10 = t64;
                        void * t65 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 16));
                        gm_l11 = t65;
                        gm_tpe1hv3fekn05 t66;
                        gm_b16_8 t67 = gm_l10;
                        GM_ST(gm_b16_8, (uint8_t *)&t66 + 0, t67);
                        void * t68 = gm_l0;
                        void * t69 = gm_l11;
                        void * t70 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t68, t69);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        GM_ST(void *, (uint8_t *)&t66 + 16, t70);
                        gm_t2k9dbsv5ff58 t71;
                        GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t71 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t71 + 8), t66);
                        t20 = t71;
                        goto me21;
mn62:;
ma24:;
                        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t73 != ((uint8_t)2ULL)) goto mn72;
                        void * t74 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 0));
                        gm_l12 = t74;
                        void * t75 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 8));
                        gm_l13 = t75;
                        gm_t2k9dbsv5ff58 t77;
                        void * t79 = gm_l12;
                        uint8_t t80 = GM_LD(uint8_t, ((((uint8_t *)t79) + 8) + 0));
                        if (t80 != ((uint8_t)15ULL)) goto ie78;
                        uint32_t t81 = GM_LD(uint32_t, ((((uint8_t *)t79) + 8) + 8));
                        gm_l14 = t81;
                        uint32_t t83 = gm_l14;
                        void * t84 = gm_l0;
                        void * t85 = gm_l13;
                        void * t86 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t84, t85);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        gm_ttggq7ng4kp3p t87;
                        GM_ST(uint32_t, (uint8_t *)&t87 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t87 + 0), t83);
                        GM_ST(void *, ((uint8_t *)&t87 + 8), t86);
                        gm_t2k9dbsv5ff58 t88;
                        GM_ST(uint8_t, (uint8_t *)&t88 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t88 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t88 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t88 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t88 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t88 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t88 + 8), t87);
                        gm_t2k9dbsv5ff58 t89 = t88;
                        t77 = t89;
                        goto ix78;
ie78:;
                        void * t91 = gm_l0;
                        void * t92 = gm_l12;
                        void * t93 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t91, t92);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        void * t94 = gm_l0;
                        void * t95 = gm_l13;
                        void * t96 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t94, t95);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        gm_t3mf58gv9kf3n t97;
                        GM_ST(void *, ((uint8_t *)&t97 + 0), t93);
                        GM_ST(void *, ((uint8_t *)&t97 + 8), t96);
                        gm_t2k9dbsv5ff58 t98;
                        GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t98 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t98 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t98 + 8), t97);
                        gm_t2k9dbsv5ff58 t99 = t98;
                        t77 = t99;
ix78:;
                        gm_t2k9dbsv5ff58 t100 = t77;
                        t20 = t100;
                        goto me21;
mn72:;
ma25:;
                        uint8_t t102 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t102 != ((uint8_t)3ULL)) goto mn101;
                        gm_b16_8 t103 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l7) + 8) + 0));
                        gm_l15 = t103;
                        void * t104 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 16));
                        gm_l16 = t104;
                        {
                            gm_f_list_mlkakgl((void *)&F.l17, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll11;
                            }
                        }
                        {
                            {
                                gm_b16_8 t107 = gm_l15;
                                gm_l18 = t107;
                            }
                            {
                                gm_l19 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t109 = gm_l19;
                                    uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                    uint8_t t111 = (t109 < t110);
                                    if (!t111) goto lb108;
                                    {
                                        uint64_t t114 = gm_l19;
                                        uint64_t t115 = (uintptr_t)t114;
                                        uint64_t t116 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                        void * t117 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t115 * 8));
                                        void * t118 = t117;
                                        void * t121 = GM_LD(void *, ((uint8_t *)&t118));
                                        gm_l20 = t121;
                                        {
                                            void * t123 = (void *)((uint8_t *)&F.l17);
                                            void * t124 = gm_l0;
                                            void * t125 = gm_l20;
                                            void * t126 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t124, t125);
                                            if (gm_unw.kind) {
                                                goto gm_ll112;
                                            }
                                            gm_tftb1odc739t5 t127 = gm_f_push_h04leh4(t123, t126);
                                            gm_tftb1odc739t5 t128 = t127;
                                            if (((((uint8_t *)&t128))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t128) + 1, gm_src7afujsggtlje, 491, 35);
                                            gm_b0_1 t129 = GM_LD(gm_b0_1, (((uint8_t *)&t128) + 1));
                                        }
                                        goto me119;
mn120:;
                                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 491, 17);
me119:;
                                    }
lc108:;
                                    {
                                        uint64_t t131 = gm_l19;
                                        uint64_t r132; bool o133 = __builtin_add_overflow(t131, ((uint64_t)1ULL), &r132);
                                        uint64_t t134 = r132;
                                        gm_l19 = t134;
                                    }
                                }
gm_ll112:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb108; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc108; }
                                }
                                goto gm_ll11;
lb108:;
                            }
                        }
                        gm_tpe1hv3fekn05 t135;
                        gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t135 + 0, t136);
                        void * t137 = gm_l0;
                        void * t138 = gm_l16;
                        void * t139 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t137, t138);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        GM_ST(void *, (uint8_t *)&t135 + 16, t139);
                        gm_t2k9dbsv5ff58 t140;
                        GM_ST(uint8_t, (uint8_t *)&t140 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t140 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t140 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t140 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)4ULL));
                        GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t140 + 8), t135);
                        gm_t2k9dbsv5ff58 t141 = t140;
                        t20 = t141;
                        goto me21;
mn101:;
ma26:;
                        uint8_t t143 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t143 != ((uint8_t)4ULL)) goto mn142;
                        void * t144 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                        gm_l21 = t144;
                        void * t145 = gm_l0;
                        void * t146 = gm_l21;
                        void * t147 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t145, t146);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        gm_t2k9dbsv5ff58 t148;
                        GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t148 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t148 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t148 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t148 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t148 + 0), ((uint8_t)5ULL));
                        GM_ST(void *, ((uint8_t *)&t148 + 8), t147);
                        t20 = t148;
                        goto me21;
mn142:;
ma27:;
                        uint8_t t150 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t150 != ((uint8_t)5ULL)) goto mn149;
                        uint8_t t151 = GM_LD(uint8_t, ((((uint8_t *)&gm_l7) + 8) + 0));
                        gm_l22 = t151;
                        void * t152 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 8));
                        gm_l23 = t152;
                        {
                            gm_l4 = ((uint8_t)1);
                        }
                        uint8_t t154 = gm_l22;
                        void * t155 = gm_l0;
                        void * t156 = gm_l23;
                        void * t157 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t155, t156);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        gm_tgtub0v94d5u5 t158;
                        GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t158 + 0), t154);
                        GM_ST(void *, ((uint8_t *)&t158 + 8), t157);
                        gm_t2k9dbsv5ff58 t159;
                        GM_ST(uint8_t, (uint8_t *)&t159 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t159 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t159 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t159 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t159 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t159 + 0), ((uint8_t)6ULL));
                        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t159 + 8), t158);
                        gm_t2k9dbsv5ff58 t160 = t159;
                        t20 = t160;
                        goto me21;
mn149:;
ma28:;
                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 483, 17);
me21:;
                        F.l24 = t20;
                    }
                    {
                        void * t161 = (void *)((uint8_t *)&F.l3);
                        gm_tftb1odc739t5 t162 = gm_f_push_esf6o94(t161, &(*(gm_t2k9dbsv5ff58 *)((uint8_t *)&F.l24)));
                        gm_tftb1odc739t5 t163 = t162;
                        if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 1, gm_src7afujsggtlje, 500, 9);
                        gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t163) + 1));
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 482, 5);
me16:;
                }
lc7:;
                {
                    uint64_t t166 = gm_l6;
                    uint64_t r167; bool o168 = __builtin_add_overflow(t166, ((uint64_t)1ULL), &r167);
                    uint64_t t169 = r167;
                    gm_l6 = t169;
                }
            }
gm_ll11:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb7; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc7; }
            }
            goto gm_lad;
lb7:;
        }
    }
    {
        gm_t4n3ujve5elie t170;
        memset((uint8_t *)&t170 + 25, 0, 7);
        gm_b16_8 t171 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t170 + 0, t171);
        void * t172 = gm_l0;
        void * t173 = gm_l1;
        void * t174 = GM_LD(void *, (((uint8_t *)t173) + 16));
        void * t175 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t172, t174);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t170 + 16, t175);
        uint8_t t176 = gm_l4;
        GM_ST(uint8_t, (uint8_t *)&t170 + 24, t176);
        gm_tq7s98ihonkju t177 = gm_f_new_jg8stkn((void *)NULL, cx, NULL, &t170, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8o4k1d1for)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t178 = t177;
        if (((((uint8_t *)&t178))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t178) + 8, gm_src7afujsggtlje, 502, 12);
        void * t179 = GM_LD(void *, (((uint8_t *)&t178) + 8));
        uint8_t t180 = 0;
        F.ret = t179;
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
__attribute__((aligned(4))) void gm_f_low_builtin_rbo6661(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tvdg94bk7aa22 a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_low_builtin_rbo6661 F;
    void * gm_l0; /* lw */
    gm_tvdg94bk7aa22 gm_l1; /* op */
    gm_b16_8 gm_l2; /* args */
    void * gm_l3; /* e */
    void * gm_l4; /* m */
    gm_b16_8 gm_l5; /* ns */
    gm_ttovhfee8mf28 gm_l6; /* from */
    gm_ttovhfee8mf28 gm_l7; /* to */
    gm_ttovhfee8mf28 gm_l8; /* t */
    gm_ttovhfee8mf28 gm_l9; /* t */
    GM_STACK_CHECK(&F, 1577u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        gm_b16_8 t5 = gm_l2;
        gm_b16_8 t6 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t6;
    }
    {
        gm_tvdg94bk7aa22 t8 = gm_l1;
        gm_t2dgk4tdinqkd t9;
        gm_tvdg94bk7aa22 t10;
        GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t10 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t9 + 0, t10);
        gm_tvdg94bk7aa22 t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)1ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t9 + 12, t11);
        gm_tvdg94bk7aa22 t12;
        GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t12 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)6ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t9 + 24, t12);
        gm_t2dgk4tdinqkd t13 = t9;
        uint8_t t14 = 0;
        gm_tvdg94bk7aa22 t15 = t8;
        if (gm_eqg63o53ar52(((uint8_t *)&t13) + 0, ((uint8_t *)&t15))) t14 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t13) + 12, ((uint8_t *)&t15))) t14 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t13) + 24, ((uint8_t *)&t15))) t14 = 1;
        uint8_t t16 = (uint8_t)(t14);
        if (!t16) goto ie7;
        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t18 = ((t17 == ((uint64_t)1ULL)));
        if (!t18) goto ie7;
        {
            void * t21 = gm_l4;
            uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t22 >= (uintptr_t)(t23)) gm_trap_index(t22, t23, gm_src7afujsggtlje, 467, 29);
            gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t22 * 88) + 0));
            gm_tg5lql6itufk7 t25 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t21, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tg5lql6itufk7 t26 = t25;
            if (!((((uint8_t *)&t26))[8] != 0)) goto ie20;
            gm_ttovhfee8mf28 t27 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t26));
            gm_l6 = t27;
            void * t28 = gm_l4;
            void * t29 = gm_l3;
            gm_t88rcucg1b7uk t30 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t29) + 0));
            gm_tg5lql6itufk7 t31 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t28, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tg5lql6itufk7 t32 = t31;
            if (!((((uint8_t *)&t32))[8] != 0)) goto ie20;
            gm_ttovhfee8mf28 t33 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t32));
            gm_l7 = t33;
            {
                gm_tr8okf1iilonm t35;
                memset((uint8_t *)&t35 + 28, 0, 4);
                gm_tvdg94bk7aa22 t36 = gm_l1;
                GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t35 + 0, t36);
                gm_ttovhfee8mf28 t37 = gm_l6;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t35 + 12, t37);
                gm_ttovhfee8mf28 t38 = gm_l7;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t35 + 20, t38);
                uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                if (t39 >= (uintptr_t)(t40)) gm_trap_index(t39, t40, gm_src7afujsggtlje, 467, 98);
                void * t41 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t39 * 8));
                GM_ST(void *, (uint8_t *)&t35 + 32, t41);
                gm_tbo4aa19b457h t42;
                memset(&t42, 0, sizeof t42);
                GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)27ULL));
                GM_ST(gm_tr8okf1iilonm, ((uint8_t *)&t42 + 8), t35);
                uint8_t t43 = 0;
                F.ret = t42;
                goto gm_fret;
            }
            goto ix20;
ie20:;
ix20:;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_tvdg94bk7aa22 t45 = gm_l1;
        gm_tq9t4giin3pm0 t46;
        gm_tvdg94bk7aa22 t47;
        GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t47 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)32ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t46 + 0, t47);
        gm_tvdg94bk7aa22 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t48 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t48 + 0), ((uint8_t)33ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t46 + 12, t48);
        gm_tq9t4giin3pm0 t49 = t46;
        uint8_t t50 = 0;
        gm_tvdg94bk7aa22 t51 = t45;
        if (gm_eqg63o53ar52(((uint8_t *)&t49) + 0, ((uint8_t *)&t51))) t50 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t49) + 12, ((uint8_t *)&t51))) t50 = 1;
        uint8_t t52 = (uint8_t)(t50);
        if (!t52) goto ie44;
        uint64_t t53 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t54 = ((t53 == ((uint64_t)2ULL)));
        if (!t54) goto ie44;
        {
            void * t57 = gm_l4;
            uint64_t t58 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t59 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t58 >= (uintptr_t)(t59)) gm_trap_index(t58, t59, gm_src7afujsggtlje, 470, 26);
            gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t58 * 88) + 0));
            gm_tg5lql6itufk7 t61 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t57, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tg5lql6itufk7 t62 = t61;
            if (!((((uint8_t *)&t62))[8] != 0)) goto ie56;
            gm_ttovhfee8mf28 t63 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t62));
            gm_l8 = t63;
            {
                gm_tscalq56heofe t65;
                memset((uint8_t *)&t65 + 20, 0, 4);
                gm_tvdg94bk7aa22 t66 = gm_l1;
                GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t65 + 0, t66);
                gm_ttovhfee8mf28 t67 = gm_l8;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t65 + 12, t67);
                uint64_t t68 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_src7afujsggtlje, 470, 62);
                void * t70 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t68 * 8));
                GM_ST(void *, (uint8_t *)&t65 + 24, t70);
                uint64_t t71 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                if (t71 >= (uintptr_t)(t72)) gm_trap_index(t71, t72, gm_src7afujsggtlje, 470, 69);
                void * t73 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t71 * 8));
                GM_ST(void *, (uint8_t *)&t65 + 32, t73);
                gm_tbo4aa19b457h t74;
                memset(&t74, 0, sizeof t74);
                GM_ST(uint8_t, ((uint8_t *)&t74 + 0), ((uint8_t)28ULL));
                GM_ST(gm_tscalq56heofe, ((uint8_t *)&t74 + 8), t65);
                uint8_t t75 = 0;
                F.ret = t74;
                goto gm_fret;
            }
            goto ix56;
ie56:;
ix56:;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        gm_tvdg94bk7aa22 t77 = gm_l1;
        gm_tpq3gshtivcif t78;
        gm_tvdg94bk7aa22 t79;
        GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t79 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)28ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t78 + 0, t79);
        gm_tvdg94bk7aa22 t80;
        GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t80 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)29ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t78 + 12, t80);
        gm_tvdg94bk7aa22 t81;
        GM_ST(uint8_t, (uint8_t *)&t81 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t81 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t81 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t81 + 0), ((uint8_t)30ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t78 + 24, t81);
        gm_tvdg94bk7aa22 t82;
        GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t82 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)31ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t78 + 36, t82);
        gm_tvdg94bk7aa22 t83;
        GM_ST(uint8_t, (uint8_t *)&t83 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t83 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t83 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t83 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)34ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t78 + 48, t83);
        gm_tpq3gshtivcif t84 = t78;
        uint8_t t85 = 0;
        gm_tvdg94bk7aa22 t86 = t77;
        if (gm_eqg63o53ar52(((uint8_t *)&t84) + 0, ((uint8_t *)&t86))) t85 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t84) + 12, ((uint8_t *)&t86))) t85 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t84) + 24, ((uint8_t *)&t86))) t85 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t84) + 36, ((uint8_t *)&t86))) t85 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t84) + 48, ((uint8_t *)&t86))) t85 = 1;
        uint8_t t87 = (uint8_t)(t85);
        if (!t87) goto ie76;
        uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t89 = ((t88 == ((uint64_t)1ULL)));
        if (!t89) goto ie76;
        {
            void * t92 = gm_l4;
            uint64_t t93 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t93 >= (uintptr_t)(t94)) gm_trap_index(t93, t94, gm_src7afujsggtlje, 473, 26);
            gm_t88rcucg1b7uk t95 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t93 * 88) + 0));
            gm_tg5lql6itufk7 t96 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t92, t95);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tg5lql6itufk7 t97 = t96;
            if (!((((uint8_t *)&t97))[8] != 0)) goto ie91;
            gm_ttovhfee8mf28 t98 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t97));
            gm_l9 = t98;
            {
                gm_ts6ln23edn25a t100;
                memset((uint8_t *)&t100 + 20, 0, 4);
                gm_tvdg94bk7aa22 t101 = gm_l1;
                GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t100 + 0, t101);
                gm_ttovhfee8mf28 t102 = gm_l9;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t100 + 12, t102);
                uint64_t t103 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                if (t103 >= (uintptr_t)(t104)) gm_trap_index(t103, t104, gm_src7afujsggtlje, 473, 63);
                void * t105 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t103 * 8));
                GM_ST(void *, (uint8_t *)&t100 + 24, t105);
                gm_tbo4aa19b457h t106;
                memset(&t106, 0, sizeof t106);
                GM_ST(uint8_t, ((uint8_t *)&t106 + 0), ((uint8_t)29ULL));
                GM_ST(gm_ts6ln23edn25a, ((uint8_t *)&t106 + 8), t100);
                uint8_t t107 = 0;
                F.ret = t106;
                goto gm_fret;
            }
            goto ix91;
ie91:;
ix91:;
        }
        goto ix76;
ie76:;
ix76:;
    }
    {
        gm_tt8o5kebou62r t108;
        memset((uint8_t *)&t108 + 12, 0, 4);
        gm_tvdg94bk7aa22 t109 = gm_l1;
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t108 + 0, t109);
        gm_b16_8 t110 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t108 + 16, t110);
        gm_tbo4aa19b457h t111;
        memset(&t111, 0, sizeof t111);
        GM_ST(uint8_t, ((uint8_t *)&t111 + 0), ((uint8_t)34ULL));
        GM_ST(gm_tt8o5kebou62r, ((uint8_t *)&t111 + 8), t108);
        uint8_t t112 = 0;
        F.ret = t111;
        goto gm_fret;
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
inline __attribute__((aligned(4))) void gm_f_low_coerce_rhvh5p0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tmn7n0n9raou5 a1, void * a2, void * a3) {
    struct gm_fr_low_coerce_rhvh5p0 F;
    void * gm_l0; /* lw */
    gm_tmn7n0n9raou5 gm_l1; /* kind */
    void * gm_l2; /* x */
    void * gm_l3; /* e */
    void * gm_l4; /* m */
    void * gm_l5; /* xn */
    gm_ttovhfee8mf28 gm_l6; /* from */
    gm_ttovhfee8mf28 gm_l7; /* to */
    GM_STACK_CHECK(&F, 511u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l2;
        void * t6 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t6;
    }
    {
        uint8_t t8 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)0ULL)));
        if (!t8) goto ie7;
        {
            void * t11 = gm_l4;
            void * t12 = gm_l2;
            gm_t88rcucg1b7uk t13 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t12) + 0));
            gm_tg5lql6itufk7 t14 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t11, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tg5lql6itufk7 t15 = t14;
            if (!((((uint8_t *)&t15))[8] != 0)) goto ie10;
            gm_ttovhfee8mf28 t16 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t15));
            gm_l6 = t16;
            void * t17 = gm_l4;
            void * t18 = gm_l3;
            gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t18) + 0));
            gm_tg5lql6itufk7 t20 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t17, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tg5lql6itufk7 t21 = t20;
            if (!((((uint8_t *)&t21))[8] != 0)) goto ie10;
            gm_ttovhfee8mf28 t22 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t21));
            gm_l7 = t22;
            {
                gm_tr8okf1iilonm t24;
                memset((uint8_t *)&t24 + 28, 0, 4);
                gm_tvdg94bk7aa22 t25;
                GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t25 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t24 + 0, t25);
                gm_ttovhfee8mf28 t26 = gm_l6;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t24 + 12, t26);
                gm_ttovhfee8mf28 t27 = gm_l7;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t24 + 20, t27);
                void * t28 = gm_l5;
                GM_ST(void *, (uint8_t *)&t24 + 32, t28);
                gm_tbo4aa19b457h t29;
                memset(&t29, 0, sizeof t29);
                GM_ST(uint8_t, ((uint8_t *)&t29 + 0), ((uint8_t)27ULL));
                GM_ST(gm_tr8okf1iilonm, ((uint8_t *)&t29 + 8), t24);
                uint8_t t30 = 0;
                F.ret = t29;
                goto gm_fret;
            }
            goto ix10;
ie10:;
ix10:;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_tmn7n0n9raou5 t31 = gm_l1;
        void * t32 = gm_l5;
        gm_t8jsc4n9vp2ib t33;
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t33 + 0), t31);
        GM_ST(void *, ((uint8_t *)&t33 + 8), t32);
        gm_tbo4aa19b457h t34;
        memset(&t34, 0, sizeof t34);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)33ULL));
        GM_ST(gm_t8jsc4n9vp2ib, ((uint8_t *)&t34 + 8), t33);
        uint8_t t35 = 0;
        F.ret = t34;
        goto gm_fret;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_low_conds_1olabjo(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_low_conds_1olabjo F;
    void * gm_l0; /* lw */
    gm_b16_8 gm_l1; /* parts */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tlr9dp305htm7 gm_l5; /* c */
    void * gm_l6; /* e */
    void * gm_l7; /* pat */
    void * gm_l8; /* value */
    gm_tlr9dp305htm7 gm_l9; /* x */
    GM_STACK_CHECK(&F, 464u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_r36qhtc((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_l3 = t3;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l4;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l4;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tlr9dp305htm7 t15 = GM_LD(gm_tlr9dp305htm7, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 24));
                    gm_l5 = t15;
                    {
                        gm_tlr9dp305htm7 t17;
                        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t20 != ((uint8_t)0ULL)) goto mn19;
                        void * t21 = GM_LD(void *, (((uint8_t *)&gm_l5) + 8));
                        gm_l6 = t21;
                        void * t22 = gm_l0;
                        void * t23 = gm_l6;
                        void * t24 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t22, t23);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tlr9dp305htm7 t25;
                        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t25 + 16, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)0ULL));
                        GM_ST(void *, ((uint8_t *)&t25 + 8), t24);
                        t17 = t25;
                        goto me18;
mn19:;
                        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l5) + 0));
                        if (t27 != ((uint8_t)1ULL)) goto mn26;
                        void * t28 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 0));
                        gm_l7 = t28;
                        void * t29 = GM_LD(void *, ((((uint8_t *)&gm_l5) + 8) + 8));
                        gm_l8 = t29;
                        void * t30 = gm_l0;
                        void * t31 = gm_l7;
                        void * t32 = gm_f_low_pat_bba97qc((void *)NULL, cx, NULL, t30, t31);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        void * t33 = gm_l0;
                        void * t34 = gm_l8;
                        void * t35 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t33, t34);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_t3mf58gv9kf3n t36;
                        GM_ST(void *, ((uint8_t *)&t36 + 0), t32);
                        GM_ST(void *, ((uint8_t *)&t36 + 8), t35);
                        gm_tlr9dp305htm7 t37;
                        GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t37 + 8), t36);
                        t17 = t37;
                        goto me18;
mn26:;
                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 508, 19);
me18:;
                        gm_l9 = t17;
                    }
                    {
                        void * t38 = (void *)((uint8_t *)&F.l2);
                        gm_tftb1odc739t5 t39 = gm_f_push_lq94qhs(t38, &(*(gm_tlr9dp305htm7 *)((uint8_t *)&gm_l9)));
                        gm_tftb1odc739t5 t40 = t39;
                        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_src7afujsggtlje, 512, 9);
                        gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 507, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t43 = gm_l4;
                    uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                    uint64_t t46 = r44;
                    gm_l4 = t46;
                }
            }
gm_ll8:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb4; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc4; }
            }
            goto gm_lad;
lb4:;
        }
    }
    {
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        uint8_t t48 = 0;
        F.ret = t47;
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
__attribute__((aligned(4))) void * gm_f_low_pat_bba97qc(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_low_pat_bba97qc F;
    void * gm_l0; /* lw */
    void * gm_l1; /* p */
    void * gm_l2; /* m */
    uint32_t gm_l3; /* l */
    void * gm_l4; /* x */
    void * gm_l6; /* x */
    void * gm_l7; /* x */
    void * gm_l8; /* x */
    uint32_t gm_l9; /* variant */
    void * gm_l10; /* payload */
    gm_t57euh612ligr gm_l11; /* u */
    gm_t88rcucg1b7uk gm_l12; /* tag_ty */
    unsigned __int128 gm_l13; /* tag */
    uint64_t gm_l14; /* size */
    gm_tcmkompbl09il gm_l15; /* d */
    gm_t88rcucg1b7uk gm_l16; /* pt */
    uint64_t gm_l17; /* ts */
    uint64_t gm_l18; /* tag_w */
    void * gm_l19; /* x */
    gm_b16_8 gm_l20; /* fields */
    gm_b16_8 gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    gm_ttggq7ng4kp3p gm_l24; /* f */
    gm_b16_8 gm_l25; /* alts */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    void * gm_l29; /* a */
    GM_STACK_CHECK(&F, 3776u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l1;
        gm_tlpa5tka3taqb t5;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        switch (t19) { case ((uint8_t)0ULL): goto ma7; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma14; case ((uint8_t)8ULL): goto ma15; case ((uint8_t)9ULL): goto ma16; case ((uint8_t)10ULL): goto ma17; default: goto ma18; }
ma7:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t21 != ((uint8_t)0ULL)) goto mn20;
        gm_tlpa5tka3taqb t22;
        memset(&t22, 0, sizeof t22);
        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
        t5 = t22;
        goto me6;
mn20:;
ma8:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t24 != ((uint8_t)1ULL)) goto mn23;
        uint32_t t25 = GM_LD(uint32_t, ((((uint8_t *)t4) + 8) + 8));
        gm_l3 = t25;
        uint32_t t26 = gm_l3;
        gm_tlpa5tka3taqb t27;
        memset(&t27, 0, sizeof t27);
        GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)1ULL));
        GM_ST(uint32_t, ((uint8_t *)&t27 + 16), t26);
        t5 = t27;
        goto me6;
mn23:;
ma9:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t29 != ((uint8_t)2ULL)) goto mn28;
        void * t30 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l4 = t30;
        void * t31 = gm_l0;
        void * t32 = gm_l4;
        void * t33 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t31, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tlpa5tka3taqb t34;
        memset(&t34, 0, sizeof t34);
        GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)2ULL));
        GM_ST(void *, ((uint8_t *)&t34 + 16), t33);
        t5 = t34;
        goto me6;
mn28:;
ma10:;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t36 != ((uint8_t)3ULL)) goto mn35;
        gm_t59juu943t8sl t37 = GM_LD(gm_t59juu943t8sl, ((((uint8_t *)t4) + 8) + 8));
        F.l5 = t37;
        gm_t59juu943t8sl t38;
        memset((uint8_t *)&t38 + 97, 0, 7);
        memcpy((uint8_t *)&t38 + 0, (((uint8_t *)&F.l5) + 0), 48);
        memcpy((uint8_t *)&t38 + 48, (((uint8_t *)&F.l5) + 48), 48);
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&F.l5) + 96));
        GM_ST(uint8_t, (uint8_t *)&t38 + 96, t39);
        gm_tlpa5tka3taqb t40;
        GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t40 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t40 + 120, 0);
        GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)3ULL));
        GM_ST(gm_t59juu943t8sl, ((uint8_t *)&t40 + 16), t38);
        t5 = t40;
        goto me6;
mn35:;
ma11:;
        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t42 != ((uint8_t)4ULL)) goto mn41;
        gm_tlpa5tka3taqb t43;
        memset(&t43, 0, sizeof t43);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)4ULL));
        t5 = t43;
        goto me6;
mn41:;
ma12:;
        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t45 != ((uint8_t)5ULL)) goto mn44;
        void * t46 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l6 = t46;
        void * t47 = gm_l0;
        void * t48 = gm_l6;
        void * t49 = gm_f_low_pat_bba97qc((void *)NULL, cx, NULL, t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tlpa5tka3taqb t50;
        memset(&t50, 0, sizeof t50);
        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)5ULL));
        GM_ST(void *, ((uint8_t *)&t50 + 16), t49);
        t5 = t50;
        goto me6;
mn44:;
ma13:;
        uint8_t t52 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t52 != ((uint8_t)6ULL)) goto mn51;
        void * t53 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l7 = t53;
        void * t54 = gm_l0;
        void * t55 = gm_l7;
        void * t56 = gm_f_low_pat_bba97qc((void *)NULL, cx, NULL, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tlpa5tka3taqb t57;
        memset(&t57, 0, sizeof t57);
        GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)6ULL));
        GM_ST(void *, ((uint8_t *)&t57 + 16), t56);
        t5 = t57;
        goto me6;
mn51:;
ma14:;
        uint8_t t59 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t59 != ((uint8_t)7ULL)) goto mn58;
        void * t60 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l8 = t60;
        void * t61 = gm_l0;
        void * t62 = gm_l8;
        void * t63 = gm_f_low_pat_bba97qc((void *)NULL, cx, NULL, t61, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tlpa5tka3taqb t64;
        memset(&t64, 0, sizeof t64);
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)7ULL));
        GM_ST(void *, ((uint8_t *)&t64 + 16), t63);
        t5 = t64;
        goto me6;
mn58:;
ma15:;
        uint8_t t66 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t66 != ((uint8_t)8ULL)) goto mn65;
        uint32_t t67 = GM_LD(uint32_t, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l9 = t67;
        void * t68 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l10 = t68;
        {
            void * t70 = gm_l2;
            void * t71 = gm_l1;
            gm_t88rcucg1b7uk t72 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t71) + 0));
            gm_f_union_def_h8mvf4t((void *)&gm_l11, t70, GM_CONV(gm_csguankko4lmhd, t72));
        }
        {
            void * t73 = gm_l1;
            gm_t88rcucg1b7uk t74 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t73) + 0));
            gm_l12 = t74;
        }
        {
            gm_l13 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
        }
        {
            gm_l14 = ((uint64_t)0ULL);
        }
        {
            if (!((((uint8_t *)&gm_l11))[24] != 0)) goto ie75;
            gm_tcmkompbl09il t76 = GM_LD(gm_tcmkompbl09il, ((uint8_t *)&gm_l11));
            gm_l15 = t76;
            {
                gm_t88rcucg1b7uk t78 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l15) + 0));
                gm_l12 = t78;
            }
            {
                gm_b16_8 t79 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                gm_b16_8 t80 = t79;
                uint32_t t81 = gm_l9;
                uint64_t t82 = (uint64_t)t81;
                uint64_t t83 = gm_f_us_cuv01fp(t82);
                uint64_t t84 = (uintptr_t)t83;
                gm_b16_8 t85 = t80;
                uint64_t t86 = GM_LD(uintptr_t, ((uint8_t *)&t85) + 8);
                if (t84 >= (uintptr_t)(t86)) gm_trap_index(t84, t86, gm_src7afujsggtlje, 535, 38);
                gm_tbci5ospk5nnn t87 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t85)) + t84 * 72) + 24));
                unsigned __int128 t88 = gm_f_bits128_jigkui4(&t87);
                gm_l13 = t88;
            }
            {
                gm_b16_8 t90 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                gm_b16_8 t91 = t90;
                uint32_t t92 = gm_l9;
                uint64_t t93 = (uint64_t)t92;
                uint64_t t94 = gm_f_us_cuv01fp(t93);
                uint64_t t95 = (uintptr_t)t94;
                gm_b16_8 t96 = t91;
                uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&t96) + 8);
                if (t95 >= (uintptr_t)(t97)) gm_trap_index(t95, t97, gm_src7afujsggtlje, 536, 26);
                if (!(((((uint8_t *)GM_LD(void *, ((uint8_t *)&t96)) + t95 * 72) + 16))[4] != 0)) goto ie89;
                gm_t88rcucg1b7uk t98 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t96)) + t95 * 72) + 16));
                gm_l16 = t98;
                {
                    void * t100 = gm_l2;
                    gm_t88rcucg1b7uk t101 = gm_l16;
                    uint64_t t102 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t100, t101);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l14 = t102;
                }
                goto ix89;
ie89:;
ix89:;
            }
            goto ix75;
ie75:;
ix75:;
        }
        {
            void * t103 = gm_l2;
            gm_t88rcucg1b7uk t104 = gm_l12;
            uint64_t t105 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t103, t104);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t105;
        }
        {
            unsigned __int128 t106 = gm_l13;
            uint64_t t107 = (uint64_t)t106;
            uint64_t t108 = gm_l17;
            uint64_t r109; bool o110 = __builtin_mul_overflow(((uint64_t)8ULL), t108, &r109);
            if (o110) gm_trap("integer overflow in `*`", gm_src7afujsggtlje, 540, 47);
            uint64_t t111 = r109;
            uint64_t t112 = (t111 < ((uint64_t)64ULL) ? t111 : ((uint64_t)64ULL));
            uint32_t t113 = (uint32_t)t112;
            uint64_t t114 = gm_f_mask64_vvoc1f6(t113);
            uint64_t t115 = (uint64_t)(t107 & t114);
            gm_l18 = t115;
        }
        gm_t4nomjfe3hlif t116;
        uint32_t t117 = gm_l9;
        GM_ST(uint32_t, (uint8_t *)&t116 + 0, t117);
        gm_t88rcucg1b7uk t118 = gm_l12;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t116 + 4, t118);
        uint64_t t119 = gm_l17;
        GM_ST(uint64_t, (uint8_t *)&t116 + 8, t119);
        unsigned __int128 t120 = gm_l13;
        GM_ST(unsigned __int128, (uint8_t *)&t116 + 16, t120);
        uint64_t t121 = gm_l18;
        GM_ST(uint64_t, (uint8_t *)&t116 + 32, t121);
        void * t122 = gm_l2;
        void * t123 = gm_l1;
        gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t123) + 0));
        uint64_t t125 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t122, t124);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint64_t, (uint8_t *)&t116 + 40, t125);
        uint64_t t126 = gm_l14;
        GM_ST(uint64_t, (uint8_t *)&t116 + 48, t126);
        void * t127;
        void * t129 = gm_l10;
        void * t130 = t129;
        if (!(GM_LD(void *, ((uint8_t *)&t130)) != 0)) goto ie128;
        void * t131 = GM_LD(void *, ((uint8_t *)&t130));
        gm_l19 = t131;
        void * t133 = gm_l0;
        void * t134 = gm_l19;
        void * t135 = gm_f_low_pat_bba97qc((void *)NULL, cx, NULL, t133, t134);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t136 = t135;
        t127 = t136;
        goto ix128;
ie128:;
        void * t138 = ((void *)0);
        t127 = t138;
ix128:;
        GM_ST(void *, (uint8_t *)&t116 + 56, t127);
        gm_tlpa5tka3taqb t139;
        memset(&t139, 0, sizeof t139);
        GM_ST(uint8_t, ((uint8_t *)&t139 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t4nomjfe3hlif, ((uint8_t *)&t139 + 16), t116);
        gm_tlpa5tka3taqb t140 = t139;
        t5 = t140;
        goto me6;
mn65:;
ma16:;
        uint8_t t142 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t142 != ((uint8_t)9ULL)) goto mn141;
        gm_b16_8 t143 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l20 = t143;
        {
            gm_f_list_31hegji((void *)&F.l21, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t146 = gm_l20;
                gm_l22 = t146;
            }
            {
                gm_l23 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t148 = gm_l23;
                    uint64_t t149 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                    uint8_t t150 = (t148 < t149);
                    if (!t150) goto lb147;
                    {
                        uint64_t t153 = gm_l23;
                        uint64_t t154 = (uintptr_t)t153;
                        uint64_t t155 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                        gm_ttggq7ng4kp3p t158 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t154 * 16));
                        gm_l24 = t158;
                        {
                            void * t160 = (void *)((uint8_t *)&F.l21);
                            uint32_t t161 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 0));
                            void * t162 = gm_l0;
                            void * t163 = GM_LD(void *, (((uint8_t *)&gm_l24) + 8));
                            void * t164 = gm_f_low_pat_bba97qc((void *)NULL, cx, NULL, t162, t163);
                            if (gm_unw.kind) {
                                goto gm_ll151;
                            }
                            gm_ttggq7ng4kp3p t165;
                            GM_ST(uint32_t, (uint8_t *)&t165 + 4, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t165 + 0), t161);
                            GM_ST(void *, ((uint8_t *)&t165 + 8), t164);
                            gm_tftb1odc739t5 t166 = gm_f_push_69l8e45(t160, t165);
                            gm_tftb1odc739t5 t167 = t166;
                            if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 1, gm_src7afujsggtlje, 545, 31);
                            gm_b0_1 t168 = GM_LD(gm_b0_1, (((uint8_t *)&t167) + 1));
                        }
                        goto me156;
mn157:;
                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 545, 13);
me156:;
                    }
lc147:;
                    {
                        uint64_t t170 = gm_l23;
                        uint64_t r171; bool o172 = __builtin_add_overflow(t170, ((uint64_t)1ULL), &r171);
                        uint64_t t173 = r171;
                        gm_l23 = t173;
                    }
                }
gm_ll151:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb147; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc147; }
                }
                goto gm_lad;
lb147:;
            }
        }
        gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
        gm_tlpa5tka3taqb t175;
        memset(&t175, 0, sizeof t175);
        GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)9ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t175 + 16), t174);
        gm_tlpa5tka3taqb t176 = t175;
        t5 = t176;
        goto me6;
mn141:;
ma17:;
        uint8_t t178 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t178 != ((uint8_t)10ULL)) goto mn177;
        gm_b16_8 t179 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l25 = t179;
        {
            gm_f_list_nh3d5t2_2((void *)&F.l26, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t182 = gm_l25;
                gm_l27 = t182;
            }
            {
                gm_l28 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t184 = gm_l28;
                    uint64_t t185 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                    uint8_t t186 = (t184 < t185);
                    if (!t186) goto lb183;
                    {
                        uint64_t t189 = gm_l28;
                        uint64_t t190 = (uintptr_t)t189;
                        uint64_t t191 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                        void * t192 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t190 * 8));
                        void * t193 = t192;
                        void * t196 = GM_LD(void *, ((uint8_t *)&t193));
                        gm_l29 = t196;
                        {
                            void * t198 = (void *)((uint8_t *)&F.l26);
                            void * t199 = gm_l0;
                            void * t200 = gm_l29;
                            void * t201 = gm_f_low_pat_bba97qc((void *)NULL, cx, NULL, t199, t200);
                            if (gm_unw.kind) {
                                goto gm_ll187;
                            }
                            gm_tftb1odc739t5 t202 = gm_f_push_u2jka7o_2(t198, t201);
                            gm_tftb1odc739t5 t203 = t202;
                            if (((((uint8_t *)&t203))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t203) + 1, gm_src7afujsggtlje, 550, 29);
                            gm_b0_1 t204 = GM_LD(gm_b0_1, (((uint8_t *)&t203) + 1));
                        }
                        goto me194;
mn195:;
                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 550, 13);
me194:;
                    }
lc183:;
                    {
                        uint64_t t206 = gm_l28;
                        uint64_t r207; bool o208 = __builtin_add_overflow(t206, ((uint64_t)1ULL), &r207);
                        uint64_t t209 = r207;
                        gm_l28 = t209;
                    }
                }
gm_ll187:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb183; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc183; }
                }
                goto gm_lad;
lb183:;
            }
        }
        gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
        gm_tlpa5tka3taqb t211;
        memset(&t211, 0, sizeof t211);
        GM_ST(uint8_t, ((uint8_t *)&t211 + 0), ((uint8_t)10ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t211 + 16), t210);
        gm_tlpa5tka3taqb t212 = t211;
        t5 = t212;
        goto me6;
mn177:;
ma18:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 519, 21);
me6:;
        F.l30 = t5;
    }
    {
        gm_tdgedp3mu9p1a t213;
        memset((uint8_t *)&t213 + 4, 0, 12);
        memset((uint8_t *)&t213 + 156, 0, 4);
        void * t214 = gm_l1;
        gm_t88rcucg1b7uk t215 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t214) + 0));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t213 + 0, t215);
        memcpy((uint8_t *)&t213 + 16, ((uint8_t *)&F.l30), 128);
        void * t216 = gm_l1;
        gm_tl874t2atnlh3 t217 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t216) + 120));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t213 + 144, t217);
        gm_tq7s98ihonkju t218 = gm_f_new_1b2khgi_2((void *)NULL, cx, NULL, &t213, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k4j26j8fh8s)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t219 = t218;
        if (((((uint8_t *)&t219))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t219) + 8, gm_src7afujsggtlje, 554, 12);
        void * t220 = GM_LD(void *, (((uint8_t *)&t219) + 8));
        uint8_t t221 = 0;
        F.ret = t220;
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
inline __attribute__((aligned(4))) void gm_f_low_unary_3f5k6s5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, void * a3) {
    struct gm_fr_low_unary_3f5k6s5 F;
    void * gm_l0; /* lw */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    void * gm_l2; /* x */
    void * gm_l3; /* e */
    void * gm_l4; /* m */
    void * gm_l5; /* xn */
    gm_ttovhfee8mf28 gm_l6; /* t */
    GM_STACK_CHECK(&F, 507u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_l4 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = gm_l2;
        void * t6 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t6;
    }
    {
        uint8_t t8 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)2ULL)));
        if (!t8) goto ie7;
        {
            void * t10 = gm_l5;
            gm_tbo4aa19b457h t11;
            memset(&t11, 0, sizeof t11);
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)25ULL));
            GM_ST(void *, ((uint8_t *)&t11 + 8), t10);
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        void * t14 = gm_l4;
        void * t15 = gm_l2;
        gm_t88rcucg1b7uk t16 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t15) + 0));
        gm_tg5lql6itufk7 t17 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t14, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tg5lql6itufk7 t18 = t17;
        if (!((((uint8_t *)&t18))[8] != 0)) goto ie13;
        gm_ttovhfee8mf28 t19 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t18));
        gm_l6 = t19;
        {
            gm_t9s8r45p2ijj5 t21;
            memset((uint8_t *)&t21 + 1, 0, 3);
            memset((uint8_t *)&t21 + 12, 0, 4);
            gm_t9qkh0rsooc8d t22 = gm_l1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t21 + 0, t22);
            gm_ttovhfee8mf28 t23 = gm_l6;
            GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t21 + 4, t23);
            void * t24 = gm_l5;
            GM_ST(void *, (uint8_t *)&t21 + 16, t24);
            gm_tbo4aa19b457h t25;
            memset(&t25, 0, sizeof t25);
            GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)26ULL));
            GM_ST(gm_t9s8r45p2ijj5, ((uint8_t *)&t25 + 8), t21);
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        gm_t9qkh0rsooc8d t27 = gm_l1;
        void * t28 = gm_l5;
        gm_t8ghgl2jj0qha t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 0), t27);
        GM_ST(void *, ((uint8_t *)&t29 + 8), t28);
        gm_tbo4aa19b457h t30;
        memset(&t30, 0, sizeof t30);
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)30ULL));
        GM_ST(gm_t8ghgl2jj0qha, ((uint8_t *)&t30 + 8), t29);
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
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
extern __attribute__((aligned(4))) void * gm_f_lower_body_g4bo546(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) void * gm_f_mk_node_ndavg23(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_mk_node_ndavg23 F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 293u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_tv8a4cs3pcpgp t1;
    memset((uint8_t *)&t1 + 100, 0, 4);
    memset((uint8_t *)&t1 + 125, 0, 3);
    memcpy((uint8_t *)&t1 + 0, ((uint8_t *)a2), 96);
    void * t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t2) + 0));
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t1 + 96, t3);
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    void * t6 = gm_l1;
    gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t6) + 0));
    uint64_t t8 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t5, t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint64_t, (uint8_t *)&t1 + 104, t8);
    void * t9 = gm_l1;
    gm_tl874t2atnlh3 t10 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t9) + 72));
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 112, t10);
    void * t11 = gm_l0;
    void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
    void * t13 = gm_l1;
    uint8_t t14 = gm_f_is_place_expr_qlfi6ot(t12, t13);
    GM_ST(uint8_t, (uint8_t *)&t1 + 124, t14);
    gm_tq7s98ihonkju t15 = gm_f_new_her8si0((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ktu15664o6a)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t16 = t15;
    if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_src7afujsggtlje, 243, 75);
    void * t17 = GM_LD(void *, (((uint8_t *)&t16) + 8));
    F.ret = t17;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void * gm_f_opt_expr_gr6e3ks(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_seq_kind_405ip0f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_seq_kind_405ip0f F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* len */
    gm_t88rcucg1b7uk gm_l3; /* pointee */
    uint64_t gm_l4; /* len */
    GM_STACK_CHECK_SHALLOW(&F, 540u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_t53at80pfdus3 t4;
        gm_f_kind_htv6093((void *)&t4, t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_t53at80pfdus3 t5 = t4;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma9; case ((uint8_t)10ULL): goto ma8; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma11; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma7:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t15 != ((uint8_t)11ULL)) goto mn14;
        uint64_t t16 = GM_LD(uint64_t, ((((uint8_t *)&t5) + 8) + 0));
        gm_l2 = t16;
        gm_t9qkh0rsooc8d t17;
        GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)0ULL));
        uint64_t t18 = gm_l2;
        gm_te0mgnjdphs16 t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 0), t17);
        GM_ST(uint64_t, ((uint8_t *)&t19 + 8), t18);
        gm_thdrvp9q0u7b5 t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 20, 0);
        GM_ST(gm_te0mgnjdphs16, ((uint8_t *)&t20), t19);
        ((uint8_t *)&t20)[16] = 1;
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
        goto me6;
mn14:;
ma8:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t23 != ((uint8_t)10ULL)) goto mn22;
        gm_t9qkh0rsooc8d t24;
        GM_ST(uint8_t, ((uint8_t *)&t24 + 0), ((uint8_t)1ULL));
        gm_te0mgnjdphs16 t25;
        GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t25 + 0), t24);
        GM_ST(uint64_t, ((uint8_t *)&t25 + 8), ((uint64_t)0ULL));
        gm_thdrvp9q0u7b5 t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 20, 0);
        GM_ST(gm_te0mgnjdphs16, ((uint8_t *)&t26), t25);
        ((uint8_t *)&t26)[16] = 1;
        uint8_t t27 = 0;
        F.ret = t26;
        goto gm_fret;
        goto me6;
mn22:;
ma9:;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t29 != ((uint8_t)9ULL)) goto mn28;
        gm_t9qkh0rsooc8d t30;
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)2ULL));
        gm_te0mgnjdphs16 t31;
        GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 0), t30);
        GM_ST(uint64_t, ((uint8_t *)&t31 + 8), ((uint64_t)0ULL));
        gm_thdrvp9q0u7b5 t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 20, 0);
        GM_ST(gm_te0mgnjdphs16, ((uint8_t *)&t32), t31);
        ((uint8_t *)&t32)[16] = 1;
        uint8_t t33 = 0;
        F.ret = t32;
        goto gm_fret;
        goto me6;
mn28:;
ma10:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t35 != ((uint8_t)8ULL)) goto mn34;
        gm_t88rcucg1b7uk t36 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t5) + 8) + 4));
        gm_l3 = t36;
        {
            void * t39 = gm_l0;
            gm_t88rcucg1b7uk t40 = gm_l3;
            gm_t53at80pfdus3 t41;
            gm_f_kind_htv6093((void *)&t41, t39, GM_CONV(gm_csguankko4lmhd, t40));
            gm_t53at80pfdus3 t42 = t41;
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
            if (t43 != ((uint8_t)11ULL)) goto ie38;
            uint64_t t44 = GM_LD(uint64_t, ((((uint8_t *)&t42) + 8) + 0));
            gm_l4 = t44;
            {
                gm_t9qkh0rsooc8d t46;
                GM_ST(uint8_t, ((uint8_t *)&t46 + 0), ((uint8_t)3ULL));
                uint64_t t47 = gm_l4;
                gm_te0mgnjdphs16 t48;
                GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t48 + 0), t46);
                GM_ST(uint64_t, ((uint8_t *)&t48 + 8), t47);
                gm_thdrvp9q0u7b5 t49;
                GM_ST(uint8_t, (uint8_t *)&t49 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t49 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t49 + 20, 0);
                GM_ST(gm_te0mgnjdphs16, ((uint8_t *)&t49), t48);
                ((uint8_t *)&t49)[16] = 1;
                uint8_t t50 = 0;
                F.ret = t49;
                goto gm_fret;
            }
            goto ix38;
ie38:;
            {
                gm_thdrvp9q0u7b5 t52;
                memset(&t52, 0, sizeof t52);
                uint8_t t53 = 0;
                F.ret = t52;
                goto gm_fret;
            }
ix38:;
        }
        goto me6;
mn34:;
ma11:;
        gm_thdrvp9q0u7b5 t55;
        memset(&t55, 0, sizeof t55);
        uint8_t t56 = 0;
        F.ret = t55;
        goto gm_fret;
        goto me6;
mn54:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 259, 5);
me6:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tbgiuvo2j5vcs gm_f_word_cmp_s2rbdbn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_word_cmp_s2rbdbn F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_ttovhfee8mf28 gm_l2; /* w */
    uint16_t gm_l3; /* w */
    GM_STACK_CHECK(&F, 463u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_tg5lql6itufk7 t5 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tg5lql6itufk7 t6 = t5;
        if (!((((uint8_t *)&t6))[8] != 0)) goto ie2;
        gm_ttovhfee8mf28 t7 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t6));
        gm_l2 = t7;
        {
            gm_tbgiuvo2j5vcs t9;
            uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (!t11) goto ie10;
            uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            gm_tmn7n0n9raou5 t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t14 + 0), ((uint8_t)1ULL));
            GM_ST(uint32_t, ((uint8_t *)&t14 + 4), t13);
            gm_tmn7n0n9raou5 t15 = t14;
            gm_tbgiuvo2j5vcs t16;
            GM_ST(uint8_t, (uint8_t *)&t16 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t16 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t16), t15);
            ((uint8_t *)&t16)[8] = 1;
            t9 = t16;
            goto ix10;
ie10:;
            gm_tmn7n0n9raou5 t18;
            GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t18 + 0), ((uint8_t)0ULL));
            gm_tmn7n0n9raou5 t19 = t18;
            gm_tbgiuvo2j5vcs t20;
            GM_ST(uint8_t, (uint8_t *)&t20 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t20 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t20), t19);
            ((uint8_t *)&t20)[8] = 1;
            t9 = t20;
ix10:;
            uint8_t t21 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l0;
        gm_t88rcucg1b7uk t23 = gm_l1;
        gm_t53at80pfdus3 t24;
        gm_f_kind_htv6093((void *)&t24, t22, GM_CONV(gm_csguankko4lmhd, t23));
        gm_t53at80pfdus3 t25 = t24;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        switch (t32) { case ((uint8_t)0ULL): goto ma30; case ((uint8_t)1ULL): goto ma30; case ((uint8_t)2ULL): goto ma30; case ((uint8_t)3ULL): goto ma27; case ((uint8_t)4ULL): goto ma28; case ((uint8_t)5ULL): goto ma30; case ((uint8_t)6ULL): goto ma30; case ((uint8_t)7ULL): goto ma29; case ((uint8_t)8ULL): goto ma29; case ((uint8_t)9ULL): goto ma29; case ((uint8_t)10ULL): goto ma30; case ((uint8_t)11ULL): goto ma30; case ((uint8_t)12ULL): goto ma30; case ((uint8_t)13ULL): goto ma30; case ((uint8_t)14ULL): goto ma30; case ((uint8_t)15ULL): goto ma30; case ((uint8_t)16ULL): goto ma29; case ((uint8_t)17ULL): goto ma30; case ((uint8_t)18ULL): goto ma30; case ((uint8_t)19ULL): goto ma30; case ((uint8_t)20ULL): goto ma30; case ((uint8_t)21ULL): goto ma30; case ((uint8_t)22ULL): goto ma30; case ((uint8_t)23ULL): goto ma30; case ((uint8_t)24ULL): goto ma30; case ((uint8_t)25ULL): goto ma30; case ((uint8_t)26ULL): goto ma30; default: goto ma30; }
ma27:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        if (t34 != ((uint8_t)3ULL)) goto mn33;
        uint16_t t35 = GM_LD(uint16_t, (((uint8_t *)&t25) + 8));
        gm_l3 = t35;
        gm_tbgiuvo2j5vcs t36;
        uint16_t t38 = gm_l3;
        uint8_t t39 = ((t38 == ((uint16_t)32ULL)));
        if (!t39) goto ie37;
        gm_tmn7n0n9raou5 t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)2ULL));
        gm_tmn7n0n9raou5 t42 = t41;
        gm_tbgiuvo2j5vcs t43;
        GM_ST(uint8_t, (uint8_t *)&t43 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t43 + 10, 0);
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t43), t42);
        ((uint8_t *)&t43)[8] = 1;
        t36 = t43;
        goto ix37;
ie37:;
        gm_tbgiuvo2j5vcs t44;
        uint16_t t46 = gm_l3;
        uint8_t t47 = ((t46 == ((uint16_t)64ULL)));
        if (!t47) goto ie45;
        gm_tmn7n0n9raou5 t49;
        GM_ST(uint8_t, (uint8_t *)&t49 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t49 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t49 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t49 + 0), ((uint8_t)3ULL));
        gm_tmn7n0n9raou5 t50 = t49;
        gm_tbgiuvo2j5vcs t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t51 + 10, 0);
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t51), t50);
        ((uint8_t *)&t51)[8] = 1;
        t44 = t51;
        goto ix45;
ie45:;
        gm_tbgiuvo2j5vcs t53;
        memset(&t53, 0, sizeof t53);
        gm_tbgiuvo2j5vcs t54 = t53;
        t44 = t54;
ix45:;
        t36 = t44;
ix37:;
        uint8_t t55 = 0;
        F.ret = t36;
        goto gm_fret;
        goto me26;
mn33:;
ma28:;
        uint8_t t57 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        if (t57 != ((uint8_t)4ULL)) goto mn56;
        gm_tmn7n0n9raou5 t58;
        GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)4ULL));
        gm_tbgiuvo2j5vcs t59;
        GM_ST(uint8_t, (uint8_t *)&t59 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t59 + 10, 0);
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t59), t58);
        ((uint8_t *)&t59)[8] = 1;
        uint8_t t60 = 0;
        F.ret = t59;
        goto gm_fret;
        goto me26;
mn56:;
ma29:;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        if (t64 != ((uint8_t)8ULL)) goto pa63;
        goto po62;
pa63:;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        if (t66 != ((uint8_t)9ULL)) goto pa65;
        goto po62;
pa65:;
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        if (t68 != ((uint8_t)7ULL)) goto pa67;
        goto po62;
pa67:;
        uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t25) + 0));
        if (t70 != ((uint8_t)16ULL)) goto pa69;
        goto po62;
pa69:;
        goto mn61;
po62:;
        gm_tmn7n0n9raou5 t71;
        GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)0ULL));
        gm_tbgiuvo2j5vcs t72;
        GM_ST(uint8_t, (uint8_t *)&t72 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t72 + 10, 0);
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t72), t71);
        ((uint8_t *)&t72)[8] = 1;
        uint8_t t73 = 0;
        F.ret = t72;
        goto gm_fret;
        goto me26;
mn61:;
ma30:;
        gm_tbgiuvo2j5vcs t75;
        memset(&t75, 0, sizeof t75);
        uint8_t t76 = 0;
        F.ret = t75;
        goto gm_fret;
        goto me26;
mn74:;
ma31:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 229, 5);
me26:;
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

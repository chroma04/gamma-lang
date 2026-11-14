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
typedef struct { _Alignas(8) uint8_t b[16]; } gm_b16_8;
typedef struct gm_cs6mnftt383dnt { void * f0; void * f1; } gm_cs6mnftt383dnt;
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; uint64_t f2; uint8_t f3; uint8_t pe[7]; } gm_t1j70din2upc4;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { uint32_t f0; uint8_t p1[4]; uint64_t f1; } gm_tb31k5l794t7p;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { void * f0; uint64_t f1; } gm_ttf6i6v1da8cm;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_cs6mnftt383dnt) == 16 && _Alignof(gm_cs6mnftt383dnt) == 8 && offsetof(gm_cs6mnftt383dnt, f0) == 0 && offsetof(gm_cs6mnftt383dnt, f1) == 8, "layout of `Allocator`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
_Static_assert(sizeof(gm_t1j70din2upc4) == 32 && _Alignof(gm_t1j70din2upc4) == 8 && offsetof(gm_t1j70din2upc4, f0) == 0 && offsetof(gm_t1j70din2upc4, f1) == 8 && offsetof(gm_t1j70din2upc4, f2) == 16 && offsetof(gm_t1j70din2upc4, f3) == 24, "layout of `(base: ^Node, index: u32, off: u64, packed: bool)`");
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
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
_Static_assert(sizeof(gm_tb31k5l794t7p) == 16 && _Alignof(gm_tb31k5l794t7p) == 8 && offsetof(gm_tb31k5l794t7p, f0) == 0 && offsetof(gm_tb31k5l794t7p, f1) == 8, "layout of `Char`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { void * f0; uint32_t f1; uint32_t f2; gm_t88rcucg1b7uk f3; uint8_t pe[4]; } gm_tdvcgvrf81sbd;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { void * f0; void * f1; gm_t9qkh0rsooc8d f2; uint8_t p3[7]; uint64_t f3; uint64_t f4; } gm_tp0fa5qv7rqnn;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; gm_t9qkh0rsooc8d f4; uint8_t p5[6]; uint64_t f5; uint64_t f6; } gm_tt49maei14cl6;
_Static_assert(sizeof(gm_ttf6i6v1da8cm) == 16 && _Alignof(gm_ttf6i6v1da8cm) == 8 && offsetof(gm_ttf6i6v1da8cm, f0) == 0 && offsetof(gm_ttf6i6v1da8cm, f1) == 8, "layout of `(slot: ^Slot, base: u64)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_ttovhfee8mf28 f1; gm_ttovhfee8mf28 f2; uint8_t p3[4]; void * f3; void * f4; } gm_t0pc2c7dafsjt;
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
_Static_assert(sizeof(gm_t4nomjfe3hlif) == 64 && _Alignof(gm_t4nomjfe3hlif) == 16 && offsetof(gm_t4nomjfe3hlif, f0) == 0 && offsetof(gm_t4nomjfe3hlif, f1) == 4 && offsetof(gm_t4nomjfe3hlif, f2) == 8 && offsetof(gm_t4nomjfe3hlif, f3) == 16 && offsetof(gm_t4nomjfe3hlif, f4) == 32 && offsetof(gm_t4nomjfe3hlif, f5) == 40 && offsetof(gm_t4nomjfe3hlif, f6) == 48 && offsetof(gm_t4nomjfe3hlif, f7) == 56, "layout of `(variant: u32, tag_ty: Ty, tag_size: u64, tag: u128, tag_w: u64, off: u64, size: u64, payload: ?^Pat)`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
_Static_assert(sizeof(gm_t9s8r45p2ijj5) == 24 && _Alignof(gm_t9s8r45p2ijj5) == 8 && offsetof(gm_t9s8r45p2ijj5, f0) == 0 && offsetof(gm_t9s8r45p2ijj5, f1) == 4 && offsetof(gm_t9s8r45p2ijj5, f2) == 16, "layout of `(op: UnOp, t: IntW, x: ^Node)`");
_Static_assert(sizeof(gm_ta6jn77i9rmqf) == 32 && _Alignof(gm_ta6jn77i9rmqf) == 8 && offsetof(gm_ta6jn77i9rmqf, f0) == 0 && offsetof(gm_ta6jn77i9rmqf, f1) == 8 && offsetof(gm_ta6jn77i9rmqf, f2) == 16 && offsetof(gm_ta6jn77i9rmqf, f3) == 24, "layout of `(how: WordCmp, l: ^Node, r: ^Node, negated: bool)`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tdvcgvrf81sbd) == 24 && _Alignof(gm_tdvcgvrf81sbd) == 8 && offsetof(gm_tdvcgvrf81sbd, f0) == 0 && offsetof(gm_tdvcgvrf81sbd, f1) == 8 && offsetof(gm_tdvcgvrf81sbd, f2) == 12 && offsetof(gm_tdvcgvrf81sbd, f3) == 16, "layout of `(base: ^Node, index: u32, bit_off: u32, fty: Ty)`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; } gm_tf4r0415i49d1;
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_th2lqbo77pvsc f0; gm_b16_8 f1; gm_b16_8 f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tj7j8jssama8n;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[2]; gm_tmn7n0n9raou5 f1; uint8_t p2[4]; void * f2; void * f3; } gm_tkm39klvnn4cr;
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
_Static_assert(sizeof(gm_tp0fa5qv7rqnn) == 40 && _Alignof(gm_tp0fa5qv7rqnn) == 8 && offsetof(gm_tp0fa5qv7rqnn, f0) == 0 && offsetof(gm_tp0fa5qv7rqnn, f1) == 8 && offsetof(gm_tp0fa5qv7rqnn, f2) == 16 && offsetof(gm_tp0fa5qv7rqnn, f3) == 24 && offsetof(gm_tp0fa5qv7rqnn, f4) == 32, "layout of `(base: ^Node, index: ^Node, seq: SeqKind, esz: u64, len: u64)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_tvdg94bk7aa22 f0; gm_ttovhfee8mf28 f1; gm_ttovhfee8mf28 f2; uint8_t p3[4]; void * f3; } gm_tr8okf1iilonm;
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
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tf4r0415i49d1) == 48 && _Alignof(gm_tf4r0415i49d1) == 8 && offsetof(gm_tf4r0415i49d1, f0) == 0 && offsetof(gm_tf4r0415i49d1, f1) == 24 && offsetof(gm_tf4r0415i49d1, f2) == 32, "layout of `(effect_: EffectRef, op: u32, args: []^Node)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t419mo45g7j0a v0; void * v1; } f1; } gm_th1g69d7vpipg;
_Static_assert(sizeof(gm_tj7j8jssama8n) == 72 && _Alignof(gm_tj7j8jssama8n) == 8 && offsetof(gm_tj7j8jssama8n, f0) == 0 && offsetof(gm_tj7j8jssama8n, f1) == 24 && offsetof(gm_tj7j8jssama8n, f2) == 40 && offsetof(gm_tj7j8jssama8n, f3) == 56, "layout of `StructLit`");
_Static_assert(sizeof(gm_tkm39klvnn4cr) == 32 && _Alignof(gm_tkm39klvnn4cr) == 8 && offsetof(gm_tkm39klvnn4cr, f0) == 0 && offsetof(gm_tkm39klvnn4cr, f1) == 4 && offsetof(gm_tkm39klvnn4cr, f2) == 16 && offsetof(gm_tkm39klvnn4cr, f3) == 24, "layout of `(op: BinOp, how: WordCmp, l: ^Node, r: ^Node)`");
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_tr8okf1iilonm) == 40 && _Alignof(gm_tr8okf1iilonm) == 8 && offsetof(gm_tr8okf1iilonm, f0) == 0 && offsetof(gm_tr8okf1iilonm, f1) == 12 && offsetof(gm_tr8okf1iilonm, f2) == 20 && offsetof(gm_tr8okf1iilonm, f3) == 32, "layout of `(op: Builtin, from: IntW, to: IntW, x: ^Node)`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts6ln23edn25a) == 32 && _Alignof(gm_ts6ln23edn25a) == 8 && offsetof(gm_ts6ln23edn25a, f0) == 0 && offsetof(gm_ts6ln23edn25a, f1) == 12 && offsetof(gm_ts6ln23edn25a, f2) == 24, "layout of `(op: Builtin, t: IntW, x: ^Node)`");
_Static_assert(sizeof(gm_tscalq56heofe) == 40 && _Alignof(gm_tscalq56heofe) == 8 && offsetof(gm_tscalq56heofe, f0) == 0 && offsetof(gm_tscalq56heofe, f1) == 12 && offsetof(gm_tscalq56heofe, f2) == 24 && offsetof(gm_tscalq56heofe, f3) == 32, "layout of `(op: Builtin, t: IntW, x: ^Node, k: ^Node)`");
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_tuggr5sml9o52) == 24 && _Alignof(gm_tuggr5sml9o52) == 8 && offsetof(gm_tuggr5sml9o52, f0) == 0 && offsetof(gm_tuggr5sml9o52, f1) == 2 && offsetof(gm_tuggr5sml9o52, f2) == 8 && offsetof(gm_tuggr5sml9o52, f3) == 16, "layout of `(op: BinOp, wide: bool, l: ^Node, r: ^Node)`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_th1g69d7vpipg) == 40 && _Alignof(gm_th1g69d7vpipg) == 8 && offsetof(gm_th1g69d7vpipg, f0) == 0 && offsetof(gm_th1g69d7vpipg, f1) == 8, "layout of `WriteSink`");
typedef struct { gm_t6evimt84lndq f0; uint8_t f1; uint8_t pe[15]; } gm_tjoi8o798ne9k;
typedef struct { uint8_t f0; uint8_t p1[15]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_t4nomjfe3hlif v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tlpa5tka3taqb;
typedef struct { gm_th1g69d7vpipg f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tntn2tf15puib;
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint64_t v0; gm_b16_8 v1; gm_b16_8 v2; gm_b16_8 v3; gm_tl874t2atnlh3 v4; gm_tb31k5l794t7p v5; gm_tcc1rq4n1avb2 v6; uint64_t v7; gm_ttf6i6v1da8cm v8; gm_t1j70din2upc4 v9; gm_tdvcgvrf81sbd v10; gm_tp0fa5qv7rqnn v11; gm_tt49maei14cl6 v12; void * v13; void * v14; void * v15; gm_ttf6i6v1da8cm v16; gm_t9805qtonbsag v17; uint32_t v18; uint32_t v19; gm_t0pc2c7dafsjt v20; gm_tuggr5sml9o52 v21; gm_tkm39klvnn4cr v22; gm_ta6jn77i9rmqf v23; void * v24; gm_t9s8r45p2ijj5 v25; gm_tr8okf1iilonm v26; gm_tscalq56heofe v27; gm_ts6ln23edn25a v28; gm_t8ghgl2jj0qha v29; gm_trl9d7m1k5lbd v30; gm_ta612fil812pm v31; gm_t8jsc4n9vp2ib v32; gm_tt8o5kebou62r v33; gm_tiilmfd16v3fc v34; gm_tf4r0415i49d1 v35; gm_tkaapcii86udk v36; gm_t6j6smj7qoctu v37; void * v38; gm_b16_8 v39; gm_b16_8 v40; void * v41; gm_ttggq7ng4kp3p v42; void * v43; gm_tsplfr33dsvji v44; gm_t3dcoc7077t1q v45; gm_t083kd7b1kkag v46; gm_tlcl4u558l6p5 v47; gm_tlcl4u558l6p5 v48; gm_tkaapcii86udk v49; void * v50; gm_trl2khmuphncd v51; gm_t710lsrq1m8gk v52; gm_teo9d4mncjfdu v53; gm_te9hrk0u35rev v54; gm_ttggq7ng4kp3p v55; void * v56; gm_t3mf58gv9kf3n v57; void * v58; gm_tntn2tf15puib v59; gm_t710lsrq1m8gk v60; } f1; } gm_tbo4aa19b457h;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tb38v5s9j1r32 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc9qv69frb08e;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[12]; gm_tlpa5tka3taqb f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tdgedp3mu9p1a;
_Static_assert(sizeof(gm_tjoi8o798ne9k) == 64 && _Alignof(gm_tjoi8o798ne9k) == 16 && offsetof(gm_tjoi8o798ne9k, f0) == 0 && offsetof(gm_tjoi8o798ne9k, f1) == 48, "layout of `?Value`");
_Static_assert(sizeof(gm_tlpa5tka3taqb) == 128 && _Alignof(gm_tlpa5tka3taqb) == 16 && offsetof(gm_tlpa5tka3taqb, f0) == 0 && offsetof(gm_tlpa5tka3taqb, f1) == 16, "layout of `PatKind`");
_Static_assert(sizeof(gm_tntn2tf15puib) == 88 && _Alignof(gm_tntn2tf15puib) == 8 && offsetof(gm_tntn2tf15puib, f0) == 0 && offsetof(gm_tntn2tf15puib, f1) == 40 && offsetof(gm_tntn2tf15puib, f2) == 56 && offsetof(gm_tntn2tf15puib, f3) == 72, "layout of `WriteExpr`");
_Static_assert(sizeof(gm_tbo4aa19b457h) == 96 && _Alignof(gm_tbo4aa19b457h) == 8 && offsetof(gm_tbo4aa19b457h, f0) == 0 && offsetof(gm_tbo4aa19b457h, f1) == 8, "layout of `Op`");
_Static_assert(sizeof(gm_tc9qv69frb08e) == 136 && _Alignof(gm_tc9qv69frb08e) == 8 && offsetof(gm_tc9qv69frb08e, f0) == 0 && offsetof(gm_tc9qv69frb08e, f1) == 8 && offsetof(gm_tc9qv69frb08e, f2) == 120, "layout of `Pat`");
_Static_assert(sizeof(gm_tdgedp3mu9p1a) == 160 && _Alignof(gm_tdgedp3mu9p1a) == 16 && offsetof(gm_tdgedp3mu9p1a, f0) == 0 && offsetof(gm_tdgedp3mu9p1a, f1) == 16 && offsetof(gm_tdgedp3mu9p1a, f2) == 144, "layout of `Pat`");
typedef struct { gm_tbo4aa19b457h f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; uint64_t f2; gm_tl874t2atnlh3 f3; uint8_t f4; uint8_t pe[3]; } gm_tv8a4cs3pcpgp;
_Static_assert(sizeof(gm_tv8a4cs3pcpgp) == 128 && _Alignof(gm_tv8a4cs3pcpgp) == 8 && offsetof(gm_tv8a4cs3pcpgp, f0) == 0 && offsetof(gm_tv8a4cs3pcpgp, f1) == 96 && offsetof(gm_tv8a4cs3pcpgp, f2) == 104 && offsetof(gm_tv8a4cs3pcpgp, f3) == 112 && offsetof(gm_tv8a4cs3pcpgp, f4) == 124, "layout of `Node`");
struct gm_fr_empty_slice_13d6rhn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_14fhq9i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_16ab7e4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1di25gg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1du9kj1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1fu8oa9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1ivdmti {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1l2vi9a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1mrcnd6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1scem7m {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_1th396a {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_h06rie2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hb2jp66 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hc7thq6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_he774nl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hgrt2o4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hh33a3d {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hii1vte {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hm6i8si {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hmpovae {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hnufvkq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hp0e9gl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hr6df4i {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hs811oq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_htfpvg4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_hu049sk {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_new_15vqrvd {
    gm_frame h;
    gm_tjoi8o798ne9k l0; /* init */
    gm_tb40qn7hk0tl9 l1; /* site */
    gm_tq7s98ihonkju ret;
};
struct gm_fr_new_1b2khgi {
    gm_frame h;
    gm_tc9qv69frb08e l0; /* init */
    gm_tb40qn7hk0tl9 l1; /* site */
    gm_tq7s98ihonkju ret;
};
struct gm_fr_new_1b2khgi_2 {
    gm_frame h;
    gm_tdgedp3mu9p1a l0; /* init */
    gm_tb40qn7hk0tl9 l1; /* site */
    gm_tq7s98ihonkju ret;
};
struct gm_fr_new_her8si0 {
    gm_frame h;
    gm_tv8a4cs3pcpgp l0; /* init */
    gm_tb40qn7hk0tl9 l1; /* site */
    gm_tq7s98ihonkju ret;
};
struct gm_fr_new_hq21sop {
    gm_frame h;
    gm_tj7j8jssama8n l0; /* init */
    gm_tb40qn7hk0tl9 l1; /* site */
    gm_tq7s98ihonkju ret;
};
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_13d6rhn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_14fhq9i(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_16ab7e4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1di25gg(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1du9kj1(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1ivdmti(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1l2vi9a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1mrcnd6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1scem7m(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_h06rie2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hb2jp66(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hc7thq6(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hgrt2o4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hh33a3d(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hii1vte(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hm6i8si(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hmpovae(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hnufvkq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hp0e9gl(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hr6df4i(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hs811oq(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_htfpvg4(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hu049sk(void);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_15vqrvd(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_her8si0(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_hq21sop(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_tag_78nb3u9(void * a0, gm_cs6mnftt383dnt a1, const void * a2);
extern const char gm_src2ah81i3aa8hm[];
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_13d6rhn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_13d6rhn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_14fhq9i(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_14fhq9i F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_16ab7e4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_16ab7e4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1di25gg(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1di25gg F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1du9kj1(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1du9kj1 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1fu8oa9 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 64)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1ivdmti(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1ivdmti F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1l2vi9a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1l2vi9a F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 96)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1mrcnd6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1mrcnd6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1scem7m(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1scem7m F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_1th396a F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 184)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_h06rie2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_h06rie2 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hb2jp66(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hb2jp66 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hc7thq6(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hc7thq6 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_he774nl F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hgrt2o4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hgrt2o4 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hh33a3d(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hh33a3d F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hii1vte(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hii1vte F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 64)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hm6i8si(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hm6i8si F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 56)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hnufvkq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hnufvkq F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 20)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hp0e9gl(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hp0e9gl F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hr6df4i(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hr6df4i F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hs811oq(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hs811oq F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_htfpvg4(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_htfpvg4 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hu049sk(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_hu049sk F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 72)));
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_13d6rhn(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_14fhq9i(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_16ab7e4(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1di25gg(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1du9kj1(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1fu8oa9(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1ivdmti(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1l2vi9a(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1mrcnd6(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1scem7m(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_1th396a(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_h06rie2(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hb2jp66(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hc7thq6(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_he774nl(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hgrt2o4(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hh33a3d(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hii1vte(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hm6i8si(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hmpovae(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hnufvkq(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hp0e9gl(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hr6df4i(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hs811oq(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_htfpvg4(void);
extern __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_hu049sk(void);
inline __attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_15vqrvd(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_new_15vqrvd F;
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* raw */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 331u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_node *pn2 = gm_find(cx, 587034433);
        if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_src2ah81i3aa8hm, 635, 10);
        gm_t3mf58gv9kf3n t3;
        t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        void * t4 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        void * t7 = t5;
        void * t8;
        t8 = ((void * (*)(void *, uint64_t, uint64_t))GM_PTR_UNTAG(t7))(t6, ((uint64_t)64ULL), ((uint64_t)16ULL));
        void * t9 = t8;
        void * t10;
        if ((GM_LD(void *, ((uint8_t *)&t9)) != 0)) {
            void * t11 = GM_LD(void *, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tq7s98ihonkju t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 8), t12);
            gm_tq7s98ihonkju t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        gm_l3 = t10;
    }
    {
        void * t16 = gm_l3;
        gm_t3mf58gv9kf3n t17 = gm_l2;
        gm_f_tag_78nb3u9(t16, GM_CONV(gm_cs6mnftt383dnt, t17), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)a1)));
    }
    {
        void * t18 = gm_l3;
        if ((uintptr_t)t18 % 16 != 0) gm_trap_misaligned((uintptr_t)t18, 16, gm_src2ah81i3aa8hm, 638, 10);
        void * t19 = t18;
        gm_l4 = t19;
    }
    {
        void * t20 = gm_l4;
        gm_tjoi8o798ne9k t21 = GM_LD(gm_tjoi8o798ne9k, ((uint8_t *)a0));
        GM_ST(gm_tjoi8o798ne9k, ((uint8_t *)t20), t21);
    }
    {
        void * t22 = gm_l4;
        gm_tq7s98ihonkju t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        ((uint8_t *)&t23)[0] = 0;
        GM_ST(void *, ((uint8_t *)&t23 + 8), t22);
        gm_tq7s98ihonkju t24 = t23;
        uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
        F.ret = t23;
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
inline __attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_new_1b2khgi F;
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* raw */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 403u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_node *pn2 = gm_find(cx, 587034433);
        if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_src2ah81i3aa8hm, 635, 10);
        gm_t3mf58gv9kf3n t3;
        t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        void * t4 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        void * t7 = t5;
        void * t8;
        t8 = ((void * (*)(void *, uint64_t, uint64_t))GM_PTR_UNTAG(t7))(t6, ((uint64_t)136ULL), ((uint64_t)8ULL));
        void * t9 = t8;
        void * t10;
        if ((GM_LD(void *, ((uint8_t *)&t9)) != 0)) {
            void * t11 = GM_LD(void *, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tq7s98ihonkju t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 8), t12);
            gm_tq7s98ihonkju t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        gm_l3 = t10;
    }
    {
        void * t16 = gm_l3;
        gm_t3mf58gv9kf3n t17 = gm_l2;
        gm_f_tag_78nb3u9(t16, GM_CONV(gm_cs6mnftt383dnt, t17), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)a1)));
    }
    {
        void * t18 = gm_l3;
        if ((uintptr_t)t18 % 8 != 0) gm_trap_misaligned((uintptr_t)t18, 8, gm_src2ah81i3aa8hm, 638, 10);
        void * t19 = t18;
        gm_l4 = t19;
    }
    {
        void * t20 = gm_l4;
        gm_tc9qv69frb08e t21 = GM_LD(gm_tc9qv69frb08e, ((uint8_t *)a0));
        GM_ST(gm_tc9qv69frb08e, ((uint8_t *)t20), t21);
    }
    {
        void * t22 = gm_l4;
        gm_tq7s98ihonkju t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        ((uint8_t *)&t23)[0] = 0;
        GM_ST(void *, ((uint8_t *)&t23 + 8), t22);
        gm_tq7s98ihonkju t24 = t23;
        uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
        F.ret = t23;
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
inline __attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_new_1b2khgi_2 F;
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* raw */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 427u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_node *pn2 = gm_find(cx, 587034433);
        if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_src2ah81i3aa8hm, 635, 10);
        gm_t3mf58gv9kf3n t3;
        t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        void * t4 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        void * t7 = t5;
        void * t8;
        t8 = ((void * (*)(void *, uint64_t, uint64_t))GM_PTR_UNTAG(t7))(t6, ((uint64_t)160ULL), ((uint64_t)16ULL));
        void * t9 = t8;
        void * t10;
        if ((GM_LD(void *, ((uint8_t *)&t9)) != 0)) {
            void * t11 = GM_LD(void *, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tq7s98ihonkju t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 8), t12);
            gm_tq7s98ihonkju t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        gm_l3 = t10;
    }
    {
        void * t16 = gm_l3;
        gm_t3mf58gv9kf3n t17 = gm_l2;
        gm_f_tag_78nb3u9(t16, GM_CONV(gm_cs6mnftt383dnt, t17), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)a1)));
    }
    {
        void * t18 = gm_l3;
        if ((uintptr_t)t18 % 16 != 0) gm_trap_misaligned((uintptr_t)t18, 16, gm_src2ah81i3aa8hm, 638, 10);
        void * t19 = t18;
        gm_l4 = t19;
    }
    {
        void * t20 = gm_l4;
        gm_tdgedp3mu9p1a t21 = GM_LD(gm_tdgedp3mu9p1a, ((uint8_t *)a0));
        GM_ST(gm_tdgedp3mu9p1a, ((uint8_t *)t20), t21);
    }
    {
        void * t22 = gm_l4;
        gm_tq7s98ihonkju t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        ((uint8_t *)&t23)[0] = 0;
        GM_ST(void *, ((uint8_t *)&t23 + 8), t22);
        gm_tq7s98ihonkju t24 = t23;
        uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
        F.ret = t23;
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
inline __attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_her8si0(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_new_her8si0 F;
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* raw */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 395u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_node *pn2 = gm_find(cx, 587034433);
        if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_src2ah81i3aa8hm, 635, 10);
        gm_t3mf58gv9kf3n t3;
        t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        void * t4 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        void * t7 = t5;
        void * t8;
        t8 = ((void * (*)(void *, uint64_t, uint64_t))GM_PTR_UNTAG(t7))(t6, ((uint64_t)128ULL), ((uint64_t)8ULL));
        void * t9 = t8;
        void * t10;
        if ((GM_LD(void *, ((uint8_t *)&t9)) != 0)) {
            void * t11 = GM_LD(void *, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tq7s98ihonkju t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 8), t12);
            gm_tq7s98ihonkju t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        gm_l3 = t10;
    }
    {
        void * t16 = gm_l3;
        gm_t3mf58gv9kf3n t17 = gm_l2;
        gm_f_tag_78nb3u9(t16, GM_CONV(gm_cs6mnftt383dnt, t17), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)a1)));
    }
    {
        void * t18 = gm_l3;
        if ((uintptr_t)t18 % 8 != 0) gm_trap_misaligned((uintptr_t)t18, 8, gm_src2ah81i3aa8hm, 638, 10);
        void * t19 = t18;
        gm_l4 = t19;
    }
    {
        void * t20 = gm_l4;
        gm_tv8a4cs3pcpgp t21 = GM_LD(gm_tv8a4cs3pcpgp, ((uint8_t *)a0));
        GM_ST(gm_tv8a4cs3pcpgp, ((uint8_t *)t20), t21);
    }
    {
        void * t22 = gm_l4;
        gm_tq7s98ihonkju t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        ((uint8_t *)&t23)[0] = 0;
        GM_ST(void *, ((uint8_t *)&t23 + 8), t22);
        gm_tq7s98ihonkju t24 = t23;
        uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
        F.ret = t23;
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
inline __attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_hq21sop(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_new_hq21sop F;
    gm_t3mf58gv9kf3n gm_l2; /* a */
    void * gm_l3; /* raw */
    void * gm_l4; /* p */
    GM_STACK_CHECK(&F, 339u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_node *pn2 = gm_find(cx, 587034433);
        if (!pn2) gm_trap("no handler installed for effect `Alloc`", gm_src2ah81i3aa8hm, 635, 10);
        gm_t3mf58gv9kf3n t3;
        t3 = ((gm_t3mf58gv9kf3n (*)(void *, gm_node *, gm_node *))pn2->clauses[0])(pn2->env, pn2->hctx, pn2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l2 = t3;
    }
    {
        void * t4 = GM_LD(void *, (((uint8_t *)&gm_l2) + 8));
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = GM_LD(void *, (((uint8_t *)&gm_l2) + 0));
        void * t7 = t5;
        void * t8;
        t8 = ((void * (*)(void *, uint64_t, uint64_t))GM_PTR_UNTAG(t7))(t6, ((uint64_t)72ULL), ((uint64_t)8ULL));
        void * t9 = t8;
        void * t10;
        if ((GM_LD(void *, ((uint8_t *)&t9)) != 0)) {
            void * t11 = GM_LD(void *, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_t9qkh0rsooc8d t12;
            GM_ST(uint8_t, ((uint8_t *)&t12 + 0), ((uint8_t)0ULL));
            gm_tq7s98ihonkju t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t13 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 12, 0);
            ((uint8_t *)&t13)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t13 + 8), t12);
            gm_tq7s98ihonkju t14 = t13;
            uint8_t t15 = ((((uint8_t *)&t14))[0] != 0);
            F.ret = t13;
            goto gm_fret;
        }
        gm_l3 = t10;
    }
    {
        void * t16 = gm_l3;
        gm_t3mf58gv9kf3n t17 = gm_l2;
        gm_f_tag_78nb3u9(t16, GM_CONV(gm_cs6mnftt383dnt, t17), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)a1)));
    }
    {
        void * t18 = gm_l3;
        if ((uintptr_t)t18 % 8 != 0) gm_trap_misaligned((uintptr_t)t18, 8, gm_src2ah81i3aa8hm, 638, 10);
        void * t19 = t18;
        gm_l4 = t19;
    }
    {
        void * t20 = gm_l4;
        gm_tj7j8jssama8n t21 = GM_LD(gm_tj7j8jssama8n, ((uint8_t *)a0));
        GM_ST(gm_tj7j8jssama8n, ((uint8_t *)t20), t21);
    }
    {
        void * t22 = gm_l4;
        gm_tq7s98ihonkju t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        ((uint8_t *)&t23)[0] = 0;
        GM_ST(void *, ((uint8_t *)&t23 + 8), t22);
        gm_tq7s98ihonkju t24 = t23;
        uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
        F.ret = t23;
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

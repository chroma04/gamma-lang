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
    GM_STACK_CHECK(&F, 88u);
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
    uint64_t t5 = GM_LD(uint64_t, (((uint8_t *)&t4) + 8));
    F.ret = t5;
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
    GM_STACK_CHECK(&F, 97u);
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
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
            uint64_t t9 = gm_f_get_u64_tgpb2vh(t6, t8);
            memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)0ULL));
            GM_ST(uint64_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t9);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t10 = gm_l1;
        memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)1ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t10);
        goto gm_fdone;
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
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        if (!(GM_LD(void *, ((uint8_t *)&t2)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t3 = GM_LD(void *, ((uint8_t *)&t2));
        gm_l0 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = ((uint8_t *)t4);
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        uint8_t t9 = 0;
        F.ret = t8;
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
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        if (!(GM_LD(void *, ((uint8_t *)&t2)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t3 = GM_LD(void *, ((uint8_t *)&t2));
        gm_l0 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = ((uint8_t *)t4);
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 24)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        uint8_t t9 = 0;
        F.ret = t8;
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
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        if (!(GM_LD(void *, ((uint8_t *)&t2)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t3 = GM_LD(void *, ((uint8_t *)&t2));
        gm_l0 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = ((uint8_t *)t4);
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        uint8_t t9 = 0;
        F.ret = t8;
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
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        if (!(GM_LD(void *, ((uint8_t *)&t2)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t3 = GM_LD(void *, ((uint8_t *)&t2));
        gm_l0 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = ((uint8_t *)t4);
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        uint8_t t9 = 0;
        F.ret = t8;
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
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        if (!(GM_LD(void *, ((uint8_t *)&t2)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t3 = GM_LD(void *, ((uint8_t *)&t2));
        gm_l0 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = ((uint8_t *)t4);
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        uint8_t t9 = 0;
        F.ret = t8;
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
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        if (!(GM_LD(void *, ((uint8_t *)&t2)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t3 = GM_LD(void *, ((uint8_t *)&t2));
        gm_l0 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = ((uint8_t *)t4);
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        uint8_t t9 = 0;
        F.ret = t8;
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
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        if (!(GM_LD(void *, ((uint8_t *)&t2)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t3 = GM_LD(void *, ((uint8_t *)&t2));
        gm_l0 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = ((uint8_t *)t4);
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        uint8_t t9 = 0;
        F.ret = t8;
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
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)8ULL));
        if (!(GM_LD(void *, ((uint8_t *)&t2)) != 0)) gm_trap("unwrap of null", gm_src2ah81i3aa8hm, 618, 10);
        void * t3 = GM_LD(void *, ((uint8_t *)&t2));
        gm_l0 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = ((uint8_t *)t4);
        uint64_t t6 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t7 = (uintptr_t)((uint64_t)0ULL);
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 8)));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(t7 - t6));
        uint8_t t9 = 0;
        F.ret = t8;
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
    GM_STACK_CHECK_SHALLOW(&F, 119u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        void * t4 = gm_f_adt_of_8k5vqud(t2, GM_CONV(gm_csguankko4lmhd, t3));
        void * t5;
        if ((GM_LD(void *, ((uint8_t *)&t4)) != 0)) {
            void * t6 = GM_LD(void *, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            uint8_t t7 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        void * t9 = gm_l2;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t9) + 24) + 0));
        if (t10 != ((uint8_t)0ULL)) goto ie8;
        gm_tpautr4br7h3l t11 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t9) + 24) + 8));
        F.l3 = t11;
        {
            uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 16));
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix8;
ie8:;
        {
            uint8_t t16 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix8:;
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
    GM_STACK_CHECK_SHALLOW(&F, 48u);
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
    memmove(gm_out, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 56), 56);
    goto gm_fdone;
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
    GM_STACK_CHECK(&F, 112u);
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
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 16));
    F.ret = t5;
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
    GM_STACK_CHECK(&F, 90u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        void * t3;
        if ((GM_LD(void *, ((uint8_t *)&t2)) != 0)) {
            void * t4 = GM_LD(void *, (((uint8_t *)&t2) + 0));
            t3 = t4;
        } else {
            uint8_t t5 = 0;
            F.ret = ((void *)0);
            goto gm_fret;
        }
        gm_l2 = t3;
    }
    {
        void * t6 = gm_l0;
        void * t7 = gm_l2;
        void * t8 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t9 = 0;
        F.ret = t8;
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
    GM_STACK_CHECK(&F, 180u);
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
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&t5) + 0));
        if (t6 != ((uint8_t)13ULL)) goto ie2;
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t5) + 8));
        gm_l2 = t7;
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 8));
        gm_t88rcucg1b7uk t10 = gm_l2;
        uint8_t t11 = gm_f_nullable_pointer_7qfmn6o(t9, GM_CONV(gm_csguankko4lmhd, t10));
        if (!t11) goto ie2;
        {
            gm_tmn7n0n9raou5 t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tbgiuvo2j5vcs t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[8] = 1;
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t16 = gm_l0;
        gm_t88rcucg1b7uk t17 = gm_l1;
        gm_tbgiuvo2j5vcs t18 = gm_f_word_cmp_s2rbdbn((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t19 = 0;
        F.ret = t18;
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
    GM_STACK_CHECK(&F, 15070u);
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
            if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 8, gm_src7afujsggtlje, 276, 18);
            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 8));
            gm_l5 = t72;
        }
        {
            void * t74 = gm_l2;
            void * t75 = gm_l1;
            gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t75) + 0));
            gm_t53at80pfdus3 t77;
            gm_f_kind_htv6093((void *)&t77, t74, GM_CONV(gm_csguankko4lmhd, t76));
            uint8_t t78;
            uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&t77) + 0));
            if (t81 != ((uint8_t)3ULL)) goto mn80;
            t78 = ((uint8_t)1);
            goto me79;
mn80:;
            t78 = ((uint8_t)0);
            goto me79;
mn82:;
            gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 277, 16);
me79:;
            if (!t78) goto ie73;
            {
                void * t84 = gm_l2;
                void * t85 = gm_l1;
                gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t85) + 0));
                void * t87 = gm_l2;
                double t88 = gm_f_big_float_lh5u3gu((void *)NULL, cx, NULL, t87, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t89 = gm_l5;
                uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&t89) + 8);
                void * t91 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t89)));
                uint64_t t92 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t93 = gm_l4;
                uint64_t t94 = gm_f_us_cuv01fp(t93);
                uint64_t t95 = (uintptr_t)t94;
                if (t92 > t95 || t95 > (uintptr_t)t90) gm_trap_slice(t92, t95, t90, gm_src7afujsggtlje, 278, 51);
                gm_b16_8 t96;
                GM_ST(void *, (uint8_t *)&t96, (void *)(((uint8_t *)t91 + t92 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(t95 - t92));
                gm_f_put_float_fiad72d(t84, GM_CONV(gm_csguankko4lmhd, t86), t88, t96);
            }
            goto ix73;
ie73:;
            {
                void * t98 = gm_l2;
                gm_b16_8 t99 = gm_l5;
                uint64_t t100 = GM_LD(uintptr_t, ((uint8_t *)&t99) + 8);
                void * t101 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t99)));
                uint64_t t102 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t103 = gm_l4;
                uint64_t t104 = gm_f_us_cuv01fp(t103);
                uint64_t t105 = (uintptr_t)t104;
                if (t102 > t105 || t105 > (uintptr_t)t100) gm_trap_slice(t102, t105, t100, gm_src7afujsggtlje, 280, 28);
                gm_b16_8 t106;
                GM_ST(void *, (uint8_t *)&t106, (void *)(((uint8_t *)t101 + t102 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(t105 - t102));
                unsigned __int128 t107 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
                gm_f_put_uint_cddin99(t98, t106, t107);
            }
ix73:;
        }
        void * t108 = gm_l2;
        gm_b16_8 t109 = gm_l5;
        uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&t109) + 8);
        void * t111 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t109)));
        uint64_t t112 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t113 = gm_l4;
        uint64_t t114 = gm_f_us_cuv01fp(t113);
        uint64_t t115 = (uintptr_t)t114;
        if (t112 > t115 || t115 > (uintptr_t)t110) gm_trap_slice(t112, t115, t110, gm_src7afujsggtlje, 282, 25);
        gm_b16_8 t116;
        GM_ST(void *, (uint8_t *)&t116, (void *)(((uint8_t *)t111 + t112 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(t115 - t112));
        gm_tbo4aa19b457h t117;
        gm_f_const_op_r0qgpnv((void *)&t117, t108, t116);
        gm_tbo4aa19b457h t118 = t117;
        t5 = t118;
        goto me6;
mn60:;
ma8:;
        uint8_t t120 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t120 != ((uint8_t)1ULL)) goto mn119;
        double t121 = GM_LD(double, ((((uint8_t *)t4) + 8) + 8));
        gm_l6 = t121;
        {
            void * t123 = gm_l2;
            void * t124 = gm_l1;
            gm_t88rcucg1b7uk t125 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t124) + 0));
            uint64_t t126 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t123, t125);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t126;
        }
        {
            uint64_t t127 = gm_l7;
            uint64_t t128 = (t127 > ((uint64_t)1ULL) ? t127 : ((uint64_t)1ULL));
            uint64_t t129 = gm_f_us_cuv01fp(t128);
            gm_tabb710d9mq2e t130;
            gm_f_make_fill_jpvm0kc((void *)&t130, (void *)NULL, cx, NULL, t129, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klsha2ujn8u)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t130))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t130) + 8, gm_src7afujsggtlje, 286, 18);
            gm_b16_8 t131 = GM_LD(gm_b16_8, (((uint8_t *)&t130) + 8));
            gm_l8 = t131;
        }
        {
            void * t132 = gm_l2;
            void * t133 = gm_l1;
            gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t133) + 0));
            double t135 = gm_l6;
            gm_b16_8 t136 = gm_l8;
            uint64_t t137 = GM_LD(uintptr_t, ((uint8_t *)&t136) + 8);
            void * t138 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t136)));
            uint64_t t139 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t140 = gm_l7;
            uint64_t t141 = gm_f_us_cuv01fp(t140);
            uint64_t t142 = (uintptr_t)t141;
            if (t139 > t142 || t142 > (uintptr_t)t137) gm_trap_slice(t139, t142, t137, gm_src7afujsggtlje, 287, 34);
            gm_b16_8 t143;
            GM_ST(void *, (uint8_t *)&t143, (void *)(((uint8_t *)t138 + t139 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(t142 - t139));
            gm_f_put_float_fiad72d(t132, GM_CONV(gm_csguankko4lmhd, t134), t135, t143);
        }
        void * t144 = gm_l2;
        gm_b16_8 t145 = gm_l8;
        uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&t145) + 8);
        void * t147 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t145)));
        uint64_t t148 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t149 = gm_l7;
        uint64_t t150 = gm_f_us_cuv01fp(t149);
        uint64_t t151 = (uintptr_t)t150;
        if (t148 > t151 || t151 > (uintptr_t)t146) gm_trap_slice(t148, t151, t146, gm_src7afujsggtlje, 288, 25);
        gm_b16_8 t152;
        GM_ST(void *, (uint8_t *)&t152, (void *)(((uint8_t *)t147 + t148 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(t151 - t148));
        gm_tbo4aa19b457h t153;
        gm_f_const_op_r0qgpnv((void *)&t153, t144, t152);
        gm_tbo4aa19b457h t154 = t153;
        t5 = t154;
        goto me6;
mn119:;
ma9:;
        uint8_t t156 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t156 != ((uint8_t)4ULL)) goto mn155;
        uint8_t t157 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 8));
        gm_l9 = t157;
        uint64_t t158;
        uint8_t t160 = gm_l9;
        if (!t160) goto ie159;
        uint64_t t162 = ((uint64_t)1ULL);
        t158 = t162;
        goto ix159;
ie159:;
        uint64_t t164 = ((uint64_t)0ULL);
        t158 = t164;
ix159:;
        gm_tbo4aa19b457h t165;
        memset(&t165, 0, sizeof t165);
        GM_ST(uint8_t, ((uint8_t *)&t165 + 0), ((uint8_t)0ULL));
        GM_ST(uint64_t, ((uint8_t *)&t165 + 8), t158);
        t5 = t165;
        goto me6;
mn155:;
ma10:;
        uint8_t t167 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t167 != ((uint8_t)5ULL)) goto mn166;
        gm_b16_8 t168 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l10 = t168;
        gm_b16_8 t169 = gm_l10;
        gm_tbo4aa19b457h t170;
        memset(&t170, 0, sizeof t170);
        GM_ST(uint8_t, ((uint8_t *)&t170 + 0), ((uint8_t)2ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t170 + 8), t169);
        t5 = t170;
        goto me6;
mn166:;
ma11:;
        uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t172 != ((uint8_t)6ULL)) goto mn171;
        gm_b16_8 t173 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l11 = t173;
        gm_b16_8 t174 = gm_l11;
        gm_tbo4aa19b457h t175;
        memset(&t175, 0, sizeof t175);
        GM_ST(uint8_t, ((uint8_t *)&t175 + 0), ((uint8_t)3ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t175 + 8), t174);
        t5 = t175;
        goto me6;
mn171:;
ma12:;
        uint8_t t177 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t177 != ((uint8_t)7ULL)) goto mn176;
        void * t178 = gm_l2;
        void * t179 = gm_l2;
        void * t180 = gm_l1;
        gm_t88rcucg1b7uk t181 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t180) + 0));
        uint64_t t182 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t179, t181);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t183 = gm_f_us_cuv01fp(t182);
        gm_tabb710d9mq2e t184;
        gm_f_make_fill_jpvm0kc((void *)&t184, (void *)NULL, cx, NULL, t183, ((uint8_t)0ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8ao9fftopr)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 8, gm_src7afujsggtlje, 293, 30);
        gm_b16_8 t185 = GM_LD(gm_b16_8, (((uint8_t *)&t184) + 8));
        gm_tbo4aa19b457h t186;
        gm_f_const_op_r0qgpnv((void *)&t186, t178, t185);
        t5 = t186;
        goto me6;
mn176:;
ma13:;
        uint8_t t188 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t188 != ((uint8_t)8ULL)) goto mn187;
        gm_tbo4aa19b457h t189;
        memset(&t189, 0, sizeof t189);
        GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)5ULL));
        t5 = t189;
        goto me6;
mn187:;
ma14:;
        uint8_t t191 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t191 != ((uint8_t)15ULL)) goto mn190;
        uint32_t t192 = GM_LD(uint32_t, ((((uint8_t *)t4) + 8) + 8));
        gm_l12 = t192;
        uint32_t t193 = gm_l12;
        void * t194 = gm_l0;
        void * t195 = GM_LD(void *, (((uint8_t *)t194) + 8));
        gm_b16_8 t196 = GM_LD(gm_b16_8, (((uint8_t *)t195) + 72));
        gm_b16_8 t197 = t196;
        uint32_t t198 = gm_l12;
        uint64_t t199 = (uint64_t)t198;
        uint64_t t200 = gm_f_us_cuv01fp(t199);
        uint64_t t201 = (uintptr_t)t200;
        uint64_t t202 = GM_LD(uintptr_t, ((uint8_t *)&t197) + 8);
        if (t201 >= (uintptr_t)(t202)) gm_trap_index(t201, t202, gm_src7afujsggtlje, 295, 32);
        uint64_t t203 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t197)) + t201 * 8));
        gm_tb31k5l794t7p t204;
        GM_ST(uint32_t, (uint8_t *)&t204 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t204 + 0), t193);
        GM_ST(uint64_t, ((uint8_t *)&t204 + 8), t203);
        gm_tbo4aa19b457h t205;
        memset(&t205, 0, sizeof t205);
        GM_ST(uint8_t, ((uint8_t *)&t205 + 0), ((uint8_t)6ULL));
        GM_ST(gm_tb31k5l794t7p, ((uint8_t *)&t205 + 8), t204);
        t5 = t205;
        goto me6;
mn190:;
ma15:;
        uint8_t t207 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t207 != ((uint8_t)16ULL)) goto mn206;
        uint32_t t208 = GM_LD(uint32_t, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l13 = t208;
        uint32_t t209 = GM_LD(uint32_t, (((((uint8_t *)t4) + 8) + 8) + 4));
        gm_l14 = t209;
        uint32_t t210 = gm_l13;
        uint32_t t211 = gm_l14;
        gm_tcc1rq4n1avb2 t212;
        GM_ST(uint32_t, ((uint8_t *)&t212 + 0), t210);
        GM_ST(uint32_t, ((uint8_t *)&t212 + 4), t211);
        gm_tbo4aa19b457h t213;
        memset(&t213, 0, sizeof t213);
        GM_ST(uint8_t, ((uint8_t *)&t213 + 0), ((uint8_t)7ULL));
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)&t213 + 8), t212);
        t5 = t213;
        goto me6;
mn206:;
ma16:;
        uint8_t t215 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t215 != ((uint8_t)17ULL)) goto mn214;
        uint64_t t216 = GM_LD(uint64_t, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l15 = t216;
        uint64_t t217 = gm_l15;
        gm_tbo4aa19b457h t218;
        memset(&t218, 0, sizeof t218);
        GM_ST(uint8_t, ((uint8_t *)&t218 + 0), ((uint8_t)8ULL));
        GM_ST(uint64_t, ((uint8_t *)&t218 + 8), t217);
        t5 = t218;
        goto me6;
mn214:;
ma17:;
        uint8_t t220 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t220 != ((uint8_t)14ULL)) goto mn219;
        gm_tl874t2atnlh3 t221 = GM_LD(gm_tl874t2atnlh3, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l16 = t221;
        gm_tl874t2atnlh3 t222 = gm_l16;
        gm_tbo4aa19b457h t223;
        memset(&t223, 0, sizeof t223);
        GM_ST(uint8_t, ((uint8_t *)&t223 + 0), ((uint8_t)4ULL));
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t223 + 8), t222);
        t5 = t223;
        goto me6;
mn219:;
ma18:;
        uint8_t t225 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t225 != ((uint8_t)18ULL)) goto mn224;
        void * t226 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l17 = t226;
        void * t227 = gm_l0;
        void * t228 = gm_l17;
        void * t229 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t227, t228);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t230 = gm_l2;
        void * t231 = gm_l1;
        gm_t88rcucg1b7uk t232 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t231) + 0));
        uint64_t t233 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t230, t232);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttf6i6v1da8cm t234;
        GM_ST(void *, ((uint8_t *)&t234 + 0), t229);
        GM_ST(uint64_t, ((uint8_t *)&t234 + 8), t233);
        gm_tbo4aa19b457h t235;
        memset(&t235, 0, sizeof t235);
        GM_ST(uint8_t, ((uint8_t *)&t235 + 0), ((uint8_t)9ULL));
        GM_ST(gm_ttf6i6v1da8cm, ((uint8_t *)&t235 + 8), t234);
        t5 = t235;
        goto me6;
mn224:;
ma19:;
        uint8_t t237 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t237 != ((uint8_t)19ULL)) goto mn236;
        void * t238 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l18 = t238;
        uint32_t t239 = GM_LD(uint32_t, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l19 = t239;
        {
            void * t241 = gm_l0;
            void * t242 = gm_l18;
            void * t243 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t241, t242);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t243;
        }
        gm_tbo4aa19b457h t244;
        void * t246 = gm_l2;
        void * t247 = gm_l18;
        gm_t88rcucg1b7uk t248 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t247) + 0));
        gm_tkn42u7na3t81 t249 = gm_f_bits_struct_ty_22ue20s(t246, GM_CONV(gm_csguankko4lmhd, t248));
        uint8_t t250;
        if (((((uint8_t *)&t249))[4] != 0)) goto mn252;
        t250 = ((uint8_t)0);
        goto me251;
mn252:;
        t250 = ((uint8_t)1);
        goto me251;
mn253:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 302, 16);
me251:;
        if (!t250) goto ie245;
        {
            void * t255 = gm_l2;
            void * t256 = gm_l18;
            gm_t88rcucg1b7uk t257 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t256) + 0));
            uint32_t t258 = gm_l19;
            gm_tplpbqf3duvn3 t259 = gm_f_bits_field_vqua3kd((void *)NULL, cx, NULL, t255, t257, t258);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l21 = t259;
        }
        gm_tdvcgvrf81sbd t260;
        memset((uint8_t *)&t260 + 20, 0, 4);
        void * t261 = gm_l20;
        GM_ST(void *, (uint8_t *)&t260 + 0, t261);
        uint32_t t262 = gm_l19;
        GM_ST(uint32_t, (uint8_t *)&t260 + 8, t262);
        uint32_t t263 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 0));
        GM_ST(uint32_t, (uint8_t *)&t260 + 12, t263);
        gm_t88rcucg1b7uk t264 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l21) + 4));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t260 + 16, t264);
        gm_tbo4aa19b457h t265;
        memset(&t265, 0, sizeof t265);
        GM_ST(uint8_t, ((uint8_t *)&t265 + 0), ((uint8_t)11ULL));
        GM_ST(gm_tdvcgvrf81sbd, ((uint8_t *)&t265 + 8), t260);
        gm_tbo4aa19b457h t266 = t265;
        t244 = t266;
        goto ix245;
ie245:;
        gm_t1j70din2upc4 t268;
        memset((uint8_t *)&t268 + 12, 0, 4);
        memset((uint8_t *)&t268 + 25, 0, 7);
        void * t269 = gm_l20;
        GM_ST(void *, (uint8_t *)&t268 + 0, t269);
        uint32_t t270 = gm_l19;
        GM_ST(uint32_t, (uint8_t *)&t268 + 8, t270);
        void * t271 = gm_l2;
        void * t272 = gm_l18;
        gm_t88rcucg1b7uk t273 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t272) + 0));
        uint32_t t274 = gm_l19;
        gm_tfbnciphf3261 t275 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t271, t273, t274);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint64_t t276 = GM_LD(uint64_t, (((uint8_t *)&t275) + 8));
        GM_ST(uint64_t, (uint8_t *)&t268 + 16, t276);
        void * t277 = gm_l2;
        void * t278 = gm_l18;
        gm_t88rcucg1b7uk t279 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t278) + 0));
        uint8_t t280 = gm_f_is_packed_8fhislp(t277, GM_CONV(gm_csguankko4lmhd, t279));
        GM_ST(uint8_t, (uint8_t *)&t268 + 24, t280);
        gm_tbo4aa19b457h t281;
        memset(&t281, 0, sizeof t281);
        GM_ST(uint8_t, ((uint8_t *)&t281 + 0), ((uint8_t)10ULL));
        GM_ST(gm_t1j70din2upc4, ((uint8_t *)&t281 + 8), t268);
        gm_tbo4aa19b457h t282 = t281;
        t244 = t282;
ix245:;
        gm_tbo4aa19b457h t283 = t244;
        t5 = t283;
        goto me6;
mn236:;
ma20:;
        uint8_t t285 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t285 != ((uint8_t)20ULL)) goto mn284;
        void * t286 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l22 = t286;
        void * t287 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l23 = t287;
        {
            void * t289 = gm_l2;
            void * t290 = gm_l22;
            gm_t88rcucg1b7uk t291 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t290) + 0));
            gm_thdrvp9q0u7b5 t292;
            gm_f_seq_kind_405ip0f((void *)&t292, (void *)NULL, cx, NULL, t289, t291);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_te0mgnjdphs16 t293;
            if (((((uint8_t *)&t292))[16] != 0)) {
                gm_te0mgnjdphs16 t294 = GM_LD(gm_te0mgnjdphs16, (((uint8_t *)&t292) + 0));
                t293 = t294;
            } else {
                gm_t9qkh0rsooc8d t295;
                GM_ST(uint8_t, ((uint8_t *)&t295 + 0), ((uint8_t)1ULL));
                gm_te0mgnjdphs16 t296;
                GM_ST(uint8_t, (uint8_t *)&t296 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t296 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t296 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t296 + 0), t295);
                GM_ST(uint64_t, ((uint8_t *)&t296 + 8), ((uint64_t)0ULL));
                t293 = t296;
            }
            gm_l24 = t293;
        }
        gm_tp0fa5qv7rqnn t297;
        memset((uint8_t *)&t297 + 17, 0, 7);
        void * t298 = gm_l0;
        void * t299 = gm_l22;
        void * t300 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t298, t299);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t297 + 0, t300);
        void * t301 = gm_l0;
        void * t302 = gm_l23;
        void * t303 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t301, t302);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t297 + 8, t303);
        gm_t9qkh0rsooc8d t304 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l24) + 0));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t297 + 16, t304);
        void * t305 = gm_l2;
        void * t306 = gm_l1;
        gm_t88rcucg1b7uk t307 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t306) + 0));
        uint64_t t308 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t305, t307);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint64_t, (uint8_t *)&t297 + 24, t308);
        uint64_t t309 = GM_LD(uint64_t, (((uint8_t *)&gm_l24) + 8));
        GM_ST(uint64_t, (uint8_t *)&t297 + 32, t309);
        gm_tbo4aa19b457h t310;
        memset(&t310, 0, sizeof t310);
        GM_ST(uint8_t, ((uint8_t *)&t310 + 0), ((uint8_t)12ULL));
        GM_ST(gm_tp0fa5qv7rqnn, ((uint8_t *)&t310 + 8), t297);
        gm_tbo4aa19b457h t311 = t310;
        t5 = t311;
        goto me6;
mn284:;
ma21:;
        uint8_t t313 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t313 != ((uint8_t)21ULL)) goto mn312;
        gm_tao63pk3r18v5 t314 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t4) + 8) + 8));
        gm_l25 = t314;
        {
            void * t316 = gm_l2;
            void * t317 = GM_LD(void *, (((uint8_t *)&gm_l25) + 0));
            gm_t88rcucg1b7uk t318 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t317) + 0));
            gm_thdrvp9q0u7b5 t319;
            gm_f_seq_kind_405ip0f((void *)&t319, (void *)NULL, cx, NULL, t316, t318);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_te0mgnjdphs16 t320;
            if (((((uint8_t *)&t319))[16] != 0)) {
                gm_te0mgnjdphs16 t321 = GM_LD(gm_te0mgnjdphs16, (((uint8_t *)&t319) + 0));
                t320 = t321;
            } else {
                gm_t9qkh0rsooc8d t322;
                GM_ST(uint8_t, ((uint8_t *)&t322 + 0), ((uint8_t)1ULL));
                gm_te0mgnjdphs16 t323;
                GM_ST(uint8_t, (uint8_t *)&t323 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t323 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t323 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t323 + 0), t322);
                GM_ST(uint64_t, ((uint8_t *)&t323 + 8), ((uint64_t)0ULL));
                t320 = t323;
            }
            gm_l26 = t320;
        }
        {
            gm_l27 = ((uint64_t)0ULL);
        }
        {
            void * t325 = gm_l2;
            void * t326 = gm_l1;
            gm_t88rcucg1b7uk t327 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t326) + 0));
            gm_t53at80pfdus3 t328;
            gm_f_kind_htv6093((void *)&t328, t325, GM_CONV(gm_csguankko4lmhd, t327));
            uint8_t t329 = GM_LD(uint8_t, (((uint8_t *)&t328) + 0));
            if (t329 != ((uint8_t)10ULL)) goto ie324;
            gm_t88rcucg1b7uk t330 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t328) + 8) + 4));
            gm_l28 = t330;
            {
                void * t332 = gm_l2;
                gm_t88rcucg1b7uk t333 = gm_l28;
                uint64_t t334 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t332, t333);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l27 = t334;
            }
            goto ix324;
ie324:;
ix324:;
        }
        gm_tt49maei14cl6 t335;
        memset((uint8_t *)&t335 + 26, 0, 6);
        void * t336 = gm_l0;
        void * t337 = GM_LD(void *, (((uint8_t *)&gm_l25) + 0));
        void * t338 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t336, t337);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t335 + 0, t338);
        void * t339 = gm_l0;
        void * t340 = GM_LD(void *, (((uint8_t *)&gm_l25) + 8));
        void * t341 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t339, t340);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t335 + 8, t341);
        void * t342 = gm_l0;
        void * t343 = GM_LD(void *, (((uint8_t *)&gm_l25) + 16));
        void * t344 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t342, t343);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t335 + 16, t344);
        uint8_t t345 = GM_LD(uint8_t, (((uint8_t *)&gm_l25) + 24));
        GM_ST(uint8_t, (uint8_t *)&t335 + 24, t345);
        gm_t9qkh0rsooc8d t346 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l26) + 0));
        GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t335 + 25, t346);
        uint64_t t347 = gm_l27;
        GM_ST(uint64_t, (uint8_t *)&t335 + 32, t347);
        uint64_t t348 = GM_LD(uint64_t, (((uint8_t *)&gm_l26) + 8));
        GM_ST(uint64_t, (uint8_t *)&t335 + 40, t348);
        gm_tbo4aa19b457h t349;
        memset(&t349, 0, sizeof t349);
        GM_ST(uint8_t, ((uint8_t *)&t349 + 0), ((uint8_t)13ULL));
        GM_ST(gm_tt49maei14cl6, ((uint8_t *)&t349 + 8), t335);
        gm_tbo4aa19b457h t350 = t349;
        t5 = t350;
        goto me6;
mn312:;
ma22:;
        uint8_t t352 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t352 != ((uint8_t)22ULL)) goto mn351;
        void * t353 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l29 = t353;
        void * t354 = gm_l0;
        void * t355 = gm_l29;
        void * t356 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t354, t355);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t357;
        memset(&t357, 0, sizeof t357);
        GM_ST(uint8_t, ((uint8_t *)&t357 + 0), ((uint8_t)14ULL));
        GM_ST(void *, ((uint8_t *)&t357 + 8), t356);
        t5 = t357;
        goto me6;
mn351:;
ma23:;
        uint8_t t359 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t359 != ((uint8_t)23ULL)) goto mn358;
        void * t360 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l30 = t360;
        void * t361 = gm_l0;
        void * t362 = gm_l30;
        void * t363 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t361, t362);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t364;
        memset(&t364, 0, sizeof t364);
        GM_ST(uint8_t, ((uint8_t *)&t364 + 0), ((uint8_t)15ULL));
        GM_ST(void *, ((uint8_t *)&t364 + 8), t363);
        t5 = t364;
        goto me6;
mn358:;
ma24:;
        uint8_t t366 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t366 != ((uint8_t)24ULL)) goto mn365;
        void * t367 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l31 = t367;
        void * t368 = gm_l0;
        void * t369 = gm_l31;
        void * t370 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t368, t369);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t371;
        memset(&t371, 0, sizeof t371);
        GM_ST(uint8_t, ((uint8_t *)&t371 + 0), ((uint8_t)16ULL));
        GM_ST(void *, ((uint8_t *)&t371 + 8), t370);
        t5 = t371;
        goto me6;
mn365:;
ma25:;
        uint8_t t373 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t373 != ((uint8_t)25ULL)) goto mn372;
        void * t374 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l32 = t374;
        void * t375 = gm_l0;
        void * t376 = gm_l32;
        void * t377 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t375, t376);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t378 = gm_l2;
        void * t379 = gm_l32;
        gm_t88rcucg1b7uk t380 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t379) + 0));
        uint64_t t381 = gm_f_align_15fjgq1((void *)NULL, cx, NULL, t378, t380);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttf6i6v1da8cm t382;
        GM_ST(void *, ((uint8_t *)&t382 + 0), t377);
        GM_ST(uint64_t, ((uint8_t *)&t382 + 8), t381);
        gm_tbo4aa19b457h t383;
        memset(&t383, 0, sizeof t383);
        GM_ST(uint8_t, ((uint8_t *)&t383 + 0), ((uint8_t)17ULL));
        GM_ST(gm_ttf6i6v1da8cm, ((uint8_t *)&t383 + 8), t382);
        t5 = t383;
        goto me6;
mn372:;
ma26:;
        uint8_t t385 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t385 != ((uint8_t)26ULL)) goto mn384;
        uint32_t t386 = GM_LD(uint32_t, ((((uint8_t *)t4) + 8) + 8));
        gm_l33 = t386;
        uint32_t t387 = gm_l33;
        void * t388 = gm_l2;
        void * t389 = gm_l1;
        gm_t88rcucg1b7uk t390 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t389) + 0));
        gm_t53at80pfdus3 t391;
        gm_f_kind_htv6093((void *)&t391, t388, GM_CONV(gm_csguankko4lmhd, t390));
        uint8_t t392;
        uint8_t t395 = GM_LD(uint8_t, (((uint8_t *)&t391) + 0));
        if (t395 != ((uint8_t)15ULL)) goto mn394;
        t392 = ((uint8_t)1);
        goto me393;
mn394:;
        t392 = ((uint8_t)0);
        goto me393;
mn396:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 323, 32);
me393:;
        gm_t9805qtonbsag t397;
        GM_ST(uint8_t, (uint8_t *)&t397 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t397 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t397 + 0), t387);
        GM_ST(uint8_t, ((uint8_t *)&t397 + 4), t392);
        gm_tbo4aa19b457h t398;
        memset(&t398, 0, sizeof t398);
        GM_ST(uint8_t, ((uint8_t *)&t398 + 0), ((uint8_t)18ULL));
        GM_ST(gm_t9805qtonbsag, ((uint8_t *)&t398 + 8), t397);
        t5 = t398;
        goto me6;
mn384:;
ma27:;
        uint8_t t400 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t400 != ((uint8_t)27ULL)) goto mn399;
        uint32_t t401 = GM_LD(uint32_t, ((((uint8_t *)t4) + 8) + 8));
        gm_l34 = t401;
        uint32_t t402 = gm_l34;
        gm_tbo4aa19b457h t403;
        memset(&t403, 0, sizeof t403);
        GM_ST(uint8_t, ((uint8_t *)&t403 + 0), ((uint8_t)19ULL));
        GM_ST(uint32_t, ((uint8_t *)&t403 + 8), t402);
        t5 = t403;
        goto me6;
mn399:;
ma28:;
        uint8_t t405 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t405 != ((uint8_t)28ULL)) goto mn404;
        uint32_t t406 = GM_LD(uint32_t, ((((uint8_t *)t4) + 8) + 8));
        gm_l35 = t406;
        uint32_t t407 = gm_l35;
        gm_tbo4aa19b457h t408;
        memset(&t408, 0, sizeof t408);
        GM_ST(uint8_t, ((uint8_t *)&t408 + 0), ((uint8_t)20ULL));
        GM_ST(uint32_t, ((uint8_t *)&t408 + 8), t407);
        t5 = t408;
        goto me6;
mn404:;
ma29:;
        uint8_t t410 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t410 != ((uint8_t)29ULL)) goto mn409;
        gm_t9qkh0rsooc8d t411 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l36 = t411;
        void * t412 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l37 = t412;
        void * t413 = gm_l0;
        gm_t9qkh0rsooc8d t414 = gm_l36;
        void * t415 = gm_l37;
        void * t416 = gm_l1;
        gm_tbo4aa19b457h t417;
        gm_f_low_unary_3f5k6s5((void *)&t417, (void *)NULL, cx, NULL, t413, t414, t415, t416);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t5 = t417;
        goto me6;
mn409:;
ma30:;
        uint8_t t419 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t419 != ((uint8_t)30ULL)) goto mn418;
        gm_ts6oqppjh0ba3 t420 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l38 = t420;
        void * t421 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l39 = t421;
        void * t422 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l40 = t422;
        void * t423 = gm_l0;
        gm_ts6oqppjh0ba3 t424 = gm_l38;
        void * t425 = gm_l39;
        void * t426 = gm_l40;
        void * t427 = gm_l1;
        gm_tbo4aa19b457h t428;
        gm_f_low_binary_5dhmccv((void *)&t428, (void *)NULL, cx, NULL, t423, t424, t425, t426, t427);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t5 = t428;
        goto me6;
mn418:;
ma31:;
        uint8_t t430 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t430 != ((uint8_t)31ULL)) goto mn429;
        uint8_t t431 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l41 = t431;
        void * t432 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l42 = t432;
        void * t433 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l43 = t433;
        gm_ta612fil812pm t434;
        memset((uint8_t *)&t434 + 1, 0, 7);
        uint8_t t435 = gm_l41;
        GM_ST(uint8_t, (uint8_t *)&t434 + 0, t435);
        void * t436 = gm_l0;
        void * t437 = gm_l42;
        void * t438 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t436, t437);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t434 + 8, t438);
        void * t439 = gm_l0;
        void * t440 = gm_l43;
        void * t441 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t439, t440);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t434 + 16, t441);
        gm_tbo4aa19b457h t442;
        memset(&t442, 0, sizeof t442);
        GM_ST(uint8_t, ((uint8_t *)&t442 + 0), ((uint8_t)32ULL));
        GM_ST(gm_ta612fil812pm, ((uint8_t *)&t442 + 8), t434);
        t5 = t442;
        goto me6;
mn429:;
ma32:;
        uint8_t t444 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t444 != ((uint8_t)32ULL)) goto mn443;
        gm_tmn7n0n9raou5 t445 = GM_LD(gm_tmn7n0n9raou5, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l44 = t445;
        void * t446 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l45 = t446;
        void * t447 = gm_l0;
        gm_tmn7n0n9raou5 t448 = gm_l44;
        void * t449 = gm_l45;
        void * t450 = gm_l1;
        gm_tbo4aa19b457h t451;
        gm_f_low_coerce_rhvh5p0((void *)&t451, (void *)NULL, cx, NULL, t447, t448, t449, t450);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t5 = t451;
        goto me6;
mn443:;
ma33:;
        uint8_t t453 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t453 != ((uint8_t)33ULL)) goto mn452;
        gm_tvdg94bk7aa22 t454 = GM_LD(gm_tvdg94bk7aa22, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l46 = t454;
        gm_b16_8 t455 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l47 = t455;
        void * t456 = gm_l0;
        gm_tvdg94bk7aa22 t457 = gm_l46;
        gm_b16_8 t458 = gm_l47;
        void * t459 = gm_l1;
        gm_tbo4aa19b457h t460;
        gm_f_low_builtin_rbo6661((void *)&t460, (void *)NULL, cx, NULL, t456, t457, t458, t459);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t5 = t460;
        goto me6;
mn452:;
ma34:;
        uint8_t t462 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t462 != ((uint8_t)34ULL)) goto mn461;
        gm_tsth9qu1dv0g3 t463 = GM_LD(gm_tsth9qu1dv0g3, (((((uint8_t *)t4) + 8) + 8) + 0));
        F.l48 = t463;
        gm_b16_8 t464 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 8) + 8) + 40));
        gm_l49 = t464;
        gm_tbo4aa19b457h t465;
        uint8_t t471 = GM_LD(uint8_t, (((uint8_t *)&F.l48) + 0));
        switch (t471) { case ((uint8_t)0ULL): goto ma467; case ((uint8_t)1ULL): goto ma468; case ((uint8_t)2ULL): goto ma469; default: goto ma470; }
ma467:;
        uint8_t t473 = GM_LD(uint8_t, (((uint8_t *)&F.l48) + 0));
        if (t473 != ((uint8_t)0ULL)) goto mn472;
        uint32_t t474 = GM_LD(uint32_t, (((uint8_t *)&F.l48) + 8));
        gm_l50 = t474;
        gm_tiilmfd16v3fc t475;
        memset((uint8_t *)&t475 + 4, 0, 4);
        uint32_t t476 = gm_l50;
        GM_ST(uint32_t, (uint8_t *)&t475 + 0, t476);
        void * t477 = gm_l0;
        gm_b16_8 t478 = gm_l49;
        gm_b16_8 t479 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t477, t478);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t475 + 8, t479);
        gm_tbo4aa19b457h t480;
        memset(&t480, 0, sizeof t480);
        GM_ST(uint8_t, ((uint8_t *)&t480 + 0), ((uint8_t)35ULL));
        GM_ST(gm_tiilmfd16v3fc, ((uint8_t *)&t480 + 8), t475);
        t465 = t480;
        goto me466;
mn472:;
ma468:;
        uint8_t t482 = GM_LD(uint8_t, (((uint8_t *)&F.l48) + 0));
        if (t482 != ((uint8_t)1ULL)) goto mn481;
        gm_tiilmfd16v3fc t483 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)&F.l48) + 8) + 0));
        gm_l51 = t483;
        uint32_t t484 = GM_LD(uint32_t, ((((uint8_t *)&F.l48) + 8) + 24));
        gm_l52 = t484;
        gm_tf4r0415i49d1 t485;
        memset((uint8_t *)&t485 + 28, 0, 4);
        memcpy((uint8_t *)&t485 + 0, ((uint8_t *)&gm_l51), 24);
        uint32_t t486 = gm_l52;
        GM_ST(uint32_t, (uint8_t *)&t485 + 24, t486);
        void * t487 = gm_l0;
        gm_b16_8 t488 = gm_l49;
        gm_b16_8 t489 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t487, t488);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t485 + 32, t489);
        gm_tbo4aa19b457h t490;
        memset(&t490, 0, sizeof t490);
        GM_ST(uint8_t, ((uint8_t *)&t490 + 0), ((uint8_t)36ULL));
        GM_ST(gm_tf4r0415i49d1, ((uint8_t *)&t490 + 8), t485);
        t465 = t490;
        goto me466;
mn481:;
ma469:;
        uint8_t t492 = GM_LD(uint8_t, (((uint8_t *)&F.l48) + 0));
        if (t492 != ((uint8_t)2ULL)) goto mn491;
        void * t493 = GM_LD(void *, (((uint8_t *)&F.l48) + 8));
        gm_l53 = t493;
        {
            void * t495 = gm_l0;
            void * t496 = gm_l53;
            void * t497 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t495, t496);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l54 = t497;
        }
        gm_tkaapcii86udk t498;
        void * t499 = gm_l54;
        GM_ST(void *, (uint8_t *)&t498 + 0, t499);
        void * t500 = gm_l0;
        gm_b16_8 t501 = gm_l49;
        gm_b16_8 t502 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t500, t501);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t498 + 8, t502);
        gm_tbo4aa19b457h t503;
        memset(&t503, 0, sizeof t503);
        GM_ST(uint8_t, ((uint8_t *)&t503 + 0), ((uint8_t)37ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t503 + 8), t498);
        gm_tbo4aa19b457h t504 = t503;
        t465 = t504;
        goto me466;
mn491:;
ma470:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 331, 32);
me466:;
        t5 = t465;
        goto me6;
mn461:;
ma35:;
        uint8_t t506 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t506 != ((uint8_t)35ULL)) goto mn505;
        gm_b16_8 t507 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l55 = t507;
        gm_tbo4aa19b457h t509;
        void * t511 = gm_l2;
        void * t512 = gm_l1;
        gm_t88rcucg1b7uk t513 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t512) + 0));
        gm_tkn42u7na3t81 t514 = gm_f_bits_struct_ty_22ue20s(t511, GM_CONV(gm_csguankko4lmhd, t513));
        uint8_t t515;
        if (((((uint8_t *)&t514))[4] != 0)) goto mn517;
        t515 = ((uint8_t)0);
        goto me516;
mn517:;
        t515 = ((uint8_t)1);
        goto me516;
mn518:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 340, 16);
me516:;
        if (!t515) goto ie510;
        void * t520 = gm_l0;
        gm_b16_8 t521 = gm_l55;
        gm_b16_8 t522 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t520, t521);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t523;
        memset(&t523, 0, sizeof t523);
        GM_ST(uint8_t, ((uint8_t *)&t523 + 0), ((uint8_t)40ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t523 + 8), t522);
        gm_tbo4aa19b457h t524 = t523;
        t509 = t524;
        goto ix510;
ie510:;
        gm_tbo4aa19b457h t525;
        void * t527 = gm_l2;
        void * t528 = gm_l1;
        gm_t88rcucg1b7uk t529 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t528) + 0));
        void * t530 = gm_f_adt_of_8k5vqud(t527, GM_CONV(gm_csguankko4lmhd, t529));
        if (!(GM_LD(void *, ((uint8_t *)&t530)) != 0)) goto ie526;
        void * t531 = GM_LD(void *, ((uint8_t *)&t530));
        gm_l56 = t531;
        void * t532 = gm_l56;
        uint8_t t533;
        uint8_t t536 = GM_LD(uint8_t, ((((uint8_t *)t532) + 24) + 0));
        if (t536 != ((uint8_t)2ULL)) goto mn535;
        t533 = ((uint8_t)1);
        goto me534;
mn535:;
        t533 = ((uint8_t)0);
        goto me534;
mn537:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 342, 48);
me534:;
        if (!t533) goto ie526;
        void * t539;
        uint64_t t541 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
        uint8_t t542 = (t541 > ((uint64_t)0ULL));
        if (!t542) goto ie540;
        void * t544 = gm_l0;
        uint64_t t545 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t546 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
        if (t545 >= (uintptr_t)(t546)) gm_trap_index(t545, t546, gm_src7afujsggtlje, 343, 60);
        void * t547 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l55)) + t545 * 88);
        void * t548 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t544, t547);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t549 = t548;
        t539 = t549;
        goto ix540;
ie540:;
        void * t551 = ((void *)0);
        t539 = t551;
ix540:;
        gm_tbo4aa19b457h t552;
        memset(&t552, 0, sizeof t552);
        GM_ST(uint8_t, ((uint8_t *)&t552 + 0), ((uint8_t)39ULL));
        GM_ST(void *, ((uint8_t *)&t552 + 8), t539);
        gm_tbo4aa19b457h t553 = t552;
        t525 = t553;
        goto ix526;
ie526:;
        {
            uint64_t t555 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
            gm_tabb710d9mq2e t556;
            gm_f_make_phdnqpj((void *)&t556, (void *)NULL, cx, NULL, t555, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krt2fusbrlm)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t556))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t556) + 8, gm_src7afujsggtlje, 345, 25);
            gm_b16_8 t557 = GM_LD(gm_b16_8, (((uint8_t *)&t556) + 8));
            gm_l57 = t557;
        }
        {
            {
                gm_l59 = ((uint64_t)0ULL);
            }
            {
                uint64_t t559 = GM_LD(uintptr_t, ((uint8_t *)&gm_l55) + 8);
                gm_l58 = t559;
            }
            {
                for (;;) {
                    uint64_t t561 = gm_l59;
                    uint64_t t562 = gm_l58;
                    uint8_t t563 = (t561 < t562);
                    if (!t563) goto lb560;
                    {
                        uint64_t t566 = gm_l59;
                        uint64_t t569 = GM_LD(uint64_t, ((uint8_t *)&t566));
                        gm_l60 = t569;
                        {
                            uint64_t t571 = gm_l60;
                            uint64_t t572 = (uintptr_t)t571;
                            uint64_t t573 = GM_LD(uintptr_t, ((uint8_t *)&gm_l57) + 8);
                            if (t572 >= (uintptr_t)(t573)) gm_trap_index(t572, t573, gm_src7afujsggtlje, 346, 41);
                            uint8_t *t574 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l57)) + t572 * 8);
                            void * t575 = gm_l2;
                            void * t576 = gm_l1;
                            gm_t88rcucg1b7uk t577 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t576) + 0));
                            uint64_t t578 = gm_l60;
                            uint32_t t579 = (uint32_t)t578;
                            gm_tfbnciphf3261 t580 = gm_f_field_layout_h34salb((void *)NULL, cx, NULL, t575, t577, t579);
                            if (gm_unw.kind) {
                                goto gm_ll564;
                            }
                            uint64_t t581 = GM_LD(uint64_t, (((uint8_t *)&t580) + 8));
                            GM_ST(uint64_t, t574, t581);
                        }
                        goto me567;
mn568:;
                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 346, 17);
me567:;
                    }
lc560:;
                    {
                        uint64_t t583 = gm_l59;
                        uint64_t r584; bool o585 = __builtin_add_overflow(t583, ((uint64_t)1ULL), &r584);
                        uint64_t t586 = r584;
                        gm_l59 = t586;
                    }
                }
gm_ll564:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb560; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc560; }
                }
                goto gm_lad;
lb560:;
            }
        }
        gm_t6j6smj7qoctu t587;
        void * t588 = gm_l0;
        gm_b16_8 t589 = gm_l55;
        gm_b16_8 t590 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t588, t589);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t587 + 0, t590);
        gm_b16_8 t591 = gm_l57;
        GM_ST(gm_b16_8, (uint8_t *)&t587 + 16, t591);
        gm_tbo4aa19b457h t592;
        memset(&t592, 0, sizeof t592);
        GM_ST(uint8_t, ((uint8_t *)&t592 + 0), ((uint8_t)38ULL));
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t592 + 8), t587);
        gm_tbo4aa19b457h t593 = t592;
        t525 = t593;
ix526:;
        t509 = t525;
ix510:;
        gm_tbo4aa19b457h t594 = t509;
        t5 = t594;
        goto me6;
mn505:;
ma36:;
        uint8_t t596 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t596 != ((uint8_t)36ULL)) goto mn595;
        gm_b16_8 t597 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l61 = t597;
        void * t598 = gm_l0;
        gm_b16_8 t599 = gm_l61;
        gm_b16_8 t600 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t598, t599);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t601;
        memset(&t601, 0, sizeof t601);
        GM_ST(uint8_t, ((uint8_t *)&t601 + 0), ((uint8_t)41ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t601 + 8), t600);
        t5 = t601;
        goto me6;
mn595:;
ma37:;
        uint8_t t603 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t603 != ((uint8_t)37ULL)) goto mn602;
        void * t604 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l62 = t604;
        void * t605 = gm_l0;
        void * t606 = gm_l62;
        void * t607 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t605, t606);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t608;
        memset(&t608, 0, sizeof t608);
        GM_ST(uint8_t, ((uint8_t *)&t608 + 0), ((uint8_t)42ULL));
        GM_ST(void *, ((uint8_t *)&t608 + 8), t607);
        t5 = t608;
        goto me6;
mn602:;
ma38:;
        uint8_t t610 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t610 != ((uint8_t)38ULL)) goto mn609;
        gm_ttggq7ng4kp3p t611 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t4) + 8) + 8));
        gm_l63 = t611;
        uint32_t t612 = GM_LD(uint32_t, (((uint8_t *)&gm_l63) + 0));
        void * t613 = gm_l0;
        void * t614 = GM_LD(void *, (((uint8_t *)&gm_l63) + 8));
        void * t615 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t613, t614);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttggq7ng4kp3p t616;
        GM_ST(uint32_t, (uint8_t *)&t616 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t616 + 0), t612);
        GM_ST(void *, ((uint8_t *)&t616 + 8), t615);
        gm_tbo4aa19b457h t617;
        memset(&t617, 0, sizeof t617);
        GM_ST(uint8_t, ((uint8_t *)&t617 + 0), ((uint8_t)43ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t617 + 8), t616);
        t5 = t617;
        goto me6;
mn609:;
ma39:;
        uint8_t t619 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t619 != ((uint8_t)39ULL)) goto mn618;
        void * t620 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l64 = t620;
        void * t621 = gm_l0;
        void * t622 = gm_l64;
        void * t623 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t621, t622);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t624;
        memset(&t624, 0, sizeof t624);
        GM_ST(uint8_t, ((uint8_t *)&t624 + 0), ((uint8_t)44ULL));
        GM_ST(void *, ((uint8_t *)&t624 + 8), t623);
        t5 = t624;
        goto me6;
mn618:;
ma40:;
        uint8_t t626 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t626 != ((uint8_t)40ULL)) goto mn625;
        void * t627 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l65 = t627;
        uint8_t t628 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l66 = t628;
        void * t629 = gm_l0;
        void * t630 = gm_l65;
        void * t631 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t629, t630);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t632 = gm_l66;
        gm_tsplfr33dsvji t633;
        GM_ST(uint8_t, (uint8_t *)&t633 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t633 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t633 + 12, 0);
        GM_ST(void *, ((uint8_t *)&t633 + 0), t631);
        GM_ST(uint8_t, ((uint8_t *)&t633 + 8), t632);
        gm_tbo4aa19b457h t634;
        memset(&t634, 0, sizeof t634);
        GM_ST(uint8_t, ((uint8_t *)&t634 + 0), ((uint8_t)45ULL));
        GM_ST(gm_tsplfr33dsvji, ((uint8_t *)&t634 + 8), t633);
        t5 = t634;
        goto me6;
mn625:;
ma41:;
        uint8_t t636 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t636 != ((uint8_t)41ULL)) goto mn635;
        gm_t3dcoc7077t1q t637 = GM_LD(gm_t3dcoc7077t1q, ((((uint8_t *)t4) + 8) + 8));
        gm_l67 = t637;
        gm_t3dcoc7077t1q t638;
        void * t639 = gm_l0;
        void * t640 = GM_LD(void *, (((uint8_t *)&gm_l67) + 0));
        void * t641 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t639, t640);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t638 + 0, t641);
        gm_t9805qtonbsag t642 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l67) + 8));
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t638 + 8, t642);
        void * t643 = gm_l0;
        void * t644 = GM_LD(void *, (((uint8_t *)&gm_l67) + 16));
        void * t645 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t643, t644);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t638 + 16, t645);
        gm_tbo4aa19b457h t646;
        memset(&t646, 0, sizeof t646);
        GM_ST(uint8_t, ((uint8_t *)&t646 + 0), ((uint8_t)46ULL));
        GM_ST(gm_t3dcoc7077t1q, ((uint8_t *)&t646 + 8), t638);
        t5 = t646;
        goto me6;
mn635:;
ma42:;
        uint8_t t648 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t648 != ((uint8_t)42ULL)) goto mn647;
        gm_t083kd7b1kkag t649 = GM_LD(gm_t083kd7b1kkag, ((((uint8_t *)t4) + 8) + 8));
        gm_l68 = t649;
        gm_t083kd7b1kkag t650;
        memset((uint8_t *)&t650 + 20, 0, 4);
        void * t651 = gm_l0;
        void * t652 = GM_LD(void *, (((uint8_t *)&gm_l68) + 0));
        void * t653 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t651, t652);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t650 + 0, t653);
        gm_t9805qtonbsag t654 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l68) + 8));
        GM_ST(gm_t9805qtonbsag, (uint8_t *)&t650 + 8, t654);
        uint32_t t655 = GM_LD(uint32_t, (((uint8_t *)&gm_l68) + 16));
        GM_ST(uint32_t, (uint8_t *)&t650 + 16, t655);
        gm_tbo4aa19b457h t656;
        memset(&t656, 0, sizeof t656);
        GM_ST(uint8_t, ((uint8_t *)&t656 + 0), ((uint8_t)47ULL));
        GM_ST(gm_t083kd7b1kkag, ((uint8_t *)&t656 + 8), t650);
        t5 = t656;
        goto me6;
mn647:;
ma43:;
        uint8_t t658 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t658 != ((uint8_t)43ULL)) goto mn657;
        void * t659 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l69 = t659;
        void * t660 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l70 = t660;
        uint8_t t661 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l71 = t661;
        {
            void * t663 = gm_l0;
            void * t664 = gm_l69;
            void * t665 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t663, t664);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l72 = t665;
        }
        {
            void * t666 = gm_l0;
            void * t667 = gm_l70;
            void * t668 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t666, t667);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l73 = t668;
        }
        gm_tbo4aa19b457h t669;
        void * t671 = gm_l2;
        void * t672 = gm_l69;
        gm_t88rcucg1b7uk t673 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t672) + 0));
        gm_tbgiuvo2j5vcs t674 = gm_f_eq_word_itm79nq((void *)NULL, cx, NULL, t671, t673);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t674))[8] != 0)) goto ie670;
        gm_tmn7n0n9raou5 t675 = GM_LD(gm_tmn7n0n9raou5, ((uint8_t *)&t674));
        gm_l74 = t675;
        void * t676 = gm_l2;
        void * t677 = gm_l69;
        gm_t88rcucg1b7uk t678 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t677) + 0));
        uint64_t t679 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t676, t678);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t680 = (t679 <= ((uint64_t)8ULL));
        if (!t680) goto ie670;
        gm_ta6jn77i9rmqf t682;
        memset((uint8_t *)&t682 + 25, 0, 7);
        gm_tmn7n0n9raou5 t683 = gm_l74;
        GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t682 + 0, t683);
        void * t684 = gm_l72;
        GM_ST(void *, (uint8_t *)&t682 + 8, t684);
        void * t685 = gm_l73;
        GM_ST(void *, (uint8_t *)&t682 + 16, t685);
        uint8_t t686 = gm_l71;
        GM_ST(uint8_t, (uint8_t *)&t682 + 24, t686);
        gm_tbo4aa19b457h t687;
        memset(&t687, 0, sizeof t687);
        GM_ST(uint8_t, ((uint8_t *)&t687 + 0), ((uint8_t)24ULL));
        GM_ST(gm_ta6jn77i9rmqf, ((uint8_t *)&t687 + 8), t682);
        gm_tbo4aa19b457h t688 = t687;
        t669 = t688;
        goto ix670;
ie670:;
        gm_tlcl4u558l6p5 t690;
        memset((uint8_t *)&t690 + 17, 0, 7);
        void * t691 = gm_l72;
        GM_ST(void *, (uint8_t *)&t690 + 0, t691);
        void * t692 = gm_l73;
        GM_ST(void *, (uint8_t *)&t690 + 8, t692);
        uint8_t t693 = gm_l71;
        GM_ST(uint8_t, (uint8_t *)&t690 + 16, t693);
        gm_tbo4aa19b457h t694;
        memset(&t694, 0, sizeof t694);
        GM_ST(uint8_t, ((uint8_t *)&t694 + 0), ((uint8_t)48ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t694 + 8), t690);
        gm_tbo4aa19b457h t695 = t694;
        t669 = t695;
ix670:;
        gm_tbo4aa19b457h t696 = t669;
        t5 = t696;
        goto me6;
mn657:;
ma44:;
        uint8_t t698 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t698 != ((uint8_t)45ULL)) goto mn697;
        void * t699 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l75 = t699;
        void * t700 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l76 = t700;
        uint8_t t701 = GM_LD(uint8_t, (((((uint8_t *)t4) + 8) + 8) + 16));
        gm_l77 = t701;
        gm_tlcl4u558l6p5 t702;
        memset((uint8_t *)&t702 + 17, 0, 7);
        void * t703 = gm_l0;
        void * t704 = gm_l75;
        void * t705 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t703, t704);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t702 + 0, t705);
        void * t706 = gm_l0;
        void * t707 = gm_l76;
        void * t708 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t706, t707);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t702 + 8, t708);
        uint8_t t709 = gm_l77;
        GM_ST(uint8_t, (uint8_t *)&t702 + 16, t709);
        gm_tbo4aa19b457h t710;
        memset(&t710, 0, sizeof t710);
        GM_ST(uint8_t, ((uint8_t *)&t710 + 0), ((uint8_t)49ULL));
        GM_ST(gm_tlcl4u558l6p5, ((uint8_t *)&t710 + 8), t702);
        t5 = t710;
        goto me6;
mn697:;
ma45:;
        uint8_t t712 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t712 != ((uint8_t)44ULL)) goto mn711;
        void * t713 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l78 = t713;
        gm_b16_8 t714 = GM_LD(gm_b16_8, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l79 = t714;
        {
            void * t716 = gm_l0;
            void * t717 = gm_l78;
            void * t718 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t716, t717);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l80 = t718;
        }
        {
            gm_f_list_58nr42v_4((void *)&F.l81, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t720 = gm_l79;
                gm_l82 = t720;
            }
            {
                gm_l83 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t722 = gm_l83;
                    uint64_t t723 = GM_LD(uintptr_t, ((uint8_t *)&gm_l82) + 8);
                    uint8_t t724 = (t722 < t723);
                    if (!t724) goto lb721;
                    {
                        uint64_t t727 = gm_l83;
                        uint64_t t728 = (uintptr_t)t727;
                        uint64_t t729 = GM_LD(uintptr_t, ((uint8_t *)&gm_l82) + 8);
                        gm_tbbqefuhv2r8h t732 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l82)) + t728 * 24));
                        gm_l84 = t732;
                        {
                            void * t734 = (void *)((uint8_t *)&F.l81);
                            gm_tbbqefuhv2r8h t735;
                            void * t736 = gm_l0;
                            void * t737 = GM_LD(void *, (((uint8_t *)&gm_l84) + 0));
                            void * t738 = gm_f_low_pat_bba97qc((void *)NULL, cx, NULL, t736, t737);
                            if (gm_unw.kind) {
                                goto gm_ll725;
                            }
                            GM_ST(void *, (uint8_t *)&t735 + 0, t738);
                            void * t739 = gm_l0;
                            void * t740 = GM_LD(void *, (((uint8_t *)&gm_l84) + 8));
                            void * t741 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t739, t740);
                            if (gm_unw.kind) {
                                goto gm_ll725;
                            }
                            GM_ST(void *, (uint8_t *)&t735 + 8, t741);
                            void * t742 = gm_l0;
                            void * t743 = GM_LD(void *, (((uint8_t *)&gm_l84) + 16));
                            void * t744 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t742, t743);
                            if (gm_unw.kind) {
                                goto gm_ll725;
                            }
                            GM_ST(void *, (uint8_t *)&t735 + 16, t744);
                            gm_tftb1odc739t5 t745 = gm_f_push_bpovr2k_4(t734, &t735);
                            if (((((uint8_t *)&t745))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t745) + 1, gm_src7afujsggtlje, 366, 31);
                            gm_b0_1 t746 = GM_LD(gm_b0_1, (((uint8_t *)&t745) + 1));
                        }
                        goto me730;
mn731:;
                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 366, 13);
me730:;
                    }
lc721:;
                    {
                        uint64_t t748 = gm_l83;
                        uint64_t r749; bool o750 = __builtin_add_overflow(t748, ((uint64_t)1ULL), &r749);
                        uint64_t t751 = r749;
                        gm_l83 = t751;
                    }
                }
gm_ll725:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb721; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc721; }
                }
                goto gm_lad;
lb721:;
            }
        }
        gm_tkaapcii86udk t752;
        void * t753 = gm_l80;
        GM_ST(void *, (uint8_t *)&t752 + 0, t753);
        gm_b16_8 t754 = GM_LD(gm_b16_8, (((uint8_t *)&F.l81) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t752 + 8, t754);
        gm_tbo4aa19b457h t755;
        memset(&t755, 0, sizeof t755);
        GM_ST(uint8_t, ((uint8_t *)&t755 + 0), ((uint8_t)50ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t755 + 8), t752);
        gm_tbo4aa19b457h t756 = t755;
        t5 = t756;
        goto me6;
mn711:;
ma46:;
        uint8_t t758 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t758 != ((uint8_t)46ULL)) goto mn757;
        void * t759 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l85 = t759;
        void * t760 = gm_l0;
        void * t761 = gm_l85;
        void * t762 = gm_f_low_block_klh2c88((void *)NULL, cx, NULL, t760, t761);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t763;
        memset(&t763, 0, sizeof t763);
        GM_ST(uint8_t, ((uint8_t *)&t763 + 0), ((uint8_t)51ULL));
        GM_ST(void *, ((uint8_t *)&t763 + 8), t762);
        t5 = t763;
        goto me6;
mn757:;
ma47:;
        uint8_t t765 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t765 != ((uint8_t)47ULL)) goto mn764;
        gm_trl2khmuphncd t766 = GM_LD(gm_trl2khmuphncd, ((((uint8_t *)t4) + 8) + 8));
        gm_l86 = t766;
        gm_trl2khmuphncd t767;
        void * t768 = gm_l0;
        gm_b16_8 t769 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l86) + 0));
        gm_b16_8 t770 = gm_f_low_conds_1olabjo((void *)NULL, cx, NULL, t768, t769);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t767 + 0, t770);
        void * t771 = gm_l0;
        void * t772 = GM_LD(void *, (((uint8_t *)&gm_l86) + 16));
        void * t773 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t771, t772);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t767 + 16, t773);
        void * t774 = gm_l0;
        void * t775 = GM_LD(void *, (((uint8_t *)&gm_l86) + 24));
        void * t776 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t774, t775);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t767 + 24, t776);
        gm_tbo4aa19b457h t777;
        memset(&t777, 0, sizeof t777);
        GM_ST(uint8_t, ((uint8_t *)&t777 + 0), ((uint8_t)52ULL));
        GM_ST(gm_trl2khmuphncd, ((uint8_t *)&t777 + 8), t767);
        t5 = t777;
        goto me6;
mn764:;
ma48:;
        uint8_t t779 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t779 != ((uint8_t)48ULL)) goto mn778;
        gm_t710lsrq1m8gk t780 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t4) + 8) + 8));
        F.l87 = t780;
        gm_t710lsrq1m8gk t781;
        memset((uint8_t *)&t781 + 4, 0, 4);
        uint32_t t782 = GM_LD(uint32_t, (((uint8_t *)&F.l87) + 0));
        GM_ST(uint32_t, (uint8_t *)&t781 + 0, t782);
        void * t783 = gm_l0;
        gm_b16_8 t784 = GM_LD(gm_b16_8, (((uint8_t *)&F.l87) + 8));
        gm_b16_8 t785 = gm_f_low_conds_1olabjo((void *)NULL, cx, NULL, t783, t784);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t781 + 8, t785);
        void * t786 = gm_l0;
        void * t787 = GM_LD(void *, (((uint8_t *)&F.l87) + 24));
        void * t788 = gm_f_low_block_klh2c88((void *)NULL, cx, NULL, t786, t787);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t781 + 24, t788);
        void * t789 = gm_l0;
        void * t790 = GM_LD(void *, (((uint8_t *)&F.l87) + 32));
        void * t791 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t789, t790);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t781 + 32, t791);
        gm_tbo4aa19b457h t792;
        memset(&t792, 0, sizeof t792);
        GM_ST(uint8_t, ((uint8_t *)&t792 + 0), ((uint8_t)53ULL));
        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t792 + 8), t781);
        t5 = t792;
        goto me6;
mn778:;
ma49:;
        uint8_t t794 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t794 != ((uint8_t)49ULL)) goto mn793;
        gm_teo9d4mncjfdu t795 = GM_LD(gm_teo9d4mncjfdu, ((((uint8_t *)t4) + 8) + 8));
        gm_l88 = t795;
        gm_teo9d4mncjfdu t796;
        memset((uint8_t *)&t796 + 12, 0, 4);
        gm_te9hrk0u35rev t797 = GM_LD(gm_te9hrk0u35rev, (((uint8_t *)&gm_l88) + 0));
        GM_ST(gm_te9hrk0u35rev, (uint8_t *)&t796 + 0, t797);
        void * t798 = gm_l0;
        void * t799 = GM_LD(void *, (((uint8_t *)&gm_l88) + 16));
        void * t800 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t798, t799);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t796 + 16, t800);
        gm_tbo4aa19b457h t801;
        memset(&t801, 0, sizeof t801);
        GM_ST(uint8_t, ((uint8_t *)&t801 + 0), ((uint8_t)54ULL));
        GM_ST(gm_teo9d4mncjfdu, ((uint8_t *)&t801 + 8), t796);
        t5 = t801;
        goto me6;
mn793:;
ma50:;
        uint8_t t803 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t803 != ((uint8_t)50ULL)) goto mn802;
        gm_te9hrk0u35rev t804 = GM_LD(gm_te9hrk0u35rev, ((((uint8_t *)t4) + 8) + 8));
        gm_l89 = t804;
        gm_te9hrk0u35rev t805 = gm_l89;
        gm_tbo4aa19b457h t806;
        memset(&t806, 0, sizeof t806);
        GM_ST(uint8_t, ((uint8_t *)&t806 + 0), ((uint8_t)55ULL));
        GM_ST(gm_te9hrk0u35rev, ((uint8_t *)&t806 + 8), t805);
        t5 = t806;
        goto me6;
mn802:;
ma51:;
        uint8_t t808 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t808 != ((uint8_t)51ULL)) goto mn807;
        uint32_t t809 = GM_LD(uint32_t, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l90 = t809;
        void * t810 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l91 = t810;
        uint32_t t811 = gm_l90;
        void * t812 = gm_l0;
        void * t813 = gm_l91;
        void * t814 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t812, t813);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttggq7ng4kp3p t815;
        GM_ST(uint32_t, (uint8_t *)&t815 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t815 + 0), t811);
        GM_ST(void *, ((uint8_t *)&t815 + 8), t814);
        gm_tbo4aa19b457h t816;
        memset(&t816, 0, sizeof t816);
        GM_ST(uint8_t, ((uint8_t *)&t816 + 0), ((uint8_t)56ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t816 + 8), t815);
        t5 = t816;
        goto me6;
mn807:;
ma52:;
        uint8_t t818 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t818 != ((uint8_t)52ULL)) goto mn817;
        void * t819 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l92 = t819;
        void * t820 = gm_l0;
        void * t821 = gm_l92;
        void * t822 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t820, t821);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t823;
        memset(&t823, 0, sizeof t823);
        GM_ST(uint8_t, ((uint8_t *)&t823 + 0), ((uint8_t)57ULL));
        GM_ST(void *, ((uint8_t *)&t823 + 8), t822);
        t5 = t823;
        goto me6;
mn817:;
ma53:;
        uint8_t t825 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t825 != ((uint8_t)53ULL)) goto mn824;
        void * t826 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 0));
        gm_l93 = t826;
        void * t827 = GM_LD(void *, (((((uint8_t *)t4) + 8) + 8) + 8));
        gm_l94 = t827;
        void * t828 = gm_l0;
        void * t829 = gm_l93;
        void * t830 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t828, t829);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t831 = gm_l0;
        void * t832 = gm_l94;
        void * t833 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t831, t832);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t3mf58gv9kf3n t834;
        GM_ST(void *, ((uint8_t *)&t834 + 0), t830);
        GM_ST(void *, ((uint8_t *)&t834 + 8), t833);
        gm_tbo4aa19b457h t835;
        memset(&t835, 0, sizeof t835);
        GM_ST(uint8_t, ((uint8_t *)&t835 + 0), ((uint8_t)58ULL));
        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t835 + 8), t834);
        t5 = t835;
        goto me6;
mn824:;
ma54:;
        uint8_t t837 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t837 != ((uint8_t)54ULL)) goto mn836;
        void * t838 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l95 = t838;
        void * t839 = gm_l0;
        void * t840 = gm_l95;
        void * t841 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t839, t840);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbo4aa19b457h t842;
        memset(&t842, 0, sizeof t842);
        GM_ST(uint8_t, ((uint8_t *)&t842 + 0), ((uint8_t)59ULL));
        GM_ST(void *, ((uint8_t *)&t842 + 8), t841);
        t5 = t842;
        goto me6;
mn836:;
ma55:;
        uint8_t t844 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t844 != ((uint8_t)55ULL)) goto mn843;
        void * t845 = GM_LD(void *, ((((uint8_t *)t4) + 8) + 8));
        gm_l96 = t845;
        {
            void * t847 = gm_l96;
            gm_th1g69d7vpipg t848;
            uint8_t t851 = GM_LD(uint8_t, ((((uint8_t *)t847) + 0) + 0));
            if (t851 != ((uint8_t)0ULL)) goto mn850;
            gm_tiilmfd16v3fc t852 = GM_LD(gm_tiilmfd16v3fc, (((((uint8_t *)t847) + 0) + 8) + 0));
            gm_l97 = t852;
            uint32_t t853 = GM_LD(uint32_t, (((((uint8_t *)t847) + 0) + 8) + 24));
            gm_l98 = t853;
            gm_t419mo45g7j0a t854;
            memset((uint8_t *)&t854 + 28, 0, 4);
            memcpy((uint8_t *)&t854 + 0, ((uint8_t *)&gm_l97), 24);
            uint32_t t855 = gm_l98;
            GM_ST(uint32_t, (uint8_t *)&t854 + 24, t855);
            gm_th1g69d7vpipg t856;
            GM_ST(uint8_t, (uint8_t *)&t856 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t856 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t856 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t856 + 0), ((uint8_t)0ULL));
            GM_ST(gm_t419mo45g7j0a, ((uint8_t *)&t856 + 8), t854);
            t848 = t856;
            goto me849;
mn850:;
            uint8_t t858 = GM_LD(uint8_t, ((((uint8_t *)t847) + 0) + 0));
            if (t858 != ((uint8_t)1ULL)) goto mn857;
            void * t859 = GM_LD(void *, ((((uint8_t *)t847) + 0) + 8));
            gm_l99 = t859;
            void * t860 = gm_l0;
            void * t861 = gm_l99;
            void * t862 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t860, t861);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th1g69d7vpipg t863;
            GM_ST(uint8_t, (uint8_t *)&t863 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t863 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t863 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t863 + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&t863 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t863 + 32, 0);
            GM_ST(uint8_t, ((uint8_t *)&t863 + 0), ((uint8_t)1ULL));
            GM_ST(void *, ((uint8_t *)&t863 + 8), t862);
            t848 = t863;
            goto me849;
mn857:;
            gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 379, 26);
me849:;
            F.l100 = t848;
        }
        gm_tntn2tf15puib t864;
        memcpy((uint8_t *)&t864 + 0, ((uint8_t *)&F.l100), 40);
        void * t865 = gm_l96;
        gm_b16_8 t866 = GM_LD(gm_b16_8, (((uint8_t *)t865) + 40));
        GM_ST(gm_b16_8, (uint8_t *)&t864 + 40, t866);
        void * t867 = gm_l96;
        gm_b16_8 t868 = GM_LD(gm_b16_8, (((uint8_t *)t867) + 56));
        GM_ST(gm_b16_8, (uint8_t *)&t864 + 56, t868);
        void * t869 = gm_l0;
        void * t870 = gm_l96;
        gm_b16_8 t871 = GM_LD(gm_b16_8, (((uint8_t *)t870) + 72));
        gm_b16_8 t872 = gm_f_exprs_musunrl((void *)NULL, cx, NULL, t869, t871);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t864 + 72, t872);
        gm_tbo4aa19b457h t873;
        GM_ST(uint8_t, (uint8_t *)&t873 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t873 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t873 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t873 + 0), ((uint8_t)60ULL));
        GM_ST(gm_tntn2tf15puib, ((uint8_t *)&t873 + 8), t864);
        gm_tbo4aa19b457h t874 = t873;
        t5 = t874;
        goto me6;
mn843:;
ma56:;
        uint8_t t876 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t876 != ((uint8_t)58ULL)) goto mn875;
        gm_t710lsrq1m8gk t877 = GM_LD(gm_t710lsrq1m8gk, ((((uint8_t *)t4) + 8) + 8));
        F.l101 = t877;
        {
            gm_f_list_kualkeq((void *)&F.l102, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t880 = GM_LD(gm_b16_8, (((uint8_t *)&F.l101) + 8));
                gm_l103 = t880;
            }
            {
                gm_l104 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t882 = gm_l104;
                    uint64_t t883 = GM_LD(uintptr_t, ((uint8_t *)&gm_l103) + 8);
                    uint8_t t884 = (t882 < t883);
                    if (!t884) goto lb881;
                    {
                        uint64_t t887 = gm_l104;
                        uint64_t t888 = (uintptr_t)t887;
                        uint64_t t889 = GM_LD(uintptr_t, ((uint8_t *)&gm_l103) + 8);
                        gm_t4lnf71mruqpt t892 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l103)) + t888 * 32));
                        gm_l105 = t892;
                        {
                            gm_t4lnf71mruqpt t894;
                            uint8_t t897 = GM_LD(uint8_t, (((uint8_t *)&gm_l105) + 0));
                            if (t897 != ((uint8_t)0ULL)) goto mn896;
                            void * t898 = GM_LD(void *, (((uint8_t *)&gm_l105) + 8));
                            gm_l106 = t898;
                            void * t899 = gm_l0;
                            void * t900 = gm_l106;
                            void * t901 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t899, t900);
                            if (gm_unw.kind) {
                                goto gm_ll885;
                            }
                            gm_t4lnf71mruqpt t902;
                            GM_ST(uint8_t, (uint8_t *)&t902 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t902 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t902 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t902 + 16, 0);
                            GM_ST(uint64_t, (uint8_t *)&t902 + 24, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t902 + 0), ((uint8_t)0ULL));
                            GM_ST(void *, ((uint8_t *)&t902 + 8), t901);
                            t894 = t902;
                            goto me895;
mn896:;
                            uint8_t t904 = GM_LD(uint8_t, (((uint8_t *)&gm_l105) + 0));
                            if (t904 != ((uint8_t)1ULL)) goto mn903;
                            gm_b16_8 t905 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l105) + 8) + 0));
                            gm_l107 = t905;
                            void * t906 = GM_LD(void *, ((((uint8_t *)&gm_l105) + 8) + 16));
                            gm_l108 = t906;
                            gm_tpe1hv3fekn05 t907;
                            gm_b16_8 t908 = gm_l107;
                            GM_ST(gm_b16_8, (uint8_t *)&t907 + 0, t908);
                            void * t909 = gm_l0;
                            void * t910 = gm_l108;
                            void * t911 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t909, t910);
                            if (gm_unw.kind) {
                                goto gm_ll885;
                            }
                            GM_ST(void *, (uint8_t *)&t907 + 16, t911);
                            gm_t4lnf71mruqpt t912;
                            GM_ST(uint8_t, (uint8_t *)&t912 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t912 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t912 + 4, 0);
                            GM_ST(uint8_t, ((uint8_t *)&t912 + 0), ((uint8_t)1ULL));
                            GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t912 + 8), t907);
                            t894 = t912;
                            goto me895;
mn903:;
                            gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 388, 31);
me895:;
                            gm_l109 = t894;
                        }
                        {
                            void * t913 = (void *)((uint8_t *)&F.l102);
                            gm_tftb1odc739t5 t914 = gm_f_push_a0ftqhm(t913, &(*(gm_t4lnf71mruqpt *)((uint8_t *)&gm_l109)));
                            if (((((uint8_t *)&t914))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t914) + 1, gm_src7afujsggtlje, 392, 17);
                            gm_b0_1 t915 = GM_LD(gm_b0_1, (((uint8_t *)&t914) + 1));
                        }
                        goto me890;
mn891:;
                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 387, 13);
me890:;
                    }
lc881:;
                    {
                        uint64_t t917 = gm_l104;
                        uint64_t r918; bool o919 = __builtin_add_overflow(t917, ((uint64_t)1ULL), &r918);
                        uint64_t t920 = r918;
                        gm_l104 = t920;
                    }
                }
gm_ll885:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb881; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc881; }
                }
                goto gm_lad;
lb881:;
            }
        }
        gm_t710lsrq1m8gk t921;
        memset((uint8_t *)&t921 + 4, 0, 4);
        uint32_t t922 = GM_LD(uint32_t, (((uint8_t *)&F.l101) + 0));
        GM_ST(uint32_t, (uint8_t *)&t921 + 0, t922);
        gm_b16_8 t923 = GM_LD(gm_b16_8, (((uint8_t *)&F.l102) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t921 + 8, t923);
        void * t924 = gm_l0;
        void * t925 = GM_LD(void *, (((uint8_t *)&F.l101) + 24));
        void * t926 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t924, t925);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t921 + 24, t926);
        void * t927 = gm_l0;
        void * t928 = GM_LD(void *, (((uint8_t *)&F.l101) + 32));
        void * t929 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t927, t928);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t921 + 32, t929);
        gm_tbo4aa19b457h t930;
        memset(&t930, 0, sizeof t930);
        GM_ST(uint8_t, ((uint8_t *)&t930 + 0), ((uint8_t)61ULL));
        GM_ST(gm_t710lsrq1m8gk, ((uint8_t *)&t930 + 8), t921);
        gm_tbo4aa19b457h t931 = t930;
        t5 = t931;
        goto me6;
mn875:;
ma57:;
        uint8_t t935 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t935 != ((uint8_t)10ULL)) goto pa934;
        goto po933;
pa934:;
        uint8_t t937 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t937 != ((uint8_t)11ULL)) goto pa936;
        goto po933;
pa936:;
        uint8_t t939 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t939 != ((uint8_t)12ULL)) goto pa938;
        goto po933;
pa938:;
        uint8_t t941 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t941 != ((uint8_t)13ULL)) goto pa940;
        goto po933;
pa940:;
        uint8_t t943 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t943 != ((uint8_t)2ULL)) goto pa942;
        goto po933;
pa942:;
        uint8_t t945 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t945 != ((uint8_t)3ULL)) goto pa944;
        goto po933;
pa944:;
        uint8_t t947 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t947 != ((uint8_t)9ULL)) goto pa946;
        goto po933;
pa946:;
        uint8_t t949 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t949 != ((uint8_t)56ULL)) goto pa948;
        goto po933;
pa948:;
        uint8_t t951 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t951 != ((uint8_t)57ULL)) goto pa950;
        goto po933;
pa950:;
        goto mn932;
po933:;
        gm_tbo4aa19b457h t952;
        memset(&t952, 0, sizeof t952);
        GM_ST(uint8_t, ((uint8_t *)&t952 + 0), ((uint8_t)62ULL));
        t5 = t952;
        goto me6;
mn932:;
ma58:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 273, 14);
me6:;
        F.l110 = t5;
    }
    {
        void * t953 = gm_l0;
        void * t954 = gm_l1;
        void * t955 = gm_f_mk_node_ndavg23((void *)NULL, cx, NULL, t953, t954, &(*(gm_tbo4aa19b457h *)((uint8_t *)&F.l110)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t956 = 0;
        F.ret = t955;
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
    GM_STACK_CHECK(&F, 212u);
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
                    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&t10));
                    gm_l5 = t13;
                    {
                        void * t15 = (void *)((uint8_t *)&F.l2);
                        void * t16 = gm_l0;
                        uint64_t t17 = gm_l5;
                        uint64_t t18 = (uintptr_t)t17;
                        uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t18 >= (uintptr_t)(t19)) gm_trap_index(t18, t19, gm_src7afujsggtlje, 247, 44);
                        void * t20 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t18 * 88);
                        void * t21 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t16, t20);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tftb1odc739t5 t22 = gm_f_push_uehemid(t15, t21);
                        if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_src7afujsggtlje, 247, 26);
                        gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 247, 5);
me11:;
                }
lc4:;
                {
                    uint64_t t25 = gm_l4;
                    uint64_t r26; bool o27 = __builtin_add_overflow(t25, ((uint64_t)1ULL), &r26);
                    uint64_t t28 = r26;
                    gm_l4 = t28;
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
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        uint8_t t30 = 0;
        F.ret = t29;
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
    GM_STACK_CHECK(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_tg5lql6itufk7 t4 = gm_f_int_info_ulcevdv(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_ttovhfee8mf28 t5;
        if (((((uint8_t *)&t4))[8] != 0)) {
            gm_ttovhfee8mf28 t6 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_tg5lql6itufk7 t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        uint8_t t10 = 0;
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        gm_thpdbp3q7rdrc t12;
        GM_ST(uint32_t, ((uint8_t *)&t12 + 0), ((uint32_t)8ULL));
        GM_ST(uint32_t, ((uint8_t *)&t12 + 4), ((uint32_t)16ULL));
        GM_ST(uint32_t, ((uint8_t *)&t12 + 8), ((uint32_t)32ULL));
        GM_ST(uint32_t, ((uint8_t *)&t12 + 12), ((uint32_t)64ULL));
        uint8_t t13 = 0;
        if (gm_eq467d5cm1d5(((uint8_t *)&t12) + 0, ((uint8_t *)&t11))) t13 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t12) + 4, ((uint8_t *)&t11))) t13 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t12) + 8, ((uint8_t *)&t11))) t13 = 1;
        if (gm_eq467d5cm1d5(((uint8_t *)&t12) + 12, ((uint8_t *)&t11))) t13 = 1;
        uint8_t t14 = (uint8_t)(!t13);
        if (!t14) {
            uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            uint64_t t16 = (uint64_t)t15;
            void * t17 = gm_l0;
            gm_t88rcucg1b7uk t18 = gm_l1;
            uint64_t t19 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint64_t r20; bool o21 = __builtin_mul_overflow(((uint64_t)8ULL), t19, &r20);
            if (o21) gm_trap("integer overflow in `*`", gm_src7afujsggtlje, 222, 66);
            uint64_t t22 = r20;
            uint8_t t23 = (!(t16 == t22));
            t10 = t23;
        } else t10 = 1;
        if (!t10) goto ie9;
        {
            gm_tg5lql6itufk7 t25;
            memset(&t25, 0, sizeof t25);
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        gm_ttovhfee8mf28 t29;
        GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
        GM_ST(uint8_t, ((uint8_t *)&t29 + 0), t27);
        GM_ST(uint32_t, ((uint8_t *)&t29 + 4), t28);
        gm_tg5lql6itufk7 t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 10, 0);
        GM_ST(gm_ttovhfee8mf28, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[8] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
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
    GM_STACK_CHECK(&F, 701u);
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
            if (!((((uint8_t *)&t18))[8] != 0)) goto ie14;
            gm_tmn7n0n9raou5 t19 = GM_LD(gm_tmn7n0n9raou5, ((uint8_t *)&t18));
            gm_l8 = t19;
            uint8_t t20 = (!(GM_LD(uint8_t, ((uint8_t *)&gm_l8) + 0) == ((uint8_t)4ULL)));
            if (!t20) goto ie14;
            {
                gm_tkm39klvnn4cr t22;
                memset((uint8_t *)&t22 + 2, 0, 2);
                memset((uint8_t *)&t22 + 12, 0, 4);
                gm_ts6oqppjh0ba3 t23 = gm_l1;
                GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t22 + 0, t23);
                gm_tmn7n0n9raou5 t24 = gm_l8;
                GM_ST(gm_tmn7n0n9raou5, (uint8_t *)&t22 + 4, t24);
                void * t25 = gm_l6;
                GM_ST(void *, (uint8_t *)&t22 + 16, t25);
                void * t26 = gm_l7;
                GM_ST(void *, (uint8_t *)&t22 + 24, t26);
                memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)23ULL));
                GM_ST(gm_tkm39klvnn4cr, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t22);
                goto gm_fdone;
            }
            goto ix14;
ie14:;
ix14:;
        }
        {
            gm_trl9d7m1k5lbd t27;
            memset((uint8_t *)&t27 + 2, 0, 6);
            gm_ts6oqppjh0ba3 t28 = gm_l1;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t27 + 0, t28);
            void * t29 = gm_l6;
            GM_ST(void *, (uint8_t *)&t27 + 8, t29);
            void * t30 = gm_l7;
            GM_ST(void *, (uint8_t *)&t27 + 16, t30);
            memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)31ULL));
            GM_ST(gm_trl9d7m1k5lbd, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t27);
            goto gm_fdone;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        void * t32 = gm_l5;
        void * t33 = gm_l2;
        gm_t88rcucg1b7uk t34 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t33) + 0));
        gm_tg5lql6itufk7 t35 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t32, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t35))[8] != 0)) goto ie31;
        gm_ttovhfee8mf28 t36 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t35));
        gm_l9 = t36;
        {
            uint8_t t38;
            uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t43 != ((uint8_t)0ULL)) goto pa42;
            gm_t9qkh0rsooc8d t44 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
            gm_l10 = t44;
            goto po41;
pa42:;
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t46 != ((uint8_t)1ULL)) goto pa45;
            gm_t9qkh0rsooc8d t47 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
            gm_l10 = t47;
            goto po41;
pa45:;
            uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t49 != ((uint8_t)2ULL)) goto pa48;
            gm_t9qkh0rsooc8d t50 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
            gm_l10 = t50;
            goto po41;
pa48:;
            uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t52 != ((uint8_t)3ULL)) goto pa51;
            gm_t9qkh0rsooc8d t53 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
            gm_l10 = t53;
            goto po41;
pa51:;
            goto mn40;
po41:;
            uint8_t t54 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l10) + 0) == ((uint8_t)3ULL)));
            t38 = t54;
            goto me39;
mn40:;
            t38 = ((uint8_t)0);
            goto me39;
mn55:;
            gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 430, 16);
me39:;
            gm_l14 = t38;
        }
        {
            uint8_t t57 = gm_l14;
            uint8_t t58 = (uint8_t)!t57;
            if (!t58) goto ie56;
            {
                gm_ttovhfee8mf28 t60 = gm_l9;
                gm_l15 = t60;
            }
            {
                gm_ts6oqppjh0ba3 t62 = gm_l1;
                uint8_t t63 = gm_f_is_shift_svl330f(t62);
                if (!t63) goto ie61;
                {
                    void * t65 = gm_l5;
                    void * t66 = gm_l3;
                    gm_t88rcucg1b7uk t67 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t66) + 0));
                    gm_tg5lql6itufk7 t68 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t65, t67);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttovhfee8mf28 t69;
                    if (((((uint8_t *)&t68))[8] != 0)) {
                        gm_ttovhfee8mf28 t70 = GM_LD(gm_ttovhfee8mf28, (((uint8_t *)&t68) + 0));
                        t69 = t70;
                    } else {
                        gm_trl9d7m1k5lbd t71;
                        memset((uint8_t *)&t71 + 2, 0, 6);
                        gm_ts6oqppjh0ba3 t72 = gm_l1;
                        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t71 + 0, t72);
                        void * t73 = gm_l6;
                        GM_ST(void *, (uint8_t *)&t71 + 8, t73);
                        void * t74 = gm_l7;
                        GM_ST(void *, (uint8_t *)&t71 + 16, t74);
                        memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
                        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)31ULL));
                        GM_ST(gm_trl9d7m1k5lbd, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t71);
                        goto gm_fdone;
                    }
                    gm_l15 = t69;
                }
                goto ix61;
ie61:;
ix61:;
            }
            {
                gm_t0pc2c7dafsjt t75;
                memset((uint8_t *)&t75 + 2, 0, 2);
                memset((uint8_t *)&t75 + 20, 0, 4);
                gm_ts6oqppjh0ba3 t76 = gm_l1;
                GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t75 + 0, t76);
                gm_ttovhfee8mf28 t77 = gm_l9;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t75 + 4, t77);
                gm_ttovhfee8mf28 t78 = gm_l15;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t75 + 12, t78);
                void * t79 = gm_l6;
                GM_ST(void *, (uint8_t *)&t75 + 24, t79);
                void * t80 = gm_l7;
                GM_ST(void *, (uint8_t *)&t75 + 32, t80);
                memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)21ULL));
                GM_ST(gm_t0pc2c7dafsjt, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t75);
                goto gm_fdone;
            }
            goto ix56;
ie56:;
ix56:;
        }
        goto ix31;
ie31:;
ix31:;
    }
    {
        void * t82 = gm_l5;
        void * t83 = gm_l2;
        gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t83) + 0));
        gm_t53at80pfdus3 t85;
        gm_f_kind_htv6093((void *)&t85, t82, GM_CONV(gm_csguankko4lmhd, t84));
        uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&t85) + 0));
        if (t86 != ((uint8_t)3ULL)) goto ie81;
        uint16_t t87 = GM_LD(uint16_t, (((uint8_t *)&t85) + 8));
        gm_l16 = t87;
        uint16_t t88 = gm_l16;
        gm_tuqvtbj0p3v9c t89;
        GM_ST(uint16_t, ((uint8_t *)&t89 + 0), ((uint16_t)32ULL));
        GM_ST(uint16_t, ((uint8_t *)&t89 + 2), ((uint16_t)64ULL));
        uint8_t t90 = 0;
        if (gm_eq9oqmmiu8qc(((uint8_t *)&t89) + 0, ((uint8_t *)&t88))) t90 = 1;
        if (gm_eq9oqmmiu8qc(((uint8_t *)&t89) + 2, ((uint8_t *)&t88))) t90 = 1;
        uint8_t t91 = (uint8_t)(t90);
        if (!t91) goto ie81;
        gm_ts6oqppjh0ba3 t92 = gm_l1;
        uint8_t t93 = gm_f_is_float_op_6co8naj(t92);
        if (!t93) goto ie81;
        {
            gm_tuggr5sml9o52 t95;
            memset((uint8_t *)&t95 + 3, 0, 5);
            gm_ts6oqppjh0ba3 t96 = gm_l1;
            GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t95 + 0, t96);
            uint16_t t97 = gm_l16;
            uint8_t t98 = ((t97 == ((uint16_t)64ULL)));
            GM_ST(uint8_t, (uint8_t *)&t95 + 2, t98);
            void * t99 = gm_l6;
            GM_ST(void *, (uint8_t *)&t95 + 8, t99);
            void * t100 = gm_l7;
            GM_ST(void *, (uint8_t *)&t95 + 16, t100);
            memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)22ULL));
            GM_ST(gm_tuggr5sml9o52, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t95);
            goto gm_fdone;
        }
        goto ix81;
ie81:;
ix81:;
    }
    {
        gm_trl9d7m1k5lbd t101;
        memset((uint8_t *)&t101 + 2, 0, 6);
        gm_ts6oqppjh0ba3 t102 = gm_l1;
        GM_ST(gm_ts6oqppjh0ba3, (uint8_t *)&t101 + 0, t102);
        void * t103 = gm_l6;
        GM_ST(void *, (uint8_t *)&t101 + 8, t103);
        void * t104 = gm_l7;
        GM_ST(void *, (uint8_t *)&t101 + 16, t104);
        memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)31ULL));
        GM_ST(gm_trl9d7m1k5lbd, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t101);
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
    GM_STACK_CHECK(&F, 1780u);
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
                        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&t39) + 8);
                        if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_src7afujsggtlje, 484, 39);
                        uint64_t t45 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t39)) + t43 * 8));
                        GM_ST(uint64_t, (uint8_t *)&t34 + 8, t45);
                        void * t46 = gm_l0;
                        void * t47 = GM_LD(void *, (((uint8_t *)t46) + 8));
                        gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)t47) + 88));
                        gm_b16_8 t49 = t48;
                        uint32_t t50 = gm_l8;
                        uint64_t t51 = (uint64_t)t50;
                        uint64_t t52 = gm_f_us_cuv01fp(t51);
                        uint64_t t53 = (uintptr_t)t52;
                        uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&t49) + 8);
                        if (t53 >= (uintptr_t)(t54)) gm_trap_index(t53, t54, gm_src7afujsggtlje, 484, 63);
                        uint64_t t55 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t49)) + t53 * 8));
                        GM_ST(uint64_t, (uint8_t *)&t34 + 16, t55);
                        void * t56 = gm_l0;
                        void * t57 = gm_l9;
                        void * t58 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t56, t57);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        GM_ST(void *, (uint8_t *)&t34 + 24, t58);
                        gm_t2k9dbsv5ff58 t59;
                        GM_ST(uint8_t, (uint8_t *)&t59 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t59 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t59 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_tg0sdqqpruvqq, ((uint8_t *)&t59 + 8), t34);
                        t20 = t59;
                        goto me21;
mn30:;
ma23:;
                        uint8_t t61 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t61 != ((uint8_t)1ULL)) goto mn60;
                        gm_b16_8 t62 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l7) + 8) + 0));
                        gm_l10 = t62;
                        void * t63 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 16));
                        gm_l11 = t63;
                        gm_tpe1hv3fekn05 t64;
                        gm_b16_8 t65 = gm_l10;
                        GM_ST(gm_b16_8, (uint8_t *)&t64 + 0, t65);
                        void * t66 = gm_l0;
                        void * t67 = gm_l11;
                        void * t68 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t66, t67);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        GM_ST(void *, (uint8_t *)&t64 + 16, t68);
                        gm_t2k9dbsv5ff58 t69;
                        GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t69 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)1ULL));
                        GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t69 + 8), t64);
                        t20 = t69;
                        goto me21;
mn60:;
ma24:;
                        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t71 != ((uint8_t)2ULL)) goto mn70;
                        void * t72 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 0));
                        gm_l12 = t72;
                        void * t73 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 8));
                        gm_l13 = t73;
                        gm_t2k9dbsv5ff58 t75;
                        void * t77 = gm_l12;
                        uint8_t t78 = GM_LD(uint8_t, ((((uint8_t *)t77) + 8) + 0));
                        if (t78 != ((uint8_t)15ULL)) goto ie76;
                        uint32_t t79 = GM_LD(uint32_t, ((((uint8_t *)t77) + 8) + 8));
                        gm_l14 = t79;
                        uint32_t t81 = gm_l14;
                        void * t82 = gm_l0;
                        void * t83 = gm_l13;
                        void * t84 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t82, t83);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        gm_ttggq7ng4kp3p t85;
                        GM_ST(uint32_t, (uint8_t *)&t85 + 4, 0);
                        GM_ST(uint32_t, ((uint8_t *)&t85 + 0), t81);
                        GM_ST(void *, ((uint8_t *)&t85 + 8), t84);
                        gm_t2k9dbsv5ff58 t86;
                        GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t86 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t86 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)2ULL));
                        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t86 + 8), t85);
                        gm_t2k9dbsv5ff58 t87 = t86;
                        t75 = t87;
                        goto ix76;
ie76:;
                        void * t89 = gm_l0;
                        void * t90 = gm_l12;
                        void * t91 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t89, t90);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        void * t92 = gm_l0;
                        void * t93 = gm_l13;
                        void * t94 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t92, t93);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        gm_t3mf58gv9kf3n t95;
                        GM_ST(void *, ((uint8_t *)&t95 + 0), t91);
                        GM_ST(void *, ((uint8_t *)&t95 + 8), t94);
                        gm_t2k9dbsv5ff58 t96;
                        GM_ST(uint8_t, (uint8_t *)&t96 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t96 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t96 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t96 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t96 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t96 + 0), ((uint8_t)3ULL));
                        GM_ST(gm_t3mf58gv9kf3n, ((uint8_t *)&t96 + 8), t95);
                        gm_t2k9dbsv5ff58 t97 = t96;
                        t75 = t97;
ix76:;
                        gm_t2k9dbsv5ff58 t98 = t75;
                        t20 = t98;
                        goto me21;
mn70:;
ma25:;
                        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t100 != ((uint8_t)3ULL)) goto mn99;
                        gm_b16_8 t101 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l7) + 8) + 0));
                        gm_l15 = t101;
                        void * t102 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 16));
                        gm_l16 = t102;
                        {
                            gm_f_list_mlkakgl((void *)&F.l17, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll11;
                            }
                        }
                        {
                            {
                                gm_b16_8 t105 = gm_l15;
                                gm_l18 = t105;
                            }
                            {
                                gm_l19 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t107 = gm_l19;
                                    uint64_t t108 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                    uint8_t t109 = (t107 < t108);
                                    if (!t109) goto lb106;
                                    {
                                        uint64_t t112 = gm_l19;
                                        uint64_t t113 = (uintptr_t)t112;
                                        uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                                        void * t115 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t113 * 8));
                                        void * t118 = GM_LD(void *, ((uint8_t *)&t115));
                                        gm_l20 = t118;
                                        {
                                            void * t120 = (void *)((uint8_t *)&F.l17);
                                            void * t121 = gm_l0;
                                            void * t122 = gm_l20;
                                            void * t123 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t121, t122);
                                            if (gm_unw.kind) {
                                                goto gm_ll110;
                                            }
                                            gm_tftb1odc739t5 t124 = gm_f_push_h04leh4(t120, t123);
                                            if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 1, gm_src7afujsggtlje, 491, 35);
                                            gm_b0_1 t125 = GM_LD(gm_b0_1, (((uint8_t *)&t124) + 1));
                                        }
                                        goto me116;
mn117:;
                                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 491, 17);
me116:;
                                    }
lc106:;
                                    {
                                        uint64_t t127 = gm_l19;
                                        uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                                        uint64_t t130 = r128;
                                        gm_l19 = t130;
                                    }
                                }
gm_ll110:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb106; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc106; }
                                }
                                goto gm_ll11;
lb106:;
                            }
                        }
                        gm_tpe1hv3fekn05 t131;
                        gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&t131 + 0, t132);
                        void * t133 = gm_l0;
                        void * t134 = gm_l16;
                        void * t135 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t133, t134);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        GM_ST(void *, (uint8_t *)&t131 + 16, t135);
                        gm_t2k9dbsv5ff58 t136;
                        GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t136 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)4ULL));
                        GM_ST(gm_tpe1hv3fekn05, ((uint8_t *)&t136 + 8), t131);
                        gm_t2k9dbsv5ff58 t137 = t136;
                        t20 = t137;
                        goto me21;
mn99:;
ma26:;
                        uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t139 != ((uint8_t)4ULL)) goto mn138;
                        void * t140 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                        gm_l21 = t140;
                        void * t141 = gm_l0;
                        void * t142 = gm_l21;
                        void * t143 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t141, t142);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        gm_t2k9dbsv5ff58 t144;
                        GM_ST(uint8_t, (uint8_t *)&t144 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t144 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t144 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t144 + 16, 0);
                        GM_ST(uint64_t, (uint8_t *)&t144 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t144 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t144 + 0), ((uint8_t)5ULL));
                        GM_ST(void *, ((uint8_t *)&t144 + 8), t143);
                        t20 = t144;
                        goto me21;
mn138:;
ma27:;
                        uint8_t t146 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
                        if (t146 != ((uint8_t)5ULL)) goto mn145;
                        uint8_t t147 = GM_LD(uint8_t, ((((uint8_t *)&gm_l7) + 8) + 0));
                        gm_l22 = t147;
                        void * t148 = GM_LD(void *, ((((uint8_t *)&gm_l7) + 8) + 8));
                        gm_l23 = t148;
                        {
                            gm_l4 = ((uint8_t)1);
                        }
                        uint8_t t150 = gm_l22;
                        void * t151 = gm_l0;
                        void * t152 = gm_l23;
                        void * t153 = gm_f_expr_a84t331((void *)NULL, cx, NULL, t151, t152);
                        if (gm_unw.kind) {
                            goto gm_ll11;
                        }
                        gm_tgtub0v94d5u5 t154;
                        GM_ST(uint8_t, (uint8_t *)&t154 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t154 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t154 + 4, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t154 + 0), t150);
                        GM_ST(void *, ((uint8_t *)&t154 + 8), t153);
                        gm_t2k9dbsv5ff58 t155;
                        GM_ST(uint8_t, (uint8_t *)&t155 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t155 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t155 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t155 + 24, 0);
                        GM_ST(uint64_t, (uint8_t *)&t155 + 32, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t155 + 0), ((uint8_t)6ULL));
                        GM_ST(gm_tgtub0v94d5u5, ((uint8_t *)&t155 + 8), t154);
                        gm_t2k9dbsv5ff58 t156 = t155;
                        t20 = t156;
                        goto me21;
mn145:;
ma28:;
                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 483, 17);
me21:;
                        F.l24 = t20;
                    }
                    {
                        void * t157 = (void *)((uint8_t *)&F.l3);
                        gm_tftb1odc739t5 t158 = gm_f_push_esf6o94(t157, &(*(gm_t2k9dbsv5ff58 *)((uint8_t *)&F.l24)));
                        if (((((uint8_t *)&t158))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t158) + 1, gm_src7afujsggtlje, 500, 9);
                        gm_b0_1 t159 = GM_LD(gm_b0_1, (((uint8_t *)&t158) + 1));
                    }
                    goto me16;
mn17:;
                    gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 482, 5);
me16:;
                }
lc7:;
                {
                    uint64_t t161 = gm_l6;
                    uint64_t r162; bool o163 = __builtin_add_overflow(t161, ((uint64_t)1ULL), &r162);
                    uint64_t t164 = r162;
                    gm_l6 = t164;
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
        gm_t4n3ujve5elie t165;
        memset((uint8_t *)&t165 + 25, 0, 7);
        gm_b16_8 t166 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t165 + 0, t166);
        void * t167 = gm_l0;
        void * t168 = gm_l1;
        void * t169 = GM_LD(void *, (((uint8_t *)t168) + 16));
        void * t170 = gm_f_opt_expr_gr6e3ks((void *)NULL, cx, NULL, t167, t169);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t165 + 16, t170);
        uint8_t t171 = gm_l4;
        GM_ST(uint8_t, (uint8_t *)&t165 + 24, t171);
        gm_tq7s98ihonkju t172 = gm_f_new_jg8stkn((void *)NULL, cx, NULL, &t165, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k8o4k1d1for)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t172))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t172) + 8, gm_src7afujsggtlje, 502, 12);
        void * t173 = GM_LD(void *, (((uint8_t *)&t172) + 8));
        uint8_t t174 = 0;
        F.ret = t173;
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
    GM_STACK_CHECK(&F, 985u);
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
        uint8_t t13 = 0;
        if (gm_eqg63o53ar52(((uint8_t *)&t9) + 0, ((uint8_t *)&t8))) t13 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t9) + 12, ((uint8_t *)&t8))) t13 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t9) + 24, ((uint8_t *)&t8))) t13 = 1;
        uint8_t t14 = (uint8_t)(t13);
        if (!t14) goto ie7;
        uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t16 = ((t15 == ((uint64_t)1ULL)));
        if (!t16) goto ie7;
        {
            void * t19 = gm_l4;
            uint64_t t20 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t20 >= (uintptr_t)(t21)) gm_trap_index(t20, t21, gm_src7afujsggtlje, 467, 29);
            gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t20 * 88) + 0));
            gm_tg5lql6itufk7 t23 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t19, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t23))[8] != 0)) goto ie18;
            gm_ttovhfee8mf28 t24 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t23));
            gm_l6 = t24;
            void * t25 = gm_l4;
            void * t26 = gm_l3;
            gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t26) + 0));
            gm_tg5lql6itufk7 t28 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t25, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t28))[8] != 0)) goto ie18;
            gm_ttovhfee8mf28 t29 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t28));
            gm_l7 = t29;
            {
                gm_tr8okf1iilonm t31;
                memset((uint8_t *)&t31 + 28, 0, 4);
                gm_tvdg94bk7aa22 t32 = gm_l1;
                GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t31 + 0, t32);
                gm_ttovhfee8mf28 t33 = gm_l6;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t31 + 12, t33);
                gm_ttovhfee8mf28 t34 = gm_l7;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t31 + 20, t34);
                uint64_t t35 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_src7afujsggtlje, 467, 98);
                void * t37 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t35 * 8));
                GM_ST(void *, (uint8_t *)&t31 + 32, t37);
                memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)27ULL));
                GM_ST(gm_tr8okf1iilonm, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t31);
                goto gm_fdone;
            }
            goto ix18;
ie18:;
ix18:;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_tvdg94bk7aa22 t39 = gm_l1;
        gm_tq9t4giin3pm0 t40;
        gm_tvdg94bk7aa22 t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t41 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t41 + 0), ((uint8_t)32ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t40 + 0, t41);
        gm_tvdg94bk7aa22 t42;
        GM_ST(uint8_t, (uint8_t *)&t42 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t42 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t42 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t42 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)33ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t40 + 12, t42);
        uint8_t t43 = 0;
        if (gm_eqg63o53ar52(((uint8_t *)&t40) + 0, ((uint8_t *)&t39))) t43 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t40) + 12, ((uint8_t *)&t39))) t43 = 1;
        uint8_t t44 = (uint8_t)(t43);
        if (!t44) goto ie38;
        uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t46 = ((t45 == ((uint64_t)2ULL)));
        if (!t46) goto ie38;
        {
            void * t49 = gm_l4;
            uint64_t t50 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_src7afujsggtlje, 470, 26);
            gm_t88rcucg1b7uk t52 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t50 * 88) + 0));
            gm_tg5lql6itufk7 t53 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t49, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t53))[8] != 0)) goto ie48;
            gm_ttovhfee8mf28 t54 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t53));
            gm_l8 = t54;
            {
                gm_tscalq56heofe t56;
                memset((uint8_t *)&t56 + 20, 0, 4);
                gm_tvdg94bk7aa22 t57 = gm_l1;
                GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t56 + 0, t57);
                gm_ttovhfee8mf28 t58 = gm_l8;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t56 + 12, t58);
                uint64_t t59 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                if (t59 >= (uintptr_t)(t60)) gm_trap_index(t59, t60, gm_src7afujsggtlje, 470, 62);
                void * t61 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t59 * 8));
                GM_ST(void *, (uint8_t *)&t56 + 24, t61);
                uint64_t t62 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                if (t62 >= (uintptr_t)(t63)) gm_trap_index(t62, t63, gm_src7afujsggtlje, 470, 69);
                void * t64 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t62 * 8));
                GM_ST(void *, (uint8_t *)&t56 + 32, t64);
                memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)28ULL));
                GM_ST(gm_tscalq56heofe, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t56);
                goto gm_fdone;
            }
            goto ix48;
ie48:;
ix48:;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        gm_tvdg94bk7aa22 t66 = gm_l1;
        gm_tpq3gshtivcif t67;
        gm_tvdg94bk7aa22 t68;
        GM_ST(uint8_t, (uint8_t *)&t68 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t68 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t68 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t68 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)28ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t67 + 0, t68);
        gm_tvdg94bk7aa22 t69;
        GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t69 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)29ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t67 + 12, t69);
        gm_tvdg94bk7aa22 t70;
        GM_ST(uint8_t, (uint8_t *)&t70 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t70 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t70 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t70 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t70 + 0), ((uint8_t)30ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t67 + 24, t70);
        gm_tvdg94bk7aa22 t71;
        GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t71 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)31ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t67 + 36, t71);
        gm_tvdg94bk7aa22 t72;
        GM_ST(uint8_t, (uint8_t *)&t72 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t72 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t72 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t72 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t72 + 0), ((uint8_t)34ULL));
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t67 + 48, t72);
        uint8_t t73 = 0;
        if (gm_eqg63o53ar52(((uint8_t *)&t67) + 0, ((uint8_t *)&t66))) t73 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t67) + 12, ((uint8_t *)&t66))) t73 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t67) + 24, ((uint8_t *)&t66))) t73 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t67) + 36, ((uint8_t *)&t66))) t73 = 1;
        if (gm_eqg63o53ar52(((uint8_t *)&t67) + 48, ((uint8_t *)&t66))) t73 = 1;
        uint8_t t74 = (uint8_t)(t73);
        if (!t74) goto ie65;
        uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t76 = ((t75 == ((uint64_t)1ULL)));
        if (!t76) goto ie65;
        {
            void * t79 = gm_l4;
            uint64_t t80 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t80 >= (uintptr_t)(t81)) gm_trap_index(t80, t81, gm_src7afujsggtlje, 473, 26);
            gm_t88rcucg1b7uk t82 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t80 * 88) + 0));
            gm_tg5lql6itufk7 t83 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t79, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t83))[8] != 0)) goto ie78;
            gm_ttovhfee8mf28 t84 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t83));
            gm_l9 = t84;
            {
                gm_ts6ln23edn25a t86;
                memset((uint8_t *)&t86 + 20, 0, 4);
                gm_tvdg94bk7aa22 t87 = gm_l1;
                GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t86 + 0, t87);
                gm_ttovhfee8mf28 t88 = gm_l9;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t86 + 12, t88);
                uint64_t t89 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                if (t89 >= (uintptr_t)(t90)) gm_trap_index(t89, t90, gm_src7afujsggtlje, 473, 63);
                void * t91 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t89 * 8));
                GM_ST(void *, (uint8_t *)&t86 + 24, t91);
                memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)29ULL));
                GM_ST(gm_ts6ln23edn25a, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t86);
                goto gm_fdone;
            }
            goto ix78;
ie78:;
ix78:;
        }
        goto ix65;
ie65:;
ix65:;
    }
    {
        gm_tt8o5kebou62r t92;
        memset((uint8_t *)&t92 + 12, 0, 4);
        gm_tvdg94bk7aa22 t93 = gm_l1;
        GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t92 + 0, t93);
        gm_b16_8 t94 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t92 + 16, t94);
        memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)34ULL));
        GM_ST(gm_tt8o5kebou62r, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t92);
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
    GM_STACK_CHECK(&F, 293u);
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
            if (!((((uint8_t *)&t14))[8] != 0)) goto ie10;
            gm_ttovhfee8mf28 t15 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t14));
            gm_l6 = t15;
            void * t16 = gm_l4;
            void * t17 = gm_l3;
            gm_t88rcucg1b7uk t18 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t17) + 0));
            gm_tg5lql6itufk7 t19 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t16, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!((((uint8_t *)&t19))[8] != 0)) goto ie10;
            gm_ttovhfee8mf28 t20 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t19));
            gm_l7 = t20;
            {
                gm_tr8okf1iilonm t22;
                memset((uint8_t *)&t22 + 28, 0, 4);
                gm_tvdg94bk7aa22 t23;
                GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
                GM_ST(uint32_t, (uint8_t *)&t23 + 8, 0);
                GM_ST(uint8_t, ((uint8_t *)&t23 + 0), ((uint8_t)0ULL));
                GM_ST(gm_tvdg94bk7aa22, (uint8_t *)&t22 + 0, t23);
                gm_ttovhfee8mf28 t24 = gm_l6;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t22 + 12, t24);
                gm_ttovhfee8mf28 t25 = gm_l7;
                GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t22 + 20, t25);
                void * t26 = gm_l5;
                GM_ST(void *, (uint8_t *)&t22 + 32, t26);
                memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)27ULL));
                GM_ST(gm_tr8okf1iilonm, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t22);
                goto gm_fdone;
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
        gm_tmn7n0n9raou5 t27 = gm_l1;
        void * t28 = gm_l5;
        gm_t8jsc4n9vp2ib t29;
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t29 + 0), t27);
        GM_ST(void *, ((uint8_t *)&t29 + 8), t28);
        memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)33ULL));
        GM_ST(gm_t8jsc4n9vp2ib, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t29);
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
    GM_STACK_CHECK(&F, 462u);
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
                        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_src7afujsggtlje, 512, 9);
                        gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 507, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t42 = gm_l4;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    uint64_t t45 = r43;
                    gm_l4 = t45;
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
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        uint8_t t47 = 0;
        F.ret = t46;
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
    GM_STACK_CHECK(&F, 3708u);
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
                uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&t80) + 8);
                if (t84 >= (uintptr_t)(t85)) gm_trap_index(t84, t85, gm_src7afujsggtlje, 535, 38);
                gm_tbci5ospk5nnn t86 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t80)) + t84 * 72) + 24));
                unsigned __int128 t87 = gm_f_bits128_jigkui4(&t86);
                gm_l13 = t87;
            }
            {
                gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l15) + 8));
                gm_b16_8 t90 = t89;
                uint32_t t91 = gm_l9;
                uint64_t t92 = (uint64_t)t91;
                uint64_t t93 = gm_f_us_cuv01fp(t92);
                uint64_t t94 = (uintptr_t)t93;
                uint64_t t95 = GM_LD(uintptr_t, ((uint8_t *)&t90) + 8);
                if (t94 >= (uintptr_t)(t95)) gm_trap_index(t94, t95, gm_src7afujsggtlje, 536, 26);
                if (!(((((uint8_t *)GM_LD(void *, ((uint8_t *)&t90)) + t94 * 72) + 16))[4] != 0)) goto ie88;
                gm_t88rcucg1b7uk t96 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&t90)) + t94 * 72) + 16));
                gm_l16 = t96;
                {
                    void * t98 = gm_l2;
                    gm_t88rcucg1b7uk t99 = gm_l16;
                    uint64_t t100 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t98, t99);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l14 = t100;
                }
                goto ix88;
ie88:;
ix88:;
            }
            goto ix75;
ie75:;
ix75:;
        }
        {
            void * t101 = gm_l2;
            gm_t88rcucg1b7uk t102 = gm_l12;
            uint64_t t103 = gm_f_size_hr4b76n((void *)NULL, cx, NULL, t101, t102);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t103;
        }
        {
            unsigned __int128 t104 = gm_l13;
            uint64_t t105 = (uint64_t)t104;
            uint64_t t106 = gm_l17;
            uint64_t r107; bool o108 = __builtin_mul_overflow(((uint64_t)8ULL), t106, &r107);
            if (o108) gm_trap("integer overflow in `*`", gm_src7afujsggtlje, 540, 47);
            uint64_t t109 = r107;
            uint64_t t110 = (t109 < ((uint64_t)64ULL) ? t109 : ((uint64_t)64ULL));
            uint32_t t111 = (uint32_t)t110;
            uint64_t t112 = gm_f_mask64_vvoc1f6(t111);
            uint64_t t113 = (uint64_t)(t105 & t112);
            gm_l18 = t113;
        }
        gm_t4nomjfe3hlif t114;
        uint32_t t115 = gm_l9;
        GM_ST(uint32_t, (uint8_t *)&t114 + 0, t115);
        gm_t88rcucg1b7uk t116 = gm_l12;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t114 + 4, t116);
        uint64_t t117 = gm_l17;
        GM_ST(uint64_t, (uint8_t *)&t114 + 8, t117);
        unsigned __int128 t118 = gm_l13;
        GM_ST(unsigned __int128, (uint8_t *)&t114 + 16, t118);
        uint64_t t119 = gm_l18;
        GM_ST(uint64_t, (uint8_t *)&t114 + 32, t119);
        void * t120 = gm_l2;
        void * t121 = gm_l1;
        gm_t88rcucg1b7uk t122 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t121) + 0));
        uint64_t t123 = gm_f_payload_offset_la0c8ft((void *)NULL, cx, NULL, t120, t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint64_t, (uint8_t *)&t114 + 40, t123);
        uint64_t t124 = gm_l14;
        GM_ST(uint64_t, (uint8_t *)&t114 + 48, t124);
        void * t125;
        void * t127 = gm_l10;
        if (!(GM_LD(void *, ((uint8_t *)&t127)) != 0)) goto ie126;
        void * t128 = GM_LD(void *, ((uint8_t *)&t127));
        gm_l19 = t128;
        void * t130 = gm_l0;
        void * t131 = gm_l19;
        void * t132 = gm_f_low_pat_bba97qc((void *)NULL, cx, NULL, t130, t131);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t133 = t132;
        t125 = t133;
        goto ix126;
ie126:;
        void * t135 = ((void *)0);
        t125 = t135;
ix126:;
        GM_ST(void *, (uint8_t *)&t114 + 56, t125);
        gm_tlpa5tka3taqb t136;
        memset(&t136, 0, sizeof t136);
        GM_ST(uint8_t, ((uint8_t *)&t136 + 0), ((uint8_t)8ULL));
        GM_ST(gm_t4nomjfe3hlif, ((uint8_t *)&t136 + 16), t114);
        gm_tlpa5tka3taqb t137 = t136;
        t5 = t137;
        goto me6;
mn65:;
ma16:;
        uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t139 != ((uint8_t)9ULL)) goto mn138;
        gm_b16_8 t140 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l20 = t140;
        {
            gm_f_list_31hegji((void *)&F.l21, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t143 = gm_l20;
                gm_l22 = t143;
            }
            {
                gm_l23 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t145 = gm_l23;
                    uint64_t t146 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                    uint8_t t147 = (t145 < t146);
                    if (!t147) goto lb144;
                    {
                        uint64_t t150 = gm_l23;
                        uint64_t t151 = (uintptr_t)t150;
                        uint64_t t152 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
                        gm_ttggq7ng4kp3p t155 = GM_LD(gm_ttggq7ng4kp3p, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l22)) + t151 * 16));
                        gm_l24 = t155;
                        {
                            void * t157 = (void *)((uint8_t *)&F.l21);
                            uint32_t t158 = GM_LD(uint32_t, (((uint8_t *)&gm_l24) + 0));
                            void * t159 = gm_l0;
                            void * t160 = GM_LD(void *, (((uint8_t *)&gm_l24) + 8));
                            void * t161 = gm_f_low_pat_bba97qc((void *)NULL, cx, NULL, t159, t160);
                            if (gm_unw.kind) {
                                goto gm_ll148;
                            }
                            gm_ttggq7ng4kp3p t162;
                            GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
                            GM_ST(uint32_t, ((uint8_t *)&t162 + 0), t158);
                            GM_ST(void *, ((uint8_t *)&t162 + 8), t161);
                            gm_tftb1odc739t5 t163 = gm_f_push_69l8e45(t157, t162);
                            if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 1, gm_src7afujsggtlje, 545, 31);
                            gm_b0_1 t164 = GM_LD(gm_b0_1, (((uint8_t *)&t163) + 1));
                        }
                        goto me153;
mn154:;
                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 545, 13);
me153:;
                    }
lc144:;
                    {
                        uint64_t t166 = gm_l23;
                        uint64_t r167; bool o168 = __builtin_add_overflow(t166, ((uint64_t)1ULL), &r167);
                        uint64_t t169 = r167;
                        gm_l23 = t169;
                    }
                }
gm_ll148:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb144; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc144; }
                }
                goto gm_lad;
lb144:;
            }
        }
        gm_b16_8 t170 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
        gm_tlpa5tka3taqb t171;
        memset(&t171, 0, sizeof t171);
        GM_ST(uint8_t, ((uint8_t *)&t171 + 0), ((uint8_t)9ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t171 + 16), t170);
        gm_tlpa5tka3taqb t172 = t171;
        t5 = t172;
        goto me6;
mn138:;
ma17:;
        uint8_t t174 = GM_LD(uint8_t, ((((uint8_t *)t4) + 8) + 0));
        if (t174 != ((uint8_t)10ULL)) goto mn173;
        gm_b16_8 t175 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l25 = t175;
        {
            gm_f_list_nh3d5t2_2((void *)&F.l26, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t178 = gm_l25;
                gm_l27 = t178;
            }
            {
                gm_l28 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t180 = gm_l28;
                    uint64_t t181 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                    uint8_t t182 = (t180 < t181);
                    if (!t182) goto lb179;
                    {
                        uint64_t t185 = gm_l28;
                        uint64_t t186 = (uintptr_t)t185;
                        uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                        void * t188 = GM_LD(void *, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t186 * 8));
                        void * t191 = GM_LD(void *, ((uint8_t *)&t188));
                        gm_l29 = t191;
                        {
                            void * t193 = (void *)((uint8_t *)&F.l26);
                            void * t194 = gm_l0;
                            void * t195 = gm_l29;
                            void * t196 = gm_f_low_pat_bba97qc((void *)NULL, cx, NULL, t194, t195);
                            if (gm_unw.kind) {
                                goto gm_ll183;
                            }
                            gm_tftb1odc739t5 t197 = gm_f_push_u2jka7o_2(t193, t196);
                            if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 1, gm_src7afujsggtlje, 550, 29);
                            gm_b0_1 t198 = GM_LD(gm_b0_1, (((uint8_t *)&t197) + 1));
                        }
                        goto me189;
mn190:;
                        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 550, 13);
me189:;
                    }
lc179:;
                    {
                        uint64_t t200 = gm_l28;
                        uint64_t r201; bool o202 = __builtin_add_overflow(t200, ((uint64_t)1ULL), &r201);
                        uint64_t t203 = r201;
                        gm_l28 = t203;
                    }
                }
gm_ll183:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb179; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc179; }
                }
                goto gm_lad;
lb179:;
            }
        }
        gm_b16_8 t204 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
        gm_tlpa5tka3taqb t205;
        memset(&t205, 0, sizeof t205);
        GM_ST(uint8_t, ((uint8_t *)&t205 + 0), ((uint8_t)10ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t205 + 16), t204);
        gm_tlpa5tka3taqb t206 = t205;
        t5 = t206;
        goto me6;
mn173:;
ma18:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 519, 21);
me6:;
        F.l30 = t5;
    }
    {
        gm_tdgedp3mu9p1a t207;
        memset((uint8_t *)&t207 + 4, 0, 12);
        memset((uint8_t *)&t207 + 156, 0, 4);
        void * t208 = gm_l1;
        gm_t88rcucg1b7uk t209 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t208) + 0));
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t207 + 0, t209);
        memcpy((uint8_t *)&t207 + 16, ((uint8_t *)&F.l30), 128);
        void * t210 = gm_l1;
        gm_tl874t2atnlh3 t211 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t210) + 120));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t207 + 144, t211);
        gm_tq7s98ihonkju t212 = gm_f_new_1b2khgi_2((void *)NULL, cx, NULL, &t207, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k4j26j8fh8s)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t212))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t212) + 8, gm_src7afujsggtlje, 554, 12);
        void * t213 = GM_LD(void *, (((uint8_t *)&t212) + 8));
        uint8_t t214 = 0;
        F.ret = t213;
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
    GM_STACK_CHECK(&F, 204u);
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
            memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)25ULL));
            GM_ST(void *, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t10);
            goto gm_fdone;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        void * t12 = gm_l4;
        void * t13 = gm_l2;
        gm_t88rcucg1b7uk t14 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t13) + 0));
        gm_tg5lql6itufk7 t15 = gm_f_int_w_a8oe7le((void *)NULL, cx, NULL, t12, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t15))[8] != 0)) goto ie11;
        gm_ttovhfee8mf28 t16 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t15));
        gm_l6 = t16;
        {
            gm_t9s8r45p2ijj5 t18;
            memset((uint8_t *)&t18 + 1, 0, 3);
            memset((uint8_t *)&t18 + 12, 0, 4);
            gm_t9qkh0rsooc8d t19 = gm_l1;
            GM_ST(gm_t9qkh0rsooc8d, (uint8_t *)&t18 + 0, t19);
            gm_ttovhfee8mf28 t20 = gm_l6;
            GM_ST(gm_ttovhfee8mf28, (uint8_t *)&t18 + 4, t20);
            void * t21 = gm_l5;
            GM_ST(void *, (uint8_t *)&t18 + 16, t21);
            memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)26ULL));
            GM_ST(gm_t9s8r45p2ijj5, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t18);
            goto gm_fdone;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        gm_t9qkh0rsooc8d t22 = gm_l1;
        void * t23 = gm_l5;
        gm_t8ghgl2jj0qha t24;
        GM_ST(uint8_t, (uint8_t *)&t24 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t24 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t24 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t24 + 0), t22);
        GM_ST(void *, ((uint8_t *)&t24 + 8), t23);
        memset(&(*(gm_tbo4aa19b457h *)gm_out), 0, sizeof (*(gm_tbo4aa19b457h *)gm_out));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 0, ((uint8_t)30ULL));
        GM_ST(gm_t8ghgl2jj0qha, (uint8_t *)&(*(gm_tbo4aa19b457h *)gm_out) + 8, t24);
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
extern __attribute__((aligned(4))) void * gm_f_lower_body_g4bo546(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) void * gm_f_mk_node_ndavg23(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2) {
    struct gm_fr_mk_node_ndavg23 F;
    void * gm_l0; /* lw */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 277u);
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
    if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_src7afujsggtlje, 243, 75);
    void * t16 = GM_LD(void *, (((uint8_t *)&t15) + 8));
    F.ret = t16;
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
    GM_STACK_CHECK_SHALLOW(&F, 328u);
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
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma9; case ((uint8_t)9ULL): goto ma8; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; default: goto ma10; }
ma6:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t14 != ((uint8_t)11ULL)) goto mn13;
        uint64_t t15 = GM_LD(uint64_t, ((((uint8_t *)&t4) + 8) + 0));
        gm_l2 = t15;
        gm_t9qkh0rsooc8d t16;
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
        uint64_t t17 = gm_l2;
        gm_te0mgnjdphs16 t18;
        GM_ST(uint8_t, (uint8_t *)&t18 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t18 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t18 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t18 + 0), t16);
        GM_ST(uint64_t, ((uint8_t *)&t18 + 8), t17);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out))[16] = 1;
        GM_ST(gm_te0mgnjdphs16, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 0, t18);
        goto gm_fdone;
        goto me5;
mn13:;
ma7:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t20 != ((uint8_t)10ULL)) goto mn19;
        gm_t9qkh0rsooc8d t21;
        GM_ST(uint8_t, ((uint8_t *)&t21 + 0), ((uint8_t)1ULL));
        gm_te0mgnjdphs16 t22;
        GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t22 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t22 + 0), t21);
        GM_ST(uint64_t, ((uint8_t *)&t22 + 8), ((uint64_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out))[16] = 1;
        GM_ST(gm_te0mgnjdphs16, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 0, t22);
        goto gm_fdone;
        goto me5;
mn19:;
ma8:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t24 != ((uint8_t)9ULL)) goto mn23;
        gm_t9qkh0rsooc8d t25;
        GM_ST(uint8_t, ((uint8_t *)&t25 + 0), ((uint8_t)2ULL));
        gm_te0mgnjdphs16 t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
        GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t26 + 0), t25);
        GM_ST(uint64_t, ((uint8_t *)&t26 + 8), ((uint64_t)0ULL));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 20, 0);
        ((uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out))[16] = 1;
        GM_ST(gm_te0mgnjdphs16, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 0, t26);
        goto gm_fdone;
        goto me5;
mn23:;
ma9:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
        if (t28 != ((uint8_t)8ULL)) goto mn27;
        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t4) + 8) + 4));
        gm_l3 = t29;
        {
            void * t32 = gm_l0;
            gm_t88rcucg1b7uk t33 = gm_l3;
            gm_t53at80pfdus3 t34;
            gm_f_kind_htv6093((void *)&t34, t32, GM_CONV(gm_csguankko4lmhd, t33));
            uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t34) + 0));
            if (t35 != ((uint8_t)11ULL)) goto ie31;
            uint64_t t36 = GM_LD(uint64_t, ((((uint8_t *)&t34) + 8) + 0));
            gm_l4 = t36;
            {
                gm_t9qkh0rsooc8d t38;
                GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)3ULL));
                uint64_t t39 = gm_l4;
                gm_te0mgnjdphs16 t40;
                GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t40 + 4, 0);
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t40 + 0), t38);
                GM_ST(uint64_t, ((uint8_t *)&t40 + 8), t39);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 20, 0);
                ((uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out))[16] = 1;
                GM_ST(gm_te0mgnjdphs16, (uint8_t *)&(*(gm_thdrvp9q0u7b5 *)gm_out) + 0, t40);
                goto gm_fdone;
            }
            goto ix31;
ie31:;
            {
                gm_thdrvp9q0u7b5 t42;
                memset(&t42, 0, sizeof t42);
                uint8_t t43 = 0;
                F.ret = t42;
                goto gm_fret;
            }
ix31:;
        }
        goto me5;
mn27:;
ma10:;
        gm_thdrvp9q0u7b5 t45;
        memset(&t45, 0, sizeof t45);
        uint8_t t46 = 0;
        F.ret = t45;
        goto gm_fret;
        goto me5;
mn44:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 259, 5);
me5:;
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
    GM_STACK_CHECK(&F, 395u);
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
        if (!((((uint8_t *)&t5))[8] != 0)) goto ie2;
        gm_ttovhfee8mf28 t6 = GM_LD(gm_ttovhfee8mf28, ((uint8_t *)&t5));
        gm_l2 = t6;
        {
            gm_tbgiuvo2j5vcs t8;
            uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
            if (!t10) goto ie9;
            uint32_t t12 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            gm_tmn7n0n9raou5 t13;
            GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)1ULL));
            GM_ST(uint32_t, ((uint8_t *)&t13 + 4), t12);
            gm_tmn7n0n9raou5 t14 = t13;
            gm_tbgiuvo2j5vcs t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[8] = 1;
            t8 = t15;
            goto ix9;
ie9:;
            gm_tmn7n0n9raou5 t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t17 + 0), ((uint8_t)0ULL));
            gm_tmn7n0n9raou5 t18 = t17;
            gm_tbgiuvo2j5vcs t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 10, 0);
            GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t19), t18);
            ((uint8_t *)&t19)[8] = 1;
            t8 = t19;
ix9:;
            uint8_t t20 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t21 = gm_l0;
        gm_t88rcucg1b7uk t22 = gm_l1;
        gm_t53at80pfdus3 t23;
        gm_f_kind_htv6093((void *)&t23, t21, GM_CONV(gm_csguankko4lmhd, t22));
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
        switch (t30) { case ((uint8_t)0ULL): goto ma28; case ((uint8_t)1ULL): goto ma28; case ((uint8_t)2ULL): goto ma28; case ((uint8_t)3ULL): goto ma25; case ((uint8_t)4ULL): goto ma26; case ((uint8_t)5ULL): goto ma28; case ((uint8_t)6ULL): goto ma28; case ((uint8_t)7ULL): goto ma27; case ((uint8_t)8ULL): goto ma27; case ((uint8_t)9ULL): goto ma27; case ((uint8_t)10ULL): goto ma28; case ((uint8_t)11ULL): goto ma28; case ((uint8_t)12ULL): goto ma28; case ((uint8_t)13ULL): goto ma28; case ((uint8_t)14ULL): goto ma28; case ((uint8_t)15ULL): goto ma28; case ((uint8_t)16ULL): goto ma27; case ((uint8_t)17ULL): goto ma28; case ((uint8_t)18ULL): goto ma28; case ((uint8_t)19ULL): goto ma28; case ((uint8_t)20ULL): goto ma28; case ((uint8_t)21ULL): goto ma28; case ((uint8_t)22ULL): goto ma28; case ((uint8_t)23ULL): goto ma28; case ((uint8_t)24ULL): goto ma28; case ((uint8_t)25ULL): goto ma28; case ((uint8_t)26ULL): goto ma28; default: goto ma28; }
ma25:;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
        if (t32 != ((uint8_t)3ULL)) goto mn31;
        uint16_t t33 = GM_LD(uint16_t, (((uint8_t *)&t23) + 8));
        gm_l3 = t33;
        gm_tbgiuvo2j5vcs t34;
        uint16_t t36 = gm_l3;
        uint8_t t37 = ((t36 == ((uint16_t)32ULL)));
        if (!t37) goto ie35;
        gm_tmn7n0n9raou5 t39;
        GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)2ULL));
        gm_tmn7n0n9raou5 t40 = t39;
        gm_tbgiuvo2j5vcs t41;
        GM_ST(uint8_t, (uint8_t *)&t41 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t41 + 10, 0);
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t41), t40);
        ((uint8_t *)&t41)[8] = 1;
        t34 = t41;
        goto ix35;
ie35:;
        gm_tbgiuvo2j5vcs t42;
        uint16_t t44 = gm_l3;
        uint8_t t45 = ((t44 == ((uint16_t)64ULL)));
        if (!t45) goto ie43;
        gm_tmn7n0n9raou5 t47;
        GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)3ULL));
        gm_tmn7n0n9raou5 t48 = t47;
        gm_tbgiuvo2j5vcs t49;
        GM_ST(uint8_t, (uint8_t *)&t49 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t49 + 10, 0);
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t49), t48);
        ((uint8_t *)&t49)[8] = 1;
        t42 = t49;
        goto ix43;
ie43:;
        gm_tbgiuvo2j5vcs t51;
        memset(&t51, 0, sizeof t51);
        gm_tbgiuvo2j5vcs t52 = t51;
        t42 = t52;
ix43:;
        t34 = t42;
ix35:;
        uint8_t t53 = 0;
        F.ret = t34;
        goto gm_fret;
        goto me24;
mn31:;
ma26:;
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
        if (t55 != ((uint8_t)4ULL)) goto mn54;
        gm_tmn7n0n9raou5 t56;
        GM_ST(uint8_t, (uint8_t *)&t56 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t56 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t56 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t56 + 0), ((uint8_t)4ULL));
        gm_tbgiuvo2j5vcs t57;
        GM_ST(uint8_t, (uint8_t *)&t57 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t57 + 10, 0);
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t57), t56);
        ((uint8_t *)&t57)[8] = 1;
        uint8_t t58 = 0;
        F.ret = t57;
        goto gm_fret;
        goto me24;
mn54:;
ma27:;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
        if (t62 != ((uint8_t)8ULL)) goto pa61;
        goto po60;
pa61:;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
        if (t64 != ((uint8_t)9ULL)) goto pa63;
        goto po60;
pa63:;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
        if (t66 != ((uint8_t)7ULL)) goto pa65;
        goto po60;
pa65:;
        uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t23) + 0));
        if (t68 != ((uint8_t)16ULL)) goto pa67;
        goto po60;
pa67:;
        goto mn59;
po60:;
        gm_tmn7n0n9raou5 t69;
        GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)0ULL));
        gm_tbgiuvo2j5vcs t70;
        GM_ST(uint8_t, (uint8_t *)&t70 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t70 + 10, 0);
        GM_ST(gm_tmn7n0n9raou5, ((uint8_t *)&t70), t69);
        ((uint8_t *)&t70)[8] = 1;
        uint8_t t71 = 0;
        F.ret = t70;
        goto gm_fret;
        goto me24;
mn59:;
ma28:;
        gm_tbgiuvo2j5vcs t73;
        memset(&t73, 0, sizeof t73);
        uint8_t t74 = 0;
        F.ret = t73;
        goto gm_fret;
        goto me24;
mn72:;
ma29:;
        gm_trap("no match arm matched the scrutinee", gm_src7afujsggtlje, 229, 5);
me24:;
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

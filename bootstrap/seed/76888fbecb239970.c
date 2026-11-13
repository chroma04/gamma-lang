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
#define GM_STACK_CHECK(p, n) do { if ((uintptr_t)__builtin_frame_address(0) < gm_stack_limit + (uintptr_t)(n) + sizeof(*(p))) gm_stack_overflow(); } while (0)
#endif
#endif
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
static uint64_t gm_mix(uint64_t h, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the eight little-endian bytes of x, whatever the byte order of the machine. */
static uint64_t gm_mix64(uint64_t h, uint64_t x) {
    for (int i = 0; i < 8; i++) { h ^= (uint8_t)(x >> (8 * i)); h *= 0x100000001b3ULL; }
    return h;
}
/* Mixes the minimal two's-complement little-endian bytes of v (as BigInt::to_signed_bytes_le). */
static uint64_t gm_hash_int(uint64_t h, __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if ((v == 0 && !(byte & 0x80)) || (v == -1 && (byte & 0x80))) break;
    }
    return h;
}
static uint64_t gm_hash_uint(uint64_t h, unsigned __int128 v) {
    for (;;) {
        uint8_t byte = (uint8_t)v; v >>= 8;
        h ^= byte; h *= 0x100000001b3ULL;
        if (v == 0 && !(byte & 0x80)) break;
        if (v == 0) { h *= 0x100000001b3ULL; break; }
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
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_5dma72c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_ivf5vgl(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_tprkpoe(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_v43n3ub(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern uint8_t gm_c1jun9snbr4[7] __attribute__((aligned(1)));
extern uint8_t gm_c1qn9qrrluj[17] __attribute__((aligned(1)));
extern uint8_t gm_c23mu1onplu[3] __attribute__((aligned(1)));
extern uint8_t gm_c30d0vfr4ha[8] __attribute__((aligned(1)));
extern uint8_t gm_c40fejj7lfq[17] __attribute__((aligned(1)));
extern uint8_t gm_c4bbipnshg6[17] __attribute__((aligned(1)));
extern uint8_t gm_c4tbnbi68ao[320] __attribute__((aligned(8)));
extern uint8_t gm_c55218gtcr0[448] __attribute__((aligned(8)));
extern uint8_t gm_c5dhf54lj9h[1] __attribute__((aligned(1)));
extern uint8_t gm_c5led8t7730[39] __attribute__((aligned(1)));
extern uint8_t gm_c5lrgm5j673[3] __attribute__((aligned(1)));
extern uint8_t gm_c6c0gfng8q4[44] __attribute__((aligned(1)));
extern uint8_t gm_c6d4g136jr7[8] __attribute__((aligned(1)));
extern uint8_t gm_c7fs6k1a08r[13] __attribute__((aligned(1)));
extern uint8_t gm_c7s3i9thp0k[12] __attribute__((aligned(1)));
extern uint8_t gm_c7svj0uc8qe[4] __attribute__((aligned(1)));
extern uint8_t gm_c9801fpr5pe[2] __attribute__((aligned(1)));
extern uint8_t gm_c9b4lh7gft4[4] __attribute__((aligned(4)));
extern uint8_t gm_cbbu3qvtp3q[8] __attribute__((aligned(8)));
extern uint8_t gm_cc0dfelltlv[10] __attribute__((aligned(1)));
extern uint8_t gm_cck6svt8s4a[51] __attribute__((aligned(1)));
extern uint8_t gm_cfaubb414id[18] __attribute__((aligned(1)));
extern uint8_t gm_cfb7p1dd1fr[11] __attribute__((aligned(1)));
extern uint8_t gm_cga7n0g3amu[8] __attribute__((aligned(8)));
extern uint8_t gm_cgd1khc5sal[24] __attribute__((aligned(8)));
extern uint8_t gm_ch33skqrgi0[5] __attribute__((aligned(1)));
extern uint8_t gm_ch6j88vtkil[2] __attribute__((aligned(1)));
extern uint8_t gm_ci51f79gd9k[8] __attribute__((aligned(1)));
extern uint8_t gm_cka88d5t8lu[16] __attribute__((aligned(8)));
extern uint8_t gm_cl03rhutns3[112] __attribute__((aligned(8)));
extern uint8_t gm_clacg9fr6j2[58] __attribute__((aligned(1)));
extern uint8_t gm_cmq9in3i5gq[3] __attribute__((aligned(1)));
extern uint8_t gm_cmtljgd99p9[15] __attribute__((aligned(1)));
extern uint8_t gm_cmu8f7egfr6[2] __attribute__((aligned(1)));
extern uint8_t gm_coj5ig90obd[5] __attribute__((aligned(1)));
extern uint8_t gm_cojk58d4vkf[16] __attribute__((aligned(8)));
extern uint8_t gm_cp10508qfq2[9] __attribute__((aligned(1)));
extern uint8_t gm_cp453i0tag9[10] __attribute__((aligned(1)));
extern uint8_t gm_cqfr6mggm8j[15] __attribute__((aligned(1)));
extern uint8_t gm_cqmh9ctmru3[8] __attribute__((aligned(1)));
extern uint8_t gm_cquib4l6q11[8] __attribute__((aligned(1)));
extern uint8_t gm_crhue63gorj[4] __attribute__((aligned(4)));
extern uint8_t gm_crjl9v1c7hv[4] __attribute__((aligned(1)));
extern uint8_t gm_cru7nui1vli[4] __attribute__((aligned(1)));
extern uint8_t gm_csoii12j71c[5] __attribute__((aligned(1)));
extern uint8_t gm_ctrunud43if[10] __attribute__((aligned(1)));
extern uint8_t gm_cu9odmhbpt4[18] __attribute__((aligned(1)));
extern uint8_t gm_cvlvde3pjno[11] __attribute__((aligned(1)));
extern uint8_t gm_cvte04sgh18[13] __attribute__((aligned(1)));
extern void *const gm_hl6hnpe3vbk7[];
extern void *const gm_hlh12hh1mjda[];
extern void *const gm_hli2tfvgok6p[];
extern void *const gm_hlvanl0isvqe[];
extern uint8_t gm_k2rn6j11rv5[40] __attribute__((aligned(8)));
extern uint8_t gm_k4ha9p0b2mp[40] __attribute__((aligned(8)));
extern uint8_t gm_k4j26j8fh8s[40] __attribute__((aligned(8)));
extern uint8_t gm_k4tn3ii07uh[40] __attribute__((aligned(8)));
extern uint8_t gm_k62pju45ug1[40] __attribute__((aligned(8)));
extern uint8_t gm_k75gp9b9r49[40] __attribute__((aligned(8)));
extern uint8_t gm_k9epus5iul2[40] __attribute__((aligned(8)));
extern uint8_t gm_k9o9iqv20c0[40] __attribute__((aligned(8)));
extern uint8_t gm_kaa2flncdji[40] __attribute__((aligned(8)));
extern uint8_t gm_kbng43moaud[40] __attribute__((aligned(8)));
extern uint8_t gm_kdjk0ti7atv[40] __attribute__((aligned(8)));
extern uint8_t gm_ki1ml5fl8pd[40] __attribute__((aligned(8)));
extern uint8_t gm_kijoa5b5euq[40] __attribute__((aligned(8)));
extern uint8_t gm_kl3hhlfsksr[40] __attribute__((aligned(8)));
extern uint8_t gm_klqd6h436uv[40] __attribute__((aligned(8)));
extern uint8_t gm_kq0m5gkhfc4[40] __attribute__((aligned(8)));
extern uint8_t gm_kqu6q38jp8e[40] __attribute__((aligned(8)));
extern uint8_t gm_kr7fqi9m0ih[40] __attribute__((aligned(8)));
extern uint8_t gm_krlms0mdefs[40] __attribute__((aligned(8)));
extern uint8_t gm_ks0d3nb0eoq[40] __attribute__((aligned(8)));
extern uint8_t gm_ksnfevokhod[40] __attribute__((aligned(8)));
extern uint8_t gm_kvu1vd24o16[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02nu08jrc7mi[13];
extern const uint8_t gm_s06pkj5koudes[11];
extern const uint8_t gm_s09rebkngh86a[34];
extern const uint8_t gm_s0a1978iad5c9[5];
extern const uint8_t gm_s0aqhubr4upni[7];
extern const uint8_t gm_s0bc9s1qderg8[70];
extern const uint8_t gm_s0eipmcrsllno[8];
extern const uint8_t gm_s0f4hcujnvqta[95];
extern const uint8_t gm_s0lfbgij46fm3[74];
extern const uint8_t gm_s0m3gh47554ab[6];
extern const uint8_t gm_s0pqdsaf6jp6t[9];
extern const uint8_t gm_s0qfhsu29ohf6[14];
extern const uint8_t gm_s0s45q9kur545[15];
extern const uint8_t gm_s0tt0537if3ve[29];
extern const uint8_t gm_s10iv34fu7qe7[3];
extern const uint8_t gm_s13vssup4nqsr[12];
extern const uint8_t gm_s147lrab6u68i[29];
extern const uint8_t gm_s15tjb1oda5hu[6];
extern const uint8_t gm_s1b0nuksrh572[29];
extern const uint8_t gm_s1ec711cn6c5f[33];
extern const uint8_t gm_s1ecdmpgj8faq[24];
extern const uint8_t gm_s1fshvu7vb9i4[16];
extern const uint8_t gm_s1hspsqta2g4h[9];
extern const uint8_t gm_s1l7s35k2bj17[8];
extern const uint8_t gm_s1mc2427h3p1h[9];
extern const uint8_t gm_s1o0cl13ab3oc[18];
extern const uint8_t gm_s1r4msb7spg8n[4];
extern const uint8_t gm_s20mq4dk47bdd[4];
extern const uint8_t gm_s24et6r4rgifd[16];
extern const uint8_t gm_s24mra5tninbk[15];
extern const uint8_t gm_s26gmgbksn2pm[61];
extern const uint8_t gm_s274psem80gbe[10];
extern const uint8_t gm_s27tupsc0esm0[17];
extern const uint8_t gm_s2a85dvpmjp4u[14];
extern const uint8_t gm_s2esennel4fsa[38];
extern const uint8_t gm_s2idf31094gi9[8];
extern const uint8_t gm_s2j5j1obh5p7l[6];
extern const uint8_t gm_s2kr3v1c3a77k[17];
extern const uint8_t gm_s2kubr08sqj72[15];
extern const uint8_t gm_s2m4r2316r14d[11];
extern const uint8_t gm_s2o9ogopuec6c[108];
extern const uint8_t gm_s2vj6kgk19dag[32];
extern const uint8_t gm_s3347hvco05di[6];
extern const uint8_t gm_s33hvuvcdjs1b[11];
extern const uint8_t gm_s38t0si42sbdd[49];
extern const uint8_t gm_s3bkmscn5fi56[3];
extern const uint8_t gm_s3duthvmnjluh[148];
extern const uint8_t gm_s3ea3k3og84ri[9];
extern const uint8_t gm_s3jllttnltak3[26];
extern const uint8_t gm_s3js9g3l46oiv[40];
extern const uint8_t gm_s3m12mc3q4gpj[71];
extern const uint8_t gm_s3nhq46nogqdt[12];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s3tqkqetpkgjr[84];
extern const uint8_t gm_s3vboaodpmi9a[4];
extern const uint8_t gm_s42atdm90l4do[14];
extern const uint8_t gm_s48n17gh1k2md[3];
extern const uint8_t gm_s4av1h975k9ok[47];
extern const uint8_t gm_s4bcn9pd0h83n[48];
extern const uint8_t gm_s4bf0cndbl9lv[3];
extern const uint8_t gm_s4cess8gum6i7[6];
extern const uint8_t gm_s4gt1buiaiaiq[3];
extern const uint8_t gm_s4i1t9tcolakn[48];
extern const uint8_t gm_s4l3p0f0c6mmc[44];
extern const uint8_t gm_s4lj2bemc87jp[3];
extern const uint8_t gm_s4o541hcfllua[9];
extern const uint8_t gm_s4pv32nkh7r2r[11];
extern const uint8_t gm_s4rj8vs79ru3v[12];
extern const uint8_t gm_s4rugtmn8jarn[57];
extern const uint8_t gm_s4uu8ali3icr8[40];
extern const uint8_t gm_s52vv4hfcfuh5[15];
extern const uint8_t gm_s57kk096d0ite[45];
extern const uint8_t gm_s57qe8p5hq2v9[13];
extern const uint8_t gm_s5dln0neuav6j[11];
extern const uint8_t gm_s5epronuu4280[7];
extern const uint8_t gm_s5ku0alcfvlas[41];
extern const uint8_t gm_s5lqhiu2bn6of[8];
extern const uint8_t gm_s5m8hf43qu2i7[27];
extern const uint8_t gm_s5m8rkq92eohf[82];
extern const uint8_t gm_s5pra70i8o734[14];
extern const uint8_t gm_s5s1m7053mh3n[8];
extern const uint8_t gm_s5sdmhb4570qm[20];
extern const uint8_t gm_s5sin3j2044cm[41];
extern const uint8_t gm_s5u5a7gd2hl9p[6];
extern const uint8_t gm_s5udr3eget84q[6];
extern const uint8_t gm_s5uj41tova4lv[41];
extern const uint8_t gm_s5url3si4frr2[69];
extern const uint8_t gm_s633opr46rccf[7];
extern const uint8_t gm_s654e61ni0n2t[67];
extern const uint8_t gm_s65dghs61r7cp[13];
extern const uint8_t gm_s66hh83pieh4l[49];
extern const uint8_t gm_s66ph0gfa95db[14];
extern const uint8_t gm_s67qasqqp59vt[513];
extern const uint8_t gm_s688e8m4pgcda[6];
extern const uint8_t gm_s6aprmc3mqp0t[2];
extern const uint8_t gm_s6c1femb11dge[76];
extern const uint8_t gm_s6g1vvoofsgo5[21];
extern const uint8_t gm_s6hq50r4rk7e6[26];
extern const uint8_t gm_s6lr92ekld44s[13];
extern const uint8_t gm_s6m1a6s4q8cuh[50];
extern const uint8_t gm_s6n6v50inu7p0[8];
extern const uint8_t gm_s6nsj8sc6uhgg[17];
extern const uint8_t gm_s6rmjmkjior9j[13];
extern const uint8_t gm_s6tkmvprase06[14];
extern const uint8_t gm_s6u6c6o8c4lg0[5];
extern const uint8_t gm_s6uch3l6osunv[50];
extern const uint8_t gm_s72l6okdj2d3g[95];
extern const uint8_t gm_s78m4481dj77r[9];
extern const uint8_t gm_s78naq2blbmbt[32];
extern const uint8_t gm_s7a0708p0cdgv[38];
extern const uint8_t gm_s7a42e93eb2m6[15];
extern const uint8_t gm_s7eippa38tft8[7];
extern const uint8_t gm_s7fpdr39r789h[15];
extern const uint8_t gm_s7h9qsm54pb7m[6];
extern const uint8_t gm_s7knd69l0mhc0[36];
extern const uint8_t gm_s7q4c8j1mnq1f[22];
extern const uint8_t gm_s7qbqthgde8lk[29];
extern const uint8_t gm_s7t5pi0jh3856[10];
extern const uint8_t gm_s7uuha79fcmn5[18];
extern const uint8_t gm_s7vsso7k8tg8d[25];
extern const uint8_t gm_s7vv7qoquqmn9[14];
extern const uint8_t gm_s81l00c48rg4v[26];
extern const uint8_t gm_s84hftqv0533g[5];
extern const uint8_t gm_s85jtgs7au5b5[9];
extern const uint8_t gm_s866ofni9sb8u[7];
extern const uint8_t gm_s88u2nt2tl9hi[11];
extern const uint8_t gm_s8c49n38a2v0d[41];
extern const uint8_t gm_s8cq7ej9hnrr0[12];
extern const uint8_t gm_s8hhhdlan8pqr[8];
extern const uint8_t gm_s8i3r4s519jok[8];
extern const uint8_t gm_s8n2bs6jikgsm[4];
extern const uint8_t gm_s8n858pjjep3e[10];
extern const uint8_t gm_s8pjrp7ublsfs[171];
extern const uint8_t gm_s8rm42n03sfss[4];
extern const uint8_t gm_s8sgcefhqa4i0[43];
extern const uint8_t gm_s8sgunk80orlb[26];
extern const uint8_t gm_s8vjpeo1vp60m[16];
extern const uint8_t gm_s90aivk1p6or5[16];
extern const uint8_t gm_s92nfbpklold8[41];
extern const uint8_t gm_s95cbsclgo7gk[36];
extern const uint8_t gm_s95gukpblkpua[33];
extern const uint8_t gm_s97nfli622uqg[5];
extern const uint8_t gm_s98novtk49ooh[28];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_s9e3360r5ee0o[7];
extern const uint8_t gm_s9fseescjm8u4[25];
extern const uint8_t gm_s9hi7i7cl3e2t[78];
extern const uint8_t gm_s9indmpqvpug4[58];
extern const uint8_t gm_s9jqgpofojve4[35];
extern const uint8_t gm_s9kudvjpckcsm[10];
extern const uint8_t gm_s9o2c36nosv54[41];
extern const uint8_t gm_s9oarmm2oe3id[41];
extern const uint8_t gm_s9oo6ammv0sch[17];
extern const uint8_t gm_s9rm2mbmo3nku[7];
extern const uint8_t gm_sa29cuhpvu442[54];
extern const uint8_t gm_sa2e14pm9g0bb[17];
extern const uint8_t gm_sa7c621p9d7mr[13];
extern const uint8_t gm_sa7chm2nt7fsk[27];
extern const uint8_t gm_sa8gjfvck9d40[30];
extern const uint8_t gm_sa8mtcad5rfmf[26];
extern const uint8_t gm_sa9pve8r9407o[35];
extern const uint8_t gm_saa0ue0fi38t8[15];
extern const uint8_t gm_sabdpctdijia8[41];
extern const uint8_t gm_saeed1dnph0jk[52];
extern const uint8_t gm_sajhc6og5bv8v[59];
extern const uint8_t gm_sajso5sujsruj[47];
extern const uint8_t gm_san68r6np95il[6];
extern const uint8_t gm_saqh6vgbmf8m2[153];
extern const uint8_t gm_sat80i3p7mq2f[26];
extern const uint8_t gm_saveva62qedei[8];
extern const uint8_t gm_sb11lrfd85gub[14];
extern const uint8_t gm_sb22ajpcscr7v[41];
extern const uint8_t gm_sb6smklmjl60o[7];
extern const uint8_t gm_sb745qqv6kdrg[11];
extern const uint8_t gm_sb895t6tk3m9h[6];
extern const uint8_t gm_sb8i7j9s2n6jl[8];
extern const uint8_t gm_sb9bgabg66tgj[42];
extern const uint8_t gm_sba0bp6ej30io[30];
extern const uint8_t gm_sba60r64q4l0i[31];
extern const uint8_t gm_sbanjlvk3n14i[4];
extern const uint8_t gm_sbb1ri5er9djq[4];
extern const uint8_t gm_sbdno1ojbbrbs[45];
extern const uint8_t gm_sbgbo6kv938k3[19];
extern const uint8_t gm_sc0mj6111f7cc[10];
extern const uint8_t gm_sc4hsc9atc69r[33];
extern const uint8_t gm_sc7ertn8kmtl3[4];
extern const uint8_t gm_scbch1b3qo8hr[9];
extern const uint8_t gm_scbj8d61i03he[14];
extern const uint8_t gm_scc54gv4a7k04[6];
extern const uint8_t gm_scds626ikjji4[13];
extern const uint8_t gm_scduvjunh0oei[9];
extern const uint8_t gm_scgahod5hue9b[56];
extern const uint8_t gm_schmrvl8mk83l[9];
extern const uint8_t gm_schvua5hkahp9[16];
extern const uint8_t gm_scj618tc62815[15];
extern const uint8_t gm_scmgj2ccom9v4[40];
extern const uint8_t gm_scq06n33arhgv[5];
extern const uint8_t gm_scqt00otrhg1t[41];
extern const uint8_t gm_sd7vgd2pm42gk[14];
extern const uint8_t gm_sdaknqn35hokt[9];
extern const uint8_t gm_sdam23bqvafqn[18];
extern const uint8_t gm_sdc36oketvdk3[4];
extern const uint8_t gm_sdf1o8jga4hc6[38];
extern const uint8_t gm_sdg7s5c89ve6u[64];
extern const uint8_t gm_sdgdb59na1854[23];
extern const uint8_t gm_sdhddtlu453l5[13];
extern const uint8_t gm_sdhelph0q09tv[5];
extern const uint8_t gm_sdhns4s61djoc[41];
extern const uint8_t gm_sdi3k3oc2kq9i[15];
extern const uint8_t gm_sdo1hnokt0mq8[17];
extern const uint8_t gm_sdqgviehbfoad[6];
extern const uint8_t gm_sdrbnq4aduai5[16];
extern const uint8_t gm_sdvp5f4riom9f[3];
extern const uint8_t gm_se006k0nbfdeg[8];
extern const uint8_t gm_se0plntg0il3g[14];
extern const uint8_t gm_se2qf1ipmcha3[26];
extern const uint8_t gm_se7kda46rm021[107];
extern const uint8_t gm_se8v3b7tfvt0e[23];
extern const uint8_t gm_sej6t944fna3a[61];
extern const uint8_t gm_sekn24oquoo60[9];
extern const uint8_t gm_selatfetsf85i[10];
extern const uint8_t gm_sem2htac3j4np[66];
extern const uint8_t gm_sepmad5fu1s0n[8];
extern const uint8_t gm_ser747td9jt8d[41];
extern const uint8_t gm_sf44a6nc03ff5[10];
extern const uint8_t gm_sf6pbim706kre[4];
extern const uint8_t gm_sfali8nl85hc9[25];
extern const uint8_t gm_sfbip2dg905u8[7];
extern const uint8_t gm_sfbr39r30psvt[4];
extern const uint8_t gm_sfc0aek8lt25a[5];
extern const uint8_t gm_sfd0mn3sol25u[4];
extern const uint8_t gm_sfdj0c5vltira[31];
extern const uint8_t gm_sfeo7g7huf2q1[17];
extern const uint8_t gm_sfhct1r6m1qqo[11];
extern const uint8_t gm_sfjsmf1lebgp7[14];
extern const uint8_t gm_sfk3r5va3vobm[5];
extern const uint8_t gm_sfna4d5mnmoa8[30];
extern const uint8_t gm_sfpeqbitcbq42[6];
extern const uint8_t gm_sfr85ju00e9hk[138];
extern const uint8_t gm_sfusuld9q5ekv[44];
extern const uint8_t gm_sg1s2lngtt92k[13];
extern const uint8_t gm_sg550i4fp569j[55];
extern const uint8_t gm_sg7of7eks8a5u[7];
extern const uint8_t gm_sg7ronlh5k7js[84];
extern const uint8_t gm_sgb12qbh3gevi[5];
extern const uint8_t gm_sgc7crh43q9tj[6];
extern const uint8_t gm_sgcku4bq4407l[19];
extern const uint8_t gm_sgcporqt2gqqg[16];
extern const uint8_t gm_sgepo3aen3iei[15];
extern const uint8_t gm_sgfrcimthamdn[10];
extern const uint8_t gm_sgfufhs3psibd[4];
extern const uint8_t gm_sgh1o65rai9bu[8];
extern const uint8_t gm_sghh3jr7dlapd[66];
extern const uint8_t gm_sgi6la00bpjt2[15];
extern const uint8_t gm_sgisth1q9gntc[41];
extern const uint8_t gm_sgjv9g1a4rlf5[5];
extern const uint8_t gm_sgq8hfs47hogb[9];
extern const uint8_t gm_sgukm1evmrjal[4];
extern const uint8_t gm_sh00ruc3m501e[42];
extern const uint8_t gm_sh16qm7deqse5[7];
extern const uint8_t gm_sh4r0fq5alj61[14];
extern const uint8_t gm_sh57gpir0vvh2[24];
extern const uint8_t gm_shbaullahutpe[37];
extern const uint8_t gm_shda201svb9eg[119];
extern const uint8_t gm_shhtc33mlfd4u[31];
extern const uint8_t gm_shhugap3t1rkg[41];
extern const uint8_t gm_shl6ef6if4a86[80];
extern const uint8_t gm_shm91i3u3uv6p[5];
extern const uint8_t gm_shnp9b6k2mab8[5];
extern const uint8_t gm_shoiushfjjjs8[9];
extern const uint8_t gm_shrc3j552p60m[9];
extern const uint8_t gm_shrq42dntcbrd[19];
extern const uint8_t gm_shukorb0nk4kg[7];
extern const uint8_t gm_si2nsakslqgpp[16];
extern const uint8_t gm_sidpdvffdra02[14];
extern const uint8_t gm_sif59l16kubo3[17];
extern const uint8_t gm_sif5vtcq0ji03[17];
extern const uint8_t gm_sig2adm7e9t55[79];
extern const uint8_t gm_sighahslvsc1i[11];
extern const uint8_t gm_sigup1gvr7a13[5];
extern const uint8_t gm_sihdt3ld7f53j[16];
extern const uint8_t gm_sihs6d097ocev[5];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_siooblm685vmo[16];
extern const uint8_t gm_siotufd8s66oj[21];
extern const uint8_t gm_siqfg68f4ud6j[16];
extern const uint8_t gm_sj4cbut2umg1f[35];
extern const uint8_t gm_sj7cc74b29dv3[14];
extern const uint8_t gm_sj9t4d2ku5m56[47];
extern const uint8_t gm_sjac221bnk353[8];
extern const uint8_t gm_sjb8gf8n3vsft[42];
extern const uint8_t gm_sjbhqu0o8ebub[6];
extern const uint8_t gm_sjdk7e5fs4top[6];
extern const uint8_t gm_sje4rbc88csdu[7];
extern const uint8_t gm_sjgrq0dv175qo[30];
extern const uint8_t gm_sjil3d717dq6h[63];
extern const uint8_t gm_sjlktf1otofg9[10];
extern const uint8_t gm_sjmhgc2b6rnks[10];
extern const uint8_t gm_sjobl2qgsrpl6[41];
extern const uint8_t gm_sjpr4fvui63qj[13];
extern const uint8_t gm_sjs62u07kjioq[15];
extern const uint8_t gm_sjtfkltao8c2o[49];
extern const uint8_t gm_sjvb17co9jlff[15];
extern const uint8_t gm_sk0qegfrnee00[4];
extern const uint8_t gm_sk25jsmieu0fo[41];
extern const uint8_t gm_sk2nrl11c01gj[20];
extern const uint8_t gm_skbgm82aqj3q0[6];
extern const uint8_t gm_skq2uj9oi1243[41];
extern const uint8_t gm_skrdl5pdcve8g[43];
extern const uint8_t gm_skv53e6m2c88s[10];
extern const uint8_t gm_sl5jkfh6104lp[5];
extern const uint8_t gm_sl7doakbaoimv[7];
extern const uint8_t gm_sld9l7nonn4o9[17];
extern const uint8_t gm_slgbfnvgul4q1[7];
extern const uint8_t gm_slmlefe4cpf5e[15];
extern const uint8_t gm_slopv2r8r4od2[5];
extern const uint8_t gm_sltejl1fekbag[6];
extern const uint8_t gm_sltjg0sbhbo73[13];
extern const uint8_t gm_slu92ianijdhp[7];
extern const uint8_t gm_sm56lhobvb6g5[6];
extern const uint8_t gm_sm7179tmr0nhi[27];
extern const uint8_t gm_sm7hp6cgh4jle[6];
extern const uint8_t gm_sm8uf6b4m0n7p[8];
extern const uint8_t gm_sma6ellkcmke6[26];
extern const uint8_t gm_smacop0e0umsg[257];
extern const uint8_t gm_smb8nsuj9ie93[15];
extern const uint8_t gm_smbgfcmcr0r8n[11];
extern const uint8_t gm_smbls870q1nm0[16];
extern const uint8_t gm_smhjrt0kub63u[15];
extern const uint8_t gm_smk6fmrgs1hnc[6];
extern const uint8_t gm_smpmbscbko3gk[4];
extern const uint8_t gm_smqhtg33814q9[23];
extern const uint8_t gm_smror2m9tsk7v[8];
extern const uint8_t gm_sn35ih51dmdii[45];
extern const uint8_t gm_sn4utba4jl66l[9];
extern const uint8_t gm_snbb2gfh3k17u[26];
extern const uint8_t gm_sncbtqrmp1734[14];
extern const uint8_t gm_snjq35j2j9d3c[113];
extern const uint8_t gm_so05t2goj6895[24];
extern const uint8_t gm_so2ihtmk3e8kf[17];
extern const uint8_t gm_so43rnba8j5pb[35];
extern const uint8_t gm_so44j4cr69vhs[16];
extern const uint8_t gm_so5kmn66hbf25[22];
extern const uint8_t gm_so686pdrsalfh[587];
extern const uint8_t gm_so77flgm8klar[52];
extern const uint8_t gm_so7qmna0fo4so[6];
extern const uint8_t gm_so81f3f3mbv7o[54];
extern const uint8_t gm_soah6d9unb7uo[32];
extern const uint8_t gm_sobecpce3c56n[35];
extern const uint8_t gm_soh6rol6f5r5k[27];
extern const uint8_t gm_sokol7c4qt12p[6];
extern const uint8_t gm_sokruug51vi6k[11];
extern const uint8_t gm_soogmlptfncs1[12];
extern const uint8_t gm_soreld657hea2[25];
extern const uint8_t gm_sos7rilr21per[7];
extern const uint8_t gm_sottk452c14qm[56];
extern const uint8_t gm_sp13ju5l53585[24];
extern const uint8_t gm_sp21r812mg46a[4];
extern const uint8_t gm_sp4l42tpo6btg[11];
extern const uint8_t gm_sp625mljmvm7d[11];
extern const uint8_t gm_sp6f2rgoi8toe[7];
extern const uint8_t gm_sp75410cl55v2[13];
extern const uint8_t gm_sp7ec3ksfdilk[5];
extern const uint8_t gm_spa5q1834e71s[49];
extern const uint8_t gm_spajg3q27hr58[15];
extern const uint8_t gm_spg4d1df6jnji[7];
extern const uint8_t gm_spj9rva8kb5ar[3];
extern const uint8_t gm_spjfgtrrvac57[14];
extern const uint8_t gm_spm4139orpkvm[10];
extern const uint8_t gm_spp0mb1aidgi1[13];
extern const uint8_t gm_spskd7qirpoe3[25];
extern const uint8_t gm_sq05bfmao5v8n[47];
extern const uint8_t gm_sq1rqv8ou3vsv[14];
extern const uint8_t gm_sq3jr80otu7tj[3];
extern const uint8_t gm_sq5p8t05ccn5h[24];
extern const uint8_t gm_sq74a8ckacjn2[41];
extern const uint8_t gm_sq99f4u8gmi6n[28];
extern const uint8_t gm_sqa6gfa8omk7o[4];
extern const uint8_t gm_sqcj2s8m7mpqj[38];
extern const uint8_t gm_sqdjjj1odavib[34];
extern const uint8_t gm_sqeho958d04c0[17];
extern const uint8_t gm_sqen1asili489[52];
extern const uint8_t gm_sqfc0qpl56rfm[7];
extern const uint8_t gm_sqh06nbksvl8i[17];
extern const uint8_t gm_sqjlosotosjfp[18];
extern const uint8_t gm_sqnathr6pbque[38];
extern const uint8_t gm_sqnuqut1dl3fi[48];
extern const uint8_t gm_sqo6bs5b6kae9[8];
extern const uint8_t gm_sqqtsglgb4lu3[28];
extern const uint8_t gm_sqtgbqlca4c1g[25];
extern const uint8_t gm_squ1rl0dbtrtr[59];
extern const uint8_t gm_sr3uqaosat41v[41];
extern const uint8_t gm_sr499ju9p7ks2[23];
extern const uint8_t gm_sr899mamktpvo[7];
extern const char gm_src4o48ksu43djd[];
extern const char gm_src5cfou479ljhn[];
extern const char gm_src92rfidruvalq[];
extern const char gm_srcb2bqqsnukmrd[];
extern const char gm_srcb3t5fii3khrp[];
extern const char gm_srcc24pnihhgkug[];
extern const char gm_srcdds6vrrht3m4[];
extern const char gm_srcgksioc7kn741[];
extern const char gm_srck89elqhmqqdc[];
extern const char gm_srcp8o48ld4n4jq[];
extern const char gm_srcpit8mdn0781q[];
extern const char gm_srcq1k9f0klj1q6[];
extern const char gm_srcqod65l394oc8[];
extern const char gm_srcqtrdhljiapb8[];
extern const char gm_srcruugaa9g5kd5[];
extern const char gm_srcstmd1b5da98l[];
extern const uint8_t gm_sreprot2s508e[3];
extern const uint8_t gm_srhrn3fa70ni4[38];
extern const uint8_t gm_sri6fceeqvrql[19];
extern const uint8_t gm_srj5ohb3f61b7[14];
extern const uint8_t gm_srj9vrlard0qi[14];
extern const uint8_t gm_srll3895bp2kd[12];
extern const uint8_t gm_srlpdrobo1i0e[38];
extern const uint8_t gm_srp6tpaiaeunu[8];
extern const uint8_t gm_srplcc0p49iui[9];
extern const uint8_t gm_srq5utruoa2v3[28];
extern const uint8_t gm_srru5vqet7hl3[11];
extern const uint8_t gm_srssp1uhlkrll[20];
extern const uint8_t gm_ss2v2ursd8pms[4];
extern const uint8_t gm_ss42hvrprenkk[26];
extern const uint8_t gm_ss558hlhv1kah[31];
extern const uint8_t gm_ss66qg3t949dp[5];
extern const uint8_t gm_ssc5ahdetbl48[17];
extern const uint8_t gm_sse16dr2qj0im[3];
extern const uint8_t gm_ssfg877iegnma[4];
extern const uint8_t gm_ssga7g3v3nnl7[10];
extern const uint8_t gm_ssj8r83l87dr2[16];
extern const uint8_t gm_ssk8mqd1u6nn7[52];
extern const uint8_t gm_sslm2oq18gln3[6];
extern const uint8_t gm_ssmvuscdpkf2v[41];
extern const uint8_t gm_ssq0semhq7h0c[13];
extern const uint8_t gm_ssqt22rph287b[4];
extern const uint8_t gm_ssrcn9mu9bp3e[19];
extern const uint8_t gm_ssv5povjldd7c[3];
extern const uint8_t gm_st3c9ev3jkdrf[32];
extern const uint8_t gm_st803nqm7iok0[17];
extern const uint8_t gm_st9f93lco88fg[3];
extern const uint8_t gm_stafskn6bv24f[2];
extern const uint8_t gm_ste1ujg4tll4q[10];
extern const uint8_t gm_stevggi5tvq7c[18];
extern const uint8_t gm_stlhqhje21tdn[97];
extern const uint8_t gm_stmg18u2tvso0[18];
extern const uint8_t gm_sto310q1gi1de[12];
extern const uint8_t gm_sto6ssj6r66e3[30];
extern const uint8_t gm_sto8li44rv2ou[8];
extern const uint8_t gm_stshdb5m6rl63[5];
extern const uint8_t gm_stsr1tpfadro2[15];
extern const uint8_t gm_stu0uvs5p21oc[17];
extern const uint8_t gm_stugmrojdno85[6];
extern const uint8_t gm_stvci3fthpt8h[47];
extern const uint8_t gm_su41i9n9tujml[14];
extern const uint8_t gm_sua0n2tdbtos7[40];
extern const uint8_t gm_sub7l8h1sjc33[12];
extern const uint8_t gm_suc0sjksvr6j7[9];
extern const uint8_t gm_sucoj11hr978o[14];
extern const uint8_t gm_sue4ionbe9lgh[21];
extern const uint8_t gm_sujqb5peare4g[6];
extern const uint8_t gm_suklu1i9clb5l[48];
extern const uint8_t gm_summega2vonc9[6];
extern const uint8_t gm_suq3cd2v0vpun[41];
extern const uint8_t gm_suqsoolo29or4[24];
extern const uint8_t gm_suuq26jjqhul4[7];
extern const uint8_t gm_suvpcmvpe5152[41];
extern const uint8_t gm_sv397f5vej3g7[30];
extern const uint8_t gm_sv44r8pinka4e[13];
extern const uint8_t gm_sv8lr20bqpljh[12];
extern const uint8_t gm_svbftsm94r0bv[7];
extern const uint8_t gm_svdodmni8et3b[15];
extern const uint8_t gm_sveguffvp7vd6[15];
extern const uint8_t gm_svfn64lgobnrm[48];
extern const uint8_t gm_svkk03nujpn4k[8];
extern const uint8_t gm_svl13be5ca8rt[5];
extern const uint8_t gm_svs91tvrok38m[7];
extern const uint8_t gm_svutc5hf6cgiv[2];
extern const uint8_t gm_svv8nt9tamumf[41];
extern const gm_type gm_ty0lfe1s2pin;
extern const gm_field gm_ty0lfe1s2pin_f[];
const char gm_src4o48ksu43djd[] = "compiler/load/target.gm";
const char gm_src5cfou479ljhn[] = "compiler/check/array_ctor.gm";
const char gm_src92rfidruvalq[] = "compiler/cgen/spec.gm";
const char gm_srcb2bqqsnukmrd[] = "compiler/check/dangling.gm";
const char gm_srcb3t5fii3khrp[] = "compiler/pkg/scaffold.gm";
const char gm_srcc24pnihhgkug[] = "<core>/slice.gm";
const char gm_srcdds6vrrht3m4[] = "lib/std/math/math.gm";
const char gm_srcgksioc7kn741[] = "compiler/tir/clone.gm";
const char gm_srck89elqhmqqdc[] = "compiler/sema/vector.gm";
const char gm_srcp8o48ld4n4jq[] = "compiler/parse/types.gm";
const char gm_srcpit8mdn0781q[] = "compiler/interp/interp.gm";
const char gm_srcq1k9f0klj1q6[] = "compiler/sema/frame.gm";
const char gm_srcqod65l394oc8[] = "compiler/interp/intarith.gm";
const char gm_srcqtrdhljiapb8[] = "lib/std/io/io.gm";
const char gm_srcruugaa9g5kd5[] = "compiler/check/intrinsics.gm";
const char gm_srcstmd1b5da98l[] = "compiler/check/consts.gm";
const gm_type gm_ty0lfe1s2pin = {.size = 8, .align = 4, .kind = GM_K_TUPLE, .named = true, .nfields = 2, .fields = gm_ty0lfe1s2pin_f};
const gm_type gm_ty0lfe1s2pin;
const uint8_t gm_s02nu08jrc7mi[13] = "gm_fs_rename";
const uint8_t gm_s06pkj5koudes[11] = "-std=gnu11";
const uint8_t gm_s09rebkngh86a[34] = "slicing this type at compile time";
const uint8_t gm_s0a1978iad5c9[5] = "List";
const uint8_t gm_s0aqhubr4upni[7] = "fnsink";
const uint8_t gm_s0bc9s1qderg8[70] = "only an array variable (a place) can be sliced, not a temporary array";
const uint8_t gm_s0eipmcrsllno[8] = "{} \"{}\"";
const uint8_t gm_s0f4hcujnvqta[95] = "`{}` cannot be matched with a struct pattern outside its {}: its field `{}` is private (\302\2473.3)";
const uint8_t gm_s0lfbgij46fm3[74] = "internal error: `try` on a value that is neither an optional nor a result";
const uint8_t gm_s0m3gh47554ab[6] = "gm_x_";
const uint8_t gm_s0pqdsaf6jp6t[9] = "\000\000\001\000\000\000\000\000";
const uint8_t gm_s0qfhsu29ohf6[14] = "` needs `git`";
const uint8_t gm_s0s45q9kur545[15] = "), &ev, &evn);";
const uint8_t gm_s0tt0537if3ve[29] = "`.{}` cannot convert to `{}`";
const uint8_t gm_s10iv34fu7qe7[3] = "cx";
const uint8_t gm_s13vssup4nqsr[12] = "read_string";
const uint8_t gm_s147lrab6u68i[29] = "value of {} exceeds u64::MAX";
const uint8_t gm_s15tjb1oda5hu[6] = "(\342\200\246)";
const uint8_t gm_s1b0nuksrh572[29] = "`dependencies` holds strings";
const uint8_t gm_s1ec711cn6c5f[33] = ") { gm_unw.kind = GM_NONE; goto ";
const uint8_t gm_s1ecdmpgj8faq[24] = "GM_LD({}, {} + {} * {})";
const uint8_t gm_s1fshvu7vb9i4[16] = "float 0x{:016x}";
const uint8_t gm_s1hspsqta2g4h[9] = "<strong>";
const uint8_t gm_s1l7s35k2bj17[8] = "gm_ctz(";
const uint8_t gm_s1mc2427h3p1h[9] = ") - 1)))";
const uint8_t gm_s1o0cl13ab3oc[18] = "the matched value";
const uint8_t gm_s1r4msb7spg8n[4] = "map";
const uint8_t gm_s20mq4dk47bdd[4] = "m{}";
const uint8_t gm_s24et6r4rgifd[16] = "declare_foreign";
const uint8_t gm_s24mra5tninbk[15] = "GM_NOINLINE {}";
const uint8_t gm_s26gmgbksn2pm[61] = "`{}` is not a symbol name: use letters, digits and `_ $ . @`";
const uint8_t gm_s274psem80gbe[10] = "openClose";
const uint8_t gm_s27tupsc0esm0[17] = "cannot compare `";
const uint8_t gm_s2a85dvpmjp4u[14] = "{:.3} \302\265s/run";
const uint8_t gm_s2esennel4fsa[38] = "expected a value, found the type `{}`";
const uint8_t gm_s2idf31094gi9[8] = "sort_by";
const uint8_t gm_s2j5j1obh5p7l[6] = "loop ";
const uint8_t gm_s2kr3v1c3a77k[17] = "\000\000\000\000\000\000\000\000\021\000\000\000\000\000\000\000";
const uint8_t gm_s2kubr08sqj72[15] = "this assertion";
const uint8_t gm_s2m4r2316r14d[11] = "expected `";
const uint8_t gm_s2o9ogopuec6c[108] = "this performs `{}` inside a `with` that handles `{}`, which the effect-set parameter might include (\302\24716.6)";
const uint8_t gm_s2vj6kgk19dag[32] = "(uint8_t)__builtin_isfinite({})";
const uint8_t gm_s3347hvco05di[6] = ") == ";
const uint8_t gm_s33hvuvcdjs1b[11] = "<argument>";
const uint8_t gm_s38t0si42sbdd[49] = ", (uintptr_t)(n));\012    if (GM_PTR_TAGGED(code)) ";
const uint8_t gm_s3bkmscn5fi56[3] = "*\077";
const uint8_t gm_s3duthvmnjluh[148] = "main :: fn() needs Out {{\012    print(\"hello from {}\\n\")\012}}\012\012double :: fn(n: i32) -> i32 => n * 2\012\012test \"double\" {{\012    expect_eq(double(21), 42)\012}}\012";
const uint8_t gm_s3ea3k3og84ri[9] = "needs {}";
const uint8_t gm_s3jllttnltak3[26] = "cannot read the archive: ";
const uint8_t gm_s3js9g3l46oiv[40] = "a constant without a compile-time value";
const uint8_t gm_s3m12mc3q4gpj[71] = "the archive of dependency `{}` ({}) has the SHA-256 {}, but {} says {}";
const uint8_t gm_s3nhq46nogqdt[12] = "working_dir";
const uint8_t gm_s3p8gug7bvl1p[3] = "\\\\";
const uint8_t gm_s3tqkqetpkgjr[84] = "` cannot be spread: its elements are named by its variants, not positions (\302\2475.6.1)";
const uint8_t gm_s3vboaodpmi9a[4] = "-O0";
const uint8_t gm_s42atdm90l4do[14] = "an expression";
const uint8_t gm_s48n17gh1k2md[3] = "-u";
const uint8_t gm_s4av1h975k9ok[47] = "a `using` field must have a struct type, not `";
const uint8_t gm_s4bcn9pd0h83n[48] = "internal error: formatted output does not lex: ";
const uint8_t gm_s4bf0cndbl9lv[3] = ")]";
const uint8_t gm_s4cess8gum6i7[6] = "float";
const uint8_t gm_s4gt1buiaiaiq[3] = "In";
const uint8_t gm_s4i1t9tcolakn[48] = "`{}` is not a C-compatible result type (\302\24719.1)";
const uint8_t gm_s4l3p0f0c6mmc[44] = " has no gamma.toml in its package directory";
const uint8_t gm_s4lj2bemc87jp[3] = "-L";
const uint8_t gm_s4o541hcfllua[9] = "{}|{}|{}";
const uint8_t gm_s4pv32nkh7r2r[11] = "; memcpy(&";
const uint8_t gm_s4rj8vs79ru3v[12] = "gm_utf8_arg";
const uint8_t gm_s4rugtmn8jarn[57] = "`, which the effect-set parameter might include (\302\24716.6)";
const uint8_t gm_s4uu8ali3icr8[40] = "`add` needs exactly one dependency name";
const uint8_t gm_s52vv4hfcfuh5[15] = "gm_win_cmdline";
const uint8_t gm_s57kk096d0ite[45] = "the variable `{}` is assigned but never read";
const uint8_t gm_s57qe8p5hq2v9[13] = "<em><strong>";
const uint8_t gm_s5dln0neuav6j[11] = "archive {}";
const uint8_t gm_s5epronuu4280[7] = ") & ~(";
const uint8_t gm_s5ku0alcfvlas[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\237\001\000\000\015\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_s5lqhiu2bn6of[8] = "gamma: ";
const uint8_t gm_s5m8hf43qu2i7[27] = "` gives the package name `";
const uint8_t gm_s5m8rkq92eohf[82] = "a vector's lanes are `i8`\342\200\223`i64`, `u8`\342\200\223`u64`, `f32`, `f64` or `bool`, not `{}`";
const uint8_t gm_s5pra70i8o734[14] = "((uint8_t *)&";
const uint8_t gm_s5s1m7053mh3n[8] = "global ";
const uint8_t gm_s5sdmhb4570qm[20] = "` takes no receiver";
const uint8_t gm_s5sin3j2044cm[41] = "\000\000\000\000\000\000\000\000\027\000\000\000\000\000\000\000\322\000\000\000\"\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000";
const uint8_t gm_s5u5a7gd2hl9p[6] = " < 0)";
const uint8_t gm_s5udr3eget84q[6] = "Clamp";
const uint8_t gm_s5uj41tova4lv[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000\205\003\000\000\027\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_s5url3si4frr2[69] = "package `{}`: `checksum` must be `sha256:` and 64 hexadecimal digits";
const uint8_t gm_s633opr46rccf[7] = " += 1;";
const uint8_t gm_s654e61ni0n2t[67] = "    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto ";
const uint8_t gm_s65dghs61r7cp[13] = " bytes and `";
const uint8_t gm_s66hh83pieh4l[49] = "the cache is disabled (GAMMA_CACHE_DIR is empty)";
const uint8_t gm_s66ph0gfa95db[14] = "the builtin `";
const uint8_t gm_s67qasqqp59vt[513] = "\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\002\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\003\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\005\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_s688e8m4pgcda[6] = "block";
const uint8_t gm_s6aprmc3mqp0t[2] = "C";
const uint8_t gm_s6c1femb11dge[76] = "a pattern on the right of `is` cannot bind names; use `if pattern := value`";
const uint8_t gm_s6g1vvoofsgo5[21] = "invalid tag value S(";
const uint8_t gm_s6hq50r4rk7e6[26] = "compiler/interp/frames.gm";
const uint8_t gm_s6lr92ekld44s[13] = "the literal ";
const uint8_t gm_s6m1a6s4q8cuh[50] = "`; the conventions are `.C`, `.SysV` and `.Win64`";
const uint8_t gm_s6n6v50inu7p0[8] = "slicing";
const uint8_t gm_s6nsj8sc6uhgg[17] = "`debug-info` in ";
const uint8_t gm_s6rmjmkjior9j[13] = "` is a label";
const uint8_t gm_s6tkmvprase06[14] = "gm_free_cstrs";
const uint8_t gm_s6u6c6o8c4lg0[5] = "&lt;";
const uint8_t gm_s6uch3l6osunv[50] = "a `using` field must have a struct type, not `{}`";
const uint8_t gm_s72l6okdj2d3g[95] = "\\n\")\012}\012\012double :: fn(n: i32) -> i32 => n * 2\012\012test \"double\" {\012    expect_eq(double(21), 42)\012}\012";
const uint8_t gm_s78m4481dj77r[9] = "sys_read";
const uint8_t gm_s78naq2blbmbt[32] = "union __attribute__((packed)) {";
const uint8_t gm_s7a0708p0cdgv[38] = " != 0) gm_trap_misaligned((uintptr_t)";
const uint8_t gm_s7a42e93eb2m6[15] = "perform_native";
const uint8_t gm_s7eippa38tft8[7] = "<code>";
const uint8_t gm_s7fpdr39r789h[15] = "((gm_closure){";
const uint8_t gm_s7h9qsm54pb7m[6] = "with ";
const uint8_t gm_s7knd69l0mhc0[36] = "`!` unwraps a result, but this is `";
const uint8_t gm_s7q4c8j1mnq1f[22] = "Read-only file system";
const uint8_t gm_s7qbqthgde8lk[29] = "`{}` is a label, not a value";
const uint8_t gm_s7t5pi0jh3856[10] = "({} < {})";
const uint8_t gm_s7uuha79fcmn5[18] = "Nothing to remove";
const uint8_t gm_s7vsso7k8tg8d[25] = " have different types: `";
const uint8_t gm_s7vv7qoquqmn9[14] = "gm_wasi_entry";
const uint8_t gm_s81l00c48rg4v[26] = " places needs a tuple of ";
const uint8_t gm_s84hftqv0533g[5] = " << ";
const uint8_t gm_s85jtgs7au5b5[9] = "HexUpper";
const uint8_t gm_s866ofni9sb8u[7] = "double";
const uint8_t gm_s88u2nt2tl9hi[11] = "enum gm_dv";
const uint8_t gm_s8c49n38a2v0d[41] = "\000\000\000\000\000\000\000\000\033\000\000\000\000\000\000\000\246\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_s8cq7ej9hnrr0[12] = "gm_fault_sp";
const uint8_t gm_s8hhhdlan8pqr[8] = "->cx = ";
const uint8_t gm_s8i3r4s519jok[8] = "<core>/";
const uint8_t gm_s8n2bs6jikgsm[4] = "// ";
const uint8_t gm_s8n858pjjep3e[10] = "and {} {}";
const uint8_t gm_s8pjrp7ublsfs[171] = "a loop variable or a pattern binding is a copy, whose storage is gone or reused after its scope (\302\24715.1); point into the original (`&items[i]`), or keep the value instead";
const uint8_t gm_s8rm42n03sfss[4] = "\"\"\"";
const uint8_t gm_s8sgcefhqa4i0[43] = "static inline __attribute__((aligned(4))) ";
const uint8_t gm_s8sgunk80orlb[26] = "compiler/interp/consts.gm";
const uint8_t gm_s8vjpeo1vp60m[16] = "unknown --vcs `";
const uint8_t gm_s90aivk1p6or5[16] = "(*({} *)gm_out)";
const uint8_t gm_s92nfbpklold8[41] = "` is a url dependency; run `gamma fetch`";
const uint8_t gm_s95cbsclgo7gk[36] = "the literal {} does not fit in `{}`";
const uint8_t gm_s95gukpblkpua[33] = "`not` takes a `bool`, not `{}`{}";
const uint8_t gm_s97nfli622uqg[5] = "`: \012";
const uint8_t gm_s98novtk49ooh[28] = "static assertion failed: {}";
const uint8_t gm_s9cb6j5ok7bdk[3] = "\\r";
const uint8_t gm_s9e3360r5ee0o[7] = "inline";
const uint8_t gm_s9fseescjm8u4[25] = "`), which is not allowed";
const uint8_t gm_s9hi7i7cl3e2t[78] = "parameters of a type are always compile-time; declare them with `:`, not `::`";
const uint8_t gm_s9indmpqvpug4[58] = "` is a compile-time value parameter, but a type was given";
const uint8_t gm_s9jqgpofojve4[35] = "`@stack_size({})` has no bound: {}";
const uint8_t gm_s9kudvjpckcsm[10] = "({} % {})";
const uint8_t gm_s9o2c36nosv54[41] = "float to integer conversion out of range";
const uint8_t gm_s9oarmm2oe3id[41] = "\000\000\000\000\000\000\000\000\032\000\000\000\000\000\000\000J\001\000\000\035\000\000\000\000\000\000\000\000\000\000\000\017\000\000\000\000\000\000\000";
const uint8_t gm_s9oo6ammv0sch[17] = "reached through ";
const uint8_t gm_s9rm2mbmo3nku[7] = "a type";
const uint8_t gm_sa29cuhpvu442[54] = "cannot use {}: No such file or directory (os error 2)";
const uint8_t gm_sa2e14pm9g0bb[17] = "\000\000\000\000\000\000\000\000\006\000\000\000\000\000\000\000";
const uint8_t gm_sa7c621p9d7mr[13] = ".cache/gamma";
const uint8_t gm_sa7chm2nt7fsk[27] = "make the binding immutable";
const uint8_t gm_sa8gjfvck9d40[30] = ", .kind = GM_LIT, .clauses = ";
const uint8_t gm_sa8mtcad5rfmf[26] = "after the constraint name";
const uint8_t gm_sa9pve8r9407o[35] = "passing `{}` by value to or from C";
const uint8_t gm_saa0ue0fi38t8[15] = "Is a directory";
const uint8_t gm_sabdpctdijia8[41] = "\000\000\000\000\000\000\000\000\025\000\000\000\000\000\000\000~\000\000\000\014\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_saeed1dnph0jk[52] = "a string pattern needs a `string`, but this is `{}`";
const uint8_t gm_sajhc6og5bv8v[59] = "invalid unicode escape: expected 1 to 6 hexadecimal digits";
const uint8_t gm_sajso5sujsruj[47] = "; the lanes of a mask are complemented by `~m`";
const uint8_t gm_san68r6np95il[6] = "<std>";
const uint8_t gm_saqh6vgbmf8m2[153] = "a result is not a union: match its error with `!e` (or `!.Variant`) and its success value with a plain pattern, as in `!e => \342\200\246` and `v => \342\200\246` (\302\2479.3)";
const uint8_t gm_sat80i3p7mq2f[26] = "TOML parse error at line ";
const uint8_t gm_saveva62qedei[8] = " + i * ";
const uint8_t gm_sb11lrfd85gub[14] = "` applies to ";
const uint8_t gm_sb22ajpcscr7v[41] = "#if defined(__clang__) || __GNUC__ >= 12";
const uint8_t gm_sb6smklmjl60o[7] = "detail";
const uint8_t gm_sb745qqv6kdrg[11] = "serverInfo";
const uint8_t gm_sb895t6tk3m9h[6] = ") || ";
const uint8_t gm_sb8i7j9s2n6jl[8] = "gm_cont";
const uint8_t gm_sb9bgabg66tgj[42] = " left the program failing to compile, so ";
const uint8_t gm_sba0bp6ej30io[30] = "internal error: fiber aborted";
const uint8_t gm_sba60r64q4l0i[31] = ")(-9223372036854775807LL - 1))";
const uint8_t gm_sbanjlvk3n14i[4] = "sat";
const uint8_t gm_sbb1ri5er9djq[4] = "get";
const uint8_t gm_sbdno1ojbbrbs[45] = "`{}` is not a git dependency of this package";
const uint8_t gm_sbgbo6kv938k3[19] = "relatedInformation";
const uint8_t gm_sc0mj6111f7cc[10] = "pat-or : ";
const uint8_t gm_sc4hsc9atc69r[33] = "a package entry needs a string `";
const uint8_t gm_sc7ertn8kmtl3[4] = "2.0";
const uint8_t gm_scbch1b3qo8hr[9] = "shutdown";
const uint8_t gm_scbj8d61i03he[14] = "GIT_WORK_TREE";
const uint8_t gm_scc54gv4a7k04[6] = "errno";
const uint8_t gm_scds626ikjji4[13] = "gm_mode_kind";
const uint8_t gm_scduvjunh0oei[9] = "len = {}";
const uint8_t gm_scgahod5hue9b[56] = "`expect_eq` takes the actual value and the expected one";
const uint8_t gm_schmrvl8mk83l[9] = "watchers";
const uint8_t gm_schvua5hkahp9[16] = "activeParameter";
const uint8_t gm_scj618tc62815[15] = "invalid number";
const uint8_t gm_scmgj2ccom9v4[40] = "every branch of this `if` does the same";
const uint8_t gm_scq06n33arhgv[5] = "`\", ";
const uint8_t gm_scqt00otrhg1t[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\017\001\000\000\025\000\000\000\000\000\000\000\000\000\000\000\004\000\000\000\000\000\000\000";
const uint8_t gm_sd7vgd2pm42gk[14] = "op_div_scalar";
const uint8_t gm_sdaknqn35hokt[9] = "download";
const uint8_t gm_sdam23bqvafqn[18] = "kind = GM_K_SLICE";
const uint8_t gm_sdc36oketvdk3[4] = "op ";
const uint8_t gm_sdf1o8jga4hc6[38] = "the operation `{}` can only be called";
const uint8_t gm_sdg7s5c89ve6u[64] = "` names a type; a local may not have the name of a type (\302\2474.6)";
const uint8_t gm_sdgdb59na1854[23] = "the foreign variable `";
const uint8_t gm_sdhddtlu453l5[13] = "((__int128)(";
const uint8_t gm_sdhelph0q09tv[5] = "));\012";
const uint8_t gm_sdhns4s61djoc[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000}\000\000\000\021\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sdi3k3oc2kq9i[15] = "` is recursive";
const uint8_t gm_sdo1hnokt0mq8[17] = "lib/std/fs/fs.gm";
const uint8_t gm_sdqgviehbfoad[6] = ".rt.c";
const uint8_t gm_sdrbnq4aduai5[16] = "gm_clause_thunk";
const uint8_t gm_sdvp5f4riom9f[3] = "&a";
const uint8_t gm_se006k0nbfdeg[8] = "riscv64";
const uint8_t gm_se0plntg0il3g[14] = "->env = env; ";
const uint8_t gm_se2qf1ipmcha3[26] = "No such file or directory";
const uint8_t gm_se7kda46rm021[107] = "`needs not` is only for foreign functions (\302\24719.2); a Gamma function without `needs` already needs nothing";
const uint8_t gm_se8v3b7tfvt0e[23] = "this loop's value is `";
const uint8_t gm_sej6t944fna3a[61] = "the {} has no parameter named `{}` (supplied by this spread)";
const uint8_t gm_sekn24oquoo60[9] = "gm_frame";
const uint8_t gm_selatfetsf85i[10] = "range_len";
const uint8_t gm_sem2htac3j4np[66] = "` instantiates itself with ever-larger type arguments (more than ";
const uint8_t gm_sepmad5fu1s0n[8] = "--allow";
const uint8_t gm_ser747td9jt8d[41] = "variant `{}` needs a payload: `.{}(\342\200\246)`";
const uint8_t gm_sf44a6nc03ff5[10] = "a complex";
const uint8_t gm_sf6pbim706kre[4] = "zig";
const uint8_t gm_sfali8nl85hc9[25] = "this `#bits` field value";
const uint8_t gm_sfbip2dg905u8[7] = "gm_veh";
const uint8_t gm_sfbr39r30psvt[4] = "v{}";
const uint8_t gm_sfc0aek8lt25a[5] = "gm_f";
const uint8_t gm_sfd0mn3sol25u[4] = "Abs";
const uint8_t gm_sfdj0c5vltira[31] = "`{}` has no element named `{}`";
const uint8_t gm_sfeo7g7huf2q1[17] = "a pointer offset";
const uint8_t gm_sfhct1r6m1qqo[11] = "gm_ld{}_{}";
const uint8_t gm_sfjsmf1lebgp7[14] = "parse_pattern";
const uint8_t gm_sfk3r5va3vobm[5] = ".env";
const uint8_t gm_sfna4d5mnmoa8[30] = "merge the `else` and the `if`";
const uint8_t gm_sfpeqbitcbq42[6] = "-tvzf";
const uint8_t gm_sfr85ju00e9hk[138] = "a line that starts with `.name` continues the previous line (\302\2472.8); to begin a new expression with a variant, name its type: `T.Variant`";
const uint8_t gm_sfusuld9q5ekv[44] = "`, so it cannot be a type argument (\302\24716.1)";
const uint8_t gm_sg1s2lngtt92k[13] = "; if (ml == ";
const uint8_t gm_sg550i4fp569j[55] = "digit separators `_` must be single and between digits";
const uint8_t gm_sg7of7eks8a5u[7] = "method";
const uint8_t gm_sg7ronlh5k7js[84] = "; `free` releases a slice from `make`: release a value from `new` with `destroy(p)`";
const uint8_t gm_sgb12qbh3gevi[5] = "[i];";
const uint8_t gm_sgc7crh43q9tj[6] = "` in ";
const uint8_t gm_sgcku4bq4407l[19] = "`; use one of them";
const uint8_t gm_sgcporqt2gqqg[16] = " deep) (\302\24716.7)";
const uint8_t gm_sgepo3aen3iei[15] = "gm_susp_unlink";
const uint8_t gm_sgfrcimthamdn[10] = "__s390x__";
const uint8_t gm_sgfufhs3psibd[4] = "Exp";
const uint8_t gm_sgh1o65rai9bu[8] = "bits = ";
const uint8_t gm_sghh3jr7dlapd[66] = "the template has {}, but {} given (in an instantiation used here)";
const uint8_t gm_sgi6la00bpjt2[15] = "`{}` is a copy";
const uint8_t gm_sgisth1q9gntc[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000y\000\000\0000\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sgjv9g1a4rlf5[5] = " != ";
const uint8_t gm_sgq8hfs47hogb[9] = "(a, b);\012";
const uint8_t gm_sgukm1evmrjal[4] = "\", ";
const uint8_t gm_sh00ruc3m501e[42] = "cannot infer the result type of `!e` here";
const uint8_t gm_sh16qm7deqse5[7] = "{} a{}";
const uint8_t gm_sh4r0fq5alj61[14] = "needless-bool";
const uint8_t gm_sh57gpir0vvh2[24] = "signed integer or float";
const uint8_t gm_shbaullahutpe[37] = "a tuple type cannot contain a spread";
const uint8_t gm_shda201svb9eg[119] = "field `{}` of a `#bits` struct must be `bool`, an integer, an enum with an explicit tag type or another `#bits` struct";
const uint8_t gm_shhtc33mlfd4u[31] = "the name of a foreign function";
const uint8_t gm_shhugap3t1rkg[41] = "`#soa` needs a struct with fields, and `";
const uint8_t gm_shl6ef6if4a86[80] = "function returning `{}` can reach the end of its body without returning a value";
const uint8_t gm_shm91i3u3uv6p[5] = "\334\005\000\000";
const uint8_t gm_shnp9b6k2mab8[5] = "list";
const uint8_t gm_shoiushfjjjs8[9] = "get on `";
const uint8_t gm_shrc3j552p60m[9] = "SeekFrom";
const uint8_t gm_shrq42dntcbrd[19] = "kind = GM_K_RESULT";
const uint8_t gm_shukorb0nk4kg[7] = "triple";
const uint8_t gm_si2nsakslqgpp[16] = "    return s \077 ";
const uint8_t gm_sidpdvffdra02[14] = "out of memory";
const uint8_t gm_sif59l16kubo3[17] = "`{}` requires {}";
const uint8_t gm_sif5vtcq0ji03[17] = "\000\000\000\000\000\000\000\000J\002\000\000\000\000\000\000";
const uint8_t gm_sig2adm7e9t55[79] = "the template numbers its values, so this spread must be a tuple of them, not `";
const uint8_t gm_sighahslvsc1i[11] = "` (\302\24712.7)";
const uint8_t gm_sigup1gvr7a13[5] = " && ";
const uint8_t gm_sihdt3ld7f53j[16] = "GIT_INDEX_FILE=";
const uint8_t gm_sihs6d097ocev[5] = "Rand";
const uint8_t gm_sikpqtja8pqpv[3] = "}\012";
const uint8_t gm_siooblm685vmo[16] = "self-comparison";
const uint8_t gm_siotufd8s66oj[21] = "{} passed, {} failed";
const uint8_t gm_siqfg68f4ud6j[16] = " gm_r = F.ret; ";
const uint8_t gm_sj4cbut2umg1f[35] = "compile-time evaluation failed: {}";
const uint8_t gm_sj7cc74b29dv3[14] = "WASI_SDK_PATH";
const uint8_t gm_sj9t4d2ku5m56[47] = "this handler has no clause for the operation `";
const uint8_t gm_sjac221bnk353[8] = " #bits(";
const uint8_t gm_sjb8gf8n3vsft[42] = "this continuation needs a value of type `";
const uint8_t gm_sjbhqu0o8ebub[6] = "tests";
const uint8_t gm_sjdk7e5fs4top[6] = ") >= ";
const uint8_t gm_sje4rbc88csdu[7] = "X86_64";
const uint8_t gm_sjgrq0dv175qo[30] = "calls to foreign functions (`";
const uint8_t gm_sjil3d717dq6h[63] = "a format spec has either `0` padding or an alignment, not both";
const uint8_t gm_sjlktf1otofg9[10] = "new_frame";
const uint8_t gm_sjmhgc2b6rnks[10] = "(void *)&";
const uint8_t gm_sjobl2qgsrpl6[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\243\001\000\000\015\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sjpr4fvui63qj[13] = "(uint64_t){}";
const uint8_t gm_sjs62u07kjioq[15] = "default: goto ";
const uint8_t gm_sjtfkltao8c2o[49] = "internal error: a `ctl` clause finished normally";
const uint8_t gm_sjvb17co9jlff[15] = "<li><a href=\"#";
const uint8_t gm_sk0qegfrnee00[4] = "{}u";
const uint8_t gm_sk25jsmieu0fo[41] = "\000\000\000\000\000\000\000\000\031\000\000\000\000\000\000\000\223\000\000\0001\000\000\000\000\000\000\000\000\000\000\000\014\000\000\000\000\000\000\000";
const uint8_t gm_sk2nrl11c01gj[20] = "cannot write {}: {}";
const uint8_t gm_skbgm82aqj3q0[6] = "Fence";
const uint8_t gm_skq2uj9oi1243[41] = "internal error: no driver for this fiber";
const uint8_t gm_skrdl5pdcve8g[43] = "(gm_node *cx, const void *d, uintptr_t n, ";
const uint8_t gm_skv53e6m2c88s[10] = "a literal";
const uint8_t gm_sl5jkfh6104lp[5] = "file";
const uint8_t gm_sl7doakbaoimv[7] = "this `";
const uint8_t gm_sld9l7nonn4o9[17] = " && env != NULL;";
const uint8_t gm_slgbfnvgul4q1[7] = "\"\"\"\012{}";
const uint8_t gm_slmlefe4cpf5e[15] = "(..x)` (\302\2476.5)";
const uint8_t gm_slopv2r8r4od2[5] = "<li>";
const uint8_t gm_sltejl1fekbag[6] = ", &ev";
const uint8_t gm_sltjg0sbhbo73[13] = "static bool ";
const uint8_t gm_slu92ianijdhp[7] = "deinit";
const uint8_t gm_sm56lhobvb6g5[6] = "names";
const uint8_t gm_sm7179tmr0nhi[27] = "this compile-time argument";
const uint8_t gm_sm7hp6cgh4jle[6] = "write";
const uint8_t gm_sm8uf6b4m0n7p[8] = "unit-{}";
const uint8_t gm_sma6ellkcmke6[26] = "` are not ordered (\302\2477.5)";
const uint8_t gm_smacop0e0umsg[257] = "\230/\212B\221D7q\317\373\300\265\245\333\265\351[\302V9\361\021\361Y\244\202\077\222\325^\034\253\230\252\007\330\001[\203\022\276\2051$\303}\014Ut]\276r\376\261\336\200\247\006\334\233t\361\233\301\301i\233\344\206G\276\357\306\235\301\017\314\241\014$o,\351-\252\204tJ\334\251\260\\\332\210\371vRQ>\230m\3061\250\310'\003\260\307\177Y\277\363\013\340\306G\221\247\325Qc\312\006g))\024\205\012\267'8!\033.\374m,M\023\0158STs\012e\273\012jv.\311\302\201\205,r\222\241\350\277\242Kf\032\250p\213K\302\243Ql\307\031\350\222\321$\006\231\326\2055\016\364p\240j\020\026\301\244\031\010l7\036LwH'\265\274\2604\263\014\0349J\252\330NO\312\234[\363o.h\356\202\217toc\245x\024x\310\204\010\002\307\214\372\377\276\220\353lP\244\367\243\371\276\362xq\306";
const uint8_t gm_smb8nsuj9ie93[15] = "untyped-float ";
const uint8_t gm_smbgfcmcr0r8n[11] = "` (\302\24717.4)";
const uint8_t gm_smbls870q1nm0[16] = "integer literal";
const uint8_t gm_smhjrt0kub63u[15] = "gm_stack_limit";
const uint8_t gm_smk6fmrgs1hnc[6] = "gm_hl";
const uint8_t gm_smpmbscbko3gk[4] = "mul";
const uint8_t gm_smqhtg33814q9[23] = "this assertion message";
const uint8_t gm_smror2m9tsk7v[8] = "branch=";
const uint8_t gm_sn35ih51dmdii[45] = "` takes no arguments and is written once: `#";
const uint8_t gm_sn4utba4jl66l[9] = "rev `{}`";
const uint8_t gm_snbb2gfh3k17u[26] = "message body is too large";
const uint8_t gm_sncbtqrmp1734[14] = "`!` on values";
const uint8_t gm_snjq35j2j9d3c[113] = "`{}` and `{}` might be the same effect for some type arguments, so performing either would be ambiguous (\302\24716.6)";
const uint8_t gm_so05t2goj6895[24] = "])(n->env, n->hctx, n, ";
const uint8_t gm_so2ihtmk3e8kf[17] = " is matched here";
const uint8_t gm_so43rnba8j5pb[35] = "(({})(({}) & ((({})1 << {}) - 1)))";
const uint8_t gm_so44j4cr69vhs[16] = "missing newName";
const uint8_t gm_so5kmn66hbf25[22] = "print the syntax tree";
const uint8_t gm_so686pdrsalfh[587] = "Enter an expression to print its value, a statement or a definition to add it to the session.\012An input continues on the next line while brackets are open or it is incomplete.\012\012  :help             this text (:h, :\077)\012  :quit             leave (:q, :exit; so does end of input)\012  :type <expr>      (:t) the type of an expression, which is not run unless it must be\012  :list             the definitions and variables of the session\012  :load <file>      add the declarations of a file\012  :reset            forget everything\012  :cancel           (on a continuation line) discard the input so far\012";
const uint8_t gm_so77flgm8klar[52] = "this `match` is not exhaustive: `{}` is not covered";
const uint8_t gm_so7qmna0fo4so[6] = "#line";
const uint8_t gm_so81f3f3mbv7o[54] = "string values must be quoted, expected literal string";
const uint8_t gm_soah6d9unb7uo[32] = "static GM_HUGE_FN __attribute__";
const uint8_t gm_sobecpce3c56n[35] = "`,` or a newline after the variant";
const uint8_t gm_soh6rol6f5r5k[27] = "invalid unicode code point";
const uint8_t gm_sokol7c4qt12p[6] = ", 0, ";
const uint8_t gm_sokruug51vi6k[11] = "gm_rt_argc";
const uint8_t gm_soogmlptfncs1[12] = "build_write";
const uint8_t gm_soreld657hea2[25] = "a number, for `+` or `0`";
const uint8_t gm_sos7rilr21per[7] = "rename";
const uint8_t gm_sottk452c14qm[56] = "`#link_name` takes the symbol name: `#link_name(\"sym\")`";
const uint8_t gm_sp13ju5l53585[24] = "replace the `if` with `";
const uint8_t gm_sp21r812mg46a[4] = "///";
const uint8_t gm_sp4l42tpo6btg[11] = "refs/heads";
const uint8_t gm_sp625mljmvm7d[11] = "parse_item";
const uint8_t gm_sp6f2rgoi8toe[7] = "cflags";
const uint8_t gm_sp75410cl55v2[13] = "enum gm_dv{}";
const uint8_t gm_sp7ec3ksfdilk[5] = ".git";
const uint8_t gm_spa5q1834e71s[49] = "); gamma.lock was edited or the cached checkout ";
const uint8_t gm_spajg3q27hr58[15] = ") / (intptr_t)";
const uint8_t gm_spg4d1df6jnji[7] = "type `";
const uint8_t gm_spj9rva8kb5ar[3] = " {";
const uint8_t gm_spjfgtrrvac57[14] = "cannot read `";
const uint8_t gm_spm4139orpkvm[10] = ") return ";
const uint8_t gm_spp0mb1aidgi1[13] = " :: import \"";
const uint8_t gm_spskd7qirpoe3[25] = "expected a type argument";
const uint8_t gm_sq05bfmao5v8n[47] = "gm_fprop:\012    gm_fiber_finish(fb);\012#undef F\012}\012";
const uint8_t gm_sq1rqv8ou3vsv[14] = "release_facts";
const uint8_t gm_sq3jr80otu7tj[3] = ")~";
const uint8_t gm_sq5p8t05ccn5h[24] = "expected an effect name";
const uint8_t gm_sq74a8ckacjn2[41] = "`{}` is not a C-compatible type (\302\24719.1)";
const uint8_t gm_sq99f4u8gmi6n[28] = "\012Next steps:\012    gamma run ";
const uint8_t gm_sqa6gfa8omk7o[4] = "i32";
const uint8_t gm_sqcj2s8m7mpqj[38] = "(uint8_t)(__builtin_signbit({}) != 0)";
const uint8_t gm_sqdjjj1odavib[34] = "`continue` can only target a loop";
const uint8_t gm_sqeho958d04c0[17] = "gm_thread_detach";
const uint8_t gm_sqen1asili489[52] = "tag value {} of `{}` does not fit the tag type `{}`";
const uint8_t gm_sqfc0qpl56rfm[7] = "length";
const uint8_t gm_sqh06nbksvl8i[17] = "package `{}`: {}";
const uint8_t gm_sqjlosotosjfp[18] = ")) return false;\012";
const uint8_t gm_sqnathr6pbque[38] = "{} (\302\261 {:.1}%, {} samples of {} runs)";
const uint8_t gm_sqnuqut1dl3fi[48] = "internal error: get on a value without elements";
const uint8_t gm_sqo6bs5b6kae9[8] = "gm_with";
const uint8_t gm_sqqtsglgb4lu3[28] = "cannot ask the C compiler `";
const uint8_t gm_sqtgbqlca4c1g[25] = "gm_asyncify_start_unwind";
const uint8_t gm_squ1rl0dbtrtr[59] = "the handler of `{}` performed here is not known statically";
const uint8_t gm_sr3uqaosat41v[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000=\000\000\000\011\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_sr499ju9p7ks2[23] = "pre-release identifier";
const uint8_t gm_sr899mamktpvo[7] = "uninit";
const uint8_t gm_sreprot2s508e[3] = ":q";
const uint8_t gm_srhrn3fa70ni4[38] = "__atomic_load_n({}, __ATOMIC_RELAXED)";
const uint8_t gm_sri6fceeqvrql[19] = "\012pub arch :: Arch.";
const uint8_t gm_srj5ohb3f61b7[14] = "gm_fiber_slab";
const uint8_t gm_srj9vrlard0qi[14] = "#soa []const ";
const uint8_t gm_srll3895bp2kd[12] = "; i++) h = ";
const uint8_t gm_srlpdrobo1i0e[38] = "the function has no parameter named `";
const uint8_t gm_srp6tpaiaeunu[8] = "->env, ";
const uint8_t gm_srplcc0p49iui[9] = "contents";
const uint8_t gm_srq5utruoa2v3[28] = " *)req; gm_node *n = r->n; ";
const uint8_t gm_srru5vqet7hl3[11] = "instrument";
const uint8_t gm_srssp1uhlkrll[20] = "expected `,` or `}`";
const uint8_t gm_ss2v2ursd8pms[4] = "n{}";
const uint8_t gm_ss42hvrprenkk[26] = "; { gm_node *f = gm_find(";
const uint8_t gm_ss558hlhv1kah[31] = "` is not a valid C symbol name";
const uint8_t gm_ss66qg3t949dp[5] = "\320\007\000\000";
const uint8_t gm_ssc5ahdetbl48[17] = "gm_altstack_init";
const uint8_t gm_sse16dr2qj0im[3] = "pr";
const uint8_t gm_ssfg877iegnma[4] = "- `";
const uint8_t gm_ssga7g3v3nnl7[10] = "benchmark";
const uint8_t gm_ssj8r83l87dr2[16] = "fs_set_modified";
const uint8_t gm_ssk8mqd1u6nn7[52] = "`in` needs a range, array, slice or tuple, not `{}`";
const uint8_t gm_sslm2oq18gln3[6] = " (\302\261 ";
const uint8_t gm_ssmvuscdpkf2v[41] = "\000\000\000\000\000\000\000\000\020\000\000\000\000\000\000\000\216\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\010\000\000\000\000\000\000\000";
const uint8_t gm_ssq0semhq7h0c[13] = "gm_sys_write";
const uint8_t gm_ssqt22rph287b[4] = "i64";
const uint8_t gm_ssrcn9mu9bp3e[19] = "gm_scratch_release";
const uint8_t gm_ssv5povjldd7c[3] = ")\012";
const uint8_t gm_st3c9ev3jkdrf[32] = "{}({}.code, {}.env, {}, {}, {})";
const uint8_t gm_st803nqm7iok0[17] = "to close `cont(`";
const uint8_t gm_st9f93lco88fg[3] = "/.";
const uint8_t gm_stafskn6bv24f[2] = "d";
const uint8_t gm_ste1ujg4tll4q[10] = "gm_thread";
const uint8_t gm_stevggi5tvq7c[18] = "instantiated here";
const uint8_t gm_stlhqhje21tdn[97] = "\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\015\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\024\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000 \000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\016\000\000\000\000\000\000\000";
const uint8_t gm_stmg18u2tvso0[18] = "gm_frame_overflow";
const uint8_t gm_sto310q1gi1de[12] = "underscore\012";
const uint8_t gm_sto6ssj6r66e3[30] = "conflicting requirements for ";
const uint8_t gm_sto8li44rv2ou[8] = "a tuple";
const uint8_t gm_stshdb5m6rl63[5] = "] \077 ";
const uint8_t gm_stsr1tpfadro2[15] = "/proc/self/exe";
const uint8_t gm_stu0uvs5p21oc[17] = "cannot read CC: ";
const uint8_t gm_stugmrojdno85[6] = "const";
const uint8_t gm_stvci3fthpt8h[47] = "gm_trap(\"no match arm matched the scrutinee\", ";
const uint8_t gm_su41i9n9tujml[14] = "gm_next_entry";
const uint8_t gm_sua0n2tdbtos7[40] = "excessive number of version comparators";
const uint8_t gm_sub7l8h1sjc33[12] = "` is used (";
const uint8_t gm_suc0sjksvr6j7[9] = ", GM_LD(";
const uint8_t gm_sucoj11hr978o[14] = "the operation";
const uint8_t gm_sue4ionbe9lgh[21] = "compiler/eval/ops.gm";
const uint8_t gm_sujqb5peare4g[6] = "not (";
const uint8_t gm_suklu1i9clb5l[48] = "invalid memory access: null pointer dereference";
const uint8_t gm_summega2vonc9[6] = "{}{}\012";
const uint8_t gm_suq3cd2v0vpun[41] = "\000\000\000\000\000\000\000\000\035\000\000\000\000\000\000\000_\000\000\000\020\000\000\000\000\000\000\000\000\000\000\000\012\000\000\000\000\000\000\000";
const uint8_t gm_suqsoolo29or4[24] = "ratio: zero denominator";
const uint8_t gm_suuq26jjqhul4[7] = "int8_t";
const uint8_t gm_suvpcmvpe5152[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000m\000\000\000\012\000\000\000\000\000\000\000\000\000\000\000\011\000\000\000\000\000\000\000";
const uint8_t gm_sv397f5vej3g7[30] = "cannot compare `{}` with `{}`";
const uint8_t gm_sv44r8pinka4e[13] = "needless-pub";
const uint8_t gm_sv8lr20bqpljh[12] = "`{}` clause";
const uint8_t gm_svbftsm94r0bv[7] = "(\342\200\246)`";
const uint8_t gm_svdodmni8et3b[15] = "this condition";
const uint8_t gm_sveguffvp7vd6[15] = ")GM_PTR_UNTAG(";
const uint8_t gm_svfn64lgobnrm[48] = "`: a method call must find exactly one (\302\2477.10)";
const uint8_t gm_svkk03nujpn4k[8] = "gm_pow_";
const uint8_t gm_svl13be5ca8rt[5] = "Wasi";
const uint8_t gm_svs91tvrok38m[7] = " = 0; ";
const uint8_t gm_svutc5hf6cgiv[2] = "e";
const uint8_t gm_svv8nt9tamumf[41] = "\000\000\000\000\000\000\000\000\030\000\000\000\000\000\000\000C\001\000\000.\000\000\000\000\000\000\000\000\000\000\000\007\000\000\000\000\000\000\000";
uint8_t gm_c1jun9snbr4[7] __attribute__((aligned(1)));
uint8_t gm_c1qn9qrrluj[17] __attribute__((aligned(1)));
uint8_t gm_c23mu1onplu[3] __attribute__((aligned(1)));
uint8_t gm_c30d0vfr4ha[8] __attribute__((aligned(1)));
uint8_t gm_c40fejj7lfq[17] __attribute__((aligned(1)));
uint8_t gm_c4bbipnshg6[17] __attribute__((aligned(1)));
uint8_t gm_c4tbnbi68ao[320] __attribute__((aligned(8)));
uint8_t gm_c55218gtcr0[448] __attribute__((aligned(8)));
uint8_t gm_c5dhf54lj9h[1] __attribute__((aligned(1)));
uint8_t gm_c5led8t7730[39] __attribute__((aligned(1)));
uint8_t gm_c5lrgm5j673[3] __attribute__((aligned(1)));
uint8_t gm_c6c0gfng8q4[44] __attribute__((aligned(1)));
uint8_t gm_c6d4g136jr7[8] __attribute__((aligned(1)));
uint8_t gm_c7fs6k1a08r[13] __attribute__((aligned(1)));
uint8_t gm_c7s3i9thp0k[12] __attribute__((aligned(1)));
uint8_t gm_c7svj0uc8qe[4] __attribute__((aligned(1)));
uint8_t gm_c9801fpr5pe[2] __attribute__((aligned(1)));
uint8_t gm_c9b4lh7gft4[4] __attribute__((aligned(4)));
uint8_t gm_cbbu3qvtp3q[8] __attribute__((aligned(8)));
uint8_t gm_cc0dfelltlv[10] __attribute__((aligned(1)));
uint8_t gm_cck6svt8s4a[51] __attribute__((aligned(1)));
uint8_t gm_cfaubb414id[18] __attribute__((aligned(1)));
uint8_t gm_cfb7p1dd1fr[11] __attribute__((aligned(1)));
uint8_t gm_cga7n0g3amu[8] __attribute__((aligned(8)));
uint8_t gm_cgd1khc5sal[24] __attribute__((aligned(8)));
uint8_t gm_ch33skqrgi0[5] __attribute__((aligned(1)));
uint8_t gm_ch6j88vtkil[2] __attribute__((aligned(1)));
uint8_t gm_ci51f79gd9k[8] __attribute__((aligned(1)));
uint8_t gm_cka88d5t8lu[16] __attribute__((aligned(8)));
uint8_t gm_cl03rhutns3[112] __attribute__((aligned(8)));
uint8_t gm_clacg9fr6j2[58] __attribute__((aligned(1)));
uint8_t gm_cmq9in3i5gq[3] __attribute__((aligned(1)));
uint8_t gm_cmtljgd99p9[15] __attribute__((aligned(1)));
uint8_t gm_cmu8f7egfr6[2] __attribute__((aligned(1)));
uint8_t gm_coj5ig90obd[5] __attribute__((aligned(1)));
uint8_t gm_cojk58d4vkf[16] __attribute__((aligned(8)));
uint8_t gm_cp10508qfq2[9] __attribute__((aligned(1)));
uint8_t gm_cp453i0tag9[10] __attribute__((aligned(1)));
uint8_t gm_cqfr6mggm8j[15] __attribute__((aligned(1)));
uint8_t gm_cqmh9ctmru3[8] __attribute__((aligned(1)));
uint8_t gm_cquib4l6q11[8] __attribute__((aligned(1)));
uint8_t gm_crhue63gorj[4] __attribute__((aligned(4)));
uint8_t gm_crjl9v1c7hv[4] __attribute__((aligned(1)));
uint8_t gm_cru7nui1vli[4] __attribute__((aligned(1)));
uint8_t gm_csoii12j71c[5] __attribute__((aligned(1)));
uint8_t gm_ctrunud43if[10] __attribute__((aligned(1)));
uint8_t gm_cu9odmhbpt4[18] __attribute__((aligned(1)));
uint8_t gm_cvlvde3pjno[11] __attribute__((aligned(1)));
uint8_t gm_cvte04sgh18[13] __attribute__((aligned(1)));
uint8_t gm_k2rn6j11rv5[40] __attribute__((aligned(8)));
uint8_t gm_k4ha9p0b2mp[40] __attribute__((aligned(8)));
uint8_t gm_k4j26j8fh8s[40] __attribute__((aligned(8)));
uint8_t gm_k4tn3ii07uh[40] __attribute__((aligned(8)));
uint8_t gm_k62pju45ug1[40] __attribute__((aligned(8)));
uint8_t gm_k75gp9b9r49[40] __attribute__((aligned(8)));
uint8_t gm_k9epus5iul2[40] __attribute__((aligned(8)));
uint8_t gm_k9o9iqv20c0[40] __attribute__((aligned(8)));
uint8_t gm_kaa2flncdji[40] __attribute__((aligned(8)));
uint8_t gm_kbng43moaud[40] __attribute__((aligned(8)));
uint8_t gm_kdjk0ti7atv[40] __attribute__((aligned(8)));
uint8_t gm_ki1ml5fl8pd[40] __attribute__((aligned(8)));
uint8_t gm_kijoa5b5euq[40] __attribute__((aligned(8)));
uint8_t gm_kl3hhlfsksr[40] __attribute__((aligned(8)));
uint8_t gm_klqd6h436uv[40] __attribute__((aligned(8)));
uint8_t gm_kq0m5gkhfc4[40] __attribute__((aligned(8)));
uint8_t gm_kqu6q38jp8e[40] __attribute__((aligned(8)));
uint8_t gm_kr7fqi9m0ih[40] __attribute__((aligned(8)));
uint8_t gm_krlms0mdefs[40] __attribute__((aligned(8)));
uint8_t gm_ks0d3nb0eoq[40] __attribute__((aligned(8)));
uint8_t gm_ksnfevokhod[40] __attribute__((aligned(8)));
uint8_t gm_kvu1vd24o16[40] __attribute__((aligned(8)));
void *const gm_hl6hnpe3vbk7[] = {(void *)gm_f_for_body_v43n3ub};
void *const gm_hlh12hh1mjda[] = {(void *)gm_f_for_body_ivf5vgl};
void *const gm_hli2tfvgok6p[] = {(void *)gm_f_for_body_tprkpoe};
void *const gm_hlvanl0isvqe[] = {(void *)gm_f_for_body_5dma72c};

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
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; uint64_t f3; } gm_t1d8endhr9k72;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; uint64_t f4; } gm_t1h4kdi7gs4v5;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; uint64_t f3; gm_t3mf58gv9kf3n f4; } gm_t4c7ipjj9s3fp;
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; uint64_t f3; gm_b16_8 f4; uint64_t f5; uint64_t f6; } gm_t6s59i0k0msk8;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; uint64_t f4; } gm_tg23pq9a29gh1;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_b16_8 f2; uint64_t f3; } gm_thddbqb6snabm;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; uint64_t f4; uint64_t f5; } gm_tr4ummvbupakb;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t1d8endhr9k72) == 40 && _Alignof(gm_t1d8endhr9k72) == 8 && offsetof(gm_t1d8endhr9k72, f0) == 0 && offsetof(gm_t1d8endhr9k72, f1) == 8 && offsetof(gm_t1d8endhr9k72, f2) == 24 && offsetof(gm_t1d8endhr9k72, f3) == 32, "layout of `(u64, string, u64, u64)`");
_Static_assert(sizeof(gm_t1h4kdi7gs4v5) == 64 && _Alignof(gm_t1h4kdi7gs4v5) == 8 && offsetof(gm_t1h4kdi7gs4v5, f0) == 0 && offsetof(gm_t1h4kdi7gs4v5, f1) == 16 && offsetof(gm_t1h4kdi7gs4v5, f2) == 32 && offsetof(gm_t1h4kdi7gs4v5, f3) == 40 && offsetof(gm_t1h4kdi7gs4v5, f4) == 56, "layout of `(string, string, u64, string, u64)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t4c7ipjj9s3fp) == 56 && _Alignof(gm_t4c7ipjj9s3fp) == 8 && offsetof(gm_t4c7ipjj9s3fp, f0) == 0 && offsetof(gm_t4c7ipjj9s3fp, f1) == 16 && offsetof(gm_t4c7ipjj9s3fp, f2) == 24 && offsetof(gm_t4c7ipjj9s3fp, f3) == 32 && offsetof(gm_t4c7ipjj9s3fp, f4) == 40, "layout of `HashMap(TyKind, u32, null, null)`");
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t6s59i0k0msk8) == 72 && _Alignof(gm_t6s59i0k0msk8) == 8 && offsetof(gm_t6s59i0k0msk8, f0) == 0 && offsetof(gm_t6s59i0k0msk8, f1) == 8 && offsetof(gm_t6s59i0k0msk8, f2) == 24 && offsetof(gm_t6s59i0k0msk8, f3) == 32 && offsetof(gm_t6s59i0k0msk8, f4) == 40 && offsetof(gm_t6s59i0k0msk8, f5) == 56 && offsetof(gm_t6s59i0k0msk8, f6) == 64, "layout of `(u64, string, u64, u64, string, u64, u64)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { uint64_t f0; gm_t0mglksrkfa5e f1; } gm_t8ta482bl6be0;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tg23pq9a29gh1) == 64 && _Alignof(gm_tg23pq9a29gh1) == 8 && offsetof(gm_tg23pq9a29gh1, f0) == 0 && offsetof(gm_tg23pq9a29gh1, f1) == 16 && offsetof(gm_tg23pq9a29gh1, f2) == 32 && offsetof(gm_tg23pq9a29gh1, f3) == 48 && offsetof(gm_tg23pq9a29gh1, f4) == 56, "layout of `(string, string, string, u64, u64)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thddbqb6snabm) == 48 && _Alignof(gm_thddbqb6snabm) == 8 && offsetof(gm_thddbqb6snabm, f0) == 0 && offsetof(gm_thddbqb6snabm, f1) == 16 && offsetof(gm_thddbqb6snabm, f2) == 24 && offsetof(gm_thddbqb6snabm, f3) == 40, "layout of `(string, u64, string, u64)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tr4ummvbupakb) == 64 && _Alignof(gm_tr4ummvbupakb) == 8 && offsetof(gm_tr4ummvbupakb, f0) == 0 && offsetof(gm_tr4ummvbupakb, f1) == 16 && offsetof(gm_tr4ummvbupakb, f2) == 24 && offsetof(gm_tr4ummvbupakb, f3) == 32 && offsetof(gm_tr4ummvbupakb, f4) == 48 && offsetof(gm_tr4ummvbupakb, f5) == 56, "layout of `(string, u64, u64, string, u64, u64)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t4c7ipjj9s3fp f0; } gm_ttiitul0263ld;
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_t8ta482bl6be0) == 48 && _Alignof(gm_t8ta482bl6be0) == 8 && offsetof(gm_t8ta482bl6be0, f0) == 0 && offsetof(gm_t8ta482bl6be0, f1) == 8, "layout of `(i: usize, x: FieldDef)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; gm_t4c7ipjj9s3fp f2; gm_t4c7ipjj9s3fp f3; gm_t4c7ipjj9s3fp f4; gm_ttiitul0263ld f5; } gm_te68172gglous;
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_ttiitul0263ld) == 56 && _Alignof(gm_ttiitul0263ld) == 8 && offsetof(gm_ttiitul0263ld, f0) == 0, "layout of `Namer`");
typedef struct { uint64_t f0; gm_tckdobunaulee f1; } gm_t3l8idb2m6c1r;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_te68172gglous) == 304 && _Alignof(gm_te68172gglous) == 8 && offsetof(gm_te68172gglous, f0) == 0 && offsetof(gm_te68172gglous, f1) == 40 && offsetof(gm_te68172gglous, f2) == 80 && offsetof(gm_te68172gglous, f3) == 136 && offsetof(gm_te68172gglous, f4) == 192 && offsetof(gm_te68172gglous, f5) == 248, "layout of `Helpers`");
_Static_assert(sizeof(gm_t3l8idb2m6c1r) == 80 && _Alignof(gm_t3l8idb2m6c1r) == 8 && offsetof(gm_t3l8idb2m6c1r, f0) == 0 && offsetof(gm_t3l8idb2m6c1r, f1) == 8, "layout of `(i: usize, x: VariantDef)`");
struct gm_fr_bytewise_eq_e8jgutj {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_declare_helper_k8mi0a4 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_define_helper_c1utd1n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_eq_fn_5udvcnu {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* b */
    gm_tpautr4br7h3l l32; /* sd */
    gm_t0mglksrkfa5e l39; /* fd */
    gm_tckdobunaulee l47; /* v */
    gm_b16_8 ret;
};
struct gm_fr_hash_fn_nlk82uj {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* b */
    gm_tpautr4br7h3l l33; /* sd */
    gm_t0mglksrkfa5e l40; /* fd */
    gm_tckdobunaulee l49; /* var */
    gm_b16_8 ret;
};
struct gm_fr_helper_name_fob48de {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_less_fn_64um0ab {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* b */
    gm_tpautr4br7h3l l29; /* sd */
    gm_b16_8 ret;
};
struct gm_fr_new_helpers_4q783cn {
    gm_frame h;
    gm_te68172gglous ret;
};
bool gm_eqvs1pd373b1(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_bytewise_eq_e8jgutj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_declare_helper_k8mi0a4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6);
__attribute__((aligned(4))) gm_b0_1 gm_f_define_helper_c1utd1n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_eq_fn_5udvcnu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_39snre1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_get_gnmase9(void *gm_out, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_fn_nlk82uj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_hash_map_tl7apid(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_helper_name_fob48de(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_less_fn_64um0ab(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_new_helpers_4q783cn(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_new_namer_5q88o9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) uint8_t gm_f_nullable_pointer_7qfmn6o(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_po09vud(void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0kurvf5(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2tci68e(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_49bd3in(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_4l0f7de(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_551e4in(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_5stoou4(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6nhr3v9(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7sl1fre(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8ijr07s(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8tfqc0p(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_91uvm3b(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_96p75mj(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_aakaioo(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_avsehn1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_baasn56(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_bq1tl8a(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_c377pgo(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e15chue(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ed06dp1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_eu9q7ts(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_g25419f(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ho6u1fd(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_idp5df3(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_ikd2l7g(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_jgspi6j(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_k88klp2(void * a0, gm_t2lqu8jsj511d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_k9lbjqb(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_l1tdme1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_lblem56(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_m3i8l46(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_o50khi1(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_pqeomd8(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_pt06m3q(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_qvj9qj9(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_sais187(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_sbsltkj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_t9pa6sj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u9rtsjt(void * a0, const void * a1);
extern const uint8_t gm_s4gcag2e2r0cj[31];
extern const uint8_t gm_s81r46mpa8ra9[29];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_s9iu5gsqog7uc[5];
extern const uint8_t gm_sciq0a78hirud[25];
extern const uint8_t gm_sckmhri27q39l[53];
extern const uint8_t gm_sel1fj24qk32p[15];
extern const uint8_t gm_sfm0hseeqersg[25];
extern const uint8_t gm_sge6a56sqn6kj[21];
extern const uint8_t gm_sgn3gq6gfib7b[40];
extern const uint8_t gm_shm5oj4temhh6[33];
extern const uint8_t gm_shmmkvp83pr05[18];
extern const uint8_t gm_shn8mjp9kc2n2[9];
extern const uint8_t gm_sigmj6bktcbbh[80];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sisc4n92pk600[13];
extern const uint8_t gm_sor8flejv9lfq[67];
extern const uint8_t gm_sp2m434o2c1ha[3];
extern const uint8_t gm_sqg70d5uem21e[35];
extern const char gm_srckg9scnip7e8g[];
extern const uint8_t gm_sstcpkpuiippb[12];
extern const uint8_t gm_sug64ltbhov0m[19];
extern const uint8_t gm_sup5ecv5jiqbn[119];
extern const uint8_t gm_surrjo1qo53mh[3];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) uint8_t gm_f_bytewise_eq_e8jgutj(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bytewise_eq_e8jgutj F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* bits */
    uint64_t gm_l3; /* storage */
    GM_STACK_CHECK(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t5v8n6qrn4rh4 t3;
    gm_f_repr_53quqki((void *)&t3, (void *)NULL, cx, NULL, t1, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t5v8n6qrn4rh4 t4 = t3;
    uint8_t t5;
    uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t8 != ((uint8_t)0ULL)) goto mn7;
    uint32_t t9 = GM_LD(uint32_t, ((((uint8_t *)&t4) + 8) + 4));
    gm_l2 = t9;
    uint64_t t10 = GM_LD(uint64_t, ((((uint8_t *)&t4) + 8) + 8));
    gm_l3 = t10;
    uint32_t t11 = gm_l2;
    uint64_t t12 = (uint64_t)t11;
    uint64_t t13 = gm_l3;
    uint64_t r14; bool o15 = __builtin_mul_overflow(t13, ((uint64_t)8ULL), &r14);
    if (o15) gm_trap("integer overflow in `*`", gm_srckg9scnip7e8g, 145, 44);
    uint64_t t16 = r14;
    uint8_t t17 = ((t12 == t16));
    t5 = t17;
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn18:;
    gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 144, 62);
me6:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_declare_helper_k8mi0a4(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_b16_8 a3, gm_t88rcucg1b7uk a4, gm_b16_8 a5, gm_b16_8 a6) {
    struct gm_fr_declare_helper_k8mi0a4 F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    void * gm_l2; /* memo */
    gm_b16_8 gm_l3; /* kind */
    gm_t88rcucg1b7uk gm_l4; /* t */
    gm_b16_8 gm_l5; /* ret */
    gm_b16_8 gm_l6; /* params */
    gm_b16_8 gm_l7; /* name */
    GM_STACK_CHECK(&F, 341u);
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
    {
        void * t2 = gm_l0;
        void * t3 = gm_l1;
        gm_b16_8 t4 = gm_l3;
        gm_t88rcucg1b7uk t5 = gm_l4;
        gm_b16_8 t6 = gm_f_helper_name_fob48de((void *)NULL, cx, NULL, t2, t3, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t6;
    }
    {
        void * t7 = gm_l2;
        uint32_t t8 = GM_LD(uint32_t, (((uint8_t *)&gm_l4) + 0));
        gm_b16_8 t9 = gm_l7;
        gm_tftb1odc739t5 t10 = gm_f_put_po09vud(t7, t8, t9);
        gm_tftb1odc739t5 t11 = t10;
        if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srckg9scnip7e8g, 35, 5);
        gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
    }
    {
        void * t13 = gm_l0;
        void * t14 = (void *)(((uint8_t *)t13) + 0);
        gm_tfeab279c16rr t15;
        gm_b16_8 t16 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t15 + 0, t16);
        gm_b16_8 t17 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t15 + 16, t17);
        gm_b16_8 t18 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t15 + 32, t18);
        gm_tftb1odc739t5 t19 = gm_f_write_sbsltkj(t14, &t15);
        gm_tftb1odc739t5 t20 = t19;
        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srckg9scnip7e8g, 36, 5);
        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
    }
    {
        gm_b16_8 t22 = gm_l7;
        uint8_t t23 = 0;
        F.ret = t22;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_define_helper_c1utd1n(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4) {
    struct gm_fr_define_helper_c1utd1n F;
    void * gm_l0; /* h */
    gm_b16_8 gm_l1; /* name */
    gm_b16_8 gm_l2; /* ret */
    gm_b16_8 gm_l3; /* params */
    gm_b16_8 gm_l4; /* body */
    GM_STACK_CHECK(&F, 260u);
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
        void * t3 = (void *)(((uint8_t *)t2) + 40);
        gm_tfeab279c16rr t4;
        gm_b16_8 t5 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_b16_8 t7 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 32, t7);
        gm_tftb1odc739t5 t8 = gm_f_write_l1tdme1(t3, &t4);
        gm_tftb1odc739t5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srckg9scnip7e8g, 42, 5);
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 40);
        gm_b16_8 t13 = gm_l4;
        gm_tftb1odc739t5 t14 = gm_f_extend_0cs85gs(t12, t13);
        gm_tftb1odc739t5 t15 = t14;
        if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 1, gm_srckg9scnip7e8g, 43, 5);
        gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t15) + 1));
    }
    {
        void * t17 = gm_l0;
        void * t18 = (void *)(((uint8_t *)t17) + 40);
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sikpqtja8pqpv));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t20 = gm_f_extend_0cs85gs(t18, t19);
        gm_tftb1odc739t5 t21 = t20;
        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srckg9scnip7e8g, 44, 5);
        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_eq_fn_5udvcnu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_eq_fn_5udvcnu F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* name */
    uint64_t gm_l6; /* p */
    gm_b16_8 gm_l7; /* c */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* et */
    gm_b16_8 gm_l14; /* f */
    uint64_t gm_l15; /* len */
    gm_t88rcucg1b7uk gm_l16; /* elem */
    uint64_t gm_l17; /* esz */
    gm_b16_8 gm_l18; /* f */
    gm_t88rcucg1b7uk gm_l19; /* elem */
    uint64_t gm_l20; /* esz */
    gm_b16_8 gm_l21; /* f */
    gm_t88rcucg1b7uk gm_l22; /* inner */
    gm_b16_8 gm_l23; /* f */
    uint64_t gm_l24; /* flag */
    gm_t88rcucg1b7uk gm_l25; /* err */
    gm_t88rcucg1b7uk gm_l26; /* ok */
    gm_b16_8 gm_l27; /* fe */
    gm_b16_8 gm_l28; /* fo */
    gm_b16_8 gm_l29; /* offs */
    uint64_t gm_l30; /* tg */
    uint64_t gm_l31; /* pl */
    gm_t88rcucg1b7uk gm_l33; /* u */
    gm_b16_8 gm_l34; /* f */
    gm_b16_8 gm_l35; /* offs */
    gm_b16_8 gm_l36; /* <tmp> */
    uint64_t gm_l37; /* <tmp> */
    uint64_t gm_l38; /* i */
    gm_b16_8 gm_l40; /* f */
    gm_t88rcucg1b7uk gm_l41; /* tag */
    gm_b16_8 gm_l42; /* variants */
    gm_b16_8 gm_l43; /* offs */
    gm_b16_8 gm_l44; /* tc */
    gm_b16_8 gm_l45; /* <tmp> */
    uint64_t gm_l46; /* <tmp> */
    gm_t88rcucg1b7uk gm_l48; /* pt */
    gm_b16_8 gm_l49; /* f */
    GM_STACK_CHECK(&F, 4043u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 80);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        gm_th2lqbo77pvsc t6;
        gm_f_get_gnmase9((void *)&t6, t4, t5);
        gm_th2lqbo77pvsc t7 = t6;
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l3 = t8;
        {
            gm_b16_8 t10 = gm_l3;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l1;
        void * t14 = gm_l0;
        void * t15 = (void *)(((uint8_t *)t14) + 80);
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sp2m434o2c1ha));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(2));
        gm_t88rcucg1b7uk t17 = gm_l2;
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sqg70d5uem21e));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(34));
        gm_b16_8 t20 = gm_f_declare_helper_k8mi0a4((void *)NULL, cx, NULL, t12, t13, t15, t16, t17, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t20;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t21 = gm_l1;
        uint64_t t22 = GM_LD(uint64_t, (((((uint8_t *)t21) + 8) + 0) + 0));
        gm_l6 = t22;
    }
    {
        void * t23 = gm_l1;
        gm_t88rcucg1b7uk t24 = gm_l2;
        gm_t5v8n6qrn4rh4 t25;
        gm_f_repr_53quqki((void *)&t25, (void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t26 = t25;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        switch (t32) { case ((uint8_t)0ULL): goto ma28; case ((uint8_t)1ULL): goto ma28; case ((uint8_t)2ULL): goto ma29; case ((uint8_t)3ULL): goto ma28; case ((uint8_t)4ULL): goto ma30; case ((uint8_t)5ULL): goto ma30; case ((uint8_t)6ULL): goto ma30; default: goto ma30; }
ma28:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t36 != ((uint8_t)0ULL)) goto pa35;
        goto po34;
pa35:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t38 != ((uint8_t)1ULL)) goto pa37;
        goto po34;
pa37:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t40 != ((uint8_t)3ULL)) goto pa39;
        goto po34;
pa39:;
        goto mn33;
po34:;
        {
            void * t42 = gm_l1;
            gm_t88rcucg1b7uk t43 = gm_l2;
            gm_b16_8 t44 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t42, t43);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t44;
        }
        {
            void * t45 = (void *)((uint8_t *)&F.l5);
            gm_t6j6smj7qoctu t46;
            gm_b16_8 t47 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t46 + 0, t47);
            gm_b16_8 t48 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t46 + 16, t48);
            gm_tftb1odc739t5 t49 = gm_f_write_avsehn1(t45, &t46);
            gm_tftb1odc739t5 t50 = t49;
            if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_srckg9scnip7e8g, 56, 13);
            gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
        }
        goto me27;
mn33:;
ma29:;
        uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t53 != ((uint8_t)2ULL)) goto mn52;
        void * t54 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t55;
        GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sgn3gq6gfib7b));
        GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(39));
        gm_tftb1odc739t5 t56 = gm_f_extend_0cs85gs(t54, t55);
        gm_tftb1odc739t5 t57 = t56;
        if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srckg9scnip7e8g, 58, 18);
        gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
        goto me27;
mn52:;
ma30:;
        void * t60 = gm_l1;
        gm_t88rcucg1b7uk t61 = gm_l2;
        gm_t53at80pfdus3 t62;
        gm_f_kind_ghvav22((void *)&t62, t60, GM_CONV(gm_csguankko4lmhd, t61));
        gm_t53at80pfdus3 t63 = t62;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
        switch (t73) { case ((uint8_t)0ULL): goto ma71; case ((uint8_t)1ULL): goto ma71; case ((uint8_t)2ULL): goto ma71; case ((uint8_t)3ULL): goto ma71; case ((uint8_t)4ULL): goto ma71; case ((uint8_t)5ULL): goto ma71; case ((uint8_t)6ULL): goto ma71; case ((uint8_t)7ULL): goto ma71; case ((uint8_t)8ULL): goto ma71; case ((uint8_t)9ULL): goto ma71; case ((uint8_t)10ULL): goto ma67; case ((uint8_t)11ULL): goto ma66; case ((uint8_t)12ULL): goto ma65; case ((uint8_t)13ULL): goto ma68; case ((uint8_t)14ULL): goto ma69; case ((uint8_t)15ULL): goto ma71; case ((uint8_t)16ULL): goto ma71; case ((uint8_t)17ULL): goto ma70; case ((uint8_t)18ULL): goto ma71; case ((uint8_t)19ULL): goto ma71; case ((uint8_t)20ULL): goto ma71; case ((uint8_t)21ULL): goto ma71; case ((uint8_t)22ULL): goto ma71; case ((uint8_t)23ULL): goto ma71; case ((uint8_t)24ULL): goto ma71; case ((uint8_t)25ULL): goto ma71; case ((uint8_t)26ULL): goto ma71; default: goto ma71; }
ma65:;
        uint8_t t75 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
        if (t75 != ((uint8_t)12ULL)) goto mn74;
        gm_b16_8 t76 = GM_LD(gm_b16_8, ((((uint8_t *)&t63) + 8) + 0));
        gm_l8 = t76;
        {
            void * t78 = gm_l1;
            gm_t88rcucg1b7uk t79 = gm_l2;
            gm_b16_8 t80 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t78, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t80;
        }
        {
            {
                gm_b16_8 t82 = gm_l8;
                gm_l10 = t82;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t84 = gm_l11;
                    uint64_t t85 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t86 = (t84 < t85);
                    if (!t86) goto lb83;
                    {
                        uint64_t t89 = gm_l11;
                        uint64_t t90 = gm_l11;
                        uint64_t t91 = (uintptr_t)t90;
                        uint64_t t92 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t91 * 4));
                        gm_tlcm4lvu3g1a8 t94;
                        GM_ST(uint32_t, (uint8_t *)&t94 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t94 + 0), t89);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t94 + 8), t93);
                        gm_tlcm4lvu3g1a8 t95 = t94;
                        uint64_t t98 = GM_LD(uint64_t, (((uint8_t *)&t95) + 0));
                        gm_l12 = t98;
                        gm_t88rcucg1b7uk t99 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t95) + 8));
                        gm_l13 = t99;
                        {
                            void * t101 = gm_l0;
                            void * t102 = gm_l1;
                            gm_t88rcucg1b7uk t103 = gm_l13;
                            gm_b16_8 t104 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t101, t102, t103);
                            if (gm_unw.kind) {
                                goto gm_ll87;
                            }
                            gm_l14 = t104;
                        }
                        {
                            void * t105 = (void *)((uint8_t *)&F.l5);
                            gm_tgbgti82mf879 t106;
                            gm_b16_8 t107 = gm_l14;
                            GM_ST(gm_b16_8, (uint8_t *)&t106 + 0, t107);
                            uint64_t t108 = gm_l12;
                            uint64_t t109 = (uintptr_t)t108;
                            uint64_t t110 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            if (t109 >= (uintptr_t)(t110)) gm_trap_index(t109, t110, gm_srckg9scnip7e8g, 64, 80);
                            uint64_t t111 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t109 * 8));
                            GM_ST(uint64_t, (uint8_t *)&t106 + 16, t111);
                            uint64_t t112 = gm_l12;
                            uint64_t t113 = (uintptr_t)t112;
                            uint64_t t114 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            if (t113 >= (uintptr_t)(t114)) gm_trap_index(t113, t114, gm_srckg9scnip7e8g, 64, 89);
                            uint64_t t115 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t113 * 8));
                            GM_ST(uint64_t, (uint8_t *)&t106 + 24, t115);
                            gm_tftb1odc739t5 t116 = gm_f_write_7sl1fre(t105, &t106);
                            gm_tftb1odc739t5 t117 = t116;
                            if (((((uint8_t *)&t117))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t117) + 1, gm_srckg9scnip7e8g, 64, 21);
                            gm_b0_1 t118 = GM_LD(gm_b0_1, (((uint8_t *)&t117) + 1));
                        }
                        goto me96;
mn97:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 62, 17);
me96:;
                    }
lc83:;
                    {
                        uint64_t t120 = gm_l11;
                        uint64_t r121; bool o122 = __builtin_add_overflow(t120, ((uint64_t)1ULL), &r121);
                        uint64_t t123 = r121;
                        gm_l11 = t123;
                    }
                }
gm_ll87:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb83; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc83; }
                }
                goto gm_lad;
lb83:;
            }
        }
        {
            void * t124 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t125;
            GM_ST(void *, (uint8_t *)&t125, (void *)(gm_shmmkvp83pr05));
            GM_ST(uintptr_t, (uint8_t *)&t125 + 8, (uintptr_t)(17));
            gm_tftb1odc739t5 t126 = gm_f_extend_0cs85gs(t124, t125);
            gm_tftb1odc739t5 t127 = t126;
            if (((((uint8_t *)&t127))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t127) + 1, gm_srckg9scnip7e8g, 66, 17);
            gm_b0_1 t128 = GM_LD(gm_b0_1, (((uint8_t *)&t127) + 1));
        }
        goto me64;
mn74:;
ma66:;
        uint8_t t130 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
        if (t130 != ((uint8_t)11ULL)) goto mn129;
        uint64_t t131 = GM_LD(uint64_t, ((((uint8_t *)&t63) + 8) + 0));
        gm_l15 = t131;
        gm_t88rcucg1b7uk t132 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t63) + 8) + 8));
        gm_l16 = t132;
        {
            void * t134 = gm_l1;
            gm_t88rcucg1b7uk t135 = gm_l16;
            uint64_t t136 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t134, t135);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l17 = t136;
        }
        {
            void * t138 = gm_l1;
            gm_t88rcucg1b7uk t139 = gm_l16;
            uint8_t t140 = gm_f_bytewise_eq_e8jgutj((void *)NULL, cx, NULL, t138, t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t140) goto ie137;
            {
                void * t142 = (void *)((uint8_t *)&F.l5);
                uint64_t t143 = gm_l15;
                uint64_t t144 = gm_l17;
                uint64_t r145; bool o146 = __builtin_mul_overflow(t143, t144, &r145);
                if (o146) gm_trap("integer overflow in `*`", gm_srckg9scnip7e8g, 71, 68);
                uint64_t t147 = r145;
                gm_t4ce0jd3tdro4 t148;
                GM_ST(uint64_t, ((uint8_t *)&t148 + 0), t147);
                gm_tftb1odc739t5 t149 = gm_f_write_idp5df3(t142, t148);
                gm_tftb1odc739t5 t150 = t149;
                if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 1, gm_srckg9scnip7e8g, 71, 21);
                gm_b0_1 t151 = GM_LD(gm_b0_1, (((uint8_t *)&t150) + 1));
            }
            goto ix137;
ie137:;
            {
                void * t153 = gm_l0;
                void * t154 = gm_l1;
                gm_t88rcucg1b7uk t155 = gm_l16;
                gm_b16_8 t156 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t153, t154, t155);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t156;
            }
            {
                void * t157 = (void *)((uint8_t *)&F.l5);
                gm_t1d8endhr9k72 t158;
                uint64_t t159 = gm_l15;
                GM_ST(uint64_t, (uint8_t *)&t158 + 0, t159);
                gm_b16_8 t160 = gm_l18;
                GM_ST(gm_b16_8, (uint8_t *)&t158 + 8, t160);
                uint64_t t161 = gm_l17;
                GM_ST(uint64_t, (uint8_t *)&t158 + 24, t161);
                uint64_t t162 = gm_l17;
                GM_ST(uint64_t, (uint8_t *)&t158 + 32, t162);
                gm_tftb1odc739t5 t163 = gm_f_write_49bd3in(t157, &t158);
                gm_tftb1odc739t5 t164 = t163;
                if (((((uint8_t *)&t164))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t164) + 1, gm_srckg9scnip7e8g, 74, 21);
                gm_b0_1 t165 = GM_LD(gm_b0_1, (((uint8_t *)&t164) + 1));
            }
            {
                void * t166 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t167;
                GM_ST(void *, (uint8_t *)&t167, (void *)(gm_shmmkvp83pr05));
                GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(17));
                gm_tftb1odc739t5 t168 = gm_f_extend_0cs85gs(t166, t167);
                gm_tftb1odc739t5 t169 = t168;
                if (((((uint8_t *)&t169))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t169) + 1, gm_srckg9scnip7e8g, 75, 21);
                gm_b0_1 t170 = GM_LD(gm_b0_1, (((uint8_t *)&t169) + 1));
            }
ix137:;
        }
        goto me64;
mn129:;
ma67:;
        uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
        if (t172 != ((uint8_t)10ULL)) goto mn171;
        gm_t88rcucg1b7uk t173 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t63) + 8) + 4));
        gm_l19 = t173;
        {
            void * t175 = gm_l1;
            gm_t88rcucg1b7uk t176 = gm_l19;
            uint64_t t177 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t175, t176);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t177;
        }
        {
            void * t178 = (void *)((uint8_t *)&F.l5);
            uint64_t t179 = gm_l6;
            gm_t4ce0jd3tdro4 t180;
            GM_ST(uint64_t, ((uint8_t *)&t180 + 0), t179);
            gm_tftb1odc739t5 t181 = gm_f_write_baasn56(t178, t180);
            gm_tftb1odc739t5 t182 = t181;
            if (((((uint8_t *)&t182))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t182) + 1, gm_srckg9scnip7e8g, 80, 17);
            gm_b0_1 t183 = GM_LD(gm_b0_1, (((uint8_t *)&t182) + 1));
        }
        {
            void * t184 = (void *)((uint8_t *)&F.l5);
            uint64_t t185 = gm_l6;
            gm_t4ce0jd3tdro4 t186;
            GM_ST(uint64_t, ((uint8_t *)&t186 + 0), t185);
            gm_tftb1odc739t5 t187 = gm_f_write_o50khi1(t184, t186);
            gm_tftb1odc739t5 t188 = t187;
            if (((((uint8_t *)&t188))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t188) + 1, gm_srckg9scnip7e8g, 81, 17);
            gm_b0_1 t189 = GM_LD(gm_b0_1, (((uint8_t *)&t188) + 1));
        }
        {
            void * t190 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t191;
            GM_ST(void *, (uint8_t *)&t191, (void *)(gm_sor8flejv9lfq));
            GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)(66));
            gm_tftb1odc739t5 t192 = gm_f_extend_0cs85gs(t190, t191);
            gm_tftb1odc739t5 t193 = t192;
            if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 1, gm_srckg9scnip7e8g, 82, 17);
            gm_b0_1 t194 = GM_LD(gm_b0_1, (((uint8_t *)&t193) + 1));
        }
        {
            void * t196 = gm_l1;
            gm_t88rcucg1b7uk t197 = gm_l19;
            uint8_t t198 = gm_f_bytewise_eq_e8jgutj((void *)NULL, cx, NULL, t196, t197);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t198) goto ie195;
            {
                void * t200 = (void *)((uint8_t *)&F.l5);
                uint64_t t201 = gm_l20;
                gm_t4ce0jd3tdro4 t202;
                GM_ST(uint64_t, ((uint8_t *)&t202 + 0), t201);
                gm_tftb1odc739t5 t203 = gm_f_write_ho6u1fd(t200, t202);
                gm_tftb1odc739t5 t204 = t203;
                if (((((uint8_t *)&t204))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t204) + 1, gm_srckg9scnip7e8g, 84, 21);
                gm_b0_1 t205 = GM_LD(gm_b0_1, (((uint8_t *)&t204) + 1));
            }
            goto ix195;
ie195:;
            {
                void * t207 = gm_l0;
                void * t208 = gm_l1;
                gm_t88rcucg1b7uk t209 = gm_l19;
                gm_b16_8 t210 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t207, t208, t209);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l21 = t210;
            }
            {
                void * t211 = (void *)((uint8_t *)&F.l5);
                gm_tgbgti82mf879 t212;
                gm_b16_8 t213 = gm_l21;
                GM_ST(gm_b16_8, (uint8_t *)&t212 + 0, t213);
                uint64_t t214 = gm_l20;
                GM_ST(uint64_t, (uint8_t *)&t212 + 16, t214);
                uint64_t t215 = gm_l20;
                GM_ST(uint64_t, (uint8_t *)&t212 + 24, t215);
                gm_tftb1odc739t5 t216 = gm_f_write_qvj9qj9(t211, &t212);
                gm_tftb1odc739t5 t217 = t216;
                if (((((uint8_t *)&t217))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t217) + 1, gm_srckg9scnip7e8g, 87, 21);
                gm_b0_1 t218 = GM_LD(gm_b0_1, (((uint8_t *)&t217) + 1));
            }
            {
                void * t219 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t220;
                GM_ST(void *, (uint8_t *)&t220, (void *)(gm_shmmkvp83pr05));
                GM_ST(uintptr_t, (uint8_t *)&t220 + 8, (uintptr_t)(17));
                gm_tftb1odc739t5 t221 = gm_f_extend_0cs85gs(t219, t220);
                gm_tftb1odc739t5 t222 = t221;
                if (((((uint8_t *)&t222))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t222) + 1, gm_srckg9scnip7e8g, 88, 21);
                gm_b0_1 t223 = GM_LD(gm_b0_1, (((uint8_t *)&t222) + 1));
            }
ix195:;
        }
        goto me64;
mn171:;
ma68:;
        uint8_t t225 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
        if (t225 != ((uint8_t)13ULL)) goto mn224;
        gm_t88rcucg1b7uk t226 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t63) + 8));
        gm_l22 = t226;
        {
            void * t228 = gm_l0;
            void * t229 = gm_l1;
            gm_t88rcucg1b7uk t230 = gm_l22;
            gm_b16_8 t231 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t228, t229, t230);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t231;
        }
        {
            uint64_t t232 = (uintptr_t)((uint64_t)1ULL);
            void * t233 = gm_l1;
            gm_t88rcucg1b7uk t234 = gm_l2;
            gm_b16_8 t235 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t233, t234);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t236 = t235;
            uint64_t t237 = GM_LD(uintptr_t, ((uint8_t *)&t236) + 8);
            if (t232 >= (uintptr_t)(t237)) gm_trap_index(t232, t237, gm_srckg9scnip7e8g, 93, 25);
            uint64_t t238 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t236)) + t232 * 8));
            gm_l24 = t238;
        }
        {
            void * t239 = (void *)((uint8_t *)&F.l5);
            uint64_t t240 = gm_l24;
            uint64_t t241 = gm_l24;
            gm_t2lqu8jsj511d t242;
            GM_ST(uint64_t, ((uint8_t *)&t242 + 0), t240);
            GM_ST(uint64_t, ((uint8_t *)&t242 + 8), t241);
            gm_tftb1odc739t5 t243 = gm_f_write_jgspi6j(t239, t242);
            gm_tftb1odc739t5 t244 = t243;
            if (((((uint8_t *)&t244))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t244) + 1, gm_srckg9scnip7e8g, 94, 17);
            gm_b0_1 t245 = GM_LD(gm_b0_1, (((uint8_t *)&t244) + 1));
        }
        {
            void * t246 = (void *)((uint8_t *)&F.l5);
            gm_tf1bjg81u0glv t247;
            uint64_t t248 = gm_l24;
            GM_ST(uint64_t, (uint8_t *)&t247 + 0, t248);
            gm_b16_8 t249 = gm_l23;
            GM_ST(gm_b16_8, (uint8_t *)&t247 + 8, t249);
            gm_tftb1odc739t5 t250 = gm_f_write_ed06dp1(t246, &t247);
            gm_tftb1odc739t5 t251 = t250;
            if (((((uint8_t *)&t251))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t251) + 1, gm_srckg9scnip7e8g, 95, 17);
            gm_b0_1 t252 = GM_LD(gm_b0_1, (((uint8_t *)&t251) + 1));
        }
        goto me64;
mn224:;
ma69:;
        uint8_t t254 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
        if (t254 != ((uint8_t)14ULL)) goto mn253;
        gm_t88rcucg1b7uk t255 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t63) + 8) + 0));
        gm_l25 = t255;
        gm_t88rcucg1b7uk t256 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t63) + 8) + 4));
        gm_l26 = t256;
        {
            void * t258 = gm_l0;
            void * t259 = gm_l1;
            gm_t88rcucg1b7uk t260 = gm_l25;
            gm_b16_8 t261 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t258, t259, t260);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t261;
        }
        {
            void * t262 = gm_l0;
            void * t263 = gm_l1;
            gm_t88rcucg1b7uk t264 = gm_l26;
            gm_b16_8 t265 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t262, t263, t264);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l28 = t265;
        }
        {
            void * t266 = gm_l1;
            gm_t88rcucg1b7uk t267 = gm_l2;
            gm_b16_8 t268 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t266, t267);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l29 = t268;
        }
        {
            uint64_t t269 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t270 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
            if (t269 >= (uintptr_t)(t270)) gm_trap_index(t269, t270, gm_srckg9scnip7e8g, 101, 23);
            uint64_t t271 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t269 * 8));
            gm_l30 = t271;
        }
        {
            uint64_t t272 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t273 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
            if (t272 >= (uintptr_t)(t273)) gm_trap_index(t272, t273, gm_srckg9scnip7e8g, 102, 23);
            uint64_t t274 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t272 * 8));
            gm_l31 = t274;
        }
        {
            void * t275 = (void *)((uint8_t *)&F.l5);
            uint64_t t276 = gm_l30;
            uint64_t t277 = gm_l30;
            gm_t2lqu8jsj511d t278;
            GM_ST(uint64_t, ((uint8_t *)&t278 + 0), t276);
            GM_ST(uint64_t, ((uint8_t *)&t278 + 8), t277);
            gm_tftb1odc739t5 t279 = gm_f_write_6nhr3v9(t275, t278);
            gm_tftb1odc739t5 t280 = t279;
            if (((((uint8_t *)&t280))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t280) + 1, gm_srckg9scnip7e8g, 103, 17);
            gm_b0_1 t281 = GM_LD(gm_b0_1, (((uint8_t *)&t280) + 1));
        }
        {
            void * t282 = (void *)((uint8_t *)&F.l5);
            gm_t6s59i0k0msk8 t283;
            uint64_t t284 = gm_l30;
            GM_ST(uint64_t, (uint8_t *)&t283 + 0, t284);
            gm_b16_8 t285 = gm_l27;
            GM_ST(gm_b16_8, (uint8_t *)&t283 + 8, t285);
            uint64_t t286 = gm_l31;
            GM_ST(uint64_t, (uint8_t *)&t283 + 24, t286);
            uint64_t t287 = gm_l31;
            GM_ST(uint64_t, (uint8_t *)&t283 + 32, t287);
            gm_b16_8 t288 = gm_l28;
            GM_ST(gm_b16_8, (uint8_t *)&t283 + 40, t288);
            uint64_t t289 = gm_l31;
            GM_ST(uint64_t, (uint8_t *)&t283 + 56, t289);
            uint64_t t290 = gm_l31;
            GM_ST(uint64_t, (uint8_t *)&t283 + 64, t290);
            gm_tftb1odc739t5 t291 = gm_f_write_bq1tl8a(t282, &t283);
            gm_tftb1odc739t5 t292 = t291;
            if (((((uint8_t *)&t292))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t292) + 1, gm_srckg9scnip7e8g, 104, 17);
            gm_b0_1 t293 = GM_LD(gm_b0_1, (((uint8_t *)&t292) + 1));
        }
        goto me64;
mn253:;
ma70:;
        uint8_t t295 = GM_LD(uint8_t, (((uint8_t *)&t63) + 0));
        if (t295 != ((uint8_t)17ULL)) goto mn294;
        void * t296 = gm_l1;
        gm_t88rcucg1b7uk t297 = gm_l2;
        void * t298 = gm_f_adt_of_gej99vn(t296, GM_CONV(gm_csguankko4lmhd, t297));
        uint8_t t304 = GM_LD(uint8_t, ((((uint8_t *)t298) + 24) + 0));
        switch (t304) { case ((uint8_t)0ULL): goto ma300; case ((uint8_t)1ULL): goto ma301; case ((uint8_t)2ULL): goto ma302; default: goto ma303; }
ma300:;
        uint8_t t306 = GM_LD(uint8_t, ((((uint8_t *)t298) + 24) + 0));
        if (t306 != ((uint8_t)0ULL)) goto mn305;
        gm_tpautr4br7h3l t307 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t298) + 24) + 8));
        F.l32 = t307;
        {
            if (!(((((uint8_t *)&F.l32) + 40))[4] != 0)) goto ie309;
            gm_t88rcucg1b7uk t310 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l32) + 40));
            gm_l33 = t310;
            {
                void * t312 = gm_l0;
                void * t313 = gm_l1;
                gm_t88rcucg1b7uk t314 = gm_l33;
                gm_b16_8 t315 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t312, t313, t314);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l34 = t315;
            }
            {
                void * t316 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t317 = gm_l34;
                gm_tnpifv20irnph t318;
                GM_ST(gm_b16_8, ((uint8_t *)&t318 + 0), t317);
                gm_tftb1odc739t5 t319 = gm_f_write_96p75mj(t316, t318);
                gm_tftb1odc739t5 t320 = t319;
                if (((((uint8_t *)&t320))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t320) + 1, gm_srckg9scnip7e8g, 110, 25);
                gm_b0_1 t321 = GM_LD(gm_b0_1, (((uint8_t *)&t320) + 1));
            }
            goto ix309;
ie309:;
            {
                void * t323 = gm_l1;
                gm_t88rcucg1b7uk t324 = gm_l2;
                gm_b16_8 t325 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t323, t324);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l35 = t325;
            }
            {
                {
                    gm_b16_8 t327 = GM_LD(gm_b16_8, (((uint8_t *)&F.l32) + 0));
                    gm_b16_8 t328 = t327;
                    gm_b16_8 t329 = GM_LD(gm_b16_8, ((uint8_t *)&t328));
                    gm_l36 = t329;
                }
                {
                    gm_l37 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t331 = gm_l37;
                        uint64_t t332 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                        uint8_t t333 = (t331 < t332);
                        if (!t333) goto lb330;
                        {
                            gm_t8ta482bl6be0 t336;
                            uint64_t t337 = gm_l37;
                            GM_ST(uint64_t, (uint8_t *)&t336 + 0, t337);
                            uint64_t t338 = gm_l37;
                            uint64_t t339 = (uintptr_t)t338;
                            uint64_t t340 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                            memcpy((uint8_t *)&t336 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t339 * 40), 40);
                            gm_t8ta482bl6be0 t341 = t336;
                            uint64_t t344 = GM_LD(uint64_t, (((uint8_t *)&t341) + 0));
                            gm_l38 = t344;
                            gm_t0mglksrkfa5e t345 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t341) + 8));
                            F.l39 = t345;
                            {
                                void * t347 = gm_l0;
                                void * t348 = gm_l1;
                                gm_t88rcucg1b7uk t349 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l39) + 16));
                                gm_b16_8 t350 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t347, t348, t349);
                                if (gm_unw.kind) {
                                    goto gm_ll334;
                                }
                                gm_l40 = t350;
                            }
                            {
                                void * t351 = (void *)((uint8_t *)&F.l5);
                                gm_tgbgti82mf879 t352;
                                gm_b16_8 t353 = gm_l40;
                                GM_ST(gm_b16_8, (uint8_t *)&t352 + 0, t353);
                                uint64_t t354 = gm_l38;
                                uint64_t t355 = (uintptr_t)t354;
                                uint64_t t356 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                                if (t355 >= (uintptr_t)(t356)) gm_trap_index(t355, t356, gm_srckg9scnip7e8g, 115, 88);
                                uint64_t t357 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t355 * 8));
                                GM_ST(uint64_t, (uint8_t *)&t352 + 16, t357);
                                uint64_t t358 = gm_l38;
                                uint64_t t359 = (uintptr_t)t358;
                                uint64_t t360 = GM_LD(uintptr_t, ((uint8_t *)&gm_l35) + 8);
                                if (t359 >= (uintptr_t)(t360)) gm_trap_index(t359, t360, gm_srckg9scnip7e8g, 115, 97);
                                uint64_t t361 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l35)) + t359 * 8));
                                GM_ST(uint64_t, (uint8_t *)&t352 + 24, t361);
                                gm_tftb1odc739t5 t362 = gm_f_write_7sl1fre(t351, &t352);
                                gm_tftb1odc739t5 t363 = t362;
                                if (((((uint8_t *)&t363))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t363) + 1, gm_srckg9scnip7e8g, 115, 29);
                                gm_b0_1 t364 = GM_LD(gm_b0_1, (((uint8_t *)&t363) + 1));
                            }
                            goto me342;
mn343:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 113, 25);
me342:;
                        }
lc330:;
                        {
                            uint64_t t366 = gm_l37;
                            uint64_t r367; bool o368 = __builtin_add_overflow(t366, ((uint64_t)1ULL), &r367);
                            uint64_t t369 = r367;
                            gm_l37 = t369;
                        }
                    }
gm_ll334:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb330; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc330; }
                    }
                    goto gm_lad;
lb330:;
                }
            }
            {
                void * t370 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t371;
                GM_ST(void *, (uint8_t *)&t371, (void *)(gm_shmmkvp83pr05));
                GM_ST(uintptr_t, (uint8_t *)&t371 + 8, (uintptr_t)(17));
                gm_tftb1odc739t5 t372 = gm_f_extend_0cs85gs(t370, t371);
                gm_tftb1odc739t5 t373 = t372;
                if (((((uint8_t *)&t373))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t373) + 1, gm_srckg9scnip7e8g, 117, 25);
                gm_b0_1 t374 = GM_LD(gm_b0_1, (((uint8_t *)&t373) + 1));
            }
ix309:;
        }
        goto me299;
mn305:;
ma301:;
        uint8_t t376 = GM_LD(uint8_t, ((((uint8_t *)t298) + 24) + 0));
        if (t376 != ((uint8_t)1ULL)) goto mn375;
        gm_t88rcucg1b7uk t377 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t298) + 24) + 8) + 0));
        gm_l41 = t377;
        gm_b16_8 t378 = GM_LD(gm_b16_8, (((((uint8_t *)t298) + 24) + 8) + 8));
        gm_l42 = t378;
        {
            void * t380 = gm_l1;
            gm_t88rcucg1b7uk t381 = gm_l2;
            gm_b16_8 t382 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t380, t381);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l43 = t382;
        }
        {
            void * t383 = gm_l1;
            gm_t88rcucg1b7uk t384 = gm_l41;
            gm_b16_8 t385 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t383, t384);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l44 = t385;
        }
        {
            void * t386 = (void *)((uint8_t *)&F.l5);
            gm_t1h4kdi7gs4v5 t387;
            gm_b16_8 t388 = gm_l44;
            GM_ST(gm_b16_8, (uint8_t *)&t387 + 0, t388);
            gm_b16_8 t389 = gm_l44;
            GM_ST(gm_b16_8, (uint8_t *)&t387 + 16, t389);
            uint64_t t390 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t391 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
            if (t390 >= (uintptr_t)(t391)) gm_trap_index(t390, t391, gm_srckg9scnip7e8g, 123, 97);
            uint64_t t392 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t390 * 8));
            GM_ST(uint64_t, (uint8_t *)&t387 + 32, t392);
            gm_b16_8 t393 = gm_l44;
            GM_ST(gm_b16_8, (uint8_t *)&t387 + 40, t393);
            uint64_t t394 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t395 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
            if (t394 >= (uintptr_t)(t395)) gm_trap_index(t394, t395, gm_srckg9scnip7e8g, 123, 110);
            uint64_t t396 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t394 * 8));
            GM_ST(uint64_t, (uint8_t *)&t387 + 56, t396);
            gm_tftb1odc739t5 t397 = gm_f_write_0kurvf5(t386, &t387);
            gm_tftb1odc739t5 t398 = t397;
            if (((((uint8_t *)&t398))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t398) + 1, gm_srckg9scnip7e8g, 123, 21);
            gm_b0_1 t399 = GM_LD(gm_b0_1, (((uint8_t *)&t398) + 1));
        }
        {
            void * t400 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t401;
            GM_ST(void *, (uint8_t *)&t401, (void *)(gm_shm5oj4temhh6));
            GM_ST(uintptr_t, (uint8_t *)&t401 + 8, (uintptr_t)(32));
            gm_tftb1odc739t5 t402 = gm_f_extend_0cs85gs(t400, t401);
            gm_tftb1odc739t5 t403 = t402;
            if (((((uint8_t *)&t403))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t403) + 1, gm_srckg9scnip7e8g, 124, 21);
            gm_b0_1 t404 = GM_LD(gm_b0_1, (((uint8_t *)&t403) + 1));
        }
        {
            {
                gm_b16_8 t406 = gm_l42;
                gm_l45 = t406;
            }
            {
                gm_l46 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t408 = gm_l46;
                    uint64_t t409 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                    uint8_t t410 = (t408 < t409);
                    if (!t410) goto lb407;
                    {
                        uint64_t t413 = gm_l46;
                        uint64_t t414 = (uintptr_t)t413;
                        uint64_t t415 = GM_LD(uintptr_t, ((uint8_t *)&gm_l45) + 8);
                        gm_tckdobunaulee t418 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l45)) + t414 * 72));
                        F.l47 = t418;
                        {
                            if (!(((((uint8_t *)&F.l47) + 16))[4] != 0)) goto ie420;
                            gm_t88rcucg1b7uk t421 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l47) + 16));
                            gm_l48 = t421;
                            {
                                void * t423 = gm_l0;
                                void * t424 = gm_l1;
                                gm_t88rcucg1b7uk t425 = gm_l48;
                                gm_b16_8 t426 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t423, t424, t425);
                                if (gm_unw.kind) {
                                    goto gm_ll411;
                                }
                                gm_l49 = t426;
                            }
                            {
                                void * t427 = (void *)((uint8_t *)&F.l5);
                                gm_tg23pq9a29gh1 t428;
                                gm_b16_8 t429 = gm_l44;
                                GM_ST(gm_b16_8, (uint8_t *)&t428 + 0, t429);
                                gm_b16_8 t430 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l47) + 24)));
                                if (gm_unw.kind) {
                                    goto gm_ll411;
                                }
                                gm_b16_8 t431 = t430;
                                gm_b16_8 t432 = GM_LD(gm_b16_8, ((uint8_t *)&t431));
                                GM_ST(gm_b16_8, (uint8_t *)&t428 + 16, t432);
                                gm_b16_8 t433 = gm_l49;
                                GM_ST(gm_b16_8, (uint8_t *)&t428 + 32, t433);
                                uint64_t t434 = (uintptr_t)((uint64_t)1ULL);
                                uint64_t t435 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                                if (t434 >= (uintptr_t)(t435)) gm_trap_index(t434, t435, gm_srckg9scnip7e8g, 128, 126);
                                uint64_t t436 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t434 * 8));
                                GM_ST(uint64_t, (uint8_t *)&t428 + 48, t436);
                                uint64_t t437 = (uintptr_t)((uint64_t)1ULL);
                                uint64_t t438 = GM_LD(uintptr_t, ((uint8_t *)&gm_l43) + 8);
                                if (t437 >= (uintptr_t)(t438)) gm_trap_index(t437, t438, gm_srckg9scnip7e8g, 128, 135);
                                uint64_t t439 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l43)) + t437 * 8));
                                GM_ST(uint64_t, (uint8_t *)&t428 + 56, t439);
                                gm_tftb1odc739t5 t440 = gm_f_write_8ijr07s(t427, &t428);
                                gm_tftb1odc739t5 t441 = t440;
                                if (((((uint8_t *)&t441))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t441) + 1, gm_srckg9scnip7e8g, 128, 29);
                                gm_b0_1 t442 = GM_LD(gm_b0_1, (((uint8_t *)&t441) + 1));
                            }
                            goto ix420;
ie420:;
ix420:;
                        }
                        goto me416;
mn417:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 125, 21);
me416:;
                    }
lc407:;
                    {
                        uint64_t t444 = gm_l46;
                        uint64_t r445; bool o446 = __builtin_add_overflow(t444, ((uint64_t)1ULL), &r445);
                        uint64_t t447 = r445;
                        gm_l46 = t447;
                    }
                }
gm_ll411:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb407; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc407; }
                }
                goto gm_lad;
lb407:;
            }
        }
        {
            void * t448 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t449;
            GM_ST(void *, (uint8_t *)&t449, (void *)(gm_shmmkvp83pr05));
            GM_ST(uintptr_t, (uint8_t *)&t449 + 8, (uintptr_t)(17));
            gm_tftb1odc739t5 t450 = gm_f_extend_0cs85gs(t448, t449);
            gm_tftb1odc739t5 t451 = t450;
            if (((((uint8_t *)&t451))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t451) + 1, gm_srckg9scnip7e8g, 131, 21);
            gm_b0_1 t452 = GM_LD(gm_b0_1, (((uint8_t *)&t451) + 1));
        }
        goto me299;
mn375:;
ma302:;
        uint8_t t454 = GM_LD(uint8_t, ((((uint8_t *)t298) + 24) + 0));
        if (t454 != ((uint8_t)2ULL)) goto mn453;
        void * t455 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t456;
        GM_ST(void *, (uint8_t *)&t456, (void *)(gm_sug64ltbhov0m));
        GM_ST(uintptr_t, (uint8_t *)&t456 + 8, (uintptr_t)(18));
        gm_tftb1odc739t5 t457 = gm_f_extend_0cs85gs(t455, t456);
        gm_tftb1odc739t5 t458 = t457;
        if (((((uint8_t *)&t458))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t458) + 1, gm_srckg9scnip7e8g, 133, 31);
        gm_b0_1 t459 = GM_LD(gm_b0_1, (((uint8_t *)&t458) + 1));
        goto me299;
mn453:;
ma303:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 106, 27);
me299:;
        goto me64;
mn294:;
ma71:;
        void * t461 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t462;
        GM_ST(void *, (uint8_t *)&t462, (void *)(gm_shmmkvp83pr05));
        GM_ST(uintptr_t, (uint8_t *)&t462 + 8, (uintptr_t)(17));
        gm_tftb1odc739t5 t463 = gm_f_extend_0cs85gs(t461, t462);
        gm_tftb1odc739t5 t464 = t463;
        if (((((uint8_t *)&t464))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t464) + 1, gm_srckg9scnip7e8g, 135, 18);
        gm_b0_1 t465 = GM_LD(gm_b0_1, (((uint8_t *)&t464) + 1));
        goto me64;
mn460:;
ma72:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 59, 14);
me64:;
        goto me27;
mn59:;
ma31:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 53, 5);
me27:;
    }
    {
        void * t466 = gm_l0;
        gm_b16_8 t467 = gm_l4;
        gm_b16_8 t468;
        GM_ST(void *, (uint8_t *)&t468, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t468 + 8, (uintptr_t)(4));
        gm_b16_8 t469;
        GM_ST(void *, (uint8_t *)&t469, (void *)(gm_sqg70d5uem21e));
        GM_ST(uintptr_t, (uint8_t *)&t469 + 8, (uintptr_t)(34));
        gm_b16_8 t470 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t471 = t470;
        gm_b16_8 t472 = GM_LD(gm_b16_8, ((uint8_t *)&t471));
        gm_b0_1 t473 = gm_f_define_helper_c1utd1n((void *)NULL, cx, NULL, t466, t467, t468, t469, t472);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t474 = gm_l4;
        uint8_t t475 = 0;
        F.ret = t474;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_fn_nlk82uj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_hash_fn_nlk82uj F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* name */
    uint64_t gm_l6; /* p */
    uint8_t gm_l7; /* signed */
    gm_b16_8 gm_l8; /* c */
    gm_b16_8 gm_l9; /* f */
    gm_b16_8 gm_l10; /* c */
    gm_b16_8 gm_l11; /* elems */
    gm_b16_8 gm_l12; /* offs */
    gm_b16_8 gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_t88rcucg1b7uk gm_l16; /* et */
    gm_b16_8 gm_l17; /* f */
    uint64_t gm_l18; /* len */
    gm_t88rcucg1b7uk gm_l19; /* elem */
    gm_b16_8 gm_l20; /* f */
    uint64_t gm_l21; /* esz */
    gm_t88rcucg1b7uk gm_l22; /* elem */
    gm_b16_8 gm_l23; /* f */
    uint64_t gm_l24; /* esz */
    gm_t88rcucg1b7uk gm_l25; /* inner */
    gm_b16_8 gm_l26; /* f */
    uint64_t gm_l27; /* flag */
    gm_t88rcucg1b7uk gm_l28; /* err */
    gm_t88rcucg1b7uk gm_l29; /* ok */
    gm_b16_8 gm_l30; /* fe */
    gm_b16_8 gm_l31; /* fo */
    gm_b16_8 gm_l32; /* offs */
    gm_t88rcucg1b7uk gm_l34; /* u */
    gm_b16_8 gm_l35; /* f */
    gm_b16_8 gm_l36; /* offs */
    gm_b16_8 gm_l37; /* <tmp> */
    uint64_t gm_l38; /* <tmp> */
    uint64_t gm_l39; /* i */
    gm_b16_8 gm_l41; /* f */
    gm_t88rcucg1b7uk gm_l42; /* tag */
    gm_b16_8 gm_l43; /* variants */
    gm_b16_8 gm_l44; /* offs */
    gm_b16_8 gm_l45; /* tc */
    gm_b16_8 gm_l46; /* <tmp> */
    uint64_t gm_l47; /* <tmp> */
    uint64_t gm_l48; /* i */
    gm_t88rcucg1b7uk gm_l50; /* pt */
    gm_b16_8 gm_l51; /* f */
    GM_STACK_CHECK(&F, 4089u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 192);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        gm_th2lqbo77pvsc t6;
        gm_f_get_gnmase9((void *)&t6, t4, t5);
        gm_th2lqbo77pvsc t7 = t6;
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l3 = t8;
        {
            gm_b16_8 t10 = gm_l3;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l1;
        void * t14 = gm_l0;
        void * t15 = (void *)(((uint8_t *)t14) + 192);
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s9iu5gsqog7uc));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(4));
        gm_t88rcucg1b7uk t17 = gm_l2;
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_shn8mjp9kc2n2));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(8));
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s81r46mpa8ra9));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(28));
        gm_b16_8 t20 = gm_f_declare_helper_k8mi0a4((void *)NULL, cx, NULL, t12, t13, t15, t16, t17, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t20;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t21 = gm_l1;
        uint64_t t22 = GM_LD(uint64_t, (((((uint8_t *)t21) + 8) + 0) + 0));
        gm_l6 = t22;
    }
    {
        void * t23 = gm_l1;
        gm_t88rcucg1b7uk t24 = gm_l2;
        gm_t5v8n6qrn4rh4 t25;
        gm_f_repr_53quqki((void *)&t25, (void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t26 = t25;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        switch (t34) { case ((uint8_t)0ULL): goto ma28; case ((uint8_t)1ULL): goto ma29; case ((uint8_t)2ULL): goto ma30; case ((uint8_t)3ULL): goto ma31; case ((uint8_t)4ULL): goto ma32; case ((uint8_t)5ULL): goto ma32; case ((uint8_t)6ULL): goto ma32; default: goto ma32; }
ma28:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t36 != ((uint8_t)0ULL)) goto mn35;
        uint8_t t37 = GM_LD(uint8_t, ((((uint8_t *)&t26) + 8) + 0));
        gm_l7 = t37;
        {
            void * t39 = gm_l1;
            gm_t88rcucg1b7uk t40 = gm_l2;
            gm_b16_8 t41 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l8 = t41;
        }
        {
            gm_b16_8 t42;
            uint8_t t44 = gm_l7;
            if (!t44) goto ie43;
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sstcpkpuiippb));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(11));
            gm_b16_8 t47 = t46;
            t42 = t47;
            goto ix43;
ie43:;
            gm_b16_8 t49;
            GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sisc4n92pk600));
            GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(12));
            gm_b16_8 t50 = t49;
            t42 = t50;
ix43:;
            gm_l9 = t42;
        }
        {
            void * t51 = (void *)((uint8_t *)&F.l5);
            gm_t6j6smj7qoctu t52;
            gm_b16_8 t53 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t52 + 0, t53);
            gm_b16_8 t54 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t52 + 16, t54);
            gm_tftb1odc739t5 t55 = gm_f_write_pqeomd8(t51, &t52);
            gm_tftb1odc739t5 t56 = t55;
            if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_srckg9scnip7e8g, 232, 13);
            gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
        }
        goto me27;
mn35:;
ma29:;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t59 != ((uint8_t)1ULL)) goto mn58;
        {
            void * t61 = gm_l1;
            gm_t88rcucg1b7uk t62 = gm_l2;
            gm_b16_8 t63 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t61, t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t63;
        }
        {
            void * t64 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t65 = gm_l10;
            gm_tnpifv20irnph t66;
            GM_ST(gm_b16_8, ((uint8_t *)&t66 + 0), t65);
            gm_tftb1odc739t5 t67 = gm_f_write_c377pgo(t64, t66);
            gm_tftb1odc739t5 t68 = t67;
            if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_srckg9scnip7e8g, 236, 13);
            gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
        }
        {
            void * t70 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t71;
            GM_ST(void *, (uint8_t *)&t71, (void *)(gm_s4gcag2e2r0cj));
            GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(30));
            gm_tftb1odc739t5 t72 = gm_f_extend_0cs85gs(t70, t71);
            gm_tftb1odc739t5 t73 = t72;
            if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_srckg9scnip7e8g, 237, 13);
            gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
        }
        goto me27;
mn58:;
ma30:;
        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t76 != ((uint8_t)2ULL)) goto mn75;
        void * t77 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t78;
        GM_ST(void *, (uint8_t *)&t78, (void *)(gm_sckmhri27q39l));
        GM_ST(uintptr_t, (uint8_t *)&t78 + 8, (uintptr_t)(52));
        gm_tftb1odc739t5 t79 = gm_f_extend_0cs85gs(t77, t78);
        gm_tftb1odc739t5 t80 = t79;
        if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_srckg9scnip7e8g, 239, 18);
        gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
        goto me27;
mn75:;
ma31:;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t83 != ((uint8_t)3ULL)) goto mn82;
        void * t84 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t85;
        GM_ST(void *, (uint8_t *)&t85, (void *)(gm_sigmj6bktcbbh));
        GM_ST(uintptr_t, (uint8_t *)&t85 + 8, (uintptr_t)(79));
        gm_tftb1odc739t5 t86 = gm_f_extend_0cs85gs(t84, t85);
        gm_tftb1odc739t5 t87 = t86;
        if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_srckg9scnip7e8g, 240, 17);
        gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
        goto me27;
mn82:;
ma32:;
        void * t90 = gm_l1;
        gm_t88rcucg1b7uk t91 = gm_l2;
        gm_t53at80pfdus3 t92;
        gm_f_kind_ghvav22((void *)&t92, t90, GM_CONV(gm_csguankko4lmhd, t91));
        gm_t53at80pfdus3 t93 = t92;
        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&t93) + 0));
        switch (t103) { case ((uint8_t)0ULL): goto ma101; case ((uint8_t)1ULL): goto ma101; case ((uint8_t)2ULL): goto ma101; case ((uint8_t)3ULL): goto ma101; case ((uint8_t)4ULL): goto ma101; case ((uint8_t)5ULL): goto ma101; case ((uint8_t)6ULL): goto ma101; case ((uint8_t)7ULL): goto ma101; case ((uint8_t)8ULL): goto ma101; case ((uint8_t)9ULL): goto ma101; case ((uint8_t)10ULL): goto ma97; case ((uint8_t)11ULL): goto ma96; case ((uint8_t)12ULL): goto ma95; case ((uint8_t)13ULL): goto ma98; case ((uint8_t)14ULL): goto ma99; case ((uint8_t)15ULL): goto ma101; case ((uint8_t)16ULL): goto ma101; case ((uint8_t)17ULL): goto ma100; case ((uint8_t)18ULL): goto ma101; case ((uint8_t)19ULL): goto ma101; case ((uint8_t)20ULL): goto ma101; case ((uint8_t)21ULL): goto ma101; case ((uint8_t)22ULL): goto ma101; case ((uint8_t)23ULL): goto ma101; case ((uint8_t)24ULL): goto ma101; case ((uint8_t)25ULL): goto ma101; case ((uint8_t)26ULL): goto ma101; default: goto ma101; }
ma95:;
        uint8_t t105 = GM_LD(uint8_t, (((uint8_t *)&t93) + 0));
        if (t105 != ((uint8_t)12ULL)) goto mn104;
        gm_b16_8 t106 = GM_LD(gm_b16_8, ((((uint8_t *)&t93) + 8) + 0));
        gm_l11 = t106;
        {
            void * t108 = gm_l1;
            gm_t88rcucg1b7uk t109 = gm_l2;
            gm_b16_8 t110 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t108, t109);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l12 = t110;
        }
        {
            {
                gm_b16_8 t112 = gm_l11;
                gm_l13 = t112;
            }
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t114 = gm_l14;
                    uint64_t t115 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                    uint8_t t116 = (t114 < t115);
                    if (!t116) goto lb113;
                    {
                        uint64_t t119 = gm_l14;
                        uint64_t t120 = gm_l14;
                        uint64_t t121 = (uintptr_t)t120;
                        uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l13) + 8);
                        gm_t88rcucg1b7uk t123 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l13)) + t121 * 4));
                        gm_tlcm4lvu3g1a8 t124;
                        GM_ST(uint32_t, (uint8_t *)&t124 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t124 + 0), t119);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t124 + 8), t123);
                        gm_tlcm4lvu3g1a8 t125 = t124;
                        uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)&t125) + 0));
                        gm_l15 = t128;
                        gm_t88rcucg1b7uk t129 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t125) + 8));
                        gm_l16 = t129;
                        {
                            void * t131 = gm_l0;
                            void * t132 = gm_l1;
                            gm_t88rcucg1b7uk t133 = gm_l16;
                            gm_b16_8 t134 = gm_f_hash_fn_nlk82uj((void *)NULL, cx, NULL, t131, t132, t133);
                            if (gm_unw.kind) {
                                goto gm_ll117;
                            }
                            gm_l17 = t134;
                        }
                        {
                            void * t135 = (void *)((uint8_t *)&F.l5);
                            gm_t3877pk7lv748 t136;
                            gm_b16_8 t137 = gm_l17;
                            GM_ST(gm_b16_8, (uint8_t *)&t136 + 0, t137);
                            uint64_t t138 = gm_l15;
                            uint64_t t139 = (uintptr_t)t138;
                            uint64_t t140 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            if (t139 >= (uintptr_t)(t140)) gm_trap_index(t139, t140, gm_srckg9scnip7e8g, 246, 60);
                            uint64_t t141 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t139 * 8));
                            GM_ST(uint64_t, (uint8_t *)&t136 + 16, t141);
                            gm_tftb1odc739t5 t142 = gm_f_write_u9rtsjt(t135, &t136);
                            gm_tftb1odc739t5 t143 = t142;
                            if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 1, gm_srckg9scnip7e8g, 246, 21);
                            gm_b0_1 t144 = GM_LD(gm_b0_1, (((uint8_t *)&t143) + 1));
                        }
                        goto me126;
mn127:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 244, 17);
me126:;
                    }
lc113:;
                    {
                        uint64_t t146 = gm_l14;
                        uint64_t r147; bool o148 = __builtin_add_overflow(t146, ((uint64_t)1ULL), &r147);
                        uint64_t t149 = r147;
                        gm_l14 = t149;
                    }
                }
gm_ll117:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb113; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc113; }
                }
                goto gm_lad;
lb113:;
            }
        }
        {
            void * t150 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t151;
            GM_ST(void *, (uint8_t *)&t151, (void *)(gm_sel1fj24qk32p));
            GM_ST(uintptr_t, (uint8_t *)&t151 + 8, (uintptr_t)(14));
            gm_tftb1odc739t5 t152 = gm_f_extend_0cs85gs(t150, t151);
            gm_tftb1odc739t5 t153 = t152;
            if (((((uint8_t *)&t153))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t153) + 1, gm_srckg9scnip7e8g, 248, 17);
            gm_b0_1 t154 = GM_LD(gm_b0_1, (((uint8_t *)&t153) + 1));
        }
        goto me94;
mn104:;
ma96:;
        uint8_t t156 = GM_LD(uint8_t, (((uint8_t *)&t93) + 0));
        if (t156 != ((uint8_t)11ULL)) goto mn155;
        uint64_t t157 = GM_LD(uint64_t, ((((uint8_t *)&t93) + 8) + 0));
        gm_l18 = t157;
        gm_t88rcucg1b7uk t158 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t93) + 8) + 8));
        gm_l19 = t158;
        {
            void * t160 = gm_l0;
            void * t161 = gm_l1;
            gm_t88rcucg1b7uk t162 = gm_l19;
            gm_b16_8 t163 = gm_f_hash_fn_nlk82uj((void *)NULL, cx, NULL, t160, t161, t162);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t163;
        }
        {
            void * t164 = gm_l1;
            gm_t88rcucg1b7uk t165 = gm_l19;
            uint64_t t166 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t164, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l21 = t166;
        }
        {
            void * t167 = (void *)((uint8_t *)&F.l5);
            gm_tob1snuii5bme t168;
            uint64_t t169 = gm_l18;
            GM_ST(uint64_t, (uint8_t *)&t168 + 0, t169);
            gm_b16_8 t170 = gm_l20;
            GM_ST(gm_b16_8, (uint8_t *)&t168 + 8, t170);
            uint64_t t171 = gm_l21;
            GM_ST(uint64_t, (uint8_t *)&t168 + 24, t171);
            gm_tftb1odc739t5 t172 = gm_f_write_pt06m3q(t167, &t168);
            gm_tftb1odc739t5 t173 = t172;
            if (((((uint8_t *)&t173))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t173) + 1, gm_srckg9scnip7e8g, 253, 17);
            gm_b0_1 t174 = GM_LD(gm_b0_1, (((uint8_t *)&t173) + 1));
        }
        goto me94;
mn155:;
ma97:;
        uint8_t t176 = GM_LD(uint8_t, (((uint8_t *)&t93) + 0));
        if (t176 != ((uint8_t)10ULL)) goto mn175;
        gm_t88rcucg1b7uk t177 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t93) + 8) + 4));
        gm_l22 = t177;
        {
            void * t179 = gm_l0;
            void * t180 = gm_l1;
            gm_t88rcucg1b7uk t181 = gm_l22;
            gm_b16_8 t182 = gm_f_hash_fn_nlk82uj((void *)NULL, cx, NULL, t179, t180, t181);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t182;
        }
        {
            void * t183 = gm_l1;
            gm_t88rcucg1b7uk t184 = gm_l22;
            uint64_t t185 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t183, t184);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t185;
        }
        {
            void * t186 = (void *)((uint8_t *)&F.l5);
            uint64_t t187 = gm_l6;
            gm_t4ce0jd3tdro4 t188;
            GM_ST(uint64_t, ((uint8_t *)&t188 + 0), t187);
            gm_tftb1odc739t5 t189 = gm_f_write_k9lbjqb(t186, t188);
            gm_tftb1odc739t5 t190 = t189;
            if (((((uint8_t *)&t190))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t190) + 1, gm_srckg9scnip7e8g, 258, 17);
            gm_b0_1 t191 = GM_LD(gm_b0_1, (((uint8_t *)&t190) + 1));
        }
        {
            void * t192 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t193;
            GM_ST(void *, (uint8_t *)&t193, (void *)(gm_sfm0hseeqersg));
            GM_ST(uintptr_t, (uint8_t *)&t193 + 8, (uintptr_t)(24));
            gm_tftb1odc739t5 t194 = gm_f_extend_0cs85gs(t192, t193);
            gm_tftb1odc739t5 t195 = t194;
            if (((((uint8_t *)&t195))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t195) + 1, gm_srckg9scnip7e8g, 259, 17);
            gm_b0_1 t196 = GM_LD(gm_b0_1, (((uint8_t *)&t195) + 1));
        }
        {
            void * t197 = (void *)((uint8_t *)&F.l5);
            gm_t3877pk7lv748 t198;
            gm_b16_8 t199 = gm_l23;
            GM_ST(gm_b16_8, (uint8_t *)&t198 + 0, t199);
            uint64_t t200 = gm_l24;
            GM_ST(uint64_t, (uint8_t *)&t198 + 16, t200);
            gm_tftb1odc739t5 t201 = gm_f_write_eu9q7ts(t197, &t198);
            gm_tftb1odc739t5 t202 = t201;
            if (((((uint8_t *)&t202))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t202) + 1, gm_srckg9scnip7e8g, 260, 17);
            gm_b0_1 t203 = GM_LD(gm_b0_1, (((uint8_t *)&t202) + 1));
        }
        goto me94;
mn175:;
ma98:;
        uint8_t t205 = GM_LD(uint8_t, (((uint8_t *)&t93) + 0));
        if (t205 != ((uint8_t)13ULL)) goto mn204;
        gm_t88rcucg1b7uk t206 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t93) + 8));
        gm_l25 = t206;
        {
            void * t209 = gm_l1;
            void * t210 = GM_LD(void *, (((uint8_t *)t209) + 0));
            gm_t88rcucg1b7uk t211 = gm_l25;
            uint8_t t212 = gm_f_nullable_pointer_7qfmn6o(t210, GM_CONV(gm_csguankko4lmhd, t211));
            if (!t212) goto ie208;
            {
                void * t214 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t215;
                GM_ST(void *, (uint8_t *)&t215, (void *)(gm_sel1fj24qk32p));
                GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(14));
                gm_tftb1odc739t5 t216 = gm_f_extend_0cs85gs(t214, t215);
                gm_tftb1odc739t5 t217 = t216;
                if (((((uint8_t *)&t217))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t217) + 1, gm_srckg9scnip7e8g, 264, 21);
                gm_b0_1 t218 = GM_LD(gm_b0_1, (((uint8_t *)&t217) + 1));
            }
            goto ix208;
ie208:;
            {
                void * t220 = gm_l0;
                void * t221 = gm_l1;
                gm_t88rcucg1b7uk t222 = gm_l25;
                gm_b16_8 t223 = gm_f_hash_fn_nlk82uj((void *)NULL, cx, NULL, t220, t221, t222);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l26 = t223;
            }
            {
                uint64_t t224 = (uintptr_t)((uint64_t)1ULL);
                void * t225 = gm_l1;
                gm_t88rcucg1b7uk t226 = gm_l2;
                gm_b16_8 t227 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t225, t226);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t228 = t227;
                uint64_t t229 = GM_LD(uintptr_t, ((uint8_t *)&t228) + 8);
                if (t224 >= (uintptr_t)(t229)) gm_trap_index(t224, t229, gm_srckg9scnip7e8g, 267, 29);
                uint64_t t230 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t228)) + t224 * 8));
                gm_l27 = t230;
            }
            {
                void * t231 = (void *)((uint8_t *)&F.l5);
                uint64_t t232 = gm_l27;
                gm_t4ce0jd3tdro4 t233;
                GM_ST(uint64_t, ((uint8_t *)&t233 + 0), t232);
                gm_tftb1odc739t5 t234 = gm_f_write_m3i8l46(t231, t233);
                gm_tftb1odc739t5 t235 = t234;
                if (((((uint8_t *)&t235))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t235) + 1, gm_srckg9scnip7e8g, 268, 21);
                gm_b0_1 t236 = GM_LD(gm_b0_1, (((uint8_t *)&t235) + 1));
            }
            {
                void * t237 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t238 = gm_l26;
                gm_tnpifv20irnph t239;
                GM_ST(gm_b16_8, ((uint8_t *)&t239 + 0), t238);
                gm_tftb1odc739t5 t240 = gm_f_write_g25419f(t237, t239);
                gm_tftb1odc739t5 t241 = t240;
                if (((((uint8_t *)&t241))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t241) + 1, gm_srckg9scnip7e8g, 269, 21);
                gm_b0_1 t242 = GM_LD(gm_b0_1, (((uint8_t *)&t241) + 1));
            }
ix208:;
        }
        goto me94;
mn204:;
ma99:;
        uint8_t t244 = GM_LD(uint8_t, (((uint8_t *)&t93) + 0));
        if (t244 != ((uint8_t)14ULL)) goto mn243;
        gm_t88rcucg1b7uk t245 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t93) + 8) + 0));
        gm_l28 = t245;
        gm_t88rcucg1b7uk t246 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t93) + 8) + 4));
        gm_l29 = t246;
        {
            void * t248 = gm_l0;
            void * t249 = gm_l1;
            gm_t88rcucg1b7uk t250 = gm_l28;
            gm_b16_8 t251 = gm_f_hash_fn_nlk82uj((void *)NULL, cx, NULL, t248, t249, t250);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l30 = t251;
        }
        {
            void * t252 = gm_l0;
            void * t253 = gm_l1;
            gm_t88rcucg1b7uk t254 = gm_l29;
            gm_b16_8 t255 = gm_f_hash_fn_nlk82uj((void *)NULL, cx, NULL, t252, t253, t254);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l31 = t255;
        }
        {
            void * t256 = gm_l1;
            gm_t88rcucg1b7uk t257 = gm_l2;
            gm_b16_8 t258 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t256, t257);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l32 = t258;
        }
        {
            void * t259 = (void *)((uint8_t *)&F.l5);
            uint64_t t260 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t261 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
            if (t260 >= (uintptr_t)(t261)) gm_trap_index(t260, t261, gm_srckg9scnip7e8g, 276, 80);
            uint64_t t262 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t260 * 8));
            gm_t4ce0jd3tdro4 t263;
            GM_ST(uint64_t, ((uint8_t *)&t263 + 0), t262);
            gm_tftb1odc739t5 t264 = gm_f_write_lblem56(t259, t263);
            gm_tftb1odc739t5 t265 = t264;
            if (((((uint8_t *)&t265))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t265) + 1, gm_srckg9scnip7e8g, 276, 17);
            gm_b0_1 t266 = GM_LD(gm_b0_1, (((uint8_t *)&t265) + 1));
        }
        {
            void * t267 = (void *)((uint8_t *)&F.l5);
            gm_thddbqb6snabm t268;
            gm_b16_8 t269 = gm_l30;
            GM_ST(gm_b16_8, (uint8_t *)&t268 + 0, t269);
            uint64_t t270 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t271 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
            if (t270 >= (uintptr_t)(t271)) gm_trap_index(t270, t271, gm_srckg9scnip7e8g, 277, 80);
            uint64_t t272 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t270 * 8));
            GM_ST(uint64_t, (uint8_t *)&t268 + 16, t272);
            gm_b16_8 t273 = gm_l31;
            GM_ST(gm_b16_8, (uint8_t *)&t268 + 24, t273);
            uint64_t t274 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t275 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
            if (t274 >= (uintptr_t)(t275)) gm_trap_index(t274, t275, gm_srckg9scnip7e8g, 277, 93);
            uint64_t t276 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t274 * 8));
            GM_ST(uint64_t, (uint8_t *)&t268 + 40, t276);
            gm_tftb1odc739t5 t277 = gm_f_write_5stoou4(t267, &t268);
            gm_tftb1odc739t5 t278 = t277;
            if (((((uint8_t *)&t278))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t278) + 1, gm_srckg9scnip7e8g, 277, 17);
            gm_b0_1 t279 = GM_LD(gm_b0_1, (((uint8_t *)&t278) + 1));
        }
        goto me94;
mn243:;
ma100:;
        uint8_t t281 = GM_LD(uint8_t, (((uint8_t *)&t93) + 0));
        if (t281 != ((uint8_t)17ULL)) goto mn280;
        void * t282 = gm_l1;
        gm_t88rcucg1b7uk t283 = gm_l2;
        void * t284 = gm_f_adt_of_gej99vn(t282, GM_CONV(gm_csguankko4lmhd, t283));
        uint8_t t290 = GM_LD(uint8_t, ((((uint8_t *)t284) + 24) + 0));
        switch (t290) { case ((uint8_t)0ULL): goto ma286; case ((uint8_t)1ULL): goto ma287; case ((uint8_t)2ULL): goto ma288; default: goto ma289; }
ma286:;
        uint8_t t292 = GM_LD(uint8_t, ((((uint8_t *)t284) + 24) + 0));
        if (t292 != ((uint8_t)0ULL)) goto mn291;
        gm_tpautr4br7h3l t293 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t284) + 24) + 8));
        F.l33 = t293;
        {
            if (!(((((uint8_t *)&F.l33) + 40))[4] != 0)) goto ie295;
            gm_t88rcucg1b7uk t296 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l33) + 40));
            gm_l34 = t296;
            {
                void * t298 = gm_l0;
                void * t299 = gm_l1;
                gm_t88rcucg1b7uk t300 = gm_l34;
                gm_b16_8 t301 = gm_f_hash_fn_nlk82uj((void *)NULL, cx, NULL, t298, t299, t300);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l35 = t301;
            }
            {
                void * t302 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t303 = gm_l35;
                gm_tnpifv20irnph t304;
                GM_ST(gm_b16_8, ((uint8_t *)&t304 + 0), t303);
                gm_tftb1odc739t5 t305 = gm_f_write_sais187(t302, t304);
                gm_tftb1odc739t5 t306 = t305;
                if (((((uint8_t *)&t306))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t306) + 1, gm_srckg9scnip7e8g, 283, 25);
                gm_b0_1 t307 = GM_LD(gm_b0_1, (((uint8_t *)&t306) + 1));
            }
            goto ix295;
ie295:;
            {
                void * t309 = gm_l1;
                gm_t88rcucg1b7uk t310 = gm_l2;
                gm_b16_8 t311 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t309, t310);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l36 = t311;
            }
            {
                {
                    gm_b16_8 t313 = GM_LD(gm_b16_8, (((uint8_t *)&F.l33) + 0));
                    gm_b16_8 t314 = t313;
                    gm_b16_8 t315 = GM_LD(gm_b16_8, ((uint8_t *)&t314));
                    gm_l37 = t315;
                }
                {
                    gm_l38 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t317 = gm_l38;
                        uint64_t t318 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                        uint8_t t319 = (t317 < t318);
                        if (!t319) goto lb316;
                        {
                            gm_t8ta482bl6be0 t322;
                            uint64_t t323 = gm_l38;
                            GM_ST(uint64_t, (uint8_t *)&t322 + 0, t323);
                            uint64_t t324 = gm_l38;
                            uint64_t t325 = (uintptr_t)t324;
                            uint64_t t326 = GM_LD(uintptr_t, ((uint8_t *)&gm_l37) + 8);
                            memcpy((uint8_t *)&t322 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l37)) + t325 * 40), 40);
                            gm_t8ta482bl6be0 t327 = t322;
                            uint64_t t330 = GM_LD(uint64_t, (((uint8_t *)&t327) + 0));
                            gm_l39 = t330;
                            gm_t0mglksrkfa5e t331 = GM_LD(gm_t0mglksrkfa5e, (((uint8_t *)&t327) + 8));
                            F.l40 = t331;
                            {
                                void * t333 = gm_l0;
                                void * t334 = gm_l1;
                                gm_t88rcucg1b7uk t335 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l40) + 16));
                                gm_b16_8 t336 = gm_f_hash_fn_nlk82uj((void *)NULL, cx, NULL, t333, t334, t335);
                                if (gm_unw.kind) {
                                    goto gm_ll320;
                                }
                                gm_l41 = t336;
                            }
                            {
                                void * t337 = (void *)((uint8_t *)&F.l5);
                                gm_t3877pk7lv748 t338;
                                gm_b16_8 t339 = gm_l41;
                                GM_ST(gm_b16_8, (uint8_t *)&t338 + 0, t339);
                                uint64_t t340 = gm_l39;
                                uint64_t t341 = (uintptr_t)t340;
                                uint64_t t342 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
                                if (t341 >= (uintptr_t)(t342)) gm_trap_index(t341, t342, gm_srckg9scnip7e8g, 288, 68);
                                uint64_t t343 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l36)) + t341 * 8));
                                GM_ST(uint64_t, (uint8_t *)&t338 + 16, t343);
                                gm_tftb1odc739t5 t344 = gm_f_write_u9rtsjt(t337, &t338);
                                gm_tftb1odc739t5 t345 = t344;
                                if (((((uint8_t *)&t345))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t345) + 1, gm_srckg9scnip7e8g, 288, 29);
                                gm_b0_1 t346 = GM_LD(gm_b0_1, (((uint8_t *)&t345) + 1));
                            }
                            goto me328;
mn329:;
                            gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 286, 25);
me328:;
                        }
lc316:;
                        {
                            uint64_t t348 = gm_l38;
                            uint64_t r349; bool o350 = __builtin_add_overflow(t348, ((uint64_t)1ULL), &r349);
                            uint64_t t351 = r349;
                            gm_l38 = t351;
                        }
                    }
gm_ll320:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb316; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc316; }
                    }
                    goto gm_lad;
lb316:;
                }
            }
            {
                void * t352 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t353;
                GM_ST(void *, (uint8_t *)&t353, (void *)(gm_sel1fj24qk32p));
                GM_ST(uintptr_t, (uint8_t *)&t353 + 8, (uintptr_t)(14));
                gm_tftb1odc739t5 t354 = gm_f_extend_0cs85gs(t352, t353);
                gm_tftb1odc739t5 t355 = t354;
                if (((((uint8_t *)&t355))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t355) + 1, gm_srckg9scnip7e8g, 290, 25);
                gm_b0_1 t356 = GM_LD(gm_b0_1, (((uint8_t *)&t355) + 1));
            }
ix295:;
        }
        goto me285;
mn291:;
ma287:;
        uint8_t t358 = GM_LD(uint8_t, ((((uint8_t *)t284) + 24) + 0));
        if (t358 != ((uint8_t)1ULL)) goto mn357;
        gm_t88rcucg1b7uk t359 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t284) + 24) + 8) + 0));
        gm_l42 = t359;
        gm_b16_8 t360 = GM_LD(gm_b16_8, (((((uint8_t *)t284) + 24) + 8) + 8));
        gm_l43 = t360;
        {
            void * t362 = gm_l1;
            gm_t88rcucg1b7uk t363 = gm_l2;
            gm_b16_8 t364 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t362, t363);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l44 = t364;
        }
        {
            void * t365 = gm_l1;
            gm_t88rcucg1b7uk t366 = gm_l42;
            gm_b16_8 t367 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t365, t366);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l45 = t367;
        }
        {
            void * t368 = (void *)((uint8_t *)&F.l5);
            gm_tftksr4op114e t369;
            gm_b16_8 t370 = gm_l45;
            GM_ST(gm_b16_8, (uint8_t *)&t369 + 0, t370);
            gm_b16_8 t371 = gm_l45;
            GM_ST(gm_b16_8, (uint8_t *)&t369 + 16, t371);
            uint64_t t372 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t373 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
            if (t372 >= (uintptr_t)(t373)) gm_trap_index(t372, t373, gm_srckg9scnip7e8g, 296, 73);
            uint64_t t374 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t372 * 8));
            GM_ST(uint64_t, (uint8_t *)&t369 + 32, t374);
            gm_tftb1odc739t5 t375 = gm_f_write_551e4in(t368, &t369);
            gm_tftb1odc739t5 t376 = t375;
            if (((((uint8_t *)&t376))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t376) + 1, gm_srckg9scnip7e8g, 296, 21);
            gm_b0_1 t377 = GM_LD(gm_b0_1, (((uint8_t *)&t376) + 1));
        }
        {
            {
                gm_b16_8 t379 = gm_l43;
                gm_b16_8 t380 = t379;
                gm_b16_8 t381 = GM_LD(gm_b16_8, ((uint8_t *)&t380));
                gm_l46 = t381;
            }
            {
                gm_l47 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t383 = gm_l47;
                    uint64_t t384 = GM_LD(uintptr_t, ((uint8_t *)&gm_l46) + 8);
                    uint8_t t385 = (t383 < t384);
                    if (!t385) goto lb382;
                    {
                        gm_t3l8idb2m6c1r t388;
                        uint64_t t389 = gm_l47;
                        GM_ST(uint64_t, (uint8_t *)&t388 + 0, t389);
                        uint64_t t390 = gm_l47;
                        uint64_t t391 = (uintptr_t)t390;
                        uint64_t t392 = GM_LD(uintptr_t, ((uint8_t *)&gm_l46) + 8);
                        memcpy((uint8_t *)&t388 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l46)) + t391 * 72), 72);
                        gm_t3l8idb2m6c1r t393 = t388;
                        uint64_t t396 = GM_LD(uint64_t, (((uint8_t *)&t393) + 0));
                        gm_l48 = t396;
                        gm_tckdobunaulee t397 = GM_LD(gm_tckdobunaulee, (((uint8_t *)&t393) + 8));
                        F.l49 = t397;
                        {
                            void * t399 = (void *)((uint8_t *)&F.l5);
                            gm_t6j6smj7qoctu t400;
                            gm_b16_8 t401 = gm_l45;
                            GM_ST(gm_b16_8, (uint8_t *)&t400 + 0, t401);
                            gm_b16_8 t402 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l49) + 24)));
                            if (gm_unw.kind) {
                                goto gm_ll386;
                            }
                            gm_b16_8 t403 = t402;
                            gm_b16_8 t404 = GM_LD(gm_b16_8, ((uint8_t *)&t403));
                            GM_ST(gm_b16_8, (uint8_t *)&t400 + 16, t404);
                            gm_tftb1odc739t5 t405 = gm_f_write_2tci68e(t399, &t400);
                            gm_tftb1odc739t5 t406 = t405;
                            if (((((uint8_t *)&t406))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t406) + 1, gm_srckg9scnip7e8g, 298, 25);
                            gm_b0_1 t407 = GM_LD(gm_b0_1, (((uint8_t *)&t406) + 1));
                        }
                        {
                            void * t408 = (void *)((uint8_t *)&F.l5);
                            uint64_t t409 = gm_l48;
                            gm_t4ce0jd3tdro4 t410;
                            GM_ST(uint64_t, ((uint8_t *)&t410 + 0), t409);
                            gm_tftb1odc739t5 t411 = gm_f_write_4l0f7de(t408, t410);
                            gm_tftb1odc739t5 t412 = t411;
                            if (((((uint8_t *)&t412))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t412) + 1, gm_srckg9scnip7e8g, 299, 25);
                            gm_b0_1 t413 = GM_LD(gm_b0_1, (((uint8_t *)&t412) + 1));
                        }
                        {
                            if (!(((((uint8_t *)&F.l49) + 16))[4] != 0)) goto ie414;
                            gm_t88rcucg1b7uk t415 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l49) + 16));
                            gm_l50 = t415;
                            {
                                void * t417 = gm_l0;
                                void * t418 = gm_l1;
                                gm_t88rcucg1b7uk t419 = gm_l50;
                                gm_b16_8 t420 = gm_f_hash_fn_nlk82uj((void *)NULL, cx, NULL, t417, t418, t419);
                                if (gm_unw.kind) {
                                    goto gm_ll386;
                                }
                                gm_l51 = t420;
                            }
                            {
                                void * t421 = (void *)((uint8_t *)&F.l5);
                                gm_t3877pk7lv748 t422;
                                gm_b16_8 t423 = gm_l51;
                                GM_ST(gm_b16_8, (uint8_t *)&t422 + 0, t423);
                                uint64_t t424 = (uintptr_t)((uint64_t)1ULL);
                                uint64_t t425 = GM_LD(uintptr_t, ((uint8_t *)&gm_l44) + 8);
                                if (t424 >= (uintptr_t)(t425)) gm_trap_index(t424, t425, gm_srckg9scnip7e8g, 302, 72);
                                uint64_t t426 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l44)) + t424 * 8));
                                GM_ST(uint64_t, (uint8_t *)&t422 + 16, t426);
                                gm_tftb1odc739t5 t427 = gm_f_write_t9pa6sj(t421, &t422);
                                gm_tftb1odc739t5 t428 = t427;
                                if (((((uint8_t *)&t428))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t428) + 1, gm_srckg9scnip7e8g, 302, 29);
                                gm_b0_1 t429 = GM_LD(gm_b0_1, (((uint8_t *)&t428) + 1));
                            }
                            goto ix414;
ie414:;
ix414:;
                        }
                        {
                            void * t430 = (void *)((uint8_t *)&F.l5);
                            gm_b16_8 t431;
                            GM_ST(void *, (uint8_t *)&t431, (void *)(gm_sciq0a78hirud));
                            GM_ST(uintptr_t, (uint8_t *)&t431 + 8, (uintptr_t)(24));
                            gm_tftb1odc739t5 t432 = gm_f_extend_0cs85gs(t430, t431);
                            gm_tftb1odc739t5 t433 = t432;
                            if (((((uint8_t *)&t433))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t433) + 1, gm_srckg9scnip7e8g, 304, 25);
                            gm_b0_1 t434 = GM_LD(gm_b0_1, (((uint8_t *)&t433) + 1));
                        }
                        goto me394;
mn395:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 297, 21);
me394:;
                    }
lc382:;
                    {
                        uint64_t t436 = gm_l47;
                        uint64_t r437; bool o438 = __builtin_add_overflow(t436, ((uint64_t)1ULL), &r437);
                        uint64_t t439 = r437;
                        gm_l47 = t439;
                    }
                }
gm_ll386:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb382; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc382; }
                }
                goto gm_lad;
lb382:;
            }
        }
        {
            void * t440 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t441;
            GM_ST(void *, (uint8_t *)&t441, (void *)(gm_sel1fj24qk32p));
            GM_ST(uintptr_t, (uint8_t *)&t441 + 8, (uintptr_t)(14));
            gm_tftb1odc739t5 t442 = gm_f_extend_0cs85gs(t440, t441);
            gm_tftb1odc739t5 t443 = t442;
            if (((((uint8_t *)&t443))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t443) + 1, gm_srckg9scnip7e8g, 306, 21);
            gm_b0_1 t444 = GM_LD(gm_b0_1, (((uint8_t *)&t443) + 1));
        }
        goto me285;
mn357:;
ma288:;
        uint8_t t446 = GM_LD(uint8_t, ((((uint8_t *)t284) + 24) + 0));
        if (t446 != ((uint8_t)2ULL)) goto mn445;
        void * t447 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t448;
        GM_ST(void *, (uint8_t *)&t448, (void *)(gm_sel1fj24qk32p));
        GM_ST(uintptr_t, (uint8_t *)&t448 + 8, (uintptr_t)(14));
        gm_tftb1odc739t5 t449 = gm_f_extend_0cs85gs(t447, t448);
        gm_tftb1odc739t5 t450 = t449;
        if (((((uint8_t *)&t450))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t450) + 1, gm_srckg9scnip7e8g, 308, 31);
        gm_b0_1 t451 = GM_LD(gm_b0_1, (((uint8_t *)&t450) + 1));
        goto me285;
mn445:;
ma289:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 279, 27);
me285:;
        goto me94;
mn280:;
ma101:;
        void * t453 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t454;
        GM_ST(void *, (uint8_t *)&t454, (void *)(gm_sel1fj24qk32p));
        GM_ST(uintptr_t, (uint8_t *)&t454 + 8, (uintptr_t)(14));
        gm_tftb1odc739t5 t455 = gm_f_extend_0cs85gs(t453, t454);
        gm_tftb1odc739t5 t456 = t455;
        if (((((uint8_t *)&t456))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t456) + 1, gm_srckg9scnip7e8g, 310, 18);
        gm_b0_1 t457 = GM_LD(gm_b0_1, (((uint8_t *)&t456) + 1));
        goto me94;
mn452:;
ma102:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 241, 14);
me94:;
        goto me27;
mn89:;
ma33:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 228, 5);
me27:;
    }
    {
        void * t458 = gm_l0;
        gm_b16_8 t459 = gm_l4;
        gm_b16_8 t460;
        GM_ST(void *, (uint8_t *)&t460, (void *)(gm_shn8mjp9kc2n2));
        GM_ST(uintptr_t, (uint8_t *)&t460 + 8, (uintptr_t)(8));
        gm_b16_8 t461;
        GM_ST(void *, (uint8_t *)&t461, (void *)(gm_s81r46mpa8ra9));
        GM_ST(uintptr_t, (uint8_t *)&t461 + 8, (uintptr_t)(28));
        gm_b16_8 t462 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t463 = t462;
        gm_b16_8 t464 = GM_LD(gm_b16_8, ((uint8_t *)&t463));
        gm_b0_1 t465 = gm_f_define_helper_c1utd1n((void *)NULL, cx, NULL, t458, t459, t460, t461, t464);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t466 = gm_l4;
        uint8_t t467 = 0;
        F.ret = t466;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_helper_name_fob48de(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_helper_name_fob48de F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_b16_8 gm_l2; /* kind */
    gm_t88rcucg1b7uk gm_l3; /* t */
    GM_STACK_CHECK(&F, 256u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 248);
    gm_b16_8 t3 = gm_l2;
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    gm_tabb710d9mq2e t5;
    gm_f_format_39snre1((void *)&t5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t6 = t5;
    if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srckg9scnip7e8g, 28, 105);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    void * t10 = gm_l1;
    gm_t88rcucg1b7uk t11 = gm_l3;
    gm_b16_8 t12 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t13 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t2, t9, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t13;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b16_8 gm_f_less_fn_64um0ab(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_less_fn_64um0ab F;
    void * gm_l0; /* h */
    void * gm_l1; /* ty */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* n */
    gm_b16_8 gm_l4; /* name */
    uint64_t gm_l6; /* p */
    gm_b16_8 gm_l7; /* c */
    gm_b16_8 gm_l8; /* elems */
    gm_b16_8 gm_l9; /* offs */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* et */
    gm_b16_8 gm_l14; /* fe */
    gm_b16_8 gm_l15; /* fl */
    uint64_t gm_l16; /* o */
    gm_t88rcucg1b7uk gm_l17; /* elem */
    gm_tf5srh4mp4b3i gm_l18; /* array_len */
    uint64_t gm_l19; /* len */
    gm_t88rcucg1b7uk gm_l20; /* el */
    gm_t88rcucg1b7uk gm_l21; /* el */
    uint64_t gm_l22; /* esz */
    uint64_t gm_l23; /* n */
    gm_b16_8 gm_l24; /* fe */
    gm_b16_8 gm_l25; /* fl */
    gm_t88rcucg1b7uk gm_l26; /* tag */
    gm_b16_8 gm_l27; /* tc */
    uint64_t gm_l28; /* o */
    gm_t88rcucg1b7uk gm_l30; /* u */
    gm_b16_8 gm_l31; /* f */
    GM_STACK_CHECK(&F, 2575u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = (void *)(((uint8_t *)t3) + 136);
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 0));
        gm_th2lqbo77pvsc t6;
        gm_f_get_gnmase9((void *)&t6, t4, t5);
        gm_th2lqbo77pvsc t7 = t6;
        if (!((((uint8_t *)&t7))[16] != 0)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_l3 = t8;
        {
            gm_b16_8 t10 = gm_l3;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        void * t13 = gm_l1;
        void * t14 = gm_l0;
        void * t15 = (void *)(((uint8_t *)t14) + 136);
        gm_b16_8 t16;
        GM_ST(void *, (uint8_t *)&t16, (void *)(gm_surrjo1qo53mh));
        GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(2));
        gm_t88rcucg1b7uk t17 = gm_l2;
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(4));
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sqg70d5uem21e));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(34));
        gm_b16_8 t20 = gm_f_declare_helper_k8mi0a4((void *)NULL, cx, NULL, t12, t13, t15, t16, t17, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t20;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t21 = gm_l1;
        uint64_t t22 = GM_LD(uint64_t, (((((uint8_t *)t21) + 8) + 0) + 0));
        gm_l6 = t22;
    }
    {
        void * t23 = gm_l1;
        gm_t88rcucg1b7uk t24 = gm_l2;
        gm_t5v8n6qrn4rh4 t25;
        gm_f_repr_53quqki((void *)&t25, (void *)NULL, cx, NULL, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t26 = t25;
        uint8_t t31 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t31 != ((uint8_t)0ULL)) goto pa30;
        goto po29;
pa30:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t33 != ((uint8_t)1ULL)) goto pa32;
        goto po29;
pa32:;
        uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t35 != ((uint8_t)3ULL)) goto pa34;
        goto po29;
pa34:;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t26) + 0));
        if (t37 != ((uint8_t)2ULL)) goto pa36;
        goto po29;
pa36:;
        goto mn28;
po29:;
        {
            void * t39 = gm_l1;
            gm_t88rcucg1b7uk t40 = gm_l2;
            gm_b16_8 t41 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t39, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t41;
        }
        {
            void * t42 = (void *)((uint8_t *)&F.l5);
            gm_t6j6smj7qoctu t43;
            gm_b16_8 t44 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t43 + 0, t44);
            gm_b16_8 t45 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t43 + 16, t45);
            gm_tftb1odc739t5 t46 = gm_f_write_ikd2l7g(t42, &t43);
            gm_tftb1odc739t5 t47 = t46;
            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srckg9scnip7e8g, 158, 13);
            gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
        }
        goto me27;
mn28:;
        void * t50 = gm_l1;
        gm_t88rcucg1b7uk t51 = gm_l2;
        gm_t53at80pfdus3 t52;
        gm_f_kind_ghvav22((void *)&t52, t50, GM_CONV(gm_csguankko4lmhd, t51));
        gm_t53at80pfdus3 t53 = t52;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        switch (t60) { case ((uint8_t)0ULL): goto ma58; case ((uint8_t)1ULL): goto ma58; case ((uint8_t)2ULL): goto ma58; case ((uint8_t)3ULL): goto ma58; case ((uint8_t)4ULL): goto ma58; case ((uint8_t)5ULL): goto ma58; case ((uint8_t)6ULL): goto ma58; case ((uint8_t)7ULL): goto ma58; case ((uint8_t)8ULL): goto ma58; case ((uint8_t)9ULL): goto ma58; case ((uint8_t)10ULL): goto ma56; case ((uint8_t)11ULL): goto ma56; case ((uint8_t)12ULL): goto ma55; case ((uint8_t)13ULL): goto ma58; case ((uint8_t)14ULL): goto ma58; case ((uint8_t)15ULL): goto ma58; case ((uint8_t)16ULL): goto ma58; case ((uint8_t)17ULL): goto ma57; case ((uint8_t)18ULL): goto ma58; case ((uint8_t)19ULL): goto ma58; case ((uint8_t)20ULL): goto ma58; case ((uint8_t)21ULL): goto ma58; case ((uint8_t)22ULL): goto ma58; case ((uint8_t)23ULL): goto ma58; case ((uint8_t)24ULL): goto ma58; case ((uint8_t)25ULL): goto ma58; case ((uint8_t)26ULL): goto ma58; default: goto ma58; }
ma55:;
        uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        if (t62 != ((uint8_t)12ULL)) goto mn61;
        gm_b16_8 t63 = GM_LD(gm_b16_8, ((((uint8_t *)&t53) + 8) + 0));
        gm_l8 = t63;
        {
            void * t65 = gm_l1;
            gm_t88rcucg1b7uk t66 = gm_l2;
            gm_b16_8 t67 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t65, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t67;
        }
        {
            {
                gm_b16_8 t69 = gm_l8;
                gm_l10 = t69;
            }
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t71 = gm_l11;
                    uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    uint8_t t73 = (t71 < t72);
                    if (!t73) goto lb70;
                    {
                        uint64_t t76 = gm_l11;
                        uint64_t t77 = gm_l11;
                        uint64_t t78 = (uintptr_t)t77;
                        uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        gm_t88rcucg1b7uk t80 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t78 * 4));
                        gm_tlcm4lvu3g1a8 t81;
                        GM_ST(uint32_t, (uint8_t *)&t81 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t81 + 0), t76);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t81 + 8), t80);
                        gm_tlcm4lvu3g1a8 t82 = t81;
                        uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)&t82) + 0));
                        gm_l12 = t85;
                        gm_t88rcucg1b7uk t86 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t82) + 8));
                        gm_l13 = t86;
                        {
                            void * t88 = gm_l0;
                            void * t89 = gm_l1;
                            gm_t88rcucg1b7uk t90 = gm_l13;
                            gm_b16_8 t91 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t88, t89, t90);
                            if (gm_unw.kind) {
                                goto gm_ll74;
                            }
                            gm_l14 = t91;
                        }
                        {
                            void * t92 = gm_l0;
                            void * t93 = gm_l1;
                            gm_t88rcucg1b7uk t94 = gm_l13;
                            gm_b16_8 t95 = gm_f_less_fn_64um0ab((void *)NULL, cx, NULL, t92, t93, t94);
                            if (gm_unw.kind) {
                                goto gm_ll74;
                            }
                            gm_l15 = t95;
                        }
                        {
                            uint64_t t96 = gm_l12;
                            uint64_t t97 = (uintptr_t)t96;
                            uint64_t t98 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            if (t97 >= (uintptr_t)(t98)) gm_trap_index(t97, t98, gm_srckg9scnip7e8g, 166, 26);
                            uint64_t t99 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t97 * 8));
                            gm_l16 = t99;
                        }
                        {
                            void * t100 = (void *)((uint8_t *)&F.l5);
                            gm_tr4ummvbupakb t101;
                            gm_b16_8 t102 = gm_l14;
                            GM_ST(gm_b16_8, (uint8_t *)&t101 + 0, t102);
                            uint64_t t103 = gm_l16;
                            GM_ST(uint64_t, (uint8_t *)&t101 + 16, t103);
                            uint64_t t104 = gm_l16;
                            GM_ST(uint64_t, (uint8_t *)&t101 + 24, t104);
                            gm_b16_8 t105 = gm_l15;
                            GM_ST(gm_b16_8, (uint8_t *)&t101 + 32, t105);
                            uint64_t t106 = gm_l16;
                            GM_ST(uint64_t, (uint8_t *)&t101 + 48, t106);
                            uint64_t t107 = gm_l16;
                            GM_ST(uint64_t, (uint8_t *)&t101 + 56, t107);
                            gm_tftb1odc739t5 t108 = gm_f_write_e15chue(t100, &t101);
                            gm_tftb1odc739t5 t109 = t108;
                            if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 1, gm_srckg9scnip7e8g, 167, 21);
                            gm_b0_1 t110 = GM_LD(gm_b0_1, (((uint8_t *)&t109) + 1));
                        }
                        goto me83;
mn84:;
                        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 163, 17);
me83:;
                    }
lc70:;
                    {
                        uint64_t t112 = gm_l11;
                        uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
                        uint64_t t115 = r113;
                        gm_l11 = t115;
                    }
                }
gm_ll74:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb70; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc70; }
                }
                goto gm_lad;
lb70:;
            }
        }
        {
            void * t116 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t117;
            GM_ST(void *, (uint8_t *)&t117, (void *)(gm_sug64ltbhov0m));
            GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(18));
            gm_tftb1odc739t5 t118 = gm_f_extend_0cs85gs(t116, t117);
            gm_tftb1odc739t5 t119 = t118;
            if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 1, gm_srckg9scnip7e8g, 169, 17);
            gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
        }
        goto me54;
mn61:;
ma56:;
        uint8_t t124 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        if (t124 != ((uint8_t)11ULL)) goto pa123;
        goto po122;
pa123:;
        uint8_t t126 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        if (t126 != ((uint8_t)10ULL)) goto pa125;
        goto po122;
pa125:;
        goto mn121;
po122:;
        {
            gm_t88rcucg1b7uk t128 = gm_l2;
            gm_l17 = t128;
        }
        {
            gm_tf5srh4mp4b3i t129;
            memset(&t129, 0, sizeof t129);
            gm_l18 = t129;
        }
        {
            void * t130 = gm_l1;
            gm_t88rcucg1b7uk t131 = gm_l2;
            gm_t53at80pfdus3 t132;
            gm_f_kind_ghvav22((void *)&t132, t130, GM_CONV(gm_csguankko4lmhd, t131));
            gm_t53at80pfdus3 t133 = t132;
            uint8_t t139 = GM_LD(uint8_t, (((uint8_t *)&t133) + 0));
            switch (t139) { case ((uint8_t)0ULL): goto ma137; case ((uint8_t)1ULL): goto ma137; case ((uint8_t)2ULL): goto ma137; case ((uint8_t)3ULL): goto ma137; case ((uint8_t)4ULL): goto ma137; case ((uint8_t)5ULL): goto ma137; case ((uint8_t)6ULL): goto ma137; case ((uint8_t)7ULL): goto ma137; case ((uint8_t)8ULL): goto ma137; case ((uint8_t)9ULL): goto ma137; case ((uint8_t)10ULL): goto ma136; case ((uint8_t)11ULL): goto ma135; case ((uint8_t)12ULL): goto ma137; case ((uint8_t)13ULL): goto ma137; case ((uint8_t)14ULL): goto ma137; case ((uint8_t)15ULL): goto ma137; case ((uint8_t)16ULL): goto ma137; case ((uint8_t)17ULL): goto ma137; case ((uint8_t)18ULL): goto ma137; case ((uint8_t)19ULL): goto ma137; case ((uint8_t)20ULL): goto ma137; case ((uint8_t)21ULL): goto ma137; case ((uint8_t)22ULL): goto ma137; case ((uint8_t)23ULL): goto ma137; case ((uint8_t)24ULL): goto ma137; case ((uint8_t)25ULL): goto ma137; case ((uint8_t)26ULL): goto ma137; default: goto ma137; }
ma135:;
            uint8_t t141 = GM_LD(uint8_t, (((uint8_t *)&t133) + 0));
            if (t141 != ((uint8_t)11ULL)) goto mn140;
            uint64_t t142 = GM_LD(uint64_t, ((((uint8_t *)&t133) + 8) + 0));
            gm_l19 = t142;
            gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t133) + 8) + 8));
            gm_l20 = t143;
            {
                gm_t88rcucg1b7uk t145 = gm_l20;
                gm_l17 = t145;
            }
            {
                uint64_t t146 = gm_l19;
                gm_tf5srh4mp4b3i t147;
                GM_ST(uint8_t, (uint8_t *)&t147 + 9, 0);
                GM_ST(uint16_t, (uint8_t *)&t147 + 10, 0);
                GM_ST(uint32_t, (uint8_t *)&t147 + 12, 0);
                GM_ST(uint64_t, ((uint8_t *)&t147), t146);
                ((uint8_t *)&t147)[8] = 1;
                gm_l18 = t147;
            }
            goto me134;
mn140:;
ma136:;
            uint8_t t149 = GM_LD(uint8_t, (((uint8_t *)&t133) + 0));
            if (t149 != ((uint8_t)10ULL)) goto mn148;
            gm_t88rcucg1b7uk t150 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t133) + 8) + 4));
            gm_l21 = t150;
            {
                gm_t88rcucg1b7uk t152 = gm_l21;
                gm_l17 = t152;
            }
            goto me134;
mn148:;
ma137:;
            gm_trap("reached `@unreachable()`", gm_srckg9scnip7e8g, 180, 26);
            goto me134;
mn153:;
ma138:;
            gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 174, 17);
me134:;
        }
        {
            void * t154 = gm_l1;
            gm_t88rcucg1b7uk t155 = gm_l17;
            uint64_t t156 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t154, t155);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l22 = t156;
        }
        {
            if (!((((uint8_t *)&gm_l18))[8] != 0)) goto ie157;
            uint64_t t158 = GM_LD(uint64_t, ((uint8_t *)&gm_l18));
            gm_l23 = t158;
            {
                void * t160 = (void *)((uint8_t *)&F.l5);
                uint64_t t161 = gm_l23;
                uint64_t t162 = gm_l23;
                gm_t2lqu8jsj511d t163;
                GM_ST(uint64_t, ((uint8_t *)&t163 + 0), t161);
                GM_ST(uint64_t, ((uint8_t *)&t163 + 8), t162);
                gm_tftb1odc739t5 t164 = gm_f_write_8tfqc0p(t160, t163);
                gm_tftb1odc739t5 t165 = t164;
                if (((((uint8_t *)&t165))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t165) + 1, gm_srckg9scnip7e8g, 184, 21);
                gm_b0_1 t166 = GM_LD(gm_b0_1, (((uint8_t *)&t165) + 1));
            }
            goto ix157;
ie157:;
            {
                void * t168 = (void *)((uint8_t *)&F.l5);
                uint64_t t169 = gm_l6;
                uint64_t t170 = gm_l6;
                gm_t2lqu8jsj511d t171;
                GM_ST(uint64_t, ((uint8_t *)&t171 + 0), t169);
                GM_ST(uint64_t, ((uint8_t *)&t171 + 8), t170);
                gm_tftb1odc739t5 t172 = gm_f_write_k88klp2(t168, t171);
                gm_tftb1odc739t5 t173 = t172;
                if (((((uint8_t *)&t173))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t173) + 1, gm_srckg9scnip7e8g, 186, 21);
                gm_b0_1 t174 = GM_LD(gm_b0_1, (((uint8_t *)&t173) + 1));
            }
            {
                void * t175 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t176;
                GM_ST(void *, (uint8_t *)&t176, (void *)(gm_sor8flejv9lfq));
                GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)(66));
                gm_tftb1odc739t5 t177 = gm_f_extend_0cs85gs(t175, t176);
                gm_tftb1odc739t5 t178 = t177;
                if (((((uint8_t *)&t178))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t178) + 1, gm_srckg9scnip7e8g, 187, 21);
                gm_b0_1 t179 = GM_LD(gm_b0_1, (((uint8_t *)&t178) + 1));
            }
ix157:;
        }
        {
            void * t181 = gm_l1;
            gm_t88rcucg1b7uk t182 = gm_l17;
            gm_t5v8n6qrn4rh4 t183;
            gm_f_repr_53quqki((void *)&t183, (void *)NULL, cx, NULL, t181, t182);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5v8n6qrn4rh4 t184 = t183;
            gm_tuglhjjmj2g58 t185;
            GM_ST(uint8_t, (uint8_t *)&t185 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t185 + 2, 0);
            GM_ST(uint8_t, ((uint8_t *)&t185 + 0), ((uint8_t)0));
            GM_ST(uint32_t, ((uint8_t *)&t185 + 4), ((uint32_t)8ULL));
            GM_ST(uint64_t, ((uint8_t *)&t185 + 8), ((uint64_t)1ULL));
            gm_tuglhjjmj2g58 t186 = t185;
            uint8_t t187 = (((GM_LD(uint8_t, ((uint8_t *)&t184) + 0) == ((uint8_t)0ULL)) && gm_eqvs1pd373b1((((uint8_t *)&t184) + 8), ((uint8_t *)&t186))));
            if (!t187) goto ie180;
            {
                void * t189 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t190;
                GM_ST(void *, (uint8_t *)&t190, (void *)(gm_sup5ecv5jiqbn));
                GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(118));
                gm_tftb1odc739t5 t191 = gm_f_extend_0cs85gs(t189, t190);
                gm_tftb1odc739t5 t192 = t191;
                if (((((uint8_t *)&t192))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t192) + 1, gm_srckg9scnip7e8g, 191, 21);
                gm_b0_1 t193 = GM_LD(gm_b0_1, (((uint8_t *)&t192) + 1));
            }
            goto ix180;
ie180:;
            {
                void * t195 = gm_l0;
                void * t196 = gm_l1;
                gm_t88rcucg1b7uk t197 = gm_l17;
                gm_b16_8 t198 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t195, t196, t197);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t198;
            }
            {
                void * t199 = gm_l0;
                void * t200 = gm_l1;
                gm_t88rcucg1b7uk t201 = gm_l17;
                gm_b16_8 t202 = gm_f_less_fn_64um0ab((void *)NULL, cx, NULL, t199, t200, t201);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l25 = t202;
            }
            {
                void * t203 = (void *)((uint8_t *)&F.l5);
                gm_tr4ummvbupakb t204;
                gm_b16_8 t205 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t204 + 0, t205);
                uint64_t t206 = gm_l22;
                GM_ST(uint64_t, (uint8_t *)&t204 + 16, t206);
                uint64_t t207 = gm_l22;
                GM_ST(uint64_t, (uint8_t *)&t204 + 24, t207);
                gm_b16_8 t208 = gm_l25;
                GM_ST(gm_b16_8, (uint8_t *)&t204 + 32, t208);
                uint64_t t209 = gm_l22;
                GM_ST(uint64_t, (uint8_t *)&t204 + 48, t209);
                uint64_t t210 = gm_l22;
                GM_ST(uint64_t, (uint8_t *)&t204 + 56, t210);
                gm_tftb1odc739t5 t211 = gm_f_write_aakaioo(t203, &t204);
                gm_tftb1odc739t5 t212 = t211;
                if (((((uint8_t *)&t212))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t212) + 1, gm_srckg9scnip7e8g, 195, 21);
                gm_b0_1 t213 = GM_LD(gm_b0_1, (((uint8_t *)&t212) + 1));
            }
            {
                void * t214 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t215;
                GM_ST(void *, (uint8_t *)&t215, (void *)(gm_sge6a56sqn6kj));
                GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(20));
                gm_tftb1odc739t5 t216 = gm_f_extend_0cs85gs(t214, t215);
                gm_tftb1odc739t5 t217 = t216;
                if (((((uint8_t *)&t217))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t217) + 1, gm_srckg9scnip7e8g, 196, 21);
                gm_b0_1 t218 = GM_LD(gm_b0_1, (((uint8_t *)&t217) + 1));
            }
ix180:;
        }
        goto me54;
mn121:;
ma57:;
        uint8_t t220 = GM_LD(uint8_t, (((uint8_t *)&t53) + 0));
        if (t220 != ((uint8_t)17ULL)) goto mn219;
        void * t221 = gm_l1;
        gm_t88rcucg1b7uk t222 = gm_l2;
        void * t223 = gm_f_adt_of_gej99vn(t221, GM_CONV(gm_csguankko4lmhd, t222));
        uint8_t t229 = GM_LD(uint8_t, ((((uint8_t *)t223) + 24) + 0));
        switch (t229) { case ((uint8_t)0ULL): goto ma226; case ((uint8_t)1ULL): goto ma225; case ((uint8_t)2ULL): goto ma227; default: goto ma227; }
ma225:;
        uint8_t t231 = GM_LD(uint8_t, ((((uint8_t *)t223) + 24) + 0));
        if (t231 != ((uint8_t)1ULL)) goto mn230;
        gm_t88rcucg1b7uk t232 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t223) + 24) + 8) + 0));
        gm_l26 = t232;
        {
            void * t234 = gm_l1;
            gm_t88rcucg1b7uk t235 = gm_l26;
            gm_b16_8 t236 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t234, t235);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l27 = t236;
        }
        {
            uint64_t t237 = (uintptr_t)((uint64_t)0ULL);
            void * t238 = gm_l1;
            gm_t88rcucg1b7uk t239 = gm_l2;
            gm_b16_8 t240 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t238, t239);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t241 = t240;
            uint64_t t242 = GM_LD(uintptr_t, ((uint8_t *)&t241) + 8);
            if (t237 >= (uintptr_t)(t242)) gm_trap_index(t237, t242, gm_srckg9scnip7e8g, 202, 26);
            uint64_t t243 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t241)) + t237 * 8));
            gm_l28 = t243;
        }
        {
            void * t244 = (void *)((uint8_t *)&F.l5);
            gm_thddbqb6snabm t245;
            gm_b16_8 t246 = gm_l27;
            GM_ST(gm_b16_8, (uint8_t *)&t245 + 0, t246);
            uint64_t t247 = gm_l28;
            GM_ST(uint64_t, (uint8_t *)&t245 + 16, t247);
            gm_b16_8 t248 = gm_l27;
            GM_ST(gm_b16_8, (uint8_t *)&t245 + 24, t248);
            uint64_t t249 = gm_l28;
            GM_ST(uint64_t, (uint8_t *)&t245 + 40, t249);
            gm_tftb1odc739t5 t250 = gm_f_write_91uvm3b(t244, &t245);
            gm_tftb1odc739t5 t251 = t250;
            if (((((uint8_t *)&t251))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t251) + 1, gm_srckg9scnip7e8g, 203, 21);
            gm_b0_1 t252 = GM_LD(gm_b0_1, (((uint8_t *)&t251) + 1));
        }
        goto me224;
mn230:;
ma226:;
        uint8_t t254 = GM_LD(uint8_t, ((((uint8_t *)t223) + 24) + 0));
        if (t254 != ((uint8_t)0ULL)) goto mn253;
        gm_tpautr4br7h3l t255 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)t223) + 24) + 8));
        F.l29 = t255;
        {
            if (!(((((uint8_t *)&F.l29) + 40))[4] != 0)) goto ie257;
            gm_t88rcucg1b7uk t258 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l29) + 40));
            gm_l30 = t258;
            {
                void * t260 = gm_l0;
                void * t261 = gm_l1;
                gm_t88rcucg1b7uk t262 = gm_l30;
                gm_b16_8 t263 = gm_f_less_fn_64um0ab((void *)NULL, cx, NULL, t260, t261, t262);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l31 = t263;
            }
            {
                void * t264 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t265 = gm_l31;
                gm_tnpifv20irnph t266;
                GM_ST(gm_b16_8, ((uint8_t *)&t266 + 0), t265);
                gm_tftb1odc739t5 t267 = gm_f_write_96p75mj(t264, t266);
                gm_tftb1odc739t5 t268 = t267;
                if (((((uint8_t *)&t268))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t268) + 1, gm_srckg9scnip7e8g, 208, 25);
                gm_b0_1 t269 = GM_LD(gm_b0_1, (((uint8_t *)&t268) + 1));
            }
            goto ix257;
ie257:;
            {
                void * t271 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t272;
                GM_ST(void *, (uint8_t *)&t272, (void *)(gm_sug64ltbhov0m));
                GM_ST(uintptr_t, (uint8_t *)&t272 + 8, (uintptr_t)(18));
                gm_tftb1odc739t5 t273 = gm_f_extend_0cs85gs(t271, t272);
                gm_tftb1odc739t5 t274 = t273;
                if (((((uint8_t *)&t274))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t274) + 1, gm_srckg9scnip7e8g, 210, 25);
                gm_b0_1 t275 = GM_LD(gm_b0_1, (((uint8_t *)&t274) + 1));
            }
ix257:;
        }
        goto me224;
mn253:;
ma227:;
        void * t277 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t278;
        GM_ST(void *, (uint8_t *)&t278, (void *)(gm_sug64ltbhov0m));
        GM_ST(uintptr_t, (uint8_t *)&t278 + 8, (uintptr_t)(18));
        gm_tftb1odc739t5 t279 = gm_f_extend_0cs85gs(t277, t278);
        gm_tftb1odc739t5 t280 = t279;
        if (((((uint8_t *)&t280))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t280) + 1, gm_srckg9scnip7e8g, 213, 22);
        gm_b0_1 t281 = GM_LD(gm_b0_1, (((uint8_t *)&t280) + 1));
        goto me224;
mn276:;
ma228:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 199, 27);
me224:;
        goto me54;
mn219:;
ma58:;
        void * t283 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t284;
        GM_ST(void *, (uint8_t *)&t284, (void *)(gm_sug64ltbhov0m));
        GM_ST(uintptr_t, (uint8_t *)&t284 + 8, (uintptr_t)(18));
        gm_tftb1odc739t5 t285 = gm_f_extend_0cs85gs(t283, t284);
        gm_tftb1odc739t5 t286 = t285;
        if (((((uint8_t *)&t286))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t286) + 1, gm_srckg9scnip7e8g, 215, 18);
        gm_b0_1 t287 = GM_LD(gm_b0_1, (((uint8_t *)&t286) + 1));
        goto me54;
mn282:;
ma59:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 160, 14);
me54:;
        goto me27;
mn49:;
        gm_trap("no match arm matched the scrutinee", gm_srckg9scnip7e8g, 155, 5);
me27:;
    }
    {
        void * t288 = gm_l0;
        gm_b16_8 t289 = gm_l4;
        gm_b16_8 t290;
        GM_ST(void *, (uint8_t *)&t290, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t290 + 8, (uintptr_t)(4));
        gm_b16_8 t291;
        GM_ST(void *, (uint8_t *)&t291, (void *)(gm_sqg70d5uem21e));
        GM_ST(uintptr_t, (uint8_t *)&t291 + 8, (uintptr_t)(34));
        gm_b16_8 t292 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_b16_8 t293 = t292;
        gm_b16_8 t294 = GM_LD(gm_b16_8, ((uint8_t *)&t293));
        gm_b0_1 t295 = gm_f_define_helper_c1utd1n((void *)NULL, cx, NULL, t288, t289, t290, t291, t294);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t296 = gm_l4;
        uint8_t t297 = 0;
        F.ret = t296;
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
inline __attribute__((aligned(4))) void gm_f_new_helpers_4q783cn(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_new_helpers_4q783cn F;
    GM_STACK_CHECK(&F, 304u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_trqfvmjtt4sij t1;
    gm_f_list_uvfu1k1((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_trqfvmjtt4sij t2;
    gm_f_list_uvfu1k1((void *)&t2, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t3;
    gm_f_hash_map_tl7apid((void *)&t3, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t4;
    gm_f_hash_map_tl7apid((void *)&t4, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t4c7ipjj9s3fp t5;
    gm_f_hash_map_tl7apid((void *)&t5, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_ttiitul0263ld t6;
    gm_f_new_namer_5q88o9g((void *)&t6, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_te68172gglous *)gm_out) + 0, t1);
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_te68172gglous *)gm_out) + 40, t2);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_te68172gglous *)gm_out) + 80, t3);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_te68172gglous *)gm_out) + 136, t4);
    GM_ST(gm_t4c7ipjj9s3fp, (uint8_t *)&(*(gm_te68172gglous *)gm_out) + 192, t5);
    GM_ST(gm_ttiitul0263ld, (uint8_t *)&(*(gm_te68172gglous *)gm_out) + 248, t6);
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

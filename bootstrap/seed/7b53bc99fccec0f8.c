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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; uint32_t v1; } f1; } gm_tj2m9dbh1ekv6;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t f2; uint8_t p3[6]; gm_b16_8 f3; void * f4; void * f5; gm_tl874t2atnlh3 f6; uint8_t pe[4]; } gm_t24301fas1b2a;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t f2; uint8_t p3[6]; void * f3; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tf2orqcrphmek;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
_Static_assert(sizeof(gm_tj2m9dbh1ekv6) == 8 && _Alignof(gm_tj2m9dbh1ekv6) == 4 && offsetof(gm_tj2m9dbh1ekv6, f0) == 0 && offsetof(gm_tj2m9dbh1ekv6, f1) == 4, "layout of `IntSuffix`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { gm_b16_8 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tndsf0a4f4lj0;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; gm_t9qkh0rsooc8d f3; uint8_t p4[7]; void * f4; gm_tl874t2atnlh3 f5; uint8_t pe[4]; } gm_tv2uf4leil5hq;
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t24301fas1b2a) == 72 && _Alignof(gm_t24301fas1b2a) == 8 && offsetof(gm_t24301fas1b2a, f0) == 0 && offsetof(gm_t24301fas1b2a, f1) == 16 && offsetof(gm_t24301fas1b2a, f2) == 17 && offsetof(gm_t24301fas1b2a, f3) == 24 && offsetof(gm_t24301fas1b2a, f4) == 40 && offsetof(gm_t24301fas1b2a, f5) == 48 && offsetof(gm_t24301fas1b2a, f6) == 56, "layout of `FieldDecl`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
typedef struct { void * f0; gm_b16_8 f1; gm_th2lqbo77pvsc f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t7bjbmukv26e7;
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; gm_b16_8 f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_t99vmhfllumbh;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_b16_8 f0; gm_tv2uf4leil5hq f1; } gm_taiafe1jasg4m;
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { gm_t08olq97rscn3 f0; gm_t9qkh0rsooc8d f1; uint8_t p2[7]; gm_b16_8 f2; void * f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tc0sbs1tp6d7f;
typedef struct { gm_tibupt4dmcqai f0; gm_tl874t2atnlh3 f1; uint8_t p2[4]; gm_t79rbdv4rafe5 f2; uint8_t f3; uint8_t pe[7]; } gm_tcfiotghd1s5k;
typedef struct { gm_t08olq97rscn3 f0; gm_t79rbdv4rafe5 f1; } gm_tcuumjkg5eni6;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tf2orqcrphmek) == 56 && _Alignof(gm_tf2orqcrphmek) == 8 && offsetof(gm_tf2orqcrphmek, f0) == 0 && offsetof(gm_tf2orqcrphmek, f1) == 16 && offsetof(gm_tf2orqcrphmek, f2) == 17 && offsetof(gm_tf2orqcrphmek, f3) == 24 && offsetof(gm_tf2orqcrphmek, f4) == 32 && offsetof(gm_tf2orqcrphmek, f5) == 40, "layout of `Param`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
typedef struct { uint8_t f0; union { gm_ts6oqppjh0ba3 v0; } f1; } gm_tjsp7bhdn2e8j;
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tkacitepg5fk0;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; void * f2; gm_th2lqbo77pvsc f3; gm_tl874t2atnlh3 f4; uint8_t pe[4]; } gm_tlivgedjs5ps3;
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tndsf0a4f4lj0) == 48 && _Alignof(gm_tndsf0a4f4lj0) == 8 && offsetof(gm_tndsf0a4f4lj0, f0) == 0 && offsetof(gm_tndsf0a4f4lj0, f1) == 16 && offsetof(gm_tndsf0a4f4lj0, f2) == 24 && offsetof(gm_tndsf0a4f4lj0, f3) == 32, "layout of `Arm`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; void * f2; gm_tl874t2atnlh3 f3; uint8_t pe[4]; } gm_tnljbma3s3h16;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tpv68m88f9lcr;
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { gm_t08olq97rscn3 f0; void * f1; } gm_ts686ckv8ehm2;
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tv2uf4leil5hq) == 64 && _Alignof(gm_tv2uf4leil5hq) == 8 && offsetof(gm_tv2uf4leil5hq, f0) == 0 && offsetof(gm_tv2uf4leil5hq, f1) == 8 && offsetof(gm_tv2uf4leil5hq, f2) == 24 && offsetof(gm_tv2uf4leil5hq, f3) == 32 && offsetof(gm_tv2uf4leil5hq, f4) == 40 && offsetof(gm_tv2uf4leil5hq, f5) == 48, "layout of `Binding`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_th2lqbo77pvsc v1; } f1; } gm_tvul7d1mdc2eq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tnljbma3s3h16 v0; gm_t7bjbmukv26e7 v1; } f1; } gm_t03a3imodqi32;
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
typedef struct { gm_t1s75i08qtptd f0; gm_t08olq97rscn3 f1; gm_t79rbdv4rafe5 f2; } gm_t43d6nbdfap6p;
typedef struct { gm_b16_8 f0; gm_tjsp7bhdn2e8j f1; uint8_t p2[5]; void * f2; } gm_t4d1qrgh0vnq1;
typedef struct { gm_tvul7d1mdc2eq f0; uint8_t f1; uint8_t pe[7]; } gm_t4eh5qkgoio6s;
typedef struct { gm_t08olq97rscn3 f0; gm_tlivgedjs5ps3 f1; } gm_t70h2c775g4n7;
_Static_assert(sizeof(gm_t7bjbmukv26e7) == 64 && _Alignof(gm_t7bjbmukv26e7) == 8 && offsetof(gm_t7bjbmukv26e7, f0) == 0 && offsetof(gm_t7bjbmukv26e7, f1) == 8 && offsetof(gm_t7bjbmukv26e7, f2) == 24 && offsetof(gm_t7bjbmukv26e7, f3) == 48, "layout of `WhenItems`");
_Static_assert(sizeof(gm_t99vmhfllumbh) == 72 && _Alignof(gm_t99vmhfllumbh) == 8 && offsetof(gm_t99vmhfllumbh, f0) == 0 && offsetof(gm_t99vmhfllumbh, f1) == 32 && offsetof(gm_t99vmhfllumbh, f2) == 48 && offsetof(gm_t99vmhfllumbh, f3) == 56, "layout of `Clause`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_taiafe1jasg4m) == 80 && _Alignof(gm_taiafe1jasg4m) == 8 && offsetof(gm_taiafe1jasg4m, f0) == 0 && offsetof(gm_taiafe1jasg4m, f1) == 16, "layout of `(attrs: []Attribute, binding: Binding)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tc0sbs1tp6d7f) == 80 && _Alignof(gm_tc0sbs1tp6d7f) == 8 && offsetof(gm_tc0sbs1tp6d7f, f0) == 0 && offsetof(gm_tc0sbs1tp6d7f, f1) == 32 && offsetof(gm_tc0sbs1tp6d7f, f2) == 40 && offsetof(gm_tc0sbs1tp6d7f, f3) == 56 && offsetof(gm_tc0sbs1tp6d7f, f4) == 64, "layout of `OpDecl`");
_Static_assert(sizeof(gm_tcfiotghd1s5k) == 88 && _Alignof(gm_tcfiotghd1s5k) == 8 && offsetof(gm_tcfiotghd1s5k, f0) == 0 && offsetof(gm_tcfiotghd1s5k, f1) == 24 && offsetof(gm_tcfiotghd1s5k, f2) == 40 && offsetof(gm_tcfiotghd1s5k, f3) == 80, "layout of `TestDecl`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
_Static_assert(sizeof(gm_tcuumjkg5eni6) == 72 && _Alignof(gm_tcuumjkg5eni6) == 8 && offsetof(gm_tcuumjkg5eni6, f0) == 0 && offsetof(gm_tcuumjkg5eni6, f1) == 32, "layout of `IfBranch`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t24301fas1b2a v0; gm_t7bjbmukv26e7 v1; } f1; } gm_tfc3oljlofib6;
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
_Static_assert(sizeof(gm_tjsp7bhdn2e8j) == 3 && _Alignof(gm_tjsp7bhdn2e8j) == 1 && offsetof(gm_tjsp7bhdn2e8j, f0) == 0 && offsetof(gm_tjsp7bhdn2e8j, f1) == 1, "layout of `AssignOp`");
_Static_assert(sizeof(gm_tkacitepg5fk0) == 72 && _Alignof(gm_tkacitepg5fk0) == 8 && offsetof(gm_tkacitepg5fk0, f0) == 0 && offsetof(gm_tkacitepg5fk0, f1) == 32 && offsetof(gm_tkacitepg5fk0, f2) == 56, "layout of `Attribute`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tv2uf4leil5hq v0; gm_t7bjbmukv26e7 v1; void * v2; gm_tcfiotghd1s5k v3; void * v4; } f1; } gm_tkoope2qgegd9;
_Static_assert(sizeof(gm_tlivgedjs5ps3) == 72 && _Alignof(gm_tlivgedjs5ps3) == 8 && offsetof(gm_tlivgedjs5ps3, f0) == 0 && offsetof(gm_tlivgedjs5ps3, f1) == 16 && offsetof(gm_tlivgedjs5ps3, f2) == 24 && offsetof(gm_tlivgedjs5ps3, f3) == 32 && offsetof(gm_tlivgedjs5ps3, f4) == 56, "layout of `FnSig`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_tnljbma3s3h16) == 64 && _Alignof(gm_tnljbma3s3h16) == 8 && offsetof(gm_tnljbma3s3h16, f0) == 0 && offsetof(gm_tnljbma3s3h16, f1) == 32 && offsetof(gm_tnljbma3s3h16, f2) == 40 && offsetof(gm_tnljbma3s3h16, f3) == 48, "layout of `Variant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
typedef struct { uint64_t f0; gm_tvul7d1mdc2eq f1; } gm_toho9hmgj287v;
_Static_assert(sizeof(gm_tpv68m88f9lcr) == 56 && _Alignof(gm_tpv68m88f9lcr) == 8 && offsetof(gm_tpv68m88f9lcr, f0) == 0 && offsetof(gm_tpv68m88f9lcr, f1) == 32 && offsetof(gm_tpv68m88f9lcr, f2) == 40, "layout of `Constraint`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
_Static_assert(sizeof(gm_ts686ckv8ehm2) == 40 && _Alignof(gm_ts686ckv8ehm2) == 8 && offsetof(gm_ts686ckv8ehm2, f0) == 0 && offsetof(gm_ts686ckv8ehm2, f1) == 32, "layout of `(name: Ident, ty: ^Expr)`");
_Static_assert(sizeof(gm_tvul7d1mdc2eq) == 32 && _Alignof(gm_tvul7d1mdc2eq) == 8 && offsetof(gm_tvul7d1mdc2eq, f0) == 0 && offsetof(gm_tvul7d1mdc2eq, f1) == 8, "layout of `Pick`");
_Static_assert(sizeof(gm_t03a3imodqi32) == 72 && _Alignof(gm_t03a3imodqi32) == 8 && offsetof(gm_t03a3imodqi32, f0) == 0 && offsetof(gm_t03a3imodqi32, f1) == 8, "layout of `UnionMember`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t43d6nbdfap6p) == 112 && _Alignof(gm_t43d6nbdfap6p) == 8 && offsetof(gm_t43d6nbdfap6p, f0) == 0 && offsetof(gm_t43d6nbdfap6p, f1) == 40 && offsetof(gm_t43d6nbdfap6p, f2) == 72, "layout of `WhileStmt`");
_Static_assert(sizeof(gm_t4d1qrgh0vnq1) == 32 && _Alignof(gm_t4d1qrgh0vnq1) == 8 && offsetof(gm_t4d1qrgh0vnq1, f0) == 0 && offsetof(gm_t4d1qrgh0vnq1, f1) == 16 && offsetof(gm_t4d1qrgh0vnq1, f2) == 24, "layout of `(places: []Expr, op: AssignOp, value: ^Expr)`");
_Static_assert(sizeof(gm_t4eh5qkgoio6s) == 40 && _Alignof(gm_t4eh5qkgoio6s) == 8 && offsetof(gm_t4eh5qkgoio6s, f0) == 0 && offsetof(gm_t4eh5qkgoio6s, f1) == 32, "layout of `?Sx`");
_Static_assert(sizeof(gm_t70h2c775g4n7) == 104 && _Alignof(gm_t70h2c775g4n7) == 8 && offsetof(gm_t70h2c775g4n7, f0) == 0 && offsetof(gm_t70h2c775g4n7, f1) == 32, "layout of `(name: Ident, sig: FnSig)`");
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tfc3oljlofib6) == 80 && _Alignof(gm_tfc3oljlofib6) == 8 && offsetof(gm_tfc3oljlofib6, f0) == 0 && offsetof(gm_tfc3oljlofib6, f1) == 8, "layout of `StructMember`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t70h2c775g4n7 v0; gm_ts686ckv8ehm2 v1; } f1; } gm_tg69e9d4krfe7;
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tkoope2qgegd9) == 96 && _Alignof(gm_tkoope2qgegd9) == 8 && offsetof(gm_tkoope2qgegd9, f0) == 0 && offsetof(gm_tkoope2qgegd9, f1) == 8, "layout of `ItemKind`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
_Static_assert(sizeof(gm_toho9hmgj287v) == 40 && _Alignof(gm_toho9hmgj287v) == 8 && offsetof(gm_toho9hmgj287v, f0) == 0 && offsetof(gm_toho9hmgj287v, f1) == 8, "layout of `(i: usize, x: Sx)`");
typedef struct { uint32_t f0; gm_tl874t2atnlh3 f1; gm_th2lqbo77pvsc f2; gm_b16_8 f3; gm_t9qkh0rsooc8d f4; uint8_t p5[7]; gm_tkoope2qgegd9 f5; } gm_tvpnktv5fo8nl;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { gm_t0d02142k4tu8 f0; void * f1; void * f2; } gm_t15nkjiutlvf3;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
typedef struct { gm_b16_8 f0; gm_tg69e9d4krfe7 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t7rh4gj19r55v;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
_Static_assert(sizeof(gm_tg69e9d4krfe7) == 112 && _Alignof(gm_tg69e9d4krfe7) == 8 && offsetof(gm_tg69e9d4krfe7, f0) == 0 && offsetof(gm_tg69e9d4krfe7, f1) == 8, "layout of `ForeignMemberKind`");
typedef struct { gm_t1s75i08qtptd f0; gm_t0d02142k4tu8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tnrgict8sthcv;
typedef struct { gm_t0d02142k4tu8 f0; void * f1; } gm_tuqaojgkqdcop;
_Static_assert(sizeof(gm_tvpnktv5fo8nl) == 160 && _Alignof(gm_tvpnktv5fo8nl) == 8 && offsetof(gm_tvpnktv5fo8nl, f0) == 0 && offsetof(gm_tvpnktv5fo8nl, f1) == 4 && offsetof(gm_tvpnktv5fo8nl, f2) == 16 && offsetof(gm_tvpnktv5fo8nl, f3) == 40 && offsetof(gm_tvpnktv5fo8nl, f4) == 56 && offsetof(gm_tvpnktv5fo8nl, f5) == 64, "layout of `Item`");
_Static_assert(sizeof(gm_t15nkjiutlvf3) == 80 && _Alignof(gm_t15nkjiutlvf3) == 8 && offsetof(gm_t15nkjiutlvf3, f0) == 0 && offsetof(gm_t15nkjiutlvf3, f1) == 64 && offsetof(gm_t15nkjiutlvf3, f2) == 72, "layout of `(pat: Pattern, value: ^Expr, fallback: ^Expr)`");
typedef struct { gm_t08olq97rscn3 f0; gm_tc3oht4ksth9d f1; } gm_t56mj5hnm2uc6;
_Static_assert(sizeof(gm_t7rh4gj19r55v) == 144 && _Alignof(gm_t7rh4gj19r55v) == 8 && offsetof(gm_t7rh4gj19r55v, f0) == 0 && offsetof(gm_t7rh4gj19r55v, f1) == 16 && offsetof(gm_t7rh4gj19r55v, f2) == 128, "layout of `ForeignMember`");
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_taiafe1jasg4m v0; gm_t4d1qrgh0vnq1 v1; void * v2; gm_tgtub0v94d5u5 v3; void * v4; gm_t43d6nbdfap6p v5; void * v6; gm_t15nkjiutlvf3 v7; gm_t79rbdv4rafe5 v8; } f1; } gm_tem8p0t66u1ce;
_Static_assert(sizeof(gm_tnrgict8sthcv) == 120 && _Alignof(gm_tnrgict8sthcv) == 8 && offsetof(gm_tnrgict8sthcv, f0) == 0 && offsetof(gm_tnrgict8sthcv, f1) == 40 && offsetof(gm_tnrgict8sthcv, f2) == 104, "layout of `PatArg`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuqaojgkqdcop v0; void * v1; } f1; } gm_tq5j0547p8452;
_Static_assert(sizeof(gm_tuqaojgkqdcop) == 72 && _Alignof(gm_tuqaojgkqdcop) == 8 && offsetof(gm_tuqaojgkqdcop, f0) == 0 && offsetof(gm_tuqaojgkqdcop, f1) == 64, "layout of `(pat: Pattern, value: ^Expr)`");
typedef struct { gm_t1s75i08qtptd f0; gm_tc3oht4ksth9d f1; uint8_t f2; uint8_t pe[7]; } gm_tv1o1j44ii4db;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tc3oht4ksth9d v0; gm_t56mj5hnm2uc6 v1; gm_tc3oht4ksth9d v2; } f1; } gm_t22mtv3sd9prf;
_Static_assert(sizeof(gm_t56mj5hnm2uc6) == 120 && _Alignof(gm_t56mj5hnm2uc6) == 8 && offsetof(gm_t56mj5hnm2uc6, f0) == 0 && offsetof(gm_t56mj5hnm2uc6, f1) == 32, "layout of `(name: Ident, value: Expr)`");
_Static_assert(sizeof(gm_tem8p0t66u1ce) == 120 && _Alignof(gm_tem8p0t66u1ce) == 8 && offsetof(gm_tem8p0t66u1ce, f0) == 0 && offsetof(gm_tem8p0t66u1ce, f1) == 8, "layout of `StmtKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_tem8p0t66u1ce f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_to88ae1b67aev;
_Static_assert(sizeof(gm_tq5j0547p8452) == 80 && _Alignof(gm_tq5j0547p8452) == 8 && offsetof(gm_tq5j0547p8452, f0) == 0 && offsetof(gm_tq5j0547p8452, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tv1o1j44ii4db) == 136 && _Alignof(gm_tv1o1j44ii4db) == 8 && offsetof(gm_tv1o1j44ii4db, f0) == 0 && offsetof(gm_tv1o1j44ii4db, f1) == 40 && offsetof(gm_tv1o1j44ii4db, f2) == 128, "layout of `TupleElem`");
_Static_assert(sizeof(gm_t22mtv3sd9prf) == 128 && _Alignof(gm_t22mtv3sd9prf) == 8 && offsetof(gm_t22mtv3sd9prf, f0) == 0 && offsetof(gm_t22mtv3sd9prf, f1) == 8, "layout of `ArgKind`");
typedef struct { gm_t22mtv3sd9prf f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t7te2tdpebbt4;
_Static_assert(sizeof(gm_to88ae1b67aev) == 144 && _Alignof(gm_to88ae1b67aev) == 8 && offsetof(gm_to88ae1b67aev, f0) == 0 && offsetof(gm_to88ae1b67aev, f1) == 8 && offsetof(gm_to88ae1b67aev, f2) == 128, "layout of `Stmt`");
_Static_assert(sizeof(gm_t7te2tdpebbt4) == 144 && _Alignof(gm_t7te2tdpebbt4) == 8 && offsetof(gm_t7te2tdpebbt4, f0) == 0 && offsetof(gm_t7te2tdpebbt4, f1) == 128, "layout of `Arg`");
struct gm_fr_assign_op_name_8crhefq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_atom_of_bytes_dj32elq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_binop_name_1d6k17q {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_bnode_hu9fd4q {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* rest */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_const_type_o8v21gi {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* v */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_arg_9iv8pdv {
    gm_frame h;
    gm_tc3oht4ksth9d l1; /* e */
    gm_tc3oht4ksth9d l3; /* e */
    gm_tc3oht4ksth9d l4; /* e */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_arm_8jsad7u {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* pats */
    gm_t0d02142k4tu8 l4; /* p */
    gm_trqfvmjtt4sij l5; /* v */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_attr_qdd7k06 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_trqfvmjtt4sij l3; /* xs */
    gm_tc3oht4ksth9d l6; /* e */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_binding_b27qkdk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_trqfvmjtt4sij l2; /* ns */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_block_5utjqg0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_to88ae1b67aev l4; /* s */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_clause_kje51q9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* params */
    gm_trqfvmjtt4sij l5; /* v */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_cond_cu0s815 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tq5j0547p8452 l4; /* part */
    gm_t0d02142k4tu8 l5; /* pat */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_constraints_68aa75n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tpv68m88f9lcr l4; /* c */
    gm_t4eh5qkgoio6s ret;
};
struct gm_fr_dump_effect_lit_4vqnm1n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tc0sbs1tp6d7f l5; /* op */
    gm_trqfvmjtt4sij l7; /* ov */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_effects_4fqcm0g {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tc3oht4ksth9d l4; /* e */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_expr_l3bbdps {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_trqfvmjtt4sij l7; /* text */
    gm_tv1o1j44ii4db l18; /* t */
    gm_tioi3jh9v22g5 l24; /* x */
    gm_t7te2tdpebbt4 l36; /* a */
    gm_t99vmhfllumbh l69; /* c */
    gm_t7rh4gj19r55v l74; /* m */
    gm_tndsf0a4f4lj0 l79; /* a */
    gm_trqfvmjtt4sij l82; /* hs */
    gm_tc3oht4ksth9d l85; /* h */
    gm_trqfvmjtt4sij l89; /* ps */
    gm_t0d02142k4tu8 l92; /* p */
    gm_t79rbdv4rafe5 l93; /* b */
    gm_tchr52g0ema53 l95; /* b */
    gm_tet9guhev5r4u l101; /* i */
    gm_trqfvmjtt4sij l103; /* items */
    gm_t7te2tdpebbt4 l106; /* a */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_file_cno3fos {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* items */
    gm_tvpnktv5fo8nl l5; /* item */
    gm_b0_1 ret;
};
struct gm_fr_dump_fn_lit_tchlprc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* tail */
    gm_t79rbdv4rafe5 l3; /* b */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_fn_sig_4ip3qer {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* tail */
    gm_trqfvmjtt4sij l3; /* v */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_foreign_member_vrht12f {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tlivgedjs5ps3 l3; /* sig */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_handler_decl_htmrmsf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_t99vmhfllumbh l6; /* c */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_if_3qo5nul {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tcuumjkg5eni6 l4; /* b */
    gm_trqfvmjtt4sij l5; /* bv */
    gm_t79rbdv4rafe5 l6; /* b */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_item_bs3lq5m {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tv2uf4leil5hq l4; /* b */
    gm_t7bjbmukv26e7 l5; /* w */
    gm_tcfiotghd1s5k l7; /* t */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_items_atcioo9 {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* v */
    gm_tvpnktv5fo8nl l5; /* item */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_param_kr39os0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* ns */
    gm_trqfvmjtt4sij l5; /* v */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_params_vfgaabj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tf2orqcrphmek l4; /* p */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_pat_arg_ai8l7kq {
    gm_frame h;
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_pattern_q6u5c24 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_trqfvmjtt4sij l11; /* items */
    gm_tnrgict8sthcv l14; /* a */
    gm_tnrgict8sthcv l18; /* a */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_stmt_tfqg5dm {
    gm_frame h;
    gm_tv2uf4leil5hq l2; /* binding */
    gm_trqfvmjtt4sij l3; /* v */
    gm_trqfvmjtt4sij l7; /* ps */
    gm_tc3oht4ksth9d l10; /* p */
    gm_trqfvmjtt4sij l11; /* v */
    gm_t43d6nbdfap6p l16; /* w */
    gm_trqfvmjtt4sij l17; /* v */
    gm_t0d02142k4tu8 l20; /* pat */
    gm_trqfvmjtt4sij l23; /* v */
    gm_t79rbdv4rafe5 l24; /* b */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_struct_lit_n9dqlnm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tfc3oljlofib6 l6; /* m */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_struct_member_q034tdn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_t24301fas1b2a l2; /* f */
    gm_trqfvmjtt4sij l4; /* ns */
    gm_t7bjbmukv26e7 l9; /* w */
    gm_trqfvmjtt4sij l10; /* th */
    gm_tfc3oljlofib6 l13; /* x */
    gm_trqfvmjtt4sij l15; /* el */
    gm_tfc3oljlofib6 l18; /* x */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_tuple_elem_bul8rgi {
    gm_frame h;
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_union_lit_leu6qp2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_t03a3imodqi32 l7; /* m */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_union_member_4lopa45 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tnljbma3s3h16 l2; /* var */
    gm_t7bjbmukv26e7 l5; /* w */
    gm_trqfvmjtt4sij l6; /* th */
    gm_t03a3imodqi32 l9; /* x */
    gm_trqfvmjtt4sij l11; /* el */
    gm_t03a3imodqi32 l14; /* x */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_when_items_16tgm3i {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_dump_when_stmt_hdq14ev {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* v */
    gm_t79rbdv4rafe5 l2; /* b */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_empty_slice_bjhhdfn {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_extend_0cs85gs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_extend_i72a9oi {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_fits_mco2o3v {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_flat_hstd39j {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_float_suffix_7g3bh9c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_ident_sx_kd0v352 {
    gm_frame h;
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_list_ccsq5cl {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_make_list_cicccjr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* rest */
    gm_trqfvmjtt4sij l3; /* items */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_node1_gpsjtpc {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* rest */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_node2_b4qpqat {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* rest */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_node_dm1r8dn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* rest */
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_push_attrs_a5l439c {
    gm_frame h;
    gm_tkacitepg5fk0 l4; /* a */
    gm_b0_1 ret;
};
struct gm_fr_push_mjiecfq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_t83rg2m {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_3cp9o8d {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_q1n4vfn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_render_7k3fqtk {
    gm_frame h;
    gm_trqfvmjtt4sij l13; /* text */
    gm_b0_1 ret;
};
struct gm_fr_str_lit_a2k13t3 {
    gm_frame h;
    gm_tvul7d1mdc2eq ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_unop_name_j99tcv9 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_visibility_u2ipb12 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
__attribute__((aligned(4))) gm_b16_8 gm_f_assign_op_name_8crhefq(void *env, gm_node *cx, gm_node *inst, gm_tjsp7bhdn2e8j a0);
inline __attribute__((aligned(4))) void gm_f_atom_of_bytes_dj32elq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_name_1d6k17q(gm_ts6oqppjh0ba3 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_text_0ljfc0r(gm_ts6oqppjh0ba3 a0);
inline __attribute__((aligned(4))) void gm_f_bnode_hu9fd4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) uint64_t gm_f_char_count_76unqru(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_const_type_o8v21gi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_dump_arg_9iv8pdv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_arm_8jsad7u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_attr_qdd7k06(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_binding_b27qkdk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_block_5utjqg0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_clause_kje51q9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_cond_cu0s815(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_constraints_68aa75n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_dump_effect_lit_4vqnm1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_effects_4fqcm0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_dump_expr_l3bbdps(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_dump_file_cno3fos(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_dump_fn_lit_tchlprc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_fn_sig_4ip3qer(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, const void * a2);
__attribute__((aligned(4))) void gm_f_dump_foreign_member_vrht12f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_handler_decl_htmrmsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_if_3qo5nul(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_item_bs3lq5m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_items_atcioo9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_dump_param_kr39os0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_params_vfgaabj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_dump_pat_arg_ai8l7kq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_pattern_q6u5c24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_stmt_tfqg5dm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_struct_lit_n9dqlnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_struct_member_q034tdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_tuple_elem_bul8rgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_union_lit_leu6qp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_union_member_4lopa45(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_when_items_16tgm3i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_dump_when_stmt_hdq14ev(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_bjhhdfn(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_escape_bytes_25mo226(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_i72a9oi(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_fits_mco2o3v(void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_flat_hstd39j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_float_suffix_7g3bh9c(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_32n178h(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_c0h6o29(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ii9pvls(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_s4n90cc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_ident_sx_kd0v352(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_tvul7d1mdc2eq gm_f_ident_sx_kd0v352_c(gm_t08olq97rscn3 a0);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_ccsq5cl(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_make_list_cicccjr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint8_t a2);
inline __attribute__((aligned(4))) void gm_f_node1_gpsjtpc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) void gm_f_node2_b4qpqat(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, const void * a2);
inline __attribute__((aligned(4))) void gm_f_node_dm1r8dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_attrs_a5l439c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_mjiecfq(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_3cp9o8d(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q1n4vfn(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
__attribute__((aligned(4))) gm_b0_1 gm_f_render_7k3fqtk(void *env, gm_node *cx, gm_node *inst, void * a0, int64_t a1, void * a2);
__attribute__((aligned(4))) void gm_f_str_lit_a2k13t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_unop_name_j99tcv9(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_visibility_u2ipb12(void *gm_out, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_visibility_u2ipb12_c(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_v26t591(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
extern uint8_t gm_cddg9214lam[8] __attribute__((aligned(8)));
extern const uint8_t gm_s0gu0jo9fq1ca[11];
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s0jho24omhfui[7];
extern const uint8_t gm_s0l81imssm78p[7];
extern const uint8_t gm_s11b339fv56g1[13];
extern const uint8_t gm_s167ok8osvuve[11];
extern const uint8_t gm_s19ndlkgdtviu[6];
extern const uint8_t gm_s1jboc8lkvac0[6];
extern const uint8_t gm_s1k3lfare8sei[4];
extern const uint8_t gm_s1m38bo3naic1[8];
extern const uint8_t gm_s20t5qopq8n9u[5];
extern const uint8_t gm_s252dj7ek002d[8];
extern const uint8_t gm_s277jq8ulrihr[7];
extern const uint8_t gm_s283bs8hsleb3[9];
extern const uint8_t gm_s2f5ea3l0ep8h[9];
extern const uint8_t gm_s3bakerohlr2a[3];
extern const uint8_t gm_s3brj349ts8fs[9];
extern const uint8_t gm_s3ia9jm7npe6t[10];
extern const uint8_t gm_s3r7cg0hbrkdg[17];
extern const uint8_t gm_s3s50a53ci7rk[7];
extern const uint8_t gm_s3t7gv740qicd[10];
extern const uint8_t gm_s420tm9ua7ggg[4];
extern const uint8_t gm_s4cess8gum6i7[6];
extern const uint8_t gm_s4dis7c905evg[10];
extern const uint8_t gm_s4fd35uidvgkn[4];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s5dbqnns2epkg[6];
extern const uint8_t gm_s5qkai093c1q0[6];
extern const uint8_t gm_s5rv6umk4akl0[6];
extern const uint8_t gm_s66uvortgrtsg[4];
extern const uint8_t gm_s688e8m4pgcda[6];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s6pilvbjelm5p[4];
extern const uint8_t gm_s76ud0alkjfse[4];
extern const uint8_t gm_s79a8ngj68lbf[5];
extern const uint8_t gm_s79s3h98matgr[6];
extern const uint8_t gm_s7dkbj8tvt9e3[7];
extern const uint8_t gm_s7ntlu0vonte9[12];
extern const uint8_t gm_s7s32h358ordg[6];
extern const uint8_t gm_s80ddhqa1o9i9[7];
extern const uint8_t gm_s8aj3oag1l1u0[14];
extern const uint8_t gm_s8av3ru5o85v4[6];
extern const uint8_t gm_s8e72gdumihb1[7];
extern const uint8_t gm_s8m5q4i8uoe5j[6];
extern const uint8_t gm_s8p1h96ji9kh0[6];
extern const uint8_t gm_s8rsdirj4dmun[4];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_s9es2tos1bhfp[5];
extern const uint8_t gm_s9r64t1166q1f[5];
extern const uint8_t gm_sa26g0ksgdkma[10];
extern const uint8_t gm_samlp70hmirme[3];
extern const uint8_t gm_saufuk3rs2i2m[15];
extern const uint8_t gm_sb88rajuutaep[4];
extern const uint8_t gm_sbiv3c5qa6ipi[6];
extern const uint8_t gm_sc49g68j2cekh[7];
extern const uint8_t gm_scc8p0a9eachg[4];
extern const uint8_t gm_scvhpklutvpjg[4];
extern const uint8_t gm_sd9regb2pbgv1[8];
extern const uint8_t gm_sdfmdfp384tf7[11];
extern const uint8_t gm_sdl8mgtvebfcs[3];
extern const uint8_t gm_se31bb9e7icn2[11];
extern const uint8_t gm_seb76g152eseg[4];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_ser8ip7vtbv6c[9];
extern const uint8_t gm_ses9qtp6kdd22[8];
extern const uint8_t gm_sf3ii7gekd9ll[13];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfkmkcagsnqnb[7];
extern const uint8_t gm_sg1h22ve4uqmq[4];
extern const uint8_t gm_sg3kpa3h16no3[7];
extern const uint8_t gm_sgbumttiou4de[8];
extern const uint8_t gm_sgf1euukkdve3[14];
extern const uint8_t gm_sgmcjv273u32m[11];
extern const uint8_t gm_sgs4qi36v3o5v[8];
extern const uint8_t gm_sh0j2ggs1vvoa[2];
extern const uint8_t gm_sh4e4kqha06b3[7];
extern const uint8_t gm_sh4niqnmh73co[6];
extern const uint8_t gm_shhr1hlq1ethc[4];
extern const uint8_t gm_shmknkannurll[5];
extern const uint8_t gm_shnig5tqtdedi[4];
extern const uint8_t gm_shofa8pdml9fb[13];
extern const uint8_t gm_shs3nem6t7u8g[4];
extern const uint8_t gm_si1m7q6ia0hto[7];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sjjc564tiep5s[13];
extern const uint8_t gm_sjkj5g78lejrj[4];
extern const uint8_t gm_sjnl36ocas0o1[11];
extern const uint8_t gm_sjqs7q3ostqbm[4];
extern const uint8_t gm_sk5s72mtujop8[5];
extern const uint8_t gm_sk83con0mdsfe[6];
extern const uint8_t gm_skissj10nclhr[6];
extern const uint8_t gm_sl4ns6hobicht[6];
extern const uint8_t gm_sl5jkfh6104lp[5];
extern const uint8_t gm_sl9nvct67or45[3];
extern const uint8_t gm_sllcm1pqagh28[6];
extern const uint8_t gm_sltau19rt9o73[8];
extern const uint8_t gm_sm56lhobvb6g5[6];
extern const uint8_t gm_sm8obe455p2d9[3];
extern const uint8_t gm_sm8si2u0a1u17[5];
extern const uint8_t gm_sm9e81djuncnu[4];
extern const uint8_t gm_smedhknhdrf46[11];
extern const uint8_t gm_smglbbtmdmip2[6];
extern const uint8_t gm_smimi08sfrg0n[5];
extern const uint8_t gm_smjq2fi49hdnf[11];
extern const uint8_t gm_smu48daue2qai[6];
extern const uint8_t gm_sn581p3lbgrjg[9];
extern const uint8_t gm_sn6sqlran7h3r[7];
extern const uint8_t gm_snas2pfnujj1i[15];
extern const uint8_t gm_snftglmif3ta2[11];
extern const uint8_t gm_so0ilvhoi1hom[5];
extern const uint8_t gm_so8qnkabacf8o[5];
extern const uint8_t gm_sobcm12sogdng[5];
extern const uint8_t gm_sofbtko7t8taa[5];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_soi4hbchhjloe[4];
extern const uint8_t gm_soifq4vd92jh0[5];
extern const uint8_t gm_spbv5h8c0ukao[5];
extern const uint8_t gm_spj9t9b8lqlpp[8];
extern const uint8_t gm_spl4qnmq2arsc[13];
extern const uint8_t gm_sqbhgmmp8kn4m[3];
extern const uint8_t gm_sqflguscq1uc5[6];
extern const uint8_t gm_sqpev88g6capg[9];
extern const uint8_t gm_sqqajgod2bb4o[4];
extern const uint8_t gm_sqrfl54mgc34i[8];
extern const uint8_t gm_squ5nrnqo0b0l[6];
extern const uint8_t gm_squedv51820an[5];
extern const uint8_t gm_sr31o7m0aohvd[9];
extern const uint8_t gm_sr899mamktpvo[7];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srck6bk8q21r07v[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srfagufvviad7[6];
extern const uint8_t gm_srj7urnaf6a1u[9];
extern const uint8_t gm_srltb43nuabaq[5];
extern const uint8_t gm_srq9bjb7d7qo7[15];
extern const uint8_t gm_srri8c6al1v1g[5];
extern const uint8_t gm_ss496b65r9omj[6];
extern const uint8_t gm_ssbg0bg772d67[5];
extern const uint8_t gm_sskduiq2hlj37[3];
extern const uint8_t gm_ssqg2h6hrn099[9];
extern const uint8_t gm_st38p0j1i82np[11];
extern const uint8_t gm_stugmrojdno85[6];
extern const uint8_t gm_su137hja0ebcd[6];
extern const uint8_t gm_suctgno4l10a5[9];
extern const uint8_t gm_sugq7dl55ocon[7];
extern const uint8_t gm_sukutl224cgkm[8];
extern const uint8_t gm_sup2jn5hml593[6];
extern const uint8_t gm_sv0fbf330lmcd[8];
extern const uint8_t gm_svqdrhms9i231[7];
extern const uint8_t gm_svvuql4te1jp4[9];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) void gm_f_atom_of_bytes_dj32elq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_atom_of_bytes_dj32elq F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 72u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t4 = gm_f_escape_bytes_25mo226((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 24, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 8, t6);
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
inline __attribute__((aligned(4))) void gm_f_bnode_hu9fd4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_bnode_hu9fd4q F;
    gm_b16_8 gm_l0; /* head */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_b16_8 t1 = gm_l0;
    gm_f_make_list_cicccjr((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t1, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)), ((uint8_t)1));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_bjhhdfn(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_bjhhdfn F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ijtp9lo F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)1ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 1)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_0cs85gs F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_3cp9o8d(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 1);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_extend_i72a9oi(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_extend_i72a9oi F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* xs */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint64_t r8; bool o9 = __builtin_sub_overflow(t6, t7, &r8);
        if (o9) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 80, 8);
        uint64_t t10 = r8;
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t12 = (t10 < t11);
        if (!t12) goto ie4;
        {
            void * t14 = gm_l0;
            uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_b16_8 t16 = gm_l1;
            gm_tftb1odc739t5 t17 = gm_f_regrow_q1n4vfn(t14, t15, t16);
            if (((((uint8_t *)&t17))[0] != 0)) {
                gm_t9qkh0rsooc8d t18 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t17) + 1));
                gm_tftb1odc739t5 t19;
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t19 + 1), t18);
                F.ret = t19;
                goto gm_fret;
            }
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
        }
        goto ix4;
ie4:;
        {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = ((uint8_t *)t23);
            uint64_t t25 = gm_l2;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = gm_l2;
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r29; bool o30 = __builtin_add_overflow(t27, t28, &r29);
            if (o30) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 84, 36);
            uint64_t t31 = r29;
            uint64_t t32 = (uintptr_t)t31;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(((uint8_t *)t24 + t26 * 32)));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(t32 - t26));
            gm_b16_8 t34 = gm_l1;
            if (GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) != 0) memmove(GM_LD(void *, ((uint8_t *)&t33)), GM_LD(void *, ((uint8_t *)&t34)), GM_LD(uintptr_t, ((uint8_t *)&t33) + 8) * 32);
        }
ix4:;
    }
    {
        void * t35 = gm_l0;
        void * t36 = gm_l0;
        void * t37 = GM_LD(void *, (((uint8_t *)t36) + 0));
        void * t38 = ((uint8_t *)t37);
        uint64_t t39 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t40 = gm_l2;
        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint64_t r42; bool o43 = __builtin_add_overflow(t40, t41, &r42);
        if (o43) gm_trap("integer overflow in `+`", gm_srcrgc9bkquf6nr, 86, 30);
        uint64_t t44 = r42;
        uint64_t t45 = (uintptr_t)t44;
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(((uint8_t *)t38 + t39 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(t45 - t39));
        GM_ST(gm_b16_8, (((uint8_t *)t35) + 0), t46);
    }
    {
        gm_b0_1 t47;
        gm_tftb1odc739t5 t48;
        GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
        ((uint8_t *)&t48)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t48 + 1), t47);
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_ident_sx_kd0v352(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ident_sx_kd0v352 F;
    gm_t08olq97rscn3 gm_l0; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 0));
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 24, 0);
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 0, ((uint8_t)0ULL));
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 8, t1);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_list_ccsq5cl(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ccsq5cl F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_bjhhdfn();
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
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_uvfu1k1 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ijtp9lo();
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
inline __attribute__((aligned(4))) void gm_f_node1_gpsjtpc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_node1_gpsjtpc F;
    gm_b16_8 gm_l0; /* head */
    gm_tvul7d1mdc2eq gm_l1; /* x */
    GM_STACK_CHECK(&F, 74u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        gm_f_list_ccsq5cl((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l2);
        gm_tftb1odc739t5 t3 = gm_f_push_mjiecfq(t2, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l1)));
        if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 1, gm_srck6bk8q21r07v, 50, 5);
        gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 1));
    }
    {
        gm_b16_8 t5 = gm_l0;
        gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t5, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l2)));
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
inline __attribute__((aligned(4))) void gm_f_node_dm1r8dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1) {
    struct gm_fr_node_dm1r8dn F;
    gm_b16_8 gm_l0; /* head */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_b16_8 t1 = gm_l0;
    gm_f_make_list_cicccjr((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t1, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)), ((uint8_t)0));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_mjiecfq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_mjiecfq F;
    void * gm_l0; /* l */
    gm_tvul7d1mdc2eq gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_bjhhdfn();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_q1n4vfn(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        gm_tvul7d1mdc2eq t21 = gm_l1;
        GM_ST(gm_tvul7d1mdc2eq, ((uint8_t *)t20 + t18 * 32), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
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
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l2 = t3;
    }
    {
        void * t5 = gm_l0;
        uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)t5) + 16));
        uint64_t t7 = gm_l2;
        uint8_t t8 = ((t6 == t7));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            gm_b16_8 t11 = gm_f_empty_slice_ijtp9lo();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_3cp9o8d(t10, ((uint64_t)1ULL), t12);
            if (((((uint8_t *)&t13))[0] != 0)) {
                gm_t9qkh0rsooc8d t14 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&t13) + 1));
                gm_tftb1odc739t5 t15;
                ((uint8_t *)&t15)[0] = 1;
                GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t15 + 1), t14);
                F.ret = t15;
                goto gm_fret;
            }
            gm_b0_1 t16 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t17 = gm_l2;
        uint64_t t18 = (uintptr_t)t17;
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        uint8_t t21 = gm_l1;
        GM_ST(uint8_t, ((uint8_t *)t20 + t18 * 1), t21);
    }
    {
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        void * t24 = GM_LD(void *, (((uint8_t *)t23) + 0));
        void * t25 = ((uint8_t *)t24);
        uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t27 = gm_l2;
        uint64_t r28; bool o29 = __builtin_add_overflow(t27, ((uint64_t)1ULL), &r28);
        uint64_t t30 = r28;
        uint64_t t31 = (uintptr_t)t30;
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
        GM_ST(gm_b16_8, (((uint8_t *)t22) + 0), t32);
    }
    {
        gm_b0_1 t33;
        gm_tftb1odc739t5 t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 1, 0);
        ((uint8_t *)&t34)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t34 + 1), t33);
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
        F.ret = t34;
        goto gm_fret;
    }
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
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)1ULL), ((uint64_t)1ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_q1n4vfn(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_q1n4vfn F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* extra */
    gm_b16_8 gm_l2; /* tail */
    GM_STACK_CHECK(&F, 87u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = t2;
        uint64_t t4 = gm_l1;
        void * t5 = GM_LD(void *, ((uint8_t *)&gm_l2));
        void * t6 = t5;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)32ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 89u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b0_1 t3 = gm_f_write_decimal_ke367ab((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = (void *)((uint8_t *)&F.l1);
        gm_tabb710d9mq2e t5;
        gm_f_into_slice_hq0te56((void *)&t5, t4);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srclv3pj5es0f8o, 624, 12);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        uint8_t t7 = 0;
        F.ret = t6;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_assign_op_name_8crhefq(void *env, gm_node *cx, gm_node *inst, gm_tjsp7bhdn2e8j a0) {
    struct gm_fr_assign_op_name_8crhefq F;
    gm_tjsp7bhdn2e8j gm_l0; /* op */
    gm_ts6oqppjh0ba3 gm_l1; /* b */
    GM_STACK_CHECK(&F, 163u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t4 != ((uint8_t)0ULL)) goto mn3;
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_sh0j2ggs1vvoa));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(1));
    t1 = t5;
    goto me2;
mn3:;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t7 != ((uint8_t)1ULL)) goto mn6;
    gm_ts6oqppjh0ba3 t8 = GM_LD(gm_ts6oqppjh0ba3, (((uint8_t *)&gm_l0) + 1));
    gm_l1 = t8;
    gm_ts6oqppjh0ba3 t9 = gm_l1;
    gm_b16_8 t10 = gm_f_binop_text_0ljfc0r(t9);
    gm_tnpifv20irnph t11;
    GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
    gm_tabb710d9mq2e t12;
    gm_f_format_ii9pvls((void *)&t12, (void *)NULL, cx, NULL, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 8, gm_srck6bk8q21r07v, 175, 21);
    gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
    gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
    t1 = t14;
    goto me2;
mn6:;
    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 173, 61);
me2:;
    F.ret = t1;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_atom_of_bytes_dj32elq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_binop_name_1d6k17q(gm_ts6oqppjh0ba3 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_binop_name_1d6k17q F;
    gm_ts6oqppjh0ba3 gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 103u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma5; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma5; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma5; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma5; case ((uint8_t)8ULL): goto ma5; case ((uint8_t)9ULL): goto ma5; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma5; case ((uint8_t)12ULL): goto ma5; case ((uint8_t)13ULL): goto ma3; case ((uint8_t)14ULL): goto ma5; case ((uint8_t)15ULL): goto ma4; case ((uint8_t)16ULL): goto ma5; case ((uint8_t)17ULL): goto ma5; case ((uint8_t)18ULL): goto ma5; case ((uint8_t)19ULL): goto ma5; case ((uint8_t)20ULL): goto ma5; case ((uint8_t)21ULL): goto ma5; default: goto ma5; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)13ULL)) goto mn8;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_s3s50a53ci7rk));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(6));
    t1 = t10;
    goto me2;
mn8:;
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)15ULL)) goto mn11;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sh4e4kqha06b3));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(6));
    t1 = t13;
    goto me2;
mn11:;
ma5:;
    gm_ts6oqppjh0ba3 t15 = gm_l0;
    gm_b16_8 t16 = gm_f_binop_text_0ljfc0r(t15);
    t1 = t16;
    goto me2;
mn14:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 158, 42);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_bnode_hu9fd4q(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_const_type_o8v21gi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1, void * a2) {
    struct gm_fr_const_type_o8v21gi F;
    gm_b16_8 gm_l0; /* head */
    uint8_t gm_l1; /* is_const */
    void * gm_l2; /* inner */
    GM_STACK_CHECK(&F, 150u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_ccsq5cl((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t3 = gm_l1;
        if (!t3) goto ie2;
        {
            void * t5 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_stugmrojdno85));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(5));
            gm_tvul7d1mdc2eq t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t7 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t7 + 8), t6);
            gm_tftb1odc739t5 t8 = gm_f_push_mjiecfq(t5, &t7);
            if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 1, gm_srck6bk8q21r07v, 499, 19);
            gm_b0_1 t9 = GM_LD(gm_b0_1, (((uint8_t *)&t8) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = (void *)((uint8_t *)&F.l3);
        void * t11 = gm_l2;
        gm_tvul7d1mdc2eq t12;
        gm_f_dump_expr_l3bbdps((void *)&t12, (void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t13 = gm_f_push_mjiecfq(t10, &t12);
        if (((((uint8_t *)&t13))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t13) + 1, gm_srck6bk8q21r07v, 500, 5);
        gm_b0_1 t14 = GM_LD(gm_b0_1, (((uint8_t *)&t13) + 1));
    }
    {
        gm_b16_8 t15 = gm_l0;
        gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t15, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3)));
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
inline __attribute__((aligned(4))) void gm_f_dump_arg_9iv8pdv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_arg_9iv8pdv F;
    void * gm_l0; /* a */
    gm_t08olq97rscn3 gm_l2; /* n */
    GM_STACK_CHECK(&F, 628u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_tvul7d1mdc2eq t2;
    uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    switch (t8) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; default: goto ma7; }
ma4:;
    uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t10 != ((uint8_t)0ULL)) goto mn9;
    gm_tc3oht4ksth9d t11 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)t1) + 0) + 8));
    F.l1 = t11;
    void * t12 = (void *)((uint8_t *)&F.l1);
    gm_tvul7d1mdc2eq t13;
    gm_f_dump_expr_l3bbdps((void *)&t13, (void *)NULL, cx, NULL, t12);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t2 = t13;
    goto me3;
mn9:;
ma5:;
    uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t15 != ((uint8_t)1ULL)) goto mn14;
    gm_t08olq97rscn3 t16 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t1) + 0) + 8) + 0));
    gm_l2 = t16;
    gm_tc3oht4ksth9d t17 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)t1) + 0) + 8) + 32));
    F.l3 = t17;
    gm_b16_8 t18;
    GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s19ndlkgdtviu));
    GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(5));
    gm_tvul7d1mdc2eq t19;
    gm_f_ident_sx_kd0v352((void *)&t19, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)));
    void * t20 = (void *)((uint8_t *)&F.l3);
    gm_tvul7d1mdc2eq t21;
    gm_f_dump_expr_l3bbdps((void *)&t21, (void *)NULL, cx, NULL, t20);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tvul7d1mdc2eq t22;
    gm_f_node2_b4qpqat((void *)&t22, (void *)NULL, cx, NULL, t18, &t19, &t21);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t2 = t22;
    goto me3;
mn14:;
ma6:;
    uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t1) + 0) + 0));
    if (t24 != ((uint8_t)2ULL)) goto mn23;
    gm_tc3oht4ksth9d t25 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)t1) + 0) + 8));
    F.l4 = t25;
    gm_b16_8 t26;
    GM_ST(void *, (uint8_t *)&t26, (void *)(gm_s80ddhqa1o9i9));
    GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(6));
    void * t27 = (void *)((uint8_t *)&F.l4);
    gm_tvul7d1mdc2eq t28;
    gm_f_dump_expr_l3bbdps((void *)&t28, (void *)NULL, cx, NULL, t27);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tvul7d1mdc2eq t29;
    gm_f_node1_gpsjtpc((void *)&t29, (void *)NULL, cx, NULL, t26, &t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    t2 = t29;
    goto me3;
mn23:;
ma7:;
    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 510, 52);
me3:;
    F.ret = t2;
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
inline __attribute__((aligned(4))) void gm_f_dump_arm_8jsad7u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_arm_8jsad7u F;
    void * gm_l0; /* a */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    void * gm_l6; /* g */
    GM_STACK_CHECK(&F, 489u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_t0d02142k4tu8 t16 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 64));
                    F.l4 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l1);
                        void * t19 = (void *)((uint8_t *)&F.l4);
                        gm_tvul7d1mdc2eq t20;
                        gm_f_dump_pattern_q6u5c24((void *)&t20, (void *)NULL, cx, NULL, t19);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tftb1odc739t5 t21 = gm_f_push_mjiecfq(t18, &t20);
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srck6bk8q21r07v, 534, 23);
                        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 534, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t24 = gm_l3;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l3 = t27;
                }
            }
gm_ll9:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb5; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc5; }
            }
            goto gm_lad;
lb5:;
        }
    }
    {
        gm_f_list_ccsq5cl((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t28 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_so0ilvhoi1hom));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(4));
        gm_tvul7d1mdc2eq t30;
        gm_f_node_dm1r8dn((void *)&t30, (void *)NULL, cx, NULL, t29, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t31 = gm_f_push_mjiecfq(t28, &t30);
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srck6bk8q21r07v, 536, 5);
        gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
    }
    {
        void * t34 = gm_l0;
        void * t35 = GM_LD(void *, (((uint8_t *)t34) + 16));
        if (!(GM_LD(void *, ((uint8_t *)&t35)) != 0)) goto ie33;
        void * t36 = GM_LD(void *, ((uint8_t *)&t35));
        gm_l6 = t36;
        {
            void * t38 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sqflguscq1uc5));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(5));
            void * t40 = gm_l6;
            gm_tvul7d1mdc2eq t41;
            gm_f_dump_expr_l3bbdps((void *)&t41, (void *)NULL, cx, NULL, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t42;
            gm_f_node1_gpsjtpc((void *)&t42, (void *)NULL, cx, NULL, t39, &t41);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t43 = gm_f_push_mjiecfq(t38, &t42);
            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srck6bk8q21r07v, 537, 23);
            gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
        }
        goto ix33;
ie33:;
ix33:;
    }
    {
        void * t45 = (void *)((uint8_t *)&F.l5);
        void * t46 = gm_l0;
        void * t47 = GM_LD(void *, (((uint8_t *)t46) + 24));
        gm_tvul7d1mdc2eq t48;
        gm_f_dump_expr_l3bbdps((void *)&t48, (void *)NULL, cx, NULL, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t49 = gm_f_push_mjiecfq(t45, &t48);
        if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_srck6bk8q21r07v, 538, 5);
        gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
    }
    {
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s76ud0alkjfse));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(3));
        gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t51, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l5)));
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
inline __attribute__((aligned(4))) void gm_f_dump_attr_qdd7k06(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_attr_qdd7k06 F;
    void * gm_l0; /* a */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 407u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        void * t3 = gm_l0;
        gm_tvul7d1mdc2eq t4;
        gm_f_ident_sx_kd0v352((void *)&t4, &(*(gm_t08olq97rscn3 *)(((uint8_t *)t3) + 0)));
        gm_tftb1odc739t5 t5 = gm_f_push_mjiecfq(t2, &t4);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srck6bk8q21r07v, 180, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t8 = gm_l0;
        if (!(((((uint8_t *)t8) + 32))[16] != 0)) goto ie7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)t8) + 32));
        gm_l2 = t9;
        {
            gm_f_list_ccsq5cl((void *)&F.l3, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t12 = gm_l2;
                gm_l4 = t12;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t14 = gm_l5;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t16 = (t14 < t15);
                    if (!t16) goto lb13;
                    {
                        uint64_t t19 = gm_l5;
                        uint64_t t20 = (uintptr_t)t19;
                        uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        gm_tc3oht4ksth9d t24 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t20 * 88));
                        F.l6 = t24;
                        {
                            void * t26 = (void *)((uint8_t *)&F.l3);
                            void * t27 = (void *)((uint8_t *)&F.l6);
                            gm_tvul7d1mdc2eq t28;
                            gm_f_dump_expr_l3bbdps((void *)&t28, (void *)NULL, cx, NULL, t27);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_tftb1odc739t5 t29 = gm_f_push_mjiecfq(t26, &t28);
                            if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srck6bk8q21r07v, 183, 25);
                            gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
                        }
                        goto me22;
mn23:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 183, 9);
me22:;
                    }
lc13:;
                    {
                        uint64_t t32 = gm_l5;
                        uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                        uint64_t t35 = r33;
                        gm_l5 = t35;
                    }
                }
gm_ll17:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb13; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc13; }
                }
                goto gm_lad;
lb13:;
            }
        }
        {
            void * t36 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s20t5qopq8n9u));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(4));
            gm_tvul7d1mdc2eq t38;
            gm_f_node_dm1r8dn((void *)&t38, (void *)NULL, cx, NULL, t37, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t39 = gm_f_push_mjiecfq(t36, &t38);
            if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srck6bk8q21r07v, 184, 9);
            gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        gm_b16_8 t41;
        GM_ST(void *, (uint8_t *)&t41, (void *)(gm_so8qnkabacf8o));
        GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(4));
        gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t41, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
__attribute__((aligned(4))) void gm_f_dump_binding_b27qkdk(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_binding_b27qkdk F;
    void * gm_l0; /* b */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_t08olq97rscn3 gm_l5; /* n */
    void * gm_l6; /* t */
    GM_STACK_CHECK(&F, 695u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        void * t5 = gm_l0;
        uint8_t t6 = ((GM_LD(uint8_t, (((uint8_t *)t5) + 32) + 0) == ((uint8_t)0ULL)));
        if (!t6) goto ie4;
        gm_b16_8 t8;
        GM_ST(void *, (uint8_t *)&t8, (void *)(gm_stugmrojdno85));
        GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(5));
        gm_b16_8 t9 = t8;
        t3 = t9;
        goto ix4;
ie4:;
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_soi4hbchhjloe));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(3));
        gm_b16_8 t12 = t11;
        t3 = t12;
ix4:;
        gm_tvul7d1mdc2eq t13;
        GM_ST(uint8_t, (uint8_t *)&t13 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t13 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t13 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t13 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t13 + 8), t3);
        gm_tftb1odc739t5 t14 = gm_f_push_mjiecfq(t2, &t13);
        if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_srck6bk8q21r07v, 261, 5);
        gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
    }
    {
        void * t17 = gm_l0;
        uint64_t t18 = GM_LD(uintptr_t, (((uint8_t *)t17) + 8) + 8);
        uint8_t t19 = ((t18 == ((uint64_t)1ULL)));
        if (!t19) goto ie16;
        {
            void * t21 = (void *)((uint8_t *)&F.l1);
            uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
            void * t23 = gm_l0;
            uint64_t t24 = GM_LD(uintptr_t, (((uint8_t *)t23) + 8) + 8);
            if (t22 >= (uintptr_t)(t24)) gm_trap_index(t22, t24, gm_srck6bk8q21r07v, 263, 25);
            gm_tvul7d1mdc2eq t25;
            gm_f_ident_sx_kd0v352((void *)&t25, &(*(gm_t08olq97rscn3 *)((uint8_t *)GM_LD(void *, (((uint8_t *)t23) + 8)) + t22 * 32)));
            gm_tftb1odc739t5 t26 = gm_f_push_mjiecfq(t21, &t25);
            if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srck6bk8q21r07v, 263, 9);
            gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
        }
        goto ix16;
ie16:;
        {
            gm_f_list_ccsq5cl((void *)&F.l2, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t30 = gm_l0;
                gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)t30) + 8));
                gm_l3 = t31;
            }
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t33 = gm_l4;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t35 = (t33 < t34);
                    if (!t35) goto lb32;
                    {
                        uint64_t t38 = gm_l4;
                        uint64_t t39 = (uintptr_t)t38;
                        uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        gm_t08olq97rscn3 t43 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t39 * 32));
                        gm_l5 = t43;
                        {
                            void * t45 = (void *)((uint8_t *)&F.l2);
                            gm_tvul7d1mdc2eq t46;
                            gm_f_ident_sx_kd0v352((void *)&t46, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l5)));
                            gm_tftb1odc739t5 t47 = gm_f_push_mjiecfq(t45, &t46);
                            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srck6bk8q21r07v, 266, 28);
                            gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
                        }
                        goto me41;
mn42:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 266, 9);
me41:;
                    }
lc32:;
                    {
                        uint64_t t50 = gm_l4;
                        uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                        uint64_t t53 = r51;
                        gm_l4 = t53;
                    }
                }
lb32:;
            }
        }
        {
            void * t54 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sm56lhobvb6g5));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(5));
            gm_tvul7d1mdc2eq t56;
            gm_f_node_dm1r8dn((void *)&t56, (void *)NULL, cx, NULL, t55, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l2)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t57 = gm_f_push_mjiecfq(t54, &t56);
            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srck6bk8q21r07v, 267, 9);
            gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
        }
ix16:;
    }
    {
        void * t60 = gm_l0;
        void * t61 = GM_LD(void *, (((uint8_t *)t60) + 24));
        if (!(GM_LD(void *, ((uint8_t *)&t61)) != 0)) goto ie59;
        void * t62 = GM_LD(void *, ((uint8_t *)&t61));
        gm_l6 = t62;
        {
            void * t64 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t65;
            GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sqbhgmmp8kn4m));
            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(2));
            void * t66 = gm_l6;
            gm_tvul7d1mdc2eq t67;
            gm_f_dump_expr_l3bbdps((void *)&t67, (void *)NULL, cx, NULL, t66);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t68;
            gm_f_node1_gpsjtpc((void *)&t68, (void *)NULL, cx, NULL, t65, &t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t69 = gm_f_push_mjiecfq(t64, &t68);
            if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 1, gm_srck6bk8q21r07v, 269, 20);
            gm_b0_1 t70 = GM_LD(gm_b0_1, (((uint8_t *)&t69) + 1));
        }
        goto ix59;
ie59:;
ix59:;
    }
    {
        void * t71 = (void *)((uint8_t *)&F.l1);
        void * t72 = gm_l0;
        void * t73 = GM_LD(void *, (((uint8_t *)t72) + 40));
        gm_tvul7d1mdc2eq t74;
        gm_f_dump_expr_l3bbdps((void *)&t74, (void *)NULL, cx, NULL, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t75 = gm_f_push_mjiecfq(t71, &t74);
        if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_srck6bk8q21r07v, 270, 5);
        gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
    }
    {
        gm_b16_8 t77;
        GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s1m38bo3naic1));
        GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(7));
        gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t77, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_dump_block_5utjqg0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_block_5utjqg0 F;
    void * gm_l0; /* b */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 323u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 8));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_to88ae1b67aev t16 = GM_LD(gm_to88ae1b67aev, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 144));
                    F.l4 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l1);
                        void * t19 = (void *)((uint8_t *)&F.l4);
                        gm_tvul7d1mdc2eq t20;
                        gm_f_dump_stmt_tfqg5dm((void *)&t20, (void *)NULL, cx, NULL, t19);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tftb1odc739t5 t21 = gm_f_push_mjiecfq(t18, &t20);
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srck6bk8q21r07v, 276, 24);
                        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 276, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t24 = gm_l3;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l3 = t27;
                }
            }
gm_ll9:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb5; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc5; }
            }
            goto gm_lad;
lb5:;
        }
    }
    {
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_s688e8m4pgcda));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(5));
        gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t28, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_dump_clause_kje51q9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_clause_kje51q9 F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t08olq97rscn3 gm_l4; /* p */
    GM_STACK_CHECK(&F, 401u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 32));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_t08olq97rscn3 t16 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l1);
                        gm_tvul7d1mdc2eq t19;
                        gm_f_ident_sx_kd0v352((void *)&t19, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l4)));
                        gm_tftb1odc739t5 t20 = gm_f_push_mjiecfq(t18, &t19);
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srck6bk8q21r07v, 243, 25);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 243, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
            }
lb5:;
        }
    }
    {
        gm_f_list_ccsq5cl((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l5);
        void * t28 = gm_l0;
        gm_tvul7d1mdc2eq t29;
        gm_f_ident_sx_kd0v352((void *)&t29, &(*(gm_t08olq97rscn3 *)(((uint8_t *)t28) + 0)));
        gm_tftb1odc739t5 t30 = gm_f_push_mjiecfq(t27, &t29);
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srck6bk8q21r07v, 245, 5);
        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t33;
        GM_ST(void *, (uint8_t *)&t33, (void *)(gm_sfkmkcagsnqnb));
        GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(6));
        gm_tvul7d1mdc2eq t34;
        gm_f_node_dm1r8dn((void *)&t34, (void *)NULL, cx, NULL, t33, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t35 = gm_f_push_mjiecfq(t32, &t34);
        if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_srck6bk8q21r07v, 246, 5);
        gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
    }
    {
        void * t37 = (void *)((uint8_t *)&F.l5);
        void * t38 = gm_l0;
        void * t39 = GM_LD(void *, (((uint8_t *)t38) + 48));
        gm_tvul7d1mdc2eq t40;
        gm_f_dump_expr_l3bbdps((void *)&t40, (void *)NULL, cx, NULL, t39);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t41 = gm_f_push_mjiecfq(t37, &t40);
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srck6bk8q21r07v, 247, 5);
        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
    }
    {
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s7dkbj8tvt9e3));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(6));
        gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t43, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l5)));
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
inline __attribute__((aligned(4))) void gm_f_dump_cond_cu0s815(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_cond_cu0s815 F;
    void * gm_l0; /* c */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    void * gm_l6; /* value */
    void * gm_l7; /* e */
    GM_STACK_CHECK(&F, 517u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tq5j0547p8452 t16 = GM_LD(gm_tq5j0547p8452, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 80));
                    F.l4 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l1);
                        gm_tvul7d1mdc2eq t19;
                        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        if (t22 != ((uint8_t)0ULL)) goto mn21;
                        gm_t0d02142k4tu8 t23 = GM_LD(gm_t0d02142k4tu8, ((((uint8_t *)&F.l4) + 8) + 0));
                        F.l5 = t23;
                        void * t24 = GM_LD(void *, ((((uint8_t *)&F.l4) + 8) + 64));
                        gm_l6 = t24;
                        gm_b16_8 t25;
                        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_shmknkannurll));
                        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(4));
                        void * t26 = (void *)((uint8_t *)&F.l5);
                        gm_tvul7d1mdc2eq t27;
                        gm_f_dump_pattern_q6u5c24((void *)&t27, (void *)NULL, cx, NULL, t26);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        void * t28 = gm_l6;
                        gm_tvul7d1mdc2eq t29;
                        gm_f_dump_expr_l3bbdps((void *)&t29, (void *)NULL, cx, NULL, t28);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tvul7d1mdc2eq t30;
                        gm_f_node2_b4qpqat((void *)&t30, (void *)NULL, cx, NULL, t25, &t27, &t29);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        t19 = t30;
                        goto me20;
mn21:;
                        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
                        if (t32 != ((uint8_t)1ULL)) goto mn31;
                        void * t33 = GM_LD(void *, (((uint8_t *)&F.l4) + 8));
                        gm_l7 = t33;
                        void * t34 = gm_l7;
                        gm_tvul7d1mdc2eq t35;
                        gm_f_dump_expr_l3bbdps((void *)&t35, (void *)NULL, cx, NULL, t34);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        t19 = t35;
                        goto me20;
mn31:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 335, 16);
me20:;
                        gm_tftb1odc739t5 t36 = gm_f_push_mjiecfq(t18, &t19);
                        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srck6bk8q21r07v, 335, 9);
                        gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 334, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t39 = gm_l3;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    uint64_t t42 = r40;
                    gm_l3 = t42;
                }
            }
gm_ll9:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb5; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc5; }
            }
            goto gm_lad;
lb5:;
        }
    }
    {
        gm_b16_8 t43;
        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_sobcm12sogdng));
        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(4));
        gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t43, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_dump_constraints_68aa75n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_dump_constraints_68aa75n F;
    gm_b16_8 gm_l0; /* cs */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 397u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
        if (!t4) goto ie2;
        {
            gm_t4eh5qkgoio6s t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t9 = gm_l0;
            gm_l2 = t9;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t11 = gm_l3;
                uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l3;
                    uint64_t t17 = (uintptr_t)t16;
                    uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tpv68m88f9lcr t21 = GM_LD(gm_tpv68m88f9lcr, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t17 * 56));
                    F.l4 = t21;
                    {
                        void * t23 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t24;
                        GM_ST(void *, (uint8_t *)&t24, (void *)(gm_sjnl36ocas0o1));
                        GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(10));
                        gm_tvul7d1mdc2eq t25;
                        gm_f_ident_sx_kd0v352((void *)&t25, &(*(gm_t08olq97rscn3 *)(((uint8_t *)&F.l4) + 0)));
                        void * t26 = GM_LD(void *, (((uint8_t *)&F.l4) + 32));
                        gm_tvul7d1mdc2eq t27;
                        gm_f_dump_expr_l3bbdps((void *)&t27, (void *)NULL, cx, NULL, t26);
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                        gm_tvul7d1mdc2eq t28;
                        gm_f_node2_b4qpqat((void *)&t28, (void *)NULL, cx, NULL, t24, &t25, &t27);
                        if (gm_unw.kind) {
                            goto gm_ll14;
                        }
                        gm_tftb1odc739t5 t29 = gm_f_push_mjiecfq(t23, &t28);
                        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srck6bk8q21r07v, 575, 19);
                        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 575, 5);
me19:;
                }
lc10:;
                {
                    uint64_t t32 = gm_l3;
                    uint64_t r33; bool o34 = __builtin_add_overflow(t32, ((uint64_t)1ULL), &r33);
                    uint64_t t35 = r33;
                    gm_l3 = t35;
                }
            }
gm_ll14:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb10; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc10; }
            }
            goto gm_lad;
lb10:;
        }
    }
    {
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_smglbbtmdmip2));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(5));
        gm_tvul7d1mdc2eq t37;
        gm_f_node_dm1r8dn((void *)&t37, (void *)NULL, cx, NULL, t36, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(uint8_t, (uint8_t *)&(*(gm_t4eh5qkgoio6s *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_t4eh5qkgoio6s *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_t4eh5qkgoio6s *)gm_out) + 36, 0);
        ((uint8_t *)&(*(gm_t4eh5qkgoio6s *)gm_out))[32] = 1;
        GM_ST(gm_tvul7d1mdc2eq, (uint8_t *)&(*(gm_t4eh5qkgoio6s *)gm_out) + 0, t37);
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
__attribute__((aligned(4))) void gm_f_dump_effect_lit_4vqnm1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_effect_lit_4vqnm1n F;
    void * gm_l0; /* e */
    gm_b16_8 gm_l2; /* p */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* kind */
    void * gm_l8; /* r */
    GM_STACK_CHECK(&F, 761u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        if (!(((((uint8_t *)t3) + 0))[16] != 0)) goto ie2;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
        gm_l2 = t4;
        {
            void * t6 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t7 = gm_l2;
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
            gm_tvul7d1mdc2eq t9;
            gm_f_dump_params_vfgaabj((void *)&t9, (void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t10 = gm_f_push_mjiecfq(t6, &t9);
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srck6bk8q21r07v, 661, 24);
            gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t13 = gm_l0;
            gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 24));
            gm_l3 = t14;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t16 = gm_l4;
                uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t18 = (t16 < t17);
                if (!t18) goto lb15;
                {
                    uint64_t t21 = gm_l4;
                    uint64_t t22 = (uintptr_t)t21;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tc0sbs1tp6d7f t26 = GM_LD(gm_tc0sbs1tp6d7f, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t22 * 80));
                    F.l5 = t26;
                    {
                        gm_b16_8 t28;
                        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 32) + 0));
                        switch (t34) { case ((uint8_t)0ULL): goto ma30; case ((uint8_t)1ULL): goto ma31; case ((uint8_t)2ULL): goto ma32; default: goto ma33; }
ma30:;
                        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 32) + 0));
                        if (t36 != ((uint8_t)0ULL)) goto mn35;
                        gm_b16_8 t37;
                        GM_ST(void *, (uint8_t *)&t37, (void *)(gm_s579fb7l5l6df));
                        GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(2));
                        t28 = t37;
                        goto me29;
mn35:;
ma31:;
                        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 32) + 0));
                        if (t39 != ((uint8_t)1ULL)) goto mn38;
                        gm_b16_8 t40;
                        GM_ST(void *, (uint8_t *)&t40, (void *)(gm_sb88rajuutaep));
                        GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(3));
                        t28 = t40;
                        goto me29;
mn38:;
ma32:;
                        uint8_t t42 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 32) + 0));
                        if (t42 != ((uint8_t)2ULL)) goto mn41;
                        gm_b16_8 t43;
                        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s66uvortgrtsg));
                        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(3));
                        t28 = t43;
                        goto me29;
mn41:;
ma33:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 663, 17);
me29:;
                        gm_l6 = t28;
                    }
                    {
                        gm_f_list_ccsq5cl((void *)&F.l7, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll19;
                        }
                    }
                    {
                        void * t44 = (void *)((uint8_t *)&F.l7);
                        gm_tvul7d1mdc2eq t45;
                        gm_f_ident_sx_kd0v352((void *)&t45, &(*(gm_t08olq97rscn3 *)(((uint8_t *)&F.l5) + 0)));
                        gm_tftb1odc739t5 t46 = gm_f_push_mjiecfq(t44, &t45);
                        if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_srck6bk8q21r07v, 669, 9);
                        gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
                    }
                    {
                        void * t48 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t49 = gm_l6;
                        gm_tvul7d1mdc2eq t50;
                        GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t50 + 24, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t50 + 0), ((uint8_t)0ULL));
                        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 8), t49);
                        gm_tftb1odc739t5 t51 = gm_f_push_mjiecfq(t48, &t50);
                        if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srck6bk8q21r07v, 670, 9);
                        gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
                    }
                    {
                        void * t53 = (void *)((uint8_t *)&F.l7);
                        gm_b16_8 t54 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 40));
                        gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                        gm_tvul7d1mdc2eq t56;
                        gm_f_dump_params_vfgaabj((void *)&t56, (void *)NULL, cx, NULL, t55);
                        if (gm_unw.kind) {
                            goto gm_ll19;
                        }
                        gm_tftb1odc739t5 t57 = gm_f_push_mjiecfq(t53, &t56);
                        if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srck6bk8q21r07v, 671, 9);
                        gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
                    }
                    {
                        void * t60 = GM_LD(void *, (((uint8_t *)&F.l5) + 56));
                        if (!(GM_LD(void *, ((uint8_t *)&t60)) != 0)) goto ie59;
                        void * t61 = GM_LD(void *, ((uint8_t *)&t60));
                        gm_l8 = t61;
                        {
                            void * t63 = (void *)((uint8_t *)&F.l7);
                            gm_b16_8 t64;
                            GM_ST(void *, (uint8_t *)&t64, (void *)(gm_s1k3lfare8sei));
                            GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(3));
                            void * t65 = gm_l8;
                            gm_tvul7d1mdc2eq t66;
                            gm_f_dump_expr_l3bbdps((void *)&t66, (void *)NULL, cx, NULL, t65);
                            if (gm_unw.kind) {
                                goto gm_ll19;
                            }
                            gm_tvul7d1mdc2eq t67;
                            gm_f_node1_gpsjtpc((void *)&t67, (void *)NULL, cx, NULL, t64, &t66);
                            if (gm_unw.kind) {
                                goto gm_ll19;
                            }
                            gm_tftb1odc739t5 t68 = gm_f_push_mjiecfq(t63, &t67);
                            if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_srck6bk8q21r07v, 672, 26);
                            gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
                        }
                        goto ix59;
ie59:;
ix59:;
                    }
                    {
                        void * t70 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t71;
                        GM_ST(void *, (uint8_t *)&t71, (void *)(gm_sskduiq2hlj37));
                        GM_ST(uintptr_t, (uint8_t *)&t71 + 8, (uintptr_t)(2));
                        gm_tvul7d1mdc2eq t72;
                        gm_f_node_dm1r8dn((void *)&t72, (void *)NULL, cx, NULL, t71, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l7)));
                        if (gm_unw.kind) {
                            goto gm_ll19;
                        }
                        gm_tftb1odc739t5 t73 = gm_f_push_mjiecfq(t70, &t72);
                        if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_srck6bk8q21r07v, 673, 9);
                        gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
                    }
                    goto me24;
mn25:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 662, 5);
me24:;
                }
lc15:;
                {
                    uint64_t t76 = gm_l4;
                    uint64_t r77; bool o78 = __builtin_add_overflow(t76, ((uint64_t)1ULL), &r77);
                    uint64_t t79 = r77;
                    gm_l4 = t79;
                }
            }
gm_ll19:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb15; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc15; }
            }
            goto gm_lad;
lb15:;
        }
    }
    {
        gm_b16_8 t80;
        GM_ST(void *, (uint8_t *)&t80, (void *)(gm_svqdrhms9i231));
        GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(6));
        gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t80, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_dump_effects_4fqcm0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_dump_effects_4fqcm0g F;
    gm_b16_8 gm_l0; /* effects */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 267u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tc3oht4ksth9d t15 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 88));
                    F.l4 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l1);
                        void * t18 = (void *)((uint8_t *)&F.l4);
                        gm_tvul7d1mdc2eq t19;
                        gm_f_dump_expr_l3bbdps((void *)&t19, (void *)NULL, cx, NULL, t18);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tftb1odc739t5 t20 = gm_f_push_mjiecfq(t17, &t19);
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srck6bk8q21r07v, 253, 24);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 253, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
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
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sgbumttiou4de));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(7));
        gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t27, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
__attribute__((aligned(4))) void gm_f_dump_expr_l3bbdps(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_expr_l3bbdps F;
    void * gm_l0; /* e */
    void * gm_l2; /* l */
    gm_tj2m9dbh1ekv6 gm_l3; /* s */
    uint32_t gm_l4; /* n */
    uint32_t gm_l5; /* n */
    void * gm_l6; /* l */
    gm_t9qkh0rsooc8d gm_l8; /* s */
    gm_tibupt4dmcqai gm_l9; /* s */
    uint8_t gm_l10; /* b */
    gm_t08olq97rscn3 gm_l11; /* id */
    gm_t08olq97rscn3 gm_l12; /* id */
    void * gm_l13; /* x */
    void * gm_l14; /* x */
    gm_b16_8 gm_l15; /* elems */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_t9qkh0rsooc8d gm_l19; /* op */
    void * gm_l20; /* operand */
    gm_ts6oqppjh0ba3 gm_l21; /* op */
    void * gm_l22; /* lhs */
    void * gm_l23; /* rhs */
    gm_t08olq97rscn3 gm_l25; /* c */
    void * gm_l26; /* x */
    void * gm_l27; /* x */
    void * gm_l28; /* lhs */
    void * gm_l29; /* rhs */
    void * gm_l30; /* x */
    void * gm_l31; /* callee */
    gm_b16_8 gm_l32; /* args */
    uint8_t gm_l33; /* trailing_comma */
    gm_b16_8 gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    void * gm_l37; /* base */
    void * gm_l38; /* index */
    gm_tao63pk3r18v5 gm_l39; /* x */
    void * gm_l40; /* lo */
    void * gm_l41; /* hi */
    void * gm_l42; /* base */
    gm_t08olq97rscn3 gm_l43; /* n */
    void * gm_l44; /* base */
    uint32_t gm_l45; /* index */
    void * gm_l46; /* x */
    uint8_t gm_l47; /* is_const */
    void * gm_l48; /* inner */
    uint8_t gm_l49; /* is_const */
    void * gm_l50; /* inner */
    uint8_t gm_l51; /* is_const */
    void * gm_l52; /* inner */
    gm_t3mf58gv9kf3n gm_l53; /* a */
    gm_tvul7d1mdc2eq gm_l54; /* length */
    void * gm_l55; /* l */
    void * gm_l56; /* err */
    void * gm_l57; /* ok */
    void * gm_l58; /* sig */
    void * gm_l59; /* x */
    void * gm_l60; /* s */
    void * gm_l61; /* u */
    void * gm_l62; /* eff */
    gm_b16_8 gm_l63; /* attrs */
    void * gm_l64; /* inner */
    void * gm_l65; /* f */
    void * gm_l66; /* h */
    gm_b16_8 gm_l67; /* <tmp> */
    uint64_t gm_l68; /* <tmp> */
    gm_tibupt4dmcqai gm_l70; /* s */
    void * gm_l71; /* f */
    gm_b16_8 gm_l72; /* <tmp> */
    uint64_t gm_l73; /* <tmp> */
    void * gm_l75; /* i */
    void * gm_l76; /* m */
    gm_b16_8 gm_l77; /* <tmp> */
    uint64_t gm_l78; /* <tmp> */
    void * gm_l80; /* w */
    gm_t08olq97rscn3 gm_l81; /* l */
    gm_b16_8 gm_l83; /* <tmp> */
    uint64_t gm_l84; /* <tmp> */
    void * gm_l86; /* x */
    void * gm_l87; /* f */
    gm_t08olq97rscn3 gm_l88; /* l */
    gm_b16_8 gm_l90; /* <tmp> */
    uint64_t gm_l91; /* <tmp> */
    void * gm_l94; /* x */
    gm_t08olq97rscn3 gm_l96; /* l */
    void * gm_l97; /* x */
    gm_t08olq97rscn3 gm_l98; /* l */
    gm_t3mf58gv9kf3n gm_l99; /* r */
    void * gm_l100; /* x */
    gm_b16_8 gm_l102; /* xs */
    gm_b16_8 gm_l104; /* <tmp> */
    uint64_t gm_l105; /* <tmp> */
    GM_STACK_CHECK(&F, 8685u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        uint8_t t58 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t58) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma15; case ((uint8_t)11ULL): goto ma14; case ((uint8_t)12ULL): goto ma16; case ((uint8_t)13ULL): goto ma17; case ((uint8_t)14ULL): goto ma18; case ((uint8_t)15ULL): goto ma19; case ((uint8_t)16ULL): goto ma20; case ((uint8_t)17ULL): goto ma21; case ((uint8_t)18ULL): goto ma22; case ((uint8_t)19ULL): goto ma23; case ((uint8_t)20ULL): goto ma24; case ((uint8_t)21ULL): goto ma25; case ((uint8_t)22ULL): goto ma26; case ((uint8_t)23ULL): goto ma27; case ((uint8_t)24ULL): goto ma28; case ((uint8_t)25ULL): goto ma29; case ((uint8_t)26ULL): goto ma30; case ((uint8_t)27ULL): goto ma31; case ((uint8_t)28ULL): goto ma32; case ((uint8_t)29ULL): goto ma33; case ((uint8_t)30ULL): goto ma34; case ((uint8_t)31ULL): goto ma35; case ((uint8_t)32ULL): goto ma36; case ((uint8_t)33ULL): goto ma37; case ((uint8_t)34ULL): goto ma38; case ((uint8_t)35ULL): goto ma39; case ((uint8_t)36ULL): goto ma40; case ((uint8_t)37ULL): goto ma41; case ((uint8_t)38ULL): goto ma42; case ((uint8_t)39ULL): goto ma43; case ((uint8_t)40ULL): goto ma44; case ((uint8_t)41ULL): goto ma45; case ((uint8_t)42ULL): goto ma46; case ((uint8_t)43ULL): goto ma47; case ((uint8_t)44ULL): goto ma48; case ((uint8_t)45ULL): goto ma49; case ((uint8_t)46ULL): goto ma51; case ((uint8_t)47ULL): goto ma52; case ((uint8_t)48ULL): goto ma54; case ((uint8_t)49ULL): goto ma55; case ((uint8_t)50ULL): goto ma56; default: goto ma57; }
ma4:;
        uint8_t t60 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t60 != ((uint8_t)0ULL)) goto mn59;
        void * t61 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t61;
        {
            void * t63 = (void *)((uint8_t *)&F.l1);
            void * t64 = gm_l2;
            gm_b16_8 t65 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)t64) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
            gm_tvul7d1mdc2eq t67;
            GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t67 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t67 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t67 + 8), t66);
            gm_tftb1odc739t5 t68 = gm_f_push_mjiecfq(t63, &t67);
            if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_srck6bk8q21r07v, 349, 13);
            gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
        }
        {
            void * t71 = gm_l2;
            if (!(((((uint8_t *)t71) + 48))[8] != 0)) goto ie70;
            gm_tj2m9dbh1ekv6 t72 = GM_LD(gm_tj2m9dbh1ekv6, (((uint8_t *)t71) + 48));
            gm_l3 = t72;
            {
                void * t74 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t75;
                uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                switch (t82) { case ((uint8_t)0ULL): goto ma77; case ((uint8_t)1ULL): goto ma78; case ((uint8_t)2ULL): goto ma79; case ((uint8_t)3ULL): goto ma80; default: goto ma81; }
ma77:;
                uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t84 != ((uint8_t)0ULL)) goto mn83;
                uint32_t t85 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
                gm_l4 = t85;
                uint32_t t86 = gm_l4;
                gm_t88rcucg1b7uk t87;
                GM_ST(uint32_t, ((uint8_t *)&t87 + 0), t86);
                gm_tabb710d9mq2e t88;
                gm_f_format_s4n90cc((void *)&t88, (void *)NULL, cx, NULL, t87);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 8, gm_srck6bk8q21r07v, 352, 30);
                gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&t88) + 8));
                gm_b16_8 t90 = GM_LD(gm_b16_8, ((uint8_t *)&t89));
                t75 = t90;
                goto me76;
mn83:;
ma78:;
                uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t92 != ((uint8_t)1ULL)) goto mn91;
                uint32_t t93 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
                gm_l5 = t93;
                uint32_t t94 = gm_l5;
                gm_t88rcucg1b7uk t95;
                GM_ST(uint32_t, ((uint8_t *)&t95 + 0), t94);
                gm_tabb710d9mq2e t96;
                gm_f_format_c0h6o29((void *)&t96, (void *)NULL, cx, NULL, t95);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t96))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t96) + 8, gm_srck6bk8q21r07v, 353, 30);
                gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&t96) + 8));
                gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
                t75 = t98;
                goto me76;
mn91:;
ma79:;
                uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t100 != ((uint8_t)2ULL)) goto mn99;
                gm_b16_8 t101;
                GM_ST(void *, (uint8_t *)&t101, (void *)(gm_s6groq37ram3h));
                GM_ST(uintptr_t, (uint8_t *)&t101 + 8, (uintptr_t)(5));
                t75 = t101;
                goto me76;
mn99:;
ma80:;
                uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
                if (t103 != ((uint8_t)3ULL)) goto mn102;
                gm_b16_8 t104;
                GM_ST(void *, (uint8_t *)&t104, (void *)(gm_s0i3i86nphpsh));
                GM_ST(uintptr_t, (uint8_t *)&t104 + 8, (uintptr_t)(5));
                t75 = t104;
                goto me76;
mn102:;
ma81:;
                gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 351, 30);
me76:;
                gm_tvul7d1mdc2eq t105;
                GM_ST(uint8_t, (uint8_t *)&t105 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t105 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t105 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t105 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t105 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t105 + 8), t75);
                gm_tftb1odc739t5 t106 = gm_f_push_mjiecfq(t74, &t105);
                if (((((uint8_t *)&t106))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t106) + 1, gm_srck6bk8q21r07v, 351, 17);
                gm_b0_1 t107 = GM_LD(gm_b0_1, (((uint8_t *)&t106) + 1));
            }
            goto ix70;
ie70:;
ix70:;
        }
        {
            gm_b16_8 t108;
            void * t110 = gm_l2;
            uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)t110) + 60));
            if (!t111) goto ie109;
            gm_b16_8 t113;
            GM_ST(void *, (uint8_t *)&t113, (void *)(gm_ssbg0bg772d67));
            GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(4));
            gm_b16_8 t114 = t113;
            t108 = t114;
            goto ix109;
ie109:;
            gm_b16_8 t116;
            GM_ST(void *, (uint8_t *)&t116, (void *)(gm_sjkj5g78lejrj));
            GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(3));
            gm_b16_8 t117 = t116;
            t108 = t117;
ix109:;
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t108, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn59:;
ma5:;
        uint8_t t119 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t119 != ((uint8_t)1ULL)) goto mn118;
        void * t120 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l6 = t120;
        {
            gm_f_list_uvfu1k1((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t122 = gm_l6;
            void * t123 = (void *)((uint8_t *)&F.l7);
            gm_b0_1 t124 = gm_f_write_v26t591((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)(((uint8_t *)t122) + 0)), t123);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t125 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t126 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_b16_8 t127 = GM_LD(gm_b16_8, ((uint8_t *)&t126));
            gm_tvul7d1mdc2eq t128;
            GM_ST(uint8_t, (uint8_t *)&t128 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t128 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t128 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t128 + 8), t127);
            gm_tftb1odc739t5 t129 = gm_f_push_mjiecfq(t125, &t128);
            if (((((uint8_t *)&t129))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t129) + 1, gm_srck6bk8q21r07v, 363, 13);
            gm_b0_1 t130 = GM_LD(gm_b0_1, (((uint8_t *)&t129) + 1));
        }
        {
            void * t132 = gm_l6;
            if (!(((((uint8_t *)t132) + 96))[1] != 0)) goto ie131;
            gm_t9qkh0rsooc8d t133 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t132) + 96));
            gm_l8 = t133;
            {
                void * t135 = (void *)((uint8_t *)&F.l1);
                gm_t9qkh0rsooc8d t136 = gm_l8;
                gm_b16_8 t137 = gm_f_float_suffix_7g3bh9c(t136);
                gm_tvul7d1mdc2eq t138;
                GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t138 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t138 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t138 + 8), t137);
                gm_tftb1odc739t5 t139 = gm_f_push_mjiecfq(t135, &t138);
                if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 1, gm_srck6bk8q21r07v, 364, 32);
                gm_b0_1 t140 = GM_LD(gm_b0_1, (((uint8_t *)&t139) + 1));
            }
            goto ix131;
ie131:;
ix131:;
        }
        {
            gm_b16_8 t141;
            void * t143 = gm_l6;
            uint8_t t144 = GM_LD(uint8_t, (((uint8_t *)t143) + 98));
            if (!t144) goto ie142;
            gm_b16_8 t146;
            GM_ST(void *, (uint8_t *)&t146, (void *)(gm_soifq4vd92jh0));
            GM_ST(uintptr_t, (uint8_t *)&t146 + 8, (uintptr_t)(4));
            gm_b16_8 t147 = t146;
            t141 = t147;
            goto ix142;
ie142:;
            gm_b16_8 t149;
            GM_ST(void *, (uint8_t *)&t149, (void *)(gm_s4cess8gum6i7));
            GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(5));
            gm_b16_8 t150 = t149;
            t141 = t150;
ix142:;
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t141, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn118:;
ma6:;
        uint8_t t152 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t152 != ((uint8_t)2ULL)) goto mn151;
        gm_tibupt4dmcqai t153 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)t2) + 8) + 8));
        gm_l9 = t153;
        gm_f_str_lit_a2k13t3((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, &(*(gm_tibupt4dmcqai *)((uint8_t *)&gm_l9)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn151:;
ma7:;
        uint8_t t155 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t155 != ((uint8_t)3ULL)) goto mn154;
        uint8_t t156 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l10 = t156;
        gm_b16_8 t157;
        GM_ST(void *, (uint8_t *)&t157, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t157 + 8, (uintptr_t)(4));
        gm_b16_8 t158;
        uint8_t t160 = gm_l10;
        if (!t160) goto ie159;
        gm_b16_8 t162;
        GM_ST(void *, (uint8_t *)&t162, (void *)(gm_sehko5bb96s6p));
        GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(4));
        gm_b16_8 t163 = t162;
        t158 = t163;
        goto ix159;
ie159:;
        gm_b16_8 t165;
        GM_ST(void *, (uint8_t *)&t165, (void *)(gm_sogehh8t1m7tn));
        GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(5));
        gm_b16_8 t166 = t165;
        t158 = t166;
ix159:;
        gm_tvul7d1mdc2eq t167;
        GM_ST(uint8_t, (uint8_t *)&t167 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t167 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t167 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t167 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t167 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t167 + 8), t158);
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t157, &t167);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn154:;
ma8:;
        uint8_t t169 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t169 != ((uint8_t)4ULL)) goto mn168;
        gm_b16_8 t170;
        GM_ST(void *, (uint8_t *)&t170, (void *)(gm_squedv51820an));
        GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(4));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 24, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 8, t170);
        goto gm_fdone;
        goto me3;
mn168:;
ma9:;
        uint8_t t172 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t172 != ((uint8_t)5ULL)) goto mn171;
        gm_b16_8 t173;
        GM_ST(void *, (uint8_t *)&t173, (void *)(gm_sr899mamktpvo));
        GM_ST(uintptr_t, (uint8_t *)&t173 + 8, (uintptr_t)(6));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 24, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 8, t173);
        goto gm_fdone;
        goto me3;
mn171:;
ma10:;
        uint8_t t175 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t175 != ((uint8_t)6ULL)) goto mn174;
        gm_t08olq97rscn3 t176 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l11 = t176;
        gm_b16_8 t177;
        GM_ST(void *, (uint8_t *)&t177, (void *)(gm_sup2jn5hml593));
        GM_ST(uintptr_t, (uint8_t *)&t177 + 8, (uintptr_t)(5));
        gm_tvul7d1mdc2eq t178;
        gm_f_ident_sx_kd0v352((void *)&t178, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l11)));
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t177, &t178);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn174:;
ma11:;
        uint8_t t180 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t180 != ((uint8_t)7ULL)) goto mn179;
        gm_b16_8 t181;
        GM_ST(void *, (uint8_t *)&t181, (void *)(gm_snftglmif3ta2));
        GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(10));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 24, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 8, t181);
        goto gm_fdone;
        goto me3;
mn179:;
ma12:;
        uint8_t t183 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t183 != ((uint8_t)8ULL)) goto mn182;
        gm_t08olq97rscn3 t184 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l12 = t184;
        gm_b16_8 t185;
        GM_ST(void *, (uint8_t *)&t185, (void *)(gm_sg1h22ve4uqmq));
        GM_ST(uintptr_t, (uint8_t *)&t185 + 8, (uintptr_t)(3));
        gm_tvul7d1mdc2eq t186;
        gm_f_ident_sx_kd0v352((void *)&t186, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l12)));
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t185, &t186);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn182:;
ma13:;
        uint8_t t188 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t188 != ((uint8_t)9ULL)) goto mn187;
        void * t189 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l13 = t189;
        gm_b16_8 t190;
        GM_ST(void *, (uint8_t *)&t190, (void *)(gm_s7s32h358ordg));
        GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(5));
        void * t191 = gm_l13;
        gm_tvul7d1mdc2eq t192;
        gm_f_dump_expr_l3bbdps((void *)&t192, (void *)NULL, cx, NULL, t191);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t190, &t192);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn187:;
ma14:;
        uint8_t t194 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t194 != ((uint8_t)11ULL)) goto mn193;
        void * t195 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l14 = t195;
        gm_b16_8 t196;
        GM_ST(void *, (uint8_t *)&t196, (void *)(gm_s2f5ea3l0ep8h));
        GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(8));
        void * t197 = gm_l14;
        gm_tvul7d1mdc2eq t198;
        gm_f_dump_expr_l3bbdps((void *)&t198, (void *)NULL, cx, NULL, t197);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t196, &t198);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn193:;
ma15:;
        uint8_t t200 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t200 != ((uint8_t)10ULL)) goto mn199;
        gm_b16_8 t201 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l15 = t201;
        {
            {
                gm_b16_8 t204 = gm_l15;
                gm_l16 = t204;
            }
            {
                gm_l17 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t206 = gm_l17;
                    uint64_t t207 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    uint8_t t208 = (t206 < t207);
                    if (!t208) goto lb205;
                    {
                        uint64_t t211 = gm_l17;
                        uint64_t t212 = (uintptr_t)t211;
                        uint64_t t213 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        gm_tv1o1j44ii4db t216 = GM_LD(gm_tv1o1j44ii4db, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t212 * 136));
                        F.l18 = t216;
                        {
                            void * t218 = (void *)((uint8_t *)&F.l1);
                            void * t219 = (void *)((uint8_t *)&F.l18);
                            gm_tvul7d1mdc2eq t220;
                            gm_f_dump_tuple_elem_bul8rgi((void *)&t220, (void *)NULL, cx, NULL, t219);
                            if (gm_unw.kind) {
                                goto gm_ll209;
                            }
                            gm_tftb1odc739t5 t221 = gm_f_push_mjiecfq(t218, &t220);
                            if (((((uint8_t *)&t221))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t221) + 1, gm_srck6bk8q21r07v, 377, 30);
                            gm_b0_1 t222 = GM_LD(gm_b0_1, (((uint8_t *)&t221) + 1));
                        }
                        goto me214;
mn215:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 377, 13);
me214:;
                    }
lc205:;
                    {
                        uint64_t t224 = gm_l17;
                        uint64_t r225; bool o226 = __builtin_add_overflow(t224, ((uint64_t)1ULL), &r225);
                        uint64_t t227 = r225;
                        gm_l17 = t227;
                    }
                }
gm_ll209:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb205; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc205; }
                }
                goto gm_lad;
lb205:;
            }
        }
        {
            gm_b16_8 t228;
            GM_ST(void *, (uint8_t *)&t228, (void *)(gm_sk83con0mdsfe));
            GM_ST(uintptr_t, (uint8_t *)&t228 + 8, (uintptr_t)(5));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t228, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn199:;
ma16:;
        uint8_t t230 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t230 != ((uint8_t)12ULL)) goto mn229;
        gm_t9qkh0rsooc8d t231 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l19 = t231;
        void * t232 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l20 = t232;
        gm_b16_8 t233;
        GM_ST(void *, (uint8_t *)&t233, (void *)(gm_sbiv3c5qa6ipi));
        GM_ST(uintptr_t, (uint8_t *)&t233 + 8, (uintptr_t)(5));
        gm_t9qkh0rsooc8d t234 = gm_l19;
        gm_b16_8 t235 = gm_f_unop_name_j99tcv9(t234);
        gm_tvul7d1mdc2eq t236;
        GM_ST(uint8_t, (uint8_t *)&t236 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t236 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t236 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t236 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t236 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t236 + 8), t235);
        void * t237 = gm_l20;
        gm_tvul7d1mdc2eq t238;
        gm_f_dump_expr_l3bbdps((void *)&t238, (void *)NULL, cx, NULL, t237);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t233, &t236, &t238);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn229:;
ma17:;
        uint8_t t240 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t240 != ((uint8_t)13ULL)) goto mn239;
        gm_ts6oqppjh0ba3 t241 = GM_LD(gm_ts6oqppjh0ba3, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l21 = t241;
        void * t242 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l22 = t242;
        void * t243 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l23 = t243;
        {
            void * t245 = (void *)((uint8_t *)&F.l1);
            gm_ts6oqppjh0ba3 t246 = gm_l21;
            gm_b16_8 t247 = gm_f_binop_name_1d6k17q(t246);
            gm_tvul7d1mdc2eq t248;
            GM_ST(uint8_t, (uint8_t *)&t248 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t248 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t248 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t248 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t248 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t248 + 8), t247);
            gm_tftb1odc739t5 t249 = gm_f_push_mjiecfq(t245, &t248);
            if (((((uint8_t *)&t249))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t249) + 1, gm_srck6bk8q21r07v, 382, 13);
            gm_b0_1 t250 = GM_LD(gm_b0_1, (((uint8_t *)&t249) + 1));
        }
        {
            void * t251 = (void *)((uint8_t *)&F.l1);
            void * t252 = gm_l22;
            gm_tvul7d1mdc2eq t253;
            gm_f_dump_expr_l3bbdps((void *)&t253, (void *)NULL, cx, NULL, t252);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t254 = gm_f_push_mjiecfq(t251, &t253);
            if (((((uint8_t *)&t254))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t254) + 1, gm_srck6bk8q21r07v, 383, 13);
            gm_b0_1 t255 = GM_LD(gm_b0_1, (((uint8_t *)&t254) + 1));
        }
        {
            void * t256 = (void *)((uint8_t *)&F.l1);
            void * t257 = gm_l23;
            gm_tvul7d1mdc2eq t258;
            gm_f_dump_expr_l3bbdps((void *)&t258, (void *)NULL, cx, NULL, t257);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t259 = gm_f_push_mjiecfq(t256, &t258);
            if (((((uint8_t *)&t259))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t259) + 1, gm_srck6bk8q21r07v, 384, 13);
            gm_b0_1 t260 = GM_LD(gm_b0_1, (((uint8_t *)&t259) + 1));
        }
        {
            gm_b16_8 t261;
            GM_ST(void *, (uint8_t *)&t261, (void *)(gm_s8e72gdumihb1));
            GM_ST(uintptr_t, (uint8_t *)&t261 + 8, (uintptr_t)(6));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t261, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn239:;
ma18:;
        uint8_t t263 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t263 != ((uint8_t)14ULL)) goto mn262;
        gm_tioi3jh9v22g5 t264 = GM_LD(gm_tioi3jh9v22g5, ((((uint8_t *)t2) + 8) + 8));
        F.l24 = t264;
        {
            void * t266 = (void *)((uint8_t *)&F.l1);
            void * t267 = GM_LD(void *, (((uint8_t *)&F.l24) + 0));
            gm_tvul7d1mdc2eq t268;
            gm_f_dump_expr_l3bbdps((void *)&t268, (void *)NULL, cx, NULL, t267);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t269 = gm_f_push_mjiecfq(t266, &t268);
            if (((((uint8_t *)&t269))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t269) + 1, gm_srck6bk8q21r07v, 388, 13);
            gm_b0_1 t270 = GM_LD(gm_b0_1, (((uint8_t *)&t269) + 1));
        }
        {
            if (!(((((uint8_t *)&F.l24) + 8))[32] != 0)) goto ie271;
            gm_t08olq97rscn3 t272 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l24) + 8));
            gm_l25 = t272;
            {
                void * t274 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t275;
                GM_ST(void *, (uint8_t *)&t275, (void *)(gm_sqrfl54mgc34i));
                GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(7));
                gm_tvul7d1mdc2eq t276;
                gm_f_ident_sx_kd0v352((void *)&t276, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l25)));
                gm_tvul7d1mdc2eq t277;
                gm_f_node1_gpsjtpc((void *)&t277, (void *)NULL, cx, NULL, t275, &t276);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t278 = gm_f_push_mjiecfq(t274, &t277);
                if (((((uint8_t *)&t278))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t278) + 1, gm_srck6bk8q21r07v, 389, 33);
                gm_b0_1 t279 = GM_LD(gm_b0_1, (((uint8_t *)&t278) + 1));
            }
            goto ix271;
ie271:;
ix271:;
        }
        {
            void * t280 = (void *)((uint8_t *)&F.l1);
            void * t281 = GM_LD(void *, (((uint8_t *)&F.l24) + 48));
            gm_tvul7d1mdc2eq t282;
            gm_f_dump_expr_l3bbdps((void *)&t282, (void *)NULL, cx, NULL, t281);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t283 = gm_f_push_mjiecfq(t280, &t282);
            if (((((uint8_t *)&t283))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t283) + 1, gm_srck6bk8q21r07v, 390, 13);
            gm_b0_1 t284 = GM_LD(gm_b0_1, (((uint8_t *)&t283) + 1));
        }
        {
            gm_b16_8 t285;
            GM_ST(void *, (uint8_t *)&t285, (void *)(gm_s9es2tos1bhfp));
            GM_ST(uintptr_t, (uint8_t *)&t285 + 8, (uintptr_t)(4));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t285, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn262:;
ma19:;
        uint8_t t287 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t287 != ((uint8_t)15ULL)) goto mn286;
        void * t288 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l26 = t288;
        gm_b16_8 t289;
        GM_ST(void *, (uint8_t *)&t289, (void *)(gm_sdfmdfp384tf7));
        GM_ST(uintptr_t, (uint8_t *)&t289 + 8, (uintptr_t)(10));
        void * t290 = gm_l26;
        gm_tvul7d1mdc2eq t291;
        gm_f_dump_expr_l3bbdps((void *)&t291, (void *)NULL, cx, NULL, t290);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t289, &t291);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn286:;
ma20:;
        uint8_t t293 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t293 != ((uint8_t)16ULL)) goto mn292;
        void * t294 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l27 = t294;
        gm_b16_8 t295;
        GM_ST(void *, (uint8_t *)&t295, (void *)(gm_st38p0j1i82np));
        GM_ST(uintptr_t, (uint8_t *)&t295 + 8, (uintptr_t)(10));
        void * t296 = gm_l27;
        gm_tvul7d1mdc2eq t297;
        gm_f_dump_expr_l3bbdps((void *)&t297, (void *)NULL, cx, NULL, t296);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t295, &t297);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn292:;
ma21:;
        uint8_t t299 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t299 != ((uint8_t)17ULL)) goto mn298;
        void * t300 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l28 = t300;
        void * t301 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l29 = t301;
        gm_b16_8 t302;
        GM_ST(void *, (uint8_t *)&t302, (void *)(gm_s79a8ngj68lbf));
        GM_ST(uintptr_t, (uint8_t *)&t302 + 8, (uintptr_t)(4));
        void * t303 = gm_l28;
        gm_tvul7d1mdc2eq t304;
        gm_f_dump_expr_l3bbdps((void *)&t304, (void *)NULL, cx, NULL, t303);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t305 = gm_l29;
        gm_tvul7d1mdc2eq t306;
        gm_f_dump_expr_l3bbdps((void *)&t306, (void *)NULL, cx, NULL, t305);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t302, &t304, &t306);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn298:;
ma22:;
        uint8_t t308 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t308 != ((uint8_t)18ULL)) goto mn307;
        void * t309 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l30 = t309;
        gm_b16_8 t310;
        GM_ST(void *, (uint8_t *)&t310, (void *)(gm_sh4niqnmh73co));
        GM_ST(uintptr_t, (uint8_t *)&t310 + 8, (uintptr_t)(5));
        void * t311 = gm_l30;
        gm_tvul7d1mdc2eq t312;
        gm_f_dump_expr_l3bbdps((void *)&t312, (void *)NULL, cx, NULL, t311);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t310, &t312);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn307:;
ma23:;
        uint8_t t314 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t314 != ((uint8_t)19ULL)) goto mn313;
        void * t315 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l31 = t315;
        gm_b16_8 t316 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l32 = t316;
        uint8_t t317 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 24));
        gm_l33 = t317;
        {
            void * t319 = (void *)((uint8_t *)&F.l1);
            void * t320 = gm_l31;
            gm_tvul7d1mdc2eq t321;
            gm_f_dump_expr_l3bbdps((void *)&t321, (void *)NULL, cx, NULL, t320);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t322 = gm_f_push_mjiecfq(t319, &t321);
            if (((((uint8_t *)&t322))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t322) + 1, gm_srck6bk8q21r07v, 398, 13);
            gm_b0_1 t323 = GM_LD(gm_b0_1, (((uint8_t *)&t322) + 1));
        }
        {
            {
                gm_b16_8 t325 = gm_l32;
                gm_l34 = t325;
            }
            {
                gm_l35 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t327 = gm_l35;
                    uint64_t t328 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                    uint8_t t329 = (t327 < t328);
                    if (!t329) goto lb326;
                    {
                        uint64_t t332 = gm_l35;
                        uint64_t t333 = (uintptr_t)t332;
                        uint64_t t334 = GM_LD(uintptr_t, ((uint8_t *)&gm_l34) + 8);
                        gm_t7te2tdpebbt4 t337 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l34)) + t333 * 144));
                        F.l36 = t337;
                        {
                            void * t339 = (void *)((uint8_t *)&F.l1);
                            void * t340 = (void *)((uint8_t *)&F.l36);
                            gm_tvul7d1mdc2eq t341;
                            gm_f_dump_arg_9iv8pdv((void *)&t341, (void *)NULL, cx, NULL, t340);
                            if (gm_unw.kind) {
                                goto gm_ll330;
                            }
                            gm_tftb1odc739t5 t342 = gm_f_push_mjiecfq(t339, &t341);
                            if (((((uint8_t *)&t342))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t342) + 1, gm_srck6bk8q21r07v, 399, 29);
                            gm_b0_1 t343 = GM_LD(gm_b0_1, (((uint8_t *)&t342) + 1));
                        }
                        goto me335;
mn336:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 399, 13);
me335:;
                    }
lc326:;
                    {
                        uint64_t t345 = gm_l35;
                        uint64_t r346; bool o347 = __builtin_add_overflow(t345, ((uint64_t)1ULL), &r346);
                        uint64_t t348 = r346;
                        gm_l35 = t348;
                    }
                }
gm_ll330:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb326; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc326; }
                }
                goto gm_lad;
lb326:;
            }
        }
        {
            uint8_t t350 = gm_l33;
            if (!t350) goto ie349;
            {
                void * t352 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t353;
                GM_ST(void *, (uint8_t *)&t353, (void *)(gm_srq9bjb7d7qo7));
                GM_ST(uintptr_t, (uint8_t *)&t353 + 8, (uintptr_t)(14));
                gm_tvul7d1mdc2eq t354;
                GM_ST(uint8_t, (uint8_t *)&t354 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t354 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t354 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t354 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t354 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t354 + 8), t353);
                gm_tftb1odc739t5 t355 = gm_f_push_mjiecfq(t352, &t354);
                if (((((uint8_t *)&t355))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t355) + 1, gm_srck6bk8q21r07v, 400, 33);
                gm_b0_1 t356 = GM_LD(gm_b0_1, (((uint8_t *)&t355) + 1));
            }
            goto ix349;
ie349:;
ix349:;
        }
        {
            gm_b16_8 t357;
            GM_ST(void *, (uint8_t *)&t357, (void *)(gm_spbv5h8c0ukao));
            GM_ST(uintptr_t, (uint8_t *)&t357 + 8, (uintptr_t)(4));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t357, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn313:;
ma24:;
        uint8_t t359 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t359 != ((uint8_t)20ULL)) goto mn358;
        void * t360 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l37 = t360;
        void * t361 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l38 = t361;
        gm_b16_8 t362;
        GM_ST(void *, (uint8_t *)&t362, (void *)(gm_s5dbqnns2epkg));
        GM_ST(uintptr_t, (uint8_t *)&t362 + 8, (uintptr_t)(5));
        void * t363 = gm_l37;
        gm_tvul7d1mdc2eq t364;
        gm_f_dump_expr_l3bbdps((void *)&t364, (void *)NULL, cx, NULL, t363);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t365 = gm_l38;
        gm_tvul7d1mdc2eq t366;
        gm_f_dump_expr_l3bbdps((void *)&t366, (void *)NULL, cx, NULL, t365);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t362, &t364, &t366);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn358:;
ma25:;
        uint8_t t368 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t368 != ((uint8_t)21ULL)) goto mn367;
        gm_tao63pk3r18v5 t369 = GM_LD(gm_tao63pk3r18v5, ((((uint8_t *)t2) + 8) + 8));
        gm_l39 = t369;
        {
            void * t371 = (void *)((uint8_t *)&F.l1);
            void * t372 = GM_LD(void *, (((uint8_t *)&gm_l39) + 0));
            gm_tvul7d1mdc2eq t373;
            gm_f_dump_expr_l3bbdps((void *)&t373, (void *)NULL, cx, NULL, t372);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t374 = gm_f_push_mjiecfq(t371, &t373);
            if (((((uint8_t *)&t374))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t374) + 1, gm_srck6bk8q21r07v, 405, 13);
            gm_b0_1 t375 = GM_LD(gm_b0_1, (((uint8_t *)&t374) + 1));
        }
        {
            void * t377 = GM_LD(void *, (((uint8_t *)&gm_l39) + 8));
            if (!(GM_LD(void *, ((uint8_t *)&t377)) != 0)) goto ie376;
            void * t378 = GM_LD(void *, ((uint8_t *)&t377));
            gm_l40 = t378;
            {
                void * t380 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t381;
                GM_ST(void *, (uint8_t *)&t381, (void *)(gm_s3bakerohlr2a));
                GM_ST(uintptr_t, (uint8_t *)&t381 + 8, (uintptr_t)(2));
                void * t382 = gm_l40;
                gm_tvul7d1mdc2eq t383;
                gm_f_dump_expr_l3bbdps((void *)&t383, (void *)NULL, cx, NULL, t382);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tvul7d1mdc2eq t384;
                gm_f_node1_gpsjtpc((void *)&t384, (void *)NULL, cx, NULL, t381, &t383);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t385 = gm_f_push_mjiecfq(t380, &t384);
                if (((((uint8_t *)&t385))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t385) + 1, gm_srck6bk8q21r07v, 406, 29);
                gm_b0_1 t386 = GM_LD(gm_b0_1, (((uint8_t *)&t385) + 1));
            }
            goto ix376;
ie376:;
ix376:;
        }
        {
            void * t388 = GM_LD(void *, (((uint8_t *)&gm_l39) + 16));
            if (!(GM_LD(void *, ((uint8_t *)&t388)) != 0)) goto ie387;
            void * t389 = GM_LD(void *, ((uint8_t *)&t388));
            gm_l41 = t389;
            {
                void * t391 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t392;
                GM_ST(void *, (uint8_t *)&t392, (void *)(gm_samlp70hmirme));
                GM_ST(uintptr_t, (uint8_t *)&t392 + 8, (uintptr_t)(2));
                void * t393 = gm_l41;
                gm_tvul7d1mdc2eq t394;
                gm_f_dump_expr_l3bbdps((void *)&t394, (void *)NULL, cx, NULL, t393);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tvul7d1mdc2eq t395;
                gm_f_node1_gpsjtpc((void *)&t395, (void *)NULL, cx, NULL, t392, &t394);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t396 = gm_f_push_mjiecfq(t391, &t395);
                if (((((uint8_t *)&t396))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t396) + 1, gm_srck6bk8q21r07v, 407, 29);
                gm_b0_1 t397 = GM_LD(gm_b0_1, (((uint8_t *)&t396) + 1));
            }
            goto ix387;
ie387:;
ix387:;
        }
        {
            uint8_t t399 = GM_LD(uint8_t, (((uint8_t *)&gm_l39) + 24));
            if (!t399) goto ie398;
            {
                void * t401 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t402;
                GM_ST(void *, (uint8_t *)&t402, (void *)(gm_s3ia9jm7npe6t));
                GM_ST(uintptr_t, (uint8_t *)&t402 + 8, (uintptr_t)(9));
                gm_tvul7d1mdc2eq t403;
                GM_ST(uint8_t, (uint8_t *)&t403 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t403 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t403 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t403 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t403 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t403 + 8), t402);
                gm_tftb1odc739t5 t404 = gm_f_push_mjiecfq(t401, &t403);
                if (((((uint8_t *)&t404))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t404) + 1, gm_srck6bk8q21r07v, 408, 30);
                gm_b0_1 t405 = GM_LD(gm_b0_1, (((uint8_t *)&t404) + 1));
            }
            goto ix398;
ie398:;
ix398:;
        }
        {
            gm_b16_8 t406;
            GM_ST(void *, (uint8_t *)&t406, (void *)(gm_ss496b65r9omj));
            GM_ST(uintptr_t, (uint8_t *)&t406 + 8, (uintptr_t)(5));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t406, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn367:;
ma26:;
        uint8_t t408 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t408 != ((uint8_t)22ULL)) goto mn407;
        void * t409 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l42 = t409;
        gm_t08olq97rscn3 t410 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l43 = t410;
        gm_b16_8 t411;
        GM_ST(void *, (uint8_t *)&t411, (void *)(gm_s1jboc8lkvac0));
        GM_ST(uintptr_t, (uint8_t *)&t411 + 8, (uintptr_t)(5));
        void * t412 = gm_l42;
        gm_tvul7d1mdc2eq t413;
        gm_f_dump_expr_l3bbdps((void *)&t413, (void *)NULL, cx, NULL, t412);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvul7d1mdc2eq t414;
        gm_f_ident_sx_kd0v352((void *)&t414, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l43)));
        gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t411, &t413, &t414);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn407:;
ma27:;
        uint8_t t416 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t416 != ((uint8_t)23ULL)) goto mn415;
        void * t417 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l44 = t417;
        uint32_t t418 = GM_LD(uint32_t, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l45 = t418;
        gm_b16_8 t419;
        GM_ST(void *, (uint8_t *)&t419, (void *)(gm_s7ntlu0vonte9));
        GM_ST(uintptr_t, (uint8_t *)&t419 + 8, (uintptr_t)(11));
        void * t420 = gm_l44;
        gm_tvul7d1mdc2eq t421;
        gm_f_dump_expr_l3bbdps((void *)&t421, (void *)NULL, cx, NULL, t420);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t422 = gm_l45;
        gm_t88rcucg1b7uk t423;
        GM_ST(uint32_t, ((uint8_t *)&t423 + 0), t422);
        gm_tabb710d9mq2e t424;
        gm_f_format_32n178h((void *)&t424, (void *)NULL, cx, NULL, t423);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (((((uint8_t *)&t424))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t424) + 8, gm_srck6bk8q21r07v, 412, 91);
        gm_b16_8 t425 = GM_LD(gm_b16_8, (((uint8_t *)&t424) + 8));
        gm_b16_8 t426 = GM_LD(gm_b16_8, ((uint8_t *)&t425));
        gm_tvul7d1mdc2eq t427;
        GM_ST(uint8_t, (uint8_t *)&t427 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t427 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t427 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t427 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t427 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t427 + 8), t426);
        gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t419, &t421, &t427);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn415:;
ma28:;
        uint8_t t429 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t429 != ((uint8_t)24ULL)) goto mn428;
        void * t430 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l46 = t430;
        gm_b16_8 t431;
        GM_ST(void *, (uint8_t *)&t431, (void *)(gm_s8aj3oag1l1u0));
        GM_ST(uintptr_t, (uint8_t *)&t431 + 8, (uintptr_t)(13));
        void * t432 = gm_l46;
        gm_tvul7d1mdc2eq t433;
        gm_f_dump_expr_l3bbdps((void *)&t433, (void *)NULL, cx, NULL, t432);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t431, &t433);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn428:;
ma29:;
        uint8_t t435 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t435 != ((uint8_t)25ULL)) goto mn434;
        uint8_t t436 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l47 = t436;
        void * t437 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l48 = t437;
        gm_b16_8 t438;
        GM_ST(void *, (uint8_t *)&t438, (void *)(gm_svvuql4te1jp4));
        GM_ST(uintptr_t, (uint8_t *)&t438 + 8, (uintptr_t)(8));
        uint8_t t439 = gm_l47;
        void * t440 = gm_l48;
        gm_f_const_type_o8v21gi((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t438, t439, t440);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn434:;
ma30:;
        uint8_t t442 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t442 != ((uint8_t)26ULL)) goto mn441;
        uint8_t t443 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l49 = t443;
        void * t444 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l50 = t444;
        gm_b16_8 t445;
        GM_ST(void *, (uint8_t *)&t445, (void *)(gm_s0gu0jo9fq1ca));
        GM_ST(uintptr_t, (uint8_t *)&t445 + 8, (uintptr_t)(10));
        uint8_t t446 = gm_l49;
        void * t447 = gm_l50;
        gm_f_const_type_o8v21gi((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t445, t446, t447);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn441:;
ma31:;
        uint8_t t449 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t449 != ((uint8_t)27ULL)) goto mn448;
        uint8_t t450 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l51 = t450;
        void * t451 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l52 = t451;
        gm_b16_8 t452;
        GM_ST(void *, (uint8_t *)&t452, (void *)(gm_sgf1euukkdve3));
        GM_ST(uintptr_t, (uint8_t *)&t452 + 8, (uintptr_t)(13));
        uint8_t t453 = gm_l51;
        void * t454 = gm_l52;
        gm_f_const_type_o8v21gi((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t452, t453, t454);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn448:;
ma32:;
        uint8_t t456 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t456 != ((uint8_t)28ULL)) goto mn455;
        gm_t3mf58gv9kf3n t457 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l53 = t457;
        {
            gm_b16_8 t459;
            GM_ST(void *, (uint8_t *)&t459, (void *)(gm_sfdbfrv46lihd));
            GM_ST(uintptr_t, (uint8_t *)&t459 + 8, (uintptr_t)(1));
            GM_ST(uint8_t, (uint8_t *)&gm_l54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&gm_l54 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&gm_l54 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&gm_l54 + 24, 0);
            GM_ST(uint8_t, (uint8_t *)&gm_l54 + 0, ((uint8_t)0ULL));
            GM_ST(gm_b16_8, (uint8_t *)&gm_l54 + 8, t459);
        }
        {
            void * t461 = GM_LD(void *, (((uint8_t *)&gm_l53) + 0));
            if (!(GM_LD(void *, ((uint8_t *)&t461)) != 0)) goto ie460;
            void * t462 = GM_LD(void *, ((uint8_t *)&t461));
            gm_l55 = t462;
            {
                void * t464 = gm_l55;
                gm_f_dump_expr_l3bbdps((void *)&(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l54)), (void *)NULL, cx, NULL, t464);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix460;
ie460:;
ix460:;
        }
        {
            gm_b16_8 t465;
            GM_ST(void *, (uint8_t *)&t465, (void *)(gm_smedhknhdrf46));
            GM_ST(uintptr_t, (uint8_t *)&t465 + 8, (uintptr_t)(10));
            gm_tvul7d1mdc2eq t466 = gm_l54;
            void * t467 = GM_LD(void *, (((uint8_t *)&gm_l53) + 8));
            gm_tvul7d1mdc2eq t468;
            gm_f_dump_expr_l3bbdps((void *)&t468, (void *)NULL, cx, NULL, t467);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t465, &t466, &t468);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn455:;
ma33:;
        uint8_t t470 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t470 != ((uint8_t)29ULL)) goto mn469;
        void * t471 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l56 = t471;
        void * t472 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l57 = t472;
        gm_b16_8 t473;
        GM_ST(void *, (uint8_t *)&t473, (void *)(gm_s3r7cg0hbrkdg));
        GM_ST(uintptr_t, (uint8_t *)&t473 + 8, (uintptr_t)(16));
        void * t474 = gm_l56;
        gm_tvul7d1mdc2eq t475;
        gm_f_dump_expr_l3bbdps((void *)&t475, (void *)NULL, cx, NULL, t474);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t476 = gm_l57;
        gm_tvul7d1mdc2eq t477;
        gm_f_dump_expr_l3bbdps((void *)&t477, (void *)NULL, cx, NULL, t476);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t473, &t475, &t477);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn469:;
ma34:;
        uint8_t t479 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t479 != ((uint8_t)30ULL)) goto mn478;
        void * t480 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l58 = t480;
        gm_b16_8 t481;
        GM_ST(void *, (uint8_t *)&t481, (void *)(gm_s252dj7ek002d));
        GM_ST(uintptr_t, (uint8_t *)&t481 + 8, (uintptr_t)(7));
        void * t482 = gm_l58;
        gm_trqfvmjtt4sij t483;
        gm_f_list_ccsq5cl((void *)&t483, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_dump_fn_sig_4ip3qer((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t481, t482, &t483);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn478:;
ma35:;
        uint8_t t485 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t485 != ((uint8_t)31ULL)) goto mn484;
        void * t486 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l59 = t486;
        gm_b16_8 t487;
        GM_ST(void *, (uint8_t *)&t487, (void *)(gm_sa26g0ksgdkma));
        GM_ST(uintptr_t, (uint8_t *)&t487 + 8, (uintptr_t)(9));
        void * t488 = gm_l59;
        gm_tvul7d1mdc2eq t489;
        gm_f_dump_expr_l3bbdps((void *)&t489, (void *)NULL, cx, NULL, t488);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t487, &t489);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn484:;
ma36:;
        uint8_t t491 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t491 != ((uint8_t)32ULL)) goto mn490;
        void * t492 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l60 = t492;
        void * t493 = gm_l60;
        gm_f_dump_struct_lit_n9dqlnm((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t493);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn490:;
ma37:;
        uint8_t t495 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t495 != ((uint8_t)33ULL)) goto mn494;
        void * t496 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l61 = t496;
        void * t497 = gm_l61;
        gm_f_dump_union_lit_leu6qp2((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t497);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn494:;
ma38:;
        uint8_t t499 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t499 != ((uint8_t)34ULL)) goto mn498;
        void * t500 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l62 = t500;
        void * t501 = gm_l62;
        gm_f_dump_effect_lit_4vqnm1n((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t501);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn498:;
ma39:;
        uint8_t t503 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t503 != ((uint8_t)35ULL)) goto mn502;
        gm_b16_8 t504 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l63 = t504;
        void * t505 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l64 = t505;
        {
            void * t507 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t508 = gm_l63;
            gm_b16_8 t509 = GM_LD(gm_b16_8, ((uint8_t *)&t508));
            gm_b0_1 t510 = gm_f_push_attrs_a5l439c((void *)NULL, cx, NULL, t507, t509);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t511 = (void *)((uint8_t *)&F.l1);
            void * t512 = gm_l64;
            gm_tvul7d1mdc2eq t513;
            gm_f_dump_expr_l3bbdps((void *)&t513, (void *)NULL, cx, NULL, t512);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t514 = gm_f_push_mjiecfq(t511, &t513);
            if (((((uint8_t *)&t514))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t514) + 1, gm_srck6bk8q21r07v, 430, 13);
            gm_b0_1 t515 = GM_LD(gm_b0_1, (((uint8_t *)&t514) + 1));
        }
        {
            gm_b16_8 t516;
            GM_ST(void *, (uint8_t *)&t516, (void *)(gm_s167ok8osvuve));
            GM_ST(uintptr_t, (uint8_t *)&t516 + 8, (uintptr_t)(10));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t516, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn502:;
ma40:;
        uint8_t t518 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t518 != ((uint8_t)36ULL)) goto mn517;
        void * t519 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l65 = t519;
        void * t520 = gm_l65;
        gm_f_dump_fn_lit_tchlprc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t520);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn517:;
ma41:;
        uint8_t t522 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t522 != ((uint8_t)37ULL)) goto mn521;
        void * t523 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l66 = t523;
        {
            void * t525 = (void *)((uint8_t *)&F.l1);
            void * t526 = gm_l66;
            void * t527 = GM_LD(void *, (((uint8_t *)t526) + 0));
            gm_tvul7d1mdc2eq t528;
            gm_f_dump_expr_l3bbdps((void *)&t528, (void *)NULL, cx, NULL, t527);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t529 = gm_f_push_mjiecfq(t525, &t528);
            if (((((uint8_t *)&t529))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t529) + 1, gm_srck6bk8q21r07v, 435, 13);
            gm_b0_1 t530 = GM_LD(gm_b0_1, (((uint8_t *)&t529) + 1));
        }
        {
            {
                void * t532 = gm_l66;
                gm_b16_8 t533 = GM_LD(gm_b16_8, (((uint8_t *)t532) + 8));
                gm_l67 = t533;
            }
            {
                gm_l68 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t535 = gm_l68;
                    uint64_t t536 = GM_LD(uintptr_t, ((uint8_t *)&gm_l67) + 8);
                    uint8_t t537 = (t535 < t536);
                    if (!t537) goto lb534;
                    {
                        uint64_t t540 = gm_l68;
                        uint64_t t541 = (uintptr_t)t540;
                        uint64_t t542 = GM_LD(uintptr_t, ((uint8_t *)&gm_l67) + 8);
                        gm_t99vmhfllumbh t545 = GM_LD(gm_t99vmhfllumbh, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l67)) + t541 * 72));
                        F.l69 = t545;
                        {
                            void * t547 = (void *)((uint8_t *)&F.l1);
                            void * t548 = (void *)((uint8_t *)&F.l69);
                            gm_tvul7d1mdc2eq t549;
                            gm_f_dump_clause_kje51q9((void *)&t549, (void *)NULL, cx, NULL, t548);
                            if (gm_unw.kind) {
                                goto gm_ll538;
                            }
                            gm_tftb1odc739t5 t550 = gm_f_push_mjiecfq(t547, &t549);
                            if (((((uint8_t *)&t550))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t550) + 1, gm_srck6bk8q21r07v, 436, 34);
                            gm_b0_1 t551 = GM_LD(gm_b0_1, (((uint8_t *)&t550) + 1));
                        }
                        goto me543;
mn544:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 436, 13);
me543:;
                    }
lc534:;
                    {
                        uint64_t t553 = gm_l68;
                        uint64_t r554; bool o555 = __builtin_add_overflow(t553, ((uint64_t)1ULL), &r554);
                        uint64_t t556 = r554;
                        gm_l68 = t556;
                    }
                }
gm_ll538:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb534; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc534; }
                }
                goto gm_lad;
lb534:;
            }
        }
        {
            gm_b16_8 t557;
            GM_ST(void *, (uint8_t *)&t557, (void *)(gm_sic5vsema0r36));
            GM_ST(uintptr_t, (uint8_t *)&t557 + 8, (uintptr_t)(7));
            gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t557, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn521:;
ma42:;
        uint8_t t559 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t559 != ((uint8_t)38ULL)) goto mn558;
        gm_tibupt4dmcqai t560 = GM_LD(gm_tibupt4dmcqai, ((((uint8_t *)t2) + 8) + 8));
        gm_l70 = t560;
        gm_b16_8 t561;
        GM_ST(void *, (uint8_t *)&t561, (void *)(gm_sg3kpa3h16no3));
        GM_ST(uintptr_t, (uint8_t *)&t561 + 8, (uintptr_t)(6));
        gm_tvul7d1mdc2eq t562;
        gm_f_str_lit_a2k13t3((void *)&t562, (void *)NULL, cx, NULL, &(*(gm_tibupt4dmcqai *)((uint8_t *)&gm_l70)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t561, &t562);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn558:;
ma43:;
        uint8_t t564 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t564 != ((uint8_t)39ULL)) goto mn563;
        void * t565 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l71 = t565;
        {
            void * t567 = (void *)((uint8_t *)&F.l1);
            void * t568 = gm_l71;
            gm_tvul7d1mdc2eq t569;
            gm_f_str_lit_a2k13t3((void *)&t569, (void *)NULL, cx, NULL, &(*(gm_tibupt4dmcqai *)(((uint8_t *)t568) + 0)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t570 = gm_f_push_mjiecfq(t567, &t569);
            if (((((uint8_t *)&t570))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t570) + 1, gm_srck6bk8q21r07v, 441, 13);
            gm_b0_1 t571 = GM_LD(gm_b0_1, (((uint8_t *)&t570) + 1));
        }
        {
            {
                void * t573 = gm_l71;
                gm_b16_8 t574 = GM_LD(gm_b16_8, (((uint8_t *)t573) + 24));
                gm_l72 = t574;
            }
            {
                gm_l73 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t576 = gm_l73;
                    uint64_t t577 = GM_LD(uintptr_t, ((uint8_t *)&gm_l72) + 8);
                    uint8_t t578 = (t576 < t577);
                    if (!t578) goto lb575;
                    {
                        uint64_t t581 = gm_l73;
                        uint64_t t582 = (uintptr_t)t581;
                        uint64_t t583 = GM_LD(uintptr_t, ((uint8_t *)&gm_l72) + 8);
                        gm_t7rh4gj19r55v t586 = GM_LD(gm_t7rh4gj19r55v, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l72)) + t582 * 144));
                        F.l74 = t586;
                        {
                            void * t588 = (void *)((uint8_t *)&F.l1);
                            void * t589 = (void *)((uint8_t *)&F.l74);
                            gm_tvul7d1mdc2eq t590;
                            gm_f_dump_foreign_member_vrht12f((void *)&t590, (void *)NULL, cx, NULL, t589);
                            if (gm_unw.kind) {
                                goto gm_ll579;
                            }
                            gm_tftb1odc739t5 t591 = gm_f_push_mjiecfq(t588, &t590);
                            if (((((uint8_t *)&t591))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t591) + 1, gm_srck6bk8q21r07v, 442, 34);
                            gm_b0_1 t592 = GM_LD(gm_b0_1, (((uint8_t *)&t591) + 1));
                        }
                        goto me584;
mn585:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 442, 13);
me584:;
                    }
lc575:;
                    {
                        uint64_t t594 = gm_l73;
                        uint64_t r595; bool o596 = __builtin_add_overflow(t594, ((uint64_t)1ULL), &r595);
                        uint64_t t597 = r595;
                        gm_l73 = t597;
                    }
                }
gm_ll579:;
                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb575; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc575; }
                }
                goto gm_lad;
lb575:;
            }
        }
        {
            gm_b16_8 t598;
            GM_ST(void *, (uint8_t *)&t598, (void *)(gm_sv0fbf330lmcd));
            GM_ST(uintptr_t, (uint8_t *)&t598 + 8, (uintptr_t)(7));
            gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t598, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn563:;
ma44:;
        uint8_t t600 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t600 != ((uint8_t)40ULL)) goto mn599;
        void * t601 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l75 = t601;
        void * t602 = gm_l75;
        gm_f_dump_if_3qo5nul((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t602);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn599:;
ma45:;
        uint8_t t604 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t604 != ((uint8_t)41ULL)) goto mn603;
        void * t605 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l76 = t605;
        {
            void * t607 = (void *)((uint8_t *)&F.l1);
            void * t608 = gm_l76;
            void * t609 = GM_LD(void *, (((uint8_t *)t608) + 0));
            gm_tvul7d1mdc2eq t610;
            gm_f_dump_expr_l3bbdps((void *)&t610, (void *)NULL, cx, NULL, t609);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t611 = gm_f_push_mjiecfq(t607, &t610);
            if (((((uint8_t *)&t611))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t611) + 1, gm_srck6bk8q21r07v, 447, 13);
            gm_b0_1 t612 = GM_LD(gm_b0_1, (((uint8_t *)&t611) + 1));
        }
        {
            {
                void * t614 = gm_l76;
                gm_b16_8 t615 = GM_LD(gm_b16_8, (((uint8_t *)t614) + 8));
                gm_l77 = t615;
            }
            {
                gm_l78 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t617 = gm_l78;
                    uint64_t t618 = GM_LD(uintptr_t, ((uint8_t *)&gm_l77) + 8);
                    uint8_t t619 = (t617 < t618);
                    if (!t619) goto lb616;
                    {
                        uint64_t t622 = gm_l78;
                        uint64_t t623 = (uintptr_t)t622;
                        uint64_t t624 = GM_LD(uintptr_t, ((uint8_t *)&gm_l77) + 8);
                        gm_tndsf0a4f4lj0 t627 = GM_LD(gm_tndsf0a4f4lj0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l77)) + t623 * 48));
                        F.l79 = t627;
                        {
                            void * t629 = (void *)((uint8_t *)&F.l1);
                            void * t630 = (void *)((uint8_t *)&F.l79);
                            gm_tvul7d1mdc2eq t631;
                            gm_f_dump_arm_8jsad7u((void *)&t631, (void *)NULL, cx, NULL, t630);
                            if (gm_unw.kind) {
                                goto gm_ll620;
                            }
                            gm_tftb1odc739t5 t632 = gm_f_push_mjiecfq(t629, &t631);
                            if (((((uint8_t *)&t632))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t632) + 1, gm_srck6bk8q21r07v, 448, 31);
                            gm_b0_1 t633 = GM_LD(gm_b0_1, (((uint8_t *)&t632) + 1));
                        }
                        goto me625;
mn626:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 448, 13);
me625:;
                    }
lc616:;
                    {
                        uint64_t t635 = gm_l78;
                        uint64_t r636; bool o637 = __builtin_add_overflow(t635, ((uint64_t)1ULL), &r636);
                        uint64_t t638 = r636;
                        gm_l78 = t638;
                    }
                }
gm_ll620:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb616; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc616; }
                }
                goto gm_lad;
lb616:;
            }
        }
        {
            gm_b16_8 t639;
            GM_ST(void *, (uint8_t *)&t639, (void *)(gm_s8m5q4i8uoe5j));
            GM_ST(uintptr_t, (uint8_t *)&t639 + 8, (uintptr_t)(5));
            gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t639, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn603:;
ma46:;
        uint8_t t641 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t641 != ((uint8_t)42ULL)) goto mn640;
        void * t642 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l80 = t642;
        {
            void * t645 = gm_l80;
            if (!(((((uint8_t *)t645) + 0))[32] != 0)) goto ie644;
            gm_t08olq97rscn3 t646 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)t645) + 0));
            gm_l81 = t646;
            {
                void * t648 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t649;
                GM_ST(void *, (uint8_t *)&t649, (void *)(gm_su137hja0ebcd));
                GM_ST(uintptr_t, (uint8_t *)&t649 + 8, (uintptr_t)(5));
                gm_tvul7d1mdc2eq t650;
                gm_f_ident_sx_kd0v352((void *)&t650, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l81)));
                gm_tvul7d1mdc2eq t651;
                gm_f_node1_gpsjtpc((void *)&t651, (void *)NULL, cx, NULL, t649, &t650);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t652 = gm_f_push_mjiecfq(t648, &t651);
                if (((((uint8_t *)&t652))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t652) + 1, gm_srck6bk8q21r07v, 452, 31);
                gm_b0_1 t653 = GM_LD(gm_b0_1, (((uint8_t *)&t652) + 1));
            }
            goto ix644;
ie644:;
ix644:;
        }
        {
            gm_f_list_ccsq5cl((void *)&F.l82, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t655 = gm_l80;
                gm_b16_8 t656 = GM_LD(gm_b16_8, (((uint8_t *)t655) + 40));
                gm_l83 = t656;
            }
            {
                gm_l84 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t658 = gm_l84;
                    uint64_t t659 = GM_LD(uintptr_t, ((uint8_t *)&gm_l83) + 8);
                    uint8_t t660 = (t658 < t659);
                    if (!t660) goto lb657;
                    {
                        uint64_t t663 = gm_l84;
                        uint64_t t664 = (uintptr_t)t663;
                        uint64_t t665 = GM_LD(uintptr_t, ((uint8_t *)&gm_l83) + 8);
                        gm_tc3oht4ksth9d t668 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l83)) + t664 * 88));
                        F.l85 = t668;
                        {
                            void * t670 = (void *)((uint8_t *)&F.l82);
                            void * t671 = (void *)((uint8_t *)&F.l85);
                            gm_tvul7d1mdc2eq t672;
                            gm_f_dump_expr_l3bbdps((void *)&t672, (void *)NULL, cx, NULL, t671);
                            if (gm_unw.kind) {
                                goto gm_ll661;
                            }
                            gm_tftb1odc739t5 t673 = gm_f_push_mjiecfq(t670, &t672);
                            if (((((uint8_t *)&t673))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t673) + 1, gm_srck6bk8q21r07v, 454, 35);
                            gm_b0_1 t674 = GM_LD(gm_b0_1, (((uint8_t *)&t673) + 1));
                        }
                        goto me666;
mn667:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 454, 13);
me666:;
                    }
lc657:;
                    {
                        uint64_t t676 = gm_l84;
                        uint64_t r677; bool o678 = __builtin_add_overflow(t676, ((uint64_t)1ULL), &r677);
                        uint64_t t679 = r677;
                        gm_l84 = t679;
                    }
                }
gm_ll661:;
                if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb657; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc657; }
                }
                goto gm_lad;
lb657:;
            }
        }
        {
            void * t680 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t681;
            GM_ST(void *, (uint8_t *)&t681, (void *)(gm_srj7urnaf6a1u));
            GM_ST(uintptr_t, (uint8_t *)&t681 + 8, (uintptr_t)(8));
            gm_tvul7d1mdc2eq t682;
            gm_f_node_dm1r8dn((void *)&t682, (void *)NULL, cx, NULL, t681, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l82)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t683 = gm_f_push_mjiecfq(t680, &t682);
            if (((((uint8_t *)&t683))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t683) + 1, gm_srck6bk8q21r07v, 455, 13);
            gm_b0_1 t684 = GM_LD(gm_b0_1, (((uint8_t *)&t683) + 1));
        }
        {
            void * t686 = gm_l80;
            void * t687 = GM_LD(void *, (((uint8_t *)t686) + 56));
            if (!(GM_LD(void *, ((uint8_t *)&t687)) != 0)) goto ie685;
            void * t688 = GM_LD(void *, ((uint8_t *)&t687));
            gm_l86 = t688;
            {
                void * t690 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t691;
                GM_ST(void *, (uint8_t *)&t691, (void *)(gm_sdl8mgtvebfcs));
                GM_ST(uintptr_t, (uint8_t *)&t691 + 8, (uintptr_t)(2));
                void * t692 = gm_l86;
                gm_tvul7d1mdc2eq t693;
                gm_f_dump_expr_l3bbdps((void *)&t693, (void *)NULL, cx, NULL, t692);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tvul7d1mdc2eq t694;
                gm_f_node1_gpsjtpc((void *)&t694, (void *)NULL, cx, NULL, t691, &t693);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t695 = gm_f_push_mjiecfq(t690, &t694);
                if (((((uint8_t *)&t695))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t695) + 1, gm_srck6bk8q21r07v, 456, 28);
                gm_b0_1 t696 = GM_LD(gm_b0_1, (((uint8_t *)&t695) + 1));
            }
            goto ix685;
ie685:;
ix685:;
        }
        {
            void * t697 = (void *)((uint8_t *)&F.l1);
            void * t698 = gm_l80;
            void * t699 = (void *)(((uint8_t *)t698) + 64);
            gm_tvul7d1mdc2eq t700;
            gm_f_dump_block_5utjqg0((void *)&t700, (void *)NULL, cx, NULL, t699);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t701 = gm_f_push_mjiecfq(t697, &t700);
            if (((((uint8_t *)&t701))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t701) + 1, gm_srck6bk8q21r07v, 457, 13);
            gm_b0_1 t702 = GM_LD(gm_b0_1, (((uint8_t *)&t701) + 1));
        }
        {
            gm_b16_8 t703;
            GM_ST(void *, (uint8_t *)&t703, (void *)(gm_srri8c6al1v1g));
            GM_ST(uintptr_t, (uint8_t *)&t703 + 8, (uintptr_t)(4));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t703, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn640:;
ma47:;
        uint8_t t705 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t705 != ((uint8_t)43ULL)) goto mn704;
        void * t706 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l87 = t706;
        {
            void * t709 = gm_l87;
            if (!(((((uint8_t *)t709) + 0))[32] != 0)) goto ie708;
            gm_t08olq97rscn3 t710 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)t709) + 0));
            gm_l88 = t710;
            {
                void * t712 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t713;
                GM_ST(void *, (uint8_t *)&t713, (void *)(gm_su137hja0ebcd));
                GM_ST(uintptr_t, (uint8_t *)&t713 + 8, (uintptr_t)(5));
                gm_tvul7d1mdc2eq t714;
                gm_f_ident_sx_kd0v352((void *)&t714, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l88)));
                gm_tvul7d1mdc2eq t715;
                gm_f_node1_gpsjtpc((void *)&t715, (void *)NULL, cx, NULL, t713, &t714);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t716 = gm_f_push_mjiecfq(t712, &t715);
                if (((((uint8_t *)&t716))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t716) + 1, gm_srck6bk8q21r07v, 461, 31);
                gm_b0_1 t717 = GM_LD(gm_b0_1, (((uint8_t *)&t716) + 1));
            }
            goto ix708;
ie708:;
ix708:;
        }
        {
            gm_f_list_ccsq5cl((void *)&F.l89, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t719 = gm_l87;
                gm_b16_8 t720 = GM_LD(gm_b16_8, (((uint8_t *)t719) + 40));
                gm_l90 = t720;
            }
            {
                gm_l91 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t722 = gm_l91;
                    uint64_t t723 = GM_LD(uintptr_t, ((uint8_t *)&gm_l90) + 8);
                    uint8_t t724 = (t722 < t723);
                    if (!t724) goto lb721;
                    {
                        uint64_t t727 = gm_l91;
                        uint64_t t728 = (uintptr_t)t727;
                        uint64_t t729 = GM_LD(uintptr_t, ((uint8_t *)&gm_l90) + 8);
                        gm_t0d02142k4tu8 t732 = GM_LD(gm_t0d02142k4tu8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l90)) + t728 * 64));
                        F.l92 = t732;
                        {
                            void * t734 = (void *)((uint8_t *)&F.l89);
                            void * t735 = (void *)((uint8_t *)&F.l92);
                            gm_tvul7d1mdc2eq t736;
                            gm_f_dump_pattern_q6u5c24((void *)&t736, (void *)NULL, cx, NULL, t735);
                            if (gm_unw.kind) {
                                goto gm_ll725;
                            }
                            gm_tftb1odc739t5 t737 = gm_f_push_mjiecfq(t734, &t736);
                            if (((((uint8_t *)&t737))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t737) + 1, gm_srck6bk8q21r07v, 463, 31);
                            gm_b0_1 t738 = GM_LD(gm_b0_1, (((uint8_t *)&t737) + 1));
                        }
                        goto me730;
mn731:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 463, 13);
me730:;
                    }
lc721:;
                    {
                        uint64_t t740 = gm_l91;
                        uint64_t r741; bool o742 = __builtin_add_overflow(t740, ((uint64_t)1ULL), &r741);
                        uint64_t t743 = r741;
                        gm_l91 = t743;
                    }
                }
gm_ll725:;
                if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb721; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc721; }
                }
                goto gm_lad;
lb721:;
            }
        }
        {
            void * t744 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t745;
            GM_ST(void *, (uint8_t *)&t745, (void *)(gm_so0ilvhoi1hom));
            GM_ST(uintptr_t, (uint8_t *)&t745 + 8, (uintptr_t)(4));
            gm_tvul7d1mdc2eq t746;
            gm_f_node_dm1r8dn((void *)&t746, (void *)NULL, cx, NULL, t745, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l89)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t747 = gm_f_push_mjiecfq(t744, &t746);
            if (((((uint8_t *)&t747))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t747) + 1, gm_srck6bk8q21r07v, 464, 13);
            gm_b0_1 t748 = GM_LD(gm_b0_1, (((uint8_t *)&t747) + 1));
        }
        {
            void * t749 = (void *)((uint8_t *)&F.l1);
            void * t750 = gm_l87;
            void * t751 = GM_LD(void *, (((uint8_t *)t750) + 56));
            gm_tvul7d1mdc2eq t752;
            gm_f_dump_expr_l3bbdps((void *)&t752, (void *)NULL, cx, NULL, t751);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t753 = gm_f_push_mjiecfq(t749, &t752);
            if (((((uint8_t *)&t753))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t753) + 1, gm_srck6bk8q21r07v, 465, 13);
            gm_b0_1 t754 = GM_LD(gm_b0_1, (((uint8_t *)&t753) + 1));
        }
        {
            void * t755 = (void *)((uint8_t *)&F.l1);
            void * t756 = gm_l87;
            void * t757 = (void *)(((uint8_t *)t756) + 64);
            gm_tvul7d1mdc2eq t758;
            gm_f_dump_block_5utjqg0((void *)&t758, (void *)NULL, cx, NULL, t757);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t759 = gm_f_push_mjiecfq(t755, &t758);
            if (((((uint8_t *)&t759))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t759) + 1, gm_srck6bk8q21r07v, 466, 13);
            gm_b0_1 t760 = GM_LD(gm_b0_1, (((uint8_t *)&t759) + 1));
        }
        {
            gm_b16_8 t761;
            GM_ST(void *, (uint8_t *)&t761, (void *)(gm_scc8p0a9eachg));
            GM_ST(uintptr_t, (uint8_t *)&t761 + 8, (uintptr_t)(3));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t761, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn704:;
ma48:;
        uint8_t t763 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t763 != ((uint8_t)44ULL)) goto mn762;
        gm_t79rbdv4rafe5 t764 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t2) + 8) + 8));
        F.l93 = t764;
        void * t765 = (void *)((uint8_t *)&F.l93);
        gm_f_dump_block_5utjqg0((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t765);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn762:;
ma49:;
        uint8_t t767 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t767 != ((uint8_t)45ULL)) goto mn766;
        if ((GM_LD(void *, ((((uint8_t *)t2) + 8) + 8)) != 0)) goto mn766;
        gm_b16_8 t768;
        GM_ST(void *, (uint8_t *)&t768, (void *)(gm_s277jq8ulrihr));
        GM_ST(uintptr_t, (uint8_t *)&t768 + 8, (uintptr_t)(6));
        gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t768, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn766:;
ma50:;
        uint8_t t770 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t770 != ((uint8_t)45ULL)) goto mn769;
        if (!(GM_LD(void *, ((((uint8_t *)t2) + 8) + 8)) != 0)) goto mn769;
        void * t771 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l94 = t771;
        gm_b16_8 t772;
        GM_ST(void *, (uint8_t *)&t772, (void *)(gm_s277jq8ulrihr));
        GM_ST(uintptr_t, (uint8_t *)&t772 + 8, (uintptr_t)(6));
        void * t773 = gm_l94;
        gm_tvul7d1mdc2eq t774;
        gm_f_dump_expr_l3bbdps((void *)&t774, (void *)NULL, cx, NULL, t773);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t772, &t774);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn769:;
ma51:;
        uint8_t t776 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t776 != ((uint8_t)46ULL)) goto mn775;
        gm_tchr52g0ema53 t777 = GM_LD(gm_tchr52g0ema53, ((((uint8_t *)t2) + 8) + 8));
        F.l95 = t777;
        {
            if (!(((((uint8_t *)&F.l95) + 0))[32] != 0)) goto ie779;
            gm_t08olq97rscn3 t780 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l95) + 0));
            gm_l96 = t780;
            {
                void * t782 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t783;
                GM_ST(void *, (uint8_t *)&t783, (void *)(gm_su137hja0ebcd));
                GM_ST(uintptr_t, (uint8_t *)&t783 + 8, (uintptr_t)(5));
                gm_tvul7d1mdc2eq t784;
                gm_f_ident_sx_kd0v352((void *)&t784, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l96)));
                gm_tvul7d1mdc2eq t785;
                gm_f_node1_gpsjtpc((void *)&t785, (void *)NULL, cx, NULL, t783, &t784);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t786 = gm_f_push_mjiecfq(t782, &t785);
                if (((((uint8_t *)&t786))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t786) + 1, gm_srck6bk8q21r07v, 473, 31);
                gm_b0_1 t787 = GM_LD(gm_b0_1, (((uint8_t *)&t786) + 1));
            }
            goto ix779;
ie779:;
ix779:;
        }
        {
            void * t789 = GM_LD(void *, (((uint8_t *)&F.l95) + 40));
            if (!(GM_LD(void *, ((uint8_t *)&t789)) != 0)) goto ie788;
            void * t790 = GM_LD(void *, ((uint8_t *)&t789));
            gm_l97 = t790;
            {
                void * t792 = (void *)((uint8_t *)&F.l1);
                void * t793 = gm_l97;
                gm_tvul7d1mdc2eq t794;
                gm_f_dump_expr_l3bbdps((void *)&t794, (void *)NULL, cx, NULL, t793);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t795 = gm_f_push_mjiecfq(t792, &t794);
                if (((((uint8_t *)&t795))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t795) + 1, gm_srck6bk8q21r07v, 474, 31);
                gm_b0_1 t796 = GM_LD(gm_b0_1, (((uint8_t *)&t795) + 1));
            }
            goto ix788;
ie788:;
ix788:;
        }
        {
            gm_b16_8 t797;
            GM_ST(void *, (uint8_t *)&t797, (void *)(gm_s8av3ru5o85v4));
            GM_ST(uintptr_t, (uint8_t *)&t797 + 8, (uintptr_t)(5));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t797, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn775:;
ma52:;
        uint8_t t799 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t799 != ((uint8_t)47ULL)) goto mn798;
        if (((((((uint8_t *)t2) + 8) + 8))[32] != 0)) goto mn798;
        gm_b16_8 t800;
        GM_ST(void *, (uint8_t *)&t800, (void *)(gm_sr31o7m0aohvd));
        GM_ST(uintptr_t, (uint8_t *)&t800 + 8, (uintptr_t)(8));
        gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t800, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn798:;
ma53:;
        uint8_t t802 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t802 != ((uint8_t)47ULL)) goto mn801;
        if (!((((((uint8_t *)t2) + 8) + 8))[32] != 0)) goto mn801;
        gm_t08olq97rscn3 t803 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l98 = t803;
        gm_b16_8 t804;
        GM_ST(void *, (uint8_t *)&t804, (void *)(gm_sr31o7m0aohvd));
        GM_ST(uintptr_t, (uint8_t *)&t804 + 8, (uintptr_t)(8));
        gm_b16_8 t805;
        GM_ST(void *, (uint8_t *)&t805, (void *)(gm_su137hja0ebcd));
        GM_ST(uintptr_t, (uint8_t *)&t805 + 8, (uintptr_t)(5));
        gm_tvul7d1mdc2eq t806;
        gm_f_ident_sx_kd0v352((void *)&t806, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l98)));
        gm_tvul7d1mdc2eq t807;
        gm_f_node1_gpsjtpc((void *)&t807, (void *)NULL, cx, NULL, t805, &t806);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t804, &t807);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn801:;
ma54:;
        uint8_t t809 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t809 != ((uint8_t)48ULL)) goto mn808;
        gm_t3mf58gv9kf3n t810 = GM_LD(gm_t3mf58gv9kf3n, ((((uint8_t *)t2) + 8) + 8));
        gm_l99 = t810;
        {
            void * t812 = (void *)((uint8_t *)&F.l1);
            void * t813 = GM_LD(void *, (((uint8_t *)&gm_l99) + 0));
            gm_tvul7d1mdc2eq t814;
            gm_f_dump_expr_l3bbdps((void *)&t814, (void *)NULL, cx, NULL, t813);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t815 = gm_f_push_mjiecfq(t812, &t814);
            if (((((uint8_t *)&t815))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t815) + 1, gm_srck6bk8q21r07v, 480, 13);
            gm_b0_1 t816 = GM_LD(gm_b0_1, (((uint8_t *)&t815) + 1));
        }
        {
            void * t818 = GM_LD(void *, (((uint8_t *)&gm_l99) + 8));
            if (!(GM_LD(void *, ((uint8_t *)&t818)) != 0)) goto ie817;
            void * t819 = GM_LD(void *, ((uint8_t *)&t818));
            gm_l100 = t819;
            {
                void * t821 = (void *)((uint8_t *)&F.l1);
                void * t822 = gm_l100;
                gm_tvul7d1mdc2eq t823;
                gm_f_dump_expr_l3bbdps((void *)&t823, (void *)NULL, cx, NULL, t822);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t824 = gm_f_push_mjiecfq(t821, &t823);
                if (((((uint8_t *)&t824))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t824) + 1, gm_srck6bk8q21r07v, 481, 31);
                gm_b0_1 t825 = GM_LD(gm_b0_1, (((uint8_t *)&t824) + 1));
            }
            goto ix817;
ie817:;
ix817:;
        }
        {
            gm_b16_8 t826;
            GM_ST(void *, (uint8_t *)&t826, (void *)(gm_sn6sqlran7h3r));
            GM_ST(uintptr_t, (uint8_t *)&t826 + 8, (uintptr_t)(6));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t826, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn808:;
ma55:;
        uint8_t t828 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t828 != ((uint8_t)49ULL)) goto mn827;
        gm_tet9guhev5r4u t829 = GM_LD(gm_tet9guhev5r4u, ((((uint8_t *)t2) + 8) + 8));
        F.l101 = t829;
        {
            void * t831 = (void *)((uint8_t *)&F.l1);
            gm_tvul7d1mdc2eq t832;
            gm_f_ident_sx_kd0v352((void *)&t832, &(*(gm_t08olq97rscn3 *)(((uint8_t *)&F.l101) + 0)));
            gm_tftb1odc739t5 t833 = gm_f_push_mjiecfq(t831, &t832);
            if (((((uint8_t *)&t833))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t833) + 1, gm_srck6bk8q21r07v, 485, 13);
            gm_b0_1 t834 = GM_LD(gm_b0_1, (((uint8_t *)&t833) + 1));
        }
        {
            if (!(((((uint8_t *)&F.l101) + 32))[16] != 0)) goto ie835;
            gm_b16_8 t836 = GM_LD(gm_b16_8, (((uint8_t *)&F.l101) + 32));
            gm_l102 = t836;
            {
                gm_f_list_ccsq5cl((void *)&F.l103, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t839 = gm_l102;
                    gm_l104 = t839;
                }
                {
                    gm_l105 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t841 = gm_l105;
                        uint64_t t842 = GM_LD(uintptr_t, ((uint8_t *)&gm_l104) + 8);
                        uint8_t t843 = (t841 < t842);
                        if (!t843) goto lb840;
                        {
                            uint64_t t846 = gm_l105;
                            uint64_t t847 = (uintptr_t)t846;
                            uint64_t t848 = GM_LD(uintptr_t, ((uint8_t *)&gm_l104) + 8);
                            gm_t7te2tdpebbt4 t851 = GM_LD(gm_t7te2tdpebbt4, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l104)) + t847 * 144));
                            F.l106 = t851;
                            {
                                void * t853 = (void *)((uint8_t *)&F.l103);
                                void * t854 = (void *)((uint8_t *)&F.l106);
                                gm_tvul7d1mdc2eq t855;
                                gm_f_dump_arg_9iv8pdv((void *)&t855, (void *)NULL, cx, NULL, t854);
                                if (gm_unw.kind) {
                                    goto gm_ll844;
                                }
                                gm_tftb1odc739t5 t856 = gm_f_push_mjiecfq(t853, &t855);
                                if (((((uint8_t *)&t856))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t856) + 1, gm_srck6bk8q21r07v, 488, 31);
                                gm_b0_1 t857 = GM_LD(gm_b0_1, (((uint8_t *)&t856) + 1));
                            }
                            goto me849;
mn850:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 488, 17);
me849:;
                        }
lc840:;
                        {
                            uint64_t t859 = gm_l105;
                            uint64_t r860; bool o861 = __builtin_add_overflow(t859, ((uint64_t)1ULL), &r860);
                            uint64_t t862 = r860;
                            gm_l105 = t862;
                        }
                    }
gm_ll844:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb840; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc840; }
                    }
                    goto gm_lad;
lb840:;
                }
            }
            {
                void * t863 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t864;
                GM_ST(void *, (uint8_t *)&t864, (void *)(gm_s20t5qopq8n9u));
                GM_ST(uintptr_t, (uint8_t *)&t864 + 8, (uintptr_t)(4));
                gm_tvul7d1mdc2eq t865;
                gm_f_node_dm1r8dn((void *)&t865, (void *)NULL, cx, NULL, t864, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l103)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t866 = gm_f_push_mjiecfq(t863, &t865);
                if (((((uint8_t *)&t866))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t866) + 1, gm_srck6bk8q21r07v, 489, 17);
                gm_b0_1 t867 = GM_LD(gm_b0_1, (((uint8_t *)&t866) + 1));
            }
            goto ix835;
ie835:;
ix835:;
        }
        {
            gm_b16_8 t868;
            GM_ST(void *, (uint8_t *)&t868, (void *)(gm_s3t7gv740qicd));
            GM_ST(uintptr_t, (uint8_t *)&t868 + 8, (uintptr_t)(9));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t868, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn827:;
ma56:;
        uint8_t t870 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t870 != ((uint8_t)50ULL)) goto mn869;
        gm_b16_8 t871;
        GM_ST(void *, (uint8_t *)&t871, (void *)(gm_s79s3h98matgr));
        GM_ST(uintptr_t, (uint8_t *)&t871 + 8, (uintptr_t)(5));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 24, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 8, t871);
        goto gm_fdone;
        goto me3;
mn869:;
ma57:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 347, 5);
me3:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_dump_file_cno3fos(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_dump_file_cno3fos F;
    void * gm_l0; /* f */
    void * gm_l1; /* out */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 405u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_ccsq5cl((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l3 = t4;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l4;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l4;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tvpnktv5fo8nl t16 = GM_LD(gm_tvpnktv5fo8nl, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t12 * 160));
                    F.l5 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l2);
                        void * t19 = (void *)((uint8_t *)&F.l5);
                        gm_tvul7d1mdc2eq t20;
                        gm_f_dump_item_bs3lq5m((void *)&t20, (void *)NULL, cx, NULL, t19);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tftb1odc739t5 t21 = gm_f_push_mjiecfq(t18, &t20);
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srck6bk8q21r07v, 19, 27);
                        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 19, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t24 = gm_l4;
                    uint64_t r25; bool o26 = __builtin_add_overflow(t24, ((uint64_t)1ULL), &r25);
                    uint64_t t27 = r25;
                    gm_l4 = t27;
                }
            }
gm_ll9:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb5; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc5; }
            }
            goto gm_lad;
lb5:;
        }
    }
    {
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_sl5jkfh6104lp));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(4));
        gm_tvul7d1mdc2eq t29;
        gm_f_bnode_hu9fd4q((void *)&t29, (void *)NULL, cx, NULL, t28, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l2)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t30 = (void *)((uint8_t *)&t29);
        void * t31 = gm_l1;
        gm_b0_1 t32 = gm_f_render_7k3fqtk((void *)NULL, cx, NULL, t30, ((int64_t)0LL), t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t33 = gm_l1;
        gm_tftb1odc739t5 t34 = gm_f_push_t83rg2m(t33, ((uint8_t)10ULL));
        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srck6bk8q21r07v, 21, 5);
        gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_dump_fn_lit_tchlprc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_fn_lit_tchlprc F;
    void * gm_l0; /* f */
    gm_tvul7d1mdc2eq gm_l2; /* w */
    void * gm_l4; /* e */
    GM_STACK_CHECK(&F, 398u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 72));
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_t4eh5qkgoio6s t6;
        gm_f_dump_constraints_68aa75n((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t6))[32] != 0)) goto ie2;
        gm_l2 = (*(gm_tvul7d1mdc2eq *)(((uint8_t *)&t6) + 0));
        {
            void * t8 = (void *)((uint8_t *)&F.l1);
            gm_tftb1odc739t5 t9 = gm_f_push_mjiecfq(t8, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l2)));
            if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srck6bk8q21r07v, 581, 42);
            gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = (void *)((uint8_t *)&F.l1);
        void * t12 = gm_l0;
        gm_tvul7d1mdc2eq t13;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t12) + 88) + 0));
        if (t16 != ((uint8_t)0ULL)) goto mn15;
        gm_t79rbdv4rafe5 t17 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t12) + 88) + 8));
        F.l3 = t17;
        void * t18 = (void *)((uint8_t *)&F.l3);
        gm_tvul7d1mdc2eq t19;
        gm_f_dump_block_5utjqg0((void *)&t19, (void *)NULL, cx, NULL, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t13 = t19;
        goto me14;
mn15:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t12) + 88) + 0));
        if (t21 != ((uint8_t)1ULL)) goto mn20;
        void * t22 = GM_LD(void *, ((((uint8_t *)t12) + 88) + 8));
        gm_l4 = t22;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_sm8obe455p2d9));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(2));
        void * t24 = gm_l4;
        gm_tvul7d1mdc2eq t25;
        gm_f_dump_expr_l3bbdps((void *)&t25, (void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvul7d1mdc2eq t26;
        gm_f_node1_gpsjtpc((void *)&t26, (void *)NULL, cx, NULL, t23, &t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t13 = t26;
        goto me14;
mn20:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 582, 15);
me14:;
        gm_tftb1odc739t5 t27 = gm_f_push_mjiecfq(t11, &t13);
        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srck6bk8q21r07v, 582, 5);
        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
    }
    {
        gm_b16_8 t29;
        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s579fb7l5l6df));
        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(2));
        void * t30 = gm_l0;
        void * t31 = (void *)(((uint8_t *)t30) + 0);
        gm_f_dump_fn_sig_4ip3qer((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t29, t31, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_dump_fn_sig_4ip3qer(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1, const void * a2) {
    struct gm_fr_dump_fn_sig_4ip3qer F;
    gm_b16_8 gm_l0; /* head */
    void * gm_l1; /* sig */
    void * gm_l4; /* r */
    gm_b16_8 gm_l5; /* e */
    GM_STACK_CHECK(&F, 475u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l2, a2, sizeof F.l2);
    {
        gm_f_list_ccsq5cl((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l3);
        void * t3 = gm_l1;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_tvul7d1mdc2eq t6;
        gm_f_dump_params_vfgaabj((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t7 = gm_f_push_mjiecfq(t2, &t6);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srck6bk8q21r07v, 564, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        void * t10 = gm_l1;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t10) + 16));
        if (!t11) goto ie9;
        {
            void * t13 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t14;
            GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sqpev88g6capg));
            GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(8));
            gm_tvul7d1mdc2eq t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t15 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t15 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t15 + 8), t14);
            gm_tftb1odc739t5 t16 = gm_f_push_mjiecfq(t13, &t15);
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_srck6bk8q21r07v, 565, 23);
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t19 = gm_l1;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 24));
        if (!(GM_LD(void *, ((uint8_t *)&t20)) != 0)) goto ie18;
        void * t21 = GM_LD(void *, ((uint8_t *)&t20));
        gm_l4 = t21;
        {
            void * t23 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_s1k3lfare8sei));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(3));
            void * t25 = gm_l4;
            gm_tvul7d1mdc2eq t26;
            gm_f_dump_expr_l3bbdps((void *)&t26, (void *)NULL, cx, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t27;
            gm_f_node1_gpsjtpc((void *)&t27, (void *)NULL, cx, NULL, t24, &t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t28 = gm_f_push_mjiecfq(t23, &t27);
            if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srck6bk8q21r07v, 566, 23);
            gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t31 = gm_l1;
        if (!(((((uint8_t *)t31) + 32))[16] != 0)) goto ie30;
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)t31) + 32));
        gm_l5 = t32;
        {
            void * t34 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t35 = gm_l5;
            gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
            gm_tvul7d1mdc2eq t37;
            gm_f_dump_effects_4fqcm0g((void *)&t37, (void *)NULL, cx, NULL, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t38 = gm_f_push_mjiecfq(t34, &t37);
            if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srck6bk8q21r07v, 567, 27);
            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t40 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)&t41));
        gm_tftb1odc739t5 t43 = gm_f_extend_i72a9oi(t40, t42);
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srck6bk8q21r07v, 568, 5);
        gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
    }
    {
        gm_b16_8 t45 = gm_l0;
        gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t45, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3)));
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
inline __attribute__((aligned(4))) void gm_f_dump_foreign_member_vrht12f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_foreign_member_vrht12f F;
    void * gm_l0; /* m */
    gm_t08olq97rscn3 gm_l2; /* n */
    gm_t08olq97rscn3 gm_l4; /* n */
    void * gm_l5; /* ty */
    GM_STACK_CHECK(&F, 554u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        void * t3 = gm_l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_b0_1 t6 = gm_f_push_attrs_a5l439c((void *)NULL, cx, NULL, t2, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t7 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t7) + 16) + 0));
        if (t10 != ((uint8_t)0ULL)) goto mn9;
        gm_t08olq97rscn3 t11 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t7) + 16) + 8) + 0));
        gm_l2 = t11;
        gm_tlivgedjs5ps3 t12 = GM_LD(gm_tlivgedjs5ps3, (((((uint8_t *)t7) + 16) + 8) + 32));
        F.l3 = t12;
        {
            void * t14 = (void *)((uint8_t *)&F.l1);
            gm_tvul7d1mdc2eq t15;
            gm_f_ident_sx_kd0v352((void *)&t15, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)));
            gm_tftb1odc739t5 t16 = gm_f_push_mjiecfq(t14, &t15);
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_srck6bk8q21r07v, 683, 13);
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
        }
        {
            void * t18 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s252dj7ek002d));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(7));
            void * t20 = (void *)((uint8_t *)&F.l3);
            gm_trqfvmjtt4sij t21;
            gm_f_list_ccsq5cl((void *)&t21, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t22;
            gm_f_dump_fn_sig_4ip3qer((void *)&t22, (void *)NULL, cx, NULL, t19, t20, &t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t23 = gm_f_push_mjiecfq(t18, &t22);
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srck6bk8q21r07v, 684, 13);
            gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
        }
        {
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_se31bb9e7icn2));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(10));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t25, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me8;
mn9:;
        uint8_t t27 = GM_LD(uint8_t, ((((uint8_t *)t7) + 16) + 0));
        if (t27 != ((uint8_t)1ULL)) goto mn26;
        gm_t08olq97rscn3 t28 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t7) + 16) + 8) + 0));
        gm_l4 = t28;
        void * t29 = GM_LD(void *, (((((uint8_t *)t7) + 16) + 8) + 32));
        gm_l5 = t29;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_tvul7d1mdc2eq t32;
            gm_f_ident_sx_kd0v352((void *)&t32, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l4)));
            gm_tftb1odc739t5 t33 = gm_f_push_mjiecfq(t31, &t32);
            if (((((uint8_t *)&t33))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t33) + 1, gm_srck6bk8q21r07v, 688, 13);
            gm_b0_1 t34 = GM_LD(gm_b0_1, (((uint8_t *)&t33) + 1));
        }
        {
            void * t35 = (void *)((uint8_t *)&F.l1);
            void * t36 = gm_l5;
            gm_tvul7d1mdc2eq t37;
            gm_f_dump_expr_l3bbdps((void *)&t37, (void *)NULL, cx, NULL, t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t38 = gm_f_push_mjiecfq(t35, &t37);
            if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srck6bk8q21r07v, 689, 13);
            gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
        }
        {
            gm_b16_8 t40;
            GM_ST(void *, (uint8_t *)&t40, (void *)(gm_saufuk3rs2i2m));
            GM_ST(uintptr_t, (uint8_t *)&t40 + 8, (uintptr_t)(14));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t40, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me8;
mn26:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 681, 5);
me8:;
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
inline __attribute__((aligned(4))) void gm_f_dump_handler_decl_htmrmsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_handler_decl_htmrmsf F;
    void * gm_l0; /* h */
    gm_b16_8 gm_l2; /* p */
    gm_b16_8 gm_l3; /* e */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 643u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        if (!(((((uint8_t *)t3) + 0))[16] != 0)) goto ie2;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
        gm_l2 = t4;
        {
            void * t6 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t7 = gm_l2;
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
            gm_tvul7d1mdc2eq t9;
            gm_f_dump_params_vfgaabj((void *)&t9, (void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t10 = gm_f_push_mjiecfq(t6, &t9);
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srck6bk8q21r07v, 233, 24);
            gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = (void *)((uint8_t *)&F.l1);
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 24));
        gm_tvul7d1mdc2eq t15;
        gm_f_dump_expr_l3bbdps((void *)&t15, (void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t16 = gm_f_push_mjiecfq(t12, &t15);
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_srck6bk8q21r07v, 234, 5);
        gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
    }
    {
        void * t18 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_scc8p0a9eachg));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(3));
        void * t20 = gm_l0;
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 32));
        gm_tvul7d1mdc2eq t22;
        gm_f_dump_expr_l3bbdps((void *)&t22, (void *)NULL, cx, NULL, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvul7d1mdc2eq t23;
        gm_f_node1_gpsjtpc((void *)&t23, (void *)NULL, cx, NULL, t19, &t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t24 = gm_f_push_mjiecfq(t18, &t23);
        if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srck6bk8q21r07v, 235, 5);
        gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
    }
    {
        void * t27 = gm_l0;
        if (!(((((uint8_t *)t27) + 40))[16] != 0)) goto ie26;
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)t27) + 40));
        gm_l3 = t28;
        {
            void * t30 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t31 = gm_l3;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            gm_tvul7d1mdc2eq t33;
            gm_f_dump_effects_4fqcm0g((void *)&t33, (void *)NULL, cx, NULL, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t34 = gm_f_push_mjiecfq(t30, &t33);
            if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srck6bk8q21r07v, 236, 25);
            gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        {
            void * t37 = gm_l0;
            gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)t37) + 64));
            gm_l4 = t38;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t40 = gm_l5;
                uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t42 = (t40 < t41);
                if (!t42) goto lb39;
                {
                    uint64_t t45 = gm_l5;
                    uint64_t t46 = (uintptr_t)t45;
                    uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t99vmhfllumbh t50 = GM_LD(gm_t99vmhfllumbh, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t46 * 72));
                    F.l6 = t50;
                    {
                        void * t52 = (void *)((uint8_t *)&F.l1);
                        void * t53 = (void *)((uint8_t *)&F.l6);
                        gm_tvul7d1mdc2eq t54;
                        gm_f_dump_clause_kje51q9((void *)&t54, (void *)NULL, cx, NULL, t53);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        gm_tftb1odc739t5 t55 = gm_f_push_mjiecfq(t52, &t54);
                        if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_srck6bk8q21r07v, 237, 26);
                        gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
                    }
                    goto me48;
mn49:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 237, 5);
me48:;
                }
lc39:;
                {
                    uint64_t t58 = gm_l5;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l5 = t61;
                }
            }
gm_ll43:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb39; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc39; }
            }
            goto gm_lad;
lb39:;
        }
    }
    {
        gm_b16_8 t62;
        GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sf3ii7gekd9ll));
        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(12));
        gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t62, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_dump_if_3qo5nul(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_if_3qo5nul F;
    void * gm_l0; /* i */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t08olq97rscn3 gm_l7; /* n */
    void * gm_l8; /* body */
    GM_STACK_CHECK(&F, 717u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tcuumjkg5eni6 t16 = GM_LD(gm_tcuumjkg5eni6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 72));
                    F.l4 = t16;
                    {
                        gm_f_list_ccsq5cl((void *)&F.l5, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                    }
                    {
                        void * t18 = (void *)((uint8_t *)&F.l5);
                        void * t19 = (void *)(((uint8_t *)&F.l4) + 0);
                        gm_tvul7d1mdc2eq t20;
                        gm_f_dump_cond_cu0s815((void *)&t20, (void *)NULL, cx, NULL, t19);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tftb1odc739t5 t21 = gm_f_push_mjiecfq(t18, &t20);
                        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srck6bk8q21r07v, 520, 9);
                        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
                    }
                    {
                        void * t23 = (void *)((uint8_t *)&F.l5);
                        void * t24 = (void *)(((uint8_t *)&F.l4) + 32);
                        gm_tvul7d1mdc2eq t25;
                        gm_f_dump_block_5utjqg0((void *)&t25, (void *)NULL, cx, NULL, t24);
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tftb1odc739t5 t26 = gm_f_push_mjiecfq(t23, &t25);
                        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srck6bk8q21r07v, 521, 9);
                        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
                    }
                    {
                        void * t28 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t29;
                        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_s0jho24omhfui));
                        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(6));
                        gm_tvul7d1mdc2eq t30;
                        gm_f_bnode_hu9fd4q((void *)&t30, (void *)NULL, cx, NULL, t29, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l5)));
                        if (gm_unw.kind) {
                            goto gm_ll9;
                        }
                        gm_tftb1odc739t5 t31 = gm_f_push_mjiecfq(t28, &t30);
                        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srck6bk8q21r07v, 522, 9);
                        gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 518, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t34 = gm_l3;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l3 = t37;
                }
            }
gm_ll9:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb5; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc5; }
            }
            goto gm_lad;
lb5:;
        }
    }
    {
        void * t38 = gm_l0;
        if ((((((uint8_t *)t38) + 16))[48] != 0)) goto mn40;
        goto me39;
mn40:;
        if (!(((((uint8_t *)t38) + 16))[48] != 0)) goto mn42;
        uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t38) + 16) + 0));
        if (t43 != ((uint8_t)0ULL)) goto mn42;
        gm_t79rbdv4rafe5 t44 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t38) + 16) + 8));
        F.l6 = t44;
        void * t45 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t46;
        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_s9es2tos1bhfp));
        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(4));
        void * t47 = (void *)((uint8_t *)&F.l6);
        gm_tvul7d1mdc2eq t48;
        gm_f_dump_block_5utjqg0((void *)&t48, (void *)NULL, cx, NULL, t47);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvul7d1mdc2eq t49;
        gm_f_node1_gpsjtpc((void *)&t49, (void *)NULL, cx, NULL, t46, &t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t50 = gm_f_push_mjiecfq(t45, &t49);
        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_srck6bk8q21r07v, 526, 22);
        gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
        goto me39;
mn42:;
        if (!(((((uint8_t *)t38) + 16))[48] != 0)) goto mn52;
        uint8_t t53 = GM_LD(uint8_t, ((((uint8_t *)t38) + 16) + 0));
        if (t53 != ((uint8_t)1ULL)) goto mn52;
        gm_t08olq97rscn3 t54 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t38) + 16) + 8) + 0));
        gm_l7 = t54;
        void * t55 = GM_LD(void *, (((((uint8_t *)t38) + 16) + 8) + 32));
        gm_l8 = t55;
        void * t56 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sjjc564tiep5s));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(12));
        gm_tvul7d1mdc2eq t58;
        gm_f_ident_sx_kd0v352((void *)&t58, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)));
        void * t59 = gm_l8;
        gm_tvul7d1mdc2eq t60;
        gm_f_dump_expr_l3bbdps((void *)&t60, (void *)NULL, cx, NULL, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvul7d1mdc2eq t61;
        gm_f_node2_b4qpqat((void *)&t61, (void *)NULL, cx, NULL, t57, &t58, &t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t62 = gm_f_push_mjiecfq(t56, &t61);
        if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srck6bk8q21r07v, 527, 30);
        gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
        goto me39;
mn52:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 524, 5);
me39:;
    }
    {
        gm_b16_8 t64;
        GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sl9nvct67or45));
        GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(2));
        gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t64, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
__attribute__((aligned(4))) void gm_f_dump_item_bs3lq5m(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_item_bs3lq5m F;
    void * gm_l0; /* item */
    gm_b16_8 gm_l2; /* vis */
    gm_b16_8 gm_l3; /* doc */
    void * gm_l6; /* h */
    void * gm_l8; /* e */
    GM_STACK_CHECK(&F, 1038u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        gm_t9qkh0rsooc8d t4 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)t3) + 56));
        gm_th2lqbo77pvsc t5;
        gm_f_visibility_u2ipb12((void *)&t5, t4);
        if (!((((uint8_t *)&t5))[16] != 0)) goto ie2;
        gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
        gm_l2 = t6;
        {
            void * t8 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t9 = gm_l2;
            gm_tvul7d1mdc2eq t10;
            GM_ST(uint8_t, (uint8_t *)&t10 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t10 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t10 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t10 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t10 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 8), t9);
            gm_tftb1odc739t5 t11 = gm_f_push_mjiecfq(t8, &t10);
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srck6bk8q21r07v, 204, 38);
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t14 = gm_l0;
        if (!(((((uint8_t *)t14) + 16))[16] != 0)) goto ie13;
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 16));
        gm_l3 = t15;
        {
            void * t17 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t18;
            GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s8rsdirj4dmun));
            GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(3));
            gm_b16_8 t19 = gm_l3;
            gm_tvul7d1mdc2eq t20;
            gm_f_atom_of_bytes_dj32elq((void *)&t20, (void *)NULL, cx, NULL, t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t21;
            gm_f_node1_gpsjtpc((void *)&t21, (void *)NULL, cx, NULL, t18, &t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t22 = gm_f_push_mjiecfq(t17, &t21);
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srck6bk8q21r07v, 205, 26);
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t24 = (void *)((uint8_t *)&F.l1);
        void * t25 = gm_l0;
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t25) + 40));
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        gm_b0_1 t28 = gm_f_push_attrs_a5l439c((void *)NULL, cx, NULL, t24, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t29 = (void *)((uint8_t *)&F.l1);
        void * t30 = gm_l0;
        gm_tvul7d1mdc2eq t31;
        uint8_t t39 = GM_LD(uint8_t, ((((uint8_t *)t30) + 64) + 0));
        switch (t39) { case ((uint8_t)0ULL): goto ma33; case ((uint8_t)1ULL): goto ma34; case ((uint8_t)2ULL): goto ma35; case ((uint8_t)3ULL): goto ma36; case ((uint8_t)4ULL): goto ma37; default: goto ma38; }
ma33:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t30) + 64) + 0));
        if (t41 != ((uint8_t)0ULL)) goto mn40;
        gm_tv2uf4leil5hq t42 = GM_LD(gm_tv2uf4leil5hq, ((((uint8_t *)t30) + 64) + 8));
        F.l4 = t42;
        void * t43 = (void *)((uint8_t *)&F.l4);
        gm_tvul7d1mdc2eq t44;
        gm_f_dump_binding_b27qkdk((void *)&t44, (void *)NULL, cx, NULL, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t31 = t44;
        goto me32;
mn40:;
ma34:;
        uint8_t t46 = GM_LD(uint8_t, ((((uint8_t *)t30) + 64) + 0));
        if (t46 != ((uint8_t)1ULL)) goto mn45;
        gm_t7bjbmukv26e7 t47 = GM_LD(gm_t7bjbmukv26e7, ((((uint8_t *)t30) + 64) + 8));
        F.l5 = t47;
        void * t48 = (void *)((uint8_t *)&F.l5);
        gm_tvul7d1mdc2eq t49;
        gm_f_dump_when_items_16tgm3i((void *)&t49, (void *)NULL, cx, NULL, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t31 = t49;
        goto me32;
mn45:;
ma35:;
        uint8_t t51 = GM_LD(uint8_t, ((((uint8_t *)t30) + 64) + 0));
        if (t51 != ((uint8_t)2ULL)) goto mn50;
        void * t52 = GM_LD(void *, ((((uint8_t *)t30) + 64) + 8));
        gm_l6 = t52;
        void * t53 = gm_l6;
        gm_tvul7d1mdc2eq t54;
        gm_f_dump_handler_decl_htmrmsf((void *)&t54, (void *)NULL, cx, NULL, t53);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t31 = t54;
        goto me32;
mn50:;
ma36:;
        uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t30) + 64) + 0));
        if (t56 != ((uint8_t)3ULL)) goto mn55;
        gm_tcfiotghd1s5k t57 = GM_LD(gm_tcfiotghd1s5k, ((((uint8_t *)t30) + 64) + 8));
        F.l7 = t57;
        gm_b16_8 t58;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&F.l7) + 80));
        if (!t60) goto ie59;
        gm_b16_8 t62;
        GM_ST(void *, (uint8_t *)&t62, (void *)(gm_srfagufvviad7));
        GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(5));
        gm_b16_8 t63 = t62;
        t58 = t63;
        goto ix59;
ie59:;
        gm_b16_8 t65;
        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s9r64t1166q1f));
        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(4));
        gm_b16_8 t66 = t65;
        t58 = t66;
ix59:;
        gm_tvul7d1mdc2eq t67;
        gm_f_str_lit_a2k13t3((void *)&t67, (void *)NULL, cx, NULL, &(*(gm_tibupt4dmcqai *)(((uint8_t *)&F.l7) + 0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t68 = (void *)(((uint8_t *)&F.l7) + 40);
        gm_tvul7d1mdc2eq t69;
        gm_f_dump_block_5utjqg0((void *)&t69, (void *)NULL, cx, NULL, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvul7d1mdc2eq t70;
        gm_f_node2_b4qpqat((void *)&t70, (void *)NULL, cx, NULL, t58, &t67, &t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t31 = t70;
        goto me32;
mn55:;
ma37:;
        uint8_t t72 = GM_LD(uint8_t, ((((uint8_t *)t30) + 64) + 0));
        if (t72 != ((uint8_t)4ULL)) goto mn71;
        void * t73 = GM_LD(void *, ((((uint8_t *)t30) + 64) + 8));
        gm_l8 = t73;
        gm_b16_8 t74;
        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_snas2pfnujj1i));
        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(14));
        void * t75 = gm_l8;
        gm_tvul7d1mdc2eq t76;
        gm_f_dump_expr_l3bbdps((void *)&t76, (void *)NULL, cx, NULL, t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvul7d1mdc2eq t77;
        gm_f_node1_gpsjtpc((void *)&t77, (void *)NULL, cx, NULL, t74, &t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        t31 = t77;
        goto me32;
mn71:;
ma38:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 207, 12);
me32:;
        gm_tftb1odc739t5 t78 = gm_f_push_mjiecfq(t29, &t31);
        if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_srck6bk8q21r07v, 207, 5);
        gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
    }
    {
        gm_b16_8 t80;
        GM_ST(void *, (uint8_t *)&t80, (void *)(gm_srltb43nuabaq));
        GM_ST(uintptr_t, (uint8_t *)&t80 + 8, (uintptr_t)(4));
        gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t80, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_dump_items_atcioo9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_dump_items_atcioo9 F;
    gm_b16_8 gm_l0; /* label */
    gm_b16_8 gm_l1; /* items */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    GM_STACK_CHECK(&F, 355u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_ccsq5cl((void *)&F.l2, (void *)NULL, cx, NULL);
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
                    gm_tvpnktv5fo8nl t15 = GM_LD(gm_tvpnktv5fo8nl, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t11 * 160));
                    F.l5 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l2);
                        void * t18 = (void *)((uint8_t *)&F.l5);
                        gm_tvul7d1mdc2eq t19;
                        gm_f_dump_item_bs3lq5m((void *)&t19, (void *)NULL, cx, NULL, t18);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tftb1odc739t5 t20 = gm_f_push_mjiecfq(t17, &t19);
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srck6bk8q21r07v, 219, 25);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 219, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t23 = gm_l4;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l4 = t26;
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
        gm_b16_8 t27 = gm_l0;
        gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t27, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l2)));
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
__attribute__((aligned(4))) void gm_f_dump_param_kr39os0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_param_kr39os0 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_t08olq97rscn3 gm_l4; /* n */
    void * gm_l6; /* t */
    void * gm_l7; /* d */
    GM_STACK_CHECK(&F, 687u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t3 = gm_l0;
            gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
            gm_l2 = t4;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l3;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    uint64_t t11 = gm_l3;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_t08olq97rscn3 t16 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t12 * 32));
                    gm_l4 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l1);
                        gm_tvul7d1mdc2eq t19;
                        gm_f_ident_sx_kd0v352((void *)&t19, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l4)));
                        gm_tftb1odc739t5 t20 = gm_f_push_mjiecfq(t18, &t19);
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srck6bk8q21r07v, 546, 24);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 546, 5);
me14:;
                }
lc5:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
                }
            }
lb5:;
        }
    }
    {
        gm_f_list_ccsq5cl((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t27 = (void *)((uint8_t *)&F.l5);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(gm_sm56lhobvb6g5));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(5));
        gm_tvul7d1mdc2eq t29;
        gm_f_node_dm1r8dn((void *)&t29, (void *)NULL, cx, NULL, t28, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t30 = gm_f_push_mjiecfq(t27, &t29);
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srck6bk8q21r07v, 548, 5);
        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
    }
    {
        void * t33 = gm_l0;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)t33) + 16));
        if (!t34) goto ie32;
        {
            void * t36 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(gm_ssqg2h6hrn099));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(8));
            gm_tvul7d1mdc2eq t38;
            GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t38 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t38 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t38 + 8), t37);
            gm_tftb1odc739t5 t39 = gm_f_push_mjiecfq(t36, &t38);
            if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srck6bk8q21r07v, 549, 21);
            gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        void * t42 = gm_l0;
        uint8_t t43 = GM_LD(uint8_t, (((uint8_t *)t42) + 17));
        if (!t43) goto ie41;
        {
            void * t45 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sm8si2u0a1u17));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(4));
            gm_tvul7d1mdc2eq t47;
            GM_ST(uint8_t, (uint8_t *)&t47 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t47 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t47 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t47 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t47 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t47 + 8), t46);
            gm_tftb1odc739t5 t48 = gm_f_push_mjiecfq(t45, &t47);
            if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srck6bk8q21r07v, 550, 17);
            gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        void * t51 = gm_l0;
        void * t52 = GM_LD(void *, (((uint8_t *)t51) + 24));
        if (!(GM_LD(void *, ((uint8_t *)&t52)) != 0)) goto ie50;
        void * t53 = GM_LD(void *, ((uint8_t *)&t52));
        gm_l6 = t53;
        {
            void * t55 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t56;
            GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sqbhgmmp8kn4m));
            GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(2));
            void * t57 = gm_l6;
            gm_tvul7d1mdc2eq t58;
            gm_f_dump_expr_l3bbdps((void *)&t58, (void *)NULL, cx, NULL, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t59;
            gm_f_node1_gpsjtpc((void *)&t59, (void *)NULL, cx, NULL, t56, &t58);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t60 = gm_f_push_mjiecfq(t55, &t59);
            if (((((uint8_t *)&t60))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t60) + 1, gm_srck6bk8q21r07v, 551, 20);
            gm_b0_1 t61 = GM_LD(gm_b0_1, (((uint8_t *)&t60) + 1));
        }
        goto ix50;
ie50:;
ix50:;
    }
    {
        void * t63 = gm_l0;
        void * t64 = GM_LD(void *, (((uint8_t *)t63) + 32));
        if (!(GM_LD(void *, ((uint8_t *)&t64)) != 0)) goto ie62;
        void * t65 = GM_LD(void *, ((uint8_t *)&t64));
        gm_l7 = t65;
        {
            void * t67 = (void *)((uint8_t *)&F.l5);
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_ses9qtp6kdd22));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(7));
            void * t69 = gm_l7;
            gm_tvul7d1mdc2eq t70;
            gm_f_dump_expr_l3bbdps((void *)&t70, (void *)NULL, cx, NULL, t69);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t71;
            gm_f_node1_gpsjtpc((void *)&t71, (void *)NULL, cx, NULL, t68, &t70);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t72 = gm_f_push_mjiecfq(t67, &t71);
            if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_srck6bk8q21r07v, 552, 25);
            gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
        }
        goto ix62;
ie62:;
ix62:;
    }
    {
        gm_b16_8 t74;
        GM_ST(void *, (uint8_t *)&t74, (void *)(gm_skissj10nclhr));
        GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(5));
        gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t74, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l5)));
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
inline __attribute__((aligned(4))) void gm_f_dump_params_vfgaabj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_dump_params_vfgaabj F;
    gm_b16_8 gm_l0; /* params */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 235u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l2 = t3;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tf2orqcrphmek t15 = GM_LD(gm_tf2orqcrphmek, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 56));
                    F.l4 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l1);
                        void * t18 = (void *)((uint8_t *)&F.l4);
                        gm_tvul7d1mdc2eq t19;
                        gm_f_dump_param_kr39os0((void *)&t19, (void *)NULL, cx, NULL, t18);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tftb1odc739t5 t20 = gm_f_push_mjiecfq(t17, &t19);
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srck6bk8q21r07v, 558, 23);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 558, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
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
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sfkmkcagsnqnb));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(6));
        gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t27, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_dump_pat_arg_ai8l7kq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_pat_arg_ai8l7kq F;
    void * gm_l0; /* a */
    gm_t08olq97rscn3 gm_l1; /* n */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        if (!(((((uint8_t *)t3) + 0))[32] != 0)) goto ie2;
        gm_t08olq97rscn3 t4 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)t3) + 0));
        gm_l1 = t4;
        {
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s19ndlkgdtviu));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(5));
            gm_tvul7d1mdc2eq t7;
            gm_f_ident_sx_kd0v352((void *)&t7, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l1)));
            void * t8 = gm_l0;
            void * t9 = (void *)(((uint8_t *)t8) + 40);
            gm_tvul7d1mdc2eq t10;
            gm_f_dump_pattern_q6u5c24((void *)&t10, (void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t6, &t7, &t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 40);
        gm_f_dump_pattern_q6u5c24((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t12);
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
__attribute__((aligned(4))) void gm_f_dump_pattern_q6u5c24(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_pattern_q6u5c24 F;
    void * gm_l0; /* p */
    gm_t08olq97rscn3 gm_l2; /* id */
    gm_t08olq97rscn3 gm_l3; /* id */
    void * gm_l4; /* e */
    void * gm_l5; /* lo */
    void * gm_l6; /* hi */
    uint8_t gm_l7; /* inclusive */
    void * gm_l8; /* inner */
    gm_tagc77imt2chm gm_l9; /* pp */
    gm_b16_8 gm_l10; /* xs */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_b16_8 gm_l15; /* args */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    GM_STACK_CHECK(&F, 1437u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; default: goto ma13; }
ma4:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)0ULL)) goto mn15;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_s3brj349ts8fs));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 24, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 8, t17);
        goto gm_fdone;
        goto me3;
mn15:;
ma5:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t19 != ((uint8_t)1ULL)) goto mn18;
        gm_t08olq97rscn3 t20 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t20;
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_shmknkannurll));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(4));
        gm_tvul7d1mdc2eq t22;
        gm_f_ident_sx_kd0v352((void *)&t22, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l2)));
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t21, &t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn18:;
ma6:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t24 != ((uint8_t)2ULL)) goto mn23;
        gm_t08olq97rscn3 t25 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t25;
        gm_b16_8 t26;
        GM_ST(void *, (uint8_t *)&t26, (void *)(gm_smjq2fi49hdnf));
        GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(10));
        gm_tvul7d1mdc2eq t27;
        gm_f_ident_sx_kd0v352((void *)&t27, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l3)));
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t26, &t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn23:;
ma7:;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t29 != ((uint8_t)3ULL)) goto mn28;
        void * t30 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t30;
        gm_b16_8 t31;
        GM_ST(void *, (uint8_t *)&t31, (void *)(gm_shs3nem6t7u8g));
        GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(3));
        void * t32 = gm_l4;
        gm_tvul7d1mdc2eq t33;
        gm_f_dump_expr_l3bbdps((void *)&t33, (void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t31, &t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn28:;
ma8:;
        uint8_t t35 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t35 != ((uint8_t)4ULL)) goto mn34;
        gm_b16_8 t36;
        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_suctgno4l10a5));
        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(8));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 24, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 0, ((uint8_t)0ULL));
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 8, t36);
        goto gm_fdone;
        goto me3;
mn34:;
ma9:;
        uint8_t t38 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t38 != ((uint8_t)5ULL)) goto mn37;
        void * t39 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l5 = t39;
        void * t40 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l6 = t40;
        uint8_t t41 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l7 = t41;
        {
            void * t43 = (void *)((uint8_t *)&F.l1);
            void * t44 = gm_l5;
            gm_tvul7d1mdc2eq t45;
            gm_f_dump_expr_l3bbdps((void *)&t45, (void *)NULL, cx, NULL, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t46 = gm_f_push_mjiecfq(t43, &t45);
            if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 1, gm_srck6bk8q21r07v, 706, 13);
            gm_b0_1 t47 = GM_LD(gm_b0_1, (((uint8_t *)&t46) + 1));
        }
        {
            void * t48 = (void *)((uint8_t *)&F.l1);
            void * t49 = gm_l6;
            gm_tvul7d1mdc2eq t50;
            gm_f_dump_expr_l3bbdps((void *)&t50, (void *)NULL, cx, NULL, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t51 = gm_f_push_mjiecfq(t48, &t50);
            if (((((uint8_t *)&t51))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t51) + 1, gm_srck6bk8q21r07v, 707, 13);
            gm_b0_1 t52 = GM_LD(gm_b0_1, (((uint8_t *)&t51) + 1));
        }
        {
            uint8_t t54 = gm_l7;
            if (!t54) goto ie53;
            {
                void * t56 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t57;
                GM_ST(void *, (uint8_t *)&t57, (void *)(gm_s3ia9jm7npe6t));
                GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(9));
                gm_tvul7d1mdc2eq t58;
                GM_ST(uint8_t, (uint8_t *)&t58 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t58 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t58 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t58 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t58 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t58 + 8), t57);
                gm_tftb1odc739t5 t59 = gm_f_push_mjiecfq(t56, &t58);
                if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srck6bk8q21r07v, 708, 28);
                gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
            }
            goto ix53;
ie53:;
ix53:;
        }
        {
            gm_b16_8 t61;
            GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sl4ns6hobicht));
            GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(5));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t61, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn37:;
ma10:;
        uint8_t t63 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t63 != ((uint8_t)6ULL)) goto mn62;
        void * t64 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l8 = t64;
        gm_b16_8 t65;
        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_shhr1hlq1ethc));
        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(3));
        void * t66 = gm_l8;
        gm_tvul7d1mdc2eq t67;
        gm_f_dump_pattern_q6u5c24((void *)&t67, (void *)NULL, cx, NULL, t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t65, &t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn62:;
ma11:;
        uint8_t t69 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t69 != ((uint8_t)7ULL)) goto mn68;
        gm_tagc77imt2chm t70 = GM_LD(gm_tagc77imt2chm, ((((uint8_t *)t2) + 8) + 8));
        gm_l9 = t70;
        {
            void * t72 = (void *)((uint8_t *)&F.l1);
            void * t73 = GM_LD(void *, (((uint8_t *)&gm_l9) + 0));
            gm_tvul7d1mdc2eq t74;
            gm_f_dump_expr_l3bbdps((void *)&t74, (void *)NULL, cx, NULL, t73);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t75 = gm_f_push_mjiecfq(t72, &t74);
            if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_srck6bk8q21r07v, 713, 13);
            gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
        }
        {
            if (!(((((uint8_t *)&gm_l9) + 8))[16] != 0)) goto ie77;
            gm_b16_8 t78 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l9) + 8));
            gm_l10 = t78;
            {
                gm_f_list_ccsq5cl((void *)&F.l11, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t81 = gm_l10;
                    gm_l12 = t81;
                }
                {
                    gm_l13 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t83 = gm_l13;
                        uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        uint8_t t85 = (t83 < t84);
                        if (!t85) goto lb82;
                        {
                            uint64_t t88 = gm_l13;
                            uint64_t t89 = (uintptr_t)t88;
                            uint64_t t90 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            gm_tnrgict8sthcv t93 = GM_LD(gm_tnrgict8sthcv, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t89 * 120));
                            F.l14 = t93;
                            {
                                void * t95 = (void *)((uint8_t *)&F.l11);
                                void * t96 = (void *)((uint8_t *)&F.l14);
                                gm_tvul7d1mdc2eq t97;
                                gm_f_dump_pat_arg_ai8l7kq((void *)&t97, (void *)NULL, cx, NULL, t96);
                                if (gm_unw.kind) {
                                    goto gm_ll86;
                                }
                                gm_tftb1odc739t5 t98 = gm_f_push_mjiecfq(t95, &t97);
                                if (((((uint8_t *)&t98))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t98) + 1, gm_srck6bk8q21r07v, 716, 31);
                                gm_b0_1 t99 = GM_LD(gm_b0_1, (((uint8_t *)&t98) + 1));
                            }
                            goto me91;
mn92:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 716, 17);
me91:;
                        }
lc82:;
                        {
                            uint64_t t101 = gm_l13;
                            uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                            uint64_t t104 = r102;
                            gm_l13 = t104;
                        }
                    }
gm_ll86:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb82; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc82; }
                    }
                    goto gm_lad;
lb82:;
                }
            }
            {
                void * t105 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t106;
                GM_ST(void *, (uint8_t *)&t106, (void *)(gm_s20t5qopq8n9u));
                GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(4));
                gm_tvul7d1mdc2eq t107;
                gm_f_node_dm1r8dn((void *)&t107, (void *)NULL, cx, NULL, t106, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l11)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t108 = gm_f_push_mjiecfq(t105, &t107);
                if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_srck6bk8q21r07v, 717, 17);
                gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
            }
            goto ix77;
ie77:;
ix77:;
        }
        {
            gm_b16_8 t110;
            GM_ST(void *, (uint8_t *)&t110, (void *)(gm_smimi08sfrg0n));
            GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(4));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t110, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn68:;
ma12:;
        uint8_t t112 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t112 != ((uint8_t)8ULL)) goto mn111;
        gm_b16_8 t113 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l15 = t113;
        {
            {
                gm_b16_8 t116 = gm_l15;
                gm_l16 = t116;
            }
            {
                gm_l17 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t118 = gm_l17;
                    uint64_t t119 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    uint8_t t120 = (t118 < t119);
                    if (!t120) goto lb117;
                    {
                        uint64_t t123 = gm_l17;
                        uint64_t t124 = (uintptr_t)t123;
                        uint64_t t125 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        gm_tnrgict8sthcv t128 = GM_LD(gm_tnrgict8sthcv, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t124 * 120));
                        F.l18 = t128;
                        {
                            void * t130 = (void *)((uint8_t *)&F.l1);
                            void * t131 = (void *)((uint8_t *)&F.l18);
                            gm_tvul7d1mdc2eq t132;
                            gm_f_dump_pat_arg_ai8l7kq((void *)&t132, (void *)NULL, cx, NULL, t131);
                            if (gm_unw.kind) {
                                goto gm_ll121;
                            }
                            gm_tftb1odc739t5 t133 = gm_f_push_mjiecfq(t130, &t132);
                            if (((((uint8_t *)&t133))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t133) + 1, gm_srck6bk8q21r07v, 722, 29);
                            gm_b0_1 t134 = GM_LD(gm_b0_1, (((uint8_t *)&t133) + 1));
                        }
                        goto me126;
mn127:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 722, 13);
me126:;
                    }
lc117:;
                    {
                        uint64_t t136 = gm_l17;
                        uint64_t r137; bool o138 = __builtin_add_overflow(t136, ((uint64_t)1ULL), &r137);
                        uint64_t t139 = r137;
                        gm_l17 = t139;
                    }
                }
gm_ll121:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb117; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc117; }
                }
                goto gm_lad;
lb117:;
            }
        }
        {
            gm_b16_8 t140;
            GM_ST(void *, (uint8_t *)&t140, (void *)(gm_sk83con0mdsfe));
            GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(5));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t140, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn111:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 699, 5);
me3:;
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
__attribute__((aligned(4))) void gm_f_dump_stmt_tfqg5dm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_stmt_tfqg5dm F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l1; /* attrs */
    gm_b16_8 gm_l4; /* places */
    gm_tjsp7bhdn2e8j gm_l5; /* op */
    void * gm_l6; /* value */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    void * gm_l12; /* e */
    uint8_t gm_l13; /* is_err */
    void * gm_l14; /* body */
    void * gm_l15; /* w */
    gm_t08olq97rscn3 gm_l18; /* l */
    void * gm_l19; /* e */
    void * gm_l21; /* value */
    void * gm_l22; /* fallback */
    GM_STACK_CHECK(&F, 1718u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; default: goto ma13; }
ma4:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)0ULL)) goto mn15;
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l1 = t17;
        gm_tv2uf4leil5hq t18 = GM_LD(gm_tv2uf4leil5hq, (((((uint8_t *)t2) + 8) + 8) + 16));
        F.l2 = t18;
        {
            uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
            if (!t22) goto ie20;
            {
                void * t24 = (void *)((uint8_t *)&F.l2);
                gm_f_dump_binding_b27qkdk((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t24);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix20;
ie20:;
ix20:;
        }
        {
            gm_f_list_ccsq5cl((void *)&F.l3, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t25 = (void *)((uint8_t *)&F.l3);
            gm_b16_8 t26 = gm_l1;
            gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
            gm_b0_1 t28 = gm_f_push_attrs_a5l439c((void *)NULL, cx, NULL, t25, t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t29 = (void *)((uint8_t *)&F.l3);
            void * t30 = (void *)((uint8_t *)&F.l2);
            gm_tvul7d1mdc2eq t31;
            gm_f_dump_binding_b27qkdk((void *)&t31, (void *)NULL, cx, NULL, t30);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t32 = gm_f_push_mjiecfq(t29, &t31);
            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srck6bk8q21r07v, 286, 13);
            gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
        }
        {
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_spl4qnmq2arsc));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(12));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t34, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn15:;
ma5:;
        uint8_t t36 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t36 != ((uint8_t)1ULL)) goto mn35;
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l4 = t37;
        gm_tjsp7bhdn2e8j t38 = GM_LD(gm_tjsp7bhdn2e8j, (((((uint8_t *)t2) + 8) + 8) + 16));
        gm_l5 = t38;
        void * t39 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 24));
        gm_l6 = t39;
        {
            gm_f_list_ccsq5cl((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t42 = gm_l4;
                gm_l8 = t42;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t44 = gm_l9;
                    uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t46 = (t44 < t45);
                    if (!t46) goto lb43;
                    {
                        uint64_t t49 = gm_l9;
                        uint64_t t50 = (uintptr_t)t49;
                        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        gm_tc3oht4ksth9d t54 = GM_LD(gm_tc3oht4ksth9d, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t50 * 88));
                        F.l10 = t54;
                        {
                            void * t56 = (void *)((uint8_t *)&F.l7);
                            void * t57 = (void *)((uint8_t *)&F.l10);
                            gm_tvul7d1mdc2eq t58;
                            gm_f_dump_expr_l3bbdps((void *)&t58, (void *)NULL, cx, NULL, t57);
                            if (gm_unw.kind) {
                                goto gm_ll47;
                            }
                            gm_tftb1odc739t5 t59 = gm_f_push_mjiecfq(t56, &t58);
                            if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srck6bk8q21r07v, 291, 31);
                            gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
                        }
                        goto me52;
mn53:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 291, 13);
me52:;
                    }
lc43:;
                    {
                        uint64_t t62 = gm_l9;
                        uint64_t r63; bool o64 = __builtin_add_overflow(t62, ((uint64_t)1ULL), &r63);
                        uint64_t t65 = r63;
                        gm_l9 = t65;
                    }
                }
gm_ll47:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb43; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc43; }
                }
                goto gm_lad;
lb43:;
            }
        }
        {
            gm_f_list_ccsq5cl((void *)&F.l11, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t66 = (void *)((uint8_t *)&F.l11);
            gm_tjsp7bhdn2e8j t67 = gm_l5;
            gm_b16_8 t68 = gm_f_assign_op_name_8crhefq((void *)NULL, cx, NULL, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t69;
            GM_ST(uint8_t, (uint8_t *)&t69 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t69 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t69 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t69 + 24, 0);
            GM_ST(uint8_t, ((uint8_t *)&t69 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t69 + 8), t68);
            gm_tftb1odc739t5 t70 = gm_f_push_mjiecfq(t66, &t69);
            if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_srck6bk8q21r07v, 293, 13);
            gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
        }
        {
            void * t72 = (void *)((uint8_t *)&F.l11);
            gm_b16_8 t73;
            GM_ST(void *, (uint8_t *)&t73, (void *)(gm_s0l81imssm78p));
            GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)(6));
            gm_tvul7d1mdc2eq t74;
            gm_f_node_dm1r8dn((void *)&t74, (void *)NULL, cx, NULL, t73, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l7)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t75 = gm_f_push_mjiecfq(t72, &t74);
            if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 1, gm_srck6bk8q21r07v, 294, 13);
            gm_b0_1 t76 = GM_LD(gm_b0_1, (((uint8_t *)&t75) + 1));
        }
        {
            void * t77 = (void *)((uint8_t *)&F.l11);
            void * t78 = gm_l6;
            gm_tvul7d1mdc2eq t79;
            gm_f_dump_expr_l3bbdps((void *)&t79, (void *)NULL, cx, NULL, t78);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t80 = gm_f_push_mjiecfq(t77, &t79);
            if (((((uint8_t *)&t80))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t80) + 1, gm_srck6bk8q21r07v, 295, 13);
            gm_b0_1 t81 = GM_LD(gm_b0_1, (((uint8_t *)&t80) + 1));
        }
        {
            gm_b16_8 t82;
            GM_ST(void *, (uint8_t *)&t82, (void *)(gm_sugq7dl55ocon));
            GM_ST(uintptr_t, (uint8_t *)&t82 + 8, (uintptr_t)(6));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t82, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l11)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn35:;
ma6:;
        uint8_t t84 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t84 != ((uint8_t)2ULL)) goto mn83;
        void * t85 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l12 = t85;
        gm_b16_8 t86;
        GM_ST(void *, (uint8_t *)&t86, (void *)(gm_sk5s72mtujop8));
        GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(4));
        void * t87 = gm_l12;
        gm_tvul7d1mdc2eq t88;
        gm_f_dump_expr_l3bbdps((void *)&t88, (void *)NULL, cx, NULL, t87);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t86, &t88);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn83:;
ma7:;
        uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t90 != ((uint8_t)3ULL)) goto mn89;
        uint8_t t91 = GM_LD(uint8_t, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l13 = t91;
        void * t92 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l14 = t92;
        gm_b16_8 t93;
        uint8_t t95 = gm_l13;
        if (!t95) goto ie94;
        gm_b16_8 t97;
        GM_ST(void *, (uint8_t *)&t97, (void *)(gm_ser8ip7vtbv6c));
        GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(8));
        gm_b16_8 t98 = t97;
        t93 = t98;
        goto ix94;
ie94:;
        gm_b16_8 t100;
        GM_ST(void *, (uint8_t *)&t100, (void *)(gm_sllcm1pqagh28));
        GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(5));
        gm_b16_8 t101 = t100;
        t93 = t101;
ix94:;
        void * t102 = gm_l14;
        gm_tvul7d1mdc2eq t103;
        gm_f_dump_stmt_tfqg5dm((void *)&t103, (void *)NULL, cx, NULL, t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t93, &t103);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn89:;
ma8:;
        uint8_t t105 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t105 != ((uint8_t)4ULL)) goto mn104;
        void * t106 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l15 = t106;
        void * t107 = gm_l15;
        gm_f_dump_when_stmt_hdq14ev((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t107);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn104:;
ma9:;
        uint8_t t109 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t109 != ((uint8_t)5ULL)) goto mn108;
        gm_t43d6nbdfap6p t110 = GM_LD(gm_t43d6nbdfap6p, ((((uint8_t *)t2) + 8) + 8));
        F.l16 = t110;
        {
            gm_f_list_ccsq5cl((void *)&F.l17, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            if (!(((((uint8_t *)&F.l16) + 0))[32] != 0)) goto ie112;
            gm_t08olq97rscn3 t113 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&F.l16) + 0));
            gm_l18 = t113;
            {
                void * t115 = (void *)((uint8_t *)&F.l17);
                gm_b16_8 t116;
                GM_ST(void *, (uint8_t *)&t116, (void *)(gm_su137hja0ebcd));
                GM_ST(uintptr_t, (uint8_t *)&t116 + 8, (uintptr_t)(5));
                gm_tvul7d1mdc2eq t117;
                gm_f_ident_sx_kd0v352((void *)&t117, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l18)));
                gm_tvul7d1mdc2eq t118;
                gm_f_node1_gpsjtpc((void *)&t118, (void *)NULL, cx, NULL, t116, &t117);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t119 = gm_f_push_mjiecfq(t115, &t118);
                if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 1, gm_srck6bk8q21r07v, 303, 31);
                gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
            }
            goto ix112;
ie112:;
ix112:;
        }
        {
            void * t121 = (void *)((uint8_t *)&F.l17);
            void * t122 = (void *)(((uint8_t *)&F.l16) + 40);
            gm_tvul7d1mdc2eq t123;
            gm_f_dump_cond_cu0s815((void *)&t123, (void *)NULL, cx, NULL, t122);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t124 = gm_f_push_mjiecfq(t121, &t123);
            if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 1, gm_srck6bk8q21r07v, 304, 13);
            gm_b0_1 t125 = GM_LD(gm_b0_1, (((uint8_t *)&t124) + 1));
        }
        {
            void * t126 = (void *)((uint8_t *)&F.l17);
            void * t127 = (void *)(((uint8_t *)&F.l16) + 72);
            gm_tvul7d1mdc2eq t128;
            gm_f_dump_block_5utjqg0((void *)&t128, (void *)NULL, cx, NULL, t127);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t129 = gm_f_push_mjiecfq(t126, &t128);
            if (((((uint8_t *)&t129))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t129) + 1, gm_srck6bk8q21r07v, 305, 13);
            gm_b0_1 t130 = GM_LD(gm_b0_1, (((uint8_t *)&t129) + 1));
        }
        {
            gm_b16_8 t131;
            GM_ST(void *, (uint8_t *)&t131, (void *)(gm_s5rv6umk4akl0));
            GM_ST(uintptr_t, (uint8_t *)&t131 + 8, (uintptr_t)(5));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t131, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l17)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn108:;
ma10:;
        uint8_t t133 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t133 != ((uint8_t)6ULL)) goto mn132;
        void * t134 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l19 = t134;
        gm_b16_8 t135;
        GM_ST(void *, (uint8_t *)&t135, (void *)(gm_si1m7q6ia0hto));
        GM_ST(uintptr_t, (uint8_t *)&t135 + 8, (uintptr_t)(6));
        void * t136 = gm_l19;
        gm_tvul7d1mdc2eq t137;
        gm_f_dump_expr_l3bbdps((void *)&t137, (void *)NULL, cx, NULL, t136);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t135, &t137);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn132:;
ma11:;
        uint8_t t139 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t139 != ((uint8_t)7ULL)) goto mn138;
        gm_t0d02142k4tu8 t140 = GM_LD(gm_t0d02142k4tu8, (((((uint8_t *)t2) + 8) + 8) + 0));
        F.l20 = t140;
        void * t141 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 64));
        gm_l21 = t141;
        void * t142 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 72));
        gm_l22 = t142;
        {
            gm_f_list_ccsq5cl((void *)&F.l23, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t144 = (void *)((uint8_t *)&F.l23);
            void * t145 = (void *)((uint8_t *)&F.l20);
            gm_tvul7d1mdc2eq t146;
            gm_f_dump_pattern_q6u5c24((void *)&t146, (void *)NULL, cx, NULL, t145);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t147 = gm_f_push_mjiecfq(t144, &t146);
            if (((((uint8_t *)&t147))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t147) + 1, gm_srck6bk8q21r07v, 311, 13);
            gm_b0_1 t148 = GM_LD(gm_b0_1, (((uint8_t *)&t147) + 1));
        }
        {
            void * t149 = (void *)((uint8_t *)&F.l23);
            void * t150 = gm_l21;
            gm_tvul7d1mdc2eq t151;
            gm_f_dump_expr_l3bbdps((void *)&t151, (void *)NULL, cx, NULL, t150);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t152 = gm_f_push_mjiecfq(t149, &t151);
            if (((((uint8_t *)&t152))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t152) + 1, gm_srck6bk8q21r07v, 312, 13);
            gm_b0_1 t153 = GM_LD(gm_b0_1, (((uint8_t *)&t152) + 1));
        }
        {
            void * t154 = (void *)((uint8_t *)&F.l23);
            void * t155 = gm_l22;
            gm_tvul7d1mdc2eq t156;
            gm_f_dump_expr_l3bbdps((void *)&t156, (void *)NULL, cx, NULL, t155);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t157 = gm_f_push_mjiecfq(t154, &t156);
            if (((((uint8_t *)&t157))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t157) + 1, gm_srck6bk8q21r07v, 313, 13);
            gm_b0_1 t158 = GM_LD(gm_b0_1, (((uint8_t *)&t157) + 1));
        }
        {
            gm_b16_8 t159;
            GM_ST(void *, (uint8_t *)&t159, (void *)(gm_s283bs8hsleb3));
            GM_ST(uintptr_t, (uint8_t *)&t159 + 8, (uintptr_t)(8));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t159, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l23)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn138:;
ma12:;
        uint8_t t161 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t161 != ((uint8_t)8ULL)) goto mn160;
        gm_t79rbdv4rafe5 t162 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t2) + 8) + 8));
        F.l24 = t162;
        void * t163 = (void *)((uint8_t *)&F.l24);
        gm_f_dump_block_5utjqg0((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t163);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn160:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 281, 5);
me3:;
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
inline __attribute__((aligned(4))) void gm_f_dump_struct_lit_n9dqlnm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_struct_lit_n9dqlnm F;
    void * gm_l0; /* s */
    gm_b16_8 gm_l2; /* p */
    gm_tvul7d1mdc2eq gm_l3; /* w */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    GM_STACK_CHECK(&F, 495u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        if (!(((((uint8_t *)t3) + 0))[16] != 0)) goto ie2;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 0));
        gm_l2 = t4;
        {
            void * t6 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t7 = gm_l2;
            gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
            gm_tvul7d1mdc2eq t9;
            gm_f_dump_params_vfgaabj((void *)&t9, (void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t10 = gm_f_push_mjiecfq(t6, &t9);
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srck6bk8q21r07v, 591, 24);
            gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t13 = gm_l0;
        gm_b16_8 t14 = GM_LD(gm_b16_8, (((uint8_t *)t13) + 24));
        gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
        gm_t4eh5qkgoio6s t16;
        gm_f_dump_constraints_68aa75n((void *)&t16, (void *)NULL, cx, NULL, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t16))[32] != 0)) goto ie12;
        gm_l3 = (*(gm_tvul7d1mdc2eq *)(((uint8_t *)&t16) + 0));
        {
            void * t18 = (void *)((uint8_t *)&F.l1);
            gm_tftb1odc739t5 t19 = gm_f_push_mjiecfq(t18, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l3)));
            if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 1, gm_srck6bk8q21r07v, 592, 42);
            gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 1));
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        {
            void * t22 = gm_l0;
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)t22) + 40));
            gm_l4 = t23;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l5;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t30 = gm_l5;
                    uint64_t t31 = (uintptr_t)t30;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_tfc3oljlofib6 t35 = GM_LD(gm_tfc3oljlofib6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t31 * 80));
                    F.l6 = t35;
                    {
                        void * t37 = (void *)((uint8_t *)&F.l1);
                        void * t38 = (void *)((uint8_t *)&F.l6);
                        gm_tvul7d1mdc2eq t39;
                        gm_f_dump_struct_member_q034tdn((void *)&t39, (void *)NULL, cx, NULL, t38);
                        if (gm_unw.kind) {
                            goto gm_ll28;
                        }
                        gm_tftb1odc739t5 t40 = gm_f_push_mjiecfq(t37, &t39);
                        if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 1, gm_srck6bk8q21r07v, 593, 26);
                        gm_b0_1 t41 = GM_LD(gm_b0_1, (((uint8_t *)&t40) + 1));
                    }
                    goto me33;
mn34:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 593, 5);
me33:;
                }
lc24:;
                {
                    uint64_t t43 = gm_l5;
                    uint64_t r44; bool o45 = __builtin_add_overflow(t43, ((uint64_t)1ULL), &r44);
                    uint64_t t46 = r44;
                    gm_l5 = t46;
                }
            }
gm_ll28:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb24; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc24; }
            }
            goto gm_lad;
lb24:;
        }
    }
    {
        gm_b16_8 t47;
        GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sc49g68j2cekh));
        GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(6));
        gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t47, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
__attribute__((aligned(4))) void gm_f_dump_struct_member_q034tdn(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_struct_member_q034tdn F;
    void * gm_l0; /* m */
    gm_b16_8 gm_l3; /* vis */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_t08olq97rscn3 gm_l7; /* n */
    void * gm_l8; /* d */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_b16_8 gm_l14; /* members */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    GM_STACK_CHECK(&F, 1485u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t5 != ((uint8_t)0ULL)) goto mn4;
        gm_t24301fas1b2a t6 = GM_LD(gm_t24301fas1b2a, (((uint8_t *)t2) + 8));
        F.l2 = t6;
        {
            void * t8 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
            gm_b0_1 t11 = gm_f_push_attrs_a5l439c((void *)NULL, cx, NULL, t8, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_t9qkh0rsooc8d t13 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&F.l2) + 16));
            gm_th2lqbo77pvsc t14;
            gm_f_visibility_u2ipb12((void *)&t14, t13);
            if (!((((uint8_t *)&t14))[16] != 0)) goto ie12;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            gm_l3 = t15;
            {
                void * t17 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t18 = gm_l3;
                gm_tvul7d1mdc2eq t19;
                GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t19 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t19 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t19 + 8), t18);
                gm_tftb1odc739t5 t20 = gm_f_push_mjiecfq(t17, &t19);
                if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srck6bk8q21r07v, 602, 43);
                gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
            }
            goto ix12;
ie12:;
ix12:;
        }
        {
            uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 17));
            if (!t23) goto ie22;
            {
                void * t25 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t26;
                GM_ST(void *, (uint8_t *)&t26, (void *)(gm_smu48daue2qai));
                GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(5));
                gm_tvul7d1mdc2eq t27;
                GM_ST(uint8_t, (uint8_t *)&t27 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t27 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t27 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t27 + 24, 0);
                GM_ST(uint8_t, ((uint8_t *)&t27 + 0), ((uint8_t)0ULL));
                GM_ST(gm_b16_8, ((uint8_t *)&t27 + 8), t26);
                gm_tftb1odc739t5 t28 = gm_f_push_mjiecfq(t25, &t27);
                if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 1, gm_srck6bk8q21r07v, 603, 29);
                gm_b0_1 t29 = GM_LD(gm_b0_1, (((uint8_t *)&t28) + 1));
            }
            goto ix22;
ie22:;
ix22:;
        }
        {
            gm_f_list_ccsq5cl((void *)&F.l4, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 24));
                gm_l5 = t31;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t33 = gm_l6;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t35 = (t33 < t34);
                    if (!t35) goto lb32;
                    {
                        uint64_t t38 = gm_l6;
                        uint64_t t39 = (uintptr_t)t38;
                        uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        gm_t08olq97rscn3 t43 = GM_LD(gm_t08olq97rscn3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t39 * 32));
                        gm_l7 = t43;
                        {
                            void * t45 = (void *)((uint8_t *)&F.l4);
                            gm_tvul7d1mdc2eq t46;
                            gm_f_ident_sx_kd0v352((void *)&t46, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l7)));
                            gm_tftb1odc739t5 t47 = gm_f_push_mjiecfq(t45, &t46);
                            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srck6bk8q21r07v, 605, 32);
                            gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
                        }
                        goto me41;
mn42:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 605, 13);
me41:;
                    }
lc32:;
                    {
                        uint64_t t50 = gm_l6;
                        uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                        uint64_t t53 = r51;
                        gm_l6 = t53;
                    }
                }
lb32:;
            }
        }
        {
            void * t54 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sm56lhobvb6g5));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(5));
            gm_tvul7d1mdc2eq t56;
            gm_f_node_dm1r8dn((void *)&t56, (void *)NULL, cx, NULL, t55, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l4)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t57 = gm_f_push_mjiecfq(t54, &t56);
            if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srck6bk8q21r07v, 606, 13);
            gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
        }
        {
            void * t59 = (void *)((uint8_t *)&F.l1);
            void * t60 = GM_LD(void *, (((uint8_t *)&F.l2) + 40));
            gm_tvul7d1mdc2eq t61;
            gm_f_dump_expr_l3bbdps((void *)&t61, (void *)NULL, cx, NULL, t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t62 = gm_f_push_mjiecfq(t59, &t61);
            if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srck6bk8q21r07v, 607, 13);
            gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
        }
        {
            void * t65 = GM_LD(void *, (((uint8_t *)&F.l2) + 48));
            if (!(GM_LD(void *, ((uint8_t *)&t65)) != 0)) goto ie64;
            void * t66 = GM_LD(void *, ((uint8_t *)&t65));
            gm_l8 = t66;
            {
                void * t68 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t69;
                GM_ST(void *, (uint8_t *)&t69, (void *)(gm_ses9qtp6kdd22));
                GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(7));
                void * t70 = gm_l8;
                gm_tvul7d1mdc2eq t71;
                gm_f_dump_expr_l3bbdps((void *)&t71, (void *)NULL, cx, NULL, t70);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tvul7d1mdc2eq t72;
                gm_f_node1_gpsjtpc((void *)&t72, (void *)NULL, cx, NULL, t69, &t71);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t73 = gm_f_push_mjiecfq(t68, &t72);
                if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 1, gm_srck6bk8q21r07v, 608, 33);
                gm_b0_1 t74 = GM_LD(gm_b0_1, (((uint8_t *)&t73) + 1));
            }
            goto ix64;
ie64:;
ix64:;
        }
        {
            gm_b16_8 t75;
            GM_ST(void *, (uint8_t *)&t75, (void *)(gm_s1jboc8lkvac0));
            GM_ST(uintptr_t, (uint8_t *)&t75 + 8, (uintptr_t)(5));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t75, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn4:;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t77 != ((uint8_t)1ULL)) goto mn76;
        gm_t7bjbmukv26e7 t78 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)t2) + 8));
        F.l9 = t78;
        {
            void * t80 = (void *)((uint8_t *)&F.l1);
            void * t81 = GM_LD(void *, (((uint8_t *)&F.l9) + 0));
            gm_tvul7d1mdc2eq t82;
            gm_f_dump_expr_l3bbdps((void *)&t82, (void *)NULL, cx, NULL, t81);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t83 = gm_f_push_mjiecfq(t80, &t82);
            if (((((uint8_t *)&t83))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t83) + 1, gm_srck6bk8q21r07v, 612, 13);
            gm_b0_1 t84 = GM_LD(gm_b0_1, (((uint8_t *)&t83) + 1));
        }
        {
            gm_f_list_ccsq5cl((void *)&F.l10, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 8));
                gm_l11 = t86;
            }
            {
                gm_l12 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t88 = gm_l12;
                    uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    uint8_t t90 = (t88 < t89);
                    if (!t90) goto lb87;
                    {
                        uint64_t t93 = gm_l12;
                        uint64_t t94 = (uintptr_t)t93;
                        uint64_t t95 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                        gm_tfc3oljlofib6 t98 = GM_LD(gm_tfc3oljlofib6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t94 * 80));
                        F.l13 = t98;
                        {
                            void * t100 = (void *)((uint8_t *)&F.l10);
                            void * t101 = (void *)((uint8_t *)&F.l13);
                            gm_tvul7d1mdc2eq t102;
                            gm_f_dump_struct_member_q034tdn((void *)&t102, (void *)NULL, cx, NULL, t101);
                            if (gm_unw.kind) {
                                goto gm_ll91;
                            }
                            gm_tftb1odc739t5 t103 = gm_f_push_mjiecfq(t100, &t102);
                            if (((((uint8_t *)&t103))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t103) + 1, gm_srck6bk8q21r07v, 614, 31);
                            gm_b0_1 t104 = GM_LD(gm_b0_1, (((uint8_t *)&t103) + 1));
                        }
                        goto me96;
mn97:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 614, 13);
me96:;
                    }
lc87:;
                    {
                        uint64_t t106 = gm_l12;
                        uint64_t r107; bool o108 = __builtin_add_overflow(t106, ((uint64_t)1ULL), &r107);
                        uint64_t t109 = r107;
                        gm_l12 = t109;
                    }
                }
gm_ll91:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb87; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc87; }
                }
                goto gm_lad;
lb87:;
            }
        }
        {
            void * t110 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t111;
            GM_ST(void *, (uint8_t *)&t111, (void *)(gm_sofbtko7t8taa));
            GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(4));
            gm_tvul7d1mdc2eq t112;
            gm_f_bnode_hu9fd4q((void *)&t112, (void *)NULL, cx, NULL, t111, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l10)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t113 = gm_f_push_mjiecfq(t110, &t112);
            if (((((uint8_t *)&t113))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t113) + 1, gm_srck6bk8q21r07v, 615, 13);
            gm_b0_1 t114 = GM_LD(gm_b0_1, (((uint8_t *)&t113) + 1));
        }
        {
            if (!(((((uint8_t *)&F.l9) + 24))[16] != 0)) goto ie115;
            gm_b16_8 t116 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 24));
            gm_l14 = t116;
            {
                gm_f_list_ccsq5cl((void *)&F.l15, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t119 = gm_l14;
                    gm_l16 = t119;
                }
                {
                    gm_l17 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t121 = gm_l17;
                        uint64_t t122 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                        uint8_t t123 = (t121 < t122);
                        if (!t123) goto lb120;
                        {
                            uint64_t t126 = gm_l17;
                            uint64_t t127 = (uintptr_t)t126;
                            uint64_t t128 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                            gm_tfc3oljlofib6 t131 = GM_LD(gm_tfc3oljlofib6, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t127 * 80));
                            F.l18 = t131;
                            {
                                void * t133 = (void *)((uint8_t *)&F.l15);
                                void * t134 = (void *)((uint8_t *)&F.l18);
                                gm_tvul7d1mdc2eq t135;
                                gm_f_dump_struct_member_q034tdn((void *)&t135, (void *)NULL, cx, NULL, t134);
                                if (gm_unw.kind) {
                                    goto gm_ll124;
                                }
                                gm_tftb1odc739t5 t136 = gm_f_push_mjiecfq(t133, &t135);
                                if (((((uint8_t *)&t136))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t136) + 1, gm_srck6bk8q21r07v, 618, 36);
                                gm_b0_1 t137 = GM_LD(gm_b0_1, (((uint8_t *)&t136) + 1));
                            }
                            goto me129;
mn130:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 618, 17);
me129:;
                        }
lc120:;
                        {
                            uint64_t t139 = gm_l17;
                            uint64_t r140; bool o141 = __builtin_add_overflow(t139, ((uint64_t)1ULL), &r140);
                            uint64_t t142 = r140;
                            gm_l17 = t142;
                        }
                    }
gm_ll124:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb120; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc120; }
                    }
                    goto gm_lad;
lb120:;
                }
            }
            {
                void * t143 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t144;
                GM_ST(void *, (uint8_t *)&t144, (void *)(gm_s9es2tos1bhfp));
                GM_ST(uintptr_t, (uint8_t *)&t144 + 8, (uintptr_t)(4));
                gm_tvul7d1mdc2eq t145;
                gm_f_bnode_hu9fd4q((void *)&t145, (void *)NULL, cx, NULL, t144, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l15)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t146 = gm_f_push_mjiecfq(t143, &t145);
                if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 1, gm_srck6bk8q21r07v, 619, 17);
                gm_b0_1 t147 = GM_LD(gm_b0_1, (((uint8_t *)&t146) + 1));
            }
            goto ix115;
ie115:;
ix115:;
        }
        {
            gm_b16_8 t148;
            GM_ST(void *, (uint8_t *)&t148, (void *)(gm_s11b339fv56g1));
            GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(12));
            gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t148, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn76:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 599, 5);
me3:;
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
inline __attribute__((aligned(4))) void gm_f_dump_tuple_elem_bul8rgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_tuple_elem_bul8rgi F;
    void * gm_l0; /* t */
    gm_t08olq97rscn3 gm_l1; /* n */
    GM_STACK_CHECK(&F, 265u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        if (!(((((uint8_t *)t3) + 0))[32] != 0)) goto ie2;
        gm_t08olq97rscn3 t4 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)t3) + 0));
        gm_l1 = t4;
        {
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_s19ndlkgdtviu));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(5));
            gm_tvul7d1mdc2eq t7;
            gm_f_ident_sx_kd0v352((void *)&t7, &(*(gm_t08olq97rscn3 *)((uint8_t *)&gm_l1)));
            void * t8 = gm_l0;
            void * t9 = (void *)(((uint8_t *)t8) + 40);
            gm_tvul7d1mdc2eq t10;
            gm_f_dump_expr_l3bbdps((void *)&t10, (void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node2_b4qpqat((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t6, &t7, &t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t12 = gm_l0;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t12) + 128));
        if (!t13) goto ie11;
        {
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(gm_s80ddhqa1o9i9));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(6));
            void * t16 = gm_l0;
            void * t17 = (void *)(((uint8_t *)t16) + 40);
            gm_tvul7d1mdc2eq t18;
            gm_f_dump_expr_l3bbdps((void *)&t18, (void *)NULL, cx, NULL, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t15, &t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        void * t19 = gm_l0;
        void * t20 = (void *)(((uint8_t *)t19) + 40);
        gm_f_dump_expr_l3bbdps((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t20);
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
inline __attribute__((aligned(4))) void gm_f_dump_union_lit_leu6qp2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_union_lit_leu6qp2 F;
    void * gm_l0; /* u */
    void * gm_l2; /* t */
    gm_b16_8 gm_l3; /* p */
    gm_tvul7d1mdc2eq gm_l4; /* w */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    GM_STACK_CHECK(&F, 617u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        if (!(GM_LD(void *, ((uint8_t *)&t4)) != 0)) goto ie2;
        void * t5 = GM_LD(void *, ((uint8_t *)&t4));
        gm_l2 = t5;
        {
            void * t7 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t8;
            GM_ST(void *, (uint8_t *)&t8, (void *)(gm_s420tm9ua7ggg));
            GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(3));
            void * t9 = gm_l2;
            gm_tvul7d1mdc2eq t10;
            gm_f_dump_expr_l3bbdps((void *)&t10, (void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvul7d1mdc2eq t11;
            gm_f_node1_gpsjtpc((void *)&t11, (void *)NULL, cx, NULL, t8, &t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t12 = gm_f_push_mjiecfq(t7, &t11);
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srck6bk8q21r07v, 628, 26);
            gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t15 = gm_l0;
        if (!(((((uint8_t *)t15) + 8))[16] != 0)) goto ie14;
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)t15) + 8));
        gm_l3 = t16;
        {
            void * t18 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t19 = gm_l3;
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            gm_tvul7d1mdc2eq t21;
            gm_f_dump_params_vfgaabj((void *)&t21, (void *)NULL, cx, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t22 = gm_f_push_mjiecfq(t18, &t21);
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 1, gm_srck6bk8q21r07v, 629, 24);
            gm_b0_1 t23 = GM_LD(gm_b0_1, (((uint8_t *)&t22) + 1));
        }
        goto ix14;
ie14:;
ix14:;
    }
    {
        void * t25 = gm_l0;
        gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)t25) + 32));
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        gm_t4eh5qkgoio6s t28;
        gm_f_dump_constraints_68aa75n((void *)&t28, (void *)NULL, cx, NULL, t27);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!((((uint8_t *)&t28))[32] != 0)) goto ie24;
        gm_l4 = (*(gm_tvul7d1mdc2eq *)(((uint8_t *)&t28) + 0));
        {
            void * t30 = (void *)((uint8_t *)&F.l1);
            gm_tftb1odc739t5 t31 = gm_f_push_mjiecfq(t30, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l4)));
            if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srck6bk8q21r07v, 630, 42);
            gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        {
            void * t34 = gm_l0;
            gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)t34) + 48));
            gm_l5 = t35;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t37 = gm_l6;
                uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t39 = (t37 < t38);
                if (!t39) goto lb36;
                {
                    uint64_t t42 = gm_l6;
                    uint64_t t43 = (uintptr_t)t42;
                    uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_t03a3imodqi32 t47 = GM_LD(gm_t03a3imodqi32, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t43 * 72));
                    F.l7 = t47;
                    {
                        void * t49 = (void *)((uint8_t *)&F.l1);
                        void * t50 = (void *)((uint8_t *)&F.l7);
                        gm_tvul7d1mdc2eq t51;
                        gm_f_dump_union_member_4lopa45((void *)&t51, (void *)NULL, cx, NULL, t50);
                        if (gm_unw.kind) {
                            goto gm_ll40;
                        }
                        gm_tftb1odc739t5 t52 = gm_f_push_mjiecfq(t49, &t51);
                        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srck6bk8q21r07v, 631, 26);
                        gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
                    }
                    goto me45;
mn46:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 631, 5);
me45:;
                }
lc36:;
                {
                    uint64_t t55 = gm_l6;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l6 = t58;
                }
            }
gm_ll40:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb36; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc36; }
            }
            goto gm_lad;
lb36:;
        }
    }
    {
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(gm_s8p1h96ji9kh0));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(5));
        gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t59, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
__attribute__((aligned(4))) void gm_f_dump_union_member_4lopa45(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_union_member_4lopa45 F;
    void * gm_l0; /* m */
    void * gm_l3; /* p */
    void * gm_l4; /* x */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    gm_b16_8 gm_l10; /* members */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    GM_STACK_CHECK(&F, 1100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t5 != ((uint8_t)0ULL)) goto mn4;
        gm_tnljbma3s3h16 t6 = GM_LD(gm_tnljbma3s3h16, (((uint8_t *)t2) + 8));
        F.l2 = t6;
        {
            void * t8 = (void *)((uint8_t *)&F.l1);
            gm_tvul7d1mdc2eq t9;
            gm_f_ident_sx_kd0v352((void *)&t9, &(*(gm_t08olq97rscn3 *)(((uint8_t *)&F.l2) + 0)));
            gm_tftb1odc739t5 t10 = gm_f_push_mjiecfq(t8, &t9);
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srck6bk8q21r07v, 639, 13);
            gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
        }
        {
            void * t13 = GM_LD(void *, (((uint8_t *)&F.l2) + 32));
            if (!(GM_LD(void *, ((uint8_t *)&t13)) != 0)) goto ie12;
            void * t14 = GM_LD(void *, ((uint8_t *)&t13));
            gm_l3 = t14;
            {
                void * t16 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t17;
                GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sd9regb2pbgv1));
                GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(7));
                void * t18 = gm_l3;
                gm_tvul7d1mdc2eq t19;
                gm_f_dump_expr_l3bbdps((void *)&t19, (void *)NULL, cx, NULL, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tvul7d1mdc2eq t20;
                gm_f_node1_gpsjtpc((void *)&t20, (void *)NULL, cx, NULL, t17, &t19);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t21 = gm_f_push_mjiecfq(t16, &t20);
                if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srck6bk8q21r07v, 640, 35);
                gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
            }
            goto ix12;
ie12:;
ix12:;
        }
        {
            void * t24 = GM_LD(void *, (((uint8_t *)&F.l2) + 40));
            if (!(GM_LD(void *, ((uint8_t *)&t24)) != 0)) goto ie23;
            void * t25 = GM_LD(void *, ((uint8_t *)&t24));
            gm_l4 = t25;
            {
                void * t27 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t28;
                GM_ST(void *, (uint8_t *)&t28, (void *)(gm_s5qkai093c1q0));
                GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(5));
                void * t29 = gm_l4;
                gm_tvul7d1mdc2eq t30;
                gm_f_dump_expr_l3bbdps((void *)&t30, (void *)NULL, cx, NULL, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tvul7d1mdc2eq t31;
                gm_f_node1_gpsjtpc((void *)&t31, (void *)NULL, cx, NULL, t28, &t30);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t32 = gm_f_push_mjiecfq(t27, &t31);
                if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 1, gm_srck6bk8q21r07v, 641, 33);
                gm_b0_1 t33 = GM_LD(gm_b0_1, (((uint8_t *)&t32) + 1));
            }
            goto ix23;
ie23:;
ix23:;
        }
        {
            gm_b16_8 t34;
            GM_ST(void *, (uint8_t *)&t34, (void *)(gm_spj9t9b8lqlpp));
            GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(7));
            gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t34, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn4:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t36 != ((uint8_t)1ULL)) goto mn35;
        gm_t7bjbmukv26e7 t37 = GM_LD(gm_t7bjbmukv26e7, (((uint8_t *)t2) + 8));
        F.l5 = t37;
        {
            void * t39 = (void *)((uint8_t *)&F.l1);
            void * t40 = GM_LD(void *, (((uint8_t *)&F.l5) + 0));
            gm_tvul7d1mdc2eq t41;
            gm_f_dump_expr_l3bbdps((void *)&t41, (void *)NULL, cx, NULL, t40);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t42 = gm_f_push_mjiecfq(t39, &t41);
            if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srck6bk8q21r07v, 645, 13);
            gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
        }
        {
            gm_f_list_ccsq5cl((void *)&F.l6, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t45 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 8));
                gm_l7 = t45;
            }
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t47 = gm_l8;
                    uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint8_t t49 = (t47 < t48);
                    if (!t49) goto lb46;
                    {
                        uint64_t t52 = gm_l8;
                        uint64_t t53 = (uintptr_t)t52;
                        uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                        gm_t03a3imodqi32 t57 = GM_LD(gm_t03a3imodqi32, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t53 * 72));
                        F.l9 = t57;
                        {
                            void * t59 = (void *)((uint8_t *)&F.l6);
                            void * t60 = (void *)((uint8_t *)&F.l9);
                            gm_tvul7d1mdc2eq t61;
                            gm_f_dump_union_member_4lopa45((void *)&t61, (void *)NULL, cx, NULL, t60);
                            if (gm_unw.kind) {
                                goto gm_ll50;
                            }
                            gm_tftb1odc739t5 t62 = gm_f_push_mjiecfq(t59, &t61);
                            if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srck6bk8q21r07v, 647, 31);
                            gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
                        }
                        goto me55;
mn56:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 647, 13);
me55:;
                    }
lc46:;
                    {
                        uint64_t t65 = gm_l8;
                        uint64_t r66; bool o67 = __builtin_add_overflow(t65, ((uint64_t)1ULL), &r66);
                        uint64_t t68 = r66;
                        gm_l8 = t68;
                    }
                }
gm_ll50:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb46; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc46; }
                }
                goto gm_lad;
lb46:;
            }
        }
        {
            void * t69 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sofbtko7t8taa));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(4));
            gm_tvul7d1mdc2eq t71;
            gm_f_bnode_hu9fd4q((void *)&t71, (void *)NULL, cx, NULL, t70, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l6)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t72 = gm_f_push_mjiecfq(t69, &t71);
            if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_srck6bk8q21r07v, 648, 13);
            gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
        }
        {
            if (!(((((uint8_t *)&F.l5) + 24))[16] != 0)) goto ie74;
            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 24));
            gm_l10 = t75;
            {
                gm_f_list_ccsq5cl((void *)&F.l11, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t78 = gm_l10;
                    gm_l12 = t78;
                }
                {
                    gm_l13 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t80 = gm_l13;
                        uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        uint8_t t82 = (t80 < t81);
                        if (!t82) goto lb79;
                        {
                            uint64_t t85 = gm_l13;
                            uint64_t t86 = (uintptr_t)t85;
                            uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            gm_t03a3imodqi32 t90 = GM_LD(gm_t03a3imodqi32, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t86 * 72));
                            F.l14 = t90;
                            {
                                void * t92 = (void *)((uint8_t *)&F.l11);
                                void * t93 = (void *)((uint8_t *)&F.l14);
                                gm_tvul7d1mdc2eq t94;
                                gm_f_dump_union_member_4lopa45((void *)&t94, (void *)NULL, cx, NULL, t93);
                                if (gm_unw.kind) {
                                    goto gm_ll83;
                                }
                                gm_tftb1odc739t5 t95 = gm_f_push_mjiecfq(t92, &t94);
                                if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_srck6bk8q21r07v, 651, 36);
                                gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
                            }
                            goto me88;
mn89:;
                            gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 651, 17);
me88:;
                        }
lc79:;
                        {
                            uint64_t t98 = gm_l13;
                            uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                            uint64_t t101 = r99;
                            gm_l13 = t101;
                        }
                    }
gm_ll83:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb79; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc79; }
                    }
                    goto gm_lad;
lb79:;
                }
            }
            {
                void * t102 = (void *)((uint8_t *)&F.l1);
                gm_b16_8 t103;
                GM_ST(void *, (uint8_t *)&t103, (void *)(gm_s9es2tos1bhfp));
                GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(4));
                gm_tvul7d1mdc2eq t104;
                gm_f_bnode_hu9fd4q((void *)&t104, (void *)NULL, cx, NULL, t103, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l11)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tftb1odc739t5 t105 = gm_f_push_mjiecfq(t102, &t104);
                if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 1, gm_srck6bk8q21r07v, 652, 17);
                gm_b0_1 t106 = GM_LD(gm_b0_1, (((uint8_t *)&t105) + 1));
            }
            goto ix74;
ie74:;
ix74:;
        }
        {
            gm_b16_8 t107;
            GM_ST(void *, (uint8_t *)&t107, (void *)(gm_s11b339fv56g1));
            GM_ST(uintptr_t, (uint8_t *)&t107 + 8, (uintptr_t)(12));
            gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t107, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me3;
mn35:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 637, 5);
me3:;
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
inline __attribute__((aligned(4))) void gm_f_dump_when_items_16tgm3i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_when_items_16tgm3i F;
    void * gm_l0; /* w */
    gm_b16_8 gm_l2; /* e */
    GM_STACK_CHECK(&F, 310u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        gm_tvul7d1mdc2eq t5;
        gm_f_dump_expr_l3bbdps((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t6 = gm_f_push_mjiecfq(t2, &t5);
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srck6bk8q21r07v, 225, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        void * t8 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sofbtko7t8taa));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(4));
        void * t10 = gm_l0;
        gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)t10) + 8));
        gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
        gm_tvul7d1mdc2eq t13;
        gm_f_dump_items_atcioo9((void *)&t13, (void *)NULL, cx, NULL, t9, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t14 = gm_f_push_mjiecfq(t8, &t13);
        if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_srck6bk8q21r07v, 226, 5);
        gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
    }
    {
        void * t17 = gm_l0;
        if (!(((((uint8_t *)t17) + 24))[16] != 0)) goto ie16;
        gm_b16_8 t18 = GM_LD(gm_b16_8, (((uint8_t *)t17) + 24));
        gm_l2 = t18;
        {
            void * t20 = (void *)((uint8_t *)&F.l1);
            gm_b16_8 t21;
            GM_ST(void *, (uint8_t *)&t21, (void *)(gm_s9es2tos1bhfp));
            GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(4));
            gm_b16_8 t22 = gm_l2;
            gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
            gm_tvul7d1mdc2eq t24;
            gm_f_dump_items_atcioo9((void *)&t24, (void *)NULL, cx, NULL, t21, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tftb1odc739t5 t25 = gm_f_push_mjiecfq(t20, &t24);
            if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_srck6bk8q21r07v, 227, 23);
            gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
        }
        goto ix16;
ie16:;
ix16:;
    }
    {
        gm_b16_8 t27;
        GM_ST(void *, (uint8_t *)&t27, (void *)(gm_sgmcjv273u32m));
        GM_ST(uintptr_t, (uint8_t *)&t27 + 8, (uintptr_t)(10));
        gm_f_bnode_hu9fd4q((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t27, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
inline __attribute__((aligned(4))) void gm_f_dump_when_stmt_hdq14ev(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_dump_when_stmt_hdq14ev F;
    void * gm_l0; /* w */
    void * gm_l3; /* inner */
    GM_STACK_CHECK(&F, 402u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_f_list_ccsq5cl((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        gm_tvul7d1mdc2eq t5;
        gm_f_dump_expr_l3bbdps((void *)&t5, (void *)NULL, cx, NULL, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t6 = gm_f_push_mjiecfq(t2, &t5);
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srck6bk8q21r07v, 322, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        void * t8 = (void *)((uint8_t *)&F.l1);
        void * t9 = gm_l0;
        void * t10 = (void *)(((uint8_t *)t9) + 8);
        gm_tvul7d1mdc2eq t11;
        gm_f_dump_block_5utjqg0((void *)&t11, (void *)NULL, cx, NULL, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t12 = gm_f_push_mjiecfq(t8, &t11);
        if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srck6bk8q21r07v, 323, 5);
        gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
    }
    {
        void * t14 = gm_l0;
        if ((((((uint8_t *)t14) + 48))[48] != 0)) goto mn16;
        goto me15;
mn16:;
        if (!(((((uint8_t *)t14) + 48))[48] != 0)) goto mn18;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t14) + 48) + 0));
        if (t19 != ((uint8_t)0ULL)) goto mn18;
        gm_t79rbdv4rafe5 t20 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t14) + 48) + 8));
        F.l2 = t20;
        void * t21 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t22;
        GM_ST(void *, (uint8_t *)&t22, (void *)(gm_s9es2tos1bhfp));
        GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(4));
        void * t23 = (void *)((uint8_t *)&F.l2);
        gm_tvul7d1mdc2eq t24;
        gm_f_dump_block_5utjqg0((void *)&t24, (void *)NULL, cx, NULL, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvul7d1mdc2eq t25;
        gm_f_node1_gpsjtpc((void *)&t25, (void *)NULL, cx, NULL, t22, &t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t26 = gm_f_push_mjiecfq(t21, &t25);
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srck6bk8q21r07v, 326, 22);
        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
        goto me15;
mn18:;
        if (!(((((uint8_t *)t14) + 48))[48] != 0)) goto mn28;
        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t14) + 48) + 0));
        if (t29 != ((uint8_t)1ULL)) goto mn28;
        void * t30 = GM_LD(void *, ((((uint8_t *)t14) + 48) + 8));
        gm_l3 = t30;
        void * t31 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t32;
        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s9es2tos1bhfp));
        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(4));
        void * t33 = gm_l3;
        gm_tvul7d1mdc2eq t34;
        gm_f_dump_when_stmt_hdq14ev((void *)&t34, (void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvul7d1mdc2eq t35;
        gm_f_node1_gpsjtpc((void *)&t35, (void *)NULL, cx, NULL, t32, &t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tftb1odc739t5 t36 = gm_f_push_mjiecfq(t31, &t35);
        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 1, gm_srck6bk8q21r07v, 327, 25);
        gm_b0_1 t37 = GM_LD(gm_b0_1, (((uint8_t *)&t36) + 1));
        goto me15;
mn28:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 324, 5);
me15:;
    }
    {
        gm_b16_8 t38;
        GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s4dis7c905evg));
        GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(9));
        gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t38, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l1)));
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
__attribute__((aligned(4))) uint8_t gm_f_fits_mco2o3v(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fits_mco2o3v F;
    void * gm_l0; /* sx */
    void * gm_l1; /* budget */
    gm_b16_8 gm_l2; /* s */
    gm_b16_8 gm_l3; /* items */
    uint8_t gm_l4; /* brk */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tvul7d1mdc2eq gm_l7; /* item */
    GM_STACK_CHECK(&F, 466u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t5 != ((uint8_t)0ULL)) goto mn4;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 8));
        gm_l2 = t6;
        {
            void * t8 = gm_l1;
            uint8_t *t9 = ((uint8_t *)t8);
            void * t10 = gm_l1;
            int64_t t11 = GM_LD(int64_t, ((uint8_t *)t10));
            gm_b16_8 t12 = gm_l2;
            uint64_t t13 = gm_f_char_count_76unqru(t12);
            int64_t t14 = (int64_t)t13;
            int64_t r15; bool o16 = __builtin_sub_overflow(t11, t14, &r15);
            if (o16) gm_trap("integer overflow in `-`", gm_srck6bk8q21r07v, 66, 13);
            int64_t t17 = r15;
            GM_ST(int64_t, t9, t17);
        }
        {
            void * t18 = gm_l1;
            int64_t t19 = GM_LD(int64_t, ((uint8_t *)t18));
            uint8_t t20 = (t19 >= ((int64_t)0LL));
            uint8_t t21 = 0;
            F.ret = t20;
            goto gm_fret;
        }
        goto me3;
mn4:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t23 != ((uint8_t)1ULL)) goto mn22;
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 0));
        gm_l3 = t24;
        uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 16));
        gm_l4 = t25;
        {
            uint8_t t28 = gm_l4;
            if (!t28) goto ie27;
            uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint8_t t30 = (t29 > ((uint64_t)1ULL));
            if (!t30) goto ie27;
            {
                uint8_t t32 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix27;
ie27:;
ix27:;
        }
        {
            void * t33 = gm_l1;
            void * t34 = gm_l1;
            int64_t t35 = GM_LD(int64_t, ((uint8_t *)t34));
            uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t t37 = (t36 > ((uint64_t)1ULL) ? t36 : ((uint64_t)1ULL));
            int64_t t38 = (int64_t)t37;
            int64_t r39; bool o40 = __builtin_add_overflow(((int64_t)2LL), t38, &r39);
            if (o40) gm_trap("integer overflow in `+`", gm_srck6bk8q21r07v, 71, 24);
            int64_t t41 = r39;
            int64_t r42; bool o43 = __builtin_sub_overflow(t41, ((int64_t)1LL), &r42);
            if (o43) gm_trap("integer overflow in `-`", gm_srck6bk8q21r07v, 71, 24);
            int64_t t44 = r42;
            int64_t r45; bool o46 = __builtin_sub_overflow(t35, t44, &r45);
            if (o46) gm_trap("integer overflow in `-`", gm_srck6bk8q21r07v, 71, 13);
            int64_t t47 = r45;
            GM_ST(int64_t, ((uint8_t *)t33), t47);
        }
        {
            void * t49 = gm_l1;
            int64_t t50 = GM_LD(int64_t, ((uint8_t *)t49));
            uint8_t t51 = (t50 < ((int64_t)0LL));
            if (!t51) goto ie48;
            {
                uint8_t t53 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix48;
ie48:;
ix48:;
        }
        {
            {
                gm_b16_8 t55 = gm_l3;
                gm_l5 = t55;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t57 = gm_l6;
                    uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t59 = (t57 < t58);
                    if (!t59) goto lb56;
                    {
                        uint64_t t62 = gm_l6;
                        uint64_t t63 = (uintptr_t)t62;
                        uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        gm_tvul7d1mdc2eq t67 = GM_LD(gm_tvul7d1mdc2eq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t63 * 32));
                        gm_l7 = t67;
                        {
                            void * t70 = (void *)((uint8_t *)&gm_l7);
                            void * t71 = gm_l1;
                            uint8_t t72 = gm_f_fits_mco2o3v(t70, t71);
                            uint8_t t73 = (uint8_t)!t72;
                            if (!t73) goto ie69;
                            {
                                uint8_t t75 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix69;
ie69:;
ix69:;
                        }
                        goto me65;
mn66:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 73, 13);
me65:;
                    }
lc56:;
                    {
                        uint64_t t77 = gm_l6;
                        uint64_t r78; bool o79 = __builtin_add_overflow(t77, ((uint64_t)1ULL), &r78);
                        uint64_t t80 = r78;
                        gm_l6 = t80;
                    }
                }
lb56:;
            }
        }
        {
            uint8_t t81 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me3;
mn22:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 64, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_flat_hstd39j(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_flat_hstd39j F;
    void * gm_l0; /* sx */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* s */
    gm_b16_8 gm_l3; /* items */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_tvul7d1mdc2eq gm_l7; /* item */
    GM_STACK_CHECK(&F, 380u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t5 != ((uint8_t)0ULL)) goto mn4;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 8));
        gm_l2 = t6;
        void * t7 = gm_l1;
        gm_b16_8 t8 = gm_l2;
        gm_tftb1odc739t5 t9 = gm_f_extend_0cs85gs(t7, t8);
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srck6bk8q21r07v, 83, 21);
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
        goto me3;
mn4:;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t2) + 0));
        if (t12 != ((uint8_t)1ULL)) goto mn11;
        gm_b16_8 t13 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 0));
        gm_l3 = t13;
        {
            void * t15 = gm_l1;
            gm_tftb1odc739t5 t16 = gm_f_push_t83rg2m(t15, ((uint8_t)40ULL));
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 1, gm_srck6bk8q21r07v, 85, 13);
            gm_b0_1 t17 = GM_LD(gm_b0_1, (((uint8_t *)&t16) + 1));
        }
        {
            {
                gm_b16_8 t19 = gm_l3;
                gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
                gm_l4 = t20;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t22 = gm_l5;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t24 = (t22 < t23);
                    if (!t24) goto lb21;
                    {
                        gm_toho9hmgj287v t27;
                        uint64_t t28 = gm_l5;
                        GM_ST(uint64_t, (uint8_t *)&t27 + 0, t28);
                        uint64_t t29 = gm_l5;
                        uint64_t t30 = (uintptr_t)t29;
                        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        memcpy((uint8_t *)&t27 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t30 * 32), 32);
                        uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&t27) + 0));
                        gm_l6 = t34;
                        gm_l7 = (*(gm_tvul7d1mdc2eq *)(((uint8_t *)&t27) + 8));
                        {
                            uint64_t t37 = gm_l6;
                            uint8_t t38 = (t37 > ((uint64_t)0ULL));
                            if (!t38) goto ie36;
                            {
                                void * t40 = gm_l1;
                                gm_tftb1odc739t5 t41 = gm_f_push_t83rg2m(t40, ((uint8_t)32ULL));
                                if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srck6bk8q21r07v, 87, 28);
                                gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                            }
                            goto ix36;
ie36:;
ix36:;
                        }
                        {
                            void * t43 = (void *)((uint8_t *)&gm_l7);
                            void * t44 = gm_l1;
                            gm_b0_1 t45 = gm_f_flat_hstd39j((void *)NULL, cx, NULL, t43, t44);
                            if (gm_unw.kind) {
                                goto gm_ll25;
                            }
                        }
                        goto me32;
mn33:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 86, 13);
me32:;
                    }
lc21:;
                    {
                        uint64_t t47 = gm_l5;
                        uint64_t r48; bool o49 = __builtin_add_overflow(t47, ((uint64_t)1ULL), &r48);
                        uint64_t t50 = r48;
                        gm_l5 = t50;
                    }
                }
gm_ll25:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb21; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc21; }
                }
                goto gm_lad;
lb21:;
            }
        }
        {
            void * t51 = gm_l1;
            gm_tftb1odc739t5 t52 = gm_f_push_t83rg2m(t51, ((uint8_t)41ULL));
            if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srck6bk8q21r07v, 90, 13);
            gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
        }
        goto me3;
mn11:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 82, 5);
me3:;
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_float_suffix_7g3bh9c(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_float_suffix_7g3bh9c F;
    gm_t9qkh0rsooc8d gm_l0; /* s */
    GM_STACK_CHECK_SHALLOW(&F, 101u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_shnig5tqtdedi));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(3));
    t1 = t10;
    goto me2;
mn8:;
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)1ULL)) goto mn11;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sm9e81djuncnu));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(3));
    t1 = t13;
    goto me2;
mn11:;
ma5:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t15 != ((uint8_t)2ULL)) goto mn14;
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sqqajgod2bb4o));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(3));
    t1 = t16;
    goto me2;
mn14:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 151, 56);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) void gm_f_ident_sx_kd0v352(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_tvul7d1mdc2eq gm_f_ident_sx_kd0v352_c(gm_t08olq97rscn3 a0) { gm_tvul7d1mdc2eq gm_r; gm_f_ident_sx_kd0v352(&gm_r, &a0); return gm_r; }
inline __attribute__((aligned(4))) void gm_f_make_list_cicccjr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint8_t a2) {
    struct gm_fr_make_list_cicccjr F;
    gm_b16_8 gm_l0; /* head */
    uint8_t gm_l2; /* brk */
    GM_STACK_CHECK(&F, 158u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        gm_f_list_ccsq5cl((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t3 = gm_l0;
        gm_tvul7d1mdc2eq t4;
        GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t4 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t4 + 24, 0);
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)0ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t4 + 8), t3);
        gm_tftb1odc739t5 t5 = gm_f_push_mjiecfq(t2, &t4);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srck6bk8q21r07v, 42, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    {
        void * t7 = (void *)((uint8_t *)&F.l3);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_tftb1odc739t5 t10 = gm_f_extend_i72a9oi(t7, t9);
        if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 1, gm_srck6bk8q21r07v, 43, 5);
        gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
    }
    {
        gm_th2lqbo77pvsc t12;
        memset((uint8_t *)&t12 + 17, 0, 7);
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t12 + 0, t13);
        uint8_t t14 = gm_l2;
        GM_ST(uint8_t, (uint8_t *)&t12 + 16, t14);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 0, ((uint8_t)1ULL));
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_tvul7d1mdc2eq *)gm_out) + 8, t12);
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
extern __attribute__((aligned(4))) void gm_f_node1_gpsjtpc(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_node2_b4qpqat(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, const void * a2) {
    struct gm_fr_node2_b4qpqat F;
    gm_b16_8 gm_l0; /* head */
    gm_tvul7d1mdc2eq gm_l1; /* x */
    gm_tvul7d1mdc2eq gm_l2; /* y */
    GM_STACK_CHECK(&F, 116u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    memcpy(&gm_l2, a2, sizeof gm_l2);
    {
        gm_f_list_ccsq5cl((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l3);
        gm_tftb1odc739t5 t3 = gm_f_push_mjiecfq(t2, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l1)));
        if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 1, gm_srck6bk8q21r07v, 57, 5);
        gm_b0_1 t4 = GM_LD(gm_b0_1, (((uint8_t *)&t3) + 1));
    }
    {
        void * t5 = (void *)((uint8_t *)&F.l3);
        gm_tftb1odc739t5 t6 = gm_f_push_mjiecfq(t5, &(*(gm_tvul7d1mdc2eq *)((uint8_t *)&gm_l2)));
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 1, gm_srck6bk8q21r07v, 58, 5);
        gm_b0_1 t7 = GM_LD(gm_b0_1, (((uint8_t *)&t6) + 1));
    }
    {
        gm_b16_8 t8 = gm_l0;
        gm_f_node_dm1r8dn((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t8, &(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3)));
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
extern __attribute__((aligned(4))) void gm_f_node_dm1r8dn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_push_attrs_a5l439c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_push_attrs_a5l439c F;
    void * gm_l0; /* v */
    gm_b16_8 gm_l1; /* attrs */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 243u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        {
            gm_b16_8 t3 = gm_l1;
            gm_l2 = t3;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l3;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l3;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_tkacitepg5fk0 t15 = GM_LD(gm_tkacitepg5fk0, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 72));
                    F.l4 = t15;
                    {
                        void * t17 = gm_l0;
                        void * t18 = (void *)((uint8_t *)&F.l4);
                        gm_tvul7d1mdc2eq t19;
                        gm_f_dump_attr_qdd7k06((void *)&t19, (void *)NULL, cx, NULL, t18);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_tftb1odc739t5 t20 = gm_f_push_mjiecfq(t17, &t19);
                        if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 1, gm_srck6bk8q21r07v, 190, 22);
                        gm_b0_1 t21 = GM_LD(gm_b0_1, (((uint8_t *)&t20) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 190, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t23 = gm_l3;
                    uint64_t r24; bool o25 = __builtin_add_overflow(t23, ((uint64_t)1ULL), &r24);
                    uint64_t t26 = r24;
                    gm_l3 = t26;
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
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_render_7k3fqtk(void *env, gm_node *cx, gm_node *inst, void * a0, int64_t a1, void * a2) {
    struct gm_fr_render_7k3fqtk F;
    void * gm_l0; /* sx */
    int64_t gm_l1; /* indent */
    void * gm_l2; /* out */
    int64_t gm_l3; /* budget */
    gm_b16_8 gm_l4; /* items */
    uint8_t gm_l5; /* brk */
    int64_t gm_l6; /* col */
    uint8_t gm_l7; /* inline */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_tvul7d1mdc2eq gm_l11; /* item */
    int64_t gm_l12; /* room */
    int64_t gm_l14; /* <tmp> */
    int64_t gm_l15; /* <tmp> */
    GM_STACK_CHECK(&F, 744u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        int64_t t2 = GM_LD(int64_t, gm_cddg9214lam);
        int64_t t3 = gm_l1;
        int64_t r4; bool o5 = __builtin_sub_overflow(t2, t3, &r4);
        if (o5) gm_trap("integer overflow in `-`", gm_srck6bk8q21r07v, 96, 15);
        int64_t t6 = r4;
        gm_l3 = t6;
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)((uint8_t *)&gm_l3);
        uint8_t t10 = gm_f_fits_mco2o3v(t8, t9);
        if (!t10) goto ie7;
        {
            void * t12 = gm_l0;
            void * t13 = gm_l2;
            gm_b0_1 t14 = gm_f_flat_hstd39j((void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b0_1 t15;
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        void * t18 = gm_l0;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)t18) + 0));
        if (t19 != ((uint8_t)1ULL)) goto ie17;
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((((uint8_t *)t18) + 8) + 0));
        gm_l4 = t20;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 16));
        gm_l5 = t21;
        {
            void * t23 = gm_l2;
            gm_tftb1odc739t5 t24 = gm_f_push_t83rg2m(t23, ((uint8_t)40ULL));
            if (((((uint8_t *)&t24))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t24) + 1, gm_srck6bk8q21r07v, 104, 5);
            gm_b0_1 t25 = GM_LD(gm_b0_1, (((uint8_t *)&t24) + 1));
        }
        {
            int64_t t26 = gm_l1;
            int64_t r27; bool o28 = __builtin_add_overflow(t26, ((int64_t)1LL), &r27);
            if (o28) gm_trap("integer overflow in `+`", gm_srck6bk8q21r07v, 105, 12);
            int64_t t29 = r27;
            gm_l6 = t29;
        }
        {
            gm_l7 = ((uint8_t)1);
        }
        {
            {
                gm_b16_8 t31 = gm_l4;
                gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                gm_l8 = t32;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t34 = gm_l9;
                    uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t36 = (t34 < t35);
                    if (!t36) goto lb33;
                    {
                        gm_toho9hmgj287v t39;
                        uint64_t t40 = gm_l9;
                        GM_ST(uint64_t, (uint8_t *)&t39 + 0, t40);
                        uint64_t t41 = gm_l9;
                        uint64_t t42 = (uintptr_t)t41;
                        uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        memcpy((uint8_t *)&t39 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t42 * 32), 32);
                        uint64_t t46 = GM_LD(uint64_t, (((uint8_t *)&t39) + 0));
                        gm_l10 = t46;
                        gm_l11 = (*(gm_tvul7d1mdc2eq *)(((uint8_t *)&t39) + 8));
                        {
                            uint8_t t49 = gm_l7;
                            if (!t49) goto ie48;
                            {
                                int64_t t51 = GM_LD(int64_t, gm_cddg9214lam);
                                int64_t t52 = gm_l6;
                                int64_t r53; bool o54 = __builtin_sub_overflow(t51, t52, &r53);
                                if (o54) gm_trap("integer overflow in `-`", gm_srck6bk8q21r07v, 109, 21);
                                int64_t t55 = r53;
                                int64_t r56; bool o57 = __builtin_sub_overflow(t55, ((int64_t)1LL), &r56);
                                if (o57) gm_trap("integer overflow in `-`", gm_srck6bk8q21r07v, 109, 21);
                                int64_t t58 = r56;
                                gm_l12 = t58;
                            }
                            {
                                uint8_t t60 = 0;
                                uint8_t t61;
                                uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l11) + 0));
                                if (t64 != ((uint8_t)0ULL)) goto mn63;
                                t61 = ((uint8_t)1);
                                goto me62;
mn63:;
                                t61 = ((uint8_t)0);
                                goto me62;
mn65:;
                                gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 110, 16);
me62:;
                                if (!t61) {
                                    uint8_t t66 = 0;
                                    uint8_t t67 = 0;
                                    uint8_t t68 = gm_l5;
                                    uint8_t t69 = (uint8_t)!t68;
                                    if (t69) {
                                        uint64_t t70 = gm_l10;
                                        uint8_t t71 = (t70 > ((uint64_t)0ULL));
                                        t67 = t71;
                                    } else t67 = 0;
                                    if (t67) {
                                        void * t72 = (void *)((uint8_t *)&gm_l11);
                                        void * t73 = (void *)((uint8_t *)&gm_l12);
                                        uint8_t t74 = gm_f_fits_mco2o3v(t72, t73);
                                        t66 = t74;
                                    } else t66 = 0;
                                    t60 = t66;
                                } else t60 = 1;
                                if (!t60) goto ie59;
                                {
                                    gm_f_list_uvfu1k1((void *)&F.l13, (void *)NULL, cx, NULL);
                                    if (gm_unw.kind) {
                                        goto gm_ll37;
                                    }
                                }
                                {
                                    void * t76 = (void *)((uint8_t *)&gm_l11);
                                    void * t77 = (void *)((uint8_t *)&F.l13);
                                    gm_b0_1 t78 = gm_f_flat_hstd39j((void *)NULL, cx, NULL, t76, t77);
                                    if (gm_unw.kind) {
                                        goto gm_ll37;
                                    }
                                }
                                {
                                    uint64_t t80 = gm_l10;
                                    uint8_t t81 = (t80 > ((uint64_t)0ULL));
                                    if (!t81) goto ie79;
                                    {
                                        void * t83 = gm_l2;
                                        gm_tftb1odc739t5 t84 = gm_f_push_t83rg2m(t83, ((uint8_t)32ULL));
                                        if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 1, gm_srck6bk8q21r07v, 114, 21);
                                        gm_b0_1 t85 = GM_LD(gm_b0_1, (((uint8_t *)&t84) + 1));
                                    }
                                    {
                                        int64_t t86 = gm_l6;
                                        int64_t r87; bool o88 = __builtin_add_overflow(t86, ((int64_t)1LL), &r87);
                                        if (o88) gm_trap("integer overflow in `+`", gm_srck6bk8q21r07v, 115, 21);
                                        int64_t t89 = r87;
                                        gm_l6 = t89;
                                    }
                                    goto ix79;
ie79:;
ix79:;
                                }
                                {
                                    int64_t t90 = gm_l6;
                                    gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                                    gm_b16_8 t92 = GM_LD(gm_b16_8, ((uint8_t *)&t91));
                                    uint64_t t93 = gm_f_char_count_76unqru(t92);
                                    int64_t t94 = (int64_t)t93;
                                    int64_t r95; bool o96 = __builtin_add_overflow(t90, t94, &r95);
                                    if (o96) gm_trap("integer overflow in `+`", gm_srck6bk8q21r07v, 117, 17);
                                    int64_t t97 = r95;
                                    gm_l6 = t97;
                                }
                                {
                                    void * t98 = gm_l2;
                                    gm_b16_8 t99 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                                    gm_b16_8 t100 = GM_LD(gm_b16_8, ((uint8_t *)&t99));
                                    gm_tftb1odc739t5 t101 = gm_f_extend_0cs85gs(t98, t100);
                                    if (((((uint8_t *)&t101))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t101) + 1, gm_srck6bk8q21r07v, 118, 17);
                                    gm_b0_1 t102 = GM_LD(gm_b0_1, (((uint8_t *)&t101) + 1));
                                }
                                {
                                    goto lc33;
                                }
                                goto ix59;
ie59:;
ix59:;
                            }
                            {
                                gm_l7 = ((uint8_t)0);
                            }
                            goto ix48;
ie48:;
ix48:;
                        }
                        {
                            void * t103 = gm_l2;
                            gm_tftb1odc739t5 t104 = gm_f_push_t83rg2m(t103, ((uint8_t)10ULL));
                            if (((((uint8_t *)&t104))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t104) + 1, gm_srck6bk8q21r07v, 123, 9);
                            gm_b0_1 t105 = GM_LD(gm_b0_1, (((uint8_t *)&t104) + 1));
                        }
                        {
                            {
                                gm_l15 = ((int64_t)0LL);
                            }
                            {
                                int64_t t107 = gm_l1;
                                int64_t r108; bool o109 = __builtin_add_overflow(t107, ((int64_t)2LL), &r108);
                                if (o109) gm_trap("integer overflow in `+`", gm_srck6bk8q21r07v, 124, 21);
                                int64_t t110 = r108;
                                gm_l14 = t110;
                            }
                            {
                                for (;;) {
                                    int64_t t112 = gm_l15;
                                    int64_t t113 = gm_l14;
                                    uint8_t t114 = (t112 < t113);
                                    if (!t114) goto lb111;
                                    {
                                        int64_t t117 = gm_l15;
                                        {
                                            void * t121 = gm_l2;
                                            gm_tftb1odc739t5 t122 = gm_f_push_t83rg2m(t121, ((uint8_t)32ULL));
                                            if (((((uint8_t *)&t122))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t122) + 1, gm_srck6bk8q21r07v, 124, 34);
                                            gm_b0_1 t123 = GM_LD(gm_b0_1, (((uint8_t *)&t122) + 1));
                                        }
                                        goto me118;
mn119:;
                                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 124, 9);
me118:;
                                    }
lc111:;
                                    {
                                        int64_t t125 = gm_l15;
                                        int64_t r126; bool o127 = __builtin_add_overflow(t125, ((int64_t)1LL), &r126);
                                        int64_t t128 = r126;
                                        gm_l15 = t128;
                                    }
                                }
lb111:;
                            }
                        }
                        {
                            void * t129 = (void *)((uint8_t *)&gm_l11);
                            int64_t t130 = gm_l1;
                            int64_t r131; bool o132 = __builtin_add_overflow(t130, ((int64_t)2LL), &r131);
                            if (o132) gm_trap("integer overflow in `+`", gm_srck6bk8q21r07v, 125, 23);
                            int64_t t133 = r131;
                            void * t134 = gm_l2;
                            gm_b0_1 t135 = gm_f_render_7k3fqtk((void *)NULL, cx, NULL, t129, t133, t134);
                            if (gm_unw.kind) {
                                goto gm_ll37;
                            }
                        }
                        goto me44;
mn45:;
                        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 107, 5);
me44:;
                    }
lc33:;
                    {
                        uint64_t t137 = gm_l9;
                        uint64_t r138; bool o139 = __builtin_add_overflow(t137, ((uint64_t)1ULL), &r138);
                        uint64_t t140 = r138;
                        gm_l9 = t140;
                    }
                }
gm_ll37:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb33; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc33; }
                }
                goto gm_lad;
lb33:;
            }
        }
        {
            void * t141 = gm_l2;
            gm_tftb1odc739t5 t142 = gm_f_push_t83rg2m(t141, ((uint8_t)41ULL));
            if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 1, gm_srck6bk8q21r07v, 127, 5);
            gm_b0_1 t143 = GM_LD(gm_b0_1, (((uint8_t *)&t142) + 1));
        }
        goto ix17;
ie17:;
        {
            gm_b0_1 t145;
            uint8_t t146 = 0;
            F.ret = t145;
            goto gm_fret;
        }
ix17:;
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_str_lit_a2k13t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_str_lit_a2k13t3 F;
    gm_tibupt4dmcqai gm_l0; /* s */
    gm_b16_8 gm_l1; /* head */
    GM_STACK_CHECK(&F, 188u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&gm_l0, a0, sizeof gm_l0);
    {
        gm_b16_8 t2;
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 16) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; default: goto ma7; }
ma4:;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 16) + 0));
        if (t10 != ((uint8_t)0ULL)) goto mn9;
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s6pilvbjelm5p));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(3));
        t2 = t11;
        goto me3;
mn9:;
ma5:;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 16) + 0));
        if (t13 != ((uint8_t)1ULL)) goto mn12;
        gm_b16_8 t14;
        GM_ST(void *, (uint8_t *)&t14, (void *)(gm_sltau19rt9o73));
        GM_ST(uintptr_t, (uint8_t *)&t14 + 8, (uintptr_t)(7));
        t2 = t14;
        goto me3;
mn12:;
ma6:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)&gm_l0) + 16) + 0));
        if (t16 != ((uint8_t)2ULL)) goto mn15;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_squ5nrnqo0b0l));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(5));
        t2 = t17;
        goto me3;
mn15:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 143, 13);
me3:;
        gm_l1 = t2;
    }
    {
        gm_b16_8 t18 = gm_l1;
        gm_b16_8 t19 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l0) + 0));
        gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
        gm_tvul7d1mdc2eq t21;
        gm_f_atom_of_bytes_dj32elq((void *)&t21, (void *)NULL, cx, NULL, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_node1_gpsjtpc((void *)&(*(gm_tvul7d1mdc2eq *)gm_out), (void *)NULL, cx, NULL, t18, &t21);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unop_name_j99tcv9(gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unop_name_j99tcv9 F;
    gm_t9qkh0rsooc8d gm_l0; /* op */
    GM_STACK_CHECK_SHALLOW(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t10) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma8; default: goto ma9; }
ma3:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)0ULL)) goto mn11;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_seb76g152eseg));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(3));
    t1 = t13;
    goto me2;
mn11:;
ma4:;
    uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t15 != ((uint8_t)1ULL)) goto mn14;
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(gm_sgs4qi36v3o5v));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(7));
    t1 = t16;
    goto me2;
mn14:;
ma5:;
    uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t18 != ((uint8_t)2ULL)) goto mn17;
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sukutl224cgkm));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(7));
    t1 = t19;
    goto me2;
mn17:;
ma6:;
    uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t21 != ((uint8_t)3ULL)) goto mn20;
    gm_b16_8 t22;
    GM_ST(void *, (uint8_t *)&t22, (void *)(gm_sn581p3lbgrjg));
    GM_ST(uintptr_t, (uint8_t *)&t22 + 8, (uintptr_t)(8));
    t1 = t22;
    goto me2;
mn20:;
ma7:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t24 != ((uint8_t)4ULL)) goto mn23;
    gm_b16_8 t25;
    GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s4fd35uidvgkn));
    GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(3));
    t1 = t25;
    goto me2;
mn23:;
ma8:;
    uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t27 != ((uint8_t)5ULL)) goto mn26;
    gm_b16_8 t28;
    GM_ST(void *, (uint8_t *)&t28, (void *)(gm_scvhpklutvpjg));
    GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(3));
    t1 = t28;
    goto me2;
mn26:;
ma9:;
    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 164, 40);
me2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_visibility_u2ipb12(void *gm_out, gm_t9qkh0rsooc8d a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_visibility_u2ipb12 F;
    gm_t9qkh0rsooc8d gm_l0; /* vis */
    GM_STACK_CHECK_SHALLOW(&F, 133u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_th2lqbo77pvsc t1;
    uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; default: goto ma6; }
ma3:;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t9 != ((uint8_t)0ULL)) goto mn8;
    gm_th2lqbo77pvsc t10;
    memset(&t10, 0, sizeof t10);
    t1 = t10;
    goto me2;
mn8:;
ma4:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t12 != ((uint8_t)1ULL)) goto mn11;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_shofa8pdml9fb));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(12));
    gm_th2lqbo77pvsc t14;
    GM_ST(uint8_t, (uint8_t *)&t14 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t14 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t14 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t14), t13);
    ((uint8_t *)&t14)[16] = 1;
    t1 = t14;
    goto me2;
mn11:;
ma5:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&gm_l0) + 0));
    if (t16 != ((uint8_t)2ULL)) goto mn15;
    gm_b16_8 t17;
    GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sjqs7q3ostqbm));
    GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(3));
    gm_th2lqbo77pvsc t18;
    GM_ST(uint8_t, (uint8_t *)&t18 + 17, 0);
    GM_ST(uint16_t, (uint8_t *)&t18 + 18, 0);
    GM_ST(uint32_t, (uint8_t *)&t18 + 20, 0);
    GM_ST(gm_b16_8, ((uint8_t *)&t18), t17);
    ((uint8_t *)&t18)[16] = 1;
    t1 = t18;
    goto me2;
mn15:;
ma6:;
    gm_trap("no match arm matched the scrutinee", gm_srck6bk8q21r07v, 196, 49);
me2:;
    F.ret = t1;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_th2lqbo77pvsc gm_f_visibility_u2ipb12_c(gm_t9qkh0rsooc8d a0) { gm_th2lqbo77pvsc gm_r; gm_f_visibility_u2ipb12(&gm_r, a0); return gm_r; }

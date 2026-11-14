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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; uint32_t f5; uint8_t p6[4]; gm_b16_8 f6; } gm_t7g0o60iqn9h2;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; } gm_temv198mna2jp;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; } gm_thognv9odov03;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
typedef struct { gm_b16_8 f0; gm_tf5srh4mp4b3i f1; } gm_tj64ui6kruni6;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[1]; } gm_tnqk2qt7j42o9;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 a[1]; } gm_tpmdr2hmdhsqr;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; uint64_t f4; gm_b16_8 f5; } gm_tsqpr25pef7fd;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tud7oovfvnf43;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t7g0o60iqn9h2) == 104 && _Alignof(gm_t7g0o60iqn9h2) == 8 && offsetof(gm_t7g0o60iqn9h2, f0) == 0 && offsetof(gm_t7g0o60iqn9h2, f1) == 16 && offsetof(gm_t7g0o60iqn9h2, f2) == 32 && offsetof(gm_t7g0o60iqn9h2, f3) == 48 && offsetof(gm_t7g0o60iqn9h2, f4) == 64 && offsetof(gm_t7g0o60iqn9h2, f5) == 80 && offsetof(gm_t7g0o60iqn9h2, f6) == 88, "layout of `([]u8, []u8, []u8, string, string, u32, []u8)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_thognv9odov03 v0; gm_tiilmfd16v3fc v1; } f1; } gm_tbf8u1b86cgmi;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_trqfvmjtt4sij f0; gm_trqfvmjtt4sij f1; } gm_td7pkamr64l0c;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_temv198mna2jp) == 40 && _Alignof(gm_temv198mna2jp) == 8 && offsetof(gm_temv198mna2jp, f0) == 0 && offsetof(gm_temv198mna2jp, f1) == 16 && offsetof(gm_temv198mna2jp, f2) == 24, "layout of `(string, u32, string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_thognv9odov03) == 40 && _Alignof(gm_thognv9odov03) == 8 && offsetof(gm_thognv9odov03, f0) == 0 && offsetof(gm_thognv9odov03, f1) == 8 && offsetof(gm_thognv9odov03, f2) == 24, "layout of `ForeignFn`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tj64ui6kruni6) == 32 && _Alignof(gm_tj64ui6kruni6) == 8 && offsetof(gm_tj64ui6kruni6, f0) == 0 && offsetof(gm_tj64ui6kruni6, f1) == 16, "layout of `(string, ?usize)`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnqk2qt7j42o9) == 4 && _Alignof(gm_tnqk2qt7j42o9) == 4 && offsetof(gm_tnqk2qt7j42o9, a) == 0, "layout of `[1]Ty`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tpmdr2hmdhsqr) == 16 && _Alignof(gm_tpmdr2hmdhsqr) == 8 && offsetof(gm_tpmdr2hmdhsqr, a) == 0, "layout of `[1]string`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tsqpr25pef7fd) == 88 && _Alignof(gm_tsqpr25pef7fd) == 8 && offsetof(gm_tsqpr25pef7fd, f0) == 0 && offsetof(gm_tsqpr25pef7fd, f1) == 16 && offsetof(gm_tsqpr25pef7fd, f2) == 32 && offsetof(gm_tsqpr25pef7fd, f3) == 48 && offsetof(gm_tsqpr25pef7fd, f4) == 64 && offsetof(gm_tsqpr25pef7fd, f5) == 72, "layout of `([]u8, string, string, []u8, u64, string)`");
_Static_assert(sizeof(gm_tud7oovfvnf43) == 80 && _Alignof(gm_tud7oovfvnf43) == 8 && offsetof(gm_tud7oovfvnf43, f0) == 0 && offsetof(gm_tud7oovfvnf43, f1) == 16 && offsetof(gm_tud7oovfvnf43, f2) == 32 && offsetof(gm_tud7oovfvnf43, f3) == 48 && offsetof(gm_tud7oovfvnf43, f4) == 64, "layout of `(string, string, string, string, string)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbf8u1b86cgmi) == 48 && _Alignof(gm_tbf8u1b86cgmi) == 8 && offsetof(gm_tbf8u1b86cgmi, f0) == 0 && offsetof(gm_tbf8u1b86cgmi, f1) == 8, "layout of `ScopeKind`");
_Static_assert(sizeof(gm_td7pkamr64l0c) == 80 && _Alignof(gm_td7pkamr64l0c) == 8 && offsetof(gm_td7pkamr64l0c, f0) == 0 && offsetof(gm_td7pkamr64l0c, f1) == 40, "layout of `(q: List(u32), r: List(u32))`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { gm_tbf8u1b86cgmi f0; uint32_t f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; uint64_t f3; } gm_tehgl6lkqgvt0;
typedef struct { gm_trqfvmjtt4sij f0; gm_td7pkamr64l0c f1; gm_b16_8 f2; uint64_t f3; uint8_t f4; uint8_t f5; uint8_t pe[6]; } gm_tjfrcdrnp2bat;
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
typedef struct { gm_tehgl6lkqgvt0 f0; uint8_t f1; uint8_t pe[7]; } gm_t1g28mkc78m0c;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tehgl6lkqgvt0) == 104 && _Alignof(gm_tehgl6lkqgvt0) == 8 && offsetof(gm_tehgl6lkqgvt0, f0) == 0 && offsetof(gm_tehgl6lkqgvt0, f1) == 48 && offsetof(gm_tehgl6lkqgvt0, f2) == 56 && offsetof(gm_tehgl6lkqgvt0, f3) == 96, "layout of `Scope`");
_Static_assert(sizeof(gm_tjfrcdrnp2bat) == 152 && _Alignof(gm_tjfrcdrnp2bat) == 8 && offsetof(gm_tjfrcdrnp2bat, f0) == 0 && offsetof(gm_tjfrcdrnp2bat, f1) == 40 && offsetof(gm_tjfrcdrnp2bat, f2) == 120 && offsetof(gm_tjfrcdrnp2bat, f3) == 136 && offsetof(gm_tjfrcdrnp2bat, f4) == 144 && offsetof(gm_tjfrcdrnp2bat, f5) == 145, "layout of `Saved`");
_Static_assert(sizeof(gm_t1g28mkc78m0c) == 112 && _Alignof(gm_t1g28mkc78m0c) == 8 && offsetof(gm_t1g28mkc78m0c, f0) == 0 && offsetof(gm_t1g28mkc78m0c, f1) == 104, "layout of `?Scope`");
struct gm_fr_by_ref_fi0o860 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_cancel__isj44kg {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_check_unwind_2hji4bl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_ctl_perform_be6h6eo {
    gm_frame h;
    gm_trqfvmjtt4sij l9; /* fields */
    gm_trqfvmjtt4sij l15; /* ptys */
    gm_trqfvmjtt4sij l21; /* args */
    gm_trqfvmjtt4sij l27; /* init */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_detached_nd71egs {
    gm_frame h;
    gm_tjfrcdrnp2bat l4; /* saved */
    gm_tqgtd5gtgj65s l5; /* r */
    gm_trqfvmjtt4sij l6; /* text */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_emit_l3j4r4e {
    gm_frame h;
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_emit_vkr7eg2 {
    gm_frame h;
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_end_lines_99v0bhd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fiber_builtin_bgfcp56 {
    gm_frame h;
    void * l0; /* fg */
    gm_tddtjfggdat0d l3; /* s */
    gm_tddtjfggdat0d l4; /* sig */
    uint8_t l5; /* hidden */
    gm_t88rcucg1b7uk l12; /* aty */
    gm_b16_8 l13; /* ac */
    uint64_t l14; /* size */
    gm_t88rcucg1b7uk l17; /* never_t */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_frame_name_stddmgg {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_is_never_in5ftma {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_17uennd {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_unit_n83ttnb {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_void_vs2outo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_kind_ghvav22 {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_label_710oev4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_ladder_36amdbn {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_list_epvducg {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_loc_u9c0aqb {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_outline_body_tsr6q2p {
    gm_frame h;
    void * l0; /* fg */
    uint32_t l1; /* label */
    gm_t88rcucg1b7uk l2; /* ty */
    void * l3; /* body */
    uint32_t l5; /* fid */
    gm_t5o0tpv5jka2l l8; /* text */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_pop_g2usrvd {
    gm_frame h;
    gm_tehgl6lkqgvt0 l2; /* x */
    gm_t1g28mkc78m0c ret;
};
struct gm_fr_ptr_bytes_eocneng {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_restore_28da45i {
    gm_frame h;
    gm_tjfrcdrnp2bat l1; /* saved */
    gm_b0_1 ret;
};
struct gm_fr_restore_scopes_f62992p {
    gm_frame h;
    gm_td7pkamr64l0c l1; /* saved */
    gm_b0_1 ret;
};
struct gm_fr_resume__rhq91rs {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_slice_len_a3383br {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_slice_ptr_hojfgr5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unit_8ekedsl {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_unit_ty_vgceilu {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_unsupported_531q51r {
    gm_frame h;
    gm_t08olq97rscn3 ret;
};
struct gm_fr_with_on_fl0pjo9 {
    gm_frame h;
    gm_tehgl6lkqgvt0 l15; /* scope */
    gm_tehgl6lkqgvt0 l16; /* popped */
    gm_t5o0tpv5jka2l ret;
};
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_backtrace_name_mars429(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_call_indirect_khk02ee(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, uint8_t a7, uint8_t a8);
__attribute__((aligned(4))) gm_b0_1 gm_f_call_site_8n74to1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) void gm_f_cancel__isj44kg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_ctl_perform_be6h6eo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint32_t a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_detached_nd71egs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_closure a3);
__attribute__((aligned(4))) void gm_f_emit_l3j4r4e(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_emit_vkr7eg2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_end_lines_99v0bhd(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_scope_45ar5qv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) void gm_f_expr_porbc9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_fiber_builtin_bgfcp56(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_ptr_type_f9efs3c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_tag_okd90ep(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t9805qtonbsag a2);
__attribute__((aligned(4))) void gm_f_format_0gjpujh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4hbjrqo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5erjuiq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5j9lqr8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7lru3c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_8ps3g8c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_9s56ahi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_bs5ht1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_dn5pqnq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_dr8c0gq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_fego630(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h6b2ofo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_j44ni19(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_kpd1ovq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lu93b1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mepvg5e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rslrdtj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_stg33ch(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_tsutrj7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_vfrm49j(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_vrlgsjr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_frame_name_stddmgg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_jump_pvnkvme(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_tf5srh4mp4b3i a2);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_label_710oev4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_ladder_36amdbn(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_ladder_target_84n3aep(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_07q39gh(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_1oiu95r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_349v8tu(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_5bcsv5p(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_5dh0opk(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_bgakk7h(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_d17op7h(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_d8lmsp4(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ep7poaf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_gfhlos1(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_gp6bqvn(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_gpkl3pa(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_gu3mj45(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_hl75la5(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_jl1vuq9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ku7hk5r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_lg9i0ct(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_m4ia8qg(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_n1p06ei(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qm581s9(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_reujofp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_t0so8o7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_t52q40t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_v8hsi1d(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_viqjr83(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_vllikfi(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_vs9ueum(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_outline_body_tsr6q2p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2, void * a3);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_g2usrvd(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pcqb56b(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_restore_28da45i(void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_restore_scopes_f62992p(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_resume__rhq91rs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_save_2j2i9g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_start_exit_m57dnn5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unwind_ladder_a9ij1gj(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_with_on_fl0pjo9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, gm_b16_8 a4, void * a5, void * a6);
__attribute__((aligned(4))) void gm_f_with_scope_o670qbc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_within_sys_2lc2ugq(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_01cqlob(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_7oqfkkd(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_cdouvhd(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f1cbljc(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fvssipp(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_i2vvij8(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_k5ng481(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_upr5k5n(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern uint8_t gm_ccmgjp6th6r[16] __attribute__((aligned(8)));
extern const uint8_t gm_s02uuedsru1qu[9];
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s10iv34fu7qe7[3];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s3bsv9idfdodt[7];
extern const uint8_t gm_sb8kl54a499bf[7];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_si2pj4mpn1kn5[11];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_sikpqtja8pqpv[3];
extern const uint8_t gm_sjeh66tmmrhop[3];
extern const uint8_t gm_sm9m81v5v04r9[12];
extern const uint8_t gm_smr5ddngcjuao[19];
extern const uint8_t gm_sq05bfmao5v8n[47];
extern const uint8_t gm_sr0u787bpqkqg[28];
extern const char gm_src0gtuh94b1fce[];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srcihhtlsvd7f6i[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srjnkb77s24ld[7];
extern const uint8_t gm_ss3kah49g222u[33];
extern const uint8_t gm_ss3op0i298nem[69];
extern const uint8_t gm_sv89ta651gq7u[20];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_by_ref_fi0o860 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 97u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_t5v8n6qrn4rh4 t4;
    gm_f_repr_53quqki((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t5v8n6qrn4rh4 t5 = t4;
    uint8_t t6 = ((GM_LD(uint8_t, ((uint8_t *)&t5) + 0) == ((uint8_t)6ULL)));
    if (t6) {
        void * t7 = gm_l0;
        gm_t88rcucg1b7uk t8 = gm_l1;
        uint64_t t9 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t10 = (t9 > ((uint64_t)16ULL));
        t1 = t10;
    } else t1 = 0;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_check_unwind_2hji4bl F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 50u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)t3) + 296));
        if (!t4) goto ie2;
        {
            gm_b0_1 t6;
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t8 = gm_l0;
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_smr5ddngcjuao));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(18));
        gm_b0_1 t10 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t11 = gm_l0;
        gm_b0_1 t12 = gm_f_unwind_ladder_a9ij1gj((void *)NULL, cx, NULL, t11);
        if (gm_unw.kind) {
            goto gm_lad;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5nn7ph0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ijtp9lo F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)1ULL));
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 1)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 4)));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_end_lines_99v0bhd(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_end_lines_99v0bhd F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 1136) + 2));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = (void *)(((uint8_t *)t7) + 184);
            gm_b16_8 t9 = GM_LD(gm_b16_8, gm_ccmgjp6th6r);
            gm_tnpifv20irnph t10;
            GM_ST(gm_b16_8, ((uint8_t *)&t10 + 0), t9);
            gm_tftb1odc739t5 t11 = gm_f_write_cdouvhd(t8, t10);
            gm_tftb1odc739t5 t12 = t11;
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 1, gm_srcmok80ho8hh01, 430, 31);
            gm_b0_1 t13 = GM_LD(gm_b0_1, (((uint8_t *)&t12) + 1));
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_frame_name_stddmgg(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_frame_name_stddmgg F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    uint32_t t2 = gm_l1;
    gm_t9805qtonbsag t3;
    memset(&t3, 0, sizeof t3);
    gm_b16_8 t4 = gm_f_fn_tag_okd90ep((void *)NULL, cx, NULL, t1, t2, t3);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t5;
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
    gm_tabb710d9mq2e t6;
    gm_f_format_5erjuiq((void *)&t6, (void *)NULL, cx, NULL, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t7 = t6;
    if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcht195tfrfuqo, 167, 60);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
    gm_b16_8 t9 = t8;
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    F.ret = t10;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_fresh_5kokjmg F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 77u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l0;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 232));
        uint32_t r5; bool o6 = __builtin_add_overflow(t4, ((uint32_t)1ULL), &r5);
        if (o6) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 452, 5);
        uint32_t t7 = r5;
        GM_ST(uint32_t, (((uint8_t *)t2) + 232), t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = gm_l0;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 344));
        uint32_t r11; bool o12 = __builtin_add_overflow(t10, ((uint32_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `+`", gm_srcmok80ho8hh01, 453, 5);
        uint32_t t13 = r11;
        GM_ST(uint32_t, (((uint8_t *)t8) + 344), t13);
    }
    {
        void * t14 = gm_l0;
        uint32_t t15 = GM_LD(uint32_t, (((uint8_t *)t14) + 232));
        uint8_t t16 = 0;
        F.ret = t15;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_never_in5ftma F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 155u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_t53at80pfdus3 t5;
    gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
    gm_t53at80pfdus3 t6 = t5;
    uint8_t t7 = ((GM_LD(uint8_t, ((uint8_t *)&t6) + 0) == ((uint8_t)5ULL)));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_unit_17uennd F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 43u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_is_unit_n83ttnb(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
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
    GM_STACK_CHECK_SHALLOW(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_t53at80pfdus3 t5;
        gm_f_kind_jcnt4hm((void *)&t5, t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_t53at80pfdus3 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (t7 != ((uint8_t)12ULL)) goto ie2;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)&t6) + 8) + 0));
        gm_l2 = t8;
        {
            uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint8_t t11 = ((t10 == ((uint64_t)0ULL)));
            uint8_t t12 = 0;
            F.ret = t11;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            uint8_t t14 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_void_vs2outo F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 41u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint8_t t1 = 0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint8_t t4 = gm_f_is_unit_17uennd(t2, GM_CONV(gm_csguankko4lmhd, t3));
    if (!t4) {
        void * t5 = gm_l0;
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint8_t t7 = gm_f_is_never_in5ftma(t5, GM_CONV(gm_csguankko4lmhd, t6));
        t1 = t7;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_kind_ghvav22 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_label_710oev4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_label_710oev4 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* name */
    GM_STACK_CHECK(&F, 76u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 184);
        gm_b16_8 t4 = gm_l1;
        gm_tnpifv20irnph t5;
        GM_ST(gm_b16_8, ((uint8_t *)&t5 + 0), t4);
        gm_tftb1odc739t5 t6 = gm_f_write_f1cbljc(t3, t5);
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcmok80ho8hh01, 507, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_ladder_36amdbn(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_ladder_36amdbn F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* target */
    gm_tf5srh4mp4b3i gm_l2; /* scope */
    GM_STACK_CHECK(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_tj64ui6kruni6 t3;
        gm_f_ladder_target_84n3aep((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tj64ui6kruni6 t4 = t3;
        gm_l1 = GM_LD(gm_b16_8, (uint8_t *)&t4 + 0);
        gm_l2 = GM_LD(gm_tf5srh4mp4b3i, (uint8_t *)&t4 + 16);
    }
    {
        void * t5 = gm_l0;
        gm_b16_8 t6 = gm_l1;
        gm_tf5srh4mp4b3i t7 = gm_l2;
        gm_b0_1 t8 = gm_f_jump_pvnkvme((void *)NULL, cx, NULL, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
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
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_epvducg F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_5nn7ph0();
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
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nof7vfj F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rcd2jln();
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_loc_u9c0aqb F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    GM_STACK_CHECK(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = gm_l1;
    gm_tl874t2atnlh3 t4 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t3) + 72));
    gm_b16_8 t5 = gm_f_src_loc_lrhtj7i((void *)NULL, cx, NULL, t2, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_never_ty_b949mgo F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)5ULL));
    gm_t88rcucg1b7uk t3 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t3;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_g2usrvd(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_g2usrvd F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint64_t t3 = GM_LD(uintptr_t, (((uint8_t *)t2) + 0) + 8);
        gm_l1 = t3;
    }
    {
        uint64_t t5 = gm_l1;
        uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
        if (!t6) goto ie4;
        {
            gm_t1g28mkc78m0c t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint64_t t10 = gm_l1;
        uint64_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint64_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 130, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 130, 10);
        gm_tehgl6lkqgvt0 t17 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 104));
        F.l2 = t17;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 0) + 8);
        void * t21 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 0)));
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l1;
        uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 131, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 131, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 104)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        gm_tehgl6lkqgvt0 t29 = F.l2;
        gm_t1g28mkc78m0c t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 105, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 106, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 108, 0);
        GM_ST(gm_tehgl6lkqgvt0, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[104] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bytes_eocneng F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    uint64_t t3 = GM_LD(uint64_t, ((((((uint8_t *)t2) + 8) + 8) + 0) + 0));
    F.ret = t3;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_restore_28da45i(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_28da45i F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 122u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_trqfvmjtt4sij t3 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 0));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 184), t3);
    }
    {
        void * t4 = gm_l0;
        gm_f_restore_scopes_f62992p(t4, &(*(gm_td7pkamr64l0c *)(((uint8_t *)a1) + 40)));
    }
    {
        void * t5 = gm_l0;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 120));
        GM_ST(gm_b16_8, (((uint8_t *)t5) + 280), t6);
    }
    {
        void * t7 = gm_l0;
        uint64_t t8 = GM_LD(uint64_t, (((uint8_t *)a1) + 136));
        GM_ST(uint64_t, (((uint8_t *)t7) + 224), t8);
    }
    {
        void * t9 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a1) + 144));
        GM_ST(uint8_t, (((uint8_t *)t9) + 296), t10);
    }
    {
        void * t11 = gm_l0;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a1) + 145));
        GM_ST(uint8_t, (((uint8_t *)t11) + 297), t12);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_restore_scopes_f62992p(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scopes_f62992p F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK_SHALLOW(&F, 104u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_trqfvmjtt4sij t3 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 0));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t2) + 240), t3);
    }
    {
        void * t4 = gm_l0;
        gm_trqfvmjtt4sij t5 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a1) + 40));
        GM_ST(gm_trqfvmjtt4sij, (((uint8_t *)t4) + 352), t5);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_dmhtkib F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 56u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    uint64_t t4 = gm_f_size_of_k0fluj2((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_size_of_k0fluj2 F;
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
    uint64_t t6 = GM_LD(uint64_t, (((uint8_t *)&t5) + 0));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_len_a3383br(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_slice_len_a3383br F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* addr */
    GM_STACK_CHECK(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_t3877pk7lv748 t1;
    gm_b16_8 t2 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 0, t2);
    void * t3 = gm_l0;
    uint64_t t4 = gm_f_ptr_bytes_eocneng(t3);
    GM_ST(uint64_t, (uint8_t *)&t1 + 16, t4);
    gm_tabb710d9mq2e t5;
    gm_f_format_0gjpujh((void *)&t5, (void *)NULL, cx, NULL, &t1);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t6 = t5;
    if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_src0gtuh94b1fce, 213, 72);
    gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    F.ret = t9;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_slice_ptr_hojfgr5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_slice_ptr_hojfgr5 F;
    gm_b16_8 gm_l0; /* addr */
    GM_STACK_CHECK(&F, 176u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1 = gm_l0;
    gm_tnpifv20irnph t2;
    GM_ST(gm_b16_8, ((uint8_t *)&t2 + 0), t1);
    gm_tabb710d9mq2e t3;
    gm_f_format_7lru3c2((void *)&t3, (void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t4 = t3;
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_src0gtuh94b1fce, 211, 60);
    gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
    gm_b16_8 t6 = t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
    F.ret = t7;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 212u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = t3;
    gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t5);
    gm_b16_8 t6 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t8);
    gm_t53at80pfdus3 t9;
    GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t9 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t9 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t9 + 8), t2);
    gm_t88rcucg1b7uk t10 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t10;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_8ekedsl F;
    void * gm_l0; /* fg */
    GM_STACK_CHECK(&F, 92u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
    void * t4 = (void *)(((uint8_t *)t3) + 8);
    gm_t88rcucg1b7uk t5 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t6 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t1, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_ty_vgceilu F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1) {
    struct gm_fr_unsupported_531q51r F;
    gm_tl874t2atnlh3 gm_l0; /* span */
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 168u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        memset((uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 28, 0, 4);
        gm_b16_8 t2 = gm_l1;
        gm_tnpifv20irnph t3;
        GM_ST(gm_b16_8, ((uint8_t *)&t3 + 0), t2);
        gm_tabb710d9mq2e t4;
        gm_f_format_fsfutor((void *)&t4, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcd5arorrf8jkg, 35, 27);
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_b16_8 t7 = t6;
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
        gm_tl874t2atnlh3 t9 = gm_l0;
        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 0, t8);
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&(*(gm_t08olq97rscn3 *)gm_out) + 16, t9);
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
inline __attribute__((aligned(4))) void gm_f_cancel__isj44kg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_cancel__isj44kg F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* kont */
    gm_b16_8 gm_l3; /* k */
    gm_b16_8 gm_l4; /* ka */
    gm_b16_8 gm_l5; /* loc */
    GM_STACK_CHECK(&F, 525u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l2;
        gm_t5o0tpv5jka2l t4;
        gm_f_expr_porbc9t((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_t08olq97rscn3 t6 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t5) + 8));
            gm_t5o0tpv5jka2l t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l3 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l2;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 0));
        gm_b16_8 t12 = gm_l3;
        gm_b16_8 t13 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t9, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t13;
    }
    {
        void * t14 = gm_l0;
        void * t15 = gm_l1;
        gm_b16_8 t16 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t16;
    }
    {
        void * t17 = gm_l0;
        gm_t6j6smj7qoctu t18;
        gm_b16_8 t19 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
        gm_b16_8 t20 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 16, t20);
        gm_b0_1 t21 = gm_f_linef_5bcsv5p((void *)NULL, cx, NULL, t17, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t22 = gm_l0;
        gm_b0_1 t23 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t24 = gm_l0;
        gm_b16_8 t25 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t26;
        GM_ST(uint8_t, (uint8_t *)&t26 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t26 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t26 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t26 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t26 + 32, 0);
        ((uint8_t *)&t26)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t26 + 8), t25);
        gm_t5o0tpv5jka2l t27 = t26;
        uint8_t t28 = ((((uint8_t *)&t27))[0] != 0);
        F.ret = t26;
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
__attribute__((aligned(4))) void gm_f_ctl_perform_be6h6eo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint32_t a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5) {
    struct gm_fr_ctl_perform_be6h6eo F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* n */
    uint32_t gm_l2; /* op */
    gm_b16_8 gm_l3; /* vals */
    gm_b16_8 gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    uint32_t gm_l6; /* id */
    gm_b16_8 gm_l7; /* req */
    gm_b16_8 gm_l8; /* thunk */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    gm_t88rcucg1b7uk gm_l13; /* t */
    gm_t88rcucg1b7uk gm_l14; /* kty */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_t88rcucg1b7uk gm_l18; /* t */
    gm_b16_8 gm_l19; /* fp */
    gm_b16_8 gm_l20; /* kc */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* i */
    gm_b16_8 gm_l25; /* head */
    gm_b16_8 gm_l26; /* r */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    gm_b16_8 gm_l30; /* v */
    gm_th2lqbo77pvsc gm_l31; /* res */
    gm_b16_8 gm_l32; /* x */
    gm_b16_8 gm_l33; /* res_ptr */
    gm_b16_8 gm_l34; /* x */
    GM_STACK_CHECK(&F, 3353u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_f_fresh_5kokjmg(t2);
        gm_l6 = t3;
    }
    {
        gm_tommf4j9upomc t4;
        memset((uint8_t *)&t4 + 20, 0, 4);
        void * t5 = gm_l0;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)t5) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 0, t6);
        uint32_t t7 = gm_l6;
        GM_ST(uint32_t, (uint8_t *)&t4 + 16, t7);
        gm_tabb710d9mq2e t8;
        gm_f_format_lu93b1k((void *)&t8, (void *)NULL, cx, NULL, &t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 8, gm_srcihhtlsvd7f6i, 107, 12);
        gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 8));
        gm_l7 = t10;
    }
    {
        gm_tommf4j9upomc t11;
        memset((uint8_t *)&t11 + 20, 0, 4);
        void * t12 = gm_l0;
        gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)t12) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t11 + 0, t13);
        uint32_t t14 = gm_l6;
        GM_ST(uint32_t, (uint8_t *)&t11 + 16, t14);
        gm_tabb710d9mq2e t15;
        gm_f_format_vrlgsjr((void *)&t15, (void *)NULL, cx, NULL, &t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_srcihhtlsvd7f6i, 108, 14);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
        gm_l8 = t17;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t18 = (void *)((uint8_t *)&F.l9);
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sm9m81v5v04r9));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(11));
        gm_tftb1odc739t5 t20 = gm_f_extend_0cs85gs(t18, t19);
        gm_tftb1odc739t5 t21 = t20;
        if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srcihhtlsvd7f6i, 110, 5);
        gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
    }
    {
        {
            gm_b16_8 t24 = gm_l4;
            gm_l10 = t24;
        }
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t26 = gm_l11;
                uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                uint8_t t28 = (t26 < t27);
                if (!t28) goto lb25;
                {
                    uint64_t t31 = gm_l11;
                    uint64_t t32 = gm_l11;
                    uint64_t t33 = (uintptr_t)t32;
                    uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                    gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t33 * 4));
                    gm_tlcm4lvu3g1a8 t36;
                    GM_ST(uint32_t, (uint8_t *)&t36 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t36 + 0), t31);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t36 + 8), t35);
                    gm_tlcm4lvu3g1a8 t37 = t36;
                    uint64_t t40 = GM_LD(uint64_t, (((uint8_t *)&t37) + 0));
                    gm_l12 = t40;
                    gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t37) + 8));
                    gm_l13 = t41;
                    {
                        void * t43 = (void *)((uint8_t *)&F.l9);
                        gm_t3877pk7lv748 t44;
                        void * t45 = gm_l0;
                        void * t46 = GM_LD(void *, (((uint8_t *)t45) + 0));
                        void * t47 = (void *)(((uint8_t *)t46) + 8);
                        gm_t88rcucg1b7uk t48 = gm_l13;
                        gm_b16_8 t49 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t47, t48);
                        if (gm_unw.kind) {
                            goto gm_ll29;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&t44 + 0, t49);
                        uint64_t t50 = gm_l12;
                        GM_ST(uint64_t, (uint8_t *)&t44 + 16, t50);
                        gm_tftb1odc739t5 t51 = gm_f_write_01cqlob(t43, &t44);
                        gm_tftb1odc739t5 t52 = t51;
                        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srcihhtlsvd7f6i, 112, 9);
                        gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
                    }
                    goto me38;
mn39:;
                    gm_trap("no match arm matched the scrutinee", gm_srcihhtlsvd7f6i, 111, 5);
me38:;
                }
lc25:;
                {
                    uint64_t t55 = gm_l11;
                    uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                    uint64_t t58 = r56;
                    gm_l11 = t58;
                }
            }
gm_ll29:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb25; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc25; }
            }
            goto gm_lad;
lb25:;
        }
    }
    {
        void * t59 = gm_l0;
        void * t60 = GM_LD(void *, (((uint8_t *)t59) + 0));
        void * t61 = (void *)(((uint8_t *)t60) + 1848);
        gm_t6j6smj7qoctu t62;
        gm_b16_8 t63 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t62 + 0, t63);
        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        gm_b16_8 t65 = t64;
        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
        GM_ST(gm_b16_8, (uint8_t *)&t62 + 16, t66);
        gm_tftb1odc739t5 t67 = gm_f_write_fvssipp(t61, &t62);
        gm_tftb1odc739t5 t68 = t67;
        if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 1, gm_srcihhtlsvd7f6i, 114, 5);
        gm_b0_1 t69 = GM_LD(gm_b0_1, (((uint8_t *)&t68) + 1));
    }
    {
        void * t70 = gm_l0;
        void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
        void * t72 = GM_LD(void *, ((((uint8_t *)t71) + 8) + 0));
        void * t73 = (void *)(((uint8_t *)t72) + 8);
        gm_t88rcucg1b7uk t74 = gm_l5;
        gm_t53at80pfdus3 t75;
        memset(&t75, 0, sizeof t75);
        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)18ULL));
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t75 + 8), t74);
        gm_t88rcucg1b7uk t76 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t73, &t75);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t76;
    }
    {
        gm_f_list_nof7vfj((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t78 = gm_l4;
            gm_l16 = t78;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t80 = gm_l17;
                uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t82 = (t80 < t81);
                if (!t82) goto lb79;
                {
                    uint64_t t85 = gm_l17;
                    uint64_t t86 = (uintptr_t)t85;
                    uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t86 * 4));
                    gm_l18 = t90;
                    {
                        void * t92 = (void *)((uint8_t *)&F.l15);
                        gm_t88rcucg1b7uk t93 = gm_l18;
                        gm_tftb1odc739t5 t94 = gm_f_push_k3l9nic(t92, GM_CONV(gm_csguankko4lmhd, t93));
                        gm_tftb1odc739t5 t95 = t94;
                        if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 1, gm_srcihhtlsvd7f6i, 117, 20);
                        gm_b0_1 t96 = GM_LD(gm_b0_1, (((uint8_t *)&t95) + 1));
                    }
                    goto me88;
mn89:;
                    gm_trap("no match arm matched the scrutinee", gm_srcihhtlsvd7f6i, 117, 5);
me88:;
                }
lc79:;
                {
                    uint64_t t98 = gm_l17;
                    uint64_t r99; bool o100 = __builtin_add_overflow(t98, ((uint64_t)1ULL), &r99);
                    uint64_t t101 = r99;
                    gm_l17 = t101;
                }
            }
lb79:;
        }
    }
    {
        void * t102 = (void *)((uint8_t *)&F.l15);
        gm_t88rcucg1b7uk t103 = gm_l14;
        gm_tftb1odc739t5 t104 = gm_f_push_k3l9nic(t102, GM_CONV(gm_csguankko4lmhd, t103));
        gm_tftb1odc739t5 t105 = t104;
        if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 1, gm_srcihhtlsvd7f6i, 118, 5);
        gm_b0_1 t106 = GM_LD(gm_b0_1, (((uint8_t *)&t105) + 1));
    }
    {
        void * t107 = gm_l0;
        void * t108 = gm_l0;
        void * t109 = GM_LD(void *, (((uint8_t *)t108) + 0));
        void * t110 = GM_LD(void *, ((((uint8_t *)t109) + 8) + 0));
        void * t111 = (void *)(((uint8_t *)t110) + 8);
        gm_t88rcucg1b7uk t112 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t111);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
        gm_b16_8 t114 = t113;
        gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
        gm_b16_8 t116 = gm_f_fn_ptr_type_f9efs3c((void *)NULL, cx, NULL, t107, t112, t115);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t116;
    }
    {
        void * t117 = gm_l0;
        void * t118 = GM_LD(void *, (((uint8_t *)t117) + 0));
        void * t119 = (void *)(((uint8_t *)t118) + 8);
        gm_t88rcucg1b7uk t120 = gm_l14;
        gm_b16_8 t121 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t119, t120);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l20 = t121;
    }
    {
        gm_f_list_epvducg((void *)&F.l21, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l23 = ((uint64_t)0ULL);
        }
        {
            uint64_t t123 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            gm_l22 = t123;
        }
        {
            for (;;) {
                uint64_t t125 = gm_l23;
                uint64_t t126 = gm_l22;
                uint8_t t127 = (t125 < t126);
                if (!t127) goto lb124;
                {
                    uint64_t t130 = gm_l23;
                    uint64_t t131 = t130;
                    uint64_t t134 = GM_LD(uint64_t, ((uint8_t *)&t131));
                    gm_l24 = t134;
                    {
                        void * t137 = gm_l0;
                        void * t138 = GM_LD(void *, (((uint8_t *)t137) + 0));
                        void * t139 = (void *)(((uint8_t *)t138) + 8);
                        uint64_t t140 = gm_l24;
                        uint64_t t141 = (uintptr_t)t140;
                        uint64_t t142 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        if (t141 >= (uintptr_t)(t142)) gm_trap_index(t141, t142, gm_srcihhtlsvd7f6i, 123, 27);
                        gm_t88rcucg1b7uk t143 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t141 * 4));
                        uint8_t t144 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t139, t143);
                        if (gm_unw.kind) {
                            goto gm_ll128;
                        }
                        if (!t144) goto ie136;
                        {
                            void * t146 = (void *)((uint8_t *)&F.l21);
                            uint64_t t147 = gm_l24;
                            gm_t4ce0jd3tdro4 t148;
                            GM_ST(uint64_t, ((uint8_t *)&t148 + 0), t147);
                            gm_tabb710d9mq2e t149;
                            gm_f_format_fego630((void *)&t149, (void *)NULL, cx, NULL, t148);
                            if (gm_unw.kind) {
                                goto gm_ll128;
                            }
                            gm_tabb710d9mq2e t150 = t149;
                            if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 8, gm_srcihhtlsvd7f6i, 124, 23);
                            gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&t150) + 8));
                            gm_b16_8 t152 = t151;
                            gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
                            gm_tftb1odc739t5 t154 = gm_f_push_3ef746b(t146, t153);
                            gm_tftb1odc739t5 t155 = t154;
                            if (((((uint8_t *)&t155))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t155) + 1, gm_srcihhtlsvd7f6i, 124, 13);
                            gm_b0_1 t156 = GM_LD(gm_b0_1, (((uint8_t *)&t155) + 1));
                        }
                        goto ix136;
ie136:;
                        {
                            void * t158 = (void *)((uint8_t *)&F.l21);
                            uint64_t t159 = gm_l24;
                            gm_t4ce0jd3tdro4 t160;
                            GM_ST(uint64_t, ((uint8_t *)&t160 + 0), t159);
                            gm_tabb710d9mq2e t161;
                            gm_f_format_j44ni19((void *)&t161, (void *)NULL, cx, NULL, t160);
                            if (gm_unw.kind) {
                                goto gm_ll128;
                            }
                            gm_tabb710d9mq2e t162 = t161;
                            if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 8, gm_srcihhtlsvd7f6i, 126, 23);
                            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&t162) + 8));
                            gm_b16_8 t164 = t163;
                            gm_b16_8 t165 = GM_LD(gm_b16_8, ((uint8_t *)&t164));
                            gm_tftb1odc739t5 t166 = gm_f_push_3ef746b(t158, t165);
                            gm_tftb1odc739t5 t167 = t166;
                            if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 1, gm_srcihhtlsvd7f6i, 126, 13);
                            gm_b0_1 t168 = GM_LD(gm_b0_1, (((uint8_t *)&t167) + 1));
                        }
ix136:;
                    }
                    goto me132;
mn133:;
                    gm_trap("no match arm matched the scrutinee", gm_srcihhtlsvd7f6i, 122, 5);
me132:;
                }
lc124:;
                {
                    uint64_t t170 = gm_l23;
                    uint64_t r171; bool o172 = __builtin_add_overflow(t170, ((uint64_t)1ULL), &r171);
                    uint64_t t173 = r171;
                    gm_l23 = t173;
                }
            }
gm_ll128:;
            if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb124; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc124; }
            }
            goto gm_lad;
lb124:;
        }
    }
    {
        void * t174 = (void *)((uint8_t *)&F.l21);
        gm_b16_8 t175;
        GM_ST(void *, (uint8_t *)&t175, (void *)(gm_sjeh66tmmrhop));
        GM_ST(uintptr_t, (uint8_t *)&t175 + 8, (uintptr_t)(2));
        gm_tftb1odc739t5 t176 = gm_f_push_3ef746b(t174, t175);
        gm_tftb1odc739t5 t177 = t176;
        if (((((uint8_t *)&t177))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t177) + 1, gm_srcihhtlsvd7f6i, 129, 5);
        gm_b0_1 t178 = GM_LD(gm_b0_1, (((uint8_t *)&t177) + 1));
    }
    {
        gm_b16_8 t179 = gm_l8;
        gm_tnpifv20irnph t180;
        GM_ST(gm_b16_8, ((uint8_t *)&t180 + 0), t179);
        gm_tabb710d9mq2e t181;
        gm_f_format_4hbjrqo((void *)&t181, (void *)NULL, cx, NULL, t180);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t182 = t181;
        if (((((uint8_t *)&t182))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t182) + 8, gm_srcihhtlsvd7f6i, 130, 13);
        gm_b16_8 t183 = GM_LD(gm_b16_8, (((uint8_t *)&t182) + 8));
        gm_l25 = t183;
    }
    {
        void * t184 = gm_l0;
        void * t185 = GM_LD(void *, (((uint8_t *)t184) + 0));
        void * t186 = (void *)(((uint8_t *)t185) + 1888);
        gm_b16_8 t187 = gm_l25;
        gm_tnpifv20irnph t188;
        GM_ST(gm_b16_8, ((uint8_t *)&t188 + 0), t187);
        gm_tftb1odc739t5 t189 = gm_f_write_i2vvij8(t186, t188);
        gm_tftb1odc739t5 t190 = t189;
        if (((((uint8_t *)&t190))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t190) + 1, gm_srcihhtlsvd7f6i, 131, 5);
        gm_b0_1 t191 = GM_LD(gm_b0_1, (((uint8_t *)&t190) + 1));
    }
    {
        void * t192 = gm_l0;
        void * t193 = GM_LD(void *, (((uint8_t *)t192) + 0));
        void * t194 = (void *)(((uint8_t *)t193) + 1928);
        gm_t7g0o60iqn9h2 t195;
        memset((uint8_t *)&t195 + 84, 0, 4);
        gm_b16_8 t196 = gm_l25;
        GM_ST(gm_b16_8, (uint8_t *)&t195 + 0, t196);
        gm_b16_8 t197 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t195 + 16, t197);
        gm_b16_8 t198 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t195 + 32, t198);
        gm_b16_8 t199 = gm_l20;
        GM_ST(gm_b16_8, (uint8_t *)&t195 + 48, t199);
        gm_b16_8 t200 = gm_l19;
        GM_ST(gm_b16_8, (uint8_t *)&t195 + 64, t200);
        uint32_t t201 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t195 + 80, t201);
        gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)&F.l21) + 0));
        gm_b16_8 t203 = t202;
        gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&t203));
        gm_b16_8 t205;
        GM_ST(void *, (uint8_t *)&t205, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t205 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t206;
        gm_f_join_sfknljb((void *)&t206, (void *)NULL, cx, NULL, t204, t205);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t207 = t206;
        if (((((uint8_t *)&t207))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t207) + 8, gm_srcihhtlsvd7f6i, 132, 198);
        gm_b16_8 t208 = GM_LD(gm_b16_8, (((uint8_t *)&t207) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t195 + 88, t208);
        gm_tftb1odc739t5 t209 = gm_f_write_k5ng481(t194, &t195);
        gm_tftb1odc739t5 t210 = t209;
        if (((((uint8_t *)&t210))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t210) + 1, gm_srcihhtlsvd7f6i, 132, 5);
        gm_b0_1 t211 = GM_LD(gm_b0_1, (((uint8_t *)&t210) + 1));
    }
    {
        void * t212 = gm_l0;
        uint32_t t213 = gm_f_fresh_5kokjmg(t212);
        gm_t88rcucg1b7uk t214;
        GM_ST(uint32_t, ((uint8_t *)&t214 + 0), t213);
        gm_tabb710d9mq2e t215;
        gm_f_format_stg33ch((void *)&t215, (void *)NULL, cx, NULL, t214);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t216 = t215;
        if (((((uint8_t *)&t216))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t216) + 8, gm_srcihhtlsvd7f6i, 133, 10);
        gm_b16_8 t217 = GM_LD(gm_b16_8, (((uint8_t *)&t216) + 8));
        gm_l26 = t217;
    }
    {
        gm_f_list_epvducg((void *)&F.l27, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t218 = (void *)((uint8_t *)&F.l27);
        gm_b16_8 t219 = gm_l1;
        gm_tftb1odc739t5 t220 = gm_f_push_3ef746b(t218, t219);
        gm_tftb1odc739t5 t221 = t220;
        if (((((uint8_t *)&t221))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t221) + 1, gm_srcihhtlsvd7f6i, 135, 5);
        gm_b0_1 t222 = GM_LD(gm_b0_1, (((uint8_t *)&t221) + 1));
    }
    {
        {
            gm_b16_8 t224 = gm_l3;
            gm_l28 = t224;
        }
        {
            gm_l29 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t226 = gm_l29;
                uint64_t t227 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                uint8_t t228 = (t226 < t227);
                if (!t228) goto lb225;
                {
                    uint64_t t231 = gm_l29;
                    uint64_t t232 = (uintptr_t)t231;
                    uint64_t t233 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                    gm_b16_8 t236 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t232 * 16));
                    gm_l30 = t236;
                    {
                        void * t238 = (void *)((uint8_t *)&F.l27);
                        gm_b16_8 t239 = gm_l30;
                        gm_tftb1odc739t5 t240 = gm_f_push_3ef746b(t238, t239);
                        gm_tftb1odc739t5 t241 = t240;
                        if (((((uint8_t *)&t241))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t241) + 1, gm_srcihhtlsvd7f6i, 136, 21);
                        gm_b0_1 t242 = GM_LD(gm_b0_1, (((uint8_t *)&t241) + 1));
                    }
                    goto me234;
mn235:;
                    gm_trap("no match arm matched the scrutinee", gm_srcihhtlsvd7f6i, 136, 5);
me234:;
                }
lc225:;
                {
                    uint64_t t244 = gm_l29;
                    uint64_t r245; bool o246 = __builtin_add_overflow(t244, ((uint64_t)1ULL), &r245);
                    uint64_t t247 = r245;
                    gm_l29 = t247;
                }
            }
lb225:;
        }
    }
    {
        void * t248 = gm_l0;
        gm_tfeab279c16rr t249;
        gm_b16_8 t250 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t249 + 0, t250);
        gm_b16_8 t251 = gm_l26;
        GM_ST(gm_b16_8, (uint8_t *)&t249 + 16, t251);
        gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)&F.l27) + 0));
        gm_b16_8 t253 = t252;
        gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
        gm_b16_8 t255;
        GM_ST(void *, (uint8_t *)&t255, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t255 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t256;
        gm_f_join_sfknljb((void *)&t256, (void *)NULL, cx, NULL, t254, t255);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t257 = t256;
        if (((((uint8_t *)&t257))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t257) + 8, gm_srcihhtlsvd7f6i, 137, 48);
        gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&t257) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t249 + 32, t258);
        gm_b0_1 t259 = gm_f_linef_bgakk7h((void *)NULL, cx, NULL, t248, &t249);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_th2lqbo77pvsc t260;
        memset(&t260, 0, sizeof t260);
        gm_l31 = t260;
    }
    {
        void * t262 = gm_l0;
        void * t263 = GM_LD(void *, (((uint8_t *)t262) + 0));
        void * t264 = (void *)(((uint8_t *)t263) + 8);
        gm_t88rcucg1b7uk t265 = gm_l5;
        uint8_t t266 = gm_f_is_void_vs2outo(t264, GM_CONV(gm_csguankko4lmhd, t265));
        uint8_t t267 = (uint8_t)!t266;
        if (!t267) goto ie261;
        {
            void * t269 = gm_l0;
            gm_t88rcucg1b7uk t270 = gm_l5;
            gm_b16_8 t271 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t269, t270);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t272;
            GM_ST(uint8_t, (uint8_t *)&t272 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t272 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t272 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t272), t271);
            ((uint8_t *)&t272)[16] = 1;
            gm_l31 = t272;
        }
        goto ix261;
ie261:;
ix261:;
    }
    {
        gm_b16_8 t273;
        if (!((((uint8_t *)&gm_l31))[16] != 0)) goto ie274;
        gm_b16_8 t275 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
        gm_l32 = t275;
        gm_b16_8 t277 = gm_l32;
        gm_tnpifv20irnph t278;
        GM_ST(gm_b16_8, ((uint8_t *)&t278 + 0), t277);
        gm_tabb710d9mq2e t279;
        gm_f_format_h6b2ofo((void *)&t279, (void *)NULL, cx, NULL, t278);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t280 = t279;
        if (((((uint8_t *)&t280))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t280) + 8, gm_srcihhtlsvd7f6i, 140, 37);
        gm_b16_8 t281 = GM_LD(gm_b16_8, (((uint8_t *)&t280) + 8));
        gm_b16_8 t282 = t281;
        gm_b16_8 t283 = t282;
        gm_b16_8 t284 = GM_LD(gm_b16_8, ((uint8_t *)&t283));
        t273 = t284;
        goto ix274;
ie274:;
        gm_b16_8 t286;
        GM_ST(void *, (uint8_t *)&t286, (void *)(gm_s04qus0luoga5));
        GM_ST(uintptr_t, (uint8_t *)&t286 + 8, (uintptr_t)(4));
        gm_b16_8 t287 = t286;
        t273 = t287;
ix274:;
        gm_l33 = t273;
    }
    {
        void * t288 = gm_l0;
        gm_t0uuhaljsf410 t289;
        gm_b16_8 t290 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t289 + 0, t290);
        gm_b16_8 t291 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t289 + 16, t291);
        gm_b16_8 t292 = gm_l26;
        GM_ST(gm_b16_8, (uint8_t *)&t289 + 32, t292);
        gm_b16_8 t293 = gm_l33;
        GM_ST(gm_b16_8, (uint8_t *)&t289 + 48, t293);
        gm_b0_1 t294 = gm_f_linef_lg9i0ct((void *)NULL, cx, NULL, t288, &t289);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t295 = gm_l0;
        gm_b0_1 t296 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t295);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((uint8_t *)&gm_l31))[16] != 0)) goto ie297;
        gm_b16_8 t298 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l31));
        gm_l34 = t298;
        {
            gm_b16_8 t300 = gm_l34;
            gm_t5o0tpv5jka2l t301;
            GM_ST(uint8_t, (uint8_t *)&t301 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t301 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t301 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t301 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t301 + 32, 0);
            ((uint8_t *)&t301)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t301 + 8), t300);
            gm_t5o0tpv5jka2l t302 = t301;
            uint8_t t303 = ((((uint8_t *)&t302))[0] != 0);
            F.ret = t301;
            goto gm_fret;
        }
        goto ix297;
ie297:;
ix297:;
    }
    {
        void * t304 = gm_l0;
        gm_t88rcucg1b7uk t305 = gm_l5;
        gm_b16_8 t306 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t304, t305);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t307;
        GM_ST(uint8_t, (uint8_t *)&t307 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t307 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t307 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t307 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t307 + 32, 0);
        ((uint8_t *)&t307)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t307 + 8), t306);
        gm_t5o0tpv5jka2l t308 = t307;
        uint8_t t309 = ((((uint8_t *)&t308))[0] != 0);
        F.ret = t307;
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
inline __attribute__((aligned(4))) void gm_f_detached_nd71egs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_closure a3) {
    struct gm_fr_detached_nd71egs F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* head */
    gm_b16_8 gm_l2; /* cx */
    gm_closure gm_l3; /* emit */
    GM_STACK_CHECK(&F, 541u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l2;
        gm_f_save_2j2i9g6((void *)&F.l4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = gm_l0;
        void * t5 = (void *)(((uint8_t *)t4) + 184);
        gm_b16_8 t6 = gm_l1;
        gm_tnpifv20irnph t7;
        GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t6);
        gm_tftb1odc739t5 t8 = gm_f_write_7oqfkkd(t5, t7);
        gm_tftb1odc739t5 t9 = t8;
        if (((((uint8_t *)&t9))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t9) + 1, gm_srcihhtlsvd7f6i, 18, 5);
        gm_b0_1 t10 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
    }
    {
        gm_closure t11 = gm_l3;
        gm_closure t12 = t11;
        gm_node *ccx13 = t12.cx;
        gm_node cn14; { gm_node *f = gm_find(cx, 587034433); if (f) { cn14 = *f; cn14.next = ccx13; ccx13 = &cn14; } }
        gm_node cn15; { gm_node *f = gm_find(cx, 2126215853); if (f) { cn15 = *f; cn15.next = ccx13; ccx13 = &cn15; } }
        void * t16 = t12.code;
        if (GM_PTR_TAGGED(t16)) {
            F.l5 = ((gm_tqgtd5gtgj65s (*)(void))GM_PTR_UNTAG(t16))();
        } else {
            ((void (*)(void *, void *, gm_node *, gm_node *))t16)((void *)&F.l5, t12.env, ccx13, NULL);
        }
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t17 = gm_l0;
        gm_b0_1 t18 = gm_f_end_lines_99v0bhd((void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t19 = gm_l0;
        gm_trqfvmjtt4sij t20 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)t19) + 184));
        F.l6 = t20;
    }
    {
        void * t21 = gm_l0;
        gm_f_restore_28da45i(t21, &(*(gm_tjfrcdrnp2bat *)((uint8_t *)&F.l4)));
    }
    {
        gm_tqgtd5gtgj65s t22 = F.l5;
        gm_tqgtd5gtgj65s t23 = t22;
        if (((((uint8_t *)&t23))[0] != 0)) {
            gm_t08olq97rscn3 t24 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t23) + 8));
            gm_t5o0tpv5jka2l t25;
            GM_ST(uint8_t, (uint8_t *)&t25 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t25 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t25 + 4, 0);
            ((uint8_t *)&t25)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t25 + 8), t24);
            F.ret = t25;
            goto gm_fret;
        }
        gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 8));
    }
    {
        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t28 = t27;
        gm_b16_8 t29 = GM_LD(gm_b16_8, ((uint8_t *)&t28));
        gm_t5o0tpv5jka2l t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t30 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t30 + 32, 0);
        ((uint8_t *)&t30)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t30 + 8), t29);
        gm_t5o0tpv5jka2l t31 = t30;
        uint8_t t32 = ((((uint8_t *)&t31))[0] != 0);
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
__attribute__((aligned(4))) void gm_f_emit_l3j4r4e(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_emit_l3j4r4e F;
    gm_b16_8 gm_l0; /* fr */
    gm_b16_8 gm_l1; /* n */
    gm_b16_8 gm_l2; /* fname */
    gm_b16_8 gm_l3; /* v */
    GM_STACK_CHECK(&F, 929u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        {
            void * t3 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l0));
            void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
            uint32_t t5 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l5));
            gm_b16_8 t6 = gm_f_frame_name_stddmgg((void *)NULL, cx, NULL, t4, t5);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l0 = t6;
        }
        {
            void * t7 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l0));
            gm_t6j6smj7qoctu t8;
            gm_b16_8 t9 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t8 + 0, t9);
            gm_b16_8 t10 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t8 + 16, t10);
            gm_b0_1 t11 = gm_f_linef_n1p06ei((void *)NULL, cx, NULL, t7, &t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t12 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l0));
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_ss3op0i298nem));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(68));
            gm_b0_1 t14 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t12, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t15 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l0));
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_ss3kah49g222u));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(32));
            gm_b0_1 t17 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t19 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l0));
            void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
            uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t20) + 1136) + 1));
            if (!t21) goto ie18;
            {
                gm_b16_8 t23;
                void * t25 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l0));
                void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
                void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
                void * t28 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l0));
                uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)t28) + 8));
                gm_th2lqbo77pvsc t30;
                gm_f_backtrace_name_mars429((void *)&t30, (void *)NULL, cx, NULL, t27, t29);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t31 = t30;
                if (!((((uint8_t *)&t31))[16] != 0)) goto ie24;
                gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
                gm_l1 = t32;
                gm_b16_8 t34 = gm_l1;
                gm_b16_8 t35 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t34);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t36 = t35;
                t23 = t36;
                goto ix24;
ie24:;
                gm_b16_8 t38;
                GM_ST(void *, (uint8_t *)&t38, (void *)(gm_s04qus0luoga5));
                GM_ST(uintptr_t, (uint8_t *)&t38 + 8, (uintptr_t)(4));
                gm_b16_8 t39 = t38;
                t23 = t39;
ix24:;
                gm_l2 = t23;
            }
            {
                void * t40 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l0));
                gm_b16_8 t41 = gm_l2;
                gm_tnpifv20irnph t42;
                GM_ST(gm_b16_8, ((uint8_t *)&t42 + 0), t41);
                gm_b0_1 t43 = gm_f_linef_reujofp((void *)NULL, cx, NULL, t40, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix18;
ie18:;
ix18:;
        }
        {
            void * t44 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l0));
            void * t45 = (void *)(((uint8_t *)t44) + 184);
            gm_b16_8 t46;
            GM_ST(void *, (uint8_t *)&t46, (void *)(gm_sv89ta651gq7u));
            GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(19));
            gm_tftb1odc739t5 t47 = gm_f_extend_0cs85gs(t45, t46);
            gm_tftb1odc739t5 t48 = t47;
            if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srcihhtlsvd7f6i, 88, 9);
            gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
        }
        {
            void * t50 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l0));
            void * t51 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l3));
            gm_t5o0tpv5jka2l t52;
            gm_f_expr_porbc9t((void *)&t52, (void *)NULL, cx, NULL, t50, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t53 = t52;
            if (((((uint8_t *)&t53))[0] != 0)) {
                gm_t08olq97rscn3 t54 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t53) + 8));
                gm_tqgtd5gtgj65s t55;
                GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
                ((uint8_t *)&t55)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t55 + 8), t54);
                F.ret = t55;
                goto gm_fret;
            }
            gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&t53) + 8));
            gm_l3 = t56;
        }
        {
            void * t58 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l0));
            void * t59 = GM_LD(void *, (((uint8_t *)t58) + 0));
            void * t60 = (void *)(((uint8_t *)t59) + 8);
            void * t61 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l3));
            gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t61) + 0));
            uint8_t t63 = gm_f_is_never_in5ftma(t60, GM_CONV(gm_csguankko4lmhd, t62));
            uint8_t t64 = (uint8_t)!t63;
            if (!t64) goto ie57;
            void * t65 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l0));
            void * t66 = GM_LD(void *, (((uint8_t *)t65) + 0));
            void * t67 = (void *)(((uint8_t *)t66) + 8);
            gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l2));
            uint8_t t69 = gm_f_is_never_in5ftma(t67, GM_CONV(gm_csguankko4lmhd, t68));
            uint8_t t70 = (uint8_t)!t69;
            if (!t70) goto ie57;
            {
                void * t72 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l0));
                gm_tiilmfd16v3fc t73;
                memset((uint8_t *)&t73 + 4, 0, 4);
                uint32_t t74 = GM_LD(uint32_t, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l1));
                GM_ST(uint32_t, (uint8_t *)&t73 + 0, t74);
                gm_b16_8 t75 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t73 + 8, t75);
                gm_b0_1 t76 = gm_f_linef_m4ia8qg((void *)NULL, cx, NULL, t72, &t73);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix57;
ie57:;
ix57:;
        }
        {
            void * t77 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_outline_body_tsr6q2p *)((&F.h)->up))->l0));
            void * t78 = (void *)(((uint8_t *)t77) + 184);
            gm_b16_8 t79;
            GM_ST(void *, (uint8_t *)&t79, (void *)(gm_sq05bfmao5v8n));
            GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(46));
            gm_tftb1odc739t5 t80 = gm_f_extend_0cs85gs(t78, t79);
            gm_tftb1odc739t5 t81 = t80;
            if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srcihhtlsvd7f6i, 93, 9);
            gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
        }
    }
    gm_b0_1 t83;
    gm_tqgtd5gtgj65s t84;
    memset(&t84, 0, sizeof t84);
    ((uint8_t *)&t84)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t84 + 8), t83);
    gm_tqgtd5gtgj65s t85 = t84;
    F.ret = t85;
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
inline __attribute__((aligned(4))) void gm_f_emit_vkr7eg2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_emit_vkr7eg2 F;
    gm_b16_8 gm_l0; /* arg0 */
    gm_b16_8 gm_l1; /* arg */
    gm_tpmdr2hmdhsqr gm_l2; /* vals */
    gm_tnqk2qt7j42o9 gm_l3; /* tys */
    GM_STACK_CHECK(&F, 654u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        {
            gm_b16_8 t3;
            uint64_t t5 = GM_LD(uint64_t, ((uint8_t *)&((struct gm_fr_fiber_builtin_bgfcp56 *)((&F.h)->up))->l14));
            uint8_t t6 = ((t5 == ((uint64_t)0ULL)));
            if (!t6) goto ie4;
            void * t8 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fiber_builtin_bgfcp56 *)((&F.h)->up))->l0));
            gm_t88rcucg1b7uk t9 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_fiber_builtin_bgfcp56 *)((&F.h)->up))->l12));
            gm_b16_8 t10 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t8, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t11 = t10;
            t3 = t11;
            goto ix4;
ie4:;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_fiber_builtin_bgfcp56 *)((&F.h)->up))->l13));
            gm_tnpifv20irnph t14;
            GM_ST(gm_b16_8, ((uint8_t *)&t14 + 0), t13);
            gm_tabb710d9mq2e t15;
            gm_f_format_kpd1ovq((void *)&t15, (void *)NULL, cx, NULL, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t16 = t15;
            if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_srcihhtlsvd7f6i, 195, 54);
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
            gm_b16_8 t18 = t17;
            gm_b16_8 t19 = t18;
            gm_b16_8 t20 = GM_LD(gm_b16_8, ((uint8_t *)&t19));
            t3 = t20;
ix4:;
            gm_l0 = t3;
        }
        {
            void * t21 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fiber_builtin_bgfcp56 *)((&F.h)->up))->l0));
            gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_fiber_builtin_bgfcp56 *)((&F.h)->up))->l12));
            gm_b16_8 t23 = gm_l0;
            gm_b16_8 t24 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t21, t22, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l1 = t24;
        }
        {
            gm_b16_8 t25 = gm_l1;
            gm_tpmdr2hmdhsqr t26;
            GM_ST(gm_b16_8, ((uint8_t *)&t26 + 0), t25);
            gm_l2 = t26;
        }
        {
            gm_t88rcucg1b7uk t27 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_fiber_builtin_bgfcp56 *)((&F.h)->up))->l12));
            gm_tnqk2qt7j42o9 t28;
            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t28 + 0), t27);
            gm_l3 = t28;
        }
        {
            {
                void * t30 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fiber_builtin_bgfcp56 *)((&F.h)->up))->l0));
                gm_b16_8 t31;
                GM_ST(void *, (uint8_t *)&t31, (void *)(gm_srjnkb77s24ld));
                GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)(6));
                gm_b16_8 t32;
                GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s04qus0luoga5));
                GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(4));
                gm_b16_8 t33;
                GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s3bsv9idfdodt));
                GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(6));
                void * t34 = ((uint8_t *)&gm_l2);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcihhtlsvd7f6i, 199, 58);
                gm_b16_8 t35;
                GM_ST(void *, (uint8_t *)&t35, (void *)(((uint8_t *)t34 + (uintptr_t)0 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_b16_8 t36 = t35;
                gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
                void * t38 = ((uint8_t *)&gm_l3);
                if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcihhtlsvd7f6i, 199, 68);
                gm_b16_8 t39;
                GM_ST(void *, (uint8_t *)&t39, (void *)(((uint8_t *)t38 + (uintptr_t)0 * 4)));
                GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
                gm_b16_8 t40 = t39;
                gm_b16_8 t41 = GM_LD(gm_b16_8, ((uint8_t *)&t40));
                gm_t88rcucg1b7uk t42 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&((struct gm_fr_fiber_builtin_bgfcp56 *)((&F.h)->up))->l17));
                uint8_t t43 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_fiber_builtin_bgfcp56 *)((&F.h)->up))->l5));
                gm_b16_8 t44 = gm_f_call_indirect_khk02ee((void *)NULL, cx, NULL, t30, t31, t32, t33, t37, t41, t42, t43, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
        }
        {
            void * t45 = GM_LD(void *, ((uint8_t *)&((struct gm_fr_fiber_builtin_bgfcp56 *)((&F.h)->up))->l0));
            void * t46 = (void *)(((uint8_t *)t45) + 184);
            gm_b16_8 t47;
            GM_ST(void *, (uint8_t *)&t47, (void *)(gm_sikpqtja8pqpv));
            GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t48 = gm_f_extend_0cs85gs(t46, t47);
            gm_tftb1odc739t5 t49 = t48;
            if (((((uint8_t *)&t49))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t49) + 1, gm_srcihhtlsvd7f6i, 200, 9);
            gm_b0_1 t50 = GM_LD(gm_b0_1, (((uint8_t *)&t49) + 1));
        }
    }
    gm_b0_1 t51;
    gm_tqgtd5gtgj65s t52;
    memset(&t52, 0, sizeof t52);
    ((uint8_t *)&t52)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t52 + 8), t51);
    gm_tqgtd5gtgj65s t53 = t52;
    F.ret = t53;
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
__attribute__((aligned(4))) void gm_f_fiber_builtin_bgfcp56(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2) {
    struct gm_fr_fiber_builtin_bgfcp56 F;
    void * gm_l1; /* e */
    gm_b16_8 gm_l2; /* args */
    gm_b16_8 gm_l6; /* f */
    gm_b16_8 gm_l7; /* a */
    gm_b16_8 gm_l8; /* s */
    gm_b16_8 gm_l9; /* sa */
    gm_b16_8 gm_l10; /* p */
    gm_b16_8 gm_l11; /* l */
    uint32_t gm_l15; /* k */
    gm_b16_8 gm_l16; /* start */
    gm_closure gm_l18; /* emit */
    gm_b16_8 gm_l19; /* text */
    gm_b16_8 gm_l20; /* loc */
    gm_b16_8 gm_l21; /* fb */
    gm_b16_8 gm_l22; /* kc */
    gm_b16_8 gm_l23; /* c */
    GM_STACK_CHECK(&F, 3182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = F.l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t5 >= (uintptr_t)(t6)) gm_trap_index(t5, t6, gm_srcihhtlsvd7f6i, 177, 31);
        gm_t88rcucg1b7uk t7 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t5 * 88) + 0));
        gm_t53at80pfdus3 t8;
        gm_f_kind_ghvav22((void *)&t8, t4, GM_CONV(gm_csguankko4lmhd, t7));
        gm_t53at80pfdus3 t9 = t8;
        gm_tddtjfggdat0d t10;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t9) + 0));
        if (t13 != ((uint8_t)16ULL)) goto mn12;
        gm_tddtjfggdat0d t14 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t9) + 8));
        F.l3 = t14;
        gm_tddtjfggdat0d t15 = F.l3;
        t10 = t15;
        goto me11;
mn12:;
        void * t17 = gm_l1;
        gm_tl874t2atnlh3 t18 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t17) + 72));
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_sr0u787bpqkqg));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(27));
        gm_t08olq97rscn3 t20;
        gm_f_unsupported_531q51r((void *)&t20, (void *)NULL, cx, NULL, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t21 + 4, 0);
        ((uint8_t *)&t21)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t21 + 8), t20);
        gm_t5o0tpv5jka2l t22 = t21;
        uint8_t t23 = ((((uint8_t *)&t22))[0] != 0);
        F.ret = t21;
        goto gm_fret;
        goto me11;
mn16:;
        gm_trap("no match arm matched the scrutinee", gm_srcihhtlsvd7f6i, 177, 12);
me11:;
        F.l4 = t10;
    }
    {
        void * t24 = F.l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = (void *)(((uint8_t *)&F.l4) + 24);
        uint8_t t27 = gm_f_within_sys_2lc2ugq(t25, t26);
        uint8_t t28 = (uint8_t)!t27;
        F.l5 = t28;
    }
    {
        void * t29 = F.l0;
        uint64_t t30 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcihhtlsvd7f6i, 182, 23);
        void * t32 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t30 * 88);
        gm_t5o0tpv5jka2l t33;
        gm_f_expr_porbc9t((void *)&t33, (void *)NULL, cx, NULL, t29, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t34 = t33;
        if (((((uint8_t *)&t34))[0] != 0)) {
            gm_t08olq97rscn3 t35 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t34) + 8));
            gm_t5o0tpv5jka2l t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
            ((uint8_t *)&t36)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t36 + 8), t35);
            F.ret = t36;
            goto gm_fret;
        }
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t34) + 8));
        gm_l6 = t37;
    }
    {
        void * t38 = F.l0;
        uint64_t t39 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t39 >= (uintptr_t)(t40)) gm_trap_index(t39, t40, gm_srcihhtlsvd7f6i, 183, 23);
        void * t41 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t39 * 88);
        gm_t5o0tpv5jka2l t42;
        gm_f_expr_porbc9t((void *)&t42, (void *)NULL, cx, NULL, t38, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t43 = t42;
        if (((((uint8_t *)&t43))[0] != 0)) {
            gm_t08olq97rscn3 t44 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t43) + 8));
            gm_t5o0tpv5jka2l t45;
            GM_ST(uint8_t, (uint8_t *)&t45 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t45 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t45 + 4, 0);
            ((uint8_t *)&t45)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t45 + 8), t44);
            F.ret = t45;
            goto gm_fret;
        }
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
        gm_l7 = t46;
    }
    {
        void * t47 = F.l0;
        uint64_t t48 = (uintptr_t)((uint64_t)2ULL);
        uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t48 >= (uintptr_t)(t49)) gm_trap_index(t48, t49, gm_srcihhtlsvd7f6i, 184, 23);
        void * t50 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t48 * 88);
        gm_t5o0tpv5jka2l t51;
        gm_f_expr_porbc9t((void *)&t51, (void *)NULL, cx, NULL, t47, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t52 = t51;
        if (((((uint8_t *)&t52))[0] != 0)) {
            gm_t08olq97rscn3 t53 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t52) + 8));
            gm_t5o0tpv5jka2l t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 4, 0);
            ((uint8_t *)&t54)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t54 + 8), t53);
            F.ret = t54;
            goto gm_fret;
        }
        gm_b16_8 t55 = GM_LD(gm_b16_8, (((uint8_t *)&t52) + 8));
        gm_l8 = t55;
    }
    {
        void * t56 = F.l0;
        uint64_t t57 = (uintptr_t)((uint64_t)2ULL);
        uint64_t t58 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t57 >= (uintptr_t)(t58)) gm_trap_index(t57, t58, gm_srcihhtlsvd7f6i, 185, 22);
        gm_t88rcucg1b7uk t59 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t57 * 88) + 0));
        gm_b16_8 t60 = gm_l8;
        gm_b16_8 t61 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t56, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t61;
    }
    {
        gm_b16_8 t62 = gm_l9;
        gm_b16_8 t63 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t63;
    }
    {
        void * t64 = F.l0;
        gm_b16_8 t65 = gm_l9;
        gm_b16_8 t66 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t64, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t66;
    }
    {
        uint64_t t67 = (uintptr_t)((uint64_t)1ULL);
        uint64_t t68 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t67 >= (uintptr_t)(t68)) gm_trap_index(t67, t68, gm_srcihhtlsvd7f6i, 188, 12);
        gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t67 * 88) + 0));
        F.l12 = t69;
    }
    {
        void * t70 = F.l0;
        void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
        void * t72 = (void *)(((uint8_t *)t71) + 8);
        gm_t88rcucg1b7uk t73 = F.l12;
        gm_b16_8 t74 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t72, t73);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l13 = t74;
    }
    {
        void * t75 = F.l0;
        void * t76 = GM_LD(void *, (((uint8_t *)t75) + 0));
        void * t77 = (void *)(((uint8_t *)t76) + 8);
        gm_t88rcucg1b7uk t78 = F.l12;
        uint64_t t79 = gm_f_size_dmhtkib((void *)NULL, cx, NULL, t77, t78);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l14 = t79;
    }
    {
        void * t80 = F.l0;
        uint32_t t81 = gm_f_fresh_5kokjmg(t80);
        gm_l15 = t81;
    }
    {
        gm_tommf4j9upomc t82;
        memset((uint8_t *)&t82 + 20, 0, 4);
        void * t83 = F.l0;
        gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)t83) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t82 + 0, t84);
        uint32_t t85 = gm_l15;
        GM_ST(uint32_t, (uint8_t *)&t82 + 16, t85);
        gm_tabb710d9mq2e t86;
        gm_f_format_tsutrj7((void *)&t86, (void *)NULL, cx, NULL, &t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t87 = t86;
        if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 8, gm_srcihhtlsvd7f6i, 192, 14);
        gm_b16_8 t88 = GM_LD(gm_b16_8, (((uint8_t *)&t87) + 8));
        gm_l16 = t88;
    }
    {
        void * t89 = F.l0;
        void * t90 = GM_LD(void *, (((uint8_t *)t89) + 0));
        void * t91 = GM_LD(void *, ((((uint8_t *)t90) + 8) + 0));
        void * t92 = (void *)(((uint8_t *)t91) + 8);
        gm_t88rcucg1b7uk t93 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t92);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        F.l17 = t93;
    }
    {
        gm_closure t94 = ((gm_closure){(void *)gm_f_emit_vkr7eg2, (void *)&F.h, cx});
        gm_l18 = t94;
    }
    {
        void * t95 = F.l0;
        gm_b16_8 t96 = gm_l16;
        gm_tnpifv20irnph t97;
        GM_ST(gm_b16_8, ((uint8_t *)&t97 + 0), t96);
        gm_tabb710d9mq2e t98;
        gm_f_format_vfrm49j((void *)&t98, (void *)NULL, cx, NULL, t97);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t99 = t98;
        if (((((uint8_t *)&t99))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t99) + 8, gm_srcihhtlsvd7f6i, 202, 30);
        gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&t99) + 8));
        gm_b16_8 t101 = t100;
        gm_b16_8 t102 = GM_LD(gm_b16_8, ((uint8_t *)&t101));
        gm_b16_8 t103;
        GM_ST(void *, (uint8_t *)&t103, (void *)(gm_s3bsv9idfdodt));
        GM_ST(uintptr_t, (uint8_t *)&t103 + 8, (uintptr_t)(6));
        gm_closure t104 = gm_l18;
        gm_t5o0tpv5jka2l t105;
        gm_f_detached_nd71egs((void *)&t105, (void *)NULL, cx, NULL, t95, t102, t103, t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t106 = t105;
        if (((((uint8_t *)&t106))[0] != 0)) {
            gm_t08olq97rscn3 t107 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t106) + 8));
            gm_t5o0tpv5jka2l t108;
            GM_ST(uint8_t, (uint8_t *)&t108 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t108 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t108 + 4, 0);
            ((uint8_t *)&t108)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t108 + 8), t107);
            F.ret = t108;
            goto gm_fret;
        }
        gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&t106) + 8));
        gm_l19 = t109;
    }
    {
        void * t110 = F.l0;
        void * t111 = GM_LD(void *, (((uint8_t *)t110) + 0));
        void * t112 = (void *)(((uint8_t *)t111) + 1888);
        gm_b16_8 t113 = gm_l16;
        gm_tnpifv20irnph t114;
        GM_ST(gm_b16_8, ((uint8_t *)&t114 + 0), t113);
        gm_tftb1odc739t5 t115 = gm_f_write_upr5k5n(t112, t114);
        gm_tftb1odc739t5 t116 = t115;
        if (((((uint8_t *)&t116))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t116) + 1, gm_srcihhtlsvd7f6i, 203, 5);
        gm_b0_1 t117 = GM_LD(gm_b0_1, (((uint8_t *)&t116) + 1));
    }
    {
        void * t118 = F.l0;
        void * t119 = GM_LD(void *, (((uint8_t *)t118) + 0));
        void * t120 = (void *)(((uint8_t *)t119) + 1928);
        gm_b16_8 t121 = gm_l19;
        gm_tftb1odc739t5 t122 = gm_f_extend_0cs85gs(t120, t121);
        gm_tftb1odc739t5 t123 = t122;
        if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 1, gm_srcihhtlsvd7f6i, 204, 5);
        gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t123) + 1));
    }
    {
        void * t125 = F.l0;
        void * t126 = gm_l1;
        gm_b16_8 t127 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t125, t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l20 = t127;
    }
    {
        void * t128 = F.l0;
        uint32_t t129 = gm_f_fresh_5kokjmg(t128);
        gm_t88rcucg1b7uk t130;
        GM_ST(uint32_t, ((uint8_t *)&t130 + 0), t129);
        gm_tabb710d9mq2e t131;
        gm_f_format_dr8c0gq((void *)&t131, (void *)NULL, cx, NULL, t130);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t132 = t131;
        if (((((uint8_t *)&t132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t132) + 8, gm_srcihhtlsvd7f6i, 206, 11);
        gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&t132) + 8));
        gm_l21 = t133;
    }
    {
        void * t134 = F.l0;
        gm_tsqpr25pef7fd t135;
        gm_b16_8 t136 = gm_l21;
        GM_ST(gm_b16_8, (uint8_t *)&t135 + 0, t136);
        gm_b16_8 t137 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t135 + 16, t137);
        gm_b16_8 t138 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t135 + 32, t138);
        gm_b16_8 t139 = gm_l16;
        GM_ST(gm_b16_8, (uint8_t *)&t135 + 48, t139);
        uint64_t t140 = F.l14;
        GM_ST(uint64_t, (uint8_t *)&t135 + 64, t140);
        gm_b16_8 t141 = gm_l20;
        GM_ST(gm_b16_8, (uint8_t *)&t135 + 72, t141);
        gm_b0_1 t142 = gm_f_linef_viqjr83((void *)NULL, cx, NULL, t134, &t135);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t143 = F.l0;
        gm_tfeab279c16rr t144;
        gm_b16_8 t145 = gm_l21;
        GM_ST(gm_b16_8, (uint8_t *)&t144 + 0, t145);
        gm_b16_8 t146 = gm_l6;
        GM_ST(gm_b16_8, (uint8_t *)&t144 + 16, t146);
        gm_b16_8 t147 = gm_l21;
        GM_ST(gm_b16_8, (uint8_t *)&t144 + 32, t147);
        gm_b0_1 t148 = gm_f_linef_qm581s9((void *)NULL, cx, NULL, t143, &t144);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t150 = F.l14;
        uint8_t t151 = (t150 > ((uint64_t)0ULL));
        if (!t151) goto ie149;
        {
            void * t153 = F.l0;
            gm_tfeab279c16rr t154;
            gm_b16_8 t155 = F.l13;
            GM_ST(gm_b16_8, (uint8_t *)&t154 + 0, t155);
            gm_b16_8 t156 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t154 + 16, t156);
            gm_b16_8 t157 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t154 + 32, t157);
            gm_b0_1 t158 = gm_f_linef_t52q40t((void *)NULL, cx, NULL, t153, &t154);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix149;
ie149:;
ix149:;
    }
    {
        void * t159 = F.l0;
        uint32_t t160 = gm_f_fresh_5kokjmg(t159);
        gm_t88rcucg1b7uk t161;
        GM_ST(uint32_t, ((uint8_t *)&t161 + 0), t160);
        gm_tabb710d9mq2e t162;
        gm_f_format_9s56ahi((void *)&t162, (void *)NULL, cx, NULL, t161);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t163 = t162;
        if (((((uint8_t *)&t163))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t163) + 8, gm_srcihhtlsvd7f6i, 212, 11);
        gm_b16_8 t164 = GM_LD(gm_b16_8, (((uint8_t *)&t163) + 8));
        gm_l22 = t164;
    }
    {
        void * t165 = F.l0;
        gm_t6j6smj7qoctu t166;
        gm_b16_8 t167 = gm_l22;
        GM_ST(gm_b16_8, (uint8_t *)&t166 + 0, t167);
        gm_b16_8 t168 = gm_l21;
        GM_ST(gm_b16_8, (uint8_t *)&t166 + 16, t168);
        gm_b0_1 t169 = gm_f_linef_ku7hk5r((void *)NULL, cx, NULL, t165, &t166);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t170 = F.l0;
        void * t171 = GM_LD(void *, (((uint8_t *)t170) + 0));
        void * t172 = (void *)(((uint8_t *)t171) + 8);
        void * t173 = gm_l1;
        gm_t88rcucg1b7uk t174 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t173) + 0));
        gm_b16_8 t175 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t172, t174);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l23 = t175;
    }
    {
        void * t176 = F.l0;
        void * t177 = gm_l1;
        gm_t88rcucg1b7uk t178 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t177) + 0));
        gm_t6j6smj7qoctu t179;
        gm_b16_8 t180 = gm_l23;
        GM_ST(gm_b16_8, (uint8_t *)&t179 + 0, t180);
        gm_b16_8 t181 = gm_l22;
        GM_ST(gm_b16_8, (uint8_t *)&t179 + 16, t181);
        gm_tabb710d9mq2e t182;
        gm_f_format_5j9lqr8((void *)&t182, (void *)NULL, cx, NULL, &t179);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t183 = t182;
        if (((((uint8_t *)&t183))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t183) + 8, gm_srcihhtlsvd7f6i, 215, 25);
        gm_b16_8 t184 = GM_LD(gm_b16_8, (((uint8_t *)&t183) + 8));
        gm_b16_8 t185 = t184;
        gm_b16_8 t186 = GM_LD(gm_b16_8, ((uint8_t *)&t185));
        gm_b16_8 t187 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t176, t178, t186);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t188;
        GM_ST(uint8_t, (uint8_t *)&t188 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t188 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t188 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t188 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t188 + 32, 0);
        ((uint8_t *)&t188)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t188 + 8), t187);
        gm_t5o0tpv5jka2l t189 = t188;
        uint8_t t190 = ((((uint8_t *)&t189))[0] != 0);
        F.ret = t188;
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
inline __attribute__((aligned(4))) void gm_f_outline_body_tsr6q2p(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_t88rcucg1b7uk a2, void * a3) {
    struct gm_fr_outline_body_tsr6q2p F;
    gm_b16_8 gm_l4; /* name */
    uint8_t gm_l6; /* saved */
    gm_closure gm_l7; /* emit */
    gm_b16_8 gm_l9; /* t */
    GM_STACK_CHECK(&F, 812u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    F.l0 = a0;
    F.l1 = a1;
    F.l2 = a2;
    F.l3 = a3;
    {
        gm_tommf4j9upomc t2;
        memset((uint8_t *)&t2 + 20, 0, 4);
        void * t3 = F.l0;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t4);
        uint32_t t5 = F.l1;
        GM_ST(uint32_t, (uint8_t *)&t2 + 16, t5);
        gm_tabb710d9mq2e t6;
        gm_f_format_mepvg5e((void *)&t6, (void *)NULL, cx, NULL, &t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcihhtlsvd7f6i, 73, 13);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_l4 = t8;
    }
    {
        void * t9 = F.l0;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 8));
        F.l5 = t10;
    }
    {
        void * t11 = F.l0;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)t11) + 298));
        gm_l6 = t12;
    }
    {
        void * t13 = F.l0;
        GM_ST(uint8_t, (((uint8_t *)t13) + 298), ((uint8_t)1));
    }
    {
        gm_closure t14 = ((gm_closure){(void *)gm_f_emit_l3j4r4e, (void *)&F.h, cx});
        gm_l7 = t14;
    }
    {
        void * t15 = F.l0;
        gm_b16_8 t16 = gm_l4;
        gm_tnpifv20irnph t17;
        GM_ST(gm_b16_8, ((uint8_t *)&t17 + 0), t16);
        gm_tabb710d9mq2e t18;
        gm_f_format_vfrm49j((void *)&t18, (void *)NULL, cx, NULL, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t19 = t18;
        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 8, gm_srcihhtlsvd7f6i, 95, 26);
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t19) + 8));
        gm_b16_8 t21 = t20;
        gm_b16_8 t22 = GM_LD(gm_b16_8, ((uint8_t *)&t21));
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_s10iv34fu7qe7));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(2));
        gm_closure t24 = gm_l7;
        gm_f_detached_nd71egs((void *)&F.l8, (void *)NULL, cx, NULL, t15, t22, t23, t24);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t25 = F.l0;
        uint8_t t26 = gm_l6;
        GM_ST(uint8_t, (((uint8_t *)t25) + 298), t26);
    }
    {
        gm_t5o0tpv5jka2l t27 = F.l8;
        gm_t5o0tpv5jka2l t28 = t27;
        if (((((uint8_t *)&t28))[0] != 0)) {
            gm_t08olq97rscn3 t29 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t28) + 8));
            gm_t5o0tpv5jka2l t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
            ((uint8_t *)&t30)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t30 + 8), t29);
            F.ret = t30;
            goto gm_fret;
        }
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
        gm_l9 = t31;
    }
    {
        void * t32 = F.l0;
        void * t33 = GM_LD(void *, (((uint8_t *)t32) + 0));
        void * t34 = (void *)(((uint8_t *)t33) + 1888);
        gm_b16_8 t35 = gm_l4;
        gm_tnpifv20irnph t36;
        GM_ST(gm_b16_8, ((uint8_t *)&t36 + 0), t35);
        gm_tftb1odc739t5 t37 = gm_f_write_upr5k5n(t34, t36);
        gm_tftb1odc739t5 t38 = t37;
        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srcihhtlsvd7f6i, 98, 5);
        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
    }
    {
        void * t40 = F.l0;
        void * t41 = GM_LD(void *, (((uint8_t *)t40) + 0));
        void * t42 = (void *)(((uint8_t *)t41) + 1928);
        gm_b16_8 t43 = gm_l9;
        gm_tftb1odc739t5 t44 = gm_f_extend_0cs85gs(t42, t43);
        gm_tftb1odc739t5 t45 = t44;
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srcihhtlsvd7f6i, 99, 5);
        gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
    }
    {
        gm_b16_8 t47 = gm_l4;
        gm_b16_8 t48 = t47;
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
        gm_t5o0tpv5jka2l t50;
        GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t50 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t50 + 32, 0);
        ((uint8_t *)&t50)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t50 + 8), t49);
        gm_t5o0tpv5jka2l t51 = t50;
        uint8_t t52 = ((((uint8_t *)&t51))[0] != 0);
        F.ret = t50;
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
__attribute__((aligned(4))) void gm_f_resume__rhq91rs(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, void * a3) {
    struct gm_fr_resume__rhq91rs F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* kont */
    void * gm_l3; /* value */
    gm_b16_8 gm_l4; /* k */
    gm_b16_8 gm_l5; /* ka */
    gm_b16_8 gm_l6; /* v */
    gm_b16_8 gm_l7; /* loc */
    gm_b16_8 gm_l8; /* rp */
    gm_b16_8 gm_l9; /* c */
    GM_STACK_CHECK(&F, 1171u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l2;
        gm_t5o0tpv5jka2l t4;
        gm_f_expr_porbc9t((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_t08olq97rscn3 t6 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t5) + 8));
            gm_t5o0tpv5jka2l t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l4 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l2;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 0));
        gm_b16_8 t12 = gm_l4;
        gm_b16_8 t13 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t9, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t13;
    }
    {
        void * t14 = gm_l0;
        void * t15 = gm_l3;
        gm_t5o0tpv5jka2l t16;
        gm_f_expr_porbc9t((void *)&t16, (void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t17 = t16;
        if (((((uint8_t *)&t17))[0] != 0)) {
            gm_t08olq97rscn3 t18 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t17) + 8));
            gm_t5o0tpv5jka2l t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            ((uint8_t *)&t19)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t19 + 8), t18);
            F.ret = t19;
            goto gm_fret;
        }
        gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&t17) + 8));
        gm_l6 = t20;
    }
    {
        void * t21 = gm_l0;
        void * t22 = gm_l1;
        gm_b16_8 t23 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t23;
    }
    {
        void * t24 = gm_l0;
        uint32_t t25 = gm_f_fresh_5kokjmg(t24);
        gm_t88rcucg1b7uk t26;
        GM_ST(uint32_t, ((uint8_t *)&t26 + 0), t25);
        gm_tabb710d9mq2e t27;
        gm_f_format_bs5ht1e((void *)&t27, (void *)NULL, cx, NULL, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t28 = t27;
        if (((((uint8_t *)&t28))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t28) + 8, gm_srcihhtlsvd7f6i, 153, 11);
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
        gm_l8 = t29;
    }
    {
        void * t30 = gm_l0;
        gm_tfeab279c16rr t31;
        gm_b16_8 t32 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t31 + 0, t32);
        gm_b16_8 t33 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t31 + 16, t33);
        gm_b16_8 t34 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t31 + 32, t34);
        gm_b0_1 t35 = gm_f_linef_gfhlos1((void *)NULL, cx, NULL, t30, &t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t37 = gm_l0;
        void * t38 = GM_LD(void *, (((uint8_t *)t37) + 0));
        void * t39 = (void *)(((uint8_t *)t38) + 8);
        void * t40 = gm_l3;
        gm_t88rcucg1b7uk t41 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t40) + 0));
        uint8_t t42 = gm_f_is_void_vs2outo(t39, GM_CONV(gm_csguankko4lmhd, t41));
        if (!t42) goto ie36;
        {
            void * t44 = gm_l0;
            gm_b16_8 t45 = gm_l8;
            gm_tnpifv20irnph t46;
            GM_ST(gm_b16_8, ((uint8_t *)&t46 + 0), t45);
            gm_b0_1 t47 = gm_f_linef_gp6bqvn((void *)NULL, cx, NULL, t44, t46);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix36;
ie36:;
        {
            void * t49 = gm_l0;
            void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
            void * t51 = (void *)(((uint8_t *)t50) + 8);
            void * t52 = gm_l3;
            gm_t88rcucg1b7uk t53 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t52) + 0));
            gm_b16_8 t54 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t51, t53);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l9 = t54;
        }
        {
            void * t55 = gm_l0;
            gm_tfeab279c16rr t56;
            gm_b16_8 t57 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t56 + 0, t57);
            gm_b16_8 t58 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t56 + 16, t58);
            gm_b16_8 t59 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t56 + 32, t59);
            gm_b0_1 t60 = gm_f_linef_gu3mj45((void *)NULL, cx, NULL, t55, &t56);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix36:;
    }
    {
        void * t61 = gm_l0;
        gm_t9qkh0rsooc8d t62;
        GM_ST(uint8_t, ((uint8_t *)&t62 + 0), ((uint8_t)4ULL));
        gm_b16_8 t63;
        GM_ST(void *, (uint8_t *)&t63, (void *)(gm_sb8kl54a499bf));
        GM_ST(uintptr_t, (uint8_t *)&t63 + 8, (uintptr_t)(6));
        gm_b16_8 t64;
        GM_ST(void *, (uint8_t *)&t64, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(1));
        gm_b16_8 t65;
        GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sicgmpca63jln));
        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(1));
        gm_b0_1 t66 = gm_f_start_exit_m57dnn5((void *)NULL, cx, NULL, t61, t62, t63, t64, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t67 = gm_l0;
        void * t68 = gm_l1;
        gm_t88rcucg1b7uk t69 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t68) + 0));
        gm_b16_8 t70 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t67, t69);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t71;
        GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t71 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t71 + 32, 0);
        ((uint8_t *)&t71)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t71 + 8), t70);
        gm_t5o0tpv5jka2l t72 = t71;
        uint8_t t73 = ((((uint8_t *)&t72))[0] != 0);
        F.ret = t71;
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
__attribute__((aligned(4))) void gm_f_with_on_fl0pjo9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, gm_b16_8 a4, void * a5, void * a6) {
    struct gm_fr_with_on_fl0pjo9 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* label */
    gm_b16_8 gm_l3; /* w */
    gm_b16_8 gm_l4; /* cx */
    void * gm_l5; /* stack */
    void * gm_l6; /* body */
    gm_b16_8 gm_l7; /* st */
    gm_b16_8 gm_l8; /* sa */
    gm_b16_8 gm_l9; /* p */
    gm_b16_8 gm_l10; /* l */
    gm_b16_8 gm_l11; /* loc */
    gm_b16_8 gm_l12; /* body_fn */
    gm_b16_8 gm_l13; /* fb */
    gm_b16_8 gm_l14; /* end */
    GM_STACK_CHECK(&F, 3039u);
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
        void * t3 = gm_l5;
        gm_t5o0tpv5jka2l t4;
        gm_f_expr_porbc9t((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_t08olq97rscn3 t6 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t5) + 8));
            gm_t5o0tpv5jka2l t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t7 + 8), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
        gm_l7 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l5;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 0));
        gm_b16_8 t12 = gm_l7;
        gm_b16_8 t13 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t9, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t13;
    }
    {
        gm_b16_8 t14 = gm_l8;
        gm_b16_8 t15 = gm_f_slice_ptr_hojfgr5((void *)NULL, cx, NULL, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t15;
    }
    {
        void * t16 = gm_l0;
        gm_b16_8 t17 = gm_l8;
        gm_b16_8 t18 = gm_f_slice_len_a3383br((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t18;
    }
    {
        void * t19 = gm_l0;
        void * t20 = gm_l1;
        gm_b16_8 t21 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t19, t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t21;
    }
    {
        void * t22 = gm_l0;
        uint32_t t23 = gm_l2;
        void * t24 = gm_l1;
        gm_t88rcucg1b7uk t25 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t24) + 0));
        void * t26 = gm_l6;
        gm_t5o0tpv5jka2l t27;
        gm_f_outline_body_tsr6q2p((void *)&t27, (void *)NULL, cx, NULL, t22, t23, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t28 = t27;
        if (((((uint8_t *)&t28))[0] != 0)) {
            gm_t08olq97rscn3 t29 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t28) + 8));
            gm_t5o0tpv5jka2l t30;
            GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t30 + 4, 0);
            ((uint8_t *)&t30)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t30 + 8), t29);
            F.ret = t30;
            goto gm_fret;
        }
        gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t28) + 8));
        gm_l12 = t31;
    }
    {
        void * t32 = gm_l0;
        uint32_t t33 = gm_f_fresh_5kokjmg(t32);
        gm_t88rcucg1b7uk t34;
        GM_ST(uint32_t, ((uint8_t *)&t34 + 0), t33);
        gm_tabb710d9mq2e t35;
        gm_f_format_8ps3g8c((void *)&t35, (void *)NULL, cx, NULL, t34);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t36 = t35;
        if (((((uint8_t *)&t36))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t36) + 8, gm_srcihhtlsvd7f6i, 36, 11);
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&t36) + 8));
        gm_l13 = t37;
    }
    {
        void * t38 = gm_l0;
        uint32_t t39 = gm_f_fresh_5kokjmg(t38);
        gm_t88rcucg1b7uk t40;
        GM_ST(uint32_t, ((uint8_t *)&t40 + 0), t39);
        gm_tabb710d9mq2e t41;
        gm_f_format_dn5pqnq((void *)&t41, (void *)NULL, cx, NULL, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t42 = t41;
        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_srcihhtlsvd7f6i, 37, 12);
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
        gm_l14 = t43;
    }
    {
        void * t44 = gm_l0;
        gm_t6j6smj7qoctu t45;
        gm_b16_8 t46 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t45 + 0, t46);
        gm_b16_8 t47 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t45 + 16, t47);
        gm_b0_1 t48 = gm_f_linef_1oiu95r((void *)NULL, cx, NULL, t44, &t45);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t49 = gm_l0;
        gm_tif3elqk01bdl t50;
        gm_b16_8 t51 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 0, t51);
        gm_b16_8 t52 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 16, t52);
        gm_b16_8 t53 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 32, t53);
        gm_b16_8 t54 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 48, t54);
        gm_b16_8 t55 = gm_l12;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 64, t55);
        gm_b16_8 t56 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t50 + 80, t56);
        gm_b0_1 t57 = gm_f_linef_vllikfi((void *)NULL, cx, NULL, t49, &t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t58 = gm_l0;
        gm_tud7oovfvnf43 t59;
        gm_b16_8 t60 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t59 + 0, t60);
        gm_b16_8 t61 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t59 + 16, t61);
        gm_b16_8 t62 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t59 + 32, t62);
        gm_b16_8 t63 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t59 + 48, t63);
        gm_b16_8 t64 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t59 + 64, t64);
        gm_b0_1 t65 = gm_f_linef_5dh0opk((void *)NULL, cx, NULL, t58, &t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t66 = gm_l0;
        void * t67 = gm_l1;
        gm_tl874t2atnlh3 t68 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t67) + 72));
        gm_b0_1 t69 = gm_f_call_site_8n74to1((void *)NULL, cx, NULL, t66, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t70 = gm_l0;
        gm_t6j6smj7qoctu t71;
        gm_b16_8 t72 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t71 + 0, t72);
        gm_b16_8 t73 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t71 + 16, t73);
        gm_b0_1 t74 = gm_f_linef_07q39gh((void *)NULL, cx, NULL, t70, &t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t75 = gm_l0;
        gm_b16_8 t76;
        GM_ST(void *, (uint8_t *)&t76, (void *)(gm_si2pj4mpn1kn5));
        GM_ST(uintptr_t, (uint8_t *)&t76 + 8, (uintptr_t)(10));
        gm_b0_1 t77 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t75, t76);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t78 = gm_l0;
        void * t79 = gm_l0;
        uint64_t t80 = GM_LD(uint64_t, (((uint8_t *)t79) + 224));
        uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
        if (o82) gm_trap("integer overflow in `+`", gm_srcihhtlsvd7f6i, 45, 5);
        uint64_t t83 = r81;
        GM_ST(uint64_t, (((uint8_t *)t78) + 224), t83);
    }
    {
        void * t84 = gm_l0;
        gm_b16_8 t85 = gm_l3;
        gm_tnpifv20irnph t86;
        GM_ST(gm_b16_8, ((uint8_t *)&t86 + 0), t85);
        gm_b0_1 t87 = gm_f_linef_t0so8o7((void *)NULL, cx, NULL, t84, t86);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t88 = gm_l0;
        gm_b16_8 t89 = gm_l3;
        gm_tnpifv20irnph t90;
        GM_ST(gm_b16_8, ((uint8_t *)&t90 + 0), t89);
        gm_b0_1 t91 = gm_f_linef_ep7poaf((void *)NULL, cx, NULL, t88, t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t92 = gm_l0;
        gm_t6j6smj7qoctu t93;
        gm_b16_8 t94 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t93 + 0, t94);
        gm_b16_8 t95 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t93 + 16, t95);
        gm_b0_1 t96 = gm_f_linef_hl75la5((void *)NULL, cx, NULL, t92, &t93);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t97 = gm_l0;
        gm_temv198mna2jp t98;
        memset((uint8_t *)&t98 + 20, 0, 4);
        gm_b16_8 t99 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t98 + 0, t99);
        uint32_t t100 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t98 + 16, t100);
        gm_b16_8 t101 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t98 + 24, t101);
        gm_b0_1 t102 = gm_f_linef_gpkl3pa((void *)NULL, cx, NULL, t97, &t98);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t103 = gm_l0;
        gm_t6j6smj7qoctu t104;
        gm_b16_8 t105 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 0, t105);
        gm_b16_8 t106 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 16, t106);
        gm_b0_1 t107 = gm_f_linef_d8lmsp4((void *)NULL, cx, NULL, t103, &t104);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t108 = gm_l0;
        gm_t6j6smj7qoctu t109;
        gm_b16_8 t110 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t109 + 0, t110);
        gm_b16_8 t111 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t109 + 16, t111);
        gm_b0_1 t112 = gm_f_linef_d17op7h((void *)NULL, cx, NULL, t108, &t109);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t113 = gm_l0;
        gm_b16_8 t114 = gm_l14;
        gm_tnpifv20irnph t115;
        GM_ST(gm_b16_8, ((uint8_t *)&t115 + 0), t114);
        gm_b0_1 t116 = gm_f_linef_v8hsi1d((void *)NULL, cx, NULL, t113, t115);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t117 = gm_l0;
        gm_b16_8 t118;
        GM_ST(void *, (uint8_t *)&t118, (void *)(gm_s02uuedsru1qu));
        GM_ST(uintptr_t, (uint8_t *)&t118 + 8, (uintptr_t)(8));
        gm_b0_1 t119 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t117, t118);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t120 = gm_l0;
        gm_b16_8 t121 = gm_l11;
        gm_tnpifv20irnph t122;
        GM_ST(gm_b16_8, ((uint8_t *)&t122 + 0), t121);
        gm_b0_1 t123 = gm_f_linef_349v8tu((void *)NULL, cx, NULL, t120, t122);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t124 = gm_l0;
        gm_t6j6smj7qoctu t125;
        gm_b16_8 t126 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t125 + 0, t126);
        gm_b16_8 t127 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t125 + 16, t127);
        gm_b0_1 t128 = gm_f_linef_vs9ueum((void *)NULL, cx, NULL, t124, &t125);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t129 = gm_l0;
        gm_b16_8 t130;
        GM_ST(void *, (uint8_t *)&t130, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(1));
        gm_b0_1 t131 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t129, t130);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t132 = gm_l0;
        gm_b16_8 t133 = gm_l11;
        gm_tnpifv20irnph t134;
        GM_ST(gm_b16_8, ((uint8_t *)&t134 + 0), t133);
        gm_b0_1 t135 = gm_f_linef_jl1vuq9((void *)NULL, cx, NULL, t132, t134);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t136 = gm_l0;
        uint32_t t137 = gm_l2;
        gm_b16_8 t138 = gm_l14;
        gm_b16_8 t139 = t138;
        gm_b16_8 t140 = GM_LD(gm_b16_8, ((uint8_t *)&t139));
        gm_f_with_scope_o670qbc((void *)&F.l15, (void *)NULL, cx, NULL, t136, t137, t140);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t141 = gm_l0;
        void * t142 = (void *)(((uint8_t *)t141) + 240);
        gm_tftb1odc739t5 t143 = gm_f_push_pcqb56b(t142, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l15)));
        gm_tftb1odc739t5 t144 = t143;
        if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 1, gm_srcihhtlsvd7f6i, 60, 5);
        gm_b0_1 t145 = GM_LD(gm_b0_1, (((uint8_t *)&t144) + 1));
    }
    {
        void * t146 = gm_l0;
        gm_b0_1 t147 = gm_f_ladder_36amdbn((void *)NULL, cx, NULL, t146);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t148 = gm_l0;
        void * t149 = (void *)(((uint8_t *)t148) + 240);
        gm_t1g28mkc78m0c t150;
        gm_f_pop_g2usrvd((void *)&t150, t149);
        gm_t1g28mkc78m0c t151 = t150;
        if (!((((uint8_t *)&t151))[104] != 0)) gm_trap("unwrap of null", gm_srcihhtlsvd7f6i, 62, 15);
        gm_tehgl6lkqgvt0 t152 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)&t151));
        F.l16 = t152;
    }
    {
        void * t153 = gm_l0;
        gm_b0_1 t154 = gm_f_end_scope_45ar5qv((void *)NULL, cx, NULL, t153, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l16)), ((uint8_t)0));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t155 = gm_l0;
        void * t156 = gm_l0;
        uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)t156) + 224));
        uint64_t r158; bool o159 = __builtin_sub_overflow(t157, ((uint64_t)1ULL), &r158);
        if (o159) gm_trap("integer overflow in `-`", gm_srcihhtlsvd7f6i, 64, 5);
        uint64_t t160 = r158;
        GM_ST(uint64_t, (((uint8_t *)t155) + 224), t160);
    }
    {
        void * t161 = gm_l0;
        gm_b16_8 t162;
        GM_ST(void *, (uint8_t *)&t162, (void *)(gm_s2cuflhqkkuaq));
        GM_ST(uintptr_t, (uint8_t *)&t162 + 8, (uintptr_t)(1));
        gm_b0_1 t163 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t161, t162);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t164 = gm_l0;
        gm_b16_8 t165 = gm_l14;
        gm_b16_8 t166 = t165;
        gm_b16_8 t167 = GM_LD(gm_b16_8, ((uint8_t *)&t166));
        gm_b0_1 t168 = gm_f_label_710oev4((void *)NULL, cx, NULL, t164, t167);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t170 = gm_l0;
        void * t171 = GM_LD(void *, (((uint8_t *)t170) + 0));
        void * t172 = (void *)(((uint8_t *)t171) + 8);
        void * t173 = gm_l1;
        gm_t88rcucg1b7uk t174 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t173) + 0));
        uint8_t t175 = gm_f_is_never_in5ftma(t172, GM_CONV(gm_csguankko4lmhd, t174));
        if (!t175) goto ie169;
        {
            void * t177 = gm_l0;
            void * t178 = gm_l1;
            gm_t88rcucg1b7uk t179 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t178) + 0));
            gm_b16_8 t180 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t177, t179);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t181;
            GM_ST(uint8_t, (uint8_t *)&t181 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t181 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t181 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t181 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t181 + 32, 0);
            ((uint8_t *)&t181)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t181 + 8), t180);
            gm_t5o0tpv5jka2l t182 = t181;
            uint8_t t183 = ((((uint8_t *)&t182))[0] != 0);
            F.ret = t181;
            goto gm_fret;
        }
        goto ix169;
ie169:;
ix169:;
    }
    {
        void * t184 = gm_l0;
        void * t185 = gm_l1;
        gm_t88rcucg1b7uk t186 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t185) + 0));
        uint32_t t187 = gm_l2;
        gm_t88rcucg1b7uk t188;
        GM_ST(uint32_t, ((uint8_t *)&t188 + 0), t187);
        gm_tabb710d9mq2e t189;
        gm_f_format_rslrdtj((void *)&t189, (void *)NULL, cx, NULL, t188);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t190 = t189;
        if (((((uint8_t *)&t190))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t190) + 8, gm_srcihhtlsvd7f6i, 68, 25);
        gm_b16_8 t191 = GM_LD(gm_b16_8, (((uint8_t *)&t190) + 8));
        gm_b16_8 t192 = t191;
        gm_b16_8 t193 = GM_LD(gm_b16_8, ((uint8_t *)&t192));
        gm_b16_8 t194 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t184, t186, t193);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t195;
        GM_ST(uint8_t, (uint8_t *)&t195 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t195 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t195 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t195 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t195 + 32, 0);
        ((uint8_t *)&t195)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t195 + 8), t194);
        gm_t5o0tpv5jka2l t196 = t195;
        uint8_t t197 = ((((uint8_t *)&t196))[0] != 0);
        F.ret = t195;
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

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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; uint64_t f2; } gm_tuglhjjmj2g58;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; } gm_t586m754bse2v;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; } gm_t5hdnnftkup6m;
typedef struct { gm_tbl8il92l97ut f0; uint8_t f1; uint8_t pe[7]; } gm_t5lf9i3n1r3co;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tuglhjjmj2g58 v0; uint16_t v1; } f1; } gm_t5v8n6qrn4rh4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t pe[4]; } gm_td7tfovebirdn;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { uint32_t f0; uint32_t f1; gm_b16_8 f2; gm_b16_8 f3; } gm_tgat3d8836orb;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; uint32_t f5; uint8_t p6[4]; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; } gm_th6ngf7aenlq3;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_b16_8 f2; } gm_thognv9odov03;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_ti71ll5knnvij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; gm_b16_8 f5; } gm_tif3elqk01bdl;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { gm_t9805qtonbsag f0; gm_t9805qtonbsag f1; gm_t9805qtonbsag f2; gm_t9805qtonbsag f3; } gm_tk8ebsa8coi4p;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { gm_t3mf58gv9kf3n a[2]; } gm_tmmlu9bk8dch4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; uint64_t f4; gm_b16_8 f5; gm_b16_8 f6; gm_b16_8 f7; gm_b16_8 f8; } gm_tn96msocmj81h;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[1]; } gm_tnqk2qt7j42o9;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9805qtonbsag v0; } f1; } gm_tnrltbc61i6v1;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t p2[4]; gm_b16_8 f2; gm_b16_8 f3; uint64_t f4; gm_b16_8 f5; uint32_t f6; uint8_t pe[4]; } gm_tobu7tlrkigeq;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { gm_tuglhjjmj2g58 f0; uint8_t f1; uint8_t pe[7]; } gm_tqcc6dmlne11b;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; } gm_tttl9nsdo1iu6;
_Static_assert(sizeof(gm_tuglhjjmj2g58) == 16 && _Alignof(gm_tuglhjjmj2g58) == 8 && offsetof(gm_tuglhjjmj2g58, f0) == 0 && offsetof(gm_tuglhjjmj2g58, f1) == 4 && offsetof(gm_tuglhjjmj2g58, f2) == 8, "layout of `(signed: bool, bits: u32, storage: u64)`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_tpe1hv3fekn05 v1; } f1; } gm_t4lnf71mruqpt;
typedef struct { gm_tcmkompbl09il f0; uint8_t f1; uint8_t pe[7]; } gm_t57euh612ligr;
_Static_assert(sizeof(gm_t586m754bse2v) == 120 && _Alignof(gm_t586m754bse2v) == 8 && offsetof(gm_t586m754bse2v, f0) == 0 && offsetof(gm_t586m754bse2v, f1) == 16 && offsetof(gm_t586m754bse2v, f2) == 32 && offsetof(gm_t586m754bse2v, f3) == 40 && offsetof(gm_t586m754bse2v, f4) == 56 && offsetof(gm_t586m754bse2v, f5) == 72 && offsetof(gm_t586m754bse2v, f6) == 88 && offsetof(gm_t586m754bse2v, f7) == 104, "layout of `([]u8, string, u32, []u8, []u8, []u8, []u8, []u8)`");
_Static_assert(sizeof(gm_t5hdnnftkup6m) == 24 && _Alignof(gm_t5hdnnftkup6m) == 8 && offsetof(gm_t5hdnnftkup6m, f0) == 0 && offsetof(gm_t5hdnnftkup6m, f1) == 16 && offsetof(gm_t5hdnnftkup6m, f2) == 20, "layout of `(string, u32, u32)`");
_Static_assert(sizeof(gm_t5lf9i3n1r3co) == 24 && _Alignof(gm_t5lf9i3n1r3co) == 8 && offsetof(gm_t5lf9i3n1r3co, f0) == 0 && offsetof(gm_t5lf9i3n1r3co, f1) == 16, "layout of `?SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_b16_8 v1; } f1; } gm_t5o0tpv5jka2l;
_Static_assert(sizeof(gm_t5v8n6qrn4rh4) == 24 && _Alignof(gm_t5v8n6qrn4rh4) == 8 && offsetof(gm_t5v8n6qrn4rh4, f0) == 0 && offsetof(gm_t5v8n6qrn4rh4, f1) == 8, "layout of `Repr`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_th2lqbo77pvsc v1; } f1; } gm_t6j4d6ei14c2t;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_thognv9odov03 v0; gm_tiilmfd16v3fc v1; } f1; } gm_tbf8u1b86cgmi;
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_td7tfovebirdn) == 40 && _Alignof(gm_td7tfovebirdn) == 8 && offsetof(gm_td7tfovebirdn, f0) == 0 && offsetof(gm_td7tfovebirdn, f1) == 16 && offsetof(gm_td7tfovebirdn, f2) == 32, "layout of `([]u8, string, u32)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgat3d8836orb) == 40 && _Alignof(gm_tgat3d8836orb) == 8 && offsetof(gm_tgat3d8836orb, f0) == 0 && offsetof(gm_tgat3d8836orb, f1) == 4 && offsetof(gm_tgat3d8836orb, f2) == 8 && offsetof(gm_tgat3d8836orb, f3) == 24, "layout of `(u32, u32, string, string)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_th6ngf7aenlq3) == 128 && _Alignof(gm_th6ngf7aenlq3) == 8 && offsetof(gm_th6ngf7aenlq3, f0) == 0 && offsetof(gm_th6ngf7aenlq3, f1) == 16 && offsetof(gm_th6ngf7aenlq3, f2) == 24 && offsetof(gm_th6ngf7aenlq3, f3) == 40 && offsetof(gm_th6ngf7aenlq3, f4) == 56 && offsetof(gm_th6ngf7aenlq3, f5) == 72 && offsetof(gm_th6ngf7aenlq3, f6) == 80 && offsetof(gm_th6ngf7aenlq3, f7) == 96 && offsetof(gm_th6ngf7aenlq3, f8) == 112, "layout of `([]u8, u32, string, string, string, u32, []u8, string, string)`");
_Static_assert(sizeof(gm_thognv9odov03) == 40 && _Alignof(gm_thognv9odov03) == 8 && offsetof(gm_thognv9odov03, f0) == 0 && offsetof(gm_thognv9odov03, f1) == 8 && offsetof(gm_thognv9odov03, f2) == 24, "layout of `ForeignFn`");
_Static_assert(sizeof(gm_ti71ll5knnvij) == 72 && _Alignof(gm_ti71ll5knnvij) == 8 && offsetof(gm_ti71ll5knnvij, f0) == 0 && offsetof(gm_ti71ll5knnvij, f1) == 16 && offsetof(gm_ti71ll5knnvij, f2) == 32 && offsetof(gm_ti71ll5knnvij, f3) == 40 && offsetof(gm_ti71ll5knnvij, f4) == 56, "layout of `(string, []u8, usize, []u8, string)`");
_Static_assert(sizeof(gm_tif3elqk01bdl) == 96 && _Alignof(gm_tif3elqk01bdl) == 8 && offsetof(gm_tif3elqk01bdl, f0) == 0 && offsetof(gm_tif3elqk01bdl, f1) == 16 && offsetof(gm_tif3elqk01bdl, f2) == 32 && offsetof(gm_tif3elqk01bdl, f3) == 48 && offsetof(gm_tif3elqk01bdl, f4) == 64 && offsetof(gm_tif3elqk01bdl, f5) == 80, "layout of `(string, string, string, string, string, string)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tk8ebsa8coi4p) == 32 && _Alignof(gm_tk8ebsa8coi4p) == 4 && offsetof(gm_tk8ebsa8coi4p, f0) == 0 && offsetof(gm_tk8ebsa8coi4p, f1) == 8 && offsetof(gm_tk8ebsa8coi4p, f2) == 16 && offsetof(gm_tk8ebsa8coi4p, f3) == 24, "layout of `Ambient`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tmmlu9bk8dch4) == 32 && _Alignof(gm_tmmlu9bk8dch4) == 8 && offsetof(gm_tmmlu9bk8dch4, a) == 0, "layout of `[2](^Expr, ^Expr)`");
_Static_assert(sizeof(gm_tn96msocmj81h) == 136 && _Alignof(gm_tn96msocmj81h) == 8 && offsetof(gm_tn96msocmj81h, f0) == 0 && offsetof(gm_tn96msocmj81h, f1) == 16 && offsetof(gm_tn96msocmj81h, f2) == 32 && offsetof(gm_tn96msocmj81h, f3) == 48 && offsetof(gm_tn96msocmj81h, f4) == 64 && offsetof(gm_tn96msocmj81h, f5) == 72 && offsetof(gm_tn96msocmj81h, f6) == 88 && offsetof(gm_tn96msocmj81h, f7) == 104 && offsetof(gm_tn96msocmj81h, f8) == 120, "layout of `(string, string, string, string, u64, string, string, string, string)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tnqk2qt7j42o9) == 4 && _Alignof(gm_tnqk2qt7j42o9) == 4 && offsetof(gm_tnqk2qt7j42o9, a) == 0, "layout of `[1]Ty`");
_Static_assert(sizeof(gm_tnrltbc61i6v1) == 12 && _Alignof(gm_tnrltbc61i6v1) == 4 && offsetof(gm_tnrltbc61i6v1, f0) == 0 && offsetof(gm_tnrltbc61i6v1, f1) == 4, "layout of `Observed`");
_Static_assert(sizeof(gm_tobu7tlrkigeq) == 88 && _Alignof(gm_tobu7tlrkigeq) == 8 && offsetof(gm_tobu7tlrkigeq, f0) == 0 && offsetof(gm_tobu7tlrkigeq, f1) == 16 && offsetof(gm_tobu7tlrkigeq, f2) == 24 && offsetof(gm_tobu7tlrkigeq, f3) == 40 && offsetof(gm_tobu7tlrkigeq, f4) == 56 && offsetof(gm_tobu7tlrkigeq, f5) == 64 && offsetof(gm_tobu7tlrkigeq, f6) == 80, "layout of `([]u8, u32, string, string, u64, string, u32)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tqcc6dmlne11b) == 24 && _Alignof(gm_tqcc6dmlne11b) == 8 && offsetof(gm_tqcc6dmlne11b, f0) == 0 && offsetof(gm_tqcc6dmlne11b, f1) == 16, "layout of `?IntInfo`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; } f1; } gm_tqgtd5gtgj65s;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { void * f0; gm_b16_8 f1; gm_tnrltbc61i6v1 f2; uint8_t pe[4]; } gm_ts0eitcfeteg8;
_Static_assert(sizeof(gm_tttl9nsdo1iu6) == 56 && _Alignof(gm_tttl9nsdo1iu6) == 8 && offsetof(gm_tttl9nsdo1iu6, f0) == 0 && offsetof(gm_tttl9nsdo1iu6, f1) == 16 && offsetof(gm_tttl9nsdo1iu6, f2) == 32 && offsetof(gm_tttl9nsdo1iu6, f3) == 48, "layout of `(string, string, string, u64)`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4lnf71mruqpt) == 32 && _Alignof(gm_t4lnf71mruqpt) == 8 && offsetof(gm_t4lnf71mruqpt, f0) == 0 && offsetof(gm_t4lnf71mruqpt, f1) == 8, "layout of `HandlerValue`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t57euh612ligr) == 32 && _Alignof(gm_t57euh612ligr) == 8 && offsetof(gm_t57euh612ligr, f0) == 0 && offsetof(gm_t57euh612ligr, f1) == 24, "layout of `?BitsLayout`");
_Static_assert(sizeof(gm_t5o0tpv5jka2l) == 40 && _Alignof(gm_t5o0tpv5jka2l) == 8 && offsetof(gm_t5o0tpv5jka2l, f0) == 0 && offsetof(gm_t5o0tpv5jka2l, f1) == 8, "layout of `Unsup!string`");
_Static_assert(sizeof(gm_t6j4d6ei14c2t) == 40 && _Alignof(gm_t6j4d6ei14c2t) == 8 && offsetof(gm_t6j4d6ei14c2t, f0) == 0 && offsetof(gm_t6j4d6ei14c2t, f1) == 8, "layout of `Unsup!?string`");
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tbf8u1b86cgmi) == 48 && _Alignof(gm_tbf8u1b86cgmi) == 8 && offsetof(gm_tbf8u1b86cgmi, f0) == 0 && offsetof(gm_tbf8u1b86cgmi, f1) == 8, "layout of `ScopeKind`");
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { gm_tbf8u1b86cgmi f0; uint32_t f1; uint8_t p2[4]; gm_trqfvmjtt4sij f2; uint64_t f3; } gm_tehgl6lkqgvt0;
typedef struct { gm_ts0eitcfeteg8 f0; uint8_t f1; uint8_t pe[7]; } gm_tjefu848ssvm9;
_Static_assert(sizeof(gm_tqgtd5gtgj65s) == 40 && _Alignof(gm_tqgtd5gtgj65s) == 8 && offsetof(gm_tqgtd5gtgj65s, f0) == 0 && offsetof(gm_tqgtd5gtgj65s, f1) == 8, "layout of `Unsup!()`");
_Static_assert(sizeof(gm_ts0eitcfeteg8) == 40 && _Alignof(gm_ts0eitcfeteg8) == 8 && offsetof(gm_ts0eitcfeteg8, f0) == 0 && offsetof(gm_ts0eitcfeteg8, f1) == 8 && offsetof(gm_ts0eitcfeteg8, f2) == 24, "layout of `DestCall`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
typedef struct { gm_tehgl6lkqgvt0 f0; uint8_t f1; uint8_t pe[7]; } gm_t1g28mkc78m0c;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tehgl6lkqgvt0) == 104 && _Alignof(gm_tehgl6lkqgvt0) == 8 && offsetof(gm_tehgl6lkqgvt0, f0) == 0 && offsetof(gm_tehgl6lkqgvt0, f1) == 48 && offsetof(gm_tehgl6lkqgvt0, f2) == 56 && offsetof(gm_tehgl6lkqgvt0, f3) == 96, "layout of `Scope`");
_Static_assert(sizeof(gm_tjefu848ssvm9) == 48 && _Alignof(gm_tjefu848ssvm9) == 8 && offsetof(gm_tjefu848ssvm9, f0) == 0 && offsetof(gm_tjefu848ssvm9, f1) == 40, "layout of `?DestCall`");
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
_Static_assert(sizeof(gm_t1g28mkc78m0c) == 112 && _Alignof(gm_t1g28mkc78m0c) == 8 && offsetof(gm_t1g28mkc78m0c, f0) == 0 && offsetof(gm_t1g28mkc78m0c, f1) == 104, "layout of `?Scope`");
struct gm_fr_adt_def_ni54he1 {
    gm_frame h;
    void * ret;
};
struct gm_fr_arg_may_overlap_u7aort7 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bits_layout_23rect9 {
    gm_frame h;
    gm_t57euh612ligr ret;
};
struct gm_fr_by_ref_fi0o860 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_call_closure_mha9p1e {
    gm_frame h;
    gm_tddtjfggdat0d l6; /* sig */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_call_fn_q122g0r {
    gm_frame h;
    gm_trqfvmjtt4sij l15; /* tys */
    gm_trqfvmjtt4sij l22; /* args */
    gm_trqfvmjtt4sij l28; /* rest */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_call_indirect_khk02ee {
    gm_frame h;
    gm_trqfvmjtt4sij l10; /* ps */
    gm_trqfvmjtt4sij l18; /* pargs */
    gm_b16_8 ret;
};
struct gm_fr_call_inner_i2bbshq {
    gm_frame h;
    gm_tsth9qu1dv0g3 l2; /* callee */
    gm_trqfvmjtt4sij l8; /* vals */
    gm_trqfvmjtt4sij l9; /* tys */
    gm_tddtjfggdat0d l29; /* sig */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_call_nk38pfa {
    gm_frame h;
    gm_tsth9qu1dv0g3 l2; /* callee */
    gm_ts0eitcfeteg8 l6; /* dc */
    gm_t5o0tpv5jka2l l7; /* r */
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_call_result_vg14o5p {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_call_site_8n74to1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_callee_borrows_mk6en5r {
    gm_frame h;
    gm_tsth9qu1dv0g3 l1; /* callee */
    uint8_t ret;
};
struct gm_fr_callee_name_4k9m9s6 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_calls_emit_plain_euobq8f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_calls_native_set_iq1d80t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_check_unwind_2hji4bl {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_conv_attr_m0b89pc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_effect_name_prpp0ip {
    gm_frame h;
    gm_b16_8 ret;
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
struct gm_fr_equal_hcuag9d {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_file_o15fqku {
    gm_frame h;
    void * ret;
};
struct gm_fr_fn_format_stub_53r45gt {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_ptr_type_f9efs3c {
    gm_frame h;
    gm_trqfvmjtt4sij l4; /* ps */
    gm_t9bmh80edou9c l5; /* <temporary array> */
    gm_b16_8 ret;
};
struct gm_fr_fn_sink_stub_5ipff9h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fresh_5kokjmg {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_gamma_arg_ldsagr2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_gamma_args_jb6l3ik {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_b16_8 ret;
};
struct gm_fr_gamma_value_rgbfhcc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_handler_decl_m66mgr8 {
    gm_frame h;
    void * ret;
};
struct gm_fr_handler_lit_b02boff {
    gm_frame h;
    void * ret;
};
struct gm_fr_in_place_root_417o9i7 {
    gm_frame h;
    void * ret;
};
struct gm_fr_int_ci6airg {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
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
struct gm_fr_keyed_h72lrop {
    gm_frame h;
    gm_b16_8 ret;
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
struct gm_fr_layout_of_e6hfj1r {
    gm_frame h;
    gm_tbccvikg6b96e ret;
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
struct gm_fr_native_op_r81a4at {
    gm_frame h;
    gm_tqgtd5gtgj65s ret;
};
struct gm_fr_offsets_eh6vfle {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_out_call_uj833o2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_out_stub_ils4a69 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_param_cty_e8tvhrm {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_perform_t77ghfg {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_piece_stub_m8mb7rc {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_plain_args_o7acjm5 {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* out */
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_plain_cty_q9lhlci {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_plain_value_e380k1u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_pop_g2usrvd {
    gm_frame h;
    gm_tehgl6lkqgvt0 l2; /* x */
    gm_t1g28mkc78m0c ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bytes_eocneng {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_ptr_type_plain_sanetd4 {
    gm_frame h;
    gm_tddtjfggdat0d l2; /* sig */
    uint8_t ret;
};
struct gm_fr_result_var_rn01cvq {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_5qbi623 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_size_dmhtkib {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_size_of_k0fluj2 {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_slice_5dt9121 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_str_equal_at_mshane1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_string_ty_j74ff0o {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_string_ty_kk94re3 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_stub_perform_m3dbb7g {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_target_ret_q7qjs0g {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_try_expr_ln1m7b8 {
    gm_frame h;
    gm_t5o0tpv5jka2l ret;
};
struct gm_fr_u8_ty_afkvj25 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
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
struct gm_fr_variant_equal_bgud9j0 {
    gm_frame h;
    gm_tckdobunaulee l16; /* variant */
    gm_t6j4d6ei14c2t ret;
};
struct gm_fr_with_expr_l1uk88k {
    gm_frame h;
    gm_tehgl6lkqgvt0 l38; /* scope */
    gm_t5o0tpv5jka2l l40; /* r */
    gm_tehgl6lkqgvt0 l41; /* scope2 */
    gm_t5o0tpv5jka2l ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_addr_of_hiaqkn0(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void * gm_f_adt_def_ni54he1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_adt_of_gej99vn(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_arg_may_overlap_u7aort7(void * a0, gm_tnrltbc61i6v1 a1, uint8_t a2);
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_bits_layout_u6kkccp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint8_t gm_f_by_ref_fi0o860(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_c_string_n7h27b3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_call_closure_mha9p1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5);
__attribute__((aligned(4))) void gm_f_call_fn_q122g0r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_call_indirect_khk02ee(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, uint8_t a7, uint8_t a8);
__attribute__((aligned(4))) void gm_f_call_inner_i2bbshq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, const void * a4, gm_tnrltbc61i6v1 a5);
__attribute__((aligned(4))) void gm_f_call_nk38pfa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_call_result_vg14o5p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_call_site_8n74to1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) uint8_t gm_f_callee_borrows_mk6en5r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_callee_name_4k9m9s6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_calls_emit_plain_euobq8f(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_calls_native_set_iq1d80t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_check_unwind_2hji4bl(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_const_arg_8kiicv6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0);
__attribute__((aligned(4))) void gm_f_ctl_perform_be6h6eo(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint32_t a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5);
__attribute__((aligned(4))) gm_b16_8 gm_f_cty_66ee0kf(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_decl_table_l7u1k0g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_direct_perform_vc33l18(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, void * a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6);
__attribute__((aligned(4))) uint32_t gm_f_effect_id_5588nqa(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
__attribute__((aligned(4))) gm_b0_1 gm_f_end_scope_45ar5qv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint8_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_eq_fn_5udvcnu(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_equal_hcuag9d(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_expr_porbc9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_vogsd72(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_file_o15fqku(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_format_stub_53r45gt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_name_6n34573(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_ptr_type_f9efs3c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_sink_stub_5ipff9h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_format_01h4tbj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_0mlfk3c(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_0mp0khb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_18tu4v6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_2d30afk(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_35to3im(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_37aah2s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3o89hhj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3s8s1g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_570tujl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_5p0qm94(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_6m84e0r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7cklfe7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7tqfnpi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8lfevbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_8n626pi(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_99gc605(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9g4m5fi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_b2nifvt(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dn5pqnq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_drnt54k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_eir8aii(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fsfutor(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_g55p0hj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_g9ssmu3(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_gjujk09(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h6b2ofo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_h6uc0qg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ht2r43m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_itcjtai(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jp1qc7o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_l0bpsnf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_lc73epv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mgf81l5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_mjg3n5a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_n1vqu08(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_nivudut(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_p19pl6n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qj6rkil(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qr5c6un(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_r9dvh2e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_rslrdtj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_ru16275(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_s4f2nfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_sjgrpqb(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tcvndjh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_frame_field_nj8m22n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_frame_pure_cbdra2q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_frame_up_g8486eu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint32_t gm_f_fresh_5kokjmg(void * a0);
__attribute__((aligned(4))) void * gm_f_func_in17r5n(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_gamma_arg_ldsagr2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_gamma_args_jb6l3ik(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_gamma_value_rgbfhcc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_get_e3joqcc(void *gm_out, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void * gm_f_handler_decl_m66mgr8(void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void * gm_f_handler_lit_b02boff(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_handler_table_2kdtlit(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_hash_text_4aekv10(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void * gm_f_in_place_root_417o9i7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_int_ci6airg(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, uint16_t a2);
__attribute__((aligned(4))) void gm_f_int_info_2tcdf1n(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_int_lit_ai37456(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, const void * a2);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_ctl_oclllo3(void * a0, uint32_t a1, uint32_t a2);
inline __attribute__((aligned(4))) uint8_t gm_f_is_never_in5ftma(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) uint8_t gm_f_is_plain_qr08i40(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_17uennd(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_void_vs2outo(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_kind_ghvav22(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_label_710oev4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_layout_of_e6hfj1r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_cs9ufftihtbpo2 gm_f_line_col_9vn24mu(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_line_l6u87p9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_0v7c424(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_18bk901(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_3fol08t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_4ku5dms(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_98ea3qh(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_arl1b28(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_bg1q0dr(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ch11let(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_d2o1cif(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_g6msml8(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_gg3n7rd(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_i03em5a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_i5ai2tu(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_icst3ps(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_lot5c36(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_m4ia8qg(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_o1esdvv(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qg7idri(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_qqk2cu4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_ubknr7g(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_uu8i6ue(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_linef_v355qja(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tnpifv20irnph a1);
inline __attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_load_0hebde7(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_loc_u9c0aqb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_none_v2s8s0l(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_res_riv19po(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint8_t a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_slice_sa50efe(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_some_9aqq928(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_make_variant_srnk8ha(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2, const void * a3);
__attribute__((aligned(4))) uint8_t gm_f_may_unwind_irl2ktm(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_mirror_lcjegsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_native_op_r81a4at(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, const void * a4, gm_t88rcucg1b7uk a5);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_nested_spec_po688kb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_no_writes_p1jjjfj(void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_jch381e(void *env, gm_node *cx, gm_node *inst, gm_tuglhjjmj2g58 a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_opt_present_joptd3k(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_out_call_uj833o2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_out_stub_ils4a69(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_passed_spec_rvmvr8h(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_perform_t77ghfg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6);
__attribute__((aligned(4))) gm_b16_8 gm_f_piece_stub_m8mb7rc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_place_addr_7ji15ot(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_plain_args_o7acjm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_plain_value_e380k1u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_g2usrvd(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint64_t gm_f_ptr_bytes_eocneng(void * a0);
__attribute__((aligned(4))) uint8_t gm_f_ptr_type_plain_sanetd4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pcqb56b(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_put_7shfu9u(void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_queue_cd51r8c(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_queue_spec_khngsmv(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_read_during_build_ng10fmi(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) void gm_f_repr_53quqki(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_res_is_err_11p8qlp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_result_var_rn01cvq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_scrutinee_addr_3fvmbme(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_dmhtkib(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) uint64_t gm_f_size_of_k0fluj2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_slice_5dt9121(void *env, gm_node *cx, gm_node *inst, void * a0, uint8_t a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_handler_table_8h552cp(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_specializable_7or9fsf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2, gm_t9805qtonbsag a3);
__attribute__((aligned(4))) gm_b16_8 gm_f_src_loc_lrhtj7i(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_start_exit_m57dnn5(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_static_bytes_u0uvco3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_str_equal_at_mshane1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_stub_perform_m3dbb7g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint32_t a3, uint32_t a4, gm_b16_8 a5, gm_b16_8 a6, gm_t88rcucg1b7uk a7);
__attribute__((aligned(4))) uint8_t gm_f_takes_fn_n239ee5(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_target_ret_q7qjs0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uninit_jr39s5u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_tmp_uohsptq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_try_expr_ln1m7b8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t9805qtonbsag a3, uint32_t a4);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_unique_fba8sj3(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_unit_8ekedsl(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_ty_vgceilu(void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) void gm_f_unsupported_531q51r(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_unwind_ladder_a9ij1gj(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_variant_equal_bgud9j0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) void gm_f_with_expr_l1uk88k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, void * a4, void * a5);
__attribute__((aligned(4))) void gm_f_with_on_fl0pjo9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, gm_b16_8 a4, void * a5, void * a6);
__attribute__((aligned(4))) void gm_f_with_scope_o670qbc(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_within_sys_2lc2ugq(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_502hm86(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_6o17mnv(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_f1cbljc(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_fp9dg1h(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_mv65o15(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_otf2npe(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_scdecr6(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_vp4v8ut(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_zero_tlk13aa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
extern uint8_t gm_cejn1dj0054[12] __attribute__((aligned(4)));
extern const uint8_t gm_s02uuedsru1qu[9];
extern const uint8_t gm_s03pmf867dcsr[2];
extern const uint8_t gm_s04qus0luoga5[5];
extern const uint8_t gm_s0aqhubr4upni[7];
extern const uint8_t gm_s1k3lfare8sei[4];
extern const uint8_t gm_s2cuflhqkkuaq[2];
extern const uint8_t gm_s3jl3afv8nlvv[4];
extern const uint8_t gm_s3vqae7u5ljtm[14];
extern const uint8_t gm_s44ek1gecbajs[7];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s6cju4qiijn6t[23];
extern const uint8_t gm_s8o6lm1cn4eo5[3];
extern const uint8_t gm_s9go9vvkpcjpd[48];
extern const uint8_t gm_s9u3cqhrfmcru[13];
extern const uint8_t gm_sb6j3egm9oljk[5];
extern const uint8_t gm_sbp38o7rrfqgd[19];
extern const uint8_t gm_sc4r4l9k6f577[3];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sicgmpca63jln[2];
extern const uint8_t gm_siihmb4jo3dsn[125];
extern const uint8_t gm_sjr00kolufse9[19];
extern const uint8_t gm_sk8csn7us23m3[9];
extern const uint8_t gm_skdpq4t6o94o2[25];
extern const uint8_t gm_sl8of2tnl835f[6];
extern const uint8_t gm_slq58vfha8k9h[2];
extern const uint8_t gm_smnfu5n5pum1l[2];
extern const uint8_t gm_smr5ddngcjuao[19];
extern const uint8_t gm_so78814tnt5g5[2];
extern const uint8_t gm_somhu4vltiil1[4];
extern const uint8_t gm_sono5n5lrjd7h[10];
extern const uint8_t gm_sp5b2hbslij9l[5];
extern const uint8_t gm_spsh7mv5lihcc[34];
extern const uint8_t gm_sqi7tb51umq69[13];
extern const uint8_t gm_sqtc9fffut4s2[13];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_srcd5arorrf8jkg[];
extern const char gm_srcht195tfrfuqo[];
extern const char gm_srcmeaag4q4qrjl[];
extern const char gm_srcmok80ho8hh01[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcuj9anpi7p1mq[];
extern const char gm_srcv1nri2247d2r[];
extern const uint8_t gm_st6to3kuud2ge[46];
extern const uint8_t gm_stafskn6bv24f[2];
extern const uint8_t gm_sts5j6f5pkov0[5];
extern const uint8_t gm_su2c9m69n1vjb[31];
extern const uint8_t gm_svubtddeidu1d[2];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline bool gm_eqkricceou4u(const uint8_t *a, const uint8_t *b) {
    if (a[4] != b[4]) return false;
    return !a[4] || gm_eq467d5cm1d5(a, b);
}
inline __attribute__((aligned(4))) void * gm_f_adt_def_ni54he1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_adt_def_ni54he1 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 64u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    void * t5 = gm_f_adt_of_gej99vn(t3, GM_CONV(gm_csguankko4lmhd, t4));
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_bits_layout_23rect9(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_bits_layout_23rect9 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_f_bits_layout_u6kkccp((void *)&(*(gm_t57euh612ligr *)gm_out), (void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_callee_name_4k9m9s6(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_callee_name_4k9m9s6 F;
    void * gm_l0; /* g */
    uint32_t gm_l1; /* f */
    GM_STACK_CHECK(&F, 73u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        gm_b16_8 t4 = gm_f_fn_name_6n34573((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t5 = 0;
        F.ret = t4;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_calls_native_set_iq1d80t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2) {
    struct gm_fr_calls_native_set_iq1d80t F;
    void * gm_l0; /* fg */
    gm_th2lqbo77pvsc gm_l1; /* res */
    gm_b16_8 gm_l2; /* v */
    gm_b16_8 gm_l3; /* r */
    GM_STACK_CHECK(&F, 152u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        if (!((((uint8_t *)a1))[16] != 0)) goto ie2;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)a1));
        gm_l3 = t3;
        {
            void * t5 = gm_l0;
            gm_t6j6smj7qoctu t6;
            gm_b16_8 t7 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t6 + 0, t7);
            gm_b16_8 t8 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t6 + 16, t8);
            gm_b0_1 t9 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t5, &t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_conv_attr_m0b89pc(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_conv_attr_m0b89pc F;
    uint8_t gm_l0; /* win64 */
    GM_STACK_CHECK_SHALLOW(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t1;
    uint8_t t3 = gm_l0;
    if (!t3) goto ie2;
    gm_b16_8 t5;
    GM_ST(void *, (uint8_t *)&t5, (void *)(gm_skdpq4t6o94o2));
    GM_ST(uintptr_t, (uint8_t *)&t5 + 8, (uintptr_t)(24));
    gm_b16_8 t6 = t5;
    t1 = t6;
    goto ix2;
ie2:;
    gm_b16_8 t8;
    GM_ST(void *, (uint8_t *)&t8, (void *)(gm_sr4qbrk6tlgaa));
    GM_ST(uintptr_t, (uint8_t *)&t8 + 8, (uintptr_t)(0));
    gm_b16_8 t9 = t8;
    t1 = t9;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_effect_name_prpp0ip F;
    void * gm_l0; /* t */
    uint32_t gm_l1; /* id */
    GM_STACK_CHECK_SHALLOW(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 216) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 283, 62);
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 216) + 0)) + t3 * 16));
    F.ret = t6;
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
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 0) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_srcv1nri2247d2r, 78, 75);
    void * t6 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 0) + 0)) + t3 * 80);
    F.ret = t6;
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
inline __attribute__((aligned(4))) void * gm_f_handler_decl_m66mgr8(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_handler_decl_m66mgr8 F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* d */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 544) + 0) + 8);
    if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcuj9anpi7p1mq, 132, 83);
    void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 544) + 0)) + t3 * 64);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_handler_lit_b02boff(void * a0, uint32_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_handler_lit_b02boff F;
    void * gm_l0; /* p */
    uint32_t gm_l1; /* h */
    GM_STACK_CHECK_SHALLOW(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint32_t t1 = gm_l1;
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    uint64_t t6 = GM_LD(uintptr_t, ((((uint8_t *)t5) + 504) + 0) + 8);
    if (t3 >= (uintptr_t)(t6)) gm_trap_index(t3, t6, gm_srcuj9anpi7p1mq, 129, 76);
    void * t7 = (void *)((uint8_t *)GM_LD(void *, ((((uint8_t *)t5) + 504) + 0)) + t3 * 40);
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
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
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    uint16_t t3 = gm_l2;
    gm_tjcj4j2gbh6c8 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(uint16_t, ((uint8_t *)&t4 + 2), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)0ULL));
    GM_ST(gm_tjcj4j2gbh6c8, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_keyed_h72lrop(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_keyed_h72lrop F;
    void * gm_l0; /* n */
    gm_b16_8 gm_l1; /* prefix */
    gm_b16_8 gm_l2; /* key */
    GM_STACK_CHECK(&F, 272u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
    gm_b16_8 t4 = gm_l2;
    gm_b16_8 t5 = gm_f_hash_text_4aekv10((void *)NULL, cx, NULL, t4, ((uint64_t)10ULL));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t5);
    gm_tabb710d9mq2e t6;
    gm_f_format_8n626pi((void *)&t6, (void *)NULL, cx, NULL, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t7 = t6;
    if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcht195tfrfuqo, 80, 91);
    gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
    gm_b16_8 t9 = t8;
    gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
    gm_b16_8 t11 = gm_f_unique_fba8sj3((void *)NULL, cx, NULL, t1, t10);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t11;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_offsets_eh6vfle(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_eh6vfle F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_out_call_uj833o2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_out_call_uj833o2 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* ret */
    gm_b16_8 gm_l2; /* callee */
    gm_b16_8 gm_l3; /* args */
    gm_b16_8 gm_l4; /* r */
    GM_STACK_CHECK(&F, 241u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_b16_8 t4 = gm_f_result_var_rn01cvq((void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l0;
        gm_tfeab279c16rr t6;
        gm_b16_8 t7 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 0, t7);
        gm_b16_8 t8 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 16, t8);
        gm_b16_8 t9 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t6 + 32, t9);
        gm_b0_1 t10 = gm_f_linef_bg1q0dr((void *)NULL, cx, NULL, t5, &t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t11 = gm_l4;
        uint8_t t12 = 0;
        F.ret = t11;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_param_cty_e8tvhrm(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_param_cty_e8tvhrm F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 149u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_b16_8 t1;
    void * t3 = gm_l0;
    gm_t88rcucg1b7uk t4 = gm_l1;
    uint8_t t5 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t3, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (!t5) goto ie2;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_sqtc9fffut4s2));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(12));
    gm_b16_8 t8 = t7;
    t1 = t8;
    goto ix2;
ie2:;
    void * t10 = gm_l0;
    gm_t88rcucg1b7uk t11 = gm_l1;
    gm_b16_8 t12 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t10, t11);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t13 = t12;
    t1 = t13;
ix2:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_cty_q9lhlci(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_plain_cty_q9lhlci F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* m */
    GM_STACK_CHECK(&F, 182u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l1;
        gm_th2lqbo77pvsc t5;
        gm_f_mirror_lcjegsf((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t6 = t5;
        if (!((((uint8_t *)&t6))[16] != 0)) goto ie2;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_l2 = t7;
        {
            gm_b16_8 t9 = gm_l2;
            uint8_t t10 = 0;
            F.ret = t9;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t11 = gm_l0;
        gm_t88rcucg1b7uk t12 = gm_l1;
        gm_b16_8 t13 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t14 = 0;
        F.ret = t13;
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
    void * t1 = gm_l0;
    GM_ST(void *, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 0, t1);
    void * t2 = gm_l1;
    GM_ST(void *, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 8, t2);
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 16, ((uint32_t)0ULL));
    GM_ST(uint32_t, (uint8_t *)&(*(gm_tmm67r2c94d5j *)gm_out) + 20, ((uint32_t)0ULL));
    goto gm_fdone;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_result_var_rn01cvq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_result_var_rn01cvq F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* ret */
    gm_b16_8 gm_l2; /* d */
    GM_STACK_CHECK(&F, 162u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        if (!(((((uint8_t *)t3) + 152))[16] != 0)) goto ie2;
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)t3) + 152));
        gm_l2 = t4;
        {
            void * t6 = gm_l0;
            gm_th2lqbo77pvsc t7;
            memset(&t7, 0, sizeof t7);
            GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)t6) + 152), t7);
        }
        {
            gm_b16_8 t8 = gm_l2;
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t10 = gm_l0;
        gm_t88rcucg1b7uk t11 = gm_l1;
        gm_b16_8 t12 = gm_f_tmp_uninit_jr39s5u((void *)NULL, cx, NULL, t10, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t13 = 0;
        F.ret = t12;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_5qbi623(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_5qbi623 F;
    void * gm_l0; /* ty */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 88u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_l1;
    gm_b16_8 t5 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t3, t4);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 145u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_f_list_uvfu1k1((void *)&F.l2, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = gm_l0;
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_f_printer_h7jigd7((void *)&gm_l3, t2, t3);
    }
    {
        void * t4 = (void *)((uint8_t *)&gm_l3);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_b0_1 t6 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t4, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
        gm_b16_8 t8 = t7;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
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
    void * t1 = gm_l0;
    uint8_t t2 = gm_l1;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_tbngattl1fo51 t4;
    GM_ST(uint8_t, (uint8_t *)&t4 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t4 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), t2);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t4 + 4), t3);
    gm_t53at80pfdus3 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)10ULL));
    GM_ST(gm_tbngattl1fo51, ((uint8_t *)&t5 + 8), t4);
    gm_t88rcucg1b7uk t6 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t5);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_j74ff0o(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_j74ff0o F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_f_u8_ty_afkvj25((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_t88rcucg1b7uk t4 = gm_f_slice_5dt9121((void *)NULL, cx, NULL, t1, ((uint8_t)1), t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_string_ty_kk94re3(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_string_ty_kk94re3 F;
    void * gm_l0; /* ty */
    GM_STACK_CHECK(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    void * t3 = (void *)(((uint8_t *)t2) + 8);
    gm_t88rcucg1b7uk t4 = gm_f_string_ty_j74ff0o((void *)NULL, cx, NULL, t3);
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_target_ret_q7qjs0g(void *env, gm_node *cx, gm_node *inst, void * a0, uint32_t a1) {
    struct gm_fr_target_ret_q7qjs0g F;
    void * gm_l0; /* fg */
    uint32_t gm_l1; /* levels */
    uint32_t gm_l2; /* f */
    GM_STACK_CHECK(&F, 125u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        uint32_t t3 = gm_l1;
        gm_tommf4j9upomc t4;
        gm_f_frame_up_g8486eu((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tommf4j9upomc t5 = t4;
        gm_l2 = GM_LD(uint32_t, (uint8_t *)&t5 + 16);
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        uint32_t t9 = gm_l2;
        void * t10 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 64));
        uint8_t t12 = 0;
        F.ret = t11;
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_u8_ty_afkvj25(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_u8_ty_afkvj25 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 28u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_f_int_ci6airg((void *)NULL, cx, NULL, t1, ((uint8_t)0), ((uint16_t)8ULL));
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
inline __attribute__((aligned(4))) uint8_t gm_f_arg_may_overlap_u7aort7(void * a0, gm_tnrltbc61i6v1 a1, uint8_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_arg_may_overlap_u7aort7 F;
    void * gm_l0; /* a */
    gm_tnrltbc61i6v1 gm_l1; /* obs */
    uint8_t gm_l2; /* borrows */
    gm_t9805qtonbsag gm_l3; /* root */
    uint32_t gm_l4; /* l */
    uint32_t gm_l5; /* r */
    GM_STACK_CHECK_SHALLOW(&F, 115u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        uint8_t t3 = gm_l2;
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            uint8_t t6 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma9; case ((uint8_t)2ULL): goto ma10; default: goto ma11; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t14 != ((uint8_t)0ULL)) goto mn13;
        uint8_t t15 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me7;
mn13:;
ma9:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t17 != ((uint8_t)1ULL)) goto mn16;
        void * t18 = gm_l0;
        uint8_t t19;
        uint8_t t22 = GM_LD(uint8_t, ((((uint8_t *)t18) + 8) + 0));
        if (t22 != ((uint8_t)15ULL)) goto mn21;
        t19 = ((uint8_t)0);
        goto me20;
mn21:;
        t19 = ((uint8_t)1);
        goto me20;
mn23:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 132, 24);
me20:;
        uint8_t t24 = 0;
        F.ret = t19;
        goto gm_fret;
        goto me7;
mn16:;
ma10:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t26 != ((uint8_t)2ULL)) goto mn25;
        gm_t9805qtonbsag t27 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l1) + 4));
        gm_l3 = t27;
        {
            void * t30 = gm_l0;
            uint8_t t31 = GM_LD(uint8_t, ((((uint8_t *)t30) + 8) + 0));
            if (t31 != ((uint8_t)15ULL)) goto ie29;
            uint32_t t32 = GM_LD(uint32_t, ((((uint8_t *)t30) + 8) + 8));
            gm_l4 = t32;
            {
                gm_t9805qtonbsag t34 = gm_l3;
                gm_t9805qtonbsag t35 = t34;
                uint32_t t36;
                if (((((uint8_t *)&t35))[4] != 0)) {
                    uint32_t t37 = GM_LD(uint32_t, (((uint8_t *)&t35) + 0));
                    t36 = t37;
                } else {
                    uint8_t t38 = 0;
                    F.ret = ((uint8_t)1);
                    goto gm_fret;
                }
                gm_l5 = t36;
            }
            {
                uint32_t t39 = gm_l5;
                uint32_t t40 = gm_l4;
                uint8_t t41 = ((t39 == t40));
                uint8_t t42 = 0;
                F.ret = t41;
                goto gm_fret;
            }
            goto ix29;
ie29:;
ix29:;
        }
        {
            uint8_t t43 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me7;
mn25:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 130, 5);
me7:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_call_closure_mha9p1e(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5) {
    struct gm_fr_call_closure_mha9p1e F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* cv */
    gm_t88rcucg1b7uk gm_l2; /* cty */
    gm_b16_8 gm_l3; /* vals */
    gm_b16_8 gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    gm_b16_8 gm_l7; /* c */
    gm_b16_8 gm_l8; /* ccx */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_tiilmfd16v3fc gm_l11; /* eff */
    uint32_t gm_l12; /* id */
    gm_b16_8 gm_l13; /* n */
    gm_b16_8 gm_l14; /* cx */
    gm_b16_8 gm_l15; /* r */
    GM_STACK_CHECK(&F, 1523u);
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
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_t53at80pfdus3 t7;
        gm_f_kind_ghvav22((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)15ULL)) goto ie2;
        gm_tddtjfggdat0d t10 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t8) + 8));
        F.l6 = t10;
        {
            void * t12 = gm_l0;
            gm_t88rcucg1b7uk t13 = gm_l2;
            gm_b16_8 t14 = gm_l1;
            gm_b16_8 t15 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t12, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l7 = t15;
        }
        {
            void * t16 = gm_l0;
            uint32_t t17 = gm_f_fresh_5kokjmg(t16);
            gm_t88rcucg1b7uk t18;
            GM_ST(uint32_t, ((uint8_t *)&t18 + 0), t17);
            gm_tabb710d9mq2e t19;
            gm_f_format_qr5c6un((void *)&t19, (void *)NULL, cx, NULL, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t20 = t19;
            if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_srcmeaag4q4qrjl, 311, 12);
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
            gm_l8 = t21;
        }
        {
            void * t22 = gm_l0;
            gm_t6j6smj7qoctu t23;
            gm_b16_8 t24 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t23 + 0, t24);
            gm_b16_8 t25 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t23 + 16, t25);
            gm_b0_1 t26 = gm_f_linef_lot5c36((void *)NULL, cx, NULL, t22, &t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t28 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l6) + 24) + 0));
                gm_l9 = t28;
            }
            {
                gm_l10 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t30 = gm_l10;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t32 = (t30 < t31);
                    if (!t32) goto lb29;
                    {
                        uint64_t t35 = gm_l10;
                        uint64_t t36 = (uintptr_t)t35;
                        uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                        gm_tiilmfd16v3fc t40 = GM_LD(gm_tiilmfd16v3fc, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t36 * 24));
                        gm_l11 = t40;
                        {
                            void * t42 = gm_l0;
                            void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
                            void * t44 = (void *)((uint8_t *)&gm_l11);
                            uint32_t t45 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t43, t44);
                            if (gm_unw.kind) {
                                goto gm_ll33;
                            }
                            gm_l12 = t45;
                        }
                        {
                            void * t46 = gm_l0;
                            uint32_t t47 = gm_f_fresh_5kokjmg(t46);
                            gm_t88rcucg1b7uk t48;
                            GM_ST(uint32_t, ((uint8_t *)&t48 + 0), t47);
                            gm_tabb710d9mq2e t49;
                            gm_f_format_mgf81l5((void *)&t49, (void *)NULL, cx, NULL, t48);
                            if (gm_unw.kind) {
                                goto gm_ll33;
                            }
                            gm_tabb710d9mq2e t50 = t49;
                            if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_srcmeaag4q4qrjl, 315, 14);
                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
                            gm_l13 = t51;
                        }
                        {
                            void * t52 = gm_l0;
                            gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)t52) + 280));
                            gm_l14 = t53;
                        }
                        {
                            void * t54 = gm_l0;
                            gm_t586m754bse2v t55;
                            memset((uint8_t *)&t55 + 36, 0, 4);
                            gm_b16_8 t56 = gm_l13;
                            GM_ST(gm_b16_8, (uint8_t *)&t55 + 0, t56);
                            gm_b16_8 t57 = gm_l14;
                            GM_ST(gm_b16_8, (uint8_t *)&t55 + 16, t57);
                            uint32_t t58 = gm_l12;
                            GM_ST(uint32_t, (uint8_t *)&t55 + 32, t58);
                            gm_b16_8 t59 = gm_l13;
                            GM_ST(gm_b16_8, (uint8_t *)&t55 + 40, t59);
                            gm_b16_8 t60 = gm_l13;
                            GM_ST(gm_b16_8, (uint8_t *)&t55 + 56, t60);
                            gm_b16_8 t61 = gm_l8;
                            GM_ST(gm_b16_8, (uint8_t *)&t55 + 72, t61);
                            gm_b16_8 t62 = gm_l8;
                            GM_ST(gm_b16_8, (uint8_t *)&t55 + 88, t62);
                            gm_b16_8 t63 = gm_l13;
                            GM_ST(gm_b16_8, (uint8_t *)&t55 + 104, t63);
                            gm_b0_1 t64 = gm_f_linef_ubknr7g((void *)NULL, cx, NULL, t54, &t55);
                            if (gm_unw.kind) {
                                goto gm_ll33;
                            }
                        }
                        goto me38;
mn39:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 313, 5);
me38:;
                    }
lc29:;
                    {
                        uint64_t t66 = gm_l10;
                        uint64_t r67; bool o68 = __builtin_add_overflow(t66, ((uint64_t)1ULL), &r67);
                        uint64_t t69 = r67;
                        gm_l10 = t69;
                    }
                }
gm_ll33:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb29; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc29; }
                }
                goto gm_lad;
lb29:;
            }
        }
        {
            void * t70 = gm_l0;
            gm_b16_8 t71 = gm_l7;
            gm_tnpifv20irnph t72;
            GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t71);
            gm_tabb710d9mq2e t73;
            gm_f_format_l0bpsnf((void *)&t73, (void *)NULL, cx, NULL, t72);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t74 = t73;
            if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 8, gm_srcmeaag4q4qrjl, 319, 27);
            gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 8));
            gm_b16_8 t76 = t75;
            gm_b16_8 t77 = GM_LD(gm_b16_8, ((uint8_t *)&t76));
            gm_b16_8 t78 = gm_l7;
            gm_tnpifv20irnph t79;
            GM_ST(gm_b16_8, ((uint8_t *)&t79 + 0), t78);
            gm_tabb710d9mq2e t80;
            gm_f_format_eir8aii((void *)&t80, (void *)NULL, cx, NULL, t79);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t81 = t80;
            if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 8, gm_srcmeaag4q4qrjl, 319, 54);
            gm_b16_8 t82 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 8));
            gm_b16_8 t83 = t82;
            gm_b16_8 t84 = GM_LD(gm_b16_8, ((uint8_t *)&t83));
            gm_b16_8 t85 = gm_l8;
            gm_b16_8 t86 = t85;
            gm_b16_8 t87 = GM_LD(gm_b16_8, ((uint8_t *)&t86));
            gm_b16_8 t88 = gm_l3;
            gm_b16_8 t89 = gm_l4;
            gm_t88rcucg1b7uk t90 = gm_l5;
            gm_b16_8 t91 = gm_f_call_indirect_khk02ee((void *)NULL, cx, NULL, t70, t77, t84, t87, t88, t89, t90, ((uint8_t)1), ((uint8_t)0));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l15 = t91;
        }
        {
            void * t92 = gm_l0;
            gm_b0_1 t93 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t92);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t94 = gm_l15;
            gm_t5o0tpv5jka2l t95;
            GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t95 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t95 + 32, 0);
            ((uint8_t *)&t95)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t95 + 8), t94);
            gm_t5o0tpv5jka2l t96 = t95;
            uint8_t t97 = ((((uint8_t *)&t96))[0] != 0);
            F.ret = t95;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_trap("reached `@unreachable()`", gm_srcmeaag4q4qrjl, 309, 40);
        }
ix2:;
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
__attribute__((aligned(4))) void gm_f_call_fn_q122g0r(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3) {
    struct gm_fr_call_fn_q122g0r F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* f */
    gm_b16_8 gm_l3; /* vals */
    void * gm_l4; /* def */
    gm_b16_8 gm_l5; /* env */
    uint32_t gm_l6; /* p */
    uint32_t gm_l7; /* <tmp> */
    uint32_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* l */
    gm_b16_8 gm_l10; /* up */
    uint32_t gm_l11; /* fid */
    gm_b16_8 gm_l12; /* name */
    gm_tbl8il92l97ut gm_l13; /* sc */
    uint32_t gm_l14; /* s */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    uint32_t gm_l18; /* p */
    gm_b16_8 gm_l19; /* r */
    uint8_t gm_l20; /* foreign_ */
    uint64_t gm_l21; /* n */
    uint64_t gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint64_t gm_l25; /* i */
    gm_t88rcucg1b7uk gm_l26; /* t */
    gm_b16_8 gm_l27; /* call */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    gm_b16_8 gm_l31; /* a */
    gm_b16_8 gm_l32; /* c2 */
    gm_b16_8 gm_l33; /* cx */
    gm_b16_8 gm_l34; /* args */
    gm_b16_8 gm_l35; /* call */
    GM_STACK_CHECK(&F, 2996u);
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
        uint32_t t4 = gm_l2;
        gm_b0_1 t5 = gm_f_queue_cd51r8c((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t6 = gm_l0;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        uint32_t t9 = gm_l2;
        void * t10 = gm_f_func_in17r5n((void *)NULL, cx, NULL, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t10;
    }
    {
        gm_b16_8 t11;
        GM_ST(void *, (uint8_t *)&t11, (void *)(gm_s04qus0luoga5));
        GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(4));
        gm_l5 = t11;
    }
    {
        void * t13 = gm_l4;
        if (!(((((uint8_t *)t13) + 88))[4] != 0)) goto ie12;
        uint32_t t14 = GM_LD(uint32_t, (((uint8_t *)t13) + 88));
        gm_l6 = t14;
        {
            {
                gm_l8 = ((uint32_t)0ULL);
            }
            {
                gm_l7 = ((uint32_t)64ULL);
            }
            {
                for (;;) {
                    uint32_t t18 = gm_l8;
                    uint32_t t19 = gm_l7;
                    uint8_t t20 = (t18 < t19);
                    if (!t20) goto lb17;
                    {
                        uint32_t t23 = gm_l8;
                        uint32_t t24 = t23;
                        uint32_t t27 = GM_LD(uint32_t, ((uint8_t *)&t24));
                        gm_l9 = t27;
                        {
                            void * t29 = gm_l0;
                            uint32_t t30 = gm_l9;
                            gm_tommf4j9upomc t31;
                            gm_f_frame_up_g8486eu((void *)&t31, (void *)NULL, cx, NULL, t29, t30);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            gm_tommf4j9upomc t32 = t31;
                            gm_l10 = GM_LD(gm_b16_8, (uint8_t *)&t32 + 0);
                            gm_l11 = GM_LD(uint32_t, (uint8_t *)&t32 + 16);
                        }
                        {
                            uint32_t t34 = gm_l11;
                            uint32_t t35 = gm_l6;
                            uint8_t t36 = ((t34 == t35));
                            if (!t36) goto ie33;
                            {
                                gm_b16_8 t38 = gm_l10;
                                gm_l5 = t38;
                            }
                            {
                                goto lb17;
                            }
                            goto ix33;
ie33:;
ix33:;
                        }
                        goto me25;
mn26:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 244, 9);
me25:;
                    }
lc17:;
                    {
                        uint32_t t40 = gm_l8;
                        uint32_t r41; bool o42 = __builtin_add_overflow(t40, ((uint32_t)1ULL), &r41);
                        uint32_t t43 = r41;
                        gm_l8 = t43;
                    }
                }
gm_ll21:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
                }
                goto gm_lad;
lb17:;
            }
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        gm_b16_8 t44;
        GM_ST(void *, (uint8_t *)&t44, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t44 + 8, (uintptr_t)(0));
        gm_l12 = t44;
    }
    {
        void * t46 = gm_l0;
        if (!(((((uint8_t *)t46) + 40))[16] != 0)) goto ie45;
        gm_tbl8il92l97ut t47 = GM_LD(gm_tbl8il92l97ut, (((uint8_t *)t46) + 40));
        gm_l13 = t47;
        {
            void * t50 = GM_LD(void *, (((uint8_t *)&gm_l13) + 0));
            void * t51 = gm_l1;
            uint8_t t52 = ((t50 == t51));
            if (!t52) goto ie49;
            {
                void * t54 = gm_l0;
                gm_t5lf9i3n1r3co t55;
                memset(&t55, 0, sizeof t55);
                GM_ST(gm_t5lf9i3n1r3co, (((uint8_t *)t54) + 40), t55);
            }
            {
                void * t56 = gm_l0;
                void * t57 = GM_LD(void *, (((uint8_t *)t56) + 0));
                uint32_t t58 = gm_l2;
                uint32_t t59 = GM_LD(uint32_t, (((uint8_t *)&gm_l13) + 8));
                gm_b16_8 t60 = gm_f_queue_spec_khngsmv((void *)NULL, cx, NULL, t57, t58, t59);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t60;
            }
            goto ix49;
ie49:;
ix49:;
        }
        goto ix45;
ie45:;
ix45:;
    }
    {
        uint64_t t62 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
        uint8_t t63 = ((t62 == ((uint64_t)0ULL)));
        if (!t63) goto ie61;
        {
            void * t66 = gm_l0;
            uint32_t t67 = gm_l2;
            gm_t9805qtonbsag t68 = gm_f_passed_spec_rvmvr8h((void *)NULL, cx, NULL, t66, t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9805qtonbsag t69 = t68;
            if (!((((uint8_t *)&t69))[4] != 0)) goto ie65;
            uint32_t t70 = GM_LD(uint32_t, ((uint8_t *)&t69));
            gm_l14 = t70;
            {
                void * t72 = gm_l0;
                void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
                uint32_t t74 = gm_l2;
                uint32_t t75 = gm_l14;
                gm_b16_8 t76 = gm_f_queue_spec_khngsmv((void *)NULL, cx, NULL, t73, t74, t75);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t76;
            }
            goto ix65;
ie65:;
            {
                void * t78 = gm_l0;
                void * t79 = GM_LD(void *, (((uint8_t *)t78) + 0));
                uint32_t t80 = gm_l2;
                gm_b16_8 t81 = gm_f_callee_name_4k9m9s6((void *)NULL, cx, NULL, t79, t80);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l12 = t81;
            }
ix65:;
        }
        goto ix61;
ie61:;
ix61:;
    }
    {
        gm_f_list_nof7vfj((void *)&F.l15, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t83 = gm_l4;
            gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)t83) + 32));
            gm_l16 = t84;
        }
        {
            gm_l17 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t86 = gm_l17;
                uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                uint8_t t88 = (t86 < t87);
                if (!t88) goto lb85;
                {
                    uint64_t t91 = gm_l17;
                    uint64_t t92 = (uintptr_t)t91;
                    uint64_t t93 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                    uint32_t t94 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t92 * 4));
                    uint32_t t95 = t94;
                    uint32_t t98 = GM_LD(uint32_t, ((uint8_t *)&t95));
                    gm_l18 = t98;
                    {
                        void * t100 = (void *)((uint8_t *)&F.l15);
                        uint32_t t101 = gm_l18;
                        uint64_t t102 = (uint64_t)t101;
                        uint64_t t103 = (uintptr_t)t102;
                        void * t104 = gm_l4;
                        uint64_t t105 = GM_LD(uintptr_t, (((uint8_t *)t104) + 48) + 8);
                        if (t103 >= (uintptr_t)(t105)) gm_trap_index(t103, t105, gm_srcmeaag4q4qrjl, 263, 36);
                        gm_t88rcucg1b7uk t106 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t104) + 48)) + t103 * 40) + 16));
                        gm_tftb1odc739t5 t107 = gm_f_push_k3l9nic(t100, GM_CONV(gm_csguankko4lmhd, t106));
                        gm_tftb1odc739t5 t108 = t107;
                        if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_srcmeaag4q4qrjl, 263, 27);
                        gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
                    }
                    goto me96;
mn97:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 263, 5);
me96:;
                }
lc85:;
                {
                    uint64_t t111 = gm_l17;
                    uint64_t r112; bool o113 = __builtin_add_overflow(t111, ((uint64_t)1ULL), &r112);
                    uint64_t t114 = r112;
                    gm_l17 = t114;
                }
            }
lb85:;
        }
    }
    {
        gm_b16_8 t115;
        GM_ST(void *, (uint8_t *)&t115, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t115 + 8, (uintptr_t)(0));
        gm_l19 = t115;
    }
    {
        void * t117 = gm_l0;
        void * t118 = GM_LD(void *, (((uint8_t *)t117) + 0));
        uint32_t t119 = gm_l2;
        uint8_t t120 = gm_f_is_plain_qr08i40((void *)NULL, cx, NULL, t118, t119);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t120) goto ie116;
        {
            void * t122 = gm_l4;
            uint8_t t123;
            uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)t122) + 96) + 0));
            if (t126 != ((uint8_t)3ULL)) goto mn125;
            t123 = ((uint8_t)1);
            goto me124;
mn125:;
            t123 = ((uint8_t)0);
            goto me124;
mn127:;
            gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 267, 21);
me124:;
            gm_l20 = t123;
        }
        {
            uint64_t t128 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
            uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            uint64_t t130 = (t128 < t129 ? t128 : t129);
            gm_l21 = t130;
        }
        {
            gm_f_list_epvducg((void *)&F.l22, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l24 = ((uint64_t)0ULL);
            }
            {
                uint64_t t132 = gm_l21;
                gm_l23 = t132;
            }
            {
                for (;;) {
                    uint64_t t134 = gm_l24;
                    uint64_t t135 = gm_l23;
                    uint8_t t136 = (t134 < t135);
                    if (!t136) goto lb133;
                    {
                        uint64_t t139 = gm_l24;
                        uint64_t t140 = t139;
                        uint64_t t143 = GM_LD(uint64_t, ((uint8_t *)&t140));
                        gm_l25 = t143;
                        {
                            uint64_t t145 = gm_l25;
                            uint64_t t146 = (uintptr_t)t145;
                            uint64_t t147 = GM_LD(uintptr_t, (((uint8_t *)&F.l15) + 0) + 8);
                            if (t146 >= (uintptr_t)(t147)) gm_trap_index(t146, t147, gm_srcmeaag4q4qrjl, 271, 18);
                            gm_t88rcucg1b7uk t148 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l15) + 0)) + t146 * 4));
                            gm_l26 = t148;
                        }
                        {
                            uint8_t t150 = gm_l20;
                            uint8_t t151 = (uint8_t)!t150;
                            if (!t151) goto ie149;
                            void * t152 = gm_l0;
                            void * t153 = GM_LD(void *, (((uint8_t *)t152) + 0));
                            void * t154 = (void *)(((uint8_t *)t153) + 8);
                            gm_t88rcucg1b7uk t155 = gm_l26;
                            uint8_t t156 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t154, t155);
                            if (gm_unw.kind) {
                                goto gm_ll137;
                            }
                            if (!t156) goto ie149;
                            {
                                void * t158 = (void *)((uint8_t *)&F.l22);
                                void * t159 = gm_l0;
                                gm_t88rcucg1b7uk t160 = gm_l26;
                                uint64_t t161 = gm_l25;
                                uint64_t t162 = (uintptr_t)t161;
                                uint64_t t163 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                if (t162 >= (uintptr_t)(t163)) gm_trap_index(t162, t163, gm_srcmeaag4q4qrjl, 274, 43);
                                gm_b16_8 t164 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t162 * 16));
                                gm_b16_8 t165 = gm_f_gamma_arg_ldsagr2((void *)NULL, cx, NULL, t159, t160, t164);
                                if (gm_unw.kind) {
                                    goto gm_ll137;
                                }
                                gm_tftb1odc739t5 t166 = gm_f_push_3ef746b(t158, t165);
                                gm_tftb1odc739t5 t167 = t166;
                                if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 1, gm_srcmeaag4q4qrjl, 274, 17);
                                gm_b0_1 t168 = GM_LD(gm_b0_1, (((uint8_t *)&t167) + 1));
                            }
                            goto ix149;
ie149:;
                            {
                                void * t170 = (void *)((uint8_t *)&F.l22);
                                void * t171 = gm_l0;
                                gm_t88rcucg1b7uk t172 = gm_l26;
                                uint64_t t173 = gm_l25;
                                uint64_t t174 = (uintptr_t)t173;
                                uint64_t t175 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                if (t174 >= (uintptr_t)(t175)) gm_trap_index(t174, t175, gm_srcmeaag4q4qrjl, 276, 45);
                                gm_b16_8 t176 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t174 * 16));
                                gm_b16_8 t177 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t171, t172, t176);
                                if (gm_unw.kind) {
                                    goto gm_ll137;
                                }
                                gm_tftb1odc739t5 t178 = gm_f_push_3ef746b(t170, t177);
                                gm_tftb1odc739t5 t179 = t178;
                                if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 1, gm_srcmeaag4q4qrjl, 276, 17);
                                gm_b0_1 t180 = GM_LD(gm_b0_1, (((uint8_t *)&t179) + 1));
                            }
ix149:;
                        }
                        goto me141;
mn142:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 270, 9);
me141:;
                    }
lc133:;
                    {
                        uint64_t t182 = gm_l24;
                        uint64_t r183; bool o184 = __builtin_add_overflow(t182, ((uint64_t)1ULL), &r183);
                        uint64_t t185 = r183;
                        gm_l24 = t185;
                    }
                }
gm_ll137:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb133; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc133; }
                }
                goto gm_lad;
lb133:;
            }
        }
        {
            void * t186 = (void *)((uint8_t *)&F.l22);
            uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            void * t188 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)));
            uint64_t t189 = gm_l21;
            uint64_t t190 = (uintptr_t)t189;
            if (t190 > (uintptr_t)t187 || (uintptr_t)t187 > (uintptr_t)t187) gm_trap_slice(t190, (uintptr_t)t187, t187, gm_srcmeaag4q4qrjl, 279, 21);
            gm_b16_8 t191;
            GM_ST(void *, (uint8_t *)&t191, (void *)(((uint8_t *)t188 + t190 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t191 + 8, (uintptr_t)((uintptr_t)t187 - t190));
            gm_tftb1odc739t5 t192 = gm_f_extend_vogsd72(t186, t191);
            gm_tftb1odc739t5 t193 = t192;
            if (((((uint8_t *)&t193))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t193) + 1, gm_srcmeaag4q4qrjl, 279, 9);
            gm_b0_1 t194 = GM_LD(gm_b0_1, (((uint8_t *)&t193) + 1));
        }
        {
            gm_t6j6smj7qoctu t195;
            gm_b16_8 t196 = gm_l12;
            GM_ST(gm_b16_8, (uint8_t *)&t195 + 0, t196);
            gm_b16_8 t197 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
            gm_b16_8 t198 = t197;
            gm_b16_8 t199 = GM_LD(gm_b16_8, ((uint8_t *)&t198));
            gm_b16_8 t200;
            GM_ST(void *, (uint8_t *)&t200, (void *)(gm_sg5gdnrith2i6));
            GM_ST(uintptr_t, (uint8_t *)&t200 + 8, (uintptr_t)(2));
            gm_tabb710d9mq2e t201;
            gm_f_join_sfknljb((void *)&t201, (void *)NULL, cx, NULL, t199, t200);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t202 = t201;
            if (((((uint8_t *)&t202))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t202) + 8, gm_srcmeaag4q4qrjl, 280, 51);
            gm_b16_8 t203 = GM_LD(gm_b16_8, (((uint8_t *)&t202) + 8));
            GM_ST(gm_b16_8, (uint8_t *)&t195 + 16, t203);
            gm_tabb710d9mq2e t204;
            gm_f_format_99gc605((void *)&t204, (void *)NULL, cx, NULL, &t195);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t205 = t204;
            if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 8, gm_srcmeaag4q4qrjl, 280, 24);
            gm_b16_8 t206 = GM_LD(gm_b16_8, (((uint8_t *)&t205) + 8));
            gm_b16_8 t207 = t206;
            gm_b16_8 t208 = GM_LD(gm_b16_8, ((uint8_t *)&t207));
            gm_l27 = t208;
        }
        {
            uint8_t t210 = gm_l20;
            uint8_t t211 = (uint8_t)!t210;
            if (!t211) goto ie209;
            void * t212 = gm_l0;
            void * t213 = GM_LD(void *, (((uint8_t *)t212) + 0));
            void * t214 = (void *)(((uint8_t *)t213) + 8);
            void * t215 = gm_l4;
            gm_t88rcucg1b7uk t216 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t215) + 64));
            uint8_t t217 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t214, t216);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t217) goto ie209;
            {
                gm_f_list_uvfu1k1((void *)&F.l28, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t220 = GM_LD(gm_b16_8, (((uint8_t *)&F.l22) + 0));
                    gm_l29 = t220;
                }
                {
                    gm_l30 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t222 = gm_l30;
                        uint64_t t223 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                        uint8_t t224 = (t222 < t223);
                        if (!t224) goto lb221;
                        {
                            uint64_t t227 = gm_l30;
                            uint64_t t228 = (uintptr_t)t227;
                            uint64_t t229 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                            gm_b16_8 t232 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t228 * 16));
                            gm_l31 = t232;
                            {
                                void * t234 = (void *)((uint8_t *)&F.l28);
                                gm_b16_8 t235 = gm_l31;
                                gm_tnpifv20irnph t236;
                                GM_ST(gm_b16_8, ((uint8_t *)&t236 + 0), t235);
                                gm_tftb1odc739t5 t237 = gm_f_write_fp9dg1h(t234, t236);
                                gm_tftb1odc739t5 t238 = t237;
                                if (((((uint8_t *)&t238))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t238) + 1, gm_srcmeaag4q4qrjl, 283, 35);
                                gm_b0_1 t239 = GM_LD(gm_b0_1, (((uint8_t *)&t238) + 1));
                            }
                            goto me230;
mn231:;
                            gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 283, 13);
me230:;
                        }
lc221:;
                        {
                            uint64_t t241 = gm_l30;
                            uint64_t r242; bool o243 = __builtin_add_overflow(t241, ((uint64_t)1ULL), &r242);
                            uint64_t t244 = r242;
                            gm_l30 = t244;
                        }
                    }
lb221:;
                }
            }
            {
                void * t245 = gm_l0;
                void * t246 = gm_l4;
                gm_t88rcucg1b7uk t247 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t246) + 64));
                gm_b16_8 t248 = gm_l12;
                gm_b16_8 t249 = GM_LD(gm_b16_8, (((uint8_t *)&F.l28) + 0));
                gm_b16_8 t250 = t249;
                gm_b16_8 t251 = GM_LD(gm_b16_8, ((uint8_t *)&t250));
                gm_b16_8 t252 = gm_f_out_call_uj833o2((void *)NULL, cx, NULL, t245, t247, t248, t251);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t252;
            }
            goto ix209;
ie209:;
            void * t254 = gm_l0;
            void * t255 = GM_LD(void *, (((uint8_t *)t254) + 0));
            void * t256 = (void *)(((uint8_t *)t255) + 8);
            void * t257 = gm_l4;
            gm_t88rcucg1b7uk t258 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t257) + 64));
            uint8_t t259 = gm_f_is_void_vs2outo(t256, GM_CONV(gm_csguankko4lmhd, t258));
            if (!t259) goto ie253;
            {
                void * t261 = gm_l0;
                gm_b16_8 t262 = gm_l27;
                gm_tnpifv20irnph t263;
                GM_ST(gm_b16_8, ((uint8_t *)&t263 + 0), t262);
                gm_b0_1 t264 = gm_f_linef_qqk2cu4((void *)NULL, cx, NULL, t261, t263);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t265 = gm_l0;
                void * t266 = gm_l1;
                gm_t88rcucg1b7uk t267 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t266) + 0));
                gm_b16_8 t268 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t265, t267);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t268;
            }
            goto ix253;
ie253:;
            {
                gm_b16_8 t270;
                uint8_t t272 = gm_l20;
                uint8_t t273 = (uint8_t)!t272;
                if (!t273) goto ie271;
                void * t274 = gm_l0;
                void * t275 = GM_LD(void *, (((uint8_t *)t274) + 0));
                void * t276 = (void *)(((uint8_t *)t275) + 8);
                void * t277 = gm_l4;
                gm_t88rcucg1b7uk t278 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t277) + 64));
                uint8_t t279 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t276, t278);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t279) goto ie271;
                gm_b16_8 t281 = gm_l27;
                gm_b16_8 t282 = t281;
                t270 = t282;
                goto ix271;
ie271:;
                void * t284 = gm_l0;
                void * t285 = gm_l4;
                gm_t88rcucg1b7uk t286 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t285) + 64));
                gm_b16_8 t287 = gm_l27;
                gm_b16_8 t288 = gm_f_gamma_value_rgbfhcc((void *)NULL, cx, NULL, t284, t286, t287);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_b16_8 t289 = t288;
                t270 = t289;
ix271:;
                gm_l32 = t270;
            }
            {
                void * t290 = gm_l0;
                void * t291 = gm_l1;
                gm_t88rcucg1b7uk t292 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t291) + 0));
                gm_b16_8 t293 = gm_l32;
                gm_b16_8 t294 = gm_f_call_result_vg14o5p((void *)NULL, cx, NULL, t290, t292, t293);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t294;
            }
ix253:;
ix209:;
        }
        goto ix116;
ie116:;
        {
            void * t296 = gm_l0;
            gm_b16_8 t297 = GM_LD(gm_b16_8, (((uint8_t *)t296) + 280));
            gm_l33 = t297;
        }
        {
            void * t298 = gm_l0;
            gm_b16_8 t299 = gm_l3;
            gm_b16_8 t300 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
            gm_b16_8 t301 = t300;
            gm_b16_8 t302 = GM_LD(gm_b16_8, ((uint8_t *)&t301));
            gm_b16_8 t303 = gm_f_gamma_args_jb6l3ik((void *)NULL, cx, NULL, t298, t299, t302);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l34 = t303;
        }
        {
            void * t305 = gm_l0;
            void * t306 = GM_LD(void *, (((uint8_t *)t305) + 0));
            void * t307 = (void *)(((uint8_t *)t306) + 8);
            void * t308 = gm_l4;
            gm_t88rcucg1b7uk t309 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t308) + 64));
            uint8_t t310 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t307, t309);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t310) goto ie304;
            {
                void * t312 = gm_l0;
                void * t313 = gm_l4;
                gm_t88rcucg1b7uk t314 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t313) + 64));
                gm_b16_8 t315 = gm_l12;
                gm_tfeab279c16rr t316;
                gm_b16_8 t317 = gm_l5;
                GM_ST(gm_b16_8, (uint8_t *)&t316 + 0, t317);
                gm_b16_8 t318 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t316 + 16, t318);
                gm_b16_8 t319 = gm_l34;
                GM_ST(gm_b16_8, (uint8_t *)&t316 + 32, t319);
                gm_tabb710d9mq2e t320;
                gm_f_format_0mp0khb((void *)&t320, (void *)NULL, cx, NULL, &t316);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t321 = t320;
                if (((((uint8_t *)&t321))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t321) + 8, gm_srcmeaag4q4qrjl, 296, 44);
                gm_b16_8 t322 = GM_LD(gm_b16_8, (((uint8_t *)&t321) + 8));
                gm_b16_8 t323 = t322;
                gm_b16_8 t324 = GM_LD(gm_b16_8, ((uint8_t *)&t323));
                gm_b16_8 t325 = gm_f_out_call_uj833o2((void *)NULL, cx, NULL, t312, t314, t315, t324);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t325;
            }
            goto ix304;
ie304:;
            {
                gm_t0uuhaljsf410 t327;
                gm_b16_8 t328 = gm_l12;
                GM_ST(gm_b16_8, (uint8_t *)&t327 + 0, t328);
                gm_b16_8 t329 = gm_l5;
                GM_ST(gm_b16_8, (uint8_t *)&t327 + 16, t329);
                gm_b16_8 t330 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t327 + 32, t330);
                gm_b16_8 t331 = gm_l34;
                GM_ST(gm_b16_8, (uint8_t *)&t327 + 48, t331);
                gm_tabb710d9mq2e t332;
                gm_f_format_570tujl((void *)&t332, (void *)NULL, cx, NULL, &t327);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t333 = t332;
                if (((((uint8_t *)&t333))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t333) + 8, gm_srcmeaag4q4qrjl, 298, 21);
                gm_b16_8 t334 = GM_LD(gm_b16_8, (((uint8_t *)&t333) + 8));
                gm_l35 = t334;
            }
            {
                void * t335 = gm_l0;
                void * t336 = gm_l1;
                gm_t88rcucg1b7uk t337 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t336) + 0));
                gm_b16_8 t338 = gm_l35;
                gm_b16_8 t339 = t338;
                gm_b16_8 t340 = GM_LD(gm_b16_8, ((uint8_t *)&t339));
                gm_b16_8 t341 = gm_f_call_result_vg14o5p((void *)NULL, cx, NULL, t335, t337, t340);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l19 = t341;
            }
ix304:;
        }
ix116:;
    }
    {
        void * t343 = gm_l0;
        void * t344 = GM_LD(void *, (((uint8_t *)t343) + 0));
        uint32_t t345 = gm_l2;
        uint8_t t346 = gm_f_may_unwind_irl2ktm((void *)NULL, cx, NULL, t344, t345);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t346) goto ie342;
        {
            void * t348 = gm_l0;
            gm_b0_1 t349 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t348);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix342;
ie342:;
ix342:;
    }
    {
        gm_b16_8 t350 = gm_l19;
        gm_t5o0tpv5jka2l t351;
        GM_ST(uint8_t, (uint8_t *)&t351 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t351 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t351 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t351 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t351 + 32, 0);
        ((uint8_t *)&t351)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t351 + 8), t350);
        gm_t5o0tpv5jka2l t352 = t351;
        uint8_t t353 = ((((uint8_t *)&t352))[0] != 0);
        F.ret = t351;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_call_indirect_khk02ee(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2, gm_b16_8 a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6, uint8_t a7, uint8_t a8) {
    struct gm_fr_call_indirect_khk02ee F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* code */
    gm_b16_8 gm_l2; /* env */
    gm_b16_8 gm_l3; /* cx */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* ret */
    uint8_t gm_l7; /* hidden */
    uint8_t gm_l8; /* win64 */
    gm_b16_8 gm_l9; /* plain_ret */
    gm_b16_8 gm_l11; /* <tmp> */
    uint64_t gm_l12; /* <tmp> */
    gm_t88rcucg1b7uk gm_l13; /* p */
    gm_b16_8 gm_l14; /* plain_params */
    gm_b16_8 gm_l15; /* plain_ty */
    gm_b16_8 gm_l16; /* p */
    gm_th2lqbo77pvsc gm_l17; /* res */
    gm_b16_8 gm_l19; /* plain_call */
    gm_b16_8 gm_l20; /* fp */
    gm_b16_8 gm_l21; /* args */
    gm_b16_8 gm_l22; /* out */
    gm_b16_8 gm_l23; /* r */
    gm_b16_8 gm_l24; /* hidden_call */
    gm_th2lqbo77pvsc gm_l25; /* rv */
    gm_b16_8 gm_l26; /* r */
    GM_STACK_CHECK(&F, 2780u);
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
    gm_l7 = a7;
    gm_l8 = a8;
    {
        gm_b16_8 t2;
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_t88rcucg1b7uk t7 = gm_l6;
        uint8_t t8 = gm_f_is_void_vs2outo(t6, GM_CONV(gm_csguankko4lmhd, t7));
        if (!t8) goto ie3;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sts5j6f5pkov0));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(4));
        gm_b16_8 t11 = t10;
        t2 = t11;
        goto ix3;
ie3:;
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_l6;
        gm_b16_8 t17 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t15, t16);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t18 = t17;
        t2 = t18;
ix3:;
        gm_l9 = t2;
    }
    {
        gm_f_list_epvducg((void *)&F.l10, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t20 = gm_l5;
            gm_l11 = t20;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t22 = gm_l12;
                uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                uint8_t t24 = (t22 < t23);
                if (!t24) goto lb21;
                {
                    uint64_t t27 = gm_l12;
                    uint64_t t28 = (uintptr_t)t27;
                    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l11) + 8);
                    gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l11)) + t28 * 4));
                    gm_l13 = t32;
                    {
                        void * t34 = (void *)((uint8_t *)&F.l10);
                        void * t35 = gm_l0;
                        void * t36 = GM_LD(void *, (((uint8_t *)t35) + 0));
                        void * t37 = (void *)(((uint8_t *)t36) + 8);
                        gm_t88rcucg1b7uk t38 = gm_l13;
                        gm_b16_8 t39 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t37, t38);
                        if (gm_unw.kind) {
                            goto gm_ll25;
                        }
                        gm_tftb1odc739t5 t40 = gm_f_push_3ef746b(t34, t39);
                        gm_tftb1odc739t5 t41 = t40;
                        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcmeaag4q4qrjl, 349, 20);
                        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                    }
                    goto me30;
mn31:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 349, 5);
me30:;
                }
lc21:;
                {
                    uint64_t t44 = gm_l12;
                    uint64_t r45; bool o46 = __builtin_add_overflow(t44, ((uint64_t)1ULL), &r45);
                    uint64_t t47 = r45;
                    gm_l12 = t47;
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
        gm_b16_8 t48;
        uint64_t t50 = GM_LD(uintptr_t, (((uint8_t *)&F.l10) + 0) + 8);
        uint8_t t51 = ((t50 == ((uint64_t)0ULL)));
        if (!t51) goto ie49;
        gm_b16_8 t53;
        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sts5j6f5pkov0));
        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(4));
        gm_b16_8 t54 = t53;
        t48 = t54;
        goto ix49;
ie49:;
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
        gm_b16_8 t57 = t56;
        gm_b16_8 t58 = GM_LD(gm_b16_8, ((uint8_t *)&t57));
        gm_b16_8 t59;
        GM_ST(void *, (uint8_t *)&t59, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t60;
        gm_f_join_sfknljb((void *)&t60, (void *)NULL, cx, NULL, t58, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t61 = t60;
        if (((((uint8_t *)&t61))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t61) + 8, gm_srcmeaag4q4qrjl, 350, 67);
        gm_b16_8 t62 = GM_LD(gm_b16_8, (((uint8_t *)&t61) + 8));
        gm_b16_8 t63 = t62;
        gm_b16_8 t64 = t63;
        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
        t48 = t65;
ix49:;
        gm_l14 = t48;
    }
    {
        gm_tfeab279c16rr t66;
        gm_b16_8 t67 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t66 + 0, t67);
        uint8_t t68 = gm_l8;
        gm_b16_8 t69 = gm_f_conv_attr_m0b89pc(t68);
        GM_ST(gm_b16_8, (uint8_t *)&t66 + 16, t69);
        gm_b16_8 t70 = gm_l14;
        GM_ST(gm_b16_8, (uint8_t *)&t66 + 32, t70);
        gm_tabb710d9mq2e t71;
        gm_f_format_7tqfnpi((void *)&t71, (void *)NULL, cx, NULL, &t66);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t72 = t71;
        if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_srcmeaag4q4qrjl, 351, 17);
        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
        gm_l15 = t73;
    }
    {
        void * t74 = gm_l0;
        void * t75 = gm_l0;
        void * t76 = GM_LD(void *, (((uint8_t *)t75) + 0));
        void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
        void * t78 = GM_LD(void *, (((uint8_t *)t77) + 0));
        void * t79 = (void *)(((uint8_t *)t78) + 8);
        gm_t53at80pfdus3 t80;
        memset(&t80, 0, sizeof t80);
        GM_ST(uint8_t, ((uint8_t *)&t80 + 0), ((uint8_t)7ULL));
        gm_t88rcucg1b7uk t81 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t79, &t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t82 = gm_l1;
        gm_b16_8 t83 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t74, t81, t82);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t83;
    }
    {
        gm_th2lqbo77pvsc t84;
        memset(&t84, 0, sizeof t84);
        gm_l17 = t84;
    }
    {
        void * t86 = gm_l0;
        void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
        void * t88 = (void *)(((uint8_t *)t87) + 8);
        gm_t88rcucg1b7uk t89 = gm_l6;
        uint8_t t90 = gm_f_is_never_in5ftma(t88, GM_CONV(gm_csguankko4lmhd, t89));
        uint8_t t91 = (uint8_t)!t90;
        if (!t91) goto ie85;
        {
            void * t93 = gm_l0;
            gm_t88rcucg1b7uk t94 = gm_l6;
            gm_b16_8 t95 = gm_f_result_var_rn01cvq((void *)NULL, cx, NULL, t93, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t96;
            GM_ST(uint8_t, (uint8_t *)&t96 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t96 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t96 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t96), t95);
            ((uint8_t *)&t96)[16] = 1;
            gm_l17 = t96;
        }
        goto ix85;
ie85:;
ix85:;
    }
    {
        void * t97 = gm_l0;
        gm_b16_8 t98 = gm_l4;
        gm_b16_8 t99 = gm_l5;
        gm_f_plain_args_o7acjm5((void *)&F.l18, (void *)NULL, cx, NULL, t97, t98, t99);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tfeab279c16rr t100;
        gm_b16_8 t101 = gm_l15;
        GM_ST(gm_b16_8, (uint8_t *)&t100 + 0, t101);
        gm_b16_8 t102 = gm_l16;
        GM_ST(gm_b16_8, (uint8_t *)&t100 + 16, t102);
        gm_b16_8 t103 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
        gm_b16_8 t104 = t103;
        gm_b16_8 t105 = GM_LD(gm_b16_8, ((uint8_t *)&t104));
        gm_b16_8 t106;
        GM_ST(void *, (uint8_t *)&t106, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t106 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t107;
        gm_f_join_sfknljb((void *)&t107, (void *)NULL, cx, NULL, t105, t106);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t108 = t107;
        if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 8, gm_srcmeaag4q4qrjl, 356, 80);
        gm_b16_8 t109 = GM_LD(gm_b16_8, (((uint8_t *)&t108) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t100 + 32, t109);
        gm_tabb710d9mq2e t110;
        gm_f_format_35to3im((void *)&t110, (void *)NULL, cx, NULL, &t100);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t111 = t110;
        if (((((uint8_t *)&t111))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t111) + 8, gm_srcmeaag4q4qrjl, 356, 26);
        gm_b16_8 t112 = GM_LD(gm_b16_8, (((uint8_t *)&t111) + 8));
        gm_b16_8 t113 = t112;
        gm_b16_8 t114 = GM_LD(gm_b16_8, ((uint8_t *)&t113));
        gm_l19 = t114;
    }
    {
        void * t115 = gm_l0;
        gm_t88rcucg1b7uk t116 = gm_l6;
        gm_b16_8 t117 = gm_l19;
        gm_b16_8 t118 = gm_f_gamma_value_rgbfhcc((void *)NULL, cx, NULL, t115, t116, t117);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l19 = t118;
    }
    {
        uint8_t t120 = gm_l7;
        if (!t120) goto ie119;
        {
            void * t122 = gm_l0;
            gm_t88rcucg1b7uk t123 = gm_l6;
            gm_b16_8 t124 = gm_l5;
            gm_b16_8 t125 = gm_f_fn_ptr_type_f9efs3c((void *)NULL, cx, NULL, t122, t123, t124);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l20 = t125;
        }
        {
            void * t126 = gm_l0;
            gm_b16_8 t127 = gm_l4;
            gm_b16_8 t128 = gm_l5;
            gm_b16_8 t129 = gm_f_gamma_args_jb6l3ik((void *)NULL, cx, NULL, t126, t127, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l21 = t129;
        }
        {
            gm_b16_8 t130;
            GM_ST(void *, (uint8_t *)&t130, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t130 + 8, (uintptr_t)(0));
            gm_l22 = t130;
        }
        {
            if (!((((uint8_t *)&gm_l17))[16] != 0)) goto ie131;
            gm_b16_8 t132 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
            gm_l23 = t132;
            {
                void * t135 = gm_l0;
                void * t136 = GM_LD(void *, (((uint8_t *)t135) + 0));
                void * t137 = (void *)(((uint8_t *)t136) + 8);
                gm_t88rcucg1b7uk t138 = gm_l6;
                uint8_t t139 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t137, t138);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (!t139) goto ie134;
                {
                    gm_b16_8 t141 = gm_l23;
                    gm_tnpifv20irnph t142;
                    GM_ST(gm_b16_8, ((uint8_t *)&t142 + 0), t141);
                    gm_tabb710d9mq2e t143;
                    gm_f_format_itcjtai((void *)&t143, (void *)NULL, cx, NULL, t142);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t144 = t143;
                    if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 8, gm_srcmeaag4q4qrjl, 363, 44);
                    gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&t144) + 8));
                    gm_b16_8 t146 = t145;
                    gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
                    gm_l22 = t147;
                }
                goto ix134;
ie134:;
ix134:;
            }
            goto ix131;
ie131:;
ix131:;
        }
        {
            gm_tif3elqk01bdl t148;
            gm_b16_8 t149 = gm_l20;
            GM_ST(gm_b16_8, (uint8_t *)&t148 + 0, t149);
            gm_b16_8 t150 = gm_l16;
            GM_ST(gm_b16_8, (uint8_t *)&t148 + 16, t150);
            gm_b16_8 t151 = gm_l22;
            GM_ST(gm_b16_8, (uint8_t *)&t148 + 32, t151);
            gm_b16_8 t152 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t148 + 48, t152);
            gm_b16_8 t153 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t148 + 64, t153);
            gm_b16_8 t154 = gm_l21;
            GM_ST(gm_b16_8, (uint8_t *)&t148 + 80, t154);
            gm_tabb710d9mq2e t155;
            gm_f_format_lc73epv((void *)&t155, (void *)NULL, cx, NULL, &t148);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t156 = t155;
            if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 8, gm_srcmeaag4q4qrjl, 365, 24);
            gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&t156) + 8));
            gm_l24 = t157;
        }
        {
            void * t158 = gm_l0;
            gm_b16_8 t159 = gm_l16;
            gm_tnpifv20irnph t160;
            GM_ST(gm_b16_8, ((uint8_t *)&t160 + 0), t159);
            gm_b0_1 t161 = gm_f_linef_98ea3qh((void *)NULL, cx, NULL, t158, t160);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t162 = gm_l0;
            void * t163 = gm_l0;
            uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)t163) + 224));
            uint64_t r165; bool o166 = __builtin_add_overflow(t164, ((uint64_t)1ULL), &r165);
            if (o166) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 367, 9);
            uint64_t t167 = r165;
            GM_ST(uint64_t, (((uint8_t *)t162) + 224), t167);
        }
        {
            void * t168 = gm_l0;
            gm_t88rcucg1b7uk t169 = gm_l6;
            gm_b16_8 t170 = gm_l19;
            gm_b0_1 t171 = gm_f_calls_emit_plain_euobq8f((void *)NULL, cx, NULL, t168, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l17)), t169, t170);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t172 = gm_l0;
            void * t173 = gm_l0;
            uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)t173) + 224));
            uint64_t r175; bool o176 = __builtin_sub_overflow(t174, ((uint64_t)1ULL), &r175);
            if (o176) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 369, 9);
            uint64_t t177 = r175;
            GM_ST(uint64_t, (((uint8_t *)t172) + 224), t177);
        }
        {
            void * t178 = gm_l0;
            gm_b16_8 t179;
            GM_ST(void *, (uint8_t *)&t179, (void *)(gm_s02uuedsru1qu));
            GM_ST(uintptr_t, (uint8_t *)&t179 + 8, (uintptr_t)(8));
            gm_b0_1 t180 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t178, t179);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t181 = gm_l0;
            void * t182 = gm_l0;
            uint64_t t183 = GM_LD(uint64_t, (((uint8_t *)t182) + 224));
            uint64_t r184; bool o185 = __builtin_add_overflow(t183, ((uint64_t)1ULL), &r184);
            if (o185) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 371, 9);
            uint64_t t186 = r184;
            GM_ST(uint64_t, (((uint8_t *)t181) + 224), t186);
        }
        {
            gm_th2lqbo77pvsc t187 = gm_l17;
            gm_l25 = t187;
        }
        {
            uint8_t t189;
            if (((((uint8_t *)&gm_l25))[16] != 0)) goto mn191;
            t189 = ((uint8_t)0);
            goto me190;
mn191:;
            t189 = ((uint8_t)1);
            goto me190;
mn192:;
            gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 373, 12);
me190:;
            if (!t189) goto ie188;
            uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l22) + 8);
            uint8_t t194 = ((t193 == ((uint64_t)0ULL)));
            if (!t194) goto ie188;
            {
                void * t196 = gm_l0;
                gm_t6j6smj7qoctu t197;
                gm_th2lqbo77pvsc t198 = gm_l25;
                gm_th2lqbo77pvsc t199 = t198;
                if (!((((uint8_t *)&t199))[16] != 0)) gm_trap("unwrap of null", gm_srcmeaag4q4qrjl, 374, 34);
                gm_b16_8 t200 = GM_LD(gm_b16_8, ((uint8_t *)&t199));
                GM_ST(gm_b16_8, (uint8_t *)&t197 + 0, t200);
                gm_b16_8 t201 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t197 + 16, t201);
                gm_b0_1 t202 = gm_f_linef_i5ai2tu((void *)NULL, cx, NULL, t196, &t197);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix188;
ie188:;
            {
                void * t204 = gm_l0;
                gm_b16_8 t205 = gm_l24;
                gm_tnpifv20irnph t206;
                GM_ST(gm_b16_8, ((uint8_t *)&t206 + 0), t205);
                gm_b0_1 t207 = gm_f_linef_v355qja((void *)NULL, cx, NULL, t204, t206);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix188:;
        }
        {
            void * t208 = gm_l0;
            void * t209 = gm_l0;
            uint64_t t210 = GM_LD(uint64_t, (((uint8_t *)t209) + 224));
            uint64_t r211; bool o212 = __builtin_sub_overflow(t210, ((uint64_t)1ULL), &r211);
            if (o212) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 378, 9);
            uint64_t t213 = r211;
            GM_ST(uint64_t, (((uint8_t *)t208) + 224), t213);
        }
        {
            void * t214 = gm_l0;
            gm_b16_8 t215;
            GM_ST(void *, (uint8_t *)&t215, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t215 + 8, (uintptr_t)(1));
            gm_b0_1 t216 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t214, t215);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix119;
ie119:;
        {
            void * t218 = gm_l0;
            gm_t88rcucg1b7uk t219 = gm_l6;
            gm_b16_8 t220 = gm_l19;
            gm_b0_1 t221 = gm_f_calls_emit_plain_euobq8f((void *)NULL, cx, NULL, t218, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l17)), t219, t220);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix119:;
    }
    {
        if (!((((uint8_t *)&gm_l17))[16] != 0)) goto ie222;
        gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l17));
        gm_l26 = t223;
        {
            void * t226 = gm_l0;
            void * t227 = GM_LD(void *, (((uint8_t *)t226) + 0));
            void * t228 = (void *)(((uint8_t *)t227) + 8);
            gm_t88rcucg1b7uk t229 = gm_l6;
            uint8_t t230 = gm_f_is_unit_17uennd(t228, GM_CONV(gm_csguankko4lmhd, t229));
            uint8_t t231 = (uint8_t)!t230;
            if (!t231) goto ie225;
            {
                gm_b16_8 t233 = gm_l26;
                uint8_t t234 = 0;
                F.ret = t233;
                goto gm_fret;
            }
            goto ix225;
ie225:;
ix225:;
        }
        goto ix222;
ie222:;
ix222:;
    }
    {
        void * t235 = gm_l0;
        gm_t88rcucg1b7uk t236 = gm_l6;
        gm_b16_8 t237 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t235, t236);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t238 = 0;
        F.ret = t237;
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
__attribute__((aligned(4))) void gm_f_call_inner_i2bbshq(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3, const void * a4, gm_tnrltbc61i6v1 a5) {
    struct gm_fr_call_inner_i2bbshq F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l3; /* args */
    gm_th2lqbo77pvsc gm_l4; /* dest */
    gm_tnrltbc61i6v1 gm_l5; /* obs */
    gm_th2lqbo77pvsc gm_l6; /* cv */
    void * gm_l7; /* c */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    void * gm_l13; /* a */
    uint8_t gm_l14; /* later_pure */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* j */
    uint8_t gm_l18; /* borrows */
    void * gm_l19; /* root */
    uint8_t gm_l20; /* overlaps */
    void * gm_l21; /* r */
    gm_th2lqbo77pvsc gm_l22; /* k */
    gm_b16_8 gm_l23; /* v */
    gm_b16_8 gm_l24; /* kc */
    gm_b16_8 gm_l25; /* addr */
    uint32_t gm_l26; /* f */
    void * gm_l27; /* c */
    gm_b16_8 gm_l28; /* cvv */
    uint8_t gm_l30; /* effectful */
    gm_b16_8 gm_l31; /* cx */
    gm_b16_8 gm_l32; /* r */
    gm_tiilmfd16v3fc gm_l33; /* eff */
    uint32_t gm_l34; /* op */
    GM_STACK_CHECK(&F, 3280u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    memcpy(&gm_l4, a4, sizeof gm_l4);
    gm_l5 = a5;
    {
        gm_th2lqbo77pvsc t2;
        memset(&t2, 0, sizeof t2);
        gm_l6 = t2;
    }
    {
        uint8_t t4 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t4 != ((uint8_t)2ULL)) goto ie3;
        void * t5 = GM_LD(void *, (((uint8_t *)a2) + 8));
        gm_l7 = t5;
        {
            void * t7 = gm_l0;
            void * t8 = gm_l7;
            gm_t5o0tpv5jka2l t9;
            gm_f_expr_porbc9t((void *)&t9, (void *)NULL, cx, NULL, t7, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t10 = t9;
            if (((((uint8_t *)&t10))[0] != 0)) {
                gm_t08olq97rscn3 t11 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t10) + 8));
                gm_t5o0tpv5jka2l t12;
                GM_ST(uint8_t, (uint8_t *)&t12 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t12 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t12 + 4, 0);
                ((uint8_t *)&t12)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t12 + 8), t11);
                F.ret = t12;
                goto gm_fret;
            }
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
            gm_th2lqbo77pvsc t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[16] = 1;
            gm_l6 = t14;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        gm_f_list_epvducg((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_nof7vfj((void *)&F.l9, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
            gm_l10 = t16;
        }
        {
            for (;;) {
                uint64_t t18 = gm_l11;
                uint64_t t19 = gm_l10;
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l11;
                    uint64_t t24 = t23;
                    uint64_t t27 = GM_LD(uint64_t, ((uint8_t *)&t24));
                    gm_l12 = t27;
                    {
                        uint64_t t29 = gm_l12;
                        uint64_t t30 = (uintptr_t)t29;
                        uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t30 >= (uintptr_t)(t31)) gm_trap_index(t30, t31, gm_srcmeaag4q4qrjl, 30, 15);
                        void * t32 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t30 * 88);
                        gm_l13 = t32;
                    }
                    {
                        gm_l14 = ((uint8_t)1);
                    }
                    {
                        {
                            uint64_t t34 = gm_l12;
                            uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                            if (o36) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 34, 19);
                            uint64_t t37 = r35;
                            gm_l16 = t37;
                        }
                        {
                            uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            gm_l15 = t38;
                        }
                        {
                            for (;;) {
                                uint64_t t40 = gm_l16;
                                uint64_t t41 = gm_l15;
                                uint8_t t42 = (t40 < t41);
                                if (!t42) goto lb39;
                                {
                                    uint64_t t45 = gm_l16;
                                    uint64_t t46 = t45;
                                    uint64_t t49 = GM_LD(uint64_t, ((uint8_t *)&t46));
                                    gm_l17 = t49;
                                    {
                                        uint64_t t52 = gm_l17;
                                        uint64_t t53 = (uintptr_t)t52;
                                        uint64_t t54 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                                        if (t53 >= (uintptr_t)(t54)) gm_trap_index(t53, t54, gm_srcmeaag4q4qrjl, 35, 31);
                                        void * t55 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t53 * 88);
                                        uint8_t t56 = gm_f_no_writes_p1jjjfj(t55);
                                        uint8_t t57 = (uint8_t)!t56;
                                        if (!t57) goto ie51;
                                        {
                                            gm_l14 = ((uint8_t)0);
                                        }
                                        goto ix51;
ie51:;
ix51:;
                                    }
                                    goto me47;
mn48:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 34, 9);
me47:;
                                }
lc39:;
                                {
                                    uint64_t t60 = gm_l16;
                                    uint64_t r61; bool o62 = __builtin_add_overflow(t60, ((uint64_t)1ULL), &r61);
                                    uint64_t t63 = r61;
                                    gm_l16 = t63;
                                }
                            }
lb39:;
                        }
                    }
                    {
                        uint8_t t64 = 0;
                        uint8_t t65;
                        if (((((uint8_t *)&gm_l4))[16] != 0)) goto mn67;
                        t65 = ((uint8_t)0);
                        goto me66;
mn67:;
                        t65 = ((uint8_t)1);
                        goto me66;
mn68:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 39, 20);
me66:;
                        if (t65) {
                            void * t69 = gm_l0;
                            uint64_t t70 = gm_l12;
                            uint8_t t71 = gm_f_callee_borrows_mk6en5r((void *)NULL, cx, NULL, t69, &(*(gm_tsth9qu1dv0g3 *)((uint8_t *)a2)), t70);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            t64 = t71;
                        } else t64 = 0;
                        gm_l18 = t64;
                    }
                    {
                        void * t72 = gm_l0;
                        void * t73 = gm_l13;
                        void * t74 = gm_f_in_place_root_417o9i7((void *)NULL, cx, NULL, t72, t73);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                        gm_l19 = t74;
                    }
                    {
                        gm_l20 = ((uint8_t)1);
                    }
                    {
                        void * t76 = gm_l19;
                        void * t77 = t76;
                        if (!(GM_LD(void *, ((uint8_t *)&t77)) != 0)) goto ie75;
                        void * t78 = GM_LD(void *, ((uint8_t *)&t77));
                        gm_l21 = t78;
                        {
                            void * t80 = gm_l21;
                            gm_tnrltbc61i6v1 t81 = gm_l5;
                            uint8_t t82 = gm_l18;
                            uint8_t t83 = gm_f_arg_may_overlap_u7aort7(t80, t81, t82);
                            gm_l20 = t83;
                        }
                        goto ix75;
ie75:;
ix75:;
                    }
                    {
                        gm_th2lqbo77pvsc t84;
                        memset(&t84, 0, sizeof t84);
                        gm_l22 = t84;
                    }
                    {
                        void * t86 = gm_l0;
                        void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
                        void * t88 = (void *)(((uint8_t *)t87) + 8);
                        void * t89 = gm_l13;
                        gm_t88rcucg1b7uk t90 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t89) + 0));
                        uint8_t t91 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t88, t90);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                        if (!t91) goto ie85;
                        {
                            void * t93 = gm_l0;
                            void * t94 = GM_LD(void *, (((uint8_t *)t93) + 0));
                            void * t95 = gm_l13;
                            gm_f_const_arg_8kiicv6((void *)&(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l22)), (void *)NULL, cx, NULL, t94, t95);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                        }
                        goto ix85;
ie85:;
ix85:;
                    }
                    {
                        gm_b16_8 t96;
                        GM_ST(void *, (uint8_t *)&t96, (void *)(gm_sr4qbrk6tlgaa));
                        GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(0));
                        gm_l23 = t96;
                    }
                    {
                        if (!((((uint8_t *)&gm_l22))[16] != 0)) goto ie97;
                        gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l22));
                        gm_l24 = t98;
                        {
                            gm_t6j6smj7qoctu t100;
                            void * t101 = gm_l0;
                            void * t102 = GM_LD(void *, (((uint8_t *)t101) + 0));
                            void * t103 = (void *)(((uint8_t *)t102) + 8);
                            void * t104 = gm_l13;
                            gm_t88rcucg1b7uk t105 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t104) + 0));
                            gm_b16_8 t106 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t103, t105);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t100 + 0, t106);
                            gm_b16_8 t107 = gm_l24;
                            GM_ST(gm_b16_8, (uint8_t *)&t100 + 16, t107);
                            gm_tabb710d9mq2e t108;
                            gm_f_format_8lfevbr((void *)&t108, (void *)NULL, cx, NULL, &t100);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            gm_tabb710d9mq2e t109 = t108;
                            if (((((uint8_t *)&t109))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t109) + 8, gm_srcmeaag4q4qrjl, 48, 17);
                            gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t109) + 8));
                            gm_b16_8 t111 = t110;
                            gm_b16_8 t112 = GM_LD(gm_b16_8, ((uint8_t *)&t111));
                            gm_l23 = t112;
                        }
                        goto ix97;
ie97:;
                        void * t114 = gm_l0;
                        void * t115 = GM_LD(void *, (((uint8_t *)t114) + 0));
                        void * t116 = (void *)(((uint8_t *)t115) + 8);
                        void * t117 = gm_l13;
                        gm_t88rcucg1b7uk t118 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t117) + 0));
                        uint8_t t119 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t116, t118);
                        if (gm_unw.kind) {
                            goto gm_ll21;
                        }
                        if (!t119) goto ie113;
                        void * t120 = gm_l19;
                        void * t121 = t120;
                        uint8_t t122;
                        if ((GM_LD(void *, ((uint8_t *)&t121)) != 0)) goto mn124;
                        t122 = ((uint8_t)0);
                        goto me123;
mn124:;
                        t122 = ((uint8_t)1);
                        goto me123;
mn125:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 49, 44);
me123:;
                        if (!t122) goto ie113;
                        uint8_t t126 = gm_l14;
                        if (!t126) goto ie113;
                        uint8_t t127 = gm_l20;
                        uint8_t t128 = (uint8_t)!t127;
                        if (!t128) goto ie113;
                        {
                            void * t130 = gm_l0;
                            void * t131 = gm_l13;
                            gm_t5o0tpv5jka2l t132;
                            gm_f_place_addr_7ji15ot((void *)&t132, (void *)NULL, cx, NULL, t130, t131);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            gm_t5o0tpv5jka2l t133 = t132;
                            if (((((uint8_t *)&t133))[0] != 0)) {
                                gm_t08olq97rscn3 t134 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t133) + 8));
                                gm_t5o0tpv5jka2l t135;
                                GM_ST(uint8_t, (uint8_t *)&t135 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t135 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t135 + 4, 0);
                                ((uint8_t *)&t135)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t135 + 8), t134);
                                F.ret = t135;
                                goto gm_fret;
                            }
                            gm_b16_8 t136 = GM_LD(gm_b16_8, (((uint8_t *)&t133) + 8));
                            gm_l25 = t136;
                        }
                        {
                            gm_t6j6smj7qoctu t137;
                            void * t138 = gm_l0;
                            void * t139 = GM_LD(void *, (((uint8_t *)t138) + 0));
                            void * t140 = (void *)(((uint8_t *)t139) + 8);
                            void * t141 = gm_l13;
                            gm_t88rcucg1b7uk t142 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t141) + 0));
                            gm_b16_8 t143 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t140, t142);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t137 + 0, t143);
                            gm_b16_8 t144 = gm_l25;
                            GM_ST(gm_b16_8, (uint8_t *)&t137 + 16, t144);
                            gm_tabb710d9mq2e t145;
                            gm_f_format_8lfevbr((void *)&t145, (void *)NULL, cx, NULL, &t137);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            gm_tabb710d9mq2e t146 = t145;
                            if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 8, gm_srcmeaag4q4qrjl, 51, 17);
                            gm_b16_8 t147 = GM_LD(gm_b16_8, (((uint8_t *)&t146) + 8));
                            gm_b16_8 t148 = t147;
                            gm_b16_8 t149 = GM_LD(gm_b16_8, ((uint8_t *)&t148));
                            gm_l23 = t149;
                        }
                        goto ix113;
ie113:;
                        {
                            void * t151 = gm_l0;
                            void * t152 = gm_l13;
                            gm_t5o0tpv5jka2l t153;
                            gm_f_expr_porbc9t((void *)&t153, (void *)NULL, cx, NULL, t151, t152);
                            if (gm_unw.kind) {
                                goto gm_ll21;
                            }
                            gm_t5o0tpv5jka2l t154 = t153;
                            if (((((uint8_t *)&t154))[0] != 0)) {
                                gm_t08olq97rscn3 t155 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t154) + 8));
                                gm_t5o0tpv5jka2l t156;
                                GM_ST(uint8_t, (uint8_t *)&t156 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t156 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t156 + 4, 0);
                                ((uint8_t *)&t156)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t156 + 8), t155);
                                F.ret = t156;
                                goto gm_fret;
                            }
                            gm_b16_8 t157 = GM_LD(gm_b16_8, (((uint8_t *)&t154) + 8));
                            gm_l23 = t157;
                        }
ix113:;
ix97:;
                    }
                    {
                        void * t158 = (void *)((uint8_t *)&F.l8);
                        gm_b16_8 t159 = gm_l23;
                        gm_tftb1odc739t5 t160 = gm_f_push_3ef746b(t158, t159);
                        gm_tftb1odc739t5 t161 = t160;
                        if (((((uint8_t *)&t161))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t161) + 1, gm_srcmeaag4q4qrjl, 55, 9);
                        gm_b0_1 t162 = GM_LD(gm_b0_1, (((uint8_t *)&t161) + 1));
                    }
                    {
                        void * t163 = (void *)((uint8_t *)&F.l9);
                        void * t164 = gm_l13;
                        gm_t88rcucg1b7uk t165 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t164) + 0));
                        gm_tftb1odc739t5 t166 = gm_f_push_k3l9nic(t163, GM_CONV(gm_csguankko4lmhd, t165));
                        gm_tftb1odc739t5 t167 = t166;
                        if (((((uint8_t *)&t167))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t167) + 1, gm_srcmeaag4q4qrjl, 56, 9);
                        gm_b0_1 t168 = GM_LD(gm_b0_1, (((uint8_t *)&t167) + 1));
                    }
                    goto me25;
mn26:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 29, 5);
me25:;
                }
lc17:;
                {
                    uint64_t t170 = gm_l11;
                    uint64_t r171; bool o172 = __builtin_add_overflow(t170, ((uint64_t)1ULL), &r171);
                    uint64_t t173 = r171;
                    gm_l11 = t173;
                }
            }
gm_ll21:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb17; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc17; }
            }
            goto gm_lad;
lb17:;
        }
    }
    {
        void * t174 = gm_l0;
        void * t175 = gm_l1;
        gm_tl874t2atnlh3 t176 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t175) + 72));
        gm_b0_1 t177 = gm_f_call_site_8n74to1((void *)NULL, cx, NULL, t174, t176);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t178 = gm_l0;
        gm_th2lqbo77pvsc t179 = gm_l4;
        GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)t178) + 152), t179);
    }
    {
        uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        switch (t185) { case ((uint8_t)0ULL): goto ma181; case ((uint8_t)1ULL): goto ma183; case ((uint8_t)2ULL): goto ma182; default: goto ma184; }
ma181:;
        uint8_t t187 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t187 != ((uint8_t)0ULL)) goto mn186;
        uint32_t t188 = GM_LD(uint32_t, (((uint8_t *)a2) + 8));
        gm_l26 = t188;
        void * t189 = gm_l0;
        void * t190 = gm_l1;
        uint32_t t191 = gm_l26;
        gm_b16_8 t192 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t193 = t192;
        gm_b16_8 t194 = GM_LD(gm_b16_8, ((uint8_t *)&t193));
        gm_f_call_fn_q122g0r((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t189, t190, t191, t194);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me180;
mn186:;
ma182:;
        uint8_t t196 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t196 != ((uint8_t)2ULL)) goto mn195;
        void * t197 = GM_LD(void *, (((uint8_t *)a2) + 8));
        gm_l27 = t197;
        {
            gm_th2lqbo77pvsc t199 = gm_l6;
            gm_th2lqbo77pvsc t200 = t199;
            if (!((((uint8_t *)&t200))[16] != 0)) gm_trap("unwrap of null", gm_srcmeaag4q4qrjl, 64, 20);
            gm_b16_8 t201 = GM_LD(gm_b16_8, ((uint8_t *)&t200));
            gm_l28 = t201;
        }
        {
            void * t202 = gm_l0;
            void * t203 = GM_LD(void *, (((uint8_t *)t202) + 0));
            void * t204 = (void *)(((uint8_t *)t203) + 8);
            void * t205 = gm_l27;
            gm_t88rcucg1b7uk t206 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t205) + 0));
            gm_t53at80pfdus3 t207;
            gm_f_kind_ghvav22((void *)&t207, t204, GM_CONV(gm_csguankko4lmhd, t206));
            gm_t53at80pfdus3 t208 = t207;
            uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&t208) + 0));
            switch (t214) { case ((uint8_t)0ULL): goto ma212; case ((uint8_t)1ULL): goto ma212; case ((uint8_t)2ULL): goto ma212; case ((uint8_t)3ULL): goto ma212; case ((uint8_t)4ULL): goto ma212; case ((uint8_t)5ULL): goto ma212; case ((uint8_t)6ULL): goto ma212; case ((uint8_t)7ULL): goto ma212; case ((uint8_t)8ULL): goto ma212; case ((uint8_t)9ULL): goto ma212; case ((uint8_t)10ULL): goto ma212; case ((uint8_t)11ULL): goto ma212; case ((uint8_t)12ULL): goto ma212; case ((uint8_t)13ULL): goto ma212; case ((uint8_t)14ULL): goto ma212; case ((uint8_t)15ULL): goto ma210; case ((uint8_t)16ULL): goto ma211; case ((uint8_t)17ULL): goto ma212; case ((uint8_t)18ULL): goto ma212; case ((uint8_t)19ULL): goto ma212; case ((uint8_t)20ULL): goto ma212; case ((uint8_t)21ULL): goto ma212; case ((uint8_t)22ULL): goto ma212; case ((uint8_t)23ULL): goto ma212; case ((uint8_t)24ULL): goto ma212; case ((uint8_t)25ULL): goto ma212; case ((uint8_t)26ULL): goto ma212; default: goto ma212; }
ma210:;
            uint8_t t216 = GM_LD(uint8_t, (((uint8_t *)&t208) + 0));
            if (t216 != ((uint8_t)15ULL)) goto mn215;
            void * t217 = gm_l0;
            gm_b16_8 t218 = gm_l28;
            void * t219 = gm_l27;
            gm_t88rcucg1b7uk t220 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t219) + 0));
            gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
            gm_b16_8 t222 = t221;
            gm_b16_8 t223 = GM_LD(gm_b16_8, ((uint8_t *)&t222));
            gm_b16_8 t224 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
            gm_b16_8 t225 = t224;
            gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
            void * t227 = gm_l1;
            gm_t88rcucg1b7uk t228 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t227) + 0));
            gm_f_call_closure_mha9p1e((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t217, t218, t220, t223, t226, t228);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
            goto me209;
mn215:;
ma211:;
            uint8_t t230 = GM_LD(uint8_t, (((uint8_t *)&t208) + 0));
            if (t230 != ((uint8_t)16ULL)) goto mn229;
            gm_tddtjfggdat0d t231 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t208) + 8));
            F.l29 = t231;
            {
                void * t233 = gm_l0;
                void * t234 = GM_LD(void *, (((uint8_t *)t233) + 0));
                void * t235 = (void *)(((uint8_t *)&F.l29) + 24);
                uint8_t t236 = gm_f_within_sys_2lc2ugq(t234, t235);
                uint8_t t237 = (uint8_t)!t236;
                gm_l30 = t237;
            }
            {
                void * t238 = gm_l0;
                gm_b16_8 t239 = GM_LD(gm_b16_8, (((uint8_t *)t238) + 280));
                gm_l31 = t239;
            }
            {
                void * t240 = gm_l0;
                gm_b16_8 t241 = gm_l28;
                gm_b16_8 t242;
                GM_ST(void *, (uint8_t *)&t242, (void *)(gm_s04qus0luoga5));
                GM_ST(uintptr_t, (uint8_t *)&t242 + 8, (uintptr_t)(4));
                gm_b16_8 t243 = gm_l31;
                gm_b16_8 t244 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                gm_b16_8 t245 = t244;
                gm_b16_8 t246 = GM_LD(gm_b16_8, ((uint8_t *)&t245));
                gm_b16_8 t247 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                gm_b16_8 t248 = t247;
                gm_b16_8 t249 = GM_LD(gm_b16_8, ((uint8_t *)&t248));
                void * t250 = gm_l1;
                gm_t88rcucg1b7uk t251 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t250) + 0));
                uint8_t t252 = gm_l30;
                uint8_t t253 = GM_LD(uint8_t, (((uint8_t *)&F.l29) + 40));
                gm_b16_8 t254 = gm_f_call_indirect_khk02ee((void *)NULL, cx, NULL, t240, t241, t242, t243, t246, t249, t251, t252, t253);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l32 = t254;
            }
            {
                uint8_t t256 = 0;
                uint8_t t257 = gm_l30;
                if (!t257) {
                    void * t258 = gm_l0;
                    void * t259 = GM_LD(void *, (((uint8_t *)t258) + 0));
                    gm_b16_8 t260 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                    gm_b16_8 t261 = t260;
                    gm_b16_8 t262 = GM_LD(gm_b16_8, ((uint8_t *)&t261));
                    uint8_t t263 = gm_f_takes_fn_n239ee5(t259, t262);
                    t256 = t263;
                } else t256 = 1;
                if (!t256) goto ie255;
                {
                    void * t265 = gm_l0;
                    gm_b0_1 t266 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t265);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix255;
ie255:;
ix255:;
            }
            {
                gm_b16_8 t267 = gm_l32;
                gm_t5o0tpv5jka2l t268;
                GM_ST(uint8_t, (uint8_t *)&t268 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t268 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t268 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t268 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t268 + 32, 0);
                ((uint8_t *)&t268)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t268 + 8), t267);
                gm_t5o0tpv5jka2l t269 = t268;
                uint8_t t270 = ((((uint8_t *)&t269))[0] != 0);
                F.ret = t268;
                goto gm_fret;
            }
            goto me209;
mn229:;
ma212:;
            void * t272 = gm_l1;
            gm_tl874t2atnlh3 t273 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t272) + 72));
            void * t274 = gm_l0;
            void * t275 = GM_LD(void *, (((uint8_t *)t274) + 0));
            void * t276 = (void *)(((uint8_t *)t275) + 8);
            void * t277 = gm_l27;
            gm_t88rcucg1b7uk t278 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t277) + 0));
            gm_b16_8 t279 = gm_f_show_5qbi623((void *)NULL, cx, NULL, t276, t278);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tnpifv20irnph t280;
            GM_ST(gm_b16_8, ((uint8_t *)&t280 + 0), t279);
            gm_tabb710d9mq2e t281;
            gm_f_format_0mlfk3c((void *)&t281, (void *)NULL, cx, NULL, t280);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t282 = t281;
            if (((((uint8_t *)&t282))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t282) + 8, gm_srcmeaag4q4qrjl, 75, 50);
            gm_b16_8 t283 = GM_LD(gm_b16_8, (((uint8_t *)&t282) + 8));
            gm_b16_8 t284 = t283;
            gm_b16_8 t285 = GM_LD(gm_b16_8, ((uint8_t *)&t284));
            gm_t08olq97rscn3 t286;
            gm_f_unsupported_531q51r((void *)&t286, (void *)NULL, cx, NULL, t273, t285);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t287;
            GM_ST(uint8_t, (uint8_t *)&t287 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t287 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t287 + 4, 0);
            ((uint8_t *)&t287)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t287 + 8), t286);
            gm_t5o0tpv5jka2l t288 = t287;
            uint8_t t289 = ((((uint8_t *)&t288))[0] != 0);
            F.ret = t287;
            goto gm_fret;
            goto me209;
mn271:;
ma213:;
            gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 65, 13);
me209:;
        }
        goto me180;
mn195:;
ma183:;
        uint8_t t291 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t291 != ((uint8_t)1ULL)) goto mn290;
        gm_tiilmfd16v3fc t292 = GM_LD(gm_tiilmfd16v3fc, ((((uint8_t *)a2) + 8) + 0));
        gm_l33 = t292;
        uint32_t t293 = GM_LD(uint32_t, ((((uint8_t *)a2) + 8) + 24));
        gm_l34 = t293;
        void * t294 = gm_l0;
        void * t295 = gm_l1;
        void * t296 = (void *)((uint8_t *)&gm_l33);
        uint32_t t297 = gm_l34;
        gm_b16_8 t298 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        gm_b16_8 t299 = t298;
        gm_b16_8 t300 = GM_LD(gm_b16_8, ((uint8_t *)&t299));
        gm_b16_8 t301 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
        gm_b16_8 t302 = t301;
        gm_b16_8 t303 = GM_LD(gm_b16_8, ((uint8_t *)&t302));
        void * t304 = gm_l1;
        gm_t88rcucg1b7uk t305 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t304) + 0));
        gm_f_perform_t77ghfg((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t294, t295, t296, t297, t300, t303, t305);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me180;
mn290:;
ma184:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 61, 5);
me180:;
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
inline __attribute__((aligned(4))) void gm_f_call_nk38pfa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, const void * a2, gm_b16_8 a3) {
    struct gm_fr_call_nk38pfa F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    gm_b16_8 gm_l3; /* args */
    gm_th2lqbo77pvsc gm_l4; /* dest */
    gm_tnrltbc61i6v1 gm_l5; /* obs */
    GM_STACK_CHECK(&F, 353u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    {
        gm_th2lqbo77pvsc t2;
        memset(&t2, 0, sizeof t2);
        gm_l4 = t2;
    }
    {
        gm_tnrltbc61i6v1 t3;
        GM_ST(uint8_t, (uint8_t *)&t3 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t3 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t3 + 4, 0);
        GM_ST(uint32_t, (uint8_t *)&t3 + 8, 0);
        GM_ST(uint8_t, ((uint8_t *)&t3 + 0), ((uint8_t)0ULL));
        gm_l5 = t3;
    }
    {
        void * t5 = gm_l0;
        if (!(((((uint8_t *)t5) + 104))[40] != 0)) goto ie4;
        gm_ts0eitcfeteg8 t6 = GM_LD(gm_ts0eitcfeteg8, (((uint8_t *)t5) + 104));
        F.l6 = t6;
        {
            void * t9 = GM_LD(void *, (((uint8_t *)&F.l6) + 0));
            void * t10 = gm_l1;
            uint8_t t11 = ((t9 == t10));
            if (!t11) goto ie8;
            {
                gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 8));
                gm_th2lqbo77pvsc t14;
                GM_ST(uint8_t, (uint8_t *)&t14 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t14 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t14 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t14), t13);
                ((uint8_t *)&t14)[16] = 1;
                gm_l4 = t14;
            }
            {
                gm_tnrltbc61i6v1 t15 = GM_LD(gm_tnrltbc61i6v1, (((uint8_t *)&F.l6) + 24));
                gm_l5 = t15;
            }
            {
                void * t16 = gm_l0;
                gm_tjefu848ssvm9 t17;
                memset(&t17, 0, sizeof t17);
                GM_ST(gm_tjefu848ssvm9, (((uint8_t *)t16) + 104), t17);
            }
            goto ix8;
ie8:;
ix8:;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l1;
        gm_b16_8 t20 = gm_l3;
        gm_tnrltbc61i6v1 t21 = gm_l5;
        gm_f_call_inner_i2bbshq((void *)&F.l7, (void *)NULL, cx, NULL, t18, t19, &(*(gm_tsth9qu1dv0g3 *)((uint8_t *)a2)), t20, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)), t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t22 = gm_l0;
        gm_th2lqbo77pvsc t23;
        memset(&t23, 0, sizeof t23);
        GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)t22) + 152), t23);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l7), 40);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_call_result_vg14o5p(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_call_result_vg14o5p F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* ret */
    gm_b16_8 gm_l2; /* call */
    GM_STACK_CHECK(&F, 203u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint8_t t7 = gm_f_is_never_in5ftma(t5, GM_CONV(gm_csguankko4lmhd, t6));
        if (!t7) goto ie2;
        {
            void * t9 = gm_l0;
            gm_b16_8 t10 = gm_l2;
            gm_tnpifv20irnph t11;
            GM_ST(gm_b16_8, ((uint8_t *)&t11 + 0), t10);
            gm_b0_1 t12 = gm_f_linef_qqk2cu4((void *)NULL, cx, NULL, t9, t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t13 = gm_l0;
            gm_t88rcucg1b7uk t14 = gm_l1;
            gm_b16_8 t15 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
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
        gm_b16_8 t19 = gm_l2;
        gm_b16_8 t20 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t17, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t21 = 0;
        F.ret = t20;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_call_site_8n74to1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_call_site_8n74to1 F;
    void * gm_l0; /* fg */
    gm_tl874t2atnlh3 gm_l1; /* span */
    void * gm_l2; /* f */
    gm_tcc1rq4n1avb2 gm_l3; /* lc */
    gm_b16_8 gm_l4; /* file */
    GM_STACK_CHECK(&F, 294u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t4) + 1136) + 1));
        if (!t5) goto ie2;
        gm_tl874t2atnlh3 t6 = gm_l1;
        gm_tl874t2atnlh3 t7 = GM_LD(gm_tl874t2atnlh3, gm_cejn1dj0054);
        gm_tl874t2atnlh3 t8 = t6;
        gm_tl874t2atnlh3 t9 = t7;
        uint8_t t10 = (!gm_eqff85ac10o3(((uint8_t *)&t8), ((uint8_t *)&t9)));
        if (!t10) goto ie2;
        {
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
            void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
            uint32_t t18 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
            void * t19 = gm_f_file_o15fqku(t17, t18);
            gm_l2 = t19;
        }
        {
            void * t20 = gm_l2;
            uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
            gm_tcc1rq4n1avb2 t22 = GM_CONV(gm_tcc1rq4n1avb2, gm_f_line_col_9vn24mu(t20, t21));
            gm_l3 = t22;
        }
        {
            void * t23 = gm_l2;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)t23) + 0));
            gm_b16_8 t25 = gm_f_c_string_n7h27b3((void *)NULL, cx, NULL, t24);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t25;
        }
        {
            void * t26 = gm_l0;
            gm_t5hdnnftkup6m t27;
            gm_b16_8 t28 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t27 + 0, t28);
            uint32_t t29 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            GM_ST(uint32_t, (uint8_t *)&t27 + 16, t29);
            uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            GM_ST(uint32_t, (uint8_t *)&t27 + 20, t30);
            gm_b0_1 t31 = gm_f_linef_4ku5dms((void *)NULL, cx, NULL, t26, &t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_callee_borrows_mk6en5r(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, uint64_t a2) {
    struct gm_fr_callee_borrows_mk6en5r F;
    void * gm_l0; /* fg */
    uint64_t gm_l2; /* i */
    uint32_t gm_l3; /* f */
    gm_b16_8 gm_l4; /* r */
    GM_STACK_CHECK(&F, 128u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        switch (t7) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma4; default: goto ma6; }
ma3:;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t9 != ((uint8_t)0ULL)) goto mn8;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)a1) + 8));
        gm_l3 = t10;
        {
            void * t12 = gm_l0;
            void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
            uint32_t t14 = gm_l3;
            gm_b16_8 t15 = gm_f_read_during_build_ng10fmi((void *)NULL, cx, NULL, t13, t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t15;
        }
        {
            uint8_t t16 = 0;
            uint64_t t17 = gm_l2;
            uint64_t t18 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint8_t t19 = (t17 < t18);
            if (t19) {
                uint64_t t20 = gm_l2;
                uint64_t t21 = (uintptr_t)t20;
                uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_srcmeaag4q4qrjl, 88, 34);
                uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t21 * 1));
                t16 = t23;
            } else t16 = 0;
            uint8_t t24 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto me2;
mn8:;
ma4:;
        uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t26 != ((uint8_t)2ULL)) goto mn25;
        uint8_t t27 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me2;
mn25:;
ma5:;
        uint8_t t29 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t29 != ((uint8_t)1ULL)) goto mn28;
        uint8_t t30 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me2;
mn28:;
ma6:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 85, 5);
me2:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_calls_emit_plain_euobq8f(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3) {
    struct gm_fr_calls_emit_plain_euobq8f F;
    void * gm_l0; /* fg */
    gm_th2lqbo77pvsc gm_l1; /* res */
    gm_t88rcucg1b7uk gm_l2; /* ret */
    gm_b16_8 gm_l3; /* plain_call */
    gm_b16_8 gm_l4; /* r */
    GM_STACK_CHECK(&F, 227u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        if (!((((uint8_t *)&gm_l1))[16] != 0)) goto ie2;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l1));
        gm_l4 = t3;
        {
            void * t6 = gm_l0;
            void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
            void * t8 = (void *)(((uint8_t *)t7) + 8);
            gm_t88rcucg1b7uk t9 = gm_l2;
            uint8_t t10 = gm_f_is_unit_17uennd(t8, GM_CONV(gm_csguankko4lmhd, t9));
            uint8_t t11 = (uint8_t)!t10;
            if (!t11) goto ie5;
            {
                void * t13 = gm_l0;
                gm_t6j6smj7qoctu t14;
                gm_b16_8 t15 = gm_l4;
                GM_ST(gm_b16_8, (uint8_t *)&t14 + 0, t15);
                gm_b16_8 t16 = gm_l3;
                GM_ST(gm_b16_8, (uint8_t *)&t14 + 16, t16);
                gm_b0_1 t17 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t13, &t14);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t18;
                uint8_t t19 = 0;
                F.ret = t18;
                goto gm_fret;
            }
            goto ix5;
ie5:;
ix5:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t20 = gm_l0;
        gm_b16_8 t21 = gm_l3;
        gm_tnpifv20irnph t22;
        GM_ST(gm_b16_8, ((uint8_t *)&t22 + 0), t21);
        gm_b0_1 t23 = gm_f_linef_qqk2cu4((void *)NULL, cx, NULL, t20, t22);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_calls_native_set_iq1d80t(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_equal_hcuag9d(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3) {
    struct gm_fr_equal_hcuag9d F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* a */
    gm_b16_8 gm_l3; /* b */
    gm_b16_8 gm_l4; /* f */
    gm_b16_8 gm_l5; /* pa */
    gm_b16_8 gm_l6; /* pb */
    GM_STACK_CHECK(&F, 711u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t5v8n6qrn4rh4 t7;
        gm_f_repr_53quqki((void *)&t7, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t8 = t7;
        uint8_t t9 = ((GM_LD(uint8_t, ((uint8_t *)&t8) + 0) == ((uint8_t)6ULL)));
        if (!t9) goto ie2;
        {
            void * t11 = gm_l0;
            void * t12 = GM_LD(void *, (((uint8_t *)t11) + 0));
            void * t13 = (void *)(((uint8_t *)t12) + 1968);
            void * t14 = gm_l0;
            void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
            void * t16 = (void *)(((uint8_t *)t15) + 8);
            gm_t88rcucg1b7uk t17 = gm_l1;
            gm_b16_8 t18 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t13, t16, t17);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l4 = t18;
        }
        {
            void * t19 = gm_l0;
            gm_t88rcucg1b7uk t20 = gm_l1;
            gm_b16_8 t21 = gm_l2;
            gm_b16_8 t22 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t19, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l5 = t22;
        }
        {
            void * t23 = gm_l0;
            gm_t88rcucg1b7uk t24 = gm_l1;
            gm_b16_8 t25 = gm_l3;
            gm_b16_8 t26 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t23, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t26;
        }
        {
            gm_tfeab279c16rr t27;
            gm_b16_8 t28 = gm_l4;
            GM_ST(gm_b16_8, (uint8_t *)&t27 + 0, t28);
            gm_b16_8 t29 = gm_l5;
            GM_ST(gm_b16_8, (uint8_t *)&t27 + 16, t29);
            gm_b16_8 t30 = gm_l6;
            GM_ST(gm_b16_8, (uint8_t *)&t27 + 32, t30);
            gm_tabb710d9mq2e t31;
            gm_f_format_ht2r43m((void *)&t31, (void *)NULL, cx, NULL, &t27);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t32 = t31;
            if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_srcmeaag4q4qrjl, 700, 16);
            gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
            gm_b16_8 t34 = t33;
            gm_b16_8 t35 = GM_LD(gm_b16_8, ((uint8_t *)&t34));
            uint8_t t36 = 0;
            F.ret = t35;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_t6j6smj7qoctu t37;
        gm_b16_8 t38 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t37 + 0, t38);
        gm_b16_8 t39 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t37 + 16, t39);
        gm_tabb710d9mq2e t40;
        gm_f_format_6m84e0r((void *)&t40, (void *)NULL, cx, NULL, &t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t41 = t40;
        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 8, gm_srcmeaag4q4qrjl, 702, 12);
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)&t41) + 8));
        gm_b16_8 t43 = t42;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
        uint8_t t45 = 0;
        F.ret = t44;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_fn_format_stub_53r45gt(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_fn_format_stub_53r45gt F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* sink */
    gm_b16_8 gm_l2; /* key */
    gm_b16_8 gm_l3; /* f */
    gm_b16_8 gm_l4; /* f */
    gm_b16_8 gm_l5; /* head */
    GM_STACK_CHECK(&F, 654u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sk8csn7us23m3));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(8));
        gm_l2 = t2;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 1216);
        gm_b16_8 t7 = gm_l2;
        gm_th2lqbo77pvsc t8;
        gm_f_get_e3joqcc((void *)&t8, t6, t7);
        gm_th2lqbo77pvsc t9 = t8;
        if (!((((uint8_t *)&t9))[16] != 0)) goto ie3;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_l3 = t10;
        {
            gm_b16_8 t12 = gm_l3;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        void * t16 = (void *)(((uint8_t *)t15) + 1272);
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sl8of2tnl835f));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(5));
        gm_b16_8 t18 = gm_l2;
        gm_b16_8 t19 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t19;
    }
    {
        void * t20 = gm_l0;
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
        void * t22 = (void *)(((uint8_t *)t21) + 1216);
        gm_b16_8 t23 = gm_l2;
        gm_b16_8 t24 = gm_l4;
        gm_tftb1odc739t5 t25 = gm_f_put_7shfu9u(t22, t23, t24);
        gm_tftb1odc739t5 t26 = t25;
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srcmeaag4q4qrjl, 498, 5);
        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
    }
    {
        gm_b16_8 t28 = gm_l4;
        gm_tnpifv20irnph t29;
        GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t28);
        gm_tabb710d9mq2e t30;
        gm_f_format_n1vqu08((void *)&t30, (void *)NULL, cx, NULL, t29);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t31 = t30;
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 8, gm_srcmeaag4q4qrjl, 499, 13);
        gm_b16_8 t32 = GM_LD(gm_b16_8, (((uint8_t *)&t31) + 8));
        gm_l5 = t32;
    }
    {
        void * t33 = gm_l0;
        void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
        void * t35 = (void *)((((uint8_t *)t34) + 1968) + 0);
        gm_b16_8 t36 = gm_l5;
        gm_tnpifv20irnph t37;
        GM_ST(gm_b16_8, ((uint8_t *)&t37 + 0), t36);
        gm_tftb1odc739t5 t38 = gm_f_write_vp4v8ut(t35, t37);
        gm_tftb1odc739t5 t39 = t38;
        if (((((uint8_t *)&t39))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t39) + 1, gm_srcmeaag4q4qrjl, 500, 5);
        gm_b0_1 t40 = GM_LD(gm_b0_1, (((uint8_t *)&t39) + 1));
    }
    {
        void * t41 = gm_l0;
        void * t42 = GM_LD(void *, (((uint8_t *)t41) + 0));
        void * t43 = (void *)((((uint8_t *)t42) + 1968) + 40);
        gm_t6j6smj7qoctu t44;
        gm_b16_8 t45 = gm_l5;
        GM_ST(gm_b16_8, (uint8_t *)&t44 + 0, t45);
        gm_b16_8 t46 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t44 + 16, t46);
        gm_tftb1odc739t5 t47 = gm_f_write_6o17mnv(t43, &t44);
        gm_tftb1odc739t5 t48 = t47;
        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srcmeaag4q4qrjl, 501, 5);
        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
    }
    {
        gm_b16_8 t50 = gm_l4;
        uint8_t t51 = 0;
        F.ret = t50;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_ptr_type_f9efs3c(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_fn_ptr_type_f9efs3c F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* ret */
    gm_b16_8 gm_l2; /* params */
    gm_b16_8 gm_l3; /* r */
    gm_b16_8 gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    gm_t88rcucg1b7uk gm_l8; /* p */
    GM_STACK_CHECK(&F, 743u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(0));
        gm_l3 = t2;
    }
    {
        gm_f_list_epvducg((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 8);
        gm_t88rcucg1b7uk t7 = gm_l1;
        uint8_t t8 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t8) goto ie3;
        {
            gm_b16_8 t10;
            GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sts5j6f5pkov0));
            GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(4));
            gm_l3 = t10;
        }
        {
            void * t11 = (void *)((uint8_t *)&F.l4);
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s44ek1gecbajs));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(6));
            gm_tftb1odc739t5 t13 = gm_f_push_3ef746b(t11, t12);
            gm_tftb1odc739t5 t14 = t13;
            if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 1, gm_srcmeaag4q4qrjl, 160, 9);
            gm_b0_1 t15 = GM_LD(gm_b0_1, (((uint8_t *)&t14) + 1));
        }
        goto ix3;
ie3:;
        {
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
            void * t19 = (void *)(((uint8_t *)t18) + 8);
            gm_t88rcucg1b7uk t20 = gm_l1;
            gm_b16_8 t21 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t19, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l3 = t21;
        }
ix3:;
    }
    {
    }
    {
        void * t22 = (void *)((uint8_t *)&F.l4);
        {
            gm_b16_8 t24;
            GM_ST(void *, (uint8_t *)&t24, (void *)(gm_s44ek1gecbajs));
            GM_ST(uintptr_t, (uint8_t *)&t24 + 8, (uintptr_t)(6));
            gm_b16_8 t25;
            GM_ST(void *, (uint8_t *)&t25, (void *)(gm_sono5n5lrjd7h));
            GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(9));
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sono5n5lrjd7h));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(9));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t9bmh80edou9c *)((uint8_t *)&F.l5)) + 0, t24);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t9bmh80edou9c *)((uint8_t *)&F.l5)) + 16, t25);
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_t9bmh80edou9c *)((uint8_t *)&F.l5)) + 32, t26);
        }
        void * t27 = ((uint8_t *)&F.l5);
        if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcmeaag4q4qrjl, 164, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t27 + (uintptr_t)0 * 16)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
        gm_b16_8 t29 = t28;
        gm_tftb1odc739t5 t30 = gm_f_extend_vogsd72(t22, t29);
        gm_tftb1odc739t5 t31 = t30;
        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srcmeaag4q4qrjl, 164, 5);
        gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
    }
    {
        {
            gm_b16_8 t34 = gm_l2;
            gm_l6 = t34;
        }
        {
            gm_l7 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t36 = gm_l7;
                uint64_t t37 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t38 = (t36 < t37);
                if (!t38) goto lb35;
                {
                    uint64_t t41 = gm_l7;
                    uint64_t t42 = (uintptr_t)t41;
                    uint64_t t43 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                    gm_t88rcucg1b7uk t46 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t42 * 4));
                    gm_l8 = t46;
                    {
                        void * t48 = (void *)((uint8_t *)&F.l4);
                        void * t49 = gm_l0;
                        void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
                        void * t51 = (void *)(((uint8_t *)t50) + 8);
                        gm_t88rcucg1b7uk t52 = gm_l8;
                        gm_b16_8 t53 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t51, t52);
                        if (gm_unw.kind) {
                            goto gm_ll39;
                        }
                        gm_tftb1odc739t5 t54 = gm_f_push_3ef746b(t48, t53);
                        gm_tftb1odc739t5 t55 = t54;
                        if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_srcmeaag4q4qrjl, 165, 23);
                        gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
                    }
                    goto me44;
mn45:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 165, 5);
me44:;
                }
lc35:;
                {
                    uint64_t t58 = gm_l7;
                    uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                    uint64_t t61 = r59;
                    gm_l7 = t61;
                }
            }
gm_ll39:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb35; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc35; }
            }
            goto gm_lad;
lb35:;
        }
    }
    {
        gm_t6j6smj7qoctu t62;
        gm_b16_8 t63 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t62 + 0, t63);
        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t65 = t64;
        gm_b16_8 t66 = GM_LD(gm_b16_8, ((uint8_t *)&t65));
        gm_b16_8 t67;
        GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t68;
        gm_f_join_sfknljb((void *)&t68, (void *)NULL, cx, NULL, t66, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t69 = t68;
        if (((((uint8_t *)&t69))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t69) + 8, gm_srcmeaag4q4qrjl, 166, 40);
        gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&t69) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t62 + 16, t70);
        gm_tabb710d9mq2e t71;
        gm_f_format_p19pl6n((void *)&t71, (void *)NULL, cx, NULL, &t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t72 = t71;
        if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_srcmeaag4q4qrjl, 166, 12);
        gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
        gm_b16_8 t74 = t73;
        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
        uint8_t t76 = 0;
        F.ret = t75;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_fn_sink_stub_5ipff9h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_fn_sink_stub_5ipff9h F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* str_ty */
    gm_b16_8 gm_l2; /* key */
    gm_b16_8 gm_l3; /* f */
    gm_b16_8 gm_l4; /* f */
    gm_t88rcucg1b7uk gm_l5; /* unit_ty */
    gm_tnqk2qt7j42o9 gm_l6; /* tys */
    gm_b16_8 gm_l7; /* head */
    gm_b16_8 gm_l8; /* pt */
    gm_b16_8 gm_l9; /* plain */
    gm_b16_8 gm_l10; /* hidden */
    GM_STACK_CHECK(&F, 1214u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t2;
        GM_ST(void *, (uint8_t *)&t2, (void *)(gm_s0aqhubr4upni));
        GM_ST(uintptr_t, (uint8_t *)&t2 + 8, (uintptr_t)(6));
        gm_l2 = t2;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        void * t6 = (void *)(((uint8_t *)t5) + 1216);
        gm_b16_8 t7 = gm_l2;
        gm_th2lqbo77pvsc t8;
        gm_f_get_e3joqcc((void *)&t8, t6, t7);
        gm_th2lqbo77pvsc t9 = t8;
        if (!((((uint8_t *)&t9))[16] != 0)) goto ie3;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        gm_l3 = t10;
        {
            gm_b16_8 t12 = gm_l3;
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        goto ix3;
ie3:;
ix3:;
    }
    {
        void * t14 = gm_l0;
        void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
        void * t16 = (void *)(((uint8_t *)t15) + 1272);
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sl8of2tnl835f));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(5));
        gm_b16_8 t18 = gm_l2;
        gm_b16_8 t19 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t16, t17, t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t19;
    }
    {
        void * t20 = gm_l0;
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
        void * t22 = (void *)(((uint8_t *)t21) + 1216);
        gm_b16_8 t23 = gm_l2;
        gm_b16_8 t24 = gm_l4;
        gm_tftb1odc739t5 t25 = gm_f_put_7shfu9u(t22, t23, t24);
        gm_tftb1odc739t5 t26 = t25;
        if (((((uint8_t *)&t26))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t26) + 1, gm_srcmeaag4q4qrjl, 478, 5);
        gm_b0_1 t27 = GM_LD(gm_b0_1, (((uint8_t *)&t26) + 1));
    }
    {
        void * t28 = gm_l0;
        void * t29 = GM_LD(void *, (((uint8_t *)t28) + 0));
        void * t30 = (void *)(((uint8_t *)t29) + 8);
        gm_t88rcucg1b7uk t31 = gm_f_unit_ty_vgceilu((void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t31;
    }
    {
        gm_t88rcucg1b7uk t32 = gm_l1;
        gm_tnqk2qt7j42o9 t33;
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t33 + 0), t32);
        gm_l6 = t33;
    }
    {
        gm_b16_8 t34 = gm_l4;
        gm_tnpifv20irnph t35;
        GM_ST(gm_b16_8, ((uint8_t *)&t35 + 0), t34);
        gm_tabb710d9mq2e t36;
        gm_f_format_01h4tbj((void *)&t36, (void *)NULL, cx, NULL, t35);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t37 = t36;
        if (((((uint8_t *)&t37))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t37) + 8, gm_srcmeaag4q4qrjl, 481, 13);
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&t37) + 8));
        gm_l7 = t38;
    }
    {
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
        void * t41 = (void *)((((uint8_t *)t40) + 1968) + 0);
        gm_b16_8 t42 = gm_l7;
        gm_tnpifv20irnph t43;
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t42);
        gm_tftb1odc739t5 t44 = gm_f_write_vp4v8ut(t41, t43);
        gm_tftb1odc739t5 t45 = t44;
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srcmeaag4q4qrjl, 482, 5);
        gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
    }
    {
        void * t47 = gm_l0;
        void * t48 = GM_LD(void *, (((uint8_t *)t47) + 0));
        void * t49 = (void *)(((uint8_t *)t48) + 8);
        gm_t88rcucg1b7uk t50 = gm_l1;
        gm_b16_8 t51 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t49, t50);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t51;
    }
    {
        gm_t6j6smj7qoctu t52;
        void * t53 = gm_l0;
        void * t54 = GM_LD(void *, (((uint8_t *)t53) + 0));
        void * t55 = (void *)(((uint8_t *)t54) + 8);
        gm_t88rcucg1b7uk t56 = gm_l1;
        gm_b16_8 t57 = gm_f_plain_cty_q9lhlci((void *)NULL, cx, NULL, t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t52 + 0, t57);
        void * t58 = gm_l0;
        gm_t88rcucg1b7uk t59 = gm_l1;
        gm_b16_8 t60;
        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_so78814tnt5g5));
        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(1));
        gm_b16_8 t61 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t58, t59, t60);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t52 + 16, t61);
        gm_tabb710d9mq2e t62;
        gm_f_format_drnt54k((void *)&t62, (void *)NULL, cx, NULL, &t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t63 = t62;
        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srcmeaag4q4qrjl, 484, 14);
        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)&t63) + 8));
        gm_l9 = t64;
    }
    {
        void * t65 = gm_l0;
        gm_t88rcucg1b7uk t66 = gm_l5;
        void * t67 = ((uint8_t *)&gm_l6);
        if ((uintptr_t)0 > (uintptr_t)1 || (uintptr_t)1 > (uintptr_t)1) gm_trap_slice((uintptr_t)0, (uintptr_t)1, 1, gm_srcmeaag4q4qrjl, 485, 82);
        gm_b16_8 t68;
        GM_ST(void *, (uint8_t *)&t68, (void *)(((uint8_t *)t67 + (uintptr_t)0 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)((uintptr_t)1 - (uintptr_t)0));
        gm_b16_8 t69 = t68;
        gm_b16_8 t70 = GM_LD(gm_b16_8, ((uint8_t *)&t69));
        gm_b16_8 t71 = gm_f_fn_ptr_type_f9efs3c((void *)NULL, cx, NULL, t65, t66, t70);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tnpifv20irnph t72;
        GM_ST(gm_b16_8, ((uint8_t *)&t72 + 0), t71);
        gm_tabb710d9mq2e t73;
        gm_f_format_5p0qm94((void *)&t73, (void *)NULL, cx, NULL, t72);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t74 = t73;
        if (((((uint8_t *)&t74))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t74) + 8, gm_srcmeaag4q4qrjl, 485, 15);
        gm_b16_8 t75 = GM_LD(gm_b16_8, (((uint8_t *)&t74) + 8));
        gm_l10 = t75;
    }
    {
        void * t76 = gm_l0;
        void * t77 = GM_LD(void *, (((uint8_t *)t76) + 0));
        void * t78 = (void *)((((uint8_t *)t77) + 1968) + 40);
        gm_ti71ll5knnvij t79;
        gm_b16_8 t80 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t79 + 0, t80);
        gm_b16_8 t81 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t79 + 16, t81);
        void * t82 = gm_l0;
        uint64_t t83 = gm_f_ptr_bytes_eocneng(t82);
        GM_ST(uint64_t, (uint8_t *)&t79 + 32, t83);
        gm_b16_8 t84 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t79 + 40, t84);
        gm_b16_8 t85 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t79 + 56, t85);
        gm_tftb1odc739t5 t86 = gm_f_write_502hm86(t78, &t79);
        gm_tftb1odc739t5 t87 = t86;
        if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_srcmeaag4q4qrjl, 486, 5);
        gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
    }
    {
        gm_b16_8 t89 = gm_l4;
        uint8_t t90 = 0;
        F.ret = t89;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_gamma_arg_ldsagr2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_gamma_arg_ldsagr2 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    uint8_t gm_l3; /* is_temp */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l6; /* b */
    gm_b16_8 gm_l7; /* n */
    GM_STACK_CHECK(&F, 555u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        uint8_t t7 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t8 = (uint8_t)!t7;
        if (!t8) goto ie2;
        {
            gm_b16_8 t10 = gm_l2;
            uint8_t t11 = 0;
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t12 = 0;
        uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t14 = (t13 > ((uint64_t)1ULL));
        if (t14) {
            uint64_t t15 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t15 >= (uintptr_t)(t16)) gm_trap_index(t15, t16, gm_srcmeaag4q4qrjl, 193, 30);
            uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t15 * 1));
            uint8_t t18 = ((t17 == ((uint8_t)116ULL)));
            t12 = t18;
        } else t12 = 0;
        gm_l3 = t12;
    }
    {
        uint8_t t20 = gm_l3;
        if (!t20) goto ie19;
        {
            {
                uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                void * t24 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
                uint64_t t25 = (uintptr_t)((uint64_t)1ULL);
                if (t25 > (uintptr_t)t23 || (uintptr_t)t23 > (uintptr_t)t23) gm_trap_slice(t25, (uintptr_t)t23, t23, gm_srcmeaag4q4qrjl, 195, 18);
                gm_b16_8 t26;
                GM_ST(void *, (uint8_t *)&t26, (void *)(((uint8_t *)t24 + t25 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)((uintptr_t)t23 - t25));
                gm_l4 = t26;
            }
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t28 = gm_l5;
                    uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t30 = (t28 < t29);
                    if (!t30) goto lb27;
                    {
                        uint64_t t33 = gm_l5;
                        uint64_t t34 = (uintptr_t)t33;
                        uint64_t t35 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                        uint8_t t36 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t34 * 1));
                        uint8_t t37 = t36;
                        uint8_t t40 = GM_LD(uint8_t, ((uint8_t *)&t37));
                        gm_l6 = t40;
                        {
                            uint8_t t43 = 0;
                            uint8_t t44 = gm_l6;
                            uint8_t t45 = (t44 < ((uint8_t)48ULL));
                            if (!t45) {
                                uint8_t t46 = gm_l6;
                                uint8_t t47 = (t46 > ((uint8_t)57ULL));
                                t43 = t47;
                            } else t43 = 1;
                            if (!t43) goto ie42;
                            {
                                gm_l3 = ((uint8_t)0);
                            }
                            goto ix42;
ie42:;
ix42:;
                        }
                        goto me38;
mn39:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 195, 9);
me38:;
                    }
lc27:;
                    {
                        uint64_t t50 = gm_l5;
                        uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                        uint64_t t53 = r51;
                        gm_l5 = t53;
                    }
                }
lb27:;
            }
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        gm_b16_8 t54;
        uint8_t t56 = 0;
        uint8_t t57 = gm_l3;
        if (!t57) {
            gm_b16_8 t58 = gm_l2;
            gm_b16_8 t59;
            GM_ST(void *, (uint8_t *)&t59, (void *)(gm_somhu4vltiil1));
            GM_ST(uintptr_t, (uint8_t *)&t59 + 8, (uintptr_t)(3));
            uint8_t t60 = gm_f_starts_with_6vvt01e(t58, t59);
            t56 = t60;
        } else t56 = 1;
        if (!t56) goto ie55;
        gm_b16_8 t62 = gm_l2;
        gm_b16_8 t63 = t62;
        t54 = t63;
        goto ix55;
ie55:;
        void * t65 = gm_l0;
        gm_t88rcucg1b7uk t66 = gm_l1;
        gm_b16_8 t67 = gm_l2;
        gm_b16_8 t68 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t65, t66, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_b16_8 t69 = t68;
        t54 = t69;
ix55:;
        gm_l7 = t54;
    }
    {
        gm_b16_8 t70 = gm_l7;
        gm_tnpifv20irnph t71;
        GM_ST(gm_b16_8, ((uint8_t *)&t71 + 0), t70);
        gm_tabb710d9mq2e t72;
        gm_f_format_h6b2ofo((void *)&t72, (void *)NULL, cx, NULL, t71);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t73 = t72;
        if (((((uint8_t *)&t73))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t73) + 8, gm_srcmeaag4q4qrjl, 200, 12);
        gm_b16_8 t74 = GM_LD(gm_b16_8, (((uint8_t *)&t73) + 8));
        gm_b16_8 t75 = t74;
        gm_b16_8 t76 = GM_LD(gm_b16_8, ((uint8_t *)&t75));
        uint8_t t77 = 0;
        F.ret = t76;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_gamma_args_jb6l3ik(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_gamma_args_jb6l3ik F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* vals */
    gm_b16_8 gm_l2; /* tys */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 346u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_uvfu1k1((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t5 = (t3 < t4 ? t3 : t4);
            gm_l4 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l5;
                uint64_t t8 = gm_l4;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l5;
                    uint64_t t13 = t12;
                    uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&t13));
                    gm_l6 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l3);
                        void * t19 = gm_l0;
                        uint64_t t20 = gm_l6;
                        uint64_t t21 = (uintptr_t)t20;
                        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_srcmeaag4q4qrjl, 207, 72);
                        gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t21 * 4));
                        uint64_t t24 = gm_l6;
                        uint64_t t25 = (uintptr_t)t24;
                        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srcmeaag4q4qrjl, 207, 80);
                        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t25 * 16));
                        gm_b16_8 t28 = gm_f_gamma_arg_ldsagr2((void *)NULL, cx, NULL, t19, t23, t27);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_tnpifv20irnph t29;
                        GM_ST(gm_b16_8, ((uint8_t *)&t29 + 0), t28);
                        gm_tftb1odc739t5 t30 = gm_f_write_fp9dg1h(t18, t29);
                        gm_tftb1odc739t5 t31 = t30;
                        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srcmeaag4q4qrjl, 207, 41);
                        gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 207, 5);
me14:;
                }
lc6:;
                {
                    uint64_t t34 = gm_l5;
                    uint64_t r35; bool o36 = __builtin_add_overflow(t34, ((uint64_t)1ULL), &r35);
                    uint64_t t37 = r35;
                    gm_l5 = t37;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
        }
    }
    {
        gm_b16_8 t38 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t39 = t38;
        gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
        uint8_t t41 = 0;
        F.ret = t40;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_gamma_value_rgbfhcc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_gamma_value_rgbfhcc F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    GM_STACK_CHECK(&F, 343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_th2lqbo77pvsc t7;
        gm_f_mirror_lcjegsf((void *)&t7, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t8 = t7;
        uint8_t t9;
        if (((((uint8_t *)&t8))[16] != 0)) goto mn11;
        t9 = ((uint8_t)0);
        goto me10;
mn11:;
        t9 = ((uint8_t)1);
        goto me10;
mn12:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 177, 8);
me10:;
        if (!t9) goto ie2;
        {
            gm_t6j6smj7qoctu t14;
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            void * t17 = (void *)(((uint8_t *)t16) + 8);
            gm_t88rcucg1b7uk t18 = gm_l1;
            gm_b16_8 t19 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t17, t18);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t14 + 0, t19);
            gm_b16_8 t20 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t14 + 16, t20);
            gm_tabb710d9mq2e t21;
            gm_f_format_3s8s1g6((void *)&t21, (void *)NULL, cx, NULL, &t14);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_srcmeaag4q4qrjl, 177, 47);
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
            gm_b16_8 t24 = t23;
            gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)&t24));
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t27 = gm_l2;
        uint8_t t28 = 0;
        F.ret = t27;
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
__attribute__((aligned(4))) void * gm_f_in_place_root_417o9i7(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_in_place_root_417o9i7 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* a */
    void * gm_l2; /* e */
    void * gm_l3; /* base */
    void * gm_l4; /* base */
    void * gm_l5; /* index */
    GM_STACK_CHECK(&F, 421u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_l2 = t2;
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb3;
            {
                void * t6 = gm_l2;
                uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
                switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma8; case ((uint8_t)16ULL): goto ma8; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma8; case ((uint8_t)19ULL): goto ma9; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; case ((uint8_t)27ULL): goto ma11; case ((uint8_t)28ULL): goto ma11; case ((uint8_t)29ULL): goto ma11; case ((uint8_t)30ULL): goto ma11; case ((uint8_t)31ULL): goto ma11; case ((uint8_t)32ULL): goto ma11; case ((uint8_t)33ULL): goto ma11; case ((uint8_t)34ULL): goto ma11; case ((uint8_t)35ULL): goto ma11; case ((uint8_t)36ULL): goto ma11; case ((uint8_t)37ULL): goto ma11; case ((uint8_t)38ULL): goto ma11; case ((uint8_t)39ULL): goto ma11; case ((uint8_t)40ULL): goto ma11; case ((uint8_t)41ULL): goto ma11; case ((uint8_t)42ULL): goto ma11; case ((uint8_t)43ULL): goto ma11; case ((uint8_t)44ULL): goto ma11; case ((uint8_t)45ULL): goto ma11; case ((uint8_t)46ULL): goto ma11; case ((uint8_t)47ULL): goto ma11; case ((uint8_t)48ULL): goto ma11; case ((uint8_t)49ULL): goto ma11; case ((uint8_t)50ULL): goto ma11; case ((uint8_t)51ULL): goto ma11; case ((uint8_t)52ULL): goto ma11; case ((uint8_t)53ULL): goto ma11; case ((uint8_t)54ULL): goto ma11; case ((uint8_t)55ULL): goto ma11; case ((uint8_t)56ULL): goto ma11; case ((uint8_t)57ULL): goto ma11; case ((uint8_t)58ULL): goto ma11; default: goto ma11; }
ma8:;
                uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
                if (t17 != ((uint8_t)15ULL)) goto pa16;
                goto po15;
pa16:;
                uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
                if (t19 != ((uint8_t)16ULL)) goto pa18;
                goto po15;
pa18:;
                uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
                if (t21 != ((uint8_t)18ULL)) goto pa20;
                goto po15;
pa20:;
                goto mn14;
po15:;
                void * t22 = gm_l2;
                uint8_t t23 = 0;
                F.ret = t22;
                goto gm_fret;
                goto me7;
mn14:;
ma9:;
                uint8_t t25 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
                if (t25 != ((uint8_t)19ULL)) goto mn24;
                void * t26 = GM_LD(void *, (((((uint8_t *)t6) + 8) + 8) + 0));
                gm_l3 = t26;
                {
                    void * t29 = gm_l0;
                    void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
                    void * t31 = (void *)(((uint8_t *)t30) + 8);
                    void * t32 = gm_l3;
                    gm_t88rcucg1b7uk t33 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t32) + 0));
                    gm_t57euh612ligr t34;
                    gm_f_bits_layout_23rect9((void *)&t34, (void *)NULL, cx, NULL, t31, t33);
                    if (gm_unw.kind) {
                        goto gm_ll4;
                    }
                    gm_t57euh612ligr t35 = t34;
                    uint8_t t36;
                    if (((((uint8_t *)&t35))[24] != 0)) goto mn38;
                    t36 = ((uint8_t)0);
                    goto me37;
mn38:;
                    t36 = ((uint8_t)1);
                    goto me37;
mn39:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 106, 20);
me37:;
                    if (!t36) goto ie28;
                    {
                        uint8_t t41 = 0;
                        F.ret = ((void *)0);
                        goto gm_fret;
                    }
                    goto ix28;
ie28:;
ix28:;
                }
                {
                    void * t42 = gm_l3;
                    gm_l2 = t42;
                }
                goto me7;
mn24:;
ma10:;
                uint8_t t44 = GM_LD(uint8_t, ((((uint8_t *)t6) + 8) + 0));
                if (t44 != ((uint8_t)20ULL)) goto mn43;
                void * t45 = GM_LD(void *, (((((uint8_t *)t6) + 8) + 8) + 0));
                gm_l4 = t45;
                void * t46 = GM_LD(void *, (((((uint8_t *)t6) + 8) + 8) + 8));
                gm_l5 = t46;
                {
                    void * t49 = gm_l5;
                    uint8_t t50 = gm_f_no_writes_p1jjjfj(t49);
                    uint8_t t51 = (uint8_t)!t50;
                    if (!t51) goto ie48;
                    {
                        uint8_t t53 = 0;
                        F.ret = ((void *)0);
                        goto gm_fret;
                    }
                    goto ix48;
ie48:;
ix48:;
                }
                {
                    void * t54 = gm_l0;
                    void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
                    void * t56 = (void *)(((uint8_t *)t55) + 8);
                    void * t57 = gm_l4;
                    gm_t88rcucg1b7uk t58 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t57) + 0));
                    gm_t53at80pfdus3 t59;
                    gm_f_kind_ghvav22((void *)&t59, t56, GM_CONV(gm_csguankko4lmhd, t58));
                    gm_t53at80pfdus3 t60 = t59;
                    uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
                    switch (t66) { case ((uint8_t)0ULL): goto ma64; case ((uint8_t)1ULL): goto ma64; case ((uint8_t)2ULL): goto ma64; case ((uint8_t)3ULL): goto ma64; case ((uint8_t)4ULL): goto ma64; case ((uint8_t)5ULL): goto ma64; case ((uint8_t)6ULL): goto ma64; case ((uint8_t)7ULL): goto ma64; case ((uint8_t)8ULL): goto ma64; case ((uint8_t)9ULL): goto ma63; case ((uint8_t)10ULL): goto ma63; case ((uint8_t)11ULL): goto ma62; case ((uint8_t)12ULL): goto ma64; case ((uint8_t)13ULL): goto ma64; case ((uint8_t)14ULL): goto ma64; case ((uint8_t)15ULL): goto ma64; case ((uint8_t)16ULL): goto ma64; case ((uint8_t)17ULL): goto ma64; case ((uint8_t)18ULL): goto ma64; case ((uint8_t)19ULL): goto ma64; case ((uint8_t)20ULL): goto ma64; case ((uint8_t)21ULL): goto ma64; case ((uint8_t)22ULL): goto ma64; case ((uint8_t)23ULL): goto ma64; case ((uint8_t)24ULL): goto ma64; case ((uint8_t)25ULL): goto ma64; case ((uint8_t)26ULL): goto ma64; default: goto ma64; }
ma62:;
                    uint8_t t68 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
                    if (t68 != ((uint8_t)11ULL)) goto mn67;
                    {
                        void * t70 = gm_l4;
                        gm_l2 = t70;
                    }
                    goto me61;
mn67:;
ma63:;
                    uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
                    if (t74 != ((uint8_t)10ULL)) goto pa73;
                    goto po72;
pa73:;
                    uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&t60) + 0));
                    if (t76 != ((uint8_t)9ULL)) goto pa75;
                    goto po72;
pa75:;
                    goto mn71;
po72:;
                    {
                        void * t79 = gm_l4;
                        uint8_t t80 = gm_f_no_writes_p1jjjfj(t79);
                        if (!t80) goto ie78;
                        {
                            void * t82 = gm_l2;
                            uint8_t t83 = 0;
                            F.ret = t82;
                            goto gm_fret;
                        }
                        goto ix78;
ie78:;
ix78:;
                    }
                    {
                        uint8_t t84 = 0;
                        F.ret = ((void *)0);
                        goto gm_fret;
                    }
                    goto me61;
mn71:;
ma64:;
                    uint8_t t86 = 0;
                    F.ret = ((void *)0);
                    goto gm_fret;
                    goto me61;
mn85:;
ma65:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 111, 17);
me61:;
                }
                goto me7;
mn43:;
ma11:;
                uint8_t t88 = 0;
                F.ret = ((void *)0);
                goto gm_fret;
                goto me7;
mn87:;
ma12:;
                gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 103, 9);
me7:;
            }
lc3:;
        }
gm_ll4:;
        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb3; }
            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc3; }
        }
        goto gm_lad;
lb3:;
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
__attribute__((aligned(4))) void gm_f_native_op_r81a4at(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, const void * a4, gm_t88rcucg1b7uk a5) {
    struct gm_fr_native_op_r81a4at F;
    void * gm_l0; /* fg */
    void * gm_l1; /* effect_ */
    uint32_t gm_l2; /* op */
    gm_b16_8 gm_l3; /* vals */
    gm_th2lqbo77pvsc gm_l4; /* res */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    gm_tk8ebsa8coi4p gm_l6; /* amb */
    uint64_t gm_l7; /* p */
    gm_b16_8 gm_l8; /* f */
    gm_b16_8 gm_l9; /* u */
    gm_b16_8 gm_l10; /* s */
    gm_b16_8 gm_l11; /* s */
    gm_b16_8 gm_l12; /* some */
    gm_b16_8 gm_l13; /* none */
    GM_STACK_CHECK(&F, 1497u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    memcpy(&gm_l4, a4, sizeof gm_l4);
    gm_l5 = a5;
    {
        {
            void * t3 = gm_l0;
            void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
            void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
            gm_tk8ebsa8coi4p t6 = GM_LD(gm_tk8ebsa8coi4p, (((uint8_t *)t5) + 48));
            gm_l6 = t6;
        }
        {
            void * t7 = gm_l0;
            uint64_t t8 = gm_f_ptr_bytes_eocneng(t7);
            gm_l7 = t8;
        }
        {
            uint8_t t10 = 0;
            gm_t9805qtonbsag t11 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l6) + 0));
            void * t12 = gm_l1;
            uint32_t t13 = GM_LD(uint32_t, (((uint8_t *)t12) + 0));
            gm_t9805qtonbsag t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[4] = 1;
            gm_t9805qtonbsag t15 = t11;
            gm_t9805qtonbsag t16 = t14;
            uint8_t t17 = (gm_eqkricceou4u(((uint8_t *)&t15), ((uint8_t *)&t16)));
            if (!t17) {
                gm_t9805qtonbsag t18 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l6) + 8));
                void * t19 = gm_l1;
                uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)t19) + 0));
                gm_t9805qtonbsag t21;
                GM_ST(uint8_t, (uint8_t *)&t21 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t21 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t21), t20);
                ((uint8_t *)&t21)[4] = 1;
                gm_t9805qtonbsag t22 = t18;
                gm_t9805qtonbsag t23 = t21;
                uint8_t t24 = (gm_eqkricceou4u(((uint8_t *)&t22), ((uint8_t *)&t23)));
                t10 = t24;
            } else t10 = 1;
            if (!t10) goto ie9;
            {
                gm_b16_8 t26;
                gm_t9805qtonbsag t28 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l6) + 0));
                void * t29 = gm_l1;
                uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)t29) + 0));
                gm_t9805qtonbsag t31;
                GM_ST(uint8_t, (uint8_t *)&t31 + 5, 0);
                GM_ST(uint16_t, (uint8_t *)&t31 + 6, 0);
                GM_ST(uint32_t, ((uint8_t *)&t31), t30);
                ((uint8_t *)&t31)[4] = 1;
                gm_t9805qtonbsag t32 = t28;
                gm_t9805qtonbsag t33 = t31;
                uint8_t t34 = (gm_eqkricceou4u(((uint8_t *)&t32), ((uint8_t *)&t33)));
                if (!t34) goto ie27;
                gm_b16_8 t36;
                GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s9u3cqhrfmcru));
                GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(12));
                gm_b16_8 t37 = t36;
                t26 = t37;
                goto ix27;
ie27:;
                gm_b16_8 t39;
                GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sqi7tb51umq69));
                GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(12));
                gm_b16_8 t40 = t39;
                t26 = t40;
ix27:;
                gm_l8 = t26;
            }
            {
                void * t41 = gm_l0;
                gm_tttl9nsdo1iu6 t42;
                gm_b16_8 t43 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t42 + 0, t43);
                uint64_t t44 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t45 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t44 >= (uintptr_t)(t45)) gm_trap_index(t44, t45, gm_srcmeaag4q4qrjl, 561, 87);
                gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t44 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t42 + 16, t46);
                uint64_t t47 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                if (t47 >= (uintptr_t)(t48)) gm_trap_index(t47, t48, gm_srcmeaag4q4qrjl, 561, 96);
                gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t47 * 16));
                GM_ST(gm_b16_8, (uint8_t *)&t42 + 32, t49);
                uint64_t t50 = gm_l7;
                GM_ST(uint64_t, (uint8_t *)&t42 + 48, t50);
                gm_b0_1 t51 = gm_f_linef_0v7c424((void *)NULL, cx, NULL, t41, &t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t52 = gm_l0;
                gm_b16_8 t53 = gm_f_unit_8ekedsl((void *)NULL, cx, NULL, t52);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l9 = t53;
            }
            {
                void * t54 = gm_l0;
                gm_b16_8 t55 = gm_l9;
                gm_b0_1 t56 = gm_f_calls_native_set_iq1d80t((void *)NULL, cx, NULL, t54, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)), t55);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix9;
ie9:;
            {
                uint32_t t58 = gm_l2;
                uint32_t t59 = t58;
                uint32_t t62 = GM_LD(uint32_t, ((uint8_t *)&t59));
                if (!(t62 == ((uint32_t)0ULL))) goto mn61;
                {
                    void * t64 = gm_l0;
                    gm_b16_8 t65;
                    GM_ST(void *, (uint8_t *)&t65, (void *)(gm_s9go9vvkpcjpd));
                    GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(47));
                    gm_b0_1 t66 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t64, t65);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t67 = gm_l0;
                    gm_t88rcucg1b7uk t68 = gm_l5;
                    gm_b16_8 t69;
                    GM_ST(void *, (uint8_t *)&t69, (void *)(gm_sp5b2hbslij9l));
                    GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)(4));
                    gm_b16_8 t70;
                    GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sb6j3egm9oljk));
                    GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(4));
                    gm_b16_8 t71 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t67, t68, t69, t70);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l10 = t71;
                }
                {
                    void * t72 = gm_l0;
                    gm_b16_8 t73 = gm_l10;
                    gm_b0_1 t74 = gm_f_calls_native_set_iq1d80t((void *)NULL, cx, NULL, t72, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l4)), t73);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto me60;
mn61:;
                uint32_t t76 = GM_LD(uint32_t, ((uint8_t *)&t59));
                if (!(t76 == ((uint32_t)1ULL))) goto mn75;
                {
                    void * t78 = gm_l0;
                    gm_b16_8 t79;
                    GM_ST(void *, (uint8_t *)&t79, (void *)(gm_su2c9m69n1vjb));
                    GM_ST(uintptr_t, (uint8_t *)&t79 + 8, (uintptr_t)(30));
                    gm_b0_1 t80 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t78, t79);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t81 = gm_l0;
                    gm_tftksr4op114e t82;
                    uint64_t t83 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t84 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t83 >= (uintptr_t)(t84)) gm_trap_index(t83, t84, gm_srcmeaag4q4qrjl, 573, 123);
                    gm_b16_8 t85 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t83 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t82 + 0, t85);
                    uint64_t t86 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t86 >= (uintptr_t)(t87)) gm_trap_index(t86, t87, gm_srcmeaag4q4qrjl, 573, 132);
                    gm_b16_8 t88 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t86 * 16));
                    GM_ST(gm_b16_8, (uint8_t *)&t82 + 16, t88);
                    uint64_t t89 = gm_l7;
                    GM_ST(uint64_t, (uint8_t *)&t82 + 32, t89);
                    gm_b0_1 t90 = gm_f_linef_3fol08t((void *)NULL, cx, NULL, t81, &t82);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t91 = gm_l0;
                    void * t92 = gm_l0;
                    void * t93 = GM_LD(void *, (((uint8_t *)t92) + 0));
                    void * t94 = (void *)(((uint8_t *)t93) + 8);
                    gm_t88rcucg1b7uk t95 = gm_f_string_ty_kk94re3((void *)NULL, cx, NULL, t94);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_b16_8 t96;
                    GM_ST(void *, (uint8_t *)&t96, (void *)(gm_sc4r4l9k6f577));
                    GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(2));
                    gm_b16_8 t97;
                    GM_ST(void *, (uint8_t *)&t97, (void *)(gm_s3jl3afv8nlvv));
                    GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(3));
                    gm_b16_8 t98 = gm_f_make_slice_sa50efe((void *)NULL, cx, NULL, t91, t95, t96, t97);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l11 = t98;
                }
                {
                    void * t99 = gm_l0;
                    gm_t88rcucg1b7uk t100 = gm_l5;
                    gm_b16_8 t101 = gm_l11;
                    gm_b16_8 t102 = gm_f_make_some_9aqq928((void *)NULL, cx, NULL, t99, t100, t101);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l12 = t102;
                }
                {
                    void * t103 = gm_l0;
                    gm_t88rcucg1b7uk t104 = gm_l5;
                    gm_b16_8 t105 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t103, t104);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l13 = t105;
                }
                {
                    void * t106 = gm_l0;
                    gm_th2lqbo77pvsc t107 = gm_l4;
                    gm_t6j6smj7qoctu t108;
                    gm_b16_8 t109 = gm_l12;
                    GM_ST(gm_b16_8, (uint8_t *)&t108 + 0, t109);
                    gm_b16_8 t110 = gm_l13;
                    GM_ST(gm_b16_8, (uint8_t *)&t108 + 16, t110);
                    gm_tabb710d9mq2e t111;
                    gm_f_format_g55p0hj((void *)&t111, (void *)NULL, cx, NULL, &t108);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t112 = t111;
                    if (((((uint8_t *)&t112))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t112) + 8, gm_srcmeaag4q4qrjl, 577, 42);
                    gm_b16_8 t113 = GM_LD(gm_b16_8, (((uint8_t *)&t112) + 8));
                    gm_b16_8 t114 = t113;
                    gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
                    gm_b0_1 t116 = gm_f_calls_native_set_iq1d80t((void *)NULL, cx, NULL, t106, &t107, t115);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto me60;
mn75:;
                {
                    void * t119 = gm_l0;
                    uint64_t t120 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t121 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    if (t120 >= (uintptr_t)(t121)) gm_trap_index(t120, t121, gm_srcmeaag4q4qrjl, 580, 56);
                    gm_b16_8 t122 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t120 * 16));
                    gm_tnpifv20irnph t123;
                    GM_ST(gm_b16_8, ((uint8_t *)&t123 + 0), t122);
                    gm_b0_1 t124 = gm_f_linef_i03em5a((void *)NULL, cx, NULL, t119, t123);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t125 = gm_l0;
                    gm_t9qkh0rsooc8d t126;
                    GM_ST(uint8_t, ((uint8_t *)&t126 + 0), ((uint8_t)3ULL));
                    gm_b16_8 t127;
                    GM_ST(void *, (uint8_t *)&t127, (void *)(gm_s04qus0luoga5));
                    GM_ST(uintptr_t, (uint8_t *)&t127 + 8, (uintptr_t)(4));
                    gm_b16_8 t128;
                    GM_ST(void *, (uint8_t *)&t128, (void *)(gm_sicgmpca63jln));
                    GM_ST(uintptr_t, (uint8_t *)&t128 + 8, (uintptr_t)(1));
                    gm_b16_8 t129;
                    GM_ST(void *, (uint8_t *)&t129, (void *)(gm_svubtddeidu1d));
                    GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(1));
                    gm_b0_1 t130 = gm_f_start_exit_m57dnn5((void *)NULL, cx, NULL, t125, t126, t127, t128, t129);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto me60;
mn117:;
                gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 565, 9);
me60:;
            }
ix9:;
        }
    }
    gm_b0_1 t131;
    gm_tqgtd5gtgj65s t132;
    memset(&t132, 0, sizeof t132);
    ((uint8_t *)&t132)[0] = 0;
    GM_ST(gm_b0_1, ((uint8_t *)&t132 + 8), t131);
    gm_tqgtd5gtgj65s t133 = t132;
    F.ret = t133;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_out_call_uj833o2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_out_stub_ils4a69(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, uint32_t a3, gm_b16_8 a4, gm_t88rcucg1b7uk a5) {
    struct gm_fr_out_stub_ils4a69 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* effect_ */
    uint32_t gm_l2; /* id */
    uint32_t gm_l3; /* op */
    gm_b16_8 gm_l4; /* tys */
    gm_t88rcucg1b7uk gm_l5; /* ret */
    gm_tk8ebsa8coi4p gm_l6; /* amb */
    gm_b16_8 gm_l7; /* writer */
    gm_b16_8 gm_l8; /* fp */
    gm_b16_8 gm_l9; /* pt */
    uint64_t gm_l10; /* p */
    gm_b16_8 gm_l11; /* name */
    gm_b16_8 gm_l12; /* key */
    gm_b16_8 gm_l13; /* f */
    gm_b16_8 gm_l14; /* f */
    gm_b16_8 gm_l15; /* sig */
    GM_STACK_CHECK(&F, 1714u);
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
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        gm_tk8ebsa8coi4p t5 = GM_LD(gm_tk8ebsa8coi4p, (((uint8_t *)t4) + 48));
        gm_l6 = t5;
    }
    {
        gm_b16_8 t6;
        GM_ST(void *, (uint8_t *)&t6, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(0));
        gm_l7 = t6;
    }
    {
        gm_t9805qtonbsag t8 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l6) + 0));
        void * t9 = gm_l1;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)t9) + 0));
        gm_t9805qtonbsag t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[4] = 1;
        gm_t9805qtonbsag t12 = t8;
        gm_t9805qtonbsag t13 = t11;
        uint8_t t14 = (gm_eqkricceou4u(((uint8_t *)&t12), ((uint8_t *)&t13)));
        if (!t14) goto ie7;
        {
            gm_b16_8 t16;
            GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s9u3cqhrfmcru));
            GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(12));
            gm_l7 = t16;
        }
        goto ix7;
ie7:;
        gm_t9805qtonbsag t18 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l6) + 8));
        void * t19 = gm_l1;
        uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)t19) + 0));
        gm_t9805qtonbsag t21;
        GM_ST(uint8_t, (uint8_t *)&t21 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t21 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t21), t20);
        ((uint8_t *)&t21)[4] = 1;
        gm_t9805qtonbsag t22 = t18;
        gm_t9805qtonbsag t23 = t21;
        uint8_t t24 = (gm_eqkricceou4u(((uint8_t *)&t22), ((uint8_t *)&t23)));
        if (!t24) goto ie17;
        {
            gm_b16_8 t26;
            GM_ST(void *, (uint8_t *)&t26, (void *)(gm_sqi7tb51umq69));
            GM_ST(uintptr_t, (uint8_t *)&t26 + 8, (uintptr_t)(12));
            gm_l7 = t26;
        }
        goto ix17;
ie17:;
        {
            gm_th2lqbo77pvsc t28;
            memset(&t28, 0, sizeof t28);
            uint8_t t29 = 0;
            F.ret = t28;
            goto gm_fret;
        }
ix17:;
ix7:;
    }
    {
        uint8_t t31 = 0;
        uint8_t t32 = 0;
        uint8_t t33 = 0;
        void * t34 = gm_l0;
        void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
        void * t36 = (void *)(((uint8_t *)t35) + 8);
        gm_t88rcucg1b7uk t37 = gm_l5;
        uint8_t t38 = gm_f_is_unit_17uennd(t36, GM_CONV(gm_csguankko4lmhd, t37));
        uint8_t t39 = (uint8_t)!t38;
        if (!t39) {
            uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            uint8_t t41 = (!(t40 == ((uint64_t)1ULL)));
            t33 = t41;
        } else t33 = 1;
        if (!t33) {
            void * t42 = gm_l0;
            void * t43 = GM_LD(void *, (((uint8_t *)t42) + 0));
            void * t44 = (void *)(((uint8_t *)t43) + 8);
            uint64_t t45 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t45 >= (uintptr_t)(t46)) gm_trap_index(t45, t46, gm_srcmeaag4q4qrjl, 454, 71);
            gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t45 * 4));
            uint8_t t48 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t44, t47);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t32 = t48;
        } else t32 = 1;
        if (!t32) {
            void * t49 = gm_l0;
            void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
            void * t51 = GM_LD(void *, (((uint8_t *)t50) + 0));
            void * t52 = gm_l1;
            uint32_t t53 = GM_LD(uint32_t, (((uint8_t *)t52) + 0));
            uint32_t t54 = gm_l3;
            uint8_t t55 = gm_f_is_ctl_oclllo3(t51, t53, t54);
            t31 = t55;
        } else t31 = 1;
        if (!t31) goto ie30;
        {
            gm_th2lqbo77pvsc t57;
            memset(&t57, 0, sizeof t57);
            uint8_t t58 = 0;
            F.ret = t57;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t59 = gm_l0;
        gm_t88rcucg1b7uk t60 = gm_l5;
        gm_b16_8 t61 = gm_l4;
        gm_b16_8 t62 = gm_f_fn_ptr_type_f9efs3c((void *)NULL, cx, NULL, t59, t60, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t62;
    }
    {
        void * t63 = gm_l0;
        void * t64 = GM_LD(void *, (((uint8_t *)t63) + 0));
        void * t65 = (void *)(((uint8_t *)t64) + 8);
        uint64_t t66 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        if (t66 >= (uintptr_t)(t67)) gm_trap_index(t66, t67, gm_srcmeaag4q4qrjl, 458, 29);
        gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t66 * 4));
        gm_b16_8 t69 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t65, t68);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t69;
    }
    {
        void * t70 = gm_l0;
        uint64_t t71 = gm_f_ptr_bytes_eocneng(t70);
        gm_l10 = t71;
    }
    {
        void * t72 = gm_l0;
        void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
        void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
        void * t75 = GM_LD(void *, (((uint8_t *)t74) + 0));
        void * t76 = (void *)(((uint8_t *)t75) + 8);
        void * t77 = gm_l1;
        uint32_t t78 = GM_LD(uint32_t, (((uint8_t *)t77) + 0));
        gm_b16_8 t79 = gm_f_effect_name_prpp0ip(t76, t78);
        gm_l11 = t79;
    }
    {
        gm_tgat3d8836orb t80;
        uint32_t t81 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t80 + 0, t81);
        uint32_t t82 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&t80 + 4, t82);
        gm_b16_8 t83 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t80 + 8, t83);
        gm_b16_8 t84 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t80 + 24, t84);
        gm_tabb710d9mq2e t85;
        gm_f_format_nivudut((void *)&t85, (void *)NULL, cx, NULL, &t80);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t86 = t85;
        if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 8, gm_srcmeaag4q4qrjl, 461, 12);
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&t86) + 8));
        gm_l12 = t87;
    }
    {
        void * t89 = gm_l0;
        void * t90 = GM_LD(void *, (((uint8_t *)t89) + 0));
        void * t91 = (void *)(((uint8_t *)t90) + 1216);
        gm_b16_8 t92 = gm_l12;
        gm_b16_8 t93 = t92;
        gm_b16_8 t94 = GM_LD(gm_b16_8, ((uint8_t *)&t93));
        gm_th2lqbo77pvsc t95;
        gm_f_get_e3joqcc((void *)&t95, t91, t94);
        gm_th2lqbo77pvsc t96 = t95;
        if (!((((uint8_t *)&t96))[16] != 0)) goto ie88;
        gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
        gm_l13 = t97;
        {
            gm_b16_8 t99 = gm_l13;
            gm_th2lqbo77pvsc t100;
            GM_ST(uint8_t, (uint8_t *)&t100 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t100 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t100 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t100), t99);
            ((uint8_t *)&t100)[16] = 1;
            uint8_t t101 = 0;
            F.ret = t100;
            goto gm_fret;
        }
        goto ix88;
ie88:;
ix88:;
    }
    {
        void * t102 = gm_l0;
        void * t103 = GM_LD(void *, (((uint8_t *)t102) + 0));
        void * t104 = (void *)(((uint8_t *)t103) + 1272);
        gm_b16_8 t105;
        GM_ST(void *, (uint8_t *)&t105, (void *)(gm_sl8of2tnl835f));
        GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(5));
        gm_b16_8 t106 = gm_l12;
        gm_b16_8 t107 = t106;
        gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
        gm_b16_8 t109 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t104, t105, t108);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l14 = t109;
    }
    {
        void * t110 = gm_l0;
        void * t111 = GM_LD(void *, (((uint8_t *)t110) + 0));
        void * t112 = (void *)(((uint8_t *)t111) + 1216);
        gm_b16_8 t113 = gm_l12;
        gm_b16_8 t114 = t113;
        gm_b16_8 t115 = GM_LD(gm_b16_8, ((uint8_t *)&t114));
        gm_b16_8 t116 = gm_l14;
        gm_tftb1odc739t5 t117 = gm_f_put_7shfu9u(t112, t115, t116);
        gm_tftb1odc739t5 t118 = t117;
        if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_srcmeaag4q4qrjl, 464, 5);
        gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
    }
    {
        gm_t6j6smj7qoctu t120;
        gm_b16_8 t121 = gm_l14;
        GM_ST(gm_b16_8, (uint8_t *)&t120 + 0, t121);
        gm_b16_8 t122 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t120 + 16, t122);
        gm_tabb710d9mq2e t123;
        gm_f_format_3o89hhj((void *)&t123, (void *)NULL, cx, NULL, &t120);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t124 = t123;
        if (((((uint8_t *)&t124))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t124) + 8, gm_srcmeaag4q4qrjl, 465, 12);
        gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&t124) + 8));
        gm_l15 = t125;
    }
    {
        void * t126 = gm_l0;
        void * t127 = GM_LD(void *, (((uint8_t *)t126) + 0));
        void * t128 = (void *)((((uint8_t *)t127) + 1968) + 0);
        gm_b16_8 t129 = gm_l15;
        gm_tnpifv20irnph t130;
        GM_ST(gm_b16_8, ((uint8_t *)&t130 + 0), t129);
        gm_tftb1odc739t5 t131 = gm_f_write_vp4v8ut(t128, t130);
        gm_tftb1odc739t5 t132 = t131;
        if (((((uint8_t *)&t132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t132) + 1, gm_srcmeaag4q4qrjl, 466, 5);
        gm_b0_1 t133 = GM_LD(gm_b0_1, (((uint8_t *)&t132) + 1));
    }
    {
        void * t134 = gm_l0;
        void * t135 = GM_LD(void *, (((uint8_t *)t134) + 0));
        void * t136 = (void *)((((uint8_t *)t135) + 1968) + 40);
        gm_tobu7tlrkigeq t137;
        memset((uint8_t *)&t137 + 20, 0, 4);
        memset((uint8_t *)&t137 + 84, 0, 4);
        gm_b16_8 t138 = gm_l15;
        GM_ST(gm_b16_8, (uint8_t *)&t137 + 0, t138);
        uint32_t t139 = gm_l2;
        GM_ST(uint32_t, (uint8_t *)&t137 + 16, t139);
        gm_b16_8 t140 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t137 + 24, t140);
        gm_b16_8 t141 = gm_l7;
        GM_ST(gm_b16_8, (uint8_t *)&t137 + 40, t141);
        uint64_t t142 = gm_l10;
        GM_ST(uint64_t, (uint8_t *)&t137 + 56, t142);
        gm_b16_8 t143 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t137 + 64, t143);
        uint32_t t144 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&t137 + 80, t144);
        gm_tftb1odc739t5 t145 = gm_f_write_mv65o15(t136, &t137);
        gm_tftb1odc739t5 t146 = t145;
        if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 1, gm_srcmeaag4q4qrjl, 467, 5);
        gm_b0_1 t147 = GM_LD(gm_b0_1, (((uint8_t *)&t146) + 1));
    }
    {
        gm_b16_8 t148 = gm_l14;
        gm_th2lqbo77pvsc t149;
        GM_ST(uint8_t, (uint8_t *)&t149 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t149 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t149 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t149), t148);
        ((uint8_t *)&t149)[16] = 1;
        uint8_t t150 = 0;
        F.ret = t149;
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
__attribute__((aligned(4))) void gm_f_perform_t77ghfg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint32_t a3, gm_b16_8 a4, gm_b16_8 a5, gm_t88rcucg1b7uk a6) {
    struct gm_fr_perform_t77ghfg F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* effect_ */
    uint32_t gm_l3; /* op */
    gm_b16_8 gm_l4; /* vals */
    gm_b16_8 gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* ret */
    uint32_t gm_l7; /* id */
    gm_b16_8 gm_l8; /* r */
    gm_b16_8 gm_l9; /* r */
    gm_b16_8 gm_l10; /* n */
    gm_b16_8 gm_l11; /* cx */
    gm_b16_8 gm_l12; /* loc */
    gm_b16_8 gm_l13; /* name */
    gm_th2lqbo77pvsc gm_l14; /* res */
    gm_b16_8 gm_l15; /* fp */
    gm_b16_8 gm_l16; /* args */
    gm_b16_8 gm_l17; /* out */
    gm_b16_8 gm_l18; /* r */
    gm_b16_8 gm_l19; /* clause_call */
    gm_tk8ebsa8coi4p gm_l20; /* amb */
    uint8_t gm_l21; /* native */
    gm_th2lqbo77pvsc gm_l22; /* rv */
    gm_b16_8 gm_l23; /* r */
    GM_STACK_CHECK(&F, 3070u);
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
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = gm_l2;
        uint32_t t5 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t5;
    }
    {
        void * t7 = gm_l0;
        uint32_t t8 = gm_l7;
        void * t9 = gm_l2;
        uint32_t t10 = gm_l3;
        gm_b16_8 t11 = gm_l4;
        gm_b16_8 t12 = gm_l5;
        gm_t88rcucg1b7uk t13 = gm_l6;
        gm_th2lqbo77pvsc t14;
        gm_f_direct_perform_vc33l18((void *)&t14, (void *)NULL, cx, NULL, t7, t8, t9, t10, t11, t12, t13);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t15 = t14;
        if (!((((uint8_t *)&t15))[16] != 0)) goto ie6;
        gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
        gm_l8 = t16;
        {
            gm_b16_8 t18 = gm_l8;
            gm_t5o0tpv5jka2l t19;
            GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t19 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t19 + 32, 0);
            ((uint8_t *)&t19)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t19 + 8), t18);
            gm_t5o0tpv5jka2l t20 = t19;
            uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
            F.ret = t19;
            goto gm_fret;
        }
        goto ix6;
ie6:;
ix6:;
    }
    {
        void * t23 = gm_l0;
        void * t24 = gm_l1;
        void * t25 = gm_l2;
        uint32_t t26 = gm_l7;
        uint32_t t27 = gm_l3;
        gm_b16_8 t28 = gm_l4;
        gm_b16_8 t29 = gm_l5;
        gm_t88rcucg1b7uk t30 = gm_l6;
        gm_th2lqbo77pvsc t31;
        gm_f_stub_perform_m3dbb7g((void *)&t31, (void *)NULL, cx, NULL, t23, t24, t25, t26, t27, t28, t29, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t32 = t31;
        if (!((((uint8_t *)&t32))[16] != 0)) goto ie22;
        gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
        gm_l9 = t33;
        {
            gm_b16_8 t35 = gm_l9;
            gm_t5o0tpv5jka2l t36;
            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t36 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t36 + 32, 0);
            ((uint8_t *)&t36)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t36 + 8), t35);
            gm_t5o0tpv5jka2l t37 = t36;
            uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
            F.ret = t36;
            goto gm_fret;
        }
        goto ix22;
ie22:;
ix22:;
    }
    {
        void * t39 = gm_l0;
        uint32_t t40 = gm_f_fresh_5kokjmg(t39);
        gm_t88rcucg1b7uk t41;
        GM_ST(uint32_t, ((uint8_t *)&t41 + 0), t40);
        gm_tabb710d9mq2e t42;
        gm_f_format_r9dvh2e((void *)&t42, (void *)NULL, cx, NULL, t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t43 = t42;
        if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcmeaag4q4qrjl, 398, 10);
        gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
        gm_l10 = t44;
    }
    {
        void * t45 = gm_l0;
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)t45) + 280));
        gm_l11 = t46;
    }
    {
        void * t47 = gm_l0;
        void * t48 = gm_l1;
        gm_b16_8 t49 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t47, t48);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l12 = t49;
    }
    {
        void * t50 = gm_l0;
        void * t51 = GM_LD(void *, (((uint8_t *)t50) + 0));
        void * t52 = GM_LD(void *, (((uint8_t *)t51) + 0));
        void * t53 = GM_LD(void *, (((uint8_t *)t52) + 0));
        void * t54 = (void *)(((uint8_t *)t53) + 8);
        void * t55 = gm_l2;
        uint32_t t56 = GM_LD(uint32_t, (((uint8_t *)t55) + 0));
        gm_b16_8 t57 = gm_f_effect_name_prpp0ip(t54, t56);
        gm_l13 = t57;
    }
    {
        void * t58 = gm_l0;
        gm_td7tfovebirdn t59;
        memset((uint8_t *)&t59 + 36, 0, 4);
        gm_b16_8 t60 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t59 + 0, t60);
        gm_b16_8 t61 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t59 + 16, t61);
        uint32_t t62 = gm_l7;
        GM_ST(uint32_t, (uint8_t *)&t59 + 32, t62);
        gm_b0_1 t63 = gm_f_linef_gg3n7rd((void *)NULL, cx, NULL, t58, &t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t64 = gm_l0;
        gm_tfeab279c16rr t65;
        gm_b16_8 t66 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t65 + 0, t66);
        gm_b16_8 t67 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t65 + 16, t67);
        gm_b16_8 t68 = gm_l12;
        GM_ST(gm_b16_8, (uint8_t *)&t65 + 32, t68);
        gm_b0_1 t69 = gm_f_linef_qg7idri((void *)NULL, cx, NULL, t64, &t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t71 = gm_l0;
        void * t72 = GM_LD(void *, (((uint8_t *)t71) + 0));
        void * t73 = GM_LD(void *, (((uint8_t *)t72) + 0));
        void * t74 = gm_l2;
        uint32_t t75 = GM_LD(uint32_t, (((uint8_t *)t74) + 0));
        uint32_t t76 = gm_l3;
        uint8_t t77 = gm_f_is_ctl_oclllo3(t73, t75, t76);
        if (!t77) goto ie70;
        {
            void * t79 = gm_l0;
            gm_b16_8 t80 = gm_l10;
            gm_b16_8 t81 = t80;
            gm_b16_8 t82 = GM_LD(gm_b16_8, ((uint8_t *)&t81));
            uint32_t t83 = gm_l3;
            gm_b16_8 t84 = gm_l4;
            gm_b16_8 t85 = gm_l5;
            gm_t88rcucg1b7uk t86 = gm_l6;
            gm_f_ctl_perform_be6h6eo((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t79, t82, t83, t84, t85, t86);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix70;
ie70:;
ix70:;
    }
    {
        gm_th2lqbo77pvsc t87;
        memset(&t87, 0, sizeof t87);
        gm_l14 = t87;
    }
    {
        void * t89 = gm_l0;
        void * t90 = GM_LD(void *, (((uint8_t *)t89) + 0));
        void * t91 = (void *)(((uint8_t *)t90) + 8);
        gm_t88rcucg1b7uk t92 = gm_l6;
        uint8_t t93 = gm_f_is_never_in5ftma(t91, GM_CONV(gm_csguankko4lmhd, t92));
        uint8_t t94 = (uint8_t)!t93;
        if (!t94) goto ie88;
        {
            void * t96 = gm_l0;
            gm_t88rcucg1b7uk t97 = gm_l6;
            gm_b16_8 t98 = gm_f_result_var_rn01cvq((void *)NULL, cx, NULL, t96, t97);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t99;
            GM_ST(uint8_t, (uint8_t *)&t99 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t99 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t99 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t99), t98);
            ((uint8_t *)&t99)[16] = 1;
            gm_l14 = t99;
        }
        goto ix88;
ie88:;
ix88:;
    }
    {
        void * t100 = gm_l0;
        gm_t88rcucg1b7uk t101 = gm_l6;
        gm_b16_8 t102 = gm_l5;
        gm_b16_8 t103 = gm_f_fn_ptr_type_f9efs3c((void *)NULL, cx, NULL, t100, t101, t102);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l15 = t103;
    }
    {
        void * t104 = gm_l0;
        gm_b16_8 t105 = gm_l4;
        gm_b16_8 t106 = gm_l5;
        gm_b16_8 t107 = gm_f_gamma_args_jb6l3ik((void *)NULL, cx, NULL, t104, t105, t106);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t107;
    }
    {
        gm_b16_8 t108;
        GM_ST(void *, (uint8_t *)&t108, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t108 + 8, (uintptr_t)(0));
        gm_l17 = t108;
    }
    {
        if (!((((uint8_t *)&gm_l14))[16] != 0)) goto ie109;
        gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
        gm_l18 = t110;
        {
            void * t113 = gm_l0;
            void * t114 = GM_LD(void *, (((uint8_t *)t113) + 0));
            void * t115 = (void *)(((uint8_t *)t114) + 8);
            gm_t88rcucg1b7uk t116 = gm_l6;
            uint8_t t117 = gm_f_by_ref_fi0o860((void *)NULL, cx, NULL, t115, t116);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (!t117) goto ie112;
            {
                gm_b16_8 t119 = gm_l18;
                gm_tnpifv20irnph t120;
                GM_ST(gm_b16_8, ((uint8_t *)&t120 + 0), t119);
                gm_tabb710d9mq2e t121;
                gm_f_format_itcjtai((void *)&t121, (void *)NULL, cx, NULL, t120);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t122 = t121;
                if (((((uint8_t *)&t122))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t122) + 8, gm_srcmeaag4q4qrjl, 411, 40);
                gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&t122) + 8));
                gm_b16_8 t124 = t123;
                gm_b16_8 t125 = GM_LD(gm_b16_8, ((uint8_t *)&t124));
                gm_l17 = t125;
            }
            goto ix112;
ie112:;
ix112:;
        }
        goto ix109;
ie109:;
ix109:;
    }
    {
        gm_t586m754bse2v t126;
        memset((uint8_t *)&t126 + 36, 0, 4);
        gm_b16_8 t127 = gm_l15;
        GM_ST(gm_b16_8, (uint8_t *)&t126 + 0, t127);
        gm_b16_8 t128 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t126 + 16, t128);
        uint32_t t129 = gm_l3;
        GM_ST(uint32_t, (uint8_t *)&t126 + 32, t129);
        gm_b16_8 t130 = gm_l17;
        GM_ST(gm_b16_8, (uint8_t *)&t126 + 40, t130);
        gm_b16_8 t131 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t126 + 56, t131);
        gm_b16_8 t132 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t126 + 72, t132);
        gm_b16_8 t133 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t126 + 88, t133);
        gm_b16_8 t134 = gm_l16;
        GM_ST(gm_b16_8, (uint8_t *)&t126 + 104, t134);
        gm_tabb710d9mq2e t135;
        gm_f_format_18tu4v6((void *)&t135, (void *)NULL, cx, NULL, &t126);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t136 = t135;
        if (((((uint8_t *)&t136))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t136) + 8, gm_srcmeaag4q4qrjl, 413, 20);
        gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&t136) + 8));
        gm_l19 = t137;
    }
    {
        void * t138 = gm_l0;
        void * t139 = GM_LD(void *, (((uint8_t *)t138) + 0));
        void * t140 = GM_LD(void *, (((uint8_t *)t139) + 0));
        gm_tk8ebsa8coi4p t141 = GM_LD(gm_tk8ebsa8coi4p, (((uint8_t *)t140) + 48));
        gm_l20 = t141;
    }
    {
        uint8_t t142 = 0;
        uint8_t t143 = 0;
        gm_t9805qtonbsag t144 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l20) + 0));
        void * t145 = gm_l2;
        uint32_t t146 = GM_LD(uint32_t, (((uint8_t *)t145) + 0));
        gm_t9805qtonbsag t147;
        GM_ST(uint8_t, (uint8_t *)&t147 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t147 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t147), t146);
        ((uint8_t *)&t147)[4] = 1;
        gm_t9805qtonbsag t148 = t144;
        gm_t9805qtonbsag t149 = t147;
        uint8_t t150 = (gm_eqkricceou4u(((uint8_t *)&t148), ((uint8_t *)&t149)));
        if (!t150) {
            gm_t9805qtonbsag t151 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l20) + 8));
            void * t152 = gm_l2;
            uint32_t t153 = GM_LD(uint32_t, (((uint8_t *)t152) + 0));
            gm_t9805qtonbsag t154;
            GM_ST(uint8_t, (uint8_t *)&t154 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t154 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t154), t153);
            ((uint8_t *)&t154)[4] = 1;
            gm_t9805qtonbsag t155 = t151;
            gm_t9805qtonbsag t156 = t154;
            uint8_t t157 = (gm_eqkricceou4u(((uint8_t *)&t155), ((uint8_t *)&t156)));
            t143 = t157;
        } else t143 = 1;
        if (!t143) {
            gm_t9805qtonbsag t158 = GM_LD(gm_t9805qtonbsag, (((uint8_t *)&gm_l20) + 16));
            void * t159 = gm_l2;
            uint32_t t160 = GM_LD(uint32_t, (((uint8_t *)t159) + 0));
            gm_t9805qtonbsag t161;
            GM_ST(uint8_t, (uint8_t *)&t161 + 5, 0);
            GM_ST(uint16_t, (uint8_t *)&t161 + 6, 0);
            GM_ST(uint32_t, ((uint8_t *)&t161), t160);
            ((uint8_t *)&t161)[4] = 1;
            gm_t9805qtonbsag t162 = t158;
            gm_t9805qtonbsag t163 = t161;
            uint8_t t164 = (gm_eqkricceou4u(((uint8_t *)&t162), ((uint8_t *)&t163)));
            t142 = t164;
        } else t142 = 1;
        gm_l21 = t142;
    }
    {
        uint8_t t166 = gm_l21;
        if (!t166) goto ie165;
        {
            void * t168 = gm_l0;
            gm_b16_8 t169 = gm_l10;
            gm_tnpifv20irnph t170;
            GM_ST(gm_b16_8, ((uint8_t *)&t170 + 0), t169);
            gm_b0_1 t171 = gm_f_linef_ch11let((void *)NULL, cx, NULL, t168, t170);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t172 = gm_l0;
            void * t173 = gm_l0;
            uint64_t t174 = GM_LD(uint64_t, (((uint8_t *)t173) + 224));
            uint64_t r175; bool o176 = __builtin_add_overflow(t174, ((uint64_t)1ULL), &r175);
            if (o176) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 418, 9);
            uint64_t t177 = r175;
            GM_ST(uint64_t, (((uint8_t *)t172) + 224), t177);
        }
        {
            void * t178 = gm_l0;
            void * t179 = gm_l2;
            uint32_t t180 = gm_l3;
            gm_b16_8 t181 = gm_l4;
            gm_t88rcucg1b7uk t182 = gm_l6;
            gm_tqgtd5gtgj65s t183;
            gm_f_native_op_r81a4at((void *)&t183, (void *)NULL, cx, NULL, t178, t179, t180, t181, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l14)), t182);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqgtd5gtgj65s t184 = t183;
            if (((((uint8_t *)&t184))[0] != 0)) {
                gm_t08olq97rscn3 t185 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t184) + 8));
                gm_t5o0tpv5jka2l t186;
                GM_ST(uint8_t, (uint8_t *)&t186 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t186 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t186 + 4, 0);
                ((uint8_t *)&t186)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t186 + 8), t185);
                F.ret = t186;
                goto gm_fret;
            }
            gm_b0_1 t187 = GM_LD(gm_b0_1, (((uint8_t *)&t184) + 8));
        }
        {
            void * t188 = gm_l0;
            void * t189 = gm_l0;
            uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)t189) + 224));
            uint64_t r191; bool o192 = __builtin_sub_overflow(t190, ((uint64_t)1ULL), &r191);
            if (o192) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 420, 9);
            uint64_t t193 = r191;
            GM_ST(uint64_t, (((uint8_t *)t188) + 224), t193);
        }
        {
            void * t194 = gm_l0;
            gm_b16_8 t195;
            GM_ST(void *, (uint8_t *)&t195, (void *)(gm_s02uuedsru1qu));
            GM_ST(uintptr_t, (uint8_t *)&t195 + 8, (uintptr_t)(8));
            gm_b0_1 t196 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t194, t195);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t197 = gm_l0;
            void * t198 = gm_l0;
            uint64_t t199 = GM_LD(uint64_t, (((uint8_t *)t198) + 224));
            uint64_t r200; bool o201 = __builtin_add_overflow(t199, ((uint64_t)1ULL), &r200);
            if (o201) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 422, 9);
            uint64_t t202 = r200;
            GM_ST(uint64_t, (((uint8_t *)t197) + 224), t202);
        }
        goto ix165;
ie165:;
ix165:;
    }
    {
        gm_th2lqbo77pvsc t203 = gm_l14;
        gm_l22 = t203;
    }
    {
        uint8_t t205;
        if (((((uint8_t *)&gm_l22))[16] != 0)) goto mn207;
        t205 = ((uint8_t)0);
        goto me206;
mn207:;
        t205 = ((uint8_t)1);
        goto me206;
mn208:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 425, 8);
me206:;
        if (!t205) goto ie204;
        uint64_t t209 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
        uint8_t t210 = ((t209 == ((uint64_t)0ULL)));
        if (!t210) goto ie204;
        {
            void * t212 = gm_l0;
            gm_t6j6smj7qoctu t213;
            gm_th2lqbo77pvsc t214 = gm_l22;
            gm_th2lqbo77pvsc t215 = t214;
            if (!((((uint8_t *)&t215))[16] != 0)) gm_trap("unwrap of null", gm_srcmeaag4q4qrjl, 426, 30);
            gm_b16_8 t216 = GM_LD(gm_b16_8, ((uint8_t *)&t215));
            GM_ST(gm_b16_8, (uint8_t *)&t213 + 0, t216);
            gm_b16_8 t217 = gm_l19;
            GM_ST(gm_b16_8, (uint8_t *)&t213 + 16, t217);
            gm_b0_1 t218 = gm_f_linef_i5ai2tu((void *)NULL, cx, NULL, t212, &t213);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix204;
ie204:;
        {
            void * t220 = gm_l0;
            gm_b16_8 t221 = gm_l19;
            gm_tnpifv20irnph t222;
            GM_ST(gm_b16_8, ((uint8_t *)&t222 + 0), t221);
            gm_b0_1 t223 = gm_f_linef_v355qja((void *)NULL, cx, NULL, t220, t222);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
ix204:;
    }
    {
        uint8_t t225 = gm_l21;
        if (!t225) goto ie224;
        {
            void * t227 = gm_l0;
            void * t228 = gm_l0;
            uint64_t t229 = GM_LD(uint64_t, (((uint8_t *)t228) + 224));
            uint64_t r230; bool o231 = __builtin_sub_overflow(t229, ((uint64_t)1ULL), &r230);
            if (o231) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 431, 9);
            uint64_t t232 = r230;
            GM_ST(uint64_t, (((uint8_t *)t227) + 224), t232);
        }
        {
            void * t233 = gm_l0;
            gm_b16_8 t234;
            GM_ST(void *, (uint8_t *)&t234, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t234 + 8, (uintptr_t)(1));
            gm_b0_1 t235 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t233, t234);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix224;
ie224:;
ix224:;
    }
    {
        void * t236 = gm_l0;
        gm_b0_1 t237 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t236);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        if (!((((uint8_t *)&gm_l14))[16] != 0)) goto ie238;
        gm_b16_8 t239 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l14));
        gm_l23 = t239;
        {
            gm_b16_8 t241 = gm_l23;
            gm_t5o0tpv5jka2l t242;
            GM_ST(uint8_t, (uint8_t *)&t242 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t242 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t242 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t242 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t242 + 32, 0);
            ((uint8_t *)&t242)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t242 + 8), t241);
            gm_t5o0tpv5jka2l t243 = t242;
            uint8_t t244 = ((((uint8_t *)&t243))[0] != 0);
            F.ret = t242;
            goto gm_fret;
        }
        goto ix238;
ie238:;
ix238:;
    }
    {
        void * t245 = gm_l0;
        gm_t88rcucg1b7uk t246 = gm_l6;
        gm_b16_8 t247 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t245, t246);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t248;
        GM_ST(uint8_t, (uint8_t *)&t248 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t248 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t248 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t248 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t248 + 32, 0);
        ((uint8_t *)&t248)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t248 + 8), t247);
        gm_t5o0tpv5jka2l t249 = t248;
        uint8_t t250 = ((((uint8_t *)&t249))[0] != 0);
        F.ret = t248;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_piece_stub_m8mb7rc(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, uint8_t a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_piece_stub_m8mb7rc F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* core */
    uint8_t gm_l2; /* kind */
    gm_t88rcucg1b7uk gm_l3; /* str_ty */
    gm_b16_8 gm_l4; /* kind_text */
    gm_b16_8 gm_l5; /* key */
    gm_b16_8 gm_l6; /* f */
    gm_b16_8 gm_l7; /* f */
    gm_b16_8 gm_l8; /* pt */
    uint64_t gm_l9; /* p */
    gm_b16_8 gm_l10; /* loc */
    gm_b16_8 gm_l11; /* head */
    gm_b16_8 gm_l12; /* pre */
    gm_b16_8 gm_l13; /* data */
    gm_b16_8 gm_l14; /* len */
    gm_b16_8 gm_l15; /* call */
    gm_b16_8 gm_l16; /* post */
    GM_STACK_CHECK(&F, 1773u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_b16_8 t2;
        uint8_t t4 = gm_l2;
        uint8_t t5 = ((t4 == ((uint8_t)115ULL)));
        if (!t5) goto ie3;
        gm_b16_8 t7;
        GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s03pmf867dcsr));
        GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
        gm_b16_8 t8 = t7;
        t2 = t8;
        goto ix3;
ie3:;
        gm_b16_8 t10;
        GM_ST(void *, (uint8_t *)&t10, (void *)(gm_slq58vfha8k9h));
        GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(1));
        gm_b16_8 t11 = t10;
        t2 = t11;
ix3:;
        gm_l4 = t2;
    }
    {
        gm_t6j6smj7qoctu t12;
        gm_b16_8 t13 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t12 + 0, t13);
        gm_b16_8 t14 = gm_l4;
        GM_ST(gm_b16_8, (uint8_t *)&t12 + 16, t14);
        gm_tabb710d9mq2e t15;
        gm_f_format_jp1qc7o((void *)&t15, (void *)NULL, cx, NULL, &t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t16 = t15;
        if (((((uint8_t *)&t16))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t16) + 8, gm_srcmeaag4q4qrjl, 510, 12);
        gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)&t16) + 8));
        gm_l5 = t17;
    }
    {
        void * t19 = gm_l0;
        void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
        void * t21 = (void *)(((uint8_t *)t20) + 1216);
        gm_b16_8 t22 = gm_l5;
        gm_b16_8 t23 = t22;
        gm_b16_8 t24 = GM_LD(gm_b16_8, ((uint8_t *)&t23));
        gm_th2lqbo77pvsc t25;
        gm_f_get_e3joqcc((void *)&t25, t21, t24);
        gm_th2lqbo77pvsc t26 = t25;
        if (!((((uint8_t *)&t26))[16] != 0)) goto ie18;
        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)&t26));
        gm_l6 = t27;
        {
            gm_b16_8 t29 = gm_l6;
            uint8_t t30 = 0;
            F.ret = t29;
            goto gm_fret;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t31 = gm_l0;
        void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
        void * t33 = (void *)(((uint8_t *)t32) + 1272);
        gm_b16_8 t34;
        GM_ST(void *, (uint8_t *)&t34, (void *)(gm_sl8of2tnl835f));
        GM_ST(uintptr_t, (uint8_t *)&t34 + 8, (uintptr_t)(5));
        gm_b16_8 t35 = gm_l5;
        gm_b16_8 t36 = t35;
        gm_b16_8 t37 = GM_LD(gm_b16_8, ((uint8_t *)&t36));
        gm_b16_8 t38 = gm_f_keyed_h72lrop((void *)NULL, cx, NULL, t33, t34, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t38;
    }
    {
        void * t39 = gm_l0;
        void * t40 = GM_LD(void *, (((uint8_t *)t39) + 0));
        void * t41 = (void *)(((uint8_t *)t40) + 1216);
        gm_b16_8 t42 = gm_l5;
        gm_b16_8 t43 = t42;
        gm_b16_8 t44 = GM_LD(gm_b16_8, ((uint8_t *)&t43));
        gm_b16_8 t45 = gm_l7;
        gm_tftb1odc739t5 t46 = gm_f_put_7shfu9u(t41, t44, t45);
        gm_tftb1odc739t5 t47 = t46;
        if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srcmeaag4q4qrjl, 513, 5);
        gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
    }
    {
        void * t49 = gm_l0;
        void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
        void * t51 = (void *)(((uint8_t *)t50) + 8);
        gm_t88rcucg1b7uk t52 = gm_l3;
        gm_b16_8 t53 = gm_f_param_cty_e8tvhrm((void *)NULL, cx, NULL, t51, t52);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t53;
    }
    {
        void * t54 = gm_l0;
        uint64_t t55 = gm_f_ptr_bytes_eocneng(t54);
        gm_l9 = t55;
    }
    {
        gm_b16_8 t56;
        GM_ST(void *, (uint8_t *)&t56, (void *)(gm_st6to3kuud2ge));
        GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(45));
        gm_l10 = t56;
    }
    {
        gm_b16_8 t57;
        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(0));
        gm_l11 = t57;
    }
    {
        uint8_t t59 = gm_l2;
        uint8_t t60 = ((t59 == ((uint8_t)115ULL)));
        if (!t60) goto ie58;
        {
            gm_t6j6smj7qoctu t62;
            gm_b16_8 t63 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 0, t63);
            gm_b16_8 t64 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t62 + 16, t64);
            gm_tabb710d9mq2e t65;
            gm_f_format_ru16275((void *)&t65, (void *)NULL, cx, NULL, &t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t66 = t65;
            if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_srcmeaag4q4qrjl, 519, 16);
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&t66) + 8));
            gm_b16_8 t68 = t67;
            gm_b16_8 t69 = GM_LD(gm_b16_8, ((uint8_t *)&t68));
            gm_l11 = t69;
        }
        goto ix58;
ie58:;
        {
            gm_t6j6smj7qoctu t71;
            gm_b16_8 t72 = gm_l7;
            GM_ST(gm_b16_8, (uint8_t *)&t71 + 0, t72);
            gm_b16_8 t73 = gm_l10;
            GM_ST(gm_b16_8, (uint8_t *)&t71 + 16, t73);
            gm_tabb710d9mq2e t74;
            gm_f_format_37aah2s((void *)&t74, (void *)NULL, cx, NULL, &t71);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t75 = t74;
            if (((((uint8_t *)&t75))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t75) + 8, gm_srcmeaag4q4qrjl, 521, 16);
            gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&t75) + 8));
            gm_b16_8 t77 = t76;
            gm_b16_8 t78 = GM_LD(gm_b16_8, ((uint8_t *)&t77));
            gm_l11 = t78;
        }
ix58:;
    }
    {
        void * t79 = gm_l0;
        void * t80 = GM_LD(void *, (((uint8_t *)t79) + 0));
        void * t81 = (void *)((((uint8_t *)t80) + 1968) + 0);
        gm_b16_8 t82 = gm_l11;
        gm_tnpifv20irnph t83;
        GM_ST(gm_b16_8, ((uint8_t *)&t83 + 0), t82);
        gm_tftb1odc739t5 t84 = gm_f_write_scdecr6(t81, t83);
        gm_tftb1odc739t5 t85 = t84;
        if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 1, gm_srcmeaag4q4qrjl, 523, 5);
        gm_b0_1 t86 = GM_LD(gm_b0_1, (((uint8_t *)&t85) + 1));
    }
    {
        gm_b16_8 t87;
        GM_ST(void *, (uint8_t *)&t87, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(0));
        gm_l12 = t87;
    }
    {
        gm_b16_8 t88;
        GM_ST(void *, (uint8_t *)&t88, (void *)(gm_stafskn6bv24f));
        GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(1));
        gm_l13 = t88;
    }
    {
        gm_b16_8 t89;
        GM_ST(void *, (uint8_t *)&t89, (void *)(gm_smnfu5n5pum1l));
        GM_ST(uintptr_t, (uint8_t *)&t89 + 8, (uintptr_t)(1));
        gm_l14 = t89;
    }
    {
        gm_b16_8 t90;
        GM_ST(void *, (uint8_t *)&t90, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(0));
        gm_l15 = t90;
    }
    {
        gm_b16_8 t91;
        GM_ST(void *, (uint8_t *)&t91, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(0));
        gm_l16 = t91;
    }
    {
        uint8_t t93 = gm_l2;
        uint8_t t94 = (!(t93 == ((uint8_t)115ULL)));
        if (!t94) goto ie92;
        {
            gm_b16_8 t96;
            GM_ST(void *, (uint8_t *)&t96, (void *)(gm_siihmb4jo3dsn));
            GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(124));
            gm_l12 = t96;
        }
        {
            gm_b16_8 t97;
            GM_ST(void *, (uint8_t *)&t97, (void *)(gm_s8o6lm1cn4eo5));
            GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(2));
            gm_l13 = t97;
        }
        {
            gm_b16_8 t98;
            GM_ST(void *, (uint8_t *)&t98, (void *)(gm_s579fb7l5l6df));
            GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(2));
            gm_l14 = t98;
        }
        {
            gm_b16_8 t99;
            GM_ST(void *, (uint8_t *)&t99, (void *)(gm_s3vqae7u5ljtm));
            GM_ST(uintptr_t, (uint8_t *)&t99 + 8, (uintptr_t)(13));
            gm_l15 = t99;
        }
        {
            gm_b16_8 t100;
            GM_ST(void *, (uint8_t *)&t100, (void *)(gm_spsh7mv5lihcc));
            GM_ST(uintptr_t, (uint8_t *)&t100 + 8, (uintptr_t)(33));
            gm_l16 = t100;
        }
        goto ix92;
ie92:;
ix92:;
    }
    {
        void * t101 = gm_l0;
        void * t102 = GM_LD(void *, (((uint8_t *)t101) + 0));
        void * t103 = (void *)((((uint8_t *)t102) + 1968) + 40);
        gm_tn96msocmj81h t104;
        gm_b16_8 t105 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 0, t105);
        gm_b16_8 t106 = gm_l12;
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 16, t106);
        gm_b16_8 t107 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 32, t107);
        gm_b16_8 t108 = gm_l13;
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 48, t108);
        uint64_t t109 = gm_l9;
        GM_ST(uint64_t, (uint8_t *)&t104 + 64, t109);
        gm_b16_8 t110 = gm_l14;
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 72, t110);
        gm_b16_8 t111 = gm_l15;
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 88, t111);
        gm_b16_8 t112 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 104, t112);
        gm_b16_8 t113 = gm_l16;
        GM_ST(gm_b16_8, (uint8_t *)&t104 + 120, t113);
        gm_tftb1odc739t5 t114 = gm_f_write_otf2npe(t103, &t104);
        gm_tftb1odc739t5 t115 = t114;
        if (((((uint8_t *)&t115))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t115) + 1, gm_srcmeaag4q4qrjl, 537, 5);
        gm_b0_1 t116 = GM_LD(gm_b0_1, (((uint8_t *)&t115) + 1));
    }
    {
        gm_b16_8 t117 = gm_l7;
        uint8_t t118 = 0;
        F.ret = t117;
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
inline __attribute__((aligned(4))) void gm_f_plain_args_o7acjm5(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_plain_args_o7acjm5 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* vals */
    gm_b16_8 gm_l2; /* tys */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    GM_STACK_CHECK(&F, 249u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_epvducg((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            uint64_t t5 = (t3 < t4 ? t3 : t4);
            gm_l4 = t5;
        }
        {
            for (;;) {
                uint64_t t7 = gm_l5;
                uint64_t t8 = gm_l4;
                uint8_t t9 = (t7 < t8);
                if (!t9) goto lb6;
                {
                    uint64_t t12 = gm_l5;
                    uint64_t t13 = t12;
                    uint64_t t16 = GM_LD(uint64_t, ((uint8_t *)&t13));
                    gm_l6 = t16;
                    {
                        void * t18 = (void *)((uint8_t *)&F.l3);
                        void * t19 = gm_l0;
                        uint64_t t20 = gm_l6;
                        uint64_t t21 = (uintptr_t)t20;
                        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t21 >= (uintptr_t)(t22)) gm_trap_index(t21, t22, gm_srcmeaag4q4qrjl, 184, 65);
                        gm_t88rcucg1b7uk t23 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t21 * 4));
                        uint64_t t24 = gm_l6;
                        uint64_t t25 = (uintptr_t)t24;
                        uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t25 >= (uintptr_t)(t26)) gm_trap_index(t25, t26, gm_srcmeaag4q4qrjl, 184, 73);
                        gm_b16_8 t27 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t25 * 16));
                        gm_b16_8 t28 = gm_f_plain_value_e380k1u((void *)NULL, cx, NULL, t19, t23, t27);
                        if (gm_unw.kind) {
                            goto gm_ll10;
                        }
                        gm_tftb1odc739t5 t29 = gm_f_push_3ef746b(t18, t28);
                        gm_tftb1odc739t5 t30 = t29;
                        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcmeaag4q4qrjl, 184, 41);
                        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                    }
                    goto me14;
mn15:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 184, 5);
me14:;
                }
lc6:;
                {
                    uint64_t t33 = gm_l5;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l5 = t36;
                }
            }
gm_ll10:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb6; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc6; }
            }
            goto gm_lad;
lb6:;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_plain_value_e380k1u(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_plain_value_e380k1u F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* v */
    gm_b16_8 gm_l3; /* m */
    GM_STACK_CHECK(&F, 346u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_th2lqbo77pvsc t7;
        gm_f_mirror_lcjegsf((void *)&t7, (void *)NULL, cx, NULL, t5, t6);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t8 = t7;
        if (!((((uint8_t *)&t8))[16] != 0)) goto ie2;
        gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
        gm_l3 = t9;
        {
            gm_t6j6smj7qoctu t11;
            gm_b16_8 t12 = gm_l3;
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 0, t12);
            gm_b16_8 t13 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t11 + 16, t13);
            gm_tabb710d9mq2e t14;
            gm_f_format_3s8s1g6((void *)&t14, (void *)NULL, cx, NULL, &t11);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t15 = t14;
            if (((((uint8_t *)&t15))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t15) + 8, gm_srcmeaag4q4qrjl, 171, 40);
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t15) + 8));
            gm_b16_8 t17 = t16;
            gm_b16_8 t18 = GM_LD(gm_b16_8, ((uint8_t *)&t17));
            uint8_t t19 = 0;
            F.ret = t18;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_b16_8 t20 = gm_l2;
        uint8_t t21 = 0;
        F.ret = t20;
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
inline __attribute__((aligned(4))) uint8_t gm_f_ptr_type_plain_sanetd4(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_ptr_type_plain_sanetd4 F;
    void * gm_l0; /* fg */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_t88rcucg1b7uk gm_l3; /* inner */
    GM_STACK_CHECK(&F, 254u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_t88rcucg1b7uk t5 = gm_l1;
        gm_t53at80pfdus3 t6;
        gm_f_kind_ghvav22((void *)&t6, t4, GM_CONV(gm_csguankko4lmhd, t5));
        gm_t53at80pfdus3 t7 = t6;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma11; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma11; case ((uint8_t)5ULL): goto ma11; case ((uint8_t)6ULL): goto ma11; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma11; case ((uint8_t)15ULL): goto ma11; case ((uint8_t)16ULL): goto ma9; case ((uint8_t)17ULL): goto ma11; case ((uint8_t)18ULL): goto ma11; case ((uint8_t)19ULL): goto ma11; case ((uint8_t)20ULL): goto ma11; case ((uint8_t)21ULL): goto ma11; case ((uint8_t)22ULL): goto ma11; case ((uint8_t)23ULL): goto ma11; case ((uint8_t)24ULL): goto ma11; case ((uint8_t)25ULL): goto ma11; case ((uint8_t)26ULL): goto ma11; default: goto ma11; }
ma9:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t15 != ((uint8_t)16ULL)) goto mn14;
        gm_tddtjfggdat0d t16 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t7) + 8));
        F.l2 = t16;
        void * t17 = gm_l0;
        void * t18 = GM_LD(void *, (((uint8_t *)t17) + 0));
        void * t19 = (void *)(((uint8_t *)&F.l2) + 24);
        uint8_t t20 = gm_f_within_sys_2lc2ugq(t18, t19);
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
        goto me8;
mn14:;
ma10:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t7) + 0));
        if (t23 != ((uint8_t)13ULL)) goto mn22;
        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t7) + 8));
        gm_l3 = t24;
        void * t25 = gm_l0;
        gm_t88rcucg1b7uk t26 = gm_l3;
        uint8_t t27 = gm_f_ptr_type_plain_sanetd4((void *)NULL, cx, NULL, t25, t26);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t28 = 0;
        F.ret = t27;
        goto gm_fret;
        goto me8;
mn22:;
ma11:;
        uint8_t t30 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me8;
mn29:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 327, 5);
me8:;
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
extern __attribute__((aligned(4))) gm_b16_8 gm_f_result_var_rn01cvq(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_str_equal_at_mshane1(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_str_equal_at_mshane1 F;
    void * gm_l0; /* fg */
    gm_b16_8 gm_l1; /* addr */
    gm_b16_8 gm_l2; /* text */
    gm_b16_8 gm_l3; /* len */
    GM_STACK_CHECK(&F, 523u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_tgbgti82mf879 t2;
        gm_b16_8 t3 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t3);
        void * t4 = gm_l0;
        uint64_t t5 = gm_f_ptr_bytes_eocneng(t4);
        GM_ST(uint64_t, (uint8_t *)&t2 + 16, t5);
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        GM_ST(uint64_t, (uint8_t *)&t2 + 24, t6);
        gm_tabb710d9mq2e t7;
        gm_f_format_b2nifvt((void *)&t7, (void *)NULL, cx, NULL, &t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t8 = t7;
        if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_srcmeaag4q4qrjl, 762, 12);
        gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
        gm_l3 = t9;
    }
    {
        uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t12 = ((t11 == ((uint64_t)0ULL)));
        if (!t12) goto ie10;
        {
            gm_b16_8 t14 = gm_l3;
            gm_b16_8 t15 = t14;
            gm_b16_8 t16 = GM_LD(gm_b16_8, ((uint8_t *)&t15));
            uint8_t t17 = 0;
            F.ret = t16;
            goto gm_fret;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        gm_tttl9nsdo1iu6 t18;
        gm_b16_8 t19 = gm_l3;
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 0, t19);
        gm_b16_8 t20 = gm_l1;
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 16, t20);
        void * t21 = gm_l0;
        void * t22 = GM_LD(void *, (((uint8_t *)t21) + 0));
        gm_b16_8 t23 = gm_l2;
        gm_b16_8 t24 = gm_f_static_bytes_u0uvco3((void *)NULL, cx, NULL, t22, t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_b16_8, (uint8_t *)&t18 + 32, t24);
        uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        GM_ST(uint64_t, (uint8_t *)&t18 + 48, t25);
        gm_tabb710d9mq2e t26;
        gm_f_format_7cklfe7((void *)&t26, (void *)NULL, cx, NULL, &t18);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t27 = t26;
        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 8, gm_srcmeaag4q4qrjl, 764, 12);
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&t27) + 8));
        gm_b16_8 t29 = t28;
        gm_b16_8 t30 = GM_LD(gm_b16_8, ((uint8_t *)&t29));
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
inline __attribute__((aligned(4))) void gm_f_stub_perform_m3dbb7g(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint32_t a3, uint32_t a4, gm_b16_8 a5, gm_b16_8 a6, gm_t88rcucg1b7uk a7) {
    struct gm_fr_stub_perform_m3dbb7g F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* effect_ */
    uint32_t gm_l3; /* id */
    uint32_t gm_l4; /* op */
    gm_b16_8 gm_l5; /* vals */
    gm_b16_8 gm_l6; /* tys */
    gm_t88rcucg1b7uk gm_l7; /* ret */
    gm_b16_8 gm_l8; /* f */
    gm_b16_8 gm_l9; /* cx */
    gm_b16_8 gm_l10; /* loc */
    GM_STACK_CHECK(&F, 534u);
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
    gm_l7 = a7;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l2;
        uint32_t t4 = gm_l3;
        uint32_t t5 = gm_l4;
        gm_b16_8 t6 = gm_l6;
        gm_t88rcucg1b7uk t7 = gm_l7;
        gm_th2lqbo77pvsc t8;
        gm_f_out_stub_ils4a69((void *)&t8, (void *)NULL, cx, NULL, t2, t3, t4, t5, t6, t7);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t9 = t8;
        gm_b16_8 t10;
        if (((((uint8_t *)&t9))[16] != 0)) {
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t9) + 0));
            t10 = t11;
        } else {
            gm_th2lqbo77pvsc t12;
            memset(&t12, 0, sizeof t12);
            uint8_t t13 = 0;
            F.ret = t12;
            goto gm_fret;
        }
        gm_l8 = t10;
    }
    {
        void * t14 = gm_l0;
        gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)t14) + 280));
        gm_l9 = t15;
    }
    {
        void * t16 = gm_l0;
        void * t17 = gm_l1;
        gm_b16_8 t18 = gm_f_loc_u9c0aqb((void *)NULL, cx, NULL, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t18;
    }
    {
        void * t19 = gm_l0;
        gm_t0uuhaljsf410 t20;
        gm_b16_8 t21 = gm_l8;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 0, t21);
        gm_b16_8 t22 = gm_l9;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 16, t22);
        uint64_t t23 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
        if (t23 >= (uintptr_t)(t24)) gm_trap_index(t23, t24, gm_srcmeaag4q4qrjl, 546, 40);
        gm_b16_8 t25 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t23 * 16));
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 32, t25);
        gm_b16_8 t26 = gm_l10;
        GM_ST(gm_b16_8, (uint8_t *)&t20 + 48, t26);
        gm_b0_1 t27 = gm_f_linef_icst3ps((void *)NULL, cx, NULL, t19, &t20);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t28 = gm_l0;
        gm_b0_1 t29 = gm_f_check_unwind_2hji4bl((void *)NULL, cx, NULL, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t30 = gm_l0;
        gm_t88rcucg1b7uk t31 = gm_l7;
        gm_b16_8 t32 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t30, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t33 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t33), t32);
        ((uint8_t *)&t33)[16] = 1;
        uint8_t t34 = 0;
        F.ret = t33;
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
__attribute__((aligned(4))) void gm_f_try_expr_ln1m7b8(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, gm_t9805qtonbsag a3, uint32_t a4) {
    struct gm_fr_try_expr_ln1m7b8 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    void * gm_l2; /* x */
    gm_t9805qtonbsag gm_l3; /* lift */
    uint32_t gm_l4; /* levels */
    gm_b16_8 gm_l5; /* v */
    gm_b16_8 gm_l6; /* a */
    gm_t88rcucg1b7uk gm_l7; /* ret */
    gm_b16_8 gm_l8; /* slot */
    gm_b16_8 gm_l9; /* fr */
    gm_b16_8 gm_l10; /* present */
    gm_b16_8 gm_l11; /* none */
    gm_t88rcucg1b7uk gm_l12; /* err */
    gm_b16_8 gm_l13; /* bad */
    uint64_t gm_l14; /* off */
    gm_t88rcucg1b7uk gm_l15; /* rerr */
    gm_b16_8 gm_l16; /* ev */
    uint32_t gm_l17; /* i */
    gm_b16_8 gm_l18; /* r */
    GM_STACK_CHECK(&F, 2594u);
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
        gm_l5 = t8;
    }
    {
        void * t9 = gm_l0;
        void * t10 = gm_l2;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 0));
        gm_b16_8 t12 = gm_l5;
        gm_b16_8 t13 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t9, t11, t12);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t13;
    }
    {
        void * t14 = gm_l0;
        uint32_t t15 = gm_l4;
        gm_t88rcucg1b7uk t16 = gm_f_target_ret_q7qjs0g((void *)NULL, cx, NULL, t14, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l7 = t16;
    }
    {
        void * t17 = gm_l0;
        uint32_t t18 = gm_l4;
        gm_b16_8 t19;
        GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s1k3lfare8sei));
        GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(3));
        gm_b16_8 t20 = gm_f_frame_field_nj8m22n((void *)NULL, cx, NULL, t17, t18, t19);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t20;
    }
    {
        void * t21 = gm_l0;
        uint32_t t22 = gm_l4;
        gm_tommf4j9upomc t23;
        gm_f_frame_up_g8486eu((void *)&t23, (void *)NULL, cx, NULL, t21, t22);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tommf4j9upomc t24 = t23;
        gm_l9 = GM_LD(gm_b16_8, (uint8_t *)&t24 + 0);
    }
    {
        void * t25 = gm_l0;
        void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
        void * t27 = (void *)(((uint8_t *)t26) + 8);
        void * t28 = gm_l2;
        gm_t88rcucg1b7uk t29 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t28) + 0));
        gm_t53at80pfdus3 t30;
        gm_f_kind_ghvav22((void *)&t30, t27, GM_CONV(gm_csguankko4lmhd, t29));
        gm_t53at80pfdus3 t31 = t30;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        switch (t37) { case ((uint8_t)0ULL): goto ma35; case ((uint8_t)1ULL): goto ma35; case ((uint8_t)2ULL): goto ma35; case ((uint8_t)3ULL): goto ma35; case ((uint8_t)4ULL): goto ma35; case ((uint8_t)5ULL): goto ma35; case ((uint8_t)6ULL): goto ma35; case ((uint8_t)7ULL): goto ma35; case ((uint8_t)8ULL): goto ma35; case ((uint8_t)9ULL): goto ma35; case ((uint8_t)10ULL): goto ma35; case ((uint8_t)11ULL): goto ma35; case ((uint8_t)12ULL): goto ma35; case ((uint8_t)13ULL): goto ma33; case ((uint8_t)14ULL): goto ma34; case ((uint8_t)15ULL): goto ma35; case ((uint8_t)16ULL): goto ma35; case ((uint8_t)17ULL): goto ma35; case ((uint8_t)18ULL): goto ma35; case ((uint8_t)19ULL): goto ma35; case ((uint8_t)20ULL): goto ma35; case ((uint8_t)21ULL): goto ma35; case ((uint8_t)22ULL): goto ma35; case ((uint8_t)23ULL): goto ma35; case ((uint8_t)24ULL): goto ma35; case ((uint8_t)25ULL): goto ma35; case ((uint8_t)26ULL): goto ma35; default: goto ma35; }
ma33:;
        uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t39 != ((uint8_t)13ULL)) goto mn38;
        {
            void * t41 = gm_l0;
            void * t42 = gm_l2;
            gm_t88rcucg1b7uk t43 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t42) + 0));
            gm_b16_8 t44 = gm_l6;
            gm_b16_8 t45 = gm_f_opt_present_joptd3k((void *)NULL, cx, NULL, t41, t43, t44);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l10 = t45;
        }
        {
            void * t46 = gm_l0;
            gm_b16_8 t47 = gm_l10;
            gm_tnpifv20irnph t48;
            GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
            gm_b0_1 t49 = gm_f_linef_o1esdvv((void *)NULL, cx, NULL, t46, t48);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t50 = gm_l0;
            void * t51 = gm_l0;
            uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)t51) + 224));
            uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
            if (o54) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 663, 13);
            uint64_t t55 = r53;
            GM_ST(uint64_t, (((uint8_t *)t50) + 224), t55);
        }
        {
            void * t56 = gm_l0;
            gm_t88rcucg1b7uk t57 = gm_l7;
            gm_b16_8 t58 = gm_f_make_none_v2s8s0l((void *)NULL, cx, NULL, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l11 = t58;
        }
        {
            void * t59 = gm_l0;
            gm_t6j6smj7qoctu t60;
            gm_b16_8 t61 = gm_l8;
            GM_ST(gm_b16_8, (uint8_t *)&t60 + 0, t61);
            gm_b16_8 t62 = gm_l11;
            GM_ST(gm_b16_8, (uint8_t *)&t60 + 16, t62);
            gm_b0_1 t63 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t59, &t60);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t64 = gm_l0;
            gm_t9qkh0rsooc8d t65;
            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)0ULL));
            gm_b16_8 t66 = gm_l9;
            gm_b16_8 t67;
            GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(1));
            gm_b16_8 t68;
            GM_ST(void *, (uint8_t *)&t68, (void *)(gm_sicgmpca63jln));
            GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(1));
            gm_b0_1 t69 = gm_f_start_exit_m57dnn5((void *)NULL, cx, NULL, t64, t65, t66, t67, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t70 = gm_l0;
            void * t71 = gm_l0;
            uint64_t t72 = GM_LD(uint64_t, (((uint8_t *)t71) + 224));
            uint64_t r73; bool o74 = __builtin_sub_overflow(t72, ((uint64_t)1ULL), &r73);
            if (o74) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 667, 13);
            uint64_t t75 = r73;
            GM_ST(uint64_t, (((uint8_t *)t70) + 224), t75);
        }
        {
            void * t76 = gm_l0;
            gm_b16_8 t77;
            GM_ST(void *, (uint8_t *)&t77, (void *)(gm_s2cuflhqkkuaq));
            GM_ST(uintptr_t, (uint8_t *)&t77 + 8, (uintptr_t)(1));
            gm_b0_1 t78 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t76, t77);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t79 = gm_l0;
            void * t80 = gm_l1;
            gm_t88rcucg1b7uk t81 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t80) + 0));
            gm_b16_8 t82 = gm_l6;
            gm_b16_8 t83 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t79, t81, t82);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t84;
            GM_ST(uint8_t, (uint8_t *)&t84 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t84 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t84 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t84 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t84 + 32, 0);
            ((uint8_t *)&t84)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t84 + 8), t83);
            gm_t5o0tpv5jka2l t85 = t84;
            uint8_t t86 = ((((uint8_t *)&t85))[0] != 0);
            F.ret = t84;
            goto gm_fret;
        }
        goto me32;
mn38:;
ma34:;
        uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&t31) + 0));
        if (t88 != ((uint8_t)14ULL)) goto mn87;
        gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t31) + 8) + 0));
        gm_l12 = t89;
        {
            void * t91 = gm_l0;
            void * t92 = gm_l2;
            gm_t88rcucg1b7uk t93 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t92) + 0));
            gm_b16_8 t94 = gm_l6;
            gm_b16_8 t95 = gm_f_res_is_err_11p8qlp((void *)NULL, cx, NULL, t91, t93, t94);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l13 = t95;
        }
        {
            uint64_t t96 = (uintptr_t)((uint64_t)1ULL);
            void * t97 = gm_l0;
            void * t98 = GM_LD(void *, (((uint8_t *)t97) + 0));
            void * t99 = (void *)(((uint8_t *)t98) + 8);
            void * t100 = gm_l2;
            gm_t88rcucg1b7uk t101 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t100) + 0));
            gm_b16_8 t102 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t99, t101);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t103 = t102;
            uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&t103) + 8);
            if (t96 >= (uintptr_t)(t104)) gm_trap_index(t96, t104, gm_srcmeaag4q4qrjl, 673, 20);
            uint64_t t105 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t103)) + t96 * 8));
            gm_l14 = t105;
        }
        {
            void * t106 = gm_l0;
            gm_b16_8 t107 = gm_l13;
            gm_tnpifv20irnph t108;
            GM_ST(gm_b16_8, ((uint8_t *)&t108 + 0), t107);
            gm_b0_1 t109 = gm_f_linef_g6msml8((void *)NULL, cx, NULL, t106, t108);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t110 = gm_l0;
            void * t111 = gm_l0;
            uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)t111) + 224));
            uint64_t r113; bool o114 = __builtin_add_overflow(t112, ((uint64_t)1ULL), &r113);
            if (o114) gm_trap("integer overflow in `+`", gm_srcmeaag4q4qrjl, 675, 13);
            uint64_t t115 = r113;
            GM_ST(uint64_t, (((uint8_t *)t110) + 224), t115);
        }
        {
            void * t117 = gm_l0;
            void * t118 = GM_LD(void *, (((uint8_t *)t117) + 0));
            void * t119 = (void *)(((uint8_t *)t118) + 8);
            gm_t88rcucg1b7uk t120 = gm_l7;
            gm_t53at80pfdus3 t121;
            gm_f_kind_ghvav22((void *)&t121, t119, GM_CONV(gm_csguankko4lmhd, t120));
            gm_t53at80pfdus3 t122 = t121;
            uint8_t t123 = GM_LD(uint8_t, (((uint8_t *)&t122) + 0));
            if (t123 != ((uint8_t)14ULL)) goto ie116;
            gm_t88rcucg1b7uk t124 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t122) + 8) + 0));
            gm_l15 = t124;
            {
                void * t126 = gm_l0;
                gm_t88rcucg1b7uk t127 = gm_l12;
                gm_t3877pk7lv748 t128;
                gm_b16_8 t129 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t128 + 0, t129);
                uint64_t t130 = gm_l14;
                GM_ST(uint64_t, (uint8_t *)&t128 + 16, t130);
                gm_tabb710d9mq2e t131;
                gm_f_format_gjujk09((void *)&t131, (void *)NULL, cx, NULL, &t128);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t132 = t131;
                if (((((uint8_t *)&t132))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t132) + 8, gm_srcmeaag4q4qrjl, 677, 32);
                gm_b16_8 t133 = GM_LD(gm_b16_8, (((uint8_t *)&t132) + 8));
                gm_b16_8 t134 = t133;
                gm_b16_8 t135 = GM_LD(gm_b16_8, ((uint8_t *)&t134));
                gm_b16_8 t136 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t126, t127, t135);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l16 = t136;
            }
            {
                if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie137;
                uint32_t t138 = GM_LD(uint32_t, ((uint8_t *)&gm_l3));
                gm_l17 = t138;
                {
                    void * t140 = gm_l0;
                    gm_t88rcucg1b7uk t141 = gm_l15;
                    uint32_t t142 = gm_l17;
                    uint64_t t143 = (uint64_t)t142;
                    gm_b16_8 t144 = gm_l16;
                    gm_th2lqbo77pvsc t145;
                    GM_ST(uint8_t, (uint8_t *)&t145 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t145 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t145 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t145), t144);
                    ((uint8_t *)&t145)[16] = 1;
                    gm_b16_8 t146 = gm_f_make_variant_srnk8ha((void *)NULL, cx, NULL, t140, t141, t143, &t145);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l16 = t146;
                }
                goto ix137;
ie137:;
ix137:;
            }
            {
                void * t147 = gm_l0;
                gm_t88rcucg1b7uk t148 = gm_l7;
                gm_t88rcucg1b7uk t149 = gm_l15;
                gm_b16_8 t150 = gm_l16;
                gm_b16_8 t151 = gm_f_make_res_riv19po((void *)NULL, cx, NULL, t147, t148, ((uint8_t)1), t149, t150);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l18 = t151;
            }
            {
                void * t152 = gm_l0;
                gm_t6j6smj7qoctu t153;
                gm_b16_8 t154 = gm_l8;
                GM_ST(gm_b16_8, (uint8_t *)&t153 + 0, t154);
                gm_b16_8 t155 = gm_l18;
                GM_ST(gm_b16_8, (uint8_t *)&t153 + 16, t155);
                gm_b0_1 t156 = gm_f_linef_uu8i6ue((void *)NULL, cx, NULL, t152, &t153);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t157 = gm_l0;
                gm_t9qkh0rsooc8d t158;
                GM_ST(uint8_t, ((uint8_t *)&t158 + 0), ((uint8_t)0ULL));
                gm_b16_8 t159 = gm_l9;
                gm_b16_8 t160;
                GM_ST(void *, (uint8_t *)&t160, (void *)(gm_sicgmpca63jln));
                GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(1));
                gm_b16_8 t161;
                GM_ST(void *, (uint8_t *)&t161, (void *)(gm_svubtddeidu1d));
                GM_ST(uintptr_t, (uint8_t *)&t161 + 8, (uintptr_t)(1));
                gm_b0_1 t162 = gm_f_start_exit_m57dnn5((void *)NULL, cx, NULL, t157, t158, t159, t160, t161);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t163 = gm_l0;
                void * t164 = gm_l0;
                uint64_t t165 = GM_LD(uint64_t, (((uint8_t *)t164) + 224));
                uint64_t r166; bool o167 = __builtin_sub_overflow(t165, ((uint64_t)1ULL), &r166);
                if (o167) gm_trap("integer overflow in `-`", gm_srcmeaag4q4qrjl, 682, 13);
                uint64_t t168 = r166;
                GM_ST(uint64_t, (((uint8_t *)t163) + 224), t168);
            }
            {
                void * t169 = gm_l0;
                gm_b16_8 t170;
                GM_ST(void *, (uint8_t *)&t170, (void *)(gm_s2cuflhqkkuaq));
                GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(1));
                gm_b0_1 t171 = gm_f_line_l6u87p9((void *)NULL, cx, NULL, t169, t170);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t172 = gm_l0;
                void * t173 = gm_l1;
                gm_t88rcucg1b7uk t174 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t173) + 0));
                gm_t3877pk7lv748 t175;
                gm_b16_8 t176 = gm_l6;
                GM_ST(gm_b16_8, (uint8_t *)&t175 + 0, t176);
                uint64_t t177 = gm_l14;
                GM_ST(uint64_t, (uint8_t *)&t175 + 16, t177);
                gm_tabb710d9mq2e t178;
                gm_f_format_gjujk09((void *)&t178, (void *)NULL, cx, NULL, &t175);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t179 = t178;
                if (((((uint8_t *)&t179))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t179) + 8, gm_srcmeaag4q4qrjl, 684, 34);
                gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)&t179) + 8));
                gm_b16_8 t181 = t180;
                gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
                gm_b16_8 t183 = gm_f_load_0hebde7((void *)NULL, cx, NULL, t172, t174, t182);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t184;
                GM_ST(uint8_t, (uint8_t *)&t184 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t184 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t184 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t184 + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&t184 + 32, 0);
                ((uint8_t *)&t184)[0] = 0;
                GM_ST(gm_b16_8, ((uint8_t *)&t184 + 8), t183);
                gm_t5o0tpv5jka2l t185 = t184;
                uint8_t t186 = ((((uint8_t *)&t185))[0] != 0);
                F.ret = t184;
                goto gm_fret;
            }
            goto ix116;
ie116:;
            {
                void * t188 = gm_l1;
                gm_tl874t2atnlh3 t189 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t188) + 72));
                gm_b16_8 t190;
                GM_ST(void *, (uint8_t *)&t190, (void *)(gm_s6cju4qiijn6t));
                GM_ST(uintptr_t, (uint8_t *)&t190 + 8, (uintptr_t)(22));
                gm_t08olq97rscn3 t191;
                gm_f_unsupported_531q51r((void *)&t191, (void *)NULL, cx, NULL, t189, t190);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t192;
                GM_ST(uint8_t, (uint8_t *)&t192 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t192 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t192 + 4, 0);
                ((uint8_t *)&t192)[0] = 1;
                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t192 + 8), t191);
                gm_t5o0tpv5jka2l t193 = t192;
                uint8_t t194 = ((((uint8_t *)&t193))[0] != 0);
                F.ret = t192;
                goto gm_fret;
            }
ix116:;
        }
        goto me32;
mn87:;
ma35:;
        void * t196 = gm_l1;
        gm_tl874t2atnlh3 t197 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t196) + 72));
        gm_b16_8 t198;
        GM_ST(void *, (uint8_t *)&t198, (void *)(gm_sjr00kolufse9));
        GM_ST(uintptr_t, (uint8_t *)&t198 + 8, (uintptr_t)(18));
        gm_t08olq97rscn3 t199;
        gm_f_unsupported_531q51r((void *)&t199, (void *)NULL, cx, NULL, t197, t198);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t200;
        GM_ST(uint8_t, (uint8_t *)&t200 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t200 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t200 + 4, 0);
        ((uint8_t *)&t200)[0] = 1;
        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t200 + 8), t199);
        gm_t5o0tpv5jka2l t201 = t200;
        uint8_t t202 = ((((uint8_t *)&t201))[0] != 0);
        F.ret = t200;
        goto gm_fret;
        goto me32;
mn195:;
ma36:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 659, 5);
me32:;
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
__attribute__((aligned(4))) void gm_f_variant_equal_bgud9j0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_variant_equal_bgud9j0 F;
    void * gm_l0; /* fg */
    void * gm_l1; /* lhs */
    void * gm_l2; /* rhs */
    gm_tmmlu9bk8dch4 gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    void * gm_l6; /* x */
    void * gm_l7; /* y */
    gm_b16_8 gm_l8; /* bytes */
    void * gm_l9; /* x */
    gm_ttggq7ng4kp3p gm_l10; /* vx */
    gm_ttggq7ng4kp3p gm_l11; /* v */
    gm_ttggq7ng4kp3p gm_l12; /* v */
    gm_t88rcucg1b7uk gm_l13; /* t */
    gm_t88rcucg1b7uk gm_l14; /* tag */
    gm_b16_8 gm_l15; /* variants */
    void * gm_l17; /* p */
    uint8_t gm_l18; /* in_place */
    gm_b16_8 gm_l19; /* addr */
    gm_th2lqbo77pvsc gm_l20; /* payload */
    void * gm_l21; /* p */
    void * gm_l22; /* p */
    gm_b16_8 gm_l23; /* offs */
    gm_b16_8 gm_l24; /* tc */
    gm_b16_8 gm_l25; /* tv */
    gm_tuglhjjmj2g58 gm_l26; /* ii */
    gm_b16_8 gm_l27; /* c */
    gm_b16_8 gm_l28; /* pv */
    gm_t88rcucg1b7uk gm_l29; /* pt */
    gm_b16_8 gm_l30; /* pa */
    gm_b16_8 gm_l31; /* pe */
    gm_b16_8 gm_l32; /* f */
    gm_b16_8 gm_l33; /* ld */
    gm_tuglhjjmj2g58 gm_l34; /* ii */
    GM_STACK_CHECK(&F, 5196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        {
            {
                void * t4 = gm_l1;
                void * t5 = gm_l2;
                gm_t3mf58gv9kf3n t6;
                GM_ST(void *, ((uint8_t *)&t6 + 0), t4);
                GM_ST(void *, ((uint8_t *)&t6 + 8), t5);
                GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&gm_l3 + 0, t6);
                void * t7 = gm_l2;
                void * t8 = gm_l1;
                gm_t3mf58gv9kf3n t9;
                GM_ST(void *, ((uint8_t *)&t9 + 0), t7);
                GM_ST(void *, ((uint8_t *)&t9 + 8), t8);
                GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&gm_l3 + 16, t9);
            }
            void * t10 = ((uint8_t *)&gm_l3);
            if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcmeaag4q4qrjl, 710, 5);
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(((uint8_t *)t10 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
            gm_b16_8 t12 = t11;
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
                    void * t24 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t20 * 16) + 0));
                    gm_l6 = t24;
                    void * t25 = GM_LD(void *, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t20 * 16) + 8));
                    gm_l7 = t25;
                    {
                        void * t28 = gm_l7;
                        uint8_t t29 = GM_LD(uint8_t, ((((uint8_t *)t28) + 8) + 0));
                        if (t29 != ((uint8_t)5ULL)) goto ie27;
                        gm_b16_8 t30 = GM_LD(gm_b16_8, ((((uint8_t *)t28) + 8) + 8));
                        gm_l8 = t30;
                        void * t31 = gm_l0;
                        void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
                        void * t33 = (void *)(((uint8_t *)t32) + 8);
                        void * t34 = gm_l6;
                        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t34) + 0));
                        gm_t53at80pfdus3 t36;
                        gm_f_kind_ghvav22((void *)&t36, t33, GM_CONV(gm_csguankko4lmhd, t35));
                        gm_t53at80pfdus3 t37 = t36;
                        uint8_t t38;
                        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)&t37) + 0));
                        if (t41 != ((uint8_t)10ULL)) goto mn40;
                        t38 = ((uint8_t)1);
                        goto me39;
mn40:;
                        t38 = ((uint8_t)0);
                        goto me39;
mn42:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 711, 38);
me39:;
                        if (!t38) goto ie27;
                        {
                            void * t44 = gm_l0;
                            void * t45 = gm_l0;
                            void * t46 = gm_l6;
                            gm_t5o0tpv5jka2l t47;
                            gm_f_scrutinee_addr_3fvmbme((void *)&t47, (void *)NULL, cx, NULL, t45, t46, ((uint8_t)1));
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_t5o0tpv5jka2l t48 = t47;
                            if (((((uint8_t *)&t48))[0] != 0)) {
                                gm_t08olq97rscn3 t49 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t48) + 8));
                                gm_t6j4d6ei14c2t t50;
                                GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                                ((uint8_t *)&t50)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t50 + 8), t49);
                                F.ret = t50;
                                goto gm_fret;
                            }
                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t48) + 8));
                            gm_b16_8 t52 = gm_l8;
                            gm_b16_8 t53 = gm_f_str_equal_at_mshane1((void *)NULL, cx, NULL, t44, t51, t52);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_th2lqbo77pvsc t54;
                            GM_ST(uint8_t, (uint8_t *)&t54 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t54 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t54 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t54), t53);
                            ((uint8_t *)&t54)[16] = 1;
                            gm_t6j4d6ei14c2t t55;
                            GM_ST(uint8_t, (uint8_t *)&t55 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t55 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t55 + 4, 0);
                            GM_ST(uint64_t, (uint8_t *)&t55 + 32, 0);
                            ((uint8_t *)&t55)[0] = 0;
                            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t55 + 8), t54);
                            gm_t6j4d6ei14c2t t56 = t55;
                            uint8_t t57 = ((((uint8_t *)&t56))[0] != 0);
                            F.ret = t55;
                            goto gm_fret;
                        }
                        goto ix27;
ie27:;
ix27:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 710, 5);
me22:;
                }
lc13:;
                {
                    uint64_t t59 = gm_l5;
                    uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                    uint64_t t62 = r60;
                    gm_l5 = t62;
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
        void * t63 = gm_l1;
        gm_l9 = t63;
    }
    {
        gm_ttggq7ng4kp3p t64;
        GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t64 + 0), ((uint32_t)0ULL));
        GM_ST(void *, ((uint8_t *)&t64 + 8), ((void *)0));
        gm_l10 = t64;
    }
    {
        void * t66 = gm_l2;
        uint8_t t67 = GM_LD(uint8_t, ((((uint8_t *)t66) + 8) + 0));
        if (t67 != ((uint8_t)38ULL)) goto ie65;
        gm_ttggq7ng4kp3p t68 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t66) + 8) + 8));
        gm_l11 = t68;
        {
            gm_ttggq7ng4kp3p t70 = gm_l11;
            gm_l10 = t70;
        }
        goto ix65;
ie65:;
        void * t72 = gm_l1;
        uint8_t t73 = GM_LD(uint8_t, ((((uint8_t *)t72) + 8) + 0));
        if (t73 != ((uint8_t)38ULL)) goto ie71;
        gm_ttggq7ng4kp3p t74 = GM_LD(gm_ttggq7ng4kp3p, ((((uint8_t *)t72) + 8) + 8));
        gm_l12 = t74;
        {
            void * t76 = gm_l2;
            gm_l9 = t76;
        }
        {
            gm_ttggq7ng4kp3p t77 = gm_l12;
            gm_l10 = t77;
        }
        goto ix71;
ie71:;
        {
            gm_th2lqbo77pvsc t79;
            memset(&t79, 0, sizeof t79);
            gm_t6j4d6ei14c2t t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t80 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t80 + 32, 0);
            ((uint8_t *)&t80)[0] = 0;
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t80 + 8), t79);
            gm_t6j4d6ei14c2t t81 = t80;
            uint8_t t82 = ((((uint8_t *)&t81))[0] != 0);
            F.ret = t80;
            goto gm_fret;
        }
ix71:;
ix65:;
    }
    {
        void * t83 = gm_l9;
        gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t83) + 0));
        gm_l13 = t84;
    }
    {
        void * t86 = gm_l0;
        void * t87 = GM_LD(void *, (((uint8_t *)t86) + 0));
        void * t88 = (void *)(((uint8_t *)t87) + 8);
        gm_t88rcucg1b7uk t89 = gm_l13;
        gm_t5v8n6qrn4rh4 t90;
        gm_f_repr_53quqki((void *)&t90, (void *)NULL, cx, NULL, t88, t89);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5v8n6qrn4rh4 t91 = t90;
        uint8_t t92 = (!(GM_LD(uint8_t, ((uint8_t *)&t91) + 0) == ((uint8_t)6ULL)));
        if (!t92) goto ie85;
        {
            gm_th2lqbo77pvsc t94;
            memset(&t94, 0, sizeof t94);
            gm_t6j4d6ei14c2t t95;
            GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t95 + 32, 0);
            ((uint8_t *)&t95)[0] = 0;
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t95 + 8), t94);
            gm_t6j4d6ei14c2t t96 = t95;
            uint8_t t97 = ((((uint8_t *)&t96))[0] != 0);
            F.ret = t95;
            goto gm_fret;
        }
        goto ix85;
ie85:;
ix85:;
    }
    {
        void * t99 = gm_l0;
        gm_t88rcucg1b7uk t100 = gm_l13;
        void * t101 = gm_f_adt_def_ni54he1(t99, GM_CONV(gm_csguankko4lmhd, t100));
        uint8_t t102 = GM_LD(uint8_t, ((((uint8_t *)t101) + 24) + 0));
        if (t102 != ((uint8_t)1ULL)) goto ie98;
        gm_t88rcucg1b7uk t103 = GM_LD(gm_t88rcucg1b7uk, (((((uint8_t *)t101) + 24) + 8) + 0));
        gm_l14 = t103;
        gm_b16_8 t104 = GM_LD(gm_b16_8, (((((uint8_t *)t101) + 24) + 8) + 8));
        gm_l15 = t104;
        {
            uint32_t t106 = GM_LD(uint32_t, (((uint8_t *)&gm_l10) + 0));
            uint64_t t107 = (uint64_t)t106;
            uint64_t t108 = (uintptr_t)t107;
            uint64_t t109 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
            if (t108 >= (uintptr_t)(t109)) gm_trap_index(t108, t109, gm_srcmeaag4q4qrjl, 728, 16);
            gm_tckdobunaulee t110 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t108 * 72));
            F.l16 = t110;
        }
        {
            uint8_t t111;
            void * t113 = GM_LD(void *, (((uint8_t *)&gm_l10) + 8));
            void * t114 = t113;
            if (!(GM_LD(void *, ((uint8_t *)&t114)) != 0)) goto ie112;
            void * t115 = GM_LD(void *, ((uint8_t *)&t114));
            gm_l17 = t115;
            void * t117 = gm_l0;
            void * t118 = gm_l17;
            uint8_t t119 = gm_f_frame_pure_cbdra2q((void *)NULL, cx, NULL, t117, t118);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t120 = t119;
            t111 = t120;
            goto ix112;
ie112:;
            uint8_t t122 = ((uint8_t)1);
            t111 = t122;
ix112:;
            gm_l18 = t111;
        }
        {
            gm_b16_8 t123;
            GM_ST(void *, (uint8_t *)&t123, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(0));
            gm_l19 = t123;
        }
        {
            gm_th2lqbo77pvsc t124;
            memset(&t124, 0, sizeof t124);
            gm_l20 = t124;
        }
        {
            void * t126 = gm_l9;
            void * t127 = gm_l1;
            uint8_t t128 = ((t126 == t127));
            if (!t128) goto ie125;
            {
                void * t130 = gm_l0;
                void * t131 = gm_l9;
                uint8_t t132 = gm_l18;
                gm_t5o0tpv5jka2l t133;
                gm_f_scrutinee_addr_3fvmbme((void *)&t133, (void *)NULL, cx, NULL, t130, t131, t132);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t134 = t133;
                if (((((uint8_t *)&t134))[0] != 0)) {
                    gm_t08olq97rscn3 t135 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t134) + 8));
                    gm_t6j4d6ei14c2t t136;
                    GM_ST(uint8_t, (uint8_t *)&t136 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t136 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t136 + 4, 0);
                    ((uint8_t *)&t136)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t136 + 8), t135);
                    F.ret = t136;
                    goto gm_fret;
                }
                gm_b16_8 t137 = GM_LD(gm_b16_8, (((uint8_t *)&t134) + 8));
                gm_l19 = t137;
            }
            {
                void * t139 = GM_LD(void *, (((uint8_t *)&gm_l10) + 8));
                void * t140 = t139;
                if (!(GM_LD(void *, ((uint8_t *)&t140)) != 0)) goto ie138;
                void * t141 = GM_LD(void *, ((uint8_t *)&t140));
                gm_l21 = t141;
                {
                    void * t143 = gm_l0;
                    void * t144 = gm_l21;
                    gm_t5o0tpv5jka2l t145;
                    gm_f_expr_porbc9t((void *)&t145, (void *)NULL, cx, NULL, t143, t144);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5o0tpv5jka2l t146 = t145;
                    if (((((uint8_t *)&t146))[0] != 0)) {
                        gm_t08olq97rscn3 t147 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t146) + 8));
                        gm_t6j4d6ei14c2t t148;
                        GM_ST(uint8_t, (uint8_t *)&t148 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t148 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t148 + 4, 0);
                        ((uint8_t *)&t148)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t148 + 8), t147);
                        F.ret = t148;
                        goto gm_fret;
                    }
                    gm_b16_8 t149 = GM_LD(gm_b16_8, (((uint8_t *)&t146) + 8));
                    gm_th2lqbo77pvsc t150;
                    GM_ST(uint8_t, (uint8_t *)&t150 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t150 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t150 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t150), t149);
                    ((uint8_t *)&t150)[16] = 1;
                    gm_l20 = t150;
                }
                goto ix138;
ie138:;
ix138:;
            }
            goto ix125;
ie125:;
            {
                void * t153 = GM_LD(void *, (((uint8_t *)&gm_l10) + 8));
                void * t154 = t153;
                if (!(GM_LD(void *, ((uint8_t *)&t154)) != 0)) goto ie152;
                void * t155 = GM_LD(void *, ((uint8_t *)&t154));
                gm_l22 = t155;
                {
                    void * t157 = gm_l0;
                    void * t158 = gm_l22;
                    gm_t5o0tpv5jka2l t159;
                    gm_f_expr_porbc9t((void *)&t159, (void *)NULL, cx, NULL, t157, t158);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t5o0tpv5jka2l t160 = t159;
                    if (((((uint8_t *)&t160))[0] != 0)) {
                        gm_t08olq97rscn3 t161 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t160) + 8));
                        gm_t6j4d6ei14c2t t162;
                        GM_ST(uint8_t, (uint8_t *)&t162 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t162 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
                        ((uint8_t *)&t162)[0] = 1;
                        GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t162 + 8), t161);
                        F.ret = t162;
                        goto gm_fret;
                    }
                    gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&t160) + 8));
                    gm_th2lqbo77pvsc t164;
                    GM_ST(uint8_t, (uint8_t *)&t164 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t164 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t164 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t164), t163);
                    ((uint8_t *)&t164)[16] = 1;
                    gm_l20 = t164;
                }
                goto ix152;
ie152:;
ix152:;
            }
            {
                void * t165 = gm_l0;
                void * t166 = gm_l9;
                gm_t5o0tpv5jka2l t167;
                gm_f_scrutinee_addr_3fvmbme((void *)&t167, (void *)NULL, cx, NULL, t165, t166, ((uint8_t)0));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5o0tpv5jka2l t168 = t167;
                if (((((uint8_t *)&t168))[0] != 0)) {
                    gm_t08olq97rscn3 t169 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t168) + 8));
                    gm_t6j4d6ei14c2t t170;
                    GM_ST(uint8_t, (uint8_t *)&t170 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t170 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t170 + 4, 0);
                    ((uint8_t *)&t170)[0] = 1;
                    GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t170 + 8), t169);
                    F.ret = t170;
                    goto gm_fret;
                }
                gm_b16_8 t171 = GM_LD(gm_b16_8, (((uint8_t *)&t168) + 8));
                gm_l19 = t171;
            }
ix125:;
        }
        {
            void * t172 = gm_l0;
            void * t173 = GM_LD(void *, (((uint8_t *)t172) + 0));
            void * t174 = (void *)(((uint8_t *)t173) + 8);
            gm_t88rcucg1b7uk t175 = gm_l13;
            gm_b16_8 t176 = gm_f_offsets_eh6vfle((void *)NULL, cx, NULL, t174, t175);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l23 = t176;
        }
        {
            void * t177 = gm_l0;
            void * t178 = GM_LD(void *, (((uint8_t *)t177) + 0));
            void * t179 = (void *)(((uint8_t *)t178) + 8);
            gm_t88rcucg1b7uk t180 = gm_l14;
            gm_b16_8 t181 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t179, t180);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l24 = t181;
        }
        {
            gm_tftksr4op114e t182;
            gm_b16_8 t183 = gm_l24;
            GM_ST(gm_b16_8, (uint8_t *)&t182 + 0, t183);
            gm_b16_8 t184 = gm_l19;
            GM_ST(gm_b16_8, (uint8_t *)&t182 + 16, t184);
            uint64_t t185 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t186 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
            if (t185 >= (uintptr_t)(t186)) gm_trap_index(t185, t186, gm_srcmeaag4q4qrjl, 742, 61);
            uint64_t t187 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t185 * 8));
            GM_ST(uint64_t, (uint8_t *)&t182 + 32, t187);
            gm_tabb710d9mq2e t188;
            gm_f_format_qj6rkil((void *)&t188, (void *)NULL, cx, NULL, &t182);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t189 = t188;
            if (((((uint8_t *)&t189))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t189) + 8, gm_srcmeaag4q4qrjl, 742, 18);
            gm_b16_8 t190 = GM_LD(gm_b16_8, (((uint8_t *)&t189) + 8));
            gm_b16_8 t191 = t190;
            gm_b16_8 t192 = GM_LD(gm_b16_8, ((uint8_t *)&t191));
            gm_l25 = t192;
        }
        {
            void * t194 = gm_l0;
            gm_t88rcucg1b7uk t195 = gm_l14;
            gm_tqcc6dmlne11b t196;
            gm_f_int_info_2tcdf1n((void *)&t196, (void *)NULL, cx, NULL, t194, t195);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tqcc6dmlne11b t197 = t196;
            if (!((((uint8_t *)&t197))[16] != 0)) goto ie193;
            gm_tuglhjjmj2g58 t198 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t197));
            gm_l26 = t198;
            {
                gm_tuglhjjmj2g58 t200 = gm_l26;
                gm_b16_8 t201 = gm_l25;
                gm_b16_8 t202 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t200, t201);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l25 = t202;
            }
            goto ix193;
ie193:;
ix193:;
        }
        {
            gm_t6j6smj7qoctu t203;
            gm_b16_8 t204 = gm_l25;
            GM_ST(gm_b16_8, (uint8_t *)&t203 + 0, t204);
            void * t205 = gm_l0;
            gm_t88rcucg1b7uk t206 = gm_l14;
            gm_b16_8 t207 = gm_f_int_lit_ai37456((void *)NULL, cx, NULL, t205, t206, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l16) + 24)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_b16_8, (uint8_t *)&t203 + 16, t207);
            gm_tabb710d9mq2e t208;
            gm_f_format_6m84e0r((void *)&t208, (void *)NULL, cx, NULL, &t203);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t209 = t208;
            if (((((uint8_t *)&t209))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t209) + 8, gm_srcmeaag4q4qrjl, 744, 10);
            gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)&t209) + 8));
            gm_l27 = t210;
        }
        {
            gm_th2lqbo77pvsc t211 = gm_l20;
            gm_th2lqbo77pvsc t212 = t211;
            gm_b16_8 t213;
            if (((((uint8_t *)&t212))[16] != 0)) {
                gm_b16_8 t214 = GM_LD(gm_b16_8, (((uint8_t *)&t212) + 0));
                t213 = t214;
            } else {
                gm_b16_8 t215 = gm_l27;
                gm_b16_8 t216 = t215;
                gm_b16_8 t217 = GM_LD(gm_b16_8, ((uint8_t *)&t216));
                gm_th2lqbo77pvsc t218;
                GM_ST(uint8_t, (uint8_t *)&t218 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t218 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t218 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t218), t217);
                ((uint8_t *)&t218)[16] = 1;
                gm_t6j4d6ei14c2t t219;
                GM_ST(uint8_t, (uint8_t *)&t219 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t219 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t219 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t219 + 32, 0);
                ((uint8_t *)&t219)[0] = 0;
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t219 + 8), t218);
                gm_t6j4d6ei14c2t t220 = t219;
                uint8_t t221 = ((((uint8_t *)&t220))[0] != 0);
                F.ret = t219;
                goto gm_fret;
            }
            gm_l28 = t213;
        }
        {
            gm_tkn42u7na3t81 t222 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)&F.l16) + 16));
            gm_tkn42u7na3t81 t223 = t222;
            gm_t88rcucg1b7uk t224;
            if (((((uint8_t *)&t223))[4] != 0)) {
                gm_t88rcucg1b7uk t225 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t223) + 0));
                t224 = t225;
            } else {
                gm_b16_8 t226 = gm_l27;
                gm_b16_8 t227 = t226;
                gm_b16_8 t228 = GM_LD(gm_b16_8, ((uint8_t *)&t227));
                gm_th2lqbo77pvsc t229;
                GM_ST(uint8_t, (uint8_t *)&t229 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t229 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t229 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t229), t228);
                ((uint8_t *)&t229)[16] = 1;
                gm_t6j4d6ei14c2t t230;
                GM_ST(uint8_t, (uint8_t *)&t230 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t230 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t230 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t230 + 32, 0);
                ((uint8_t *)&t230)[0] = 0;
                GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t230 + 8), t229);
                gm_t6j4d6ei14c2t t231 = t230;
                uint8_t t232 = ((((uint8_t *)&t231))[0] != 0);
                F.ret = t230;
                goto gm_fret;
            }
            gm_l29 = t224;
        }
        {
            gm_t3877pk7lv748 t233;
            gm_b16_8 t234 = gm_l19;
            GM_ST(gm_b16_8, (uint8_t *)&t233 + 0, t234);
            uint64_t t235 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t236 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
            if (t235 >= (uintptr_t)(t236)) gm_trap_index(t235, t236, gm_srcmeaag4q4qrjl, 747, 41);
            uint64_t t237 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t235 * 8));
            GM_ST(uint64_t, (uint8_t *)&t233 + 16, t237);
            gm_tabb710d9mq2e t238;
            gm_f_format_gjujk09((void *)&t238, (void *)NULL, cx, NULL, &t233);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t239 = t238;
            if (((((uint8_t *)&t239))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t239) + 8, gm_srcmeaag4q4qrjl, 747, 11);
            gm_b16_8 t240 = GM_LD(gm_b16_8, (((uint8_t *)&t239) + 8));
            gm_l30 = t240;
        }
        {
            gm_b16_8 t241;
            GM_ST(void *, (uint8_t *)&t241, (void *)(gm_sr4qbrk6tlgaa));
            GM_ST(uintptr_t, (uint8_t *)&t241 + 8, (uintptr_t)(0));
            gm_l31 = t241;
        }
        {
            void * t243 = gm_l0;
            void * t244 = GM_LD(void *, (((uint8_t *)t243) + 0));
            void * t245 = (void *)(((uint8_t *)t244) + 8);
            gm_t88rcucg1b7uk t246 = gm_l29;
            gm_t5v8n6qrn4rh4 t247;
            gm_f_repr_53quqki((void *)&t247, (void *)NULL, cx, NULL, t245, t246);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5v8n6qrn4rh4 t248 = t247;
            uint8_t t249 = ((GM_LD(uint8_t, ((uint8_t *)&t248) + 0) == ((uint8_t)6ULL)));
            if (!t249) goto ie242;
            {
                void * t251 = gm_l0;
                void * t252 = GM_LD(void *, (((uint8_t *)t251) + 0));
                void * t253 = (void *)(((uint8_t *)t252) + 1968);
                void * t254 = gm_l0;
                void * t255 = GM_LD(void *, (((uint8_t *)t254) + 0));
                void * t256 = (void *)(((uint8_t *)t255) + 8);
                gm_t88rcucg1b7uk t257 = gm_l29;
                gm_b16_8 t258 = gm_f_eq_fn_5udvcnu((void *)NULL, cx, NULL, t253, t256, t257);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l32 = t258;
            }
            {
                gm_tfeab279c16rr t259;
                gm_b16_8 t260 = gm_l32;
                GM_ST(gm_b16_8, (uint8_t *)&t259 + 0, t260);
                gm_b16_8 t261 = gm_l30;
                GM_ST(gm_b16_8, (uint8_t *)&t259 + 16, t261);
                void * t262 = gm_l0;
                gm_t88rcucg1b7uk t263 = gm_l29;
                gm_b16_8 t264 = gm_l28;
                gm_b16_8 t265 = gm_f_addr_of_hiaqkn0((void *)NULL, cx, NULL, t262, t263, t264);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t259 + 32, t265);
                gm_tabb710d9mq2e t266;
                gm_f_format_h6uc0qg((void *)&t266, (void *)NULL, cx, NULL, &t259);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t267 = t266;
                if (((((uint8_t *)&t267))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t267) + 8, gm_srcmeaag4q4qrjl, 751, 14);
                gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&t267) + 8));
                gm_b16_8 t269 = t268;
                gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&t269));
                gm_l31 = t270;
            }
            goto ix242;
ie242:;
            {
                gm_t6j6smj7qoctu t272;
                void * t273 = gm_l0;
                void * t274 = GM_LD(void *, (((uint8_t *)t273) + 0));
                void * t275 = (void *)(((uint8_t *)t274) + 8);
                gm_t88rcucg1b7uk t276 = gm_l29;
                gm_b16_8 t277 = gm_f_cty_66ee0kf((void *)NULL, cx, NULL, t275, t276);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t272 + 0, t277);
                gm_b16_8 t278 = gm_l30;
                GM_ST(gm_b16_8, (uint8_t *)&t272 + 16, t278);
                gm_tabb710d9mq2e t279;
                gm_f_format_g9ssmu3((void *)&t279, (void *)NULL, cx, NULL, &t272);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t280 = t279;
                if (((((uint8_t *)&t280))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t280) + 8, gm_srcmeaag4q4qrjl, 753, 22);
                gm_b16_8 t281 = GM_LD(gm_b16_8, (((uint8_t *)&t280) + 8));
                gm_b16_8 t282 = t281;
                gm_b16_8 t283 = GM_LD(gm_b16_8, ((uint8_t *)&t282));
                gm_l33 = t283;
            }
            {
                void * t285 = gm_l0;
                gm_t88rcucg1b7uk t286 = gm_l29;
                gm_tqcc6dmlne11b t287;
                gm_f_int_info_2tcdf1n((void *)&t287, (void *)NULL, cx, NULL, t285, t286);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tqcc6dmlne11b t288 = t287;
                if (!((((uint8_t *)&t288))[16] != 0)) goto ie284;
                gm_tuglhjjmj2g58 t289 = GM_LD(gm_tuglhjjmj2g58, ((uint8_t *)&t288));
                gm_l34 = t289;
                {
                    gm_tuglhjjmj2g58 t291 = gm_l34;
                    gm_b16_8 t292 = gm_l33;
                    gm_b16_8 t293 = gm_f_normalize_jch381e((void *)NULL, cx, NULL, t291, t292);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_l33 = t293;
                }
                goto ix284;
ie284:;
ix284:;
            }
            {
                gm_b16_8 t294;
                void * t296 = gm_l0;
                void * t297 = GM_LD(void *, (((uint8_t *)t296) + 0));
                void * t298 = (void *)(((uint8_t *)t297) + 8);
                gm_t88rcucg1b7uk t299 = gm_l29;
                gm_t5v8n6qrn4rh4 t300;
                gm_f_repr_53quqki((void *)&t300, (void *)NULL, cx, NULL, t298, t299);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5v8n6qrn4rh4 t301 = t300;
                uint8_t t302 = ((GM_LD(uint8_t, ((uint8_t *)&t301) + 0) == ((uint8_t)2ULL)));
                if (!t302) goto ie295;
                gm_t6j6smj7qoctu t304;
                gm_b16_8 t305 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t304 + 0, t305);
                gm_b16_8 t306 = gm_l28;
                GM_ST(gm_b16_8, (uint8_t *)&t304 + 16, t306);
                gm_tabb710d9mq2e t307;
                gm_f_format_mjg3n5a((void *)&t307, (void *)NULL, cx, NULL, &t304);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t308 = t307;
                if (((((uint8_t *)&t308))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t308) + 8, gm_srcmeaag4q4qrjl, 755, 45);
                gm_b16_8 t309 = GM_LD(gm_b16_8, (((uint8_t *)&t308) + 8));
                gm_b16_8 t310 = t309;
                gm_b16_8 t311 = t310;
                gm_b16_8 t312 = GM_LD(gm_b16_8, ((uint8_t *)&t311));
                t294 = t312;
                goto ix295;
ie295:;
                gm_t6j6smj7qoctu t314;
                gm_b16_8 t315 = gm_l33;
                GM_ST(gm_b16_8, (uint8_t *)&t314 + 0, t315);
                gm_b16_8 t316 = gm_l28;
                GM_ST(gm_b16_8, (uint8_t *)&t314 + 16, t316);
                gm_tabb710d9mq2e t317;
                gm_f_format_6m84e0r((void *)&t317, (void *)NULL, cx, NULL, &t314);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t318 = t317;
                if (((((uint8_t *)&t318))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t318) + 8, gm_srcmeaag4q4qrjl, 755, 102);
                gm_b16_8 t319 = GM_LD(gm_b16_8, (((uint8_t *)&t318) + 8));
                gm_b16_8 t320 = t319;
                gm_b16_8 t321 = t320;
                gm_b16_8 t322 = GM_LD(gm_b16_8, ((uint8_t *)&t321));
                t294 = t322;
ix295:;
                gm_l31 = t294;
            }
ix242:;
        }
        {
            gm_t6j6smj7qoctu t323;
            gm_b16_8 t324 = gm_l27;
            GM_ST(gm_b16_8, (uint8_t *)&t323 + 0, t324);
            gm_b16_8 t325 = gm_l31;
            GM_ST(gm_b16_8, (uint8_t *)&t323 + 16, t325);
            gm_tabb710d9mq2e t326;
            gm_f_format_sjgrpqb((void *)&t326, (void *)NULL, cx, NULL, &t323);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t327 = t326;
            if (((((uint8_t *)&t327))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t327) + 8, gm_srcmeaag4q4qrjl, 757, 12);
            gm_b16_8 t328 = GM_LD(gm_b16_8, (((uint8_t *)&t327) + 8));
            gm_b16_8 t329 = t328;
            gm_b16_8 t330 = GM_LD(gm_b16_8, ((uint8_t *)&t329));
            gm_th2lqbo77pvsc t331;
            GM_ST(uint8_t, (uint8_t *)&t331 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t331 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t331 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t331), t330);
            ((uint8_t *)&t331)[16] = 1;
            gm_t6j4d6ei14c2t t332;
            GM_ST(uint8_t, (uint8_t *)&t332 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t332 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t332 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t332 + 32, 0);
            ((uint8_t *)&t332)[0] = 0;
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t332 + 8), t331);
            gm_t6j4d6ei14c2t t333 = t332;
            uint8_t t334 = ((((uint8_t *)&t333))[0] != 0);
            F.ret = t332;
            goto gm_fret;
        }
        goto ix98;
ie98:;
        {
            gm_th2lqbo77pvsc t336;
            memset(&t336, 0, sizeof t336);
            gm_t6j4d6ei14c2t t337;
            GM_ST(uint8_t, (uint8_t *)&t337 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t337 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t337 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t337 + 32, 0);
            ((uint8_t *)&t337)[0] = 0;
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t337 + 8), t336);
            gm_t6j4d6ei14c2t t338 = t337;
            uint8_t t339 = ((((uint8_t *)&t338))[0] != 0);
            F.ret = t337;
            goto gm_fret;
        }
ix98:;
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
__attribute__((aligned(4))) void gm_f_with_expr_l1uk88k(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint32_t a2, gm_b16_8 a3, void * a4, void * a5) {
    struct gm_fr_with_expr_l1uk88k F;
    void * gm_l0; /* fg */
    void * gm_l1; /* e */
    uint32_t gm_l2; /* label */
    gm_b16_8 gm_l3; /* handlers */
    void * gm_l4; /* on_ */
    void * gm_l5; /* body */
    gm_b16_8 gm_l6; /* cur */
    gm_b16_8 gm_l7; /* result_slot */
    gm_b16_8 gm_l8; /* fw */
    gm_b16_8 gm_l9; /* w */
    gm_b16_8 gm_l10; /* fw_ref */
    gm_t9805qtonbsag gm_l11; /* nested */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_t4lnf71mruqpt gm_l14; /* h */
    void * gm_l15; /* x */
    gm_b16_8 gm_l16; /* hv */
    uint32_t gm_l17; /* hid */
    gm_b16_8 gm_l18; /* table */
    uint32_t gm_l19; /* ns */
    gm_tiilmfd16v3fc gm_l20; /* eref */
    uint32_t gm_l21; /* id */
    gm_b16_8 gm_l22; /* n */
    gm_b16_8 gm_l23; /* decls */
    void * gm_l24; /* value */
    gm_b16_8 gm_l25; /* v */
    gm_b16_8 gm_l26; /* s */
    gm_b16_8 gm_l27; /* outer */
    gm_b16_8 gm_l28; /* <tmp> */
    uint64_t gm_l29; /* <tmp> */
    uint32_t gm_l30; /* d */
    gm_b16_8 gm_l31; /* table */
    gm_tiilmfd16v3fc gm_l32; /* eref */
    uint32_t gm_l33; /* id */
    gm_b16_8 gm_l34; /* n */
    void * gm_l35; /* stack */
    gm_b16_8 gm_l36; /* saved */
    gm_b16_8 gm_l37; /* end */
    gm_t5lf9i3n1r3co gm_l39; /* saved_spec */
    gm_b16_8 gm_l42; /* v */
    GM_STACK_CHECK(&F, 5027u);
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
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)t2) + 280));
        gm_l6 = t3;
    }
    {
        uint32_t t4 = gm_l2;
        gm_t88rcucg1b7uk t5;
        GM_ST(uint32_t, ((uint8_t *)&t5 + 0), t4);
        gm_tabb710d9mq2e t6;
        gm_f_format_9g4m5fi((void *)&t6, (void *)NULL, cx, NULL, t5);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcmeaag4q4qrjl, 593, 20);
        gm_b16_8 t8 = GM_LD(gm_b16_8, (((uint8_t *)&t7) + 8));
        gm_l7 = t8;
    }
    {
        gm_b16_8 t9;
        GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sr4qbrk6tlgaa));
        GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(0));
        gm_l8 = t9;
    }
    {
        void * t11 = gm_l4;
        void * t12 = t11;
        uint8_t t13;
        if ((GM_LD(void *, ((uint8_t *)&t12)) != 0)) goto mn15;
        t13 = ((uint8_t)0);
        goto me14;
mn15:;
        t13 = ((uint8_t)1);
        goto me14;
mn16:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 596, 8);
me14:;
        if (!t13) goto ie10;
        {
            void * t18 = gm_l0;
            uint32_t t19 = gm_f_fresh_5kokjmg(t18);
            gm_t88rcucg1b7uk t20;
            GM_ST(uint32_t, ((uint8_t *)&t20 + 0), t19);
            gm_tabb710d9mq2e t21;
            gm_f_format_2d30afk((void *)&t21, (void *)NULL, cx, NULL, t20);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t22 = t21;
            if (((((uint8_t *)&t22))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t22) + 8, gm_srcmeaag4q4qrjl, 597, 14);
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 8));
            gm_l9 = t23;
        }
        {
            void * t24 = gm_l0;
            gm_tfeab279c16rr t25;
            gm_b16_8 t26 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 0, t26);
            gm_b16_8 t27 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 16, t27);
            gm_b16_8 t28 = gm_l9;
            GM_ST(gm_b16_8, (uint8_t *)&t25 + 32, t28);
            gm_b0_1 t29 = gm_f_linef_18bk901((void *)NULL, cx, NULL, t24, &t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_b16_8 t30 = gm_l9;
            gm_b16_8 t31 = t30;
            gm_b16_8 t32 = GM_LD(gm_b16_8, ((uint8_t *)&t31));
            gm_l8 = t32;
        }
        goto ix10;
ie10:;
ix10:;
    }
    {
        gm_b16_8 t33;
        void * t35 = gm_l4;
        void * t36 = t35;
        uint8_t t37;
        if ((GM_LD(void *, ((uint8_t *)&t36)) != 0)) goto mn39;
        t37 = ((uint8_t)0);
        goto me38;
mn39:;
        t37 = ((uint8_t)1);
        goto me38;
mn40:;
        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 601, 25);
me38:;
        if (!t37) goto ie34;
        gm_b16_8 t42 = gm_l8;
        gm_tnpifv20irnph t43;
        GM_ST(gm_b16_8, ((uint8_t *)&t43 + 0), t42);
        gm_tabb710d9mq2e t44;
        gm_f_format_h6b2ofo((void *)&t44, (void *)NULL, cx, NULL, t43);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t45 = t44;
        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 8, gm_srcmeaag4q4qrjl, 601, 43);
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t45) + 8));
        gm_b16_8 t47 = t46;
        gm_b16_8 t48 = t47;
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
        t33 = t49;
        goto ix34;
ie34:;
        gm_b16_8 t51;
        GM_ST(void *, (uint8_t *)&t51, (void *)(gm_s04qus0luoga5));
        GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(4));
        gm_b16_8 t52 = t51;
        t33 = t52;
ix34:;
        gm_l10 = t33;
    }
    {
        void * t53 = gm_l0;
        gm_b16_8 t54 = gm_l3;
        void * t55 = gm_l4;
        gm_t9805qtonbsag t56 = gm_f_nested_spec_po688kb((void *)NULL, cx, NULL, t53, t54, t55);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l11 = t56;
    }
    {
        {
            gm_b16_8 t58 = gm_l3;
            gm_l12 = t58;
        }
        {
            gm_l13 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t60 = gm_l13;
                uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                uint8_t t62 = (t60 < t61);
                if (!t62) goto lb59;
                {
                    uint64_t t65 = gm_l13;
                    uint64_t t66 = (uintptr_t)t65;
                    uint64_t t67 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                    gm_t4lnf71mruqpt t70 = GM_LD(gm_t4lnf71mruqpt, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t66 * 32));
                    gm_l14 = t70;
                    {
                        uint8_t t74 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                        if (t74 != ((uint8_t)0ULL)) goto mn73;
                        void * t75 = GM_LD(void *, (((uint8_t *)&gm_l14) + 8));
                        gm_l15 = t75;
                        {
                            void * t77 = gm_l0;
                            void * t78 = gm_l15;
                            gm_t5o0tpv5jka2l t79;
                            gm_f_expr_porbc9t((void *)&t79, (void *)NULL, cx, NULL, t77, t78);
                            if (gm_unw.kind) {
                                goto gm_ll63;
                            }
                            gm_t5o0tpv5jka2l t80 = t79;
                            if (((((uint8_t *)&t80))[0] != 0)) {
                                gm_t08olq97rscn3 t81 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t80) + 8));
                                gm_t5o0tpv5jka2l t82;
                                GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
                                ((uint8_t *)&t82)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t82 + 8), t81);
                                F.ret = t82;
                                goto gm_fret;
                            }
                            gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t80) + 8));
                            gm_l16 = t83;
                        }
                        {
                            void * t85 = gm_l0;
                            void * t86 = GM_LD(void *, (((uint8_t *)t85) + 0));
                            void * t87 = (void *)(((uint8_t *)t86) + 8);
                            void * t88 = gm_l15;
                            gm_t88rcucg1b7uk t89 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t88) + 0));
                            gm_t53at80pfdus3 t90;
                            gm_f_kind_ghvav22((void *)&t90, t87, GM_CONV(gm_csguankko4lmhd, t89));
                            gm_t53at80pfdus3 t91 = t90;
                            uint8_t t92 = GM_LD(uint8_t, (((uint8_t *)&t91) + 0));
                            if (t92 != ((uint8_t)25ULL)) goto ie84;
                            uint32_t t93 = GM_LD(uint32_t, (((uint8_t *)&t91) + 8));
                            gm_l17 = t93;
                            {
                                gm_b16_8 t95;
                                GM_ST(void *, (uint8_t *)&t95, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)(0));
                                gm_l18 = t95;
                            }
                            {
                                if (!((((uint8_t *)&gm_l11))[4] != 0)) goto ie96;
                                uint32_t t97 = GM_LD(uint32_t, ((uint8_t *)&gm_l11));
                                gm_l19 = t97;
                                {
                                    void * t99 = gm_l0;
                                    void * t100 = GM_LD(void *, (((uint8_t *)t99) + 0));
                                    uint32_t t101 = gm_l17;
                                    uint32_t t102 = gm_l19;
                                    gm_b16_8 t103 = gm_f_spec_handler_table_8h552cp((void *)NULL, cx, NULL, t100, t101, t102);
                                    if (gm_unw.kind) {
                                        goto gm_ll63;
                                    }
                                    gm_l18 = t103;
                                }
                                goto ix96;
ie96:;
                                {
                                    void * t105 = gm_l0;
                                    void * t106 = GM_LD(void *, (((uint8_t *)t105) + 0));
                                    uint32_t t107 = gm_l17;
                                    gm_b16_8 t108 = gm_f_handler_table_2kdtlit((void *)NULL, cx, NULL, t106, t107);
                                    if (gm_unw.kind) {
                                        goto gm_ll63;
                                    }
                                    gm_l18 = t108;
                                }
ix96:;
                            }
                            {
                                void * t109 = gm_l0;
                                void * t110 = GM_LD(void *, (((uint8_t *)t109) + 0));
                                void * t111 = GM_LD(void *, (((uint8_t *)t110) + 0));
                                uint32_t t112 = gm_l17;
                                void * t113 = gm_f_handler_lit_b02boff(t111, t112);
                                gm_tiilmfd16v3fc t114 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)t113) + 0));
                                gm_l20 = t114;
                            }
                            {
                                void * t115 = gm_l0;
                                void * t116 = GM_LD(void *, (((uint8_t *)t115) + 0));
                                void * t117 = (void *)((uint8_t *)&gm_l20);
                                uint32_t t118 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t116, t117);
                                if (gm_unw.kind) {
                                    goto gm_ll63;
                                }
                                gm_l21 = t118;
                            }
                            {
                                void * t119 = gm_l0;
                                uint32_t t120 = gm_f_fresh_5kokjmg(t119);
                                gm_t88rcucg1b7uk t121;
                                GM_ST(uint32_t, ((uint8_t *)&t121 + 0), t120);
                                gm_tabb710d9mq2e t122;
                                gm_f_format_tcvndjh((void *)&t122, (void *)NULL, cx, NULL, t121);
                                if (gm_unw.kind) {
                                    goto gm_ll63;
                                }
                                gm_tabb710d9mq2e t123 = t122;
                                if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 8, gm_srcmeaag4q4qrjl, 612, 22);
                                gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 8));
                                gm_l22 = t124;
                            }
                            {
                                void * t125 = gm_l0;
                                gm_th6ngf7aenlq3 t126;
                                memset((uint8_t *)&t126 + 20, 0, 4);
                                memset((uint8_t *)&t126 + 76, 0, 4);
                                gm_b16_8 t127 = gm_l22;
                                GM_ST(gm_b16_8, (uint8_t *)&t126 + 0, t127);
                                uint32_t t128 = gm_l21;
                                GM_ST(uint32_t, (uint8_t *)&t126 + 16, t128);
                                gm_b16_8 t129 = gm_l18;
                                GM_ST(gm_b16_8, (uint8_t *)&t126 + 24, t129);
                                gm_b16_8 t130 = gm_l16;
                                GM_ST(gm_b16_8, (uint8_t *)&t126 + 40, t130);
                                gm_b16_8 t131 = gm_l16;
                                GM_ST(gm_b16_8, (uint8_t *)&t126 + 56, t131);
                                uint32_t t132 = gm_l2;
                                GM_ST(uint32_t, (uint8_t *)&t126 + 72, t132);
                                gm_b16_8 t133 = gm_l7;
                                GM_ST(gm_b16_8, (uint8_t *)&t126 + 80, t133);
                                gm_b16_8 t134 = gm_l10;
                                GM_ST(gm_b16_8, (uint8_t *)&t126 + 96, t134);
                                gm_b16_8 t135 = gm_l6;
                                GM_ST(gm_b16_8, (uint8_t *)&t126 + 112, t135);
                                gm_b0_1 t136 = gm_f_linef_d2o1cif((void *)NULL, cx, NULL, t125, &t126);
                                if (gm_unw.kind) {
                                    goto gm_ll63;
                                }
                            }
                            {
                                gm_b16_8 t137 = gm_l22;
                                gm_tnpifv20irnph t138;
                                GM_ST(gm_b16_8, ((uint8_t *)&t138 + 0), t137);
                                gm_tabb710d9mq2e t139;
                                gm_f_format_s4f2nfi((void *)&t139, (void *)NULL, cx, NULL, t138);
                                if (gm_unw.kind) {
                                    goto gm_ll63;
                                }
                                gm_tabb710d9mq2e t140 = t139;
                                if (((((uint8_t *)&t140))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t140) + 8, gm_srcmeaag4q4qrjl, 614, 23);
                                gm_b16_8 t141 = GM_LD(gm_b16_8, (((uint8_t *)&t140) + 8));
                                gm_b16_8 t142 = t141;
                                gm_b16_8 t143 = GM_LD(gm_b16_8, ((uint8_t *)&t142));
                                gm_l6 = t143;
                            }
                            goto ix84;
ie84:;
                            {
                                void * t145 = gm_l15;
                                gm_tl874t2atnlh3 t146 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t145) + 72));
                                gm_b16_8 t147;
                                GM_ST(void *, (uint8_t *)&t147, (void *)(gm_sbp38o7rrfqgd));
                                GM_ST(uintptr_t, (uint8_t *)&t147 + 8, (uintptr_t)(18));
                                gm_t08olq97rscn3 t148;
                                gm_f_unsupported_531q51r((void *)&t148, (void *)NULL, cx, NULL, t146, t147);
                                if (gm_unw.kind) {
                                    goto gm_ll63;
                                }
                                gm_t5o0tpv5jka2l t149;
                                GM_ST(uint8_t, (uint8_t *)&t149 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t149 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t149 + 4, 0);
                                ((uint8_t *)&t149)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t149 + 8), t148);
                                gm_t5o0tpv5jka2l t150 = t149;
                                uint8_t t151 = ((((uint8_t *)&t150))[0] != 0);
                                F.ret = t149;
                                goto gm_fret;
                            }
ix84:;
                        }
                        goto me72;
mn73:;
                        uint8_t t153 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
                        if (t153 != ((uint8_t)1ULL)) goto mn152;
                        gm_b16_8 t154 = GM_LD(gm_b16_8, ((((uint8_t *)&gm_l14) + 8) + 0));
                        gm_l23 = t154;
                        void * t155 = GM_LD(void *, ((((uint8_t *)&gm_l14) + 8) + 16));
                        gm_l24 = t155;
                        {
                            void * t157 = gm_l0;
                            void * t158 = gm_l24;
                            gm_t5o0tpv5jka2l t159;
                            gm_f_expr_porbc9t((void *)&t159, (void *)NULL, cx, NULL, t157, t158);
                            if (gm_unw.kind) {
                                goto gm_ll63;
                            }
                            gm_t5o0tpv5jka2l t160 = t159;
                            if (((((uint8_t *)&t160))[0] != 0)) {
                                gm_t08olq97rscn3 t161 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t160) + 8));
                                gm_t5o0tpv5jka2l t162;
                                GM_ST(uint8_t, (uint8_t *)&t162 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t162 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t162 + 4, 0);
                                ((uint8_t *)&t162)[0] = 1;
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t162 + 8), t161);
                                F.ret = t162;
                                goto gm_fret;
                            }
                            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&t160) + 8));
                            gm_l25 = t163;
                        }
                        {
                            void * t164 = gm_l0;
                            void * t165 = gm_l24;
                            gm_t88rcucg1b7uk t166 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t165) + 0));
                            gm_b16_8 t167 = gm_l25;
                            gm_b16_8 t168 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t164, t166, t167);
                            if (gm_unw.kind) {
                                goto gm_ll63;
                            }
                            gm_l26 = t168;
                        }
                        {
                            gm_b16_8 t169 = gm_l6;
                            gm_l27 = t169;
                        }
                        {
                            {
                                gm_b16_8 t171 = gm_l23;
                                gm_l28 = t171;
                            }
                            {
                                gm_l29 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t173 = gm_l29;
                                    uint64_t t174 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                    uint8_t t175 = (t173 < t174);
                                    if (!t175) goto lb172;
                                    {
                                        uint64_t t178 = gm_l29;
                                        uint64_t t179 = (uintptr_t)t178;
                                        uint64_t t180 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                                        uint32_t t181 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t179 * 4));
                                        uint32_t t182 = t181;
                                        uint32_t t185 = GM_LD(uint32_t, ((uint8_t *)&t182));
                                        gm_l30 = t185;
                                        {
                                            void * t187 = gm_l0;
                                            void * t188 = GM_LD(void *, (((uint8_t *)t187) + 0));
                                            uint32_t t189 = gm_l30;
                                            gm_t5o0tpv5jka2l t190;
                                            gm_f_decl_table_l7u1k0g((void *)&t190, (void *)NULL, cx, NULL, t188, t189);
                                            if (gm_unw.kind) {
                                                goto gm_ll176;
                                            }
                                            gm_t5o0tpv5jka2l t191 = t190;
                                            if (((((uint8_t *)&t191))[0] != 0)) {
                                                gm_t08olq97rscn3 t192 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t191) + 8));
                                                gm_t5o0tpv5jka2l t193;
                                                GM_ST(uint8_t, (uint8_t *)&t193 + 1, 0);
                                                GM_ST(uint16_t, (uint8_t *)&t193 + 2, 0);
                                                GM_ST(uint32_t, (uint8_t *)&t193 + 4, 0);
                                                ((uint8_t *)&t193)[0] = 1;
                                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t193 + 8), t192);
                                                F.ret = t193;
                                                goto gm_fret;
                                            }
                                            gm_b16_8 t194 = GM_LD(gm_b16_8, (((uint8_t *)&t191) + 8));
                                            gm_l31 = t194;
                                        }
                                        {
                                            void * t195 = gm_l0;
                                            void * t196 = GM_LD(void *, (((uint8_t *)t195) + 0));
                                            void * t197 = GM_LD(void *, (((uint8_t *)t196) + 0));
                                            uint32_t t198 = gm_l30;
                                            void * t199 = gm_f_handler_decl_m66mgr8(t197, t198);
                                            gm_tiilmfd16v3fc t200 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)t199) + 0));
                                            gm_l32 = t200;
                                        }
                                        {
                                            void * t201 = gm_l0;
                                            void * t202 = GM_LD(void *, (((uint8_t *)t201) + 0));
                                            void * t203 = (void *)((uint8_t *)&gm_l32);
                                            uint32_t t204 = gm_f_effect_id_5588nqa((void *)NULL, cx, NULL, t202, t203);
                                            if (gm_unw.kind) {
                                                goto gm_ll176;
                                            }
                                            gm_l33 = t204;
                                        }
                                        {
                                            void * t205 = gm_l0;
                                            uint32_t t206 = gm_f_fresh_5kokjmg(t205);
                                            gm_t88rcucg1b7uk t207;
                                            GM_ST(uint32_t, ((uint8_t *)&t207 + 0), t206);
                                            gm_tabb710d9mq2e t208;
                                            gm_f_format_tcvndjh((void *)&t208, (void *)NULL, cx, NULL, t207);
                                            if (gm_unw.kind) {
                                                goto gm_ll176;
                                            }
                                            gm_tabb710d9mq2e t209 = t208;
                                            if (((((uint8_t *)&t209))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t209) + 8, gm_srcmeaag4q4qrjl, 624, 26);
                                            gm_b16_8 t210 = GM_LD(gm_b16_8, (((uint8_t *)&t209) + 8));
                                            gm_l34 = t210;
                                        }
                                        {
                                            void * t211 = gm_l0;
                                            gm_th6ngf7aenlq3 t212;
                                            memset((uint8_t *)&t212 + 20, 0, 4);
                                            memset((uint8_t *)&t212 + 76, 0, 4);
                                            gm_b16_8 t213 = gm_l34;
                                            GM_ST(gm_b16_8, (uint8_t *)&t212 + 0, t213);
                                            uint32_t t214 = gm_l33;
                                            GM_ST(uint32_t, (uint8_t *)&t212 + 16, t214);
                                            gm_b16_8 t215 = gm_l31;
                                            GM_ST(gm_b16_8, (uint8_t *)&t212 + 24, t215);
                                            gm_b16_8 t216 = gm_l26;
                                            GM_ST(gm_b16_8, (uint8_t *)&t212 + 40, t216);
                                            gm_b16_8 t217 = gm_l27;
                                            GM_ST(gm_b16_8, (uint8_t *)&t212 + 56, t217);
                                            uint32_t t218 = gm_l2;
                                            GM_ST(uint32_t, (uint8_t *)&t212 + 72, t218);
                                            gm_b16_8 t219 = gm_l7;
                                            GM_ST(gm_b16_8, (uint8_t *)&t212 + 80, t219);
                                            gm_b16_8 t220 = gm_l10;
                                            GM_ST(gm_b16_8, (uint8_t *)&t212 + 96, t220);
                                            gm_b16_8 t221 = gm_l6;
                                            GM_ST(gm_b16_8, (uint8_t *)&t212 + 112, t221);
                                            gm_b0_1 t222 = gm_f_linef_arl1b28((void *)NULL, cx, NULL, t211, &t212);
                                            if (gm_unw.kind) {
                                                goto gm_ll176;
                                            }
                                        }
                                        {
                                            gm_b16_8 t223 = gm_l34;
                                            gm_tnpifv20irnph t224;
                                            GM_ST(gm_b16_8, ((uint8_t *)&t224 + 0), t223);
                                            gm_tabb710d9mq2e t225;
                                            gm_f_format_s4f2nfi((void *)&t225, (void *)NULL, cx, NULL, t224);
                                            if (gm_unw.kind) {
                                                goto gm_ll176;
                                            }
                                            gm_tabb710d9mq2e t226 = t225;
                                            if (((((uint8_t *)&t226))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t226) + 8, gm_srcmeaag4q4qrjl, 626, 27);
                                            gm_b16_8 t227 = GM_LD(gm_b16_8, (((uint8_t *)&t226) + 8));
                                            gm_b16_8 t228 = t227;
                                            gm_b16_8 t229 = GM_LD(gm_b16_8, ((uint8_t *)&t228));
                                            gm_l6 = t229;
                                        }
                                        goto me183;
mn184:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 620, 17);
me183:;
                                    }
lc172:;
                                    {
                                        uint64_t t231 = gm_l29;
                                        uint64_t r232; bool o233 = __builtin_add_overflow(t231, ((uint64_t)1ULL), &r232);
                                        uint64_t t234 = r232;
                                        gm_l29 = t234;
                                    }
                                }
gm_ll176:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb172; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc172; }
                                }
                                goto gm_ll63;
lb172:;
                            }
                        }
                        goto me72;
mn152:;
                        gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 604, 9);
me72:;
                    }
                    goto me68;
mn69:;
                    gm_trap("no match arm matched the scrutinee", gm_srcmeaag4q4qrjl, 603, 5);
me68:;
                }
lc59:;
                {
                    uint64_t t236 = gm_l13;
                    uint64_t r237; bool o238 = __builtin_add_overflow(t236, ((uint64_t)1ULL), &r237);
                    uint64_t t239 = r237;
                    gm_l13 = t239;
                }
            }
gm_ll63:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb59; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc59; }
            }
            goto gm_lad;
lb59:;
        }
    }
    {
        void * t241 = gm_l4;
        void * t242 = t241;
        if (!(GM_LD(void *, ((uint8_t *)&t242)) != 0)) goto ie240;
        void * t243 = GM_LD(void *, ((uint8_t *)&t242));
        gm_l35 = t243;
        {
            void * t245 = gm_l0;
            void * t246 = gm_l1;
            uint32_t t247 = gm_l2;
            gm_b16_8 t248 = gm_l8;
            gm_b16_8 t249 = gm_l6;
            void * t250 = gm_l35;
            void * t251 = gm_l5;
            gm_f_with_on_fl0pjo9((void *)&(*(gm_t5o0tpv5jka2l *)gm_out), (void *)NULL, cx, NULL, t245, t246, t247, t248, t249, t250, t251);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto ix240;
ie240:;
ix240:;
    }
    {
        void * t252 = gm_l0;
        gm_b16_8 t253 = GM_LD(gm_b16_8, (((uint8_t *)t252) + 280));
        gm_l36 = t253;
    }
    {
        void * t254 = gm_l0;
        gm_b16_8 t255 = gm_l6;
        GM_ST(gm_b16_8, (((uint8_t *)t254) + 280), t255);
    }
    {
        void * t256 = gm_l0;
        uint32_t t257 = gm_f_fresh_5kokjmg(t256);
        gm_t88rcucg1b7uk t258;
        GM_ST(uint32_t, ((uint8_t *)&t258 + 0), t257);
        gm_tabb710d9mq2e t259;
        gm_f_format_dn5pqnq((void *)&t259, (void *)NULL, cx, NULL, t258);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t260 = t259;
        if (((((uint8_t *)&t260))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t260) + 8, gm_srcmeaag4q4qrjl, 634, 12);
        gm_b16_8 t261 = GM_LD(gm_b16_8, (((uint8_t *)&t260) + 8));
        gm_l37 = t261;
    }
    {
        void * t262 = gm_l0;
        uint32_t t263 = gm_l2;
        gm_b16_8 t264 = gm_l37;
        gm_b16_8 t265 = t264;
        gm_b16_8 t266 = GM_LD(gm_b16_8, ((uint8_t *)&t265));
        gm_f_with_scope_o670qbc((void *)&F.l38, (void *)NULL, cx, NULL, t262, t263, t266);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t267 = gm_l0;
        void * t268 = (void *)(((uint8_t *)t267) + 240);
        gm_tftb1odc739t5 t269 = gm_f_push_pcqb56b(t268, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l38)));
        gm_tftb1odc739t5 t270 = t269;
        if (((((uint8_t *)&t270))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t270) + 1, gm_srcmeaag4q4qrjl, 636, 5);
        gm_b0_1 t271 = GM_LD(gm_b0_1, (((uint8_t *)&t270) + 1));
    }
    {
        void * t272 = gm_l0;
        gm_t5lf9i3n1r3co t273 = GM_LD(gm_t5lf9i3n1r3co, (((uint8_t *)t272) + 40));
        gm_l39 = t273;
    }
    {
        void * t274 = gm_l0;
        uint8_t *t275 = (((uint8_t *)t274) + 40);
        void * t276 = gm_l0;
        gm_b16_8 t277 = gm_l3;
        void * t278 = gm_l5;
        gm_t9805qtonbsag t279 = gm_l11;
        gm_f_specializable_7or9fsf((void *)&(*(gm_t5lf9i3n1r3co *)t275), (void *)NULL, cx, NULL, t276, t277, t278, t279);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t280 = gm_l0;
        void * t281 = gm_l5;
        gm_f_expr_porbc9t((void *)&F.l40, (void *)NULL, cx, NULL, t280, t281);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t282 = gm_l0;
        gm_t5lf9i3n1r3co t283 = gm_l39;
        GM_ST(gm_t5lf9i3n1r3co, (((uint8_t *)t282) + 40), t283);
    }
    {
        void * t284 = gm_l0;
        void * t285 = (void *)(((uint8_t *)t284) + 240);
        gm_t1g28mkc78m0c t286;
        gm_f_pop_g2usrvd((void *)&t286, t285);
        gm_t1g28mkc78m0c t287 = t286;
        if (!((((uint8_t *)&t287))[104] != 0)) gm_trap("unwrap of null", gm_srcmeaag4q4qrjl, 641, 15);
        gm_tehgl6lkqgvt0 t288 = GM_LD(gm_tehgl6lkqgvt0, ((uint8_t *)&t287));
        F.l41 = t288;
    }
    {
        void * t289 = gm_l0;
        gm_b16_8 t290 = gm_l36;
        GM_ST(gm_b16_8, (((uint8_t *)t289) + 280), t290);
    }
    {
        gm_t5o0tpv5jka2l t291 = F.l40;
        gm_t5o0tpv5jka2l t292 = t291;
        if (((((uint8_t *)&t292))[0] != 0)) {
            gm_t08olq97rscn3 t293 = GM_LD(gm_t08olq97rscn3, (((uint8_t *)&t292) + 8));
            gm_t5o0tpv5jka2l t294;
            GM_ST(uint8_t, (uint8_t *)&t294 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t294 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
            ((uint8_t *)&t294)[0] = 1;
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t294 + 8), t293);
            F.ret = t294;
            goto gm_fret;
        }
        gm_b16_8 t295 = GM_LD(gm_b16_8, (((uint8_t *)&t292) + 8));
        gm_l42 = t295;
    }
    {
        void * t297 = gm_l0;
        void * t298 = GM_LD(void *, (((uint8_t *)t297) + 0));
        void * t299 = (void *)(((uint8_t *)t298) + 8);
        void * t300 = gm_l5;
        gm_t88rcucg1b7uk t301 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t300) + 0));
        uint8_t t302 = gm_f_is_never_in5ftma(t299, GM_CONV(gm_csguankko4lmhd, t301));
        uint8_t t303 = (uint8_t)!t302;
        if (!t303) goto ie296;
        void * t304 = gm_l0;
        void * t305 = GM_LD(void *, (((uint8_t *)t304) + 0));
        void * t306 = (void *)(((uint8_t *)t305) + 8);
        void * t307 = gm_l1;
        gm_t88rcucg1b7uk t308 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t307) + 0));
        uint8_t t309 = gm_f_is_never_in5ftma(t306, GM_CONV(gm_csguankko4lmhd, t308));
        uint8_t t310 = (uint8_t)!t309;
        if (!t310) goto ie296;
        {
            void * t312 = gm_l0;
            gm_tiilmfd16v3fc t313;
            memset((uint8_t *)&t313 + 4, 0, 4);
            uint32_t t314 = gm_l2;
            GM_ST(uint32_t, (uint8_t *)&t313 + 0, t314);
            gm_b16_8 t315 = gm_l42;
            GM_ST(gm_b16_8, (uint8_t *)&t313 + 8, t315);
            gm_b0_1 t316 = gm_f_linef_m4ia8qg((void *)NULL, cx, NULL, t312, &t313);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix296;
ie296:;
ix296:;
    }
    {
        void * t317 = gm_l0;
        gm_b0_1 t318 = gm_f_end_scope_45ar5qv((void *)NULL, cx, NULL, t317, &(*(gm_tehgl6lkqgvt0 *)((uint8_t *)&F.l41)), ((uint8_t)1));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t319 = gm_l0;
        gm_b16_8 t320 = gm_l37;
        gm_b16_8 t321 = t320;
        gm_b16_8 t322 = GM_LD(gm_b16_8, ((uint8_t *)&t321));
        gm_b0_1 t323 = gm_f_label_710oev4((void *)NULL, cx, NULL, t319, t322);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t325 = gm_l0;
        void * t326 = GM_LD(void *, (((uint8_t *)t325) + 0));
        void * t327 = (void *)(((uint8_t *)t326) + 8);
        void * t328 = gm_l1;
        gm_t88rcucg1b7uk t329 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t328) + 0));
        uint8_t t330 = gm_f_is_never_in5ftma(t327, GM_CONV(gm_csguankko4lmhd, t329));
        if (!t330) goto ie324;
        {
            void * t332 = gm_l0;
            void * t333 = gm_l1;
            gm_t88rcucg1b7uk t334 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t333) + 0));
            gm_b16_8 t335 = gm_f_zero_tlk13aa((void *)NULL, cx, NULL, t332, t334);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t5o0tpv5jka2l t336;
            GM_ST(uint8_t, (uint8_t *)&t336 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t336 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t336 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t336 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t336 + 32, 0);
            ((uint8_t *)&t336)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t336 + 8), t335);
            gm_t5o0tpv5jka2l t337 = t336;
            uint8_t t338 = ((((uint8_t *)&t337))[0] != 0);
            F.ret = t336;
            goto gm_fret;
        }
        goto ix324;
ie324:;
ix324:;
    }
    {
        void * t339 = gm_l0;
        void * t340 = gm_l1;
        gm_t88rcucg1b7uk t341 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t340) + 0));
        uint32_t t342 = gm_l2;
        gm_t88rcucg1b7uk t343;
        GM_ST(uint32_t, ((uint8_t *)&t343 + 0), t342);
        gm_tabb710d9mq2e t344;
        gm_f_format_rslrdtj((void *)&t344, (void *)NULL, cx, NULL, t343);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t345 = t344;
        if (((((uint8_t *)&t345))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t345) + 8, gm_srcmeaag4q4qrjl, 650, 25);
        gm_b16_8 t346 = GM_LD(gm_b16_8, (((uint8_t *)&t345) + 8));
        gm_b16_8 t347 = t346;
        gm_b16_8 t348 = GM_LD(gm_b16_8, ((uint8_t *)&t347));
        gm_b16_8 t349 = gm_f_tmp_uohsptq((void *)NULL, cx, NULL, t339, t341, t348);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t5o0tpv5jka2l t350;
        GM_ST(uint8_t, (uint8_t *)&t350 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t350 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t350 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t350 + 24, 0);
        GM_ST(uint64_t, (uint8_t *)&t350 + 32, 0);
        ((uint8_t *)&t350)[0] = 0;
        GM_ST(gm_b16_8, ((uint8_t *)&t350 + 8), t349);
        gm_t5o0tpv5jka2l t351 = t350;
        uint8_t t352 = ((((uint8_t *)&t351))[0] != 0);
        F.ret = t350;
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

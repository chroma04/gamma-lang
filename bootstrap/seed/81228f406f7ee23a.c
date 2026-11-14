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
typedef struct gm_cs9ufftihtbpo2 { uint32_t f0; uint32_t f1; } gm_cs9ufftihtbpo2;
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; uint64_t f3; } gm_t66j1im9ip128;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; void * f1; void * f2; } gm_tbbqefuhv2r8h;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; } gm_tgtub0v94d5u5;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
_Static_assert(sizeof(gm_cs9ufftihtbpo2) == 8 && _Alignof(gm_cs9ufftihtbpo2) == 4 && offsetof(gm_cs9ufftihtbpo2, f0) == 0 && offsetof(gm_cs9ufftihtbpo2, f1) == 4, "layout of `LineCol`");
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
_Static_assert(sizeof(gm_t66j1im9ip128) == 32 && _Alignof(gm_t66j1im9ip128) == 8 && offsetof(gm_t66j1im9ip128, f0) == 0 && offsetof(gm_t66j1im9ip128, f1) == 8 && offsetof(gm_t66j1im9ip128, f2) == 16 && offsetof(gm_t66j1im9ip128, f3) == 24, "layout of `(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t79rbdv4rafe5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
_Static_assert(sizeof(gm_tbbqefuhv2r8h) == 24 && _Alignof(gm_tbbqefuhv2r8h) == 8 && offsetof(gm_tbbqefuhv2r8h, f0) == 0 && offsetof(gm_tbbqefuhv2r8h, f1) == 8 && offsetof(gm_tbbqefuhv2r8h, f2) == 16, "layout of `Arm`");
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { gm_t66j1im9ip128 f0; uint8_t f1; uint8_t pe[7]; } gm_td8hfgklo8dil;
typedef struct { gm_b16_8 f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tdu0qimd4ae4c;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tep2drsl9408b;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tgtub0v94d5u5) == 16 && _Alignof(gm_tgtub0v94d5u5) == 8 && offsetof(gm_tgtub0v94d5u5, f0) == 0 && offsetof(gm_tgtub0v94d5u5, f1) == 8, "layout of `(is_const: bool, inner: ^Expr)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_t9qkh0rsooc8d f1; uint8_t pe[7]; } gm_tibupt4dmcqai;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { void * f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tlljg61a2ctgq;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; gm_t3mf58gv9kf3n v1; } f1; } gm_tlr9dp305htm7;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t f1; uint8_t pe[3]; } gm_toceffb697p0u;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; void * f1; } gm_tpe1hv3fekn05;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_ts78ca2amthgu;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
typedef struct { void * f0; uint32_t f1; gm_tl874t2atnlh3 f2; } gm_tupk62cr6lbdu;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
typedef struct { gm_t08olq97rscn3 f0; uint8_t f1; uint8_t pe[7]; } gm_t1s75i08qtptd;
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; gm_b16_8 f1; gm_tl874t2atnlh3 f2; uint8_t p3[4]; gm_trqfvmjtt4sij f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; gm_th2lqbo77pvsc f6; } gm_t4gut2c9fj1kt;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t79rbdv4rafe5) == 40 && _Alignof(gm_t79rbdv4rafe5) == 8 && offsetof(gm_t79rbdv4rafe5, f0) == 0 && offsetof(gm_t79rbdv4rafe5, f1) == 8 && offsetof(gm_t79rbdv4rafe5, f2) == 24, "layout of `Block`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { void * f0; gm_th2lqbo77pvsc f1; } gm_tagc77imt2chm;
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
_Static_assert(sizeof(gm_td8hfgklo8dil) == 40 && _Alignof(gm_td8hfgklo8dil) == 8 && offsetof(gm_td8hfgklo8dil, f0) == 0 && offsetof(gm_td8hfgklo8dil, f1) == 32, "layout of `?(types: usize, values: usize, fns: usize, consts: usize)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
typedef struct { gm_tep2drsl9408b f0; uint8_t f1; uint8_t pe[7]; } gm_tdlpv0q69lov1;
typedef struct { uint64_t f0; gm_tbgd14esf0jvi f1; } gm_tdlq2ugehok8g;
_Static_assert(sizeof(gm_tdu0qimd4ae4c) == 32 && _Alignof(gm_tdu0qimd4ae4c) == 8 && offsetof(gm_tdu0qimd4ae4c, f0) == 0 && offsetof(gm_tdu0qimd4ae4c, f1) == 16 && offsetof(gm_tdu0qimd4ae4c, f2) == 20, "layout of `PatBind`");
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tep2drsl9408b) == 24 && _Alignof(gm_tep2drsl9408b) == 8 && offsetof(gm_tep2drsl9408b, f0) == 0 && offsetof(gm_tep2drsl9408b, f1) == 16, "layout of `ValTy`");
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
typedef struct { gm_t08olq97rscn3 f0; gm_th2lqbo77pvsc f1; } gm_tet9guhev5r4u;
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
_Static_assert(sizeof(gm_tibupt4dmcqai) == 24 && _Alignof(gm_tibupt4dmcqai) == 8 && offsetof(gm_tibupt4dmcqai, f0) == 0 && offsetof(gm_tibupt4dmcqai, f1) == 16, "layout of `StrLit`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { void * f0; gm_t08olq97rscn3 f1; } gm_tioie2ju3q4ul;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlljg61a2ctgq) == 32 && _Alignof(gm_tlljg61a2ctgq) == 8 && offsetof(gm_tlljg61a2ctgq, f0) == 0 && offsetof(gm_tlljg61a2ctgq, f1) == 8 && offsetof(gm_tlljg61a2ctgq, f2) == 24, "layout of `(callee: ^Expr, args: []Arg, trailing_comma: bool)`");
_Static_assert(sizeof(gm_tlr9dp305htm7) == 24 && _Alignof(gm_tlr9dp305htm7) == 8 && offsetof(gm_tlr9dp305htm7, f0) == 0 && offsetof(gm_tlr9dp305htm7, f1) == 8, "layout of `CondPart`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_toceffb697p0u) == 16 && _Alignof(gm_toceffb697p0u) == 4 && offsetof(gm_toceffb697p0u, f0) == 0 && offsetof(gm_toceffb697p0u, f1) == 12, "layout of `?Span`");
_Static_assert(sizeof(gm_tpe1hv3fekn05) == 24 && _Alignof(gm_tpe1hv3fekn05) == 8 && offsetof(gm_tpe1hv3fekn05, f0) == 0 && offsetof(gm_tpe1hv3fekn05, f1) == 16, "layout of `(attrs: []Attribute, inner: ^Expr)`");
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
typedef struct { gm_trqfvmjtt4sij f0; uint8_t f1; uint8_t pe[7]; } gm_tqetrgo0q24b5;
typedef struct { gm_th2lqbo77pvsc f0; gm_b16_8 f1; gm_toceffb697p0u f2; } gm_tqs4ilbobv1qq;
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
_Static_assert(sizeof(gm_ts78ca2amthgu) == 40 && _Alignof(gm_ts78ca2amthgu) == 8 && offsetof(gm_ts78ca2amthgu, f0) == 0 && offsetof(gm_ts78ca2amthgu, f1) == 8 && offsetof(gm_ts78ca2amthgu, f2) == 24, "layout of `(ty: Ty, name: string, span: Span)`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
_Static_assert(sizeof(gm_tupk62cr6lbdu) == 24 && _Alignof(gm_tupk62cr6lbdu) == 8 && offsetof(gm_tupk62cr6lbdu, f0) == 0 && offsetof(gm_tupk62cr6lbdu, f1) == 8 && offsetof(gm_tupk62cr6lbdu, f2) == 12, "layout of `(base: ^Expr, index: u32, index_span: Span)`");
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t1s75i08qtptd) == 40 && _Alignof(gm_t1s75i08qtptd) == 8 && offsetof(gm_t1s75i08qtptd, f0) == 0 && offsetof(gm_t1s75i08qtptd, f1) == 32, "layout of `?Ident`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4gut2c9fj1kt) == 184 && _Alignof(gm_t4gut2c9fj1kt) == 8 && offsetof(gm_t4gut2c9fj1kt, f0) == 0 && offsetof(gm_t4gut2c9fj1kt, f1) == 8 && offsetof(gm_t4gut2c9fj1kt, f2) == 24 && offsetof(gm_t4gut2c9fj1kt, f3) == 40 && offsetof(gm_t4gut2c9fj1kt, f4) == 80 && offsetof(gm_t4gut2c9fj1kt, f5) == 120 && offsetof(gm_t4gut2c9fj1kt, f6) == 160, "layout of `Diagnostic`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; uint8_t f2; uint8_t pe[7]; } gm_t59juu943t8sl;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tagc77imt2chm) == 32 && _Alignof(gm_tagc77imt2chm) == 8 && offsetof(gm_tagc77imt2chm, f0) == 0 && offsetof(gm_tagc77imt2chm, f1) == 8, "layout of `PathPattern`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
typedef struct { gm_t1s75i08qtptd f0; void * f1; } gm_tchr52g0ema53;
typedef struct { gm_b16_8 f0; gm_tkn42u7na3t81 f1; gm_tbci5ospk5nnn f2; } gm_tckdobunaulee;
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_tdlpv0q69lov1) == 32 && _Alignof(gm_tdlpv0q69lov1) == 8 && offsetof(gm_tdlpv0q69lov1, f0) == 0 && offsetof(gm_tdlpv0q69lov1, f1) == 24, "layout of `?ValTy`");
_Static_assert(sizeof(gm_tdlq2ugehok8g) == 40 && _Alignof(gm_tdlq2ugehok8g) == 8 && offsetof(gm_tdlq2ugehok8g, f0) == 0 && offsetof(gm_tdlq2ugehok8g, f1) == 8, "layout of `(i: usize, x: Edit)`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tet9guhev5r4u) == 56 && _Alignof(gm_tet9guhev5r4u) == 8 && offsetof(gm_tet9guhev5r4u, f0) == 0 && offsetof(gm_tet9guhev5r4u, f1) == 32, "layout of `IntrinsicExpr`");
typedef struct { void * f0; gm_t1s75i08qtptd f1; void * f2; } gm_tioi3jh9v22g5;
_Static_assert(sizeof(gm_tioie2ju3q4ul) == 40 && _Alignof(gm_tioie2ju3q4ul) == 8 && offsetof(gm_tioie2ju3q4ul, f0) == 0 && offsetof(gm_tioie2ju3q4ul, f1) == 8, "layout of `(base: ^Expr, name: Ident)`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t08olq97rscn3 v0; gm_t08olq97rscn3 v1; void * v2; gm_tlcl4u558l6p5 v3; void * v4; gm_tagc77imt2chm v5; gm_b16_8 v6; } f1; } gm_to361lgknsu69;
_Static_assert(sizeof(gm_tqetrgo0q24b5) == 48 && _Alignof(gm_tqetrgo0q24b5) == 8 && offsetof(gm_tqetrgo0q24b5, f0) == 0 && offsetof(gm_tqetrgo0q24b5, f1) == 40, "layout of `?List(usize)`");
_Static_assert(sizeof(gm_tqs4ilbobv1qq) == 56 && _Alignof(gm_tqs4ilbobv1qq) == 8 && offsetof(gm_tqs4ilbobv1qq, f0) == 0 && offsetof(gm_tqs4ilbobv1qq, f1) == 24 && offsetof(gm_tqs4ilbobv1qq, f2) == 40, "layout of `MatchReport`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_to361lgknsu69 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t0d02142k4tu8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { void * v0; void * v1; gm_tibupt4dmcqai v2; uint8_t v3; gm_t08olq97rscn3 v4; gm_t08olq97rscn3 v5; void * v6; gm_b16_8 v7; void * v8; gm_t8ghgl2jj0qha v9; gm_trl9d7m1k5lbd v10; gm_tioi3jh9v22g5 v11; void * v12; void * v13; gm_t3mf58gv9kf3n v14; void * v15; gm_tlljg61a2ctgq v16; gm_t3mf58gv9kf3n v17; gm_tao63pk3r18v5 v18; gm_tioie2ju3q4ul v19; gm_tupk62cr6lbdu v20; void * v21; gm_tgtub0v94d5u5 v22; gm_tgtub0v94d5u5 v23; gm_tgtub0v94d5u5 v24; gm_t3mf58gv9kf3n v25; gm_t3mf58gv9kf3n v26; void * v27; void * v28; void * v29; void * v30; void * v31; gm_tpe1hv3fekn05 v32; void * v33; void * v34; gm_tibupt4dmcqai v35; void * v36; void * v37; void * v38; void * v39; void * v40; gm_t79rbdv4rafe5 v41; void * v42; gm_tchr52g0ema53 v43; gm_t1s75i08qtptd v44; gm_t3mf58gv9kf3n v45; gm_tet9guhev5r4u v46; } f1; } gm_t2gtbh39ptekd;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t59juu943t8sl) == 104 && _Alignof(gm_t59juu943t8sl) == 8 && offsetof(gm_t59juu943t8sl, f0) == 0 && offsetof(gm_t59juu943t8sl, f1) == 48 && offsetof(gm_t59juu943t8sl, f2) == 96, "layout of `(lo: Int, hi: Int, inclusive: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; void * v1; gm_t59juu943t8sl v2; void * v3; void * v4; void * v5; gm_ttggq7ng4kp3p v6; gm_b16_8 v7; gm_b16_8 v8; } f1; } gm_tb38v5s9j1r32;
_Static_assert(sizeof(gm_tchr52g0ema53) == 48 && _Alignof(gm_tchr52g0ema53) == 8 && offsetof(gm_tchr52g0ema53, f0) == 0 && offsetof(gm_tchr52g0ema53, f1) == 40, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_tckdobunaulee) == 72 && _Alignof(gm_tckdobunaulee) == 8 && offsetof(gm_tckdobunaulee, f0) == 0 && offsetof(gm_tckdobunaulee, f1) == 16 && offsetof(gm_tckdobunaulee, f2) == 24, "layout of `VariantDef`");
_Static_assert(sizeof(gm_tioi3jh9v22g5) == 56 && _Alignof(gm_tioi3jh9v22g5) == 8 && offsetof(gm_tioi3jh9v22g5, f0) == 0 && offsetof(gm_tioi3jh9v22g5, f1) == 8 && offsetof(gm_tioi3jh9v22g5, f2) == 48, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
_Static_assert(sizeof(gm_to361lgknsu69) == 40 && _Alignof(gm_to361lgknsu69) == 8 && offsetof(gm_to361lgknsu69, f0) == 0 && offsetof(gm_to361lgknsu69, f1) == 8, "layout of `PatternKind`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; gm_tngtuiuskn938 f1; } gm_tu15p0hi2qlv6;
_Static_assert(sizeof(gm_t0d02142k4tu8) == 64 && _Alignof(gm_t0d02142k4tu8) == 8 && offsetof(gm_t0d02142k4tu8, f0) == 0 && offsetof(gm_t0d02142k4tu8, f1) == 8 && offsetof(gm_t0d02142k4tu8, f2) == 48, "layout of `Pattern`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; gm_tngtuiuskn938 v1; uint8_t v2; gm_tu15p0hi2qlv6 v3; } f1; } gm_t0ialelq3ql1k;
_Static_assert(sizeof(gm_t2gtbh39ptekd) == 64 && _Alignof(gm_t2gtbh39ptekd) == 8 && offsetof(gm_t2gtbh39ptekd, f0) == 0 && offsetof(gm_t2gtbh39ptekd, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tb38v5s9j1r32) == 112 && _Alignof(gm_tb38v5s9j1r32) == 8 && offsetof(gm_tb38v5s9j1r32, f0) == 0 && offsetof(gm_tb38v5s9j1r32, f1) == 8, "layout of `PatKind`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_t2gtbh39ptekd f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc3oht4ksth9d;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_tb38v5s9j1r32 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tc9qv69frb08e;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
typedef struct { gm_t1s75i08qtptd f0; gm_t0d02142k4tu8 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_tnrgict8sthcv;
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
typedef struct { gm_t0d02142k4tu8 f0; uint8_t f1; uint8_t pe[7]; } gm_toqf51949ppls;
_Static_assert(sizeof(gm_tu15p0hi2qlv6) == 192 && _Alignof(gm_tu15p0hi2qlv6) == 8 && offsetof(gm_tu15p0hi2qlv6, f0) == 0 && offsetof(gm_tu15p0hi2qlv6, f1) == 96, "layout of `(re: Rat, im: Rat)`");
_Static_assert(sizeof(gm_t0ialelq3ql1k) == 200 && _Alignof(gm_t0ialelq3ql1k) == 8 && offsetof(gm_t0ialelq3ql1k, f0) == 0 && offsetof(gm_t0ialelq3ql1k, f1) == 8, "layout of `Untyped`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t0ialelq3ql1k v1; uint32_t v2; gm_ts78ca2amthgu v3; gm_t88rcucg1b7uk v4; } f1; } gm_t930q5l38hdap;
_Static_assert(sizeof(gm_tc3oht4ksth9d) == 88 && _Alignof(gm_tc3oht4ksth9d) == 8 && offsetof(gm_tc3oht4ksth9d, f0) == 0 && offsetof(gm_tc3oht4ksth9d, f1) == 8 && offsetof(gm_tc3oht4ksth9d, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_tc9qv69frb08e) == 136 && _Alignof(gm_tc9qv69frb08e) == 8 && offsetof(gm_tc9qv69frb08e, f0) == 0 && offsetof(gm_tc9qv69frb08e, f1) == 8 && offsetof(gm_tc9qv69frb08e, f2) == 120, "layout of `Pat`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
_Static_assert(sizeof(gm_tnrgict8sthcv) == 120 && _Alignof(gm_tnrgict8sthcv) == 8 && offsetof(gm_tnrgict8sthcv, f0) == 0 && offsetof(gm_tnrgict8sthcv, f1) == 40 && offsetof(gm_tnrgict8sthcv, f2) == 104, "layout of `PatArg`");
_Static_assert(sizeof(gm_toqf51949ppls) == 72 && _Alignof(gm_toqf51949ppls) == 8 && offsetof(gm_toqf51949ppls, f0) == 0 && offsetof(gm_toqf51949ppls, f1) == 64, "layout of `?Pattern`");
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
_Static_assert(sizeof(gm_t930q5l38hdap) == 208 && _Alignof(gm_t930q5l38hdap) == 8 && offsetof(gm_t930q5l38hdap, f0) == 0 && offsetof(gm_t930q5l38hdap, f1) == 8, "layout of `LocalBinding`");
typedef struct { gm_t930q5l38hdap f0; uint8_t f1; uint8_t pe[7]; } gm_tqvu78rpi6q1h;
_Static_assert(sizeof(gm_tqvu78rpi6q1h) == 216 && _Alignof(gm_tqvu78rpi6q1h) == 8 && offsetof(gm_tqvu78rpi6q1h, f0) == 0 && offsetof(gm_tqvu78rpi6q1h, f1) == 208, "layout of `?LocalBinding`");
struct gm_fr_add_label_50fj7vv {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_add_note_46v502s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_arm_pattern_rftjhhb {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* first_binds */
    gm_trqfvmjtt4sij l4; /* alts */
    gm_trqfvmjtt4sij l9; /* binds */
    gm_trqfvmjtt4sij l15; /* map */
    void * ret;
};
struct gm_fr_binding_cond_v86td9t {
    gm_frame h;
    gm_trqfvmjtt4sij l5; /* binds */
    gm_tlr9dp305htm7 ret;
};
struct gm_fr_binds_unknown_name_7m00m7a {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_bool_ty_pn2p277 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_c_union_match_2tgsktr {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_check_arms_65v4isv {
    gm_frame h;
    gm_trqfvmjtt4sij l3; /* pats */
    gm_trqfvmjtt4sij l4; /* guarded */
    gm_tqs4ilbobv1qq ret;
};
struct gm_fr_check_deferred_match_742e2u5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_contains_wildcard_jgbf94d {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_emit_dph5v1e {
    gm_frame h;
    gm_t4gut2c9fj1kt l1; /* d */
    gm_b0_1 ret;
};
struct gm_fr_empty_slice_3v2gjd4_3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_5nn7ph0 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_g3tddrb_2 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_ilis91k {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_jbu9fo3 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_l26i0t5 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r81t436 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_r9g5q6n {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rcd2jln {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_rtces00 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_s1lbe5s {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_tcmn71c {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_error_89glhjr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_9huj44a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_error_count_omtt6qd {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_error_expr_o6qt7e2 {
    gm_frame h;
    void * ret;
};
struct gm_fr_error_ki8i3k6 {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_expr_to_pattern_rchr7ig {
    gm_frame h;
    gm_to361lgknsu69 l1; /* kind */
    gm_trqfvmjtt4sij l4; /* pargs */
    gm_tc3oht4ksth9d l9; /* v */
    gm_t0d02142k4tu8 l10; /* np */
    gm_tc3oht4ksth9d l12; /* v */
    gm_t0d02142k4tu8 l13; /* np */
    gm_t0d02142k4tu8 l16; /* inner */
    gm_trqfvmjtt4sij l18; /* pargs */
    gm_t0d02142k4tu8 l23; /* np */
    gm_toqf51949ppls ret;
};
struct gm_fr_fn_ho0p9pu {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_has_infer_3o82a0b {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_in_patterns_mi6p7hb {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* arms */
    gm_toqf51949ppls l12; /* p */
    gm_t0d02142k4tu8 l13; /* pp */
    gm_trqfvmjtt4sij l14; /* binds */
    void * ret;
};
struct gm_fr_is_error_8k27gd1 {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_pattern_operand_p51tban {
    gm_frame h;
    gm_t0d02142k4tu8 l3; /* pat */
    gm_toqf51949ppls ret;
};
struct gm_fr_is_pattern_test_kdd7cfh {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* binds */
    gm_trqfvmjtt4sij l11; /* arms */
    void * ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_58nr42v_3 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_6l26q4a {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_7s4om10 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_br4lm90 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nh3d5t2 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_nof7vfj {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_s83jn2q {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_ttksl9g {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_match_expr_npcoo60 {
    gm_frame h;
    gm_trqfvmjtt4sij l8; /* arms */
    gm_t79rbdv4rafe5 l18; /* b */
    gm_trqfvmjtt4sij l20; /* arm_spans */
    gm_trqfvmjtt4sij l26; /* branches */
    void * ret;
};
struct gm_fr_mk_9bc97fd {
    gm_frame h;
    gm_teti411eujvf4 l1; /* kind */
    void * ret;
};
struct gm_fr_mk_pat_hr2l6c4 {
    gm_frame h;
    gm_tb38v5s9j1r32 l1; /* kind */
    void * ret;
};
struct gm_fr_nested_pattern_oiclpgf {
    gm_frame h;
    gm_toqf51949ppls ret;
};
struct gm_fr_never_ty_b949mgo {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
struct gm_fr_omits_payload_e33pdef {
    gm_frame h;
    gm_tckdobunaulee l17; /* v */
    uint8_t ret;
};
struct gm_fr_pop_570g79n {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_pop_8sodrk9 {
    gm_frame h;
    gm_td8hfgklo8dil ret;
};
struct gm_fr_pop_dt2bd2s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* x */
    gm_tqetrgo0q24b5 ret;
};
struct gm_fr_pop_scope_g5j7o4e {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_push_0mk7udh {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_2uqiirs {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_3ef746b {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_bpovr2k_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_k3l9nic {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_kiclmcn {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_l74t9rq {
    gm_frame h;
    gm_tnrgict8sthcv l1; /* x */
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_ltrd0kq {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_u2jka7o {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_push_vd1290p {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_0jmnnpk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_8skjmj4 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_94j8fj1 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_h9tbucc_3 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hkmvuvc {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_hm23q6l {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_i2ujhou {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_j4iu2lk {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_t5pgl2t {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_regrow_v0p8ic9 {
    gm_frame h;
    gm_tftb1odc739t5 ret;
};
struct gm_fr_remap_binds_kao301q {
    gm_frame h;
    gm_b16_8 l1; /* map */
    gm_b0_1 ret;
};
struct gm_fr_report_eardf07 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_report_match_d77kbfe {
    gm_frame h;
    gm_tqs4ilbobv1qq l1; /* report */
    gm_t4gut2c9fj1kt l5; /* d */
    gm_b0_1 ret;
};
struct gm_fr_restore_scope_edqq1v8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_fsnl6fa {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_truncate_7r782uc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_daelje0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_dsb08pk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_truncate_upeumbt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_unit_2age34u {
    gm_frame h;
    gm_t88rcucg1b7uk ret;
};
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_arm_pattern_rftjhhb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_binding_cond_v86td9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_binds_unknown_name_7m00m7a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void * gm_f_block_expected_bv4ukm2(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_bound_n051ued(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_c_union_match_2tgsktr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_check_arms_65v4isv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deferred_match_742e2u5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_check_match_mj577a4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, gm_b16_8 a3);
__attribute__((aligned(4))) void * gm_f_coerce_sudc6c1(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_contains_wildcard_jgbf94d(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_declare_binds_rk1ncsj(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ilis91k(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_l26i0t5(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tcmn71c(void);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_89glhjr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0);
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_error_s6hn871(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tl874t2atnlh3 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void * gm_f_expect_bool_cdo5g1q(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_expr_to_pattern_rchr7ig(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_expr_vmp704u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_tkn42u7na3t81 a2);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ho0p9pu(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void gm_f_for_each_subpat_arlrtrm(void * a0, gm_closure a1);
__attribute__((aligned(4))) void gm_f_format_d277g96(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t8lugsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tb06a6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void * gm_f_in_patterns_mi6p7hb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_intern_ia19mhl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_irrefutable_lpkmeii(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_c_union_p1r4eve(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_is_pattern_operand_p51tban(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_is_pattern_test_kdd7cfh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_join_type_cck70vp(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_6l26q4a(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_br4lm90(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nh3d5t2(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_lookup_local_6f38t7p(void *gm_out, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lookup_module_name_cukfp7t(void *env, gm_node *cx, gm_node *inst, void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void * gm_f_match_expr_npcoo60(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3);
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2);
__attribute__((aligned(4))) void gm_f_nested_pattern_oiclpgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_never_ty_b949mgo(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_1b2khgi(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_f7qroea(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_omits_payload_e33pdef(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void * gm_f_pattern_5gn6kid(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2, gm_t9qkh0rsooc8d a3, void * a4);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2uqiirs(void * a0, gm_cs9ufftihtbpo2 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ltrd0kq(void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_push_o4ckdr1(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_push_scope_mu7j3lg(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u2jka7o(void * a0, void * a1);
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_94j8fj1(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_3(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hm23q6l(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_i2ujhou(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_j4iu2lk(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_raw_4ag1s1i(void * a0, uint64_t a1, void * a2, uint64_t a3, uint64_t a4, uint64_t a5);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t5pgl2t(void * a0, uint64_t a1, gm_b16_8 a2);
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_remap_binds_kao301q(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_eardf07(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_report_match_d77kbfe(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3);
__attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_resolve_t2kal61(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void * gm_f_stmt_expr_evs4ic8(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_csguankko4lmhd gm_f_strip_wrappers_e5thpib(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_swap_remove_lfi9t15(void *gm_out, void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_cseahvg8v7p0vs gm_f_to_c2nvemb(gm_cseahvg8v7p0vs a0, gm_cseahvg8v7p0vs a1);
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1);
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_type_of_name_oqtujpn(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_union_variants_a7khaqp(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0);
extern uint8_t gm_k3tttsr3hp1[40] __attribute__((aligned(8)));
extern uint8_t gm_k652m2ksgeg[40] __attribute__((aligned(8)));
extern uint8_t gm_k9foool24ve[40] __attribute__((aligned(8)));
extern uint8_t gm_kdhojj6r45o[40] __attribute__((aligned(8)));
extern uint8_t gm_kns860jaqph[40] __attribute__((aligned(8)));
extern uint8_t gm_ku35m9tsdej[40] __attribute__((aligned(8)));
extern const uint8_t gm_s3ceboq671qr5[39];
extern const uint8_t gm_s5p756i54h2sb[52];
extern const uint8_t gm_s67h6d8kdg7qc[167];
extern const uint8_t gm_sa8p4rum5g6ig[116];
extern const uint8_t gm_sk9dt92p27k0o[53];
extern const uint8_t gm_slbqo2q5mg776[107];
extern const uint8_t gm_sn9h18hshc2p3[60];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src786i8duem00q[];
extern const char gm_srcgfglnv8p059r[];
extern const char gm_srcod98slk3vnra[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcsk3h2du2ldnc[];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b) {
    uintptr_t n = GM_LD(uintptr_t, a + 8);
    if (n != GM_LD(uintptr_t, b + 8)) return false;
    const uint8_t *pa = GM_LD(void *, a), *pb = GM_LD(void *, b);
    return n == 0 || memcmp(pa, pb, n * 1) == 0;
}
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline bool gm_eqff85ac10o3(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    if (!gm_eq467d5cm1d5(a + 4, b + 4)) return false;
    if (!gm_eq467d5cm1d5(a + 8, b + 8)) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_add_label_50fj7vv(void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_label_50fj7vv F;
    void * gm_l0; /* d */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* message */
    GM_STACK_CHECK(&F, 114u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 40);
        gm_tbgd14esf0jvi t4;
        memset((uint8_t *)&t4 + 12, 0, 4);
        gm_tl874t2atnlh3 t5 = gm_l1;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t4 + 0, t5);
        gm_b16_8 t6 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t4 + 16, t6);
        gm_tftb1odc739t5 t7 = gm_f_push_kiclmcn(t3, &t4);
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srcgfglnv8p059r, 92, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_add_note_46v502s(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_add_note_46v502s F;
    void * gm_l0; /* d */
    gm_b16_8 gm_l1; /* note */
    GM_STACK_CHECK(&F, 58u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 80);
        gm_b16_8 t4 = gm_l1;
        gm_tftb1odc739t5 t5 = gm_f_push_3ef746b(t3, t4);
        if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 1, gm_srcgfglnv8p059r, 96, 5);
        gm_b0_1 t6 = GM_LD(gm_b0_1, (((uint8_t *)&t5) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_bool_ty_pn2p277(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_bool_ty_pn2p277 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)4ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_c_union_match_2tgsktr(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_c_union_match_2tgsktr F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 144u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_b16_8 t3 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t1, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t4;
    GM_ST(gm_b16_8, ((uint8_t *)&t4 + 0), t3);
    gm_tabb710d9mq2e t5;
    gm_f_format_tb06a6e((void *)&t5, (void *)NULL, cx, NULL, t4);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t5))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t5) + 8, gm_srcsk3h2du2ldnc, 478, 63);
    gm_b16_8 t6 = GM_LD(gm_b16_8, (((uint8_t *)&t5) + 8));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_emit_dph5v1e(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_emit_dph5v1e F;
    void * gm_l0; /* s */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        gm_f_push_o4ckdr1(t4, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l1)));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_3v2gjd4_3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_3v2gjd4_3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_5nn7ph0(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_5nn7ph0 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_g3tddrb_2(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_g3tddrb_2 F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 88)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ilis91k(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_ilis91k F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_jbu9fo3(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_jbu9fo3 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_l26i0t5(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_l26i0t5 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r81t436(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r81t436 F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 12)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_r9g5q6n(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_r9g5q6n F;
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 120)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rcd2jln F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)4ULL));
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
        GM_ST(void *, (uint8_t *)&t8, (void *)(((uint8_t *)t5 + t6 * 4)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rtces00(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_rtces00 F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_s1lbe5s(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_s1lbe5s F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_tcmn71c(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_tcmn71c F;
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
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 8));
        gm_tl874t2atnlh3 t5 = gm_l1;
        gm_b16_8 t6 = gm_l2;
        gm_b0_1 t7 = gm_f_report_eardf07((void *)NULL, cx, NULL, t4, t5, t6);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_error_9huj44a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2) {
    struct gm_fr_error_9huj44a F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* msg */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_b16_8 t5 = gm_l2;
        gm_b0_1 t6 = gm_f_error_89glhjr((void *)NULL, cx, NULL, t3, t4, t5);
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
inline __attribute__((aligned(4))) uint64_t gm_f_error_count_omtt6qd(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_error_count_omtt6qd F;
    void * gm_l0; /* ds */
    GM_STACK_CHECK_SHALLOW(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 48));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void * gm_f_error_expr_o6qt7e2(void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1) {
    struct gm_fr_error_expr_o6qt7e2 F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    GM_STACK_CHECK(&F, 156u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_b16_8 t4 = gm_f_empty_slice_g3tddrb_2();
    gm_teti411eujvf4 t5;
    memset(&t5, 0, sizeof t5);
    GM_ST(uint8_t, ((uint8_t *)&t5 + 0), ((uint8_t)35ULL));
    GM_ST(gm_b16_8, ((uint8_t *)&t5 + 8), t4);
    gm_tl874t2atnlh3 t6 = gm_l1;
    void * t7 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t3, &t5, t6);
    if (gm_unw.kind) {
        goto gm_lad;
    }
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
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_error_ki8i3k6(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_error_ki8i3k6 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 84u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t53at80pfdus3 t2;
    memset(&t2, 0, sizeof t2);
    GM_ST(uint8_t, ((uint8_t *)&t2 + 0), ((uint8_t)26ULL));
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ho0p9pu(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_fn_ho0p9pu F;
    void * gm_l0; /* q */
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&((struct gm_fr_remap_binds_kao301q *)((&F.h)->up))->l1));
        gm_f_remap_binds_kao301q(t2, t3);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_has_infer_3o82a0b(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_has_infer_3o82a0b F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 53u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    uint32_t t1 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 0));
    uint64_t t2 = (uint64_t)t1;
    uint64_t t3 = (uintptr_t)t2;
    void * t4 = gm_l0;
    uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)t4) + 40) + 0) + 8);
    if (t3 >= (uintptr_t)(t5)) gm_trap_index(t3, t5, gm_src0ih77f43mhf2, 252, 57);
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t4) + 40) + 0)) + t3 * 1));
    uint8_t t7 = (uint8_t)(t6 & ((uint8_t)2ULL));
    uint8_t t8 = (!(t7 == ((uint8_t)0ULL)));
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_error_8k27gd1(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_error_8k27gd1 F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 83u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    uint8_t t4 = ((GM_LD(uint8_t, ((uint8_t *)&t3) + 0) == ((uint8_t)26ULL)));
    F.ret = t4;
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
inline __attribute__((aligned(4))) void gm_f_list_58nr42v_3(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_58nr42v_3 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_3v2gjd4_3();
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
inline __attribute__((aligned(4))) void gm_f_list_6l26q4a(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_6l26q4a F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_ilis91k();
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
inline __attribute__((aligned(4))) void gm_f_list_7s4om10(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_7s4om10 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_r81t436();
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
inline __attribute__((aligned(4))) void gm_f_list_br4lm90(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_br4lm90 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_l26i0t5();
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
inline __attribute__((aligned(4))) void gm_f_list_nh3d5t2(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_nh3d5t2 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_rtces00();
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
inline __attribute__((aligned(4))) void gm_f_list_s83jn2q(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_s83jn2q F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_s1lbe5s();
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
inline __attribute__((aligned(4))) void gm_f_list_ttksl9g(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_ttksl9g F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_r9g5q6n();
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
inline __attribute__((aligned(4))) void * gm_f_mk_9bc97fd(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_9bc97fd F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 160u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_t69m56meqefs8 t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    memset((uint8_t *)&t1 + 84, 0, 4);
    gm_t88rcucg1b7uk t2 = gm_l0;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t1 + 0, t2);
    memcpy((uint8_t *)&t1 + 8, ((uint8_t *)&F.l1), 64);
    gm_tl874t2atnlh3 t3 = gm_l2;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 72, t3);
    gm_tq7s98ihonkju t4 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k3tttsr3hp1)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 558, 81);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
inline __attribute__((aligned(4))) void * gm_f_mk_pat_hr2l6c4(void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0, const void * a1, gm_tl874t2atnlh3 a2) {
    struct gm_fr_mk_pat_hr2l6c4 F;
    gm_t88rcucg1b7uk gm_l0; /* ty */
    gm_tl874t2atnlh3 gm_l2; /* span */
    GM_STACK_CHECK(&F, 208u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_tc9qv69frb08e t1;
    memset((uint8_t *)&t1 + 4, 0, 4);
    memset((uint8_t *)&t1 + 132, 0, 4);
    gm_t88rcucg1b7uk t2 = gm_l0;
    GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t1 + 0, t2);
    memcpy((uint8_t *)&t1 + 8, ((uint8_t *)&F.l1), 112);
    gm_tl874t2atnlh3 t3 = gm_l2;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 120, t3);
    gm_tq7s98ihonkju t4 = gm_f_new_1b2khgi((void *)NULL, cx, NULL, &t1, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k652m2ksgeg)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcod98slk3vnra, 311, 83);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_t9805qtonbsag gm_f_pop_570g79n(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_570g79n F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    uint32_t gm_l2; /* x */
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
            gm_t9805qtonbsag t8;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
        uint32_t t17 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 4));
        gm_l2 = t17;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 0) + 8);
        void * t21 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 0)));
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l1;
        uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 4)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        uint32_t t29 = gm_l2;
        gm_t9805qtonbsag t30;
        GM_ST(uint8_t, (uint8_t *)&t30 + 5, 0);
        GM_ST(uint16_t, (uint8_t *)&t30 + 6, 0);
        GM_ST(uint32_t, ((uint8_t *)&t30), t29);
        ((uint8_t *)&t30)[4] = 1;
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_8sodrk9(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_8sodrk9 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    gm_t66j1im9ip128 gm_l2; /* x */
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
            gm_td8hfgklo8dil t8;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
        gm_t66j1im9ip128 t17 = GM_LD(gm_t66j1im9ip128, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 32));
        gm_l2 = t17;
    }
    {
        void * t18 = gm_l0;
        void * t19 = gm_l0;
        uint64_t t20 = GM_LD(uintptr_t, (((uint8_t *)t19) + 0) + 8);
        void * t21 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t19) + 0)));
        uint64_t t22 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t23 = gm_l1;
        uint64_t r24; bool o25 = __builtin_sub_overflow(t23, ((uint64_t)1ULL), &r24);
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 32)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&gm_l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 36, 0);
        ((uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out))[32] = 1;
        memmove((uint8_t *)&(*(gm_td8hfgklo8dil *)gm_out) + 0, t29, 32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) __attribute__((always_inline)) inline void gm_f_pop_dt2bd2s(void *gm_out, void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_dt2bd2s F;
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
            gm_tqetrgo0q24b5 t8;
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
        if (o12) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 133, 18);
        uint64_t t13 = r11;
        uint64_t t14 = (uintptr_t)t13;
        void * t15 = gm_l0;
        uint64_t t16 = GM_LD(uintptr_t, (((uint8_t *)t15) + 0) + 8);
        if (t14 >= (uintptr_t)(t16)) gm_trap_index(t14, t16, gm_srcrgc9bkquf6nr, 133, 10);
        gm_trqfvmjtt4sij t17 = GM_LD(gm_trqfvmjtt4sij, ((uint8_t *)GM_LD(void *, (((uint8_t *)t15) + 0)) + t14 * 40));
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
        if (o25) gm_trap("integer overflow in `-`", gm_srcrgc9bkquf6nr, 134, 26);
        uint64_t t26 = r24;
        uint64_t t27 = (uintptr_t)t26;
        if (t22 > t27 || t27 > (uintptr_t)t20) gm_trap_slice(t22, t27, t20, gm_srcrgc9bkquf6nr, 134, 15);
        gm_b16_8 t28;
        GM_ST(void *, (uint8_t *)&t28, (void *)(((uint8_t *)t21 + t22 * 40)));
        GM_ST(uintptr_t, (uint8_t *)&t28 + 8, (uintptr_t)(t27 - t22));
        GM_ST(gm_b16_8, (((uint8_t *)t18) + 0), t28);
    }
    {
        const uint8_t *t29 = ((uint8_t *)&F.l2);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 41, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 42, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 44, 0);
        ((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out))[40] = 1;
        memmove((uint8_t *)&(*(gm_tqetrgo0q24b5 *)gm_out) + 0, t29, 40);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_pop_scope_g5j7o4e(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_pop_scope_g5j7o4e F;
    void * gm_l0; /* cx */
    gm_t66j1im9ip128 gm_l1; /* m */
    GM_STACK_CHECK_SHALLOW(&F, 200u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = gm_l0;
            void * t4 = (void *)(((uint8_t *)t3) + 56);
            gm_tqetrgo0q24b5 t5;
            gm_f_pop_dt2bd2s((void *)&t5, t4);
        }
    }
    {
        {
            void * t7 = gm_l0;
            void * t8 = (void *)(((uint8_t *)t7) + 616);
            gm_t9805qtonbsag t9 = gm_f_pop_570g79n(t8);
        }
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 656);
        gm_td8hfgklo8dil t13;
        gm_f_pop_8sodrk9((void *)&t13, t12);
        if (!((((uint8_t *)&t13))[32] != 0)) goto ie10;
        gm_l1 = (*(gm_t66j1im9ip128 *)(((uint8_t *)&t13) + 0));
        {
            void * t15 = gm_l0;
            void * t16 = GM_LD(void *, (((uint8_t *)t15) + 0));
            gm_f_restore_scope_edqq1v8(t16, &(*(gm_t66j1im9ip128 *)((uint8_t *)&gm_l1)));
        }
        goto ix10;
ie10:;
ix10:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_0mk7udh(void * a0, gm_cseahvg8v7p0vs a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_0mk7udh F;
    void * gm_l0; /* l */
    gm_tl874t2atnlh3 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
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
            gm_b16_8 t11 = gm_f_empty_slice_r81t436();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_v0p8ic9(t10, ((uint64_t)1ULL), t12);
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
        gm_tl874t2atnlh3 t21 = gm_l1;
        GM_ST(gm_tl874t2atnlh3, ((uint8_t *)t20 + t18 * 12), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 12)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_2uqiirs(void * a0, gm_cs9ufftihtbpo2 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_2uqiirs F;
    void * gm_l0; /* l */
    gm_tcc1rq4n1avb2 gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tcc1rq4n1avb2, a1);
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
            gm_b16_8 t11 = gm_f_empty_slice_l26i0t5();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_i2ujhou(t10, ((uint64_t)1ULL), t12);
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
        gm_tcc1rq4n1avb2 t21 = gm_l1;
        GM_ST(gm_tcc1rq4n1avb2, ((uint8_t *)t20 + t18 * 8), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 8)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_3ef746b F;
    void * gm_l0; /* l */
    gm_b16_8 gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_5nn7ph0();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_0jmnnpk(t10, ((uint64_t)1ULL), t12);
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
        gm_b16_8 t21 = gm_l1;
        GM_ST(gm_b16_8, ((uint8_t *)t20 + t18 * 16), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 16)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_bpovr2k_3(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_bpovr2k_3 F;
    void * gm_l0; /* l */
    gm_tbbqefuhv2r8h gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
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
            gm_b16_8 t11 = gm_f_empty_slice_3v2gjd4_3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_h9tbucc_3(t10, ((uint64_t)1ULL), t12);
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
        gm_tbbqefuhv2r8h t21 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)a1));
        GM_ST(gm_tbbqefuhv2r8h, ((uint8_t *)t20 + t18 * 24), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 24)));
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
            gm_b16_8 t11 = gm_f_empty_slice_rcd2jln();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_j4iu2lk(t10, ((uint64_t)1ULL), t12);
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
        gm_t88rcucg1b7uk t21 = gm_l1;
        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)t20 + t18 * 4), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 4)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_kiclmcn(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_kiclmcn F;
    void * gm_l0; /* l */
    gm_tbgd14esf0jvi gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
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
            gm_b16_8 t11 = gm_f_empty_slice_jbu9fo3();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_8skjmj4(t10, ((uint64_t)1ULL), t12);
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
        gm_tbgd14esf0jvi t21 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)a1));
        GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)t20 + t18 * 32), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_l74t9rq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_l74t9rq F;
    void * gm_l0; /* l */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
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
            gm_b16_8 t11 = gm_f_empty_slice_r9g5q6n();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_t5pgl2t(t10, ((uint64_t)1ULL), t12);
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
        gm_tnrgict8sthcv t21 = F.l1;
        GM_ST(gm_tnrgict8sthcv, ((uint8_t *)t20 + t18 * 120), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 120)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_ltrd0kq(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_ltrd0kq F;
    void * gm_l0; /* l */
    gm_tbgd14esf0jvi gm_l1; /* x */
    uint64_t gm_l2; /* n */
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
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
            gm_b16_8 t11 = gm_f_empty_slice_tcmn71c();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_hm23q6l(t10, ((uint64_t)1ULL), t12);
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
        gm_tbgd14esf0jvi t21 = GM_LD(gm_tbgd14esf0jvi, ((uint8_t *)a1));
        GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)t20 + t18 * 32), t21);
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_u2jka7o(void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_u2jka7o F;
    void * gm_l0; /* l */
    void * gm_l1; /* x */
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
            gm_b16_8 t11 = gm_f_empty_slice_rtces00();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_94j8fj1(t10, ((uint64_t)1ULL), t12);
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
        void * t21 = gm_l1;
        GM_ST(void *, ((uint8_t *)t20 + t18 * 8), t21);
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
        GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 8)));
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
__attribute__((aligned(4))) __attribute__((always_inline)) inline gm_tftb1odc739t5 gm_f_push_vd1290p(void * a0, uint8_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_push_vd1290p F;
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
            gm_b16_8 t11 = gm_f_empty_slice_s1lbe5s();
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
            gm_tftb1odc739t5 t13 = gm_f_regrow_hkmvuvc(t10, ((uint64_t)1ULL), t12);
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_0jmnnpk(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_0jmnnpk F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)16ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_8skjmj4(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_8skjmj4 F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_94j8fj1(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_94j8fj1 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_h9tbucc_3(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_h9tbucc_3 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)24ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hkmvuvc(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hkmvuvc F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_hm23q6l(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_hm23q6l F;
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
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_i2ujhou(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_i2ujhou F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)8ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)4ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_t5pgl2t(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_t5pgl2t F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)120ULL), ((uint64_t)8ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_regrow_v0p8ic9(void * a0, uint64_t a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_regrow_v0p8ic9 F;
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
        gm_tftb1odc739t5 t8 = gm_f_regrow_raw_4ag1s1i(t3, t4, t6, t7, ((uint64_t)12ULL), ((uint64_t)4ULL));
        uint8_t t9 = ((((uint8_t *)&t8))[0] != 0);
        F.ret = t8;
        goto gm_fret;
    }
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
    {
        void * t2 = gm_l0;
        gm_tl874t2atnlh3 t3 = gm_l1;
        gm_b16_8 t4 = gm_l2;
        gm_t4gut2c9fj1kt t5;
        gm_f_error_s6hn871((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_push_o4ckdr1(t2, &t5);
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
inline __attribute__((aligned(4))) void gm_f_restore_scope_edqq1v8(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_restore_scope_edqq1v8 F;
    void * gm_l0; /* s */
    gm_t66j1im9ip128 gm_l1; /* mark */
    GM_STACK_CHECK_SHALLOW(&F, 136u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        void * t3 = (void *)(((uint8_t *)t2) + 1488);
        uint64_t t4 = GM_LD(uint64_t, (((uint8_t *)a1) + 0));
        gm_f_truncate_7r782uc(t3, t4);
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 1528);
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a1) + 8));
        gm_f_truncate_daelje0(t6, t7);
    }
    {
        void * t8 = gm_l0;
        void * t9 = (void *)(((uint8_t *)t8) + 1608);
        uint64_t t10 = GM_LD(uint64_t, (((uint8_t *)a1) + 16));
        gm_f_truncate_dsb08pk(t9, t10);
    }
    {
        void * t11 = gm_l0;
        void * t12 = (void *)(((uint8_t *)t11) + 1568);
        uint64_t t13 = GM_LD(uint64_t, (((uint8_t *)a1) + 24));
        gm_f_truncate_upeumbt(t12, t13);
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_8uijkbs F;
    void * gm_l0; /* s */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 8);
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_kmjeh8s((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_fsnl6fa(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_fsnl6fa F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* t */
    GM_STACK_CHECK(&F, 80u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    void * t1 = gm_l0;
    void * t2 = GM_LD(void *, (((uint8_t *)t1) + 0));
    gm_t88rcucg1b7uk t3 = gm_l1;
    gm_b16_8 t4 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t2, t3);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_show_kmjeh8s F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 129u);
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
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
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
inline __attribute__((aligned(4))) void gm_f_truncate_7r782uc(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_7r782uc F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_daelje0(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_daelje0 F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_dsb08pk(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_dsb08pk F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 24)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) void gm_f_truncate_upeumbt(void * a0, uint64_t a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_truncate_upeumbt F;
    void * gm_l0; /* l */
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t3 = gm_l1;
        void * t4 = gm_l0;
        uint64_t t5 = GM_LD(uintptr_t, (((uint8_t *)t4) + 0) + 8);
        uint8_t t6 = (t3 < t5);
        if (!t6) goto ie2;
        {
            void * t8 = gm_l0;
            void * t9 = gm_l0;
            uint64_t t10 = GM_LD(uintptr_t, (((uint8_t *)t9) + 0) + 8);
            void * t11 = ((uint8_t *)GM_LD(void *, (((uint8_t *)t9) + 0)));
            uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t13 = gm_l1;
            uint64_t t14 = (uintptr_t)t13;
            if (t12 > t14 || t14 > (uintptr_t)t10) gm_trap_slice(t12, t14, t10, gm_srcrgc9bkquf6nr, 125, 36);
            gm_b16_8 t15;
            GM_ST(void *, (uint8_t *)&t15, (void *)(((uint8_t *)t11 + t12 * 224)));
            GM_ST(uintptr_t, (uint8_t *)&t15 + 8, (uintptr_t)(t14 - t12));
            GM_ST(gm_b16_8, (((uint8_t *)t8) + 0), t15);
        }
        goto ix2;
ie2:;
ix2:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_t88rcucg1b7uk gm_f_unit_2age34u(void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_unit_2age34u F;
    void * gm_l0; /* t */
    GM_STACK_CHECK(&F, 180u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    gm_t6j6smj7qoctu t2;
    gm_b16_8 t3 = gm_f_empty_slice_rcd2jln();
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 0, t4);
    gm_b16_8 t5 = gm_f_empty_slice_5nn7ph0();
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    GM_ST(gm_b16_8, (uint8_t *)&t2 + 16, t6);
    gm_t53at80pfdus3 t7;
    GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
    GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 40, 0);
    GM_ST(uint64_t, (uint8_t *)&t7 + 48, 0);
    GM_ST(uint8_t, ((uint8_t *)&t7 + 0), ((uint8_t)12ULL));
    GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t7 + 8), t2);
    gm_t88rcucg1b7uk t8 = gm_f_intern_ia19mhl((void *)NULL, cx, NULL, t1, &t7);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    F.ret = t8;
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void * gm_f_arm_pattern_rftjhhb(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_arm_pattern_rftjhhb F;
    void * gm_l0; /* cx */
    gm_b16_8 gm_l1; /* pats */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    void * gm_l8; /* p */
    void * gm_l10; /* pt */
    uint8_t gm_l11; /* same */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    gm_tdu0qimd4ae4c gm_l14; /* b */
    gm_b16_8 gm_l16; /* <tmp> */
    uint64_t gm_l17; /* <tmp> */
    gm_tdu0qimd4ae4c gm_l18; /* b */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_tdu0qimd4ae4c gm_l21; /* f */
    gm_t88rcucg1b7uk gm_l22; /* t1 */
    gm_t88rcucg1b7uk gm_l23; /* t2 */
    gm_tl874t2atnlh3 gm_l24; /* span */
    GM_STACK_CHECK(&F, 1557u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_6l26q4a((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_nh3d5t2((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            gm_l5 = t3;
        }
        {
            for (;;) {
                uint64_t t5 = gm_l6;
                uint64_t t6 = gm_l5;
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l6;
                    uint64_t t13 = GM_LD(uint64_t, ((uint8_t *)&t10));
                    gm_l7 = t13;
                    {
                        uint64_t t15 = gm_l7;
                        uint64_t t16 = (uintptr_t)t15;
                        uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t16 >= (uintptr_t)(t17)) gm_trap_index(t16, t17, gm_src786i8duem00q, 122, 15);
                        void * t18 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t16 * 64);
                        gm_l8 = t18;
                    }
                    {
                        gm_f_list_6l26q4a((void *)&F.l9, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                    }
                    {
                        void * t19 = gm_l0;
                        void * t20 = gm_l8;
                        gm_t88rcucg1b7uk t21 = gm_l2;
                        gm_t9qkh0rsooc8d t22;
                        GM_ST(uint8_t, ((uint8_t *)&t22 + 0), ((uint8_t)0ULL));
                        void * t23 = (void *)((uint8_t *)&F.l9);
                        void * t24 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t19, t20, t21, t22, t23);
                        if (gm_unw.kind) {
                            goto gm_ll8;
                        }
                        gm_l10 = t24;
                    }
                    {
                        uint64_t t26 = gm_l7;
                        uint8_t t27 = ((t26 == ((uint64_t)0ULL)));
                        if (!t27) goto ie25;
                        {
                            gm_trqfvmjtt4sij t29 = F.l9;
                            F.l3 = t29;
                        }
                        goto ix25;
ie25:;
                        {
                            uint64_t t31 = GM_LD(uintptr_t, (((uint8_t *)&F.l9) + 0) + 8);
                            uint64_t t32 = GM_LD(uintptr_t, (((uint8_t *)&F.l3) + 0) + 8);
                            uint8_t t33 = ((t31 == t32));
                            gm_l11 = t33;
                        }
                        {
                            {
                                gm_b16_8 t35 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                                gm_l12 = t35;
                            }
                            {
                                gm_l13 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t37 = gm_l13;
                                    uint64_t t38 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                    uint8_t t39 = (t37 < t38);
                                    if (!t39) goto lb36;
                                    {
                                        uint64_t t42 = gm_l13;
                                        uint64_t t43 = (uintptr_t)t42;
                                        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                                        gm_tdu0qimd4ae4c t47 = GM_LD(gm_tdu0qimd4ae4c, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t43 * 32));
                                        gm_l14 = t47;
                                        {
                                            void * t50 = (void *)((uint8_t *)&F.l3);
                                            gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 0));
                                            uint8_t t52 = gm_f_bound_n051ued(t50, t51);
                                            uint8_t t53 = (uint8_t)!t52;
                                            if (!t53) goto ie49;
                                            {
                                                gm_l11 = ((uint8_t)0);
                                            }
                                            goto ix49;
ie49:;
ix49:;
                                        }
                                        goto me45;
mn46:;
                                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 131, 13);
me45:;
                                    }
lc36:;
                                    {
                                        uint64_t t56 = gm_l13;
                                        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                                        uint64_t t59 = r57;
                                        gm_l13 = t59;
                                    }
                                }
lb36:;
                            }
                        }
                        {
                            uint8_t t61 = gm_l11;
                            uint8_t t62 = (uint8_t)!t61;
                            if (!t62) goto ie60;
                            {
                                void * t64 = gm_l0;
                                void * t65 = gm_l8;
                                gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t65) + 48));
                                gm_b16_8 t67;
                                GM_ST(void *, (uint8_t *)&t67, (void *)(gm_sk9dt92p27k0o));
                                GM_ST(uintptr_t, (uint8_t *)&t67 + 8, (uintptr_t)(52));
                                gm_b0_1 t68 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t64, t66, t67);
                                if (gm_unw.kind) {
                                    goto gm_ll8;
                                }
                            }
                            goto ix60;
ie60:;
ix60:;
                        }
                        {
                            gm_f_list_br4lm90((void *)&F.l15, (void *)NULL, cx, NULL);
                            if (gm_unw.kind) {
                                goto gm_ll8;
                            }
                        }
                        {
                            {
                                gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                                gm_l16 = t70;
                            }
                            {
                                gm_l17 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t72 = gm_l17;
                                    uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                    uint8_t t74 = (t72 < t73);
                                    if (!t74) goto lb71;
                                    {
                                        uint64_t t77 = gm_l17;
                                        uint64_t t78 = (uintptr_t)t77;
                                        uint64_t t79 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
                                        gm_tdu0qimd4ae4c t82 = GM_LD(gm_tdu0qimd4ae4c, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l16)) + t78 * 32));
                                        gm_l18 = t82;
                                        {
                                            {
                                                gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
                                                gm_l19 = t85;
                                            }
                                            {
                                                gm_l20 = ((uint64_t)0ULL);
                                            }
                                            {
                                                for (;;) {
                                                    uint64_t t87 = gm_l20;
                                                    uint64_t t88 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                                    uint8_t t89 = (t87 < t88);
                                                    if (!t89) goto lb86;
                                                    {
                                                        uint64_t t92 = gm_l20;
                                                        uint64_t t93 = (uintptr_t)t92;
                                                        uint64_t t94 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                                        gm_tdu0qimd4ae4c t97 = GM_LD(gm_tdu0qimd4ae4c, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t93 * 32));
                                                        gm_l21 = t97;
                                                        {
                                                            gm_b16_8 t100 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l21) + 0));
                                                            gm_b16_8 t101 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                                                            uint8_t t102 = (gm_eq0iqiubfm7e(((uint8_t *)&t100), ((uint8_t *)&t101)));
                                                            if (!t102) goto ie99;
                                                            {
                                                                uint32_t t104 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 16));
                                                                uint64_t t105 = (uint64_t)t104;
                                                                uint64_t t106 = (uintptr_t)t105;
                                                                void * t107 = gm_l0;
                                                                uint64_t t108 = GM_LD(uintptr_t, ((((uint8_t *)t107) + 16) + 0) + 8);
                                                                if (t106 >= (uintptr_t)(t108)) gm_trap_index(t106, t108, gm_src786i8duem00q, 140, 31);
                                                                gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t107) + 16) + 0)) + t106 * 40) + 16));
                                                                gm_l22 = t109;
                                                            }
                                                            {
                                                                uint32_t t110 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 16));
                                                                uint64_t t111 = (uint64_t)t110;
                                                                uint64_t t112 = (uintptr_t)t111;
                                                                void * t113 = gm_l0;
                                                                uint64_t t114 = GM_LD(uintptr_t, ((((uint8_t *)t113) + 16) + 0) + 8);
                                                                if (t112 >= (uintptr_t)(t114)) gm_trap_index(t112, t114, gm_src786i8duem00q, 141, 31);
                                                                gm_t88rcucg1b7uk t115 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((((uint8_t *)t113) + 16) + 0)) + t112 * 40) + 16));
                                                                gm_l23 = t115;
                                                            }
                                                            {
                                                                gm_t88rcucg1b7uk t117 = gm_l22;
                                                                gm_t88rcucg1b7uk t118 = gm_l23;
                                                                uint8_t t119 = (!gm_eq9m6mvtmgde(((uint8_t *)&t117), ((uint8_t *)&t118)));
                                                                if (!t119) goto ie116;
                                                                {
                                                                    void * t121 = gm_l0;
                                                                    gm_tl874t2atnlh3 t122 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l18) + 20));
                                                                    gm_tfeab279c16rr t123;
                                                                    gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l18) + 0));
                                                                    GM_ST(gm_b16_8, (uint8_t *)&t123 + 0, t124);
                                                                    void * t125 = gm_l0;
                                                                    gm_t88rcucg1b7uk t126 = gm_l23;
                                                                    gm_b16_8 t127 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t125, t126);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_ll90;
                                                                    }
                                                                    GM_ST(gm_b16_8, (uint8_t *)&t123 + 16, t127);
                                                                    void * t128 = gm_l0;
                                                                    gm_t88rcucg1b7uk t129 = gm_l22;
                                                                    gm_b16_8 t130 = gm_f_show_fsnl6fa((void *)NULL, cx, NULL, t128, t129);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_ll90;
                                                                    }
                                                                    GM_ST(gm_b16_8, (uint8_t *)&t123 + 32, t130);
                                                                    gm_tabb710d9mq2e t131;
                                                                    gm_f_format_t8lugsu((void *)&t131, (void *)NULL, cx, NULL, &t123);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_ll90;
                                                                    }
                                                                    if (((((uint8_t *)&t131))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t131) + 8, gm_src786i8duem00q, 143, 46);
                                                                    gm_b16_8 t132 = GM_LD(gm_b16_8, (((uint8_t *)&t131) + 8));
                                                                    gm_b16_8 t133 = GM_LD(gm_b16_8, ((uint8_t *)&t132));
                                                                    gm_b0_1 t134 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t121, t122, t133);
                                                                    if (gm_unw.kind) {
                                                                        goto gm_ll90;
                                                                    }
                                                                }
                                                                goto ix116;
ie116:;
ix116:;
                                                            }
                                                            {
                                                                void * t135 = (void *)((uint8_t *)&F.l15);
                                                                uint32_t t136 = GM_LD(uint32_t, (((uint8_t *)&gm_l18) + 16));
                                                                uint32_t t137 = GM_LD(uint32_t, (((uint8_t *)&gm_l21) + 16));
                                                                gm_tcc1rq4n1avb2 t138;
                                                                GM_ST(uint32_t, ((uint8_t *)&t138 + 0), t136);
                                                                GM_ST(uint32_t, ((uint8_t *)&t138 + 4), t137);
                                                                gm_tftb1odc739t5 t139 = gm_f_push_2uqiirs(t135, GM_CONV(gm_cs9ufftihtbpo2, t138));
                                                                if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 1, gm_src786i8duem00q, 145, 25);
                                                                gm_b0_1 t140 = GM_LD(gm_b0_1, (((uint8_t *)&t139) + 1));
                                                            }
                                                            {
                                                                goto lb86;
                                                            }
                                                            goto ix99;
ie99:;
ix99:;
                                                        }
                                                        goto me95;
mn96:;
                                                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 138, 17);
me95:;
                                                    }
lc86:;
                                                    {
                                                        uint64_t t142 = gm_l20;
                                                        uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                                                        uint64_t t145 = r143;
                                                        gm_l20 = t145;
                                                    }
                                                }
gm_ll90:;
                                                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb86; }
                                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc86; }
                                                }
                                                goto gm_ll75;
lb86:;
                                            }
                                        }
                                        goto me80;
mn81:;
                                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 137, 13);
me80:;
                                    }
lc71:;
                                    {
                                        uint64_t t147 = gm_l17;
                                        uint64_t r148; bool o149 = __builtin_add_overflow(t147, ((uint64_t)1ULL), &r148);
                                        uint64_t t150 = r148;
                                        gm_l17 = t150;
                                    }
                                }
gm_ll75:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb71; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc71; }
                                }
                                goto gm_ll8;
lb71:;
                            }
                        }
                        {
                            void * t151 = gm_l10;
                            gm_b16_8 t152 = GM_LD(gm_b16_8, (((uint8_t *)&F.l15) + 0));
                            gm_b16_8 t153 = GM_LD(gm_b16_8, ((uint8_t *)&t152));
                            gm_f_remap_binds_kao301q(t151, t153);
                        }
ix25:;
                    }
                    {
                        void * t154 = (void *)((uint8_t *)&F.l4);
                        void * t155 = gm_l10;
                        gm_tftb1odc739t5 t156 = gm_f_push_u2jka7o(t154, t155);
                        if (((((uint8_t *)&t156))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t156) + 1, gm_src786i8duem00q, 152, 9);
                        gm_b0_1 t157 = GM_LD(gm_b0_1, (((uint8_t *)&t156) + 1));
                    }
                    goto me11;
mn12:;
                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 121, 5);
me11:;
                }
lc4:;
                {
                    uint64_t t159 = gm_l6;
                    uint64_t r160; bool o161 = __builtin_add_overflow(t159, ((uint64_t)1ULL), &r160);
                    uint64_t t162 = r160;
                    gm_l6 = t162;
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
        void * t163 = gm_l0;
        void * t164 = (void *)((uint8_t *)&F.l3);
        gm_b0_1 t165 = gm_f_declare_binds_rk1ncsj((void *)NULL, cx, NULL, t163, t164);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t167 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        uint8_t t168 = ((t167 == ((uint64_t)1ULL)));
        if (!t168) goto ie166;
        {
            uint64_t t170 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t171 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
            if (t170 >= (uintptr_t)(t171)) gm_trap_index(t170, t171, gm_src786i8duem00q, 155, 37);
            void * t172 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t170 * 8));
            uint8_t t173 = 0;
            F.ret = t172;
            goto gm_fret;
        }
        goto ix166;
ie166:;
ix166:;
    }
    {
        uint64_t t174 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t175 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        if (t174 >= (uintptr_t)(t175)) gm_trap_index(t174, t175, gm_src786i8duem00q, 156, 23);
        void * t176 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t174 * 8));
        gm_tl874t2atnlh3 t177 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t176) + 120));
        uint64_t t178 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        uint64_t r179; bool o180 = __builtin_sub_overflow(t178, ((uint64_t)1ULL), &r179);
        if (o180) gm_trap("integer overflow in `-`", gm_src786i8duem00q, 156, 54);
        uint64_t t181 = r179;
        uint64_t t182 = (uintptr_t)t181;
        uint64_t t183 = GM_LD(uintptr_t, (((uint8_t *)&F.l4) + 0) + 8);
        if (t182 >= (uintptr_t)(t183)) gm_trap_index(t182, t183, gm_src786i8duem00q, 156, 43);
        void * t184 = GM_LD(void *, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l4) + 0)) + t182 * 8));
        gm_tl874t2atnlh3 t185 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t184) + 120));
        gm_tl874t2atnlh3 t186 = GM_CONV(gm_tl874t2atnlh3, gm_f_to_c2nvemb(GM_CONV(gm_cseahvg8v7p0vs, t177), GM_CONV(gm_cseahvg8v7p0vs, t185)));
        gm_l24 = t186;
    }
    {
        gm_t88rcucg1b7uk t187 = gm_l2;
        gm_b16_8 t188 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_tb38v5s9j1r32 t189;
        memset(&t189, 0, sizeof t189);
        GM_ST(uint8_t, ((uint8_t *)&t189 + 0), ((uint8_t)10ULL));
        GM_ST(gm_b16_8, ((uint8_t *)&t189 + 8), t188);
        gm_tl874t2atnlh3 t190 = gm_l24;
        void * t191 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t187, &t189, t190);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t192 = 0;
        F.ret = t191;
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
__attribute__((aligned(4))) void gm_f_binding_cond_v86td9t(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2) {
    struct gm_fr_binding_cond_v86td9t F;
    void * gm_l0; /* cx */
    void * gm_l1; /* pat */
    void * gm_l2; /* value */
    void * gm_l3; /* v */
    gm_t88rcucg1b7uk gm_l4; /* vty */
    void * gm_l6; /* p */
    uint8_t gm_l7; /* optional */
    GM_STACK_CHECK(&F, 451u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = gm_l2;
        gm_tkn42u7na3t81 t4;
        memset(&t4, 0, sizeof t4);
        void * t5 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t2, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l3 = t5;
    }
    {
        void * t6 = gm_l0;
        void * t7 = (void *)(((uint8_t *)t6) + 96);
        void * t8 = gm_l0;
        void * t9 = GM_LD(void *, (((uint8_t *)t8) + 0));
        void * t10 = gm_l3;
        gm_t88rcucg1b7uk t11 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t10) + 0));
        gm_t88rcucg1b7uk t12 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t7, t9, t11);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t12;
    }
    {
        gm_f_list_6l26q4a((void *)&F.l5, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t13 = gm_l0;
        void * t14 = gm_l1;
        gm_t88rcucg1b7uk t15 = gm_l4;
        gm_t9qkh0rsooc8d t16;
        GM_ST(uint8_t, ((uint8_t *)&t16 + 0), ((uint8_t)0ULL));
        void * t17 = (void *)((uint8_t *)&F.l5);
        void * t18 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t13, t14, t15, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t18;
    }
    {
        void * t20 = gm_l0;
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
        void * t22 = (void *)(((uint8_t *)t21) + 8);
        gm_t88rcucg1b7uk t23 = gm_l4;
        uint8_t t24 = gm_f_is_error_8k27gd1(t22, GM_CONV(gm_csguankko4lmhd, t23));
        uint8_t t25 = (uint8_t)!t24;
        if (!t25) goto ie19;
        void * t26 = gm_l0;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
        void * t28 = gm_l6;
        uint8_t t29 = gm_f_irrefutable_lpkmeii((void *)NULL, cx, NULL, t27, t28);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        if (!t29) goto ie19;
        {
            void * t31 = gm_l0;
            void * t32 = GM_LD(void *, (((uint8_t *)t31) + 0));
            void * t33 = (void *)(((uint8_t *)t32) + 8);
            gm_t88rcucg1b7uk t34 = gm_l4;
            gm_t53at80pfdus3 t35;
            gm_f_kind_jcnt4hm((void *)&t35, t33, GM_CONV(gm_csguankko4lmhd, t34));
            uint8_t t36;
            uint8_t t42 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
            switch (t42) { case ((uint8_t)0ULL): goto ma40; case ((uint8_t)1ULL): goto ma40; case ((uint8_t)2ULL): goto ma40; case ((uint8_t)3ULL): goto ma40; case ((uint8_t)4ULL): goto ma40; case ((uint8_t)5ULL): goto ma40; case ((uint8_t)6ULL): goto ma40; case ((uint8_t)7ULL): goto ma40; case ((uint8_t)8ULL): goto ma40; case ((uint8_t)9ULL): goto ma40; case ((uint8_t)10ULL): goto ma40; case ((uint8_t)11ULL): goto ma40; case ((uint8_t)12ULL): goto ma40; case ((uint8_t)13ULL): goto ma38; case ((uint8_t)14ULL): goto ma39; case ((uint8_t)15ULL): goto ma40; case ((uint8_t)16ULL): goto ma40; case ((uint8_t)17ULL): goto ma40; case ((uint8_t)18ULL): goto ma40; case ((uint8_t)19ULL): goto ma40; case ((uint8_t)20ULL): goto ma40; case ((uint8_t)21ULL): goto ma40; case ((uint8_t)22ULL): goto ma40; case ((uint8_t)23ULL): goto ma40; case ((uint8_t)24ULL): goto ma40; case ((uint8_t)25ULL): goto ma40; case ((uint8_t)26ULL): goto ma40; default: goto ma40; }
ma38:;
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
            if (t44 != ((uint8_t)13ULL)) goto mn43;
            t36 = ((uint8_t)1);
            goto me37;
mn43:;
ma39:;
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
            if (t46 != ((uint8_t)14ULL)) goto mn45;
            t36 = ((uint8_t)1);
            goto me37;
mn45:;
ma40:;
            t36 = ((uint8_t)0);
            goto me37;
mn47:;
ma41:;
            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 184, 21);
me37:;
            gm_l7 = t36;
        }
        {
            uint8_t t49 = gm_l7;
            if (!t49) goto ie48;
            void * t50 = gm_l1;
            uint8_t t51 = ((GM_LD(uint8_t, (((uint8_t *)t50) + 8) + 0) == ((uint8_t)0ULL)));
            if (!t51) goto ie48;
            {
                void * t53 = gm_l0;
                void * t54 = gm_l1;
                gm_tl874t2atnlh3 t55 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t54) + 48));
                gm_b16_8 t56;
                GM_ST(void *, (uint8_t *)&t56, (void *)(gm_sa8p4rum5g6ig));
                GM_ST(uintptr_t, (uint8_t *)&t56 + 8, (uintptr_t)(115));
                gm_b0_1 t57 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t53, t55, t56);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto ix48;
ie48:;
            {
                void * t59 = gm_l0;
                void * t60 = gm_l1;
                gm_tl874t2atnlh3 t61 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t60) + 48));
                gm_b16_8 t62;
                GM_ST(void *, (uint8_t *)&t62, (void *)(gm_sn9h18hshc2p3));
                GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(59));
                gm_b0_1 t63 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t59, t61, t62);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix48:;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        void * t64 = gm_l0;
        void * t65 = (void *)((uint8_t *)&F.l5);
        gm_b0_1 t66 = gm_f_declare_binds_rk1ncsj((void *)NULL, cx, NULL, t64, t65);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t67 = gm_l6;
        void * t68 = gm_l3;
        gm_t3mf58gv9kf3n t69;
        GM_ST(void *, ((uint8_t *)&t69 + 0), t67);
        GM_ST(void *, ((uint8_t *)&t69 + 8), t68);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tlr9dp305htm7 *)gm_out) + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_tlr9dp305htm7 *)gm_out) + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_tlr9dp305htm7 *)gm_out) + 4, 0);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tlr9dp305htm7 *)gm_out) + 0, ((uint8_t)1ULL));
        GM_ST(gm_t3mf58gv9kf3n, (uint8_t *)&(*(gm_tlr9dp305htm7 *)gm_out) + 8, t69);
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
__attribute__((aligned(4))) uint8_t gm_f_binds_unknown_name_7m00m7a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_binds_unknown_name_7m00m7a F;
    void * gm_l0; /* cx */
    void * gm_l1; /* p */
    gm_t08olq97rscn3 gm_l2; /* n */
    void * gm_l3; /* q */
    gm_tagc77imt2chm gm_l4; /* pp */
    gm_b16_8 gm_l5; /* args */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* i */
    gm_b16_8 gm_l9; /* args */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    GM_STACK_CHECK(&F, 855u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma8; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma7; default: goto ma8; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)1ULL)) goto mn11;
        gm_t08olq97rscn3 t13 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t13;
        uint8_t t14 = 0;
        void * t15 = gm_l0;
        gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
        gm_tqvu78rpi6q1h t17;
        gm_f_lookup_local_6f38t7p((void *)&t17, t15, t16);
        uint8_t t18;
        if (((((uint8_t *)&t17))[208] != 0)) goto mn20;
        t18 = ((uint8_t)1);
        goto me19;
mn20:;
        t18 = ((uint8_t)0);
        goto me19;
mn21:;
        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 212, 28);
me19:;
        if (t18) {
            void * t22 = gm_l0;
            void * t23 = GM_LD(void *, (((uint8_t *)t22) + 0));
            void * t24 = gm_l0;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)t24) + 8));
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l2) + 0));
            gm_tf5srh4mp4b3i t27 = gm_f_lookup_module_name_cukfp7t((void *)NULL, cx, NULL, t23, t25, t26);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t28;
            if (((((uint8_t *)&t27))[8] != 0)) goto mn30;
            t28 = ((uint8_t)1);
            goto me29;
mn30:;
            t28 = ((uint8_t)0);
            goto me29;
mn31:;
            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 212, 64);
me29:;
            t14 = t28;
        } else t14 = 0;
        uint8_t t32 = 0;
        F.ret = t14;
        goto gm_fret;
        goto me3;
mn11:;
ma5:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t34 != ((uint8_t)6ULL)) goto mn33;
        void * t35 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l3 = t35;
        void * t36 = gm_l0;
        void * t37 = gm_l3;
        uint8_t t38 = gm_f_binds_unknown_name_7m00m7a((void *)NULL, cx, NULL, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t39 = 0;
        F.ret = t38;
        goto gm_fret;
        goto me3;
mn33:;
ma6:;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t41 != ((uint8_t)7ULL)) goto mn40;
        gm_tagc77imt2chm t42 = GM_LD(gm_tagc77imt2chm, ((((uint8_t *)t2) + 8) + 8));
        gm_l4 = t42;
        {
            gm_th2lqbo77pvsc t44 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&gm_l4) + 8));
            gm_b16_8 t45;
            if (((((uint8_t *)&t44))[16] != 0)) {
                gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&t44) + 0));
                t45 = t46;
            } else {
                uint8_t t47 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l5 = t45;
        }
        {
            {
                gm_l7 = ((uint64_t)0ULL);
            }
            {
                uint64_t t49 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                gm_l6 = t49;
            }
            {
                for (;;) {
                    uint64_t t51 = gm_l7;
                    uint64_t t52 = gm_l6;
                    uint8_t t53 = (t51 < t52);
                    if (!t53) goto lb50;
                    {
                        uint64_t t56 = gm_l7;
                        uint64_t t59 = GM_LD(uint64_t, ((uint8_t *)&t56));
                        gm_l8 = t59;
                        {
                            void * t62 = gm_l0;
                            uint64_t t63 = gm_l8;
                            uint64_t t64 = (uintptr_t)t63;
                            uint64_t t65 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                            if (t64 >= (uintptr_t)(t65)) gm_trap_index(t64, t65, gm_src786i8duem00q, 217, 43);
                            void * t66 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t64 * 120) + 40);
                            uint8_t t67 = gm_f_binds_unknown_name_7m00m7a((void *)NULL, cx, NULL, t62, t66);
                            if (gm_unw.kind) {
                                goto gm_ll54;
                            }
                            if (!t67) goto ie61;
                            {
                                uint8_t t69 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix61;
ie61:;
ix61:;
                        }
                        goto me57;
mn58:;
                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 216, 13);
me57:;
                    }
lc50:;
                    {
                        uint64_t t71 = gm_l7;
                        uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                        uint64_t t74 = r72;
                        gm_l7 = t74;
                    }
                }
gm_ll54:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb50; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc50; }
                }
                goto gm_lad;
lb50:;
            }
        }
        {
            uint8_t t75 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn40:;
ma7:;
        uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t77 != ((uint8_t)8ULL)) goto mn76;
        gm_b16_8 t78 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l9 = t78;
        {
            {
                gm_l11 = ((uint64_t)0ULL);
            }
            {
                uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                gm_l10 = t81;
            }
            {
                for (;;) {
                    uint64_t t83 = gm_l11;
                    uint64_t t84 = gm_l10;
                    uint8_t t85 = (t83 < t84);
                    if (!t85) goto lb82;
                    {
                        uint64_t t88 = gm_l11;
                        uint64_t t91 = GM_LD(uint64_t, ((uint8_t *)&t88));
                        gm_l12 = t91;
                        {
                            void * t94 = gm_l0;
                            uint64_t t95 = gm_l12;
                            uint64_t t96 = (uintptr_t)t95;
                            uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            if (t96 >= (uintptr_t)(t97)) gm_trap_index(t96, t97, gm_src786i8duem00q, 223, 43);
                            void * t98 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t96 * 120) + 40);
                            uint8_t t99 = gm_f_binds_unknown_name_7m00m7a((void *)NULL, cx, NULL, t94, t98);
                            if (gm_unw.kind) {
                                goto gm_ll86;
                            }
                            if (!t99) goto ie93;
                            {
                                uint8_t t101 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix93;
ie93:;
ix93:;
                        }
                        goto me89;
mn90:;
                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 222, 13);
me89:;
                    }
lc82:;
                    {
                        uint64_t t103 = gm_l11;
                        uint64_t r104; bool o105 = __builtin_add_overflow(t103, ((uint64_t)1ULL), &r104);
                        uint64_t t106 = r104;
                        gm_l11 = t106;
                    }
                }
gm_ll86:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb82; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc82; }
                }
                goto gm_lad;
lb82:;
            }
        }
        {
            uint8_t t107 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn76:;
ma8:;
        uint8_t t109 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn108:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 211, 5);
me3:;
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
inline __attribute__((aligned(4))) void gm_f_check_arms_65v4isv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2) {
    struct gm_fr_check_arms_65v4isv F;
    void * gm_l0; /* cx */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    gm_b16_8 gm_l2; /* arms */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l7; /* a */
    GM_STACK_CHECK(&F, 294u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_f_list_nh3d5t2((void *)&F.l3, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_list_s83jn2q((void *)&F.l4, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t3 = gm_l2;
            gm_l5 = t3;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l6;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l6;
                    uint64_t t11 = (uintptr_t)t10;
                    uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_tbbqefuhv2r8h t15 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t11 * 24));
                    gm_l7 = t15;
                    {
                        void * t17 = (void *)((uint8_t *)&F.l3);
                        void * t18 = GM_LD(void *, (((uint8_t *)&gm_l7) + 0));
                        gm_tftb1odc739t5 t19 = gm_f_push_u2jka7o(t17, t18);
                        if (((((uint8_t *)&t19))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t19) + 1, gm_src786i8duem00q, 82, 9);
                        gm_b0_1 t20 = GM_LD(gm_b0_1, (((uint8_t *)&t19) + 1));
                    }
                    {
                        void * t21 = (void *)((uint8_t *)&F.l4);
                        void * t22 = GM_LD(void *, (((uint8_t *)&gm_l7) + 8));
                        uint8_t t23;
                        if ((GM_LD(void *, ((uint8_t *)&t22)) != 0)) goto mn25;
                        t23 = ((uint8_t)0);
                        goto me24;
mn25:;
                        t23 = ((uint8_t)1);
                        goto me24;
mn26:;
                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 83, 22);
me24:;
                        gm_tftb1odc739t5 t27 = gm_f_push_vd1290p(t21, t23);
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_src786i8duem00q, 83, 9);
                        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                    }
                    goto me13;
mn14:;
                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 81, 5);
me13:;
                }
lc4:;
                {
                    uint64_t t30 = gm_l6;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l6 = t33;
                }
            }
lb4:;
        }
    }
    {
        void * t34 = gm_l0;
        void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
        gm_t88rcucg1b7uk t36 = gm_l1;
        gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
        gm_b16_8 t38 = GM_LD(gm_b16_8, ((uint8_t *)&t37));
        gm_b16_8 t39 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
        gm_b16_8 t40 = GM_LD(gm_b16_8, ((uint8_t *)&t39));
        gm_f_check_match_mj577a4((void *)&(*(gm_tqs4ilbobv1qq *)gm_out), (void *)NULL, cx, NULL, t35, t36, t38, t40);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_check_deferred_match_742e2u5(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_check_deferred_match_742e2u5 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* e */
    void * gm_l2; /* scrutinee */
    gm_b16_8 gm_l3; /* arms */
    gm_tf5srh4mp4b3i gm_l4; /* found */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    gm_tbgd14esf0jvi gm_l8; /* d */
    uint64_t gm_l9; /* i */
    gm_tbgd14esf0jvi gm_l10; /* d */
    GM_STACK_CHECK(&F, 675u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)44ULL)) goto ie2;
        void * t5 = GM_LD(void *, (((((uint8_t *)t3) + 8) + 8) + 0));
        gm_l2 = t5;
        gm_b16_8 t6 = GM_LD(gm_b16_8, (((((uint8_t *)t3) + 8) + 8) + 8));
        gm_l3 = t6;
        {
            gm_tf5srh4mp4b3i t8;
            memset(&t8, 0, sizeof t8);
            gm_l4 = t8;
        }
        {
            {
                void * t10 = gm_l0;
                gm_b16_8 t11 = GM_LD(gm_b16_8, ((((uint8_t *)t10) + 480) + 0));
                gm_b16_8 t12 = GM_LD(gm_b16_8, ((uint8_t *)&t11));
                gm_l5 = t12;
            }
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t14 = gm_l6;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    uint8_t t16 = (t14 < t15);
                    if (!t16) goto lb13;
                    {
                        gm_tdlq2ugehok8g t19;
                        uint64_t t20 = gm_l6;
                        GM_ST(uint64_t, (uint8_t *)&t19 + 0, t20);
                        uint64_t t21 = gm_l6;
                        uint64_t t22 = (uintptr_t)t21;
                        uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                        memcpy((uint8_t *)&t19 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t22 * 32), 32);
                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&t19) + 0));
                        gm_l7 = t26;
                        gm_l8 = (*(gm_tbgd14esf0jvi *)(((uint8_t *)&t19) + 8));
                        {
                            gm_tl874t2atnlh3 t29 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l8) + 0));
                            void * t30 = gm_l1;
                            gm_tl874t2atnlh3 t31 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t30) + 72));
                            uint8_t t32 = (gm_eqff85ac10o3(((uint8_t *)&t29), ((uint8_t *)&t31)));
                            if (!t32) goto ie28;
                            {
                                uint64_t t34 = gm_l7;
                                gm_tf5srh4mp4b3i t35;
                                GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
                                GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
                                GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
                                GM_ST(uint64_t, ((uint8_t *)&t35), t34);
                                ((uint8_t *)&t35)[8] = 1;
                                gm_l4 = t35;
                            }
                            {
                                goto lb13;
                            }
                            goto ix28;
ie28:;
ix28:;
                        }
                        goto me24;
mn25:;
                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 105, 5);
me24:;
                    }
lc13:;
                    {
                        uint64_t t37 = gm_l6;
                        uint64_t r38; bool o39 = __builtin_add_overflow(t37, ((uint64_t)1ULL), &r38);
                        uint64_t t40 = r38;
                        gm_l6 = t40;
                    }
                }
lb13:;
            }
        }
        {
            gm_tf5srh4mp4b3i t41 = gm_l4;
            uint64_t t42;
            if (((((uint8_t *)&t41))[8] != 0)) {
                uint64_t t43 = GM_LD(uint64_t, (((uint8_t *)&t41) + 0));
                t42 = t43;
            } else {
                gm_b0_1 t44;
                uint8_t t45 = 0;
                F.ret = t44;
                goto gm_fret;
            }
            gm_l9 = t42;
        }
        {
            void * t46 = gm_l0;
            void * t47 = (void *)(((uint8_t *)t46) + 480);
            uint64_t t48 = gm_l9;
            gm_f_swap_remove_lfi9t15((void *)&gm_l10, t47, t48);
        }
        {
            void * t50 = gm_l0;
            void * t51 = GM_LD(void *, (((uint8_t *)t50) + 0));
            void * t52 = (void *)(((uint8_t *)t51) + 8);
            void * t53 = gm_l2;
            gm_t88rcucg1b7uk t54 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t53) + 0));
            uint8_t t55 = gm_f_is_error_8k27gd1(t52, GM_CONV(gm_csguankko4lmhd, t54));
            if (!t55) goto ie49;
            {
                gm_b0_1 t57;
                uint8_t t58 = 0;
                F.ret = t57;
                goto gm_fret;
            }
            goto ix49;
ie49:;
ix49:;
        }
        {
            void * t59 = gm_l0;
            void * t60 = gm_l0;
            void * t61 = gm_l2;
            gm_t88rcucg1b7uk t62 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t61) + 0));
            gm_b16_8 t63 = gm_l3;
            gm_b16_8 t64 = GM_LD(gm_b16_8, ((uint8_t *)&t63));
            gm_tqs4ilbobv1qq t65;
            gm_f_check_arms_65v4isv((void *)&t65, (void *)NULL, cx, NULL, t60, t62, t64);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tl874t2atnlh3 t66 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)&gm_l10) + 0));
            gm_b16_8 t67 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l10) + 16));
            gm_b16_8 t68 = GM_LD(gm_b16_8, ((uint8_t *)&t67));
            gm_b0_1 t69 = gm_f_report_match_d77kbfe((void *)NULL, cx, NULL, t59, &t65, t66, t68);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
        {
            gm_b0_1 t71;
            uint8_t t72 = 0;
            F.ret = t71;
            goto gm_fret;
        }
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
__attribute__((aligned(4))) uint8_t gm_f_contains_wildcard_jgbf94d(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_contains_wildcard_jgbf94d F;
    void * gm_l0; /* p */
    void * gm_l1; /* q */
    gm_tagc77imt2chm gm_l2; /* pp */
    gm_b16_8 gm_l3; /* args */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_b16_8 gm_l7; /* args */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    GM_STACK_CHECK(&F, 452u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t2 = gm_l0;
        uint8_t t10 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma8; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma8; case ((uint8_t)5ULL): goto ma8; case ((uint8_t)6ULL): goto ma5; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma7; default: goto ma8; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        uint8_t t13 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
        goto me3;
mn11:;
ma5:;
        uint8_t t15 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t15 != ((uint8_t)6ULL)) goto mn14;
        void * t16 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l1 = t16;
        void * t17 = gm_l1;
        uint8_t t18 = gm_f_contains_wildcard_jgbf94d(t17);
        uint8_t t19 = 0;
        F.ret = t18;
        goto gm_fret;
        goto me3;
mn14:;
ma6:;
        uint8_t t21 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t21 != ((uint8_t)7ULL)) goto mn20;
        gm_tagc77imt2chm t22 = GM_LD(gm_tagc77imt2chm, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t22;
        {
            gm_th2lqbo77pvsc t24 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&gm_l2) + 8));
            gm_b16_8 t25;
            if (((((uint8_t *)&t24))[16] != 0)) {
                gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&t24) + 0));
                t25 = t26;
            } else {
                uint8_t t27 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l3 = t25;
        }
        {
            {
                gm_l5 = ((uint64_t)0ULL);
            }
            {
                uint64_t t29 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l4 = t29;
            }
            {
                for (;;) {
                    uint64_t t31 = gm_l5;
                    uint64_t t32 = gm_l4;
                    uint8_t t33 = (t31 < t32);
                    if (!t33) goto lb30;
                    {
                        uint64_t t36 = gm_l5;
                        uint64_t t39 = GM_LD(uint64_t, ((uint8_t *)&t36));
                        gm_l6 = t39;
                        {
                            uint64_t t42 = gm_l6;
                            uint64_t t43 = (uintptr_t)t42;
                            uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t43 >= (uintptr_t)(t44)) gm_trap_index(t43, t44, gm_src786i8duem00q, 367, 39);
                            void * t45 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t43 * 120) + 40);
                            uint8_t t46 = gm_f_contains_wildcard_jgbf94d(t45);
                            if (!t46) goto ie41;
                            {
                                uint8_t t48 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix41;
ie41:;
ix41:;
                        }
                        goto me37;
mn38:;
                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 366, 13);
me37:;
                    }
lc30:;
                    {
                        uint64_t t50 = gm_l5;
                        uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                        uint64_t t53 = r51;
                        gm_l5 = t53;
                    }
                }
lb30:;
            }
        }
        {
            uint8_t t54 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn20:;
ma7:;
        uint8_t t56 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t56 != ((uint8_t)8ULL)) goto mn55;
        gm_b16_8 t57 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l7 = t57;
        {
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                uint64_t t60 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                gm_l8 = t60;
            }
            {
                for (;;) {
                    uint64_t t62 = gm_l9;
                    uint64_t t63 = gm_l8;
                    uint8_t t64 = (t62 < t63);
                    if (!t64) goto lb61;
                    {
                        uint64_t t67 = gm_l9;
                        uint64_t t70 = GM_LD(uint64_t, ((uint8_t *)&t67));
                        gm_l10 = t70;
                        {
                            uint64_t t73 = gm_l10;
                            uint64_t t74 = (uintptr_t)t73;
                            uint64_t t75 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                            if (t74 >= (uintptr_t)(t75)) gm_trap_index(t74, t75, gm_src786i8duem00q, 373, 39);
                            void * t76 = (void *)(((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t74 * 120) + 40);
                            uint8_t t77 = gm_f_contains_wildcard_jgbf94d(t76);
                            if (!t77) goto ie72;
                            {
                                uint8_t t79 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix72;
ie72:;
ix72:;
                        }
                        goto me68;
mn69:;
                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 372, 13);
me68:;
                    }
lc61:;
                    {
                        uint64_t t81 = gm_l9;
                        uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                        uint64_t t84 = r82;
                        gm_l9 = t84;
                    }
                }
lb61:;
            }
        }
        {
            uint8_t t85 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto me3;
mn55:;
ma8:;
        uint8_t t87 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me3;
mn86:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 361, 5);
me3:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_expr_to_pattern_rchr7ig(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_expr_to_pattern_rchr7ig F;
    void * gm_l0; /* e */
    void * gm_l2; /* callee */
    gm_b16_8 gm_l3; /* args */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    uint64_t gm_l7; /* i */
    void * gm_l8; /* a */
    gm_t08olq97rscn3 gm_l11; /* n */
    gm_t9qkh0rsooc8d gm_l14; /* op */
    void * gm_l15; /* operand */
    gm_b16_8 gm_l17; /* elems */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    uint64_t gm_l21; /* i */
    void * gm_l22; /* el */
    void * gm_l24; /* inner */
    GM_STACK_CHECK(&F, 2864u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        memset(&F.l1, 0, sizeof F.l1);
        GM_ST(uint8_t, (uint8_t *)&F.l1 + 0, ((uint8_t)0ULL));
    }
    {
        void * t2 = gm_l0;
        uint8_t t14 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma12; case ((uint8_t)2ULL): goto ma9; case ((uint8_t)3ULL): goto ma9; case ((uint8_t)4ULL): goto ma5; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma4; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma8; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma7; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma6; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; case ((uint8_t)27ULL): goto ma12; case ((uint8_t)28ULL): goto ma12; case ((uint8_t)29ULL): goto ma12; case ((uint8_t)30ULL): goto ma12; case ((uint8_t)31ULL): goto ma12; case ((uint8_t)32ULL): goto ma12; case ((uint8_t)33ULL): goto ma12; case ((uint8_t)34ULL): goto ma12; case ((uint8_t)35ULL): goto ma12; case ((uint8_t)36ULL): goto ma12; case ((uint8_t)37ULL): goto ma12; case ((uint8_t)38ULL): goto ma12; case ((uint8_t)39ULL): goto ma12; case ((uint8_t)40ULL): goto ma12; case ((uint8_t)41ULL): goto ma12; case ((uint8_t)42ULL): goto ma12; case ((uint8_t)43ULL): goto ma12; case ((uint8_t)44ULL): goto ma12; case ((uint8_t)45ULL): goto ma12; case ((uint8_t)46ULL): goto ma12; case ((uint8_t)47ULL): goto ma12; case ((uint8_t)48ULL): goto ma12; case ((uint8_t)49ULL): goto ma12; case ((uint8_t)50ULL): goto ma12; default: goto ma12; }
ma4:;
        uint8_t t16 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t16 != ((uint8_t)7ULL)) goto mn15;
        goto me3;
mn15:;
ma5:;
        uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t19 != ((uint8_t)4ULL)) goto mn18;
        {
            memset(&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)), 0, sizeof (*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 0, ((uint8_t)4ULL));
        }
        goto me3;
mn18:;
ma6:;
        uint8_t t24 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t24 != ((uint8_t)8ULL)) goto pa23;
        goto po22;
pa23:;
        uint8_t t26 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t26 != ((uint8_t)22ULL)) goto pa25;
        goto po22;
pa25:;
        goto mn21;
po22:;
        {
            gm_tagc77imt2chm t28;
            void * t29 = gm_l0;
            gm_tq7s98ihonkju t30 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t29)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ku35m9tsdej)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_src786i8duem00q, 307, 73);
            void * t31 = GM_LD(void *, (((uint8_t *)&t30) + 8));
            GM_ST(void *, (uint8_t *)&t28 + 0, t31);
            gm_th2lqbo77pvsc t32;
            memset(&t32, 0, sizeof t32);
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t28 + 8, t32);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 0, ((uint8_t)7ULL));
            GM_ST(gm_tagc77imt2chm, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 8, t28);
        }
        goto me3;
mn21:;
ma7:;
        uint8_t t34 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t34 != ((uint8_t)19ULL)) goto mn33;
        void * t35 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l2 = t35;
        gm_b16_8 t36 = GM_LD(gm_b16_8, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l3 = t36;
        {
            void * t39 = gm_l2;
            uint8_t t40;
            uint8_t t43 = GM_LD(uint8_t, ((((uint8_t *)t39) + 8) + 0));
            if (t43 != ((uint8_t)8ULL)) goto mn42;
            t40 = ((uint8_t)0);
            goto me41;
mn42:;
            t40 = ((uint8_t)1);
            goto me41;
mn44:;
            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 309, 16);
me41:;
            if (!t40) goto ie38;
            void * t45 = gm_l2;
            uint8_t t46;
            uint8_t t49 = GM_LD(uint8_t, ((((uint8_t *)t45) + 8) + 0));
            if (t49 != ((uint8_t)22ULL)) goto mn48;
            t46 = ((uint8_t)0);
            goto me47;
mn48:;
            t46 = ((uint8_t)1);
            goto me47;
mn50:;
            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 309, 52);
me47:;
            if (!t46) goto ie38;
            {
                gm_toqf51949ppls t52;
                memset(&t52, 0, sizeof t52);
                uint8_t t53 = 0;
                F.ret = t52;
                goto gm_fret;
            }
            goto ix38;
ie38:;
ix38:;
        }
        {
            gm_f_list_ttksl9g((void *)&F.l4, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l6 = ((uint64_t)0ULL);
            }
            {
                uint64_t t55 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                gm_l5 = t55;
            }
            {
                for (;;) {
                    uint64_t t57 = gm_l6;
                    uint64_t t58 = gm_l5;
                    uint8_t t59 = (t57 < t58);
                    if (!t59) goto lb56;
                    {
                        uint64_t t62 = gm_l6;
                        uint64_t t65 = GM_LD(uint64_t, ((uint8_t *)&t62));
                        gm_l7 = t65;
                        {
                            uint64_t t67 = gm_l7;
                            uint64_t t68 = (uintptr_t)t67;
                            uint64_t t69 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                            if (t68 >= (uintptr_t)(t69)) gm_trap_index(t68, t69, gm_src786i8duem00q, 312, 23);
                            void * t70 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t68 * 144);
                            gm_l8 = t70;
                        }
                        {
                            void * t71 = gm_l8;
                            uint8_t t77 = GM_LD(uint8_t, ((((uint8_t *)t71) + 0) + 0));
                            switch (t77) { case ((uint8_t)0ULL): goto ma73; case ((uint8_t)1ULL): goto ma74; case ((uint8_t)2ULL): goto ma75; default: goto ma76; }
ma73:;
                            uint8_t t79 = GM_LD(uint8_t, ((((uint8_t *)t71) + 0) + 0));
                            if (t79 != ((uint8_t)0ULL)) goto mn78;
                            gm_tc3oht4ksth9d t80 = GM_LD(gm_tc3oht4ksth9d, ((((uint8_t *)t71) + 0) + 8));
                            F.l9 = t80;
                            {
                                void * t82 = (void *)((uint8_t *)&F.l9);
                                gm_toqf51949ppls t83;
                                gm_f_nested_pattern_oiclpgf((void *)&t83, (void *)NULL, cx, NULL, t82);
                                if (gm_unw.kind) {
                                    goto gm_ll60;
                                }
                                gm_t0d02142k4tu8 t84;
                                if (((((uint8_t *)&t83))[64] != 0)) {
                                    t84 = (*(gm_t0d02142k4tu8 *)(((uint8_t *)&t83) + 0));
                                } else {
                                    gm_toqf51949ppls t85;
                                    memset(&t85, 0, sizeof t85);
                                    uint8_t t86 = 0;
                                    F.ret = t85;
                                    goto gm_fret;
                                }
                                F.l10 = t84;
                            }
                            {
                                void * t87 = (void *)((uint8_t *)&F.l4);
                                gm_tnrgict8sthcv t88;
                                memset((uint8_t *)&t88 + 116, 0, 4);
                                gm_t1s75i08qtptd t89;
                                memset(&t89, 0, sizeof t89);
                                GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t88 + 0, t89);
                                memcpy((uint8_t *)&t88 + 40, ((uint8_t *)&F.l10), 64);
                                void * t90 = gm_l8;
                                gm_tl874t2atnlh3 t91 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t90) + 128));
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t88 + 104, t91);
                                gm_tftb1odc739t5 t92 = gm_f_push_l74t9rq(t87, &t88);
                                if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 1, gm_src786i8duem00q, 316, 25);
                                gm_b0_1 t93 = GM_LD(gm_b0_1, (((uint8_t *)&t92) + 1));
                            }
                            goto me72;
mn78:;
ma74:;
                            uint8_t t95 = GM_LD(uint8_t, ((((uint8_t *)t71) + 0) + 0));
                            if (t95 != ((uint8_t)1ULL)) goto mn94;
                            gm_t08olq97rscn3 t96 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t71) + 0) + 8) + 0));
                            gm_l11 = t96;
                            gm_tc3oht4ksth9d t97 = GM_LD(gm_tc3oht4ksth9d, (((((uint8_t *)t71) + 0) + 8) + 32));
                            F.l12 = t97;
                            {
                                void * t99 = (void *)((uint8_t *)&F.l12);
                                gm_toqf51949ppls t100;
                                gm_f_nested_pattern_oiclpgf((void *)&t100, (void *)NULL, cx, NULL, t99);
                                if (gm_unw.kind) {
                                    goto gm_ll60;
                                }
                                gm_t0d02142k4tu8 t101;
                                if (((((uint8_t *)&t100))[64] != 0)) {
                                    t101 = (*(gm_t0d02142k4tu8 *)(((uint8_t *)&t100) + 0));
                                } else {
                                    gm_toqf51949ppls t102;
                                    memset(&t102, 0, sizeof t102);
                                    uint8_t t103 = 0;
                                    F.ret = t102;
                                    goto gm_fret;
                                }
                                F.l13 = t101;
                            }
                            {
                                void * t104 = (void *)((uint8_t *)&F.l4);
                                gm_tnrgict8sthcv t105;
                                memset((uint8_t *)&t105 + 116, 0, 4);
                                gm_t08olq97rscn3 t106 = gm_l11;
                                gm_t1s75i08qtptd t107;
                                GM_ST(uint8_t, (uint8_t *)&t107 + 33, 0);
                                GM_ST(uint16_t, (uint8_t *)&t107 + 34, 0);
                                GM_ST(uint32_t, (uint8_t *)&t107 + 36, 0);
                                GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t107), t106);
                                ((uint8_t *)&t107)[32] = 1;
                                GM_ST(gm_t1s75i08qtptd, (uint8_t *)&t105 + 0, t107);
                                memcpy((uint8_t *)&t105 + 40, ((uint8_t *)&F.l13), 64);
                                void * t108 = gm_l8;
                                gm_tl874t2atnlh3 t109 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t108) + 128));
                                GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t105 + 104, t109);
                                gm_tftb1odc739t5 t110 = gm_f_push_l74t9rq(t104, &t105);
                                if (((((uint8_t *)&t110))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t110) + 1, gm_src786i8duem00q, 320, 25);
                                gm_b0_1 t111 = GM_LD(gm_b0_1, (((uint8_t *)&t110) + 1));
                            }
                            goto me72;
mn94:;
ma75:;
                            uint8_t t113 = GM_LD(uint8_t, ((((uint8_t *)t71) + 0) + 0));
                            if (t113 != ((uint8_t)2ULL)) goto mn112;
                            gm_toqf51949ppls t114;
                            memset(&t114, 0, sizeof t114);
                            uint8_t t115 = 0;
                            F.ret = t114;
                            goto gm_fret;
                            goto me72;
mn112:;
ma76:;
                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 313, 17);
me72:;
                        }
                        goto me63;
mn64:;
                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 311, 13);
me63:;
                    }
lc56:;
                    {
                        uint64_t t117 = gm_l6;
                        uint64_t r118; bool o119 = __builtin_add_overflow(t117, ((uint64_t)1ULL), &r118);
                        uint64_t t120 = r118;
                        gm_l6 = t120;
                    }
                }
gm_ll60:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb56; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc56; }
                }
                goto gm_lad;
lb56:;
            }
        }
        {
            gm_tagc77imt2chm t121;
            void * t122 = gm_l2;
            GM_ST(void *, (uint8_t *)&t121 + 0, t122);
            gm_b16_8 t123 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
            gm_th2lqbo77pvsc t124;
            GM_ST(uint8_t, (uint8_t *)&t124 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t124 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t124 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t124), t123);
            ((uint8_t *)&t124)[16] = 1;
            GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&t121 + 8, t124);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 0, ((uint8_t)7ULL));
            GM_ST(gm_tagc77imt2chm, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 8, t121);
        }
        goto me3;
mn33:;
ma8:;
        uint8_t t126 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t126 != ((uint8_t)12ULL)) goto mn125;
        gm_t9qkh0rsooc8d t127 = GM_LD(gm_t9qkh0rsooc8d, (((((uint8_t *)t2) + 8) + 8) + 0));
        gm_l14 = t127;
        void * t128 = GM_LD(void *, (((((uint8_t *)t2) + 8) + 8) + 8));
        gm_l15 = t128;
        {
            uint8_t t131 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l14) + 0) == ((uint8_t)3ULL)));
            if (!t131) goto ie130;
            {
                void * t133 = gm_l15;
                gm_toqf51949ppls t134;
                gm_f_expr_to_pattern_rchr7ig((void *)&t134, (void *)NULL, cx, NULL, t133);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t0d02142k4tu8 t135;
                if (((((uint8_t *)&t134))[64] != 0)) {
                    t135 = (*(gm_t0d02142k4tu8 *)(((uint8_t *)&t134) + 0));
                } else {
                    gm_toqf51949ppls t136;
                    memset(&t136, 0, sizeof t136);
                    uint8_t t137 = 0;
                    F.ret = t136;
                    goto gm_fret;
                }
                F.l16 = t135;
            }
            {
                gm_tq7s98ihonkju t138 = gm_f_new_f7qroea((void *)NULL, cx, NULL, &(*(gm_t0d02142k4tu8 *)((uint8_t *)&F.l16)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdhojj6r45o)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t138))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t138) + 8, gm_src786i8duem00q, 330, 29);
                void * t139 = GM_LD(void *, (((uint8_t *)&t138) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 32, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 0, ((uint8_t)6ULL));
                GM_ST(void *, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 8, t139);
            }
            goto ix130;
ie130:;
            uint8_t t141 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l14) + 0) == ((uint8_t)0ULL)));
            if (!t141) goto ie140;
            void * t142 = gm_l15;
            uint8_t t143;
            uint8_t t146 = GM_LD(uint8_t, ((((uint8_t *)t142) + 8) + 0));
            if (t146 != ((uint8_t)0ULL)) goto mn145;
            t143 = ((uint8_t)1);
            goto me144;
mn145:;
            t143 = ((uint8_t)0);
            goto me144;
mn147:;
            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 331, 38);
me144:;
            if (!t143) goto ie140;
            {
                void * t149 = gm_l0;
                gm_tq7s98ihonkju t150 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t149)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k9foool24ve)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                if (((((uint8_t *)&t150))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t150) + 8, gm_src786i8duem00q, 332, 29);
                void * t151 = GM_LD(void *, (((uint8_t *)&t150) + 8));
                GM_ST(uint8_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 16, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 24, 0);
                GM_ST(uint64_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 32, 0);
                GM_ST(uint8_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 0, ((uint8_t)3ULL));
                GM_ST(void *, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 8, t151);
            }
            goto ix140;
ie140:;
            {
                gm_toqf51949ppls t153;
                memset(&t153, 0, sizeof t153);
                uint8_t t154 = 0;
                F.ret = t153;
                goto gm_fret;
            }
ix140:;
ix130:;
        }
        goto me3;
mn125:;
ma9:;
        uint8_t t158 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t158 != ((uint8_t)0ULL)) goto pa157;
        goto po156;
pa157:;
        uint8_t t160 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t160 != ((uint8_t)2ULL)) goto pa159;
        goto po156;
pa159:;
        uint8_t t162 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t162 != ((uint8_t)3ULL)) goto pa161;
        goto po156;
pa161:;
        goto mn155;
po156:;
        {
            void * t164 = gm_l0;
            gm_tq7s98ihonkju t165 = gm_f_new_ve8bb01((void *)NULL, cx, NULL, &(*(gm_tc3oht4ksth9d *)((uint8_t *)t164)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kns860jaqph)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t165))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t165) + 8, gm_src786i8duem00q, 337, 53);
            void * t166 = GM_LD(void *, (((uint8_t *)&t165) + 8));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 16, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 32, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 0, ((uint8_t)3ULL));
            GM_ST(void *, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 8, t166);
        }
        goto me3;
mn155:;
ma10:;
        uint8_t t168 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t168 != ((uint8_t)10ULL)) goto mn167;
        gm_b16_8 t169 = GM_LD(gm_b16_8, ((((uint8_t *)t2) + 8) + 8));
        gm_l17 = t169;
        {
            gm_f_list_ttksl9g((void *)&F.l18, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l20 = ((uint64_t)0ULL);
            }
            {
                uint64_t t172 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                gm_l19 = t172;
            }
            {
                for (;;) {
                    uint64_t t174 = gm_l20;
                    uint64_t t175 = gm_l19;
                    uint8_t t176 = (t174 < t175);
                    if (!t176) goto lb173;
                    {
                        uint64_t t179 = gm_l20;
                        uint64_t t182 = GM_LD(uint64_t, ((uint8_t *)&t179));
                        gm_l21 = t182;
                        {
                            uint64_t t184 = gm_l21;
                            uint64_t t185 = (uintptr_t)t184;
                            uint64_t t186 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                            if (t185 >= (uintptr_t)(t186)) gm_trap_index(t185, t186, gm_src786i8duem00q, 341, 24);
                            void * t187 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t185 * 136);
                            gm_l22 = t187;
                        }
                        {
                            void * t189 = gm_l22;
                            uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)t189) + 128));
                            if (!t190) goto ie188;
                            {
                                gm_toqf51949ppls t192;
                                memset(&t192, 0, sizeof t192);
                                uint8_t t193 = 0;
                                F.ret = t192;
                                goto gm_fret;
                            }
                            goto ix188;
ie188:;
ix188:;
                        }
                        {
                            void * t194 = gm_l22;
                            void * t195 = (void *)(((uint8_t *)t194) + 40);
                            gm_toqf51949ppls t196;
                            gm_f_nested_pattern_oiclpgf((void *)&t196, (void *)NULL, cx, NULL, t195);
                            if (gm_unw.kind) {
                                goto gm_ll177;
                            }
                            gm_t0d02142k4tu8 t197;
                            if (((((uint8_t *)&t196))[64] != 0)) {
                                t197 = (*(gm_t0d02142k4tu8 *)(((uint8_t *)&t196) + 0));
                            } else {
                                gm_toqf51949ppls t198;
                                memset(&t198, 0, sizeof t198);
                                uint8_t t199 = 0;
                                F.ret = t198;
                                goto gm_fret;
                            }
                            F.l23 = t197;
                        }
                        {
                            void * t200 = (void *)((uint8_t *)&F.l18);
                            gm_tnrgict8sthcv t201;
                            memset((uint8_t *)&t201 + 116, 0, 4);
                            void * t202 = gm_l22;
                            memcpy((uint8_t *)&t201 + 0, (((uint8_t *)t202) + 0), 40);
                            memcpy((uint8_t *)&t201 + 40, ((uint8_t *)&F.l23), 64);
                            void * t203 = gm_l22;
                            gm_tl874t2atnlh3 t204 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t203) + 40) + 72));
                            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t201 + 104, t204);
                            gm_tftb1odc739t5 t205 = gm_f_push_l74t9rq(t200, &t201);
                            if (((((uint8_t *)&t205))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t205) + 1, gm_src786i8duem00q, 344, 17);
                            gm_b0_1 t206 = GM_LD(gm_b0_1, (((uint8_t *)&t205) + 1));
                        }
                        goto me180;
mn181:;
                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 340, 13);
me180:;
                    }
lc173:;
                    {
                        uint64_t t208 = gm_l20;
                        uint64_t r209; bool o210 = __builtin_add_overflow(t208, ((uint64_t)1ULL), &r209);
                        uint64_t t211 = r209;
                        gm_l20 = t211;
                    }
                }
gm_ll177:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb173; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc173; }
                }
                goto gm_lad;
lb173:;
            }
        }
        {
            gm_b16_8 t212 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 32, 0);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 0, ((uint8_t)8ULL));
            GM_ST(gm_b16_8, (uint8_t *)&(*(gm_to361lgknsu69 *)((uint8_t *)&F.l1)) + 8, t212);
        }
        goto me3;
mn167:;
ma11:;
        uint8_t t214 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t214 != ((uint8_t)9ULL)) goto mn213;
        void * t215 = GM_LD(void *, ((((uint8_t *)t2) + 8) + 8));
        gm_l24 = t215;
        void * t216 = gm_l24;
        gm_f_expr_to_pattern_rchr7ig((void *)&(*(gm_toqf51949ppls *)gm_out), (void *)NULL, cx, NULL, t216);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me3;
mn213:;
ma12:;
        gm_toqf51949ppls t218;
        memset(&t218, 0, sizeof t218);
        uint8_t t219 = 0;
        F.ret = t218;
        goto gm_fret;
        goto me3;
mn217:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 304, 5);
me3:;
    }
    {
        gm_t0d02142k4tu8 t220;
        memset((uint8_t *)&t220 + 4, 0, 4);
        memset((uint8_t *)&t220 + 60, 0, 4);
        void * t221 = gm_l0;
        uint32_t t222 = GM_LD(uint32_t, (((uint8_t *)t221) + 0));
        GM_ST(uint32_t, (uint8_t *)&t220 + 0, t222);
        memcpy((uint8_t *)&t220 + 8, ((uint8_t *)&F.l1), 40);
        void * t223 = gm_l0;
        gm_tl874t2atnlh3 t224 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t223) + 72));
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t220 + 48, t224);
        GM_ST(uint8_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 65, 0);
        GM_ST(uint16_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 66, 0);
        GM_ST(uint32_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 68, 0);
        ((uint8_t *)&(*(gm_toqf51949ppls *)gm_out))[64] = 1;
        GM_ST(gm_t0d02142k4tu8, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 0, t220);
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ho0p9pu(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) void * gm_f_in_patterns_mi6p7hb(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_b16_8 a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_in_patterns_mi6p7hb F;
    void * gm_l0; /* cx */
    void * gm_l1; /* lhs */
    gm_b16_8 gm_l2; /* members */
    uint8_t gm_l3; /* negated */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* ty */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    void * gm_l11; /* m */
    void * gm_l15; /* pat */
    void * gm_l16; /* otherwise */
    GM_STACK_CHECK(&F, 1570u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l1;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t11;
    }
    {
        void * t13 = gm_l0;
        gm_t88rcucg1b7uk t14 = gm_l6;
        uint8_t t15 = gm_f_is_c_union_p1r4eve(t13, GM_CONV(gm_csguankko4lmhd, t14));
        if (!t15) goto ie12;
        {
            void * t17 = gm_l0;
            void * t18 = gm_l1;
            gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t18) + 72));
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
            gm_t88rcucg1b7uk t22 = gm_l6;
            gm_b16_8 t23 = gm_f_c_union_match_2tgsktr((void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t24 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t17, t19, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t25 = gm_l5;
            gm_t88rcucg1b7uk t26 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t26;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        gm_f_list_58nr42v_3((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l9 = ((uint64_t)0ULL);
        }
        {
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            gm_l8 = t28;
        }
        {
            for (;;) {
                uint64_t t30 = gm_l9;
                uint64_t t31 = gm_l8;
                uint8_t t32 = (t30 < t31);
                if (!t32) goto lb29;
                {
                    uint64_t t35 = gm_l9;
                    uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&t35));
                    gm_l10 = t38;
                    {
                        uint64_t t40 = gm_l10;
                        uint64_t t41 = (uintptr_t)t40;
                        uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t41 >= (uintptr_t)(t42)) gm_trap_index(t41, t42, gm_src786i8duem00q, 267, 15);
                        void * t43 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t41 * 136);
                        gm_l11 = t43;
                    }
                    {
                        gm_toqf51949ppls t44;
                        void * t46 = gm_l11;
                        uint8_t t47;
                        if ((((((uint8_t *)t46) + 0))[32] != 0)) goto mn49;
                        t47 = ((uint8_t)1);
                        goto me48;
mn49:;
                        t47 = ((uint8_t)0);
                        goto me48;
mn50:;
                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 268, 30);
me48:;
                        if (!t47) goto ie45;
                        void * t52 = gm_l11;
                        void * t53 = (void *)(((uint8_t *)t52) + 40);
                        gm_toqf51949ppls t54;
                        gm_f_expr_to_pattern_rchr7ig((void *)&t54, (void *)NULL, cx, NULL, t53);
                        if (gm_unw.kind) {
                            goto gm_ll33;
                        }
                        gm_toqf51949ppls t55 = t54;
                        t44 = t55;
                        goto ix45;
ie45:;
                        gm_toqf51949ppls t57;
                        memset(&t57, 0, sizeof t57);
                        gm_toqf51949ppls t58 = t57;
                        t44 = t58;
ix45:;
                        F.l12 = t44;
                    }
                    {
                        gm_toqf51949ppls t59 = F.l12;
                        gm_t0d02142k4tu8 t60;
                        if (((((uint8_t *)&t59))[64] != 0)) {
                            t60 = (*(gm_t0d02142k4tu8 *)(((uint8_t *)&t59) + 0));
                        } else {
                            {
                                void * t62 = gm_l0;
                                void * t63 = gm_l11;
                                gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, ((((uint8_t *)t63) + 40) + 72));
                                gm_b16_8 t65;
                                GM_ST(void *, (uint8_t *)&t65, (void *)(gm_slbqo2q5mg776));
                                GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(106));
                                gm_b0_1 t66 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t62, t64, t65);
                                if (gm_unw.kind) {
                                    goto gm_ll33;
                                }
                            }
                            void * t67 = gm_l0;
                            void * t68 = GM_LD(void *, (((uint8_t *)t67) + 0));
                            gm_tl874t2atnlh3 t69 = gm_l4;
                            void * t70 = gm_f_error_expr_o6qt7e2((void *)NULL, cx, NULL, t68, t69);
                            if (gm_unw.kind) {
                                goto gm_ll33;
                            }
                            uint8_t t71 = 0;
                            F.ret = t70;
                            goto gm_fret;
                        }
                        F.l13 = t60;
                    }
                    {
                        gm_f_list_6l26q4a((void *)&F.l14, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_ll33;
                        }
                    }
                    {
                        void * t72 = gm_l0;
                        void * t73 = (void *)((uint8_t *)&F.l13);
                        gm_t88rcucg1b7uk t74 = gm_l6;
                        gm_t9qkh0rsooc8d t75;
                        GM_ST(uint8_t, ((uint8_t *)&t75 + 0), ((uint8_t)2ULL));
                        void * t76 = (void *)((uint8_t *)&F.l14);
                        void * t77 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t72, t73, t74, t75, t76);
                        if (gm_unw.kind) {
                            goto gm_ll33;
                        }
                        gm_l15 = t77;
                    }
                    {
                        void * t78 = (void *)((uint8_t *)&F.l7);
                        gm_tbbqefuhv2r8h t79;
                        void * t80 = gm_l15;
                        GM_ST(void *, (uint8_t *)&t79 + 0, t80);
                        GM_ST(void *, (uint8_t *)&t79 + 8, ((void *)0));
                        void * t81 = gm_l5;
                        gm_t88rcucg1b7uk t82 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t81);
                        if (gm_unw.kind) {
                            goto gm_ll33;
                        }
                        uint8_t t83 = gm_l3;
                        uint8_t t84 = (uint8_t)!t83;
                        gm_teti411eujvf4 t85;
                        memset(&t85, 0, sizeof t85);
                        GM_ST(uint8_t, ((uint8_t *)&t85 + 0), ((uint8_t)4ULL));
                        GM_ST(uint8_t, ((uint8_t *)&t85 + 8), t84);
                        gm_tl874t2atnlh3 t86 = gm_l4;
                        void * t87 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t82, &t85, t86);
                        if (gm_unw.kind) {
                            goto gm_ll33;
                        }
                        GM_ST(void *, (uint8_t *)&t79 + 16, t87);
                        gm_tftb1odc739t5 t88 = gm_f_push_bpovr2k_3(t78, &t79);
                        if (((((uint8_t *)&t88))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t88) + 1, gm_src786i8duem00q, 275, 9);
                        gm_b0_1 t89 = GM_LD(gm_b0_1, (((uint8_t *)&t88) + 1));
                    }
                    goto me36;
mn37:;
                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 266, 5);
me36:;
                }
lc29:;
                {
                    uint64_t t91 = gm_l9;
                    uint64_t r92; bool o93 = __builtin_add_overflow(t91, ((uint64_t)1ULL), &r92);
                    uint64_t t94 = r92;
                    gm_l9 = t94;
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
        void * t95 = gm_l5;
        gm_t88rcucg1b7uk t96 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t95);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t97 = gm_l3;
        gm_teti411eujvf4 t98;
        memset(&t98, 0, sizeof t98);
        GM_ST(uint8_t, ((uint8_t *)&t98 + 0), ((uint8_t)4ULL));
        GM_ST(uint8_t, ((uint8_t *)&t98 + 8), t97);
        gm_tl874t2atnlh3 t99 = gm_l4;
        void * t100 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t96, &t98, t99);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l16 = t100;
    }
    {
        void * t101 = (void *)((uint8_t *)&F.l7);
        gm_tbbqefuhv2r8h t102;
        gm_t88rcucg1b7uk t103 = gm_l6;
        gm_tb38v5s9j1r32 t104;
        memset(&t104, 0, sizeof t104);
        GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)0ULL));
        gm_tl874t2atnlh3 t105 = gm_l4;
        void * t106 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t103, &t104, t105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t102 + 0, t106);
        GM_ST(void *, (uint8_t *)&t102 + 8, ((void *)0));
        void * t107 = gm_l16;
        GM_ST(void *, (uint8_t *)&t102 + 16, t107);
        gm_tftb1odc739t5 t108 = gm_f_push_bpovr2k_3(t101, &t102);
        if (((((uint8_t *)&t108))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t108) + 1, gm_src786i8duem00q, 278, 5);
        gm_b0_1 t109 = GM_LD(gm_b0_1, (((uint8_t *)&t108) + 1));
    }
    {
        void * t110 = gm_l5;
        gm_t88rcucg1b7uk t111 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t110);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkaapcii86udk t112;
        void * t113 = gm_l1;
        GM_ST(void *, (uint8_t *)&t112 + 0, t113);
        gm_b16_8 t114 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t112 + 8, t114);
        gm_teti411eujvf4 t115;
        memset(&t115, 0, sizeof t115);
        GM_ST(uint8_t, ((uint8_t *)&t115 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t115 + 8), t112);
        gm_tl874t2atnlh3 t116 = gm_l4;
        void * t117 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t111, &t115, t116);
        if (gm_unw.kind) {
            goto gm_lad;
        }
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
inline __attribute__((aligned(4))) void gm_f_is_pattern_operand_p51tban(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_is_pattern_operand_p51tban F;
    void * gm_l0; /* cx */
    void * gm_l1; /* rhs */
    gm_t88rcucg1b7uk gm_l2; /* lhs_ty */
    gm_tagc77imt2chm gm_l4; /* pp */
    void * gm_l5; /* base */
    GM_STACK_CHECK(&F, 686u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l1;
        gm_toqf51949ppls t3;
        gm_f_expr_to_pattern_rchr7ig((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t0d02142k4tu8 t4;
        if (((((uint8_t *)&t3))[64] != 0)) {
            t4 = (*(gm_t0d02142k4tu8 *)(((uint8_t *)&t3) + 0));
        } else {
            gm_toqf51949ppls t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        F.l3 = t4;
    }
    {
        uint8_t t8 = GM_LD(uint8_t, ((((uint8_t *)&F.l3) + 8) + 0));
        if (t8 != ((uint8_t)7ULL)) goto ie7;
        gm_tagc77imt2chm t9 = GM_LD(gm_tagc77imt2chm, ((((uint8_t *)&F.l3) + 8) + 8));
        gm_l4 = t9;
        void * t10 = GM_LD(void *, (((uint8_t *)&gm_l4) + 0));
        uint8_t t11 = GM_LD(uint8_t, ((((uint8_t *)t10) + 8) + 0));
        if (t11 != ((uint8_t)22ULL)) goto ie7;
        void * t12 = GM_LD(void *, (((((uint8_t *)t10) + 8) + 8) + 0));
        gm_l5 = t12;
        void * t13 = gm_l0;
        void * t14 = gm_l5;
        gm_tkn42u7na3t81 t15 = gm_f_type_of_name_oqtujpn((void *)NULL, cx, NULL, t13, t14);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t16;
        if (((((uint8_t *)&t15))[4] != 0)) goto mn18;
        t16 = ((uint8_t)1);
        goto me17;
mn18:;
        t16 = ((uint8_t)0);
        goto me17;
mn19:;
        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 201, 70);
me17:;
        if (!t16) goto ie7;
        {
            gm_toqf51949ppls t21;
            memset(&t21, 0, sizeof t21);
            uint8_t t22 = 0;
            F.ret = t21;
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = (void *)(((uint8_t *)t25) + 8);
        void * t27 = gm_l0;
        void * t28 = (void *)(((uint8_t *)t27) + 96);
        void * t29 = gm_l0;
        void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
        gm_t88rcucg1b7uk t31 = gm_l2;
        gm_t88rcucg1b7uk t32 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t28, t30, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t53at80pfdus3 t33;
        gm_f_kind_jcnt4hm((void *)&t33, t26, GM_CONV(gm_csguankko4lmhd, t32));
        uint8_t t34;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t33) + 0));
        if (t37 != ((uint8_t)13ULL)) goto mn36;
        t34 = ((uint8_t)1);
        goto me35;
mn36:;
        t34 = ((uint8_t)0);
        goto me35;
mn38:;
        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 203, 8);
me35:;
        if (!t34) goto ie23;
        uint8_t t39 = ((GM_LD(uint8_t, (((uint8_t *)&F.l3) + 8) + 0) == ((uint8_t)4ULL)));
        if (!t39) goto ie23;
        {
            const uint8_t *t41 = ((uint8_t *)&F.l3);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 65, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 66, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 68, 0);
            ((uint8_t *)&(*(gm_toqf51949ppls *)gm_out))[64] = 1;
            memmove((uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 0, t41, 64);
            goto gm_fdone;
        }
        goto ix23;
ie23:;
ix23:;
    }
    {
        uint8_t t43 = 0;
        uint8_t t44 = 0;
        void * t45 = (void *)((uint8_t *)&F.l3);
        uint8_t t46 = gm_f_contains_wildcard_jgbf94d(t45);
        if (!t46) {
            void * t47 = gm_l0;
            void * t48 = (void *)((uint8_t *)&F.l3);
            gm_t88rcucg1b7uk t49 = gm_l2;
            uint8_t t50 = gm_f_omits_payload_e33pdef((void *)NULL, cx, NULL, t47, t48, t49);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t44 = t50;
        } else t44 = 1;
        if (!t44) {
            void * t51 = gm_l0;
            void * t52 = (void *)((uint8_t *)&F.l3);
            uint8_t t53 = gm_f_binds_unknown_name_7m00m7a((void *)NULL, cx, NULL, t51, t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t43 = t53;
        } else t43 = 1;
        if (!t43) goto ie42;
        {
            const uint8_t *t55 = ((uint8_t *)&F.l3);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 65, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 66, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 68, 0);
            ((uint8_t *)&(*(gm_toqf51949ppls *)gm_out))[64] = 1;
            memmove((uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 0, t55, 64);
            goto gm_fdone;
        }
        goto ix42;
ie42:;
ix42:;
    }
    {
        gm_toqf51949ppls t56;
        memset(&t56, 0, sizeof t56);
        uint8_t t57 = 0;
        F.ret = t56;
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
__attribute__((aligned(4))) void * gm_f_is_pattern_test_kdd7cfh(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, void * a2, uint8_t a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_is_pattern_test_kdd7cfh F;
    void * gm_l0; /* cx */
    void * gm_l1; /* lhs */
    void * gm_l2; /* p */
    uint8_t gm_l3; /* negated */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    gm_t88rcucg1b7uk gm_l6; /* ty */
    void * gm_l8; /* pat */
    void * gm_l9; /* then */
    void * gm_l10; /* otherwise */
    GM_STACK_CHECK(&F, 855u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        void * t9 = gm_l1;
        gm_t88rcucg1b7uk t10 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t9) + 0));
        gm_t88rcucg1b7uk t11 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t11;
    }
    {
        void * t13 = gm_l0;
        gm_t88rcucg1b7uk t14 = gm_l6;
        uint8_t t15 = gm_f_is_c_union_p1r4eve(t13, GM_CONV(gm_csguankko4lmhd, t14));
        if (!t15) goto ie12;
        {
            void * t17 = gm_l0;
            void * t18 = gm_l1;
            gm_tl874t2atnlh3 t19 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t18) + 72));
            void * t20 = gm_l0;
            void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
            gm_t88rcucg1b7uk t22 = gm_l6;
            gm_b16_8 t23 = gm_f_c_union_match_2tgsktr((void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t24 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t17, t19, t23);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t25 = gm_l5;
            gm_t88rcucg1b7uk t26 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t26;
        }
        goto ix12;
ie12:;
ix12:;
    }
    {
        gm_f_list_6l26q4a((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t27 = gm_l0;
        void * t28 = gm_l2;
        gm_t88rcucg1b7uk t29 = gm_l6;
        gm_t9qkh0rsooc8d t30;
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)2ULL));
        void * t31 = (void *)((uint8_t *)&F.l7);
        void * t32 = gm_f_pattern_5gn6kid((void *)NULL, cx, NULL, t27, t28, t29, t30, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l8 = t32;
    }
    {
        void * t33 = gm_l5;
        gm_t88rcucg1b7uk t34 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t33);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t35 = gm_l3;
        uint8_t t36 = (uint8_t)!t35;
        gm_teti411eujvf4 t37;
        memset(&t37, 0, sizeof t37);
        GM_ST(uint8_t, ((uint8_t *)&t37 + 0), ((uint8_t)4ULL));
        GM_ST(uint8_t, ((uint8_t *)&t37 + 8), t36);
        gm_tl874t2atnlh3 t38 = gm_l4;
        void * t39 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t34, &t37, t38);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l9 = t39;
    }
    {
        void * t40 = gm_l5;
        gm_t88rcucg1b7uk t41 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t42 = gm_l3;
        gm_teti411eujvf4 t43;
        memset(&t43, 0, sizeof t43);
        GM_ST(uint8_t, ((uint8_t *)&t43 + 0), ((uint8_t)4ULL));
        GM_ST(uint8_t, ((uint8_t *)&t43 + 8), t42);
        gm_tl874t2atnlh3 t44 = gm_l4;
        void * t45 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t41, &t43, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l10 = t45;
    }
    {
        gm_f_list_58nr42v_3((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t46 = (void *)((uint8_t *)&F.l11);
        gm_tbbqefuhv2r8h t47;
        void * t48 = gm_l8;
        GM_ST(void *, (uint8_t *)&t47 + 0, t48);
        GM_ST(void *, (uint8_t *)&t47 + 8, ((void *)0));
        void * t49 = gm_l9;
        GM_ST(void *, (uint8_t *)&t47 + 16, t49);
        gm_tftb1odc739t5 t50 = gm_f_push_bpovr2k_3(t46, &t47);
        if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 1, gm_src786i8duem00q, 295, 5);
        gm_b0_1 t51 = GM_LD(gm_b0_1, (((uint8_t *)&t50) + 1));
    }
    {
        void * t52 = (void *)((uint8_t *)&F.l11);
        gm_tbbqefuhv2r8h t53;
        gm_t88rcucg1b7uk t54 = gm_l6;
        gm_tb38v5s9j1r32 t55;
        memset(&t55, 0, sizeof t55);
        GM_ST(uint8_t, ((uint8_t *)&t55 + 0), ((uint8_t)0ULL));
        gm_tl874t2atnlh3 t56 = gm_l4;
        void * t57 = gm_f_mk_pat_hr2l6c4((void *)NULL, cx, NULL, t54, &t55, t56);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(void *, (uint8_t *)&t53 + 0, t57);
        GM_ST(void *, (uint8_t *)&t53 + 8, ((void *)0));
        void * t58 = gm_l10;
        GM_ST(void *, (uint8_t *)&t53 + 16, t58);
        gm_tftb1odc739t5 t59 = gm_f_push_bpovr2k_3(t52, &t53);
        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_src786i8duem00q, 296, 5);
        gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
    }
    {
        void * t61 = gm_l5;
        gm_t88rcucg1b7uk t62 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tkaapcii86udk t63;
        void * t64 = gm_l1;
        GM_ST(void *, (uint8_t *)&t63 + 0, t64);
        gm_b16_8 t65 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t63 + 8, t65);
        gm_teti411eujvf4 t66;
        memset(&t66, 0, sizeof t66);
        GM_ST(uint8_t, ((uint8_t *)&t66 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t66 + 8), t63);
        gm_tl874t2atnlh3 t67 = gm_l4;
        void * t68 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t62, &t66, t67);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t69 = 0;
        F.ret = t68;
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
__attribute__((aligned(4))) void * gm_f_match_expr_npcoo60(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, uint8_t a2, gm_tkn42u7na3t81 a3) {
    struct gm_fr_match_expr_npcoo60 F;
    void * gm_l0; /* cx */
    void * gm_l1; /* m */
    uint8_t gm_l2; /* value */
    gm_tkn42u7na3t81 gm_l3; /* expect */
    void * gm_l4; /* tys */
    void * gm_l5; /* scrutinee */
    gm_t88rcucg1b7uk gm_l6; /* sty */
    uint8_t gm_l7; /* deferred */
    uint8_t gm_l9; /* bad_pattern */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    void * gm_l13; /* arm */
    uint64_t gm_l14; /* before */
    void * gm_l15; /* pat */
    void * gm_l16; /* guard */
    void * gm_l17; /* g */
    void * gm_l19; /* body */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* <tmp> */
    uint64_t gm_l23; /* i */
    gm_t88rcucg1b7uk gm_l24; /* ty */
    gm_t88rcucg1b7uk gm_l25; /* t */
    gm_b16_8 gm_l27; /* <tmp> */
    uint64_t gm_l28; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l29; /* a */
    uint8_t gm_l30; /* all_never */
    gm_b16_8 gm_l31; /* <tmp> */
    uint64_t gm_l32; /* <tmp> */
    gm_tbbqefuhv2r8h gm_l33; /* a */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* <tmp> */
    uint64_t gm_l36; /* i */
    void * gm_l37; /* b */
    GM_STACK_CHECK(&F, 2327u);
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
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l4 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = gm_l1;
        void * t7 = GM_LD(void *, (((uint8_t *)t6) + 0));
        gm_tkn42u7na3t81 t8;
        memset(&t8, 0, sizeof t8);
        void * t9 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t5, t7, t8);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l5 = t9;
    }
    {
        void * t10 = gm_l0;
        void * t11 = (void *)(((uint8_t *)t10) + 96);
        void * t12 = gm_l0;
        void * t13 = GM_LD(void *, (((uint8_t *)t12) + 0));
        void * t14 = gm_l5;
        gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t14) + 0));
        gm_t88rcucg1b7uk t16 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t11, t13, t15);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l6 = t16;
    }
    {
        void * t17 = gm_l4;
        gm_t88rcucg1b7uk t18 = gm_l6;
        uint8_t t19 = gm_f_has_infer_3o82a0b(t17, GM_CONV(gm_csguankko4lmhd, t18));
        gm_l7 = t19;
    }
    {
        void * t21 = gm_l0;
        gm_t88rcucg1b7uk t22 = gm_l6;
        uint8_t t23 = gm_f_is_c_union_p1r4eve(t21, GM_CONV(gm_csguankko4lmhd, t22));
        if (!t23) goto ie20;
        {
            void * t25 = gm_l0;
            void * t26 = gm_l1;
            void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
            gm_tl874t2atnlh3 t28 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t27) + 72));
            void * t29 = gm_l0;
            void * t30 = GM_LD(void *, (((uint8_t *)t29) + 0));
            gm_t88rcucg1b7uk t31 = gm_l6;
            gm_b16_8 t32 = gm_f_c_union_match_2tgsktr((void *)NULL, cx, NULL, t30, t31);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b0_1 t33 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t25, t28, t32);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t34 = gm_l4;
            gm_t88rcucg1b7uk t35 = gm_f_error_ki8i3k6((void *)NULL, cx, NULL, t34);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_l6 = t35;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        gm_f_list_58nr42v_3((void *)&F.l8, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_l9 = ((uint8_t)0);
    }
    {
        {
            gm_l11 = ((uint64_t)0ULL);
        }
        {
            void * t37 = gm_l1;
            uint64_t t38 = GM_LD(uintptr_t, (((uint8_t *)t37) + 8) + 8);
            gm_l10 = t38;
        }
        {
            for (;;) {
                uint64_t t40 = gm_l11;
                uint64_t t41 = gm_l10;
                uint8_t t42 = (t40 < t41);
                if (!t42) goto lb39;
                {
                    uint64_t t45 = gm_l11;
                    uint64_t t48 = GM_LD(uint64_t, ((uint8_t *)&t45));
                    gm_l12 = t48;
                    {
                        uint64_t t50 = gm_l12;
                        uint64_t t51 = (uintptr_t)t50;
                        void * t52 = gm_l1;
                        uint64_t t53 = GM_LD(uintptr_t, (((uint8_t *)t52) + 8) + 8);
                        if (t51 >= (uintptr_t)(t53)) gm_trap_index(t51, t53, gm_src786i8duem00q, 28, 17);
                        void * t54 = (void *)((uint8_t *)GM_LD(void *, (((uint8_t *)t52) + 8)) + t51 * 48);
                        gm_l13 = t54;
                    }
                    {
                        void * t55 = gm_l0;
                        gm_b0_1 t56 = gm_f_push_scope_mu7j3lg((void *)NULL, cx, NULL, t55);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                    }
                    {
                        void * t57 = gm_l0;
                        void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
                        void * t59 = GM_LD(void *, (((uint8_t *)t58) + 0));
                        void * t60 = GM_LD(void *, (((uint8_t *)t59) + 8));
                        uint64_t t61 = gm_f_error_count_omtt6qd(t60);
                        gm_l14 = t61;
                    }
                    {
                        void * t62 = gm_l0;
                        void * t63 = gm_l13;
                        gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)t63) + 0));
                        gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
                        gm_t88rcucg1b7uk t66 = gm_l6;
                        void * t67 = gm_f_arm_pattern_rftjhhb((void *)NULL, cx, NULL, t62, t65, t66);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        gm_l15 = t67;
                    }
                    {
                        void * t69 = gm_l0;
                        void * t70 = GM_LD(void *, (((uint8_t *)t69) + 0));
                        void * t71 = GM_LD(void *, (((uint8_t *)t70) + 0));
                        void * t72 = GM_LD(void *, (((uint8_t *)t71) + 8));
                        uint64_t t73 = gm_f_error_count_omtt6qd(t72);
                        uint64_t t74 = gm_l14;
                        uint8_t t75 = (t73 > t74);
                        if (!t75) goto ie68;
                        {
                            gm_l9 = ((uint8_t)1);
                        }
                        goto ix68;
ie68:;
ix68:;
                    }
                    {
                        gm_l16 = ((void *)0);
                    }
                    {
                        void * t78 = gm_l13;
                        void * t79 = GM_LD(void *, (((uint8_t *)t78) + 16));
                        if (!(GM_LD(void *, ((uint8_t *)&t79)) != 0)) goto ie77;
                        void * t80 = GM_LD(void *, ((uint8_t *)&t79));
                        gm_l17 = t80;
                        {
                            void * t82 = gm_l0;
                            void * t83 = gm_l0;
                            void * t84 = gm_l17;
                            void * t85 = gm_l4;
                            gm_t88rcucg1b7uk t86 = gm_f_bool_ty_pn2p277((void *)NULL, cx, NULL, t85);
                            if (gm_unw.kind) {
                                goto gm_ll43;
                            }
                            gm_tkn42u7na3t81 t87;
                            GM_ST(uint8_t, (uint8_t *)&t87 + 5, 0);
                            GM_ST(uint16_t, (uint8_t *)&t87 + 6, 0);
                            GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t87), t86);
                            ((uint8_t *)&t87)[4] = 1;
                            void * t88 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t83, t84, t87);
                            if (gm_unw.kind) {
                                goto gm_ll43;
                            }
                            void * t89 = gm_f_expect_bool_cdo5g1q((void *)NULL, cx, NULL, t82, t88);
                            if (gm_unw.kind) {
                                goto gm_ll43;
                            }
                            gm_l16 = t89;
                        }
                        goto ix77;
ie77:;
ix77:;
                    }
                    {
                        void * t90;
                        void * t92 = gm_l13;
                        void * t93 = GM_LD(void *, (((uint8_t *)t92) + 24));
                        uint8_t t94 = GM_LD(uint8_t, ((((uint8_t *)t93) + 8) + 0));
                        if (t94 != ((uint8_t)44ULL)) goto ie91;
                        gm_t79rbdv4rafe5 t95 = GM_LD(gm_t79rbdv4rafe5, ((((uint8_t *)t93) + 8) + 8));
                        F.l18 = t95;
                        void * t97 = gm_l0;
                        void * t98 = (void *)((uint8_t *)&F.l18);
                        uint8_t t99 = gm_l2;
                        gm_tkn42u7na3t81 t100 = gm_l3;
                        void * t101 = gm_f_block_expected_bv4ukm2((void *)NULL, cx, NULL, t97, t98, t99, t100);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        void * t102 = t101;
                        t90 = t102;
                        goto ix91;
ie91:;
                        void * t103;
                        uint8_t t105 = gm_l2;
                        if (!t105) goto ie104;
                        void * t107 = gm_l0;
                        void * t108 = gm_l13;
                        void * t109 = GM_LD(void *, (((uint8_t *)t108) + 24));
                        gm_tkn42u7na3t81 t110 = gm_l3;
                        void * t111 = gm_f_expr_vmp704u((void *)NULL, cx, NULL, t107, t109, t110);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        void * t112 = t111;
                        t103 = t112;
                        goto ix104;
ie104:;
                        void * t114 = gm_l0;
                        void * t115 = gm_l13;
                        void * t116 = GM_LD(void *, (((uint8_t *)t115) + 24));
                        void * t117 = gm_f_stmt_expr_evs4ic8((void *)NULL, cx, NULL, t114, t116);
                        if (gm_unw.kind) {
                            goto gm_ll43;
                        }
                        void * t118 = t117;
                        t103 = t118;
ix104:;
                        t90 = t103;
ix91:;
                        gm_l19 = t90;
                    }
                    {
                        void * t119 = gm_l0;
                        gm_f_pop_scope_g5j7o4e(t119);
                    }
                    {
                        void * t120 = (void *)((uint8_t *)&F.l8);
                        gm_tbbqefuhv2r8h t121;
                        void * t122 = gm_l15;
                        GM_ST(void *, (uint8_t *)&t121 + 0, t122);
                        void * t123 = gm_l16;
                        GM_ST(void *, (uint8_t *)&t121 + 8, t123);
                        void * t124 = gm_l19;
                        GM_ST(void *, (uint8_t *)&t121 + 16, t124);
                        gm_tftb1odc739t5 t125 = gm_f_push_bpovr2k_3(t120, &t121);
                        if (((((uint8_t *)&t125))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t125) + 1, gm_src786i8duem00q, 43, 9);
                        gm_b0_1 t126 = GM_LD(gm_b0_1, (((uint8_t *)&t125) + 1));
                    }
                    goto me46;
mn47:;
                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 27, 5);
me46:;
                }
lc39:;
                {
                    uint64_t t128 = gm_l11;
                    uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
                    uint64_t t131 = r129;
                    gm_l11 = t131;
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
        gm_f_list_7s4om10((void *)&F.l20, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l22 = ((uint64_t)0ULL);
        }
        {
            void * t133 = gm_l1;
            uint64_t t134 = GM_LD(uintptr_t, (((uint8_t *)t133) + 8) + 8);
            gm_l21 = t134;
        }
        {
            for (;;) {
                uint64_t t136 = gm_l22;
                uint64_t t137 = gm_l21;
                uint8_t t138 = (t136 < t137);
                if (!t138) goto lb135;
                {
                    uint64_t t141 = gm_l22;
                    uint64_t t144 = GM_LD(uint64_t, ((uint8_t *)&t141));
                    gm_l23 = t144;
                    {
                        void * t146 = (void *)((uint8_t *)&F.l20);
                        uint64_t t147 = gm_l23;
                        uint64_t t148 = (uintptr_t)t147;
                        void * t149 = gm_l1;
                        uint64_t t150 = GM_LD(uintptr_t, (((uint8_t *)t149) + 8) + 8);
                        if (t148 >= (uintptr_t)(t150)) gm_trap_index(t148, t150, gm_src786i8duem00q, 47, 45);
                        gm_tl874t2atnlh3 t151 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)GM_LD(void *, (((uint8_t *)t149) + 8)) + t148 * 48) + 32));
                        gm_tftb1odc739t5 t152 = gm_f_push_0mk7udh(t146, GM_CONV(gm_cseahvg8v7p0vs, t151));
                        if (((((uint8_t *)&t152))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t152) + 1, gm_src786i8duem00q, 47, 30);
                        gm_b0_1 t153 = GM_LD(gm_b0_1, (((uint8_t *)&t152) + 1));
                    }
                    goto me142;
mn143:;
                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 47, 5);
me142:;
                }
lc135:;
                {
                    uint64_t t155 = gm_l22;
                    uint64_t r156; bool o157 = __builtin_add_overflow(t155, ((uint64_t)1ULL), &r156);
                    uint64_t t158 = r156;
                    gm_l22 = t158;
                }
            }
lb135:;
        }
    }
    {
        uint8_t t160 = gm_l7;
        if (!t160) goto ie159;
        {
            void * t162 = gm_l0;
            void * t163 = (void *)(((uint8_t *)t162) + 480);
            gm_tbgd14esf0jvi t164;
            memset((uint8_t *)&t164 + 12, 0, 4);
            void * t165 = gm_l1;
            gm_tl874t2atnlh3 t166 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t165) + 24));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t164 + 0, t166);
            gm_b16_8 t167 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t164 + 16, t167);
            gm_tftb1odc739t5 t168 = gm_f_push_ltrd0kq(t163, &t164);
            if (((((uint8_t *)&t168))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t168) + 1, gm_src786i8duem00q, 49, 9);
            gm_b0_1 t169 = GM_LD(gm_b0_1, (((uint8_t *)&t168) + 1));
        }
        goto ix159;
ie159:;
        void * t171 = gm_l4;
        gm_t88rcucg1b7uk t172 = gm_l6;
        uint8_t t173 = gm_f_is_error_8k27gd1(t171, GM_CONV(gm_csguankko4lmhd, t172));
        uint8_t t174 = (uint8_t)!t173;
        if (!t174) goto ie170;
        uint8_t t175 = gm_l9;
        uint8_t t176 = (uint8_t)!t175;
        if (!t176) goto ie170;
        {
            void * t178 = gm_l0;
            void * t179 = gm_l0;
            gm_t88rcucg1b7uk t180 = gm_l6;
            gm_b16_8 t181 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
            gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
            gm_tqs4ilbobv1qq t183;
            gm_f_check_arms_65v4isv((void *)&t183, (void *)NULL, cx, NULL, t179, t180, t182);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            void * t184 = gm_l1;
            gm_tl874t2atnlh3 t185 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t184) + 24));
            gm_b16_8 t186 = GM_LD(gm_b16_8, (((uint8_t *)&F.l20) + 0));
            gm_b16_8 t187 = GM_LD(gm_b16_8, ((uint8_t *)&t186));
            gm_b0_1 t188 = gm_f_report_match_d77kbfe((void *)NULL, cx, NULL, t178, &t183, t185, t187);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix170;
ie170:;
ix170:;
ix159:;
    }
    {
        void * t189 = gm_l4;
        gm_t88rcucg1b7uk t190 = gm_f_unit_2age34u((void *)NULL, cx, NULL, t189);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l24 = t190;
    }
    {
        uint8_t t192 = gm_l2;
        if (!t192) goto ie191;
        {
            if (!((((uint8_t *)&gm_l3))[4] != 0)) goto ie194;
            gm_t88rcucg1b7uk t195 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)&gm_l3));
            gm_l25 = t195;
            {
                gm_t88rcucg1b7uk t197 = gm_l25;
                gm_l24 = t197;
            }
            goto ix194;
ie194:;
            {
                gm_f_list_nof7vfj((void *)&F.l26, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t200 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                    gm_l27 = t200;
                }
                {
                    gm_l28 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t202 = gm_l28;
                        uint64_t t203 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                        uint8_t t204 = (t202 < t203);
                        if (!t204) goto lb201;
                        {
                            uint64_t t207 = gm_l28;
                            uint64_t t208 = (uintptr_t)t207;
                            uint64_t t209 = GM_LD(uintptr_t, ((uint8_t *)&gm_l27) + 8);
                            gm_tbbqefuhv2r8h t212 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l27)) + t208 * 24));
                            gm_l29 = t212;
                            {
                                void * t214 = (void *)((uint8_t *)&F.l26);
                                void * t215 = GM_LD(void *, (((uint8_t *)&gm_l29) + 16));
                                gm_t88rcucg1b7uk t216 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t215) + 0));
                                gm_tftb1odc739t5 t217 = gm_f_push_k3l9nic(t214, GM_CONV(gm_csguankko4lmhd, t216));
                                if (((((uint8_t *)&t217))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t217) + 1, gm_src786i8duem00q, 59, 35);
                                gm_b0_1 t218 = GM_LD(gm_b0_1, (((uint8_t *)&t217) + 1));
                            }
                            goto me210;
mn211:;
                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 59, 13);
me210:;
                        }
lc201:;
                        {
                            uint64_t t220 = gm_l28;
                            uint64_t r221; bool o222 = __builtin_add_overflow(t220, ((uint64_t)1ULL), &r221);
                            uint64_t t223 = r221;
                            gm_l28 = t223;
                        }
                    }
lb201:;
                }
            }
            {
                void * t224 = gm_l0;
                gm_b16_8 t225 = GM_LD(gm_b16_8, (((uint8_t *)&F.l26) + 0));
                gm_b16_8 t226 = GM_LD(gm_b16_8, ((uint8_t *)&t225));
                gm_t88rcucg1b7uk t227 = gm_f_join_type_cck70vp((void *)NULL, cx, NULL, t224, t226);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t227;
            }
ix194:;
        }
        goto ix191;
ie191:;
        {
            gm_l30 = ((uint8_t)1);
        }
        {
            {
                gm_b16_8 t230 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
                gm_l31 = t230;
            }
            {
                gm_l32 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t232 = gm_l32;
                    uint64_t t233 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                    uint8_t t234 = (t232 < t233);
                    if (!t234) goto lb231;
                    {
                        uint64_t t237 = gm_l32;
                        uint64_t t238 = (uintptr_t)t237;
                        uint64_t t239 = GM_LD(uintptr_t, ((uint8_t *)&gm_l31) + 8);
                        gm_tbbqefuhv2r8h t242 = GM_LD(gm_tbbqefuhv2r8h, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l31)) + t238 * 24));
                        gm_l33 = t242;
                        {
                            void * t245 = gm_l4;
                            void * t246 = GM_LD(void *, (((uint8_t *)&gm_l33) + 16));
                            gm_t88rcucg1b7uk t247 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t246) + 0));
                            gm_t53at80pfdus3 t248;
                            gm_f_kind_jcnt4hm((void *)&t248, t245, GM_CONV(gm_csguankko4lmhd, t247));
                            uint8_t t249 = (!(GM_LD(uint8_t, ((uint8_t *)&t248) + 0) == ((uint8_t)5ULL)));
                            if (!t249) goto ie244;
                            {
                                gm_l30 = ((uint8_t)0);
                            }
                            goto ix244;
ie244:;
ix244:;
                        }
                        goto me240;
mn241:;
                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 64, 9);
me240:;
                    }
lc231:;
                    {
                        uint64_t t252 = gm_l32;
                        uint64_t r253; bool o254 = __builtin_add_overflow(t252, ((uint64_t)1ULL), &r253);
                        uint64_t t255 = r253;
                        gm_l32 = t255;
                    }
                }
lb231:;
            }
        }
        {
            uint8_t t257 = gm_l30;
            if (!t257) goto ie256;
            {
                void * t259 = gm_l4;
                gm_t88rcucg1b7uk t260 = gm_f_never_ty_b949mgo((void *)NULL, cx, NULL, t259);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l24 = t260;
            }
            goto ix256;
ie256:;
ix256:;
        }
ix191:;
    }
    {
        uint8_t t262 = gm_l2;
        if (!t262) goto ie261;
        void * t263 = gm_l4;
        gm_t88rcucg1b7uk t264 = gm_l24;
        gm_t53at80pfdus3 t265;
        gm_f_kind_jcnt4hm((void *)&t265, t263, GM_CONV(gm_csguankko4lmhd, t264));
        uint8_t t266 = (!(GM_LD(uint8_t, ((uint8_t *)&t265) + 0) == ((uint8_t)5ULL)));
        if (!t266) goto ie261;
        {
            {
                gm_l35 = ((uint64_t)0ULL);
            }
            {
                uint64_t t269 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                gm_l34 = t269;
            }
            {
                for (;;) {
                    uint64_t t271 = gm_l35;
                    uint64_t t272 = gm_l34;
                    uint8_t t273 = (t271 < t272);
                    if (!t273) goto lb270;
                    {
                        uint64_t t276 = gm_l35;
                        uint64_t t279 = GM_LD(uint64_t, ((uint8_t *)&t276));
                        gm_l36 = t279;
                        {
                            uint64_t t281 = gm_l36;
                            uint64_t t282 = (uintptr_t)t281;
                            uint64_t t283 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                            if (t282 >= (uintptr_t)(t283)) gm_trap_index(t282, t283, gm_src786i8duem00q, 71, 18);
                            void * t284 = GM_LD(void *, (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)) + t282 * 24) + 16));
                            gm_l37 = t284;
                        }
                        {
                            void * t286 = gm_l4;
                            void * t287 = gm_l37;
                            gm_t88rcucg1b7uk t288 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t287) + 0));
                            gm_t53at80pfdus3 t289;
                            gm_f_kind_jcnt4hm((void *)&t289, t286, GM_CONV(gm_csguankko4lmhd, t288));
                            uint8_t t290 = (!(GM_LD(uint8_t, ((uint8_t *)&t289) + 0) == ((uint8_t)5ULL)));
                            if (!t290) goto ie285;
                            {
                                uint64_t t292 = gm_l36;
                                uint64_t t293 = (uintptr_t)t292;
                                uint64_t t294 = GM_LD(uintptr_t, (((uint8_t *)&F.l8) + 0) + 8);
                                if (t293 >= (uintptr_t)(t294)) gm_trap_index(t293, t294, gm_src786i8duem00q, 72, 47);
                                uint8_t *t295 = (((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l8) + 0)) + t293 * 24) + 16);
                                void * t296 = gm_l0;
                                void * t297 = gm_l37;
                                gm_t88rcucg1b7uk t298 = gm_l24;
                                void * t299 = gm_f_coerce_sudc6c1((void *)NULL, cx, NULL, t296, t297, t298);
                                if (gm_unw.kind) {
                                    goto gm_ll274;
                                }
                                GM_ST(void *, t295, t299);
                            }
                            goto ix285;
ie285:;
ix285:;
                        }
                        goto me277;
mn278:;
                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 70, 9);
me277:;
                    }
lc270:;
                    {
                        uint64_t t301 = gm_l35;
                        uint64_t r302; bool o303 = __builtin_add_overflow(t301, ((uint64_t)1ULL), &r302);
                        uint64_t t304 = r302;
                        gm_l35 = t304;
                    }
                }
gm_ll274:;
                if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb270; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc270; }
                }
                goto gm_lad;
lb270:;
            }
        }
        goto ix261;
ie261:;
ix261:;
    }
    {
        gm_t88rcucg1b7uk t305 = gm_l24;
        gm_tkaapcii86udk t306;
        void * t307 = gm_l5;
        GM_ST(void *, (uint8_t *)&t306 + 0, t307);
        gm_b16_8 t308 = GM_LD(gm_b16_8, (((uint8_t *)&F.l8) + 0));
        GM_ST(gm_b16_8, (uint8_t *)&t306 + 8, t308);
        gm_teti411eujvf4 t309;
        memset(&t309, 0, sizeof t309);
        GM_ST(uint8_t, ((uint8_t *)&t309 + 0), ((uint8_t)44ULL));
        GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t309 + 8), t306);
        void * t310 = gm_l1;
        gm_tl874t2atnlh3 t311 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t310) + 24));
        void * t312 = gm_f_mk_9bc97fd((void *)NULL, cx, NULL, t305, &t309, t311);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t313 = 0;
        F.ret = t312;
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
inline __attribute__((aligned(4))) void gm_f_nested_pattern_oiclpgf(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0) {
    struct gm_fr_nested_pattern_oiclpgf F;
    void * gm_l0; /* e */
    gm_t08olq97rscn3 gm_l1; /* n */
    GM_STACK_CHECK(&F, 257u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        void * t3 = gm_l0;
        uint8_t t4 = GM_LD(uint8_t, ((((uint8_t *)t3) + 8) + 0));
        if (t4 != ((uint8_t)6ULL)) goto ie2;
        gm_t08olq97rscn3 t5 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t3) + 8) + 8));
        gm_l1 = t5;
        {
            gm_t0d02142k4tu8 t7;
            memset((uint8_t *)&t7 + 4, 0, 4);
            memset((uint8_t *)&t7 + 60, 0, 4);
            void * t8 = gm_l0;
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)t8) + 0));
            GM_ST(uint32_t, (uint8_t *)&t7 + 0, t9);
            gm_t08olq97rscn3 t10 = gm_l1;
            gm_to361lgknsu69 t11;
            GM_ST(uint8_t, (uint8_t *)&t11 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t11 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t11 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)1ULL));
            GM_ST(gm_t08olq97rscn3, ((uint8_t *)&t11 + 8), t10);
            GM_ST(gm_to361lgknsu69, (uint8_t *)&t7 + 8, t11);
            void * t12 = gm_l0;
            gm_tl874t2atnlh3 t13 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t12) + 72));
            GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t7 + 48, t13);
            GM_ST(uint8_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 65, 0);
            GM_ST(uint16_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 66, 0);
            GM_ST(uint32_t, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 68, 0);
            ((uint8_t *)&(*(gm_toqf51949ppls *)gm_out))[64] = 1;
            GM_ST(gm_t0d02142k4tu8, (uint8_t *)&(*(gm_toqf51949ppls *)gm_out) + 0, t7);
            goto gm_fdone;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t14 = gm_l0;
        gm_f_expr_to_pattern_rchr7ig((void *)&(*(gm_toqf51949ppls *)gm_out), (void *)NULL, cx, NULL, t14);
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
__attribute__((aligned(4))) uint8_t gm_f_omits_payload_e33pdef(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_omits_payload_e33pdef F;
    void * gm_l0; /* cx */
    void * gm_l1; /* p */
    gm_t88rcucg1b7uk gm_l2; /* ty0 */
    void * gm_l3; /* tys */
    gm_t88rcucg1b7uk gm_l4; /* ty */
    void * gm_l5; /* inner */
    gm_t88rcucg1b7uk gm_l6; /* t */
    gm_t88rcucg1b7uk gm_l7; /* i */
    gm_t88rcucg1b7uk gm_l8; /* err */
    gm_t88rcucg1b7uk gm_l9; /* t */
    gm_tagc77imt2chm gm_l10; /* pp */
    gm_t08olq97rscn3 gm_l11; /* n */
    gm_t08olq97rscn3 gm_l12; /* n */
    gm_b16_8 gm_l13; /* name */
    gm_tep2drsl9408b gm_l14; /* uv */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    GM_STACK_CHECK(&F, 918u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l3 = t4;
    }
    {
        void * t5 = gm_l0;
        void * t6 = (void *)(((uint8_t *)t5) + 96);
        void * t7 = gm_l0;
        void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
        gm_t88rcucg1b7uk t9 = gm_l2;
        gm_t88rcucg1b7uk t10 = gm_f_resolve_t2kal61((void *)NULL, cx, NULL, t6, t8, t9);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_l4 = t10;
    }
    {
        void * t12 = gm_l1;
        uint8_t t13 = GM_LD(uint8_t, ((((uint8_t *)t12) + 8) + 0));
        if (t13 != ((uint8_t)6ULL)) goto ie11;
        void * t14 = GM_LD(void *, ((((uint8_t *)t12) + 8) + 8));
        gm_l5 = t14;
        {
            gm_t88rcucg1b7uk t16 = gm_l4;
            gm_l6 = t16;
        }
        {
            for (;;) {
                void * t18 = gm_l3;
                gm_t88rcucg1b7uk t19 = gm_l6;
                gm_t53at80pfdus3 t20;
                gm_f_kind_jcnt4hm((void *)&t20, t18, GM_CONV(gm_csguankko4lmhd, t19));
                uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&t20) + 0));
                if (t21 != ((uint8_t)13ULL)) goto lb17;
                gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t20) + 8));
                gm_l7 = t22;
                {
                    gm_t88rcucg1b7uk t25 = gm_l7;
                    gm_l6 = t25;
                }
lc17:;
            }
lb17:;
        }
        {
            void * t27 = gm_l3;
            gm_t88rcucg1b7uk t28 = gm_l6;
            gm_t53at80pfdus3 t29;
            gm_f_kind_jcnt4hm((void *)&t29, t27, GM_CONV(gm_csguankko4lmhd, t28));
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&t29) + 0));
            if (t30 != ((uint8_t)14ULL)) goto ie26;
            gm_t88rcucg1b7uk t31 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t29) + 8) + 0));
            gm_l8 = t31;
            {
                void * t33 = gm_l0;
                void * t34 = gm_l5;
                gm_t88rcucg1b7uk t35 = gm_l8;
                uint8_t t36 = gm_f_omits_payload_e33pdef((void *)NULL, cx, NULL, t33, t34, t35);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint8_t t37 = 0;
                F.ret = t36;
                goto gm_fret;
            }
            goto ix26;
ie26:;
            {
                uint8_t t39 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
ix26:;
        }
        goto ix11;
ie11:;
ix11:;
    }
    {
        void * t40 = gm_l0;
        gm_t88rcucg1b7uk t41 = gm_l4;
        gm_t88rcucg1b7uk t42 = GM_CONV(gm_t88rcucg1b7uk, gm_f_strip_wrappers_e5thpib(t40, GM_CONV(gm_csguankko4lmhd, t41)));
        gm_l9 = t42;
    }
    {
        void * t44 = gm_l1;
        uint8_t t45 = GM_LD(uint8_t, ((((uint8_t *)t44) + 8) + 0));
        if (t45 != ((uint8_t)7ULL)) goto ie43;
        gm_tagc77imt2chm t46 = GM_LD(gm_tagc77imt2chm, ((((uint8_t *)t44) + 8) + 8));
        gm_l10 = t46;
        {
            uint8_t t49;
            if ((((((uint8_t *)&gm_l10) + 8))[16] != 0)) goto mn51;
            t49 = ((uint8_t)0);
            goto me50;
mn51:;
            t49 = ((uint8_t)1);
            goto me50;
mn52:;
            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 243, 8);
me50:;
            if (!t49) goto ie48;
            {
                uint8_t t54 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            goto ix48;
ie48:;
ix48:;
        }
        {
            void * t55 = GM_LD(void *, (((uint8_t *)&gm_l10) + 0));
            gm_b16_8 t56;
            uint8_t t62 = GM_LD(uint8_t, ((((uint8_t *)t55) + 8) + 0));
            switch (t62) { case ((uint8_t)0ULL): goto ma60; case ((uint8_t)1ULL): goto ma60; case ((uint8_t)2ULL): goto ma60; case ((uint8_t)3ULL): goto ma60; case ((uint8_t)4ULL): goto ma60; case ((uint8_t)5ULL): goto ma60; case ((uint8_t)6ULL): goto ma60; case ((uint8_t)7ULL): goto ma60; case ((uint8_t)8ULL): goto ma58; case ((uint8_t)9ULL): goto ma60; case ((uint8_t)10ULL): goto ma60; case ((uint8_t)11ULL): goto ma60; case ((uint8_t)12ULL): goto ma60; case ((uint8_t)13ULL): goto ma60; case ((uint8_t)14ULL): goto ma60; case ((uint8_t)15ULL): goto ma60; case ((uint8_t)16ULL): goto ma60; case ((uint8_t)17ULL): goto ma60; case ((uint8_t)18ULL): goto ma60; case ((uint8_t)19ULL): goto ma60; case ((uint8_t)20ULL): goto ma60; case ((uint8_t)21ULL): goto ma60; case ((uint8_t)22ULL): goto ma59; case ((uint8_t)23ULL): goto ma60; case ((uint8_t)24ULL): goto ma60; case ((uint8_t)25ULL): goto ma60; case ((uint8_t)26ULL): goto ma60; case ((uint8_t)27ULL): goto ma60; case ((uint8_t)28ULL): goto ma60; case ((uint8_t)29ULL): goto ma60; case ((uint8_t)30ULL): goto ma60; case ((uint8_t)31ULL): goto ma60; case ((uint8_t)32ULL): goto ma60; case ((uint8_t)33ULL): goto ma60; case ((uint8_t)34ULL): goto ma60; case ((uint8_t)35ULL): goto ma60; case ((uint8_t)36ULL): goto ma60; case ((uint8_t)37ULL): goto ma60; case ((uint8_t)38ULL): goto ma60; case ((uint8_t)39ULL): goto ma60; case ((uint8_t)40ULL): goto ma60; case ((uint8_t)41ULL): goto ma60; case ((uint8_t)42ULL): goto ma60; case ((uint8_t)43ULL): goto ma60; case ((uint8_t)44ULL): goto ma60; case ((uint8_t)45ULL): goto ma60; case ((uint8_t)46ULL): goto ma60; case ((uint8_t)47ULL): goto ma60; case ((uint8_t)48ULL): goto ma60; case ((uint8_t)49ULL): goto ma60; case ((uint8_t)50ULL): goto ma60; default: goto ma60; }
ma58:;
            uint8_t t64 = GM_LD(uint8_t, ((((uint8_t *)t55) + 8) + 0));
            if (t64 != ((uint8_t)8ULL)) goto mn63;
            gm_t08olq97rscn3 t65 = GM_LD(gm_t08olq97rscn3, ((((uint8_t *)t55) + 8) + 8));
            gm_l11 = t65;
            gm_b16_8 t66 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l11) + 0));
            t56 = t66;
            goto me57;
mn63:;
ma59:;
            uint8_t t68 = GM_LD(uint8_t, ((((uint8_t *)t55) + 8) + 0));
            if (t68 != ((uint8_t)22ULL)) goto mn67;
            gm_t08olq97rscn3 t69 = GM_LD(gm_t08olq97rscn3, (((((uint8_t *)t55) + 8) + 8) + 8));
            gm_l12 = t69;
            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l12) + 0));
            t56 = t70;
            goto me57;
mn67:;
ma60:;
            uint8_t t72 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
            goto me57;
mn71:;
ma61:;
            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 244, 13);
me57:;
            gm_l13 = t56;
        }
        {
            void * t73 = gm_l0;
            void * t74 = GM_LD(void *, (((uint8_t *)t73) + 0));
            gm_t88rcucg1b7uk t75 = gm_l9;
            gm_tdlpv0q69lov1 t76;
            gm_f_union_variants_a7khaqp((void *)&t76, t74, GM_CONV(gm_csguankko4lmhd, t75));
            gm_tep2drsl9408b t77;
            if (((((uint8_t *)&t76))[24] != 0)) {
                t77 = (*(gm_tep2drsl9408b *)(((uint8_t *)&t76) + 0));
            } else {
                uint8_t t78 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l14 = t77;
        }
        {
            {
                gm_b16_8 t80 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l14) + 0));
                gm_l15 = t80;
            }
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t82 = gm_l16;
                    uint64_t t83 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                    uint8_t t84 = (t82 < t83);
                    if (!t84) goto lb81;
                    {
                        uint64_t t87 = gm_l16;
                        uint64_t t88 = (uintptr_t)t87;
                        uint64_t t89 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        gm_tckdobunaulee t92 = GM_LD(gm_tckdobunaulee, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t88 * 72));
                        F.l17 = t92;
                        {
                            gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
                            gm_b16_8 t96 = gm_l13;
                            uint8_t t97 = (gm_eq0iqiubfm7e(((uint8_t *)&t95), ((uint8_t *)&t96)));
                            if (!t97) goto ie94;
                            uint8_t t98;
                            if ((((((uint8_t *)&F.l17) + 16))[4] != 0)) goto mn100;
                            t98 = ((uint8_t)0);
                            goto me99;
mn100:;
                            t98 = ((uint8_t)1);
                            goto me99;
mn101:;
                            gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 251, 31);
me99:;
                            if (!t98) goto ie94;
                            {
                                uint8_t t103 = 0;
                                F.ret = ((uint8_t)1);
                                goto gm_fret;
                            }
                            goto ix94;
ie94:;
ix94:;
                        }
                        goto me90;
mn91:;
                        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 250, 5);
me90:;
                    }
lc81:;
                    {
                        uint64_t t105 = gm_l16;
                        uint64_t r106; bool o107 = __builtin_add_overflow(t105, ((uint64_t)1ULL), &r106);
                        uint64_t t108 = r106;
                        gm_l16 = t108;
                    }
                }
lb81:;
            }
        }
        {
            uint8_t t109 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix43;
ie43:;
        {
            uint8_t t111 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
ix43:;
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
inline __attribute__((aligned(4))) void gm_f_remap_binds_kao301q(void * a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_remap_binds_kao301q F;
    void * gm_l0; /* p */
    uint32_t gm_l2; /* id */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_tcc1rq4n1avb2 gm_l5; /* m */
    GM_STACK_CHECK(&F, 191u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    F.l1 = a1;
    {
        void * t2 = gm_l0;
        uint8_t t5 = GM_LD(uint8_t, ((((uint8_t *)t2) + 8) + 0));
        if (t5 != ((uint8_t)1ULL)) goto mn4;
        uint32_t t6 = GM_LD(uint32_t, ((((uint8_t *)t2) + 8) + 8));
        gm_l2 = t6;
        {
            gm_b16_8 t8 = F.l1;
            gm_l3 = t8;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t10 = gm_l4;
                uint64_t t11 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t12 = (t10 < t11);
                if (!t12) goto lb9;
                {
                    uint64_t t15 = gm_l4;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_tcc1rq4n1avb2 t20 = GM_LD(gm_tcc1rq4n1avb2, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t16 * 8));
                    gm_l5 = t20;
                    {
                        uint32_t t23 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 0));
                        uint32_t t24 = gm_l2;
                        uint8_t t25 = ((t23 == t24));
                        if (!t25) goto ie22;
                        {
                            void * t27 = gm_l0;
                            uint32_t t28 = GM_LD(uint32_t, (((uint8_t *)&gm_l5) + 4));
                            memset(&(*(gm_tb38v5s9j1r32 *)(((uint8_t *)t27) + 8)), 0, sizeof (*(gm_tb38v5s9j1r32 *)(((uint8_t *)t27) + 8)));
                            GM_ST(uint8_t, (uint8_t *)&(*(gm_tb38v5s9j1r32 *)(((uint8_t *)t27) + 8)) + 0, ((uint8_t)1ULL));
                            GM_ST(uint32_t, (uint8_t *)&(*(gm_tb38v5s9j1r32 *)(((uint8_t *)t27) + 8)) + 8, t28);
                        }
                        {
                            goto lb9;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 167, 22);
me18:;
                }
lc9:;
                {
                    uint64_t t30 = gm_l4;
                    uint64_t r31; bool o32 = __builtin_add_overflow(t30, ((uint64_t)1ULL), &r31);
                    uint64_t t33 = r31;
                    gm_l4 = t33;
                }
            }
lb9:;
        }
        goto me3;
mn4:;
        void * t35 = gm_l0;
        gm_closure t36 = ((gm_closure){(void *)gm_f_fn_ho0p9pu, (void *)&F.h, cx});
        gm_f_for_each_subpat_arlrtrm(t35, t36);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me3;
mn34:;
        gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 166, 5);
me3:;
    }
    F.ret = GM_ZERO(gm_b0_1);
    goto gm_fret;
gm_lad:;
    if (gm_unw.kind == GM_RETURN && gm_unw.frame == &F.h) { gm_unw.kind = GM_NONE; goto gm_fret; }
    goto gm_fprop;
gm_fret:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_b0_1 gm_f_report_match_d77kbfe(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_tl874t2atnlh3 a2, gm_b16_8 a3) {
    struct gm_fr_report_match_d77kbfe F;
    void * gm_l0; /* cx */
    gm_tl874t2atnlh3 gm_l2; /* span */
    gm_b16_8 gm_l3; /* arms */
    gm_b16_8 gm_l4; /* missing */
    gm_tl874t2atnlh3 gm_l6; /* at */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    GM_STACK_CHECK(&F, 449u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        if (!(((((uint8_t *)a1) + 0))[16] != 0)) goto ie2;
        gm_b16_8 t3 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 0));
        gm_l4 = t3;
        {
            gm_tl874t2atnlh3 t5 = gm_l2;
            gm_b16_8 t6 = gm_l4;
            gm_tnpifv20irnph t7;
            GM_ST(gm_b16_8, ((uint8_t *)&t7 + 0), t6);
            gm_tabb710d9mq2e t8;
            gm_f_format_d277g96((void *)&t8, (void *)NULL, cx, NULL, t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            if (((((uint8_t *)&t8))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t8) + 8, gm_src786i8duem00q, 90, 31);
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)&t8) + 8));
            gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
            gm_f_error_s6hn871((void *)&F.l5, (void *)NULL, cx, NULL, t5, t10);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            if (!(((((uint8_t *)a1) + 40))[12] != 0)) goto ie11;
            gm_tl874t2atnlh3 t12 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)a1) + 40));
            gm_l6 = t12;
            {
                void * t14 = (void *)((uint8_t *)&F.l5);
                gm_tl874t2atnlh3 t15 = gm_l6;
                gm_b16_8 t16;
                GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s3ceboq671qr5));
                GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(38));
                gm_f_add_label_50fj7vv(t14, GM_CONV(gm_cseahvg8v7p0vs, t15), t16);
            }
            {
                void * t17 = (void *)((uint8_t *)&F.l5);
                gm_b16_8 t18;
                GM_ST(void *, (uint8_t *)&t18, (void *)(gm_s67h6d8kdg7qc));
                GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(166));
                gm_f_add_note_46v502s(t17, t18);
            }
            goto ix11;
ie11:;
ix11:;
        }
        {
            void * t19 = gm_l0;
            void * t20 = GM_LD(void *, (((uint8_t *)t19) + 0));
            gm_b0_1 t21 = gm_f_emit_dph5v1e((void *)NULL, cx, NULL, t20, &(*(gm_t4gut2c9fj1kt *)((uint8_t *)&F.l5)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 24));
            gm_l7 = t23;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t25 = gm_l8;
                uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t27 = (t25 < t26);
                if (!t27) goto lb24;
                {
                    uint64_t t30 = gm_l8;
                    uint64_t t31 = (uintptr_t)t30;
                    uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    uint64_t t33 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t31 * 8));
                    uint64_t t36 = GM_LD(uint64_t, ((uint8_t *)&t33));
                    gm_l9 = t36;
                    {
                        void * t38 = gm_l0;
                        uint64_t t39 = gm_l9;
                        uint64_t t40 = (uintptr_t)t39;
                        uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        if (t40 >= (uintptr_t)(t41)) gm_trap_index(t40, t41, gm_src786i8duem00q, 97, 44);
                        gm_tl874t2atnlh3 t42 = GM_LD(gm_tl874t2atnlh3, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t40 * 12));
                        gm_b16_8 t43;
                        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s5p756i54h2sb));
                        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(51));
                        gm_b0_1 t44 = gm_f_error_9huj44a((void *)NULL, cx, NULL, t38, t42, t43);
                        if (gm_unw.kind) {
                            goto gm_ll28;
                        }
                    }
                    goto me34;
mn35:;
                    gm_trap("no match arm matched the scrutinee", gm_src786i8duem00q, 97, 5);
me34:;
                }
lc24:;
                {
                    uint64_t t46 = gm_l8;
                    uint64_t r47; bool o48 = __builtin_add_overflow(t46, ((uint64_t)1ULL), &r47);
                    uint64_t t49 = r47;
                    gm_l8 = t49;
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

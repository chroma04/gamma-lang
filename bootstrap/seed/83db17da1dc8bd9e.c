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
typedef struct gm_cs32e03j420uj1 { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_cs32e03j420uj1;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint8_t a[40]; } gm_t6kvj62qe5fp2;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { double f0; } gm_tidu2er0d7h95;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint16_t f0; } gm_tu0pangbbaeec;
_Static_assert(sizeof(gm_cs32e03j420uj1) == 24 && _Alignof(gm_cs32e03j420uj1) == 8 && offsetof(gm_cs32e03j420uj1, f0) == 0 && offsetof(gm_cs32e03j420uj1, f1) == 8 && offsetof(gm_cs32e03j420uj1, f2) == 16 && offsetof(gm_cs32e03j420uj1, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t6kvj62qe5fp2) == 40 && _Alignof(gm_t6kvj62qe5fp2) == 1 && offsetof(gm_t6kvj62qe5fp2, a) == 0, "layout of `[40]u8`");
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
_Static_assert(sizeof(gm_tidu2er0d7h95) == 8 && _Alignof(gm_tidu2er0d7h95) == 8 && offsetof(gm_tidu2er0d7h95, f0) == 0, "layout of `(f64,)`");
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_t88rcucg1b7uk a[2]; } gm_tqsk8dt6ib3qq;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
_Static_assert(sizeof(gm_tu0pangbbaeec) == 2 && _Alignof(gm_tu0pangbbaeec) == 2 && offsetof(gm_tu0pangbbaeec, f0) == 0, "layout of `(u16,)`");
typedef struct { gm_b16_8 f0; uint16_t f1; uint8_t pe[6]; } gm_tvaas22dl64e0;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
typedef struct { uint64_t f0; gm_tiilmfd16v3fc f1; } gm_t4f5fr60fo50r;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tqsk8dt6ib3qq) == 8 && _Alignof(gm_tqsk8dt6ib3qq) == 4 && offsetof(gm_tqsk8dt6ib3qq, a) == 0, "layout of `[2]Ty`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tvaas22dl64e0) == 24 && _Alignof(gm_tvaas22dl64e0) == 8 && offsetof(gm_tvaas22dl64e0, f0) == 0 && offsetof(gm_tvaas22dl64e0, f1) == 16, "layout of `(string, u16)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
_Static_assert(sizeof(gm_t4f5fr60fo50r) == 32 && _Alignof(gm_t4f5fr60fo50r) == 8 && offsetof(gm_t4f5fr60fo50r, f0) == 0 && offsetof(gm_t4f5fr60fo50r, f1) == 8, "layout of `(i: usize, x: EffectRef)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t88rcucg1b7uk v0; gm_t428ue2u3hf5g v1; } f1; } gm_toa2vhe3l4hfr;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
typedef struct { uint64_t f0; gm_toa2vhe3l4hfr f1; } gm_tdr8bh2bm3fc8;
_Static_assert(sizeof(gm_toa2vhe3l4hfr) == 40 && _Alignof(gm_toa2vhe3l4hfr) == 8 && offsetof(gm_toa2vhe3l4hfr, f0) == 0 && offsetof(gm_toa2vhe3l4hfr, f1) == 8, "layout of `GenericArg`");
_Static_assert(sizeof(gm_tdr8bh2bm3fc8) == 48 && _Alignof(gm_tdr8bh2bm3fc8) == 8 && offsetof(gm_tdr8bh2bm3fc8, f0) == 0 && offsetof(gm_tdr8bh2bm3fc8, f1) == 8, "layout of `(i: usize, x: GenericArg)`");
struct gm_fr_args_881ap90 {
    gm_frame h;
    gm_toa2vhe3l4hfr l6; /* a */
    gm_b0_1 ret;
};
struct gm_fr_components_dvsj2j9 {
    gm_frame h;
    gm_tddtjfggdat0d l25; /* sig */
    gm_tddtjfggdat0d l26; /* sig */
    gm_b0_1 ret;
};
struct gm_fr_effect_ref_6sjtehp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_effects_mg3vf2a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_is_empty_set_3vntabf {
    gm_frame h;
    gm_toa2vhe3l4hfr l1; /* a */
    uint8_t ret;
};
struct gm_fr_key_fr74f0n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_parse_exponent_4uekfcv {
    gm_frame h;
    int64_t ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_show_effects_vieja9u {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_show_ref_aqt1t7c {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_sig_uguln8a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_ty_vka2r8h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_debug_string_pns0400 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_write_float_7p0icl6 {
    gm_frame h;
    gm_trqfvmjtt4sij l7; /* digits */
    gm_b0_1 ret;
};
struct gm_fr_write_int_lq1ktof {
    gm_frame h;
    gm_t6kvj62qe5fp2 l8; /* digits */
    gm_b0_1 ret;
};
bool gm_eqo3v9599nus(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) gm_b16_8 gm_f_adt_name_h7mjik7(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_args_881ap90(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_components_dvsj2j9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_effect_name_prpp0ip(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_effects_mg3vf2a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_format_gu67n99(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_index_of_hdeg351(gm_b16_8 a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_is_empty_set_3vntabf(void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_is_empty_set_3vntabf_c(void * a0, gm_toa2vhe3l4hfr a1);
__attribute__((aligned(4))) uint8_t gm_f_is_unit_n83ttnb(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_key_fr74f0n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_len_op_symbol_2qoiciu(gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_param_name_rbnop5l(void * a0, uint32_t a1);
__attribute__((aligned(4))) int64_t gm_f_parse_exponent_4uekfcv(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
__attribute__((aligned(4))) gm_cs32e03j420uj1 gm_f_printer_h7jigd7_c(void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_effects_vieja9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_sig_uguln8a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint32_t gm_f_template_9o92cvv(void * a0, uint32_t a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_0f0dp07(void * a0, gm_tu0pangbbaeec a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_2q6gmep(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_8gmb252(void * a0, const void * a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_debug_string_pns0400(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_e9824eu(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_float_7p0icl6(void *env, gm_node *cx, gm_node *inst, double a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_g9nufph(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_int_lq1ktof(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1, void * a2);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kki8q0t(void * a0, gm_t9qkh0rsooc8d a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_m28h1lv(void * a0, gm_tnpifv20irnph a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_n5e95le(void * a0, gm_t4ce0jd3tdro4 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_u50pgu2(void * a0, gm_t4ce0jd3tdro4 a1);
extern const uint8_t gm_s0i3i86nphpsh[6];
extern const uint8_t gm_s0iqiubfm7e7e[7];
extern const uint8_t gm_s0lrfrtvffhu7[4];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0m9d22aen6f8[2];
extern const uint8_t gm_s15tjb1oda5hu[6];
extern const uint8_t gm_s1rqgg2ibsis6[10];
extern const uint8_t gm_s22g482qk7a40[3];
extern const uint8_t gm_s2737i1iq57nf[5];
extern const uint8_t gm_s2v5amvfkfoll[3];
extern const uint8_t gm_s3cv29r83pgn4[6];
extern const uint8_t gm_s3ndsddjcvskd[3];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s4imhc4v1tf2p[10];
extern const uint8_t gm_s579fb7l5l6df[3];
extern const uint8_t gm_s5j8t1cg2erp9[8];
extern const uint8_t gm_s6groq37ram3h[6];
extern const uint8_t gm_s6gt8b7hsisgu[3];
extern const uint8_t gm_s7mhpbh399e5s[4];
extern const uint8_t gm_s929lennfd3g6[5];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_sb7t8ia5pj1l5[19];
extern const uint8_t gm_sbvutvpa8pe3d[3];
extern const uint8_t gm_scp1b2sk2nlmr[8];
extern const uint8_t gm_sdbvaa960ee0f[5];
extern const uint8_t gm_segch6a5dme9u[5];
extern const uint8_t gm_sehko5bb96s6p[5];
extern const uint8_t gm_sfif278eeflhm[4];
extern const uint8_t gm_sfqmvjqk1sdmt[6];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_shsv7bl83q04d[9];
extern const uint8_t gm_sic5vsema0r36[8];
extern const uint8_t gm_sitlbs33jqqal[2];
extern const uint8_t gm_sj21lfdl9q0t4[6];
extern const uint8_t gm_sjq55q61p743j[3];
extern const uint8_t gm_slolcr6ikvg33[7];
extern const uint8_t gm_sm337p11h7spc[8];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_sns75reqq32d2[4];
extern const uint8_t gm_sogehh8t1m7tn[6];
extern const uint8_t gm_soqt3jnmovq5l[4];
extern const uint8_t gm_squedv51820an[5];
extern const char gm_srcp2qdr7uqsmui[];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const gm_type gm_tyo86ks9qkvt;
__attribute__((aligned(4))) gm_b0_1 gm_f_args_881ap90(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_args_881ap90 F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* args */
    uint64_t gm_l2; /* n */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l5; /* i */
    gm_t88rcucg1b7uk gm_l7; /* t */
    gm_t428ue2u3hf5g gm_l8; /* k */
    GM_STACK_CHECK(&F, 600u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        gm_l2 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l2;
            uint8_t t5 = (t4 > ((uint64_t)0ULL));
            if (!t5) goto lb3;
            void * t6 = gm_l0;
            uint64_t t7 = gm_l2;
            uint64_t r8; bool o9 = __builtin_sub_overflow(t7, ((uint64_t)1ULL), &r8);
            if (o9) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 203, 41);
            uint64_t t10 = r8;
            uint64_t t11 = (uintptr_t)t10;
            uint64_t t12 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            if (t11 >= (uintptr_t)(t12)) gm_trap_index(t11, t12, gm_srcp2qdr7uqsmui, 203, 36);
            uint8_t t13 = gm_f_is_empty_set_3vntabf(t6, &(*(gm_toa2vhe3l4hfr *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t11 * 40)));
            if (!t13) goto lb3;
            {
                uint64_t t16 = gm_l2;
                uint64_t r17; bool o18 = __builtin_sub_overflow(t16, ((uint64_t)1ULL), &r17);
                if (o18) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 203, 51);
                uint64_t t19 = r17;
                gm_l2 = t19;
            }
lc3:;
        }
lb3:;
    }
    {
        uint64_t t21 = gm_l2;
        uint8_t t22 = ((t21 == ((uint64_t)0ULL)));
        if (!t22) goto ie20;
        {
            gm_b0_1 t24;
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix20;
ie20:;
ix20:;
    }
    {
        void * t26 = gm_l0;
        void * t27 = GM_LD(void *, (((uint8_t *)t26) + 8));
        gm_tftb1odc739t5 t28 = gm_f_push_t83rg2m(t27, ((uint8_t)40ULL));
        gm_tftb1odc739t5 t29 = t28;
        if (((((uint8_t *)&t29))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t29) + 1, gm_srcp2qdr7uqsmui, 205, 5);
        gm_b0_1 t30 = GM_LD(gm_b0_1, (((uint8_t *)&t29) + 1));
    }
    {
        {
            uint64_t t32 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            void * t33 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
            uint64_t t34 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t35 = gm_l2;
            uint64_t t36 = (uintptr_t)t35;
            if (t34 > t36 || t36 > (uintptr_t)t32) gm_trap_slice(t34, t36, t32, gm_srcp2qdr7uqsmui, 206, 27);
            gm_b16_8 t37;
            GM_ST(void *, (uint8_t *)&t37, (void *)(((uint8_t *)t33 + t34 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)(t36 - t34));
            gm_l3 = t37;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t39 = gm_l4;
                uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t41 = (t39 < t40);
                if (!t41) goto lb38;
                {
                    gm_tdr8bh2bm3fc8 t44;
                    uint64_t t45 = gm_l4;
                    GM_ST(uint64_t, (uint8_t *)&t44 + 0, t45);
                    uint64_t t46 = gm_l4;
                    uint64_t t47 = (uintptr_t)t46;
                    uint64_t t48 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    memcpy((uint8_t *)&t44 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t47 * 40), 40);
                    gm_tdr8bh2bm3fc8 t49 = t44;
                    uint64_t t52 = GM_LD(uint64_t, (((uint8_t *)&t49) + 0));
                    gm_l5 = t52;
                    gm_toa2vhe3l4hfr t53 = GM_LD(gm_toa2vhe3l4hfr, (((uint8_t *)&t49) + 8));
                    F.l6 = t53;
                    {
                        uint64_t t56 = gm_l5;
                        uint8_t t57 = (t56 > ((uint64_t)0ULL));
                        if (!t57) goto ie55;
                        {
                            void * t59 = gm_l0;
                            void * t60 = GM_LD(void *, (((uint8_t *)t59) + 8));
                            gm_b16_8 t61;
                            GM_ST(void *, (uint8_t *)&t61, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t62 = gm_f_extend_0cs85gs(t60, t61);
                            gm_tftb1odc739t5 t63 = t62;
                            if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srcp2qdr7uqsmui, 207, 20);
                            gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
                        }
                        goto ix55;
ie55:;
ix55:;
                    }
                    {
                        uint8_t t67 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t67 != ((uint8_t)0ULL)) goto mn66;
                        gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l6) + 8));
                        gm_l7 = t68;
                        void * t69 = gm_l0;
                        gm_t88rcucg1b7uk t70 = gm_l7;
                        gm_b0_1 t71 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t69, t70);
                        if (gm_unw.kind) {
                            goto gm_ll42;
                        }
                        goto me65;
mn66:;
                        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&F.l6) + 0));
                        if (t73 != ((uint8_t)1ULL)) goto mn72;
                        gm_t428ue2u3hf5g t74 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&F.l6) + 8));
                        gm_l8 = t74;
                        void * t75 = gm_l0;
                        void * t76 = (void *)((uint8_t *)&gm_l8);
                        gm_b0_1 t77 = gm_f_key_fr74f0n((void *)NULL, cx, NULL, t75, t76);
                        if (gm_unw.kind) {
                            goto gm_ll42;
                        }
                        goto me65;
mn72:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 208, 9);
me65:;
                    }
                    goto me50;
mn51:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 206, 5);
me50:;
                }
lc38:;
                {
                    uint64_t t79 = gm_l4;
                    uint64_t r80; bool o81 = __builtin_add_overflow(t79, ((uint64_t)1ULL), &r80);
                    uint64_t t82 = r80;
                    gm_l4 = t82;
                }
            }
gm_ll42:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb38; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc38; }
            }
            goto gm_lad;
lb38:;
        }
    }
    {
        void * t83 = gm_l0;
        void * t84 = GM_LD(void *, (((uint8_t *)t83) + 8));
        gm_tftb1odc739t5 t85 = gm_f_push_t83rg2m(t84, ((uint8_t)41ULL));
        gm_tftb1odc739t5 t86 = t85;
        if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srcp2qdr7uqsmui, 213, 5);
        gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
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
__attribute__((aligned(4))) gm_b0_1 gm_f_components_dvsj2j9(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_components_dvsj2j9 F;
    void * gm_l0; /* p */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    void * gm_l2; /* out */
    uint8_t gm_l3; /* signed */
    uint16_t gm_l4; /* bits */
    uint16_t gm_l5; /* bits */
    uint8_t gm_l6; /* c */
    gm_t88rcucg1b7uk gm_l7; /* t */
    uint8_t gm_l8; /* c */
    gm_t88rcucg1b7uk gm_l9; /* t */
    uint8_t gm_l10; /* c */
    gm_t88rcucg1b7uk gm_l11; /* t */
    uint64_t gm_l12; /* len */
    gm_t88rcucg1b7uk gm_l13; /* elem */
    gm_tnnjeerolos0a gm_l14; /* kind */
    gm_t88rcucg1b7uk gm_l15; /* e */
    gm_b16_8 gm_l16; /* elems */
    gm_b16_8 gm_l17; /* names */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t88rcucg1b7uk gm_l21; /* t */
    gm_t88rcucg1b7uk gm_l22; /* t */
    gm_t88rcucg1b7uk gm_l23; /* err */
    gm_t88rcucg1b7uk gm_l24; /* ok */
    uint32_t gm_l27; /* id */
    gm_b16_8 gm_l28; /* args */
    gm_b16_8 gm_l29; /* name */
    gm_t88rcucg1b7uk gm_l30; /* t */
    gm_t88rcucg1b7uk gm_l31; /* t */
    uint32_t gm_l32; /* id */
    gm_t88rcucg1b7uk gm_l33; /* len */
    gm_t88rcucg1b7uk gm_l34; /* elem */
    gm_t9qkh0rsooc8d gm_l35; /* op */
    gm_t88rcucg1b7uk gm_l36; /* lhs */
    gm_t88rcucg1b7uk gm_l37; /* rhs */
    gm_tqsk8dt6ib3qq gm_l38; /* <temporary array> */
    gm_b16_8 gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* i */
    gm_t88rcucg1b7uk gm_l42; /* t */
    gm_t428ue2u3hf5g gm_l43; /* k */
    gm_tnpifv20irnph gm_l44; /* set */
    GM_STACK_CHECK(&F, 2974u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l0;
        void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        switch (t38) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma15; case ((uint8_t)6ULL): goto ma16; case ((uint8_t)7ULL): goto ma17; case ((uint8_t)8ULL): goto ma18; case ((uint8_t)9ULL): goto ma19; case ((uint8_t)10ULL): goto ma20; case ((uint8_t)11ULL): goto ma21; case ((uint8_t)12ULL): goto ma22; case ((uint8_t)13ULL): goto ma23; case ((uint8_t)14ULL): goto ma24; case ((uint8_t)15ULL): goto ma25; case ((uint8_t)16ULL): goto ma26; case ((uint8_t)17ULL): goto ma27; case ((uint8_t)18ULL): goto ma28; case ((uint8_t)19ULL): goto ma29; case ((uint8_t)20ULL): goto ma30; case ((uint8_t)21ULL): goto ma32; case ((uint8_t)22ULL): goto ma31; case ((uint8_t)23ULL): goto ma35; case ((uint8_t)24ULL): goto ma33; case ((uint8_t)25ULL): goto ma34; case ((uint8_t)26ULL): goto ma36; default: goto ma37; }
ma10:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t40 != ((uint8_t)0ULL)) goto mn39;
        uint8_t t41 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l3 = t41;
        uint16_t t42 = GM_LD(uint16_t, ((((uint8_t *)&t8) + 8) + 2));
        gm_l4 = t42;
        void * t43 = gm_l2;
        gm_tvaas22dl64e0 t44;
        memset((uint8_t *)&t44 + 18, 0, 6);
        gm_b16_8 t45;
        uint8_t t47 = gm_l3;
        if (!t47) goto ie46;
        gm_b16_8 t49;
        GM_ST(void *, (uint8_t *)&t49, (void *)(gm_sitlbs33jqqal));
        GM_ST(uintptr_t, (uint8_t *)&t49 + 8, (uintptr_t)(1));
        gm_b16_8 t50 = t49;
        t45 = t50;
        goto ix46;
ie46:;
        gm_b16_8 t52;
        GM_ST(void *, (uint8_t *)&t52, (void *)(gm_s0m9d22aen6f8));
        GM_ST(uintptr_t, (uint8_t *)&t52 + 8, (uintptr_t)(1));
        gm_b16_8 t53 = t52;
        t45 = t53;
ix46:;
        GM_ST(gm_b16_8, (uint8_t *)&t44 + 0, t45);
        uint16_t t54 = gm_l4;
        GM_ST(uint16_t, (uint8_t *)&t44 + 16, t54);
        gm_tftb1odc739t5 t55 = gm_f_write_8gmb252(t43, &t44);
        gm_tftb1odc739t5 t56 = t55;
        if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 1, gm_srcp2qdr7uqsmui, 56, 31);
        gm_b0_1 t57 = GM_LD(gm_b0_1, (((uint8_t *)&t56) + 1));
        goto me9;
mn39:;
ma11:;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t59 != ((uint8_t)1ULL)) goto mn58;
        void * t60 = gm_l2;
        gm_b16_8 t61;
        GM_ST(void *, (uint8_t *)&t61, (void *)(gm_s6groq37ram3h));
        GM_ST(uintptr_t, (uint8_t *)&t61 + 8, (uintptr_t)(5));
        gm_tftb1odc739t5 t62 = gm_f_extend_0cs85gs(t60, t61);
        gm_tftb1odc739t5 t63 = t62;
        if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 1, gm_srcp2qdr7uqsmui, 57, 19);
        gm_b0_1 t64 = GM_LD(gm_b0_1, (((uint8_t *)&t63) + 1));
        goto me9;
mn58:;
ma12:;
        uint8_t t66 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t66 != ((uint8_t)2ULL)) goto mn65;
        void * t67 = gm_l2;
        gm_b16_8 t68;
        GM_ST(void *, (uint8_t *)&t68, (void *)(gm_s0i3i86nphpsh));
        GM_ST(uintptr_t, (uint8_t *)&t68 + 8, (uintptr_t)(5));
        gm_tftb1odc739t5 t69 = gm_f_extend_0cs85gs(t67, t68);
        gm_tftb1odc739t5 t70 = t69;
        if (((((uint8_t *)&t70))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t70) + 1, gm_srcp2qdr7uqsmui, 58, 19);
        gm_b0_1 t71 = GM_LD(gm_b0_1, (((uint8_t *)&t70) + 1));
        goto me9;
mn65:;
ma13:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t73 != ((uint8_t)3ULL)) goto mn72;
        uint16_t t74 = GM_LD(uint16_t, (((uint8_t *)&t8) + 8));
        gm_l5 = t74;
        void * t75 = gm_l2;
        uint16_t t76 = gm_l5;
        gm_tu0pangbbaeec t77;
        GM_ST(uint16_t, ((uint8_t *)&t77 + 0), t76);
        gm_tftb1odc739t5 t78 = gm_f_write_0f0dp07(t75, t77);
        gm_tftb1odc739t5 t79 = t78;
        if (((((uint8_t *)&t79))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t79) + 1, gm_srcp2qdr7uqsmui, 59, 25);
        gm_b0_1 t80 = GM_LD(gm_b0_1, (((uint8_t *)&t79) + 1));
        goto me9;
mn72:;
ma14:;
        uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t82 != ((uint8_t)4ULL)) goto mn81;
        void * t83 = gm_l2;
        gm_b16_8 t84;
        GM_ST(void *, (uint8_t *)&t84, (void *)(gm_s929lennfd3g6));
        GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(4));
        gm_tftb1odc739t5 t85 = gm_f_extend_0cs85gs(t83, t84);
        gm_tftb1odc739t5 t86 = t85;
        if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srcp2qdr7uqsmui, 60, 18);
        gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
        goto me9;
mn81:;
ma15:;
        uint8_t t89 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t89 != ((uint8_t)5ULL)) goto mn88;
        void * t90 = gm_l2;
        gm_b16_8 t91;
        GM_ST(void *, (uint8_t *)&t91, (void *)(gm_s3cv29r83pgn4));
        GM_ST(uintptr_t, (uint8_t *)&t91 + 8, (uintptr_t)(5));
        gm_tftb1odc739t5 t92 = gm_f_extend_0cs85gs(t90, t91);
        gm_tftb1odc739t5 t93 = t92;
        if (((((uint8_t *)&t93))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t93) + 1, gm_srcp2qdr7uqsmui, 61, 19);
        gm_b0_1 t94 = GM_LD(gm_b0_1, (((uint8_t *)&t93) + 1));
        goto me9;
mn88:;
ma16:;
        uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t96 != ((uint8_t)6ULL)) goto mn95;
        void * t97 = gm_l2;
        gm_b16_8 t98;
        GM_ST(void *, (uint8_t *)&t98, (void *)(gm_s2737i1iq57nf));
        GM_ST(uintptr_t, (uint8_t *)&t98 + 8, (uintptr_t)(4));
        gm_tftb1odc739t5 t99 = gm_f_extend_0cs85gs(t97, t98);
        gm_tftb1odc739t5 t100 = t99;
        if (((((uint8_t *)&t100))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t100) + 1, gm_srcp2qdr7uqsmui, 62, 18);
        gm_b0_1 t101 = GM_LD(gm_b0_1, (((uint8_t *)&t100) + 1));
        goto me9;
mn95:;
ma17:;
        uint8_t t103 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t103 != ((uint8_t)7ULL)) goto mn102;
        void * t104 = gm_l2;
        gm_b16_8 t105;
        GM_ST(void *, (uint8_t *)&t105, (void *)(gm_slolcr6ikvg33));
        GM_ST(uintptr_t, (uint8_t *)&t105 + 8, (uintptr_t)(6));
        gm_tftb1odc739t5 t106 = gm_f_extend_0cs85gs(t104, t105);
        gm_tftb1odc739t5 t107 = t106;
        if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 1, gm_srcp2qdr7uqsmui, 63, 20);
        gm_b0_1 t108 = GM_LD(gm_b0_1, (((uint8_t *)&t107) + 1));
        goto me9;
mn102:;
ma18:;
        uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t110 != ((uint8_t)8ULL)) goto mn109;
        uint8_t t111 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l6 = t111;
        gm_t88rcucg1b7uk t112 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 4));
        gm_l7 = t112;
        {
            void * t114 = gm_l2;
            gm_b16_8 t115;
            uint8_t t117 = gm_l6;
            if (!t117) goto ie116;
            gm_b16_8 t119;
            GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sm337p11h7spc));
            GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(7));
            gm_b16_8 t120 = t119;
            t115 = t120;
            goto ix116;
ie116:;
            gm_b16_8 t122;
            GM_ST(void *, (uint8_t *)&t122, (void *)(gm_srs1vrn2uaams));
            GM_ST(uintptr_t, (uint8_t *)&t122 + 8, (uintptr_t)(1));
            gm_b16_8 t123 = t122;
            t115 = t123;
ix116:;
            gm_tftb1odc739t5 t124 = gm_f_extend_0cs85gs(t114, t115);
            gm_tftb1odc739t5 t125 = t124;
            if (((((uint8_t *)&t125))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t125) + 1, gm_srcp2qdr7uqsmui, 65, 13);
            gm_b0_1 t126 = GM_LD(gm_b0_1, (((uint8_t *)&t125) + 1));
        }
        {
            void * t127 = gm_l0;
            gm_t88rcucg1b7uk t128 = gm_l7;
            gm_b0_1 t129 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t127, t128);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me9;
mn109:;
ma19:;
        uint8_t t131 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t131 != ((uint8_t)9ULL)) goto mn130;
        uint8_t t132 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l8 = t132;
        gm_t88rcucg1b7uk t133 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 4));
        gm_l9 = t133;
        {
            void * t135 = gm_l2;
            gm_b16_8 t136;
            uint8_t t138 = gm_l8;
            if (!t138) goto ie137;
            gm_b16_8 t140;
            GM_ST(void *, (uint8_t *)&t140, (void *)(gm_s1rqgg2ibsis6));
            GM_ST(uintptr_t, (uint8_t *)&t140 + 8, (uintptr_t)(9));
            gm_b16_8 t141 = t140;
            t136 = t141;
            goto ix137;
ie137:;
            gm_b16_8 t143;
            GM_ST(void *, (uint8_t *)&t143, (void *)(gm_sfif278eeflhm));
            GM_ST(uintptr_t, (uint8_t *)&t143 + 8, (uintptr_t)(3));
            gm_b16_8 t144 = t143;
            t136 = t144;
ix137:;
            gm_tftb1odc739t5 t145 = gm_f_extend_0cs85gs(t135, t136);
            gm_tftb1odc739t5 t146 = t145;
            if (((((uint8_t *)&t146))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t146) + 1, gm_srcp2qdr7uqsmui, 69, 13);
            gm_b0_1 t147 = GM_LD(gm_b0_1, (((uint8_t *)&t146) + 1));
        }
        {
            void * t148 = gm_l0;
            gm_t88rcucg1b7uk t149 = gm_l9;
            gm_b0_1 t150 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t148, t149);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me9;
mn130:;
ma20:;
        uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t152 != ((uint8_t)10ULL)) goto mn151;
        uint8_t t153 = GM_LD(uint8_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l10 = t153;
        gm_t88rcucg1b7uk t154 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 4));
        gm_l11 = t154;
        {
            uint8_t t157 = gm_l10;
            if (!t157) goto ie156;
            void * t158 = gm_l0;
            void * t159 = GM_LD(void *, (((uint8_t *)t158) + 0));
            gm_t88rcucg1b7uk t160 = gm_l11;
            gm_t53at80pfdus3 t161;
            gm_f_kind_jcnt4hm((void *)&t161, t159, GM_CONV(gm_csguankko4lmhd, t160));
            gm_t53at80pfdus3 t162 = t161;
            gm_tjcj4j2gbh6c8 t163;
            GM_ST(uint8_t, (uint8_t *)&t163 + 1, 0);
            GM_ST(uint8_t, ((uint8_t *)&t163 + 0), ((uint8_t)0));
            GM_ST(uint16_t, ((uint8_t *)&t163 + 2), ((uint16_t)8ULL));
            gm_tjcj4j2gbh6c8 t164 = t163;
            uint8_t t165 = (((GM_LD(uint8_t, ((uint8_t *)&t162) + 0) == ((uint8_t)0ULL)) && gm_eqo3v9599nus((((uint8_t *)&t162) + 8), ((uint8_t *)&t164))));
            if (!t165) goto ie156;
            {
                void * t167 = gm_l2;
                gm_b16_8 t168;
                GM_ST(void *, (uint8_t *)&t168, (void *)(gm_s0iqiubfm7e7e));
                GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(6));
                gm_tftb1odc739t5 t169 = gm_f_extend_0cs85gs(t167, t168);
                gm_tftb1odc739t5 t170 = t169;
                if (((((uint8_t *)&t170))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t170) + 1, gm_srcp2qdr7uqsmui, 74, 17);
                gm_b0_1 t171 = GM_LD(gm_b0_1, (((uint8_t *)&t170) + 1));
            }
            goto ix156;
ie156:;
            {
                void * t173 = gm_l2;
                gm_b16_8 t174;
                uint8_t t176 = gm_l10;
                if (!t176) goto ie175;
                gm_b16_8 t178;
                GM_ST(void *, (uint8_t *)&t178, (void *)(gm_shsv7bl83q04d));
                GM_ST(uintptr_t, (uint8_t *)&t178 + 8, (uintptr_t)(8));
                gm_b16_8 t179 = t178;
                t174 = t179;
                goto ix175;
ie175:;
                gm_b16_8 t181;
                GM_ST(void *, (uint8_t *)&t181, (void *)(gm_s3ndsddjcvskd));
                GM_ST(uintptr_t, (uint8_t *)&t181 + 8, (uintptr_t)(2));
                gm_b16_8 t182 = t181;
                t174 = t182;
ix175:;
                gm_tftb1odc739t5 t183 = gm_f_extend_0cs85gs(t173, t174);
                gm_tftb1odc739t5 t184 = t183;
                if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 1, gm_srcp2qdr7uqsmui, 76, 17);
                gm_b0_1 t185 = GM_LD(gm_b0_1, (((uint8_t *)&t184) + 1));
            }
            {
                void * t186 = gm_l0;
                gm_t88rcucg1b7uk t187 = gm_l11;
                gm_b0_1 t188 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t186, t187);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
ix156:;
        }
        goto me9;
mn151:;
ma21:;
        uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t190 != ((uint8_t)11ULL)) goto mn189;
        uint64_t t191 = GM_LD(uint64_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l12 = t191;
        gm_t88rcucg1b7uk t192 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 8));
        gm_l13 = t192;
        gm_tnnjeerolos0a t193 = GM_LD(gm_tnnjeerolos0a, ((((uint8_t *)&t8) + 8) + 12));
        gm_l14 = t193;
        {
            uint8_t t201 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            switch (t201) { case ((uint8_t)0ULL): goto ma199; case ((uint8_t)1ULL): goto ma196; case ((uint8_t)2ULL): goto ma197; case ((uint8_t)3ULL): goto ma198; default: goto ma200; }
ma196:;
            uint8_t t203 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            if (t203 != ((uint8_t)1ULL)) goto mn202;
            gm_t88rcucg1b7uk t204 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l14) + 4));
            gm_l15 = t204;
            {
                void * t206 = gm_l2;
                gm_tftb1odc739t5 t207 = gm_f_push_t83rg2m(t206, ((uint8_t)91ULL));
                gm_tftb1odc739t5 t208 = t207;
                if (((((uint8_t *)&t208))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t208) + 1, gm_srcp2qdr7uqsmui, 83, 21);
                gm_b0_1 t209 = GM_LD(gm_b0_1, (((uint8_t *)&t208) + 1));
            }
            {
                void * t210 = gm_l0;
                gm_t88rcucg1b7uk t211 = gm_l15;
                gm_b0_1 t212 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t210, t211);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t213 = gm_l2;
                gm_tftb1odc739t5 t214 = gm_f_push_t83rg2m(t213, ((uint8_t)93ULL));
                gm_tftb1odc739t5 t215 = t214;
                if (((((uint8_t *)&t215))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t215) + 1, gm_srcp2qdr7uqsmui, 85, 21);
                gm_b0_1 t216 = GM_LD(gm_b0_1, (((uint8_t *)&t215) + 1));
            }
            goto me195;
mn202:;
ma197:;
            uint8_t t218 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            if (t218 != ((uint8_t)2ULL)) goto mn217;
            void * t219 = gm_l2;
            uint64_t t220 = gm_l12;
            gm_t4ce0jd3tdro4 t221;
            GM_ST(uint64_t, ((uint8_t *)&t221 + 0), t220);
            gm_tftb1odc739t5 t222 = gm_f_write_n5e95le(t219, t221);
            gm_tftb1odc739t5 t223 = t222;
            if (((((uint8_t *)&t223))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t223) + 1, gm_srcp2qdr7uqsmui, 87, 26);
            gm_b0_1 t224 = GM_LD(gm_b0_1, (((uint8_t *)&t223) + 1));
            goto me195;
mn217:;
ma198:;
            uint8_t t226 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            if (t226 != ((uint8_t)3ULL)) goto mn225;
            void * t227 = gm_l2;
            uint64_t t228 = gm_l12;
            gm_t4ce0jd3tdro4 t229;
            GM_ST(uint64_t, ((uint8_t *)&t229 + 0), t228);
            gm_tftb1odc739t5 t230 = gm_f_write_2q6gmep(t227, t229);
            gm_tftb1odc739t5 t231 = t230;
            if (((((uint8_t *)&t231))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t231) + 1, gm_srcp2qdr7uqsmui, 88, 26);
            gm_b0_1 t232 = GM_LD(gm_b0_1, (((uint8_t *)&t231) + 1));
            goto me195;
mn225:;
ma199:;
            uint8_t t234 = GM_LD(uint8_t, (((uint8_t *)&gm_l14) + 0));
            if (t234 != ((uint8_t)0ULL)) goto mn233;
            void * t235 = gm_l2;
            uint64_t t236 = gm_l12;
            gm_t4ce0jd3tdro4 t237;
            GM_ST(uint64_t, ((uint8_t *)&t237 + 0), t236);
            gm_tftb1odc739t5 t238 = gm_f_write_u50pgu2(t235, t237);
            gm_tftb1odc739t5 t239 = t238;
            if (((((uint8_t *)&t239))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t239) + 1, gm_srcp2qdr7uqsmui, 89, 27);
            gm_b0_1 t240 = GM_LD(gm_b0_1, (((uint8_t *)&t239) + 1));
            goto me195;
mn233:;
ma200:;
            gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 81, 13);
me195:;
        }
        {
            void * t241 = gm_l0;
            gm_t88rcucg1b7uk t242 = gm_l13;
            gm_b0_1 t243 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t241, t242);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me9;
mn189:;
ma22:;
        uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t245 != ((uint8_t)12ULL)) goto mn244;
        gm_b16_8 t246 = GM_LD(gm_b16_8, ((((uint8_t *)&t8) + 8) + 0));
        gm_l16 = t246;
        gm_b16_8 t247 = GM_LD(gm_b16_8, ((((uint8_t *)&t8) + 8) + 16));
        gm_l17 = t247;
        {
            void * t249 = gm_l2;
            gm_tftb1odc739t5 t250 = gm_f_push_t83rg2m(t249, ((uint8_t)40ULL));
            gm_tftb1odc739t5 t251 = t250;
            if (((((uint8_t *)&t251))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t251) + 1, gm_srcp2qdr7uqsmui, 94, 13);
            gm_b0_1 t252 = GM_LD(gm_b0_1, (((uint8_t *)&t251) + 1));
        }
        {
            {
                gm_b16_8 t254 = gm_l16;
                gm_l18 = t254;
            }
            {
                gm_l19 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t256 = gm_l19;
                    uint64_t t257 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    uint8_t t258 = (t256 < t257);
                    if (!t258) goto lb255;
                    {
                        uint64_t t261 = gm_l19;
                        uint64_t t262 = gm_l19;
                        uint64_t t263 = (uintptr_t)t262;
                        uint64_t t264 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                        gm_t88rcucg1b7uk t265 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t263 * 4));
                        gm_tlcm4lvu3g1a8 t266;
                        GM_ST(uint32_t, (uint8_t *)&t266 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t266 + 0), t261);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t266 + 8), t265);
                        gm_tlcm4lvu3g1a8 t267 = t266;
                        uint64_t t270 = GM_LD(uint64_t, (((uint8_t *)&t267) + 0));
                        gm_l20 = t270;
                        gm_t88rcucg1b7uk t271 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t267) + 8));
                        gm_l21 = t271;
                        {
                            uint64_t t274 = gm_l20;
                            uint8_t t275 = (t274 > ((uint64_t)0ULL));
                            if (!t275) goto ie273;
                            {
                                void * t277 = gm_l2;
                                gm_b16_8 t278;
                                GM_ST(void *, (uint8_t *)&t278, (void *)(gm_sg5gdnrith2i6));
                                GM_ST(uintptr_t, (uint8_t *)&t278 + 8, (uintptr_t)(2));
                                gm_tftb1odc739t5 t279 = gm_f_extend_0cs85gs(t277, t278);
                                gm_tftb1odc739t5 t280 = t279;
                                if (((((uint8_t *)&t280))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t280) + 1, gm_srcp2qdr7uqsmui, 96, 28);
                                gm_b0_1 t281 = GM_LD(gm_b0_1, (((uint8_t *)&t280) + 1));
                            }
                            goto ix273;
ie273:;
ix273:;
                        }
                        {
                            uint64_t t283 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                            uint8_t t284 = (t283 > ((uint64_t)0ULL));
                            if (!t284) goto ie282;
                            {
                                void * t286 = gm_l2;
                                uint64_t t287 = gm_l20;
                                uint64_t t288 = (uintptr_t)t287;
                                uint64_t t289 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                if (t288 >= (uintptr_t)(t289)) gm_trap_index(t288, t289, gm_srcp2qdr7uqsmui, 97, 54);
                                gm_b16_8 t290 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l17)) + t288 * 16));
                                gm_tnpifv20irnph t291;
                                GM_ST(gm_b16_8, ((uint8_t *)&t291 + 0), t290);
                                gm_tftb1odc739t5 t292 = gm_f_write_g9nufph(t286, t291);
                                gm_tftb1odc739t5 t293 = t292;
                                if (((((uint8_t *)&t293))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t293) + 1, gm_srcp2qdr7uqsmui, 97, 36);
                                gm_b0_1 t294 = GM_LD(gm_b0_1, (((uint8_t *)&t293) + 1));
                            }
                            goto ix282;
ie282:;
ix282:;
                        }
                        {
                            void * t295 = gm_l0;
                            gm_t88rcucg1b7uk t296 = gm_l21;
                            gm_b0_1 t297 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t295, t296);
                            if (gm_unw.kind) {
                                goto gm_ll259;
                            }
                        }
                        goto me268;
mn269:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 95, 13);
me268:;
                    }
lc255:;
                    {
                        uint64_t t299 = gm_l19;
                        uint64_t r300; bool o301 = __builtin_add_overflow(t299, ((uint64_t)1ULL), &r300);
                        uint64_t t302 = r300;
                        gm_l19 = t302;
                    }
                }
gm_ll259:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb255; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc255; }
                }
                goto gm_lad;
lb255:;
            }
        }
        {
            uint64_t t304 = GM_LD(uintptr_t, ((uint8_t *)&gm_l16) + 8);
            uint8_t t305 = ((t304 == ((uint64_t)1ULL)));
            if (!t305) goto ie303;
            uint64_t t306 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
            uint8_t t307 = ((t306 == ((uint64_t)0ULL)));
            if (!t307) goto ie303;
            {
                void * t309 = gm_l2;
                gm_tftb1odc739t5 t310 = gm_f_push_t83rg2m(t309, ((uint8_t)44ULL));
                gm_tftb1odc739t5 t311 = t310;
                if (((((uint8_t *)&t311))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t311) + 1, gm_srcp2qdr7uqsmui, 100, 52);
                gm_b0_1 t312 = GM_LD(gm_b0_1, (((uint8_t *)&t311) + 1));
            }
            goto ix303;
ie303:;
ix303:;
        }
        {
            void * t313 = gm_l2;
            gm_tftb1odc739t5 t314 = gm_f_push_t83rg2m(t313, ((uint8_t)41ULL));
            gm_tftb1odc739t5 t315 = t314;
            if (((((uint8_t *)&t315))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t315) + 1, gm_srcp2qdr7uqsmui, 101, 13);
            gm_b0_1 t316 = GM_LD(gm_b0_1, (((uint8_t *)&t315) + 1));
        }
        goto me9;
mn244:;
ma23:;
        uint8_t t318 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t318 != ((uint8_t)13ULL)) goto mn317;
        gm_t88rcucg1b7uk t319 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t8) + 8));
        gm_l22 = t319;
        {
            void * t321 = gm_l2;
            gm_tftb1odc739t5 t322 = gm_f_push_t83rg2m(t321, ((uint8_t)63ULL));
            gm_tftb1odc739t5 t323 = t322;
            if (((((uint8_t *)&t323))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t323) + 1, gm_srcp2qdr7uqsmui, 104, 13);
            gm_b0_1 t324 = GM_LD(gm_b0_1, (((uint8_t *)&t323) + 1));
        }
        {
            void * t325 = gm_l0;
            gm_t88rcucg1b7uk t326 = gm_l22;
            gm_b0_1 t327 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t325, t326);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me9;
mn317:;
ma24:;
        uint8_t t329 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t329 != ((uint8_t)14ULL)) goto mn328;
        gm_t88rcucg1b7uk t330 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 0));
        gm_l23 = t330;
        gm_t88rcucg1b7uk t331 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 4));
        gm_l24 = t331;
        {
            void * t333 = gm_l0;
            gm_t88rcucg1b7uk t334 = gm_l23;
            gm_b0_1 t335 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t333, t334);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t336 = gm_l2;
            gm_tftb1odc739t5 t337 = gm_f_push_t83rg2m(t336, ((uint8_t)33ULL));
            gm_tftb1odc739t5 t338 = t337;
            if (((((uint8_t *)&t338))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t338) + 1, gm_srcp2qdr7uqsmui, 109, 13);
            gm_b0_1 t339 = GM_LD(gm_b0_1, (((uint8_t *)&t338) + 1));
        }
        {
            void * t340 = gm_l0;
            gm_t88rcucg1b7uk t341 = gm_l24;
            gm_b0_1 t342 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t340, t341);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me9;
mn328:;
ma25:;
        uint8_t t344 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t344 != ((uint8_t)15ULL)) goto mn343;
        gm_tddtjfggdat0d t345 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t8) + 8));
        F.l25 = t345;
        void * t346 = gm_l0;
        gm_b16_8 t347;
        GM_ST(void *, (uint8_t *)&t347, (void *)(gm_s579fb7l5l6df));
        GM_ST(uintptr_t, (uint8_t *)&t347 + 8, (uintptr_t)(2));
        void * t348 = (void *)((uint8_t *)&F.l25);
        gm_b0_1 t349 = gm_f_sig_uguln8a((void *)NULL, cx, NULL, t346, t347, t348);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me9;
mn343:;
ma26:;
        uint8_t t351 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t351 != ((uint8_t)16ULL)) goto mn350;
        gm_tddtjfggdat0d t352 = GM_LD(gm_tddtjfggdat0d, (((uint8_t *)&t8) + 8));
        F.l26 = t352;
        void * t353 = gm_l0;
        gm_b16_8 t354;
        GM_ST(void *, (uint8_t *)&t354, (void *)(gm_s7mhpbh399e5s));
        GM_ST(uintptr_t, (uint8_t *)&t354 + 8, (uintptr_t)(3));
        void * t355 = (void *)((uint8_t *)&F.l26);
        gm_b0_1 t356 = gm_f_sig_uguln8a((void *)NULL, cx, NULL, t353, t354, t355);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me9;
mn350:;
ma27:;
        uint8_t t358 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t358 != ((uint8_t)17ULL)) goto mn357;
        uint32_t t359 = GM_LD(uint32_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l27 = t359;
        gm_b16_8 t360 = GM_LD(gm_b16_8, ((((uint8_t *)&t8) + 8) + 8));
        gm_l28 = t360;
        {
            void * t362 = gm_l0;
            void * t363 = GM_LD(void *, (((uint8_t *)t362) + 0));
            uint32_t t364 = gm_l27;
            gm_b16_8 t365 = gm_f_adt_name_h7mjik7(t363, t364);
            gm_l29 = t365;
        }
        {
            void * t366 = gm_l2;
            gm_b16_8 t367 = gm_l29;
            gm_tftb1odc739t5 t368 = gm_f_extend_0cs85gs(t366, t367);
            gm_tftb1odc739t5 t369 = t368;
            if (((((uint8_t *)&t369))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t369) + 1, gm_srcp2qdr7uqsmui, 116, 13);
            gm_b0_1 t370 = GM_LD(gm_b0_1, (((uint8_t *)&t369) + 1));
        }
        {
            uint64_t t372 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
            uint8_t t373 = ((t372 == ((uint64_t)1ULL)));
            if (!t373) goto ie371;
            gm_b16_8 t374 = gm_l29;
            gm_b16_8 t375;
            GM_ST(void *, (uint8_t *)&t375, (void *)(gm_sj21lfdl9q0t4));
            GM_ST(uintptr_t, (uint8_t *)&t375 + 8, (uintptr_t)(5));
            uint8_t t376 = gm_f_starts_with_6vvt01e(t374, t375);
            if (!t376) goto ie371;
            {
                uint64_t t379 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t380 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                if (t379 >= (uintptr_t)(t380)) gm_trap_index(t379, t380, gm_srcp2qdr7uqsmui, 120, 32);
                uint8_t t381 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t379 * 40) + 0));
                if (t381 != ((uint8_t)0ULL)) goto ie378;
                gm_t88rcucg1b7uk t382 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l28)) + t379 * 40) + 8));
                gm_l30 = t382;
                void * t383 = gm_l0;
                void * t384 = GM_LD(void *, (((uint8_t *)t383) + 0));
                uint32_t t385 = gm_l27;
                uint32_t t386 = gm_f_template_9o92cvv(t384, t385);
                uint32_t t387 = gm_l27;
                uint8_t t388 = ((t386 == t387));
                if (!t388) goto ie378;
                {
                    void * t390 = gm_l0;
                    gm_t88rcucg1b7uk t391 = gm_l30;
                    gm_b0_1 t392 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t390, t391);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix378;
ie378:;
ix378:;
            }
            {
                gm_b0_1 t393;
                uint8_t t394 = 0;
                F.ret = t393;
                goto gm_fret;
            }
            goto ix371;
ie371:;
ix371:;
        }
        {
            void * t395 = gm_l0;
            gm_b16_8 t396 = gm_l28;
            gm_b0_1 t397 = gm_f_args_881ap90((void *)NULL, cx, NULL, t395, t396);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me9;
mn357:;
ma28:;
        uint8_t t399 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t399 != ((uint8_t)18ULL)) goto mn398;
        gm_t88rcucg1b7uk t400 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t8) + 8));
        gm_l31 = t400;
        {
            void * t402 = gm_l2;
            gm_b16_8 t403;
            GM_ST(void *, (uint8_t *)&t403, (void *)(gm_sfqmvjqk1sdmt));
            GM_ST(uintptr_t, (uint8_t *)&t403 + 8, (uintptr_t)(5));
            gm_tftb1odc739t5 t404 = gm_f_extend_0cs85gs(t402, t403);
            gm_tftb1odc739t5 t405 = t404;
            if (((((uint8_t *)&t405))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t405) + 1, gm_srcp2qdr7uqsmui, 126, 13);
            gm_b0_1 t406 = GM_LD(gm_b0_1, (((uint8_t *)&t405) + 1));
        }
        {
            void * t407 = gm_l0;
            gm_t88rcucg1b7uk t408 = gm_l31;
            gm_b0_1 t409 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t407, t408);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t410 = gm_l2;
            gm_tftb1odc739t5 t411 = gm_f_push_t83rg2m(t410, ((uint8_t)41ULL));
            gm_tftb1odc739t5 t412 = t411;
            if (((((uint8_t *)&t412))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t412) + 1, gm_srcp2qdr7uqsmui, 128, 13);
            gm_b0_1 t413 = GM_LD(gm_b0_1, (((uint8_t *)&t412) + 1));
        }
        goto me9;
mn398:;
ma29:;
        uint8_t t415 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t415 != ((uint8_t)19ULL)) goto mn414;
        uint32_t t416 = GM_LD(uint32_t, (((uint8_t *)&t8) + 8));
        gm_l32 = t416;
        void * t417 = gm_l2;
        void * t418 = gm_l0;
        void * t419 = GM_LD(void *, (((uint8_t *)t418) + 0));
        uint32_t t420 = gm_l32;
        gm_b16_8 t421 = gm_f_param_name_rbnop5l(t419, t420);
        gm_tftb1odc739t5 t422 = gm_f_extend_0cs85gs(t417, t421);
        gm_tftb1odc739t5 t423 = t422;
        if (((((uint8_t *)&t423))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t423) + 1, gm_srcp2qdr7uqsmui, 130, 23);
        gm_b0_1 t424 = GM_LD(gm_b0_1, (((uint8_t *)&t423) + 1));
        goto me9;
mn414:;
ma30:;
        uint8_t t426 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t426 != ((uint8_t)20ULL)) goto mn425;
        gm_t88rcucg1b7uk t427 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 0));
        gm_l33 = t427;
        gm_t88rcucg1b7uk t428 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 4));
        gm_l34 = t428;
        {
            void * t430 = gm_l2;
            gm_tftb1odc739t5 t431 = gm_f_push_t83rg2m(t430, ((uint8_t)91ULL));
            gm_tftb1odc739t5 t432 = t431;
            if (((((uint8_t *)&t432))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t432) + 1, gm_srcp2qdr7uqsmui, 132, 13);
            gm_b0_1 t433 = GM_LD(gm_b0_1, (((uint8_t *)&t432) + 1));
        }
        {
            void * t434 = gm_l0;
            gm_t88rcucg1b7uk t435 = gm_l33;
            gm_b0_1 t436 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t434, t435);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t437 = gm_l2;
            gm_tftb1odc739t5 t438 = gm_f_push_t83rg2m(t437, ((uint8_t)93ULL));
            gm_tftb1odc739t5 t439 = t438;
            if (((((uint8_t *)&t439))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t439) + 1, gm_srcp2qdr7uqsmui, 134, 13);
            gm_b0_1 t440 = GM_LD(gm_b0_1, (((uint8_t *)&t439) + 1));
        }
        {
            void * t441 = gm_l0;
            gm_t88rcucg1b7uk t442 = gm_l34;
            gm_b0_1 t443 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t441, t442);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto me9;
mn425:;
ma31:;
        uint8_t t445 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t445 != ((uint8_t)22ULL)) goto mn444;
        gm_t9qkh0rsooc8d t446 = GM_LD(gm_t9qkh0rsooc8d, ((((uint8_t *)&t8) + 8) + 0));
        gm_l35 = t446;
        gm_t88rcucg1b7uk t447 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 4));
        gm_l36 = t447;
        gm_t88rcucg1b7uk t448 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 8));
        gm_l37 = t448;
        {
        }
        {
            {
                {
                    gm_t88rcucg1b7uk t452 = gm_l36;
                    gm_t88rcucg1b7uk t453 = gm_l37;
                    gm_tqsk8dt6ib3qq t454;
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t454 + 0), t452);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t454 + 4), t453);
                    gm_l38 = t454;
                }
                void * t455 = ((uint8_t *)&gm_l38);
                if ((uintptr_t)0 > (uintptr_t)2 || (uintptr_t)2 > (uintptr_t)2) gm_trap_slice((uintptr_t)0, (uintptr_t)2, 2, gm_srcp2qdr7uqsmui, 138, 35);
                gm_b16_8 t456;
                GM_ST(void *, (uint8_t *)&t456, (void *)(((uint8_t *)t455 + (uintptr_t)0 * 4)));
                GM_ST(uintptr_t, (uint8_t *)&t456 + 8, (uintptr_t)((uintptr_t)2 - (uintptr_t)0));
                gm_b16_8 t457 = t456;
                gm_l39 = t457;
            }
            {
                gm_l40 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t459 = gm_l40;
                    uint64_t t460 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                    uint8_t t461 = (t459 < t460);
                    if (!t461) goto lb458;
                    {
                        uint64_t t464 = gm_l40;
                        uint64_t t465 = gm_l40;
                        uint64_t t466 = (uintptr_t)t465;
                        uint64_t t467 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                        gm_t88rcucg1b7uk t468 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l39)) + t466 * 4));
                        gm_tlcm4lvu3g1a8 t469;
                        GM_ST(uint32_t, (uint8_t *)&t469 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t469 + 0), t464);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t469 + 8), t468);
                        gm_tlcm4lvu3g1a8 t470 = t469;
                        uint64_t t473 = GM_LD(uint64_t, (((uint8_t *)&t470) + 0));
                        gm_l41 = t473;
                        gm_t88rcucg1b7uk t474 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t470) + 8));
                        gm_l42 = t474;
                        {
                            uint64_t t477 = gm_l41;
                            uint8_t t478 = ((t477 == ((uint64_t)1ULL)));
                            if (!t478) goto ie476;
                            {
                                void * t480 = gm_l2;
                                gm_t9qkh0rsooc8d t481 = gm_l35;
                                gm_b16_8 t482 = gm_f_len_op_symbol_2qoiciu(t481);
                                gm_tnpifv20irnph t483;
                                GM_ST(gm_b16_8, ((uint8_t *)&t483 + 0), t482);
                                gm_tftb1odc739t5 t484 = gm_f_write_m28h1lv(t480, t483);
                                gm_tftb1odc739t5 t485 = t484;
                                if (((((uint8_t *)&t485))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t485) + 1, gm_srcp2qdr7uqsmui, 139, 29);
                                gm_b0_1 t486 = GM_LD(gm_b0_1, (((uint8_t *)&t485) + 1));
                            }
                            goto ix476;
ie476:;
ix476:;
                        }
                        {
                            void * t488 = gm_l0;
                            void * t489 = GM_LD(void *, (((uint8_t *)t488) + 0));
                            gm_t88rcucg1b7uk t490 = gm_l42;
                            gm_t53at80pfdus3 t491;
                            gm_f_kind_jcnt4hm((void *)&t491, t489, GM_CONV(gm_csguankko4lmhd, t490));
                            gm_t53at80pfdus3 t492 = t491;
                            uint8_t t493;
                            uint8_t t496 = GM_LD(uint8_t, (((uint8_t *)&t492) + 0));
                            if (t496 != ((uint8_t)22ULL)) goto mn495;
                            t493 = ((uint8_t)1);
                            goto me494;
mn495:;
                            t493 = ((uint8_t)0);
                            goto me494;
mn497:;
                            gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 140, 20);
me494:;
                            if (!t493) goto ie487;
                            {
                                void * t499 = gm_l2;
                                gm_tftb1odc739t5 t500 = gm_f_push_t83rg2m(t499, ((uint8_t)40ULL));
                                gm_tftb1odc739t5 t501 = t500;
                                if (((((uint8_t *)&t501))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t501) + 1, gm_srcp2qdr7uqsmui, 141, 21);
                                gm_b0_1 t502 = GM_LD(gm_b0_1, (((uint8_t *)&t501) + 1));
                            }
                            {
                                void * t503 = gm_l0;
                                gm_t88rcucg1b7uk t504 = gm_l42;
                                gm_b0_1 t505 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t503, t504);
                                if (gm_unw.kind) {
                                    goto gm_ll462;
                                }
                            }
                            {
                                void * t506 = gm_l2;
                                gm_tftb1odc739t5 t507 = gm_f_push_t83rg2m(t506, ((uint8_t)41ULL));
                                gm_tftb1odc739t5 t508 = t507;
                                if (((((uint8_t *)&t508))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t508) + 1, gm_srcp2qdr7uqsmui, 143, 21);
                                gm_b0_1 t509 = GM_LD(gm_b0_1, (((uint8_t *)&t508) + 1));
                            }
                            goto ix487;
ie487:;
                            {
                                void * t511 = gm_l0;
                                gm_t88rcucg1b7uk t512 = gm_l42;
                                gm_b0_1 t513 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t511, t512);
                                if (gm_unw.kind) {
                                    goto gm_ll462;
                                }
                            }
ix487:;
                        }
                        goto me471;
mn472:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 138, 13);
me471:;
                    }
lc458:;
                    {
                        uint64_t t515 = gm_l40;
                        uint64_t r516; bool o517 = __builtin_add_overflow(t515, ((uint64_t)1ULL), &r516);
                        uint64_t t518 = r516;
                        gm_l40 = t518;
                    }
                }
gm_ll462:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb458; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc458; }
                }
                goto gm_lad;
lb458:;
            }
        }
        goto me9;
mn444:;
ma32:;
        uint8_t t520 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t520 != ((uint8_t)21ULL)) goto mn519;
        gm_t428ue2u3hf5g t521 = GM_LD(gm_t428ue2u3hf5g, (((uint8_t *)&t8) + 8));
        gm_l43 = t521;
        void * t522 = gm_l0;
        void * t523 = (void *)((uint8_t *)&gm_l43);
        gm_b0_1 t524 = gm_f_key_fr74f0n((void *)NULL, cx, NULL, t522, t523);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me9;
mn519:;
ma33:;
        uint8_t t526 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t526 != ((uint8_t)24ULL)) goto mn525;
        gm_tnpifv20irnph t527 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&t8) + 8));
        gm_l44 = t527;
        void * t528 = gm_l0;
        void * t529 = (void *)((uint8_t *)&gm_l44);
        gm_b0_1 t530 = gm_f_effects_mg3vf2a((void *)NULL, cx, NULL, t528, t529);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me9;
mn525:;
ma34:;
        uint8_t t532 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t532 != ((uint8_t)25ULL)) goto mn531;
        void * t533 = gm_l2;
        gm_b16_8 t534;
        GM_ST(void *, (uint8_t *)&t534, (void *)(gm_sic5vsema0r36));
        GM_ST(uintptr_t, (uint8_t *)&t534 + 8, (uintptr_t)(7));
        gm_tftb1odc739t5 t535 = gm_f_extend_0cs85gs(t533, t534);
        gm_tftb1odc739t5 t536 = t535;
        if (((((uint8_t *)&t536))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t536) + 1, gm_srcp2qdr7uqsmui, 151, 24);
        gm_b0_1 t537 = GM_LD(gm_b0_1, (((uint8_t *)&t536) + 1));
        goto me9;
mn531:;
ma35:;
        uint8_t t539 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t539 != ((uint8_t)23ULL)) goto mn538;
        void * t540 = gm_l2;
        gm_b16_8 t541;
        GM_ST(void *, (uint8_t *)&t541, (void *)(gm_s4imhc4v1tf2p));
        GM_ST(uintptr_t, (uint8_t *)&t541 + 8, (uintptr_t)(9));
        gm_tftb1odc739t5 t542 = gm_f_extend_0cs85gs(t540, t541);
        gm_tftb1odc739t5 t543 = t542;
        if (((((uint8_t *)&t543))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t543) + 1, gm_srcp2qdr7uqsmui, 152, 22);
        gm_b0_1 t544 = GM_LD(gm_b0_1, (((uint8_t *)&t543) + 1));
        goto me9;
mn538:;
ma36:;
        uint8_t t546 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t546 != ((uint8_t)26ULL)) goto mn545;
        void * t547 = gm_l2;
        gm_b16_8 t548;
        GM_ST(void *, (uint8_t *)&t548, (void *)(gm_scp1b2sk2nlmr));
        GM_ST(uintptr_t, (uint8_t *)&t548 + 8, (uintptr_t)(7));
        gm_tftb1odc739t5 t549 = gm_f_extend_0cs85gs(t547, t548);
        gm_tftb1odc739t5 t550 = t549;
        if (((((uint8_t *)&t550))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t550) + 1, gm_srcp2qdr7uqsmui, 153, 19);
        gm_b0_1 t551 = GM_LD(gm_b0_1, (((uint8_t *)&t550) + 1));
        goto me9;
mn545:;
ma37:;
        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 55, 5);
me9:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_effect_ref_6sjtehp(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_effect_ref_6sjtehp F;
    void * gm_l0; /* p */
    void * gm_l1; /* e */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK(&F, 188u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint32_t t4 = GM_LD(uint32_t, (((uint8_t *)t3) + 0));
        uint8_t t5 = ((t4 == ((uint32_t)0ULL)));
        if (!t5) goto ie2;
        void * t6 = gm_l1;
        uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)t6) + 8) + 8);
        uint8_t t8 = (t7 > ((uint64_t)0ULL));
        if (!t8) goto ie2;
        {
            uint64_t t11 = (uintptr_t)((uint64_t)0ULL);
            void * t12 = gm_l1;
            uint64_t t13 = GM_LD(uintptr_t, (((uint8_t *)t12) + 8) + 8);
            if (t11 >= (uintptr_t)(t13)) gm_trap_index(t11, t13, gm_srcp2qdr7uqsmui, 190, 24);
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 8)) + t11 * 40) + 0));
            if (t14 != ((uint8_t)0ULL)) goto ie10;
            gm_t88rcucg1b7uk t15 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, (((uint8_t *)t12) + 8)) + t11 * 40) + 8));
            gm_l2 = t15;
            {
                void * t17 = gm_l0;
                gm_t88rcucg1b7uk t18 = gm_l2;
                gm_b0_1 t19 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t17, t18);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                gm_b0_1 t20;
                uint8_t t21 = 0;
                F.ret = t20;
                goto gm_fret;
            }
            goto ix10;
ie10:;
ix10:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t22 = gm_l0;
        void * t23 = GM_LD(void *, (((uint8_t *)t22) + 8));
        void * t24 = gm_l0;
        void * t25 = GM_LD(void *, (((uint8_t *)t24) + 0));
        void * t26 = gm_l1;
        uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t26) + 0));
        gm_b16_8 t28 = gm_f_effect_name_prpp0ip(t25, t27);
        gm_tftb1odc739t5 t29 = gm_f_extend_0cs85gs(t23, t28);
        gm_tftb1odc739t5 t30 = t29;
        if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcp2qdr7uqsmui, 195, 5);
        gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
    }
    {
        void * t32 = gm_l0;
        void * t33 = gm_l1;
        gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)t33) + 8));
        gm_b0_1 t35 = gm_f_args_881ap90((void *)NULL, cx, NULL, t32, t34);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_effects_mg3vf2a(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_effects_mg3vf2a F;
    void * gm_l0; /* p */
    void * gm_l1; /* set */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    gm_tiilmfd16v3fc gm_l5; /* e */
    GM_STACK_CHECK(&F, 372u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t3 = gm_l1;
        uint64_t t4 = GM_LD(uintptr_t, (((uint8_t *)t3) + 0) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) goto ie2;
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, (((uint8_t *)t7) + 8));
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sbvutvpa8pe3d));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t10 = gm_f_extend_0cs85gs(t8, t9);
            gm_tftb1odc739t5 t11 = t10;
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srcp2qdr7uqsmui, 179, 9);
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
        }
        {
            gm_b0_1 t13;
            uint8_t t14 = 0;
            F.ret = t13;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            void * t16 = gm_l1;
            gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t16) + 0));
            gm_l2 = t17;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t19 = gm_l3;
                uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t21 = (t19 < t20);
                if (!t21) goto lb18;
                {
                    gm_t4f5fr60fo50r t24;
                    uint64_t t25 = gm_l3;
                    GM_ST(uint64_t, (uint8_t *)&t24 + 0, t25);
                    uint64_t t26 = gm_l3;
                    uint64_t t27 = (uintptr_t)t26;
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    memcpy((uint8_t *)&t24 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t27 * 24), 24);
                    gm_t4f5fr60fo50r t29 = t24;
                    uint64_t t32 = GM_LD(uint64_t, (((uint8_t *)&t29) + 0));
                    gm_l4 = t32;
                    gm_tiilmfd16v3fc t33 = GM_LD(gm_tiilmfd16v3fc, (((uint8_t *)&t29) + 8));
                    gm_l5 = t33;
                    {
                        uint64_t t36 = gm_l4;
                        uint8_t t37 = (t36 > ((uint64_t)0ULL));
                        if (!t37) goto ie35;
                        {
                            void * t39 = gm_l0;
                            void * t40 = GM_LD(void *, (((uint8_t *)t39) + 8));
                            gm_b16_8 t41;
                            GM_ST(void *, (uint8_t *)&t41, (void *)(gm_s0m0qaokk51g0));
                            GM_ST(uintptr_t, (uint8_t *)&t41 + 8, (uintptr_t)(3));
                            gm_tftb1odc739t5 t42 = gm_f_extend_0cs85gs(t40, t41);
                            gm_tftb1odc739t5 t43 = t42;
                            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcp2qdr7uqsmui, 183, 20);
                            gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
                        }
                        goto ix35;
ie35:;
ix35:;
                    }
                    {
                        void * t45 = gm_l0;
                        void * t46 = (void *)((uint8_t *)&gm_l5);
                        gm_b0_1 t47 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t45, t46);
                        if (gm_unw.kind) {
                            goto gm_ll22;
                        }
                    }
                    goto me30;
mn31:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 182, 5);
me30:;
                }
lc18:;
                {
                    uint64_t t49 = gm_l3;
                    uint64_t r50; bool o51 = __builtin_add_overflow(t49, ((uint64_t)1ULL), &r50);
                    uint64_t t52 = r50;
                    gm_l3 = t52;
                }
            }
gm_ll22:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb18; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc18; }
            }
            goto gm_lad;
lb18:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_empty_set_3vntabf(void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_empty_set_3vntabf F;
    void * gm_l0; /* p */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tnpifv20irnph gm_l3; /* set */
    GM_STACK_CHECK(&F, 196u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t3 != ((uint8_t)0ULL)) goto ie2;
        gm_t88rcucg1b7uk t4 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)a1) + 8));
        gm_l2 = t4;
        {
            void * t7 = gm_l0;
            void * t8 = GM_LD(void *, (((uint8_t *)t7) + 0));
            gm_t88rcucg1b7uk t9 = gm_l2;
            gm_t53at80pfdus3 t10;
            gm_f_kind_jcnt4hm((void *)&t10, t8, GM_CONV(gm_csguankko4lmhd, t9));
            gm_t53at80pfdus3 t11 = t10;
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t11) + 0));
            if (t12 != ((uint8_t)24ULL)) goto ie6;
            gm_tnpifv20irnph t13 = GM_LD(gm_tnpifv20irnph, (((uint8_t *)&t11) + 8));
            gm_l3 = t13;
            {
                uint64_t t15 = GM_LD(uintptr_t, (((uint8_t *)&gm_l3) + 0) + 8);
                uint8_t t16 = ((t15 == ((uint64_t)0ULL)));
                uint8_t t17 = 0;
                F.ret = t16;
                goto gm_fret;
            }
            goto ix6;
ie6:;
            {
                uint8_t t19 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
ix6:;
        }
        goto ix2;
ie2:;
        {
            uint8_t t21 = 0;
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
__attribute__((aligned(4))) uint8_t gm_f_is_empty_set_3vntabf_c(void * a0, gm_toa2vhe3l4hfr a1) { return gm_f_is_empty_set_3vntabf(a0, &a1); }
__attribute__((aligned(4))) gm_b0_1 gm_f_key_fr74f0n(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_key_fr74f0n F;
    void * gm_l0; /* p */
    void * gm_l1; /* k */
    void * gm_l2; /* out */
    gm_b16_8 gm_l3; /* name */
    gm_b16_8 gm_l4; /* s */
    gm_t88rcucg1b7uk gm_l5; /* t */
    gm_b16_8 gm_l6; /* b */
    uint8_t gm_l7; /* signed */
    uint16_t gm_l8; /* bits */
    uint64_t gm_l9; /* raw */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    uint8_t gm_l13; /* c */
    GM_STACK_CHECK(&F, 966u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
        gm_l2 = t3;
    }
    {
        void * t4 = gm_l1;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        switch (t11) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma7; case ((uint8_t)3ULL): goto ma8; default: goto ma10; }
ma6:;
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        if (t13 != ((uint8_t)1ULL)) goto mn12;
        gm_b16_8 t14 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l3 = t14;
        void * t15 = gm_l2;
        gm_b16_8 t16 = gm_l3;
        gm_tftb1odc739t5 t17 = gm_f_extend_0cs85gs(t15, t16);
        gm_tftb1odc739t5 t18 = t17;
        if (((((uint8_t *)&t18))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t18) + 1, gm_srcp2qdr7uqsmui, 225, 25);
        gm_b0_1 t19 = GM_LD(gm_b0_1, (((uint8_t *)&t18) + 1));
        goto me5;
mn12:;
ma7:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        if (t21 != ((uint8_t)2ULL)) goto mn20;
        void * t22 = gm_l2;
        gm_b16_8 t23;
        GM_ST(void *, (uint8_t *)&t23, (void *)(gm_squedv51820an));
        GM_ST(uintptr_t, (uint8_t *)&t23 + 8, (uintptr_t)(4));
        gm_tftb1odc739t5 t24 = gm_f_extend_0cs85gs(t22, t23);
        gm_tftb1odc739t5 t25 = t24;
        if (((((uint8_t *)&t25))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t25) + 1, gm_srcp2qdr7uqsmui, 226, 18);
        gm_b0_1 t26 = GM_LD(gm_b0_1, (((uint8_t *)&t25) + 1));
        goto me5;
mn20:;
ma8:;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        if (t28 != ((uint8_t)3ULL)) goto mn27;
        gm_b16_8 t29 = GM_LD(gm_b16_8, (((uint8_t *)t4) + 8));
        gm_l4 = t29;
        gm_b16_8 t30 = gm_l4;
        void * t31 = gm_l2;
        gm_b0_1 t32 = gm_f_write_debug_string_pns0400((void *)NULL, cx, NULL, t30, t31);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto me5;
mn27:;
ma9:;
        uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)t4) + 0));
        if (t34 != ((uint8_t)0ULL)) goto mn33;
        gm_t88rcucg1b7uk t35 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)t4) + 8) + 0));
        gm_l5 = t35;
        gm_b16_8 t36 = GM_LD(gm_b16_8, ((((uint8_t *)t4) + 8) + 8));
        gm_l6 = t36;
        {
            void * t38 = gm_l0;
            void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
            gm_t88rcucg1b7uk t40 = gm_l5;
            gm_t53at80pfdus3 t41;
            gm_f_kind_jcnt4hm((void *)&t41, t39, GM_CONV(gm_csguankko4lmhd, t40));
            gm_t53at80pfdus3 t42 = t41;
            uint8_t t51 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
            switch (t51) { case ((uint8_t)0ULL): goto ma44; case ((uint8_t)1ULL): goto ma45; case ((uint8_t)2ULL): goto ma46; case ((uint8_t)3ULL): goto ma48; case ((uint8_t)4ULL): goto ma47; case ((uint8_t)5ULL): goto ma49; case ((uint8_t)6ULL): goto ma49; case ((uint8_t)7ULL): goto ma49; case ((uint8_t)8ULL): goto ma49; case ((uint8_t)9ULL): goto ma49; case ((uint8_t)10ULL): goto ma49; case ((uint8_t)11ULL): goto ma49; case ((uint8_t)12ULL): goto ma49; case ((uint8_t)13ULL): goto ma49; case ((uint8_t)14ULL): goto ma49; case ((uint8_t)15ULL): goto ma49; case ((uint8_t)16ULL): goto ma49; case ((uint8_t)17ULL): goto ma49; case ((uint8_t)18ULL): goto ma49; case ((uint8_t)19ULL): goto ma49; case ((uint8_t)20ULL): goto ma49; case ((uint8_t)21ULL): goto ma49; case ((uint8_t)22ULL): goto ma49; case ((uint8_t)23ULL): goto ma49; case ((uint8_t)24ULL): goto ma49; case ((uint8_t)25ULL): goto ma49; case ((uint8_t)26ULL): goto ma49; default: goto ma49; }
ma44:;
            uint8_t t53 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
            if (t53 != ((uint8_t)0ULL)) goto mn52;
            uint8_t t54 = GM_LD(uint8_t, ((((uint8_t *)&t42) + 8) + 0));
            gm_l7 = t54;
            gm_b16_8 t55 = gm_l6;
            uint8_t t56 = gm_l7;
            void * t57 = gm_l2;
            gm_b0_1 t58 = gm_f_write_int_lq1ktof((void *)NULL, cx, NULL, t55, t56, t57);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me43;
mn52:;
ma45:;
            uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
            if (t60 != ((uint8_t)1ULL)) goto mn59;
            gm_b16_8 t61 = gm_l6;
            void * t62 = gm_l2;
            gm_b0_1 t63 = gm_f_write_int_lq1ktof((void *)NULL, cx, NULL, t61, ((uint8_t)0), t62);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me43;
mn59:;
ma46:;
            uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
            if (t65 != ((uint8_t)2ULL)) goto mn64;
            gm_b16_8 t66 = gm_l6;
            void * t67 = gm_l2;
            gm_b0_1 t68 = gm_f_write_int_lq1ktof((void *)NULL, cx, NULL, t66, ((uint8_t)1), t67);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto me43;
mn64:;
ma47:;
            uint8_t t70 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
            if (t70 != ((uint8_t)4ULL)) goto mn69;
            void * t71 = gm_l2;
            gm_b16_8 t72;
            uint64_t t74 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            uint8_t t75 = (t74 > ((uint64_t)0ULL));
            if (!t75) goto ie73;
            uint64_t t76 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t77 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
            if (t76 >= (uintptr_t)(t77)) gm_trap_index(t76, t77, gm_srcp2qdr7uqsmui, 233, 54);
            uint8_t t78 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l6)) + t76 * 1));
            uint8_t t79 = ((t78 == ((uint8_t)1ULL)));
            if (!t79) goto ie73;
            gm_b16_8 t81;
            GM_ST(void *, (uint8_t *)&t81, (void *)(gm_sehko5bb96s6p));
            GM_ST(uintptr_t, (uint8_t *)&t81 + 8, (uintptr_t)(4));
            gm_b16_8 t82 = t81;
            t72 = t82;
            goto ix73;
ie73:;
            gm_b16_8 t84;
            GM_ST(void *, (uint8_t *)&t84, (void *)(gm_sogehh8t1m7tn));
            GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(5));
            gm_b16_8 t85 = t84;
            t72 = t85;
ix73:;
            gm_tftb1odc739t5 t86 = gm_f_extend_0cs85gs(t71, t72);
            gm_tftb1odc739t5 t87 = t86;
            if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_srcp2qdr7uqsmui, 233, 26);
            gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
            goto me43;
mn69:;
ma48:;
            uint8_t t90 = GM_LD(uint8_t, (((uint8_t *)&t42) + 0));
            if (t90 != ((uint8_t)3ULL)) goto mn89;
            uint16_t t91 = GM_LD(uint16_t, (((uint8_t *)&t42) + 8));
            gm_l8 = t91;
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                {
                    gm_b16_8 t94 = gm_l6;
                    gm_l10 = t94;
                }
                {
                    gm_l11 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t96 = gm_l11;
                        uint64_t t97 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        uint8_t t98 = (t96 < t97);
                        if (!t98) goto lb95;
                        {
                            uint64_t t101 = gm_l11;
                            uint64_t t102 = gm_l11;
                            uint64_t t103 = (uintptr_t)t102;
                            uint64_t t104 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                            uint8_t t105 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t103 * 1));
                            gm_tf5srh4mp4b3i t106;
                            GM_ST(uint8_t, (uint8_t *)&t106 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t106 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t106 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t106 + 0), t101);
                            GM_ST(uint8_t, ((uint8_t *)&t106 + 8), t105);
                            gm_tf5srh4mp4b3i t107 = t106;
                            uint64_t t110 = GM_LD(uint64_t, (((uint8_t *)&t107) + 0));
                            gm_l12 = t110;
                            uint8_t t111 = GM_LD(uint8_t, (((uint8_t *)&t107) + 8));
                            gm_l13 = t111;
                            {
                                uint64_t t113 = gm_l9;
                                uint8_t t114 = gm_l13;
                                uint64_t t115 = (uint64_t)t114;
                                uint64_t t116 = gm_l12;
                                uint64_t r117; bool o118 = __builtin_mul_overflow(t116, ((uint64_t)8ULL), &r117);
                                if (o118) gm_trap("integer overflow in `*`", gm_srcp2qdr7uqsmui, 236, 69);
                                uint64_t t119 = r117;
                                uint64_t t120 = (uint64_t)t119;
                                unsigned __int128 n121 = (unsigned __int128)t120;
                                if (n121 >= 64) gm_trap_shift((uint64_t)n121, "u64", gm_srcp2qdr7uqsmui, 236, 55);
                                uint64_t r122 = (uint64_t)((uint64_t)t115 << (int)n121);
                                bool o123 = (uint64_t)(r122 >> (int)n121) != t115;
                                if (o123) gm_trap("integer overflow in `<<`", gm_srcp2qdr7uqsmui, 236, 55);
                                uint64_t t124 = r122;
                                uint64_t t125 = (uint64_t)(t113 | t124);
                                gm_l9 = t125;
                            }
                            goto me108;
mn109:;
                            gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 236, 21);
me108:;
                        }
lc95:;
                        {
                            uint64_t t127 = gm_l11;
                            uint64_t r128; bool o129 = __builtin_add_overflow(t127, ((uint64_t)1ULL), &r128);
                            uint64_t t130 = r128;
                            gm_l11 = t130;
                        }
                    }
lb95:;
                }
            }
            {
                uint16_t t132 = gm_l8;
                uint8_t t133 = ((t132 == ((uint16_t)64ULL)));
                if (!t133) goto ie131;
                uint64_t t134 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t135 = ((t134 == ((uint64_t)8ULL)));
                if (!t135) goto ie131;
                {
                    uint64_t t137 = gm_l9;
                    uint64_t t138 = t137;
                    double t139;
                    memset(&t139, 0, sizeof t139); memcpy(&t139, ((uint8_t *)&t138), sizeof t139 < 8 ? sizeof t139 : 8);
                    void * t140 = gm_l2;
                    gm_b0_1 t141 = gm_f_write_float_7p0icl6((void *)NULL, cx, NULL, t139, t140);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix131;
ie131:;
                uint16_t t143 = gm_l8;
                uint8_t t144 = ((t143 == ((uint16_t)32ULL)));
                if (!t144) goto ie142;
                uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&gm_l6) + 8);
                uint8_t t146 = ((t145 == ((uint64_t)4ULL)));
                if (!t146) goto ie142;
                {
                    uint64_t t148 = gm_l9;
                    uint32_t t149 = (uint32_t)t148;
                    uint32_t t150 = t149;
                    float t151;
                    memset(&t151, 0, sizeof t151); memcpy(&t151, ((uint8_t *)&t150), sizeof t151 < 4 ? sizeof t151 : 4);
                    double t152 = (double)t151;
                    void * t153 = gm_l2;
                    gm_b0_1 t154 = gm_f_write_float_7p0icl6((void *)NULL, cx, NULL, t152, t153);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix142;
ie142:;
                {
                    void * t156 = gm_l0;
                    gm_t88rcucg1b7uk t157 = gm_l5;
                    gm_b0_1 t158 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t156, t157);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t159 = gm_l2;
                    gm_b16_8 t160;
                    GM_ST(void *, (uint8_t *)&t160, (void *)(gm_s15tjb1oda5hu));
                    GM_ST(uintptr_t, (uint8_t *)&t160 + 8, (uintptr_t)(5));
                    gm_tftb1odc739t5 t161 = gm_f_extend_0cs85gs(t159, t160);
                    gm_tftb1odc739t5 t162 = t161;
                    if (((((uint8_t *)&t162))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t162) + 1, gm_srcp2qdr7uqsmui, 243, 25);
                    gm_b0_1 t163 = GM_LD(gm_b0_1, (((uint8_t *)&t162) + 1));
                }
ix142:;
ix131:;
            }
            goto me43;
mn89:;
ma49:;
            {
                void * t166 = gm_l0;
                gm_t88rcucg1b7uk t167 = gm_l5;
                gm_b0_1 t168 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t166, t167);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t169 = gm_l2;
                gm_b16_8 t170;
                GM_ST(void *, (uint8_t *)&t170, (void *)(gm_s15tjb1oda5hu));
                GM_ST(uintptr_t, (uint8_t *)&t170 + 8, (uintptr_t)(5));
                gm_tftb1odc739t5 t171 = gm_f_extend_0cs85gs(t169, t170);
                gm_tftb1odc739t5 t172 = t171;
                if (((((uint8_t *)&t172))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t172) + 1, gm_srcp2qdr7uqsmui, 248, 21);
                gm_b0_1 t173 = GM_LD(gm_b0_1, (((uint8_t *)&t172) + 1));
            }
            goto me43;
mn164:;
ma50:;
            gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 229, 13);
me43:;
        }
        goto me5;
mn33:;
ma10:;
        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 224, 5);
me5:;
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
inline __attribute__((aligned(4))) int64_t gm_f_parse_exponent_4uekfcv(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_parse_exponent_4uekfcv F;
    gm_b16_8 gm_l0; /* t */
    uint8_t gm_l1; /* negative */
    int64_t gm_l2; /* v */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint8_t gm_l5; /* c */
    GM_STACK_CHECK(&F, 258u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t2 = 0;
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = (t3 > ((uint64_t)0ULL));
        if (t4) {
            uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t5 >= (uintptr_t)(t6)) gm_trap_index(t5, t6, gm_srcp2qdr7uqsmui, 303, 31);
            uint8_t t7 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t5 * 1));
            uint8_t t8 = ((t7 == ((uint8_t)45ULL)));
            t2 = t8;
        } else t2 = 0;
        gm_l1 = t2;
    }
    {
        gm_l2 = ((int64_t)0LL);
    }
    {
        {
            gm_b16_8 t10 = gm_l0;
            gm_l3 = t10;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t12 = gm_l4;
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t14 = (t12 < t13);
                if (!t14) goto lb11;
                {
                    uint64_t t17 = gm_l4;
                    uint64_t t18 = (uintptr_t)t17;
                    uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t20 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t18 * 1));
                    uint8_t t21 = t20;
                    uint8_t t24 = GM_LD(uint8_t, ((uint8_t *)&t21));
                    gm_l5 = t24;
                    {
                        uint8_t t27 = gm_l5;
                        uint8_t t28 = (t27 >= ((uint8_t)48ULL));
                        if (!t28) goto ie26;
                        uint8_t t29 = gm_l5;
                        uint8_t t30 = (t29 <= ((uint8_t)57ULL));
                        if (!t30) goto ie26;
                        {
                            int64_t t32 = gm_l2;
                            int64_t r33; bool o34 = __builtin_mul_overflow(t32, ((int64_t)10LL), &r33);
                            if (o34) gm_trap("integer overflow in `*`", gm_srcp2qdr7uqsmui, 306, 40);
                            int64_t t35 = r33;
                            uint8_t t36 = gm_l5;
                            uint8_t r37; bool o38 = __builtin_sub_overflow(t36, ((uint8_t)48ULL), &r37);
                            if (o38) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 306, 53);
                            uint8_t t39 = r37;
                            int64_t t40 = (int64_t)t39;
                            int64_t r41; bool o42 = __builtin_add_overflow(t35, t40, &r41);
                            if (o42) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 306, 40);
                            int64_t t43 = r41;
                            gm_l2 = t43;
                        }
                        goto ix26;
ie26:;
ix26:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 305, 5);
me22:;
                }
lc11:;
                {
                    uint64_t t45 = gm_l4;
                    uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                    uint64_t t48 = r46;
                    gm_l4 = t48;
                }
            }
lb11:;
        }
    }
    {
        int64_t t49;
        uint8_t t51 = gm_l1;
        if (!t51) goto ie50;
        int64_t t53 = gm_l2;
        if (t53 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 308, 26);
        int64_t t54 = (int64_t)((uint64_t)0 - (uint64_t)t53);
        int64_t t55 = t54;
        t49 = t55;
        goto ix50;
ie50:;
        int64_t t57 = gm_l2;
        int64_t t58 = t57;
        t49 = t58;
ix50:;
        uint8_t t59 = 0;
        F.ret = t49;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_printer_h7jigd7 F;
    void * gm_l0; /* t */
    void * gm_l1; /* out */
    GM_STACK_CHECK(&F, 32u);
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
__attribute__((aligned(4))) gm_cs32e03j420uj1 gm_f_printer_h7jigd7_c(void * a0, void * a1) { gm_tmm67r2c94d5j gm_r; gm_f_printer_h7jigd7(&gm_r, a0, a1); return GM_CONV(gm_cs32e03j420uj1, gm_r); }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_effects_vieja9u(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_effects_vieja9u F;
    void * gm_l0; /* t */
    void * gm_l1; /* set */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 153u);
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
        void * t5 = gm_l1;
        gm_b0_1 t6 = gm_f_effects_mg3vf2a((void *)NULL, cx, NULL, t4, t5);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_ref_aqt1t7c(void *env, gm_node *cx, gm_node *inst, void * a0, void * a1) {
    struct gm_fr_show_ref_aqt1t7c F;
    void * gm_l0; /* t */
    void * gm_l1; /* e */
    gm_tmm67r2c94d5j gm_l3; /* p */
    GM_STACK_CHECK(&F, 153u);
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
        void * t5 = gm_l1;
        gm_b0_1 t6 = gm_f_effect_ref_6sjtehp((void *)NULL, cx, NULL, t4, t5);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_sig_uguln8a(void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, void * a2) {
    struct gm_fr_sig_uguln8a F;
    void * gm_l0; /* p */
    gm_b16_8 gm_l1; /* head */
    void * gm_l2; /* sig */
    void * gm_l3; /* out */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    gm_t88rcucg1b7uk gm_l7; /* t */
    GM_STACK_CHECK(&F, 518u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 8));
        gm_l3 = t3;
    }
    {
        void * t5 = gm_l2;
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)t5) + 40));
        if (!t6) goto ie4;
        {
            void * t8 = gm_l3;
            gm_b16_8 t9;
            GM_ST(void *, (uint8_t *)&t9, (void *)(gm_sb7t8ia5pj1l5));
            GM_ST(uintptr_t, (uint8_t *)&t9 + 8, (uintptr_t)(18));
            gm_tftb1odc739t5 t10 = gm_f_extend_0cs85gs(t8, t9);
            gm_tftb1odc739t5 t11 = t10;
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 1, gm_srcp2qdr7uqsmui, 159, 20);
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t11) + 1));
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        void * t13 = gm_l3;
        gm_b16_8 t14 = gm_l1;
        gm_tnpifv20irnph t15;
        GM_ST(gm_b16_8, ((uint8_t *)&t15 + 0), t14);
        gm_tftb1odc739t5 t16 = gm_f_write_e9824eu(t13, t15);
        gm_tftb1odc739t5 t17 = t16;
        if (((((uint8_t *)&t17))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t17) + 1, gm_srcp2qdr7uqsmui, 160, 5);
        gm_b0_1 t18 = GM_LD(gm_b0_1, (((uint8_t *)&t17) + 1));
    }
    {
        {
            void * t20 = gm_l2;
            gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)t20) + 0));
            gm_l4 = t21;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t23 = gm_l5;
                uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t25 = (t23 < t24);
                if (!t25) goto lb22;
                {
                    uint64_t t28 = gm_l5;
                    uint64_t t29 = gm_l5;
                    uint64_t t30 = (uintptr_t)t29;
                    uint64_t t31 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t30 * 4));
                    gm_tlcm4lvu3g1a8 t33;
                    GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t33 + 0), t28);
                    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t33 + 8), t32);
                    gm_tlcm4lvu3g1a8 t34 = t33;
                    uint64_t t37 = GM_LD(uint64_t, (((uint8_t *)&t34) + 0));
                    gm_l6 = t37;
                    gm_t88rcucg1b7uk t38 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t34) + 8));
                    gm_l7 = t38;
                    {
                        uint64_t t41 = gm_l6;
                        uint8_t t42 = (t41 > ((uint64_t)0ULL));
                        if (!t42) goto ie40;
                        {
                            void * t44 = gm_l3;
                            gm_b16_8 t45;
                            GM_ST(void *, (uint8_t *)&t45, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t45 + 8, (uintptr_t)(2));
                            gm_tftb1odc739t5 t46 = gm_f_extend_0cs85gs(t44, t45);
                            gm_tftb1odc739t5 t47 = t46;
                            if (((((uint8_t *)&t47))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t47) + 1, gm_srcp2qdr7uqsmui, 162, 20);
                            gm_b0_1 t48 = GM_LD(gm_b0_1, (((uint8_t *)&t47) + 1));
                        }
                        goto ix40;
ie40:;
ix40:;
                    }
                    {
                        void * t49 = gm_l0;
                        gm_t88rcucg1b7uk t50 = gm_l7;
                        gm_b0_1 t51 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t49, t50);
                        if (gm_unw.kind) {
                            goto gm_ll26;
                        }
                    }
                    goto me35;
mn36:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 161, 5);
me35:;
                }
lc22:;
                {
                    uint64_t t53 = gm_l5;
                    uint64_t r54; bool o55 = __builtin_add_overflow(t53, ((uint64_t)1ULL), &r54);
                    uint64_t t56 = r54;
                    gm_l5 = t56;
                }
            }
gm_ll26:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb22; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc22; }
            }
            goto gm_lad;
lb22:;
        }
    }
    {
        void * t57 = gm_l3;
        gm_tftb1odc739t5 t58 = gm_f_push_t83rg2m(t57, ((uint8_t)41ULL));
        gm_tftb1odc739t5 t59 = t58;
        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srcp2qdr7uqsmui, 165, 5);
        gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
    }
    {
        void * t62 = gm_l0;
        void * t63 = GM_LD(void *, (((uint8_t *)t62) + 0));
        void * t64 = gm_l2;
        gm_t88rcucg1b7uk t65 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t64) + 16));
        uint8_t t66 = gm_f_is_unit_n83ttnb(t63, GM_CONV(gm_csguankko4lmhd, t65));
        uint8_t t67 = (uint8_t)!t66;
        if (!t67) goto ie61;
        {
            void * t69 = gm_l3;
            gm_b16_8 t70;
            GM_ST(void *, (uint8_t *)&t70, (void *)(gm_sdbvaa960ee0f));
            GM_ST(uintptr_t, (uint8_t *)&t70 + 8, (uintptr_t)(4));
            gm_tftb1odc739t5 t71 = gm_f_extend_0cs85gs(t69, t70);
            gm_tftb1odc739t5 t72 = t71;
            if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 1, gm_srcp2qdr7uqsmui, 167, 9);
            gm_b0_1 t73 = GM_LD(gm_b0_1, (((uint8_t *)&t72) + 1));
        }
        {
            void * t74 = gm_l0;
            void * t75 = gm_l2;
            gm_t88rcucg1b7uk t76 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t75) + 16));
            gm_b0_1 t77 = gm_f_ty_vka2r8h((void *)NULL, cx, NULL, t74, t76);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix61;
ie61:;
ix61:;
    }
    {
        void * t79 = gm_l2;
        uint64_t t80 = GM_LD(uintptr_t, ((((uint8_t *)t79) + 24) + 0) + 8);
        uint8_t t81 = (t80 > ((uint64_t)0ULL));
        if (!t81) goto ie78;
        {
            void * t83 = gm_l3;
            gm_b16_8 t84;
            GM_ST(void *, (uint8_t *)&t84, (void *)(gm_s5j8t1cg2erp9));
            GM_ST(uintptr_t, (uint8_t *)&t84 + 8, (uintptr_t)(7));
            gm_tftb1odc739t5 t85 = gm_f_extend_0cs85gs(t83, t84);
            gm_tftb1odc739t5 t86 = t85;
            if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srcp2qdr7uqsmui, 171, 9);
            gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
        }
        {
            void * t88 = gm_l0;
            void * t89 = gm_l2;
            void * t90 = (void *)(((uint8_t *)t89) + 24);
            gm_b0_1 t91 = gm_f_effects_mg3vf2a((void *)NULL, cx, NULL, t88, t90);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix78;
ie78:;
ix78:;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_ty_vka2r8h F;
    void * gm_l0; /* p */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    uint32_t gm_l2; /* left */
    GM_STACK_CHECK(&F, 171u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint32_t t2;
        void * t4 = gm_l0;
        uint32_t t5 = GM_LD(uint32_t, (((uint8_t *)t4) + 16));
        uint8_t t6 = ((t5 == ((uint32_t)0ULL)));
        if (!t6) goto ie3;
        uint32_t t8 = ((uint32_t)300ULL);
        t2 = t8;
        goto ix3;
ie3:;
        void * t10 = gm_l0;
        uint32_t t11 = GM_LD(uint32_t, (((uint8_t *)t10) + 20));
        uint32_t t12 = t11;
        t2 = t12;
ix3:;
        gm_l2 = t2;
    }
    {
        uint32_t t14 = gm_l2;
        uint8_t t15 = ((t14 == ((uint32_t)0ULL)));
        if (!t15) goto ie13;
        {
            void * t17 = gm_l0;
            void * t18 = GM_LD(void *, (((uint8_t *)t17) + 8));
            gm_b16_8 t19;
            GM_ST(void *, (uint8_t *)&t19, (void *)(gm_soqt3jnmovq5l));
            GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(3));
            gm_tftb1odc739t5 t20 = gm_f_extend_0cs85gs(t18, t19);
            gm_tftb1odc739t5 t21 = t20;
            if (((((uint8_t *)&t21))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t21) + 1, gm_srcp2qdr7uqsmui, 44, 9);
            gm_b0_1 t22 = GM_LD(gm_b0_1, (((uint8_t *)&t21) + 1));
        }
        {
            gm_b0_1 t23;
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix13;
ie13:;
ix13:;
    }
    {
        void * t25 = gm_l0;
        void * t26 = gm_l0;
        uint32_t t27 = GM_LD(uint32_t, (((uint8_t *)t26) + 16));
        uint32_t r28; bool o29 = __builtin_add_overflow(t27, ((uint32_t)1ULL), &r28);
        if (o29) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 47, 5);
        uint32_t t30 = r28;
        GM_ST(uint32_t, (((uint8_t *)t25) + 16), t30);
    }
    {
        void * t31 = gm_l0;
        uint32_t t32 = gm_l2;
        uint32_t r33; bool o34 = __builtin_sub_overflow(t32, ((uint32_t)1ULL), &r33);
        if (o34) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 48, 14);
        uint32_t t35 = r33;
        GM_ST(uint32_t, (((uint8_t *)t31) + 20), t35);
    }
    {
        void * t36 = gm_l0;
        gm_t88rcucg1b7uk t37 = gm_l1;
        gm_b0_1 t38 = gm_f_components_dvsj2j9((void *)NULL, cx, NULL, t36, t37);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t39 = gm_l0;
        void * t40 = gm_l0;
        uint32_t t41 = GM_LD(uint32_t, (((uint8_t *)t40) + 16));
        uint32_t r42; bool o43 = __builtin_sub_overflow(t41, ((uint32_t)1ULL), &r42);
        if (o43) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 50, 5);
        uint32_t t44 = r42;
        GM_ST(uint32_t, (((uint8_t *)t39) + 16), t44);
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_debug_string_pns0400(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, void * a1) {
    struct gm_fr_write_debug_string_pns0400 F;
    gm_b16_8 gm_l0; /* s */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 357u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l1;
        gm_tftb1odc739t5 t3 = gm_f_push_t83rg2m(t2, ((uint8_t)34ULL));
        gm_tftb1odc739t5 t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 1, gm_srcp2qdr7uqsmui, 340, 5);
        gm_b0_1 t5 = GM_LD(gm_b0_1, (((uint8_t *)&t4) + 1));
    }
    {
        {
            gm_b16_8 t7 = gm_l0;
            gm_l2 = t7;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t9 = gm_l3;
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t11 = (t9 < t10);
                if (!t11) goto lb8;
                {
                    uint64_t t14 = gm_l3;
                    uint64_t t15 = (uintptr_t)t14;
                    uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t15 * 1));
                    uint8_t t18 = t17;
                    uint8_t t21 = GM_LD(uint8_t, ((uint8_t *)&t18));
                    gm_l4 = t21;
                    {
                        uint8_t t23 = gm_l4;
                        uint8_t t24 = t23;
                        uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        if (!(t27 == ((uint8_t)34ULL))) goto mn26;
                        void * t28 = gm_l1;
                        gm_b16_8 t29;
                        GM_ST(void *, (uint8_t *)&t29, (void *)(gm_smee1janbl2t9));
                        GM_ST(uintptr_t, (uint8_t *)&t29 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t30 = gm_f_extend_0cs85gs(t28, t29);
                        gm_tftb1odc739t5 t31 = t30;
                        if (((((uint8_t *)&t31))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t31) + 1, gm_srcp2qdr7uqsmui, 343, 20);
                        gm_b0_1 t32 = GM_LD(gm_b0_1, (((uint8_t *)&t31) + 1));
                        goto me25;
mn26:;
                        uint8_t t34 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        if (!(t34 == ((uint8_t)92ULL))) goto mn33;
                        void * t35 = gm_l1;
                        gm_b16_8 t36;
                        GM_ST(void *, (uint8_t *)&t36, (void *)(gm_s3p8gug7bvl1p));
                        GM_ST(uintptr_t, (uint8_t *)&t36 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t37 = gm_f_extend_0cs85gs(t35, t36);
                        gm_tftb1odc739t5 t38 = t37;
                        if (((((uint8_t *)&t38))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t38) + 1, gm_srcp2qdr7uqsmui, 344, 21);
                        gm_b0_1 t39 = GM_LD(gm_b0_1, (((uint8_t *)&t38) + 1));
                        goto me25;
mn33:;
                        uint8_t t41 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        if (!(t41 == ((uint8_t)10ULL))) goto mn40;
                        void * t42 = gm_l1;
                        gm_b16_8 t43;
                        GM_ST(void *, (uint8_t *)&t43, (void *)(gm_s6gt8b7hsisgu));
                        GM_ST(uintptr_t, (uint8_t *)&t43 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t44 = gm_f_extend_0cs85gs(t42, t43);
                        gm_tftb1odc739t5 t45 = t44;
                        if (((((uint8_t *)&t45))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t45) + 1, gm_srcp2qdr7uqsmui, 345, 21);
                        gm_b0_1 t46 = GM_LD(gm_b0_1, (((uint8_t *)&t45) + 1));
                        goto me25;
mn40:;
                        uint8_t t48 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        if (!(t48 == ((uint8_t)13ULL))) goto mn47;
                        void * t49 = gm_l1;
                        gm_b16_8 t50;
                        GM_ST(void *, (uint8_t *)&t50, (void *)(gm_s9cb6j5ok7bdk));
                        GM_ST(uintptr_t, (uint8_t *)&t50 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t51 = gm_f_extend_0cs85gs(t49, t50);
                        gm_tftb1odc739t5 t52 = t51;
                        if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srcp2qdr7uqsmui, 346, 21);
                        gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
                        goto me25;
mn47:;
                        uint8_t t55 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        if (!(t55 == ((uint8_t)9ULL))) goto mn54;
                        void * t56 = gm_l1;
                        gm_b16_8 t57;
                        GM_ST(void *, (uint8_t *)&t57, (void *)(gm_s22g482qk7a40));
                        GM_ST(uintptr_t, (uint8_t *)&t57 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t58 = gm_f_extend_0cs85gs(t56, t57);
                        gm_tftb1odc739t5 t59 = t58;
                        if (((((uint8_t *)&t59))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t59) + 1, gm_srcp2qdr7uqsmui, 347, 21);
                        gm_b0_1 t60 = GM_LD(gm_b0_1, (((uint8_t *)&t59) + 1));
                        goto me25;
mn54:;
                        uint8_t t62 = GM_LD(uint8_t, ((uint8_t *)&t24));
                        if (!(t62 == ((uint8_t)0ULL))) goto mn61;
                        void * t63 = gm_l1;
                        gm_b16_8 t64;
                        GM_ST(void *, (uint8_t *)&t64, (void *)(gm_s2v5amvfkfoll));
                        GM_ST(uintptr_t, (uint8_t *)&t64 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t65 = gm_f_extend_0cs85gs(t63, t64);
                        gm_tftb1odc739t5 t66 = t65;
                        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 1, gm_srcp2qdr7uqsmui, 348, 18);
                        gm_b0_1 t67 = GM_LD(gm_b0_1, (((uint8_t *)&t66) + 1));
                        goto me25;
mn61:;
                        {
                            uint8_t t71 = 0;
                            uint8_t t72 = gm_l4;
                            uint8_t t73 = (t72 < ((uint8_t)32ULL));
                            if (!t73) {
                                uint8_t t74 = gm_l4;
                                uint8_t t75 = ((t74 == ((uint8_t)127ULL)));
                                t71 = t75;
                            } else t71 = 1;
                            if (!t71) goto ie70;
                            {
                                void * t77 = gm_l1;
                                uint8_t t78 = gm_l4;
                                gm_t9qkh0rsooc8d t79;
                                GM_ST(uint8_t, ((uint8_t *)&t79 + 0), t78);
                                gm_tftb1odc739t5 t80 = gm_f_write_kki8q0t(t77, t79);
                                gm_tftb1odc739t5 t81 = t80;
                                if (((((uint8_t *)&t81))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t81) + 1, gm_srcp2qdr7uqsmui, 351, 21);
                                gm_b0_1 t82 = GM_LD(gm_b0_1, (((uint8_t *)&t81) + 1));
                            }
                            goto ix70;
ie70:;
                            {
                                void * t84 = gm_l1;
                                uint8_t t85 = gm_l4;
                                gm_tftb1odc739t5 t86 = gm_f_push_t83rg2m(t84, t85);
                                gm_tftb1odc739t5 t87 = t86;
                                if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 1, gm_srcp2qdr7uqsmui, 353, 21);
                                gm_b0_1 t88 = GM_LD(gm_b0_1, (((uint8_t *)&t87) + 1));
                            }
ix70:;
                        }
                        goto me25;
mn68:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 342, 9);
me25:;
                    }
                    goto me19;
mn20:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 341, 5);
me19:;
                }
lc8:;
                {
                    uint64_t t90 = gm_l3;
                    uint64_t r91; bool o92 = __builtin_add_overflow(t90, ((uint64_t)1ULL), &r91);
                    uint64_t t93 = r91;
                    gm_l3 = t93;
                }
            }
lb8:;
        }
    }
    {
        void * t94 = gm_l1;
        gm_tftb1odc739t5 t95 = gm_f_push_t83rg2m(t94, ((uint8_t)34ULL));
        gm_tftb1odc739t5 t96 = t95;
        if (((((uint8_t *)&t96))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t96) + 1, gm_srcp2qdr7uqsmui, 358, 5);
        gm_b0_1 t97 = GM_LD(gm_b0_1, (((uint8_t *)&t96) + 1));
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) gm_b0_1 gm_f_write_float_7p0icl6(void *env, gm_node *cx, gm_node *inst, double a0, void * a1) {
    struct gm_fr_write_float_7p0icl6 F;
    double gm_l0; /* x */
    void * gm_l1; /* out */
    gm_b16_8 gm_l2; /* text */
    uint64_t gm_l3; /* e */
    gm_b16_8 gm_l4; /* mantissa */
    int64_t gm_l5; /* exp */
    uint8_t gm_l6; /* negative */
    int64_t gm_l8; /* point */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    uint64_t gm_l11; /* i */
    uint8_t gm_l12; /* c */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* k */
    GM_STACK_CHECK(&F, 1653u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        double t3 = gm_l0;
        double t4 = gm_l0;
        double t5 = (t3 - t4);
        uint8_t t6 = (!(t5 == ((double)0e0)));
        if (!t6) goto ie2;
        double t7 = gm_l0;
        double t8 = gm_l0;
        uint8_t t9 = ((t7 == t8));
        if (!t9) goto ie2;
        {
            void * t11 = gm_l1;
            gm_b16_8 t12;
            double t14 = gm_l0;
            uint8_t t15 = (t14 < ((double)0e0));
            if (!t15) goto ie13;
            gm_b16_8 t17;
            GM_ST(void *, (uint8_t *)&t17, (void *)(gm_segch6a5dme9u));
            GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(4));
            gm_b16_8 t18 = t17;
            t12 = t18;
            goto ix13;
ie13:;
            gm_b16_8 t20;
            GM_ST(void *, (uint8_t *)&t20, (void *)(gm_sns75reqq32d2));
            GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(3));
            gm_b16_8 t21 = t20;
            t12 = t21;
ix13:;
            gm_tftb1odc739t5 t22 = gm_f_extend_0cs85gs(t11, t12);
            gm_tftb1odc739t5 t23 = t22;
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srcp2qdr7uqsmui, 258, 9);
            gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
        }
        {
            gm_b0_1 t25;
            uint8_t t26 = 0;
            F.ret = t25;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        double t28 = gm_l0;
        double t29 = gm_l0;
        uint8_t t30 = (!(t28 == t29));
        if (!t30) goto ie27;
        {
            void * t32 = gm_l1;
            gm_b16_8 t33;
            GM_ST(void *, (uint8_t *)&t33, (void *)(gm_s0lrfrtvffhu7));
            GM_ST(uintptr_t, (uint8_t *)&t33 + 8, (uintptr_t)(3));
            gm_tftb1odc739t5 t34 = gm_f_extend_0cs85gs(t32, t33);
            gm_tftb1odc739t5 t35 = t34;
            if (((((uint8_t *)&t35))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t35) + 1, gm_srcp2qdr7uqsmui, 262, 9);
            gm_b0_1 t36 = GM_LD(gm_b0_1, (((uint8_t *)&t35) + 1));
        }
        {
            gm_b0_1 t37;
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
        goto ix27;
ie27:;
ix27:;
    }
    {
        double t39 = gm_l0;
        gm_tidu2er0d7h95 t40;
        GM_ST(double, ((uint8_t *)&t40 + 0), t39);
        gm_tabb710d9mq2e t41;
        gm_f_format_gu67n99((void *)&t41, (void *)NULL, cx, NULL, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t42 = t41;
        if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_srcp2qdr7uqsmui, 265, 13);
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
        gm_l2 = t43;
    }
    {
        gm_b16_8 t44 = gm_l2;
        gm_b16_8 t45 = t44;
        gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
        gm_tf5srh4mp4b3i t47 = gm_f_index_of_hdeg351(t46, ((uint8_t)101ULL));
        gm_tf5srh4mp4b3i t48 = t47;
        uint64_t t49;
        if (((((uint8_t *)&t48))[8] != 0)) {
            uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)&t48) + 0));
            t49 = t50;
        } else {
            {
                void * t52 = gm_l1;
                gm_b16_8 t53 = gm_l2;
                gm_b16_8 t54 = t53;
                gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
                gm_tftb1odc739t5 t56 = gm_f_extend_0cs85gs(t52, t55);
                gm_tftb1odc739t5 t57 = t56;
                if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 1, gm_srcp2qdr7uqsmui, 267, 9);
                gm_b0_1 t58 = GM_LD(gm_b0_1, (((uint8_t *)&t57) + 1));
            }
            gm_b0_1 t59;
            uint8_t t60 = 0;
            F.ret = t59;
            goto gm_fret;
        }
        gm_l3 = t49;
    }
    {
        uint64_t t61 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t62 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t63 = gm_l3;
        uint64_t t64 = (uintptr_t)t63;
        if ((uintptr_t)0 > t64 || t64 > (uintptr_t)t61) gm_trap_slice((uintptr_t)0, t64, t61, gm_srcp2qdr7uqsmui, 271, 17);
        gm_b16_8 t65;
        GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t62 + (uintptr_t)0 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(t64 - (uintptr_t)0));
        gm_l4 = t65;
    }
    {
        uint64_t t66 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        void * t67 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)));
        uint64_t t68 = gm_l3;
        uint64_t r69; bool o70 = __builtin_add_overflow(t68, ((uint64_t)1ULL), &r69);
        if (o70) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 272, 32);
        uint64_t t71 = r69;
        uint64_t t72 = (uintptr_t)t71;
        if (t72 > (uintptr_t)t66 || (uintptr_t)t66 > (uintptr_t)t66) gm_trap_slice(t72, (uintptr_t)t66, t66, gm_srcp2qdr7uqsmui, 272, 27);
        gm_b16_8 t73;
        GM_ST(void *, (uint8_t *)&t73, (void *)(((uint8_t *)t67 + t72 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t73 + 8, (uintptr_t)((uintptr_t)t66 - t72));
        gm_b16_8 t74 = t73;
        gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
        int64_t t76 = gm_f_parse_exponent_4uekfcv(t75);
        gm_l5 = t76;
    }
    {
        uint8_t t77 = 0;
        uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        uint8_t t79 = (t78 > ((uint64_t)0ULL));
        if (t79) {
            uint64_t t80 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t81 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            if (t80 >= (uintptr_t)(t81)) gm_trap_index(t80, t81, gm_srcp2qdr7uqsmui, 273, 38);
            uint8_t t82 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t80 * 1));
            uint8_t t83 = ((t82 == ((uint8_t)45ULL)));
            t77 = t83;
        } else t77 = 0;
        gm_l6 = t77;
    }
    {
        uint8_t t85 = gm_l6;
        if (!t85) goto ie84;
        {
            uint64_t t87 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
            void * t88 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)));
            uint64_t t89 = (uintptr_t)((uint64_t)1ULL);
            if (t89 > (uintptr_t)t87 || (uintptr_t)t87 > (uintptr_t)t87) gm_trap_slice(t89, (uintptr_t)t87, t87, gm_srcp2qdr7uqsmui, 274, 30);
            gm_b16_8 t90;
            GM_ST(void *, (uint8_t *)&t90, (void *)(((uint8_t *)t88 + t89 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)((uintptr_t)t87 - t89));
            gm_l4 = t90;
        }
        goto ix84;
ie84:;
ix84:;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l7, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint64_t t91 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
        int64_t t92 = (int64_t)t91;
        gm_l8 = t92;
    }
    {
        {
            gm_b16_8 t94 = gm_l4;
            gm_b16_8 t95 = t94;
            gm_b16_8 t96 = GM_LD(gm_b16_8, ((uint8_t *)&t95));
            gm_l9 = t96;
        }
        {
            gm_l10 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t98 = gm_l10;
                uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                uint8_t t100 = (t98 < t99);
                if (!t100) goto lb97;
                {
                    uint64_t t103 = gm_l10;
                    uint64_t t104 = gm_l10;
                    uint64_t t105 = (uintptr_t)t104;
                    uint64_t t106 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    uint8_t t107 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t105 * 1));
                    gm_tf5srh4mp4b3i t108;
                    GM_ST(uint8_t, (uint8_t *)&t108 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t108 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t108 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t108 + 0), t103);
                    GM_ST(uint8_t, ((uint8_t *)&t108 + 8), t107);
                    gm_tf5srh4mp4b3i t109 = t108;
                    uint64_t t112 = GM_LD(uint64_t, (((uint8_t *)&t109) + 0));
                    gm_l11 = t112;
                    uint8_t t113 = GM_LD(uint8_t, (((uint8_t *)&t109) + 8));
                    gm_l12 = t113;
                    {
                        uint8_t t116 = gm_l12;
                        uint8_t t117 = ((t116 == ((uint8_t)46ULL)));
                        if (!t117) goto ie115;
                        {
                            uint64_t t119 = gm_l11;
                            int64_t t120 = (int64_t)t119;
                            gm_l8 = t120;
                        }
                        goto ix115;
ie115:;
                        {
                            void * t122 = (void *)((uint8_t *)&F.l7);
                            uint8_t t123 = gm_l12;
                            gm_tftb1odc739t5 t124 = gm_f_push_t83rg2m(t122, t123);
                            gm_tftb1odc739t5 t125 = t124;
                            if (((((uint8_t *)&t125))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t125) + 1, gm_srcp2qdr7uqsmui, 281, 13);
                            gm_b0_1 t126 = GM_LD(gm_b0_1, (((uint8_t *)&t125) + 1));
                        }
ix115:;
                    }
                    goto me110;
mn111:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 277, 5);
me110:;
                }
lc97:;
                {
                    uint64_t t128 = gm_l10;
                    uint64_t r129; bool o130 = __builtin_add_overflow(t128, ((uint64_t)1ULL), &r129);
                    uint64_t t131 = r129;
                    gm_l10 = t131;
                }
            }
lb97:;
        }
    }
    {
        int64_t t132 = gm_l8;
        int64_t t133 = gm_l5;
        int64_t r134; bool o135 = __builtin_add_overflow(t132, t133, &r134);
        if (o135) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 284, 5);
        int64_t t136 = r134;
        gm_l8 = t136;
    }
    {
        uint8_t t138 = gm_l6;
        if (!t138) goto ie137;
        {
            void * t140 = gm_l1;
            gm_tftb1odc739t5 t141 = gm_f_push_t83rg2m(t140, ((uint8_t)45ULL));
            gm_tftb1odc739t5 t142 = t141;
            if (((((uint8_t *)&t142))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t142) + 1, gm_srcp2qdr7uqsmui, 285, 19);
            gm_b0_1 t143 = GM_LD(gm_b0_1, (((uint8_t *)&t142) + 1));
        }
        goto ix137;
ie137:;
ix137:;
    }
    {
        int64_t t145 = gm_l8;
        uint8_t t146 = (t145 <= ((int64_t)0LL));
        if (!t146) goto ie144;
        {
            void * t148 = gm_l1;
            gm_b16_8 t149;
            GM_ST(void *, (uint8_t *)&t149, (void *)(gm_sjq55q61p743j));
            GM_ST(uintptr_t, (uint8_t *)&t149 + 8, (uintptr_t)(2));
            gm_tftb1odc739t5 t150 = gm_f_extend_0cs85gs(t148, t149);
            gm_tftb1odc739t5 t151 = t150;
            if (((((uint8_t *)&t151))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t151) + 1, gm_srcp2qdr7uqsmui, 287, 9);
            gm_b0_1 t152 = GM_LD(gm_b0_1, (((uint8_t *)&t151) + 1));
        }
        {
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                int64_t t154 = gm_l8;
                if (t154 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 288, 22);
                int64_t t155 = (int64_t)((uint64_t)0 - (uint64_t)t154);
                int64_t t156 = t155;
                uint64_t t157;
                memset(&t157, 0, sizeof t157); memcpy(&t157, ((uint8_t *)&t156), sizeof t157 < 8 ? sizeof t157 : 8);
                uint64_t t158 = (uint64_t)t157;
                gm_l13 = t158;
            }
            {
                for (;;) {
                    uint64_t t160 = gm_l14;
                    uint64_t t161 = gm_l13;
                    uint8_t t162 = (t160 < t161);
                    if (!t162) goto lb159;
                    {
                        uint64_t t165 = gm_l14;
                        uint64_t t166 = t165;
                        {
                            void * t170 = gm_l1;
                            gm_tftb1odc739t5 t171 = gm_f_push_t83rg2m(t170, ((uint8_t)48ULL));
                            gm_tftb1odc739t5 t172 = t171;
                            if (((((uint8_t *)&t172))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t172) + 1, gm_srcp2qdr7uqsmui, 288, 55);
                            gm_b0_1 t173 = GM_LD(gm_b0_1, (((uint8_t *)&t172) + 1));
                        }
                        goto me167;
mn168:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 288, 9);
me167:;
                    }
lc159:;
                    {
                        uint64_t t175 = gm_l14;
                        uint64_t r176; bool o177 = __builtin_add_overflow(t175, ((uint64_t)1ULL), &r176);
                        uint64_t t178 = r176;
                        gm_l14 = t178;
                    }
                }
lb159:;
            }
        }
        {
            void * t179 = gm_l1;
            gm_b16_8 t180 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_b16_8 t181 = t180;
            gm_b16_8 t182 = GM_LD(gm_b16_8, ((uint8_t *)&t181));
            gm_tftb1odc739t5 t183 = gm_f_extend_0cs85gs(t179, t182);
            gm_tftb1odc739t5 t184 = t183;
            if (((((uint8_t *)&t184))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t184) + 1, gm_srcp2qdr7uqsmui, 289, 9);
            gm_b0_1 t185 = GM_LD(gm_b0_1, (((uint8_t *)&t184) + 1));
        }
        goto ix144;
ie144:;
        int64_t t187 = gm_l8;
        uint64_t t188 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
        int64_t t189 = (int64_t)t188;
        uint8_t t190 = (t187 >= t189);
        if (!t190) goto ie186;
        {
            void * t192 = gm_l1;
            gm_b16_8 t193 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_b16_8 t194 = t193;
            gm_b16_8 t195 = GM_LD(gm_b16_8, ((uint8_t *)&t194));
            gm_tftb1odc739t5 t196 = gm_f_extend_0cs85gs(t192, t195);
            gm_tftb1odc739t5 t197 = t196;
            if (((((uint8_t *)&t197))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t197) + 1, gm_srcp2qdr7uqsmui, 291, 9);
            gm_b0_1 t198 = GM_LD(gm_b0_1, (((uint8_t *)&t197) + 1));
        }
        {
            {
                gm_l16 = ((uint64_t)0ULL);
            }
            {
                int64_t t200 = gm_l8;
                uint64_t t201 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
                int64_t t202 = (int64_t)t201;
                int64_t r203; bool o204 = __builtin_sub_overflow(t200, t202, &r203);
                if (o204) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 292, 22);
                int64_t t205 = r203;
                int64_t t206 = t205;
                uint64_t t207;
                memset(&t207, 0, sizeof t207); memcpy(&t207, ((uint8_t *)&t206), sizeof t207 < 8 ? sizeof t207 : 8);
                uint64_t t208 = (uint64_t)t207;
                gm_l15 = t208;
            }
            {
                for (;;) {
                    uint64_t t210 = gm_l16;
                    uint64_t t211 = gm_l15;
                    uint8_t t212 = (t210 < t211);
                    if (!t212) goto lb209;
                    {
                        uint64_t t215 = gm_l16;
                        uint64_t t216 = t215;
                        {
                            void * t220 = gm_l1;
                            gm_tftb1odc739t5 t221 = gm_f_push_t83rg2m(t220, ((uint8_t)48ULL));
                            gm_tftb1odc739t5 t222 = t221;
                            if (((((uint8_t *)&t222))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t222) + 1, gm_srcp2qdr7uqsmui, 292, 84);
                            gm_b0_1 t223 = GM_LD(gm_b0_1, (((uint8_t *)&t222) + 1));
                        }
                        goto me217;
mn218:;
                        gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 292, 9);
me217:;
                    }
lc209:;
                    {
                        uint64_t t225 = gm_l16;
                        uint64_t r226; bool o227 = __builtin_add_overflow(t225, ((uint64_t)1ULL), &r226);
                        uint64_t t228 = r226;
                        gm_l16 = t228;
                    }
                }
lb209:;
            }
        }
        goto ix186;
ie186:;
        {
            int64_t t230 = gm_l8;
            int64_t t231 = t230;
            uint64_t t232;
            memset(&t232, 0, sizeof t232); memcpy(&t232, ((uint8_t *)&t231), sizeof t232 < 8 ? sizeof t232 : 8);
            uint64_t t233 = (uint64_t)t232;
            gm_l17 = t233;
        }
        {
            void * t234 = gm_l1;
            uint64_t t235 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
            void * t236 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)));
            uint64_t t237 = gm_l17;
            uint64_t t238 = (uintptr_t)t237;
            if ((uintptr_t)0 > t238 || t238 > (uintptr_t)t235) gm_trap_slice((uintptr_t)0, t238, t235, gm_srcp2qdr7uqsmui, 295, 20);
            gm_b16_8 t239;
            GM_ST(void *, (uint8_t *)&t239, (void *)(((uint8_t *)t236 + (uintptr_t)0 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(t238 - (uintptr_t)0));
            gm_b16_8 t240 = t239;
            gm_b16_8 t241 = GM_LD(gm_b16_8, ((uint8_t *)&t240));
            gm_tftb1odc739t5 t242 = gm_f_extend_0cs85gs(t234, t241);
            gm_tftb1odc739t5 t243 = t242;
            if (((((uint8_t *)&t243))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t243) + 1, gm_srcp2qdr7uqsmui, 295, 9);
            gm_b0_1 t244 = GM_LD(gm_b0_1, (((uint8_t *)&t243) + 1));
        }
        {
            void * t245 = gm_l1;
            gm_tftb1odc739t5 t246 = gm_f_push_t83rg2m(t245, ((uint8_t)46ULL));
            gm_tftb1odc739t5 t247 = t246;
            if (((((uint8_t *)&t247))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t247) + 1, gm_srcp2qdr7uqsmui, 296, 9);
            gm_b0_1 t248 = GM_LD(gm_b0_1, (((uint8_t *)&t247) + 1));
        }
        {
            void * t249 = gm_l1;
            uint64_t t250 = GM_LD(uintptr_t, (((uint8_t *)&F.l7) + 0) + 8);
            void * t251 = ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l7) + 0)));
            uint64_t t252 = gm_l17;
            uint64_t t253 = (uintptr_t)t252;
            if (t253 > (uintptr_t)t250 || (uintptr_t)t250 > (uintptr_t)t250) gm_trap_slice(t253, (uintptr_t)t250, t250, gm_srcp2qdr7uqsmui, 297, 20);
            gm_b16_8 t254;
            GM_ST(void *, (uint8_t *)&t254, (void *)(((uint8_t *)t251 + t253 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t254 + 8, (uintptr_t)((uintptr_t)t250 - t253));
            gm_b16_8 t255 = t254;
            gm_b16_8 t256 = GM_LD(gm_b16_8, ((uint8_t *)&t255));
            gm_tftb1odc739t5 t257 = gm_f_extend_0cs85gs(t249, t256);
            gm_tftb1odc739t5 t258 = t257;
            if (((((uint8_t *)&t258))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t258) + 1, gm_srcp2qdr7uqsmui, 297, 9);
            gm_b0_1 t259 = GM_LD(gm_b0_1, (((uint8_t *)&t258) + 1));
        }
ix186:;
ix144:;
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
__attribute__((aligned(4))) gm_b0_1 gm_f_write_int_lq1ktof(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint8_t a1, void * a2) {
    struct gm_fr_write_int_lq1ktof F;
    gm_b16_8 gm_l0; /* b */
    uint8_t gm_l1; /* signed */
    void * gm_l2; /* out */
    unsigned __int128 gm_l3; /* v */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint64_t gm_l6; /* i */
    uint8_t gm_l7; /* c */
    uint64_t gm_l9; /* n */
    GM_STACK_CHECK(&F, 714u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        gm_l3 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
    }
    {
        {
            gm_b16_8 t3 = gm_l0;
            gm_l4 = t3;
        }
        {
            gm_l5 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t5 = gm_l5;
                uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t7 = (t5 < t6);
                if (!t7) goto lb4;
                {
                    uint64_t t10 = gm_l5;
                    uint64_t t11 = gm_l5;
                    uint64_t t12 = (uintptr_t)t11;
                    uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    uint8_t t14 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t12 * 1));
                    gm_tf5srh4mp4b3i t15;
                    GM_ST(uint8_t, (uint8_t *)&t15 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t15 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t15 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t15 + 0), t10);
                    GM_ST(uint8_t, ((uint8_t *)&t15 + 8), t14);
                    gm_tf5srh4mp4b3i t16 = t15;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    gm_l6 = t19;
                    uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t16) + 8));
                    gm_l7 = t20;
                    {
                        unsigned __int128 t22 = gm_l3;
                        uint8_t t23 = gm_l7;
                        unsigned __int128 t24 = (unsigned __int128)t23;
                        uint64_t t25 = gm_l6;
                        uint64_t r26; bool o27 = __builtin_mul_overflow(t25, ((uint64_t)8ULL), &r26);
                        if (o27) gm_trap("integer overflow in `*`", gm_srcp2qdr7uqsmui, 314, 53);
                        uint64_t t28 = r26;
                        unsigned __int128 t29 = (unsigned __int128)t28;
                        unsigned __int128 n30 = (unsigned __int128)t29;
                        if (n30 >= 128) gm_trap_shift((uint64_t)n30, "u128", gm_srcp2qdr7uqsmui, 314, 37);
                        unsigned __int128 r31 = (unsigned __int128)((unsigned __int128)t24 << (int)n30);
                        bool o32 = (unsigned __int128)(r31 >> (int)n30) != t24;
                        if (o32) gm_trap("integer overflow in `<<`", gm_srcp2qdr7uqsmui, 314, 37);
                        unsigned __int128 t33 = r31;
                        unsigned __int128 t34 = (unsigned __int128)(t22 | t33);
                        gm_l3 = t34;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcp2qdr7uqsmui, 314, 5);
me17:;
                }
lc4:;
                {
                    uint64_t t36 = gm_l5;
                    uint64_t r37; bool o38 = __builtin_add_overflow(t36, ((uint64_t)1ULL), &r37);
                    uint64_t t39 = r37;
                    gm_l5 = t39;
                }
            }
lb4:;
        }
    }
    {
        uint8_t t41 = gm_l1;
        if (!t41) goto ie40;
        uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t43 = (t42 > ((uint64_t)0ULL));
        if (!t43) goto ie40;
        uint64_t t44 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t45 = (t44 < ((uint64_t)16ULL));
        if (!t45) goto ie40;
        uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint64_t r47; bool o48 = __builtin_sub_overflow(t46, ((uint64_t)1ULL), &r47);
        if (o48) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 315, 50);
        uint64_t t49 = r47;
        uint64_t t50 = (uintptr_t)t49;
        uint64_t t51 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t50 >= (uintptr_t)(t51)) gm_trap_index(t50, t51, gm_srcp2qdr7uqsmui, 315, 48);
        uint8_t t52 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t50 * 1));
        uint8_t t53 = (t52 >= ((uint8_t)128ULL));
        if (!t53) goto ie40;
        {
            unsigned __int128 t55 = gm_l3;
            unsigned __int128 t56 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            uint64_t r58; bool o59 = __builtin_mul_overflow(t57, ((uint64_t)8ULL), &r58);
            if (o59) gm_trap("integer overflow in `*`", gm_srcp2qdr7uqsmui, 317, 31);
            uint64_t t60 = r58;
            unsigned __int128 t61 = (unsigned __int128)t60;
            unsigned __int128 n62 = (unsigned __int128)t61;
            if (n62 >= 128) gm_trap_shift((uint64_t)n62, "u128", gm_srcp2qdr7uqsmui, 317, 14);
            unsigned __int128 r63 = (unsigned __int128)((unsigned __int128)t56 << (int)n62);
            bool o64 = (unsigned __int128)(r63 >> (int)n62) != t56;
            if (o64) gm_trap("integer overflow in `<<`", gm_srcp2qdr7uqsmui, 317, 14);
            unsigned __int128 t65 = r63;
            unsigned __int128 t66 = (unsigned __int128)(t55 | t65);
            gm_l3 = t66;
        }
        goto ix40;
ie40:;
ix40:;
    }
    {
        uint8_t t68 = gm_l1;
        if (!t68) goto ie67;
        unsigned __int128 t69 = gm_l3;
        unsigned __int128 n70 = (unsigned __int128)((uint64_t)127ULL);
        if (n70 >= 128) gm_trap_shift((uint64_t)n70, "u128", gm_srcp2qdr7uqsmui, 319, 19);
        unsigned __int128 t71 = (unsigned __int128)(t69 >> (int)n70);
        uint8_t t72 = ((t71 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
        if (!t72) goto ie67;
        {
            void * t74 = gm_l2;
            gm_tftb1odc739t5 t75 = gm_f_push_t83rg2m(t74, ((uint8_t)45ULL));
            gm_tftb1odc739t5 t76 = t75;
            if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcp2qdr7uqsmui, 320, 9);
            gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
        }
        {
            unsigned __int128 t78 = gm_l3;
            unsigned __int128 t79 = (unsigned __int128)~t78;
            unsigned __int128 r80; bool o81 = __builtin_add_overflow(t79, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r80);
            unsigned __int128 t82 = r80;
            gm_l3 = t82;
        }
        goto ix67;
ie67:;
ix67:;
    }
    {
    }
    {
        gm_l9 = ((uint64_t)0ULL);
    }
    {
        for (;;) {
            if (!((uint8_t)1)) goto lb83;
            {
                uint64_t t86 = gm_l9;
                uint64_t t87 = (uintptr_t)t86;
                if (t87 >= (uintptr_t)(40)) gm_trap_index(t87, 40, gm_srcp2qdr7uqsmui, 326, 9);
                unsigned __int128 t88 = gm_l3;
                if (((unsigned __int128)(((unsigned __int128)0u << 64) | 10u)) == 0) gm_trap("division by zero", gm_srcp2qdr7uqsmui, 326, 28);
                unsigned __int128 t89 = (unsigned __int128)(t88 % ((unsigned __int128)(((unsigned __int128)0u << 64) | 10u)));
                uint8_t t90 = (uint8_t)t89;
                uint8_t r91; bool o92 = __builtin_add_overflow(((uint8_t)48ULL), t90, &r91);
                if (o92) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 326, 21);
                uint8_t t93 = r91;
                GM_ST(uint8_t, (((uint8_t *)&F.l8) + t87 * 1), t93);
            }
            {
                uint64_t t94 = gm_l9;
                uint64_t r95; bool o96 = __builtin_add_overflow(t94, ((uint64_t)1ULL), &r95);
                if (o96) gm_trap("integer overflow in `+`", gm_srcp2qdr7uqsmui, 327, 9);
                uint64_t t97 = r95;
                gm_l9 = t97;
            }
            {
                unsigned __int128 t98 = gm_l3;
                if (((unsigned __int128)(((unsigned __int128)0u << 64) | 10u)) == 0) gm_trap("division by zero", gm_srcp2qdr7uqsmui, 328, 9);
                unsigned __int128 t99 = (unsigned __int128)(t98 / ((unsigned __int128)(((unsigned __int128)0u << 64) | 10u)));
                gm_l3 = t99;
            }
            {
                unsigned __int128 t101 = gm_l3;
                uint8_t t102 = ((t101 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
                if (!t102) goto ie100;
                {
                    goto lb83;
                }
                goto ix100;
ie100:;
ix100:;
            }
lc83:;
        }
lb83:;
    }
    {
        for (;;) {
            uint64_t t105 = gm_l9;
            uint8_t t106 = (t105 > ((uint64_t)0ULL));
            if (!t106) goto lb104;
            {
                uint64_t t109 = gm_l9;
                uint64_t r110; bool o111 = __builtin_sub_overflow(t109, ((uint64_t)1ULL), &r110);
                if (o111) gm_trap("integer overflow in `-`", gm_srcp2qdr7uqsmui, 332, 9);
                uint64_t t112 = r110;
                gm_l9 = t112;
            }
            {
                void * t113 = gm_l2;
                uint64_t t114 = gm_l9;
                uint64_t t115 = (uintptr_t)t114;
                if (t115 >= (uintptr_t)(40)) gm_trap_index(t115, 40, gm_srcp2qdr7uqsmui, 333, 18);
                uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&F.l8) + t115 * 1));
                gm_tftb1odc739t5 t117 = gm_f_push_t83rg2m(t113, t116);
                gm_tftb1odc739t5 t118 = t117;
                if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 1, gm_srcp2qdr7uqsmui, 333, 9);
                gm_b0_1 t119 = GM_LD(gm_b0_1, (((uint8_t *)&t118) + 1));
            }
lc104:;
        }
lb104:;
    }
    F.ret = GM_ZERO(gm_b0_1);
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}

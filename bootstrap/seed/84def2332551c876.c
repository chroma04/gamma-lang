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
typedef struct { int32_t f0; } gm_t19enj90vg8bd;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint64_t f0; } gm_t4ce0jd3tdro4;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { double f0; } gm_tidu2er0d7h95;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t19enj90vg8bd) == 4 && _Alignof(gm_t19enj90vg8bd) == 4 && offsetof(gm_t19enj90vg8bd, f0) == 0, "layout of `(i32,)`");
typedef struct { uint64_t f0; uint8_t f1; uint8_t p2[7]; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; uint8_t f5; uint8_t f6; uint8_t p7[6]; gm_b16_8 f7; } gm_t2t9klu4dfekp;
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
_Static_assert(sizeof(gm_t4ce0jd3tdro4) == 8 && _Alignof(gm_t4ce0jd3tdro4) == 8 && offsetof(gm_t4ce0jd3tdro4, f0) == 0, "layout of `Instant`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; uint64_t f3; gm_b16_8 f4; } gm_t89vrq05jgle5;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; double f1; uint64_t f2; uint64_t f3; } gm_tcir0vf4307lp;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint64_t f1; uint64_t f2; } gm_tgbgti82mf879;
_Static_assert(sizeof(gm_tidu2er0d7h95) == 8 && _Alignof(gm_tidu2er0d7h95) == 8 && offsetof(gm_tidu2er0d7h95, f0) == 0, "layout of `(f64,)`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; gm_b16_8 f4; uint64_t f5; } gm_tjftu5su6fh8e;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { gm_b16_8 f0; uint32_t f1; uint8_t pe[4]; } gm_tommf4j9upomc;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint32_t f2; uint8_t p3[4]; gm_b16_8 f3; } gm_tu8mt0h8clq1l;
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
_Static_assert(sizeof(gm_t2t9klu4dfekp) == 80 && _Alignof(gm_t2t9klu4dfekp) == 8 && offsetof(gm_t2t9klu4dfekp, f0) == 0 && offsetof(gm_t2t9klu4dfekp, f1) == 8 && offsetof(gm_t2t9klu4dfekp, f2) == 16 && offsetof(gm_t2t9klu4dfekp, f3) == 24 && offsetof(gm_t2t9klu4dfekp, f4) == 40 && offsetof(gm_t2t9klu4dfekp, f5) == 56 && offsetof(gm_t2t9klu4dfekp, f6) == 57 && offsetof(gm_t2t9klu4dfekp, f7) == 64, "layout of `(u64, bool, u64, string, string, bool, bool, string)`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t89vrq05jgle5) == 72 && _Alignof(gm_t89vrq05jgle5) == 8 && offsetof(gm_t89vrq05jgle5, f0) == 0 && offsetof(gm_t89vrq05jgle5, f1) == 16 && offsetof(gm_t89vrq05jgle5, f2) == 32 && offsetof(gm_t89vrq05jgle5, f3) == 48 && offsetof(gm_t89vrq05jgle5, f4) == 56, "layout of `(string, string, string, usize, string)`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tcir0vf4307lp) == 40 && _Alignof(gm_tcir0vf4307lp) == 8 && offsetof(gm_tcir0vf4307lp, f0) == 0 && offsetof(gm_tcir0vf4307lp, f1) == 16 && offsetof(gm_tcir0vf4307lp, f2) == 24 && offsetof(gm_tcir0vf4307lp, f3) == 32, "layout of `(string, f64, usize, u64)`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_tgbgti82mf879) == 32 && _Alignof(gm_tgbgti82mf879) == 8 && offsetof(gm_tgbgti82mf879, f0) == 0 && offsetof(gm_tgbgti82mf879, f1) == 16 && offsetof(gm_tgbgti82mf879, f2) == 24, "layout of `PayloadUnion`");
_Static_assert(sizeof(gm_tjftu5su6fh8e) == 88 && _Alignof(gm_tjftu5su6fh8e) == 8 && offsetof(gm_tjftu5su6fh8e, f0) == 0 && offsetof(gm_tjftu5su6fh8e, f1) == 16 && offsetof(gm_tjftu5su6fh8e, f2) == 32 && offsetof(gm_tjftu5su6fh8e, f3) == 48 && offsetof(gm_tjftu5su6fh8e, f4) == 64 && offsetof(gm_tjftu5su6fh8e, f5) == 80, "layout of `(string, string, string, []u8, string, usize)`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
_Static_assert(sizeof(gm_tommf4j9upomc) == 24 && _Alignof(gm_tommf4j9upomc) == 8 && offsetof(gm_tommf4j9upomc, f0) == 0 && offsetof(gm_tommf4j9upomc, f1) == 16, "layout of `(string, u32)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_tu8mt0h8clq1l) == 56 && _Alignof(gm_tu8mt0h8clq1l) == 8 && offsetof(gm_tu8mt0h8clq1l, f0) == 0 && offsetof(gm_tu8mt0h8clq1l, f1) == 16 && offsetof(gm_tu8mt0h8clq1l, f2) == 32 && offsetof(gm_tu8mt0h8clq1l, f3) == 40, "layout of `(string, string, u32, string)`");
struct gm_fr_empty_slice_ijtp9lo {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_fn_05ojb2u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_0a9g03n {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_0t1ag66 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_1obfloe {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_2g2eiv7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_3rspl53 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4ffciir {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4lcplfd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4tf2ot2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_4tfjk3f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_51kjin8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_543a87u {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5bfs7qc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_5ib6bd8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_6f1gf3a {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_728pqq7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_77nmu40 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_7qeivkm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_8cqj68l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_8mh6knp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_904hlgo {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_937s740 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_98g0vtt {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9cg7es9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_9dmltq8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_a4ftpa8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_a7rjjrm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_a7v6hmb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_aieghgq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_aj2h13f {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_b410aaq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_b55ja8s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_chvm7s9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_cul2d9o {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_d4uo89t {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_d6e80g1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dcje4d7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_di6bt53 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_dncn65l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_e4nkn50 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_e9652ut {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ec7eir4 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_enpptps {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_f2cbcor {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_f7ktlj1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fe6g8l3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fljhc60 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_fpsbucs {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ft417o7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_gb3qal8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_god4jpr {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_h0erin9 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_h3fae34 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_i88qt72 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_iai6vb1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_idv1c0k {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_igffqdd {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ivdb044 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_j0o76ud {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_j9camnq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_jmf3ip7 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_k4qgi5h {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_k9j41lp {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_kfov7le {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_kplljv5 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ktkp9ie {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_l4n8i44 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_llf386l {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_m1o2jhc {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_met4j5v {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mjfoigj {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_mvtglrm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_nfe1f9q {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_njvtn59 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ob47von {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_obu2gu3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_of719hm {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_oh96lv6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ok83ap3 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_p8oa45s {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pglbpad {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pm0r0gg {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_pqvj985 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_ptsem8q {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_q87drk0 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_r93psta {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_rdnojv6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_s5945uf {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_shchrt8 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_si15bi1 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_sq4r3sb {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_su5eab6 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_t92bq9c {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_tts9q1g {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_v14dpu2 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vbi5n40 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vp9f5dk {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vqjh5ta {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vsotr9q {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_fn_vtbsvdq {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_format_d04m6io {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d0bas9o {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d0lfl1k {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d277g96 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d2d7bae {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d38c0qs {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d3cmh7d {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d5h9av5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d613p2h {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d6fj9tl {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d6hu3gb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d7osfot {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_d801g52 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_da6unq6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_da7bimt {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_daod35i {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_davqh84 {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dbdhrrf {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dcns5hm {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dd3t0vb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dd6d4bt {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dd92ata {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ddl06ea {
    gm_frame h;
    gm_t2t9klu4dfekp l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t2t9klu4dfekp l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ddrb17v {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_de12mga {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_df9v4jq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dg0m8l8 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dgfvefu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dhjqjjt {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dir32ml {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_djdpqv2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dl7scf9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dlemvkb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dmukbli {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dn5pqnq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dp1qh3k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dr0nbiu {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dr7jgt6 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dr8c0gq {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_drnt54k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dsemf9s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dsta2ql {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dtmr1c2 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_du48r1i {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_du76tsh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ducddq0 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dukk0t3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_duvvd80 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dv5f37i {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dvcii4b {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_dvcvfgj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t00o2ec {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t04dp7b {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t0vntpb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t2lplfr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t32jdqr {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t3f3kj5 {
    gm_frame h;
    gm_tjftu5su6fh8e l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tjftu5su6fh8e l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t3l2mea {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t3u5vlm {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t472cpe {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t4gbkjg {
    gm_frame h;
    gm_tftksr4op114e l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tftksr4op114e l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t4ivgd1 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t5fmuah {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t68frbc {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t6rdn7r {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t725p24 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t8lugsu {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t8pf89n {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_t9gsufu {
    gm_frame h;
    gm_tjftu5su6fh8e l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tjftu5su6fh8e l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ta7nl7k {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tat75d3 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tb06a6e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tbdgr6h {
    gm_frame h;
    gm_t89vrq05jgle5 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t89vrq05jgle5 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tbvfjdi {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tc7tn98 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tcr2jpk {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tcvndjh {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_td6patj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_te2qqk4 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tffhbfn {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tg9hp73 {
    gm_frame h;
    gm_tu8mt0h8clq1l l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tu8mt0h8clq1l l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_thddtj7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_thmq1t9 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ti4oun0 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_ti6vs6t {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tjkla39 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tjld97n {
    gm_frame h;
    gm_tcir0vf4307lp l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tcir0vf4307lp l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tjqc107 {
    gm_frame h;
    gm_tfeab279c16rr l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tfeab279c16rr l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tn4egi5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tovpseb {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tppjqmg {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tra8sop {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tsutrj7 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tt07921 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tt4n370 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tth3l1d {
    gm_frame h;
    gm_t0uuhaljsf410 l0; /* args */
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_t0uuhaljsf410 l3; /* <tmp> */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tttig7s {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tugmr4e {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tv3i6qj {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_format_tvr4di5 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* l */
    uint8_t l2; /* failed */
    gm_tabb710d9mq2e ret;
};
struct gm_fr_list_uvfu1k1 {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
__attribute__((aligned(4))) void gm_f_deinit_r70pji5(void * a0);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05ojb2u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0a9g03n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0t1ag66(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1obfloe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2g2eiv7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3rspl53(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4ffciir(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4lcplfd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4tf2ot2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4tfjk3f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_51kjin8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_543a87u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5bfs7qc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5ib6bd8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6f1gf3a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_728pqq7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_77nmu40(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7qeivkm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8cqj68l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8mh6knp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_904hlgo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_937s740(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_98g0vtt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9cg7es9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9dmltq8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a4ftpa8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a7rjjrm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a7v6hmb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aieghgq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aj2h13f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b410aaq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b55ja8s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_chvm7s9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cul2d9o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_d4uo89t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_d6e80g1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dcje4d7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_di6bt53(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dncn65l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e4nkn50(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e9652ut(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ec7eir4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_enpptps(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f2cbcor(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f7ktlj1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fe6g8l3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fljhc60(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fpsbucs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ft417o7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gb3qal8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_god4jpr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h0erin9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h3fae34(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i88qt72(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_iai6vb1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_idv1c0k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_igffqdd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ivdb044(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j0o76ud(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9camnq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jmf3ip7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k4qgi5h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k9j41lp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kfov7le(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kplljv5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ktkp9ie(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l4n8i44(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_llf386l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m1o2jhc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_met4j5v(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mjfoigj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mvtglrm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nfe1f9q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_njvtn59(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ob47von(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_obu2gu3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_of719hm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oh96lv6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ok83ap3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p8oa45s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pglbpad(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pm0r0gg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pqvj985(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ptsem8q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q87drk0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r93psta(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rdnojv6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s5945uf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_shchrt8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_si15bi1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sq4r3sb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_su5eab6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t92bq9c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tts9q1g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v14dpu2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vbi5n40(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vp9f5dk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vqjh5ta(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vsotr9q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vtbsvdq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_d04m6io(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_d0bas9o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d0lfl1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d277g96(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_d2d7bae(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d38c0qs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_d3cmh7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d5h9av5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d613p2h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_d6fj9tl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_d6hu3gb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_d7osfot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_d801g52(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_da6unq6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_da7bimt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_daod35i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_davqh84(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dbdhrrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dcns5hm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dd3t0vb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dd6d4bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_dd92ata(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ddl06ea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ddrb17v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_de12mga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0);
__attribute__((aligned(4))) void gm_f_format_df9v4jq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_dg0m8l8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dgfvefu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dhjqjjt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dir32ml(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_djdpqv2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dl7scf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dlemvkb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dmukbli(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dn5pqnq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_dp1qh3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dr0nbiu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dr7jgt6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dr8c0gq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_drnt54k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dsemf9s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dtmr1c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_du48r1i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_du76tsh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ducddq0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dukk0t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_duvvd80(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_dv5f37i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dvcii4b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dvcvfgj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t00o2ec(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t04dp7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_t0vntpb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t2lplfr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t32jdqr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t3f3kj5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t3l2mea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t3u5vlm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t472cpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t4gbkjg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t4ivgd1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_t5fmuah(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t68frbc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t6rdn7r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t725p24(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_t8lugsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t8pf89n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_t9gsufu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ta7nl7k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tat75d3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_tb06a6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tbdgr6h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tbvfjdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_tc7tn98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_tcr2jpk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tcvndjh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_format_td6patj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0);
__attribute__((aligned(4))) void gm_f_format_te2qqk4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tffhbfn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tg9hp73(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_thddtj7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_thmq1t9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ti4oun0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ti6vs6t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tjkla39(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tjld97n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tjqc107(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tn4egi5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tovpseb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tppjqmg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tra8sop(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tsutrj7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tt07921(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tt4n370(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tth3l1d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tttig7s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0);
__attribute__((aligned(4))) void gm_f_format_tugmr4e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_tv3i6qj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_tvr4di5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_reserve_d1qo8mf(void * a0, uint64_t a1);
GM_NOINLINE bool gm_pf0aqhubr4up(void *code, void *env, gm_node *cx, const void *d, uintptr_t n);
GM_NOINLINE bool gm_pfk8csn7us23(void *code, void *env, gm_node *cx, const gm_type *t, const void *v, const gm_spec *s);
extern const uint8_t gm_s03itq0fotubb[14];
extern const uint8_t gm_s05336tfii06a[18];
extern const uint8_t gm_s06nnq6fe98bg[53];
extern const uint8_t gm_s0ek1hibl01t2[47];
extern const uint8_t gm_s0eudsv4chfi5[44];
extern const uint8_t gm_s0ibfi39mcc7n[9];
extern const uint8_t gm_s0irgfjsc3pat[76];
extern const uint8_t gm_s0m0qaokk51g0[4];
extern const uint8_t gm_s0ucactj6hj0a[4];
extern const uint8_t gm_s0unahdn5j99v[14];
extern const uint8_t gm_s147lrab6u68i[29];
extern const uint8_t gm_s148evlodt315[18];
extern const uint8_t gm_s14lpmr3v2vk3[57];
extern const uint8_t gm_s1asl1e1tvb0k[2];
extern const uint8_t gm_s1i6nhfib8r9u[11];
extern const uint8_t gm_s1smed2pig5ts[35];
extern const uint8_t gm_s2053hhvf16fm[2];
extern const uint8_t gm_s23t712hkqlmp[5];
extern const uint8_t gm_s24mnh1a25k0f[31];
extern const uint8_t gm_s24s9g8r9gc0v[7];
extern const uint8_t gm_s2a85dvpmjp4u[14];
extern const uint8_t gm_s2e0u34gjtsqn[11];
extern const uint8_t gm_s2g9gpcumahrc[24];
extern const uint8_t gm_s2kieedtscsk1[12];
extern const uint8_t gm_s2l500n2vri8f[77];
extern const uint8_t gm_s2qcdb7brsccv[123];
extern const uint8_t gm_s2sfjihvaabi3[9];
extern const uint8_t gm_s2u5posfkbgnq[2];
extern const uint8_t gm_s2vme195ddtm7[25];
extern const uint8_t gm_s336v1oga58d4[41];
extern const uint8_t gm_s34hhp1ucrags[12];
extern const uint8_t gm_s35u3nlsc2qa0[27];
extern const uint8_t gm_s39mljh669qgl[2];
extern const uint8_t gm_s3brf0f9sa1j7[26];
extern const uint8_t gm_s3e9133b3pasn[3];
extern const uint8_t gm_s3mbo1s262isg[14];
extern const uint8_t gm_s3p0rbs9hc3lg[2];
extern const uint8_t gm_s3ss0hcg1h7js[7];
extern const uint8_t gm_s3t2d4i4t34ct[74];
extern const uint8_t gm_s407iureo3mia[17];
extern const uint8_t gm_s41bap0m04tt9[12];
extern const uint8_t gm_s4du6i4h6s7qo[32];
extern const uint8_t gm_s4ehfliahvei9[6];
extern const uint8_t gm_s4l676io35g5u[12];
extern const uint8_t gm_s4o541hcfllua[9];
extern const uint8_t gm_s52od60hi0cpi[21];
extern const uint8_t gm_s5c0ruf0vg8mu[3];
extern const uint8_t gm_s5c6gmkd367jj[46];
extern const uint8_t gm_s5el4r6boe881[14];
extern const uint8_t gm_s5i2d5hm88htd[4];
extern const uint8_t gm_s5l9jdmrtaean[11];
extern const uint8_t gm_s5llvatkc5h5i[14];
extern const uint8_t gm_s5pra70i8o734[14];
extern const uint8_t gm_s61i2o7ldibjh[35];
extern const uint8_t gm_s66fbu3uhl900[20];
extern const uint8_t gm_s6i2cgc2057sp[5];
extern const uint8_t gm_s6iln1emufss5[20];
extern const uint8_t gm_s6p5i58jsib94[8];
extern const uint8_t gm_s6peoato8pau5[13];
extern const uint8_t gm_s6t4r831mukdr[3];
extern const uint8_t gm_s71l0hs29ihls[39];
extern const uint8_t gm_s78lhtg84qh95[9];
extern const uint8_t gm_s79r5ktousup8[157];
extern const uint8_t gm_s7gn2sla7drif[3];
extern const uint8_t gm_s8075t766i1f0[50];
extern const uint8_t gm_s80tqs0e3u7de[11];
extern const uint8_t gm_s81dvppjuktle[9];
extern const uint8_t gm_s89gtqa9dnss0[35];
extern const uint8_t gm_s8de5r2vp8807[20];
extern const uint8_t gm_s8fq66mhsgcib[104];
extern const uint8_t gm_s948vtbtkq957[32];
extern const uint8_t gm_s94bv82atjueb[5];
extern const uint8_t gm_s9dudft75oi1c[33];
extern const uint8_t gm_s9e7jg1nv469k[6];
extern const uint8_t gm_s9jqgpofojve4[35];
extern const uint8_t gm_s9lpec5ab0tsm[111];
extern const uint8_t gm_s9mhd8h7bolj3[48];
extern const uint8_t gm_s9nl6jkck8c25[8];
extern const uint8_t gm_s9p37ubghllhn[64];
extern const uint8_t gm_s9uc7or8diipg[35];
extern const uint8_t gm_sa39uag0oed9a[16];
extern const uint8_t gm_sa525m041ir3n[17];
extern const uint8_t gm_sa9ukgikn2pki[26];
extern const uint8_t gm_sachcj4qd96of[48];
extern const uint8_t gm_sahjmrj1mjf77[8];
extern const uint8_t gm_sakto9q5516h3[50];
extern const uint8_t gm_saq7q763oq882[48];
extern const uint8_t gm_sauf6ibu7mnba[5];
extern const uint8_t gm_sb26s4qotngkv[27];
extern const uint8_t gm_sb2k9e71jsraf[7];
extern const uint8_t gm_sb2nfo787ropg[6];
extern const uint8_t gm_sb6c4qgs6rpfi[8];
extern const uint8_t gm_sb7hastj3mkf0[36];
extern const uint8_t gm_sbeoq5iu4cto8[11];
extern const uint8_t gm_sbgdbpkf33nsc[30];
extern const uint8_t gm_sbol12a8gjr17[9];
extern const uint8_t gm_sbp9encpv0e61[75];
extern const uint8_t gm_sbvet41ukod2k[16];
extern const uint8_t gm_sc48cegnkinfm[12];
extern const uint8_t gm_sc4r4l9k6f577[3];
extern const uint8_t gm_sc6pb1tb5tnnv[34];
extern const uint8_t gm_scc6ms0hrfo8j[9];
extern const uint8_t gm_scch7vqldh7aj[13];
extern const uint8_t gm_scl6is0vlr1m0[62];
extern const uint8_t gm_sd4sd2h1n94bh[7];
extern const uint8_t gm_sd70ob31745fn[4];
extern const uint8_t gm_sd7spmqdlcohl[25];
extern const uint8_t gm_sdf1o8jga4hc6[38];
extern const uint8_t gm_sdupocj1qjnur[7];
extern const uint8_t gm_sdvp5f4riom9f[3];
extern const uint8_t gm_seaesh4v4toj4[76];
extern const uint8_t gm_seccpktgmrevt[2];
extern const uint8_t gm_secneq49c8dtf[36];
extern const uint8_t gm_sedq394prkqe4[10];
extern const uint8_t gm_seidpmiq51gtp[60];
extern const uint8_t gm_sesqjijoh7km4[9];
extern const uint8_t gm_setftr5leknrn[23];
extern const uint8_t gm_setofg57no9qt[34];
extern const uint8_t gm_sf1hl3n20st3g[37];
extern const uint8_t gm_sf1v2j94ne72t[16];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sfg1v6bqcbbav[10];
extern const uint8_t gm_sfjc41f66fqb2[10];
extern const uint8_t gm_sfkrpkv21b1rq[4];
extern const uint8_t gm_sfm9elkq6bn2e[2];
extern const uint8_t gm_sg0jqm2pij3el[78];
extern const uint8_t gm_sg25rp8qmrbme[57];
extern const uint8_t gm_sg50kn7lo9hgn[86];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sghk06svnbp7m[40];
extern const uint8_t gm_sgip9ekbho6mc[11];
extern const uint8_t gm_sgj6i64633q0l[11];
extern const uint8_t gm_sgks95jf6fs73[21];
extern const uint8_t gm_sglu5epjv38rl[92];
extern const uint8_t gm_sgme8q1t414rd[19];
extern const uint8_t gm_sgmhc3klfmn46[5];
extern const uint8_t gm_sgmjl48sjjf69[22];
extern const uint8_t gm_shhn92c601tgo[29];
extern const uint8_t gm_shi7nidbc64qf[4];
extern const uint8_t gm_shl0ucmfrvk77[54];
extern const uint8_t gm_shv4snn6f8hcr[29];
extern const uint8_t gm_si0b5q45ot754[101];
extern const uint8_t gm_si4l87dl4tpkq[8];
extern const uint8_t gm_si5gf6931vagv[11];
extern const uint8_t gm_si731m8b0sr6s[4];
extern const uint8_t gm_si7vj1dcaqhfd[5];
extern const uint8_t gm_sijeantqk7jl2[11];
extern const uint8_t gm_simmbi8o1lo4k[46];
extern const uint8_t gm_siqc5cjnsa99v[38];
extern const uint8_t gm_sj21lfdl9q0t4[6];
extern const uint8_t gm_sj6enltl2qi3c[21];
extern const uint8_t gm_sj815koajgkmb[6];
extern const uint8_t gm_sj9t4d2ku5m56[47];
extern const uint8_t gm_sjaeqf6f3s51a[45];
extern const uint8_t gm_sjbtfuea8qauf[23];
extern const uint8_t gm_sjkqdj35ksd9p[3];
extern const uint8_t gm_sjl0s4joed61f[15];
extern const uint8_t gm_sk2dpusgkm375[22];
extern const uint8_t gm_sk754mj3p1el6[2];
extern const uint8_t gm_skmg81d57fq1r[31];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_skqb5p67mhjp2[43];
extern const uint8_t gm_skqf89fdi6p8e[12];
extern const uint8_t gm_skr00pujn28ln[29];
extern const uint8_t gm_sl1botv4tp1fk[58];
extern const uint8_t gm_sla366gd7nphd[58];
extern const uint8_t gm_slcgg5l8i9236[50];
extern const uint8_t gm_slj7i6lishtfg[47];
extern const uint8_t gm_sloe55935mobb[8];
extern const uint8_t gm_slq58vfha8k9h[2];
extern const uint8_t gm_slqq08j0fro21[31];
extern const uint8_t gm_sm55k23067j17[16];
extern const uint8_t gm_sm5be4659ulci[2];
extern const uint8_t gm_sm884rqp4fp7u[68];
extern const uint8_t gm_smbjf525bk9a0[20];
extern const uint8_t gm_smd81jcp0bga9[19];
extern const uint8_t gm_smdjnv4d2cfrh[31];
extern const uint8_t gm_smh12ad93nju6[45];
extern const uint8_t gm_smooaouqmm8dj[2];
extern const uint8_t gm_smuti066jdthl[24];
extern const uint8_t gm_sn048vvii9ap5[83];
extern const uint8_t gm_sn1puqmdafub0[14];
extern const uint8_t gm_sn1seggk13bop[4];
extern const uint8_t gm_sn5ntorhqodmg[39];
extern const uint8_t gm_sn5pv7k63ni4c[11];
extern const uint8_t gm_sno069vrt5g9q[4];
extern const uint8_t gm_snqmbfl2fr85m[17];
extern const uint8_t gm_snuinkttlbd28[14];
extern const uint8_t gm_snupav63sairp[12];
extern const uint8_t gm_so1us53sh0cre[44];
extern const uint8_t gm_so2blaqutst8g[53];
extern const uint8_t gm_so31m6feg820p[4];
extern const uint8_t gm_so77flgm8klar[52];
extern const uint8_t gm_so9fi6r28770a[3];
extern const uint8_t gm_soa9mnl4ahjud[8];
extern const uint8_t gm_sodgjk6pvm3b4[21];
extern const uint8_t gm_sp0doh9njf7i3[27];
extern const uint8_t gm_sp7cdd0n91vli[8];
extern const uint8_t gm_spbdk1kpko0k2[141];
extern const uint8_t gm_spf3a0kmdpgm5[6];
extern const uint8_t gm_spf5krhuq011v[11];
extern const uint8_t gm_spgnoteg4vcil[11];
extern const uint8_t gm_spgt2nbblhu8j[9];
extern const uint8_t gm_spicth2mans91[44];
extern const uint8_t gm_spod3tmkh25up[2];
extern const uint8_t gm_spp0f644is6jb[12];
extern const uint8_t gm_sq68mfjf87n5h[16];
extern const uint8_t gm_sq6v0u0m2nfv8[23];
extern const uint8_t gm_sqdqshat9srld[37];
extern const uint8_t gm_sqi8fcv4bi7s6[10];
extern const uint8_t gm_sql1qe620spbq[23];
extern const uint8_t gm_sql9r2b8fvivt[10];
extern const uint8_t gm_sqnathr6pbque[38];
extern const uint8_t gm_sqqaphhmqv39s[5];
extern const uint8_t gm_sqslcbfraugjg[8];
extern const uint8_t gm_squ601q9vv2so[33];
extern const uint8_t gm_squ7ob8ltdkn4[20];
extern const uint8_t gm_sqvepl1893o5g[3];
extern const uint8_t gm_sr4vl7ov3g7t6[55];
extern const uint8_t gm_sr87tgv3ltq65[13];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src3d0v42kavhv7[];
extern const char gm_srcrgc9bkquf6nr[];
extern const uint8_t gm_srs1vrn2uaams[2];
extern const uint8_t gm_srv4dcfccuset[3];
extern const uint8_t gm_ss27lm4pmhcij[45];
extern const uint8_t gm_ss3f6irsroj9l[37];
extern const uint8_t gm_ss6k80g3hocsv[6];
extern const uint8_t gm_ss7pu45qp7oic[6];
extern const uint8_t gm_ssa0pabeurppq[20];
extern const uint8_t gm_ssb6m4k22290v[17];
extern const uint8_t gm_ssbqeok4607ju[25];
extern const uint8_t gm_ssdho4pv9rajn[55];
extern const uint8_t gm_ssfs2ebod65fl[38];
extern const uint8_t gm_ssift49n290vj[5];
extern const uint8_t gm_ssjhh9bdn23jf[17];
extern const uint8_t gm_ssk4a01soguqj[7];
extern const uint8_t gm_sslm2oq18gln3[6];
extern const uint8_t gm_ssnku2e1q7u3k[13];
extern const uint8_t gm_ssr6vk8erbj18[13];
extern const uint8_t gm_ssud7c614kqjt[5];
extern const uint8_t gm_st2ertk6tjva7[14];
extern const uint8_t gm_st3c9ev3jkdrf[32];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_st52k2850n5r5[5];
extern const uint8_t gm_st6hj8v0h80gt[6];
extern const uint8_t gm_stc3e0j0moc2h[5];
extern const uint8_t gm_ste817enr6egu[95];
extern const uint8_t gm_stkm2m8f3okk8[18];
extern const uint8_t gm_stoidbi3vfa99[30];
extern const uint8_t gm_su1mil1q3eqgp[16];
extern const uint8_t gm_su6s568b7b4kr[5];
extern const uint8_t gm_sub7l8h1sjc33[12];
extern const uint8_t gm_sub9rknupld7d[52];
extern const uint8_t gm_subcm6amchfvv[5];
extern const uint8_t gm_suc4jg0r7eog6[59];
extern const uint8_t gm_suekibjsm2ees[34];
extern const uint8_t gm_sukriak12588g[5];
extern const uint8_t gm_sulgkejn2inql[7];
extern const uint8_t gm_sumvvvaqn43sl[10];
extern const uint8_t gm_sus38q16nfojv[7];
extern const uint8_t gm_sv3gqbo7rudsj[13];
extern const uint8_t gm_sv8lr20bqpljh[12];
extern const uint8_t gm_svdcmt7tps0ma[57];
extern const uint8_t gm_sve2u8a7eapjo[18];
extern const uint8_t gm_svgk3n4sk1uat[9];
extern const uint8_t gm_sviu2hfr78esg[6];
extern const gm_type gm_ty0iqiubfm7e;
extern const gm_type gm_ty467d5cm1d5;
extern const gm_type gm_ty6groq37ram;
extern const gm_type gm_ty929lennfd3;
extern const gm_type gm_tyb2nno7n67r;
extern const gm_type gm_tybihgc69jv3;
extern const gm_type gm_tyqa6gfa8omk;
extern const gm_type gm_tyqqajgod2bb;
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05ojb2u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_05ojb2u F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_da7bimt *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_da7bimt *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_da7bimt *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0a9g03n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_0a9g03n F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_du48r1i *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_du48r1i *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_du48r1i *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0t1ag66(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_0t1ag66 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dcns5hm *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dcns5hm *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dcns5hm *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1obfloe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_1obfloe F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_d801g52 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_d801g52 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_d801g52 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2g2eiv7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_2g2eiv7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dmukbli *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dmukbli *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dmukbli *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3rspl53(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_3rspl53 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_d613p2h *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_d613p2h *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_d613p2h *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4ffciir(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4ffciir F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tat75d3 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tat75d3 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tat75d3 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4lcplfd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4lcplfd F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tra8sop *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tra8sop *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tra8sop *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4tf2ot2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4tf2ot2 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_djdpqv2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_djdpqv2 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_djdpqv2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4tfjk3f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_4tfjk3f F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t32jdqr *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t32jdqr *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t32jdqr *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_51kjin8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_51kjin8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ducddq0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ducddq0 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ducddq0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_543a87u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_543a87u F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_d6hu3gb *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_d6hu3gb *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_d6hu3gb *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5bfs7qc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_5bfs7qc F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ta7nl7k *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ta7nl7k *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ta7nl7k *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5ib6bd8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_5ib6bd8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tovpseb *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tovpseb *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tovpseb *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6f1gf3a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_6f1gf3a F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tppjqmg *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tppjqmg *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tppjqmg *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_728pqq7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_728pqq7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dr7jgt6 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dr7jgt6 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dr7jgt6 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_77nmu40(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_77nmu40 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tg9hp73 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tg9hp73 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tg9hp73 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7qeivkm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_7qeivkm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t00o2ec *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t00o2ec *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t00o2ec *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8cqj68l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_8cqj68l F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ddrb17v *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ddrb17v *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ddrb17v *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8mh6knp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_8mh6knp F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t68frbc *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t68frbc *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t68frbc *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_904hlgo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_904hlgo F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tffhbfn *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tffhbfn *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tffhbfn *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_937s740(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_937s740 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t6rdn7r *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t6rdn7r *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t6rdn7r *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_98g0vtt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_98g0vtt F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t8pf89n *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t8pf89n *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t8pf89n *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9cg7es9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9cg7es9 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_du76tsh *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_du76tsh *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_du76tsh *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9dmltq8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_9dmltq8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_d38c0qs *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_d38c0qs *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_d38c0qs *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a4ftpa8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_a4ftpa8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dv5f37i *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dv5f37i *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dv5f37i *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a7rjjrm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_a7rjjrm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t8lugsu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t8lugsu *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t8lugsu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a7v6hmb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_a7v6hmb F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dr0nbiu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dr0nbiu *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dr0nbiu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aieghgq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_aieghgq F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t725p24 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t725p24 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t725p24 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aj2h13f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_aj2h13f F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tjld97n *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tjld97n *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tjld97n *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b410aaq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b410aaq F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tbvfjdi *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tbvfjdi *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tbvfjdi *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b55ja8s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_b55ja8s F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dl7scf9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dl7scf9 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dl7scf9 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_chvm7s9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_chvm7s9 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t9gsufu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t9gsufu *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t9gsufu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cul2d9o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_cul2d9o F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t0vntpb *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t0vntpb *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t0vntpb *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_d4uo89t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_d4uo89t F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_thddtj7 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_thddtj7 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_thddtj7 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_d6e80g1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_d6e80g1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_thmq1t9 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_thmq1t9 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_thmq1t9 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dcje4d7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_dcje4d7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ti6vs6t *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ti6vs6t *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ti6vs6t *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_di6bt53(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_di6bt53 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tjkla39 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tjkla39 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tjkla39 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dncn65l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_dncn65l F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_d0bas9o *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_d0bas9o *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_d0bas9o *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e4nkn50(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_e4nkn50 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dvcii4b *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dvcii4b *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dvcii4b *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e9652ut(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_e9652ut F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tbdgr6h *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tbdgr6h *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tbdgr6h *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ec7eir4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ec7eir4 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_d277g96 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_d277g96 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_d277g96 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_enpptps(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_enpptps F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tttig7s *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tttig7s *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tttig7s *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f2cbcor(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_f2cbcor F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tcr2jpk *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tcr2jpk *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tcr2jpk *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f7ktlj1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_f7ktlj1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dd92ata *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dd92ata *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dd92ata *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fe6g8l3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fe6g8l3 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t4ivgd1 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t4ivgd1 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t4ivgd1 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fljhc60(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fljhc60 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dsemf9s *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dsemf9s *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dsemf9s *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fpsbucs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_fpsbucs F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tth3l1d *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tth3l1d *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tth3l1d *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ft417o7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ft417o7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tt07921 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tt07921 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tt07921 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gb3qal8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_gb3qal8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_te2qqk4 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_te2qqk4 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_te2qqk4 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_god4jpr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_god4jpr F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dukk0t3 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dukk0t3 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dukk0t3 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h0erin9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_h0erin9 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tt4n370 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tt4n370 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tt4n370 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h3fae34(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_h3fae34 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_d3cmh7d *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_d3cmh7d *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_d3cmh7d *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i88qt72(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_i88qt72 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dsta2ql *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dsta2ql *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dsta2ql *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_iai6vb1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_iai6vb1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dlemvkb *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dlemvkb *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dlemvkb *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_idv1c0k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_idv1c0k F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t5fmuah *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t5fmuah *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t5fmuah *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_igffqdd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_igffqdd F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dbdhrrf *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dbdhrrf *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dbdhrrf *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ivdb044(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ivdb044 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tvr4di5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tvr4di5 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tvr4di5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j0o76ud(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_j0o76ud F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t3l2mea *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t3l2mea *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t3l2mea *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9camnq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_j9camnq F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_d0lfl1k *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_d0lfl1k *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_d0lfl1k *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jmf3ip7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_jmf3ip7 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dg0m8l8 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dg0m8l8 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dg0m8l8 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k4qgi5h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_k4qgi5h F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dn5pqnq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dn5pqnq *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dn5pqnq *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k9j41lp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_k9j41lp F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dhjqjjt *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dhjqjjt *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dhjqjjt *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kfov7le(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_kfov7le F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dvcvfgj *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dvcvfgj *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dvcvfgj *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kplljv5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_kplljv5 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_daod35i *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_daod35i *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_daod35i *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ktkp9ie(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ktkp9ie F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_d6fj9tl *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_d6fj9tl *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_d6fj9tl *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l4n8i44(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_l4n8i44 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t2lplfr *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t2lplfr *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t2lplfr *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_llf386l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_llf386l F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_d04m6io *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_d04m6io *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_d04m6io *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m1o2jhc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_m1o2jhc F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_duvvd80 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_duvvd80 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_duvvd80 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_met4j5v(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_met4j5v F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t472cpe *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t472cpe *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t472cpe *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mjfoigj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_mjfoigj F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tn4egi5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tn4egi5 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tn4egi5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mvtglrm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_mvtglrm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dp1qh3k *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dp1qh3k *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dp1qh3k *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nfe1f9q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_nfe1f9q F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t3f3kj5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t3f3kj5 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t3f3kj5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_njvtn59(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_njvtn59 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dd3t0vb *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dd3t0vb *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dd3t0vb *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ob47von(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ob47von F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_drnt54k *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_drnt54k *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_drnt54k *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_obu2gu3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_obu2gu3 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dtmr1c2 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dtmr1c2 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dtmr1c2 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_of719hm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_of719hm F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t4gbkjg *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t4gbkjg *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t4gbkjg *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oh96lv6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_oh96lv6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ddl06ea *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ddl06ea *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ddl06ea *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ok83ap3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ok83ap3 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dr8c0gq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dr8c0gq *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dr8c0gq *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p8oa45s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_p8oa45s F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tjqc107 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tjqc107 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tjqc107 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pglbpad(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pglbpad F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_d5h9av5 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_d5h9av5 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_d5h9av5 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pm0r0gg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pm0r0gg F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_de12mga *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_de12mga *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_de12mga *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pqvj985(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_pqvj985 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tb06a6e *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tb06a6e *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tb06a6e *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ptsem8q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_ptsem8q F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dd6d4bt *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dd6d4bt *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dd6d4bt *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q87drk0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_q87drk0 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_davqh84 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_davqh84 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_davqh84 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r93psta(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_r93psta F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tsutrj7 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tsutrj7 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tsutrj7 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rdnojv6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_rdnojv6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t04dp7b *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t04dp7b *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t04dp7b *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s5945uf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_s5945uf F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_d7osfot *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_d7osfot *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_d7osfot *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_shchrt8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_shchrt8 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tv3i6qj *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tv3i6qj *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tv3i6qj *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_si15bi1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_si15bi1 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_da6unq6 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_da6unq6 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_da6unq6 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sq4r3sb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_sq4r3sb F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_td6patj *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_td6patj *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_td6patj *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_su5eab6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_su5eab6 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_ti4oun0 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_ti4oun0 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_ti4oun0 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t92bq9c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_t92bq9c F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tugmr4e *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tugmr4e *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tugmr4e *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tts9q1g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_tts9q1g F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dir32ml *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dir32ml *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dir32ml *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v14dpu2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_v14dpu2 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_t3u5vlm *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_t3u5vlm *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_t3u5vlm *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vbi5n40(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vbi5n40 F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tc7tn98 *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tc7tn98 *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tc7tn98 *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vp9f5dk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vp9f5dk F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_tcvndjh *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_tcvndjh *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_tcvndjh *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vqjh5ta(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vqjh5ta F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_dgfvefu *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_dgfvefu *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_dgfvefu *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vsotr9q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vsotr9q F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_df9v4jq *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_df9v4jq *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_df9v4jq *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vtbsvdq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_fn_vtbsvdq F;
    gm_b16_8 gm_l0; /* bytes */
    GM_STACK_CHECK(&F, 46u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = GM_LD(uint8_t, ((uint8_t *)&((struct gm_fr_format_d2d7bae *)((&F.h)->up))->l2));
        uint8_t t4 = (uint8_t)!t3;
        if (!t4) goto ie2;
        {
            void * t6 = (void *)((uint8_t *)&((struct gm_fr_format_d2d7bae *)((&F.h)->up))->l1);
            gm_b16_8 t7 = gm_l0;
            gm_tftb1odc739t5 t8 = gm_f_extend_0cs85gs(t6, t7);
            gm_tftb1odc739t5 t9 = t8;
            gm_b0_1 t10;
            if (!((((uint8_t *)&t9))[0] != 0)) {
                gm_b0_1 t11 = GM_LD(gm_b0_1, (((uint8_t *)&t9) + 1));
                t10 = t11;
            } else {
                {
                    GM_ST(uint8_t, ((uint8_t *)&((struct gm_fr_format_d2d7bae *)((&F.h)->up))->l2), ((uint8_t)1));
                }
                t10 = GM_ZERO(gm_b0_1);
            }
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
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_05ojb2u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0a9g03n(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_0t1ag66(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_1obfloe(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_2g2eiv7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_3rspl53(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4ffciir(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4lcplfd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4tf2ot2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_4tfjk3f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_51kjin8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_543a87u(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5bfs7qc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_5ib6bd8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_6f1gf3a(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_728pqq7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_77nmu40(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_7qeivkm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8cqj68l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_8mh6knp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_904hlgo(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_937s740(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_98g0vtt(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9cg7es9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_9dmltq8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a4ftpa8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a7rjjrm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_a7v6hmb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aieghgq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_aj2h13f(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b410aaq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_b55ja8s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_chvm7s9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_cul2d9o(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_d4uo89t(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_d6e80g1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dcje4d7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_di6bt53(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_dncn65l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e4nkn50(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_e9652ut(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ec7eir4(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_enpptps(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f2cbcor(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_f7ktlj1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fe6g8l3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fljhc60(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_fpsbucs(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ft417o7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_gb3qal8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_god4jpr(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h0erin9(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_h3fae34(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_i88qt72(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_iai6vb1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_idv1c0k(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_igffqdd(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ivdb044(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j0o76ud(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_j9camnq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_jmf3ip7(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k4qgi5h(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_k9j41lp(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kfov7le(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_kplljv5(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ktkp9ie(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_l4n8i44(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_llf386l(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_m1o2jhc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_met4j5v(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mjfoigj(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_mvtglrm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_nfe1f9q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_njvtn59(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ob47von(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_obu2gu3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_of719hm(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_oh96lv6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ok83ap3(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_p8oa45s(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pglbpad(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pm0r0gg(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_pqvj985(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_ptsem8q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_q87drk0(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_r93psta(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_rdnojv6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_s5945uf(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_shchrt8(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_si15bi1(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_sq4r3sb(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_su5eab6(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_t92bq9c(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_tts9q1g(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_v14dpu2(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vbi5n40(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vp9f5dk(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vqjh5ta(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vsotr9q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
extern __attribute__((aligned(4))) gm_b0_1 gm_f_fn_vtbsvdq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
inline __attribute__((aligned(4))) void gm_f_format_d04m6io(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_d04m6io F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_si7vj1dcaqhfd));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_llf386l, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sk754mj3p1el6, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s2053hhvf16fm, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_d0bas9o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_d0bas9o F;
    GM_STACK_CHECK(&F, 417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn048vvii9ap5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(82));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)64ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0uuhaljsf410 t18 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_dncn65l, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_smh12ad93nju6, 44)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s23t712hkqlmp, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_ssbqeok4607ju, 24)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_d0lfl1k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_d0lfl1k F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_stoidbi3vfa99));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(29));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_j9camnq, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_snqmbfl2fr85m, 16)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s24s9g8r9gc0v, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_d277g96(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_d277g96 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so77flgm8klar));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ec7eir4, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_suekibjsm2ees, 33)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s407iureo3mia, 16)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_d2d7bae(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_d2d7bae F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s14lpmr3v2vk3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_vtbsvdq, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sgks95jf6fs73, 20)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_shv4snn6f8hcr, 28)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_d38c0qs(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_d38c0qs F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2qcdb7brsccv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(122));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_9dmltq8, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sla366gd7nphd, 57)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s9p37ubghllhn, 63)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_d3cmh7d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_d3cmh7d F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sijeantqk7jl2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_h3fae34, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_st52k2850n5r5, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_d5h9av5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_d5h9av5 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_squ7ob8ltdkn4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_pglbpad, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_ss6k80g3hocsv, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s2e0u34gjtsqn, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_d613p2h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_d613p2h F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sm884rqp4fp7u));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(67));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_3rspl53, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sodgjk6pvm3b4, 20)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s5el4r6boe881, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tybihgc69jv3, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_smdjnv4d2cfrh, 30)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_d6fj9tl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_d6fj9tl F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc6pb1tb5tnnv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ktkp9ie, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s948vtbtkq957, 31)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_d6hu3gb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_d6hu3gb F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sv8lr20bqpljh));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_543a87u, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s0ibfi39mcc7n, 8)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_d7osfot(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_d7osfot F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr4vl7ov3g7t6));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(54));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_s5945uf, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sqdqshat9srld, 36)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_ssb6m4k22290v, 16)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_d801g52(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_d801g52 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4ehfliahvei9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_1obfloe, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_st3ja0nqi37bv, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_da6unq6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_da6unq6 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su1mil1q3eqgp));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(15));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_si15bi1, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s6peoato8pau5, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_da7bimt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_da7bimt F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9mhd8h7bolj3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_05ojb2u, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sjaeqf6f3s51a, 44)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_daod35i(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_daod35i F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_setftr5leknrn));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(22));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_kplljv5, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s6iln1emufss5, 19)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_davqh84(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_davqh84 F;
    GM_STACK_CHECK(&F, 417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s06nnq6fe98bg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(52));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)64ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0uuhaljsf410 t18 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_q87drk0, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sgip9ekbho6mc, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s2g9gpcumahrc, 23)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_spf5krhuq011v, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_dbdhrrf(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dbdhrrf F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s147lrab6u68i));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_igffqdd, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfjc41f66fqb2, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s05336tfii06a, 17)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_dcns5hm(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dcns5hm F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb2k9e71jsraf));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_0t1ag66, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tybihgc69jv3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tybihgc69jv3, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_dd3t0vb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dd3t0vb F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn5ntorhqodmg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_njvtn59, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sb26s4qotngkv, 26)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sn5pv7k63ni4c, 10)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_dd6d4bt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_dd6d4bt F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9nl6jkck8c25));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ptsem8q, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s9e7jg1nv469k, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_dd92ata(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dd92ata F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sa9ukgikn2pki));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(25));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_f7ktlj1, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sjbtfuea8qauf, 22)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_ddl06ea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ddl06ea F;
    GM_STACK_CHECK(&F, 439u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smuti066jdthl));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(23));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)80ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t2t9klu4dfekp t18 = GM_LD(gm_t2t9klu4dfekp, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_oh96lv6, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 0));
            uint64_t t21 = t20;
            uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 8));
            uint8_t t23 = t22;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 16));
            uint64_t t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 24));
            gm_b16_8 t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
            gm_b16_8 t29 = t28;
            uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 56));
            uint8_t t31 = t30;
            uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 57));
            uint8_t t33 = t32;
            gm_b16_8 t34 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 64));
            gm_b16_8 t35 = t34;
            gm_closure t36 = t19;
            gm_node *ccx37 = t36.cx;
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_st3ja0nqi37bv, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_ty929lennfd3, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_st3ja0nqi37bv, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_tyb2nno7n67r, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_st3ja0nqi37bv, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_st3ja0nqi37bv, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_ty0iqiubfm7e, ((uint8_t *)&t29), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_st3ja0nqi37bv, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_ty929lennfd3, ((uint8_t *)&t31), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_st3ja0nqi37bv, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_ty929lennfd3, ((uint8_t *)&t33), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t36.code, t36.env, ccx37, gm_st3ja0nqi37bv, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t36.code, t36.env, ccx37, &gm_ty0iqiubfm7e, ((uint8_t *)&t35), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t39 = F.l2;
        if (!t39) goto ie38;
        {
            void * t41 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t41);
        }
        {
            gm_t9qkh0rsooc8d t42;
            GM_ST(uint8_t, ((uint8_t *)&t42 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t43;
            GM_ST(uint8_t, (uint8_t *)&t43 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t43 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t43 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t43 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t43 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t43 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t43 + 16, 0);
            ((uint8_t *)&t43)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t43 + 8), t42);
            gm_tabb710d9mq2e t44 = t43;
            uint8_t t45 = ((((uint8_t *)&t44))[0] != 0);
            F.ret = t43;
            goto gm_fret;
        }
        goto ix38;
ie38:;
ix38:;
    }
    {
        void * t46 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t46);
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
inline __attribute__((aligned(4))) void gm_f_format_ddrb17v(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_ddrb17v F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqqaphhmqv39s));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_8cqj68l, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sdvp5f4riom9f, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_de12mga(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t19enj90vg8bd a0) {
    struct gm_fr_format_de12mga F;
    gm_t19enj90vg8bd gm_l0; /* args */
    gm_t19enj90vg8bd gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spbdk1kpko0k2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(140));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t19enj90vg8bd t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_pm0r0gg, (void *)&F.h, cx});
            int32_t t20 = GM_LD(int32_t, (((uint8_t *)&gm_l3) + 0));
            int32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s9lpec5ab0tsm, 110)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyqa6gfa8omk, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sp0doh9njf7i3, 26)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_df9v4jq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0) {
    struct gm_fr_format_df9v4jq F;
    gm_t9qkh0rsooc8d gm_l0; /* args */
    gm_t9qkh0rsooc8d gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 230u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbeoq5iu4cto8));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)1ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t9qkh0rsooc8d t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_vsotr9q, (void *)&F.h, cx});
            uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
            uint8_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s78lhtg84qh95, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty929lennfd3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_dg0m8l8(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dg0m8l8 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_si731m8b0sr6s));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_jmf3ip7, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_spod3tmkh25up, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_dgfvefu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dgfvefu F;
    gm_t3877pk7lv748 gm_l0; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5l9jdmrtaean));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t18 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_vqjh5ta, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sj815koajgkmb, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfdbfrv46lihd, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty6groq37ram, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_dhjqjjt(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dhjqjjt F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5i2d5hm88htd));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_k9j41lp, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_srs1vrn2uaams, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_dir32ml(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dir32ml F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sf1hl3n20st3g));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_tts9q1g, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s2vme195ddtm7, 24)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s80tqs0e3u7de, 10)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_djdpqv2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_djdpqv2 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0unahdn5j99v));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_4tf2ot2, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_scc6ms0hrfo8j, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_dl7scf9(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dl7scf9 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss3f6irsroj9l));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(36));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_b55ja8s, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sa525m041ir3n, 16)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sumvvvaqn43sl, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sb6c4qgs6rpfi, 7)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_dlemvkb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dlemvkb F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd70ob31745fn));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_iai6vb1, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_skolmnfc0ccbd, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_dmukbli(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dmukbli F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4du6i4h6s7qo));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_2g2eiv7, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sbgdbpkf33nsc, 29)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_dn5pqnq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_dn5pqnq F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssift49n290vj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_k4qgi5h, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s5c0ruf0vg8mu, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_dp1qh3k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dp1qh3k F;
    gm_tgbgti82mf879 gm_l0; /* args */
    gm_tgbgti82mf879 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ss27lm4pmhcij));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(44));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tgbgti82mf879 t18 = GM_LD(gm_tgbgti82mf879, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_mvtglrm, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t23 = t22;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 24));
            uint64_t t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_so9fi6r28770a, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sq6v0u0m2nfv8, 22)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tyb2nno7n67r, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_skqf89fdi6p8e, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tyb2nno7n67r, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sn1seggk13bop, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_dr0nbiu(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dr0nbiu F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ste817enr6egu));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(94));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_a7v6hmb, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sglu5epjv38rl, 91)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_dr7jgt6(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dr7jgt6 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2sfjihvaabi3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_728pqq7, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sd4sd2h1n94bh, 6)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_dr8c0gq(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_dr8c0gq F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s94bv82atjueb));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ok83ap3, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s7gn2sla7drif, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_drnt54k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_drnt54k F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s71l0hs29ihls));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(38));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ob47von, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s4l676io35g5u, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sql1qe620spbq, 22)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_dsemf9s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dsemf9s F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_siqc5cjnsa99v));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_fljhc60, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s9uc7or8diipg, 34)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_dsta2ql(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dsta2ql F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sb2k9e71jsraf));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_i88qt72, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_dtmr1c2(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dtmr1c2 F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    gm_tf1bjg81u0glv gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s81dvppjuktle));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tf1bjg81u0glv t18 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_obu2gu3, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_ssud7c614kqjt, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_du48r1i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_du48r1i F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sub9rknupld7d));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(51));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_0a9g03n, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_scch7vqldh7aj, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s61i2o7ldibjh, 34)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_du76tsh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_du76tsh F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_scl6is0vlr1m0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(61));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_9cg7es9, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_suc4jg0r7eog6, 58)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_ducddq0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ducddq0 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sakto9q5516h3));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_51kjin8, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s0ek1hibl01t2, 46)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_dukk0t3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_dukk0t3 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sdf1o8jga4hc6));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_god4jpr, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sf1v2j94ne72t, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s52od60hi0cpi, 20)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_duvvd80(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_duvvd80 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5c6gmkd367jj));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(45));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_m1o2jhc, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s24mnh1a25k0f, 30)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_st2ertk6tjva7, 13)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_dv5f37i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dv5f37i F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_smd81jcp0bga9));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_a4ftpa8, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s34hhp1ucrags, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_shi7nidbc64qf, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_dvcii4b(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dvcii4b F;
    GM_STACK_CHECK(&F, 417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s79r5ktousup8));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(156));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)64ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0uuhaljsf410 t18 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_e4nkn50, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_slqq08j0fro21, 30)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s41bap0m04tt9, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_svgk3n4sk1uat, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sachcj4qd96of, 47)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_so2blaqutst8g, 52)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_dvcvfgj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_dvcvfgj F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    gm_tf1bjg81u0glv gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so1us53sh0cre));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(43));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tf1bjg81u0glv t18 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_kfov7le, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sghk06svnbp7m, 39)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_t00o2ec(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t00o2ec F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg50kn7lo9hgn));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(85));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_7qeivkm, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s3t2d4i4t34ct, 73)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sp7cdd0n91vli, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_t04dp7b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_t04dp7b F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sbol12a8gjr17));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_rdnojv6, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_ss7pu45qp7oic, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sm5be4659ulci, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_t0vntpb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_t0vntpb F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sus38q16nfojv));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(6));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_cul2d9o, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s6i2cgc2057sp, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_t2lplfr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_t2lplfr F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skqb5p67mhjp2));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(42));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_l4n8i44, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sk2dpusgkm375, 21)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_smbjf525bk9a0, 19)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_t32jdqr(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_t32jdqr F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sn1puqmdafub0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_4tfjk3f, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s1i6nhfib8r9u, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_t3f3kj5(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t3f3kj5 F;
    GM_STACK_CHECK(&F, 489u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_seaesh4v4toj4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(75));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)88ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tjftu5su6fh8e t18 = GM_LD(gm_tjftu5su6fh8e, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_nfe1f9q, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 64));
            gm_b16_8 t29 = t28;
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 80));
            uint64_t t31 = t30;
            gm_closure t32 = t19;
            gm_node *ccx33 = t32.cx;
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_sm55k23067j17, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_s24s9g8r9gc0v, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_s23t712hkqlmp, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_s3brf0f9sa1j7, 25)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_sub7l8h1sjc33, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t29), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_s39mljh669qgl, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty6groq37ram, ((uint8_t *)&t31), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t35 = F.l2;
        if (!t35) goto ie34;
        {
            void * t37 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t37);
        }
        {
            gm_t9qkh0rsooc8d t38;
            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t39 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t39 + 16, 0);
            ((uint8_t *)&t39)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t39 + 8), t38);
            gm_tabb710d9mq2e t40 = t39;
            uint8_t t41 = ((((uint8_t *)&t40))[0] != 0);
            F.ret = t39;
            goto gm_fret;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        void * t42 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t42);
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
inline __attribute__((aligned(4))) void gm_f_format_t3l2mea(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t3l2mea F;
    gm_tommf4j9upomc gm_l0; /* args */
    gm_tommf4j9upomc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spf3a0kmdpgm5));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(5));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tommf4j9upomc t18 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_j0o76ud, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 16));
            uint32_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_seccpktgmrevt, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty467d5cm1d5, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_t3u5vlm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_t3u5vlm F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slj7i6lishtfg));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(46));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_v14dpu2, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_spicth2mans91, 43)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_t472cpe(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t472cpe F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_squ601q9vv2so));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_met4j5v, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sauf6ibu7mnba, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s3e9133b3pasn, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sj6enltl2qi3c, 20)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_t4gbkjg(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t4gbkjg F;
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sd7spmqdlcohl));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(24));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)40ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tftksr4op114e t18 = GM_LD(gm_tftksr4op114e, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_of719hm, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 32));
            uint64_t t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s2kieedtscsk1, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_stc3e0j0moc2h, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tyb2nno7n67r, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sqvepl1893o5g, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_t4ivgd1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_t4ivgd1 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s0irgfjsc3pat));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(75));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_fe6g8l3, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s1smed2pig5ts, 34)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), &(gm_spec){32, 0, 0, 0, 1, 2, 4u, -1})) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sb7hastj3mkf0, 35)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_t5fmuah(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t5fmuah F;
    GM_STACK_CHECK(&F, 417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_seidpmiq51gtp));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(59));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)64ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0uuhaljsf410 t18 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_idv1c0k, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sb2nfo787ropg, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sq68mfjf87n5h, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sbvet41ukod2k, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sqslcbfraugjg, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sfg1v6bqcbbav, 9)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_t68frbc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t68frbc F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9jqgpofojve4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(34));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_8mh6knp, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s3mbo1s262isg, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s148evlodt315, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_t6rdn7r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t6rdn7r F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2l500n2vri8f));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(76));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_937s740, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sm55k23067j17, 15)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s24s9g8r9gc0v, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s89gtqa9dnss0, 34)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sa39uag0oed9a, 15)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_t725p24(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_t725p24 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sr87tgv3ltq65));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(12));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_aieghgq, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_si5gf6931vagv, 10)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_t8lugsu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t8lugsu F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sl1botv4tp1fk));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(57));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_a7rjjrm, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_ssr6vk8erbj18, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sv3gqbo7rudsj, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s35u3nlsc2qa0, 26)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_t8pf89n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t8pf89n F;
    gm_tf1bjg81u0glv gm_l0; /* args */
    gm_tf1bjg81u0glv gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_setofg57no9qt));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(33));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tf1bjg81u0glv t18 = GM_LD(gm_tf1bjg81u0glv, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_98g0vtt, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 8));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_spgt2nbblhu8j, 8)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_st6hj8v0h80gt, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_ssjhh9bdn23jf, 16)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_t9gsufu(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_t9gsufu F;
    GM_STACK_CHECK(&F, 489u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_st3c9ev3jkdrf));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(31));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)88ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tjftu5su6fh8e t18 = GM_LD(gm_tjftu5su6fh8e, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_chvm7s9, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 64));
            gm_b16_8 t29 = t28;
            uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 80));
            uint64_t t31 = t30;
            gm_closure t32 = t19;
            gm_node *ccx33 = t32.cx;
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_si4l87dl4tpkq, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_ssk4a01soguqj, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_tybihgc69jv3, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty0iqiubfm7e, ((uint8_t *)&t29), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t32.code, t32.env, ccx33, &gm_ty6groq37ram, ((uint8_t *)&t31), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t32.code, t32.env, ccx33, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t35 = F.l2;
        if (!t35) goto ie34;
        {
            void * t37 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t37);
        }
        {
            gm_t9qkh0rsooc8d t38;
            GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t39;
            GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t39 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t39 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t39 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t39 + 16, 0);
            ((uint8_t *)&t39)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t39 + 8), t38);
            gm_tabb710d9mq2e t40 = t39;
            uint8_t t41 = ((((uint8_t *)&t40))[0] != 0);
            F.ret = t39;
            goto gm_fret;
        }
        goto ix34;
ie34:;
ix34:;
    }
    {
        void * t42 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t42);
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
inline __attribute__((aligned(4))) void gm_f_format_ta7nl7k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ta7nl7k F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skmg81d57fq1r));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(30));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_5bfs7qc, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sqi8fcv4bi7s6, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s8de5r2vp8807, 19)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_tat75d3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_tat75d3 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s6p5i58jsib94));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_4ffciir, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyb2nno7n67r, ((uint8_t *)&t21), &(gm_spec){32, 0, 0, 0, 1, 1, 16u, -1})) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_tb06a6e(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tb06a6e F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8fq66mhsgcib));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(103));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_pqvj985, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_si0b5q45ot754, 100)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_tbdgr6h(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tbdgr6h F;
    GM_STACK_CHECK(&F, 441u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgme8q1t414rd));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(18));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)72ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t89vrq05jgle5 t18 = GM_LD(gm_t89vrq05jgle5, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_e9652ut, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 48));
            uint64_t t27 = t26;
            gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 56));
            gm_b16_8 t29 = t28;
            gm_closure t30 = t19;
            gm_node *ccx31 = t30.cx;
            if (gm_pfk8csn7us23(t30.code, t30.env, ccx31, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t30.code, t30.env, ccx31, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t30.code, t30.env, ccx31, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t30.code, t30.env, ccx31, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t30.code, t30.env, ccx31, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t30.code, t30.env, ccx31, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t30.code, t30.env, ccx31, &gm_ty6groq37ram, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t30.code, t30.env, ccx31, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t30.code, t30.env, ccx31, &gm_ty0iqiubfm7e, ((uint8_t *)&t29), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t30.code, t30.env, ccx31, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t33 = F.l2;
        if (!t33) goto ie32;
        {
            void * t35 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t35);
        }
        {
            gm_t9qkh0rsooc8d t36;
            GM_ST(uint8_t, ((uint8_t *)&t36 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t37;
            GM_ST(uint8_t, (uint8_t *)&t37 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t37 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t37 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t37 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t37 + 16, 0);
            ((uint8_t *)&t37)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t37 + 8), t36);
            gm_tabb710d9mq2e t38 = t37;
            uint8_t t39 = ((((uint8_t *)&t38))[0] != 0);
            F.ret = t37;
            goto gm_fret;
        }
        goto ix32;
ie32:;
ix32:;
    }
    {
        void * t40 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t40);
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
inline __attribute__((aligned(4))) void gm_f_format_tbvfjdi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_tbvfjdi F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s5llvatkc5h5i));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_b410aaq, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_spp0f644is6jb, 11)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyb2nno7n67r, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_tc7tn98(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_tc7tn98 F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_so31m6feg820p));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(3));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_vbi5n40, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_slq58vfha8k9h, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_tcr2jpk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tcr2jpk F;
    gm_t3877pk7lv748 gm_l0; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgmjl48sjjf69));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(21));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t18 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_f2cbcor, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s5pra70i8o734, 13)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s0m0qaokk51g0, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tyb2nno7n67r, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_tcvndjh(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_tcvndjh F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_su6s568b7b4kr));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_vp9f5dk, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sjkqdj35ksd9p, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_td6patj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t4ce0jd3tdro4 a0) {
    struct gm_fr_format_td6patj F;
    gm_t4ce0jd3tdro4 gm_l0; /* args */
    gm_t4ce0jd3tdro4 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_snuinkttlbd28));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t4ce0jd3tdro4 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_sq4r3sb, (void *)&F.h, cx});
            uint64_t t20 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 0));
            uint64_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty6groq37ram, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_snupav63sairp, 11)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_te2qqk4(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_te2qqk4 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sedq394prkqe4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(9));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_gb3qal8, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sulgkejn2inql, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_tffhbfn(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tffhbfn F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_saq7q763oq882));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(47));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_904hlgo, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_simmbi8o1lo4k, 45)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_tg9hp73(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tg9hp73 F;
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_slcgg5l8i9236));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)56ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tu8mt0h8clq1l t18 = GM_LD(gm_tu8mt0h8clq1l, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_77nmu40, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            uint32_t t24 = GM_LD(uint32_t, (((uint8_t *)&F.l3) + 32));
            uint32_t t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 40));
            gm_b16_8 t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_shhn92c601tgo, 28)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sg5gdnrith2i6, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sdupocj1qjnur, 6)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty467d5cm1d5, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_stc3e0j0moc2h, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_thddtj7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_thddtj7 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s336v1oga58d4));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(40));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_d4uo89t, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_ssfs2ebod65fl, 37)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_thmq1t9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_thmq1t9 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sloe55935mobb));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_d6e80g1, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sj21lfdl9q0t4, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_ti4oun0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_ti4oun0 F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssa0pabeurppq));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(19));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_su5eab6, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_soa9mnl4ahjud, 7)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_sgmhc3klfmn46, 4)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tybihgc69jv3, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_tybihgc69jv3, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_ti6vs6t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_ti6vs6t F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg0jqm2pij3el));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(77));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_dcje4d7, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sbp9encpv0e61, 74)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_tjkla39(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tjkla39 F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_skr00pujn28ln));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(28));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_di6bt53, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sve2u8a7eapjo, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tybihgc69jv3, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sno069vrt5g9q, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s6t4r831mukdr, 2)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_tjld97n(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tjld97n F;
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sqnathr6pbque));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(37));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)40ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tcir0vf4307lp t18 = GM_LD(gm_tcir0vf4307lp, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_aj2h13f, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            double t22 = GM_LD(double, (((uint8_t *)&F.l3) + 16));
            double t23 = t22;
            uint64_t t24 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 24));
            uint64_t t25 = t24;
            uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&F.l3) + 32));
            uint64_t t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sslm2oq18gln3, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_tyqqajgod2bb, ((uint8_t *)&t23), &(gm_spec){32, 0, 0, 0, 0, 0, 0u, 1})) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_sfkrpkv21b1rq, 3)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty6groq37ram, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_ssnku2e1q7u3k, 12)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_tyb2nno7n67r, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s3ss0hcg1h7js, 6)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_tjqc107(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tjqc107 F;
    GM_STACK_CHECK(&F, 369u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s4o541hcfllua));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(8));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)48ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tfeab279c16rr t18 = GM_LD(gm_tfeab279c16rr, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_p8oa45s, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_closure t26 = t19;
            gm_node *ccx27 = t26.cx;
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s2u5posfkbgnq, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t26.code, t26.env, ccx27, gm_s2u5posfkbgnq, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t26.code, t26.env, ccx27, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t29 = F.l2;
        if (!t29) goto ie28;
        {
            void * t31 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t31);
        }
        {
            gm_t9qkh0rsooc8d t32;
            GM_ST(uint8_t, ((uint8_t *)&t32 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t33;
            GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t33 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t33 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t33 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t33 + 16, 0);
            ((uint8_t *)&t33)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t33 + 8), t32);
            gm_tabb710d9mq2e t34 = t33;
            uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
            F.ret = t33;
            goto gm_fret;
        }
        goto ix28;
ie28:;
ix28:;
    }
    {
        void * t36 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t36);
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
inline __attribute__((aligned(4))) void gm_f_format_tn4egi5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tn4egi5 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_ssdho4pv9rajn));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(54));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_mjfoigj, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sql9r2b8fvivt, 9)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s0eudsv4chfi5, 43)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_tovpseb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tovpseb F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sg25rp8qmrbme));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_5ib6bd8, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s66fbu3uhl900, 19)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_secneq49c8dtf, 35)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_tppjqmg(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tppjqmg F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s9dudft75oi1c));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(32));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_6f1gf3a, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_stkm2m8f3okk8, 17)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_s03itq0fotubb, 13)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_tra8sop(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tra8sop F;
    GM_STACK_CHECK(&F, 417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_spgnoteg4vcil));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)64ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0uuhaljsf410 t18 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_4lcplfd, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_spod3tmkh25up, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_tsutrj7(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tsutrj7 F;
    gm_tommf4j9upomc gm_l0; /* args */
    gm_tommf4j9upomc gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 337u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sgj6i64633q0l));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(10));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tommf4j9upomc t18 = GM_LD(gm_tommf4j9upomc, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_r93psta, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint32_t t22 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 16));
            uint32_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sviu2hfr78esg, 5)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_sfdbfrv46lihd, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty467d5cm1d5, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_tt07921(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tt07921 F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_svdcmt7tps0ma));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(56));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ft417o7, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_shl0ucmfrvk77, 53)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_tt4n370(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tt4n370 F;
    gm_t3877pk7lv748 gm_l0; /* args */
    gm_t3877pk7lv748 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 345u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sc48cegnkinfm));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(11));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)24ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t3877pk7lv748 t18 = GM_LD(gm_t3877pk7lv748, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_h0erin9, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            uint64_t t22 = GM_LD(uint64_t, (((uint8_t *)&gm_l3) + 16));
            uint64_t t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_so9fi6r28770a, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_tyb2nno7n67r, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_subcm6amchfvv, 4)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_tth3l1d(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tth3l1d F;
    GM_STACK_CHECK(&F, 417u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sjl0s4joed61f));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(14));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)64ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t0uuhaljsf410 t18 = GM_LD(gm_t0uuhaljsf410, ((uint8_t *)a0));
            F.l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_fpsbucs, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 16));
            gm_b16_8 t23 = t22;
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 32));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)&F.l3) + 48));
            gm_b16_8 t27 = t26;
            gm_closure t28 = t19;
            gm_node *ccx29 = t28.cx;
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_smooaouqmm8dj, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_srv4dcfccuset, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t25), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s3p0rbs9hc3lg, 1)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t28.code, t28.env, ccx29, &gm_ty0iqiubfm7e, ((uint8_t *)&t27), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t28.code, t28.env, ccx29, gm_s1asl1e1tvb0k, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t31 = F.l2;
        if (!t31) goto ie30;
        {
            void * t33 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t33);
        }
        {
            gm_t9qkh0rsooc8d t34;
            GM_ST(uint8_t, ((uint8_t *)&t34 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t35;
            GM_ST(uint8_t, (uint8_t *)&t35 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t35 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t35 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t35 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t35 + 16, 0);
            ((uint8_t *)&t35)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t35 + 8), t34);
            gm_tabb710d9mq2e t36 = t35;
            uint8_t t37 = ((((uint8_t *)&t36))[0] != 0);
            F.ret = t35;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t38 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t38);
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
inline __attribute__((aligned(4))) void gm_f_format_tttig7s(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tidu2er0d7h95 a0) {
    struct gm_fr_format_tttig7s F;
    gm_tidu2er0d7h95 gm_l0; /* args */
    gm_tidu2er0d7h95 gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 265u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s2a85dvpmjp4u));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(13));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)8ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tidu2er0d7h95 t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_enpptps, (void *)&F.h, cx});
            double t20 = GM_LD(double, (((uint8_t *)&gm_l3) + 0));
            double t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_tyqqajgod2bb, ((uint8_t *)&t21), &(gm_spec){32, 0, 0, 0, 0, 0, 0u, 3})) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sesqjijoh7km4, 8)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_tugmr4e(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_format_tugmr4e F;
    gm_t6j6smj7qoctu gm_l0; /* args */
    gm_t6j6smj7qoctu gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_uvfu1k1((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sahjmrj1mjf77));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(7));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)32ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t6j6smj7qoctu t18 = GM_LD(gm_t6j6smj7qoctu, ((uint8_t *)a0));
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_t92bq9c, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_b16_8 t22 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 16));
            gm_b16_8 t23 = t22;
            gm_closure t24 = t19;
            gm_node *ccx25 = t24.cx;
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t24.code, t24.env, ccx25, &gm_ty0iqiubfm7e, ((uint8_t *)&t23), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t24.code, t24.env, ccx25, gm_s0ucactj6hj0a, 3)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t27 = F.l2;
        if (!t27) goto ie26;
        {
            void * t29 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t29);
        }
        {
            gm_t9qkh0rsooc8d t30;
            GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t31;
            GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t31 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t31 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t31 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t31 + 16, 0);
            ((uint8_t *)&t31)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t31 + 8), t30);
            gm_tabb710d9mq2e t32 = t31;
            uint8_t t33 = ((((uint8_t *)&t32))[0] != 0);
            F.ret = t31;
            goto gm_fret;
        }
        goto ix26;
ie26:;
ix26:;
    }
    {
        void * t34 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t34);
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
inline __attribute__((aligned(4))) void gm_f_format_tv3i6qj(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0) {
    struct gm_fr_format_tv3i6qj F;
    gm_tnpifv20irnph gm_l0; /* args */
    gm_tnpifv20irnph gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 305u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_s8075t766i1f0));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(49));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)16ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_tnpifv20irnph t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_shchrt8, (void *)&F.h, cx});
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l3) + 0));
            gm_b16_8 t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sj9t4d2ku5m56, 46)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty0iqiubfm7e, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sfm9elkq6bn2e, 1)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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
inline __attribute__((aligned(4))) void gm_f_format_tvr4di5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0) {
    struct gm_fr_format_tvr4di5 F;
    gm_t88rcucg1b7uk gm_l0; /* args */
    gm_t88rcucg1b7uk gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 245u);
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
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3;
        GM_ST(void *, (uint8_t *)&t3, (void *)(gm_sukriak12588g));
        GM_ST(uintptr_t, (uint8_t *)&t3 + 8, (uintptr_t)(4));
        gm_b16_8 t4 = t3;
        uint64_t t5 = GM_LD(uintptr_t, ((uint8_t *)&t4) + 8);
        uint64_t r6; bool o7 = __builtin_add_overflow(t5, ((uint64_t)4ULL), &r6);
        if (o7) gm_trap("integer overflow in `+`", gm_src3d0v42kavhv7, 787, 15);
        uint64_t t8 = r6;
        gm_tftb1odc739t5 t9 = gm_f_reserve_d1qo8mf(t2, t8);
        gm_tftb1odc739t5 t10 = t9;
        gm_b0_1 t11;
        if (!((((uint8_t *)&t10))[0] != 0)) {
            gm_b0_1 t12 = GM_LD(gm_b0_1, (((uint8_t *)&t10) + 1));
            t11 = t12;
        } else {
            gm_t9qkh0rsooc8d t13;
            GM_ST(uint8_t, ((uint8_t *)&t13 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t14 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 16, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t14 + 8), t13);
            gm_tabb710d9mq2e t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
    }
    {
        F.l2 = ((uint8_t)0);
    }
    {
        {
            gm_t88rcucg1b7uk t18 = gm_l0;
            gm_l3 = t18;
        }
        {
            gm_closure t19 = ((gm_closure){(void *)gm_f_fn_ivdb044, (void *)&F.h, cx});
            uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 0));
            uint32_t t21 = t20;
            gm_closure t22 = t19;
            gm_node *ccx23 = t22.cx;
            if (gm_pf0aqhubr4up(t22.code, t22.env, ccx23, gm_sc4r4l9k6f577, 2)) {
                goto gm_lad;
            }
            if (gm_pfk8csn7us23(t22.code, t22.env, ccx23, &gm_ty467d5cm1d5, ((uint8_t *)&t21), NULL)) {
                goto gm_lad;
            }
        }
    }
    {
        uint8_t t25 = F.l2;
        if (!t25) goto ie24;
        {
            void * t27 = (void *)((uint8_t *)&F.l1);
            gm_f_deinit_r70pji5(t27);
        }
        {
            gm_t9qkh0rsooc8d t28;
            GM_ST(uint8_t, ((uint8_t *)&t28 + 0), ((uint8_t)0ULL));
            gm_tabb710d9mq2e t29;
            GM_ST(uint8_t, (uint8_t *)&t29 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 4, 0);
            GM_ST(uint8_t, (uint8_t *)&t29 + 9, 0);
            GM_ST(uint16_t, (uint8_t *)&t29 + 10, 0);
            GM_ST(uint32_t, (uint8_t *)&t29 + 12, 0);
            GM_ST(uint64_t, (uint8_t *)&t29 + 16, 0);
            ((uint8_t *)&t29)[0] = 1;
            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t29 + 8), t28);
            gm_tabb710d9mq2e t30 = t29;
            uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
            F.ret = t29;
            goto gm_fret;
        }
        goto ix24;
ie24:;
ix24:;
    }
    {
        void * t32 = (void *)((uint8_t *)&F.l1);
        gm_f_into_slice_hq0te56((void *)&(*(gm_tabb710d9mq2e *)gm_out), t32);
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

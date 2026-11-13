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
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t f1; } gm_tqs5p4n2tq19v;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; gm_b16_8 f3; } gm_t0uuhaljsf410;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { gm_b16_8 a[3]; } gm_t9bmh80edou9c;
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t f1; } gm_taari1s0m51a7;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { gm_b16_8 a[4]; } gm_tdmi4fk9nhqa7;
typedef struct { uint64_t f0; gm_b16_8 f1; } gm_tf1bjg81u0glv;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { gm_b16_8 f0; gm_b16_8 f1; gm_b16_8 f2; } gm_tfeab279c16rr;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint64_t f2; } gm_tftksr4op114e;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; uint8_t f2; uint8_t pe[7]; } gm_tk9n3vlila9qg;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
typedef struct { uint64_t f0; gm_b16_8 f1; uint64_t f2; } gm_tob1snuii5bme;
_Static_assert(sizeof(gm_tqs5p4n2tq19v) == 2 && _Alignof(gm_tqs5p4n2tq19v) == 1 && offsetof(gm_tqs5p4n2tq19v, f0) == 0 && offsetof(gm_tqs5p4n2tq19v, f1) == 1, "layout of `(u8, u8)`");
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint64_t f0; uint64_t f1; uint64_t f2; gm_b16_8 f3; gm_b16_8 f4; } gm_tu1089hhijadf;
_Static_assert(sizeof(gm_t0uuhaljsf410) == 64 && _Alignof(gm_t0uuhaljsf410) == 8 && offsetof(gm_t0uuhaljsf410, f0) == 0 && offsetof(gm_t0uuhaljsf410, f1) == 16 && offsetof(gm_t0uuhaljsf410, f2) == 32 && offsetof(gm_t0uuhaljsf410, f3) == 48, "layout of `(string, string, string, string)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; } f1; } gm_t6h2b31csotun;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t9bmh80edou9c) == 48 && _Alignof(gm_t9bmh80edou9c) == 8 && offsetof(gm_t9bmh80edou9c, a) == 0, "layout of `[3]string`");
_Static_assert(sizeof(gm_taari1s0m51a7) == 2 && _Alignof(gm_taari1s0m51a7) == 1 && offsetof(gm_taari1s0m51a7, f0) == 0 && offsetof(gm_taari1s0m51a7, f1) == 1, "layout of `?FloatSuffix`");
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_th2lqbo77pvsc v1; } f1; } gm_tccklsmdasd9s;
_Static_assert(sizeof(gm_tdmi4fk9nhqa7) == 64 && _Alignof(gm_tdmi4fk9nhqa7) == 8 && offsetof(gm_tdmi4fk9nhqa7, a) == 0, "layout of `[4]string`");
_Static_assert(sizeof(gm_tf1bjg81u0glv) == 24 && _Alignof(gm_tf1bjg81u0glv) == 8 && offsetof(gm_tf1bjg81u0glv, f0) == 0 && offsetof(gm_tf1bjg81u0glv, f1) == 8, "layout of `(i: usize, x: string)`");
_Static_assert(sizeof(gm_tfeab279c16rr) == 48 && _Alignof(gm_tfeab279c16rr) == 8 && offsetof(gm_tfeab279c16rr, f0) == 0 && offsetof(gm_tfeab279c16rr, f1) == 16 && offsetof(gm_tfeab279c16rr, f2) == 32, "layout of `Toolchain`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tftksr4op114e) == 40 && _Alignof(gm_tftksr4op114e) == 8 && offsetof(gm_tftksr4op114e, f0) == 0 && offsetof(gm_tftksr4op114e, f1) == 16 && offsetof(gm_tftksr4op114e, f2) == 32, "layout of `(string, string, usize)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_taari1s0m51a7 f0; gm_tqs5p4n2tq19v f1; uint8_t p2[4]; gm_th2lqbo77pvsc f2; gm_th2lqbo77pvsc f3; gm_th2lqbo77pvsc f4; gm_th2lqbo77pvsc f5; gm_th2lqbo77pvsc f6; gm_th2lqbo77pvsc f7; } gm_ti0g3bmheuq9o;
typedef struct { gm_trqfvmjtt4sij f0; } gm_tin97tt4ud0s1;
_Static_assert(sizeof(gm_tk9n3vlila9qg) == 40 && _Alignof(gm_tk9n3vlila9qg) == 8 && offsetof(gm_tk9n3vlila9qg, f0) == 0 && offsetof(gm_tk9n3vlila9qg, f1) == 16 && offsetof(gm_tk9n3vlila9qg, f2) == 32, "layout of `(lib: string, link_name: string, variadic: bool)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; int64_t v1; uint8_t v2; gm_trqfvmjtt4sij v3; void * v4; gm_b16_8 v5; } f1; } gm_tktr4rmbindgv;
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { uint64_t f0; gm_tk9n3vlila9qg f1; } gm_toa5kh9gm9738;
_Static_assert(sizeof(gm_tob1snuii5bme) == 32 && _Alignof(gm_tob1snuii5bme) == 8 && offsetof(gm_tob1snuii5bme, f0) == 0 && offsetof(gm_tob1snuii5bme, f1) == 8 && offsetof(gm_tob1snuii5bme, f2) == 24, "layout of `Reloc`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_b16_8 v1; } f1; } gm_tptmds653bni0;
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tob1snuii5bme v0; void * v1; } f1; } gm_trssmtuv8jr0v;
typedef struct { gm_b16_8 f0; gm_th2lqbo77pvsc f1; gm_th2lqbo77pvsc f2; } gm_tt008mgut8glb;
_Static_assert(sizeof(gm_tu1089hhijadf) == 56 && _Alignof(gm_tu1089hhijadf) == 8 && offsetof(gm_tu1089hhijadf, f0) == 0 && offsetof(gm_tu1089hhijadf, f1) == 8 && offsetof(gm_tu1089hhijadf, f2) == 16 && offsetof(gm_tu1089hhijadf, f3) == 24 && offsetof(gm_tu1089hhijadf, f4) == 40, "layout of `Version`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tu1089hhijadf v1; } f1; } gm_tvtl3n8qta358;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_ti0g3bmheuq9o v1; } f1; } gm_t171adjpb0m86;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_tin97tt4ud0s1 v1; } f1; } gm_t62efos6ssciq;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_ti0g3bmheuq9o f2; } gm_t6b36gvdk984b;
_Static_assert(sizeof(gm_t6h2b31csotun) == 32 && _Alignof(gm_t6h2b31csotun) == 8 && offsetof(gm_t6h2b31csotun, f0) == 0 && offsetof(gm_t6h2b31csotun, f1) == 8, "layout of `ManifestError!()`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_ti0g3bmheuq9o v1; } f1; } gm_tacj1cskem694;
_Static_assert(sizeof(gm_tccklsmdasd9s) == 32 && _Alignof(gm_tccklsmdasd9s) == 8 && offsetof(gm_tccklsmdasd9s, f0) == 0 && offsetof(gm_tccklsmdasd9s, f1) == 8, "layout of `ManifestError!?string`");
_Static_assert(sizeof(gm_ti0g3bmheuq9o) == 152 && _Alignof(gm_ti0g3bmheuq9o) == 8 && offsetof(gm_ti0g3bmheuq9o, f0) == 0 && offsetof(gm_ti0g3bmheuq9o, f1) == 2 && offsetof(gm_ti0g3bmheuq9o, f2) == 8 && offsetof(gm_ti0g3bmheuq9o, f3) == 32 && offsetof(gm_ti0g3bmheuq9o, f4) == 56 && offsetof(gm_ti0g3bmheuq9o, f5) == 80 && offsetof(gm_ti0g3bmheuq9o, f6) == 104 && offsetof(gm_ti0g3bmheuq9o, f7) == 128, "layout of `BuildSettings`");
_Static_assert(sizeof(gm_tin97tt4ud0s1) == 40 && _Alignof(gm_tin97tt4ud0s1) == 8 && offsetof(gm_tin97tt4ud0s1, f0) == 0, "layout of `SourceMap`");
_Static_assert(sizeof(gm_tktr4rmbindgv) == 48 && _Alignof(gm_tktr4rmbindgv) == 8 && offsetof(gm_tktr4rmbindgv, f0) == 0 && offsetof(gm_tktr4rmbindgv, f1) == 8, "layout of `Value`");
typedef struct { gm_th2lqbo77pvsc f0; gm_th2lqbo77pvsc f1; gm_trqfvmjtt4sij f2; gm_ti0g3bmheuq9o f3; gm_trqfvmjtt4sij f4; gm_trqfvmjtt4sij f5; } gm_tm43kqe3e8b4a;
_Static_assert(sizeof(gm_toa5kh9gm9738) == 48 && _Alignof(gm_toa5kh9gm9738) == 8 && offsetof(gm_toa5kh9gm9738, f0) == 0 && offsetof(gm_toa5kh9gm9738, f1) == 8, "layout of `(i: usize, x: LintRule)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tin97tt4ud0s1 v0; gm_b16_8 v1; gm_b16_8 v2; gm_b16_8 v3; } f1; } gm_toq2f2q4opoa3;
_Static_assert(sizeof(gm_tptmds653bni0) == 32 && _Alignof(gm_tptmds653bni0) == 8 && offsetof(gm_tptmds653bni0, f0) == 0 && offsetof(gm_tptmds653bni0, f1) == 8, "layout of `ManifestError![]const string`");
typedef struct { gm_b16_8 f0; gm_tktr4rmbindgv f1; uint64_t f2; } gm_trc3jdv2g5qre;
_Static_assert(sizeof(gm_trssmtuv8jr0v) == 40 && _Alignof(gm_trssmtuv8jr0v) == 8 && offsetof(gm_trssmtuv8jr0v, f0) == 0 && offsetof(gm_trssmtuv8jr0v, f1) == 8, "layout of `TomlError!^Table`");
_Static_assert(sizeof(gm_tt008mgut8glb) == 64 && _Alignof(gm_tt008mgut8glb) == 8 && offsetof(gm_tt008mgut8glb, f0) == 0 && offsetof(gm_tt008mgut8glb, f1) == 16 && offsetof(gm_tt008mgut8glb, f2) == 40, "layout of `UrlSource`");
_Static_assert(sizeof(gm_tvtl3n8qta358) == 64 && _Alignof(gm_tvtl3n8qta358) == 8 && offsetof(gm_tvtl3n8qta358, f0) == 0 && offsetof(gm_tvtl3n8qta358, f1) == 8, "layout of `string!Version`");
_Static_assert(sizeof(gm_t171adjpb0m86) == 160 && _Alignof(gm_t171adjpb0m86) == 8 && offsetof(gm_t171adjpb0m86, f0) == 0 && offsetof(gm_t171adjpb0m86, f1) == 8, "layout of `string!BuildSettings`");
_Static_assert(sizeof(gm_t62efos6ssciq) == 48 && _Alignof(gm_t62efos6ssciq) == 8 && offsetof(gm_t62efos6ssciq, f0) == 0 && offsetof(gm_t62efos6ssciq, f1) == 8, "layout of `string!VersionReq`");
_Static_assert(sizeof(gm_t6b36gvdk984b) == 176 && _Alignof(gm_t6b36gvdk984b) == 8 && offsetof(gm_t6b36gvdk984b, f0) == 0 && offsetof(gm_t6b36gvdk984b, f1) == 16 && offsetof(gm_t6b36gvdk984b, f2) == 24, "layout of `Profile`");
_Static_assert(sizeof(gm_tacj1cskem694) == 160 && _Alignof(gm_tacj1cskem694) == 8 && offsetof(gm_tacj1cskem694, f0) == 0 && offsetof(gm_tacj1cskem694, f1) == 8, "layout of `ManifestError!BuildSettings`");
typedef struct { gm_b16_8 f0; gm_toq2f2q4opoa3 f1; gm_th2lqbo77pvsc f2; } gm_te5jk64pjl28k;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tm43kqe3e8b4a v1; } f1; } gm_tjgmtdrk88t7m;
_Static_assert(sizeof(gm_tm43kqe3e8b4a) == 320 && _Alignof(gm_tm43kqe3e8b4a) == 8 && offsetof(gm_tm43kqe3e8b4a, f0) == 0 && offsetof(gm_tm43kqe3e8b4a, f1) == 24 && offsetof(gm_tm43kqe3e8b4a, f2) == 48 && offsetof(gm_tm43kqe3e8b4a, f3) == 88 && offsetof(gm_tm43kqe3e8b4a, f4) == 240 && offsetof(gm_tm43kqe3e8b4a, f5) == 280, "layout of `Manifest`");
_Static_assert(sizeof(gm_toq2f2q4opoa3) == 48 && _Alignof(gm_toq2f2q4opoa3) == 8 && offsetof(gm_toq2f2q4opoa3, f0) == 0 && offsetof(gm_toq2f2q4opoa3, f1) == 8, "layout of `GitSpec`");
_Static_assert(sizeof(gm_trc3jdv2g5qre) == 72 && _Alignof(gm_trc3jdv2g5qre) == 8 && offsetof(gm_trc3jdv2g5qre, f0) == 0 && offsetof(gm_trc3jdv2g5qre, f1) == 16 && offsetof(gm_trc3jdv2g5qre, f2) == 64, "layout of `Entry`");
_Static_assert(sizeof(gm_te5jk64pjl28k) == 88 && _Alignof(gm_te5jk64pjl28k) == 8 && offsetof(gm_te5jk64pjl28k, f0) == 0 && offsetof(gm_te5jk64pjl28k, f1) == 16 && offsetof(gm_te5jk64pjl28k, f2) == 64, "layout of `GitSource`");
_Static_assert(sizeof(gm_tjgmtdrk88t7m) == 328 && _Alignof(gm_tjgmtdrk88t7m) == 8 && offsetof(gm_tjgmtdrk88t7m, f0) == 0 && offsetof(gm_tjgmtdrk88t7m, f1) == 8, "layout of `ManifestError!Manifest`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_b16_8 v0; gm_te5jk64pjl28k v1; gm_tt008mgut8glb v2; } f1; } gm_tnlfbo6r4vlhg;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_tnlfbo6r4vlhg f2; } gm_t7tiv6vif86qg;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tf1bjg81u0glv v0; gm_tnlfbo6r4vlhg v1; } f1; } gm_tmn9qm2bp7f46;
_Static_assert(sizeof(gm_tnlfbo6r4vlhg) == 96 && _Alignof(gm_tnlfbo6r4vlhg) == 8 && offsetof(gm_tnlfbo6r4vlhg, f0) == 0 && offsetof(gm_tnlfbo6r4vlhg, f1) == 8, "layout of `Source`");
_Static_assert(sizeof(gm_t7tiv6vif86qg) == 120 && _Alignof(gm_t7tiv6vif86qg) == 8 && offsetof(gm_t7tiv6vif86qg, f0) == 0 && offsetof(gm_t7tiv6vif86qg, f1) == 16 && offsetof(gm_t7tiv6vif86qg, f2) == 24, "layout of `Dependency`");
_Static_assert(sizeof(gm_tmn9qm2bp7f46) == 104 && _Alignof(gm_tmn9qm2bp7f46) == 8 && offsetof(gm_tmn9qm2bp7f46, f0) == 0 && offsetof(gm_tmn9qm2bp7f46, f1) == 8, "layout of `ManifestError!Source`");
struct gm_fr_build_list_f41mb8u {
    gm_frame h;
    gm_trc3jdv2g5qre l0; /* e */
    gm_tptmds653bni0 ret;
};
struct gm_fr_build_settings_brkieib {
    gm_frame h;
    gm_t6b36gvdk984b l5; /* p */
    gm_trqfvmjtt4sij l6; /* known */
    gm_t6b36gvdk984b l9; /* p */
    gm_t171adjpb0m86 ret;
};
struct gm_fr_byte_text_9ib4q2b {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_check_subdir_2uljb06 {
    gm_frame h;
    gm_t6h2b31csotun ret;
};
struct gm_fr_debug_char_p6i205h {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_debug_escape_t8t1h0j {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_dep_string_mmgrsgi {
    gm_frame h;
    gm_tccklsmdasd9s ret;
};
struct gm_fr_for_body_vgf3305 {
    gm_frame h;
    gm_b0_1 ret;
};
struct gm_fr_is_hex_digit_q8s6utq {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_identifier_kvlcjjo {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_space_or_control_3uo6i3o {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_subdir_sjdvkdc {
    gm_frame h;
    uint8_t ret;
    gm_b0_1 w0;
};
struct gm_fr_lint_rule_o3ccae1 {
    gm_frame h;
    gm_tk9n3vlila9qg l4; /* r */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_lower_ascii_uggh3ca {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_mfail_uqq7o7g {
    gm_frame h;
    gm_tf1bjg81u0glv ret;
};
struct gm_fr_name_problem_6r4mp11 {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_normalize_url_faghdja {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_overlay_pjf5fej {
    gm_frame h;
    gm_ti0g3bmheuq9o l0; /* base */
    gm_ti0g3bmheuq9o l1; /* over */
    gm_ti0g3bmheuq9o ret;
};
struct gm_fr_parse_build_c2teosa {
    gm_frame h;
    gm_ti0g3bmheuq9o l2; /* b */
    gm_trc3jdv2g5qre l5; /* e */
    gm_tacj1cskem694 ret;
};
struct gm_fr_parse_dependency_8qmcjrm {
    gm_frame h;
    gm_tktr4rmbindgv l1; /* item */
    gm_trc3jdv2g5qre l6; /* e */
    gm_trqfvmjtt4sij l11; /* sources */
    gm_tfeab279c16rr l12; /* <tmp> */
    gm_t9bmh80edou9c l13; /* <tmp> */
    gm_t0uuhaljsf410 l18; /* <tmp> */
    gm_tdmi4fk9nhqa7 l19; /* <tmp> */
    gm_t0uuhaljsf410 l37; /* <tmp> */
    gm_tdmi4fk9nhqa7 l38; /* <tmp> */
    gm_trqfvmjtt4sij l44; /* present */
    gm_t0uuhaljsf410 l45; /* <tmp> */
    gm_tdmi4fk9nhqa7 l46; /* <tmp> */
    gm_toq2f2q4opoa3 l50; /* spec */
    gm_tin97tt4ud0s1 l53; /* req */
    gm_tmn9qm2bp7f46 ret;
};
struct gm_fr_parse_manifest_b61s3ln {
    gm_frame h;
    gm_tm43kqe3e8b4a l3; /* m */
    gm_trc3jdv2g5qre l16; /* e */
    gm_tnlfbo6r4vlhg l18; /* source */
    gm_trc3jdv2g5qre l25; /* e */
    gm_trc3jdv2g5qre l31; /* e */
    gm_ti0g3bmheuq9o l40; /* settings */
    gm_tjgmtdrk88t7m ret;
};
struct gm_fr_spec_equal_foa1umn {
    gm_frame h;
    gm_toq2f2q4opoa3 l0; /* a */
    gm_toq2f2q4opoa3 l1; /* b */
    gm_tin97tt4ud0s1 l2; /* x */
    gm_tin97tt4ud0s1 l3; /* y */
    uint8_t ret;
};
struct gm_fr_spec_text_9k2gjft {
    gm_frame h;
    gm_toq2f2q4opoa3 l0; /* s */
    gm_tin97tt4ud0s1 l1; /* r */
    gm_b16_8 ret;
};
struct gm_fr_string_array_bn13c0i {
    gm_frame h;
    gm_tktr4rmbindgv l0; /* v */
    gm_trqfvmjtt4sij l1; /* items */
    gm_trqfvmjtt4sij l2; /* out */
    gm_tktr4rmbindgv l5; /* it */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_trim_ascii_c9j36d1 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_url_problem_38kdh3a {
    gm_frame h;
    gm_th2lqbo77pvsc ret;
};
bool gm_eq0iqiubfm7e(const uint8_t *a, const uint8_t *b);
bool gm_eqjmccqdb90f(const uint8_t *a, const uint8_t *b);
__attribute__((aligned(4))) void gm_f_as_str_982o16v(void *gm_out, const void * a0);
__attribute__((aligned(4))) void * gm_f_as_table_4f7n31g(const void * a0);
__attribute__((aligned(4))) void gm_f_build_list_f41mb8u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_build_settings_brkieib(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_byte_text_9ib4q2b(void *env, gm_node *cx, gm_node *inst, uint8_t a0);
__attribute__((aligned(4))) void gm_f_check_subdir_2uljb06(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint64_t a2);
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_char_p6i205h(void *env, gm_node *cx, gm_node *inst, uint8_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_escape_t8t1h0j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_dep_string_mmgrsgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2);
__attribute__((aligned(4))) uint8_t gm_f_ends_with_biriu5l(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_0cs85gs(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vgf3305(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_format_1ncs69t(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_23qvjoj(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_3aass3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4af676s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_4eslq57(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_4pbmdvl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_55igra0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_5bo63p7(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_64tgs30(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_6cnv7j9(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_6ld92gp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_79gt991(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_7duae7m(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7p2ook8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_7sbr40s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_850ujed(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_86t06at(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_97etovl(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_9d8u2dd(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_aome4un(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_dd3t0vb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fn68qf0(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_fqmocq1(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_gs0k8gk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_h0dnf9n(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_hvnghft(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_i3a6tth(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ivhanlp(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_je4jsfp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_jh4rfvo(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jrdns83(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_jv6n7oh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_ll295o3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_llhsr0b(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_mcrh28a(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_mvlqlcp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_nll5o74(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_p73hfi0(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_p8qi5m3(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_pah1p62(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_qjfu8g6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_rlu0tid(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_rp4le08(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_uqgrksh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_usjmuj4(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void * gm_f_get_9rme0gb(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_is_control_65ubfpo(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_hex_digit_q8s6utq(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_ident_cont_317fpdb(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_ident_start_rjefpfm(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_identifier_kvlcjjo(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_reserved_type_name_tuq4qsj(gm_b16_8 a0);
__attribute__((aligned(4))) uint8_t gm_f_is_space_or_control_3uo6i3o(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_space_uen38t5(uint8_t a0);
__attribute__((aligned(4))) uint8_t gm_f_is_subdir_sjdvkdc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_join_sfknljb(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lint_rule_o3ccae1(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_list_7393u00(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_7mv9vi7(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_epvducg(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_hs66gbq(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_b16_8 gm_f_lower_ascii_uggh3ca(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_make_2ip78bj(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0, const void * a1);
__attribute__((aligned(4))) void gm_f_mfail_uqq7o7g(void *gm_out, uint64_t a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tf1bjg81u0glv gm_f_mfail_uqq7o7g_c(uint64_t a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_name_problem_6r4mp11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_normalize_url_faghdja(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_overlay_pjf5fej(void *gm_out, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_ti0g3bmheuq9o gm_f_overlay_pjf5fej_c(gm_ti0g3bmheuq9o a0, gm_ti0g3bmheuq9o a1);
__attribute__((aligned(4))) void gm_f_parse_build_c2teosa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_parse_dependency_8qmcjrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_toml_gjtmf79(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_version_9a6ns50(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_parse_version_req_alipvfi(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_3ef746b(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7hnmra3(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_dob2kfb(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_pdknc9g(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_req_equal_7ia8ip6(void * a0, void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_req_string_fk059jr(void *env, gm_node *cx, gm_node *inst, void * a0);
__attribute__((aligned(4))) uint8_t gm_f_spec_equal_foa1umn(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_spec_equal_foa1umn_c(gm_toq2f2q4opoa3 a0, gm_toq2f2q4opoa3 a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_spec_text_9k2gjft(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_split_obtinjv_s7d377q(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_starts_with_6vvt01e(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_string_array_bn13c0i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_strip_prefix_b4949s8(void *gm_out, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) uint8_t gm_f_to_lower_0fu6efk(uint8_t a0);
__attribute__((aligned(4))) gm_b16_8 gm_f_trim_ascii_c9j36d1(gm_b16_8 a0);
__attribute__((aligned(4))) void gm_f_url_problem_38kdh3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_write_kki8q0t(void * a0, gm_t9qkh0rsooc8d a1);
extern uint8_t gm_c42ctv8im24[920] __attribute__((aligned(8)));
extern uint8_t gm_cb60tlkkq8o[152] __attribute__((aligned(8)));
extern uint8_t gm_cev3sfhfj2p[48] __attribute__((aligned(8)));
extern uint8_t gm_cfcggcra0bi[128] __attribute__((aligned(8)));
extern uint8_t gm_cka4oohkshv[512] __attribute__((aligned(8)));
extern uint8_t gm_ct5id4947nt[144] __attribute__((aligned(8)));
extern void *const gm_hl5aqi4imlms[];
extern uint8_t gm_klkc6gi5fh5[40] __attribute__((aligned(8)));
extern uint8_t gm_ko53fduln7u[40] __attribute__((aligned(8)));
extern const uint8_t gm_s02419ud122hi[8];
extern const uint8_t gm_s0iqiubfm7e7e[7];
extern const uint8_t gm_s0jho24omhfui[7];
extern const uint8_t gm_s0k6ioi39f878[4];
extern const uint8_t gm_s0nf1rg04f3m5[4];
extern const uint8_t gm_s22g482qk7a40[3];
extern const uint8_t gm_s2nmq6ak9nl9d[5];
extern const uint8_t gm_s3p8gug7bvl1p[3];
extern const uint8_t gm_s420tm9ua7ggg[4];
extern const uint8_t gm_s460n81rl2c9c[5];
extern const uint8_t gm_s47llp68ptjgu[8];
extern const uint8_t gm_s4b0873bt7c1q[5];
extern const uint8_t gm_s5ojatldfpfs2[8];
extern const uint8_t gm_s5tee1epg2e5a[24];
extern const uint8_t gm_s6gt8b7hsisgu[3];
extern const uint8_t gm_s6iuue4hdrbt9[5];
extern const uint8_t gm_s6v08aos37nfe[8];
extern const uint8_t gm_s7j1ioljmsuka[19];
extern const uint8_t gm_s8f9rv6hrr024[11];
extern const uint8_t gm_s8sv6qdepk5ba[4];
extern const uint8_t gm_s920j0georape[5];
extern const uint8_t gm_s9cb6j5ok7bdk[3];
extern const uint8_t gm_sa2h9vonan0ma[4];
extern const uint8_t gm_sd1smnnii4cig[7];
extern const uint8_t gm_sdbqk29a3n8jm[13];
extern const uint8_t gm_setdfq42nhg1q[5];
extern const uint8_t gm_sf9ilivr4c86m[4];
extern const uint8_t gm_sfag3nu74qn7r[31];
extern const uint8_t gm_sfdbfrv46lihd[2];
extern const uint8_t gm_sflee3fpg0btv[5];
extern const uint8_t gm_sfvspsnv23gbt[7];
extern const uint8_t gm_sg5gdnrith2i6[3];
extern const uint8_t gm_sgdafdv2n5c25[5];
extern const uint8_t gm_sh4f06c2ecgj1[5];
extern const uint8_t gm_shl9ceh0q5tpe[6];
extern const uint8_t gm_sius9pki5u4e3[5];
extern const uint8_t gm_sj2jft9n0jm50[3];
extern const uint8_t gm_skof48dte1s1o[13];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_sl0oflhpdomi7[5];
extern const uint8_t gm_sl3q52ep6aa53[2];
extern const uint8_t gm_sla3alt5340mf[8];
extern const uint8_t gm_slb6cj7ndgvdm[6];
extern const uint8_t gm_slc3n2ku5uff5[55];
extern const uint8_t gm_slfpvqm0f02j6[8];
extern const uint8_t gm_slllafvm0pbeb[6];
extern const uint8_t gm_smee1janbl2t9[3];
extern const uint8_t gm_smimi08sfrg0n[5];
extern const uint8_t gm_sna94ju8c8em3[48];
extern const uint8_t gm_snenkhfev166s[3];
extern const uint8_t gm_snqvo5bgc2fch[8];
extern const uint8_t gm_sob5t135sl7q9[34];
extern const uint8_t gm_son0j0k0t10ro[8];
extern const uint8_t gm_soofuqhnovv5o[5];
extern const uint8_t gm_sp6f2rgoi8toe[7];
extern const uint8_t gm_sp7ec3ksfdilk[5];
extern const uint8_t gm_sq3r7vbcbs4ll[26];
extern const uint8_t gm_sq6e9d3107t3s[11];
extern const uint8_t gm_sr4qbrk6tlgaa[1];
extern const uint8_t gm_srcf8l485c5uq[10];
extern const char gm_srcg0ns322676oc[];
extern const uint8_t gm_st3ja0nqi37bv[2];
extern const uint8_t gm_st3p5nb1e6cc8[5];
extern const uint8_t gm_su0gvqh5vvvlj[37];
extern const uint8_t gm_sv1gci5lo7scg[5];
extern const uint8_t gm_svkmjs6p8bhtg[9];
extern const gm_type gm_tyo86ks9qkvt;
inline __attribute__((aligned(4))) void gm_f_build_list_f41mb8u(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, gm_b16_8 a1) {
    struct gm_fr_build_list_f41mb8u F;
    gm_b16_8 gm_l1; /* what */
    GM_STACK_CHECK(&F, 401u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l1 = a1;
    {
        gm_th2lqbo77pvsc t2;
        gm_f_string_array_bn13c0i((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tktr4rmbindgv *)(((uint8_t *)a0) + 16)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t3 = t2;
        gm_tptmds653bni0 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
            gm_tptmds653bni0 t6;
            GM_ST(uint8_t, (uint8_t *)&t6 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t6 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t6 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t6 + 24, 0);
            ((uint8_t *)&t6)[0] = 0;
            GM_ST(gm_b16_8, ((uint8_t *)&t6 + 8), t5);
            t4 = t6;
        } else {
            uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)a0) + 64));
            gm_t6j6smj7qoctu t8;
            gm_b16_8 t9 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 0));
            GM_ST(gm_b16_8, (uint8_t *)&t8 + 0, t9);
            gm_b16_8 t10 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t8 + 16, t10);
            gm_tabb710d9mq2e t11;
            gm_f_format_p73hfi0((void *)&t11, (void *)NULL, cx, NULL, &t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t12 = t11;
            if (((((uint8_t *)&t12))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t12) + 8, gm_srcg0ns322676oc, 471, 54);
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)&t12) + 8));
            gm_b16_8 t14 = t13;
            gm_b16_8 t15 = GM_LD(gm_b16_8, ((uint8_t *)&t14));
            gm_tf1bjg81u0glv t16;
            gm_f_mfail_uqq7o7g((void *)&t16, t7, t15);
            gm_tptmds653bni0 t17;
            GM_ST(uint8_t, (uint8_t *)&t17 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t17 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t17 + 4, 0);
            ((uint8_t *)&t17)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t17 + 8), t16);
            t4 = t17;
        }
        gm_tptmds653bni0 t18 = t4;
        uint8_t t19 = ((((uint8_t *)&t18))[0] != 0);
        F.ret = t4;
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
__attribute__((aligned(4))) void gm_f_build_settings_brkieib(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_build_settings_brkieib F;
    void * gm_l0; /* m */
    gm_th2lqbo77pvsc gm_l1; /* profile */
    gm_b16_8 gm_l2; /* name */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 2804u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&gm_l1, a1, sizeof gm_l1);
    {
        gm_th2lqbo77pvsc t2 = gm_l1;
        gm_th2lqbo77pvsc t3 = t2;
        gm_b16_8 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            void * t6 = gm_l0;
            gm_ti0g3bmheuq9o t7 = GM_LD(gm_ti0g3bmheuq9o, (((uint8_t *)t6) + 88));
            gm_t171adjpb0m86 t8;
            GM_ST(uint8_t, (uint8_t *)&t8 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t8 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t8 + 4, 0);
            ((uint8_t *)&t8)[0] = 0;
            GM_ST(gm_ti0g3bmheuq9o, ((uint8_t *)&t8 + 8), t7);
            gm_t171adjpb0m86 t9 = t8;
            uint8_t t10 = ((((uint8_t *)&t9))[0] != 0);
            F.ret = t8;
            goto gm_fret;
        }
        gm_l2 = t4;
    }
    {
        {
            void * t12 = gm_l0;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((((uint8_t *)t12) + 240) + 0));
            gm_l3 = t13;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l4;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l4;
                    uint64_t t21 = (uintptr_t)t20;
                    uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_t6b36gvdk984b t25 = GM_LD(gm_t6b36gvdk984b, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t21 * 176));
                    F.l5 = t25;
                    {
                        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                        gm_b16_8 t29 = gm_l2;
                        gm_b16_8 t30 = t28;
                        gm_b16_8 t31 = t29;
                        uint8_t t32 = (gm_eq0iqiubfm7e(((uint8_t *)&t30), ((uint8_t *)&t31)));
                        if (!t32) goto ie27;
                        {
                            void * t34 = gm_l0;
                            gm_ti0g3bmheuq9o t35;
                            gm_f_overlay_pjf5fej((void *)&t35, &(*(gm_ti0g3bmheuq9o *)(((uint8_t *)t34) + 88)), &(*(gm_ti0g3bmheuq9o *)(((uint8_t *)&F.l5) + 24)));
                            gm_t171adjpb0m86 t36;
                            GM_ST(uint8_t, (uint8_t *)&t36 + 1, 0);
                            GM_ST(uint16_t, (uint8_t *)&t36 + 2, 0);
                            GM_ST(uint32_t, (uint8_t *)&t36 + 4, 0);
                            ((uint8_t *)&t36)[0] = 0;
                            GM_ST(gm_ti0g3bmheuq9o, ((uint8_t *)&t36 + 8), t35);
                            gm_t171adjpb0m86 t37 = t36;
                            uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
                            F.ret = t36;
                            goto gm_fret;
                        }
                        goto ix27;
ie27:;
ix27:;
                    }
                    goto me23;
mn24:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 388, 5);
me23:;
                }
lc14:;
                {
                    uint64_t t40 = gm_l4;
                    uint64_t r41; bool o42 = __builtin_add_overflow(t40, ((uint64_t)1ULL), &r41);
                    uint64_t t43 = r41;
                    gm_l4 = t43;
                }
            }
lb14:;
        }
    }
    {
        void * t45 = gm_l0;
        uint64_t t46 = GM_LD(uintptr_t, ((((uint8_t *)t45) + 240) + 0) + 8);
        uint8_t t47 = ((t46 == ((uint64_t)0ULL)));
        if (!t47) goto ie44;
        {
            gm_b16_8 t49 = gm_l2;
            gm_tnpifv20irnph t50;
            GM_ST(gm_b16_8, ((uint8_t *)&t50 + 0), t49);
            gm_tabb710d9mq2e t51;
            gm_f_format_jrdns83((void *)&t51, (void *)NULL, cx, NULL, t50);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t52 = t51;
            if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 8, gm_srcg0ns322676oc, 392, 17);
            gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&t52) + 8));
            gm_b16_8 t54 = t53;
            gm_b16_8 t55 = GM_LD(gm_b16_8, ((uint8_t *)&t54));
            gm_t171adjpb0m86 t56;
            memset(&t56, 0, sizeof t56);
            ((uint8_t *)&t56)[0] = 1;
            GM_ST(gm_b16_8, ((uint8_t *)&t56 + 8), t55);
            gm_t171adjpb0m86 t57 = t56;
            uint8_t t58 = ((((uint8_t *)&t57))[0] != 0);
            F.ret = t56;
            goto gm_fret;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        gm_f_list_epvducg((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            void * t60 = gm_l0;
            gm_b16_8 t61 = GM_LD(gm_b16_8, ((((uint8_t *)t60) + 240) + 0));
            gm_l7 = t61;
        }
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t63 = gm_l8;
                uint64_t t64 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                uint8_t t65 = (t63 < t64);
                if (!t65) goto lb62;
                {
                    uint64_t t68 = gm_l8;
                    uint64_t t69 = (uintptr_t)t68;
                    uint64_t t70 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    gm_t6b36gvdk984b t73 = GM_LD(gm_t6b36gvdk984b, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t69 * 176));
                    F.l9 = t73;
                    {
                        void * t75 = (void *)((uint8_t *)&F.l6);
                        gm_b16_8 t76 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                        gm_tftb1odc739t5 t77 = gm_f_push_3ef746b(t75, t76);
                        gm_tftb1odc739t5 t78 = t77;
                        if (((((uint8_t *)&t78))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t78) + 1, gm_srcg0ns322676oc, 395, 33);
                        gm_b0_1 t79 = GM_LD(gm_b0_1, (((uint8_t *)&t78) + 1));
                    }
                    goto me71;
mn72:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 395, 5);
me71:;
                }
lc62:;
                {
                    uint64_t t81 = gm_l8;
                    uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                    uint64_t t84 = r82;
                    gm_l8 = t84;
                }
            }
lb62:;
        }
    }
    {
        gm_t6j6smj7qoctu t85;
        gm_b16_8 t86 = gm_l2;
        GM_ST(gm_b16_8, (uint8_t *)&t85 + 0, t86);
        gm_b16_8 t87 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t88 = t87;
        gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
        gm_b16_8 t90;
        GM_ST(void *, (uint8_t *)&t90, (void *)(gm_sg5gdnrith2i6));
        GM_ST(uintptr_t, (uint8_t *)&t90 + 8, (uintptr_t)(2));
        gm_tabb710d9mq2e t91;
        gm_f_join_sfknljb((void *)&t91, (void *)NULL, cx, NULL, t89, t90);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t92 = t91;
        if (((((uint8_t *)&t92))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t92) + 8, gm_srcg0ns322676oc, 396, 75);
        gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&t92) + 8));
        GM_ST(gm_b16_8, (uint8_t *)&t85 + 16, t93);
        gm_tabb710d9mq2e t94;
        gm_f_format_7p2ook8((void *)&t94, (void *)NULL, cx, NULL, &t85);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t95 = t94;
        if (((((uint8_t *)&t95))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t95) + 8, gm_srcg0ns322676oc, 396, 13);
        gm_b16_8 t96 = GM_LD(gm_b16_8, (((uint8_t *)&t95) + 8));
        gm_b16_8 t97 = t96;
        gm_b16_8 t98 = GM_LD(gm_b16_8, ((uint8_t *)&t97));
        gm_t171adjpb0m86 t99;
        memset(&t99, 0, sizeof t99);
        ((uint8_t *)&t99)[0] = 1;
        GM_ST(gm_b16_8, ((uint8_t *)&t99 + 8), t98);
        gm_t171adjpb0m86 t100 = t99;
        uint8_t t101 = ((((uint8_t *)&t100))[0] != 0);
        F.ret = t99;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_byte_text_9ib4q2b(void *env, gm_node *cx, gm_node *inst, uint8_t a0) {
    struct gm_fr_byte_text_9ib4q2b F;
    uint8_t gm_l0; /* c */
    gm_b16_8 gm_l1; /* b */
    GM_STACK_CHECK(&F, 179u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_tabb710d9mq2e t2;
        gm_f_make_2ip78bj((void *)&t2, (void *)NULL, cx, NULL, ((uint64_t)1ULL), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_klkc6gi5fh5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t3 = t2;
        if (((((uint8_t *)&t3))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t3) + 8, gm_srcg0ns322676oc, 199, 10);
        gm_b16_8 t4 = GM_LD(gm_b16_8, (((uint8_t *)&t3) + 8));
        gm_l1 = t4;
    }
    {
        uint64_t t5 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t6 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        if (t5 >= (uintptr_t)(t6)) gm_trap_index(t5, t6, gm_srcg0ns322676oc, 200, 5);
        uint8_t t7 = gm_l0;
        GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t5 * 1), t7);
    }
    {
        gm_b16_8 t8 = gm_l1;
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        uint8_t t11 = 0;
        F.ret = t10;
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
inline __attribute__((aligned(4))) void gm_f_check_subdir_2uljb06(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint64_t a2) {
    struct gm_fr_check_subdir_2uljb06 F;
    gm_b16_8 gm_l0; /* name */
    gm_th2lqbo77pvsc gm_l1; /* subdir */
    uint64_t gm_l2; /* line */
    gm_b16_8 gm_l3; /* s */
    GM_STACK_CHECK(&F, 388u);
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
            gm_b16_8 t6 = gm_l3;
            uint8_t t7 = gm_f_is_subdir_sjdvkdc((void *)NULL, cx, NULL, t6);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t8 = (uint8_t)!t7;
            if (!t8) goto ie5;
            {
                uint64_t t10 = gm_l2;
                gm_b16_8 t11 = gm_l0;
                gm_tnpifv20irnph t12;
                GM_ST(gm_b16_8, ((uint8_t *)&t12 + 0), t11);
                gm_tabb710d9mq2e t13;
                gm_f_format_86t06at((void *)&t13, (void *)NULL, cx, NULL, t12);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t14 = t13;
                if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 8, gm_srcg0ns322676oc, 586, 33);
                gm_b16_8 t15 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 8));
                gm_b16_8 t16 = t15;
                gm_b16_8 t17 = GM_LD(gm_b16_8, ((uint8_t *)&t16));
                gm_tf1bjg81u0glv t18;
                gm_f_mfail_uqq7o7g((void *)&t18, t10, t17);
                gm_t6h2b31csotun t19;
                GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
                ((uint8_t *)&t19)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t19 + 8), t18);
                gm_t6h2b31csotun t20 = t19;
                uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
                F.ret = t19;
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
        gm_b0_1 t22;
        gm_t6h2b31csotun t23;
        GM_ST(uint8_t, (uint8_t *)&t23 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t23 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t23 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t23 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t23 + 16, 0);
        GM_ST(uint64_t, (uint8_t *)&t23 + 24, 0);
        ((uint8_t *)&t23)[0] = 0;
        GM_ST(gm_b0_1, ((uint8_t *)&t23 + 8), t22);
        gm_t6h2b31csotun t24 = t23;
        uint8_t t25 = ((((uint8_t *)&t24))[0] != 0);
        F.ret = t23;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_debug_char_p6i205h(void *env, gm_node *cx, gm_node *inst, uint8_t a0) {
    struct gm_fr_debug_char_p6i205h F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK(&F, 416u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = gm_l0;
    uint8_t t2 = t1;
    gm_b16_8 t3;
    uint8_t t6 = GM_LD(uint8_t, ((uint8_t *)&t2));
    if (!(t6 == ((uint8_t)10ULL))) goto mn5;
    gm_b16_8 t7;
    GM_ST(void *, (uint8_t *)&t7, (void *)(gm_s460n81rl2c9c));
    GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(4));
    t3 = t7;
    goto me4;
mn5:;
    uint8_t t9 = GM_LD(uint8_t, ((uint8_t *)&t2));
    if (!(t9 == ((uint8_t)9ULL))) goto mn8;
    gm_b16_8 t10;
    GM_ST(void *, (uint8_t *)&t10, (void *)(gm_sh4f06c2ecgj1));
    GM_ST(uintptr_t, (uint8_t *)&t10 + 8, (uintptr_t)(4));
    t3 = t10;
    goto me4;
mn8:;
    uint8_t t12 = GM_LD(uint8_t, ((uint8_t *)&t2));
    if (!(t12 == ((uint8_t)13ULL))) goto mn11;
    gm_b16_8 t13;
    GM_ST(void *, (uint8_t *)&t13, (void *)(gm_setdfq42nhg1q));
    GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(4));
    t3 = t13;
    goto me4;
mn11:;
    uint8_t t15 = GM_LD(uint8_t, ((uint8_t *)&t2));
    if (!(t15 == ((uint8_t)39ULL))) goto mn14;
    gm_b16_8 t16;
    GM_ST(void *, (uint8_t *)&t16, (void *)(gm_s2nmq6ak9nl9d));
    GM_ST(uintptr_t, (uint8_t *)&t16 + 8, (uintptr_t)(4));
    t3 = t16;
    goto me4;
mn14:;
    uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)&t2));
    if (!(t18 == ((uint8_t)92ULL))) goto mn17;
    gm_b16_8 t19;
    GM_ST(void *, (uint8_t *)&t19, (void *)(gm_s4b0873bt7c1q));
    GM_ST(uintptr_t, (uint8_t *)&t19 + 8, (uintptr_t)(4));
    t3 = t19;
    goto me4;
mn17:;
    gm_b16_8 t21;
    uint8_t t23 = 0;
    uint8_t t24 = gm_l0;
    uint8_t t25 = (t24 < ((uint8_t)32ULL));
    if (!t25) {
        uint8_t t26 = gm_l0;
        uint8_t t27 = ((t26 == ((uint8_t)127ULL)));
        t23 = t27;
    } else t23 = 1;
    if (!t23) goto ie22;
    uint8_t t29 = gm_l0;
    gm_t9qkh0rsooc8d t30;
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), t29);
    gm_tabb710d9mq2e t31;
    gm_f_format_llhsr0b((void *)&t31, (void *)NULL, cx, NULL, t30);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t32 = t31;
    if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_srcg0ns322676oc, 194, 37);
    gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
    gm_b16_8 t34 = t33;
    gm_b16_8 t35 = t34;
    gm_b16_8 t36 = GM_LD(gm_b16_8, ((uint8_t *)&t35));
    t21 = t36;
    goto ix22;
ie22:;
    uint8_t t38 = gm_l0;
    gm_b16_8 t39 = gm_f_byte_text_9ib4q2b((void *)NULL, cx, NULL, t38);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t40;
    GM_ST(gm_b16_8, ((uint8_t *)&t40 + 0), t39);
    gm_tabb710d9mq2e t41;
    gm_f_format_3aass3a((void *)&t41, (void *)NULL, cx, NULL, t40);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t42 = t41;
    if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 8, gm_srcg0ns322676oc, 194, 78);
    gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&t42) + 8));
    gm_b16_8 t44 = t43;
    gm_b16_8 t45 = t44;
    gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
    t21 = t46;
ix22:;
    t3 = t21;
    goto me4;
mn20:;
    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 188, 50);
me4:;
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
__attribute__((aligned(4))) gm_b16_8 gm_f_debug_escape_t8t1h0j(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_debug_escape_t8t1h0j F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint8_t gm_l4; /* c */
    GM_STACK_CHECK(&F, 406u);
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
                    uint8_t t13 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t11 * 1));
                    uint8_t t14 = t13;
                    uint8_t t17 = GM_LD(uint8_t, ((uint8_t *)&t14));
                    gm_l4 = t17;
                    {
                        uint8_t t19 = gm_l4;
                        uint8_t t20 = t19;
                        uint8_t t23 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t23 == ((uint8_t)10ULL))) goto mn22;
                        void * t24 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t25;
                        GM_ST(void *, (uint8_t *)&t25, (void *)(gm_s6gt8b7hsisgu));
                        GM_ST(uintptr_t, (uint8_t *)&t25 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t26 = gm_f_extend_0cs85gs(t24, t25);
                        gm_tftb1odc739t5 t27 = t26;
                        if (((((uint8_t *)&t27))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t27) + 1, gm_srcg0ns322676oc, 209, 21);
                        gm_b0_1 t28 = GM_LD(gm_b0_1, (((uint8_t *)&t27) + 1));
                        goto me21;
mn22:;
                        uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t30 == ((uint8_t)9ULL))) goto mn29;
                        void * t31 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t32;
                        GM_ST(void *, (uint8_t *)&t32, (void *)(gm_s22g482qk7a40));
                        GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t33 = gm_f_extend_0cs85gs(t31, t32);
                        gm_tftb1odc739t5 t34 = t33;
                        if (((((uint8_t *)&t34))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t34) + 1, gm_srcg0ns322676oc, 210, 21);
                        gm_b0_1 t35 = GM_LD(gm_b0_1, (((uint8_t *)&t34) + 1));
                        goto me21;
mn29:;
                        uint8_t t37 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t37 == ((uint8_t)13ULL))) goto mn36;
                        void * t38 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t39;
                        GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s9cb6j5ok7bdk));
                        GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t40 = gm_f_extend_0cs85gs(t38, t39);
                        gm_tftb1odc739t5 t41 = t40;
                        if (((((uint8_t *)&t41))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t41) + 1, gm_srcg0ns322676oc, 211, 21);
                        gm_b0_1 t42 = GM_LD(gm_b0_1, (((uint8_t *)&t41) + 1));
                        goto me21;
mn36:;
                        uint8_t t44 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t44 == ((uint8_t)34ULL))) goto mn43;
                        void * t45 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t46;
                        GM_ST(void *, (uint8_t *)&t46, (void *)(gm_smee1janbl2t9));
                        GM_ST(uintptr_t, (uint8_t *)&t46 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t47 = gm_f_extend_0cs85gs(t45, t46);
                        gm_tftb1odc739t5 t48 = t47;
                        if (((((uint8_t *)&t48))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t48) + 1, gm_srcg0ns322676oc, 212, 20);
                        gm_b0_1 t49 = GM_LD(gm_b0_1, (((uint8_t *)&t48) + 1));
                        goto me21;
mn43:;
                        uint8_t t51 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t51 == ((uint8_t)39ULL))) goto mn50;
                        void * t52 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t53;
                        GM_ST(void *, (uint8_t *)&t53, (void *)(gm_sj2jft9n0jm50));
                        GM_ST(uintptr_t, (uint8_t *)&t53 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t54 = gm_f_extend_0cs85gs(t52, t53);
                        gm_tftb1odc739t5 t55 = t54;
                        if (((((uint8_t *)&t55))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t55) + 1, gm_srcg0ns322676oc, 213, 21);
                        gm_b0_1 t56 = GM_LD(gm_b0_1, (((uint8_t *)&t55) + 1));
                        goto me21;
mn50:;
                        uint8_t t58 = GM_LD(uint8_t, ((uint8_t *)&t20));
                        if (!(t58 == ((uint8_t)92ULL))) goto mn57;
                        void * t59 = (void *)((uint8_t *)&F.l1);
                        gm_b16_8 t60;
                        GM_ST(void *, (uint8_t *)&t60, (void *)(gm_s3p8gug7bvl1p));
                        GM_ST(uintptr_t, (uint8_t *)&t60 + 8, (uintptr_t)(2));
                        gm_tftb1odc739t5 t61 = gm_f_extend_0cs85gs(t59, t60);
                        gm_tftb1odc739t5 t62 = t61;
                        if (((((uint8_t *)&t62))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t62) + 1, gm_srcg0ns322676oc, 214, 21);
                        gm_b0_1 t63 = GM_LD(gm_b0_1, (((uint8_t *)&t62) + 1));
                        goto me21;
mn57:;
                        uint8_t t66 = 0;
                        uint8_t t67 = gm_l4;
                        uint8_t t68 = (t67 < ((uint8_t)32ULL));
                        if (!t68) {
                            uint8_t t69 = gm_l4;
                            uint8_t t70 = ((t69 == ((uint8_t)127ULL)));
                            t66 = t70;
                        } else t66 = 1;
                        if (!t66) goto ie65;
                        {
                            void * t72 = (void *)((uint8_t *)&F.l1);
                            uint8_t t73 = gm_l4;
                            gm_t9qkh0rsooc8d t74;
                            GM_ST(uint8_t, ((uint8_t *)&t74 + 0), t73);
                            gm_tftb1odc739t5 t75 = gm_f_write_kki8q0t(t72, t74);
                            gm_tftb1odc739t5 t76 = t75;
                            if (((((uint8_t *)&t76))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t76) + 1, gm_srcg0ns322676oc, 215, 45);
                            gm_b0_1 t77 = GM_LD(gm_b0_1, (((uint8_t *)&t76) + 1));
                        }
                        goto ix65;
ie65:;
                        {
                            void * t79 = (void *)((uint8_t *)&F.l1);
                            uint8_t t80 = gm_l4;
                            gm_tftb1odc739t5 t81 = gm_f_push_t83rg2m(t79, t80);
                            gm_tftb1odc739t5 t82 = t81;
                            if (((((uint8_t *)&t82))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t82) + 1, gm_srcg0ns322676oc, 215, 83);
                            gm_b0_1 t83 = GM_LD(gm_b0_1, (((uint8_t *)&t82) + 1));
                        }
ix65:;
                        goto me21;
mn64:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 208, 9);
me21:;
                    }
                    goto me15;
mn16:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 207, 5);
me15:;
                }
lc4:;
                {
                    uint64_t t85 = gm_l3;
                    uint64_t r86; bool o87 = __builtin_add_overflow(t85, ((uint64_t)1ULL), &r86);
                    uint64_t t88 = r86;
                    gm_l3 = t88;
                }
            }
lb4:;
        }
    }
    {
        gm_b16_8 t89 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
        gm_b16_8 t90 = t89;
        gm_b16_8 t91 = GM_LD(gm_b16_8, ((uint8_t *)&t90));
        uint8_t t92 = 0;
        F.ret = t91;
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
inline __attribute__((aligned(4))) void gm_f_dep_string_mmgrsgi(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1, gm_b16_8 a2) {
    struct gm_fr_dep_string_mmgrsgi F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* key */
    gm_b16_8 gm_l2; /* name */
    void * gm_l3; /* e */
    gm_b16_8 gm_l4; /* s */
    GM_STACK_CHECK(&F, 667u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t2 = gm_l0;
        gm_b16_8 t3 = gm_l1;
        void * t4 = gm_f_get_9rme0gb(t2, t3);
        void * t5 = t4;
        void * t6;
        if ((GM_LD(void *, ((uint8_t *)&t5)) != 0)) {
            void * t7 = GM_LD(void *, (((uint8_t *)&t5) + 0));
            t6 = t7;
        } else {
            gm_th2lqbo77pvsc t8;
            memset(&t8, 0, sizeof t8);
            gm_tccklsmdasd9s t9;
            GM_ST(uint8_t, (uint8_t *)&t9 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t9 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t9 + 4, 0);
            ((uint8_t *)&t9)[0] = 0;
            GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t9 + 8), t8);
            gm_tccklsmdasd9s t10 = t9;
            uint8_t t11 = ((((uint8_t *)&t10))[0] != 0);
            F.ret = t9;
            goto gm_fret;
        }
        gm_l3 = t6;
    }
    {
        void * t12 = gm_l3;
        gm_th2lqbo77pvsc t13;
        gm_f_as_str_982o16v((void *)&t13, &(*(gm_tktr4rmbindgv *)(((uint8_t *)t12) + 16)));
        gm_th2lqbo77pvsc t14 = t13;
        gm_b16_8 t15;
        if (((((uint8_t *)&t14))[16] != 0)) {
            gm_b16_8 t16 = GM_LD(gm_b16_8, (((uint8_t *)&t14) + 0));
            t15 = t16;
        } else {
            void * t17 = gm_l3;
            uint64_t t18 = GM_LD(uint64_t, (((uint8_t *)t17) + 64));
            gm_t6j6smj7qoctu t19;
            gm_b16_8 t20 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t19 + 0, t20);
            gm_b16_8 t21 = gm_l2;
            GM_ST(gm_b16_8, (uint8_t *)&t19 + 16, t21);
            gm_tabb710d9mq2e t22;
            gm_f_format_fqmocq1((void *)&t22, (void *)NULL, cx, NULL, &t19);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t23 = t22;
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_srcg0ns322676oc, 479, 53);
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            gm_tf1bjg81u0glv t27;
            gm_f_mfail_uqq7o7g((void *)&t27, t18, t26);
            gm_tccklsmdasd9s t28;
            GM_ST(uint8_t, (uint8_t *)&t28 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t28 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t28 + 4, 0);
            ((uint8_t *)&t28)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t28 + 8), t27);
            gm_tccklsmdasd9s t29 = t28;
            uint8_t t30 = ((((uint8_t *)&t29))[0] != 0);
            F.ret = t28;
            goto gm_fret;
        }
        gm_l4 = t15;
    }
    {
        gm_b16_8 t31 = gm_l4;
        gm_th2lqbo77pvsc t32;
        GM_ST(uint8_t, (uint8_t *)&t32 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t32 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t32 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t32), t31);
        ((uint8_t *)&t32)[16] = 1;
        gm_tccklsmdasd9s t33;
        GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
        ((uint8_t *)&t33)[0] = 0;
        GM_ST(gm_th2lqbo77pvsc, ((uint8_t *)&t33 + 8), t32);
        gm_tccklsmdasd9s t34 = t33;
        uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
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
inline __attribute__((aligned(4))) gm_b0_1 gm_f_for_body_vgf3305(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_for_body_vgf3305 F;
    gm_b16_8 gm_l0; /* <item> */
    gm_b16_8 gm_l1; /* c */
    GM_STACK_CHECK(&F, 237u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 1 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_b16_8 t3 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l0));
    gm_l1 = t3;
    {
        uint8_t t6 = 0;
        uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
        uint8_t t8 = ((t7 == ((uint64_t)0ULL)));
        if (!t8) {
            gm_b16_8 t9 = gm_l1;
            gm_tfeab279c16rr t10;
            gm_b16_8 t11;
            GM_ST(void *, (uint8_t *)&t11, (void *)(gm_snenkhfev166s));
            GM_ST(uintptr_t, (uint8_t *)&t11 + 8, (uintptr_t)(2));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 0, t11);
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sl3q52ep6aa53));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(1));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 16, t12);
            gm_b16_8 t13;
            GM_ST(void *, (uint8_t *)&t13, (void *)(gm_sp7ec3ksfdilk));
            GM_ST(uintptr_t, (uint8_t *)&t13 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t10 + 32, t13);
            gm_tfeab279c16rr t14 = t10;
            uint8_t t15 = 0;
            gm_b16_8 t16 = t9;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t14) + 0, ((uint8_t *)&t16))) t15 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t14) + 16, ((uint8_t *)&t16))) t15 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t14) + 32, ((uint8_t *)&t16))) t15 = 1;
            uint8_t t17 = (uint8_t)(t15);
            t6 = t17;
        } else t6 = 1;
        if (!t6) goto ie5;
        {
            uint8_t t19 = 0;
            ((struct gm_fr_is_subdir_sjdvkdc *)((&F.h)->up))->ret = ((uint8_t)0);
            gm_unw.kind = GM_RETURN; gm_unw.frame = ((&F.h)->up); gm_unw.label = 0; gm_unw.is_err = t19;
            goto gm_lad;
        }
        goto ix5;
ie5:;
ix5:;
    }
    goto me1;
mn2:;
    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 270, 5);
me1:;
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_hex_digit_q8s6utq(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_hex_digit_q8s6utq F;
    uint8_t gm_l0; /* c */
    uint8_t gm_l1; /* <tmp> */
    uint8_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* <tmp> */
    GM_STACK_CHECK(&F, 29u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = 0;
    {
        uint8_t t4 = gm_l0;
        gm_l1 = t4;
    }
    uint8_t t5 = 0;
    uint8_t t6 = gm_l1;
    uint8_t t7 = (((uint8_t)48ULL) <= t6);
    if (t7) {
        uint8_t t8 = gm_l1;
        uint8_t t9 = (t8 <= ((uint8_t)57ULL));
        t5 = t9;
    } else t5 = 0;
    uint8_t t10 = t5;
    if (!t10) {
        {
            uint8_t t12 = gm_l0;
            gm_l2 = t12;
        }
        uint8_t t13 = 0;
        uint8_t t14 = gm_l2;
        uint8_t t15 = (((uint8_t)97ULL) <= t14);
        if (t15) {
            uint8_t t16 = gm_l2;
            uint8_t t17 = (t16 <= ((uint8_t)102ULL));
            t13 = t17;
        } else t13 = 0;
        uint8_t t18 = t13;
        t2 = t18;
    } else t2 = 1;
    if (!t2) {
        {
            uint8_t t20 = gm_l0;
            gm_l3 = t20;
        }
        uint8_t t21 = 0;
        uint8_t t22 = gm_l3;
        uint8_t t23 = (((uint8_t)65ULL) <= t22);
        if (t23) {
            uint8_t t24 = gm_l3;
            uint8_t t25 = (t24 <= ((uint8_t)70ULL));
            t21 = t25;
        } else t21 = 0;
        uint8_t t26 = t21;
        t1 = t26;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_identifier_kvlcjjo(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_identifier_kvlcjjo F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* d */
    GM_STACK_CHECK(&F, 154u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t3 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t4 = ((t3 == ((uint64_t)0ULL)));
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
        uint64_t t8 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srcg0ns322676oc, 153, 34);
        uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 1));
        uint8_t t11 = gm_f_is_ident_start_rjefpfm(t10);
        uint8_t t12 = (uint8_t)!t11;
        if (!t12) goto ie7;
        {
            uint8_t t14 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix7;
ie7:;
ix7:;
    }
    {
        {
            gm_b16_8 t16 = gm_l0;
            gm_l1 = t16;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t18 = gm_l2;
                uint64_t t19 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t20 = (t18 < t19);
                if (!t20) goto lb17;
                {
                    uint64_t t23 = gm_l2;
                    uint64_t t24 = (uintptr_t)t23;
                    uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint8_t t26 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t24 * 1));
                    uint8_t t27 = t26;
                    uint8_t t30 = GM_LD(uint8_t, ((uint8_t *)&t27));
                    gm_l3 = t30;
                    {
                        uint8_t t33 = gm_l3;
                        uint8_t t34 = gm_f_is_ident_cont_317fpdb(t33);
                        uint8_t t35 = (uint8_t)!t34;
                        if (!t35) goto ie32;
                        {
                            uint8_t t37 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix32;
ie32:;
ix32:;
                    }
                    goto me28;
mn29:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 154, 5);
me28:;
                }
lc17:;
                {
                    uint64_t t39 = gm_l2;
                    uint64_t r40; bool o41 = __builtin_add_overflow(t39, ((uint64_t)1ULL), &r40);
                    uint64_t t42 = r40;
                    gm_l2 = t42;
                }
            }
lb17:;
        }
    }
    {
        uint8_t t43 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_space_or_control_3uo6i3o(uint8_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_space_or_control_3uo6i3o F;
    uint8_t gm_l0; /* c */
    GM_STACK_CHECK(&F, 8u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    uint8_t t1 = 0;
    uint8_t t2 = gm_l0;
    uint8_t t3 = ((t2 == ((uint8_t)32ULL)));
    if (!t3) {
        uint8_t t4 = gm_l0;
        uint8_t t5 = gm_f_is_control_65ubfpo(t4);
        t1 = t5;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_subdir_sjdvkdc(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_is_subdir_sjdvkdc F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* <tmp> */
    GM_STACK_CHECK(&F, 143u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint8_t t3 = 0;
        uint64_t t4 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        uint8_t t5 = ((t4 == ((uint64_t)0ULL)));
        if (!t5) {
            gm_b16_8 t6 = gm_l0;
            gm_b16_8 t7;
            GM_ST(void *, (uint8_t *)&t7, (void *)(gm_st3ja0nqi37bv));
            GM_ST(uintptr_t, (uint8_t *)&t7 + 8, (uintptr_t)(1));
            uint8_t t8 = gm_f_starts_with_6vvt01e(t6, t7);
            t3 = t8;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t10 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_b16_8 t12 = gm_l0;
            gm_l1 = t12;
        }
        gm_hval t13 = ((gm_hval){(void *)&F.h, cx});
        gm_node wn14 = {.eff = 993381376, .kind = GM_LIT, .clauses = gm_hl5aqi4imlms, .env = t13.env, .hctx = t13.cx, .with_frame = &F.h, .with_label = 0, .result = (void *)&F.w0, .fw = NULL, .next = cx};
        gm_b16_8 t17 = gm_l1;
        gm_b16_8 t18;
        GM_ST(void *, (uint8_t *)&t18, (void *)(gm_st3ja0nqi37bv));
        GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(1));
        gm_b0_1 t19 = gm_f_split_obtinjv_s7d377q((void *)NULL, (&wn14), NULL, t17, t18);
        if (gm_unw.kind) {
            goto gm_ll16;
        }
        F.w0 = t19;
        goto gm_ls20;
gm_ll16:;
        if (gm_unw.kind == GM_BREAK && gm_unw.frame == &F.h && gm_unw.label == 0) { gm_unw.kind = GM_NONE; goto we15; }
        goto gm_lad;
gm_ls20:;
we15:;
        gm_b0_1 t21 = F.w0;
        gm_b0_1 t22 = t21;
    }
    {
        uint8_t t23 = 0;
        F.ret = ((uint8_t)1);
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
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_lint_rule_o3ccae1(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_lint_rule_o3ccae1 F;
    gm_b16_8 gm_l0; /* name */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint64_t gm_l3; /* i */
    GM_STACK_CHECK(&F, 420u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        {
            void * t3 = ((uint8_t *)gm_c42ctv8im24);
            if ((uintptr_t)0 > (uintptr_t)23 || (uintptr_t)23 > (uintptr_t)23) gm_trap_slice((uintptr_t)0, (uintptr_t)23, 23, gm_srcg0ns322676oc, 378, 27);
            gm_b16_8 t4;
            GM_ST(void *, (uint8_t *)&t4, (void *)(((uint8_t *)t3 + (uintptr_t)0 * 40)));
            GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)((uintptr_t)23 - (uintptr_t)0));
            gm_l1 = t4;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t6 = gm_l2;
                uint64_t t7 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t8 = (t6 < t7);
                if (!t8) goto lb5;
                {
                    gm_toa5kh9gm9738 t11;
                    uint64_t t12 = gm_l2;
                    GM_ST(uint64_t, (uint8_t *)&t11 + 0, t12);
                    uint64_t t13 = gm_l2;
                    uint64_t t14 = (uintptr_t)t13;
                    uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    memcpy((uint8_t *)&t11 + 8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t14 * 40), 40);
                    gm_toa5kh9gm9738 t16 = t11;
                    uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t16) + 0));
                    gm_l3 = t19;
                    gm_tk9n3vlila9qg t20 = GM_LD(gm_tk9n3vlila9qg, (((uint8_t *)&t16) + 8));
                    F.l4 = t20;
                    {
                        gm_b16_8 t23 = GM_LD(gm_b16_8, (((uint8_t *)&F.l4) + 0));
                        gm_b16_8 t24 = gm_l0;
                        gm_b16_8 t25 = t23;
                        gm_b16_8 t26 = t24;
                        uint8_t t27 = (gm_eq0iqiubfm7e(((uint8_t *)&t25), ((uint8_t *)&t26)));
                        if (!t27) goto ie22;
                        {
                            uint64_t t29 = gm_l3;
                            gm_tf5srh4mp4b3i t30;
                            GM_ST(uint8_t, (uint8_t *)&t30 + 9, 0);
                            GM_ST(uint16_t, (uint8_t *)&t30 + 10, 0);
                            GM_ST(uint32_t, (uint8_t *)&t30 + 12, 0);
                            GM_ST(uint64_t, ((uint8_t *)&t30), t29);
                            ((uint8_t *)&t30)[8] = 1;
                            uint8_t t31 = 0;
                            F.ret = t30;
                            goto gm_fret;
                        }
                        goto ix22;
ie22:;
ix22:;
                    }
                    goto me17;
mn18:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 378, 5);
me17:;
                }
lc5:;
                {
                    uint64_t t33 = gm_l2;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l2 = t36;
                }
            }
lb5:;
        }
    }
    {
        gm_tf5srh4mp4b3i t37;
        memset(&t37, 0, sizeof t37);
        uint8_t t38 = 0;
        F.ret = t37;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) gm_b16_8 gm_f_lower_ascii_uggh3ca(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_lower_ascii_uggh3ca F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* out */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint8_t gm_l5; /* c */
    GM_STACK_CHECK(&F, 375u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_tabb710d9mq2e t3;
        gm_f_make_2ip78bj((void *)&t3, (void *)NULL, cx, NULL, t2, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ko53fduln7u)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t4 = t3;
        if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_srcg0ns322676oc, 258, 12);
        gm_b16_8 t5 = GM_LD(gm_b16_8, (((uint8_t *)&t4) + 8));
        gm_l1 = t5;
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
                    uint64_t t15 = gm_l3;
                    uint64_t t16 = (uintptr_t)t15;
                    uint64_t t17 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint8_t t18 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t16 * 1));
                    gm_tf5srh4mp4b3i t19;
                    GM_ST(uint8_t, (uint8_t *)&t19 + 9, 0);
                    GM_ST(uint16_t, (uint8_t *)&t19 + 10, 0);
                    GM_ST(uint32_t, (uint8_t *)&t19 + 12, 0);
                    GM_ST(uint64_t, ((uint8_t *)&t19 + 0), t14);
                    GM_ST(uint8_t, ((uint8_t *)&t19 + 8), t18);
                    gm_tf5srh4mp4b3i t20 = t19;
                    uint64_t t23 = GM_LD(uint64_t, (((uint8_t *)&t20) + 0));
                    gm_l4 = t23;
                    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t20) + 8));
                    gm_l5 = t24;
                    {
                        uint64_t t26 = gm_l4;
                        uint64_t t27 = (uintptr_t)t26;
                        uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                        if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srcg0ns322676oc, 259, 32);
                        uint8_t *t29 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t27 * 1);
                        uint8_t t30 = gm_l5;
                        uint8_t t31 = gm_f_to_lower_0fu6efk(t30);
                        GM_ST(uint8_t, t29, t31);
                    }
                    goto me21;
mn22:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 259, 5);
me21:;
                }
lc8:;
                {
                    uint64_t t33 = gm_l3;
                    uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                    uint64_t t36 = r34;
                    gm_l3 = t36;
                }
            }
lb8:;
        }
    }
    {
        gm_b16_8 t37 = gm_l1;
        gm_b16_8 t38 = t37;
        gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
        uint8_t t40 = 0;
        F.ret = t39;
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
inline __attribute__((aligned(4))) void gm_f_mfail_uqq7o7g(void *gm_out, uint64_t a0, gm_b16_8 a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mfail_uqq7o7g F;
    uint64_t gm_l0; /* line */
    gm_b16_8 gm_l1; /* msg */
    GM_STACK_CHECK(&F, 48u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    uint64_t t1 = gm_l0;
    GM_ST(uint64_t, (uint8_t *)&(*(gm_tf1bjg81u0glv *)gm_out) + 0, t1);
    gm_b16_8 t2 = gm_l1;
    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tf1bjg81u0glv *)gm_out) + 8, t2);
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_tf1bjg81u0glv gm_f_mfail_uqq7o7g_c(uint64_t a0, gm_b16_8 a1) { gm_tf1bjg81u0glv gm_r; gm_f_mfail_uqq7o7g(&gm_r, a0, a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_name_problem_6r4mp11(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1) {
    struct gm_fr_name_problem_6r4mp11 F;
    gm_b16_8 gm_l0; /* s */
    gm_b16_8 gm_l1; /* what */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    gm_b16_8 gm_l4; /* k */
    gm_b16_8 gm_l5; /* <tmp> */
    uint64_t gm_l6; /* <tmp> */
    gm_b16_8 gm_l7; /* k */
    GM_STACK_CHECK(&F, 1884u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        gm_b16_8 t3 = gm_l0;
        uint8_t t4 = gm_f_is_identifier_kvlcjjo(t3);
        uint8_t t5 = (uint8_t)!t4;
        if (!t5) goto ie2;
        {
            gm_t6j6smj7qoctu t7;
            gm_b16_8 t8 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t7 + 0, t8);
            gm_b16_8 t9 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t7 + 16, t9);
            gm_tabb710d9mq2e t10;
            gm_f_format_rp4le08((void *)&t10, (void *)NULL, cx, NULL, &t7);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t11 = t10;
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_srcg0ns322676oc, 168, 16);
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
            gm_b16_8 t13 = t12;
            gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
            gm_th2lqbo77pvsc t15;
            GM_ST(uint8_t, (uint8_t *)&t15 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t15 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t15 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t15), t14);
            ((uint8_t *)&t15)[16] = 1;
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t18 = (((GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8) == 1) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l0)), gm_sfdbfrv46lihd, 1) == 0));
        if (!t18) goto ie17;
        {
            gm_b16_8 t20 = gm_l1;
            gm_tnpifv20irnph t21;
            GM_ST(gm_b16_8, ((uint8_t *)&t21 + 0), t20);
            gm_tabb710d9mq2e t22;
            gm_f_format_i3a6tth((void *)&t22, (void *)NULL, cx, NULL, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t23 = t22;
            if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 8, gm_srcg0ns322676oc, 170, 26);
            gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t23) + 8));
            gm_b16_8 t25 = t24;
            gm_b16_8 t26 = GM_LD(gm_b16_8, ((uint8_t *)&t25));
            gm_th2lqbo77pvsc t27;
            GM_ST(uint8_t, (uint8_t *)&t27 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t27 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t27 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t27), t26);
            ((uint8_t *)&t27)[16] = 1;
            uint8_t t28 = 0;
            F.ret = t27;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        {
            void * t30 = ((uint8_t *)gm_cka4oohkshv);
            if ((uintptr_t)0 > (uintptr_t)32 || (uintptr_t)32 > (uintptr_t)32) gm_trap_slice((uintptr_t)0, (uintptr_t)32, 32, gm_srcg0ns322676oc, 171, 5);
            gm_b16_8 t31;
            GM_ST(void *, (uint8_t *)&t31, (void *)(((uint8_t *)t30 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t31 + 8, (uintptr_t)((uintptr_t)32 - (uintptr_t)0));
            gm_l2 = t31;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t33 = gm_l3;
                uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t35 = (t33 < t34);
                if (!t35) goto lb32;
                {
                    uint64_t t38 = gm_l3;
                    uint64_t t39 = (uintptr_t)t38;
                    uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t39 * 16));
                    gm_l4 = t43;
                    {
                        gm_b16_8 t46 = gm_l4;
                        gm_b16_8 t47 = gm_l0;
                        gm_b16_8 t48 = t46;
                        gm_b16_8 t49 = t47;
                        uint8_t t50 = (gm_eq0iqiubfm7e(((uint8_t *)&t48), ((uint8_t *)&t49)));
                        if (!t50) goto ie45;
                        {
                            gm_t6j6smj7qoctu t52;
                            gm_b16_8 t53 = gm_l1;
                            GM_ST(gm_b16_8, (uint8_t *)&t52 + 0, t53);
                            gm_b16_8 t54 = gm_l0;
                            GM_ST(gm_b16_8, (uint8_t *)&t52 + 16, t54);
                            gm_tabb710d9mq2e t55;
                            gm_f_format_usjmuj4((void *)&t55, (void *)NULL, cx, NULL, &t52);
                            if (gm_unw.kind) {
                                goto gm_ll36;
                            }
                            gm_tabb710d9mq2e t56 = t55;
                            if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_srcg0ns322676oc, 172, 28);
                            gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
                            gm_b16_8 t58 = t57;
                            gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
                            gm_th2lqbo77pvsc t60;
                            GM_ST(uint8_t, (uint8_t *)&t60 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t60 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t60 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t60), t59);
                            ((uint8_t *)&t60)[16] = 1;
                            uint8_t t61 = 0;
                            F.ret = t60;
                            goto gm_fret;
                        }
                        goto ix45;
ie45:;
ix45:;
                    }
                    goto me41;
mn42:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 171, 5);
me41:;
                }
lc32:;
                {
                    uint64_t t63 = gm_l3;
                    uint64_t r64; bool o65 = __builtin_add_overflow(t63, ((uint64_t)1ULL), &r64);
                    uint64_t t66 = r64;
                    gm_l3 = t66;
                }
            }
gm_ll36:;
            if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb32; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc32; }
            }
            goto gm_lad;
lb32:;
        }
    }
    {
        {
            void * t68 = ((uint8_t *)gm_cev3sfhfj2p);
            if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcg0ns322676oc, 174, 5);
            gm_b16_8 t69;
            GM_ST(void *, (uint8_t *)&t69, (void *)(((uint8_t *)t68 + (uintptr_t)0 * 16)));
            GM_ST(uintptr_t, (uint8_t *)&t69 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
            gm_l5 = t69;
        }
        {
            gm_l6 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t71 = gm_l6;
                uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                uint8_t t73 = (t71 < t72);
                if (!t73) goto lb70;
                {
                    uint64_t t76 = gm_l6;
                    uint64_t t77 = (uintptr_t)t76;
                    uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l5) + 8);
                    gm_b16_8 t81 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l5)) + t77 * 16));
                    gm_l7 = t81;
                    {
                        gm_b16_8 t84 = gm_l7;
                        gm_b16_8 t85 = gm_l0;
                        gm_b16_8 t86 = t84;
                        gm_b16_8 t87 = t85;
                        uint8_t t88 = (gm_eq0iqiubfm7e(((uint8_t *)&t86), ((uint8_t *)&t87)));
                        if (!t88) goto ie83;
                        {
                            gm_t6j6smj7qoctu t90;
                            gm_b16_8 t91 = gm_l1;
                            GM_ST(gm_b16_8, (uint8_t *)&t90 + 0, t91);
                            gm_b16_8 t92 = gm_l0;
                            GM_ST(gm_b16_8, (uint8_t *)&t90 + 16, t92);
                            gm_tabb710d9mq2e t93;
                            gm_f_format_usjmuj4((void *)&t93, (void *)NULL, cx, NULL, &t90);
                            if (gm_unw.kind) {
                                goto gm_ll74;
                            }
                            gm_tabb710d9mq2e t94 = t93;
                            if (((((uint8_t *)&t94))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t94) + 8, gm_srcg0ns322676oc, 175, 28);
                            gm_b16_8 t95 = GM_LD(gm_b16_8, (((uint8_t *)&t94) + 8));
                            gm_b16_8 t96 = t95;
                            gm_b16_8 t97 = GM_LD(gm_b16_8, ((uint8_t *)&t96));
                            gm_th2lqbo77pvsc t98;
                            GM_ST(uint8_t, (uint8_t *)&t98 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t98 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t98 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t98), t97);
                            ((uint8_t *)&t98)[16] = 1;
                            uint8_t t99 = 0;
                            F.ret = t98;
                            goto gm_fret;
                        }
                        goto ix83;
ie83:;
ix83:;
                    }
                    goto me79;
mn80:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 174, 5);
me79:;
                }
lc70:;
                {
                    uint64_t t101 = gm_l6;
                    uint64_t r102; bool o103 = __builtin_add_overflow(t101, ((uint64_t)1ULL), &r102);
                    uint64_t t104 = r102;
                    gm_l6 = t104;
                }
            }
gm_ll74:;
            if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb70; }
                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc70; }
            }
            goto gm_lad;
lb70:;
        }
    }
    {
        uint8_t t106 = 0;
        gm_b16_8 t107 = gm_l0;
        uint8_t t108 = gm_f_is_reserved_type_name_tuq4qsj(t107);
        if (!t108) {
            gm_b16_8 t109 = gm_l0;
            gm_tfeab279c16rr t110;
            gm_b16_8 t111;
            GM_ST(void *, (uint8_t *)&t111, (void *)(gm_s0iqiubfm7e7e));
            GM_ST(uintptr_t, (uint8_t *)&t111 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (uint8_t *)&t110 + 0, t111);
            gm_b16_8 t112;
            GM_ST(void *, (uint8_t *)&t112, (void *)(gm_s47llp68ptjgu));
            GM_ST(uintptr_t, (uint8_t *)&t112 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&t110 + 16, t112);
            gm_b16_8 t113;
            GM_ST(void *, (uint8_t *)&t113, (void *)(gm_sflee3fpg0btv));
            GM_ST(uintptr_t, (uint8_t *)&t113 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&t110 + 32, t113);
            gm_tfeab279c16rr t114 = t110;
            uint8_t t115 = 0;
            gm_b16_8 t116 = t109;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t114) + 0, ((uint8_t *)&t116))) t115 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t114) + 16, ((uint8_t *)&t116))) t115 = 1;
            if (gm_eq0iqiubfm7e(((uint8_t *)&t114) + 32, ((uint8_t *)&t116))) t115 = 1;
            uint8_t t117 = (uint8_t)(t115);
            t106 = t117;
        } else t106 = 1;
        if (!t106) goto ie105;
        {
            gm_t6j6smj7qoctu t119;
            gm_b16_8 t120 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t119 + 0, t120);
            gm_b16_8 t121 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t119 + 16, t121);
            gm_tabb710d9mq2e t122;
            gm_f_format_mvlqlcp((void *)&t122, (void *)NULL, cx, NULL, &t119);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t123 = t122;
            if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 8, gm_srcg0ns322676oc, 178, 16);
            gm_b16_8 t124 = GM_LD(gm_b16_8, (((uint8_t *)&t123) + 8));
            gm_b16_8 t125 = t124;
            gm_b16_8 t126 = GM_LD(gm_b16_8, ((uint8_t *)&t125));
            gm_th2lqbo77pvsc t127;
            GM_ST(uint8_t, (uint8_t *)&t127 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t127 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t127 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t127), t126);
            ((uint8_t *)&t127)[16] = 1;
            uint8_t t128 = 0;
            F.ret = t127;
            goto gm_fret;
        }
        goto ix105;
ie105:;
ix105:;
    }
    {
        gm_b16_8 t130 = gm_l0;
        gm_t6j6smj7qoctu t131;
        gm_b16_8 t132;
        GM_ST(void *, (uint8_t *)&t132, (void *)(gm_s8sv6qdepk5ba));
        GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(3));
        GM_ST(gm_b16_8, (uint8_t *)&t131 + 0, t132);
        gm_b16_8 t133;
        GM_ST(void *, (uint8_t *)&t133, (void *)(gm_st3p5nb1e6cc8));
        GM_ST(uintptr_t, (uint8_t *)&t133 + 8, (uintptr_t)(4));
        GM_ST(gm_b16_8, (uint8_t *)&t131 + 16, t133);
        gm_t6j6smj7qoctu t134 = t131;
        uint8_t t135 = 0;
        gm_b16_8 t136 = t130;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t134) + 0, ((uint8_t *)&t136))) t135 = 1;
        if (gm_eq0iqiubfm7e(((uint8_t *)&t134) + 16, ((uint8_t *)&t136))) t135 = 1;
        uint8_t t137 = (uint8_t)(t135);
        if (!t137) goto ie129;
        {
            gm_t6j6smj7qoctu t139;
            gm_b16_8 t140 = gm_l1;
            GM_ST(gm_b16_8, (uint8_t *)&t139 + 0, t140);
            gm_b16_8 t141 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t139 + 16, t141);
            gm_tabb710d9mq2e t142;
            gm_f_format_gs0k8gk((void *)&t142, (void *)NULL, cx, NULL, &t139);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t143 = t142;
            if (((((uint8_t *)&t143))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t143) + 8, gm_srcg0ns322676oc, 181, 16);
            gm_b16_8 t144 = GM_LD(gm_b16_8, (((uint8_t *)&t143) + 8));
            gm_b16_8 t145 = t144;
            gm_b16_8 t146 = GM_LD(gm_b16_8, ((uint8_t *)&t145));
            gm_th2lqbo77pvsc t147;
            GM_ST(uint8_t, (uint8_t *)&t147 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t147 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t147 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t147), t146);
            ((uint8_t *)&t147)[16] = 1;
            uint8_t t148 = 0;
            F.ret = t147;
            goto gm_fret;
        }
        goto ix129;
ie129:;
ix129:;
    }
    {
        gm_th2lqbo77pvsc t149;
        memset(&t149, 0, sizeof t149);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_normalize_url_faghdja(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_normalize_url_faghdja F;
    gm_b16_8 gm_l0; /* url */
    gm_b16_8 gm_l1; /* u */
    GM_STACK_CHECK(&F, 404u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_b16_8 t3 = gm_f_trim_ascii_c9j36d1(t2);
        gm_l1 = t3;
    }
    {
        for (;;) {
            gm_b16_8 t5 = gm_l1;
            gm_b16_8 t6;
            GM_ST(void *, (uint8_t *)&t6, (void *)(gm_st3ja0nqi37bv));
            GM_ST(uintptr_t, (uint8_t *)&t6 + 8, (uintptr_t)(1));
            uint8_t t7 = gm_f_ends_with_biriu5l(t5, t6);
            if (!t7) goto lb4;
            {
                uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                void * t11 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                uint64_t t12 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t13 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint64_t r14; bool o15 = __builtin_sub_overflow(t13, ((uint64_t)1ULL), &r14);
                if (o15) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 239, 39);
                uint64_t t16 = r14;
                uint64_t t17 = (uintptr_t)t16;
                if (t12 > t17 || t17 > (uintptr_t)t10) gm_trap_slice(t12, t17, t10, gm_srcg0ns322676oc, 239, 34);
                gm_b16_8 t18;
                GM_ST(void *, (uint8_t *)&t18, (void *)(((uint8_t *)t11 + t12 * 1)));
                GM_ST(uintptr_t, (uint8_t *)&t18 + 8, (uintptr_t)(t17 - t12));
                gm_l1 = t18;
            }
lc4:;
        }
lb4:;
    }
    {
        gm_b16_8 t20 = gm_l1;
        gm_b16_8 t21;
        GM_ST(void *, (uint8_t *)&t21, (void *)(gm_sp7ec3ksfdilk));
        GM_ST(uintptr_t, (uint8_t *)&t21 + 8, (uintptr_t)(4));
        uint8_t t22 = gm_f_ends_with_biriu5l(t20, t21);
        if (!t22) goto ie19;
        {
            uint64_t t24 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            void * t25 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
            uint64_t t26 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
            uint64_t r28; bool o29 = __builtin_sub_overflow(t27, ((uint64_t)4ULL), &r28);
            if (o29) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 241, 18);
            uint64_t t30 = r28;
            uint64_t t31 = (uintptr_t)t30;
            if (t26 > t31 || t31 > (uintptr_t)t24) gm_trap_slice(t26, t31, t24, gm_srcg0ns322676oc, 241, 13);
            gm_b16_8 t32;
            GM_ST(void *, (uint8_t *)&t32, (void *)(((uint8_t *)t25 + t26 * 1)));
            GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(t31 - t26));
            gm_l1 = t32;
        }
        {
            for (;;) {
                gm_b16_8 t34 = gm_l1;
                gm_b16_8 t35;
                GM_ST(void *, (uint8_t *)&t35, (void *)(gm_st3ja0nqi37bv));
                GM_ST(uintptr_t, (uint8_t *)&t35 + 8, (uintptr_t)(1));
                uint8_t t36 = gm_f_ends_with_biriu5l(t34, t35);
                if (!t36) goto lb33;
                {
                    uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    void * t40 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)));
                    uint64_t t41 = (uintptr_t)((uint64_t)0ULL);
                    uint64_t t42 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint64_t r43; bool o44 = __builtin_sub_overflow(t42, ((uint64_t)1ULL), &r43);
                    if (o44) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 242, 43);
                    uint64_t t45 = r43;
                    uint64_t t46 = (uintptr_t)t45;
                    if (t41 > t46 || t46 > (uintptr_t)t39) gm_trap_slice(t41, t46, t39, gm_srcg0ns322676oc, 242, 38);
                    gm_b16_8 t47;
                    GM_ST(void *, (uint8_t *)&t47, (void *)(((uint8_t *)t40 + t41 * 1)));
                    GM_ST(uintptr_t, (uint8_t *)&t47 + 8, (uintptr_t)(t46 - t41));
                    gm_l1 = t47;
                }
lc33:;
            }
lb33:;
        }
        goto ix19;
ie19:;
ix19:;
    }
    {
        gm_b16_8 t48 = gm_l1;
        uint8_t t49 = 0;
        F.ret = t48;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_overlay_pjf5fej(void *gm_out, const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_overlay_pjf5fej F;
    GM_STACK_CHECK(&F, 592u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        memset((uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 4, 0, 4);
        gm_taari1s0m51a7 t2 = GM_LD(gm_taari1s0m51a7, (((uint8_t *)a1) + 0));
        gm_taari1s0m51a7 t3 = t2;
        gm_taari1s0m51a7 t4;
        if (((((uint8_t *)&t3))[1] != 0)) {
            t4 = t2;
        } else {
            gm_taari1s0m51a7 t5 = GM_LD(gm_taari1s0m51a7, (((uint8_t *)a0) + 0));
            t4 = t5;
        }
        gm_tqs5p4n2tq19v t6 = GM_LD(gm_tqs5p4n2tq19v, (((uint8_t *)a1) + 2));
        gm_tqs5p4n2tq19v t7 = t6;
        gm_tqs5p4n2tq19v t8;
        if (((((uint8_t *)&t7))[1] != 0)) {
            t8 = t6;
        } else {
            gm_tqs5p4n2tq19v t9 = GM_LD(gm_tqs5p4n2tq19v, (((uint8_t *)a0) + 2));
            t8 = t9;
        }
        gm_th2lqbo77pvsc t10 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 8));
        gm_th2lqbo77pvsc t11 = t10;
        gm_th2lqbo77pvsc t12;
        if (((((uint8_t *)&t11))[16] != 0)) {
            t12 = t10;
        } else {
            gm_th2lqbo77pvsc t13 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 8));
            t12 = t13;
        }
        gm_th2lqbo77pvsc t14 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 32));
        gm_th2lqbo77pvsc t15 = t14;
        gm_th2lqbo77pvsc t16;
        if (((((uint8_t *)&t15))[16] != 0)) {
            t16 = t14;
        } else {
            gm_th2lqbo77pvsc t17 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 32));
            t16 = t17;
        }
        gm_th2lqbo77pvsc t18 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 56));
        gm_th2lqbo77pvsc t19 = t18;
        gm_th2lqbo77pvsc t20;
        if (((((uint8_t *)&t19))[16] != 0)) {
            t20 = t18;
        } else {
            gm_th2lqbo77pvsc t21 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 56));
            t20 = t21;
        }
        gm_th2lqbo77pvsc t22 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 80));
        gm_th2lqbo77pvsc t23 = t22;
        gm_th2lqbo77pvsc t24;
        if (((((uint8_t *)&t23))[16] != 0)) {
            t24 = t22;
        } else {
            gm_th2lqbo77pvsc t25 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 80));
            t24 = t25;
        }
        gm_th2lqbo77pvsc t26 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 104));
        gm_th2lqbo77pvsc t27 = t26;
        gm_th2lqbo77pvsc t28;
        if (((((uint8_t *)&t27))[16] != 0)) {
            t28 = t26;
        } else {
            gm_th2lqbo77pvsc t29 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 104));
            t28 = t29;
        }
        gm_th2lqbo77pvsc t30 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a1) + 128));
        gm_th2lqbo77pvsc t31 = t30;
        gm_th2lqbo77pvsc t32;
        if (((((uint8_t *)&t31))[16] != 0)) {
            t32 = t30;
        } else {
            gm_th2lqbo77pvsc t33 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)a0) + 128));
            t32 = t33;
        }
        GM_ST(gm_taari1s0m51a7, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 0, t4);
        GM_ST(gm_tqs5p4n2tq19v, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 2, t8);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 8, t12);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 32, t16);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 56, t20);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 80, t24);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 104, t28);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&(*(gm_ti0g3bmheuq9o *)gm_out) + 128, t32);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_ti0g3bmheuq9o gm_f_overlay_pjf5fej_c(gm_ti0g3bmheuq9o a0, gm_ti0g3bmheuq9o a1) { gm_ti0g3bmheuq9o gm_r; gm_f_overlay_pjf5fej(&gm_r, &a0, &a1); return gm_r; }
__attribute__((aligned(4))) void gm_f_parse_build_c2teosa(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1) {
    struct gm_fr_parse_build_c2teosa F;
    void * gm_l0; /* t */
    gm_b16_8 gm_l1; /* what */
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    uint64_t gm_l6; /* line */
    gm_b16_8 gm_l7; /* k */
    uint8_t gm_l8; /* known */
    gm_b16_8 gm_l9; /* <tmp> */
    uint64_t gm_l10; /* <tmp> */
    gm_b16_8 gm_l11; /* x */
    gm_b16_8 gm_l12; /* s */
    uint8_t gm_l13; /* v */
    gm_b16_8 gm_l14; /* l */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    gm_b16_8 gm_l17; /* p */
    gm_b16_8 gm_l18; /* l */
    gm_b16_8 gm_l19; /* <tmp> */
    uint64_t gm_l20; /* <tmp> */
    gm_b16_8 gm_l21; /* n */
    uint8_t gm_l22; /* bad */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    uint8_t gm_l25; /* c */
    GM_STACK_CHECK(&F, 6103u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        memset((uint8_t *)&F.l2 + 4, 0, 4);
        gm_taari1s0m51a7 t2;
        memset(&t2, 0, sizeof t2);
        GM_ST(gm_taari1s0m51a7, (uint8_t *)&F.l2 + 0, t2);
        gm_tqs5p4n2tq19v t3;
        memset(&t3, 0, sizeof t3);
        GM_ST(gm_tqs5p4n2tq19v, (uint8_t *)&F.l2 + 2, t3);
        gm_th2lqbo77pvsc t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 8, t4);
        gm_th2lqbo77pvsc t5;
        memset(&t5, 0, sizeof t5);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 32, t5);
        gm_th2lqbo77pvsc t6;
        memset(&t6, 0, sizeof t6);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 56, t6);
        gm_th2lqbo77pvsc t7;
        memset(&t7, 0, sizeof t7);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 80, t7);
        gm_th2lqbo77pvsc t8;
        memset(&t8, 0, sizeof t8);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 104, t8);
        gm_th2lqbo77pvsc t9;
        memset(&t9, 0, sizeof t9);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l2 + 128, t9);
    }
    {
        {
            void * t11 = gm_l0;
            gm_b16_8 t12 = GM_LD(gm_b16_8, ((((uint8_t *)t11) + 0) + 0));
            gm_l3 = t12;
        }
        {
            gm_l4 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t14 = gm_l4;
                uint64_t t15 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                uint8_t t16 = (t14 < t15);
                if (!t16) goto lb13;
                {
                    uint64_t t19 = gm_l4;
                    uint64_t t20 = (uintptr_t)t19;
                    uint64_t t21 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    gm_trc3jdv2g5qre t24 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t20 * 72));
                    F.l5 = t24;
                    {
                        uint64_t t26 = GM_LD(uint64_t, (((uint8_t *)&F.l5) + 64));
                        gm_l6 = t26;
                    }
                    {
                        gm_b16_8 t27 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
                        gm_l7 = t27;
                    }
                    {
                        gm_l8 = ((uint8_t)0);
                    }
                    {
                        {
                            void * t29 = ((uint8_t *)gm_cfcggcra0bi);
                            if ((uintptr_t)0 > (uintptr_t)8 || (uintptr_t)8 > (uintptr_t)8) gm_trap_slice((uintptr_t)0, (uintptr_t)8, 8, gm_srcg0ns322676oc, 418, 9);
                            gm_b16_8 t30;
                            GM_ST(void *, (uint8_t *)&t30, (void *)(((uint8_t *)t29 + (uintptr_t)0 * 16)));
                            GM_ST(uintptr_t, (uint8_t *)&t30 + 8, (uintptr_t)((uintptr_t)8 - (uintptr_t)0));
                            gm_l9 = t30;
                        }
                        {
                            gm_l10 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t32 = gm_l10;
                                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                uint8_t t34 = (t32 < t33);
                                if (!t34) goto lb31;
                                {
                                    uint64_t t37 = gm_l10;
                                    uint64_t t38 = (uintptr_t)t37;
                                    uint64_t t39 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                                    gm_b16_8 t42 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t38 * 16));
                                    gm_l11 = t42;
                                    {
                                        gm_b16_8 t45 = gm_l11;
                                        gm_b16_8 t46 = gm_l7;
                                        gm_b16_8 t47 = t45;
                                        gm_b16_8 t48 = t46;
                                        uint8_t t49 = (gm_eq0iqiubfm7e(((uint8_t *)&t47), ((uint8_t *)&t48)));
                                        if (!t49) goto ie44;
                                        {
                                            gm_l8 = ((uint8_t)1);
                                        }
                                        goto ix44;
ie44:;
ix44:;
                                    }
                                    goto me40;
mn41:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 418, 9);
me40:;
                                }
lc31:;
                                {
                                    uint64_t t52 = gm_l10;
                                    uint64_t r53; bool o54 = __builtin_add_overflow(t52, ((uint64_t)1ULL), &r53);
                                    uint64_t t55 = r53;
                                    gm_l10 = t55;
                                }
                            }
lb31:;
                        }
                    }
                    {
                        uint8_t t57 = gm_l8;
                        uint8_t t58 = (uint8_t)!t57;
                        if (!t58) goto ie56;
                        {
                            uint64_t t60 = gm_l6;
                            gm_tfeab279c16rr t61;
                            gm_b16_8 t62 = gm_l1;
                            GM_ST(gm_b16_8, (uint8_t *)&t61 + 0, t62);
                            gm_b16_8 t63 = gm_l7;
                            GM_ST(gm_b16_8, (uint8_t *)&t61 + 16, t63);
                            void * t64 = ((uint8_t *)gm_cfcggcra0bi);
                            if ((uintptr_t)0 > (uintptr_t)8 || (uintptr_t)8 > (uintptr_t)8) gm_trap_slice((uintptr_t)0, (uintptr_t)8, 8, gm_srcg0ns322676oc, 422, 109);
                            gm_b16_8 t65;
                            GM_ST(void *, (uint8_t *)&t65, (void *)(((uint8_t *)t64 + (uintptr_t)0 * 16)));
                            GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)((uintptr_t)8 - (uintptr_t)0));
                            gm_b16_8 t66;
                            GM_ST(void *, (uint8_t *)&t66, (void *)(gm_sg5gdnrith2i6));
                            GM_ST(uintptr_t, (uint8_t *)&t66 + 8, (uintptr_t)(2));
                            gm_tabb710d9mq2e t67;
                            gm_f_join_sfknljb((void *)&t67, (void *)NULL, cx, NULL, t65, t66);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_tabb710d9mq2e t68 = t67;
                            if (((((uint8_t *)&t68))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t68) + 8, gm_srcg0ns322676oc, 422, 100);
                            gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&t68) + 8));
                            GM_ST(gm_b16_8, (uint8_t *)&t61 + 32, t69);
                            gm_tabb710d9mq2e t70;
                            gm_f_format_23qvjoj((void *)&t70, (void *)NULL, cx, NULL, &t61);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_tabb710d9mq2e t71 = t70;
                            if (((((uint8_t *)&t71))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t71) + 8, gm_srcg0ns322676oc, 422, 33);
                            gm_b16_8 t72 = GM_LD(gm_b16_8, (((uint8_t *)&t71) + 8));
                            gm_b16_8 t73 = t72;
                            gm_b16_8 t74 = GM_LD(gm_b16_8, ((uint8_t *)&t73));
                            gm_tf1bjg81u0glv t75;
                            gm_f_mfail_uqq7o7g((void *)&t75, t60, t74);
                            gm_tacj1cskem694 t76;
                            memset(&t76, 0, sizeof t76);
                            ((uint8_t *)&t76)[0] = 1;
                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t76 + 8), t75);
                            gm_tacj1cskem694 t77 = t76;
                            uint8_t t78 = ((((uint8_t *)&t77))[0] != 0);
                            F.ret = t76;
                            goto gm_fret;
                        }
                        goto ix56;
ie56:;
ix56:;
                    }
                    {
                        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_sgdafdv2n5c25, 4) == 0)) goto mn80;
                        {
                            gm_th2lqbo77pvsc t82;
                            gm_f_as_str_982o16v((void *)&t82, &(*(gm_tktr4rmbindgv *)(((uint8_t *)&F.l5) + 16)));
                            gm_th2lqbo77pvsc t83 = t82;
                            gm_b16_8 t84;
                            if (((((uint8_t *)&t83))[16] != 0)) {
                                gm_b16_8 t85 = GM_LD(gm_b16_8, (((uint8_t *)&t83) + 0));
                                t84 = t85;
                            } else {
                                uint64_t t86 = gm_l6;
                                gm_b16_8 t87 = gm_l1;
                                gm_tnpifv20irnph t88;
                                GM_ST(gm_b16_8, ((uint8_t *)&t88 + 0), t87);
                                gm_tabb710d9mq2e t89;
                                gm_f_format_4eslq57((void *)&t89, (void *)NULL, cx, NULL, t88);
                                if (gm_unw.kind) {
                                    goto gm_ll17;
                                }
                                gm_tabb710d9mq2e t90 = t89;
                                if (((((uint8_t *)&t90))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t90) + 8, gm_srcg0ns322676oc, 426, 63);
                                gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&t90) + 8));
                                gm_b16_8 t92 = t91;
                                gm_b16_8 t93 = GM_LD(gm_b16_8, ((uint8_t *)&t92));
                                gm_tf1bjg81u0glv t94;
                                gm_f_mfail_uqq7o7g((void *)&t94, t86, t93);
                                gm_tacj1cskem694 t95;
                                memset(&t95, 0, sizeof t95);
                                ((uint8_t *)&t95)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t95 + 8), t94);
                                gm_tacj1cskem694 t96 = t95;
                                uint8_t t97 = ((((uint8_t *)&t96))[0] != 0);
                                F.ret = t95;
                                goto gm_fret;
                            }
                            gm_l12 = t84;
                        }
                        {
                            gm_taari1s0m51a7 t98;
                            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8) == 5) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l12)), gm_shl9ceh0q5tpe, 5) == 0)) goto mn100;
                            gm_t9qkh0rsooc8d t101;
                            GM_ST(uint8_t, ((uint8_t *)&t101 + 0), ((uint8_t)0ULL));
                            gm_taari1s0m51a7 t102;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t102), t101);
                            ((uint8_t *)&t102)[1] = 1;
                            t98 = t102;
                            goto me99;
mn100:;
                            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l12)), gm_s02419ud122hi, 7) == 0)) goto mn103;
                            gm_t9qkh0rsooc8d t104;
                            GM_ST(uint8_t, ((uint8_t *)&t104 + 0), ((uint8_t)1ULL));
                            gm_taari1s0m51a7 t105;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t105), t104);
                            ((uint8_t *)&t105)[1] = 1;
                            t98 = t105;
                            goto me99;
mn103:;
                            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l12)), gm_s6iuue4hdrbt9, 4) == 0)) goto mn106;
                            gm_t9qkh0rsooc8d t107;
                            GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)2ULL));
                            gm_taari1s0m51a7 t108;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t108), t107);
                            ((uint8_t *)&t108)[1] = 1;
                            t98 = t108;
                            goto me99;
mn106:;
                            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8) == 4) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l12)), gm_sl0oflhpdomi7, 4) == 0)) goto mn109;
                            gm_t9qkh0rsooc8d t110;
                            GM_ST(uint8_t, ((uint8_t *)&t110 + 0), ((uint8_t)3ULL));
                            gm_taari1s0m51a7 t111;
                            GM_ST(gm_t9qkh0rsooc8d, ((uint8_t *)&t111), t110);
                            ((uint8_t *)&t111)[1] = 1;
                            t98 = t111;
                            goto me99;
mn109:;
                            uint64_t t113 = gm_l6;
                            gm_t6j6smj7qoctu t114;
                            gm_b16_8 t115 = gm_l12;
                            GM_ST(gm_b16_8, (uint8_t *)&t114 + 0, t115);
                            gm_b16_8 t116 = gm_l1;
                            GM_ST(gm_b16_8, (uint8_t *)&t114 + 16, t116);
                            gm_tabb710d9mq2e t117;
                            gm_f_format_mcrh28a((void *)&t117, (void *)NULL, cx, NULL, &t114);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_tabb710d9mq2e t118 = t117;
                            if (((((uint8_t *)&t118))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t118) + 8, gm_srcg0ns322676oc, 432, 46);
                            gm_b16_8 t119 = GM_LD(gm_b16_8, (((uint8_t *)&t118) + 8));
                            gm_b16_8 t120 = t119;
                            gm_b16_8 t121 = GM_LD(gm_b16_8, ((uint8_t *)&t120));
                            gm_tf1bjg81u0glv t122;
                            gm_f_mfail_uqq7o7g((void *)&t122, t113, t121);
                            gm_tacj1cskem694 t123;
                            memset(&t123, 0, sizeof t123);
                            ((uint8_t *)&t123)[0] = 1;
                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t123 + 8), t122);
                            gm_tacj1cskem694 t124 = t123;
                            uint8_t t125 = ((((uint8_t *)&t124))[0] != 0);
                            F.ret = t123;
                            goto gm_fret;
                            goto me99;
mn112:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 427, 26);
me99:;
                            GM_ST(gm_taari1s0m51a7, (((uint8_t *)&F.l2) + 0), t98);
                        }
                        goto me79;
mn80:;
                        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 10) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_sq6e9d3107t3s, 10) == 0)) goto mn126;
                        {
                            uint8_t t129 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 16) + 0));
                            if (t129 != ((uint8_t)2ULL)) goto ie128;
                            uint8_t t130 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 16) + 8));
                            gm_l13 = t130;
                            {
                                uint8_t t132 = gm_l13;
                                gm_tqs5p4n2tq19v t133;
                                GM_ST(uint8_t, ((uint8_t *)&t133), t132);
                                ((uint8_t *)&t133)[1] = 1;
                                GM_ST(gm_tqs5p4n2tq19v, (((uint8_t *)&F.l2) + 2), t133);
                            }
                            goto ix128;
ie128:;
                            {
                                uint64_t t135 = gm_l6;
                                gm_b16_8 t136 = gm_l1;
                                gm_tnpifv20irnph t137;
                                GM_ST(gm_b16_8, ((uint8_t *)&t137 + 0), t136);
                                gm_tabb710d9mq2e t138;
                                gm_f_format_6cnv7j9((void *)&t138, (void *)NULL, cx, NULL, t137);
                                if (gm_unw.kind) {
                                    goto gm_ll17;
                                }
                                gm_tabb710d9mq2e t139 = t138;
                                if (((((uint8_t *)&t139))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t139) + 8, gm_srcg0ns322676oc, 436, 62);
                                gm_b16_8 t140 = GM_LD(gm_b16_8, (((uint8_t *)&t139) + 8));
                                gm_b16_8 t141 = t140;
                                gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)&t141));
                                gm_tf1bjg81u0glv t143;
                                gm_f_mfail_uqq7o7g((void *)&t143, t135, t142);
                                gm_tacj1cskem694 t144;
                                memset(&t144, 0, sizeof t144);
                                ((uint8_t *)&t144)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t144 + 8), t143);
                                gm_tacj1cskem694 t145 = t144;
                                uint8_t t146 = ((((uint8_t *)&t145))[0] != 0);
                                F.ret = t144;
                                goto gm_fret;
                            }
ix128:;
                        }
                        goto me79;
mn126:;
                        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 6) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_sp6f2rgoi8toe, 6) == 0)) goto mn147;
                        {
                            gm_b16_8 t149 = gm_l1;
                            gm_tptmds653bni0 t150;
                            gm_f_build_list_f41mb8u((void *)&t150, (void *)NULL, cx, NULL, &(*(gm_trc3jdv2g5qre *)((uint8_t *)&F.l5)), t149);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_tptmds653bni0 t151 = t150;
                            if (((((uint8_t *)&t151))[0] != 0)) {
                                gm_tf1bjg81u0glv t152 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t151) + 8));
                                gm_tacj1cskem694 t153;
                                memset(&t153, 0, sizeof t153);
                                ((uint8_t *)&t153)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t153 + 8), t152);
                                F.ret = t153;
                                goto gm_fret;
                            }
                            gm_b16_8 t154 = GM_LD(gm_b16_8, (((uint8_t *)&t151) + 8));
                            gm_th2lqbo77pvsc t155;
                            GM_ST(uint8_t, (uint8_t *)&t155 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t155 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t155 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t155), t154);
                            ((uint8_t *)&t155)[16] = 1;
                            GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 8), t155);
                        }
                        goto me79;
mn147:;
                        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 7) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_slfpvqm0f02j6, 7) == 0)) goto mn156;
                        {
                            gm_b16_8 t158 = gm_l1;
                            gm_tptmds653bni0 t159;
                            gm_f_build_list_f41mb8u((void *)&t159, (void *)NULL, cx, NULL, &(*(gm_trc3jdv2g5qre *)((uint8_t *)&F.l5)), t158);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_tptmds653bni0 t160 = t159;
                            if (((((uint8_t *)&t160))[0] != 0)) {
                                gm_tf1bjg81u0glv t161 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t160) + 8));
                                gm_tacj1cskem694 t162;
                                memset(&t162, 0, sizeof t162);
                                ((uint8_t *)&t162)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t162 + 8), t161);
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
                            GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 32), t164);
                        }
                        goto me79;
mn156:;
                        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 9) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_srcf8l485c5uq, 9) == 0)) goto pa167;
                        goto po166;
pa167:;
                        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 12) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_sdbqk29a3n8jm, 12) == 0)) goto pa168;
                        goto po166;
pa168:;
                        if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 8) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_svkmjs6p8bhtg, 8) == 0)) goto pa169;
                        goto po166;
pa169:;
                        goto mn165;
po166:;
                        {
                            gm_b16_8 t171 = gm_l1;
                            gm_tptmds653bni0 t172;
                            gm_f_build_list_f41mb8u((void *)&t172, (void *)NULL, cx, NULL, &(*(gm_trc3jdv2g5qre *)((uint8_t *)&F.l5)), t171);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_tptmds653bni0 t173 = t172;
                            if (((((uint8_t *)&t173))[0] != 0)) {
                                gm_tf1bjg81u0glv t174 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t173) + 8));
                                gm_tacj1cskem694 t175;
                                memset(&t175, 0, sizeof t175);
                                ((uint8_t *)&t175)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t175 + 8), t174);
                                F.ret = t175;
                                goto gm_fret;
                            }
                            gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&t173) + 8));
                            gm_l14 = t176;
                        }
                        {
                            {
                                gm_b16_8 t178 = gm_l14;
                                gm_l15 = t178;
                            }
                            {
                                gm_l16 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t180 = gm_l16;
                                    uint64_t t181 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                    uint8_t t182 = (t180 < t181);
                                    if (!t182) goto lb179;
                                    {
                                        uint64_t t185 = gm_l16;
                                        uint64_t t186 = (uintptr_t)t185;
                                        uint64_t t187 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                                        gm_b16_8 t190 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t186 * 16));
                                        gm_l17 = t190;
                                        {
                                            uint64_t t193 = GM_LD(uintptr_t, ((uint8_t *)&gm_l17) + 8);
                                            uint8_t t194 = ((t193 == ((uint64_t)0ULL)));
                                            if (!t194) goto ie192;
                                            {
                                                uint64_t t196 = gm_l6;
                                                gm_t6j6smj7qoctu t197;
                                                gm_b16_8 t198 = gm_l7;
                                                GM_ST(gm_b16_8, (uint8_t *)&t197 + 0, t198);
                                                gm_b16_8 t199 = gm_l1;
                                                GM_ST(gm_b16_8, (uint8_t *)&t197 + 16, t199);
                                                gm_tabb710d9mq2e t200;
                                                gm_f_format_6ld92gp((void *)&t200, (void *)NULL, cx, NULL, &t197);
                                                if (gm_unw.kind) {
                                                    goto gm_ll183;
                                                }
                                                gm_tabb710d9mq2e t201 = t200;
                                                if (((((uint8_t *)&t201))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t201) + 8, gm_srcg0ns322676oc, 444, 57);
                                                gm_b16_8 t202 = GM_LD(gm_b16_8, (((uint8_t *)&t201) + 8));
                                                gm_b16_8 t203 = t202;
                                                gm_b16_8 t204 = GM_LD(gm_b16_8, ((uint8_t *)&t203));
                                                gm_tf1bjg81u0glv t205;
                                                gm_f_mfail_uqq7o7g((void *)&t205, t196, t204);
                                                gm_tacj1cskem694 t206;
                                                memset(&t206, 0, sizeof t206);
                                                ((uint8_t *)&t206)[0] = 1;
                                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t206 + 8), t205);
                                                gm_tacj1cskem694 t207 = t206;
                                                uint8_t t208 = ((((uint8_t *)&t207))[0] != 0);
                                                F.ret = t206;
                                                goto gm_fret;
                                            }
                                            goto ix192;
ie192:;
ix192:;
                                        }
                                        goto me188;
mn189:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 443, 17);
me188:;
                                    }
lc179:;
                                    {
                                        uint64_t t210 = gm_l16;
                                        uint64_t r211; bool o212 = __builtin_add_overflow(t210, ((uint64_t)1ULL), &r211);
                                        uint64_t t213 = r211;
                                        gm_l16 = t213;
                                    }
                                }
gm_ll183:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb179; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc179; }
                                }
                                goto gm_ll17;
lb179:;
                            }
                        }
                        {
                            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 9) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_srcf8l485c5uq, 9) == 0)) goto mn215;
                            {
                                gm_b16_8 t217 = gm_l14;
                                gm_th2lqbo77pvsc t218;
                                GM_ST(uint8_t, (uint8_t *)&t218 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t218 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t218 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t218), t217);
                                ((uint8_t *)&t218)[16] = 1;
                                GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 56), t218);
                            }
                            goto me214;
mn215:;
                            if (!((GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8) == 12) && memcmp(GM_LD(void *, ((uint8_t *)&gm_l7)), gm_sdbqk29a3n8jm, 12) == 0)) goto mn219;
                            {
                                gm_b16_8 t221 = gm_l14;
                                gm_th2lqbo77pvsc t222;
                                GM_ST(uint8_t, (uint8_t *)&t222 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t222 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t222 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t222), t221);
                                ((uint8_t *)&t222)[16] = 1;
                                GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 80), t222);
                            }
                            goto me214;
mn219:;
                            {
                                gm_b16_8 t225 = gm_l14;
                                gm_th2lqbo77pvsc t226;
                                GM_ST(uint8_t, (uint8_t *)&t226 + 17, 0);
                                GM_ST(uint16_t, (uint8_t *)&t226 + 18, 0);
                                GM_ST(uint32_t, (uint8_t *)&t226 + 20, 0);
                                GM_ST(gm_b16_8, ((uint8_t *)&t226), t225);
                                ((uint8_t *)&t226)[16] = 1;
                                GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 128), t226);
                            }
                            goto me214;
mn223:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 446, 17);
me214:;
                        }
                        goto me79;
mn165:;
                        {
                            gm_b16_8 t229 = gm_l1;
                            gm_tptmds653bni0 t230;
                            gm_f_build_list_f41mb8u((void *)&t230, (void *)NULL, cx, NULL, &(*(gm_trc3jdv2g5qre *)((uint8_t *)&F.l5)), t229);
                            if (gm_unw.kind) {
                                goto gm_ll17;
                            }
                            gm_tptmds653bni0 t231 = t230;
                            if (((((uint8_t *)&t231))[0] != 0)) {
                                gm_tf1bjg81u0glv t232 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t231) + 8));
                                gm_tacj1cskem694 t233;
                                memset(&t233, 0, sizeof t233);
                                ((uint8_t *)&t233)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t233 + 8), t232);
                                F.ret = t233;
                                goto gm_fret;
                            }
                            gm_b16_8 t234 = GM_LD(gm_b16_8, (((uint8_t *)&t231) + 8));
                            gm_l18 = t234;
                        }
                        {
                            {
                                gm_b16_8 t236 = gm_l18;
                                gm_l19 = t236;
                            }
                            {
                                gm_l20 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t238 = gm_l20;
                                    uint64_t t239 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                    uint8_t t240 = (t238 < t239);
                                    if (!t240) goto lb237;
                                    {
                                        uint64_t t243 = gm_l20;
                                        uint64_t t244 = (uintptr_t)t243;
                                        uint64_t t245 = GM_LD(uintptr_t, ((uint8_t *)&gm_l19) + 8);
                                        gm_b16_8 t248 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l19)) + t244 * 16));
                                        gm_l21 = t248;
                                        {
                                            uint8_t t250 = 0;
                                            uint64_t t251 = GM_LD(uintptr_t, ((uint8_t *)&gm_l21) + 8);
                                            uint8_t t252 = ((t251 == ((uint64_t)0ULL)));
                                            if (!t252) {
                                                gm_b16_8 t253 = gm_l21;
                                                gm_b16_8 t254;
                                                GM_ST(void *, (uint8_t *)&t254, (void *)(gm_skolmnfc0ccbd));
                                                GM_ST(uintptr_t, (uint8_t *)&t254 + 8, (uintptr_t)(1));
                                                uint8_t t255 = gm_f_starts_with_6vvt01e(t253, t254);
                                                t250 = t255;
                                            } else t250 = 1;
                                            gm_l22 = t250;
                                        }
                                        {
                                            {
                                                gm_b16_8 t257 = gm_l21;
                                                gm_l23 = t257;
                                            }
                                            {
                                                gm_l24 = ((uint64_t)0ULL);
                                            }
                                            {
                                                for (;;) {
                                                    uint64_t t259 = gm_l24;
                                                    uint64_t t260 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                                    uint8_t t261 = (t259 < t260);
                                                    if (!t261) goto lb258;
                                                    {
                                                        uint64_t t264 = gm_l24;
                                                        uint64_t t265 = (uintptr_t)t264;
                                                        uint64_t t266 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                                                        uint8_t t267 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t265 * 1));
                                                        uint8_t t268 = t267;
                                                        uint8_t t271 = GM_LD(uint8_t, ((uint8_t *)&t268));
                                                        gm_l25 = t271;
                                                        {
                                                            uint8_t t274 = 0;
                                                            uint8_t t275 = gm_l25;
                                                            uint8_t t276 = gm_f_is_space_uen38t5(t275);
                                                            if (!t276) {
                                                                uint8_t t277 = gm_l25;
                                                                uint8_t t278 = ((t277 == ((uint8_t)47ULL)));
                                                                t274 = t278;
                                                            } else t274 = 1;
                                                            if (!t274) goto ie273;
                                                            {
                                                                gm_l22 = ((uint8_t)1);
                                                            }
                                                            goto ix273;
ie273:;
ix273:;
                                                        }
                                                        goto me269;
mn270:;
                                                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 456, 21);
me269:;
                                                    }
lc258:;
                                                    {
                                                        uint64_t t281 = gm_l24;
                                                        uint64_t r282; bool o283 = __builtin_add_overflow(t281, ((uint64_t)1ULL), &r282);
                                                        uint64_t t284 = r282;
                                                        gm_l24 = t284;
                                                    }
                                                }
lb258:;
                                            }
                                        }
                                        {
                                            uint8_t t286 = gm_l22;
                                            if (!t286) goto ie285;
                                            {
                                                uint64_t t288 = gm_l6;
                                                gm_t6j6smj7qoctu t289;
                                                gm_b16_8 t290 = gm_l21;
                                                GM_ST(gm_b16_8, (uint8_t *)&t289 + 0, t290);
                                                gm_b16_8 t291 = gm_l1;
                                                GM_ST(gm_b16_8, (uint8_t *)&t289 + 16, t291);
                                                gm_tabb710d9mq2e t292;
                                                gm_f_format_jv6n7oh((void *)&t292, (void *)NULL, cx, NULL, &t289);
                                                if (gm_unw.kind) {
                                                    goto gm_ll241;
                                                }
                                                gm_tabb710d9mq2e t293 = t292;
                                                if (((((uint8_t *)&t293))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t293) + 8, gm_srcg0ns322676oc, 460, 45);
                                                gm_b16_8 t294 = GM_LD(gm_b16_8, (((uint8_t *)&t293) + 8));
                                                gm_b16_8 t295 = t294;
                                                gm_b16_8 t296 = GM_LD(gm_b16_8, ((uint8_t *)&t295));
                                                gm_tf1bjg81u0glv t297;
                                                gm_f_mfail_uqq7o7g((void *)&t297, t288, t296);
                                                gm_tacj1cskem694 t298;
                                                memset(&t298, 0, sizeof t298);
                                                ((uint8_t *)&t298)[0] = 1;
                                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t298 + 8), t297);
                                                gm_tacj1cskem694 t299 = t298;
                                                uint8_t t300 = ((((uint8_t *)&t299))[0] != 0);
                                                F.ret = t298;
                                                goto gm_fret;
                                            }
                                            goto ix285;
ie285:;
ix285:;
                                        }
                                        goto me246;
mn247:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 454, 17);
me246:;
                                    }
lc237:;
                                    {
                                        uint64_t t302 = gm_l20;
                                        uint64_t r303; bool o304 = __builtin_add_overflow(t302, ((uint64_t)1ULL), &r303);
                                        uint64_t t305 = r303;
                                        gm_l20 = t305;
                                    }
                                }
gm_ll241:;
                                if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb237; }
                                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc237; }
                                }
                                goto gm_ll17;
lb237:;
                            }
                        }
                        {
                            gm_b16_8 t306 = gm_l18;
                            gm_th2lqbo77pvsc t307;
                            GM_ST(uint8_t, (uint8_t *)&t307 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t307 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t307 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t307), t306);
                            ((uint8_t *)&t307)[16] = 1;
                            GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l2) + 104), t307);
                        }
                        goto me79;
mn227:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 424, 9);
me79:;
                    }
                    goto me22;
mn23:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 414, 5);
me22:;
                }
lc13:;
                {
                    uint64_t t309 = gm_l4;
                    uint64_t r310; bool o311 = __builtin_add_overflow(t309, ((uint64_t)1ULL), &r310);
                    uint64_t t312 = r310;
                    gm_l4 = t312;
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
        gm_ti0g3bmheuq9o t313 = F.l2;
        gm_tacj1cskem694 t314;
        GM_ST(uint8_t, (uint8_t *)&t314 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t314 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t314 + 4, 0);
        ((uint8_t *)&t314)[0] = 0;
        GM_ST(gm_ti0g3bmheuq9o, ((uint8_t *)&t314 + 8), t313);
        gm_tacj1cskem694 t315 = t314;
        uint8_t t316 = ((((uint8_t *)&t315))[0] != 0);
        F.ret = t314;
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
__attribute__((aligned(4))) void gm_f_parse_dependency_8qmcjrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, const void * a1, uint64_t a2) {
    struct gm_fr_parse_dependency_8qmcjrm F;
    gm_b16_8 gm_l0; /* name */
    uint64_t gm_l2; /* line */
    void * gm_l3; /* t */
    gm_b16_8 gm_l4; /* <tmp> */
    uint64_t gm_l5; /* <tmp> */
    uint8_t gm_l7; /* known */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    gm_b16_8 gm_l10; /* k */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l16; /* k */
    gm_b16_8 gm_l17; /* url0 */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    gm_b16_8 gm_l22; /* k */
    gm_b16_8 gm_l23; /* url */
    gm_b16_8 gm_l24; /* p */
    gm_th2lqbo77pvsc gm_l25; /* sha */
    gm_b16_8 gm_l26; /* h0 */
    gm_b16_8 gm_l27; /* h1 */
    gm_b16_8 gm_l28; /* h */
    uint8_t gm_l29; /* ok */
    gm_b16_8 gm_l30; /* <tmp> */
    uint64_t gm_l31; /* <tmp> */
    uint8_t gm_l32; /* c */
    gm_th2lqbo77pvsc gm_l33; /* subdir */
    gm_th2lqbo77pvsc gm_l34; /* path */
    gm_th2lqbo77pvsc gm_l35; /* git_url */
    gm_b16_8 gm_l36; /* p */
    gm_b16_8 gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    gm_b16_8 gm_l41; /* k */
    gm_b16_8 gm_l42; /* url */
    gm_b16_8 gm_l43; /* p */
    gm_b16_8 gm_l47; /* <tmp> */
    uint64_t gm_l48; /* <tmp> */
    gm_b16_8 gm_l49; /* k */
    gm_b16_8 gm_l51; /* v */
    gm_b16_8 gm_l52; /* e */
    gm_b16_8 gm_l54; /* v */
    gm_b16_8 gm_l55; /* v */
    gm_b16_8 gm_l56; /* v */
    uint8_t gm_l57; /* ok */
    gm_b16_8 gm_l58; /* <tmp> */
    uint64_t gm_l59; /* <tmp> */
    uint8_t gm_l60; /* c */
    gm_th2lqbo77pvsc gm_l61; /* subdir */
    GM_STACK_CHECK(&F, 14617u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    {
        void * t2 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)((uint8_t *)a1)));
        void * t3 = t2;
        void * t4;
        if ((GM_LD(void *, ((uint8_t *)&t3)) != 0)) {
            void * t5 = GM_LD(void *, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            uint64_t t7 = gm_l2;
            gm_b16_8 t8 = gm_l0;
            gm_tnpifv20irnph t9;
            GM_ST(gm_b16_8, ((uint8_t *)&t9 + 0), t8);
            gm_tabb710d9mq2e t10;
            gm_f_format_9d8u2dd((void *)&t10, (void *)NULL, cx, NULL, t9);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t11 = t10;
            if (((((uint8_t *)&t11))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t11) + 8, gm_srcg0ns322676oc, 485, 29);
            gm_b16_8 t12 = GM_LD(gm_b16_8, (((uint8_t *)&t11) + 8));
            gm_b16_8 t13 = t12;
            gm_b16_8 t14 = GM_LD(gm_b16_8, ((uint8_t *)&t13));
            gm_tf1bjg81u0glv t15;
            gm_f_mfail_uqq7o7g((void *)&t15, t7, t14);
            gm_tmn9qm2bp7f46 t16;
            memset(&t16, 0, sizeof t16);
            ((uint8_t *)&t16)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t16 + 8), t15);
            gm_tmn9qm2bp7f46 t17 = t16;
            uint8_t t18 = ((((uint8_t *)&t17))[0] != 0);
            F.ret = t16;
            goto gm_fret;
        }
        gm_l3 = t4;
    }
    {
        {
            void * t20 = gm_l3;
            gm_b16_8 t21 = GM_LD(gm_b16_8, ((((uint8_t *)t20) + 0) + 0));
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
                    uint64_t t29 = (uintptr_t)t28;
                    uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                    gm_trc3jdv2g5qre t33 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t29 * 72));
                    F.l6 = t33;
                    {
                        gm_l7 = ((uint8_t)0);
                    }
                    {
                        {
                            void * t36 = ((uint8_t *)gm_ct5id4947nt);
                            if ((uintptr_t)0 > (uintptr_t)9 || (uintptr_t)9 > (uintptr_t)9) gm_trap_slice((uintptr_t)0, (uintptr_t)9, 9, gm_srcg0ns322676oc, 489, 9);
                            gm_b16_8 t37;
                            GM_ST(void *, (uint8_t *)&t37, (void *)(((uint8_t *)t36 + (uintptr_t)0 * 16)));
                            GM_ST(uintptr_t, (uint8_t *)&t37 + 8, (uintptr_t)((uintptr_t)9 - (uintptr_t)0));
                            gm_l8 = t37;
                        }
                        {
                            gm_l9 = ((uint64_t)0ULL);
                        }
                        {
                            for (;;) {
                                uint64_t t39 = gm_l9;
                                uint64_t t40 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                uint8_t t41 = (t39 < t40);
                                if (!t41) goto lb38;
                                {
                                    uint64_t t44 = gm_l9;
                                    uint64_t t45 = (uintptr_t)t44;
                                    uint64_t t46 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                                    gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t45 * 16));
                                    gm_l10 = t49;
                                    {
                                        gm_b16_8 t52 = gm_l10;
                                        gm_b16_8 t53 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                                        gm_b16_8 t54 = t52;
                                        gm_b16_8 t55 = t53;
                                        uint8_t t56 = (gm_eq0iqiubfm7e(((uint8_t *)&t54), ((uint8_t *)&t55)));
                                        if (!t56) goto ie51;
                                        {
                                            gm_l7 = ((uint8_t)1);
                                        }
                                        goto ix51;
ie51:;
ix51:;
                                    }
                                    goto me47;
mn48:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 489, 9);
me47:;
                                }
lc38:;
                                {
                                    uint64_t t59 = gm_l9;
                                    uint64_t r60; bool o61 = __builtin_add_overflow(t59, ((uint64_t)1ULL), &r60);
                                    uint64_t t62 = r60;
                                    gm_l9 = t62;
                                }
                            }
lb38:;
                        }
                    }
                    {
                        uint8_t t64 = gm_l7;
                        uint8_t t65 = (uint8_t)!t64;
                        if (!t65) goto ie63;
                        {
                            uint64_t t67 = GM_LD(uint64_t, (((uint8_t *)&F.l6) + 64));
                            gm_t6j6smj7qoctu t68;
                            gm_b16_8 t69 = gm_l0;
                            GM_ST(gm_b16_8, (uint8_t *)&t68 + 0, t69);
                            gm_b16_8 t70 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t68 + 16, t70);
                            gm_tabb710d9mq2e t71;
                            gm_f_format_je4jsfp((void *)&t71, (void *)NULL, cx, NULL, &t68);
                            if (gm_unw.kind) {
                                goto gm_ll26;
                            }
                            gm_tabb710d9mq2e t72 = t71;
                            if (((((uint8_t *)&t72))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t72) + 8, gm_srcg0ns322676oc, 492, 46);
                            gm_b16_8 t73 = GM_LD(gm_b16_8, (((uint8_t *)&t72) + 8));
                            gm_b16_8 t74 = t73;
                            gm_b16_8 t75 = GM_LD(gm_b16_8, ((uint8_t *)&t74));
                            gm_tf1bjg81u0glv t76;
                            gm_f_mfail_uqq7o7g((void *)&t76, t67, t75);
                            gm_tmn9qm2bp7f46 t77;
                            memset(&t77, 0, sizeof t77);
                            ((uint8_t *)&t77)[0] = 1;
                            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t77 + 8), t76);
                            gm_tmn9qm2bp7f46 t78 = t77;
                            uint8_t t79 = ((((uint8_t *)&t78))[0] != 0);
                            F.ret = t77;
                            goto gm_fret;
                        }
                        goto ix63;
ie63:;
ix63:;
                    }
                    goto me31;
mn32:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 487, 5);
me31:;
                }
lc22:;
                {
                    uint64_t t81 = gm_l5;
                    uint64_t r82; bool o83 = __builtin_add_overflow(t81, ((uint64_t)1ULL), &r82);
                    uint64_t t84 = r82;
                    gm_l5 = t84;
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
        gm_f_list_epvducg((void *)&F.l11, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t86;
            GM_ST(void *, (uint8_t *)&t86, (void *)(gm_smimi08sfrg0n));
            GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(4));
            GM_ST(gm_b16_8, (uint8_t *)&F.l12 + 0, t86);
            gm_b16_8 t87;
            GM_ST(void *, (uint8_t *)&t87, (void *)(gm_s0k6ioi39f878));
            GM_ST(uintptr_t, (uint8_t *)&t87 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&F.l12 + 16, t87);
            gm_b16_8 t88;
            GM_ST(void *, (uint8_t *)&t88, (void *)(gm_sa2h9vonan0ma));
            GM_ST(uintptr_t, (uint8_t *)&t88 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&F.l12 + 32, t88);
        }
        {
            {
                {
                    gm_b16_8 t91 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l13 + 0, t91);
                    gm_b16_8 t92 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 16));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l13 + 16, t92);
                    gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l12) + 32));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l13 + 32, t93);
                }
                void * t94 = ((uint8_t *)&F.l13);
                if ((uintptr_t)0 > (uintptr_t)3 || (uintptr_t)3 > (uintptr_t)3) gm_trap_slice((uintptr_t)0, (uintptr_t)3, 3, gm_srcg0ns322676oc, 495, 5);
                gm_b16_8 t95;
                GM_ST(void *, (uint8_t *)&t95, (void *)(((uint8_t *)t94 + (uintptr_t)0 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t95 + 8, (uintptr_t)((uintptr_t)3 - (uintptr_t)0));
                gm_b16_8 t96 = t95;
                gm_l14 = t96;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t98 = gm_l15;
                    uint64_t t99 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t100 = (t98 < t99);
                    if (!t100) goto lb97;
                    {
                        uint64_t t103 = gm_l15;
                        uint64_t t104 = (uintptr_t)t103;
                        uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t104 * 16));
                        gm_l16 = t108;
                        {
                            void * t111 = gm_l3;
                            gm_b16_8 t112 = gm_l16;
                            void * t113 = gm_f_get_9rme0gb(t111, t112);
                            void * t114 = t113;
                            uint8_t t115;
                            if ((GM_LD(void *, ((uint8_t *)&t114)) != 0)) goto mn117;
                            t115 = ((uint8_t)0);
                            goto me116;
mn117:;
                            t115 = ((uint8_t)1);
                            goto me116;
mn118:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 496, 12);
me116:;
                            if (!t115) goto ie110;
                            {
                                void * t120 = (void *)((uint8_t *)&F.l11);
                                gm_b16_8 t121 = gm_l16;
                                gm_tftb1odc739t5 t122 = gm_f_push_3ef746b(t120, t121);
                                gm_tftb1odc739t5 t123 = t122;
                                if (((((uint8_t *)&t123))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t123) + 1, gm_srcg0ns322676oc, 496, 35);
                                gm_b0_1 t124 = GM_LD(gm_b0_1, (((uint8_t *)&t123) + 1));
                            }
                            goto ix110;
ie110:;
ix110:;
                        }
                        goto me106;
mn107:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 495, 5);
me106:;
                    }
lc97:;
                    {
                        uint64_t t126 = gm_l15;
                        uint64_t r127; bool o128 = __builtin_add_overflow(t126, ((uint64_t)1ULL), &r127);
                        uint64_t t129 = r127;
                        gm_l15 = t129;
                    }
                }
lb97:;
            }
        }
    }
    {
        uint64_t t131 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
        uint8_t t132 = (t131 > ((uint64_t)1ULL));
        if (!t132) goto ie130;
        {
            uint64_t t134 = gm_l2;
            gm_tfeab279c16rr t135;
            gm_b16_8 t136 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t135 + 0, t136);
            uint64_t t137 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t138 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
            if (t137 >= (uintptr_t)(t138)) gm_trap_index(t137, t138, gm_srcg0ns322676oc, 499, 105);
            gm_b16_8 t139 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t137 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t135 + 16, t139);
            uint64_t t140 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t141 = GM_LD(uintptr_t, (((uint8_t *)&F.l11) + 0) + 8);
            if (t140 >= (uintptr_t)(t141)) gm_trap_index(t140, t141, gm_srcg0ns322676oc, 499, 123);
            gm_b16_8 t142 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l11) + 0)) + t140 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t135 + 32, t142);
            gm_tabb710d9mq2e t143;
            gm_f_format_aome4un((void *)&t143, (void *)NULL, cx, NULL, &t135);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t144 = t143;
            if (((((uint8_t *)&t144))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t144) + 8, gm_srcg0ns322676oc, 499, 29);
            gm_b16_8 t145 = GM_LD(gm_b16_8, (((uint8_t *)&t144) + 8));
            gm_b16_8 t146 = t145;
            gm_b16_8 t147 = GM_LD(gm_b16_8, ((uint8_t *)&t146));
            gm_tf1bjg81u0glv t148;
            gm_f_mfail_uqq7o7g((void *)&t148, t134, t147);
            gm_tmn9qm2bp7f46 t149;
            memset(&t149, 0, sizeof t149);
            ((uint8_t *)&t149)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t149 + 8), t148);
            gm_tmn9qm2bp7f46 t150 = t149;
            uint8_t t151 = ((((uint8_t *)&t150))[0] != 0);
            F.ret = t149;
            goto gm_fret;
        }
        goto ix130;
ie130:;
ix130:;
    }
    {
        void * t153 = gm_l3;
        gm_b16_8 t154;
        GM_ST(void *, (uint8_t *)&t154, (void *)(gm_sa2h9vonan0ma));
        GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(3));
        gm_b16_8 t155 = gm_l0;
        gm_tccklsmdasd9s t156;
        gm_f_dep_string_mmgrsgi((void *)&t156, (void *)NULL, cx, NULL, t153, t154, t155);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tccklsmdasd9s t157 = t156;
        if (((((uint8_t *)&t157))[0] != 0)) {
            gm_tf1bjg81u0glv t158 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t157) + 8));
            gm_tmn9qm2bp7f46 t159;
            memset(&t159, 0, sizeof t159);
            ((uint8_t *)&t159)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t159 + 8), t158);
            F.ret = t159;
            goto gm_fret;
        }
        gm_th2lqbo77pvsc t160 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t157) + 8));
        gm_th2lqbo77pvsc t161 = t160;
        if (!((((uint8_t *)&t161))[16] != 0)) goto ie152;
        gm_b16_8 t162 = GM_LD(gm_b16_8, ((uint8_t *)&t161));
        gm_l17 = t162;
        {
            {
                gm_b16_8 t165;
                GM_ST(void *, (uint8_t *)&t165, (void *)(gm_sla3alt5340mf));
                GM_ST(uintptr_t, (uint8_t *)&t165 + 8, (uintptr_t)(7));
                GM_ST(gm_b16_8, (uint8_t *)&F.l18 + 0, t165);
                gm_b16_8 t166;
                GM_ST(void *, (uint8_t *)&t166, (void *)(gm_s420tm9ua7ggg));
                GM_ST(uintptr_t, (uint8_t *)&t166 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&F.l18 + 16, t166);
                gm_b16_8 t167;
                GM_ST(void *, (uint8_t *)&t167, (void *)(gm_s0jho24omhfui));
                GM_ST(uintptr_t, (uint8_t *)&t167 + 8, (uintptr_t)(6));
                GM_ST(gm_b16_8, (uint8_t *)&F.l18 + 32, t167);
                gm_b16_8 t168;
                GM_ST(void *, (uint8_t *)&t168, (void *)(gm_sf9ilivr4c86m));
                GM_ST(uintptr_t, (uint8_t *)&t168 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&F.l18 + 48, t168);
            }
            {
                {
                    {
                        gm_b16_8 t171 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l19 + 0, t171);
                        gm_b16_8 t172 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 16));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l19 + 16, t172);
                        gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 32));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l19 + 32, t173);
                        gm_b16_8 t174 = GM_LD(gm_b16_8, (((uint8_t *)&F.l18) + 48));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l19 + 48, t174);
                    }
                    void * t175 = ((uint8_t *)&F.l19);
                    if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcg0ns322676oc, 502, 9);
                    gm_b16_8 t176;
                    GM_ST(void *, (uint8_t *)&t176, (void *)(((uint8_t *)t175 + (uintptr_t)0 * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t176 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                    gm_b16_8 t177 = t176;
                    gm_l20 = t177;
                }
                {
                    gm_l21 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t179 = gm_l21;
                        uint64_t t180 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        uint8_t t181 = (t179 < t180);
                        if (!t181) goto lb178;
                        {
                            uint64_t t184 = gm_l21;
                            uint64_t t185 = (uintptr_t)t184;
                            uint64_t t186 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                            gm_b16_8 t189 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t185 * 16));
                            gm_l22 = t189;
                            {
                                void * t192 = gm_l3;
                                gm_b16_8 t193 = gm_l22;
                                void * t194 = gm_f_get_9rme0gb(t192, t193);
                                void * t195 = t194;
                                uint8_t t196;
                                if ((GM_LD(void *, ((uint8_t *)&t195)) != 0)) goto mn198;
                                t196 = ((uint8_t)0);
                                goto me197;
mn198:;
                                t196 = ((uint8_t)1);
                                goto me197;
mn199:;
                                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 503, 16);
me197:;
                                if (!t196) goto ie191;
                                {
                                    uint64_t t201 = gm_l2;
                                    gm_t6j6smj7qoctu t202;
                                    gm_b16_8 t203 = gm_l22;
                                    GM_ST(gm_b16_8, (uint8_t *)&t202 + 0, t203);
                                    gm_b16_8 t204 = gm_l0;
                                    GM_ST(gm_b16_8, (uint8_t *)&t202 + 16, t204);
                                    gm_tabb710d9mq2e t205;
                                    gm_f_format_64tgs30((void *)&t205, (void *)NULL, cx, NULL, &t202);
                                    if (gm_unw.kind) {
                                        goto gm_ll182;
                                    }
                                    gm_tabb710d9mq2e t206 = t205;
                                    if (((((uint8_t *)&t206))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t206) + 8, gm_srcg0ns322676oc, 503, 59);
                                    gm_b16_8 t207 = GM_LD(gm_b16_8, (((uint8_t *)&t206) + 8));
                                    gm_b16_8 t208 = t207;
                                    gm_b16_8 t209 = GM_LD(gm_b16_8, ((uint8_t *)&t208));
                                    gm_tf1bjg81u0glv t210;
                                    gm_f_mfail_uqq7o7g((void *)&t210, t201, t209);
                                    gm_tmn9qm2bp7f46 t211;
                                    memset(&t211, 0, sizeof t211);
                                    ((uint8_t *)&t211)[0] = 1;
                                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t211 + 8), t210);
                                    gm_tmn9qm2bp7f46 t212 = t211;
                                    uint8_t t213 = ((((uint8_t *)&t212))[0] != 0);
                                    F.ret = t211;
                                    goto gm_fret;
                                }
                                goto ix191;
ie191:;
ix191:;
                            }
                            goto me187;
mn188:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 502, 9);
me187:;
                        }
lc178:;
                        {
                            uint64_t t215 = gm_l21;
                            uint64_t r216; bool o217 = __builtin_add_overflow(t215, ((uint64_t)1ULL), &r216);
                            uint64_t t218 = r216;
                            gm_l21 = t218;
                        }
                    }
gm_ll182:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 3) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb178; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc178; }
                    }
                    goto gm_lad;
lb178:;
                }
            }
        }
        {
            gm_b16_8 t220 = gm_l17;
            gm_b16_8 t221 = gm_f_trim_ascii_c9j36d1(t220);
            gm_b16_8 t222 = t221;
            uint64_t t223 = GM_LD(uintptr_t, ((uint8_t *)&t222) + 8);
            uint8_t t224 = ((t223 == ((uint64_t)0ULL)));
            if (!t224) goto ie219;
            {
                uint64_t t226 = gm_l2;
                gm_b16_8 t227 = gm_l0;
                gm_tnpifv20irnph t228;
                GM_ST(gm_b16_8, ((uint8_t *)&t228 + 0), t227);
                gm_tabb710d9mq2e t229;
                gm_f_format_97etovl((void *)&t229, (void *)NULL, cx, NULL, t228);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t230 = t229;
                if (((((uint8_t *)&t230))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t230) + 8, gm_srcg0ns322676oc, 505, 60);
                gm_b16_8 t231 = GM_LD(gm_b16_8, (((uint8_t *)&t230) + 8));
                gm_b16_8 t232 = t231;
                gm_b16_8 t233 = GM_LD(gm_b16_8, ((uint8_t *)&t232));
                gm_tf1bjg81u0glv t234;
                gm_f_mfail_uqq7o7g((void *)&t234, t226, t233);
                gm_tmn9qm2bp7f46 t235;
                memset(&t235, 0, sizeof t235);
                ((uint8_t *)&t235)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t235 + 8), t234);
                gm_tmn9qm2bp7f46 t236 = t235;
                uint8_t t237 = ((((uint8_t *)&t236))[0] != 0);
                F.ret = t235;
                goto gm_fret;
            }
            goto ix219;
ie219:;
ix219:;
        }
        {
            gm_b16_8 t238 = gm_l17;
            gm_b16_8 t239 = gm_f_trim_ascii_c9j36d1(t238);
            gm_l23 = t239;
        }
        {
            gm_b16_8 t241 = gm_l23;
            gm_th2lqbo77pvsc t242;
            gm_f_url_problem_38kdh3a((void *)&t242, (void *)NULL, cx, NULL, t241);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t243 = t242;
            if (!((((uint8_t *)&t243))[16] != 0)) goto ie240;
            gm_b16_8 t244 = GM_LD(gm_b16_8, ((uint8_t *)&t243));
            gm_l24 = t244;
            {
                uint64_t t246 = gm_l2;
                gm_t6j6smj7qoctu t247;
                gm_b16_8 t248 = gm_l0;
                GM_ST(gm_b16_8, (uint8_t *)&t247 + 0, t248);
                gm_b16_8 t249 = gm_l24;
                GM_ST(gm_b16_8, (uint8_t *)&t247 + 16, t249);
                gm_tabb710d9mq2e t250;
                gm_f_format_qjfu8g6((void *)&t250, (void *)NULL, cx, NULL, &t247);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t251 = t250;
                if (((((uint8_t *)&t251))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t251) + 8, gm_srcg0ns322676oc, 508, 33);
                gm_b16_8 t252 = GM_LD(gm_b16_8, (((uint8_t *)&t251) + 8));
                gm_b16_8 t253 = t252;
                gm_b16_8 t254 = GM_LD(gm_b16_8, ((uint8_t *)&t253));
                gm_tf1bjg81u0glv t255;
                gm_f_mfail_uqq7o7g((void *)&t255, t246, t254);
                gm_tmn9qm2bp7f46 t256;
                memset(&t256, 0, sizeof t256);
                ((uint8_t *)&t256)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t256 + 8), t255);
                gm_tmn9qm2bp7f46 t257 = t256;
                uint8_t t258 = ((((uint8_t *)&t257))[0] != 0);
                F.ret = t256;
                goto gm_fret;
            }
            goto ix240;
ie240:;
ix240:;
        }
        {
            gm_th2lqbo77pvsc t259;
            memset(&t259, 0, sizeof t259);
            gm_l25 = t259;
        }
        {
            void * t261 = gm_l3;
            gm_b16_8 t262;
            GM_ST(void *, (uint8_t *)&t262, (void *)(gm_sd1smnnii4cig));
            GM_ST(uintptr_t, (uint8_t *)&t262 + 8, (uintptr_t)(6));
            gm_b16_8 t263 = gm_l0;
            gm_tccklsmdasd9s t264;
            gm_f_dep_string_mmgrsgi((void *)&t264, (void *)NULL, cx, NULL, t261, t262, t263);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tccklsmdasd9s t265 = t264;
            if (((((uint8_t *)&t265))[0] != 0)) {
                gm_tf1bjg81u0glv t266 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t265) + 8));
                gm_tmn9qm2bp7f46 t267;
                memset(&t267, 0, sizeof t267);
                ((uint8_t *)&t267)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t267 + 8), t266);
                F.ret = t267;
                goto gm_fret;
            }
            gm_th2lqbo77pvsc t268 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t265) + 8));
            gm_th2lqbo77pvsc t269 = t268;
            if (!((((uint8_t *)&t269))[16] != 0)) goto ie260;
            gm_b16_8 t270 = GM_LD(gm_b16_8, ((uint8_t *)&t269));
            gm_l26 = t270;
            {
                gm_b16_8 t272 = gm_l26;
                gm_b16_8 t273 = gm_f_trim_ascii_c9j36d1(t272);
                gm_l27 = t273;
            }
            {
                gm_b16_8 t274 = gm_l27;
                gm_b16_8 t275;
                GM_ST(void *, (uint8_t *)&t275, (void *)(gm_son0j0k0t10ro));
                GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(7));
                gm_th2lqbo77pvsc t276;
                gm_f_strip_prefix_b4949s8((void *)&t276, t274, t275);
                gm_th2lqbo77pvsc t277 = t276;
                gm_b16_8 t278;
                if (((((uint8_t *)&t277))[16] != 0)) {
                    gm_b16_8 t279 = GM_LD(gm_b16_8, (((uint8_t *)&t277) + 0));
                    t278 = t279;
                } else {
                    gm_b16_8 t280 = gm_l27;
                    t278 = t280;
                }
                gm_l27 = t278;
            }
            {
                gm_b16_8 t281 = gm_l27;
                gm_b16_8 t282 = gm_f_lower_ascii_uggh3ca((void *)NULL, cx, NULL, t281);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l28 = t282;
            }
            {
                uint64_t t283 = GM_LD(uintptr_t, ((uint8_t *)&gm_l28) + 8);
                uint8_t t284 = ((t283 == ((uint64_t)64ULL)));
                gm_l29 = t284;
            }
            {
                {
                    gm_b16_8 t286 = gm_l28;
                    gm_l30 = t286;
                }
                {
                    gm_l31 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t288 = gm_l31;
                        uint64_t t289 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                        uint8_t t290 = (t288 < t289);
                        if (!t290) goto lb287;
                        {
                            uint64_t t293 = gm_l31;
                            uint64_t t294 = (uintptr_t)t293;
                            uint64_t t295 = GM_LD(uintptr_t, ((uint8_t *)&gm_l30) + 8);
                            uint8_t t296 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l30)) + t294 * 1));
                            uint8_t t297 = t296;
                            uint8_t t300 = GM_LD(uint8_t, ((uint8_t *)&t297));
                            gm_l32 = t300;
                            {
                                uint8_t t303 = gm_l32;
                                uint8_t t304 = gm_f_is_hex_digit_q8s6utq(t303);
                                uint8_t t305 = (uint8_t)!t304;
                                if (!t305) goto ie302;
                                {
                                    gm_l29 = ((uint8_t)0);
                                }
                                goto ix302;
ie302:;
ix302:;
                            }
                            goto me298;
mn299:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 516, 13);
me298:;
                        }
lc287:;
                        {
                            uint64_t t308 = gm_l31;
                            uint64_t r309; bool o310 = __builtin_add_overflow(t308, ((uint64_t)1ULL), &r309);
                            uint64_t t311 = r309;
                            gm_l31 = t311;
                        }
                    }
lb287:;
                }
            }
            {
                uint8_t t313 = gm_l29;
                uint8_t t314 = (uint8_t)!t313;
                if (!t314) goto ie312;
                {
                    void * t316 = gm_l3;
                    gm_b16_8 t317;
                    GM_ST(void *, (uint8_t *)&t317, (void *)(gm_sd1smnnii4cig));
                    GM_ST(uintptr_t, (uint8_t *)&t317 + 8, (uintptr_t)(6));
                    void * t318 = gm_f_get_9rme0gb(t316, t317);
                    void * t319 = t318;
                    if (!(GM_LD(void *, ((uint8_t *)&t319)) != 0)) gm_trap("unwrap of null", gm_srcg0ns322676oc, 520, 31);
                    void * t320 = GM_LD(void *, ((uint8_t *)&t319));
                    uint64_t t321 = GM_LD(uint64_t, (((uint8_t *)t320) + 64));
                    gm_b16_8 t322 = gm_l0;
                    gm_tnpifv20irnph t323;
                    GM_ST(gm_b16_8, ((uint8_t *)&t323 + 0), t322);
                    gm_tabb710d9mq2e t324;
                    gm_f_format_nll5o74((void *)&t324, (void *)NULL, cx, NULL, t323);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t325 = t324;
                    if (((((uint8_t *)&t325))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t325) + 8, gm_srcg0ns322676oc, 520, 54);
                    gm_b16_8 t326 = GM_LD(gm_b16_8, (((uint8_t *)&t325) + 8));
                    gm_b16_8 t327 = t326;
                    gm_b16_8 t328 = GM_LD(gm_b16_8, ((uint8_t *)&t327));
                    gm_tf1bjg81u0glv t329;
                    gm_f_mfail_uqq7o7g((void *)&t329, t321, t328);
                    gm_tmn9qm2bp7f46 t330;
                    memset(&t330, 0, sizeof t330);
                    ((uint8_t *)&t330)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t330 + 8), t329);
                    gm_tmn9qm2bp7f46 t331 = t330;
                    uint8_t t332 = ((((uint8_t *)&t331))[0] != 0);
                    F.ret = t330;
                    goto gm_fret;
                }
                goto ix312;
ie312:;
ix312:;
            }
            {
                gm_b16_8 t333 = gm_l28;
                gm_th2lqbo77pvsc t334;
                GM_ST(uint8_t, (uint8_t *)&t334 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t334 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t334 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t334), t333);
                ((uint8_t *)&t334)[16] = 1;
                gm_l25 = t334;
            }
            goto ix260;
ie260:;
ix260:;
        }
        {
            void * t335 = gm_l3;
            gm_b16_8 t336;
            GM_ST(void *, (uint8_t *)&t336, (void *)(gm_sfvspsnv23gbt));
            GM_ST(uintptr_t, (uint8_t *)&t336 + 8, (uintptr_t)(6));
            gm_b16_8 t337 = gm_l0;
            gm_tccklsmdasd9s t338;
            gm_f_dep_string_mmgrsgi((void *)&t338, (void *)NULL, cx, NULL, t335, t336, t337);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tccklsmdasd9s t339 = t338;
            if (((((uint8_t *)&t339))[0] != 0)) {
                gm_tf1bjg81u0glv t340 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t339) + 8));
                gm_tmn9qm2bp7f46 t341;
                memset(&t341, 0, sizeof t341);
                ((uint8_t *)&t341)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t341 + 8), t340);
                F.ret = t341;
                goto gm_fret;
            }
            gm_th2lqbo77pvsc t342 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t339) + 8));
            gm_l33 = t342;
        }
        {
            gm_b16_8 t343 = gm_l0;
            uint64_t t344 = gm_l2;
            gm_t6h2b31csotun t345;
            gm_f_check_subdir_2uljb06((void *)&t345, (void *)NULL, cx, NULL, t343, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l33)), t344);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6h2b31csotun t346 = t345;
            if (((((uint8_t *)&t346))[0] != 0)) {
                gm_tf1bjg81u0glv t347 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t346) + 8));
                gm_tmn9qm2bp7f46 t348;
                memset(&t348, 0, sizeof t348);
                ((uint8_t *)&t348)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t348 + 8), t347);
                F.ret = t348;
                goto gm_fret;
            }
            gm_b0_1 t349 = GM_LD(gm_b0_1, (((uint8_t *)&t346) + 8));
        }
        {
            gm_tt008mgut8glb t350;
            gm_b16_8 t351 = gm_l23;
            GM_ST(gm_b16_8, (uint8_t *)&t350 + 0, t351);
            memcpy((uint8_t *)&t350 + 16, ((uint8_t *)&gm_l25), 24);
            memcpy((uint8_t *)&t350 + 40, ((uint8_t *)&gm_l33), 24);
            gm_tnlfbo6r4vlhg t352;
            GM_ST(uint8_t, (uint8_t *)&t352 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t352 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t352 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t352 + 72, 0);
            GM_ST(uint64_t, (uint8_t *)&t352 + 80, 0);
            GM_ST(uint64_t, (uint8_t *)&t352 + 88, 0);
            GM_ST(uint8_t, ((uint8_t *)&t352 + 0), ((uint8_t)2ULL));
            GM_ST(gm_tt008mgut8glb, ((uint8_t *)&t352 + 8), t350);
            gm_tmn9qm2bp7f46 t353;
            GM_ST(uint8_t, (uint8_t *)&t353 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t353 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t353 + 4, 0);
            ((uint8_t *)&t353)[0] = 0;
            GM_ST(gm_tnlfbo6r4vlhg, ((uint8_t *)&t353 + 8), t352);
            gm_tmn9qm2bp7f46 t354 = t353;
            uint8_t t355 = ((((uint8_t *)&t354))[0] != 0);
            F.ret = t353;
            goto gm_fret;
        }
        goto ix152;
ie152:;
ix152:;
    }
    {
        void * t357 = gm_l3;
        gm_b16_8 t358;
        GM_ST(void *, (uint8_t *)&t358, (void *)(gm_sd1smnnii4cig));
        GM_ST(uintptr_t, (uint8_t *)&t358 + 8, (uintptr_t)(6));
        void * t359 = gm_f_get_9rme0gb(t357, t358);
        void * t360 = t359;
        uint8_t t361;
        if ((GM_LD(void *, ((uint8_t *)&t360)) != 0)) goto mn363;
        t361 = ((uint8_t)0);
        goto me362;
mn363:;
        t361 = ((uint8_t)1);
        goto me362;
mn364:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 528, 8);
me362:;
        if (!t361) goto ie356;
        {
            uint64_t t366 = gm_l2;
            gm_b16_8 t367 = gm_l0;
            gm_tnpifv20irnph t368;
            GM_ST(gm_b16_8, ((uint8_t *)&t368 + 0), t367);
            gm_tabb710d9mq2e t369;
            gm_f_format_p8qi5m3((void *)&t369, (void *)NULL, cx, NULL, t368);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t370 = t369;
            if (((((uint8_t *)&t370))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t370) + 8, gm_srcg0ns322676oc, 528, 58);
            gm_b16_8 t371 = GM_LD(gm_b16_8, (((uint8_t *)&t370) + 8));
            gm_b16_8 t372 = t371;
            gm_b16_8 t373 = GM_LD(gm_b16_8, ((uint8_t *)&t372));
            gm_tf1bjg81u0glv t374;
            gm_f_mfail_uqq7o7g((void *)&t374, t366, t373);
            gm_tmn9qm2bp7f46 t375;
            memset(&t375, 0, sizeof t375);
            ((uint8_t *)&t375)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t375 + 8), t374);
            gm_tmn9qm2bp7f46 t376 = t375;
            uint8_t t377 = ((((uint8_t *)&t376))[0] != 0);
            F.ret = t375;
            goto gm_fret;
        }
        goto ix356;
ie356:;
ix356:;
    }
    {
        void * t378 = gm_l3;
        gm_b16_8 t379;
        GM_ST(void *, (uint8_t *)&t379, (void *)(gm_smimi08sfrg0n));
        GM_ST(uintptr_t, (uint8_t *)&t379 + 8, (uintptr_t)(4));
        gm_b16_8 t380 = gm_l0;
        gm_tccklsmdasd9s t381;
        gm_f_dep_string_mmgrsgi((void *)&t381, (void *)NULL, cx, NULL, t378, t379, t380);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tccklsmdasd9s t382 = t381;
        if (((((uint8_t *)&t382))[0] != 0)) {
            gm_tf1bjg81u0glv t383 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t382) + 8));
            gm_tmn9qm2bp7f46 t384;
            memset(&t384, 0, sizeof t384);
            ((uint8_t *)&t384)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t384 + 8), t383);
            F.ret = t384;
            goto gm_fret;
        }
        gm_th2lqbo77pvsc t385 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t382) + 8));
        gm_l34 = t385;
    }
    {
        void * t386 = gm_l3;
        gm_b16_8 t387;
        GM_ST(void *, (uint8_t *)&t387, (void *)(gm_s0k6ioi39f878));
        GM_ST(uintptr_t, (uint8_t *)&t387 + 8, (uintptr_t)(3));
        gm_b16_8 t388 = gm_l0;
        gm_tccklsmdasd9s t389;
        gm_f_dep_string_mmgrsgi((void *)&t389, (void *)NULL, cx, NULL, t386, t387, t388);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tccklsmdasd9s t390 = t389;
        if (((((uint8_t *)&t390))[0] != 0)) {
            gm_tf1bjg81u0glv t391 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t390) + 8));
            gm_tmn9qm2bp7f46 t392;
            memset(&t392, 0, sizeof t392);
            ((uint8_t *)&t392)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t392 + 8), t391);
            F.ret = t392;
            goto gm_fret;
        }
        gm_th2lqbo77pvsc t393 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t390) + 8));
        gm_l35 = t393;
    }
    {
        uint8_t t395;
        if (((((uint8_t *)&gm_l34))[16] != 0)) goto mn397;
        t395 = ((uint8_t)1);
        goto me396;
mn397:;
        t395 = ((uint8_t)0);
        goto me396;
mn398:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 531, 8);
me396:;
        if (!t395) goto ie394;
        uint8_t t399;
        if (((((uint8_t *)&gm_l35))[16] != 0)) goto mn401;
        t399 = ((uint8_t)1);
        goto me400;
mn401:;
        t399 = ((uint8_t)0);
        goto me400;
mn402:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 531, 25);
me400:;
        if (!t399) goto ie394;
        {
            uint64_t t404 = gm_l2;
            gm_b16_8 t405 = gm_l0;
            gm_tnpifv20irnph t406;
            GM_ST(gm_b16_8, ((uint8_t *)&t406 + 0), t405);
            gm_tabb710d9mq2e t407;
            gm_f_format_5bo63p7((void *)&t407, (void *)NULL, cx, NULL, t406);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t408 = t407;
            if (((((uint8_t *)&t408))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t408) + 8, gm_srcg0ns322676oc, 532, 29);
            gm_b16_8 t409 = GM_LD(gm_b16_8, (((uint8_t *)&t408) + 8));
            gm_b16_8 t410 = t409;
            gm_b16_8 t411 = GM_LD(gm_b16_8, ((uint8_t *)&t410));
            gm_tf1bjg81u0glv t412;
            gm_f_mfail_uqq7o7g((void *)&t412, t404, t411);
            gm_tmn9qm2bp7f46 t413;
            memset(&t413, 0, sizeof t413);
            ((uint8_t *)&t413)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t413 + 8), t412);
            gm_tmn9qm2bp7f46 t414 = t413;
            uint8_t t415 = ((((uint8_t *)&t414))[0] != 0);
            F.ret = t413;
            goto gm_fret;
        }
        goto ix394;
ie394:;
ix394:;
    }
    {
        if (!((((uint8_t *)&gm_l34))[16] != 0)) goto ie416;
        gm_b16_8 t417 = GM_LD(gm_b16_8, ((uint8_t *)&gm_l34));
        gm_l36 = t417;
        {
            {
                gm_b16_8 t420;
                GM_ST(void *, (uint8_t *)&t420, (void *)(gm_s420tm9ua7ggg));
                GM_ST(uintptr_t, (uint8_t *)&t420 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&F.l37 + 0, t420);
                gm_b16_8 t421;
                GM_ST(void *, (uint8_t *)&t421, (void *)(gm_s0jho24omhfui));
                GM_ST(uintptr_t, (uint8_t *)&t421 + 8, (uintptr_t)(6));
                GM_ST(gm_b16_8, (uint8_t *)&F.l37 + 16, t421);
                gm_b16_8 t422;
                GM_ST(void *, (uint8_t *)&t422, (void *)(gm_sf9ilivr4c86m));
                GM_ST(uintptr_t, (uint8_t *)&t422 + 8, (uintptr_t)(3));
                GM_ST(gm_b16_8, (uint8_t *)&F.l37 + 32, t422);
                gm_b16_8 t423;
                GM_ST(void *, (uint8_t *)&t423, (void *)(gm_sfvspsnv23gbt));
                GM_ST(uintptr_t, (uint8_t *)&t423 + 8, (uintptr_t)(6));
                GM_ST(gm_b16_8, (uint8_t *)&F.l37 + 48, t423);
            }
            {
                {
                    {
                        gm_b16_8 t426 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 0));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l38 + 0, t426);
                        gm_b16_8 t427 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 16));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l38 + 16, t427);
                        gm_b16_8 t428 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 32));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l38 + 32, t428);
                        gm_b16_8 t429 = GM_LD(gm_b16_8, (((uint8_t *)&F.l37) + 48));
                        GM_ST(gm_b16_8, (uint8_t *)&F.l38 + 48, t429);
                    }
                    void * t430 = ((uint8_t *)&F.l38);
                    if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcg0ns322676oc, 535, 9);
                    gm_b16_8 t431;
                    GM_ST(void *, (uint8_t *)&t431, (void *)(((uint8_t *)t430 + (uintptr_t)0 * 16)));
                    GM_ST(uintptr_t, (uint8_t *)&t431 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                    gm_b16_8 t432 = t431;
                    gm_l39 = t432;
                }
                {
                    gm_l40 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t434 = gm_l40;
                        uint64_t t435 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                        uint8_t t436 = (t434 < t435);
                        if (!t436) goto lb433;
                        {
                            uint64_t t439 = gm_l40;
                            uint64_t t440 = (uintptr_t)t439;
                            uint64_t t441 = GM_LD(uintptr_t, ((uint8_t *)&gm_l39) + 8);
                            gm_b16_8 t444 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l39)) + t440 * 16));
                            gm_l41 = t444;
                            {
                                void * t447 = gm_l3;
                                gm_b16_8 t448 = gm_l41;
                                void * t449 = gm_f_get_9rme0gb(t447, t448);
                                void * t450 = t449;
                                uint8_t t451;
                                if ((GM_LD(void *, ((uint8_t *)&t450)) != 0)) goto mn453;
                                t451 = ((uint8_t)0);
                                goto me452;
mn453:;
                                t451 = ((uint8_t)1);
                                goto me452;
mn454:;
                                gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 536, 16);
me452:;
                                if (!t451) goto ie446;
                                {
                                    uint64_t t456 = gm_l2;
                                    gm_t6j6smj7qoctu t457;
                                    gm_b16_8 t458 = gm_l41;
                                    GM_ST(gm_b16_8, (uint8_t *)&t457 + 0, t458);
                                    gm_b16_8 t459 = gm_l0;
                                    GM_ST(gm_b16_8, (uint8_t *)&t457 + 16, t459);
                                    gm_tabb710d9mq2e t460;
                                    gm_f_format_64tgs30((void *)&t460, (void *)NULL, cx, NULL, &t457);
                                    if (gm_unw.kind) {
                                        goto gm_ll437;
                                    }
                                    gm_tabb710d9mq2e t461 = t460;
                                    if (((((uint8_t *)&t461))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t461) + 8, gm_srcg0ns322676oc, 536, 59);
                                    gm_b16_8 t462 = GM_LD(gm_b16_8, (((uint8_t *)&t461) + 8));
                                    gm_b16_8 t463 = t462;
                                    gm_b16_8 t464 = GM_LD(gm_b16_8, ((uint8_t *)&t463));
                                    gm_tf1bjg81u0glv t465;
                                    gm_f_mfail_uqq7o7g((void *)&t465, t456, t464);
                                    gm_tmn9qm2bp7f46 t466;
                                    memset(&t466, 0, sizeof t466);
                                    ((uint8_t *)&t466)[0] = 1;
                                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t466 + 8), t465);
                                    gm_tmn9qm2bp7f46 t467 = t466;
                                    uint8_t t468 = ((((uint8_t *)&t467))[0] != 0);
                                    F.ret = t466;
                                    goto gm_fret;
                                }
                                goto ix446;
ie446:;
ix446:;
                            }
                            goto me442;
mn443:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 535, 9);
me442:;
                        }
lc433:;
                        {
                            uint64_t t470 = gm_l40;
                            uint64_t r471; bool o472 = __builtin_add_overflow(t470, ((uint64_t)1ULL), &r471);
                            uint64_t t473 = r471;
                            gm_l40 = t473;
                        }
                    }
gm_ll437:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 5) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb433; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc433; }
                    }
                    goto gm_lad;
lb433:;
                }
            }
        }
        {
            {
                void * t475 = gm_l3;
                gm_b16_8 t476;
                GM_ST(void *, (uint8_t *)&t476, (void *)(gm_sla3alt5340mf));
                GM_ST(uintptr_t, (uint8_t *)&t476 + 8, (uintptr_t)(7));
                gm_b16_8 t477 = gm_l0;
                gm_tccklsmdasd9s t478;
                gm_f_dep_string_mmgrsgi((void *)&t478, (void *)NULL, cx, NULL, t475, t476, t477);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tccklsmdasd9s t479 = t478;
                if (((((uint8_t *)&t479))[0] != 0)) {
                    gm_tf1bjg81u0glv t480 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t479) + 8));
                    gm_tmn9qm2bp7f46 t481;
                    memset(&t481, 0, sizeof t481);
                    ((uint8_t *)&t481)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t481 + 8), t480);
                    F.ret = t481;
                    goto gm_fret;
                }
                gm_th2lqbo77pvsc t482 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t479) + 8));
            }
        }
        {
            uint64_t t484 = GM_LD(uintptr_t, ((uint8_t *)&gm_l36) + 8);
            uint8_t t485 = ((t484 == ((uint64_t)0ULL)));
            if (!t485) goto ie483;
            {
                uint64_t t487 = gm_l2;
                gm_b16_8 t488 = gm_l0;
                gm_tnpifv20irnph t489;
                GM_ST(gm_b16_8, ((uint8_t *)&t489 + 0), t488);
                gm_tabb710d9mq2e t490;
                gm_f_format_dd3t0vb((void *)&t490, (void *)NULL, cx, NULL, t489);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t491 = t490;
                if (((((uint8_t *)&t491))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t491) + 8, gm_srcg0ns322676oc, 540, 45);
                gm_b16_8 t492 = GM_LD(gm_b16_8, (((uint8_t *)&t491) + 8));
                gm_b16_8 t493 = t492;
                gm_b16_8 t494 = GM_LD(gm_b16_8, ((uint8_t *)&t493));
                gm_tf1bjg81u0glv t495;
                gm_f_mfail_uqq7o7g((void *)&t495, t487, t494);
                gm_tmn9qm2bp7f46 t496;
                memset(&t496, 0, sizeof t496);
                ((uint8_t *)&t496)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t496 + 8), t495);
                gm_tmn9qm2bp7f46 t497 = t496;
                uint8_t t498 = ((((uint8_t *)&t497))[0] != 0);
                F.ret = t496;
                goto gm_fret;
            }
            goto ix483;
ie483:;
ix483:;
        }
        {
            gm_b16_8 t499 = gm_l36;
            gm_tnlfbo6r4vlhg t500;
            memset(&t500, 0, sizeof t500);
            GM_ST(uint8_t, ((uint8_t *)&t500 + 0), ((uint8_t)0ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t500 + 8), t499);
            gm_tmn9qm2bp7f46 t501;
            GM_ST(uint8_t, (uint8_t *)&t501 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t501 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t501 + 4, 0);
            ((uint8_t *)&t501)[0] = 0;
            GM_ST(gm_tnlfbo6r4vlhg, ((uint8_t *)&t501 + 8), t500);
            gm_tmn9qm2bp7f46 t502 = t501;
            uint8_t t503 = ((((uint8_t *)&t502))[0] != 0);
            F.ret = t501;
            goto gm_fret;
        }
        goto ix416;
ie416:;
ix416:;
    }
    {
        gm_th2lqbo77pvsc t504 = gm_l35;
        gm_th2lqbo77pvsc t505 = t504;
        if (!((((uint8_t *)&t505))[16] != 0)) gm_trap("unwrap of null", gm_srcg0ns322676oc, 543, 12);
        gm_b16_8 t506 = GM_LD(gm_b16_8, ((uint8_t *)&t505));
        gm_l42 = t506;
    }
    {
        gm_b16_8 t508 = gm_l42;
        gm_b16_8 t509 = gm_f_normalize_url_faghdja(t508);
        gm_b16_8 t510 = t509;
        uint64_t t511 = GM_LD(uintptr_t, ((uint8_t *)&t510) + 8);
        uint8_t t512 = ((t511 == ((uint64_t)0ULL)));
        if (!t512) goto ie507;
        {
            uint64_t t514 = gm_l2;
            gm_b16_8 t515 = gm_l0;
            gm_tnpifv20irnph t516;
            GM_ST(gm_b16_8, ((uint8_t *)&t516 + 0), t515);
            gm_tabb710d9mq2e t517;
            gm_f_format_fn68qf0((void *)&t517, (void *)NULL, cx, NULL, t516);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t518 = t517;
            if (((((uint8_t *)&t518))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t518) + 8, gm_srcg0ns322676oc, 544, 58);
            gm_b16_8 t519 = GM_LD(gm_b16_8, (((uint8_t *)&t518) + 8));
            gm_b16_8 t520 = t519;
            gm_b16_8 t521 = GM_LD(gm_b16_8, ((uint8_t *)&t520));
            gm_tf1bjg81u0glv t522;
            gm_f_mfail_uqq7o7g((void *)&t522, t514, t521);
            gm_tmn9qm2bp7f46 t523;
            memset(&t523, 0, sizeof t523);
            ((uint8_t *)&t523)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t523 + 8), t522);
            gm_tmn9qm2bp7f46 t524 = t523;
            uint8_t t525 = ((((uint8_t *)&t524))[0] != 0);
            F.ret = t523;
            goto gm_fret;
        }
        goto ix507;
ie507:;
ix507:;
    }
    {
        gm_b16_8 t527 = gm_l42;
        gm_b16_8 t528 = gm_f_trim_ascii_c9j36d1(t527);
        gm_th2lqbo77pvsc t529;
        gm_f_url_problem_38kdh3a((void *)&t529, (void *)NULL, cx, NULL, t528);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_th2lqbo77pvsc t530 = t529;
        if (!((((uint8_t *)&t530))[16] != 0)) goto ie526;
        gm_b16_8 t531 = GM_LD(gm_b16_8, ((uint8_t *)&t530));
        gm_l43 = t531;
        {
            uint64_t t533 = gm_l2;
            gm_t6j6smj7qoctu t534;
            gm_b16_8 t535 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t534 + 0, t535);
            gm_b16_8 t536 = gm_l43;
            GM_ST(gm_b16_8, (uint8_t *)&t534 + 16, t536);
            gm_tabb710d9mq2e t537;
            gm_f_format_7sbr40s((void *)&t537, (void *)NULL, cx, NULL, &t534);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t538 = t537;
            if (((((uint8_t *)&t538))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t538) + 8, gm_srcg0ns322676oc, 546, 29);
            gm_b16_8 t539 = GM_LD(gm_b16_8, (((uint8_t *)&t538) + 8));
            gm_b16_8 t540 = t539;
            gm_b16_8 t541 = GM_LD(gm_b16_8, ((uint8_t *)&t540));
            gm_tf1bjg81u0glv t542;
            gm_f_mfail_uqq7o7g((void *)&t542, t533, t541);
            gm_tmn9qm2bp7f46 t543;
            memset(&t543, 0, sizeof t543);
            ((uint8_t *)&t543)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t543 + 8), t542);
            gm_tmn9qm2bp7f46 t544 = t543;
            uint8_t t545 = ((((uint8_t *)&t544))[0] != 0);
            F.ret = t543;
            goto gm_fret;
        }
        goto ix526;
ie526:;
ix526:;
    }
    {
        gm_f_list_epvducg((void *)&F.l44, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_b16_8 t547;
            GM_ST(void *, (uint8_t *)&t547, (void *)(gm_sla3alt5340mf));
            GM_ST(uintptr_t, (uint8_t *)&t547 + 8, (uintptr_t)(7));
            GM_ST(gm_b16_8, (uint8_t *)&F.l45 + 0, t547);
            gm_b16_8 t548;
            GM_ST(void *, (uint8_t *)&t548, (void *)(gm_s420tm9ua7ggg));
            GM_ST(uintptr_t, (uint8_t *)&t548 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&F.l45 + 16, t548);
            gm_b16_8 t549;
            GM_ST(void *, (uint8_t *)&t549, (void *)(gm_s0jho24omhfui));
            GM_ST(uintptr_t, (uint8_t *)&t549 + 8, (uintptr_t)(6));
            GM_ST(gm_b16_8, (uint8_t *)&F.l45 + 32, t549);
            gm_b16_8 t550;
            GM_ST(void *, (uint8_t *)&t550, (void *)(gm_sf9ilivr4c86m));
            GM_ST(uintptr_t, (uint8_t *)&t550 + 8, (uintptr_t)(3));
            GM_ST(gm_b16_8, (uint8_t *)&F.l45 + 48, t550);
        }
        {
            {
                {
                    gm_b16_8 t553 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 0));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l46 + 0, t553);
                    gm_b16_8 t554 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 16));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l46 + 16, t554);
                    gm_b16_8 t555 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 32));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l46 + 32, t555);
                    gm_b16_8 t556 = GM_LD(gm_b16_8, (((uint8_t *)&F.l45) + 48));
                    GM_ST(gm_b16_8, (uint8_t *)&F.l46 + 48, t556);
                }
                void * t557 = ((uint8_t *)&F.l46);
                if ((uintptr_t)0 > (uintptr_t)4 || (uintptr_t)4 > (uintptr_t)4) gm_trap_slice((uintptr_t)0, (uintptr_t)4, 4, gm_srcg0ns322676oc, 549, 5);
                gm_b16_8 t558;
                GM_ST(void *, (uint8_t *)&t558, (void *)(((uint8_t *)t557 + (uintptr_t)0 * 16)));
                GM_ST(uintptr_t, (uint8_t *)&t558 + 8, (uintptr_t)((uintptr_t)4 - (uintptr_t)0));
                gm_b16_8 t559 = t558;
                gm_l47 = t559;
            }
            {
                gm_l48 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t561 = gm_l48;
                    uint64_t t562 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                    uint8_t t563 = (t561 < t562);
                    if (!t563) goto lb560;
                    {
                        uint64_t t566 = gm_l48;
                        uint64_t t567 = (uintptr_t)t566;
                        uint64_t t568 = GM_LD(uintptr_t, ((uint8_t *)&gm_l47) + 8);
                        gm_b16_8 t571 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l47)) + t567 * 16));
                        gm_l49 = t571;
                        {
                            void * t574 = gm_l3;
                            gm_b16_8 t575 = gm_l49;
                            void * t576 = gm_f_get_9rme0gb(t574, t575);
                            void * t577 = t576;
                            uint8_t t578;
                            if ((GM_LD(void *, ((uint8_t *)&t577)) != 0)) goto mn580;
                            t578 = ((uint8_t)0);
                            goto me579;
mn580:;
                            t578 = ((uint8_t)1);
                            goto me579;
mn581:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 550, 12);
me579:;
                            if (!t578) goto ie573;
                            {
                                void * t583 = (void *)((uint8_t *)&F.l44);
                                gm_b16_8 t584 = gm_l49;
                                gm_tftb1odc739t5 t585 = gm_f_push_3ef746b(t583, t584);
                                gm_tftb1odc739t5 t586 = t585;
                                if (((((uint8_t *)&t586))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t586) + 1, gm_srcg0ns322676oc, 550, 35);
                                gm_b0_1 t587 = GM_LD(gm_b0_1, (((uint8_t *)&t586) + 1));
                            }
                            goto ix573;
ie573:;
ix573:;
                        }
                        goto me569;
mn570:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 549, 5);
me569:;
                    }
lc560:;
                    {
                        uint64_t t589 = gm_l48;
                        uint64_t r590; bool o591 = __builtin_add_overflow(t589, ((uint64_t)1ULL), &r590);
                        uint64_t t592 = r590;
                        gm_l48 = t592;
                    }
                }
lb560:;
            }
        }
    }
    {
        uint64_t t594 = GM_LD(uintptr_t, (((uint8_t *)&F.l44) + 0) + 8);
        uint8_t t595 = (t594 > ((uint64_t)1ULL));
        if (!t595) goto ie593;
        {
            uint64_t t597 = gm_l2;
            gm_tfeab279c16rr t598;
            gm_b16_8 t599 = gm_l0;
            GM_ST(gm_b16_8, (uint8_t *)&t598 + 0, t599);
            uint64_t t600 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t601 = GM_LD(uintptr_t, (((uint8_t *)&F.l44) + 0) + 8);
            if (t600 >= (uintptr_t)(t601)) gm_trap_index(t600, t601, gm_srcg0ns322676oc, 556, 13);
            gm_b16_8 t602 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l44) + 0)) + t600 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t598 + 16, t602);
            uint64_t t603 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t604 = GM_LD(uintptr_t, (((uint8_t *)&F.l44) + 0) + 8);
            if (t603 >= (uintptr_t)(t604)) gm_trap_index(t603, t604, gm_srcg0ns322676oc, 557, 13);
            gm_b16_8 t605 = GM_LD(gm_b16_8, ((uint8_t *)GM_LD(void *, (((uint8_t *)&F.l44) + 0)) + t603 * 16));
            GM_ST(gm_b16_8, (uint8_t *)&t598 + 32, t605);
            gm_tabb710d9mq2e t606;
            gm_f_format_4af676s((void *)&t606, (void *)NULL, cx, NULL, &t598);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t607 = t606;
            if (((((uint8_t *)&t607))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t607) + 8, gm_srcg0ns322676oc, 553, 29);
            gm_b16_8 t608 = GM_LD(gm_b16_8, (((uint8_t *)&t607) + 8));
            gm_b16_8 t609 = t608;
            gm_b16_8 t610 = GM_LD(gm_b16_8, ((uint8_t *)&t609));
            gm_tf1bjg81u0glv t611;
            gm_f_mfail_uqq7o7g((void *)&t611, t597, t610);
            gm_tmn9qm2bp7f46 t612;
            memset(&t612, 0, sizeof t612);
            ((uint8_t *)&t612)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t612 + 8), t611);
            gm_tmn9qm2bp7f46 t613 = t612;
            uint8_t t614 = ((((uint8_t *)&t613))[0] != 0);
            F.ret = t612;
            goto gm_fret;
        }
        goto ix593;
ie593:;
ix593:;
    }
    {
        gm_toq2f2q4opoa3 t615;
        memset(&t615, 0, sizeof t615);
        GM_ST(uint8_t, ((uint8_t *)&t615 + 0), ((uint8_t)0ULL));
        F.l50 = t615;
    }
    {
        void * t617 = gm_l3;
        gm_b16_8 t618;
        GM_ST(void *, (uint8_t *)&t618, (void *)(gm_sla3alt5340mf));
        GM_ST(uintptr_t, (uint8_t *)&t618 + 8, (uintptr_t)(7));
        gm_b16_8 t619 = gm_l0;
        gm_tccklsmdasd9s t620;
        gm_f_dep_string_mmgrsgi((void *)&t620, (void *)NULL, cx, NULL, t617, t618, t619);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tccklsmdasd9s t621 = t620;
        if (((((uint8_t *)&t621))[0] != 0)) {
            gm_tf1bjg81u0glv t622 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t621) + 8));
            gm_tmn9qm2bp7f46 t623;
            memset(&t623, 0, sizeof t623);
            ((uint8_t *)&t623)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t623 + 8), t622);
            F.ret = t623;
            goto gm_fret;
        }
        gm_th2lqbo77pvsc t624 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t621) + 8));
        gm_th2lqbo77pvsc t625 = t624;
        if (!((((uint8_t *)&t625))[16] != 0)) goto ie616;
        gm_b16_8 t626 = GM_LD(gm_b16_8, ((uint8_t *)&t625));
        gm_l51 = t626;
        {
            gm_b16_8 t628 = gm_l51;
            gm_b16_8 t629 = gm_f_trim_ascii_c9j36d1(t628);
            gm_t62efos6ssciq t630;
            gm_f_parse_version_req_alipvfi((void *)&t630, (void *)NULL, cx, NULL, t629);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t62efos6ssciq t631 = t630;
            gm_tin97tt4ud0s1 t632;
            if (!((((uint8_t *)&t631))[0] != 0)) {
                gm_tin97tt4ud0s1 t633 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)&t631) + 8));
                t632 = t633;
            } else {
                gm_b16_8 t634 = GM_LD(gm_b16_8, (((uint8_t *)&t631) + 8));
                gm_l52 = t634;
                void * t636 = gm_l3;
                gm_b16_8 t637;
                GM_ST(void *, (uint8_t *)&t637, (void *)(gm_sla3alt5340mf));
                GM_ST(uintptr_t, (uint8_t *)&t637 + 8, (uintptr_t)(7));
                void * t638 = gm_f_get_9rme0gb(t636, t637);
                void * t639 = t638;
                if (!(GM_LD(void *, ((uint8_t *)&t639)) != 0)) gm_trap("unwrap of null", gm_srcg0ns322676oc, 563, 27);
                void * t640 = GM_LD(void *, ((uint8_t *)&t639));
                uint64_t t641 = GM_LD(uint64_t, (((uint8_t *)t640) + 64));
                gm_t6j6smj7qoctu t642;
                gm_b16_8 t643 = gm_l51;
                GM_ST(gm_b16_8, (uint8_t *)&t642 + 0, t643);
                gm_b16_8 t644 = gm_l52;
                GM_ST(gm_b16_8, (uint8_t *)&t642 + 16, t644);
                gm_tabb710d9mq2e t645;
                gm_f_format_pah1p62((void *)&t645, (void *)NULL, cx, NULL, &t642);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t646 = t645;
                if (((((uint8_t *)&t646))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t646) + 8, gm_srcg0ns322676oc, 563, 51);
                gm_b16_8 t647 = GM_LD(gm_b16_8, (((uint8_t *)&t646) + 8));
                gm_b16_8 t648 = t647;
                gm_b16_8 t649 = GM_LD(gm_b16_8, ((uint8_t *)&t648));
                gm_tf1bjg81u0glv t650;
                gm_f_mfail_uqq7o7g((void *)&t650, t641, t649);
                gm_tmn9qm2bp7f46 t651;
                memset(&t651, 0, sizeof t651);
                ((uint8_t *)&t651)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t651 + 8), t650);
                gm_tmn9qm2bp7f46 t652 = t651;
                uint8_t t653 = ((((uint8_t *)&t652))[0] != 0);
                F.ret = t651;
                goto gm_fret;
            }
            F.l53 = t632;
        }
        {
            gm_tin97tt4ud0s1 t654 = F.l53;
            gm_toq2f2q4opoa3 t655;
            GM_ST(uint8_t, (uint8_t *)&t655 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t655 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t655 + 4, 0);
            GM_ST(uint8_t, ((uint8_t *)&t655 + 0), ((uint8_t)1ULL));
            GM_ST(gm_tin97tt4ud0s1, ((uint8_t *)&t655 + 8), t654);
            F.l50 = t655;
        }
        goto ix616;
ie616:;
        void * t657 = gm_l3;
        gm_b16_8 t658;
        GM_ST(void *, (uint8_t *)&t658, (void *)(gm_s420tm9ua7ggg));
        GM_ST(uintptr_t, (uint8_t *)&t658 + 8, (uintptr_t)(3));
        gm_b16_8 t659 = gm_l0;
        gm_tccklsmdasd9s t660;
        gm_f_dep_string_mmgrsgi((void *)&t660, (void *)NULL, cx, NULL, t657, t658, t659);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tccklsmdasd9s t661 = t660;
        if (((((uint8_t *)&t661))[0] != 0)) {
            gm_tf1bjg81u0glv t662 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t661) + 8));
            gm_tmn9qm2bp7f46 t663;
            memset(&t663, 0, sizeof t663);
            ((uint8_t *)&t663)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t663 + 8), t662);
            F.ret = t663;
            goto gm_fret;
        }
        gm_th2lqbo77pvsc t664 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t661) + 8));
        gm_th2lqbo77pvsc t665 = t664;
        if (!((((uint8_t *)&t665))[16] != 0)) goto ie656;
        gm_b16_8 t666 = GM_LD(gm_b16_8, ((uint8_t *)&t665));
        gm_l54 = t666;
        {
            gm_b16_8 t668 = gm_l54;
            gm_toq2f2q4opoa3 t669;
            GM_ST(uint8_t, (uint8_t *)&t669 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t669 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t669 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t669 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t669 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t669 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t669 + 0), ((uint8_t)2ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t669 + 8), t668);
            F.l50 = t669;
        }
        goto ix656;
ie656:;
        void * t671 = gm_l3;
        gm_b16_8 t672;
        GM_ST(void *, (uint8_t *)&t672, (void *)(gm_s0jho24omhfui));
        GM_ST(uintptr_t, (uint8_t *)&t672 + 8, (uintptr_t)(6));
        gm_b16_8 t673 = gm_l0;
        gm_tccklsmdasd9s t674;
        gm_f_dep_string_mmgrsgi((void *)&t674, (void *)NULL, cx, NULL, t671, t672, t673);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tccklsmdasd9s t675 = t674;
        if (((((uint8_t *)&t675))[0] != 0)) {
            gm_tf1bjg81u0glv t676 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t675) + 8));
            gm_tmn9qm2bp7f46 t677;
            memset(&t677, 0, sizeof t677);
            ((uint8_t *)&t677)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t677 + 8), t676);
            F.ret = t677;
            goto gm_fret;
        }
        gm_th2lqbo77pvsc t678 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t675) + 8));
        gm_th2lqbo77pvsc t679 = t678;
        if (!((((uint8_t *)&t679))[16] != 0)) goto ie670;
        gm_b16_8 t680 = GM_LD(gm_b16_8, ((uint8_t *)&t679));
        gm_l55 = t680;
        {
            gm_b16_8 t682 = gm_l55;
            gm_toq2f2q4opoa3 t683;
            GM_ST(uint8_t, (uint8_t *)&t683 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t683 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t683 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t683 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t683 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t683 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t683 + 0), ((uint8_t)3ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t683 + 8), t682);
            F.l50 = t683;
        }
        goto ix670;
ie670:;
        void * t685 = gm_l3;
        gm_b16_8 t686;
        GM_ST(void *, (uint8_t *)&t686, (void *)(gm_sf9ilivr4c86m));
        GM_ST(uintptr_t, (uint8_t *)&t686 + 8, (uintptr_t)(3));
        gm_b16_8 t687 = gm_l0;
        gm_tccklsmdasd9s t688;
        gm_f_dep_string_mmgrsgi((void *)&t688, (void *)NULL, cx, NULL, t685, t686, t687);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tccklsmdasd9s t689 = t688;
        if (((((uint8_t *)&t689))[0] != 0)) {
            gm_tf1bjg81u0glv t690 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t689) + 8));
            gm_tmn9qm2bp7f46 t691;
            memset(&t691, 0, sizeof t691);
            ((uint8_t *)&t691)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t691 + 8), t690);
            F.ret = t691;
            goto gm_fret;
        }
        gm_th2lqbo77pvsc t692 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t689) + 8));
        gm_th2lqbo77pvsc t693 = t692;
        if (!((((uint8_t *)&t693))[16] != 0)) goto ie684;
        gm_b16_8 t694 = GM_LD(gm_b16_8, ((uint8_t *)&t693));
        gm_l56 = t694;
        {
            uint64_t t696 = GM_LD(uintptr_t, ((uint8_t *)&gm_l56) + 8);
            uint8_t t697 = (t696 > ((uint64_t)0ULL));
            gm_l57 = t697;
        }
        {
            {
                gm_b16_8 t699 = gm_l56;
                gm_l58 = t699;
            }
            {
                gm_l59 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t701 = gm_l59;
                    uint64_t t702 = GM_LD(uintptr_t, ((uint8_t *)&gm_l58) + 8);
                    uint8_t t703 = (t701 < t702);
                    if (!t703) goto lb700;
                    {
                        uint64_t t706 = gm_l59;
                        uint64_t t707 = (uintptr_t)t706;
                        uint64_t t708 = GM_LD(uintptr_t, ((uint8_t *)&gm_l58) + 8);
                        uint8_t t709 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l58)) + t707 * 1));
                        uint8_t t710 = t709;
                        uint8_t t713 = GM_LD(uint8_t, ((uint8_t *)&t710));
                        gm_l60 = t713;
                        {
                            uint8_t t716 = gm_l60;
                            uint8_t t717 = gm_f_is_hex_digit_q8s6utq(t716);
                            uint8_t t718 = (uint8_t)!t717;
                            if (!t718) goto ie715;
                            {
                                gm_l57 = ((uint8_t)0);
                            }
                            goto ix715;
ie715:;
ix715:;
                        }
                        goto me711;
mn712:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 572, 9);
me711:;
                    }
lc700:;
                    {
                        uint64_t t721 = gm_l59;
                        uint64_t r722; bool o723 = __builtin_add_overflow(t721, ((uint64_t)1ULL), &r722);
                        uint64_t t724 = r722;
                        gm_l59 = t724;
                    }
                }
lb700:;
            }
        }
        {
            uint8_t t726 = gm_l57;
            uint8_t t727 = (uint8_t)!t726;
            if (!t727) goto ie725;
            {
                uint64_t t729 = gm_l2;
                gm_b16_8 t730 = gm_l0;
                gm_tnpifv20irnph t731;
                GM_ST(gm_b16_8, ((uint8_t *)&t731 + 0), t730);
                gm_tabb710d9mq2e t732;
                gm_f_format_ll295o3((void *)&t732, (void *)NULL, cx, NULL, t731);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t733 = t732;
                if (((((uint8_t *)&t733))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t733) + 8, gm_srcg0ns322676oc, 575, 41);
                gm_b16_8 t734 = GM_LD(gm_b16_8, (((uint8_t *)&t733) + 8));
                gm_b16_8 t735 = t734;
                gm_b16_8 t736 = GM_LD(gm_b16_8, ((uint8_t *)&t735));
                gm_tf1bjg81u0glv t737;
                gm_f_mfail_uqq7o7g((void *)&t737, t729, t736);
                gm_tmn9qm2bp7f46 t738;
                memset(&t738, 0, sizeof t738);
                ((uint8_t *)&t738)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t738 + 8), t737);
                gm_tmn9qm2bp7f46 t739 = t738;
                uint8_t t740 = ((((uint8_t *)&t739))[0] != 0);
                F.ret = t738;
                goto gm_fret;
            }
            goto ix725;
ie725:;
ix725:;
        }
        {
            gm_b16_8 t741 = gm_l56;
            gm_b16_8 t742 = gm_f_lower_ascii_uggh3ca((void *)NULL, cx, NULL, t741);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_toq2f2q4opoa3 t743;
            GM_ST(uint8_t, (uint8_t *)&t743 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t743 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t743 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t743 + 24, 0);
            GM_ST(uint64_t, (uint8_t *)&t743 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t743 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t743 + 0), ((uint8_t)4ULL));
            GM_ST(gm_b16_8, ((uint8_t *)&t743 + 8), t742);
            F.l50 = t743;
        }
        goto ix684;
ie684:;
ix684:;
ix670:;
ix656:;
ix616:;
    }
    {
        void * t744 = gm_l3;
        gm_b16_8 t745;
        GM_ST(void *, (uint8_t *)&t745, (void *)(gm_sfvspsnv23gbt));
        GM_ST(uintptr_t, (uint8_t *)&t745 + 8, (uintptr_t)(6));
        gm_b16_8 t746 = gm_l0;
        gm_tccklsmdasd9s t747;
        gm_f_dep_string_mmgrsgi((void *)&t747, (void *)NULL, cx, NULL, t744, t745, t746);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tccklsmdasd9s t748 = t747;
        if (((((uint8_t *)&t748))[0] != 0)) {
            gm_tf1bjg81u0glv t749 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t748) + 8));
            gm_tmn9qm2bp7f46 t750;
            memset(&t750, 0, sizeof t750);
            ((uint8_t *)&t750)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t750 + 8), t749);
            F.ret = t750;
            goto gm_fret;
        }
        gm_th2lqbo77pvsc t751 = GM_LD(gm_th2lqbo77pvsc, (((uint8_t *)&t748) + 8));
        gm_l61 = t751;
    }
    {
        gm_b16_8 t752 = gm_l0;
        uint64_t t753 = gm_l2;
        gm_t6h2b31csotun t754;
        gm_f_check_subdir_2uljb06((void *)&t754, (void *)NULL, cx, NULL, t752, &(*(gm_th2lqbo77pvsc *)((uint8_t *)&gm_l61)), t753);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t6h2b31csotun t755 = t754;
        if (((((uint8_t *)&t755))[0] != 0)) {
            gm_tf1bjg81u0glv t756 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t755) + 8));
            gm_tmn9qm2bp7f46 t757;
            memset(&t757, 0, sizeof t757);
            ((uint8_t *)&t757)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t757 + 8), t756);
            F.ret = t757;
            goto gm_fret;
        }
        gm_b0_1 t758 = GM_LD(gm_b0_1, (((uint8_t *)&t755) + 8));
    }
    {
        gm_te5jk64pjl28k t759;
        gm_b16_8 t760 = gm_l42;
        GM_ST(gm_b16_8, (uint8_t *)&t759 + 0, t760);
        memcpy((uint8_t *)&t759 + 16, ((uint8_t *)&F.l50), 48);
        memcpy((uint8_t *)&t759 + 64, ((uint8_t *)&gm_l61), 24);
        gm_tnlfbo6r4vlhg t761;
        GM_ST(uint8_t, (uint8_t *)&t761 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t761 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t761 + 4, 0);
        GM_ST(uint8_t, ((uint8_t *)&t761 + 0), ((uint8_t)1ULL));
        GM_ST(gm_te5jk64pjl28k, ((uint8_t *)&t761 + 8), t759);
        gm_tmn9qm2bp7f46 t762;
        GM_ST(uint8_t, (uint8_t *)&t762 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t762 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t762 + 4, 0);
        ((uint8_t *)&t762)[0] = 0;
        GM_ST(gm_tnlfbo6r4vlhg, ((uint8_t *)&t762 + 8), t761);
        gm_tmn9qm2bp7f46 t763 = t762;
        uint8_t t764 = ((((uint8_t *)&t763))[0] != 0);
        F.ret = t762;
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
__attribute__((aligned(4))) void gm_f_parse_manifest_b61s3ln(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_parse_manifest_b61s3ln F;
    gm_b16_8 gm_l0; /* text */
    gm_tob1snuii5bme gm_l1; /* e */
    void * gm_l2; /* doc */
    void * gm_l4; /* pkg */
    void * gm_l5; /* t */
    void * gm_l6; /* item */
    gm_b16_8 gm_l7; /* n */
    gm_b16_8 gm_l8; /* problem */
    void * gm_l9; /* item */
    gm_b16_8 gm_l10; /* v */
    gm_b16_8 gm_l11; /* e */
    void * gm_l12; /* deps */
    void * gm_l13; /* t */
    gm_b16_8 gm_l14; /* <tmp> */
    uint64_t gm_l15; /* <tmp> */
    gm_b16_8 gm_l17; /* problem */
    void * gm_l19; /* b */
    void * gm_l20; /* t */
    void * gm_l21; /* l */
    void * gm_l22; /* t */
    gm_b16_8 gm_l23; /* <tmp> */
    uint64_t gm_l24; /* <tmp> */
    gm_b16_8 gm_l26; /* level */
    void * gm_l27; /* ps */
    void * gm_l28; /* t */
    gm_b16_8 gm_l29; /* <tmp> */
    uint64_t gm_l30; /* <tmp> */
    uint8_t gm_l32; /* ok */
    gm_b16_8 gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint8_t gm_l35; /* c */
    uint8_t gm_l36; /* <tmp> */
    uint8_t gm_l37; /* <tmp> */
    uint8_t gm_l38; /* <tmp> */
    void * gm_l39; /* pt */
    GM_STACK_CHECK(&F, 18056u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t2 = gm_l0;
        gm_trssmtuv8jr0v t3;
        gm_f_parse_toml_gjtmf79((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_trssmtuv8jr0v t4 = t3;
        void * t5;
        if (!((((uint8_t *)&t4))[0] != 0)) {
            void * t6 = GM_LD(void *, (((uint8_t *)&t4) + 8));
            t5 = t6;
        } else {
            gm_tob1snuii5bme t7 = GM_LD(gm_tob1snuii5bme, (((uint8_t *)&t4) + 8));
            gm_l1 = t7;
            uint64_t t9 = GM_LD(uint64_t, (((uint8_t *)&gm_l1) + 0));
            gm_b16_8 t10 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l1) + 8));
            gm_tf1bjg81u0glv t11;
            gm_f_mfail_uqq7o7g((void *)&t11, t9, t10);
            gm_tjgmtdrk88t7m t12;
            memset(&t12, 0, sizeof t12);
            ((uint8_t *)&t12)[0] = 1;
            GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t12 + 8), t11);
            gm_tjgmtdrk88t7m t13 = t12;
            uint8_t t14 = ((((uint8_t *)&t13))[0] != 0);
            F.ret = t12;
            goto gm_fret;
        }
        gm_l2 = t5;
    }
    {
        gm_th2lqbo77pvsc t15;
        memset(&t15, 0, sizeof t15);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l3 + 0, t15);
        gm_th2lqbo77pvsc t16;
        memset(&t16, 0, sizeof t16);
        GM_ST(gm_th2lqbo77pvsc, (uint8_t *)&F.l3 + 24, t16);
        gm_f_list_7393u00((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 48)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ti0g3bmheuq9o t17 = GM_LD(gm_ti0g3bmheuq9o, gm_cb60tlkkq8o);
        GM_ST(gm_ti0g3bmheuq9o, (uint8_t *)&F.l3 + 88, t17);
        gm_f_list_hs66gbq((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 240)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_list_7mv9vi7((void *)&(*(gm_trqfvmjtt4sij *)((uint8_t *)&F.l3 + 280)), (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t19 = gm_l2;
        gm_b16_8 t20;
        GM_ST(void *, (uint8_t *)&t20, (void *)(gm_snqvo5bgc2fch));
        GM_ST(uintptr_t, (uint8_t *)&t20 + 8, (uintptr_t)(7));
        void * t21 = gm_f_get_9rme0gb(t19, t20);
        void * t22 = t21;
        if (!(GM_LD(void *, ((uint8_t *)&t22)) != 0)) goto ie18;
        void * t23 = GM_LD(void *, ((uint8_t *)&t22));
        gm_l4 = t23;
        {
            void * t25 = gm_l4;
            void * t26 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t25) + 16)));
            void * t27 = t26;
            void * t28;
            if ((GM_LD(void *, ((uint8_t *)&t27)) != 0)) {
                void * t29 = GM_LD(void *, (((uint8_t *)&t27) + 0));
                t28 = t29;
            } else {
                void * t30 = gm_l4;
                uint64_t t31 = GM_LD(uint64_t, (((uint8_t *)t30) + 64));
                gm_b16_8 t32;
                GM_ST(void *, (uint8_t *)&t32, (void *)(gm_sq3r7vbcbs4ll));
                GM_ST(uintptr_t, (uint8_t *)&t32 + 8, (uintptr_t)(25));
                gm_tf1bjg81u0glv t33;
                gm_f_mfail_uqq7o7g((void *)&t33, t31, t32);
                gm_tjgmtdrk88t7m t34;
                memset(&t34, 0, sizeof t34);
                ((uint8_t *)&t34)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t34 + 8), t33);
                gm_tjgmtdrk88t7m t35 = t34;
                uint8_t t36 = ((((uint8_t *)&t35))[0] != 0);
                F.ret = t34;
                goto gm_fret;
            }
            gm_l5 = t28;
        }
        {
            void * t38 = gm_l5;
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sius9pki5u4e3));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(4));
            void * t40 = gm_f_get_9rme0gb(t38, t39);
            void * t41 = t40;
            if (!(GM_LD(void *, ((uint8_t *)&t41)) != 0)) goto ie37;
            void * t42 = GM_LD(void *, ((uint8_t *)&t41));
            gm_l6 = t42;
            {
                void * t44 = gm_l6;
                gm_th2lqbo77pvsc t45;
                gm_f_as_str_982o16v((void *)&t45, &(*(gm_tktr4rmbindgv *)(((uint8_t *)t44) + 16)));
                gm_th2lqbo77pvsc t46 = t45;
                gm_b16_8 t47;
                if (((((uint8_t *)&t46))[16] != 0)) {
                    gm_b16_8 t48 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 0));
                    t47 = t48;
                } else {
                    void * t49 = gm_l6;
                    uint64_t t50 = GM_LD(uint64_t, (((uint8_t *)t49) + 64));
                    gm_b16_8 t51;
                    GM_ST(void *, (uint8_t *)&t51, (void *)(gm_sob5t135sl7q9));
                    GM_ST(uintptr_t, (uint8_t *)&t51 + 8, (uintptr_t)(33));
                    gm_tf1bjg81u0glv t52;
                    gm_f_mfail_uqq7o7g((void *)&t52, t50, t51);
                    gm_tjgmtdrk88t7m t53;
                    memset(&t53, 0, sizeof t53);
                    ((uint8_t *)&t53)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t53 + 8), t52);
                    gm_tjgmtdrk88t7m t54 = t53;
                    uint8_t t55 = ((((uint8_t *)&t54))[0] != 0);
                    F.ret = t53;
                    goto gm_fret;
                }
                gm_l7 = t47;
            }
            {
                gm_b16_8 t57 = gm_l7;
                gm_b16_8 t58;
                GM_ST(void *, (uint8_t *)&t58, (void *)(gm_snqvo5bgc2fch));
                GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(7));
                gm_th2lqbo77pvsc t59;
                gm_f_name_problem_6r4mp11((void *)&t59, (void *)NULL, cx, NULL, t57, t58);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t60 = t59;
                if (!((((uint8_t *)&t60))[16] != 0)) goto ie56;
                gm_b16_8 t61 = GM_LD(gm_b16_8, ((uint8_t *)&t60));
                gm_l8 = t61;
                {
                    void * t63 = gm_l6;
                    uint64_t t64 = GM_LD(uint64_t, (((uint8_t *)t63) + 64));
                    gm_b16_8 t65 = gm_l8;
                    gm_tf1bjg81u0glv t66;
                    gm_f_mfail_uqq7o7g((void *)&t66, t64, t65);
                    gm_tjgmtdrk88t7m t67;
                    memset(&t67, 0, sizeof t67);
                    ((uint8_t *)&t67)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t67 + 8), t66);
                    gm_tjgmtdrk88t7m t68 = t67;
                    uint8_t t69 = ((((uint8_t *)&t68))[0] != 0);
                    F.ret = t67;
                    goto gm_fret;
                }
                goto ix56;
ie56:;
ix56:;
            }
            {
                gm_b16_8 t70 = gm_l7;
                gm_th2lqbo77pvsc t71;
                GM_ST(uint8_t, (uint8_t *)&t71 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t71 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t71 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t71), t70);
                ((uint8_t *)&t71)[16] = 1;
                GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l3) + 0), t71);
            }
            goto ix37;
ie37:;
ix37:;
        }
        {
            void * t73 = gm_l5;
            gm_b16_8 t74;
            GM_ST(void *, (uint8_t *)&t74, (void *)(gm_sla3alt5340mf));
            GM_ST(uintptr_t, (uint8_t *)&t74 + 8, (uintptr_t)(7));
            void * t75 = gm_f_get_9rme0gb(t73, t74);
            void * t76 = t75;
            if (!(GM_LD(void *, ((uint8_t *)&t76)) != 0)) goto ie72;
            void * t77 = GM_LD(void *, ((uint8_t *)&t76));
            gm_l9 = t77;
            {
                void * t79 = gm_l9;
                gm_th2lqbo77pvsc t80;
                gm_f_as_str_982o16v((void *)&t80, &(*(gm_tktr4rmbindgv *)(((uint8_t *)t79) + 16)));
                gm_th2lqbo77pvsc t81 = t80;
                gm_b16_8 t82;
                if (((((uint8_t *)&t81))[16] != 0)) {
                    gm_b16_8 t83 = GM_LD(gm_b16_8, (((uint8_t *)&t81) + 0));
                    t82 = t83;
                } else {
                    void * t84 = gm_l9;
                    uint64_t t85 = GM_LD(uint64_t, (((uint8_t *)t84) + 64));
                    gm_b16_8 t86;
                    GM_ST(void *, (uint8_t *)&t86, (void *)(gm_su0gvqh5vvvlj));
                    GM_ST(uintptr_t, (uint8_t *)&t86 + 8, (uintptr_t)(36));
                    gm_tf1bjg81u0glv t87;
                    gm_f_mfail_uqq7o7g((void *)&t87, t85, t86);
                    gm_tjgmtdrk88t7m t88;
                    memset(&t88, 0, sizeof t88);
                    ((uint8_t *)&t88)[0] = 1;
                    GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t88 + 8), t87);
                    gm_tjgmtdrk88t7m t89 = t88;
                    uint8_t t90 = ((((uint8_t *)&t89))[0] != 0);
                    F.ret = t88;
                    goto gm_fret;
                }
                gm_l10 = t82;
            }
            {
                {
                    gm_b16_8 t92 = gm_l10;
                    gm_tvtl3n8qta358 t93;
                    gm_f_parse_version_9a6ns50((void *)&t93, (void *)NULL, cx, NULL, t92);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tvtl3n8qta358 t94 = t93;
                    gm_tu1089hhijadf t95;
                    if (!((((uint8_t *)&t94))[0] != 0)) {
                        gm_tu1089hhijadf t96 = GM_LD(gm_tu1089hhijadf, (((uint8_t *)&t94) + 8));
                        t95 = t96;
                    } else {
                        gm_b16_8 t97 = GM_LD(gm_b16_8, (((uint8_t *)&t94) + 8));
                        gm_l11 = t97;
                        void * t99 = gm_l9;
                        uint64_t t100 = GM_LD(uint64_t, (((uint8_t *)t99) + 64));
                        gm_t6j6smj7qoctu t101;
                        gm_b16_8 t102 = gm_l10;
                        GM_ST(gm_b16_8, (uint8_t *)&t101 + 0, t102);
                        gm_b16_8 t103 = gm_l11;
                        GM_ST(gm_b16_8, (uint8_t *)&t101 + 16, t103);
                        gm_tabb710d9mq2e t104;
                        gm_f_format_7duae7m((void *)&t104, (void *)NULL, cx, NULL, &t101);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t105 = t104;
                        if (((((uint8_t *)&t105))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t105) + 8, gm_srcg0ns322676oc, 290, 42);
                        gm_b16_8 t106 = GM_LD(gm_b16_8, (((uint8_t *)&t105) + 8));
                        gm_b16_8 t107 = t106;
                        gm_b16_8 t108 = GM_LD(gm_b16_8, ((uint8_t *)&t107));
                        gm_tf1bjg81u0glv t109;
                        gm_f_mfail_uqq7o7g((void *)&t109, t100, t108);
                        gm_tjgmtdrk88t7m t110;
                        memset(&t110, 0, sizeof t110);
                        ((uint8_t *)&t110)[0] = 1;
                        GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t110 + 8), t109);
                        gm_tjgmtdrk88t7m t111 = t110;
                        uint8_t t112 = ((((uint8_t *)&t111))[0] != 0);
                        F.ret = t110;
                        goto gm_fret;
                    }
                }
            }
            {
                gm_b16_8 t113 = gm_l10;
                gm_th2lqbo77pvsc t114;
                GM_ST(uint8_t, (uint8_t *)&t114 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t114 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t114 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t114), t113);
                ((uint8_t *)&t114)[16] = 1;
                GM_ST(gm_th2lqbo77pvsc, (((uint8_t *)&F.l3) + 24), t114);
            }
            goto ix72;
ie72:;
ix72:;
        }
        goto ix18;
ie18:;
ix18:;
    }
    {
        void * t116 = gm_l2;
        gm_b16_8 t117;
        GM_ST(void *, (uint8_t *)&t117, (void *)(gm_skof48dte1s1o));
        GM_ST(uintptr_t, (uint8_t *)&t117 + 8, (uintptr_t)(12));
        void * t118 = gm_f_get_9rme0gb(t116, t117);
        void * t119 = t118;
        if (!(GM_LD(void *, ((uint8_t *)&t119)) != 0)) goto ie115;
        void * t120 = GM_LD(void *, ((uint8_t *)&t119));
        gm_l12 = t120;
        {
            void * t122 = gm_l12;
            void * t123 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t122) + 16)));
            void * t124 = t123;
            void * t125;
            if ((GM_LD(void *, ((uint8_t *)&t124)) != 0)) {
                void * t126 = GM_LD(void *, (((uint8_t *)&t124) + 0));
                t125 = t126;
            } else {
                void * t127 = gm_l12;
                uint64_t t128 = GM_LD(uint64_t, (((uint8_t *)t127) + 64));
                gm_b16_8 t129;
                GM_ST(void *, (uint8_t *)&t129, (void *)(gm_sfag3nu74qn7r));
                GM_ST(uintptr_t, (uint8_t *)&t129 + 8, (uintptr_t)(30));
                gm_tf1bjg81u0glv t130;
                gm_f_mfail_uqq7o7g((void *)&t130, t128, t129);
                gm_tjgmtdrk88t7m t131;
                memset(&t131, 0, sizeof t131);
                ((uint8_t *)&t131)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t131 + 8), t130);
                gm_tjgmtdrk88t7m t132 = t131;
                uint8_t t133 = ((((uint8_t *)&t132))[0] != 0);
                F.ret = t131;
                goto gm_fret;
            }
            gm_l13 = t125;
        }
        {
            {
                void * t135 = gm_l13;
                gm_b16_8 t136 = GM_LD(gm_b16_8, ((((uint8_t *)t135) + 0) + 0));
                gm_l14 = t136;
            }
            {
                gm_l15 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t138 = gm_l15;
                    uint64_t t139 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                    uint8_t t140 = (t138 < t139);
                    if (!t140) goto lb137;
                    {
                        uint64_t t143 = gm_l15;
                        uint64_t t144 = (uintptr_t)t143;
                        uint64_t t145 = GM_LD(uintptr_t, ((uint8_t *)&gm_l14) + 8);
                        gm_trc3jdv2g5qre t148 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l14)) + t144 * 72));
                        F.l16 = t148;
                        {
                            gm_b16_8 t151 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                            gm_b16_8 t152;
                            GM_ST(void *, (uint8_t *)&t152, (void *)(gm_s8f9rv6hrr024));
                            GM_ST(uintptr_t, (uint8_t *)&t152 + 8, (uintptr_t)(10));
                            gm_th2lqbo77pvsc t153;
                            gm_f_name_problem_6r4mp11((void *)&t153, (void *)NULL, cx, NULL, t151, t152);
                            if (gm_unw.kind) {
                                goto gm_ll141;
                            }
                            gm_th2lqbo77pvsc t154 = t153;
                            if (!((((uint8_t *)&t154))[16] != 0)) goto ie150;
                            gm_b16_8 t155 = GM_LD(gm_b16_8, ((uint8_t *)&t154));
                            gm_l17 = t155;
                            {
                                uint64_t t157 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 64));
                                gm_b16_8 t158 = gm_l17;
                                gm_tf1bjg81u0glv t159;
                                gm_f_mfail_uqq7o7g((void *)&t159, t157, t158);
                                gm_tjgmtdrk88t7m t160;
                                memset(&t160, 0, sizeof t160);
                                ((uint8_t *)&t160)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t160 + 8), t159);
                                gm_tjgmtdrk88t7m t161 = t160;
                                uint8_t t162 = ((((uint8_t *)&t161))[0] != 0);
                                F.ret = t160;
                                goto gm_fret;
                            }
                            goto ix150;
ie150:;
ix150:;
                        }
                        {
                            gm_b16_8 t163 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                            uint64_t t164 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 64));
                            gm_tmn9qm2bp7f46 t165;
                            gm_f_parse_dependency_8qmcjrm((void *)&t165, (void *)NULL, cx, NULL, t163, &(*(gm_tktr4rmbindgv *)(((uint8_t *)&F.l16) + 16)), t164);
                            if (gm_unw.kind) {
                                goto gm_ll141;
                            }
                            gm_tmn9qm2bp7f46 t166 = t165;
                            if (((((uint8_t *)&t166))[0] != 0)) {
                                gm_tf1bjg81u0glv t167 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t166) + 8));
                                gm_tjgmtdrk88t7m t168;
                                memset(&t168, 0, sizeof t168);
                                ((uint8_t *)&t168)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t168 + 8), t167);
                                F.ret = t168;
                                goto gm_fret;
                            }
                            gm_tnlfbo6r4vlhg t169 = GM_LD(gm_tnlfbo6r4vlhg, (((uint8_t *)&t166) + 8));
                            F.l18 = t169;
                        }
                        {
                            void * t170 = (void *)(((uint8_t *)&F.l3) + 48);
                            gm_t7tiv6vif86qg t171;
                            gm_b16_8 t172 = GM_LD(gm_b16_8, (((uint8_t *)&F.l16) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t171 + 0, t172);
                            uint64_t t173 = GM_LD(uint64_t, (((uint8_t *)&F.l16) + 64));
                            GM_ST(uint64_t, (uint8_t *)&t171 + 16, t173);
                            memcpy((uint8_t *)&t171 + 24, ((uint8_t *)&F.l18), 96);
                            gm_tftb1odc739t5 t174 = gm_f_push_pdknc9g(t170, &t171);
                            gm_tftb1odc739t5 t175 = t174;
                            if (((((uint8_t *)&t175))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t175) + 1, gm_srcg0ns322676oc, 300, 13);
                            gm_b0_1 t176 = GM_LD(gm_b0_1, (((uint8_t *)&t175) + 1));
                        }
                        goto me146;
mn147:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 297, 9);
me146:;
                    }
lc137:;
                    {
                        uint64_t t178 = gm_l15;
                        uint64_t r179; bool o180 = __builtin_add_overflow(t178, ((uint64_t)1ULL), &r179);
                        uint64_t t181 = r179;
                        gm_l15 = t181;
                    }
                }
gm_ll141:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb137; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc137; }
                }
                goto gm_lad;
lb137:;
            }
        }
        goto ix115;
ie115:;
ix115:;
    }
    {
        void * t183 = gm_l2;
        gm_b16_8 t184;
        GM_ST(void *, (uint8_t *)&t184, (void *)(gm_slllafvm0pbeb));
        GM_ST(uintptr_t, (uint8_t *)&t184 + 8, (uintptr_t)(5));
        void * t185 = gm_f_get_9rme0gb(t183, t184);
        void * t186 = t185;
        if (!(GM_LD(void *, ((uint8_t *)&t186)) != 0)) goto ie182;
        void * t187 = GM_LD(void *, ((uint8_t *)&t186));
        gm_l19 = t187;
        {
            void * t189 = gm_l19;
            void * t190 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t189) + 16)));
            void * t191 = t190;
            void * t192;
            if ((GM_LD(void *, ((uint8_t *)&t191)) != 0)) {
                void * t193 = GM_LD(void *, (((uint8_t *)&t191) + 0));
                t192 = t193;
            } else {
                void * t194 = gm_l19;
                uint64_t t195 = GM_LD(uint64_t, (((uint8_t *)t194) + 64));
                gm_b16_8 t196;
                GM_ST(void *, (uint8_t *)&t196, (void *)(gm_s5tee1epg2e5a));
                GM_ST(uintptr_t, (uint8_t *)&t196 + 8, (uintptr_t)(23));
                gm_tf1bjg81u0glv t197;
                gm_f_mfail_uqq7o7g((void *)&t197, t195, t196);
                gm_tjgmtdrk88t7m t198;
                memset(&t198, 0, sizeof t198);
                ((uint8_t *)&t198)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t198 + 8), t197);
                gm_tjgmtdrk88t7m t199 = t198;
                uint8_t t200 = ((((uint8_t *)&t199))[0] != 0);
                F.ret = t198;
                goto gm_fret;
            }
            gm_l20 = t192;
        }
        {
            void * t201 = gm_l20;
            gm_b16_8 t202;
            GM_ST(void *, (uint8_t *)&t202, (void *)(gm_s5ojatldfpfs2));
            GM_ST(uintptr_t, (uint8_t *)&t202 + 8, (uintptr_t)(7));
            gm_tacj1cskem694 t203;
            gm_f_parse_build_c2teosa((void *)&t203, (void *)NULL, cx, NULL, t201, t202);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tacj1cskem694 t204 = t203;
            if (((((uint8_t *)&t204))[0] != 0)) {
                gm_tf1bjg81u0glv t205 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t204) + 8));
                gm_tjgmtdrk88t7m t206;
                memset(&t206, 0, sizeof t206);
                ((uint8_t *)&t206)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t206 + 8), t205);
                F.ret = t206;
                goto gm_fret;
            }
            gm_ti0g3bmheuq9o t207 = GM_LD(gm_ti0g3bmheuq9o, (((uint8_t *)&t204) + 8));
            GM_ST(gm_ti0g3bmheuq9o, (((uint8_t *)&F.l3) + 88), t207);
        }
        goto ix182;
ie182:;
ix182:;
    }
    {
        void * t209 = gm_l2;
        gm_b16_8 t210;
        GM_ST(void *, (uint8_t *)&t210, (void *)(gm_sv1gci5lo7scg));
        GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(4));
        void * t211 = gm_f_get_9rme0gb(t209, t210);
        void * t212 = t211;
        if (!(GM_LD(void *, ((uint8_t *)&t212)) != 0)) goto ie208;
        void * t213 = GM_LD(void *, ((uint8_t *)&t212));
        gm_l21 = t213;
        {
            void * t215 = gm_l21;
            void * t216 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t215) + 16)));
            void * t217 = t216;
            void * t218;
            if ((GM_LD(void *, ((uint8_t *)&t217)) != 0)) {
                void * t219 = GM_LD(void *, (((uint8_t *)&t217) + 0));
                t218 = t219;
            } else {
                void * t220 = gm_l21;
                uint64_t t221 = GM_LD(uint64_t, (((uint8_t *)t220) + 64));
                gm_b16_8 t222;
                GM_ST(void *, (uint8_t *)&t222, (void *)(gm_sna94ju8c8em3));
                GM_ST(uintptr_t, (uint8_t *)&t222 + 8, (uintptr_t)(47));
                gm_tf1bjg81u0glv t223;
                gm_f_mfail_uqq7o7g((void *)&t223, t221, t222);
                gm_tjgmtdrk88t7m t224;
                memset(&t224, 0, sizeof t224);
                ((uint8_t *)&t224)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t224 + 8), t223);
                gm_tjgmtdrk88t7m t225 = t224;
                uint8_t t226 = ((((uint8_t *)&t225))[0] != 0);
                F.ret = t224;
                goto gm_fret;
            }
            gm_l22 = t218;
        }
        {
            {
                void * t228 = gm_l22;
                gm_b16_8 t229 = GM_LD(gm_b16_8, ((((uint8_t *)t228) + 0) + 0));
                gm_l23 = t229;
            }
            {
                gm_l24 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t231 = gm_l24;
                    uint64_t t232 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                    uint8_t t233 = (t231 < t232);
                    if (!t233) goto lb230;
                    {
                        uint64_t t236 = gm_l24;
                        uint64_t t237 = (uintptr_t)t236;
                        uint64_t t238 = GM_LD(uintptr_t, ((uint8_t *)&gm_l23) + 8);
                        gm_trc3jdv2g5qre t241 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l23)) + t237 * 72));
                        F.l25 = t241;
                        {
                            uint8_t t244 = (!((GM_LD(uintptr_t, (((uint8_t *)&F.l25) + 0) + 8) == 3) && memcmp(GM_LD(void *, (((uint8_t *)&F.l25) + 0)), gm_s0nf1rg04f3m5, 3) == 0));
                            if (!t244) goto ie243;
                            gm_b16_8 t245 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                            gm_tf5srh4mp4b3i t246 = gm_f_lint_rule_o3ccae1(t245);
                            gm_tf5srh4mp4b3i t247 = t246;
                            uint8_t t248;
                            if (((((uint8_t *)&t247))[8] != 0)) goto mn250;
                            t248 = ((uint8_t)1);
                            goto me249;
mn250:;
                            t248 = ((uint8_t)0);
                            goto me249;
mn251:;
                            gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 310, 39);
me249:;
                            if (!t248) goto ie243;
                            {
                                uint64_t t253 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 64));
                                gm_b16_8 t254 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                                gm_tnpifv20irnph t255;
                                GM_ST(gm_b16_8, ((uint8_t *)&t255 + 0), t254);
                                gm_tabb710d9mq2e t256;
                                gm_f_format_55igra0((void *)&t256, (void *)NULL, cx, NULL, t255);
                                if (gm_unw.kind) {
                                    goto gm_ll234;
                                }
                                gm_tabb710d9mq2e t257 = t256;
                                if (((((uint8_t *)&t257))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t257) + 8, gm_srcg0ns322676oc, 311, 39);
                                gm_b16_8 t258 = GM_LD(gm_b16_8, (((uint8_t *)&t257) + 8));
                                gm_b16_8 t259 = t258;
                                gm_b16_8 t260 = GM_LD(gm_b16_8, ((uint8_t *)&t259));
                                gm_tf1bjg81u0glv t261;
                                gm_f_mfail_uqq7o7g((void *)&t261, t253, t260);
                                gm_tjgmtdrk88t7m t262;
                                memset(&t262, 0, sizeof t262);
                                ((uint8_t *)&t262)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t262 + 8), t261);
                                gm_tjgmtdrk88t7m t263 = t262;
                                uint8_t t264 = ((((uint8_t *)&t263))[0] != 0);
                                F.ret = t262;
                                goto gm_fret;
                            }
                            goto ix243;
ie243:;
ix243:;
                        }
                        {
                            gm_th2lqbo77pvsc t265;
                            gm_f_as_str_982o16v((void *)&t265, &(*(gm_tktr4rmbindgv *)(((uint8_t *)&F.l25) + 16)));
                            gm_th2lqbo77pvsc t266 = t265;
                            gm_b16_8 t267;
                            if (((((uint8_t *)&t266))[16] != 0)) {
                                gm_b16_8 t268 = GM_LD(gm_b16_8, (((uint8_t *)&t266) + 0));
                                t267 = t268;
                            } else {
                                gm_b16_8 t269;
                                GM_ST(void *, (uint8_t *)&t269, (void *)(gm_sr4qbrk6tlgaa));
                                GM_ST(uintptr_t, (uint8_t *)&t269 + 8, (uintptr_t)(0));
                                t267 = t269;
                            }
                            gm_l26 = t267;
                        }
                        {
                            gm_b16_8 t271 = gm_l26;
                            gm_tfeab279c16rr t272;
                            gm_b16_8 t273;
                            GM_ST(void *, (uint8_t *)&t273, (void *)(gm_slb6cj7ndgvdm));
                            GM_ST(uintptr_t, (uint8_t *)&t273 + 8, (uintptr_t)(5));
                            GM_ST(gm_b16_8, (uint8_t *)&t272 + 0, t273);
                            gm_b16_8 t274;
                            GM_ST(void *, (uint8_t *)&t274, (void *)(gm_soofuqhnovv5o));
                            GM_ST(uintptr_t, (uint8_t *)&t274 + 8, (uintptr_t)(4));
                            GM_ST(gm_b16_8, (uint8_t *)&t272 + 16, t274);
                            gm_b16_8 t275;
                            GM_ST(void *, (uint8_t *)&t275, (void *)(gm_s920j0georape));
                            GM_ST(uintptr_t, (uint8_t *)&t275 + 8, (uintptr_t)(4));
                            GM_ST(gm_b16_8, (uint8_t *)&t272 + 32, t275);
                            gm_tfeab279c16rr t276 = t272;
                            uint8_t t277 = 0;
                            gm_b16_8 t278 = t271;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t276) + 0, ((uint8_t *)&t278))) t277 = 1;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t276) + 16, ((uint8_t *)&t278))) t277 = 1;
                            if (gm_eq0iqiubfm7e(((uint8_t *)&t276) + 32, ((uint8_t *)&t278))) t277 = 1;
                            uint8_t t279 = (uint8_t)(!t277);
                            if (!t279) goto ie270;
                            {
                                uint64_t t281 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 64));
                                gm_b16_8 t282 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                                gm_tnpifv20irnph t283;
                                GM_ST(gm_b16_8, ((uint8_t *)&t283 + 0), t282);
                                gm_tabb710d9mq2e t284;
                                gm_f_format_hvnghft((void *)&t284, (void *)NULL, cx, NULL, t283);
                                if (gm_unw.kind) {
                                    goto gm_ll234;
                                }
                                gm_tabb710d9mq2e t285 = t284;
                                if (((((uint8_t *)&t285))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t285) + 8, gm_srcg0ns322676oc, 315, 39);
                                gm_b16_8 t286 = GM_LD(gm_b16_8, (((uint8_t *)&t285) + 8));
                                gm_b16_8 t287 = t286;
                                gm_b16_8 t288 = GM_LD(gm_b16_8, ((uint8_t *)&t287));
                                gm_tf1bjg81u0glv t289;
                                gm_f_mfail_uqq7o7g((void *)&t289, t281, t288);
                                gm_tjgmtdrk88t7m t290;
                                memset(&t290, 0, sizeof t290);
                                ((uint8_t *)&t290)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t290 + 8), t289);
                                gm_tjgmtdrk88t7m t291 = t290;
                                uint8_t t292 = ((((uint8_t *)&t291))[0] != 0);
                                F.ret = t290;
                                goto gm_fret;
                            }
                            goto ix270;
ie270:;
ix270:;
                        }
                        {
                            void * t293 = (void *)(((uint8_t *)&F.l3) + 280);
                            gm_tftksr4op114e t294;
                            gm_b16_8 t295 = GM_LD(gm_b16_8, (((uint8_t *)&F.l25) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t294 + 0, t295);
                            gm_b16_8 t296 = gm_l26;
                            GM_ST(gm_b16_8, (uint8_t *)&t294 + 16, t296);
                            uint64_t t297 = GM_LD(uint64_t, (((uint8_t *)&F.l25) + 64));
                            GM_ST(uint64_t, (uint8_t *)&t294 + 32, t297);
                            gm_tftb1odc739t5 t298 = gm_f_push_dob2kfb(t293, &t294);
                            gm_tftb1odc739t5 t299 = t298;
                            if (((((uint8_t *)&t299))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t299) + 1, gm_srcg0ns322676oc, 317, 13);
                            gm_b0_1 t300 = GM_LD(gm_b0_1, (((uint8_t *)&t299) + 1));
                        }
                        goto me239;
mn240:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 309, 9);
me239:;
                    }
lc230:;
                    {
                        uint64_t t302 = gm_l24;
                        uint64_t r303; bool o304 = __builtin_add_overflow(t302, ((uint64_t)1ULL), &r303);
                        uint64_t t305 = r303;
                        gm_l24 = t305;
                    }
                }
gm_ll234:;
                if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb230; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc230; }
                }
                goto gm_lad;
lb230:;
            }
        }
        goto ix208;
ie208:;
ix208:;
    }
    {
        void * t307 = gm_l2;
        gm_b16_8 t308;
        GM_ST(void *, (uint8_t *)&t308, (void *)(gm_s6v08aos37nfe));
        GM_ST(uintptr_t, (uint8_t *)&t308 + 8, (uintptr_t)(7));
        void * t309 = gm_f_get_9rme0gb(t307, t308);
        void * t310 = t309;
        if (!(GM_LD(void *, ((uint8_t *)&t310)) != 0)) goto ie306;
        void * t311 = GM_LD(void *, ((uint8_t *)&t310));
        gm_l27 = t311;
        {
            void * t313 = gm_l27;
            void * t314 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)t313) + 16)));
            void * t315 = t314;
            void * t316;
            if ((GM_LD(void *, ((uint8_t *)&t315)) != 0)) {
                void * t317 = GM_LD(void *, (((uint8_t *)&t315) + 0));
                t316 = t317;
            } else {
                void * t318 = gm_l27;
                uint64_t t319 = GM_LD(uint64_t, (((uint8_t *)t318) + 64));
                gm_b16_8 t320;
                GM_ST(void *, (uint8_t *)&t320, (void *)(gm_slc3n2ku5uff5));
                GM_ST(uintptr_t, (uint8_t *)&t320 + 8, (uintptr_t)(54));
                gm_tf1bjg81u0glv t321;
                gm_f_mfail_uqq7o7g((void *)&t321, t319, t320);
                gm_tjgmtdrk88t7m t322;
                memset(&t322, 0, sizeof t322);
                ((uint8_t *)&t322)[0] = 1;
                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t322 + 8), t321);
                gm_tjgmtdrk88t7m t323 = t322;
                uint8_t t324 = ((((uint8_t *)&t323))[0] != 0);
                F.ret = t322;
                goto gm_fret;
            }
            gm_l28 = t316;
        }
        {
            {
                void * t326 = gm_l28;
                gm_b16_8 t327 = GM_LD(gm_b16_8, ((((uint8_t *)t326) + 0) + 0));
                gm_l29 = t327;
            }
            {
                gm_l30 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t329 = gm_l30;
                    uint64_t t330 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                    uint8_t t331 = (t329 < t330);
                    if (!t331) goto lb328;
                    {
                        uint64_t t334 = gm_l30;
                        uint64_t t335 = (uintptr_t)t334;
                        uint64_t t336 = GM_LD(uintptr_t, ((uint8_t *)&gm_l29) + 8);
                        gm_trc3jdv2g5qre t339 = GM_LD(gm_trc3jdv2g5qre, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l29)) + t335 * 72));
                        F.l31 = t339;
                        {
                            uint64_t t341 = GM_LD(uintptr_t, (((uint8_t *)&F.l31) + 0) + 8);
                            uint8_t t342 = (t341 > ((uint64_t)0ULL));
                            gm_l32 = t342;
                        }
                        {
                            {
                                gm_b16_8 t344 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                                gm_l33 = t344;
                            }
                            {
                                gm_l34 = ((uint64_t)0ULL);
                            }
                            {
                                for (;;) {
                                    uint64_t t346 = gm_l34;
                                    uint64_t t347 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                    uint8_t t348 = (t346 < t347);
                                    if (!t348) goto lb345;
                                    {
                                        uint64_t t351 = gm_l34;
                                        uint64_t t352 = (uintptr_t)t351;
                                        uint64_t t353 = GM_LD(uintptr_t, ((uint8_t *)&gm_l33) + 8);
                                        uint8_t t354 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l33)) + t352 * 1));
                                        uint8_t t355 = t354;
                                        uint8_t t358 = GM_LD(uint8_t, ((uint8_t *)&t355));
                                        gm_l35 = t358;
                                        {
                                            uint8_t t361 = 0;
                                            uint8_t t362 = 0;
                                            uint8_t t363 = 0;
                                            {
                                                uint8_t t365 = gm_l35;
                                                gm_l36 = t365;
                                            }
                                            uint8_t t366 = 0;
                                            uint8_t t367 = gm_l36;
                                            uint8_t t368 = (((uint8_t)97ULL) <= t367);
                                            if (t368) {
                                                uint8_t t369 = gm_l36;
                                                uint8_t t370 = (t369 <= ((uint8_t)122ULL));
                                                t366 = t370;
                                            } else t366 = 0;
                                            uint8_t t371 = t366;
                                            if (!t371) {
                                                {
                                                    uint8_t t373 = gm_l35;
                                                    gm_l37 = t373;
                                                }
                                                uint8_t t374 = 0;
                                                uint8_t t375 = gm_l37;
                                                uint8_t t376 = (((uint8_t)65ULL) <= t375);
                                                if (t376) {
                                                    uint8_t t377 = gm_l37;
                                                    uint8_t t378 = (t377 <= ((uint8_t)90ULL));
                                                    t374 = t378;
                                                } else t374 = 0;
                                                uint8_t t379 = t374;
                                                t363 = t379;
                                            } else t363 = 1;
                                            if (!t363) {
                                                {
                                                    uint8_t t381 = gm_l35;
                                                    gm_l38 = t381;
                                                }
                                                uint8_t t382 = 0;
                                                uint8_t t383 = gm_l38;
                                                uint8_t t384 = (((uint8_t)48ULL) <= t383);
                                                if (t384) {
                                                    uint8_t t385 = gm_l38;
                                                    uint8_t t386 = (t385 <= ((uint8_t)57ULL));
                                                    t382 = t386;
                                                } else t382 = 0;
                                                uint8_t t387 = t382;
                                                t362 = t387;
                                            } else t362 = 1;
                                            if (!t362) {
                                                uint8_t t388 = gm_l35;
                                                gm_tqs5p4n2tq19v t389;
                                                GM_ST(uint8_t, ((uint8_t *)&t389 + 0), ((uint8_t)95ULL));
                                                GM_ST(uint8_t, ((uint8_t *)&t389 + 1), ((uint8_t)45ULL));
                                                gm_tqs5p4n2tq19v t390 = t389;
                                                uint8_t t391 = 0;
                                                uint8_t t392 = t388;
                                                if (gm_eqjmccqdb90f(((uint8_t *)&t390) + 0, ((uint8_t *)&t392))) t391 = 1;
                                                if (gm_eqjmccqdb90f(((uint8_t *)&t390) + 1, ((uint8_t *)&t392))) t391 = 1;
                                                uint8_t t393 = (uint8_t)(t391);
                                                t361 = t393;
                                            } else t361 = 1;
                                            uint8_t t394 = (uint8_t)!t361;
                                            if (!t394) goto ie360;
                                            {
                                                gm_l32 = ((uint8_t)0);
                                            }
                                            goto ix360;
ie360:;
ix360:;
                                        }
                                        goto me356;
mn357:;
                                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 324, 13);
me356:;
                                    }
lc345:;
                                    {
                                        uint64_t t397 = gm_l34;
                                        uint64_t r398; bool o399 = __builtin_add_overflow(t397, ((uint64_t)1ULL), &r398);
                                        uint64_t t400 = r398;
                                        gm_l34 = t400;
                                    }
                                }
lb345:;
                            }
                        }
                        {
                            uint8_t t402 = gm_l32;
                            uint8_t t403 = (uint8_t)!t402;
                            if (!t403) goto ie401;
                            {
                                uint64_t t405 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 64));
                                gm_b16_8 t406 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                                gm_tnpifv20irnph t407;
                                GM_ST(gm_b16_8, ((uint8_t *)&t407 + 0), t406);
                                gm_tabb710d9mq2e t408;
                                gm_f_format_850ujed((void *)&t408, (void *)NULL, cx, NULL, t407);
                                if (gm_unw.kind) {
                                    goto gm_ll332;
                                }
                                gm_tabb710d9mq2e t409 = t408;
                                if (((((uint8_t *)&t409))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t409) + 8, gm_srcg0ns322676oc, 328, 39);
                                gm_b16_8 t410 = GM_LD(gm_b16_8, (((uint8_t *)&t409) + 8));
                                gm_b16_8 t411 = t410;
                                gm_b16_8 t412 = GM_LD(gm_b16_8, ((uint8_t *)&t411));
                                gm_tf1bjg81u0glv t413;
                                gm_f_mfail_uqq7o7g((void *)&t413, t405, t412);
                                gm_tjgmtdrk88t7m t414;
                                memset(&t414, 0, sizeof t414);
                                ((uint8_t *)&t414)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t414 + 8), t413);
                                gm_tjgmtdrk88t7m t415 = t414;
                                uint8_t t416 = ((((uint8_t *)&t415))[0] != 0);
                                F.ret = t414;
                                goto gm_fret;
                            }
                            goto ix401;
ie401:;
ix401:;
                        }
                        {
                            void * t417 = gm_f_as_table_4f7n31g(&(*(gm_tktr4rmbindgv *)(((uint8_t *)&F.l31) + 16)));
                            void * t418 = t417;
                            void * t419;
                            if ((GM_LD(void *, ((uint8_t *)&t418)) != 0)) {
                                void * t420 = GM_LD(void *, (((uint8_t *)&t418) + 0));
                                t419 = t420;
                            } else {
                                uint64_t t421 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 64));
                                gm_b16_8 t422 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                                gm_tnpifv20irnph t423;
                                GM_ST(gm_b16_8, ((uint8_t *)&t423 + 0), t422);
                                gm_tabb710d9mq2e t424;
                                gm_f_format_jh4rfvo((void *)&t424, (void *)NULL, cx, NULL, t423);
                                if (gm_unw.kind) {
                                    goto gm_ll332;
                                }
                                gm_tabb710d9mq2e t425 = t424;
                                if (((((uint8_t *)&t425))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t425) + 8, gm_srcg0ns322676oc, 330, 64);
                                gm_b16_8 t426 = GM_LD(gm_b16_8, (((uint8_t *)&t425) + 8));
                                gm_b16_8 t427 = t426;
                                gm_b16_8 t428 = GM_LD(gm_b16_8, ((uint8_t *)&t427));
                                gm_tf1bjg81u0glv t429;
                                gm_f_mfail_uqq7o7g((void *)&t429, t421, t428);
                                gm_tjgmtdrk88t7m t430;
                                memset(&t430, 0, sizeof t430);
                                ((uint8_t *)&t430)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t430 + 8), t429);
                                gm_tjgmtdrk88t7m t431 = t430;
                                uint8_t t432 = ((((uint8_t *)&t431))[0] != 0);
                                F.ret = t430;
                                goto gm_fret;
                            }
                            gm_l39 = t419;
                        }
                        {
                            void * t433 = gm_l39;
                            gm_b16_8 t434 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                            gm_tnpifv20irnph t435;
                            GM_ST(gm_b16_8, ((uint8_t *)&t435 + 0), t434);
                            gm_tabb710d9mq2e t436;
                            gm_f_format_1ncs69t((void *)&t436, (void *)NULL, cx, NULL, t435);
                            if (gm_unw.kind) {
                                goto gm_ll332;
                            }
                            gm_tabb710d9mq2e t437 = t436;
                            if (((((uint8_t *)&t437))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t437) + 8, gm_srcg0ns322676oc, 331, 45);
                            gm_b16_8 t438 = GM_LD(gm_b16_8, (((uint8_t *)&t437) + 8));
                            gm_b16_8 t439 = t438;
                            gm_b16_8 t440 = GM_LD(gm_b16_8, ((uint8_t *)&t439));
                            gm_tacj1cskem694 t441;
                            gm_f_parse_build_c2teosa((void *)&t441, (void *)NULL, cx, NULL, t433, t440);
                            if (gm_unw.kind) {
                                goto gm_ll332;
                            }
                            gm_tacj1cskem694 t442 = t441;
                            if (((((uint8_t *)&t442))[0] != 0)) {
                                gm_tf1bjg81u0glv t443 = GM_LD(gm_tf1bjg81u0glv, (((uint8_t *)&t442) + 8));
                                gm_tjgmtdrk88t7m t444;
                                memset(&t444, 0, sizeof t444);
                                ((uint8_t *)&t444)[0] = 1;
                                GM_ST(gm_tf1bjg81u0glv, ((uint8_t *)&t444 + 8), t443);
                                F.ret = t444;
                                goto gm_fret;
                            }
                            gm_ti0g3bmheuq9o t445 = GM_LD(gm_ti0g3bmheuq9o, (((uint8_t *)&t442) + 8));
                            F.l40 = t445;
                        }
                        {
                            void * t446 = (void *)(((uint8_t *)&F.l3) + 240);
                            gm_t6b36gvdk984b t447;
                            gm_b16_8 t448 = GM_LD(gm_b16_8, (((uint8_t *)&F.l31) + 0));
                            GM_ST(gm_b16_8, (uint8_t *)&t447 + 0, t448);
                            uint64_t t449 = GM_LD(uint64_t, (((uint8_t *)&F.l31) + 64));
                            GM_ST(uint64_t, (uint8_t *)&t447 + 16, t449);
                            memcpy((uint8_t *)&t447 + 24, ((uint8_t *)&F.l40), 152);
                            gm_tftb1odc739t5 t450 = gm_f_push_7hnmra3(t446, &t447);
                            gm_tftb1odc739t5 t451 = t450;
                            if (((((uint8_t *)&t451))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t451) + 1, gm_srcg0ns322676oc, 332, 13);
                            gm_b0_1 t452 = GM_LD(gm_b0_1, (((uint8_t *)&t451) + 1));
                        }
                        goto me337;
mn338:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 322, 9);
me337:;
                    }
lc328:;
                    {
                        uint64_t t454 = gm_l30;
                        uint64_t r455; bool o456 = __builtin_add_overflow(t454, ((uint64_t)1ULL), &r455);
                        uint64_t t457 = r455;
                        gm_l30 = t457;
                    }
                }
gm_ll332:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb328; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc328; }
                }
                goto gm_lad;
lb328:;
            }
        }
        goto ix306;
ie306:;
ix306:;
    }
    {
        gm_tm43kqe3e8b4a t458 = F.l3;
        gm_tjgmtdrk88t7m t459;
        GM_ST(uint8_t, (uint8_t *)&t459 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t459 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t459 + 4, 0);
        ((uint8_t *)&t459)[0] = 0;
        GM_ST(gm_tm43kqe3e8b4a, ((uint8_t *)&t459 + 8), t458);
        gm_tjgmtdrk88t7m t460 = t459;
        uint8_t t461 = ((((uint8_t *)&t460))[0] != 0);
        F.ret = t459;
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
__attribute__((aligned(4))) uint8_t gm_f_spec_equal_foa1umn(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_spec_equal_foa1umn F;
    gm_b16_8 gm_l4; /* x */
    gm_b16_8 gm_l5; /* y */
    gm_b16_8 gm_l6; /* x */
    gm_b16_8 gm_l7; /* y */
    gm_b16_8 gm_l8; /* x */
    gm_b16_8 gm_l9; /* y */
    GM_STACK_CHECK(&F, 503u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t2;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        switch (t10) { case ((uint8_t)0ULL): goto ma4; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma6; case ((uint8_t)3ULL): goto ma7; case ((uint8_t)4ULL): goto ma8; default: goto ma9; }
ma4:;
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t12 != ((uint8_t)0ULL)) goto mn11;
        uint8_t t13 = ((GM_LD(uint8_t, ((uint8_t *)a1) + 0) == ((uint8_t)0ULL)));
        t2 = t13;
        goto me3;
mn11:;
ma5:;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t15 != ((uint8_t)1ULL)) goto mn14;
        gm_tin97tt4ud0s1 t16 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)a0) + 8));
        F.l2 = t16;
        uint8_t t17;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t20 != ((uint8_t)1ULL)) goto mn19;
        gm_tin97tt4ud0s1 t21 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)a1) + 8));
        F.l3 = t21;
        void * t22 = (void *)((uint8_t *)&F.l2);
        void * t23 = (void *)((uint8_t *)&F.l3);
        uint8_t t24 = gm_f_req_equal_7ia8ip6(t22, t23);
        t17 = t24;
        goto me18;
mn19:;
        t17 = ((uint8_t)0);
        goto me18;
mn25:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 44, 24);
me18:;
        t2 = t17;
        goto me3;
mn14:;
ma6:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t27 != ((uint8_t)2ULL)) goto mn26;
        gm_b16_8 t28 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_l4 = t28;
        uint8_t t29;
        uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t32 != ((uint8_t)2ULL)) goto mn31;
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
        gm_l5 = t33;
        gm_b16_8 t34 = gm_l4;
        gm_b16_8 t35 = gm_l5;
        gm_b16_8 t36 = t34;
        gm_b16_8 t37 = t35;
        uint8_t t38 = (gm_eq0iqiubfm7e(((uint8_t *)&t36), ((uint8_t *)&t37)));
        t29 = t38;
        goto me30;
mn31:;
        t29 = ((uint8_t)0);
        goto me30;
mn39:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 48, 20);
me30:;
        t2 = t29;
        goto me3;
mn26:;
ma7:;
        uint8_t t41 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t41 != ((uint8_t)3ULL)) goto mn40;
        gm_b16_8 t42 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_l6 = t42;
        uint8_t t43;
        uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t46 != ((uint8_t)3ULL)) goto mn45;
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
        gm_l7 = t47;
        gm_b16_8 t48 = gm_l6;
        gm_b16_8 t49 = gm_l7;
        gm_b16_8 t50 = t48;
        gm_b16_8 t51 = t49;
        uint8_t t52 = (gm_eq0iqiubfm7e(((uint8_t *)&t50), ((uint8_t *)&t51)));
        t43 = t52;
        goto me44;
mn45:;
        t43 = ((uint8_t)0);
        goto me44;
mn53:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 52, 23);
me44:;
        t2 = t43;
        goto me3;
mn40:;
ma8:;
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t55 != ((uint8_t)4ULL)) goto mn54;
        gm_b16_8 t56 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
        gm_l8 = t56;
        uint8_t t57;
        uint8_t t60 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t60 != ((uint8_t)4ULL)) goto mn59;
        gm_b16_8 t61 = GM_LD(gm_b16_8, (((uint8_t *)a1) + 8));
        gm_l9 = t61;
        gm_b16_8 t62 = gm_l8;
        gm_b16_8 t63 = gm_l9;
        gm_b16_8 t64 = t62;
        gm_b16_8 t65 = t63;
        uint8_t t66 = (gm_eq0iqiubfm7e(((uint8_t *)&t64), ((uint8_t *)&t65)));
        t57 = t66;
        goto me58;
mn59:;
        t57 = ((uint8_t)0);
        goto me58;
mn67:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 56, 20);
me58:;
        t2 = t57;
        goto me3;
mn54:;
ma9:;
        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 42, 12);
me3:;
        uint8_t t68 = 0;
        F.ret = t2;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_spec_equal_foa1umn_c(gm_toq2f2q4opoa3 a0, gm_toq2f2q4opoa3 a1) { return gm_f_spec_equal_foa1umn(&a0, &a1); }
inline __attribute__((aligned(4))) gm_b16_8 gm_f_spec_text_9k2gjft(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_spec_text_9k2gjft F;
    gm_b16_8 gm_l2; /* t */
    gm_b16_8 gm_l3; /* b */
    gm_b16_8 gm_l4; /* r */
    GM_STACK_CHECK(&F, 726u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1;
    uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    switch (t9) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; default: goto ma8; }
ma3:;
    uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t11 != ((uint8_t)0ULL)) goto mn10;
    gm_b16_8 t12;
    GM_ST(void *, (uint8_t *)&t12, (void *)(gm_s7j1ioljmsuka));
    GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(18));
    t1 = t12;
    goto me2;
mn10:;
ma4:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t14 != ((uint8_t)1ULL)) goto mn13;
    gm_tin97tt4ud0s1 t15 = GM_LD(gm_tin97tt4ud0s1, (((uint8_t *)a0) + 8));
    F.l1 = t15;
    void * t16 = (void *)((uint8_t *)&F.l1);
    gm_b16_8 t17 = gm_f_req_string_fk059jr((void *)NULL, cx, NULL, t16);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tnpifv20irnph t18;
    GM_ST(gm_b16_8, ((uint8_t *)&t18 + 0), t17);
    gm_tabb710d9mq2e t19;
    gm_f_format_rlu0tid((void *)&t19, (void *)NULL, cx, NULL, t18);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t20 = t19;
    if (((((uint8_t *)&t20))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t20) + 8, gm_srcg0ns322676oc, 34, 20);
    gm_b16_8 t21 = GM_LD(gm_b16_8, (((uint8_t *)&t20) + 8));
    gm_b16_8 t22 = t21;
    gm_b16_8 t23 = GM_LD(gm_b16_8, ((uint8_t *)&t22));
    t1 = t23;
    goto me2;
mn13:;
ma5:;
    uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t25 != ((uint8_t)2ULL)) goto mn24;
    gm_b16_8 t26 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l2 = t26;
    gm_b16_8 t27 = gm_l2;
    gm_tnpifv20irnph t28;
    GM_ST(gm_b16_8, ((uint8_t *)&t28 + 0), t27);
    gm_tabb710d9mq2e t29;
    gm_f_format_79gt991((void *)&t29, (void *)NULL, cx, NULL, t28);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t30 = t29;
    if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 8, gm_srcg0ns322676oc, 35, 16);
    gm_b16_8 t31 = GM_LD(gm_b16_8, (((uint8_t *)&t30) + 8));
    gm_b16_8 t32 = t31;
    gm_b16_8 t33 = GM_LD(gm_b16_8, ((uint8_t *)&t32));
    t1 = t33;
    goto me2;
mn24:;
ma6:;
    uint8_t t35 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t35 != ((uint8_t)3ULL)) goto mn34;
    gm_b16_8 t36 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l3 = t36;
    gm_b16_8 t37 = gm_l3;
    gm_tnpifv20irnph t38;
    GM_ST(gm_b16_8, ((uint8_t *)&t38 + 0), t37);
    gm_tabb710d9mq2e t39;
    gm_f_format_4pbmdvl((void *)&t39, (void *)NULL, cx, NULL, t38);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t40 = t39;
    if (((((uint8_t *)&t40))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t40) + 8, gm_srcg0ns322676oc, 36, 19);
    gm_b16_8 t41 = GM_LD(gm_b16_8, (((uint8_t *)&t40) + 8));
    gm_b16_8 t42 = t41;
    gm_b16_8 t43 = GM_LD(gm_b16_8, ((uint8_t *)&t42));
    t1 = t43;
    goto me2;
mn34:;
ma7:;
    uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    if (t45 != ((uint8_t)4ULL)) goto mn44;
    gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
    gm_l4 = t46;
    gm_b16_8 t47 = gm_l4;
    gm_tnpifv20irnph t48;
    GM_ST(gm_b16_8, ((uint8_t *)&t48 + 0), t47);
    gm_tabb710d9mq2e t49;
    gm_f_format_h0dnf9n((void *)&t49, (void *)NULL, cx, NULL, t48);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tabb710d9mq2e t50 = t49;
    if (((((uint8_t *)&t50))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t50) + 8, gm_srcg0ns322676oc, 37, 16);
    gm_b16_8 t51 = GM_LD(gm_b16_8, (((uint8_t *)&t50) + 8));
    gm_b16_8 t52 = t51;
    gm_b16_8 t53 = GM_LD(gm_b16_8, ((uint8_t *)&t52));
    t1 = t53;
    goto me2;
mn44:;
ma8:;
    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 32, 58);
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
inline __attribute__((aligned(4))) void gm_f_string_array_bn13c0i(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_string_array_bn13c0i F;
    gm_b16_8 gm_l3; /* <tmp> */
    uint64_t gm_l4; /* <tmp> */
    gm_b16_8 gm_l6; /* s */
    GM_STACK_CHECK(&F, 433u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (t3 != ((uint8_t)3ULL)) goto ie2;
        gm_trqfvmjtt4sij t4 = GM_LD(gm_trqfvmjtt4sij, (((uint8_t *)a0) + 8));
        F.l1 = t4;
        {
            gm_f_list_epvducg((void *)&F.l2, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&F.l1) + 0));
                gm_l3 = t7;
            }
            {
                gm_l4 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t9 = gm_l4;
                    uint64_t t10 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                    uint8_t t11 = (t9 < t10);
                    if (!t11) goto lb8;
                    {
                        uint64_t t14 = gm_l4;
                        uint64_t t15 = (uintptr_t)t14;
                        uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l3) + 8);
                        gm_tktr4rmbindgv t19 = GM_LD(gm_tktr4rmbindgv, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l3)) + t15 * 48));
                        F.l5 = t19;
                        {
                            gm_th2lqbo77pvsc t21;
                            gm_f_as_str_982o16v((void *)&t21, &(*(gm_tktr4rmbindgv *)((uint8_t *)&F.l5)));
                            gm_th2lqbo77pvsc t22 = t21;
                            gm_b16_8 t23;
                            if (((((uint8_t *)&t22))[16] != 0)) {
                                gm_b16_8 t24 = GM_LD(gm_b16_8, (((uint8_t *)&t22) + 0));
                                t23 = t24;
                            } else {
                                gm_th2lqbo77pvsc t25;
                                memset(&t25, 0, sizeof t25);
                                uint8_t t26 = 0;
                                F.ret = t25;
                                goto gm_fret;
                            }
                            gm_l6 = t23;
                        }
                        {
                            void * t27 = (void *)((uint8_t *)&F.l2);
                            gm_b16_8 t28 = gm_l6;
                            gm_tftb1odc739t5 t29 = gm_f_push_3ef746b(t27, t28);
                            gm_tftb1odc739t5 t30 = t29;
                            if (((((uint8_t *)&t30))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t30) + 1, gm_srcg0ns322676oc, 407, 9);
                            gm_b0_1 t31 = GM_LD(gm_b0_1, (((uint8_t *)&t30) + 1));
                        }
                        goto me17;
mn18:;
                        gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 405, 5);
me17:;
                    }
lc8:;
                    {
                        uint64_t t33 = gm_l4;
                        uint64_t r34; bool o35 = __builtin_add_overflow(t33, ((uint64_t)1ULL), &r34);
                        uint64_t t36 = r34;
                        gm_l4 = t36;
                    }
                }
lb8:;
            }
        }
        {
            gm_b16_8 t37 = GM_LD(gm_b16_8, (((uint8_t *)&F.l2) + 0));
            gm_b16_8 t38 = t37;
            gm_b16_8 t39 = GM_LD(gm_b16_8, ((uint8_t *)&t38));
            gm_th2lqbo77pvsc t40;
            GM_ST(uint8_t, (uint8_t *)&t40 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t40 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t40 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t40), t39);
            ((uint8_t *)&t40)[16] = 1;
            uint8_t t41 = 0;
            F.ret = t40;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            gm_th2lqbo77pvsc t43;
            memset(&t43, 0, sizeof t43);
            uint8_t t44 = 0;
            F.ret = t43;
            goto gm_fret;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_trim_ascii_c9j36d1(gm_b16_8 a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_trim_ascii_c9j36d1 F;
    gm_b16_8 gm_l0; /* s */
    uint64_t gm_l1; /* lo */
    uint64_t gm_l2; /* hi */
    GM_STACK_CHECK(&F, 263u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_l1 = ((uint64_t)0ULL);
    }
    {
        uint64_t t2 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        gm_l2 = t2;
    }
    {
        for (;;) {
            uint64_t t4 = gm_l1;
            uint64_t t5 = gm_l2;
            uint8_t t6 = (t4 < t5);
            if (!t6) goto lb3;
            uint64_t t7 = gm_l1;
            uint64_t t8 = (uintptr_t)t7;
            uint64_t t9 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t8 >= (uintptr_t)(t9)) gm_trap_index(t8, t9, gm_srcg0ns322676oc, 251, 38);
            uint8_t t10 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t8 * 1));
            uint8_t t11 = gm_f_is_space_uen38t5(t10);
            if (!t11) goto lb3;
            {
                uint64_t t14 = gm_l1;
                uint64_t r15; bool o16 = __builtin_add_overflow(t14, ((uint64_t)1ULL), &r15);
                if (o16) gm_trap("integer overflow in `+`", gm_srcg0ns322676oc, 251, 47);
                uint64_t t17 = r15;
                gm_l1 = t17;
            }
lc3:;
        }
lb3:;
    }
    {
        for (;;) {
            uint64_t t19 = gm_l2;
            uint64_t t20 = gm_l1;
            uint8_t t21 = (t19 > t20);
            if (!t21) goto lb18;
            uint64_t t22 = gm_l2;
            uint64_t r23; bool o24 = __builtin_sub_overflow(t22, ((uint64_t)1ULL), &r23);
            if (o24) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 252, 40);
            uint64_t t25 = r23;
            uint64_t t26 = (uintptr_t)t25;
            uint64_t t27 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
            if (t26 >= (uintptr_t)(t27)) gm_trap_index(t26, t27, gm_srcg0ns322676oc, 252, 38);
            uint8_t t28 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)) + t26 * 1));
            uint8_t t29 = gm_f_is_space_uen38t5(t28);
            if (!t29) goto lb18;
            {
                uint64_t t32 = gm_l2;
                uint64_t r33; bool o34 = __builtin_sub_overflow(t32, ((uint64_t)1ULL), &r33);
                if (o34) gm_trap("integer overflow in `-`", gm_srcg0ns322676oc, 252, 51);
                uint64_t t35 = r33;
                gm_l2 = t35;
            }
lc18:;
        }
lb18:;
    }
    {
        uint64_t t36 = GM_LD(uintptr_t, ((uint8_t *)&gm_l0) + 8);
        void * t37 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l0)));
        uint64_t t38 = gm_l1;
        uint64_t t39 = (uintptr_t)t38;
        uint64_t t40 = gm_l2;
        uint64_t t41 = (uintptr_t)t40;
        if (t39 > t41 || t41 > (uintptr_t)t36) gm_trap_slice(t39, t41, t36, gm_srcg0ns322676oc, 253, 12);
        gm_b16_8 t42;
        GM_ST(void *, (uint8_t *)&t42, (void *)(((uint8_t *)t37 + t39 * 1)));
        GM_ST(uintptr_t, (uint8_t *)&t42 + 8, (uintptr_t)(t41 - t39));
        uint8_t t43 = 0;
        F.ret = t42;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_url_problem_38kdh3a(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0) {
    struct gm_fr_url_problem_38kdh3a F;
    gm_b16_8 gm_l0; /* url */
    gm_b16_8 gm_l1; /* <tmp> */
    uint64_t gm_l2; /* <tmp> */
    uint8_t gm_l3; /* c */
    GM_STACK_CHECK(&F, 532u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        gm_b16_8 t3 = gm_l0;
        gm_b16_8 t4;
        GM_ST(void *, (uint8_t *)&t4, (void *)(gm_skolmnfc0ccbd));
        GM_ST(uintptr_t, (uint8_t *)&t4 + 8, (uintptr_t)(1));
        uint8_t t5 = gm_f_starts_with_6vvt01e(t3, t4);
        if (!t5) goto ie2;
        {
            gm_b16_8 t7 = gm_l0;
            gm_tnpifv20irnph t8;
            GM_ST(gm_b16_8, ((uint8_t *)&t8 + 0), t7);
            gm_tabb710d9mq2e t9;
            gm_f_format_ivhanlp((void *)&t9, (void *)NULL, cx, NULL, t8);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tabb710d9mq2e t10 = t9;
            if (((((uint8_t *)&t10))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t10) + 8, gm_srcg0ns322676oc, 227, 38);
            gm_b16_8 t11 = GM_LD(gm_b16_8, (((uint8_t *)&t10) + 8));
            gm_b16_8 t12 = t11;
            gm_b16_8 t13 = GM_LD(gm_b16_8, ((uint8_t *)&t12));
            gm_th2lqbo77pvsc t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t14), t13);
            ((uint8_t *)&t14)[16] = 1;
            uint8_t t15 = 0;
            F.ret = t14;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_b16_8 t17 = gm_l0;
            gm_l1 = t17;
        }
        {
            gm_l2 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t19 = gm_l2;
                uint64_t t20 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                uint8_t t21 = (t19 < t20);
                if (!t21) goto lb18;
                {
                    uint64_t t24 = gm_l2;
                    uint64_t t25 = (uintptr_t)t24;
                    uint64_t t26 = GM_LD(uintptr_t, ((uint8_t *)&gm_l1) + 8);
                    uint8_t t27 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l1)) + t25 * 1));
                    uint8_t t28 = t27;
                    uint8_t t31 = GM_LD(uint8_t, ((uint8_t *)&t28));
                    gm_l3 = t31;
                    {
                        uint8_t t34 = gm_l3;
                        uint8_t t35 = gm_f_is_space_or_control_3uo6i3o(t34);
                        if (!t35) goto ie33;
                        {
                            gm_t6j6smj7qoctu t37;
                            gm_b16_8 t38 = gm_l0;
                            gm_b16_8 t39 = gm_f_debug_escape_t8t1h0j((void *)NULL, cx, NULL, t38);
                            if (gm_unw.kind) {
                                goto gm_ll22;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t37 + 0, t39);
                            uint8_t t40 = gm_l3;
                            gm_b16_8 t41 = gm_f_debug_char_p6i205h((void *)NULL, cx, NULL, t40);
                            if (gm_unw.kind) {
                                goto gm_ll22;
                            }
                            GM_ST(gm_b16_8, (uint8_t *)&t37 + 16, t41);
                            gm_tabb710d9mq2e t42;
                            gm_f_format_uqgrksh((void *)&t42, (void *)NULL, cx, NULL, &t37);
                            if (gm_unw.kind) {
                                goto gm_ll22;
                            }
                            gm_tabb710d9mq2e t43 = t42;
                            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcg0ns322676oc, 230, 20);
                            gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
                            gm_b16_8 t45 = t44;
                            gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                            gm_th2lqbo77pvsc t47;
                            GM_ST(uint8_t, (uint8_t *)&t47 + 17, 0);
                            GM_ST(uint16_t, (uint8_t *)&t47 + 18, 0);
                            GM_ST(uint32_t, (uint8_t *)&t47 + 20, 0);
                            GM_ST(gm_b16_8, ((uint8_t *)&t47), t46);
                            ((uint8_t *)&t47)[16] = 1;
                            uint8_t t48 = 0;
                            F.ret = t47;
                            goto gm_fret;
                        }
                        goto ix33;
ie33:;
ix33:;
                    }
                    goto me29;
mn30:;
                    gm_trap("no match arm matched the scrutinee", gm_srcg0ns322676oc, 228, 5);
me29:;
                }
lc18:;
                {
                    uint64_t t50 = gm_l2;
                    uint64_t r51; bool o52 = __builtin_add_overflow(t50, ((uint64_t)1ULL), &r51);
                    uint64_t t53 = r51;
                    gm_l2 = t53;
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
    {
        gm_th2lqbo77pvsc t54;
        memset(&t54, 0, sizeof t54);
        uint8_t t55 = 0;
        F.ret = t54;
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

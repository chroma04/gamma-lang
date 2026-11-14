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
typedef struct gm_cseahvg8v7p0vs { uint32_t f0; uint32_t f1; uint32_t f2; } gm_cseahvg8v7p0vs;
typedef struct gm_csguankko4lmhd { uint32_t f0; } gm_csguankko4lmhd;
typedef struct { uint64_t f0; uint64_t f1; } gm_t2lqu8jsj511d;
typedef struct { void * f0; void * f1; } gm_t3mf58gv9kf3n;
typedef struct { uint32_t f0; } gm_t88rcucg1b7uk;
typedef struct { uint32_t f0; uint8_t f1; uint8_t pe[3]; } gm_t9805qtonbsag;
typedef struct { unsigned __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_t9p38b040grms;
typedef struct { uint8_t f0; } gm_t9qkh0rsooc8d;
typedef struct { __int128 f0; uint8_t f1; uint8_t pe[15]; } gm_ta24rd13gupia;
typedef struct { uint8_t f0; uint8_t p1[7]; void * f1; void * f2; } gm_ta612fil812pm;
typedef struct { void * f0; void * f1; void * f2; uint8_t f3; uint8_t pe[7]; } gm_tao63pk3r18v5;
typedef struct { void * f0; uint32_t f1; uint8_t pe[4]; } gm_tbl8il92l97ut;
typedef struct { uint32_t f0; uint32_t f1; } gm_tcc1rq4n1avb2;
typedef struct { uint64_t f0; uint8_t f1; uint8_t pe[7]; } gm_tf5srh4mp4b3i;
typedef struct { uint8_t f0; uint8_t p1[1]; uint16_t f1; } gm_tjcj4j2gbh6c8;
typedef struct { uint32_t f0; uint32_t f1; uint32_t f2; } gm_tl874t2atnlh3;
typedef struct { void * f0; void * f1; uint8_t f2; uint8_t pe[7]; } gm_tlcl4u558l6p5;
typedef struct { void * f0; void * f1; uint32_t f2; uint32_t f3; } gm_tmm67r2c94d5j;
typedef struct { uint8_t f0; uint8_t p1[3]; union { uint32_t v0; } f1; } gm_tmn7n0n9raou5;
typedef struct { uint8_t f0; uint8_t f1; uint8_t f2; } gm_toh0ootodhjue;
typedef struct { void * f0; uint8_t f1; uint8_t pe[7]; } gm_tsplfr33dsvji;
typedef struct { uint32_t f0; uint8_t p1[4]; void * f1; } gm_ttggq7ng4kp3p;
typedef struct { uint8_t f0; uint8_t p1[3]; uint32_t f1; } gm_ttovhfee8mf28;
_Static_assert(sizeof(gm_cseahvg8v7p0vs) == 12 && _Alignof(gm_cseahvg8v7p0vs) == 4 && offsetof(gm_cseahvg8v7p0vs, f0) == 0 && offsetof(gm_cseahvg8v7p0vs, f1) == 4 && offsetof(gm_cseahvg8v7p0vs, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_csguankko4lmhd) == 4 && _Alignof(gm_csguankko4lmhd) == 4 && offsetof(gm_csguankko4lmhd, f0) == 0, "layout of `Mutex`");
typedef struct { void * f0; gm_t9805qtonbsag f1; uint32_t f2; uint8_t pe[4]; } gm_t083kd7b1kkag;
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; uint8_t pe[4]; } gm_t08olq97rscn3;
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; gm_t9qkh0rsooc8d f2; uint8_t p3[3]; gm_tf5srh4mp4b3i f3; } gm_t0mglksrkfa5e;
_Static_assert(sizeof(gm_t2lqu8jsj511d) == 16 && _Alignof(gm_t2lqu8jsj511d) == 8 && offsetof(gm_t2lqu8jsj511d, f0) == 0 && offsetof(gm_t2lqu8jsj511d, f1) == 8, "layout of `(u64, u64)`");
typedef struct { gm_b16_8 f0; uint64_t f1; } gm_t3877pk7lv748;
typedef struct { void * f0; gm_t9805qtonbsag f1; void * f2; } gm_t3dcoc7077t1q;
_Static_assert(sizeof(gm_t3mf58gv9kf3n) == 16 && _Alignof(gm_t3mf58gv9kf3n) == 8 && offsetof(gm_t3mf58gv9kf3n, f0) == 0 && offsetof(gm_t3mf58gv9kf3n, f1) == 8, "layout of `Allocator`");
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; uint8_t f2; uint8_t pe[3]; } gm_t3rcjibl8kkg4;
typedef struct { gm_b16_8 f0; gm_b16_8 f1; } gm_t6j6smj7qoctu;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; void * f2; void * f3; } gm_t710lsrq1m8gk;
typedef struct { __int128 f0; gm_b16_8 f1; } gm_t7a4aqr549lb5;
_Static_assert(sizeof(gm_t88rcucg1b7uk) == 4 && _Alignof(gm_t88rcucg1b7uk) == 4 && offsetof(gm_t88rcucg1b7uk, f0) == 0, "layout of `(u32,)`");
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[7]; void * f1; } gm_t8ghgl2jj0qha;
typedef struct { gm_tmn7n0n9raou5 f0; void * f1; } gm_t8jsc4n9vp2ib;
_Static_assert(sizeof(gm_t9805qtonbsag) == 8 && _Alignof(gm_t9805qtonbsag) == 4 && offsetof(gm_t9805qtonbsag, f0) == 0 && offsetof(gm_t9805qtonbsag, f1) == 4, "layout of `?u32`");
_Static_assert(sizeof(gm_t9p38b040grms) == 32 && _Alignof(gm_t9p38b040grms) == 16 && offsetof(gm_t9p38b040grms, f0) == 0 && offsetof(gm_t9p38b040grms, f1) == 16, "layout of `?u128`");
_Static_assert(sizeof(gm_t9qkh0rsooc8d) == 1 && _Alignof(gm_t9qkh0rsooc8d) == 1 && offsetof(gm_t9qkh0rsooc8d, f0) == 0, "layout of `SeekFrom`");
_Static_assert(sizeof(gm_ta24rd13gupia) == 32 && _Alignof(gm_ta24rd13gupia) == 16 && offsetof(gm_ta24rd13gupia, f0) == 0 && offsetof(gm_ta24rd13gupia, f1) == 16, "layout of `?i128`");
_Static_assert(sizeof(gm_ta612fil812pm) == 24 && _Alignof(gm_ta612fil812pm) == 8 && offsetof(gm_ta612fil812pm, f0) == 0 && offsetof(gm_ta612fil812pm, f1) == 8 && offsetof(gm_ta612fil812pm, f2) == 16, "layout of `(is_and: bool, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; gm_b16_8 v1; } f1; } gm_tabb710d9mq2e;
_Static_assert(sizeof(gm_tao63pk3r18v5) == 32 && _Alignof(gm_tao63pk3r18v5) == 8 && offsetof(gm_tao63pk3r18v5, f0) == 0 && offsetof(gm_tao63pk3r18v5, f1) == 8 && offsetof(gm_tao63pk3r18v5, f2) == 16 && offsetof(gm_tao63pk3r18v5, f3) == 24, "layout of `SliceExpr`");
typedef struct { gm_b16_8 f0; uint32_t f1; uint32_t f2; gm_b16_8 f3; } gm_tb40qn7hk0tl9;
typedef struct { uint64_t f0; uint64_t f1; gm_b16_8 f2; } gm_tbccvikg6b96e;
typedef struct { gm_tl874t2atnlh3 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tbgd14esf0jvi;
_Static_assert(sizeof(gm_tbl8il92l97ut) == 16 && _Alignof(gm_tbl8il92l97ut) == 8 && offsetof(gm_tbl8il92l97ut, f0) == 0 && offsetof(gm_tbl8il92l97ut, f1) == 8, "layout of `SpecCall`");
typedef struct { uint8_t f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; } gm_tbngattl1fo51;
_Static_assert(sizeof(gm_tcc1rq4n1avb2) == 8 && _Alignof(gm_tcc1rq4n1avb2) == 4 && offsetof(gm_tcc1rq4n1avb2, f0) == 0 && offsetof(gm_tcc1rq4n1avb2, f1) == 4, "layout of `LineCol`");
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tcmkompbl09il;
typedef struct { gm_t9qkh0rsooc8d f0; uint8_t p1[3]; gm_t88rcucg1b7uk f1; gm_t88rcucg1b7uk f2; } gm_tcmtrmhj7ak4u;
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_tcc1rq4n1avb2 v0; } f1; } gm_te9hrk0u35rev;
typedef struct { uint8_t f0; union { gm_toh0ootodhjue v0; } f1; } gm_tesaltf0mhcki;
_Static_assert(sizeof(gm_tf5srh4mp4b3i) == 16 && _Alignof(gm_tf5srh4mp4b3i) == 8 && offsetof(gm_tf5srh4mp4b3i, f0) == 0 && offsetof(gm_tf5srh4mp4b3i, f1) == 8, "layout of `?usize`");
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; } f1; } gm_tftb1odc739t5;
typedef struct { gm_t88rcucg1b7uk f0; gm_t88rcucg1b7uk f1; } gm_tgglga8ro3isc;
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t pe[7]; } gm_th2lqbo77pvsc;
typedef struct { uint32_t f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tiilmfd16v3fc;
_Static_assert(sizeof(gm_tjcj4j2gbh6c8) == 4 && _Alignof(gm_tjcj4j2gbh6c8) == 2 && offsetof(gm_tjcj4j2gbh6c8, f0) == 0 && offsetof(gm_tjcj4j2gbh6c8, f1) == 2, "layout of `(signed: bool, bits: u16)`");
typedef struct { void * f0; gm_b16_8 f1; } gm_tkaapcii86udk;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t f1; uint8_t pe[3]; } gm_tkn42u7na3t81;
_Static_assert(sizeof(gm_tl874t2atnlh3) == 12 && _Alignof(gm_tl874t2atnlh3) == 4 && offsetof(gm_tl874t2atnlh3, f0) == 0 && offsetof(gm_tl874t2atnlh3, f1) == 4 && offsetof(gm_tl874t2atnlh3, f2) == 8, "layout of `Span`");
_Static_assert(sizeof(gm_tlcl4u558l6p5) == 24 && _Alignof(gm_tlcl4u558l6p5) == 8 && offsetof(gm_tlcl4u558l6p5, f0) == 0 && offsetof(gm_tlcl4u558l6p5, f1) == 8 && offsetof(gm_tlcl4u558l6p5, f2) == 16, "layout of `(lo: ^Expr, hi: ^Expr, inclusive: bool)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; uint8_t pe[4]; } gm_tlcm4lvu3g1a8;
_Static_assert(sizeof(gm_tmm67r2c94d5j) == 24 && _Alignof(gm_tmm67r2c94d5j) == 8 && offsetof(gm_tmm67r2c94d5j, f0) == 0 && offsetof(gm_tmm67r2c94d5j, f1) == 8 && offsetof(gm_tmm67r2c94d5j, f2) == 16 && offsetof(gm_tmm67r2c94d5j, f3) == 20, "layout of `Printer`");
_Static_assert(sizeof(gm_tmn7n0n9raou5) == 8 && _Alignof(gm_tmn7n0n9raou5) == 4 && offsetof(gm_tmn7n0n9raou5, f0) == 0 && offsetof(gm_tmn7n0n9raou5, f1) == 4, "layout of `Coercion`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t88rcucg1b7uk v0; } f1; } gm_tnnjeerolos0a;
typedef struct { gm_b16_8 f0; } gm_tnpifv20irnph;
_Static_assert(sizeof(gm_toh0ootodhjue) == 3 && _Alignof(gm_toh0ootodhjue) == 1 && offsetof(gm_toh0ootodhjue, f0) == 0 && offsetof(gm_toh0ootodhjue, f1) == 1 && offsetof(gm_toh0ootodhjue, f2) == 2, "layout of `Restr`");
typedef struct { gm_b16_8 f0; unsigned __int128 f1; } gm_tou0girmhri5g;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_t9qkh0rsooc8d v0; void * v1; } f1; } gm_tq7s98ihonkju;
typedef struct { gm_b16_8 f0; void * f1; void * f2; } gm_trl2khmuphncd;
typedef struct { gm_tl874t2atnlh3 f0; gm_t88rcucg1b7uk f1; } gm_trlabqsjjp0ds;
typedef struct { gm_b16_8 f0; uint64_t f1; gm_t3mf58gv9kf3n f2; } gm_trqfvmjtt4sij;
typedef struct { uint8_t f0; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_t9qkh0rsooc8d v3; } f1; } gm_ts6oqppjh0ba3;
_Static_assert(sizeof(gm_tsplfr33dsvji) == 16 && _Alignof(gm_tsplfr33dsvji) == 8 && offsetof(gm_tsplfr33dsvji, f0) == 0 && offsetof(gm_tsplfr33dsvji, f1) == 8, "layout of `(expr: ^Expr, is_result: bool)`");
_Static_assert(sizeof(gm_ttggq7ng4kp3p) == 16 && _Alignof(gm_ttggq7ng4kp3p) == 8 && offsetof(gm_ttggq7ng4kp3p, f0) == 0 && offsetof(gm_ttggq7ng4kp3p, f1) == 8, "layout of `(index: u32, payload: ?^Value)`");
_Static_assert(sizeof(gm_ttovhfee8mf28) == 8 && _Alignof(gm_ttovhfee8mf28) == 4 && offsetof(gm_ttovhfee8mf28, f0) == 0 && offsetof(gm_ttovhfee8mf28, f1) == 4, "layout of `(signed: bool, bits: u32)`");
typedef struct { uint8_t f0; uint8_t p1[3]; union { gm_t9qkh0rsooc8d v0; gm_t9qkh0rsooc8d v1; gm_t9qkh0rsooc8d v2; gm_tcc1rq4n1avb2 v3; uint32_t v4; uint32_t v5; } f1; } gm_tvdg94bk7aa22;
_Static_assert(sizeof(gm_t083kd7b1kkag) == 24 && _Alignof(gm_t083kd7b1kkag) == 8 && offsetof(gm_t083kd7b1kkag, f0) == 0 && offsetof(gm_t083kd7b1kkag, f1) == 8 && offsetof(gm_t083kd7b1kkag, f2) == 16, "layout of `TryExpr`");
_Static_assert(sizeof(gm_t08olq97rscn3) == 32 && _Alignof(gm_t08olq97rscn3) == 8 && offsetof(gm_t08olq97rscn3, f0) == 0 && offsetof(gm_t08olq97rscn3, f1) == 16, "layout of `File`");
_Static_assert(sizeof(gm_t0mglksrkfa5e) == 40 && _Alignof(gm_t0mglksrkfa5e) == 8 && offsetof(gm_t0mglksrkfa5e, f0) == 0 && offsetof(gm_t0mglksrkfa5e, f1) == 16 && offsetof(gm_t0mglksrkfa5e, f2) == 20 && offsetof(gm_t0mglksrkfa5e, f3) == 24, "layout of `FieldDef`");
_Static_assert(sizeof(gm_t3877pk7lv748) == 24 && _Alignof(gm_t3877pk7lv748) == 8 && offsetof(gm_t3877pk7lv748, f0) == 0 && offsetof(gm_t3877pk7lv748, f1) == 16, "layout of `WasmReader`");
_Static_assert(sizeof(gm_t3dcoc7077t1q) == 24 && _Alignof(gm_t3dcoc7077t1q) == 8 && offsetof(gm_t3dcoc7077t1q, f0) == 0 && offsetof(gm_t3dcoc7077t1q, f1) == 8 && offsetof(gm_t3dcoc7077t1q, f2) == 16, "layout of `ElseExpr`");
_Static_assert(sizeof(gm_t3rcjibl8kkg4) == 12 && _Alignof(gm_t3rcjibl8kkg4) == 4 && offsetof(gm_t3rcjibl8kkg4, f0) == 0 && offsetof(gm_t3rcjibl8kkg4, f1) == 4 && offsetof(gm_t3rcjibl8kkg4, f2) == 8, "layout of `(a: Ty, b: Ty, negated: bool)`");
typedef struct { gm_tiilmfd16v3fc f0; uint32_t f1; uint8_t pe[4]; } gm_t419mo45g7j0a;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tcmkompbl09il v0; gm_tiilmfd16v3fc v1; gm_b16_8 v2; } f1; } gm_t428ue2u3hf5g;
_Static_assert(sizeof(gm_t6j6smj7qoctu) == 32 && _Alignof(gm_t6j6smj7qoctu) == 8 && offsetof(gm_t6j6smj7qoctu, f0) == 0 && offsetof(gm_t6j6smj7qoctu, f1) == 16, "layout of `(string, string)`");
_Static_assert(sizeof(gm_t710lsrq1m8gk) == 40 && _Alignof(gm_t710lsrq1m8gk) == 8 && offsetof(gm_t710lsrq1m8gk, f0) == 0 && offsetof(gm_t710lsrq1m8gk, f1) == 8 && offsetof(gm_t710lsrq1m8gk, f2) == 24 && offsetof(gm_t710lsrq1m8gk, f3) == 32, "layout of `WithExpr`");
_Static_assert(sizeof(gm_t7a4aqr549lb5) == 32 && _Alignof(gm_t7a4aqr549lb5) == 16 && offsetof(gm_t7a4aqr549lb5, f0) == 0 && offsetof(gm_t7a4aqr549lb5, f1) == 16, "layout of `(i128, string)`");
_Static_assert(sizeof(gm_t8ghgl2jj0qha) == 16 && _Alignof(gm_t8ghgl2jj0qha) == 8 && offsetof(gm_t8ghgl2jj0qha, f0) == 0 && offsetof(gm_t8ghgl2jj0qha, f1) == 8, "layout of `(op: UnOp, operand: ^Expr)`");
_Static_assert(sizeof(gm_t8jsc4n9vp2ib) == 16 && _Alignof(gm_t8jsc4n9vp2ib) == 8 && offsetof(gm_t8jsc4n9vp2ib, f0) == 0 && offsetof(gm_t8jsc4n9vp2ib, f1) == 8, "layout of `(kind: Coercion, expr: ^Expr)`");
typedef struct { uint64_t f0; gm_t88rcucg1b7uk f1; gm_tnnjeerolos0a f2; uint8_t pe[4]; } gm_t8nckeqfenl8a;
typedef struct { gm_tkaapcii86udk f0; uint8_t f1; uint8_t pe[7]; } gm_tab260m0n3sqj;
_Static_assert(sizeof(gm_tabb710d9mq2e) == 24 && _Alignof(gm_tabb710d9mq2e) == 8 && offsetof(gm_tabb710d9mq2e, f0) == 0 && offsetof(gm_tabb710d9mq2e, f1) == 8, "layout of `AllocError![]u8`");
typedef struct { gm_tesaltf0mhcki f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tandvbt63uk7n;
_Static_assert(sizeof(gm_tb40qn7hk0tl9) == 40 && _Alignof(gm_tb40qn7hk0tl9) == 8 && offsetof(gm_tb40qn7hk0tl9, f0) == 0 && offsetof(gm_tb40qn7hk0tl9, f1) == 16 && offsetof(gm_tb40qn7hk0tl9, f2) == 20 && offsetof(gm_tb40qn7hk0tl9, f3) == 24, "layout of `SourceLocation`");
typedef struct { gm_tlcm4lvu3g1a8 f0; uint8_t f1; uint8_t pe[7]; } gm_tb7u19loo3j87;
_Static_assert(sizeof(gm_tbccvikg6b96e) == 32 && _Alignof(gm_tbccvikg6b96e) == 8 && offsetof(gm_tbccvikg6b96e, f0) == 0 && offsetof(gm_tbccvikg6b96e, f1) == 8 && offsetof(gm_tbccvikg6b96e, f2) == 16, "layout of `Layout`");
typedef struct { uint8_t f0; uint8_t p1[7]; gm_trqfvmjtt4sij f1; } gm_tbci5ospk5nnn;
_Static_assert(sizeof(gm_tbgd14esf0jvi) == 32 && _Alignof(gm_tbgd14esf0jvi) == 8 && offsetof(gm_tbgd14esf0jvi, f0) == 0 && offsetof(gm_tbgd14esf0jvi, f1) == 16, "layout of `Edit`");
_Static_assert(sizeof(gm_tbngattl1fo51) == 8 && _Alignof(gm_tbngattl1fo51) == 4 && offsetof(gm_tbngattl1fo51, f0) == 0 && offsetof(gm_tbngattl1fo51, f1) == 4, "layout of `(is_const: bool, pointee: Ty)`");
_Static_assert(sizeof(gm_tcmkompbl09il) == 24 && _Alignof(gm_tcmkompbl09il) == 8 && offsetof(gm_tcmkompbl09il, f0) == 0 && offsetof(gm_tcmkompbl09il, f1) == 8, "layout of `(ty: Ty, bytes: string)`");
_Static_assert(sizeof(gm_tcmtrmhj7ak4u) == 12 && _Alignof(gm_tcmtrmhj7ak4u) == 4 && offsetof(gm_tcmtrmhj7ak4u, f0) == 0 && offsetof(gm_tcmtrmhj7ak4u, f1) == 4 && offsetof(gm_tcmtrmhj7ak4u, f2) == 8, "layout of `(op: LenOp, lhs: Ty, rhs: Ty)`");
typedef struct { gm_b16_8 f0; gm_t88rcucg1b7uk f1; uint8_t p2[4]; gm_tnpifv20irnph f2; uint8_t f3; uint8_t pe[7]; } gm_tddtjfggdat0d;
_Static_assert(sizeof(gm_te9hrk0u35rev) == 12 && _Alignof(gm_te9hrk0u35rev) == 4 && offsetof(gm_te9hrk0u35rev, f0) == 0 && offsetof(gm_te9hrk0u35rev, f1) == 4, "layout of `Target`");
typedef struct { gm_te9hrk0u35rev f0; uint8_t p1[4]; void * f1; } gm_teo9d4mncjfdu;
_Static_assert(sizeof(gm_tesaltf0mhcki) == 4 && _Alignof(gm_tesaltf0mhcki) == 1 && offsetof(gm_tesaltf0mhcki, f0) == 0 && offsetof(gm_tesaltf0mhcki, f1) == 1, "layout of `SoaOp`");
_Static_assert(sizeof(gm_tftb1odc739t5) == 2 && _Alignof(gm_tftb1odc739t5) == 1 && offsetof(gm_tftb1odc739t5, f0) == 0 && offsetof(gm_tftb1odc739t5, f1) == 1, "layout of `AllocError!()`");
_Static_assert(sizeof(gm_tgglga8ro3isc) == 8 && _Alignof(gm_tgglga8ro3isc) == 4 && offsetof(gm_tgglga8ro3isc, f0) == 0 && offsetof(gm_tgglga8ro3isc, f1) == 4, "layout of `(err: Ty, ok: Ty)`");
_Static_assert(sizeof(gm_th2lqbo77pvsc) == 24 && _Alignof(gm_th2lqbo77pvsc) == 8 && offsetof(gm_th2lqbo77pvsc, f0) == 0 && offsetof(gm_th2lqbo77pvsc, f1) == 16, "layout of `?cont(())`");
typedef struct { gm_tvdg94bk7aa22 f0; } gm_th9nl3oso747e;
_Static_assert(sizeof(gm_tiilmfd16v3fc) == 24 && _Alignof(gm_tiilmfd16v3fc) == 8 && offsetof(gm_tiilmfd16v3fc, f0) == 0 && offsetof(gm_tiilmfd16v3fc, f1) == 8, "layout of `EffectRef`");
typedef struct { gm_b16_8 f0; gm_tl874t2atnlh3 f1; gm_t88rcucg1b7uk f2; uint64_t f3; void * f4; gm_tkn42u7na3t81 f5; uint64_t f6; void * f7; gm_t9qkh0rsooc8d f8; uint8_t p9[7]; void * f9; } gm_tjcptv9sagfq8;
_Static_assert(sizeof(gm_tkaapcii86udk) == 24 && _Alignof(gm_tkaapcii86udk) == 8 && offsetof(gm_tkaapcii86udk, f0) == 0 && offsetof(gm_tkaapcii86udk, f1) == 8, "layout of `Place`");
_Static_assert(sizeof(gm_tkn42u7na3t81) == 8 && _Alignof(gm_tkn42u7na3t81) == 4 && offsetof(gm_tkn42u7na3t81, f0) == 0 && offsetof(gm_tkn42u7na3t81, f1) == 4, "layout of `?Ty`");
_Static_assert(sizeof(gm_tlcm4lvu3g1a8) == 16 && _Alignof(gm_tlcm4lvu3g1a8) == 8 && offsetof(gm_tlcm4lvu3g1a8, f0) == 0 && offsetof(gm_tlcm4lvu3g1a8, f1) == 8, "layout of `(i: usize, x: Ty)`");
_Static_assert(sizeof(gm_tnnjeerolos0a) == 8 && _Alignof(gm_tnnjeerolos0a) == 4 && offsetof(gm_tnnjeerolos0a, f0) == 0 && offsetof(gm_tnnjeerolos0a, f1) == 4, "layout of `ArrayKind`");
_Static_assert(sizeof(gm_tnpifv20irnph) == 16 && _Alignof(gm_tnpifv20irnph) == 8 && offsetof(gm_tnpifv20irnph, f0) == 0, "layout of `(string,)`");
typedef struct { gm_tkaapcii86udk f0; uint64_t f1; } gm_to6glrduves6o;
_Static_assert(sizeof(gm_tou0girmhri5g) == 32 && _Alignof(gm_tou0girmhri5g) == 16 && offsetof(gm_tou0girmhri5g, f0) == 0 && offsetof(gm_tou0girmhri5g, f1) == 16, "layout of `([]u8, u128)`");
typedef struct { gm_b16_8 f0; uint8_t f1; uint8_t p2[7]; gm_tf5srh4mp4b3i f2; gm_tkn42u7na3t81 f3; } gm_tpautr4br7h3l;
_Static_assert(sizeof(gm_tq7s98ihonkju) == 16 && _Alignof(gm_tq7s98ihonkju) == 8 && offsetof(gm_tq7s98ihonkju, f0) == 0 && offsetof(gm_tq7s98ihonkju, f1) == 8, "layout of `AllocError!^HandlerDecl`");
_Static_assert(sizeof(gm_trl2khmuphncd) == 32 && _Alignof(gm_trl2khmuphncd) == 8 && offsetof(gm_trl2khmuphncd, f0) == 0 && offsetof(gm_trl2khmuphncd, f1) == 16 && offsetof(gm_trl2khmuphncd, f2) == 24, "layout of `IfExpr`");
typedef struct { gm_ts6oqppjh0ba3 f0; uint8_t p1[6]; void * f1; void * f2; } gm_trl9d7m1k5lbd;
_Static_assert(sizeof(gm_trlabqsjjp0ds) == 16 && _Alignof(gm_trlabqsjjp0ds) == 4 && offsetof(gm_trlabqsjjp0ds, f0) == 0 && offsetof(gm_trlabqsjjp0ds, f1) == 12, "layout of `(Span, Ty)`");
_Static_assert(sizeof(gm_trqfvmjtt4sij) == 40 && _Alignof(gm_trqfvmjtt4sij) == 8 && offsetof(gm_trqfvmjtt4sij, f0) == 0 && offsetof(gm_trqfvmjtt4sij, f1) == 16 && offsetof(gm_trqfvmjtt4sij, f2) == 24, "layout of `List(Sleeper)`");
_Static_assert(sizeof(gm_ts6oqppjh0ba3) == 2 && _Alignof(gm_ts6oqppjh0ba3) == 1 && offsetof(gm_ts6oqppjh0ba3, f0) == 0 && offsetof(gm_ts6oqppjh0ba3, f1) == 1, "layout of `BinOp`");
typedef struct { gm_tvdg94bk7aa22 f0; uint8_t p1[4]; gm_b16_8 f1; } gm_tt8o5kebou62r;
typedef struct { gm_t6j6smj7qoctu f0; uint8_t f1; uint8_t pe[7]; } gm_ttu09lij36f8c;
_Static_assert(sizeof(gm_tvdg94bk7aa22) == 12 && _Alignof(gm_tvdg94bk7aa22) == 4 && offsetof(gm_tvdg94bk7aa22, f0) == 0 && offsetof(gm_tvdg94bk7aa22, f1) == 4, "layout of `Builtin`");
_Static_assert(sizeof(gm_t419mo45g7j0a) == 32 && _Alignof(gm_t419mo45g7j0a) == 8 && offsetof(gm_t419mo45g7j0a, f0) == 0 && offsetof(gm_t419mo45g7j0a, f1) == 24, "layout of `(effect_: EffectRef, op: u32)`");
_Static_assert(sizeof(gm_t428ue2u3hf5g) == 32 && _Alignof(gm_t428ue2u3hf5g) == 8 && offsetof(gm_t428ue2u3hf5g, f0) == 0 && offsetof(gm_t428ue2u3hf5g, f1) == 8, "layout of `ConstKey`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tjcj4j2gbh6c8 v0; uint16_t v1; gm_tbngattl1fo51 v2; gm_tbngattl1fo51 v3; gm_tbngattl1fo51 v4; gm_t8nckeqfenl8a v5; gm_t6j6smj7qoctu v6; gm_t88rcucg1b7uk v7; gm_tgglga8ro3isc v8; gm_tddtjfggdat0d v9; gm_tddtjfggdat0d v10; gm_tiilmfd16v3fc v11; gm_t88rcucg1b7uk v12; uint32_t v13; gm_tgglga8ro3isc v14; gm_t428ue2u3hf5g v15; gm_tcmtrmhj7ak4u v16; uint32_t v17; gm_tnpifv20irnph v18; uint32_t v19; } f1; } gm_t53at80pfdus3;
typedef struct { uint8_t f0; uint8_t p1[15]; union { unsigned __int128 v0; double v1; uint8_t v2; void * v3; gm_tkaapcii86udk v4; gm_to6glrduves6o v5; void * v6; void * v7; void * v8; gm_ttggq7ng4kp3p v9; uint32_t v10; gm_ttggq7ng4kp3p v11; gm_ttggq7ng4kp3p v12; } f1; } gm_t6evimt84lndq;
_Static_assert(sizeof(gm_t8nckeqfenl8a) == 24 && _Alignof(gm_t8nckeqfenl8a) == 8 && offsetof(gm_t8nckeqfenl8a, f0) == 0 && offsetof(gm_t8nckeqfenl8a, f1) == 8 && offsetof(gm_t8nckeqfenl8a, f2) == 12, "layout of `(len: u64, elem: Ty, kind: ArrayKind)`");
_Static_assert(sizeof(gm_tab260m0n3sqj) == 32 && _Alignof(gm_tab260m0n3sqj) == 8 && offsetof(gm_tab260m0n3sqj, f0) == 0 && offsetof(gm_tab260m0n3sqj, f1) == 24, "layout of `?Place`");
_Static_assert(sizeof(gm_tandvbt63uk7n) == 24 && _Alignof(gm_tandvbt63uk7n) == 8 && offsetof(gm_tandvbt63uk7n, f0) == 0 && offsetof(gm_tandvbt63uk7n, f1) == 8, "layout of `(op: SoaOp, args: []Expr)`");
_Static_assert(sizeof(gm_tb7u19loo3j87) == 24 && _Alignof(gm_tb7u19loo3j87) == 8 && offsetof(gm_tb7u19loo3j87, f0) == 0 && offsetof(gm_tb7u19loo3j87, f1) == 16, "layout of `?(len: u64, elem: Ty)`");
_Static_assert(sizeof(gm_tbci5ospk5nnn) == 48 && _Alignof(gm_tbci5ospk5nnn) == 8 && offsetof(gm_tbci5ospk5nnn, f0) == 0 && offsetof(gm_tbci5ospk5nnn, f1) == 8, "layout of `Int`");
_Static_assert(sizeof(gm_tddtjfggdat0d) == 48 && _Alignof(gm_tddtjfggdat0d) == 8 && offsetof(gm_tddtjfggdat0d, f0) == 0 && offsetof(gm_tddtjfggdat0d, f1) == 16 && offsetof(gm_tddtjfggdat0d, f2) == 24 && offsetof(gm_tddtjfggdat0d, f3) == 40, "layout of `FnTy`");
_Static_assert(sizeof(gm_teo9d4mncjfdu) == 24 && _Alignof(gm_teo9d4mncjfdu) == 8 && offsetof(gm_teo9d4mncjfdu, f0) == 0 && offsetof(gm_teo9d4mncjfdu, f1) == 16, "layout of `BreakExpr`");
_Static_assert(sizeof(gm_th9nl3oso747e) == 12 && _Alignof(gm_th9nl3oso747e) == 4 && offsetof(gm_th9nl3oso747e, f0) == 0, "layout of `(Builtin,)`");
typedef struct { gm_tbci5ospk5nnn f0; uint8_t f1; uint8_t pe[7]; } gm_thrdpodrs8pgt;
_Static_assert(sizeof(gm_tjcptv9sagfq8) == 88 && _Alignof(gm_tjcptv9sagfq8) == 8 && offsetof(gm_tjcptv9sagfq8, f0) == 0 && offsetof(gm_tjcptv9sagfq8, f1) == 16 && offsetof(gm_tjcptv9sagfq8, f2) == 28 && offsetof(gm_tjcptv9sagfq8, f3) == 32 && offsetof(gm_tjcptv9sagfq8, f4) == 40 && offsetof(gm_tjcptv9sagfq8, f5) == 48 && offsetof(gm_tjcptv9sagfq8, f6) == 56 && offsetof(gm_tjcptv9sagfq8, f7) == 64 && offsetof(gm_tjcptv9sagfq8, f8) == 72 && offsetof(gm_tjcptv9sagfq8, f9) == 80, "layout of `ConstDef`");
typedef struct { gm_tbci5ospk5nnn f0; gm_tbci5ospk5nnn f1; } gm_tngtuiuskn938;
_Static_assert(sizeof(gm_to6glrduves6o) == 32 && _Alignof(gm_to6glrduves6o) == 8 && offsetof(gm_to6glrduves6o, f0) == 0 && offsetof(gm_to6glrduves6o, f1) == 24, "layout of `(start: Place, len: u64)`");
_Static_assert(sizeof(gm_tpautr4br7h3l) == 48 && _Alignof(gm_tpautr4br7h3l) == 8 && offsetof(gm_tpautr4br7h3l, f0) == 0 && offsetof(gm_tpautr4br7h3l, f1) == 16 && offsetof(gm_tpautr4br7h3l, f2) == 24 && offsetof(gm_tpautr4br7h3l, f3) == 40, "layout of `StructDef`");
_Static_assert(sizeof(gm_trl9d7m1k5lbd) == 24 && _Alignof(gm_trl9d7m1k5lbd) == 8 && offsetof(gm_trl9d7m1k5lbd, f0) == 0 && offsetof(gm_trl9d7m1k5lbd, f1) == 8 && offsetof(gm_trl9d7m1k5lbd, f2) == 16, "layout of `(op: BinOp, lhs: ^Expr, rhs: ^Expr)`");
typedef struct { uint8_t f0; uint8_t p1[7]; union { uint32_t v0; gm_t419mo45g7j0a v1; void * v2; } f1; } gm_tsth9qu1dv0g3;
_Static_assert(sizeof(gm_tt8o5kebou62r) == 32 && _Alignof(gm_tt8o5kebou62r) == 8 && offsetof(gm_tt8o5kebou62r, f0) == 0 && offsetof(gm_tt8o5kebou62r, f1) == 16, "layout of `(op: Builtin, args: []Expr)`");
_Static_assert(sizeof(gm_ttu09lij36f8c) == 40 && _Alignof(gm_ttu09lij36f8c) == 8 && offsetof(gm_ttu09lij36f8c, f0) == 0 && offsetof(gm_ttu09lij36f8c, f1) == 32, "layout of `?Split`");
typedef struct { void * f0; uint8_t p1[8]; gm_t6evimt84lndq f1; } gm_t2i13nq5toap9;
_Static_assert(sizeof(gm_t53at80pfdus3) == 56 && _Alignof(gm_t53at80pfdus3) == 8 && offsetof(gm_t53at80pfdus3, f0) == 0 && offsetof(gm_t53at80pfdus3, f1) == 8, "layout of `TyKind`");
_Static_assert(sizeof(gm_t6evimt84lndq) == 48 && _Alignof(gm_t6evimt84lndq) == 16 && offsetof(gm_t6evimt84lndq, f0) == 0 && offsetof(gm_t6evimt84lndq, f1) == 16, "layout of `Value`");
typedef struct { uint64_t f0; uint8_t p1[8]; gm_t6evimt84lndq f1; } gm_thf8as07kj3uj;
_Static_assert(sizeof(gm_thrdpodrs8pgt) == 56 && _Alignof(gm_thrdpodrs8pgt) == 8 && offsetof(gm_thrdpodrs8pgt, f0) == 0 && offsetof(gm_thrdpodrs8pgt, f1) == 48, "layout of `?Int`");
typedef struct { gm_t6evimt84lndq f0; uint8_t f1; uint8_t pe[15]; } gm_tjoi8o798ne9k;
_Static_assert(sizeof(gm_tngtuiuskn938) == 96 && _Alignof(gm_tngtuiuskn938) == 8 && offsetof(gm_tngtuiuskn938, f0) == 0 && offsetof(gm_tngtuiuskn938, f1) == 48, "layout of `Rat`");
typedef struct { gm_tsth9qu1dv0g3 f0; gm_b16_8 f1; } gm_to76jli3bqddq;
_Static_assert(sizeof(gm_tsth9qu1dv0g3) == 40 && _Alignof(gm_tsth9qu1dv0g3) == 8 && offsetof(gm_tsth9qu1dv0g3, f0) == 0 && offsetof(gm_tsth9qu1dv0g3, f1) == 8, "layout of `Callee`");
typedef struct { gm_tngtuiuskn938 f0; uint8_t f1; uint8_t pe[7]; } gm_tvegkb6ouf3m0;
_Static_assert(sizeof(gm_t2i13nq5toap9) == 64 && _Alignof(gm_t2i13nq5toap9) == 16 && offsetof(gm_t2i13nq5toap9, f0) == 0 && offsetof(gm_t2i13nq5toap9, f1) == 16, "layout of `(frame: ^Frame, value: Value)`");
typedef struct { void * f0; uint32_t f1; uint8_t p2[4]; gm_tjoi8o798ne9k f2; } gm_ta160mkacvukc;
typedef struct { uint32_t f0; uint8_t p1[12]; gm_tjoi8o798ne9k f1; } gm_tbnj1edh31eaa;
typedef struct { uint8_t f0; uint8_t p1[7]; union { gm_tbci5ospk5nnn v0; double v1; void * v2; void * v3; uint8_t v4; gm_b16_8 v5; gm_b16_8 v6; uint32_t v7; gm_tkn42u7na3t81 v8; gm_tkn42u7na3t81 v9; gm_t88rcucg1b7uk v10; gm_t3rcjibl8kkg4 v11; gm_t08olq97rscn3 v12; uint32_t v13; gm_tcc1rq4n1avb2 v14; gm_t3877pk7lv748 v15; void * v16; gm_tbl8il92l97ut v17; gm_tlcl4u558l6p5 v18; gm_tao63pk3r18v5 v19; void * v20; void * v21; void * v22; void * v23; uint32_t v24; uint32_t v25; uint32_t v26; gm_t8ghgl2jj0qha v27; gm_trl9d7m1k5lbd v28; gm_ta612fil812pm v29; gm_t8jsc4n9vp2ib v30; gm_tt8o5kebou62r v31; gm_to76jli3bqddq v32; gm_b16_8 v33; gm_b16_8 v34; void * v35; gm_ttggq7ng4kp3p v36; void * v37; gm_tsplfr33dsvji v38; gm_t3dcoc7077t1q v39; gm_t083kd7b1kkag v40; gm_tlcl4u558l6p5 v41; gm_tkaapcii86udk v42; gm_tlcl4u558l6p5 v43; void * v44; gm_trl2khmuphncd v45; gm_t710lsrq1m8gk v46; gm_teo9d4mncjfdu v47; gm_te9hrk0u35rev v48; gm_ttggq7ng4kp3p v49; void * v50; gm_t3mf58gv9kf3n v51; void * v52; void * v53; void * v54; gm_tandvbt63uk7n v55; gm_t710lsrq1m8gk v56; } f1; } gm_teti411eujvf4;
_Static_assert(sizeof(gm_thf8as07kj3uj) == 64 && _Alignof(gm_thf8as07kj3uj) == 16 && offsetof(gm_thf8as07kj3uj, f0) == 0 && offsetof(gm_thf8as07kj3uj, f1) == 16, "layout of `(i: usize, x: Value)`");
_Static_assert(sizeof(gm_tjoi8o798ne9k) == 64 && _Alignof(gm_tjoi8o798ne9k) == 16 && offsetof(gm_tjoi8o798ne9k, f0) == 0 && offsetof(gm_tjoi8o798ne9k, f1) == 48, "layout of `?Value`");
_Static_assert(sizeof(gm_to76jli3bqddq) == 56 && _Alignof(gm_to76jli3bqddq) == 8 && offsetof(gm_to76jli3bqddq, f0) == 0 && offsetof(gm_to76jli3bqddq, f1) == 40, "layout of `(callee: Callee, args: []Expr)`");
_Static_assert(sizeof(gm_tvegkb6ouf3m0) == 104 && _Alignof(gm_tvegkb6ouf3m0) == 8 && offsetof(gm_tvegkb6ouf3m0, f0) == 0 && offsetof(gm_tvegkb6ouf3m0, f1) == 96, "layout of `?Rat`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_ta160mkacvukc v0; gm_tbl8il92l97ut v1; gm_t2i13nq5toap9 v2; gm_tbnj1edh31eaa v3; void * v4; gm_tbgd14esf0jvi v5; } f1; } gm_t39uai1ajk3qv;
typedef struct { gm_t88rcucg1b7uk f0; uint8_t p1[4]; gm_teti411eujvf4 f1; gm_tl874t2atnlh3 f2; uint8_t pe[4]; } gm_t69m56meqefs8;
_Static_assert(sizeof(gm_ta160mkacvukc) == 80 && _Alignof(gm_ta160mkacvukc) == 16 && offsetof(gm_ta160mkacvukc, f0) == 0 && offsetof(gm_ta160mkacvukc, f1) == 8 && offsetof(gm_ta160mkacvukc, f2) == 16, "layout of `(frame: ^Frame, label: u32, value: ?Value)`");
_Static_assert(sizeof(gm_tbnj1edh31eaa) == 80 && _Alignof(gm_tbnj1edh31eaa) == 16 && offsetof(gm_tbnj1edh31eaa, f0) == 0 && offsetof(gm_tbnj1edh31eaa, f1) == 16, "layout of `(with_id: u32, value: ?Value)`");
_Static_assert(sizeof(gm_teti411eujvf4) == 64 && _Alignof(gm_teti411eujvf4) == 8 && offsetof(gm_teti411eujvf4, f0) == 0 && offsetof(gm_teti411eujvf4, f1) == 8, "layout of `ExprKind`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; double v1; } f1; } gm_t26jhpkpc0sda;
_Static_assert(sizeof(gm_t39uai1ajk3qv) == 96 && _Alignof(gm_t39uai1ajk3qv) == 16 && offsetof(gm_t39uai1ajk3qv, f0) == 0 && offsetof(gm_t39uai1ajk3qv, f1) == 16, "layout of `Flow`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; gm_tkaapcii86udk v1; } f1; } gm_t5kq49rr2n9f8;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; gm_b16_8 v1; } f1; } gm_t65n7r6ql4pgd;
_Static_assert(sizeof(gm_t69m56meqefs8) == 88 && _Alignof(gm_t69m56meqefs8) == 8 && offsetof(gm_t69m56meqefs8, f0) == 0 && offsetof(gm_t69m56meqefs8, f1) == 8 && offsetof(gm_t69m56meqefs8, f2) == 72, "layout of `Expr`");
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; void * v1; } f1; } gm_t7g11k265ojef;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; unsigned __int128 v1; } f1; } gm_t8carr7mku484;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; } f1; } gm_t9j082e6qvnjj;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; uint8_t v1; } f1; } gm_tsg40glpvq01o;
typedef struct { uint8_t f0; uint8_t p1[15]; union { gm_t39uai1ajk3qv v0; gm_t6evimt84lndq v1; } f1; } gm_ttk92ufj2f6eh;
_Static_assert(sizeof(gm_t26jhpkpc0sda) == 112 && _Alignof(gm_t26jhpkpc0sda) == 16 && offsetof(gm_t26jhpkpc0sda, f0) == 0 && offsetof(gm_t26jhpkpc0sda, f1) == 16, "layout of `Flow!f64`");
_Static_assert(sizeof(gm_t5kq49rr2n9f8) == 112 && _Alignof(gm_t5kq49rr2n9f8) == 16 && offsetof(gm_t5kq49rr2n9f8, f0) == 0 && offsetof(gm_t5kq49rr2n9f8, f1) == 16, "layout of `Flow!Place`");
_Static_assert(sizeof(gm_t65n7r6ql4pgd) == 112 && _Alignof(gm_t65n7r6ql4pgd) == 16 && offsetof(gm_t65n7r6ql4pgd, f0) == 0 && offsetof(gm_t65n7r6ql4pgd, f1) == 16, "layout of `Flow![]Value`");
_Static_assert(sizeof(gm_t7g11k265ojef) == 112 && _Alignof(gm_t7g11k265ojef) == 16 && offsetof(gm_t7g11k265ojef, f0) == 0 && offsetof(gm_t7g11k265ojef, f1) == 16, "layout of `Flow!^Cell`");
_Static_assert(sizeof(gm_t8carr7mku484) == 112 && _Alignof(gm_t8carr7mku484) == 16 && offsetof(gm_t8carr7mku484, f0) == 0 && offsetof(gm_t8carr7mku484, f1) == 16, "layout of `Flow!u128`");
_Static_assert(sizeof(gm_t9j082e6qvnjj) == 112 && _Alignof(gm_t9j082e6qvnjj) == 16 && offsetof(gm_t9j082e6qvnjj, f0) == 0 && offsetof(gm_t9j082e6qvnjj, f1) == 16, "layout of `Flow!()`");
_Static_assert(sizeof(gm_tsg40glpvq01o) == 112 && _Alignof(gm_tsg40glpvq01o) == 16 && offsetof(gm_tsg40glpvq01o, f0) == 0 && offsetof(gm_tsg40glpvq01o, f1) == 16, "layout of `Flow!bool`");
_Static_assert(sizeof(gm_ttk92ufj2f6eh) == 112 && _Alignof(gm_ttk92ufj2f6eh) == 16 && offsetof(gm_ttk92ufj2f6eh, f0) == 0 && offsetof(gm_ttk92ufj2f6eh, f1) == 16, "layout of `Flow!Value`");
struct gm_fr_abs_v7cf214 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_add_420lj6o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_big_float_jmiufgl {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* i */
    double ret;
};
struct gm_fr_big_of_1cq0qao {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_bigint_i128_p6m54kv {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_ta24rd13gupia ret;
};
struct gm_fr_binary_fmoblrg {
    gm_frame h;
    gm_t6evimt84lndq l2; /* a */
    gm_t6evimt84lndq l3; /* b */
    gm_trqfvmjtt4sij l9; /* out */
    gm_t6evimt84lndq l13; /* x */
    gm_t6evimt84lndq l14; /* y */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_bit_s517clu {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_builtin_psq6ql3 {
    gm_frame h;
    gm_t6evimt84lndq l7; /* msg */
    gm_t6evimt84lndq l10; /* x */
    gm_tbci5ospk5nnn l16; /* tag */
    gm_tbci5ospk5nnn l19; /* i */
    gm_tbci5ospk5nnn l22; /* v */
    gm_tngtuiuskn938 l23; /* r */
    gm_tngtuiuskn938 l25; /* q */
    gm_tngtuiuskn938 l26; /* r */
    gm_tngtuiuskn938 l27; /* r */
    gm_tbci5ospk5nnn l28; /* v */
    gm_tngtuiuskn938 l31; /* q */
    gm_t6evimt84lndq l32; /* y */
    gm_tbci5ospk5nnn l41; /* a */
    gm_tbci5ospk5nnn l43; /* v */
    gm_tngtuiuskn938 l44; /* r */
    gm_tbci5ospk5nnn l45; /* a */
    gm_tbci5ospk5nnn l49; /* d */
    gm_tbci5ospk5nnn l53; /* exact */
    gm_tngtuiuskn938 l55; /* r */
    gm_tbci5ospk5nnn l63; /* tag */
    gm_t6evimt84lndq l75; /* v */
    gm_trqfvmjtt4sij l78; /* elems */
    gm_t6evimt84lndq l85; /* v */
    gm_t6evimt84lndq l87; /* pv */
    gm_trqfvmjtt4sij l95; /* path */
    unsigned __int128 l100; /* v */
    gm_tbci5ospk5nnn l116; /* amount */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_byte_at_m94bj3e {
    gm_frame h;
    uint64_t ret;
};
struct gm_fr_cmp_9aseneh {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    int32_t ret;
};
struct gm_fr_coerce_fesnqv4 {
    gm_frame h;
    gm_t6evimt84lndq l4; /* v */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_const_cell_p4adg2s {
    gm_frame h;
    gm_tjcptv9sagfq8 l5; /* c */
    gm_t7g11k265ojef ret;
};
struct gm_fr_const_id_2mlh0og {
    gm_frame h;
    gm_t9805qtonbsag ret;
};
struct gm_fr_convert_value_v1i8v3i {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_copy_pmkg67r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_display_covn4i2 {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_div_f0qrfbr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* q */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_div_floor_u3mfihk {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* q */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_empty_slice_84b0b46 {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_d914i5u {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_empty_slice_dheabhi {
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
struct gm_fr_eq_60fjn21 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    uint8_t ret;
};
struct gm_fr_equal_hinsj77 {
    gm_frame h;
    gm_t6evimt84lndq l1; /* a */
    gm_t6evimt84lndq l2; /* b */
    gm_tsg40glpvq01o ret;
};
struct gm_fr_field_tys_foan32f {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* out */
    gm_tpautr4br7h3l l10; /* def */
    gm_trqfvmjtt4sij l11; /* out */
    gm_t0mglksrkfa5e l14; /* f */
    gm_th2lqbo77pvsc ret;
};
struct gm_fr_fit_8hvi4bv {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* exact */
    gm_tngtuiuskn938 l6; /* r */
    gm_t8carr7mku484 ret;
};
struct gm_fr_fit_small_7o94i8v {
    gm_frame h;
    gm_t8carr7mku484 ret;
};
struct gm_fr_float_exact_1g17c6c {
    gm_frame h;
    gm_tbci5ospk5nnn l6; /* num */
    gm_tvegkb6ouf3m0 ret;
};
struct gm_fr_float_rem_24loign {
    gm_frame h;
    gm_tngtuiuskn938 l2; /* a */
    gm_tngtuiuskn938 l3; /* b */
    gm_tngtuiuskn938 l4; /* q */
    gm_tbci5ospk5nnn l5; /* t */
    gm_tngtuiuskn938 l6; /* r */
    double ret;
};
struct gm_fr_float_to_int_sf4dap7 {
    gm_frame h;
    gm_tngtuiuskn938 l4; /* q */
    gm_tbci5ospk5nnn l5; /* fl */
    gm_tngtuiuskn938 l6; /* frac */
    gm_tbci5ospk5nnn l7; /* i */
    gm_tngtuiuskn938 l9; /* half */
    gm_tngtuiuskn938 l11; /* r */
    gm_thrdpodrs8pgt ret;
};
struct gm_fr_from_i128_1n0s49q {
    gm_frame h;
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_from_int_ofjuoqh {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* i */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_get_bytes_ql9o4k0 {
    gm_frame h;
    gm_trqfvmjtt4sij l17; /* elems */
    gm_tjoi8o798ne9k ret;
};
struct gm_fr_int_info_2r9qb5n {
    gm_frame h;
    gm_ttovhfee8mf28 ret;
};
struct gm_fr_int_op_jnafd2a {
    gm_frame h;
    gm_tbci5ospk5nnn l16; /* a */
    gm_tbci5ospk5nnn l17; /* c */
    gm_tbci5ospk5nnn l18; /* exact */
    gm_tbci5ospk5nnn l25; /* a */
    gm_tbci5ospk5nnn l26; /* c */
    gm_tbci5ospk5nnn l27; /* exact */
    gm_t8carr7mku484 ret;
};
struct gm_fr_int_range_hue3vms {
    gm_frame h;
    gm_tbci5ospk5nnn l3; /* half */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_int_value_6hmh8tj {
    gm_frame h;
    __int128 ret;
};
struct gm_fr_is_even_3kgcpjs {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_integer_8d4f9ek {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_is_integer_ab2ohk2 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_negative_k87pp3n {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_is_negative_rb46l9j {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_zero_5aept6u {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* r */
    uint8_t ret;
};
struct gm_fr_is_zero_8tcct9o {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    uint8_t ret;
};
struct gm_fr_kind_jcnt4hm {
    gm_frame h;
    gm_t53at80pfdus3 ret;
};
struct gm_fr_list_0202sdh {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_go2qg0b {
    gm_frame h;
    gm_trqfvmjtt4sij ret;
};
struct gm_fr_list_kl0t9m0 {
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
struct gm_fr_make_r4pbu77 {
    gm_frame h;
    gm_trqfvmjtt4sij l1; /* m */
    gm_trqfvmjtt4sij l2; /* l */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_mask_gbhl4hp {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_mul_nfj5rjv {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_mul_od1e7nd {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_neg_agd5c5k {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_offsets_niooj98 {
    gm_frame h;
    gm_trqfvmjtt4sij l6; /* tys */
    gm_trqfvmjtt4sij l7; /* offs */
    gm_tpautr4br7h3l l13; /* def */
    gm_trqfvmjtt4sij l14; /* tys */
    gm_t0mglksrkfa5e l17; /* f */
    gm_ttu09lij36f8c ret;
};
struct gm_fr_one_08qg1op {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_payload_ty_mu1ii1k {
    gm_frame h;
    gm_tkn42u7na3t81 ret;
};
struct gm_fr_printer_h7jigd7 {
    gm_frame h;
    gm_tmm67r2c94d5j ret;
};
struct gm_fr_ptr_bits_glps5i7 {
    gm_frame h;
    uint32_t ret;
};
struct gm_fr_put_bytes_1ltl1lv {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_t6evimt84lndq l23; /* x */
    uint8_t ret;
};
struct gm_fr_reinterpret_jmr6jts {
    gm_frame h;
    gm_t6evimt84lndq l1; /* v */
    gm_trqfvmjtt4sij l6; /* bytes */
    gm_t6evimt84lndq l9; /* r */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_rem_ib4l1vl {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn l2; /* r */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_round_float_saju9fu {
    gm_frame h;
    gm_tngtuiuskn938 l4; /* r */
    gm_t26jhpkpc0sda ret;
};
struct gm_fr_same_place_1r7qrqk {
    gm_frame h;
    uint8_t ret;
};
struct gm_fr_scalar_binary_bvc65kt {
    gm_frame h;
    gm_t6evimt84lndq l2; /* a */
    gm_t6evimt84lndq l3; /* b */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_shl_qqecl81 {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_show_8uijkbs {
    gm_frame h;
    gm_b16_8 ret;
};
struct gm_fr_show_kmjeh8s {
    gm_frame h;
    gm_trqfvmjtt4sij l2; /* out */
    gm_b16_8 ret;
};
struct gm_fr_sub_mler6q8 {
    gm_frame h;
    gm_tngtuiuskn938 l0; /* a */
    gm_tngtuiuskn938 l1; /* b */
    gm_tngtuiuskn938 ret;
};
struct gm_fr_sub_pktm5cr {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tbci5ospk5nnn l1; /* b */
    gm_tbci5ospk5nnn ret;
};
struct gm_fr_to_decimal_8bshn5b {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_trqfvmjtt4sij l1; /* out */
    gm_b16_8 ret;
};
struct gm_fr_to_u128_haka99r {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_t9p38b040grms ret;
};
struct gm_fr_to_u64_m01t1da {
    gm_frame h;
    gm_tbci5ospk5nnn l0; /* a */
    gm_tf5srh4mp4b3i ret;
};
struct gm_fr_unary_19d8qg7 {
    gm_frame h;
    gm_t6evimt84lndq l4; /* v */
    gm_trqfvmjtt4sij l7; /* out */
    gm_t6evimt84lndq l10; /* el */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_unary_value_vi7cs6u {
    gm_frame h;
    gm_t6evimt84lndq l2; /* v */
    gm_ttk92ufj2f6eh ret;
};
struct gm_fr_unit_value_evae6mr {
    gm_frame h;
    gm_t6evimt84lndq ret;
};
struct gm_fr_unsupported_2seq49l {
    gm_frame h;
    gm_t39uai1ajk3qv ret;
};
struct gm_fr_variant_tag_4pbamin {
    gm_frame h;
    gm_thrdpodrs8pgt ret;
};
struct gm_fr_wrap_bt8r149 {
    gm_frame h;
    unsigned __int128 ret;
};
struct gm_fr_zero_s25l990 {
    gm_frame h;
    gm_tbci5ospk5nnn ret;
};
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b);
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b);
inline __attribute__((aligned(4))) void gm_f_abs_v7cf214(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_b16_8 gm_f_append_sajo2rq(void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_args_6f16vj1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_b16_8 a1);
inline __attribute__((aligned(4))) double gm_f_big_float_jmiufgl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_big_of_1cq0qao(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, unsigned __int128 a1, gm_t88rcucg1b7uk a2);
inline __attribute__((aligned(4))) void gm_f_bigint_i128_p6m54kv(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_ta24rd13gupia gm_f_bigint_i128_p6m54kv_c(gm_tbci5ospk5nnn a0);
__attribute__((aligned(4))) void gm_f_binary_fmoblrg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) gm_b16_8 gm_f_binop_symbol_9ijp0eo(gm_ts6oqppjh0ba3 a0);
inline __attribute__((aligned(4))) uint8_t gm_f_bit_s517clu(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
__attribute__((aligned(4))) unsigned __int128 gm_f_bits128_jigkui4(const void * a0);
__attribute__((aligned(4))) uint64_t gm_f_bits_lr3of4i(const void * a0);
__attribute__((aligned(4))) void gm_f_builtin_psq6ql3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tvdg94bk7aa22 a1, gm_b16_8 a2, void * a3);
inline __attribute__((aligned(4))) uint64_t gm_f_byte_at_m94bj3e(void * a0, uint64_t a1, uint64_t a2);
inline __attribute__((aligned(4))) int32_t gm_f_cmp_9aseneh(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) int32_t gm_f_cmp_i1knrgu(const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_coerce_fesnqv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tmn7n0n9raou5 a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_const_cell_p4adg2s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3);
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_convert_value_v1i8v3i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3);
__attribute__((aligned(4))) void gm_f_copy_nk4jid6(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_display_covn4i2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) void gm_f_div_d8sd4qc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_div_f0qrfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_div_floor_u3mfihk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_div_mod_floor_eqv1ila(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_div_rem_sc09dcc(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_ijtp9lo(void);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_rcd2jln(void);
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_equal_hinsj77(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
__attribute__((aligned(4))) void gm_f_eval_2vbtp1v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_extend_7hs67uj(void * a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_field_tys_foan32f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_fit_8hvi4bv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_fit_small_7o94i8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, __int128 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5);
__attribute__((aligned(4))) void gm_f_float_exact_1g17c6c(void *gm_out, void *env, gm_node *cx, gm_node *inst, double a0);
__attribute__((aligned(4))) double gm_f_float_rem_24loign(void *env, gm_node *cx, gm_node *inst, double a0, double a1);
__attribute__((aligned(4))) void gm_f_float_to_int_sf4dap7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, double a1, gm_t88rcucg1b7uk a2, gm_tvdg94bk7aa22 a3);
__attribute__((aligned(4))) void gm_f_format_0fuesvo(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_0sujqbp(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_format_9u3jvus(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t9qkh0rsooc8d a0);
__attribute__((aligned(4))) void gm_f_format_eruojoe(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_g0ibdjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_o6d0gj1(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_th9nl3oso747e a0);
__attribute__((aligned(4))) void gm_f_format_q8f0n70(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_u0vfg08(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0);
__attribute__((aligned(4))) void gm_f_format_ven7kt5(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_t88rcucg1b7uk a0);
inline __attribute__((aligned(4))) void gm_f_from_i128_1n0s49q(void *gm_out, void *env, gm_node *cx, gm_node *inst, __int128 a0);
inline __attribute__((aligned(4))) void gm_f_from_int_ofjuoqh(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_from_u128_gl5fk0t(void *gm_out, void *env, gm_node *cx, gm_node *inst, unsigned __int128 a0);
__attribute__((aligned(4))) void gm_f_from_u64_ud13q5v(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint64_t a0);
__attribute__((aligned(4))) void gm_f_get_bytes_ql9o4k0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint64_t a3);
__attribute__((aligned(4))) gm_t9805qtonbsag gm_f_get_pk8ncss(void * a0, gm_trlabqsjjp0ds a1);
__attribute__((aligned(4))) gm_ttovhfee8mf28 gm_f_int_info_2r9qb5n(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_int_op_jnafd2a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, unsigned __int128 a2, unsigned __int128 a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6);
__attribute__((aligned(4))) void gm_f_int_range_hue3vms(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) __int128 gm_f_int_value_6hmh8tj(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) void gm_f_into_slice_hq0te56(void *gm_out, void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_even_3kgcpjs(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_rb46l9j(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0);
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0);
inline __attribute__((aligned(4))) void gm_f_kind_jcnt4hm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) void gm_f_layout_a5ovnrm(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_list_0202sdh(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_kl0t9m0(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_nof7vfj(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_list_uvfu1k1(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) void gm_f_load_nf7o5kl(void *gm_out, void * a0, const void * a1);
__attribute__((aligned(4))) int32_t gm_f_mag_cmp_s39veq6(gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_mul_if5s38k(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, gm_b16_8 a1);
__attribute__((aligned(4))) void gm_f_mag_shl_5df643p(void *gm_out, void *env, gm_node *cx, gm_node *inst, gm_b16_8 a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_mag_to_u128_f843dal(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_mask_gbhl4hp(uint32_t a0);
inline __attribute__((aligned(4))) void gm_f_mul_nfj5rjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_mul_od1e7nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_15vqrvd(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_new_2jv6hag(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_70le0lp(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_lcff781(void *env, gm_node *cx, gm_node *inst, gm_tnpifv20irnph a0, const void * a1);
__attribute__((aligned(4))) gm_tq7s98ihonkju gm_f_new_ve8bb01_2(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_offset_nkn711s(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
__attribute__((aligned(4))) void gm_f_offsets_niooj98(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) void gm_f_one_08qg1op(void *gm_out, void *env, gm_node *cx, gm_node *inst);
__attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_payload_ty_mu1ii1k(void * a0, gm_csguankko4lmhd a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_place_9889dgp(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, void * a1);
__attribute__((aligned(4))) void gm_f_pow_mhlb5fr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint32_t a1);
inline __attribute__((aligned(4))) void gm_f_printer_h7jigd7(void *gm_out, void * a0, void * a1);
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_0pgacsj(void * a0, const void * a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_7o6oubd(void * a0, uint64_t a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_k3l9nic(void * a0, gm_csguankko4lmhd a1);
__attribute__((aligned(4))) gm_tftb1odc739t5 gm_f_push_t83rg2m(void * a0, uint8_t a1);
__attribute__((aligned(4))) uint8_t gm_f_put_bytes_1ltl1lv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint64_t a4);
__attribute__((aligned(4))) void gm_f_reinterpret_jmr6jts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4);
inline __attribute__((aligned(4))) void gm_f_rem_ib4l1vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
__attribute__((aligned(4))) void gm_f_round_float_saju9fu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, double a1, gm_t88rcucg1b7uk a2);
__attribute__((aligned(4))) uint8_t gm_f_same_place_1r7qrqk(const void * a0, const void * a1);
__attribute__((aligned(4))) uint8_t gm_f_same_place_1r7qrqk_c(gm_tkaapcii86udk a0, gm_tkaapcii86udk a1);
__attribute__((aligned(4))) void gm_f_scalar_binary_bvc65kt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6);
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_8uijkbs(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_show_kmjeh8s(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_signed_add_ek0dpmo(void *gm_out, void *env, gm_node *cx, gm_node *inst, uint8_t a0, gm_b16_8 a1, uint8_t a2, gm_b16_8 a3);
__attribute__((aligned(4))) void gm_f_store_kejn5q1(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_tl874t2atnlh3 a3);
__attribute__((aligned(4))) void gm_f_string_of_plna4gu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_sub_mler6q8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1);
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_f16_0pcu0pq(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) double gm_f_to_f64_4avhrht(void *env, gm_node *cx, gm_node *inst, const void * a0);
__attribute__((aligned(4))) void gm_f_to_i128_o7ud1ue(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0);
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0);
__attribute__((aligned(4))) void gm_f_trap_jftbss6(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_trim_8rdppmk(void * a0);
__attribute__((aligned(4))) gm_b0_1 gm_f_ty_vka2r8h(void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1);
__attribute__((aligned(4))) void gm_f_unary_19d8qg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, void * a3);
__attribute__((aligned(4))) void gm_f_unary_value_vi7cs6u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, const void * a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4, gm_tl874t2atnlh3 a5);
inline __attribute__((aligned(4))) void gm_f_unit_value_evae6mr(void *gm_out, void *env, gm_node *cx, gm_node *inst);
inline __attribute__((aligned(4))) void gm_f_unsupported_2seq49l(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2);
__attribute__((aligned(4))) void gm_f_variant_tag_4pbamin(void *gm_out, void * a0, gm_csguankko4lmhd a1, uint32_t a2);
__attribute__((aligned(4))) gm_thrdpodrs8pgt gm_f_variant_tag_4pbamin_c(void * a0, gm_csguankko4lmhd a1, uint32_t a2);
__attribute__((aligned(4))) void gm_f_vector_parts_crrfopm(void *gm_out, void * a0, gm_csguankko4lmhd a1);
inline __attribute__((aligned(4))) unsigned __int128 gm_f_wrap_bt8r149(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2);
__attribute__((aligned(4))) gm_b0_1 gm_f_write_decimal_ke367ab(void *env, gm_node *cx, gm_node *inst, const void * a0, void * a1);
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst);
extern uint8_t gm_k1fl2q13fn0[40] __attribute__((aligned(8)));
extern uint8_t gm_k4nsaof9c90[40] __attribute__((aligned(8)));
extern uint8_t gm_k4tn3ii07uh[40] __attribute__((aligned(8)));
extern uint8_t gm_k7898pnkupv[40] __attribute__((aligned(8)));
extern uint8_t gm_kct6b70ce3d[40] __attribute__((aligned(8)));
extern uint8_t gm_kdra5fvcfrb[40] __attribute__((aligned(8)));
extern uint8_t gm_kequv58cbtg[40] __attribute__((aligned(8)));
extern uint8_t gm_kha5d1oq7pb[40] __attribute__((aligned(8)));
extern uint8_t gm_kinrtga7ov4[40] __attribute__((aligned(8)));
extern uint8_t gm_kjh6c1ct3mq[40] __attribute__((aligned(8)));
extern uint8_t gm_kjqc35oao72[40] __attribute__((aligned(8)));
extern uint8_t gm_knj30d2h126[40] __attribute__((aligned(8)));
extern uint8_t gm_kqu6q38jp8e[40] __attribute__((aligned(8)));
extern uint8_t gm_krchumal22f[40] __attribute__((aligned(8)));
extern uint8_t gm_ksjvoab7999[40] __attribute__((aligned(8)));
extern uint8_t gm_ksrlu8s6v53[40] __attribute__((aligned(8)));
extern uint8_t gm_ktbnd3fuoao[40] __attribute__((aligned(8)));
extern uint8_t gm_ktv7kfnnmgp[40] __attribute__((aligned(8)));
extern const uint8_t gm_s0dggtblbl7jq[30];
extern const uint8_t gm_s2kubr08sqj72[15];
extern const uint8_t gm_s3385032lmmqq[24];
extern const uint8_t gm_s8afn1h3asffl[32];
extern const uint8_t gm_s94n39ltlvmhe[36];
extern const uint8_t gm_s9o2c36nosv54[41];
extern const uint8_t gm_s9ofbo8c28rrv[16];
extern const uint8_t gm_sbjuojbqo51av[36];
extern const uint8_t gm_sbsu8rcurukea[25];
extern const uint8_t gm_sd3b7a2bnmpef[25];
extern const uint8_t gm_sdrg69bmlm0tm[30];
extern const uint8_t gm_seg4idqqmijmd[34];
extern const uint8_t gm_sjn4krvshoajd[26];
extern const uint8_t gm_skolmnfc0ccbd[2];
extern const uint8_t gm_slf2kl43ogurh[17];
extern const uint8_t gm_sllgvbfukep8p[24];
extern const uint8_t gm_smqhtg33814q9[23];
extern const uint8_t gm_smr3su985na96[16];
extern const uint8_t gm_spi8n2atkg603[17];
extern const uint8_t gm_sqe4aodms9tfs[14];
extern const uint8_t gm_sqh0568fl8oao[14];
extern const char gm_src0ih77f43mhf2[];
extern const char gm_src2ah81i3aa8hm[];
extern const char gm_src4o48ksu43djd[];
extern const char gm_src9vg8k4mbj38f[];
extern const char gm_srclv3pj5es0f8o[];
extern const char gm_srcrgc9bkquf6nr[];
extern const char gm_srcue4ionbe9lgh[];
extern const uint8_t gm_su1oqg46rlg2d[10];
extern const uint8_t gm_sv7lsha1e0i8r[13];
extern const uint8_t gm_svpofgjtij7vj[4];
extern const gm_type gm_tyo86ks9qkvt;
inline bool gm_eq467d5cm1d5(const uint8_t *a, const uint8_t *b) {
    return GM_LD(uint32_t, a) == GM_LD(uint32_t, b);
}
inline bool gm_eq9m6mvtmgde(const uint8_t *a, const uint8_t *b) {
    if (!gm_eq467d5cm1d5(a + 0, b + 0)) return false;
    return true;
}
inline __attribute__((aligned(4))) void gm_f_abs_v7cf214(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_abs_v7cf214 F;
    GM_STACK_CHECK(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), ((uint8_t)0));
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) void gm_f_add_420lj6o(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_add_420lj6o F;
    GM_STACK_CHECK(&F, 98u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t7 = t6;
    gm_b16_8 t8 = GM_LD(gm_b16_8, ((uint8_t *)&t7));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t4, t5, t8);
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
inline __attribute__((aligned(4))) double gm_f_big_float_jmiufgl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1) {
    struct gm_fr_big_float_jmiufgl F;
    void * gm_l0; /* m */
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
inline __attribute__((aligned(4))) void gm_f_bigint_i128_p6m54kv(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_bigint_i128_p6m54kv F;
    GM_STACK_CHECK_SHALLOW(&F, 0u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_f_to_i128_o7ud1ue((void *)&(*(gm_ta24rd13gupia *)gm_out), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
    goto gm_fdone;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) uint8_t gm_f_bit_s517clu(void *env, gm_node *cx, gm_node *inst, uint32_t a0) {
    struct gm_fr_bit_s517clu F;
    uint32_t gm_l0; /* i */
    GM_STACK_CHECK_SHALLOW(&F, 75u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    unsigned __int128 t1 = GM_LD(unsigned __int128, ((uint8_t *)&((struct gm_fr_builtin_psq6ql3 *)((&F.h)->up))->l100));
    uint32_t t2 = gm_l0;
    unsigned __int128 t3 = (unsigned __int128)t2;
    unsigned __int128 n4 = (unsigned __int128)t3;
    if (n4 >= 128) gm_trap_shift((uint64_t)n4, "u128", gm_srcue4ionbe9lgh, 594, 43);
    unsigned __int128 t5 = (unsigned __int128)(t1 >> (int)n4);
    unsigned __int128 t6 = (unsigned __int128)(t5 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)));
    uint8_t t7 = ((t6 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint64_t gm_f_byte_at_m94bj3e(void * a0, uint64_t a1, uint64_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_byte_at_m94bj3e F;
    void * gm_l0; /* m */
    uint64_t gm_l1; /* i */
    uint64_t gm_l2; /* n */
    GM_STACK_CHECK_SHALLOW(&F, 129u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    uint64_t t1;
    void * t3 = gm_l0;
    void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
    void * t5 = GM_LD(void *, (((uint8_t *)t4) + 0));
    uint8_t t6 = GM_LD(uint8_t, ((((uint8_t *)t5) + 88) + 8));
    if (!t6) goto ie2;
    uint64_t t8 = gm_l1;
    uint64_t t9 = t8;
    t1 = t9;
    goto ix2;
ie2:;
    uint64_t t11 = gm_l2;
    uint64_t r12; bool o13 = __builtin_sub_overflow(t11, ((uint64_t)1ULL), &r12);
    if (o13) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 835, 98);
    uint64_t t14 = r12;
    uint64_t t15 = gm_l1;
    uint64_t r16; bool o17 = __builtin_sub_overflow(t14, t15, &r16);
    if (o17) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 835, 98);
    uint64_t t18 = r16;
    uint64_t t19 = t18;
    t1 = t19;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) int32_t gm_f_cmp_9aseneh(void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_cmp_9aseneh F;
    GM_STACK_CHECK(&F, 108u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_mul_nfj5rjv((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    int32_t t3 = gm_f_cmp_i1knrgu(&t1, &t2);
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
inline __attribute__((aligned(4))) gm_t9805qtonbsag gm_f_const_id_2mlh0og(void * a0, gm_cseahvg8v7p0vs a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_const_id_2mlh0og F;
    void * gm_l0; /* s */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    void * t1 = gm_l0;
    void * t2 = (void *)(((uint8_t *)t1) + 2008);
    gm_tl874t2atnlh3 t3 = gm_l1;
    gm_t88rcucg1b7uk t4 = gm_l2;
    gm_trlabqsjjp0ds t5;
    GM_ST(gm_tl874t2atnlh3, ((uint8_t *)&t5 + 0), t3);
    GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t5 + 12), t4);
    gm_t9805qtonbsag t6 = gm_f_get_pk8ncss(t2, t5);
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_copy_pmkg67r(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_copy_pmkg67r F;
    GM_STACK_CHECK(&F, 61u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_f_list_go2qg0b((void *)&F.l1, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        void * t2 = (void *)((uint8_t *)&F.l1);
        gm_b16_8 t3 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t4 = t3;
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((uint8_t *)&t4));
        gm_tftb1odc739t5 t6 = gm_f_extend_7hs67uj(t2, t5);
        gm_tftb1odc739t5 t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 1, gm_srclv3pj5es0f8o, 330, 5);
        gm_b0_1 t8 = GM_LD(gm_b0_1, (((uint8_t *)&t7) + 1));
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t9);
        memcpy((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, ((uint8_t *)&F.l1), 40);
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
inline __attribute__((aligned(4))) void gm_f_div_f0qrfbr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_div_f0qrfbr F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_tngtuiuskn938 t2;
        gm_f_div_rem_sc09dcc((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t3 = t2;
        F.l2 = GM_LD(gm_tbci5ospk5nnn, (uint8_t *)&t3 + 0);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 48);
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
inline __attribute__((aligned(4))) void gm_f_div_floor_u3mfihk(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_div_floor_u3mfihk F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tngtuiuskn938 t2;
        gm_f_div_mod_floor_eqv1ila((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t3 = t2;
        F.l2 = GM_LD(gm_tbci5ospk5nnn, (uint8_t *)&t3 + 0);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 48);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_84b0b46(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_84b0b46 F;
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 8)));
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_d914i5u(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_d914i5u F;
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_empty_slice_dheabhi(void) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_empty_slice_dheabhi F;
    void * gm_l0; /* p */
    GM_STACK_CHECK_SHALLOW(&F, 113u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        void * t2 = GM_INT_TO_PTR(((uint64_t)16ULL));
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
        GM_ST(void *, (uint8_t *)&t9, (void *)(((uint8_t *)t6 + t7 * 48)));
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
inline __attribute__((aligned(4))) uint8_t gm_f_eq_60fjn21(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_eq_60fjn21 F;
    GM_STACK_CHECK_SHALLOW(&F, 107u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t4 = ((t2 == t3));
    if (t4) {
        gm_b16_8 t5 = GM_LD(gm_b16_8, ((((uint8_t *)a0) + 8) + 0));
        gm_b16_8 t6 = t5;
        gm_b16_8 t7 = GM_LD(gm_b16_8, ((uint8_t *)&t6));
        gm_b16_8 t8 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
        gm_b16_8 t9 = t8;
        gm_b16_8 t10 = GM_LD(gm_b16_8, ((uint8_t *)&t9));
        int32_t t11 = gm_f_mag_cmp_s39veq6(t7, t10);
        uint8_t t12 = ((t11 == ((int32_t)0LL)));
        t1 = t12;
    } else t1 = 0;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_from_i128_1n0s49q(void *gm_out, void *env, gm_node *cx, gm_node *inst, __int128 a0) {
    struct gm_fr_from_i128_1n0s49q F;
    __int128 gm_l0; /* v */
    GM_STACK_CHECK(&F, 65u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        __int128 t2 = gm_l0;
        unsigned __int128 t3 = (t2 > ((__int128)(((unsigned __int128)0u << 64) | 0u)) ? (unsigned __int128)t2 - (unsigned __int128)((__int128)(((unsigned __int128)0u << 64) | 0u)) : (unsigned __int128)((__int128)(((unsigned __int128)0u << 64) | 0u)) - (unsigned __int128)t2);
        gm_f_from_u128_gl5fk0t((void *)&F.l1, (void *)NULL, cx, NULL, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        __int128 t4 = gm_l0;
        uint8_t t5 = (t4 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), t5);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) uint8_t gm_f_is_even_3kgcpjs(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_even_3kgcpjs F;
    GM_STACK_CHECK_SHALLOW(&F, 37u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = 0;
    uint64_t t2 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
    uint8_t t3 = ((t2 == ((uint64_t)0ULL)));
    if (!t3) {
        uint64_t t4 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
        if (t4 >= (uintptr_t)(t5)) gm_trap_index(t4, t5, gm_srclv3pj5es0f8o, 344, 63);
        uint32_t t6 = GM_LD(uint32_t, ((uint8_t *)GM_LD(void *, ((((uint8_t *)a0) + 8) + 0)) + t4 * 4));
        uint32_t t7 = (uint32_t)(t6 & ((uint32_t)1ULL));
        uint8_t t8 = ((t7 == ((uint32_t)0ULL)));
        t1 = t8;
    } else t1 = 1;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_8d4f9ek(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_8d4f9ek F;
    void * gm_l0; /* t */
    gm_t88rcucg1b7uk gm_l1; /* ty */
    GM_STACK_CHECK_SHALLOW(&F, 142u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    void * t1 = gm_l0;
    gm_t88rcucg1b7uk t2 = gm_l1;
    gm_t53at80pfdus3 t3;
    gm_f_kind_jcnt4hm((void *)&t3, t1, GM_CONV(gm_csguankko4lmhd, t2));
    gm_t53at80pfdus3 t4 = t3;
    uint8_t t5;
    uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t10 != ((uint8_t)0ULL)) goto pa9;
    goto po8;
pa9:;
    uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t12 != ((uint8_t)1ULL)) goto pa11;
    goto po8;
pa11:;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t4) + 0));
    if (t14 != ((uint8_t)2ULL)) goto pa13;
    goto po8;
pa13:;
    goto mn7;
po8:;
    t5 = ((uint8_t)1);
    goto me6;
mn7:;
    t5 = ((uint8_t)0);
    goto me6;
mn15:;
    gm_trap("no match arm matched the scrutinee", gm_src0ih77f43mhf2, 374, 58);
me6:;
    F.ret = t5;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_integer_ab2ohk2(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_integer_ab2ohk2 F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t1 = gm_f_bits_lr3of4i(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    uint8_t t2 = ((t1 == ((uint64_t)1ULL)));
    F.ret = t2;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_k87pp3n(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_negative_k87pp3n F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_negative_rb46l9j(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_negative_rb46l9j F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_5aept6u(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_5aept6u F;
    GM_STACK_CHECK_SHALLOW(&F, 3u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint8_t t1 = gm_f_is_zero_8tcct9o(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)));
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) uint8_t gm_f_is_zero_8tcct9o(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_is_zero_8tcct9o F;
    GM_STACK_CHECK_SHALLOW(&F, 11u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    uint64_t t1 = GM_LD(uintptr_t, ((((uint8_t *)a0) + 8) + 0) + 8);
    uint8_t t2 = ((t1 == ((uint64_t)0ULL)));
    F.ret = t2;
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
inline __attribute__((aligned(4))) void gm_f_list_0202sdh(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_0202sdh F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dheabhi();
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
inline __attribute__((aligned(4))) void gm_f_list_go2qg0b(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_go2qg0b F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_d914i5u();
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
inline __attribute__((aligned(4))) void gm_f_list_kl0t9m0(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_list_kl0t9m0 F;
    GM_STACK_CHECK(&F, 32u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_84b0b46();
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
inline __attribute__((aligned(4))) void gm_f_make_r4pbu77(void *gm_out, uint8_t a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_make_r4pbu77 F;
    uint8_t gm_l0; /* negative */
    GM_STACK_CHECK_SHALLOW(&F, 100u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    {
        gm_trqfvmjtt4sij t2 = F.l1;
        F.l2 = t2;
    }
    {
        void * t3 = (void *)((uint8_t *)&F.l2);
        gm_f_trim_8rdppmk(t3);
    }
    {
        memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
        uint8_t t4 = 0;
        uint8_t t5 = gm_l0;
        if (t5) {
            uint64_t t6 = GM_LD(uintptr_t, (((uint8_t *)&F.l2) + 0) + 8);
            uint8_t t7 = (t6 > ((uint64_t)0ULL));
            t4 = t7;
        } else t4 = 0;
        gm_trqfvmjtt4sij t8 = F.l2;
        GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, t4);
        GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t8);
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_mask_gbhl4hp(uint32_t a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_mask_gbhl4hp F;
    uint32_t gm_l0; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 157u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    unsigned __int128 t1;
    uint32_t t3 = gm_l0;
    uint8_t t4 = (t3 >= ((uint32_t)128ULL));
    if (!t4) goto ie2;
    unsigned __int128 t6 = (unsigned __int128)~((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
    unsigned __int128 t7 = t6;
    t1 = t7;
    goto ix2;
ie2:;
    uint32_t t9 = gm_l0;
    unsigned __int128 t10 = (unsigned __int128)t9;
    unsigned __int128 n11 = (unsigned __int128)t10;
    if (n11 >= 128) gm_trap_shift((uint64_t)n11, "u128", gm_srcue4ionbe9lgh, 14, 70);
    unsigned __int128 r12 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n11);
    bool o13 = (unsigned __int128)(r12 >> (int)n11) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
    if (o13) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 14, 70);
    unsigned __int128 t14 = r12;
    unsigned __int128 r15; bool o16 = __builtin_sub_overflow(t14, ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)), &r15);
    if (o16) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 14, 69);
    unsigned __int128 t17 = r15;
    unsigned __int128 t18 = t17;
    t1 = t18;
ix2:;
    F.ret = t1;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_mul_nfj5rjv(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_mul_nfj5rjv F;
    GM_STACK_CHECK(&F, 139u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    memcpy(&F.l1, a1, sizeof F.l1);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    uint8_t t2 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
    uint8_t t3 = (!(t1 == t2));
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t5 = t4;
    gm_b16_8 t6 = GM_LD(gm_b16_8, ((uint8_t *)&t5));
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l1) + 8) + 0));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_trqfvmjtt4sij t10;
    gm_f_mag_mul_if5s38k((void *)&t10, (void *)NULL, cx, NULL, t6, t9);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t3, &t10);
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
inline __attribute__((aligned(4))) void gm_f_mul_od1e7nd(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_mul_od1e7nd F;
    GM_STACK_CHECK(&F, 96u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_mul_nfj5rjv((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t1, &t2);
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
inline __attribute__((aligned(4))) void gm_f_neg_agd5c5k(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_neg_agd5c5k F;
    GM_STACK_CHECK(&F, 12u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    {
        gm_f_copy_pmkg67r((void *)&F.l1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l0)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t2 = 0;
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
        uint8_t t4 = (uint8_t)!t3;
        if (t4) {
            uint64_t t5 = GM_LD(uintptr_t, ((((uint8_t *)&F.l0) + 8) + 0) + 8);
            uint8_t t6 = (t5 > ((uint64_t)0ULL));
            t2 = t6;
        } else t2 = 0;
        GM_ST(uint8_t, (((uint8_t *)&F.l1) + 0), t2);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l1), 48);
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
inline __attribute__((aligned(4))) uint32_t gm_f_ptr_bits_glps5i7(void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_ptr_bits_glps5i7 F;
    void * gm_l0; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 44u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    void * t1 = gm_l0;
    uint64_t t2 = GM_LD(uint64_t, (((uint8_t *)t1) + 0));
    uint64_t r3; bool o4 = __builtin_mul_overflow(t2, ((uint64_t)8ULL), &r3);
    if (o4) gm_trap("integer overflow in `*`", gm_src4o48ksu43djd, 91, 49);
    uint64_t t5 = r3;
    uint32_t t6 = (uint32_t)t5;
    F.ret = t6;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_rem_ib4l1vl(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_rem_ib4l1vl F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_tngtuiuskn938 t2;
        gm_f_div_rem_sc09dcc((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t3 = t2;
        F.l2 = GM_LD(gm_tbci5ospk5nnn, (uint8_t *)&t3 + 48);
    }
    {
        memmove(gm_out, ((uint8_t *)&F.l2), 48);
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
inline __attribute__((aligned(4))) void gm_f_shl_qqecl81(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, uint64_t a1) {
    struct gm_fr_shl_qqecl81 F;
    uint64_t gm_l1; /* n */
    GM_STACK_CHECK(&F, 105u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    gm_l1 = a1;
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint64_t t5 = gm_l1;
    gm_trqfvmjtt4sij t6;
    gm_f_mag_shl_5df643p((void *)&t6, (void *)NULL, cx, NULL, t4, t5);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_make_r4pbu77((void *)&(*(gm_tbci5ospk5nnn *)gm_out), t1, &t6);
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
inline __attribute__((aligned(4))) void gm_f_sub_mler6q8(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_mler6q8 F;
    GM_STACK_CHECK(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_tbci5ospk5nnn t1;
    gm_f_mul_nfj5rjv((void *)&t1, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t2;
    gm_f_mul_nfj5rjv((void *)&t2, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t3;
    gm_f_sub_pktm5cr((void *)&t3, (void *)NULL, cx, NULL, &t1, &t2);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tbci5ospk5nnn t4;
    gm_f_mul_nfj5rjv((void *)&t4, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a0) + 48)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)a1) + 48)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_f_new_2jv6hag((void *)&(*(gm_tngtuiuskn938 *)gm_out), (void *)NULL, cx, NULL, &t3, &t4);
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
inline __attribute__((aligned(4))) void gm_f_sub_pktm5cr(void *gm_out, void *env, gm_node *cx, gm_node *inst, const void * a0, const void * a1) {
    struct gm_fr_sub_pktm5cr F;
    GM_STACK_CHECK(&F, 99u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memcpy(&F.l0, a0, sizeof F.l0);
    uint8_t t1 = GM_LD(uint8_t, (((uint8_t *)&F.l0) + 0));
    gm_b16_8 t2 = GM_LD(gm_b16_8, ((((uint8_t *)&F.l0) + 8) + 0));
    gm_b16_8 t3 = t2;
    gm_b16_8 t4 = GM_LD(gm_b16_8, ((uint8_t *)&t3));
    uint8_t t5 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
    uint8_t t6 = (uint8_t)!t5;
    gm_b16_8 t7 = GM_LD(gm_b16_8, ((((uint8_t *)a1) + 8) + 0));
    gm_b16_8 t8 = t7;
    gm_b16_8 t9 = GM_LD(gm_b16_8, ((uint8_t *)&t8));
    gm_f_signed_add_ek0dpmo((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t1, t4, t6, t9);
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
inline __attribute__((aligned(4))) gm_b16_8 gm_f_to_decimal_8bshn5b(void *env, gm_node *cx, gm_node *inst, const void * a0) {
    struct gm_fr_to_decimal_8bshn5b F;
    GM_STACK_CHECK(&F, 113u);
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
        gm_tabb710d9mq2e t6 = t5;
        if (((((uint8_t *)&t6))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t6) + 8, gm_srclv3pj5es0f8o, 624, 12);
        gm_b16_8 t7 = GM_LD(gm_b16_8, (((uint8_t *)&t6) + 8));
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
inline __attribute__((aligned(4))) void gm_f_to_u128_haka99r(void *gm_out, const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u128_haka99r F;
    GM_STACK_CHECK_SHALLOW(&F, 34u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = GM_LD(uint8_t, (((uint8_t *)a0) + 0));
        if (!t3) goto ie2;
        {
            gm_t9p38b040grms t5;
            memset(&t5, 0, sizeof t5);
            uint8_t t6 = 0;
            F.ret = t5;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        gm_f_mag_to_u128_f843dal((void *)&(*(gm_t9p38b040grms *)gm_out), &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        goto gm_fdone;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) gm_tf5srh4mp4b3i gm_f_to_u64_m01t1da(const void * a0) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_to_u64_m01t1da F;
    unsigned __int128 gm_l1; /* v */
    GM_STACK_CHECK_SHALLOW(&F, 234u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        gm_t9p38b040grms t2;
        gm_f_to_u128_haka99r((void *)&t2, &(*(gm_tbci5ospk5nnn *)((uint8_t *)a0)));
        gm_t9p38b040grms t3 = t2;
        unsigned __int128 t4;
        if (((((uint8_t *)&t3))[16] != 0)) {
            unsigned __int128 t5 = GM_LD(unsigned __int128, (((uint8_t *)&t3) + 0));
            t4 = t5;
        } else {
            gm_tf5srh4mp4b3i t6;
            memset(&t6, 0, sizeof t6);
            uint8_t t7 = 0;
            F.ret = t6;
            goto gm_fret;
        }
        gm_l1 = t4;
    }
    {
        unsigned __int128 t8 = gm_l1;
        gm_tf5srh4mp4b3i t9;
        uint64_t t10 = (uint64_t)t8;
        gm_tf5srh4mp4b3i t11;
        GM_ST(uint8_t, (uint8_t *)&t11 + 9, 0);
        GM_ST(uint16_t, (uint8_t *)&t11 + 10, 0);
        GM_ST(uint32_t, (uint8_t *)&t11 + 12, 0);
        GM_ST(uint64_t, ((uint8_t *)&t11), t10);
        ((uint8_t *)&t11)[8] = 1;
        gm_tf5srh4mp4b3i t12;
        memset(&t12, 0, sizeof t12);
        t9 = ((unsigned __int128)(t8) <= (((unsigned __int128)0ULL << 64) | 18446744073709551615ULL)) ? t11 : t12;
        uint8_t t13 = 0;
        F.ret = t9;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_unit_value_evae6mr(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_unit_value_evae6mr F;
    GM_STACK_CHECK(&F, 120u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_b16_8 t1 = gm_f_empty_slice_dheabhi();
    gm_tnpifv20irnph t2;
    GM_ST(gm_b16_8, ((uint8_t *)&t2 + 0), t1);
    gm_tq7s98ihonkju t3 = gm_f_new_lcff781((void *)NULL, cx, NULL, t2, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kequv58cbtg)));
    if (gm_unw.kind) {
        goto gm_lad;
    }
    gm_tq7s98ihonkju t4 = t3;
    if (((((uint8_t *)&t4))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t4) + 8, gm_src9vg8k4mbj38f, 133, 49);
    void * t5 = GM_LD(void *, (((uint8_t *)&t4) + 8));
    gm_t6evimt84lndq t6;
    memset(&t6, 0, sizeof t6);
    GM_ST(uint8_t, ((uint8_t *)&t6 + 0), ((uint8_t)3ULL));
    GM_ST(void *, ((uint8_t *)&t6 + 16), t5);
    F.ret = t6;
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
inline __attribute__((aligned(4))) void gm_f_unsupported_2seq49l(void *gm_out, void * a0, gm_cseahvg8v7p0vs a1, gm_b16_8 a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_unsupported_2seq49l F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_b16_8 gm_l2; /* what */
    GM_STACK_CHECK_SHALLOW(&F, 192u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_tl874t2atnlh3, a1);
    gm_l2 = a2;
    gm_tbgd14esf0jvi t1;
    memset((uint8_t *)&t1 + 12, 0, 4);
    gm_tl874t2atnlh3 t2 = gm_l1;
    GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t1 + 0, t2);
    gm_b16_8 t3 = gm_l2;
    GM_ST(gm_b16_8, (uint8_t *)&t1 + 16, t3);
    gm_t39uai1ajk3qv t4;
    memset(&t4, 0, sizeof t4);
    GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)5ULL));
    GM_ST(gm_tbgd14esf0jvi, ((uint8_t *)&t4 + 16), t1);
    F.ret = t4;
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
inline __attribute__((aligned(4))) unsigned __int128 gm_f_wrap_bt8r149(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_wrap_bt8r149 F;
    void * gm_l0; /* m */
    unsigned __int128 gm_l1; /* bits */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK_SHALLOW(&F, 140u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    unsigned __int128 t1 = gm_l1;
    void * t2 = gm_l0;
    gm_t88rcucg1b7uk t3 = gm_l2;
    gm_ttovhfee8mf28 t4 = gm_f_int_info_2r9qb5n(t2, GM_CONV(gm_csguankko4lmhd, t3));
    gm_ttovhfee8mf28 t5 = t4;
    uint32_t t6 = GM_LD(uint32_t, (((uint8_t *)&t5) + 4));
    unsigned __int128 t7 = gm_f_mask_gbhl4hp(t6);
    unsigned __int128 t8 = (unsigned __int128)(t1 & t7);
    F.ret = t8;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
inline __attribute__((aligned(4))) void gm_f_zero_s25l990(void *gm_out, void *env, gm_node *cx, gm_node *inst) {
    struct gm_fr_zero_s25l990 F;
    GM_STACK_CHECK(&F, 40u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    memset((uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 1, 0, 7);
    gm_trqfvmjtt4sij t1;
    gm_f_list_go2qg0b((void *)&t1, (void *)NULL, cx, NULL);
    if (gm_unw.kind) {
        goto gm_lad;
    }
    GM_ST(uint8_t, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 0, ((uint8_t)0));
    GM_ST(gm_trqfvmjtt4sij, (uint8_t *)&(*(gm_tbci5ospk5nnn *)gm_out) + 8, t1);
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
extern __attribute__((aligned(4))) double gm_f_big_float_jmiufgl(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1);
inline __attribute__((aligned(4))) void gm_f_big_of_1cq0qao(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, unsigned __int128 a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_big_of_1cq0qao F;
    void * gm_l0; /* m */
    unsigned __int128 gm_l1; /* bits */
    gm_t88rcucg1b7uk gm_l2; /* t */
    GM_STACK_CHECK(&F, 117u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_ttovhfee8mf28 t5 = gm_f_int_info_2r9qb5n(t3, GM_CONV(gm_csguankko4lmhd, t4));
        gm_ttovhfee8mf28 t6 = t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
        if (!t7) goto ie2;
        {
            void * t9 = gm_l0;
            unsigned __int128 t10 = gm_l1;
            gm_t88rcucg1b7uk t11 = gm_l2;
            __int128 t12 = gm_f_int_value_6hmh8tj(t9, t10, GM_CONV(gm_csguankko4lmhd, t11));
            gm_f_from_i128_1n0s49q((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t12);
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
        unsigned __int128 t13 = gm_l1;
        gm_f_from_u128_gl5fk0t((void *)&(*(gm_tbci5ospk5nnn *)gm_out), (void *)NULL, cx, NULL, t13);
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
extern __attribute__((aligned(4))) void gm_f_bigint_i128_p6m54kv(void *gm_out, const void * a0);
__attribute__((aligned(4))) gm_ta24rd13gupia gm_f_bigint_i128_p6m54kv_c(gm_tbci5ospk5nnn a0) { gm_ta24rd13gupia gm_r; gm_f_bigint_i128_p6m54kv(&gm_r, &a0); return gm_r; }
__attribute__((aligned(4))) void gm_f_binary_fmoblrg(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_binary_fmoblrg F;
    void * gm_l0; /* m */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    gm_t88rcucg1b7uk gm_l4; /* lt */
    gm_t88rcucg1b7uk gm_l5; /* rt */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_tlcm4lvu3g1a8 gm_l7; /* lp */
    void * gm_l8; /* xa */
    gm_b16_8 gm_l10; /* <tmp> */
    uint64_t gm_l11; /* <tmp> */
    uint64_t gm_l12; /* i */
    void * gm_l15; /* ya */
    gm_t88rcucg1b7uk gm_l16; /* rlane */
    GM_STACK_CHECK(&F, 1992u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l4;
        gm_tb7u19loo3j87 t7;
        gm_f_vector_parts_crrfopm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_tb7u19loo3j87 t8 = t7;
        if (!((((uint8_t *)&t8))[16] != 0)) goto ie2;
        gm_tlcm4lvu3g1a8 t9 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t8));
        gm_l7 = t9;
        {
            uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t12 != ((uint8_t)3ULL)) goto ie11;
            void * t13 = GM_LD(void *, (((uint8_t *)a2) + 16));
            gm_l8 = t13;
            {
                gm_f_list_0202sdh((void *)&F.l9, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    void * t16 = gm_l8;
                    gm_b16_8 t17 = GM_LD(gm_b16_8, (((uint8_t *)t16) + 0));
                    gm_b16_8 t18 = t17;
                    gm_b16_8 t19 = GM_LD(gm_b16_8, ((uint8_t *)&t18));
                    gm_l10 = t19;
                }
                {
                    gm_l11 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t21 = gm_l11;
                        uint64_t t22 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                        uint8_t t23 = (t21 < t22);
                        if (!t23) goto lb20;
                        {
                            gm_thf8as07kj3uj t26;
                            memset((uint8_t *)&t26 + 8, 0, 8);
                            uint64_t t27 = gm_l11;
                            GM_ST(uint64_t, (uint8_t *)&t26 + 0, t27);
                            uint64_t t28 = gm_l11;
                            uint64_t t29 = (uintptr_t)t28;
                            uint64_t t30 = GM_LD(uintptr_t, ((uint8_t *)&gm_l10) + 8);
                            memcpy((uint8_t *)&t26 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l10)) + t29 * 48), 48);
                            gm_thf8as07kj3uj t31 = t26;
                            uint64_t t34 = GM_LD(uint64_t, (((uint8_t *)&t31) + 0));
                            gm_l12 = t34;
                            gm_t6evimt84lndq t35 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t31) + 16));
                            F.l13 = t35;
                            {
                                gm_t6evimt84lndq t37 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)a3));
                                F.l14 = t37;
                            }
                            {
                                uint8_t t39 = GM_LD(uint8_t, (((uint8_t *)a3) + 0));
                                if (t39 != ((uint8_t)3ULL)) goto ie38;
                                void * t40 = GM_LD(void *, (((uint8_t *)a3) + 16));
                                gm_l15 = t40;
                                {
                                    uint64_t t42 = gm_l12;
                                    uint64_t t43 = (uintptr_t)t42;
                                    void * t44 = gm_l15;
                                    uint64_t t45 = GM_LD(uintptr_t, (((uint8_t *)t44) + 0) + 8);
                                    if (t43 >= (uintptr_t)(t45)) gm_trap_index(t43, t45, gm_srcue4ionbe9lgh, 113, 36);
                                    gm_t6evimt84lndq t46 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, (((uint8_t *)t44) + 0)) + t43 * 48));
                                    F.l14 = t46;
                                }
                                goto ix38;
ie38:;
ix38:;
                            }
                            {
                                gm_t88rcucg1b7uk t47;
                                void * t49 = gm_l0;
                                void * t50 = GM_LD(void *, (((uint8_t *)t49) + 0));
                                void * t51 = (void *)(((uint8_t *)t50) + 8);
                                gm_t88rcucg1b7uk t52 = gm_l5;
                                gm_tb7u19loo3j87 t53;
                                gm_f_vector_parts_crrfopm((void *)&t53, t51, GM_CONV(gm_csguankko4lmhd, t52));
                                gm_tb7u19loo3j87 t54 = t53;
                                uint8_t t55;
                                if (((((uint8_t *)&t54))[16] != 0)) goto mn57;
                                t55 = ((uint8_t)0);
                                goto me56;
mn57:;
                                t55 = ((uint8_t)1);
                                goto me56;
mn58:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 114, 25);
me56:;
                                if (!t55) goto ie48;
                                gm_t88rcucg1b7uk t60 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                                gm_t88rcucg1b7uk t61 = t60;
                                t47 = t61;
                                goto ix48;
ie48:;
                                gm_t88rcucg1b7uk t63 = gm_l5;
                                gm_t88rcucg1b7uk t64 = t63;
                                t47 = t64;
ix48:;
                                gm_l16 = t47;
                            }
                            {
                                void * t65 = (void *)((uint8_t *)&F.l9);
                                void * t66 = gm_l0;
                                gm_ts6oqppjh0ba3 t67 = gm_l1;
                                gm_t88rcucg1b7uk t68 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&gm_l7) + 8));
                                gm_t88rcucg1b7uk t69 = gm_l16;
                                gm_tl874t2atnlh3 t70 = gm_l6;
                                gm_ttk92ufj2f6eh t71;
                                gm_f_scalar_binary_bvc65kt((void *)&t71, (void *)NULL, cx, NULL, t66, t67, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l13)), &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l14)), t68, t69, t70);
                                if (gm_unw.kind) {
                                    goto gm_ll24;
                                }
                                gm_ttk92ufj2f6eh t72 = t71;
                                if (((((uint8_t *)&t72))[0] != 0)) {
                                    gm_t39uai1ajk3qv t73 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t72) + 16));
                                    gm_ttk92ufj2f6eh t74;
                                    GM_ST(uint8_t, (uint8_t *)&t74 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t74 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t74 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t74 + 8, 0);
                                    ((uint8_t *)&t74)[0] = 1;
                                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t74 + 16), t73);
                                    F.ret = t74;
                                    goto gm_fret;
                                }
                                gm_t6evimt84lndq t75 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t72) + 16));
                                gm_tftb1odc739t5 t76 = gm_f_push_0pgacsj(t65, &t75);
                                gm_tftb1odc739t5 t77 = t76;
                                if (((((uint8_t *)&t77))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t77) + 1, gm_srcue4ionbe9lgh, 115, 13);
                                gm_b0_1 t78 = GM_LD(gm_b0_1, (((uint8_t *)&t77) + 1));
                            }
                            goto me32;
mn33:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 111, 9);
me32:;
                        }
lc20:;
                        {
                            uint64_t t80 = gm_l11;
                            uint64_t r81; bool o82 = __builtin_add_overflow(t80, ((uint64_t)1ULL), &r81);
                            uint64_t t83 = r81;
                            gm_l11 = t83;
                        }
                    }
gm_ll24:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb20; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc20; }
                    }
                    goto gm_lad;
lb20:;
                }
            }
            {
                gm_b16_8 t84 = GM_LD(gm_b16_8, (((uint8_t *)&F.l9) + 0));
                gm_tnpifv20irnph t85;
                GM_ST(gm_b16_8, ((uint8_t *)&t85 + 0), t84);
                gm_tq7s98ihonkju t86 = gm_f_new_lcff781((void *)NULL, cx, NULL, t85, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kha5d1oq7pb)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tq7s98ihonkju t87 = t86;
                if (((((uint8_t *)&t87))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t87) + 8, gm_srcue4ionbe9lgh, 117, 21);
                void * t88 = GM_LD(void *, (((uint8_t *)&t87) + 8));
                gm_t6evimt84lndq t89;
                memset(&t89, 0, sizeof t89);
                GM_ST(uint8_t, ((uint8_t *)&t89 + 0), ((uint8_t)3ULL));
                GM_ST(void *, ((uint8_t *)&t89 + 16), t88);
                gm_ttk92ufj2f6eh t90;
                memset(&t90, 0, sizeof t90);
                ((uint8_t *)&t90)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t90 + 16), t89);
                gm_ttk92ufj2f6eh t91 = t90;
                uint8_t t92 = ((((uint8_t *)&t91))[0] != 0);
                F.ret = t90;
                goto gm_fret;
            }
            goto ix11;
ie11:;
            {
                void * t94 = gm_l0;
                gm_tl874t2atnlh3 t95 = gm_l6;
                gm_b16_8 t96;
                GM_ST(void *, (uint8_t *)&t96, (void *)(gm_s0dggtblbl7jq));
                GM_ST(uintptr_t, (uint8_t *)&t96 + 8, (uintptr_t)(29));
                gm_t39uai1ajk3qv t97;
                gm_f_unsupported_2seq49l((void *)&t97, t94, GM_CONV(gm_cseahvg8v7p0vs, t95), t96);
                gm_ttk92ufj2f6eh t98;
                GM_ST(uint8_t, (uint8_t *)&t98 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t98 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t98 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t98 + 8, 0);
                ((uint8_t *)&t98)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t98 + 16), t97);
                gm_ttk92ufj2f6eh t99 = t98;
                uint8_t t100 = ((((uint8_t *)&t99))[0] != 0);
                F.ret = t98;
                goto gm_fret;
            }
ix11:;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        void * t101 = gm_l0;
        gm_ts6oqppjh0ba3 t102 = gm_l1;
        gm_t88rcucg1b7uk t103 = gm_l4;
        gm_t88rcucg1b7uk t104 = gm_l5;
        gm_tl874t2atnlh3 t105 = gm_l6;
        gm_f_scalar_binary_bvc65kt((void *)&(*(gm_ttk92ufj2f6eh *)gm_out), (void *)NULL, cx, NULL, t101, t102, &(*(gm_t6evimt84lndq *)((uint8_t *)a2)), &(*(gm_t6evimt84lndq *)((uint8_t *)a3)), t103, t104, t105);
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
extern __attribute__((aligned(4))) uint8_t gm_f_bit_s517clu(void *env, gm_node *cx, gm_node *inst, uint32_t a0);
__attribute__((aligned(4))) void gm_f_builtin_psq6ql3(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tvdg94bk7aa22 a1, gm_b16_8 a2, void * a3) {
    struct gm_fr_builtin_psq6ql3 F;
    void * gm_l0; /* m */
    gm_tvdg94bk7aa22 gm_l1; /* op */
    gm_b16_8 gm_l2; /* args */
    void * gm_l3; /* e */
    void * gm_l4; /* tys */
    gm_tl874t2atnlh3 gm_l5; /* span */
    uint8_t gm_l6; /* ok */
    gm_b16_8 gm_l8; /* text */
    gm_b16_8 gm_l9; /* vals */
    gm_t88rcucg1b7uk gm_l11; /* at */
    unsigned __int128 gm_l12; /* bits */
    uint8_t gm_l13; /* b */
    double gm_l14; /* f */
    uint32_t gm_l15; /* i */
    unsigned __int128 gm_l17; /* bits */
    double gm_l18; /* f */
    gm_t88rcucg1b7uk gm_l20; /* t */
    unsigned __int128 gm_l21; /* bits */
    double gm_l24; /* f */
    unsigned __int128 gm_l29; /* bits */
    double gm_l30; /* f */
    unsigned __int128 gm_l33; /* a */
    unsigned __int128 gm_l34; /* b */
    int32_t gm_l35; /* c */
    double gm_l36; /* a */
    double gm_l37; /* b */
    unsigned __int128 gm_l38; /* v */
    unsigned __int128 gm_l39; /* lo */
    unsigned __int128 gm_l40; /* hi */
    unsigned __int128 gm_l42; /* bits */
    double gm_l46; /* f */
    unsigned __int128 gm_l47; /* a */
    unsigned __int128 gm_l48; /* b */
    gm_t9qkh0rsooc8d gm_l50; /* flavor */
    unsigned __int128 gm_l51; /* base */
    unsigned __int128 gm_l52; /* n */
    gm_t88rcucg1b7uk gm_l54; /* t */
    uint8_t gm_l56; /* fits */
    gm_b16_8 gm_l57; /* text */
    double gm_l58; /* f */
    double gm_l59; /* f */
    double gm_l60; /* f */
    double gm_l61; /* f */
    uint32_t gm_l62; /* i */
    uint64_t gm_l64; /* n */
    gm_tab260m0n3sqj gm_l65; /* start */
    void * gm_l66; /* a */
    gm_to6glrduves6o gm_l67; /* sl */
    unsigned __int128 gm_l68; /* bits */
    __int128 gm_l69; /* i */
    uint64_t gm_l70; /* k */
    gm_tkaapcii86udk gm_l71; /* pl */
    gm_tkaapcii86udk gm_l72; /* pl */
    gm_tkaapcii86udk gm_l73; /* pl */
    void * gm_l74; /* a */
    gm_to6glrduves6o gm_l76; /* d */
    gm_to6glrduves6o gm_l77; /* s */
    uint64_t gm_l79; /* <tmp> */
    uint64_t gm_l80; /* <tmp> */
    uint64_t gm_l81; /* k */
    gm_b16_8 gm_l82; /* <tmp> */
    uint64_t gm_l83; /* <tmp> */
    uint64_t gm_l84; /* k */
    void * gm_l86; /* inner */
    gm_tkaapcii86udk gm_l88; /* pl */
    uint64_t gm_l89; /* elem */
    gm_t88rcucg1b7uk gm_l90; /* t */
    gm_t88rcucg1b7uk gm_l91; /* t */
    uint64_t gm_l92; /* index */
    uint64_t gm_l93; /* base */
    unsigned __int128 gm_l94; /* bits */
    double gm_l96; /* f */
    unsigned __int128 gm_l97; /* raw */
    gm_ttovhfee8mf28 gm_l98; /* info */
    uint32_t gm_l99; /* n */
    gm_closure gm_l101; /* bit */
    unsigned __int128 gm_l102; /* r */
    uint32_t gm_l103; /* c */
    uint32_t gm_l104; /* c */
    uint32_t gm_l105; /* c */
    uint32_t gm_l106; /* <tmp> */
    uint32_t gm_l107; /* <tmp> */
    uint32_t gm_l108; /* i */
    uint32_t gm_l109; /* <tmp> */
    uint32_t gm_l110; /* <tmp> */
    uint32_t gm_l111; /* i */
    uint32_t gm_l112; /* <tmp> */
    uint32_t gm_l113; /* <tmp> */
    uint32_t gm_l114; /* i */
    unsigned __int128 gm_l115; /* amount_bits */
    uint32_t gm_l117; /* k */
    uint32_t gm_l118; /* <tmp> */
    uint32_t gm_l119; /* <tmp> */
    uint32_t gm_l120; /* i */
    gm_tvdg94bk7aa22 gm_l121; /* other */
    GM_STACK_CHECK(&F, 47276u);
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
        void * t5 = gm_l3;
        gm_tl874t2atnlh3 t6 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t5) + 72));
        gm_l5 = t6;
    }
    {
        uint8_t t12 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t12) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma10; case ((uint8_t)4ULL): goto ma10; case ((uint8_t)5ULL): goto ma10; case ((uint8_t)6ULL): goto ma10; case ((uint8_t)7ULL): goto ma10; case ((uint8_t)8ULL): goto ma10; case ((uint8_t)9ULL): goto ma10; case ((uint8_t)10ULL): goto ma10; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma10; case ((uint8_t)14ULL): goto ma10; case ((uint8_t)15ULL): goto ma10; case ((uint8_t)16ULL): goto ma10; case ((uint8_t)17ULL): goto ma10; case ((uint8_t)18ULL): goto ma10; case ((uint8_t)19ULL): goto ma10; case ((uint8_t)20ULL): goto ma10; case ((uint8_t)21ULL): goto ma10; case ((uint8_t)22ULL): goto ma10; case ((uint8_t)23ULL): goto ma10; case ((uint8_t)24ULL): goto ma10; case ((uint8_t)25ULL): goto ma10; case ((uint8_t)26ULL): goto ma10; case ((uint8_t)27ULL): goto ma10; case ((uint8_t)28ULL): goto ma10; case ((uint8_t)29ULL): goto ma10; case ((uint8_t)30ULL): goto ma10; case ((uint8_t)31ULL): goto ma10; case ((uint8_t)32ULL): goto ma10; case ((uint8_t)33ULL): goto ma10; case ((uint8_t)34ULL): goto ma10; case ((uint8_t)35ULL): goto ma10; case ((uint8_t)36ULL): goto ma10; case ((uint8_t)37ULL): goto ma10; case ((uint8_t)38ULL): goto ma10; case ((uint8_t)39ULL): goto ma10; case ((uint8_t)40ULL): goto ma10; case ((uint8_t)41ULL): goto ma10; case ((uint8_t)42ULL): goto ma10; case ((uint8_t)43ULL): goto ma10; case ((uint8_t)44ULL): goto ma10; case ((uint8_t)45ULL): goto ma10; case ((uint8_t)46ULL): goto ma10; case ((uint8_t)47ULL): goto ma10; case ((uint8_t)48ULL): goto ma10; case ((uint8_t)49ULL): goto ma10; case ((uint8_t)50ULL): goto ma10; case ((uint8_t)51ULL): goto ma10; case ((uint8_t)52ULL): goto ma10; case ((uint8_t)53ULL): goto ma10; case ((uint8_t)54ULL): goto ma10; case ((uint8_t)55ULL): goto ma10; case ((uint8_t)56ULL): goto ma10; case ((uint8_t)57ULL): goto ma10; case ((uint8_t)58ULL): goto ma10; case ((uint8_t)59ULL): goto ma10; case ((uint8_t)60ULL): goto ma10; case ((uint8_t)61ULL): goto ma9; case ((uint8_t)62ULL): goto ma10; case ((uint8_t)63ULL): goto ma8; case ((uint8_t)64ULL): goto ma10; case ((uint8_t)65ULL): goto ma10; case ((uint8_t)66ULL): goto ma10; case ((uint8_t)67ULL): goto ma10; case ((uint8_t)68ULL): goto ma10; case ((uint8_t)69ULL): goto ma10; case ((uint8_t)70ULL): goto ma10; case ((uint8_t)71ULL): goto ma10; case ((uint8_t)72ULL): goto ma10; case ((uint8_t)73ULL): goto ma10; case ((uint8_t)74ULL): goto ma10; case ((uint8_t)75ULL): goto ma10; case ((uint8_t)76ULL): goto ma10; case ((uint8_t)77ULL): goto ma10; case ((uint8_t)78ULL): goto ma10; case ((uint8_t)79ULL): goto ma10; case ((uint8_t)80ULL): goto ma10; case ((uint8_t)81ULL): goto ma10; default: goto ma10; }
ma8:;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t14 != ((uint8_t)63ULL)) goto mn13;
        void * t15 = gm_l0;
        gm_tl874t2atnlh3 t16 = gm_l5;
        gm_b16_8 t17;
        GM_ST(void *, (uint8_t *)&t17, (void *)(gm_sd3b7a2bnmpef));
        GM_ST(uintptr_t, (uint8_t *)&t17 + 8, (uintptr_t)(24));
        gm_t39uai1ajk3qv t18;
        gm_f_trap_jftbss6((void *)&t18, (void *)NULL, cx, NULL, t15, t16, t17);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttk92ufj2f6eh t19;
        GM_ST(uint8_t, (uint8_t *)&t19 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t19 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t19 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t19 + 8, 0);
        ((uint8_t *)&t19)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t19 + 16), t18);
        gm_ttk92ufj2f6eh t20 = t19;
        uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
        F.ret = t19;
        goto gm_fret;
        goto me7;
mn13:;
ma9:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t23 != ((uint8_t)61ULL)) goto mn22;
        {
            void * t26 = gm_l0;
            uint64_t t27 = (uintptr_t)((uint64_t)0ULL);
            uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t27 >= (uintptr_t)(t28)) gm_trap_index(t27, t28, gm_srcue4ionbe9lgh, 358, 39);
            void * t29 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t27 * 88);
            gm_ttk92ufj2f6eh t30;
            gm_f_eval_2vbtp1v((void *)&t30, (void *)NULL, cx, NULL, t26, t29);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttk92ufj2f6eh t31 = t30;
            if (((((uint8_t *)&t31))[0] != 0)) {
                gm_t39uai1ajk3qv t32 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t31) + 16));
                gm_ttk92ufj2f6eh t33;
                GM_ST(uint8_t, (uint8_t *)&t33 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t33 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t33 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t33 + 8, 0);
                ((uint8_t *)&t33)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t33 + 16), t32);
                F.ret = t33;
                goto gm_fret;
            }
            gm_t6evimt84lndq t34 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t31) + 16));
            gm_t6evimt84lndq t35 = t34;
            uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&t35) + 0));
            if (t36 != ((uint8_t)2ULL)) goto ie25;
            uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&t35) + 16));
            gm_l6 = t37;
            {
                void * t39 = gm_l0;
                uint64_t t40 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t41 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t40 >= (uintptr_t)(t41)) gm_trap_index(t40, t41, gm_srcue4ionbe9lgh, 359, 32);
                void * t42 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t40 * 88);
                gm_ttk92ufj2f6eh t43;
                gm_f_eval_2vbtp1v((void *)&t43, (void *)NULL, cx, NULL, t39, t42);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_ttk92ufj2f6eh t44 = t43;
                if (((((uint8_t *)&t44))[0] != 0)) {
                    gm_t39uai1ajk3qv t45 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t44) + 16));
                    gm_ttk92ufj2f6eh t46;
                    GM_ST(uint8_t, (uint8_t *)&t46 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t46 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t46 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t46 + 8, 0);
                    ((uint8_t *)&t46)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t46 + 16), t45);
                    F.ret = t46;
                    goto gm_fret;
                }
                gm_t6evimt84lndq t47 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t44) + 16));
                F.l7 = t47;
            }
            {
                uint8_t t49 = gm_l6;
                if (!t49) goto ie48;
                {
                    gm_t6evimt84lndq t51;
                    gm_f_unit_value_evae6mr((void *)&t51, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttk92ufj2f6eh t52;
                    memset(&t52, 0, sizeof t52);
                    ((uint8_t *)&t52)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t52 + 16), t51);
                    gm_ttk92ufj2f6eh t53 = t52;
                    uint8_t t54 = ((((uint8_t *)&t53))[0] != 0);
                    F.ret = t52;
                    goto gm_fret;
                }
                goto ix48;
ie48:;
ix48:;
            }
            {
                void * t55 = gm_l0;
                gm_th2lqbo77pvsc t56;
                gm_f_string_of_plna4gu((void *)&t56, (void *)NULL, cx, NULL, t55, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l7)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t57 = t56;
                gm_b16_8 t58;
                if (((((uint8_t *)&t57))[16] != 0)) {
                    gm_b16_8 t59 = GM_LD(gm_b16_8, (((uint8_t *)&t57) + 0));
                    t58 = t59;
                } else {
                    void * t60 = gm_l0;
                    gm_tl874t2atnlh3 t61 = gm_l5;
                    gm_b16_8 t62;
                    GM_ST(void *, (uint8_t *)&t62, (void *)(gm_smqhtg33814q9));
                    GM_ST(uintptr_t, (uint8_t *)&t62 + 8, (uintptr_t)(22));
                    gm_t39uai1ajk3qv t63;
                    gm_f_unsupported_2seq49l((void *)&t63, t60, GM_CONV(gm_cseahvg8v7p0vs, t61), t62);
                    gm_ttk92ufj2f6eh t64;
                    GM_ST(uint8_t, (uint8_t *)&t64 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t64 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t64 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t64 + 8, 0);
                    ((uint8_t *)&t64)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t64 + 16), t63);
                    gm_ttk92ufj2f6eh t65 = t64;
                    uint8_t t66 = ((((uint8_t *)&t65))[0] != 0);
                    F.ret = t64;
                    goto gm_fret;
                }
                gm_l8 = t58;
            }
            {
                void * t67 = gm_l0;
                gm_tl874t2atnlh3 t68 = gm_l5;
                gm_b16_8 t69;
                gm_b16_8 t71 = gm_l8;
                gm_b16_8 t72 = t71;
                gm_b16_8 t73 = GM_LD(gm_b16_8, ((uint8_t *)&t72));
                gm_b16_8 t74 = t73;
                uint8_t t75 = (((GM_LD(uintptr_t, ((uint8_t *)&t74) + 8) == 16) && memcmp(GM_LD(void *, ((uint8_t *)&t74)), gm_spi8n2atkg603, 16) == 0));
                if (!t75) goto ie70;
                gm_b16_8 t77 = gm_l8;
                gm_b16_8 t78 = t77;
                gm_b16_8 t79 = t78;
                gm_b16_8 t80 = GM_LD(gm_b16_8, ((uint8_t *)&t79));
                t69 = t80;
                goto ix70;
ie70:;
                gm_b16_8 t82 = gm_l8;
                gm_tnpifv20irnph t83;
                GM_ST(gm_b16_8, ((uint8_t *)&t83 + 0), t82);
                gm_tabb710d9mq2e t84;
                gm_f_format_u0vfg08((void *)&t84, (void *)NULL, cx, NULL, t83);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t85 = t84;
                if (((((uint8_t *)&t85))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t85) + 8, gm_srcue4ionbe9lgh, 362, 80);
                gm_b16_8 t86 = GM_LD(gm_b16_8, (((uint8_t *)&t85) + 8));
                gm_b16_8 t87 = t86;
                gm_b16_8 t88 = t87;
                gm_b16_8 t89 = GM_LD(gm_b16_8, ((uint8_t *)&t88));
                t69 = t89;
ix70:;
                gm_t39uai1ajk3qv t90;
                gm_f_trap_jftbss6((void *)&t90, (void *)NULL, cx, NULL, t67, t68, t69);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_ttk92ufj2f6eh t91;
                GM_ST(uint8_t, (uint8_t *)&t91 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t91 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t91 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t91 + 8, 0);
                ((uint8_t *)&t91)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t91 + 16), t90);
                gm_ttk92ufj2f6eh t92 = t91;
                uint8_t t93 = ((((uint8_t *)&t92))[0] != 0);
                F.ret = t91;
                goto gm_fret;
            }
            goto ix25;
ie25:;
            {
                void * t95 = gm_l0;
                gm_tl874t2atnlh3 t96 = gm_l5;
                gm_b16_8 t97;
                GM_ST(void *, (uint8_t *)&t97, (void *)(gm_s2kubr08sqj72));
                GM_ST(uintptr_t, (uint8_t *)&t97 + 8, (uintptr_t)(14));
                gm_t39uai1ajk3qv t98;
                gm_f_unsupported_2seq49l((void *)&t98, t95, GM_CONV(gm_cseahvg8v7p0vs, t96), t97);
                gm_ttk92ufj2f6eh t99;
                GM_ST(uint8_t, (uint8_t *)&t99 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t99 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t99 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t99 + 8, 0);
                ((uint8_t *)&t99)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t99 + 16), t98);
                gm_ttk92ufj2f6eh t100 = t99;
                uint8_t t101 = ((((uint8_t *)&t100))[0] != 0);
                F.ret = t99;
                goto gm_fret;
            }
ix25:;
        }
        goto me7;
mn22:;
ma10:;
        goto me7;
mn102:;
ma11:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 355, 5);
me7:;
    }
    {
        void * t104 = gm_l0;
        gm_b16_8 t105 = gm_l2;
        gm_t65n7r6ql4pgd t106;
        gm_f_args_6f16vj1((void *)&t106, (void *)NULL, cx, NULL, t104, t105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t65n7r6ql4pgd t107 = t106;
        if (((((uint8_t *)&t107))[0] != 0)) {
            gm_t39uai1ajk3qv t108 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t107) + 16));
            gm_ttk92ufj2f6eh t109;
            GM_ST(uint8_t, (uint8_t *)&t109 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t109 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t109 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t109 + 8, 0);
            ((uint8_t *)&t109)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t109 + 16), t108);
            F.ret = t109;
            goto gm_fret;
        }
        gm_b16_8 t110 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 16));
        gm_l9 = t110;
    }
    {
        gm_t6evimt84lndq t111;
        uint64_t t113 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        uint8_t t114 = (t113 > ((uint64_t)0ULL));
        if (!t114) goto ie112;
        uint64_t t116 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t117 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
        if (t116 >= (uintptr_t)(t117)) gm_trap_index(t116, t117, gm_srcue4ionbe9lgh, 367, 28);
        gm_t6evimt84lndq t118 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t116 * 48));
        gm_t6evimt84lndq t119 = t118;
        t111 = t119;
        goto ix112;
ie112:;
        gm_t6evimt84lndq t121;
        memset(&t121, 0, sizeof t121);
        GM_ST(uint8_t, ((uint8_t *)&t121 + 0), ((uint8_t)6ULL));
        gm_t6evimt84lndq t122 = t121;
        t111 = t122;
ix112:;
        F.l10 = t111;
    }
    {
        gm_t88rcucg1b7uk t123;
        uint64_t t125 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        uint8_t t126 = (t125 > ((uint64_t)0ULL));
        if (!t126) goto ie124;
        uint64_t t128 = (uintptr_t)((uint64_t)0ULL);
        uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
        if (t128 >= (uintptr_t)(t129)) gm_trap_index(t128, t129, gm_srcue4ionbe9lgh, 368, 29);
        gm_t88rcucg1b7uk t130 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t128 * 88) + 0));
        gm_t88rcucg1b7uk t131 = t130;
        t123 = t131;
        goto ix124;
ie124:;
        void * t133 = gm_l3;
        gm_t88rcucg1b7uk t134 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t133) + 0));
        gm_t88rcucg1b7uk t135 = t134;
        t123 = t135;
ix124:;
        gm_l11 = t123;
    }
    {
        uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t162) { case ((uint8_t)0ULL): goto ma137; case ((uint8_t)1ULL): goto ma138; case ((uint8_t)2ULL): goto ma138; case ((uint8_t)3ULL): goto ma138; case ((uint8_t)4ULL): goto ma138; case ((uint8_t)5ULL): goto ma139; case ((uint8_t)6ULL): goto ma140; case ((uint8_t)7ULL): goto ma157; case ((uint8_t)8ULL): goto ma154; case ((uint8_t)9ULL): goto ma155; case ((uint8_t)10ULL): goto ma160; case ((uint8_t)11ULL): goto ma160; case ((uint8_t)12ULL): goto ma160; case ((uint8_t)13ULL): goto ma156; case ((uint8_t)14ULL): goto ma150; case ((uint8_t)15ULL): goto ma160; case ((uint8_t)16ULL): goto ma141; case ((uint8_t)17ULL): goto ma141; case ((uint8_t)18ULL): goto ma142; case ((uint8_t)19ULL): goto ma143; case ((uint8_t)20ULL): goto ma144; case ((uint8_t)21ULL): goto ma145; case ((uint8_t)22ULL): goto ma146; case ((uint8_t)23ULL): goto ma147; case ((uint8_t)24ULL): goto ma148; case ((uint8_t)25ULL): goto ma149; case ((uint8_t)26ULL): goto ma158; case ((uint8_t)27ULL): goto ma160; case ((uint8_t)28ULL): goto ma159; case ((uint8_t)29ULL): goto ma159; case ((uint8_t)30ULL): goto ma159; case ((uint8_t)31ULL): goto ma159; case ((uint8_t)32ULL): goto ma159; case ((uint8_t)33ULL): goto ma159; case ((uint8_t)34ULL): goto ma159; case ((uint8_t)35ULL): goto ma152; case ((uint8_t)36ULL): goto ma151; case ((uint8_t)37ULL): goto ma151; case ((uint8_t)38ULL): goto ma151; case ((uint8_t)39ULL): goto ma151; case ((uint8_t)40ULL): goto ma160; case ((uint8_t)41ULL): goto ma160; case ((uint8_t)42ULL): goto ma160; case ((uint8_t)43ULL): goto ma160; case ((uint8_t)44ULL): goto ma160; case ((uint8_t)45ULL): goto ma160; case ((uint8_t)46ULL): goto ma160; case ((uint8_t)47ULL): goto ma160; case ((uint8_t)48ULL): goto ma160; case ((uint8_t)49ULL): goto ma160; case ((uint8_t)50ULL): goto ma160; case ((uint8_t)51ULL): goto ma160; case ((uint8_t)52ULL): goto ma160; case ((uint8_t)53ULL): goto ma160; case ((uint8_t)54ULL): goto ma160; case ((uint8_t)55ULL): goto ma160; case ((uint8_t)56ULL): goto ma160; case ((uint8_t)57ULL): goto ma160; case ((uint8_t)58ULL): goto ma160; case ((uint8_t)59ULL): goto ma160; case ((uint8_t)60ULL): goto ma160; case ((uint8_t)61ULL): goto ma160; case ((uint8_t)62ULL): goto ma160; case ((uint8_t)63ULL): goto ma160; case ((uint8_t)64ULL): goto ma160; case ((uint8_t)65ULL): goto ma160; case ((uint8_t)66ULL): goto ma160; case ((uint8_t)67ULL): goto ma160; case ((uint8_t)68ULL): goto ma160; case ((uint8_t)69ULL): goto ma160; case ((uint8_t)70ULL): goto ma160; case ((uint8_t)71ULL): goto ma160; case ((uint8_t)72ULL): goto ma160; case ((uint8_t)73ULL): goto ma160; case ((uint8_t)74ULL): goto ma160; case ((uint8_t)75ULL): goto ma160; case ((uint8_t)76ULL): goto ma160; case ((uint8_t)77ULL): goto ma160; case ((uint8_t)78ULL): goto ma153; case ((uint8_t)79ULL): goto ma160; case ((uint8_t)80ULL): goto ma160; case ((uint8_t)81ULL): goto ma160; default: goto ma160; }
ma137:;
        uint8_t t164 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t164 != ((uint8_t)0ULL)) goto mn163;
        uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        switch (t172) { case ((uint8_t)0ULL): goto ma166; case ((uint8_t)1ULL): goto ma168; case ((uint8_t)2ULL): goto ma167; case ((uint8_t)3ULL): goto ma170; case ((uint8_t)4ULL): goto ma170; case ((uint8_t)5ULL): goto ma170; case ((uint8_t)6ULL): goto ma170; case ((uint8_t)7ULL): goto ma170; case ((uint8_t)8ULL): goto ma170; case ((uint8_t)9ULL): goto ma170; case ((uint8_t)10ULL): goto ma169; case ((uint8_t)11ULL): goto ma170; case ((uint8_t)12ULL): goto ma170; case ((uint8_t)13ULL): goto ma170; default: goto ma170; }
ma166:;
        uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t174 != ((uint8_t)0ULL)) goto mn173;
        unsigned __int128 t175 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
        gm_l12 = t175;
        void * t176 = gm_l4;
        void * t177 = gm_l3;
        gm_t88rcucg1b7uk t178 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t177) + 0));
        gm_t53at80pfdus3 t179;
        gm_f_kind_jcnt4hm((void *)&t179, t176, GM_CONV(gm_csguankko4lmhd, t178));
        gm_t53at80pfdus3 t180 = t179;
        uint8_t t186 = GM_LD(uint8_t, (((uint8_t *)&t180) + 0));
        switch (t186) { case ((uint8_t)0ULL): goto ma183; case ((uint8_t)1ULL): goto ma183; case ((uint8_t)2ULL): goto ma183; case ((uint8_t)3ULL): goto ma182; case ((uint8_t)4ULL): goto ma184; case ((uint8_t)5ULL): goto ma184; case ((uint8_t)6ULL): goto ma184; case ((uint8_t)7ULL): goto ma184; case ((uint8_t)8ULL): goto ma184; case ((uint8_t)9ULL): goto ma184; case ((uint8_t)10ULL): goto ma184; case ((uint8_t)11ULL): goto ma184; case ((uint8_t)12ULL): goto ma184; case ((uint8_t)13ULL): goto ma184; case ((uint8_t)14ULL): goto ma184; case ((uint8_t)15ULL): goto ma184; case ((uint8_t)16ULL): goto ma184; case ((uint8_t)17ULL): goto ma184; case ((uint8_t)18ULL): goto ma184; case ((uint8_t)19ULL): goto ma184; case ((uint8_t)20ULL): goto ma184; case ((uint8_t)21ULL): goto ma184; case ((uint8_t)22ULL): goto ma184; case ((uint8_t)23ULL): goto ma184; case ((uint8_t)24ULL): goto ma184; case ((uint8_t)25ULL): goto ma184; case ((uint8_t)26ULL): goto ma184; default: goto ma184; }
ma182:;
        uint8_t t188 = GM_LD(uint8_t, (((uint8_t *)&t180) + 0));
        if (t188 != ((uint8_t)3ULL)) goto mn187;
        void * t189 = gm_l0;
        void * t190 = gm_l0;
        void * t191 = gm_l0;
        unsigned __int128 t192 = gm_l12;
        gm_t88rcucg1b7uk t193 = gm_l11;
        gm_tbci5ospk5nnn t194;
        gm_f_big_of_1cq0qao((void *)&t194, (void *)NULL, cx, NULL, t191, t192, t193);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        double t195 = gm_f_big_float_jmiufgl((void *)NULL, cx, NULL, t190, &t194);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        void * t196 = gm_l3;
        gm_t88rcucg1b7uk t197 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t196) + 0));
        gm_t26jhpkpc0sda t198;
        gm_f_round_float_saju9fu((void *)&t198, (void *)NULL, cx, NULL, t189, t195, t197);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t26jhpkpc0sda t199 = t198;
        if (((((uint8_t *)&t199))[0] != 0)) {
            gm_t39uai1ajk3qv t200 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t199) + 16));
            gm_ttk92ufj2f6eh t201;
            GM_ST(uint8_t, (uint8_t *)&t201 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t201 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t201 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t201 + 8, 0);
            ((uint8_t *)&t201)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t201 + 16), t200);
            F.ret = t201;
            goto gm_fret;
        }
        double t202 = GM_LD(double, (((uint8_t *)&t199) + 16));
        gm_t6evimt84lndq t203;
        memset(&t203, 0, sizeof t203);
        GM_ST(uint8_t, ((uint8_t *)&t203 + 0), ((uint8_t)1ULL));
        GM_ST(double, ((uint8_t *)&t203 + 16), t202);
        gm_ttk92ufj2f6eh t204;
        memset(&t204, 0, sizeof t204);
        ((uint8_t *)&t204)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t204 + 16), t203);
        gm_ttk92ufj2f6eh t205 = t204;
        uint8_t t206 = ((((uint8_t *)&t205))[0] != 0);
        F.ret = t204;
        goto gm_fret;
        goto me181;
mn187:;
ma183:;
        uint8_t t210 = GM_LD(uint8_t, (((uint8_t *)&t180) + 0));
        if (t210 != ((uint8_t)0ULL)) goto pa209;
        goto po208;
pa209:;
        uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&t180) + 0));
        if (t212 != ((uint8_t)1ULL)) goto pa211;
        goto po208;
pa211:;
        uint8_t t214 = GM_LD(uint8_t, (((uint8_t *)&t180) + 0));
        if (t214 != ((uint8_t)2ULL)) goto pa213;
        goto po208;
pa213:;
        goto mn207;
po208:;
        void * t215 = gm_l0;
        void * t216 = gm_l0;
        unsigned __int128 t217 = gm_l12;
        gm_t88rcucg1b7uk t218 = gm_l11;
        __int128 t219 = gm_f_int_value_6hmh8tj(t216, t217, GM_CONV(gm_csguankko4lmhd, t218));
        __int128 t220 = t219;
        unsigned __int128 t221;
        memset(&t221, 0, sizeof t221); memcpy(&t221, ((uint8_t *)&t220), sizeof t221 < 16 ? sizeof t221 : 16);
        void * t222 = gm_l3;
        gm_t88rcucg1b7uk t223 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t222) + 0));
        unsigned __int128 t224 = gm_f_wrap_bt8r149(t215, t221, GM_CONV(gm_csguankko4lmhd, t223));
        gm_t6evimt84lndq t225;
        GM_ST(uint8_t, (uint8_t *)&t225 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t225 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t225 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t225 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t225 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t225 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t225 + 0), ((uint8_t)0ULL));
        GM_ST(unsigned __int128, ((uint8_t *)&t225 + 16), t224);
        gm_ttk92ufj2f6eh t226;
        memset(&t226, 0, sizeof t226);
        ((uint8_t *)&t226)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t226 + 16), t225);
        gm_ttk92ufj2f6eh t227 = t226;
        uint8_t t228 = ((((uint8_t *)&t227))[0] != 0);
        F.ret = t226;
        goto gm_fret;
        goto me181;
mn207:;
ma184:;
        void * t230 = gm_l0;
        gm_tl874t2atnlh3 t231 = gm_l5;
        gm_b16_8 t232;
        GM_ST(void *, (uint8_t *)&t232, (void *)(gm_smr3su985na96));
        GM_ST(uintptr_t, (uint8_t *)&t232 + 8, (uintptr_t)(15));
        gm_t39uai1ajk3qv t233;
        gm_f_unsupported_2seq49l((void *)&t233, t230, GM_CONV(gm_cseahvg8v7p0vs, t231), t232);
        gm_ttk92ufj2f6eh t234;
        GM_ST(uint8_t, (uint8_t *)&t234 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t234 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t234 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t234 + 8, 0);
        ((uint8_t *)&t234)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t234 + 16), t233);
        gm_ttk92ufj2f6eh t235 = t234;
        uint8_t t236 = ((((uint8_t *)&t235))[0] != 0);
        F.ret = t234;
        goto gm_fret;
        goto me181;
mn229:;
ma185:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 371, 27);
me181:;
        goto me165;
mn173:;
ma167:;
        uint8_t t238 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t238 != ((uint8_t)2ULL)) goto mn237;
        uint8_t t239 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 16));
        gm_l13 = t239;
        unsigned __int128 t240;
        uint8_t t242 = gm_l13;
        if (!t242) goto ie241;
        unsigned __int128 t244 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
        t240 = t244;
        goto ix241;
ie241:;
        unsigned __int128 t246 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
        t240 = t246;
ix241:;
        gm_t6evimt84lndq t247;
        GM_ST(uint8_t, (uint8_t *)&t247 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t247 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t247 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t247 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t247 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t247 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t247 + 0), ((uint8_t)0ULL));
        GM_ST(unsigned __int128, ((uint8_t *)&t247 + 16), t240);
        gm_ttk92ufj2f6eh t248;
        memset(&t248, 0, sizeof t248);
        ((uint8_t *)&t248)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t248 + 16), t247);
        gm_ttk92ufj2f6eh t249 = t248;
        uint8_t t250 = ((((uint8_t *)&t249))[0] != 0);
        F.ret = t248;
        goto gm_fret;
        goto me165;
mn237:;
ma168:;
        uint8_t t252 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t252 != ((uint8_t)1ULL)) goto mn251;
        double t253 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
        gm_l14 = t253;
        void * t254 = gm_l0;
        double t255 = gm_l14;
        void * t256 = gm_l3;
        gm_t88rcucg1b7uk t257 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t256) + 0));
        gm_t26jhpkpc0sda t258;
        gm_f_round_float_saju9fu((void *)&t258, (void *)NULL, cx, NULL, t254, t255, t257);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t26jhpkpc0sda t259 = t258;
        if (((((uint8_t *)&t259))[0] != 0)) {
            gm_t39uai1ajk3qv t260 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t259) + 16));
            gm_ttk92ufj2f6eh t261;
            GM_ST(uint8_t, (uint8_t *)&t261 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t261 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t261 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t261 + 8, 0);
            ((uint8_t *)&t261)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t261 + 16), t260);
            F.ret = t261;
            goto gm_fret;
        }
        double t262 = GM_LD(double, (((uint8_t *)&t259) + 16));
        gm_t6evimt84lndq t263;
        memset(&t263, 0, sizeof t263);
        GM_ST(uint8_t, ((uint8_t *)&t263 + 0), ((uint8_t)1ULL));
        GM_ST(double, ((uint8_t *)&t263 + 16), t262);
        gm_ttk92ufj2f6eh t264;
        memset(&t264, 0, sizeof t264);
        ((uint8_t *)&t264)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t264 + 16), t263);
        gm_ttk92ufj2f6eh t265 = t264;
        uint8_t t266 = ((((uint8_t *)&t265))[0] != 0);
        F.ret = t264;
        goto gm_fret;
        goto me165;
mn251:;
ma169:;
        uint8_t t268 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t268 != ((uint8_t)10ULL)) goto mn267;
        uint32_t t269 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 16) + 0));
        gm_l15 = t269;
        {
            void * t271 = gm_l0;
            gm_t88rcucg1b7uk t272 = gm_l11;
            uint32_t t273 = gm_l15;
            gm_thrdpodrs8pgt t274;
            gm_f_variant_tag_4pbamin((void *)&t274, t271, GM_CONV(gm_csguankko4lmhd, t272), t273);
            gm_thrdpodrs8pgt t275 = t274;
            gm_tbci5ospk5nnn t276;
            if (((((uint8_t *)&t275))[48] != 0)) {
                gm_tbci5ospk5nnn t277 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&t275) + 0));
                t276 = t277;
            } else {
                void * t278 = gm_l0;
                gm_tl874t2atnlh3 t279 = gm_l5;
                gm_b16_8 t280;
                GM_ST(void *, (uint8_t *)&t280, (void *)(gm_smr3su985na96));
                GM_ST(uintptr_t, (uint8_t *)&t280 + 8, (uintptr_t)(15));
                gm_t39uai1ajk3qv t281;
                gm_f_unsupported_2seq49l((void *)&t281, t278, GM_CONV(gm_cseahvg8v7p0vs, t279), t280);
                gm_ttk92ufj2f6eh t282;
                GM_ST(uint8_t, (uint8_t *)&t282 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t282 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t282 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t282 + 8, 0);
                ((uint8_t *)&t282)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t282 + 16), t281);
                gm_ttk92ufj2f6eh t283 = t282;
                uint8_t t284 = ((((uint8_t *)&t283))[0] != 0);
                F.ret = t282;
                goto gm_fret;
            }
            F.l16 = t276;
        }
        {
            void * t285 = gm_l0;
            unsigned __int128 t286 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)));
            void * t287 = gm_l3;
            gm_t88rcucg1b7uk t288 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t287) + 0));
            unsigned __int128 t289 = gm_f_wrap_bt8r149(t285, t286, GM_CONV(gm_csguankko4lmhd, t288));
            gm_t6evimt84lndq t290;
            GM_ST(uint8_t, (uint8_t *)&t290 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t290 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t290 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t290 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t290 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t290 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t290 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t290 + 16), t289);
            gm_ttk92ufj2f6eh t291;
            memset(&t291, 0, sizeof t291);
            ((uint8_t *)&t291)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t291 + 16), t290);
            gm_ttk92ufj2f6eh t292 = t291;
            uint8_t t293 = ((((uint8_t *)&t292))[0] != 0);
            F.ret = t291;
            goto gm_fret;
        }
        goto me165;
mn267:;
ma170:;
        void * t295 = gm_l0;
        gm_tl874t2atnlh3 t296 = gm_l5;
        gm_b16_8 t297;
        GM_ST(void *, (uint8_t *)&t297, (void *)(gm_smr3su985na96));
        GM_ST(uintptr_t, (uint8_t *)&t297 + 8, (uintptr_t)(15));
        gm_t39uai1ajk3qv t298;
        gm_f_unsupported_2seq49l((void *)&t298, t295, GM_CONV(gm_cseahvg8v7p0vs, t296), t297);
        gm_ttk92ufj2f6eh t299;
        GM_ST(uint8_t, (uint8_t *)&t299 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t299 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t299 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t299 + 8, 0);
        ((uint8_t *)&t299)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t299 + 16), t298);
        gm_ttk92ufj2f6eh t300 = t299;
        uint8_t t301 = ((((uint8_t *)&t300))[0] != 0);
        F.ret = t299;
        goto gm_fret;
        goto me165;
mn294:;
ma171:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 370, 21);
me165:;
        goto me136;
mn163:;
ma138:;
        uint8_t t305 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t305 != ((uint8_t)1ULL)) goto pa304;
        goto po303;
pa304:;
        uint8_t t307 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t307 != ((uint8_t)2ULL)) goto pa306;
        goto po303;
pa306:;
        uint8_t t309 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t309 != ((uint8_t)3ULL)) goto pa308;
        goto po303;
pa308:;
        uint8_t t311 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t311 != ((uint8_t)4ULL)) goto pa310;
        goto po303;
pa310:;
        goto mn302;
po303:;
        uint8_t t317 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        switch (t317) { case ((uint8_t)0ULL): goto ma313; case ((uint8_t)1ULL): goto ma314; case ((uint8_t)2ULL): goto ma315; case ((uint8_t)3ULL): goto ma315; case ((uint8_t)4ULL): goto ma315; case ((uint8_t)5ULL): goto ma315; case ((uint8_t)6ULL): goto ma315; case ((uint8_t)7ULL): goto ma315; case ((uint8_t)8ULL): goto ma315; case ((uint8_t)9ULL): goto ma315; case ((uint8_t)10ULL): goto ma315; case ((uint8_t)11ULL): goto ma315; case ((uint8_t)12ULL): goto ma315; case ((uint8_t)13ULL): goto ma315; default: goto ma315; }
ma313:;
        uint8_t t319 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t319 != ((uint8_t)0ULL)) goto mn318;
        unsigned __int128 t320 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
        gm_l17 = t320;
        {
            void * t323 = gm_l4;
            void * t324 = gm_l3;
            gm_t88rcucg1b7uk t325 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t324) + 0));
            gm_t53at80pfdus3 t326;
            gm_f_kind_jcnt4hm((void *)&t326, t323, GM_CONV(gm_csguankko4lmhd, t325));
            gm_t53at80pfdus3 t327 = t326;
            uint8_t t328 = GM_LD(uint8_t, (((uint8_t *)&t327) + 0));
            if (t328 != ((uint8_t)3ULL)) goto ie322;
            {
                void * t330 = gm_l0;
                void * t331 = gm_l0;
                void * t332 = gm_l0;
                unsigned __int128 t333 = gm_l17;
                gm_t88rcucg1b7uk t334 = gm_l11;
                gm_tbci5ospk5nnn t335;
                gm_f_big_of_1cq0qao((void *)&t335, (void *)NULL, cx, NULL, t332, t333, t334);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                double t336 = gm_f_big_float_jmiufgl((void *)NULL, cx, NULL, t331, &t335);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                void * t337 = gm_l3;
                gm_t88rcucg1b7uk t338 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t337) + 0));
                gm_t26jhpkpc0sda t339;
                gm_f_round_float_saju9fu((void *)&t339, (void *)NULL, cx, NULL, t330, t336, t338);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t26jhpkpc0sda t340 = t339;
                if (((((uint8_t *)&t340))[0] != 0)) {
                    gm_t39uai1ajk3qv t341 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t340) + 16));
                    gm_ttk92ufj2f6eh t342;
                    GM_ST(uint8_t, (uint8_t *)&t342 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t342 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t342 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t342 + 8, 0);
                    ((uint8_t *)&t342)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t342 + 16), t341);
                    F.ret = t342;
                    goto gm_fret;
                }
                double t343 = GM_LD(double, (((uint8_t *)&t340) + 16));
                gm_t6evimt84lndq t344;
                memset(&t344, 0, sizeof t344);
                GM_ST(uint8_t, ((uint8_t *)&t344 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t344 + 16), t343);
                gm_ttk92ufj2f6eh t345;
                memset(&t345, 0, sizeof t345);
                ((uint8_t *)&t345)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t345 + 16), t344);
                gm_ttk92ufj2f6eh t346 = t345;
                uint8_t t347 = ((((uint8_t *)&t346))[0] != 0);
                F.ret = t345;
                goto gm_fret;
            }
            goto ix322;
ie322:;
ix322:;
        }
        {
            void * t348 = gm_l0;
            unsigned __int128 t349 = gm_l17;
            void * t350 = gm_l3;
            gm_t88rcucg1b7uk t351 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t350) + 0));
            unsigned __int128 t352 = gm_f_wrap_bt8r149(t348, t349, GM_CONV(gm_csguankko4lmhd, t351));
            gm_t6evimt84lndq t353;
            GM_ST(uint8_t, (uint8_t *)&t353 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t353 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t353 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t353 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t353 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t353 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t353 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t353 + 16), t352);
            gm_ttk92ufj2f6eh t354;
            memset(&t354, 0, sizeof t354);
            ((uint8_t *)&t354)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t354 + 16), t353);
            gm_ttk92ufj2f6eh t355 = t354;
            uint8_t t356 = ((((uint8_t *)&t355))[0] != 0);
            F.ret = t354;
            goto gm_fret;
        }
        goto me312;
mn318:;
ma314:;
        uint8_t t358 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t358 != ((uint8_t)1ULL)) goto mn357;
        double t359 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
        gm_l18 = t359;
        {
            void * t362 = gm_l4;
            void * t363 = gm_l3;
            gm_t88rcucg1b7uk t364 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t363) + 0));
            gm_t53at80pfdus3 t365;
            gm_f_kind_jcnt4hm((void *)&t365, t362, GM_CONV(gm_csguankko4lmhd, t364));
            gm_t53at80pfdus3 t366 = t365;
            uint8_t t367 = GM_LD(uint8_t, (((uint8_t *)&t366) + 0));
            if (t367 != ((uint8_t)3ULL)) goto ie361;
            {
                void * t369 = gm_l0;
                double t370 = gm_l18;
                void * t371 = gm_l3;
                gm_t88rcucg1b7uk t372 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t371) + 0));
                gm_t26jhpkpc0sda t373;
                gm_f_round_float_saju9fu((void *)&t373, (void *)NULL, cx, NULL, t369, t370, t372);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t26jhpkpc0sda t374 = t373;
                if (((((uint8_t *)&t374))[0] != 0)) {
                    gm_t39uai1ajk3qv t375 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t374) + 16));
                    gm_ttk92ufj2f6eh t376;
                    GM_ST(uint8_t, (uint8_t *)&t376 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t376 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t376 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t376 + 8, 0);
                    ((uint8_t *)&t376)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t376 + 16), t375);
                    F.ret = t376;
                    goto gm_fret;
                }
                double t377 = GM_LD(double, (((uint8_t *)&t374) + 16));
                gm_t6evimt84lndq t378;
                memset(&t378, 0, sizeof t378);
                GM_ST(uint8_t, ((uint8_t *)&t378 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t378 + 16), t377);
                gm_ttk92ufj2f6eh t379;
                memset(&t379, 0, sizeof t379);
                ((uint8_t *)&t379)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t379 + 16), t378);
                gm_ttk92ufj2f6eh t380 = t379;
                uint8_t t381 = ((((uint8_t *)&t380))[0] != 0);
                F.ret = t379;
                goto gm_fret;
            }
            goto ix361;
ie361:;
ix361:;
        }
        {
            void * t382 = gm_l0;
            double t383 = gm_l18;
            void * t384 = gm_l3;
            gm_t88rcucg1b7uk t385 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t384) + 0));
            gm_tvdg94bk7aa22 t386 = gm_l1;
            gm_thrdpodrs8pgt t387;
            gm_f_float_to_int_sf4dap7((void *)&t387, (void *)NULL, cx, NULL, t382, t383, t385, t386);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_thrdpodrs8pgt t388 = t387;
            gm_tbci5ospk5nnn t389;
            if (((((uint8_t *)&t388))[48] != 0)) {
                gm_tbci5ospk5nnn t390 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&t388) + 0));
                t389 = t390;
            } else {
                void * t391 = gm_l0;
                gm_tl874t2atnlh3 t392 = gm_l5;
                gm_b16_8 t393;
                GM_ST(void *, (uint8_t *)&t393, (void *)(gm_s9o2c36nosv54));
                GM_ST(uintptr_t, (uint8_t *)&t393 + 8, (uintptr_t)(40));
                gm_t39uai1ajk3qv t394;
                gm_f_trap_jftbss6((void *)&t394, (void *)NULL, cx, NULL, t391, t392, t393);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_ttk92ufj2f6eh t395;
                GM_ST(uint8_t, (uint8_t *)&t395 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t395 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t395 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t395 + 8, 0);
                ((uint8_t *)&t395)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t395 + 16), t394);
                gm_ttk92ufj2f6eh t396 = t395;
                uint8_t t397 = ((((uint8_t *)&t396))[0] != 0);
                F.ret = t395;
                goto gm_fret;
            }
            F.l19 = t389;
        }
        {
            void * t398 = gm_l0;
            unsigned __int128 t399 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l19)));
            void * t400 = gm_l3;
            gm_t88rcucg1b7uk t401 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t400) + 0));
            unsigned __int128 t402 = gm_f_wrap_bt8r149(t398, t399, GM_CONV(gm_csguankko4lmhd, t401));
            gm_t6evimt84lndq t403;
            GM_ST(uint8_t, (uint8_t *)&t403 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t403 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t403 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t403 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t403 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t403 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t403 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t403 + 16), t402);
            gm_ttk92ufj2f6eh t404;
            memset(&t404, 0, sizeof t404);
            ((uint8_t *)&t404)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t404 + 16), t403);
            gm_ttk92ufj2f6eh t405 = t404;
            uint8_t t406 = ((((uint8_t *)&t405))[0] != 0);
            F.ret = t404;
            goto gm_fret;
        }
        goto me312;
mn357:;
ma315:;
        void * t408 = gm_l0;
        gm_tl874t2atnlh3 t409 = gm_l5;
        gm_b16_8 t410;
        GM_ST(void *, (uint8_t *)&t410, (void *)(gm_smr3su985na96));
        GM_ST(uintptr_t, (uint8_t *)&t410 + 8, (uintptr_t)(15));
        gm_t39uai1ajk3qv t411;
        gm_f_unsupported_2seq49l((void *)&t411, t408, GM_CONV(gm_cseahvg8v7p0vs, t409), t410);
        gm_ttk92ufj2f6eh t412;
        GM_ST(uint8_t, (uint8_t *)&t412 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t412 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t412 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t412 + 8, 0);
        ((uint8_t *)&t412)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t412 + 16), t411);
        gm_ttk92ufj2f6eh t413 = t412;
        uint8_t t414 = ((((uint8_t *)&t413))[0] != 0);
        F.ret = t412;
        goto gm_fret;
        goto me312;
mn407:;
ma316:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 384, 42);
me312:;
        goto me136;
mn302:;
ma139:;
        uint8_t t416 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t416 != ((uint8_t)5ULL)) goto mn415;
        {
            void * t419 = gm_l4;
            void * t420 = gm_l3;
            gm_t88rcucg1b7uk t421 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t420) + 0));
            gm_t53at80pfdus3 t422;
            gm_f_kind_jcnt4hm((void *)&t422, t419, GM_CONV(gm_csguankko4lmhd, t421));
            gm_t53at80pfdus3 t423 = t422;
            uint8_t t424 = GM_LD(uint8_t, (((uint8_t *)&t423) + 0));
            if (t424 != ((uint8_t)13ULL)) goto ie418;
            gm_t88rcucg1b7uk t425 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t423) + 8));
            gm_l20 = t425;
            {
                uint8_t t432 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                switch (t432) { case ((uint8_t)0ULL): goto ma428; case ((uint8_t)1ULL): goto ma429; case ((uint8_t)2ULL): goto ma430; case ((uint8_t)3ULL): goto ma430; case ((uint8_t)4ULL): goto ma430; case ((uint8_t)5ULL): goto ma430; case ((uint8_t)6ULL): goto ma430; case ((uint8_t)7ULL): goto ma430; case ((uint8_t)8ULL): goto ma430; case ((uint8_t)9ULL): goto ma430; case ((uint8_t)10ULL): goto ma430; case ((uint8_t)11ULL): goto ma430; case ((uint8_t)12ULL): goto ma430; case ((uint8_t)13ULL): goto ma430; default: goto ma430; }
ma428:;
                uint8_t t434 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                if (t434 != ((uint8_t)0ULL)) goto mn433;
                unsigned __int128 t435 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
                gm_l21 = t435;
                {
                    void * t437 = gm_l0;
                    unsigned __int128 t438 = gm_l21;
                    gm_t88rcucg1b7uk t439 = gm_l11;
                    gm_f_big_of_1cq0qao((void *)&F.l22, (void *)NULL, cx, NULL, t437, t438, t439);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t440 = gm_l0;
                    gm_t88rcucg1b7uk t441 = gm_l20;
                    gm_f_int_range_hue3vms((void *)&F.l23, (void *)NULL, cx, NULL, t440, t441);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    uint8_t t443 = 0;
                    int32_t t444 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l23) + 0)));
                    uint8_t t445 = (t444 < ((int32_t)0LL));
                    if (!t445) {
                        int32_t t446 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l23) + 48)));
                        uint8_t t447 = (t446 > ((int32_t)0LL));
                        t443 = t447;
                    } else t443 = 1;
                    if (!t443) goto ie442;
                    {
                        gm_t6evimt84lndq t449;
                        memset(&t449, 0, sizeof t449);
                        GM_ST(uint8_t, ((uint8_t *)&t449 + 0), ((uint8_t)6ULL));
                        gm_ttk92ufj2f6eh t450;
                        memset(&t450, 0, sizeof t450);
                        ((uint8_t *)&t450)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t450 + 16), t449);
                        gm_ttk92ufj2f6eh t451 = t450;
                        uint8_t t452 = ((((uint8_t *)&t451))[0] != 0);
                        F.ret = t450;
                        goto gm_fret;
                    }
                    goto ix442;
ie442:;
ix442:;
                }
                {
                    void * t453 = gm_l0;
                    unsigned __int128 t454 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l22)));
                    gm_t88rcucg1b7uk t455 = gm_l20;
                    unsigned __int128 t456 = gm_f_wrap_bt8r149(t453, t454, GM_CONV(gm_csguankko4lmhd, t455));
                    gm_t6evimt84lndq t457;
                    GM_ST(uint8_t, (uint8_t *)&t457 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t457 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t457 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t457 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t457 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t457 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t457 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t457 + 16), t456);
                    gm_tq7s98ihonkju t458 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t457, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k4tn3ii07uh)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tq7s98ihonkju t459 = t458;
                    if (((((uint8_t *)&t459))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t459) + 8, gm_srcue4ionbe9lgh, 403, 34);
                    void * t460 = GM_LD(void *, (((uint8_t *)&t459) + 8));
                    gm_t6evimt84lndq t461;
                    memset(&t461, 0, sizeof t461);
                    GM_ST(uint8_t, ((uint8_t *)&t461 + 0), ((uint8_t)7ULL));
                    GM_ST(void *, ((uint8_t *)&t461 + 16), t460);
                    gm_ttk92ufj2f6eh t462;
                    memset(&t462, 0, sizeof t462);
                    ((uint8_t *)&t462)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t462 + 16), t461);
                    gm_ttk92ufj2f6eh t463 = t462;
                    uint8_t t464 = ((((uint8_t *)&t463))[0] != 0);
                    F.ret = t462;
                    goto gm_fret;
                }
                goto me427;
mn433:;
ma429:;
                uint8_t t466 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
                if (t466 != ((uint8_t)1ULL)) goto mn465;
                double t467 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
                gm_l24 = t467;
                {
                    double t469 = gm_l24;
                    gm_tvegkb6ouf3m0 t470;
                    gm_f_float_exact_1g17c6c((void *)&t470, (void *)NULL, cx, NULL, t469);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tvegkb6ouf3m0 t471 = t470;
                    gm_tngtuiuskn938 t472;
                    if (((((uint8_t *)&t471))[96] != 0)) {
                        gm_tngtuiuskn938 t473 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&t471) + 0));
                        t472 = t473;
                    } else {
                        gm_t6evimt84lndq t474;
                        memset(&t474, 0, sizeof t474);
                        GM_ST(uint8_t, ((uint8_t *)&t474 + 0), ((uint8_t)6ULL));
                        gm_ttk92ufj2f6eh t475;
                        memset(&t475, 0, sizeof t475);
                        ((uint8_t *)&t475)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t475 + 16), t474);
                        gm_ttk92ufj2f6eh t476 = t475;
                        uint8_t t477 = ((((uint8_t *)&t476))[0] != 0);
                        F.ret = t475;
                        goto gm_fret;
                    }
                    F.l25 = t472;
                }
                {
                    uint8_t t479 = gm_f_is_integer_ab2ohk2(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l25)));
                    uint8_t t480 = (uint8_t)!t479;
                    if (!t480) goto ie478;
                    {
                        gm_t6evimt84lndq t482;
                        memset(&t482, 0, sizeof t482);
                        GM_ST(uint8_t, ((uint8_t *)&t482 + 0), ((uint8_t)6ULL));
                        gm_ttk92ufj2f6eh t483;
                        memset(&t483, 0, sizeof t483);
                        ((uint8_t *)&t483)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t483 + 16), t482);
                        gm_ttk92ufj2f6eh t484 = t483;
                        uint8_t t485 = ((((uint8_t *)&t484))[0] != 0);
                        F.ret = t483;
                        goto gm_fret;
                    }
                    goto ix478;
ie478:;
ix478:;
                }
                {
                    void * t486 = gm_l0;
                    gm_t88rcucg1b7uk t487 = gm_l20;
                    gm_f_int_range_hue3vms((void *)&F.l26, (void *)NULL, cx, NULL, t486, t487);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    uint8_t t489 = 0;
                    int32_t t490 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l25) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l26) + 0)));
                    uint8_t t491 = (t490 < ((int32_t)0LL));
                    if (!t491) {
                        int32_t t492 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l25) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l26) + 48)));
                        uint8_t t493 = (t492 > ((int32_t)0LL));
                        t489 = t493;
                    } else t489 = 1;
                    if (!t489) goto ie488;
                    {
                        gm_t6evimt84lndq t495;
                        memset(&t495, 0, sizeof t495);
                        GM_ST(uint8_t, ((uint8_t *)&t495 + 0), ((uint8_t)6ULL));
                        gm_ttk92ufj2f6eh t496;
                        memset(&t496, 0, sizeof t496);
                        ((uint8_t *)&t496)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t496 + 16), t495);
                        gm_ttk92ufj2f6eh t497 = t496;
                        uint8_t t498 = ((((uint8_t *)&t497))[0] != 0);
                        F.ret = t496;
                        goto gm_fret;
                    }
                    goto ix488;
ie488:;
ix488:;
                }
                {
                    void * t499 = gm_l0;
                    unsigned __int128 t500 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l25) + 0)));
                    gm_t88rcucg1b7uk t501 = gm_l20;
                    unsigned __int128 t502 = gm_f_wrap_bt8r149(t499, t500, GM_CONV(gm_csguankko4lmhd, t501));
                    gm_t6evimt84lndq t503;
                    GM_ST(uint8_t, (uint8_t *)&t503 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t503 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t503 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t503 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t503 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t503 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t503 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t503 + 16), t502);
                    gm_tq7s98ihonkju t504 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t503, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_krchumal22f)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tq7s98ihonkju t505 = t504;
                    if (((((uint8_t *)&t505))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t505) + 8, gm_srcue4ionbe9lgh, 410, 34);
                    void * t506 = GM_LD(void *, (((uint8_t *)&t505) + 8));
                    gm_t6evimt84lndq t507;
                    memset(&t507, 0, sizeof t507);
                    GM_ST(uint8_t, ((uint8_t *)&t507 + 0), ((uint8_t)7ULL));
                    GM_ST(void *, ((uint8_t *)&t507 + 16), t506);
                    gm_ttk92ufj2f6eh t508;
                    memset(&t508, 0, sizeof t508);
                    ((uint8_t *)&t508)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t508 + 16), t507);
                    gm_ttk92ufj2f6eh t509 = t508;
                    uint8_t t510 = ((((uint8_t *)&t509))[0] != 0);
                    F.ret = t508;
                    goto gm_fret;
                }
                goto me427;
mn465:;
ma430:;
                void * t512 = gm_l0;
                gm_tl874t2atnlh3 t513 = gm_l5;
                gm_b16_8 t514;
                GM_ST(void *, (uint8_t *)&t514, (void *)(gm_smr3su985na96));
                GM_ST(uintptr_t, (uint8_t *)&t514 + 8, (uintptr_t)(15));
                gm_t39uai1ajk3qv t515;
                gm_f_unsupported_2seq49l((void *)&t515, t512, GM_CONV(gm_cseahvg8v7p0vs, t513), t514);
                gm_ttk92ufj2f6eh t516;
                GM_ST(uint8_t, (uint8_t *)&t516 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t516 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t516 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t516 + 8, 0);
                ((uint8_t *)&t516)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t516 + 16), t515);
                gm_ttk92ufj2f6eh t517 = t516;
                uint8_t t518 = ((((uint8_t *)&t517))[0] != 0);
                F.ret = t516;
                goto gm_fret;
                goto me427;
mn511:;
ma431:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 398, 13);
me427:;
            }
            goto ix418;
ie418:;
            {
                void * t520 = gm_l0;
                gm_tl874t2atnlh3 t521 = gm_l5;
                gm_b16_8 t522;
                GM_ST(void *, (uint8_t *)&t522, (void *)(gm_smr3su985na96));
                GM_ST(uintptr_t, (uint8_t *)&t522 + 8, (uintptr_t)(15));
                gm_t39uai1ajk3qv t523;
                gm_f_unsupported_2seq49l((void *)&t523, t520, GM_CONV(gm_cseahvg8v7p0vs, t521), t522);
                gm_ttk92ufj2f6eh t524;
                GM_ST(uint8_t, (uint8_t *)&t524 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t524 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t524 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t524 + 8, 0);
                ((uint8_t *)&t524)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t524 + 16), t523);
                gm_ttk92ufj2f6eh t525 = t524;
                uint8_t t526 = ((((uint8_t *)&t525))[0] != 0);
                F.ret = t524;
                goto gm_fret;
            }
ix418:;
        }
        goto me136;
mn415:;
ma140:;
        uint8_t t528 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t528 != ((uint8_t)6ULL)) goto mn527;
        {
            void * t530 = gm_l0;
            void * t531 = gm_l3;
            gm_t88rcucg1b7uk t532 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t531) + 0));
            gm_f_int_range_hue3vms((void *)&F.l27, (void *)NULL, cx, NULL, t530, t532);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_zero_s25l990((void *)&F.l28, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t538 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            switch (t538) { case ((uint8_t)0ULL): goto ma534; case ((uint8_t)1ULL): goto ma535; case ((uint8_t)2ULL): goto ma536; case ((uint8_t)3ULL): goto ma536; case ((uint8_t)4ULL): goto ma536; case ((uint8_t)5ULL): goto ma536; case ((uint8_t)6ULL): goto ma536; case ((uint8_t)7ULL): goto ma536; case ((uint8_t)8ULL): goto ma536; case ((uint8_t)9ULL): goto ma536; case ((uint8_t)10ULL): goto ma536; case ((uint8_t)11ULL): goto ma536; case ((uint8_t)12ULL): goto ma536; case ((uint8_t)13ULL): goto ma536; default: goto ma536; }
ma534:;
            uint8_t t540 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t540 != ((uint8_t)0ULL)) goto mn539;
            unsigned __int128 t541 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
            gm_l29 = t541;
            {
                void * t543 = gm_l0;
                unsigned __int128 t544 = gm_l29;
                gm_t88rcucg1b7uk t545 = gm_l11;
                gm_f_big_of_1cq0qao((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)), (void *)NULL, cx, NULL, t543, t544, t545);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            goto me533;
mn539:;
ma535:;
            uint8_t t547 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t547 != ((uint8_t)1ULL)) goto mn546;
            double t548 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
            gm_l30 = t548;
            {
                double t551 = gm_l30;
                double t552 = gm_l30;
                uint8_t t553 = (!(t551 == t552));
                if (!t553) goto ie550;
                {
                    gm_t6evimt84lndq t555;
                    GM_ST(uint8_t, (uint8_t *)&t555 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t555 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t555 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t555 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t555 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t555 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t555 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t555 + 16), ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
                    gm_ttk92ufj2f6eh t556;
                    memset(&t556, 0, sizeof t556);
                    ((uint8_t *)&t556)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t556 + 16), t555);
                    gm_ttk92ufj2f6eh t557 = t556;
                    uint8_t t558 = ((((uint8_t *)&t557))[0] != 0);
                    F.ret = t556;
                    goto gm_fret;
                }
                goto ix550;
ie550:;
ix550:;
            }
            {
                double t560 = gm_l30;
                gm_tvegkb6ouf3m0 t561;
                gm_f_float_exact_1g17c6c((void *)&t561, (void *)NULL, cx, NULL, t560);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tvegkb6ouf3m0 t562 = t561;
                if (!((((uint8_t *)&t562))[96] != 0)) goto ie559;
                gm_tngtuiuskn938 t563 = GM_LD(gm_tngtuiuskn938, ((uint8_t *)&t562));
                F.l31 = t563;
                {
                    gm_f_div_floor_u3mfihk((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l31) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l31) + 48)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    uint8_t t566 = gm_f_is_negative_rb46l9j(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l31)));
                    if (!t566) goto ie565;
                    uint8_t t567 = gm_f_is_integer_ab2ohk2(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l31)));
                    uint8_t t568 = (uint8_t)!t567;
                    if (!t568) goto ie565;
                    {
                        gm_tbci5ospk5nnn t570 = F.l28;
                        gm_tbci5ospk5nnn t571;
                        gm_f_one_08qg1op((void *)&t571, (void *)NULL, cx, NULL);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_f_add_420lj6o((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)), (void *)NULL, cx, NULL, &t570, &t571);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    goto ix565;
ie565:;
ix565:;
                }
                goto ix559;
ie559:;
                {
                    gm_tbci5ospk5nnn t573;
                    double t575 = gm_l30;
                    uint8_t t576 = (t575 < ((double)0e0));
                    if (!t576) goto ie574;
                    gm_tbci5ospk5nnn t578 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l27) + 0));
                    gm_tbci5ospk5nnn t579 = t578;
                    t573 = t579;
                    goto ix574;
ie574:;
                    gm_tbci5ospk5nnn t581 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l27) + 48));
                    gm_tbci5ospk5nnn t582 = t581;
                    t573 = t582;
ix574:;
                    F.l28 = t573;
                }
ix559:;
            }
            goto me533;
mn546:;
ma536:;
            void * t584 = gm_l0;
            gm_tl874t2atnlh3 t585 = gm_l5;
            gm_b16_8 t586;
            GM_ST(void *, (uint8_t *)&t586, (void *)(gm_smr3su985na96));
            GM_ST(uintptr_t, (uint8_t *)&t586 + 8, (uintptr_t)(15));
            gm_t39uai1ajk3qv t587;
            gm_f_unsupported_2seq49l((void *)&t587, t584, GM_CONV(gm_cseahvg8v7p0vs, t585), t586);
            gm_ttk92ufj2f6eh t588;
            GM_ST(uint8_t, (uint8_t *)&t588 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t588 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t588 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t588 + 8, 0);
            ((uint8_t *)&t588)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t588 + 16), t587);
            gm_ttk92ufj2f6eh t589 = t588;
            uint8_t t590 = ((((uint8_t *)&t589))[0] != 0);
            F.ret = t588;
            goto gm_fret;
            goto me533;
mn583:;
ma537:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 418, 13);
me533:;
        }
        {
            int32_t t592 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l27) + 0)));
            uint8_t t593 = (t592 < ((int32_t)0LL));
            if (!t593) goto ie591;
            {
                gm_tbci5ospk5nnn t595 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l27) + 0));
                F.l28 = t595;
            }
            goto ix591;
ie591:;
ix591:;
        }
        {
            int32_t t597 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l27) + 48)));
            uint8_t t598 = (t597 > ((int32_t)0LL));
            if (!t598) goto ie596;
            {
                gm_tbci5ospk5nnn t600 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l27) + 48));
                F.l28 = t600;
            }
            goto ix596;
ie596:;
ix596:;
        }
        {
            void * t601 = gm_l0;
            unsigned __int128 t602 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l28)));
            void * t603 = gm_l3;
            gm_t88rcucg1b7uk t604 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t603) + 0));
            unsigned __int128 t605 = gm_f_wrap_bt8r149(t601, t602, GM_CONV(gm_csguankko4lmhd, t604));
            gm_t6evimt84lndq t606;
            GM_ST(uint8_t, (uint8_t *)&t606 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t606 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t606 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t606 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t606 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t606 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t606 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t606 + 16), t605);
            gm_ttk92ufj2f6eh t607;
            memset(&t607, 0, sizeof t607);
            ((uint8_t *)&t607)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t607 + 16), t606);
            gm_ttk92ufj2f6eh t608 = t607;
            uint8_t t609 = ((((uint8_t *)&t608))[0] != 0);
            F.ret = t607;
            goto gm_fret;
        }
        goto me136;
mn527:;
ma141:;
        uint8_t t613 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t613 != ((uint8_t)16ULL)) goto pa612;
        goto po611;
pa612:;
        uint8_t t615 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t615 != ((uint8_t)17ULL)) goto pa614;
        goto po611;
pa614:;
        goto mn610;
po611:;
        {
            uint64_t t617 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t618 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
            if (t617 >= (uintptr_t)(t618)) gm_trap_index(t617, t618, gm_srcue4ionbe9lgh, 436, 18);
            gm_t6evimt84lndq t619 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t617 * 48));
            F.l32 = t619;
        }
        {
            uint8_t t625 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            switch (t625) { case ((uint8_t)0ULL): goto ma621; case ((uint8_t)1ULL): goto ma622; case ((uint8_t)2ULL): goto ma623; case ((uint8_t)3ULL): goto ma623; case ((uint8_t)4ULL): goto ma623; case ((uint8_t)5ULL): goto ma623; case ((uint8_t)6ULL): goto ma623; case ((uint8_t)7ULL): goto ma623; case ((uint8_t)8ULL): goto ma623; case ((uint8_t)9ULL): goto ma623; case ((uint8_t)10ULL): goto ma623; case ((uint8_t)11ULL): goto ma623; case ((uint8_t)12ULL): goto ma623; case ((uint8_t)13ULL): goto ma623; default: goto ma623; }
ma621:;
            uint8_t t627 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t627 != ((uint8_t)0ULL)) goto mn626;
            unsigned __int128 t628 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
            gm_l33 = t628;
            {
                uint8_t t631 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 0));
                if (t631 != ((uint8_t)0ULL)) goto ie630;
                unsigned __int128 t632 = GM_LD(unsigned __int128, (((uint8_t *)&F.l32) + 16));
                gm_l34 = t632;
                {
                    void * t634 = gm_l0;
                    unsigned __int128 t635 = gm_l33;
                    gm_t88rcucg1b7uk t636 = gm_l11;
                    gm_tbci5ospk5nnn t637;
                    gm_f_big_of_1cq0qao((void *)&t637, (void *)NULL, cx, NULL, t634, t635, t636);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t638 = gm_l0;
                    unsigned __int128 t639 = gm_l34;
                    uint64_t t640 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t641 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t640 >= (uintptr_t)(t641)) gm_trap_index(t640, t641, gm_srcue4ionbe9lgh, 440, 58);
                    gm_t88rcucg1b7uk t642 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t640 * 88) + 0));
                    gm_tbci5ospk5nnn t643;
                    gm_f_big_of_1cq0qao((void *)&t643, (void *)NULL, cx, NULL, t638, t639, t642);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int32_t t644 = gm_f_cmp_i1knrgu(&t637, &t643);
                    gm_l35 = t644;
                }
                {
                    gm_ttk92ufj2f6eh t645;
                    int32_t t647 = gm_l35;
                    uint8_t t648 = (t647 <= ((int32_t)0LL));
                    uint8_t t649 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)16ULL)));
                    uint8_t t650 = ((t648 == t649));
                    if (!t650) goto ie646;
                    gm_t6evimt84lndq t652 = F.l10;
                    gm_t6evimt84lndq t653 = t652;
                    gm_ttk92ufj2f6eh t654;
                    memset(&t654, 0, sizeof t654);
                    ((uint8_t *)&t654)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t654 + 16), t653);
                    t645 = t654;
                    goto ix646;
ie646:;
                    gm_t6evimt84lndq t656 = F.l32;
                    gm_t6evimt84lndq t657 = t656;
                    gm_ttk92ufj2f6eh t658;
                    memset(&t658, 0, sizeof t658);
                    ((uint8_t *)&t658)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t658 + 16), t657);
                    t645 = t658;
ix646:;
                    gm_ttk92ufj2f6eh t659 = t645;
                    uint8_t t660 = ((((uint8_t *)&t659))[0] != 0);
                    F.ret = t645;
                    goto gm_fret;
                }
                goto ix630;
ie630:;
                {
                    void * t662 = gm_l0;
                    gm_tl874t2atnlh3 t663 = gm_l5;
                    gm_b16_8 t664;
                    GM_ST(void *, (uint8_t *)&t664, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t664 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t665;
                    gm_f_unsupported_2seq49l((void *)&t665, t662, GM_CONV(gm_cseahvg8v7p0vs, t663), t664);
                    gm_ttk92ufj2f6eh t666;
                    GM_ST(uint8_t, (uint8_t *)&t666 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t666 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t666 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t666 + 8, 0);
                    ((uint8_t *)&t666)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t666 + 16), t665);
                    gm_ttk92ufj2f6eh t667 = t666;
                    uint8_t t668 = ((((uint8_t *)&t667))[0] != 0);
                    F.ret = t666;
                    goto gm_fret;
                }
ix630:;
            }
            goto me620;
mn626:;
ma622:;
            uint8_t t670 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t670 != ((uint8_t)1ULL)) goto mn669;
            double t671 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
            gm_l36 = t671;
            {
                uint8_t t674 = GM_LD(uint8_t, (((uint8_t *)&F.l32) + 0));
                if (t674 != ((uint8_t)1ULL)) goto ie673;
                double t675 = GM_LD(double, (((uint8_t *)&F.l32) + 16));
                gm_l37 = t675;
                {
                    double t678 = gm_l36;
                    double t679 = gm_l36;
                    uint8_t t680 = (!(t678 == t679));
                    if (!t680) goto ie677;
                    {
                        gm_t6evimt84lndq t682 = F.l32;
                        gm_ttk92ufj2f6eh t683;
                        memset(&t683, 0, sizeof t683);
                        ((uint8_t *)&t683)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t683 + 16), t682);
                        gm_ttk92ufj2f6eh t684 = t683;
                        uint8_t t685 = ((((uint8_t *)&t684))[0] != 0);
                        F.ret = t683;
                        goto gm_fret;
                    }
                    goto ix677;
ie677:;
ix677:;
                }
                {
                    double t687 = gm_l37;
                    double t688 = gm_l37;
                    uint8_t t689 = (!(t687 == t688));
                    if (!t689) goto ie686;
                    {
                        gm_t6evimt84lndq t691 = F.l10;
                        gm_ttk92ufj2f6eh t692;
                        memset(&t692, 0, sizeof t692);
                        ((uint8_t *)&t692)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t692 + 16), t691);
                        gm_ttk92ufj2f6eh t693 = t692;
                        uint8_t t694 = ((((uint8_t *)&t693))[0] != 0);
                        F.ret = t692;
                        goto gm_fret;
                    }
                    goto ix686;
ie686:;
ix686:;
                }
                {
                    gm_ttk92ufj2f6eh t695;
                    double t697 = gm_l36;
                    double t698 = gm_l37;
                    uint8_t t699 = (t697 <= t698);
                    uint8_t t700 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)16ULL)));
                    uint8_t t701 = ((t699 == t700));
                    if (!t701) goto ie696;
                    gm_t6evimt84lndq t703 = F.l10;
                    gm_t6evimt84lndq t704 = t703;
                    gm_ttk92ufj2f6eh t705;
                    memset(&t705, 0, sizeof t705);
                    ((uint8_t *)&t705)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t705 + 16), t704);
                    t695 = t705;
                    goto ix696;
ie696:;
                    gm_t6evimt84lndq t707 = F.l32;
                    gm_t6evimt84lndq t708 = t707;
                    gm_ttk92ufj2f6eh t709;
                    memset(&t709, 0, sizeof t709);
                    ((uint8_t *)&t709)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t709 + 16), t708);
                    t695 = t709;
ix696:;
                    gm_ttk92ufj2f6eh t710 = t695;
                    uint8_t t711 = ((((uint8_t *)&t710))[0] != 0);
                    F.ret = t695;
                    goto gm_fret;
                }
                goto ix673;
ie673:;
                {
                    void * t713 = gm_l0;
                    gm_tl874t2atnlh3 t714 = gm_l5;
                    gm_b16_8 t715;
                    GM_ST(void *, (uint8_t *)&t715, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t715 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t716;
                    gm_f_unsupported_2seq49l((void *)&t716, t713, GM_CONV(gm_cseahvg8v7p0vs, t714), t715);
                    gm_ttk92ufj2f6eh t717;
                    GM_ST(uint8_t, (uint8_t *)&t717 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t717 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t717 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t717 + 8, 0);
                    ((uint8_t *)&t717)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t717 + 16), t716);
                    gm_ttk92ufj2f6eh t718 = t717;
                    uint8_t t719 = ((((uint8_t *)&t718))[0] != 0);
                    F.ret = t717;
                    goto gm_fret;
                }
ix673:;
            }
            goto me620;
mn669:;
ma623:;
            void * t721 = gm_l0;
            gm_tl874t2atnlh3 t722 = gm_l5;
            gm_b16_8 t723;
            GM_ST(void *, (uint8_t *)&t723, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t723 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t724;
            gm_f_unsupported_2seq49l((void *)&t724, t721, GM_CONV(gm_cseahvg8v7p0vs, t722), t723);
            gm_ttk92ufj2f6eh t725;
            GM_ST(uint8_t, (uint8_t *)&t725 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t725 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t725 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t725 + 8, 0);
            ((uint8_t *)&t725)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t725 + 16), t724);
            gm_ttk92ufj2f6eh t726 = t725;
            uint8_t t727 = ((((uint8_t *)&t726))[0] != 0);
            F.ret = t725;
            goto gm_fret;
            goto me620;
mn720:;
ma624:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 437, 13);
me620:;
        }
        goto me136;
mn610:;
ma142:;
        uint8_t t729 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t729 != ((uint8_t)18ULL)) goto mn728;
        {
            uint8_t t732 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t732 != ((uint8_t)0ULL)) goto ie731;
            unsigned __int128 t733 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
            gm_l38 = t733;
            {
                uint64_t t736 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t737 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                if (t736 >= (uintptr_t)(t737)) gm_trap_index(t736, t737, gm_srcue4ionbe9lgh, 454, 25);
                uint8_t t738 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t736 * 48) + 0));
                if (t738 != ((uint8_t)0ULL)) goto ie735;
                unsigned __int128 t739 = GM_LD(unsigned __int128, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t736 * 48) + 16));
                gm_l39 = t739;
                {
                    uint64_t t742 = (uintptr_t)((uint64_t)2ULL);
                    uint64_t t743 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    if (t742 >= (uintptr_t)(t743)) gm_trap_index(t742, t743, gm_srcue4ionbe9lgh, 455, 25);
                    uint8_t t744 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t742 * 48) + 0));
                    if (t744 != ((uint8_t)0ULL)) goto ie741;
                    unsigned __int128 t745 = GM_LD(unsigned __int128, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t742 * 48) + 16));
                    gm_l40 = t745;
                    {
                        void * t747 = gm_l0;
                        unsigned __int128 t748 = gm_l38;
                        gm_t88rcucg1b7uk t749 = gm_l11;
                        gm_f_big_of_1cq0qao((void *)&F.l41, (void *)NULL, cx, NULL, t747, t748, t749);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_tbci5ospk5nnn t751 = F.l41;
                        void * t752 = gm_l0;
                        unsigned __int128 t753 = gm_l39;
                        gm_t88rcucg1b7uk t754 = gm_l11;
                        gm_tbci5ospk5nnn t755;
                        gm_f_big_of_1cq0qao((void *)&t755, (void *)NULL, cx, NULL, t752, t753, t754);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int32_t t756 = gm_f_cmp_i1knrgu(&t751, &t755);
                        uint8_t t757 = (t756 < ((int32_t)0LL));
                        if (!t757) goto ie750;
                        {
                            uint64_t t759 = (uintptr_t)((uint64_t)1ULL);
                            uint64_t t760 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            if (t759 >= (uintptr_t)(t760)) gm_trap_index(t759, t760, gm_srcue4ionbe9lgh, 457, 53);
                            gm_t6evimt84lndq t761 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t759 * 48));
                            gm_ttk92ufj2f6eh t762;
                            memset(&t762, 0, sizeof t762);
                            ((uint8_t *)&t762)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t762 + 16), t761);
                            gm_ttk92ufj2f6eh t763 = t762;
                            uint8_t t764 = ((((uint8_t *)&t763))[0] != 0);
                            F.ret = t762;
                            goto gm_fret;
                        }
                        goto ix750;
ie750:;
ix750:;
                    }
                    {
                        gm_tbci5ospk5nnn t766 = F.l41;
                        void * t767 = gm_l0;
                        unsigned __int128 t768 = gm_l40;
                        gm_t88rcucg1b7uk t769 = gm_l11;
                        gm_tbci5ospk5nnn t770;
                        gm_f_big_of_1cq0qao((void *)&t770, (void *)NULL, cx, NULL, t767, t768, t769);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        int32_t t771 = gm_f_cmp_i1knrgu(&t766, &t770);
                        uint8_t t772 = (t771 > ((int32_t)0LL));
                        if (!t772) goto ie765;
                        {
                            uint64_t t774 = (uintptr_t)((uint64_t)2ULL);
                            uint64_t t775 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                            if (t774 >= (uintptr_t)(t775)) gm_trap_index(t774, t775, gm_srcue4ionbe9lgh, 458, 53);
                            gm_t6evimt84lndq t776 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t774 * 48));
                            gm_ttk92ufj2f6eh t777;
                            memset(&t777, 0, sizeof t777);
                            ((uint8_t *)&t777)[0] = 0;
                            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t777 + 16), t776);
                            gm_ttk92ufj2f6eh t778 = t777;
                            uint8_t t779 = ((((uint8_t *)&t778))[0] != 0);
                            F.ret = t777;
                            goto gm_fret;
                        }
                        goto ix765;
ie765:;
ix765:;
                    }
                    {
                        gm_t6evimt84lndq t780 = F.l10;
                        gm_ttk92ufj2f6eh t781;
                        memset(&t781, 0, sizeof t781);
                        ((uint8_t *)&t781)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t781 + 16), t780);
                        gm_ttk92ufj2f6eh t782 = t781;
                        uint8_t t783 = ((((uint8_t *)&t782))[0] != 0);
                        F.ret = t781;
                        goto gm_fret;
                    }
                    goto ix741;
ie741:;
                    {
                        void * t785 = gm_l0;
                        gm_tl874t2atnlh3 t786 = gm_l5;
                        gm_b16_8 t787;
                        GM_ST(void *, (uint8_t *)&t787, (void *)(gm_sv7lsha1e0i8r));
                        GM_ST(uintptr_t, (uint8_t *)&t787 + 8, (uintptr_t)(12));
                        gm_t39uai1ajk3qv t788;
                        gm_f_unsupported_2seq49l((void *)&t788, t785, GM_CONV(gm_cseahvg8v7p0vs, t786), t787);
                        gm_ttk92ufj2f6eh t789;
                        GM_ST(uint8_t, (uint8_t *)&t789 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t789 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t789 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t789 + 8, 0);
                        ((uint8_t *)&t789)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t789 + 16), t788);
                        gm_ttk92ufj2f6eh t790 = t789;
                        uint8_t t791 = ((((uint8_t *)&t790))[0] != 0);
                        F.ret = t789;
                        goto gm_fret;
                    }
ix741:;
                }
                goto ix735;
ie735:;
                {
                    void * t793 = gm_l0;
                    gm_tl874t2atnlh3 t794 = gm_l5;
                    gm_b16_8 t795;
                    GM_ST(void *, (uint8_t *)&t795, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t795 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t796;
                    gm_f_unsupported_2seq49l((void *)&t796, t793, GM_CONV(gm_cseahvg8v7p0vs, t794), t795);
                    gm_ttk92ufj2f6eh t797;
                    GM_ST(uint8_t, (uint8_t *)&t797 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t797 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t797 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t797 + 8, 0);
                    ((uint8_t *)&t797)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t797 + 16), t796);
                    gm_ttk92ufj2f6eh t798 = t797;
                    uint8_t t799 = ((((uint8_t *)&t798))[0] != 0);
                    F.ret = t797;
                    goto gm_fret;
                }
ix735:;
            }
            goto ix731;
ie731:;
            {
                void * t801 = gm_l0;
                gm_tl874t2atnlh3 t802 = gm_l5;
                gm_b16_8 t803;
                GM_ST(void *, (uint8_t *)&t803, (void *)(gm_seg4idqqmijmd));
                GM_ST(uintptr_t, (uint8_t *)&t803 + 8, (uintptr_t)(33));
                gm_t39uai1ajk3qv t804;
                gm_f_unsupported_2seq49l((void *)&t804, t801, GM_CONV(gm_cseahvg8v7p0vs, t802), t803);
                gm_ttk92ufj2f6eh t805;
                GM_ST(uint8_t, (uint8_t *)&t805 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t805 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t805 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t805 + 8, 0);
                ((uint8_t *)&t805)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t805 + 16), t804);
                gm_ttk92ufj2f6eh t806 = t805;
                uint8_t t807 = ((((uint8_t *)&t806))[0] != 0);
                F.ret = t805;
                goto gm_fret;
            }
ix731:;
        }
        goto me136;
mn728:;
ma143:;
        uint8_t t809 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t809 != ((uint8_t)19ULL)) goto mn808;
        uint8_t t815 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        switch (t815) { case ((uint8_t)0ULL): goto ma811; case ((uint8_t)1ULL): goto ma812; case ((uint8_t)2ULL): goto ma813; case ((uint8_t)3ULL): goto ma813; case ((uint8_t)4ULL): goto ma813; case ((uint8_t)5ULL): goto ma813; case ((uint8_t)6ULL): goto ma813; case ((uint8_t)7ULL): goto ma813; case ((uint8_t)8ULL): goto ma813; case ((uint8_t)9ULL): goto ma813; case ((uint8_t)10ULL): goto ma813; case ((uint8_t)11ULL): goto ma813; case ((uint8_t)12ULL): goto ma813; case ((uint8_t)13ULL): goto ma813; default: goto ma813; }
ma811:;
        uint8_t t817 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t817 != ((uint8_t)0ULL)) goto mn816;
        unsigned __int128 t818 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
        gm_l42 = t818;
        {
            void * t820 = gm_l0;
            unsigned __int128 t821 = gm_l42;
            gm_t88rcucg1b7uk t822 = gm_l11;
            gm_f_big_of_1cq0qao((void *)&F.l43, (void *)NULL, cx, NULL, t820, t821, t822);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t823 = gm_l0;
            void * t824 = gm_l3;
            gm_t88rcucg1b7uk t825 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t824) + 0));
            gm_f_int_range_hue3vms((void *)&F.l44, (void *)NULL, cx, NULL, t823, t825);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_abs_v7cf214((void *)&F.l45, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l43)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            int32_t t827 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l45)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l44) + 48)));
            uint8_t t828 = (t827 > ((int32_t)0LL));
            if (!t828) goto ie826;
            {
                void * t830 = gm_l0;
                gm_tl874t2atnlh3 t831 = gm_l5;
                gm_b16_8 t832;
                GM_ST(void *, (uint8_t *)&t832, (void *)(gm_sjn4krvshoajd));
                GM_ST(uintptr_t, (uint8_t *)&t832 + 8, (uintptr_t)(25));
                gm_t39uai1ajk3qv t833;
                gm_f_trap_jftbss6((void *)&t833, (void *)NULL, cx, NULL, t830, t831, t832);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_ttk92ufj2f6eh t834;
                GM_ST(uint8_t, (uint8_t *)&t834 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t834 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t834 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t834 + 8, 0);
                ((uint8_t *)&t834)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t834 + 16), t833);
                gm_ttk92ufj2f6eh t835 = t834;
                uint8_t t836 = ((((uint8_t *)&t835))[0] != 0);
                F.ret = t834;
                goto gm_fret;
            }
            goto ix826;
ie826:;
ix826:;
        }
        {
            void * t837 = gm_l0;
            unsigned __int128 t838 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l45)));
            void * t839 = gm_l3;
            gm_t88rcucg1b7uk t840 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t839) + 0));
            unsigned __int128 t841 = gm_f_wrap_bt8r149(t837, t838, GM_CONV(gm_csguankko4lmhd, t840));
            gm_t6evimt84lndq t842;
            GM_ST(uint8_t, (uint8_t *)&t842 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t842 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t842 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t842 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t842 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t842 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t842 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t842 + 16), t841);
            gm_ttk92ufj2f6eh t843;
            memset(&t843, 0, sizeof t843);
            ((uint8_t *)&t843)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t843 + 16), t842);
            gm_ttk92ufj2f6eh t844 = t843;
            uint8_t t845 = ((((uint8_t *)&t844))[0] != 0);
            F.ret = t843;
            goto gm_fret;
        }
        goto me810;
mn816:;
ma812:;
        uint8_t t847 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
        if (t847 != ((uint8_t)1ULL)) goto mn846;
        double t848 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
        gm_l46 = t848;
        double t849;
        uint8_t t851 = 0;
        double t852 = gm_l46;
        uint8_t t853 = (t852 < ((double)0e0));
        if (!t853) {
            uint8_t t854 = 0;
            double t855 = gm_l46;
            uint8_t t856 = ((t855 == ((double)0e0)));
            if (t856) {
                double t857 = gm_l46;
                double t858 = t857;
                uint64_t t859;
                memset(&t859, 0, sizeof t859); memcpy(&t859, ((uint8_t *)&t858), sizeof t859 < 8 ? sizeof t859 : 8);
                unsigned __int128 n860 = (unsigned __int128)((uint64_t)63ULL);
                if (n860 >= 64) gm_trap_shift((uint64_t)n860, "u64", gm_srcue4ionbe9lgh, 469, 68);
                uint64_t t861 = (uint64_t)(t859 >> (int)n860);
                uint8_t t862 = ((t861 == ((uint64_t)1ULL)));
                t854 = t862;
            } else t854 = 0;
            t851 = t854;
        } else t851 = 1;
        if (!t851) goto ie850;
        double t864 = gm_l46;
        double t865 = -t864;
        double t866 = t865;
        t849 = t866;
        goto ix850;
ie850:;
        double t868 = gm_l46;
        double t869 = t868;
        t849 = t869;
ix850:;
        gm_t6evimt84lndq t870;
        memset(&t870, 0, sizeof t870);
        GM_ST(uint8_t, ((uint8_t *)&t870 + 0), ((uint8_t)1ULL));
        GM_ST(double, ((uint8_t *)&t870 + 16), t849);
        gm_ttk92ufj2f6eh t871;
        memset(&t871, 0, sizeof t871);
        ((uint8_t *)&t871)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t871 + 16), t870);
        gm_ttk92ufj2f6eh t872 = t871;
        uint8_t t873 = ((((uint8_t *)&t872))[0] != 0);
        F.ret = t871;
        goto gm_fret;
        goto me810;
mn846:;
ma813:;
        void * t875 = gm_l0;
        gm_tl874t2atnlh3 t876 = gm_l5;
        gm_b16_8 t877;
        GM_ST(void *, (uint8_t *)&t877, (void *)(gm_sv7lsha1e0i8r));
        GM_ST(uintptr_t, (uint8_t *)&t877 + 8, (uintptr_t)(12));
        gm_t39uai1ajk3qv t878;
        gm_f_unsupported_2seq49l((void *)&t878, t875, GM_CONV(gm_cseahvg8v7p0vs, t876), t877);
        gm_ttk92ufj2f6eh t879;
        GM_ST(uint8_t, (uint8_t *)&t879 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t879 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t879 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t879 + 8, 0);
        ((uint8_t *)&t879)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t879 + 16), t878);
        gm_ttk92ufj2f6eh t880 = t879;
        uint8_t t881 = ((((uint8_t *)&t880))[0] != 0);
        F.ret = t879;
        goto gm_fret;
        goto me810;
mn874:;
ma814:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 461, 17);
me810:;
        goto me136;
mn808:;
ma144:;
        uint8_t t883 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t883 != ((uint8_t)20ULL)) goto mn882;
        {
            uint8_t t886 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t886 != ((uint8_t)0ULL)) goto ie885;
            unsigned __int128 t887 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
            gm_l47 = t887;
            {
                uint64_t t890 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t891 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                if (t890 >= (uintptr_t)(t891)) gm_trap_index(t890, t891, gm_srcue4ionbe9lgh, 474, 24);
                uint8_t t892 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t890 * 48) + 0));
                if (t892 != ((uint8_t)0ULL)) goto ie889;
                unsigned __int128 t893 = GM_LD(unsigned __int128, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t890 * 48) + 16));
                gm_l48 = t893;
                {
                    void * t895 = gm_l0;
                    unsigned __int128 t896 = gm_l47;
                    gm_t88rcucg1b7uk t897 = gm_l11;
                    gm_tbci5ospk5nnn t898;
                    gm_f_big_of_1cq0qao((void *)&t898, (void *)NULL, cx, NULL, t895, t896, t897);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t899 = gm_l0;
                    unsigned __int128 t900 = gm_l48;
                    uint64_t t901 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t902 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    if (t901 >= (uintptr_t)(t902)) gm_trap_index(t901, t902, gm_srcue4ionbe9lgh, 475, 50);
                    gm_t88rcucg1b7uk t903 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t901 * 88) + 0));
                    gm_tbci5ospk5nnn t904;
                    gm_f_big_of_1cq0qao((void *)&t904, (void *)NULL, cx, NULL, t899, t900, t903);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tbci5ospk5nnn t905;
                    gm_f_sub_pktm5cr((void *)&t905, (void *)NULL, cx, NULL, &t898, &t904);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_abs_v7cf214((void *)&F.l49, (void *)NULL, cx, NULL, &t905);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    void * t906 = gm_l0;
                    unsigned __int128 t907 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l49)));
                    void * t908 = gm_l3;
                    gm_t88rcucg1b7uk t909 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t908) + 0));
                    unsigned __int128 t910 = gm_f_wrap_bt8r149(t906, t907, GM_CONV(gm_csguankko4lmhd, t909));
                    gm_t6evimt84lndq t911;
                    GM_ST(uint8_t, (uint8_t *)&t911 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t911 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t911 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t911 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t911 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t911 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t911 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t911 + 16), t910);
                    gm_ttk92ufj2f6eh t912;
                    memset(&t912, 0, sizeof t912);
                    ((uint8_t *)&t912)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t912 + 16), t911);
                    gm_ttk92ufj2f6eh t913 = t912;
                    uint8_t t914 = ((((uint8_t *)&t913))[0] != 0);
                    F.ret = t912;
                    goto gm_fret;
                }
                goto ix889;
ie889:;
                {
                    void * t916 = gm_l0;
                    gm_tl874t2atnlh3 t917 = gm_l5;
                    gm_b16_8 t918;
                    GM_ST(void *, (uint8_t *)&t918, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t918 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t919;
                    gm_f_unsupported_2seq49l((void *)&t919, t916, GM_CONV(gm_cseahvg8v7p0vs, t917), t918);
                    gm_ttk92ufj2f6eh t920;
                    GM_ST(uint8_t, (uint8_t *)&t920 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t920 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t920 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t920 + 8, 0);
                    ((uint8_t *)&t920)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t920 + 16), t919);
                    gm_ttk92ufj2f6eh t921 = t920;
                    uint8_t t922 = ((((uint8_t *)&t921))[0] != 0);
                    F.ret = t920;
                    goto gm_fret;
                }
ix889:;
            }
            goto ix885;
ie885:;
            {
                void * t924 = gm_l0;
                gm_tl874t2atnlh3 t925 = gm_l5;
                gm_b16_8 t926;
                GM_ST(void *, (uint8_t *)&t926, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t926 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t927;
                gm_f_unsupported_2seq49l((void *)&t927, t924, GM_CONV(gm_cseahvg8v7p0vs, t925), t926);
                gm_ttk92ufj2f6eh t928;
                GM_ST(uint8_t, (uint8_t *)&t928 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t928 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t928 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t928 + 8, 0);
                ((uint8_t *)&t928)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t928 + 16), t927);
                gm_ttk92ufj2f6eh t929 = t928;
                uint8_t t930 = ((((uint8_t *)&t929))[0] != 0);
                F.ret = t928;
                goto gm_fret;
            }
ix885:;
        }
        goto me136;
mn882:;
ma145:;
        uint8_t t932 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t932 != ((uint8_t)21ULL)) goto mn931;
        gm_t9qkh0rsooc8d t933 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 4));
        gm_l50 = t933;
        {
            uint8_t t936 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t936 != ((uint8_t)0ULL)) goto ie935;
            unsigned __int128 t937 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
            gm_l51 = t937;
            {
                uint64_t t940 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t941 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                if (t940 >= (uintptr_t)(t941)) gm_trap_index(t940, t941, gm_srcue4ionbe9lgh, 480, 24);
                uint8_t t942 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t940 * 48) + 0));
                if (t942 != ((uint8_t)0ULL)) goto ie939;
                unsigned __int128 t943 = GM_LD(unsigned __int128, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t940 * 48) + 16));
                gm_l52 = t943;
                {
                    void * t945 = gm_l0;
                    unsigned __int128 t946 = gm_l51;
                    gm_t88rcucg1b7uk t947 = gm_l11;
                    gm_tbci5ospk5nnn t948;
                    gm_f_big_of_1cq0qao((void *)&t948, (void *)NULL, cx, NULL, t945, t946, t947);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    unsigned __int128 t949 = gm_l52;
                    uint32_t t950 = (uint32_t)t949;
                    gm_f_pow_mhlb5fr((void *)&F.l53, (void *)NULL, cx, NULL, &t948, t950);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    gm_t88rcucg1b7uk t951 = gm_l11;
                    gm_l54 = t951;
                }
                {
                    void * t952 = gm_l0;
                    gm_t88rcucg1b7uk t953 = gm_l54;
                    gm_f_int_range_hue3vms((void *)&F.l55, (void *)NULL, cx, NULL, t952, t953);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    uint8_t t954 = 0;
                    int32_t t955 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l53)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l55) + 0)));
                    uint8_t t956 = (t955 >= ((int32_t)0LL));
                    if (t956) {
                        int32_t t957 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l53)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l55) + 48)));
                        uint8_t t958 = (t957 <= ((int32_t)0LL));
                        t954 = t958;
                    } else t954 = 0;
                    gm_l56 = t954;
                }
                {
                    uint8_t t964 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    switch (t964) { case ((uint8_t)0ULL): goto ma961; case ((uint8_t)1ULL): goto ma962; case ((uint8_t)2ULL): goto ma962; case ((uint8_t)3ULL): goto ma960; default: goto ma962; }
ma960:;
                    uint8_t t966 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    if (t966 != ((uint8_t)3ULL)) goto mn965;
                    gm_ttk92ufj2f6eh t967;
                    uint8_t t969 = gm_l56;
                    if (!t969) goto ie968;
                    void * t971 = gm_l0;
                    unsigned __int128 t972 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l53)));
                    gm_t88rcucg1b7uk t973 = gm_l54;
                    unsigned __int128 t974 = gm_f_wrap_bt8r149(t971, t972, GM_CONV(gm_csguankko4lmhd, t973));
                    gm_t6evimt84lndq t975;
                    GM_ST(uint8_t, (uint8_t *)&t975 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t975 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t975 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t975 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t975 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t975 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t975 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t975 + 16), t974);
                    gm_tq7s98ihonkju t976 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t975, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ksjvoab7999)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tq7s98ihonkju t977 = t976;
                    if (((((uint8_t *)&t977))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t977) + 8, gm_srcue4ionbe9lgh, 486, 53);
                    void * t978 = GM_LD(void *, (((uint8_t *)&t977) + 8));
                    gm_t6evimt84lndq t979;
                    memset(&t979, 0, sizeof t979);
                    GM_ST(uint8_t, ((uint8_t *)&t979 + 0), ((uint8_t)7ULL));
                    GM_ST(void *, ((uint8_t *)&t979 + 16), t978);
                    gm_t6evimt84lndq t980 = t979;
                    gm_ttk92ufj2f6eh t981;
                    memset(&t981, 0, sizeof t981);
                    ((uint8_t *)&t981)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t981 + 16), t980);
                    t967 = t981;
                    goto ix968;
ie968:;
                    gm_t6evimt84lndq t983;
                    memset(&t983, 0, sizeof t983);
                    GM_ST(uint8_t, ((uint8_t *)&t983 + 0), ((uint8_t)6ULL));
                    gm_t6evimt84lndq t984 = t983;
                    gm_ttk92ufj2f6eh t985;
                    memset(&t985, 0, sizeof t985);
                    ((uint8_t *)&t985)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t985 + 16), t984);
                    t967 = t985;
ix968:;
                    gm_ttk92ufj2f6eh t986 = t967;
                    uint8_t t987 = ((((uint8_t *)&t986))[0] != 0);
                    F.ret = t967;
                    goto gm_fret;
                    goto me959;
mn965:;
ma961:;
                    uint8_t t989 = GM_LD(uint8_t, (((uint8_t *)&gm_l50) + 0));
                    if (t989 != ((uint8_t)0ULL)) goto mn988;
                    uint8_t t991 = gm_l56;
                    uint8_t t992 = (uint8_t)!t991;
                    if (!t992) goto ie990;
                    {
                        void * t994 = gm_l0;
                        unsigned __int128 t995 = gm_l51;
                        gm_t88rcucg1b7uk t996 = gm_l11;
                        gm_tbci5ospk5nnn t997;
                        gm_f_big_of_1cq0qao((void *)&t997, (void *)NULL, cx, NULL, t994, t995, t996);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_b16_8 t998 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &t997);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_l57 = t998;
                    }
                    {
                        void * t999 = gm_l0;
                        gm_tl874t2atnlh3 t1000 = gm_l5;
                        gm_tou0girmhri5g t1001;
                        gm_b16_8 t1002 = gm_l57;
                        GM_ST(gm_b16_8, (uint8_t *)&t1001 + 0, t1002);
                        unsigned __int128 t1003 = gm_l52;
                        GM_ST(unsigned __int128, (uint8_t *)&t1001 + 16, t1003);
                        gm_tabb710d9mq2e t1004;
                        gm_f_format_0sujqbp((void *)&t1004, (void *)NULL, cx, NULL, &t1001);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t1005 = t1004;
                        if (((((uint8_t *)&t1005))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1005) + 8, gm_srcue4ionbe9lgh, 489, 42);
                        gm_b16_8 t1006 = GM_LD(gm_b16_8, (((uint8_t *)&t1005) + 8));
                        gm_b16_8 t1007 = t1006;
                        gm_b16_8 t1008 = GM_LD(gm_b16_8, ((uint8_t *)&t1007));
                        gm_t39uai1ajk3qv t1009;
                        gm_f_trap_jftbss6((void *)&t1009, (void *)NULL, cx, NULL, t999, t1000, t1008);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_ttk92ufj2f6eh t1010;
                        GM_ST(uint8_t, (uint8_t *)&t1010 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1010 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1010 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t1010 + 8, 0);
                        ((uint8_t *)&t1010)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1010 + 16), t1009);
                        gm_ttk92ufj2f6eh t1011 = t1010;
                        uint8_t t1012 = ((((uint8_t *)&t1011))[0] != 0);
                        F.ret = t1010;
                        goto gm_fret;
                    }
                    goto ix990;
ie990:;
ix990:;
                    goto me959;
mn988:;
ma962:;
                    goto me959;
mn1013:;
ma963:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 485, 13);
me959:;
                }
                {
                    void * t1015 = gm_l0;
                    gm_t9qkh0rsooc8d t1016 = gm_l50;
                    gm_t88rcucg1b7uk t1017 = gm_l54;
                    gm_b16_8 t1018;
                    GM_ST(void *, (uint8_t *)&t1018, (void *)(gm_svpofgjtij7vj));
                    GM_ST(uintptr_t, (uint8_t *)&t1018 + 8, (uintptr_t)(3));
                    gm_tl874t2atnlh3 t1019 = gm_l5;
                    gm_t8carr7mku484 t1020;
                    gm_f_fit_8hvi4bv((void *)&t1020, (void *)NULL, cx, NULL, t1015, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l53)), t1016, t1017, t1018, t1019);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t8carr7mku484 t1021 = t1020;
                    if (((((uint8_t *)&t1021))[0] != 0)) {
                        gm_t39uai1ajk3qv t1022 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t1021) + 16));
                        gm_ttk92ufj2f6eh t1023;
                        GM_ST(uint8_t, (uint8_t *)&t1023 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1023 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1023 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t1023 + 8, 0);
                        ((uint8_t *)&t1023)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1023 + 16), t1022);
                        F.ret = t1023;
                        goto gm_fret;
                    }
                    unsigned __int128 t1024 = GM_LD(unsigned __int128, (((uint8_t *)&t1021) + 16));
                    gm_t6evimt84lndq t1025;
                    GM_ST(uint8_t, (uint8_t *)&t1025 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1025 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1025 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1025 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1025 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1025 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t1025 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t1025 + 16), t1024);
                    gm_ttk92ufj2f6eh t1026;
                    memset(&t1026, 0, sizeof t1026);
                    ((uint8_t *)&t1026)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1026 + 16), t1025);
                    gm_ttk92ufj2f6eh t1027 = t1026;
                    uint8_t t1028 = ((((uint8_t *)&t1027))[0] != 0);
                    F.ret = t1026;
                    goto gm_fret;
                }
                goto ix939;
ie939:;
                {
                    void * t1030 = gm_l0;
                    gm_tl874t2atnlh3 t1031 = gm_l5;
                    gm_b16_8 t1032;
                    GM_ST(void *, (uint8_t *)&t1032, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t1032 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t1033;
                    gm_f_unsupported_2seq49l((void *)&t1033, t1030, GM_CONV(gm_cseahvg8v7p0vs, t1031), t1032);
                    gm_ttk92ufj2f6eh t1034;
                    GM_ST(uint8_t, (uint8_t *)&t1034 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1034 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1034 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1034 + 8, 0);
                    ((uint8_t *)&t1034)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1034 + 16), t1033);
                    gm_ttk92ufj2f6eh t1035 = t1034;
                    uint8_t t1036 = ((((uint8_t *)&t1035))[0] != 0);
                    F.ret = t1034;
                    goto gm_fret;
                }
ix939:;
            }
            goto ix935;
ie935:;
            {
                void * t1038 = gm_l0;
                gm_tl874t2atnlh3 t1039 = gm_l5;
                gm_b16_8 t1040;
                GM_ST(void *, (uint8_t *)&t1040, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t1040 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t1041;
                gm_f_unsupported_2seq49l((void *)&t1041, t1038, GM_CONV(gm_cseahvg8v7p0vs, t1039), t1040);
                gm_ttk92ufj2f6eh t1042;
                GM_ST(uint8_t, (uint8_t *)&t1042 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1042 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1042 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1042 + 8, 0);
                ((uint8_t *)&t1042)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1042 + 16), t1041);
                gm_ttk92ufj2f6eh t1043 = t1042;
                uint8_t t1044 = ((((uint8_t *)&t1043))[0] != 0);
                F.ret = t1042;
                goto gm_fret;
            }
ix935:;
        }
        goto me136;
mn931:;
ma146:;
        uint8_t t1046 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1046 != ((uint8_t)22ULL)) goto mn1045;
        {
            uint8_t t1049 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1049 != ((uint8_t)1ULL)) goto ie1048;
            double t1050 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
            gm_l58 = t1050;
            {
                double t1052 = gm_l58;
                double t1053 = gm_l58;
                uint8_t t1054 = (!(t1052 == t1053));
                gm_t6evimt84lndq t1055;
                memset(&t1055, 0, sizeof t1055);
                GM_ST(uint8_t, ((uint8_t *)&t1055 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t1055 + 16), t1054);
                gm_ttk92ufj2f6eh t1056;
                memset(&t1056, 0, sizeof t1056);
                ((uint8_t *)&t1056)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1056 + 16), t1055);
                gm_ttk92ufj2f6eh t1057 = t1056;
                uint8_t t1058 = ((((uint8_t *)&t1057))[0] != 0);
                F.ret = t1056;
                goto gm_fret;
            }
            goto ix1048;
ie1048:;
            {
                void * t1060 = gm_l0;
                gm_tl874t2atnlh3 t1061 = gm_l5;
                gm_b16_8 t1062;
                GM_ST(void *, (uint8_t *)&t1062, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t1062 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t1063;
                gm_f_unsupported_2seq49l((void *)&t1063, t1060, GM_CONV(gm_cseahvg8v7p0vs, t1061), t1062);
                gm_ttk92ufj2f6eh t1064;
                GM_ST(uint8_t, (uint8_t *)&t1064 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1064 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1064 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1064 + 8, 0);
                ((uint8_t *)&t1064)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1064 + 16), t1063);
                gm_ttk92ufj2f6eh t1065 = t1064;
                uint8_t t1066 = ((((uint8_t *)&t1065))[0] != 0);
                F.ret = t1064;
                goto gm_fret;
            }
ix1048:;
        }
        goto me136;
mn1045:;
ma147:;
        uint8_t t1068 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1068 != ((uint8_t)23ULL)) goto mn1067;
        {
            uint8_t t1071 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1071 != ((uint8_t)1ULL)) goto ie1070;
            double t1072 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
            gm_l59 = t1072;
            {
                uint8_t t1074 = 0;
                double t1075 = gm_l59;
                double t1076 = gm_l59;
                uint8_t t1077 = ((t1075 == t1076));
                if (t1077) {
                    double t1078 = gm_l59;
                    double t1079 = gm_l59;
                    double t1080 = (t1078 - t1079);
                    uint8_t t1081 = (!(t1080 == ((double)0e0)));
                    t1074 = t1081;
                } else t1074 = 0;
                gm_t6evimt84lndq t1082;
                memset(&t1082, 0, sizeof t1082);
                GM_ST(uint8_t, ((uint8_t *)&t1082 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t1082 + 16), t1074);
                gm_ttk92ufj2f6eh t1083;
                memset(&t1083, 0, sizeof t1083);
                ((uint8_t *)&t1083)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1083 + 16), t1082);
                gm_ttk92ufj2f6eh t1084 = t1083;
                uint8_t t1085 = ((((uint8_t *)&t1084))[0] != 0);
                F.ret = t1083;
                goto gm_fret;
            }
            goto ix1070;
ie1070:;
            {
                void * t1087 = gm_l0;
                gm_tl874t2atnlh3 t1088 = gm_l5;
                gm_b16_8 t1089;
                GM_ST(void *, (uint8_t *)&t1089, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t1089 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t1090;
                gm_f_unsupported_2seq49l((void *)&t1090, t1087, GM_CONV(gm_cseahvg8v7p0vs, t1088), t1089);
                gm_ttk92ufj2f6eh t1091;
                GM_ST(uint8_t, (uint8_t *)&t1091 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1091 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1091 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1091 + 8, 0);
                ((uint8_t *)&t1091)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1091 + 16), t1090);
                gm_ttk92ufj2f6eh t1092 = t1091;
                uint8_t t1093 = ((((uint8_t *)&t1092))[0] != 0);
                F.ret = t1091;
                goto gm_fret;
            }
ix1070:;
        }
        goto me136;
mn1067:;
ma148:;
        uint8_t t1095 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1095 != ((uint8_t)24ULL)) goto mn1094;
        {
            uint8_t t1098 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1098 != ((uint8_t)1ULL)) goto ie1097;
            double t1099 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
            gm_l60 = t1099;
            {
                double t1101 = gm_l60;
                double t1102 = gm_l60;
                double t1103 = (t1101 - t1102);
                uint8_t t1104 = ((t1103 == ((double)0e0)));
                gm_t6evimt84lndq t1105;
                memset(&t1105, 0, sizeof t1105);
                GM_ST(uint8_t, ((uint8_t *)&t1105 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t1105 + 16), t1104);
                gm_ttk92ufj2f6eh t1106;
                memset(&t1106, 0, sizeof t1106);
                ((uint8_t *)&t1106)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1106 + 16), t1105);
                gm_ttk92ufj2f6eh t1107 = t1106;
                uint8_t t1108 = ((((uint8_t *)&t1107))[0] != 0);
                F.ret = t1106;
                goto gm_fret;
            }
            goto ix1097;
ie1097:;
            {
                void * t1110 = gm_l0;
                gm_tl874t2atnlh3 t1111 = gm_l5;
                gm_b16_8 t1112;
                GM_ST(void *, (uint8_t *)&t1112, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t1112 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t1113;
                gm_f_unsupported_2seq49l((void *)&t1113, t1110, GM_CONV(gm_cseahvg8v7p0vs, t1111), t1112);
                gm_ttk92ufj2f6eh t1114;
                GM_ST(uint8_t, (uint8_t *)&t1114 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1114 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1114 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1114 + 8, 0);
                ((uint8_t *)&t1114)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1114 + 16), t1113);
                gm_ttk92ufj2f6eh t1115 = t1114;
                uint8_t t1116 = ((((uint8_t *)&t1115))[0] != 0);
                F.ret = t1114;
                goto gm_fret;
            }
ix1097:;
        }
        goto me136;
mn1094:;
ma149:;
        uint8_t t1118 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1118 != ((uint8_t)25ULL)) goto mn1117;
        {
            uint8_t t1121 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1121 != ((uint8_t)1ULL)) goto ie1120;
            double t1122 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
            gm_l61 = t1122;
            {
                double t1124 = gm_l61;
                double t1125 = t1124;
                uint64_t t1126;
                memset(&t1126, 0, sizeof t1126); memcpy(&t1126, ((uint8_t *)&t1125), sizeof t1126 < 8 ? sizeof t1126 : 8);
                unsigned __int128 n1127 = (unsigned __int128)((uint64_t)63ULL);
                if (n1127 >= 64) gm_trap_shift((uint64_t)n1127, "u64", gm_srcue4ionbe9lgh, 509, 26);
                uint64_t t1128 = (uint64_t)(t1126 >> (int)n1127);
                uint8_t t1129 = ((t1128 == ((uint64_t)1ULL)));
                gm_t6evimt84lndq t1130;
                memset(&t1130, 0, sizeof t1130);
                GM_ST(uint8_t, ((uint8_t *)&t1130 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t1130 + 16), t1129);
                gm_ttk92ufj2f6eh t1131;
                memset(&t1131, 0, sizeof t1131);
                ((uint8_t *)&t1131)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1131 + 16), t1130);
                gm_ttk92ufj2f6eh t1132 = t1131;
                uint8_t t1133 = ((((uint8_t *)&t1132))[0] != 0);
                F.ret = t1131;
                goto gm_fret;
            }
            goto ix1120;
ie1120:;
            {
                void * t1135 = gm_l0;
                gm_tl874t2atnlh3 t1136 = gm_l5;
                gm_b16_8 t1137;
                GM_ST(void *, (uint8_t *)&t1137, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t1137 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t1138;
                gm_f_unsupported_2seq49l((void *)&t1138, t1135, GM_CONV(gm_cseahvg8v7p0vs, t1136), t1137);
                gm_ttk92ufj2f6eh t1139;
                GM_ST(uint8_t, (uint8_t *)&t1139 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1139 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1139 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1139 + 8, 0);
                ((uint8_t *)&t1139)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1139 + 16), t1138);
                gm_ttk92ufj2f6eh t1140 = t1139;
                uint8_t t1141 = ((((uint8_t *)&t1140))[0] != 0);
                F.ret = t1139;
                goto gm_fret;
            }
ix1120:;
        }
        goto me136;
mn1117:;
ma150:;
        uint8_t t1143 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1143 != ((uint8_t)14ULL)) goto mn1142;
        {
            uint8_t t1146 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1146 != ((uint8_t)10ULL)) goto ie1145;
            uint32_t t1147 = GM_LD(uint32_t, ((((uint8_t *)&F.l10) + 16) + 0));
            gm_l62 = t1147;
            {
                void * t1149 = gm_l0;
                gm_t88rcucg1b7uk t1150 = gm_l11;
                uint32_t t1151 = gm_l62;
                gm_thrdpodrs8pgt t1152;
                gm_f_variant_tag_4pbamin((void *)&t1152, t1149, GM_CONV(gm_csguankko4lmhd, t1150), t1151);
                gm_thrdpodrs8pgt t1153 = t1152;
                gm_tbci5ospk5nnn t1154;
                if (((((uint8_t *)&t1153))[48] != 0)) {
                    gm_tbci5ospk5nnn t1155 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&t1153) + 0));
                    t1154 = t1155;
                } else {
                    void * t1156 = gm_l0;
                    gm_tl874t2atnlh3 t1157 = gm_l5;
                    gm_b16_8 t1158;
                    GM_ST(void *, (uint8_t *)&t1158, (void *)(gm_sv7lsha1e0i8r));
                    GM_ST(uintptr_t, (uint8_t *)&t1158 + 8, (uintptr_t)(12));
                    gm_t39uai1ajk3qv t1159;
                    gm_f_unsupported_2seq49l((void *)&t1159, t1156, GM_CONV(gm_cseahvg8v7p0vs, t1157), t1158);
                    gm_ttk92ufj2f6eh t1160;
                    GM_ST(uint8_t, (uint8_t *)&t1160 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1160 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1160 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1160 + 8, 0);
                    ((uint8_t *)&t1160)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1160 + 16), t1159);
                    gm_ttk92ufj2f6eh t1161 = t1160;
                    uint8_t t1162 = ((((uint8_t *)&t1161))[0] != 0);
                    F.ret = t1160;
                    goto gm_fret;
                }
                F.l63 = t1154;
            }
            {
                void * t1163 = gm_l0;
                unsigned __int128 t1164 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l63)));
                void * t1165 = gm_l3;
                gm_t88rcucg1b7uk t1166 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1165) + 0));
                unsigned __int128 t1167 = gm_f_wrap_bt8r149(t1163, t1164, GM_CONV(gm_csguankko4lmhd, t1166));
                gm_t6evimt84lndq t1168;
                GM_ST(uint8_t, (uint8_t *)&t1168 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1168 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1168 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1168 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t1168 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t1168 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1168 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t1168 + 16), t1167);
                gm_ttk92ufj2f6eh t1169;
                memset(&t1169, 0, sizeof t1169);
                ((uint8_t *)&t1169)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1169 + 16), t1168);
                gm_ttk92ufj2f6eh t1170 = t1169;
                uint8_t t1171 = ((((uint8_t *)&t1170))[0] != 0);
                F.ret = t1169;
                goto gm_fret;
            }
            goto ix1145;
ie1145:;
            {
                void * t1173 = gm_l0;
                gm_tl874t2atnlh3 t1174 = gm_l5;
                gm_b16_8 t1175;
                GM_ST(void *, (uint8_t *)&t1175, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t1175 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t1176;
                gm_f_unsupported_2seq49l((void *)&t1176, t1173, GM_CONV(gm_cseahvg8v7p0vs, t1174), t1175);
                gm_ttk92ufj2f6eh t1177;
                GM_ST(uint8_t, (uint8_t *)&t1177 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1177 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1177 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1177 + 8, 0);
                ((uint8_t *)&t1177)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1177 + 16), t1176);
                gm_ttk92ufj2f6eh t1178 = t1177;
                uint8_t t1179 = ((((uint8_t *)&t1178))[0] != 0);
                F.ret = t1177;
                goto gm_fret;
            }
ix1145:;
        }
        goto me136;
mn1142:;
ma151:;
        uint8_t t1183 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1183 != ((uint8_t)36ULL)) goto pa1182;
        goto po1181;
pa1182:;
        uint8_t t1185 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1185 != ((uint8_t)37ULL)) goto pa1184;
        goto po1181;
pa1184:;
        uint8_t t1187 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1187 != ((uint8_t)38ULL)) goto pa1186;
        goto po1181;
pa1186:;
        uint8_t t1189 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1189 != ((uint8_t)39ULL)) goto pa1188;
        goto po1181;
pa1188:;
        goto mn1180;
po1181:;
        {
            gm_l64 = ((uint64_t)0ULL);
        }
        {
            gm_tab260m0n3sqj t1191;
            memset(&t1191, 0, sizeof t1191);
            gm_l65 = t1191;
        }
        {
            uint8_t t1197 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            switch (t1197) { case ((uint8_t)0ULL): goto ma1195; case ((uint8_t)1ULL): goto ma1195; case ((uint8_t)2ULL): goto ma1195; case ((uint8_t)3ULL): goto ma1193; case ((uint8_t)4ULL): goto ma1195; case ((uint8_t)5ULL): goto ma1194; case ((uint8_t)6ULL): goto ma1195; case ((uint8_t)7ULL): goto ma1195; case ((uint8_t)8ULL): goto ma1195; case ((uint8_t)9ULL): goto ma1195; case ((uint8_t)10ULL): goto ma1195; case ((uint8_t)11ULL): goto ma1195; case ((uint8_t)12ULL): goto ma1195; case ((uint8_t)13ULL): goto ma1195; default: goto ma1195; }
ma1193:;
            uint8_t t1199 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1199 != ((uint8_t)3ULL)) goto mn1198;
            void * t1200 = GM_LD(void *, (((uint8_t *)&F.l10) + 16));
            gm_l66 = t1200;
            {
                void * t1202 = gm_l66;
                uint64_t t1203 = GM_LD(uintptr_t, (((uint8_t *)t1202) + 0) + 8);
                uint64_t t1204 = (uint64_t)t1203;
                gm_l64 = t1204;
            }
            goto me1192;
mn1198:;
ma1194:;
            uint8_t t1206 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1206 != ((uint8_t)5ULL)) goto mn1205;
            gm_to6glrduves6o t1207 = GM_LD(gm_to6glrduves6o, (((uint8_t *)&F.l10) + 16));
            gm_l67 = t1207;
            {
                uint64_t t1209 = GM_LD(uint64_t, (((uint8_t *)&gm_l67) + 24));
                gm_l64 = t1209;
            }
            {
                gm_tkaapcii86udk t1210 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&gm_l67) + 0));
                gm_tab260m0n3sqj t1211;
                GM_ST(uint8_t, (uint8_t *)&t1211 + 25, 0);
                GM_ST(uint16_t, (uint8_t *)&t1211 + 26, 0);
                GM_ST(uint32_t, (uint8_t *)&t1211 + 28, 0);
                GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t1211), t1210);
                ((uint8_t *)&t1211)[24] = 1;
                gm_l65 = t1211;
            }
            goto me1192;
mn1205:;
ma1195:;
            void * t1213 = gm_l0;
            gm_tl874t2atnlh3 t1214 = gm_l5;
            gm_b16_8 t1215;
            GM_ST(void *, (uint8_t *)&t1215, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t1215 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t1216;
            gm_f_unsupported_2seq49l((void *)&t1216, t1213, GM_CONV(gm_cseahvg8v7p0vs, t1214), t1215);
            gm_ttk92ufj2f6eh t1217;
            GM_ST(uint8_t, (uint8_t *)&t1217 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t1217 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t1217 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t1217 + 8, 0);
            ((uint8_t *)&t1217)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1217 + 16), t1216);
            gm_ttk92ufj2f6eh t1218 = t1217;
            uint8_t t1219 = ((((uint8_t *)&t1218))[0] != 0);
            F.ret = t1217;
            goto gm_fret;
            goto me1192;
mn1212:;
ma1196:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 519, 13);
me1192:;
        }
        {
            __int128 t1220;
            uint8_t t1226 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t1226) { case ((uint8_t)0ULL): goto ma1224; case ((uint8_t)1ULL): goto ma1224; case ((uint8_t)2ULL): goto ma1224; case ((uint8_t)3ULL): goto ma1224; case ((uint8_t)4ULL): goto ma1224; case ((uint8_t)5ULL): goto ma1224; case ((uint8_t)6ULL): goto ma1224; case ((uint8_t)7ULL): goto ma1224; case ((uint8_t)8ULL): goto ma1224; case ((uint8_t)9ULL): goto ma1224; case ((uint8_t)10ULL): goto ma1224; case ((uint8_t)11ULL): goto ma1224; case ((uint8_t)12ULL): goto ma1224; case ((uint8_t)13ULL): goto ma1224; case ((uint8_t)14ULL): goto ma1224; case ((uint8_t)15ULL): goto ma1224; case ((uint8_t)16ULL): goto ma1224; case ((uint8_t)17ULL): goto ma1224; case ((uint8_t)18ULL): goto ma1224; case ((uint8_t)19ULL): goto ma1224; case ((uint8_t)20ULL): goto ma1224; case ((uint8_t)21ULL): goto ma1224; case ((uint8_t)22ULL): goto ma1224; case ((uint8_t)23ULL): goto ma1224; case ((uint8_t)24ULL): goto ma1224; case ((uint8_t)25ULL): goto ma1224; case ((uint8_t)26ULL): goto ma1224; case ((uint8_t)27ULL): goto ma1224; case ((uint8_t)28ULL): goto ma1224; case ((uint8_t)29ULL): goto ma1224; case ((uint8_t)30ULL): goto ma1224; case ((uint8_t)31ULL): goto ma1224; case ((uint8_t)32ULL): goto ma1224; case ((uint8_t)33ULL): goto ma1224; case ((uint8_t)34ULL): goto ma1224; case ((uint8_t)35ULL): goto ma1224; case ((uint8_t)36ULL): goto ma1224; case ((uint8_t)37ULL): goto ma1224; case ((uint8_t)38ULL): goto ma1222; case ((uint8_t)39ULL): goto ma1223; case ((uint8_t)40ULL): goto ma1224; case ((uint8_t)41ULL): goto ma1224; case ((uint8_t)42ULL): goto ma1224; case ((uint8_t)43ULL): goto ma1224; case ((uint8_t)44ULL): goto ma1224; case ((uint8_t)45ULL): goto ma1224; case ((uint8_t)46ULL): goto ma1224; case ((uint8_t)47ULL): goto ma1224; case ((uint8_t)48ULL): goto ma1224; case ((uint8_t)49ULL): goto ma1224; case ((uint8_t)50ULL): goto ma1224; case ((uint8_t)51ULL): goto ma1224; case ((uint8_t)52ULL): goto ma1224; case ((uint8_t)53ULL): goto ma1224; case ((uint8_t)54ULL): goto ma1224; case ((uint8_t)55ULL): goto ma1224; case ((uint8_t)56ULL): goto ma1224; case ((uint8_t)57ULL): goto ma1224; case ((uint8_t)58ULL): goto ma1224; case ((uint8_t)59ULL): goto ma1224; case ((uint8_t)60ULL): goto ma1224; case ((uint8_t)61ULL): goto ma1224; case ((uint8_t)62ULL): goto ma1224; case ((uint8_t)63ULL): goto ma1224; case ((uint8_t)64ULL): goto ma1224; case ((uint8_t)65ULL): goto ma1224; case ((uint8_t)66ULL): goto ma1224; case ((uint8_t)67ULL): goto ma1224; case ((uint8_t)68ULL): goto ma1224; case ((uint8_t)69ULL): goto ma1224; case ((uint8_t)70ULL): goto ma1224; case ((uint8_t)71ULL): goto ma1224; case ((uint8_t)72ULL): goto ma1224; case ((uint8_t)73ULL): goto ma1224; case ((uint8_t)74ULL): goto ma1224; case ((uint8_t)75ULL): goto ma1224; case ((uint8_t)76ULL): goto ma1224; case ((uint8_t)77ULL): goto ma1224; case ((uint8_t)78ULL): goto ma1224; case ((uint8_t)79ULL): goto ma1224; case ((uint8_t)80ULL): goto ma1224; case ((uint8_t)81ULL): goto ma1224; default: goto ma1224; }
ma1222:;
            uint8_t t1228 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t1228 != ((uint8_t)38ULL)) goto mn1227;
            t1220 = ((__int128)(((unsigned __int128)0u << 64) | 0u));
            goto me1221;
mn1227:;
ma1223:;
            uint8_t t1230 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t1230 != ((uint8_t)39ULL)) goto mn1229;
            uint64_t t1231 = gm_l64;
            __int128 t1232 = (__int128)t1231;
            __int128 r1233; bool o1234 = __builtin_sub_overflow(t1232, ((__int128)(((unsigned __int128)0u << 64) | 1u)), &r1233);
            if (o1234) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 529, 26);
            __int128 t1235 = r1233;
            t1220 = t1235;
            goto me1221;
mn1229:;
ma1224:;
            __int128 t1238;
            uint64_t t1240 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t1241 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
            if (t1240 >= (uintptr_t)(t1241)) gm_trap_index(t1240, t1241, gm_srcue4ionbe9lgh, 531, 35);
            uint8_t t1242 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t1240 * 48) + 0));
            if (t1242 != ((uint8_t)0ULL)) goto ie1239;
            unsigned __int128 t1243 = GM_LD(unsigned __int128, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t1240 * 48) + 16));
            gm_l68 = t1243;
            void * t1245 = gm_l0;
            unsigned __int128 t1246 = gm_l68;
            uint64_t t1247 = (uintptr_t)((uint64_t)1ULL);
            uint64_t t1248 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
            if (t1247 >= (uintptr_t)(t1248)) gm_trap_index(t1247, t1248, gm_srcue4ionbe9lgh, 532, 39);
            gm_t88rcucg1b7uk t1249 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t1247 * 88) + 0));
            __int128 t1250 = gm_f_int_value_6hmh8tj(t1245, t1246, GM_CONV(gm_csguankko4lmhd, t1249));
            __int128 t1251 = t1250;
            t1238 = t1251;
            goto ix1239;
ie1239:;
            void * t1253 = gm_l0;
            gm_tl874t2atnlh3 t1254 = gm_l5;
            gm_b16_8 t1255;
            GM_ST(void *, (uint8_t *)&t1255, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t1255 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t1256;
            gm_f_unsupported_2seq49l((void *)&t1256, t1253, GM_CONV(gm_cseahvg8v7p0vs, t1254), t1255);
            gm_ttk92ufj2f6eh t1257;
            GM_ST(uint8_t, (uint8_t *)&t1257 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t1257 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t1257 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t1257 + 8, 0);
            ((uint8_t *)&t1257)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1257 + 16), t1256);
            gm_ttk92ufj2f6eh t1258 = t1257;
            uint8_t t1259 = ((((uint8_t *)&t1258))[0] != 0);
            F.ret = t1257;
            goto gm_fret;
            t1238 = ((__int128)0);
ix1239:;
            __int128 t1260 = t1238;
            t1220 = t1260;
            goto me1221;
mn1236:;
ma1225:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 527, 23);
me1221:;
            gm_l69 = t1220;
        }
        {
            uint8_t t1262 = 0;
            __int128 t1263 = gm_l69;
            uint8_t t1264 = (t1263 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
            if (!t1264) {
                __int128 t1265 = gm_l69;
                uint64_t t1266 = gm_l64;
                __int128 t1267 = (__int128)t1266;
                uint8_t t1268 = (t1265 >= t1267);
                t1262 = t1268;
            } else t1262 = 1;
            if (!t1262) goto ie1261;
            {
                gm_t6evimt84lndq t1270;
                memset(&t1270, 0, sizeof t1270);
                GM_ST(uint8_t, ((uint8_t *)&t1270 + 0), ((uint8_t)6ULL));
                gm_ttk92ufj2f6eh t1271;
                memset(&t1271, 0, sizeof t1271);
                ((uint8_t *)&t1271)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1271 + 16), t1270);
                gm_ttk92ufj2f6eh t1272 = t1271;
                uint8_t t1273 = ((((uint8_t *)&t1272))[0] != 0);
                F.ret = t1271;
                goto gm_fret;
            }
            goto ix1261;
ie1261:;
ix1261:;
        }
        {
            __int128 t1274 = gm_l69;
            uint64_t t1275 = (uint64_t)t1274;
            gm_l70 = t1275;
        }
        {
            uint8_t t1277 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)37ULL)));
            if (!t1277) goto ie1276;
            {
                if (!((((uint8_t *)&gm_l65))[24] != 0)) goto ie1279;
                gm_tkaapcii86udk t1280 = GM_LD(gm_tkaapcii86udk, ((uint8_t *)&gm_l65));
                gm_l71 = t1280;
                {
                    uint64_t t1282 = gm_l70;
                    gm_tkaapcii86udk t1283;
                    gm_f_offset_nkn711s((void *)&t1283, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l71)), t1282);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_t6evimt84lndq t1284;
                    GM_ST(uint8_t, (uint8_t *)&t1284 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1284 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1284 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1284 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1284 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t1284 + 0), ((uint8_t)4ULL));
                    GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t1284 + 16), t1283);
                    gm_tq7s98ihonkju t1285 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t1284, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k7898pnkupv)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tq7s98ihonkju t1286 = t1285;
                    if (((((uint8_t *)&t1286))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1286) + 8, gm_srcue4ionbe9lgh, 538, 47);
                    void * t1287 = GM_LD(void *, (((uint8_t *)&t1286) + 8));
                    gm_t6evimt84lndq t1288;
                    memset(&t1288, 0, sizeof t1288);
                    GM_ST(uint8_t, ((uint8_t *)&t1288 + 0), ((uint8_t)7ULL));
                    GM_ST(void *, ((uint8_t *)&t1288 + 16), t1287);
                    gm_ttk92ufj2f6eh t1289;
                    memset(&t1289, 0, sizeof t1289);
                    ((uint8_t *)&t1289)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1289 + 16), t1288);
                    gm_ttk92ufj2f6eh t1290 = t1289;
                    uint8_t t1291 = ((((uint8_t *)&t1290))[0] != 0);
                    F.ret = t1289;
                    goto gm_fret;
                }
                goto ix1279;
ie1279:;
ix1279:;
            }
            {
                void * t1292 = gm_l0;
                uint64_t t1293 = (uintptr_t)((uint64_t)0ULL);
                uint64_t t1294 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                if (t1293 >= (uintptr_t)(t1294)) gm_trap_index(t1293, t1294, gm_srcue4ionbe9lgh, 539, 36);
                void * t1295 = (void *)((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t1293 * 88);
                gm_t5kq49rr2n9f8 t1296;
                gm_f_place_9889dgp((void *)&t1296, (void *)NULL, cx, NULL, t1292, t1295);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t5kq49rr2n9f8 t1297 = t1296;
                if (((((uint8_t *)&t1297))[0] != 0)) {
                    gm_t39uai1ajk3qv t1298 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t1297) + 16));
                    gm_ttk92ufj2f6eh t1299;
                    GM_ST(uint8_t, (uint8_t *)&t1299 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1299 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1299 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1299 + 8, 0);
                    ((uint8_t *)&t1299)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1299 + 16), t1298);
                    F.ret = t1299;
                    goto gm_fret;
                }
                gm_tkaapcii86udk t1300 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&t1297) + 16));
                gm_l72 = t1300;
            }
            {
                gm_tkaapcii86udk t1301;
                void * t1302 = GM_LD(void *, (((uint8_t *)&gm_l72) + 0));
                GM_ST(void *, (uint8_t *)&t1301 + 0, t1302);
                gm_b16_8 t1303 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l72) + 8));
                uint64_t t1304 = gm_l70;
                gm_b16_8 t1305 = gm_f_append_sajo2rq((void *)NULL, cx, NULL, t1303, t1304);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t1301 + 8, t1305);
                gm_t6evimt84lndq t1306;
                GM_ST(uint8_t, (uint8_t *)&t1306 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1306 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1306 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1306 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t1306 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1306 + 0), ((uint8_t)4ULL));
                GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t1306 + 16), t1301);
                gm_tq7s98ihonkju t1307 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t1306, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_knj30d2h126)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tq7s98ihonkju t1308 = t1307;
                if (((((uint8_t *)&t1308))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1308) + 8, gm_srcue4ionbe9lgh, 540, 30);
                void * t1309 = GM_LD(void *, (((uint8_t *)&t1308) + 8));
                gm_t6evimt84lndq t1310;
                memset(&t1310, 0, sizeof t1310);
                GM_ST(uint8_t, ((uint8_t *)&t1310 + 0), ((uint8_t)7ULL));
                GM_ST(void *, ((uint8_t *)&t1310 + 16), t1309);
                gm_ttk92ufj2f6eh t1311;
                memset(&t1311, 0, sizeof t1311);
                ((uint8_t *)&t1311)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1311 + 16), t1310);
                gm_ttk92ufj2f6eh t1312 = t1311;
                uint8_t t1313 = ((((uint8_t *)&t1312))[0] != 0);
                F.ret = t1311;
                goto gm_fret;
            }
            goto ix1276;
ie1276:;
ix1276:;
        }
        {
            gm_t6evimt84lndq t1314;
            if (!((((uint8_t *)&gm_l65))[24] != 0)) goto ie1315;
            gm_tkaapcii86udk t1316 = GM_LD(gm_tkaapcii86udk, ((uint8_t *)&gm_l65));
            gm_l73 = t1316;
            void * t1318 = gm_l0;
            uint64_t t1319 = gm_l70;
            gm_tkaapcii86udk t1320;
            gm_f_offset_nkn711s((void *)&t1320, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l73)), t1319);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t6evimt84lndq t1321;
            gm_f_load_nf7o5kl((void *)&t1321, t1318, &t1320);
            gm_t6evimt84lndq t1322 = t1321;
            t1314 = t1322;
            goto ix1315;
ie1315:;
            gm_t6evimt84lndq t1324;
            uint8_t t1326 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1326 != ((uint8_t)3ULL)) goto ie1325;
            void * t1327 = GM_LD(void *, (((uint8_t *)&F.l10) + 16));
            gm_l74 = t1327;
            void * t1329 = gm_l74;
            gm_b16_8 t1330 = GM_LD(gm_b16_8, (((uint8_t *)t1329) + 0));
            gm_b16_8 t1331 = t1330;
            uint64_t t1332 = gm_l70;
            uint64_t t1333 = (uint64_t)t1332;
            uint64_t t1334 = (uintptr_t)t1333;
            gm_b16_8 t1335 = t1331;
            uint64_t t1336 = GM_LD(uintptr_t, ((uint8_t *)&t1335) + 8);
            if (t1334 >= (uintptr_t)(t1336)) gm_trap_index(t1334, t1336, gm_srcue4ionbe9lgh, 544, 17);
            gm_t6evimt84lndq t1337 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t1335)) + t1334 * 48));
            gm_t6evimt84lndq t1338 = t1337;
            t1324 = t1338;
            goto ix1325;
ie1325:;
            void * t1340 = gm_l0;
            gm_tl874t2atnlh3 t1341 = gm_l5;
            gm_b16_8 t1342;
            GM_ST(void *, (uint8_t *)&t1342, (void *)(gm_sv7lsha1e0i8r));
            GM_ST(uintptr_t, (uint8_t *)&t1342 + 8, (uintptr_t)(12));
            gm_t39uai1ajk3qv t1343;
            gm_f_unsupported_2seq49l((void *)&t1343, t1340, GM_CONV(gm_cseahvg8v7p0vs, t1341), t1342);
            gm_ttk92ufj2f6eh t1344;
            GM_ST(uint8_t, (uint8_t *)&t1344 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t1344 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t1344 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t1344 + 8, 0);
            ((uint8_t *)&t1344)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1344 + 16), t1343);
            gm_ttk92ufj2f6eh t1345 = t1344;
            uint8_t t1346 = ((((uint8_t *)&t1345))[0] != 0);
            F.ret = t1344;
            goto gm_fret;
            t1324 = GM_ZERO(gm_t6evimt84lndq);
ix1325:;
            gm_t6evimt84lndq t1347 = t1324;
            t1314 = t1347;
ix1315:;
            F.l75 = t1314;
        }
        {
            gm_t6evimt84lndq t1348;
            gm_f_copy_nk4jid6((void *)&t1348, (void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l75)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t1349 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t1348, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k4nsaof9c90)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t1350 = t1349;
            if (((((uint8_t *)&t1350))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1350) + 8, gm_srcue4ionbe9lgh, 546, 26);
            void * t1351 = GM_LD(void *, (((uint8_t *)&t1350) + 8));
            gm_t6evimt84lndq t1352;
            memset(&t1352, 0, sizeof t1352);
            GM_ST(uint8_t, ((uint8_t *)&t1352 + 0), ((uint8_t)7ULL));
            GM_ST(void *, ((uint8_t *)&t1352 + 16), t1351);
            gm_ttk92ufj2f6eh t1353;
            memset(&t1353, 0, sizeof t1353);
            ((uint8_t *)&t1353)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1353 + 16), t1352);
            gm_ttk92ufj2f6eh t1354 = t1353;
            uint8_t t1355 = ((((uint8_t *)&t1354))[0] != 0);
            F.ret = t1353;
            goto gm_fret;
        }
        goto me136;
mn1180:;
ma152:;
        uint8_t t1357 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1357 != ((uint8_t)35ULL)) goto mn1356;
        gm_t6evimt84lndq t1358 = F.l10;
        gm_ttk92ufj2f6eh t1359;
        memset(&t1359, 0, sizeof t1359);
        ((uint8_t *)&t1359)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1359 + 16), t1358);
        gm_ttk92ufj2f6eh t1360 = t1359;
        uint8_t t1361 = ((((uint8_t *)&t1360))[0] != 0);
        F.ret = t1359;
        goto gm_fret;
        goto me136;
mn1356:;
ma153:;
        uint8_t t1363 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1363 != ((uint8_t)78ULL)) goto mn1362;
        {
            uint8_t t1366 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1366 != ((uint8_t)5ULL)) goto ie1365;
            gm_to6glrduves6o t1367 = GM_LD(gm_to6glrduves6o, (((uint8_t *)&F.l10) + 16));
            gm_l76 = t1367;
            {
                uint64_t t1370 = (uintptr_t)((uint64_t)1ULL);
                uint64_t t1371 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                if (t1370 >= (uintptr_t)(t1371)) gm_trap_index(t1370, t1371, gm_srcue4ionbe9lgh, 551, 26);
                uint8_t t1372 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t1370 * 48) + 0));
                if (t1372 != ((uint8_t)5ULL)) goto ie1369;
                gm_to6glrduves6o t1373 = GM_LD(gm_to6glrduves6o, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t1370 * 48) + 16));
                gm_l77 = t1373;
                {
                    gm_f_list_0202sdh((void *)&F.l78, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                {
                    {
                        gm_l80 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t1376 = GM_LD(uint64_t, (((uint8_t *)&gm_l77) + 24));
                        gm_l79 = t1376;
                    }
                    {
                        for (;;) {
                            uint64_t t1378 = gm_l80;
                            uint64_t t1379 = gm_l79;
                            uint8_t t1380 = (t1378 < t1379);
                            if (!t1380) goto lb1377;
                            {
                                uint64_t t1383 = gm_l80;
                                uint64_t t1384 = t1383;
                                uint64_t t1387 = GM_LD(uint64_t, ((uint8_t *)&t1384));
                                gm_l81 = t1387;
                                {
                                    void * t1389 = (void *)((uint8_t *)&F.l78);
                                    void * t1390 = gm_l0;
                                    uint64_t t1391 = gm_l81;
                                    gm_tkaapcii86udk t1392;
                                    gm_f_offset_nkn711s((void *)&t1392, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)(((uint8_t *)&gm_l77) + 0)), t1391);
                                    if (gm_unw.kind) {
                                        goto gm_ll1381;
                                    }
                                    gm_t6evimt84lndq t1393;
                                    gm_f_load_nf7o5kl((void *)&t1393, t1390, &t1392);
                                    gm_t6evimt84lndq t1394;
                                    gm_f_copy_nk4jid6((void *)&t1394, (void *)NULL, cx, NULL, &t1393);
                                    if (gm_unw.kind) {
                                        goto gm_ll1381;
                                    }
                                    gm_tftb1odc739t5 t1395 = gm_f_push_0pgacsj(t1389, &t1394);
                                    gm_tftb1odc739t5 t1396 = t1395;
                                    if (((((uint8_t *)&t1396))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1396) + 1, gm_srcue4ionbe9lgh, 554, 33);
                                    gm_b0_1 t1397 = GM_LD(gm_b0_1, (((uint8_t *)&t1396) + 1));
                                }
                                goto me1385;
mn1386:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 554, 13);
me1385:;
                            }
lc1377:;
                            {
                                uint64_t t1399 = gm_l80;
                                uint64_t r1400; bool o1401 = __builtin_add_overflow(t1399, ((uint64_t)1ULL), &r1400);
                                uint64_t t1402 = r1400;
                                gm_l80 = t1402;
                            }
                        }
gm_ll1381:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb1377; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc1377; }
                        }
                        goto gm_lad;
lb1377:;
                    }
                }
                {
                    {
                        gm_b16_8 t1404 = GM_LD(gm_b16_8, (((uint8_t *)&F.l78) + 0));
                        gm_b16_8 t1405 = t1404;
                        gm_b16_8 t1406 = GM_LD(gm_b16_8, ((uint8_t *)&t1405));
                        gm_l82 = t1406;
                    }
                    {
                        gm_l83 = ((uint64_t)0ULL);
                    }
                    {
                        for (;;) {
                            uint64_t t1408 = gm_l83;
                            uint64_t t1409 = GM_LD(uintptr_t, ((uint8_t *)&gm_l82) + 8);
                            uint8_t t1410 = (t1408 < t1409);
                            if (!t1410) goto lb1407;
                            {
                                gm_thf8as07kj3uj t1413;
                                memset((uint8_t *)&t1413 + 8, 0, 8);
                                uint64_t t1414 = gm_l83;
                                GM_ST(uint64_t, (uint8_t *)&t1413 + 0, t1414);
                                uint64_t t1415 = gm_l83;
                                uint64_t t1416 = (uintptr_t)t1415;
                                uint64_t t1417 = GM_LD(uintptr_t, ((uint8_t *)&gm_l82) + 8);
                                memcpy((uint8_t *)&t1413 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l82)) + t1416 * 48), 48);
                                gm_thf8as07kj3uj t1418 = t1413;
                                uint64_t t1421 = GM_LD(uint64_t, (((uint8_t *)&t1418) + 0));
                                gm_l84 = t1421;
                                gm_t6evimt84lndq t1422 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t1418) + 16));
                                F.l85 = t1422;
                                {
                                    void * t1424 = gm_l0;
                                    gm_tkaapcii86udk t1425 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&gm_l76) + 0));
                                    uint64_t t1426 = gm_l84;
                                    uint64_t t1427 = (uint64_t)t1426;
                                    gm_tkaapcii86udk t1428;
                                    gm_f_offset_nkn711s((void *)&t1428, (void *)NULL, cx, NULL, &t1425, t1427);
                                    if (gm_unw.kind) {
                                        goto gm_ll1411;
                                    }
                                    gm_tl874t2atnlh3 t1429 = gm_l5;
                                    gm_t9j082e6qvnjj t1430;
                                    gm_f_store_kejn5q1((void *)&t1430, (void *)NULL, cx, NULL, t1424, &t1428, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l85)), t1429);
                                    if (gm_unw.kind) {
                                        goto gm_ll1411;
                                    }
                                    gm_t9j082e6qvnjj t1431 = t1430;
                                    if (((((uint8_t *)&t1431))[0] != 0)) {
                                        gm_t39uai1ajk3qv t1432 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t1431) + 16));
                                        gm_ttk92ufj2f6eh t1433;
                                        GM_ST(uint8_t, (uint8_t *)&t1433 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t1433 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t1433 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t1433 + 8, 0);
                                        ((uint8_t *)&t1433)[0] = 1;
                                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1433 + 16), t1432);
                                        F.ret = t1433;
                                        goto gm_fret;
                                    }
                                    gm_b0_1 t1434 = GM_LD(gm_b0_1, (((uint8_t *)&t1431) + 16));
                                }
                                goto me1419;
mn1420:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 555, 13);
me1419:;
                            }
lc1407:;
                            {
                                uint64_t t1436 = gm_l83;
                                uint64_t r1437; bool o1438 = __builtin_add_overflow(t1436, ((uint64_t)1ULL), &r1437);
                                uint64_t t1439 = r1437;
                                gm_l83 = t1439;
                            }
                        }
gm_ll1411:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb1407; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc1407; }
                        }
                        goto gm_lad;
lb1407:;
                    }
                }
                {
                    gm_t6evimt84lndq t1440;
                    gm_f_unit_value_evae6mr((void *)&t1440, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttk92ufj2f6eh t1441;
                    memset(&t1441, 0, sizeof t1441);
                    ((uint8_t *)&t1441)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1441 + 16), t1440);
                    gm_ttk92ufj2f6eh t1442 = t1441;
                    uint8_t t1443 = ((((uint8_t *)&t1442))[0] != 0);
                    F.ret = t1441;
                    goto gm_fret;
                }
                goto ix1369;
ie1369:;
                {
                    void * t1445 = gm_l0;
                    gm_tl874t2atnlh3 t1446 = gm_l5;
                    gm_b16_8 t1447;
                    GM_ST(void *, (uint8_t *)&t1447, (void *)(gm_su1oqg46rlg2d));
                    GM_ST(uintptr_t, (uint8_t *)&t1447 + 8, (uintptr_t)(9));
                    gm_t39uai1ajk3qv t1448;
                    gm_f_unsupported_2seq49l((void *)&t1448, t1445, GM_CONV(gm_cseahvg8v7p0vs, t1446), t1447);
                    gm_ttk92ufj2f6eh t1449;
                    GM_ST(uint8_t, (uint8_t *)&t1449 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1449 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1449 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1449 + 8, 0);
                    ((uint8_t *)&t1449)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1449 + 16), t1448);
                    gm_ttk92ufj2f6eh t1450 = t1449;
                    uint8_t t1451 = ((((uint8_t *)&t1450))[0] != 0);
                    F.ret = t1449;
                    goto gm_fret;
                }
ix1369:;
            }
            goto ix1365;
ie1365:;
            {
                void * t1453 = gm_l0;
                gm_tl874t2atnlh3 t1454 = gm_l5;
                gm_b16_8 t1455;
                GM_ST(void *, (uint8_t *)&t1455, (void *)(gm_su1oqg46rlg2d));
                GM_ST(uintptr_t, (uint8_t *)&t1455 + 8, (uintptr_t)(9));
                gm_t39uai1ajk3qv t1456;
                gm_f_unsupported_2seq49l((void *)&t1456, t1453, GM_CONV(gm_cseahvg8v7p0vs, t1454), t1455);
                gm_ttk92ufj2f6eh t1457;
                GM_ST(uint8_t, (uint8_t *)&t1457 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1457 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1457 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1457 + 8, 0);
                ((uint8_t *)&t1457)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1457 + 16), t1456);
                gm_ttk92ufj2f6eh t1458 = t1457;
                uint8_t t1459 = ((((uint8_t *)&t1458))[0] != 0);
                F.ret = t1457;
                goto gm_fret;
            }
ix1365:;
        }
        goto me136;
mn1362:;
ma154:;
        uint8_t t1461 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1461 != ((uint8_t)8ULL)) goto mn1460;
        gm_t6evimt84lndq t1462 = F.l10;
        gm_ttk92ufj2f6eh t1463;
        memset(&t1463, 0, sizeof t1463);
        ((uint8_t *)&t1463)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1463 + 16), t1462);
        gm_ttk92ufj2f6eh t1464 = t1463;
        uint8_t t1465 = ((((uint8_t *)&t1464))[0] != 0);
        F.ret = t1463;
        goto gm_fret;
        goto me136;
mn1460:;
ma155:;
        uint8_t t1467 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1467 != ((uint8_t)9ULL)) goto mn1466;
        {
            gm_t6evimt84lndq t1469;
            uint8_t t1471 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1471 != ((uint8_t)7ULL)) goto ie1470;
            void * t1472 = GM_LD(void *, (((uint8_t *)&F.l10) + 16));
            gm_l86 = t1472;
            void * t1474 = gm_l86;
            gm_t6evimt84lndq t1475 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)t1474));
            gm_t6evimt84lndq t1476 = t1475;
            t1469 = t1476;
            goto ix1470;
ie1470:;
            gm_t6evimt84lndq t1478 = F.l10;
            gm_t6evimt84lndq t1479 = t1478;
            t1469 = t1479;
ix1470:;
            F.l87 = t1469;
        }
        {
            uint8_t t1481 = GM_LD(uint8_t, (((uint8_t *)&F.l87) + 0));
            if (t1481 != ((uint8_t)4ULL)) goto ie1480;
            gm_tkaapcii86udk t1482 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l87) + 16));
            gm_l88 = t1482;
            {
                gm_l89 = ((uint64_t)1ULL);
            }
            {
                void * t1484 = gm_l4;
                gm_t88rcucg1b7uk t1485 = gm_l11;
                gm_t53at80pfdus3 t1486;
                gm_f_kind_jcnt4hm((void *)&t1486, t1484, GM_CONV(gm_csguankko4lmhd, t1485));
                gm_t53at80pfdus3 t1487 = t1486;
                uint8_t t1492 = GM_LD(uint8_t, (((uint8_t *)&t1487) + 0));
                if (t1492 != ((uint8_t)8ULL)) goto pa1491;
                gm_t88rcucg1b7uk t1493 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t1487) + 8) + 4));
                gm_l90 = t1493;
                goto po1490;
pa1491:;
                uint8_t t1495 = GM_LD(uint8_t, (((uint8_t *)&t1487) + 0));
                if (t1495 != ((uint8_t)9ULL)) goto pa1494;
                gm_t88rcucg1b7uk t1496 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t1487) + 8) + 4));
                gm_l90 = t1496;
                goto po1490;
pa1494:;
                goto mn1489;
po1490:;
                {
                    void * t1498 = gm_l0;
                    void * t1499 = GM_LD(void *, (((uint8_t *)t1498) + 0));
                    gm_t88rcucg1b7uk t1500 = gm_l90;
                    gm_tbccvikg6b96e t1501;
                    gm_f_layout_a5ovnrm((void *)&t1501, (void *)NULL, cx, NULL, t1499, t1500);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tbccvikg6b96e t1502 = t1501;
                    uint64_t t1503 = GM_LD(uint64_t, (((uint8_t *)&t1502) + 0));
                    uint64_t t1504 = (t1503 > ((uint64_t)1ULL) ? t1503 : ((uint64_t)1ULL));
                    gm_l89 = t1504;
                }
                goto me1488;
mn1489:;
                goto me1488;
mn1505:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 567, 13);
me1488:;
            }
            {
                uint64_t t1507;
                uint64_t t1509 = GM_LD(uintptr_t, (((uint8_t *)&gm_l88) + 8) + 8);
                uint8_t t1510 = (t1509 > ((uint64_t)0ULL));
                if (!t1510) goto ie1508;
                uint64_t t1512 = GM_LD(uintptr_t, (((uint8_t *)&gm_l88) + 8) + 8);
                uint64_t r1513; bool o1514 = __builtin_sub_overflow(t1512, ((uint64_t)1ULL), &r1513);
                if (o1514) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 571, 55);
                uint64_t t1515 = r1513;
                uint64_t t1516 = (uintptr_t)t1515;
                uint64_t t1517 = GM_LD(uintptr_t, (((uint8_t *)&gm_l88) + 8) + 8);
                if (t1516 >= (uintptr_t)(t1517)) gm_trap_index(t1516, t1517, gm_srcue4ionbe9lgh, 571, 47);
                uint64_t t1518 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l88) + 8)) + t1516 * 8));
                uint64_t t1519 = t1518;
                t1507 = t1519;
                goto ix1508;
ie1508:;
                uint64_t t1521 = ((uint64_t)0ULL);
                t1507 = t1521;
ix1508:;
                gm_l92 = t1507;
            }
            {
                void * t1522 = GM_LD(void *, (((uint8_t *)&gm_l88) + 0));
                void * t1523 = t1522;
                uint64_t t1524 = (uintptr_t)t1523;
                uint64_t t1525 = (uint64_t)t1524;
                unsigned __int128 n1526 = (unsigned __int128)((uint64_t)12ULL);
                if (n1526 >= 64) gm_trap_shift((uint64_t)n1526, "u64", gm_srcue4ionbe9lgh, 572, 21);
                uint64_t r1527 = (uint64_t)((uint64_t)t1525 << (int)n1526);
                bool o1528 = (uint64_t)(r1527 >> (int)n1526) != t1525;
                if (o1528) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 572, 21);
                uint64_t t1529 = r1527;
                gm_l93 = t1529;
            }
            {
                void * t1530 = gm_l0;
                uint64_t t1531 = gm_l93;
                uint64_t t1532 = gm_l92;
                uint64_t t1533 = gm_l89;
                uint64_t r1534; bool o1535 = __builtin_mul_overflow(t1532, t1533, &r1534);
                uint64_t t1536 = r1534;
                uint64_t r1537; bool o1538 = __builtin_add_overflow(t1531, t1536, &r1537);
                uint64_t t1539 = r1537;
                unsigned __int128 t1540 = (unsigned __int128)t1539;
                void * t1541 = gm_l3;
                gm_t88rcucg1b7uk t1542 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1541) + 0));
                unsigned __int128 t1543 = gm_f_wrap_bt8r149(t1530, t1540, GM_CONV(gm_csguankko4lmhd, t1542));
                gm_t6evimt84lndq t1544;
                GM_ST(uint8_t, (uint8_t *)&t1544 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1544 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1544 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1544 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t1544 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t1544 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1544 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t1544 + 16), t1543);
                gm_ttk92ufj2f6eh t1545;
                memset(&t1545, 0, sizeof t1545);
                ((uint8_t *)&t1545)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1545 + 16), t1544);
                gm_ttk92ufj2f6eh t1546 = t1545;
                uint8_t t1547 = ((((uint8_t *)&t1546))[0] != 0);
                F.ret = t1545;
                goto gm_fret;
            }
            goto ix1480;
ie1480:;
            {
                gm_t6evimt84lndq t1549;
                GM_ST(uint8_t, (uint8_t *)&t1549 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1549 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1549 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1549 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t1549 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t1549 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1549 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t1549 + 16), ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u)));
                gm_ttk92ufj2f6eh t1550;
                memset(&t1550, 0, sizeof t1550);
                ((uint8_t *)&t1550)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1550 + 16), t1549);
                gm_ttk92ufj2f6eh t1551 = t1550;
                uint8_t t1552 = ((((uint8_t *)&t1551))[0] != 0);
                F.ret = t1550;
                goto gm_fret;
            }
ix1480:;
        }
        goto me136;
mn1466:;
ma156:;
        uint8_t t1554 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1554 != ((uint8_t)13ULL)) goto mn1553;
        {
            uint8_t t1557 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1557 != ((uint8_t)0ULL)) goto ie1556;
            unsigned __int128 t1558 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
            gm_l94 = t1558;
            {
                unsigned __int128 t1561 = gm_l94;
                uint8_t t1562 = ((t1561 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
                if (!t1562) goto ie1560;
                {
                    gm_t6evimt84lndq t1564;
                    memset(&t1564, 0, sizeof t1564);
                    GM_ST(uint8_t, ((uint8_t *)&t1564 + 0), ((uint8_t)6ULL));
                    gm_ttk92ufj2f6eh t1565;
                    memset(&t1565, 0, sizeof t1565);
                    ((uint8_t *)&t1565)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1565 + 16), t1564);
                    gm_ttk92ufj2f6eh t1566 = t1565;
                    uint8_t t1567 = ((((uint8_t *)&t1566))[0] != 0);
                    F.ret = t1565;
                    goto gm_fret;
                }
                goto ix1560;
ie1560:;
ix1560:;
            }
            {
                gm_f_list_kl0t9m0((void *)&F.l95, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                void * t1568 = (void *)((uint8_t *)&F.l95);
                gm_tftb1odc739t5 t1569 = gm_f_push_7o6oubd(t1568, ((uint64_t)0ULL));
                gm_tftb1odc739t5 t1570 = t1569;
                if (((((uint8_t *)&t1570))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1570) + 1, gm_srcue4ionbe9lgh, 581, 13);
                gm_b0_1 t1571 = GM_LD(gm_b0_1, (((uint8_t *)&t1570) + 1));
            }
            {
                gm_tkaapcii86udk t1572;
                gm_tjoi8o798ne9k t1573;
                memset((uint8_t *)&t1573 + 49, 0, 15);
                gm_f_unit_value_evae6mr((void *)&(*(gm_t6evimt84lndq *)((uint8_t *)&t1573 + 0)), (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(uint8_t, (uint8_t *)&t1573 + 48, ((uint8_t)1));
                gm_tq7s98ihonkju t1574 = gm_f_new_15vqrvd((void *)NULL, cx, NULL, &t1573, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kqu6q38jp8e)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tq7s98ihonkju t1575 = t1574;
                if (((((uint8_t *)&t1575))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1575) + 8, gm_srcue4ionbe9lgh, 582, 52);
                void * t1576 = GM_LD(void *, (((uint8_t *)&t1575) + 8));
                GM_ST(void *, (uint8_t *)&t1572 + 0, t1576);
                gm_b16_8 t1577 = GM_LD(gm_b16_8, (((uint8_t *)&F.l95) + 0));
                gm_b16_8 t1578 = t1577;
                gm_b16_8 t1579 = GM_LD(gm_b16_8, ((uint8_t *)&t1578));
                GM_ST(gm_b16_8, (uint8_t *)&t1572 + 8, t1579);
                gm_t6evimt84lndq t1580;
                GM_ST(uint8_t, (uint8_t *)&t1580 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1580 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1580 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1580 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t1580 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1580 + 0), ((uint8_t)4ULL));
                GM_ST(gm_tkaapcii86udk, ((uint8_t *)&t1580 + 16), t1572);
                gm_tq7s98ihonkju t1581 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t1580, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjh6c1ct3mq)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tq7s98ihonkju t1582 = t1581;
                if (((((uint8_t *)&t1582))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1582) + 8, gm_srcue4ionbe9lgh, 582, 26);
                void * t1583 = GM_LD(void *, (((uint8_t *)&t1582) + 8));
                gm_t6evimt84lndq t1584;
                memset(&t1584, 0, sizeof t1584);
                GM_ST(uint8_t, ((uint8_t *)&t1584 + 0), ((uint8_t)7ULL));
                GM_ST(void *, ((uint8_t *)&t1584 + 16), t1583);
                gm_ttk92ufj2f6eh t1585;
                memset(&t1585, 0, sizeof t1585);
                ((uint8_t *)&t1585)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1585 + 16), t1584);
                gm_ttk92ufj2f6eh t1586 = t1585;
                uint8_t t1587 = ((((uint8_t *)&t1586))[0] != 0);
                F.ret = t1585;
                goto gm_fret;
            }
            goto ix1556;
ie1556:;
            {
                void * t1589 = gm_l0;
                gm_tl874t2atnlh3 t1590 = gm_l5;
                gm_b16_8 t1591;
                GM_ST(void *, (uint8_t *)&t1591, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t1591 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t1592;
                gm_f_unsupported_2seq49l((void *)&t1592, t1589, GM_CONV(gm_cseahvg8v7p0vs, t1590), t1591);
                gm_ttk92ufj2f6eh t1593;
                GM_ST(uint8_t, (uint8_t *)&t1593 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1593 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1593 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1593 + 8, 0);
                ((uint8_t *)&t1593)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1593 + 16), t1592);
                gm_ttk92ufj2f6eh t1594 = t1593;
                uint8_t t1595 = ((((uint8_t *)&t1594))[0] != 0);
                F.ret = t1593;
                goto gm_fret;
            }
ix1556:;
        }
        goto me136;
mn1553:;
ma157:;
        uint8_t t1597 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1597 != ((uint8_t)7ULL)) goto mn1596;
        void * t1598 = gm_l0;
        gm_t88rcucg1b7uk t1599 = gm_l11;
        void * t1600 = gm_l3;
        gm_t88rcucg1b7uk t1601 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1600) + 0));
        gm_tl874t2atnlh3 t1602 = gm_l5;
        gm_f_reinterpret_jmr6jts((void *)&(*(gm_ttk92ufj2f6eh *)gm_out), (void *)NULL, cx, NULL, t1598, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l10)), t1599, t1601, t1602);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        goto gm_fdone;
        goto me136;
mn1596:;
ma158:;
        uint8_t t1604 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1604 != ((uint8_t)26ULL)) goto mn1603;
        {
            uint8_t t1607 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1607 != ((uint8_t)1ULL)) goto ie1606;
            double t1608 = GM_LD(double, (((uint8_t *)&F.l10) + 16));
            gm_l96 = t1608;
            {
                void * t1610 = gm_l0;
                double t1611 = gm_l96;
                double t1612 = (double)(__builtin_sqrt(t1611));
                void * t1613 = gm_l3;
                gm_t88rcucg1b7uk t1614 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t1613) + 0));
                gm_t26jhpkpc0sda t1615;
                gm_f_round_float_saju9fu((void *)&t1615, (void *)NULL, cx, NULL, t1610, t1612, t1614);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t26jhpkpc0sda t1616 = t1615;
                if (((((uint8_t *)&t1616))[0] != 0)) {
                    gm_t39uai1ajk3qv t1617 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t1616) + 16));
                    gm_ttk92ufj2f6eh t1618;
                    GM_ST(uint8_t, (uint8_t *)&t1618 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1618 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1618 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1618 + 8, 0);
                    ((uint8_t *)&t1618)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1618 + 16), t1617);
                    F.ret = t1618;
                    goto gm_fret;
                }
                double t1619 = GM_LD(double, (((uint8_t *)&t1616) + 16));
                gm_t6evimt84lndq t1620;
                memset(&t1620, 0, sizeof t1620);
                GM_ST(uint8_t, ((uint8_t *)&t1620 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t1620 + 16), t1619);
                gm_ttk92ufj2f6eh t1621;
                memset(&t1621, 0, sizeof t1621);
                ((uint8_t *)&t1621)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1621 + 16), t1620);
                gm_ttk92ufj2f6eh t1622 = t1621;
                uint8_t t1623 = ((((uint8_t *)&t1622))[0] != 0);
                F.ret = t1621;
                goto gm_fret;
            }
            goto ix1606;
ie1606:;
            {
                void * t1625 = gm_l0;
                gm_tl874t2atnlh3 t1626 = gm_l5;
                gm_b16_8 t1627;
                GM_ST(void *, (uint8_t *)&t1627, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t1627 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t1628;
                gm_f_unsupported_2seq49l((void *)&t1628, t1625, GM_CONV(gm_cseahvg8v7p0vs, t1626), t1627);
                gm_ttk92ufj2f6eh t1629;
                GM_ST(uint8_t, (uint8_t *)&t1629 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1629 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1629 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1629 + 8, 0);
                ((uint8_t *)&t1629)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1629 + 16), t1628);
                gm_ttk92ufj2f6eh t1630 = t1629;
                uint8_t t1631 = ((((uint8_t *)&t1630))[0] != 0);
                F.ret = t1629;
                goto gm_fret;
            }
ix1606:;
        }
        goto me136;
mn1603:;
ma159:;
        uint8_t t1635 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1635 != ((uint8_t)28ULL)) goto pa1634;
        goto po1633;
pa1634:;
        uint8_t t1637 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1637 != ((uint8_t)29ULL)) goto pa1636;
        goto po1633;
pa1636:;
        uint8_t t1639 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1639 != ((uint8_t)30ULL)) goto pa1638;
        goto po1633;
pa1638:;
        uint8_t t1641 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1641 != ((uint8_t)31ULL)) goto pa1640;
        goto po1633;
pa1640:;
        uint8_t t1643 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1643 != ((uint8_t)34ULL)) goto pa1642;
        goto po1633;
pa1642:;
        uint8_t t1645 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1645 != ((uint8_t)32ULL)) goto pa1644;
        goto po1633;
pa1644:;
        uint8_t t1647 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t1647 != ((uint8_t)33ULL)) goto pa1646;
        goto po1633;
pa1646:;
        goto mn1632;
po1633:;
        {
            uint8_t t1650 = GM_LD(uint8_t, (((uint8_t *)&F.l10) + 0));
            if (t1650 != ((uint8_t)0ULL)) goto ie1649;
            unsigned __int128 t1651 = GM_LD(unsigned __int128, (((uint8_t *)&F.l10) + 16));
            gm_l97 = t1651;
            {
                void * t1653 = gm_l0;
                gm_t88rcucg1b7uk t1654 = gm_l11;
                gm_ttovhfee8mf28 t1655 = gm_f_int_info_2r9qb5n(t1653, GM_CONV(gm_csguankko4lmhd, t1654));
                gm_l98 = t1655;
            }
            {
                uint32_t t1656 = GM_LD(uint32_t, (((uint8_t *)&gm_l98) + 4));
                gm_l99 = t1656;
            }
            {
                unsigned __int128 t1657 = gm_l97;
                uint32_t t1658 = gm_l99;
                unsigned __int128 t1659 = gm_f_mask_gbhl4hp(t1658);
                unsigned __int128 t1660 = (unsigned __int128)(t1657 & t1659);
                F.l100 = t1660;
            }
            {
                gm_closure t1661 = ((gm_closure){(void *)gm_f_bit_s517clu, (void *)&F.h, cx});
                gm_l101 = t1661;
            }
            {
                gm_l102 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
            }
            {
                uint8_t t1670 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                switch (t1670) { case ((uint8_t)0ULL): goto ma1668; case ((uint8_t)1ULL): goto ma1668; case ((uint8_t)2ULL): goto ma1668; case ((uint8_t)3ULL): goto ma1668; case ((uint8_t)4ULL): goto ma1668; case ((uint8_t)5ULL): goto ma1668; case ((uint8_t)6ULL): goto ma1668; case ((uint8_t)7ULL): goto ma1668; case ((uint8_t)8ULL): goto ma1668; case ((uint8_t)9ULL): goto ma1668; case ((uint8_t)10ULL): goto ma1668; case ((uint8_t)11ULL): goto ma1668; case ((uint8_t)12ULL): goto ma1668; case ((uint8_t)13ULL): goto ma1668; case ((uint8_t)14ULL): goto ma1668; case ((uint8_t)15ULL): goto ma1668; case ((uint8_t)16ULL): goto ma1668; case ((uint8_t)17ULL): goto ma1668; case ((uint8_t)18ULL): goto ma1668; case ((uint8_t)19ULL): goto ma1668; case ((uint8_t)20ULL): goto ma1668; case ((uint8_t)21ULL): goto ma1668; case ((uint8_t)22ULL): goto ma1668; case ((uint8_t)23ULL): goto ma1668; case ((uint8_t)24ULL): goto ma1668; case ((uint8_t)25ULL): goto ma1668; case ((uint8_t)26ULL): goto ma1668; case ((uint8_t)27ULL): goto ma1668; case ((uint8_t)28ULL): goto ma1663; case ((uint8_t)29ULL): goto ma1664; case ((uint8_t)30ULL): goto ma1665; case ((uint8_t)31ULL): goto ma1666; case ((uint8_t)32ULL): goto ma1668; case ((uint8_t)33ULL): goto ma1668; case ((uint8_t)34ULL): goto ma1667; case ((uint8_t)35ULL): goto ma1668; case ((uint8_t)36ULL): goto ma1668; case ((uint8_t)37ULL): goto ma1668; case ((uint8_t)38ULL): goto ma1668; case ((uint8_t)39ULL): goto ma1668; case ((uint8_t)40ULL): goto ma1668; case ((uint8_t)41ULL): goto ma1668; case ((uint8_t)42ULL): goto ma1668; case ((uint8_t)43ULL): goto ma1668; case ((uint8_t)44ULL): goto ma1668; case ((uint8_t)45ULL): goto ma1668; case ((uint8_t)46ULL): goto ma1668; case ((uint8_t)47ULL): goto ma1668; case ((uint8_t)48ULL): goto ma1668; case ((uint8_t)49ULL): goto ma1668; case ((uint8_t)50ULL): goto ma1668; case ((uint8_t)51ULL): goto ma1668; case ((uint8_t)52ULL): goto ma1668; case ((uint8_t)53ULL): goto ma1668; case ((uint8_t)54ULL): goto ma1668; case ((uint8_t)55ULL): goto ma1668; case ((uint8_t)56ULL): goto ma1668; case ((uint8_t)57ULL): goto ma1668; case ((uint8_t)58ULL): goto ma1668; case ((uint8_t)59ULL): goto ma1668; case ((uint8_t)60ULL): goto ma1668; case ((uint8_t)61ULL): goto ma1668; case ((uint8_t)62ULL): goto ma1668; case ((uint8_t)63ULL): goto ma1668; case ((uint8_t)64ULL): goto ma1668; case ((uint8_t)65ULL): goto ma1668; case ((uint8_t)66ULL): goto ma1668; case ((uint8_t)67ULL): goto ma1668; case ((uint8_t)68ULL): goto ma1668; case ((uint8_t)69ULL): goto ma1668; case ((uint8_t)70ULL): goto ma1668; case ((uint8_t)71ULL): goto ma1668; case ((uint8_t)72ULL): goto ma1668; case ((uint8_t)73ULL): goto ma1668; case ((uint8_t)74ULL): goto ma1668; case ((uint8_t)75ULL): goto ma1668; case ((uint8_t)76ULL): goto ma1668; case ((uint8_t)77ULL): goto ma1668; case ((uint8_t)78ULL): goto ma1668; case ((uint8_t)79ULL): goto ma1668; case ((uint8_t)80ULL): goto ma1668; case ((uint8_t)81ULL): goto ma1668; default: goto ma1668; }
ma1663:;
                uint8_t t1672 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t1672 != ((uint8_t)28ULL)) goto mn1671;
                {
                    gm_l103 = ((uint32_t)0ULL);
                }
                {
                    for (;;) {
                        uint32_t t1675 = gm_l103;
                        uint32_t t1676 = gm_l99;
                        uint8_t t1677 = (t1675 < t1676);
                        if (!t1677) goto lb1674;
                        gm_closure t1678 = gm_l101;
                        uint32_t t1679 = gm_l99;
                        uint32_t r1680; bool o1681 = __builtin_sub_overflow(t1679, ((uint32_t)1ULL), &r1680);
                        if (o1681) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 599, 45);
                        uint32_t t1682 = r1680;
                        uint32_t t1683 = gm_l103;
                        uint32_t r1684; bool o1685 = __builtin_sub_overflow(t1682, t1683, &r1684);
                        if (o1685) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 599, 45);
                        uint32_t t1686 = r1684;
                        gm_closure t1687 = t1678;
                        gm_node *ccx1688 = t1687.cx;
                        void * t1689 = t1687.code;
                        uint8_t t1690;
                        if (GM_PTR_TAGGED(t1689)) {
                            t1690 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1689))(t1686);
                        } else {
                            t1690 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1689)(t1687.env, ccx1688, NULL, t1686);
                        }
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t1691 = (uint8_t)!t1690;
                        if (!t1691) goto lb1674;
                        {
                            uint32_t t1694 = gm_l103;
                            uint32_t r1695; bool o1696 = __builtin_add_overflow(t1694, ((uint32_t)1ULL), &r1695);
                            if (o1696) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 599, 58);
                            uint32_t t1697 = r1695;
                            gm_l103 = t1697;
                        }
lc1674:;
                    }
lb1674:;
                }
                {
                    uint32_t t1698 = gm_l103;
                    unsigned __int128 t1699 = (unsigned __int128)t1698;
                    gm_t6evimt84lndq t1700;
                    GM_ST(uint8_t, (uint8_t *)&t1700 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1700 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1700 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1700 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1700 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1700 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t1700 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t1700 + 16), t1699);
                    gm_ttk92ufj2f6eh t1701;
                    memset(&t1701, 0, sizeof t1701);
                    ((uint8_t *)&t1701)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1701 + 16), t1700);
                    gm_ttk92ufj2f6eh t1702 = t1701;
                    uint8_t t1703 = ((((uint8_t *)&t1702))[0] != 0);
                    F.ret = t1701;
                    goto gm_fret;
                }
                goto me1662;
mn1671:;
ma1664:;
                uint8_t t1705 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t1705 != ((uint8_t)29ULL)) goto mn1704;
                {
                    gm_l104 = ((uint32_t)0ULL);
                }
                {
                    for (;;) {
                        uint32_t t1708 = gm_l104;
                        uint32_t t1709 = gm_l99;
                        uint8_t t1710 = (t1708 < t1709);
                        if (!t1710) goto lb1707;
                        gm_closure t1711 = gm_l101;
                        uint32_t t1712 = gm_l104;
                        gm_closure t1713 = t1711;
                        gm_node *ccx1714 = t1713.cx;
                        void * t1715 = t1713.code;
                        uint8_t t1716;
                        if (GM_PTR_TAGGED(t1715)) {
                            t1716 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1715))(t1712);
                        } else {
                            t1716 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1715)(t1713.env, ccx1714, NULL, t1712);
                        }
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        uint8_t t1717 = (uint8_t)!t1716;
                        if (!t1717) goto lb1707;
                        {
                            uint32_t t1720 = gm_l104;
                            uint32_t r1721; bool o1722 = __builtin_add_overflow(t1720, ((uint32_t)1ULL), &r1721);
                            if (o1722) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 604, 50);
                            uint32_t t1723 = r1721;
                            gm_l104 = t1723;
                        }
lc1707:;
                    }
lb1707:;
                }
                {
                    uint32_t t1724 = gm_l104;
                    unsigned __int128 t1725 = (unsigned __int128)t1724;
                    gm_t6evimt84lndq t1726;
                    GM_ST(uint8_t, (uint8_t *)&t1726 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1726 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1726 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1726 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1726 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1726 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t1726 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t1726 + 16), t1725);
                    gm_ttk92ufj2f6eh t1727;
                    memset(&t1727, 0, sizeof t1727);
                    ((uint8_t *)&t1727)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1727 + 16), t1726);
                    gm_ttk92ufj2f6eh t1728 = t1727;
                    uint8_t t1729 = ((((uint8_t *)&t1728))[0] != 0);
                    F.ret = t1727;
                    goto gm_fret;
                }
                goto me1662;
mn1704:;
ma1665:;
                uint8_t t1731 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t1731 != ((uint8_t)30ULL)) goto mn1730;
                {
                    gm_l105 = ((uint32_t)0ULL);
                }
                {
                    {
                        gm_l107 = ((uint32_t)0ULL);
                    }
                    {
                        uint32_t t1734 = gm_l99;
                        gm_l106 = t1734;
                    }
                    {
                        for (;;) {
                            uint32_t t1736 = gm_l107;
                            uint32_t t1737 = gm_l106;
                            uint8_t t1738 = (t1736 < t1737);
                            if (!t1738) goto lb1735;
                            {
                                uint32_t t1741 = gm_l107;
                                uint32_t t1742 = t1741;
                                uint32_t t1745 = GM_LD(uint32_t, ((uint8_t *)&t1742));
                                gm_l108 = t1745;
                                {
                                    gm_closure t1748 = gm_l101;
                                    uint32_t t1749 = gm_l108;
                                    gm_closure t1750 = t1748;
                                    gm_node *ccx1751 = t1750.cx;
                                    void * t1752 = t1750.code;
                                    uint8_t t1753;
                                    if (GM_PTR_TAGGED(t1752)) {
                                        t1753 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1752))(t1749);
                                    } else {
                                        t1753 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1752)(t1750.env, ccx1751, NULL, t1749);
                                    }
                                    if (gm_unw.kind) {
                                        goto gm_ll1739;
                                    }
                                    if (!t1753) goto ie1747;
                                    {
                                        uint32_t t1755 = gm_l105;
                                        uint32_t r1756; bool o1757 = __builtin_add_overflow(t1755, ((uint32_t)1ULL), &r1756);
                                        if (o1757) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 610, 37);
                                        uint32_t t1758 = r1756;
                                        gm_l105 = t1758;
                                    }
                                    goto ix1747;
ie1747:;
ix1747:;
                                }
                                goto me1743;
mn1744:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 609, 21);
me1743:;
                            }
lc1735:;
                            {
                                uint32_t t1760 = gm_l107;
                                uint32_t r1761; bool o1762 = __builtin_add_overflow(t1760, ((uint32_t)1ULL), &r1761);
                                uint32_t t1763 = r1761;
                                gm_l107 = t1763;
                            }
                        }
gm_ll1739:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 4) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb1735; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc1735; }
                        }
                        goto gm_lad;
lb1735:;
                    }
                }
                {
                    uint32_t t1764 = gm_l105;
                    unsigned __int128 t1765 = (unsigned __int128)t1764;
                    gm_t6evimt84lndq t1766;
                    GM_ST(uint8_t, (uint8_t *)&t1766 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t1766 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t1766 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1766 + 8, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1766 + 32, 0);
                    GM_ST(uint64_t, (uint8_t *)&t1766 + 40, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t1766 + 0), ((uint8_t)0ULL));
                    GM_ST(unsigned __int128, ((uint8_t *)&t1766 + 16), t1765);
                    gm_ttk92ufj2f6eh t1767;
                    memset(&t1767, 0, sizeof t1767);
                    ((uint8_t *)&t1767)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1767 + 16), t1766);
                    gm_ttk92ufj2f6eh t1768 = t1767;
                    uint8_t t1769 = ((((uint8_t *)&t1768))[0] != 0);
                    F.ret = t1767;
                    goto gm_fret;
                }
                goto me1662;
mn1730:;
ma1666:;
                uint8_t t1771 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t1771 != ((uint8_t)31ULL)) goto mn1770;
                {
                    uint32_t t1774 = gm_l99;
                    if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 615, 24);
                    uint32_t t1775 = (uint32_t)(t1774 % ((uint32_t)8ULL));
                    uint8_t t1776 = (!(t1775 == ((uint32_t)0ULL)));
                    if (!t1776) goto ie1773;
                    {
                        void * t1778 = gm_l0;
                        gm_tl874t2atnlh3 t1779 = gm_l5;
                        uint32_t t1780 = gm_l99;
                        gm_t88rcucg1b7uk t1781;
                        GM_ST(uint32_t, ((uint8_t *)&t1781 + 0), t1780);
                        gm_tabb710d9mq2e t1782;
                        gm_f_format_ven7kt5((void *)&t1782, (void *)NULL, cx, NULL, t1781);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_tabb710d9mq2e t1783 = t1782;
                        if (((((uint8_t *)&t1783))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1783) + 8, gm_srcue4ionbe9lgh, 615, 62);
                        gm_b16_8 t1784 = GM_LD(gm_b16_8, (((uint8_t *)&t1783) + 8));
                        gm_b16_8 t1785 = t1784;
                        gm_b16_8 t1786 = GM_LD(gm_b16_8, ((uint8_t *)&t1785));
                        gm_t39uai1ajk3qv t1787;
                        gm_f_trap_jftbss6((void *)&t1787, (void *)NULL, cx, NULL, t1778, t1779, t1786);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        gm_ttk92ufj2f6eh t1788;
                        GM_ST(uint8_t, (uint8_t *)&t1788 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1788 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1788 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t1788 + 8, 0);
                        ((uint8_t *)&t1788)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1788 + 16), t1787);
                        gm_ttk92ufj2f6eh t1789 = t1788;
                        uint8_t t1790 = ((((uint8_t *)&t1789))[0] != 0);
                        F.ret = t1788;
                        goto gm_fret;
                    }
                    goto ix1773;
ie1773:;
ix1773:;
                }
                {
                    {
                        gm_l110 = ((uint32_t)0ULL);
                    }
                    {
                        uint32_t t1792 = gm_l99;
                        if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 616, 33);
                        uint32_t t1793 = (uint32_t)(t1792 / ((uint32_t)8ULL));
                        gm_l109 = t1793;
                    }
                    {
                        for (;;) {
                            uint32_t t1795 = gm_l110;
                            uint32_t t1796 = gm_l109;
                            uint8_t t1797 = (t1795 < t1796);
                            if (!t1797) goto lb1794;
                            {
                                uint32_t t1800 = gm_l110;
                                uint32_t t1801 = t1800;
                                uint32_t t1804 = GM_LD(uint32_t, ((uint8_t *)&t1801));
                                gm_l111 = t1804;
                                {
                                    unsigned __int128 t1806 = gm_l102;
                                    unsigned __int128 t1807 = F.l100;
                                    uint32_t t1808 = gm_l111;
                                    uint32_t r1809; bool o1810 = __builtin_mul_overflow(t1808, ((uint32_t)8ULL), &r1809);
                                    if (o1810) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 616, 58);
                                    uint32_t t1811 = r1809;
                                    unsigned __int128 t1812 = (unsigned __int128)t1811;
                                    unsigned __int128 n1813 = (unsigned __int128)t1812;
                                    if (n1813 >= 128) gm_trap_shift((uint64_t)n1813, "u128", gm_srcue4ionbe9lgh, 616, 48);
                                    unsigned __int128 t1814 = (unsigned __int128)(t1807 >> (int)n1813);
                                    unsigned __int128 t1815 = (unsigned __int128)(t1814 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 255u)));
                                    uint32_t t1816 = gm_l99;
                                    if (((uint32_t)8ULL) == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 616, 83);
                                    uint32_t t1817 = (uint32_t)(t1816 / ((uint32_t)8ULL));
                                    uint32_t r1818; bool o1819 = __builtin_sub_overflow(t1817, ((uint32_t)1ULL), &r1818);
                                    if (o1819) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 616, 83);
                                    uint32_t t1820 = r1818;
                                    uint32_t t1821 = gm_l111;
                                    uint32_t r1822; bool o1823 = __builtin_sub_overflow(t1820, t1821, &r1822);
                                    if (o1823) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 616, 83);
                                    uint32_t t1824 = r1822;
                                    uint32_t r1825; bool o1826 = __builtin_mul_overflow(t1824, ((uint32_t)8ULL), &r1825);
                                    if (o1826) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 616, 82);
                                    uint32_t t1827 = r1825;
                                    unsigned __int128 t1828 = (unsigned __int128)t1827;
                                    unsigned __int128 n1829 = (unsigned __int128)t1828;
                                    if (n1829 >= 128) gm_trap_shift((uint64_t)n1829, "u128", gm_srcue4ionbe9lgh, 616, 46);
                                    unsigned __int128 r1830 = (unsigned __int128)((unsigned __int128)t1815 << (int)n1829);
                                    bool o1831 = (unsigned __int128)(r1830 >> (int)n1829) != t1815;
                                    if (o1831) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 616, 46);
                                    unsigned __int128 t1832 = r1830;
                                    unsigned __int128 t1833 = (unsigned __int128)(t1806 | t1832);
                                    gm_l102 = t1833;
                                }
                                goto me1802;
mn1803:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 616, 21);
me1802:;
                            }
lc1794:;
                            {
                                uint32_t t1835 = gm_l110;
                                uint32_t r1836; bool o1837 = __builtin_add_overflow(t1835, ((uint32_t)1ULL), &r1836);
                                uint32_t t1838 = r1836;
                                gm_l110 = t1838;
                            }
                        }
lb1794:;
                    }
                }
                goto me1662;
mn1770:;
ma1667:;
                uint8_t t1840 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t1840 != ((uint8_t)34ULL)) goto mn1839;
                {
                    gm_l113 = ((uint32_t)0ULL);
                }
                {
                    uint32_t t1842 = gm_l99;
                    gm_l112 = t1842;
                }
                {
                    for (;;) {
                        uint32_t t1844 = gm_l113;
                        uint32_t t1845 = gm_l112;
                        uint8_t t1846 = (t1844 < t1845);
                        if (!t1846) goto lb1843;
                        {
                            uint32_t t1849 = gm_l113;
                            uint32_t t1850 = t1849;
                            uint32_t t1853 = GM_LD(uint32_t, ((uint8_t *)&t1850));
                            gm_l114 = t1853;
                            {
                                gm_closure t1856 = gm_l101;
                                uint32_t t1857 = gm_l114;
                                gm_closure t1858 = t1856;
                                gm_node *ccx1859 = t1858.cx;
                                void * t1860 = t1858.code;
                                uint8_t t1861;
                                if (GM_PTR_TAGGED(t1860)) {
                                    t1861 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1860))(t1857);
                                } else {
                                    t1861 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1860)(t1858.env, ccx1859, NULL, t1857);
                                }
                                if (gm_unw.kind) {
                                    goto gm_ll1847;
                                }
                                if (!t1861) goto ie1855;
                                {
                                    unsigned __int128 t1863 = gm_l102;
                                    uint32_t t1864 = gm_l99;
                                    uint32_t r1865; bool o1866 = __builtin_sub_overflow(t1864, ((uint32_t)1ULL), &r1865);
                                    if (o1866) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 619, 54);
                                    uint32_t t1867 = r1865;
                                    uint32_t t1868 = gm_l114;
                                    uint32_t r1869; bool o1870 = __builtin_sub_overflow(t1867, t1868, &r1869);
                                    if (o1870) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 619, 54);
                                    uint32_t t1871 = r1869;
                                    unsigned __int128 t1872 = (unsigned __int128)t1871;
                                    unsigned __int128 n1873 = (unsigned __int128)t1872;
                                    if (n1873 >= 128) gm_trap_shift((uint64_t)n1873, "u128", gm_srcue4ionbe9lgh, 619, 38);
                                    unsigned __int128 r1874 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n1873);
                                    bool o1875 = (unsigned __int128)(r1874 >> (int)n1873) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
                                    if (o1875) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 619, 38);
                                    unsigned __int128 t1876 = r1874;
                                    unsigned __int128 t1877 = (unsigned __int128)(t1863 | t1876);
                                    gm_l102 = t1877;
                                }
                                goto ix1855;
ie1855:;
ix1855:;
                            }
                            goto me1851;
mn1852:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 618, 32);
me1851:;
                        }
lc1843:;
                        {
                            uint32_t t1879 = gm_l113;
                            uint32_t r1880; bool o1881 = __builtin_add_overflow(t1879, ((uint32_t)1ULL), &r1880);
                            uint32_t t1882 = r1880;
                            gm_l113 = t1882;
                        }
                    }
gm_ll1847:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 6) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb1843; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc1843; }
                    }
                    goto gm_lad;
lb1843:;
                }
                goto me1662;
mn1839:;
ma1668:;
                {
                    uint64_t t1886 = (uintptr_t)((uint64_t)1ULL);
                    uint64_t t1887 = GM_LD(uintptr_t, ((uint8_t *)&gm_l9) + 8);
                    if (t1886 >= (uintptr_t)(t1887)) gm_trap_index(t1886, t1887, gm_srcue4ionbe9lgh, 622, 42);
                    uint8_t t1888 = GM_LD(uint8_t, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t1886 * 48) + 0));
                    if (t1888 != ((uint8_t)0ULL)) goto ie1885;
                    unsigned __int128 t1889 = GM_LD(unsigned __int128, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l9)) + t1886 * 48) + 16));
                    gm_l115 = t1889;
                    {
                        void * t1891 = gm_l0;
                        unsigned __int128 t1892 = gm_l115;
                        uint64_t t1893 = (uintptr_t)((uint64_t)1ULL);
                        uint64_t t1894 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                        if (t1893 >= (uintptr_t)(t1894)) gm_trap_index(t1893, t1894, gm_srcue4ionbe9lgh, 623, 53);
                        gm_t88rcucg1b7uk t1895 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t1893 * 88) + 0));
                        gm_f_big_of_1cq0qao((void *)&F.l116, (void *)NULL, cx, NULL, t1891, t1892, t1895);
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                    }
                    {
                        gm_l117 = ((uint32_t)0ULL);
                    }
                    {
                        uint8_t t1897 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l116)));
                        uint8_t t1898 = (uint8_t)!t1897;
                        if (!t1898) goto ie1896;
                        {
                            gm_tbci5ospk5nnn t1900 = F.l116;
                            uint32_t t1901 = gm_l99;
                            uint64_t t1902 = (uint64_t)t1901;
                            gm_tbci5ospk5nnn t1903;
                            gm_f_from_u64_ud13q5v((void *)&t1903, (void *)NULL, cx, NULL, t1902);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tbci5ospk5nnn t1904;
                            gm_f_rem_ib4l1vl((void *)&t1904, (void *)NULL, cx, NULL, &t1900, &t1903);
                            if (gm_unw.kind) {
                                goto gm_lad;
                            }
                            gm_tf5srh4mp4b3i t1905 = gm_f_to_u64_m01t1da(&t1904);
                            gm_tf5srh4mp4b3i t1906 = t1905;
                            uint64_t t1907;
                            if (((((uint8_t *)&t1906))[8] != 0)) {
                                uint64_t t1908 = GM_LD(uint64_t, (((uint8_t *)&t1906) + 0));
                                t1907 = t1908;
                            } else {
                                t1907 = ((uint64_t)0ULL);
                            }
                            uint32_t t1909 = (uint32_t)t1907;
                            gm_l117 = t1909;
                        }
                        goto ix1896;
ie1896:;
ix1896:;
                    }
                    {
                        uint8_t t1911 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)33ULL)));
                        if (!t1911) goto ie1910;
                        {
                            uint32_t t1913 = gm_l99;
                            uint32_t t1914 = gm_l117;
                            uint32_t r1915; bool o1916 = __builtin_sub_overflow(t1913, t1914, &r1915);
                            if (o1916) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 626, 43);
                            uint32_t t1917 = r1915;
                            uint32_t t1918 = gm_l99;
                            if (t1918 == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 626, 42);
                            uint32_t t1919 = (uint32_t)(t1917 % t1918);
                            gm_l117 = t1919;
                        }
                        goto ix1910;
ie1910:;
ix1910:;
                    }
                    {
                        {
                            gm_l119 = ((uint32_t)0ULL);
                        }
                        {
                            uint32_t t1921 = gm_l99;
                            gm_l118 = t1921;
                        }
                        {
                            for (;;) {
                                uint32_t t1923 = gm_l119;
                                uint32_t t1924 = gm_l118;
                                uint8_t t1925 = (t1923 < t1924);
                                if (!t1925) goto lb1922;
                                {
                                    uint32_t t1928 = gm_l119;
                                    uint32_t t1929 = t1928;
                                    uint32_t t1932 = GM_LD(uint32_t, ((uint8_t *)&t1929));
                                    gm_l120 = t1932;
                                    {
                                        gm_closure t1935 = gm_l101;
                                        uint32_t t1936 = gm_l120;
                                        gm_closure t1937 = t1935;
                                        gm_node *ccx1938 = t1937.cx;
                                        void * t1939 = t1937.code;
                                        uint8_t t1940;
                                        if (GM_PTR_TAGGED(t1939)) {
                                            t1940 = ((uint8_t (*)(uint32_t))GM_PTR_UNTAG(t1939))(t1936);
                                        } else {
                                            t1940 = ((uint8_t (*)(void *, gm_node *, gm_node *, uint32_t))t1939)(t1937.env, ccx1938, NULL, t1936);
                                        }
                                        if (gm_unw.kind) {
                                            goto gm_ll1926;
                                        }
                                        if (!t1940) goto ie1934;
                                        {
                                            unsigned __int128 t1942 = gm_l102;
                                            uint32_t t1943 = gm_l120;
                                            uint32_t t1944 = gm_l117;
                                            uint32_t r1945; bool o1946 = __builtin_add_overflow(t1943, t1944, &r1945);
                                            if (o1946) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 629, 59);
                                            uint32_t t1947 = r1945;
                                            uint32_t t1948 = gm_l99;
                                            if (t1948 == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 629, 58);
                                            uint32_t t1949 = (uint32_t)(t1947 % t1948);
                                            unsigned __int128 t1950 = (unsigned __int128)t1949;
                                            unsigned __int128 n1951 = (unsigned __int128)t1950;
                                            if (n1951 >= 128) gm_trap_shift((uint64_t)n1951, "u128", gm_srcue4ionbe9lgh, 629, 42);
                                            unsigned __int128 r1952 = (unsigned __int128)((unsigned __int128)((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n1951);
                                            bool o1953 = (unsigned __int128)(r1952 >> (int)n1951) != ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u));
                                            if (o1953) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 629, 42);
                                            unsigned __int128 t1954 = r1952;
                                            unsigned __int128 t1955 = (unsigned __int128)(t1942 | t1954);
                                            gm_l102 = t1955;
                                        }
                                        goto ix1934;
ie1934:;
ix1934:;
                                    }
                                    goto me1930;
mn1931:;
                                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 628, 21);
me1930:;
                                }
lc1922:;
                                {
                                    uint32_t t1957 = gm_l119;
                                    uint32_t r1958; bool o1959 = __builtin_add_overflow(t1957, ((uint32_t)1ULL), &r1958);
                                    uint32_t t1960 = r1958;
                                    gm_l119 = t1960;
                                }
                            }
gm_ll1926:;
                            if (gm_unw.frame == &F.h && gm_unw.label == 7) {
                                if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb1922; }
                                if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc1922; }
                            }
                            goto gm_lad;
lb1922:;
                        }
                    }
                    goto ix1885;
ie1885:;
                    {
                        void * t1962 = gm_l0;
                        gm_tl874t2atnlh3 t1963 = gm_l5;
                        gm_b16_8 t1964;
                        GM_ST(void *, (uint8_t *)&t1964, (void *)(gm_sv7lsha1e0i8r));
                        GM_ST(uintptr_t, (uint8_t *)&t1964 + 8, (uintptr_t)(12));
                        gm_t39uai1ajk3qv t1965;
                        gm_f_unsupported_2seq49l((void *)&t1965, t1962, GM_CONV(gm_cseahvg8v7p0vs, t1963), t1964);
                        gm_ttk92ufj2f6eh t1966;
                        GM_ST(uint8_t, (uint8_t *)&t1966 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t1966 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t1966 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t1966 + 8, 0);
                        ((uint8_t *)&t1966)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1966 + 16), t1965);
                        gm_ttk92ufj2f6eh t1967 = t1966;
                        uint8_t t1968 = ((((uint8_t *)&t1967))[0] != 0);
                        F.ret = t1966;
                        goto gm_fret;
                    }
ix1885:;
                }
                goto me1662;
mn1883:;
ma1669:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 596, 13);
me1662:;
            }
            {
                void * t1969 = gm_l0;
                unsigned __int128 t1970 = gm_l102;
                gm_t88rcucg1b7uk t1971 = gm_l11;
                unsigned __int128 t1972 = gm_f_wrap_bt8r149(t1969, t1970, GM_CONV(gm_csguankko4lmhd, t1971));
                gm_t6evimt84lndq t1973;
                GM_ST(uint8_t, (uint8_t *)&t1973 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1973 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1973 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1973 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t1973 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t1973 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t1973 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t1973 + 16), t1972);
                gm_ttk92ufj2f6eh t1974;
                memset(&t1974, 0, sizeof t1974);
                ((uint8_t *)&t1974)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t1974 + 16), t1973);
                gm_ttk92ufj2f6eh t1975 = t1974;
                uint8_t t1976 = ((((uint8_t *)&t1975))[0] != 0);
                F.ret = t1974;
                goto gm_fret;
            }
            goto ix1649;
ie1649:;
            {
                void * t1978 = gm_l0;
                gm_tl874t2atnlh3 t1979 = gm_l5;
                gm_b16_8 t1980;
                GM_ST(void *, (uint8_t *)&t1980, (void *)(gm_sv7lsha1e0i8r));
                GM_ST(uintptr_t, (uint8_t *)&t1980 + 8, (uintptr_t)(12));
                gm_t39uai1ajk3qv t1981;
                gm_f_unsupported_2seq49l((void *)&t1981, t1978, GM_CONV(gm_cseahvg8v7p0vs, t1979), t1980);
                gm_ttk92ufj2f6eh t1982;
                GM_ST(uint8_t, (uint8_t *)&t1982 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t1982 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t1982 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t1982 + 8, 0);
                ((uint8_t *)&t1982)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1982 + 16), t1981);
                gm_ttk92ufj2f6eh t1983 = t1982;
                uint8_t t1984 = ((((uint8_t *)&t1983))[0] != 0);
                F.ret = t1982;
                goto gm_fret;
            }
ix1649:;
        }
        goto me136;
mn1632:;
ma160:;
        gm_tvdg94bk7aa22 t1986 = GM_LD(gm_tvdg94bk7aa22, ((uint8_t *)&gm_l1));
        gm_l121 = t1986;
        void * t1987 = gm_l0;
        gm_tl874t2atnlh3 t1988 = gm_l5;
        gm_tvdg94bk7aa22 t1989 = gm_l121;
        gm_th9nl3oso747e t1990;
        GM_ST(gm_tvdg94bk7aa22, ((uint8_t *)&t1990 + 0), t1989);
        gm_tabb710d9mq2e t1991;
        gm_f_format_o6d0gj1((void *)&t1991, (void *)NULL, cx, NULL, t1990);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t1992 = t1991;
        if (((((uint8_t *)&t1992))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t1992) + 8, gm_srcue4ionbe9lgh, 635, 46);
        gm_b16_8 t1993 = GM_LD(gm_b16_8, (((uint8_t *)&t1992) + 8));
        gm_b16_8 t1994 = t1993;
        gm_b16_8 t1995 = GM_LD(gm_b16_8, ((uint8_t *)&t1994));
        gm_t39uai1ajk3qv t1996;
        gm_f_unsupported_2seq49l((void *)&t1996, t1987, GM_CONV(gm_cseahvg8v7p0vs, t1988), t1995);
        gm_ttk92ufj2f6eh t1997;
        GM_ST(uint8_t, (uint8_t *)&t1997 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t1997 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t1997 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t1997 + 8, 0);
        ((uint8_t *)&t1997)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t1997 + 16), t1996);
        gm_ttk92ufj2f6eh t1998 = t1997;
        uint8_t t1999 = ((((uint8_t *)&t1998))[0] != 0);
        F.ret = t1997;
        goto gm_fret;
        goto me136;
mn1985:;
ma161:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 369, 5);
me136:;
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
extern __attribute__((aligned(4))) uint64_t gm_f_byte_at_m94bj3e(void * a0, uint64_t a1, uint64_t a2);
__attribute__((aligned(4))) void gm_f_coerce_fesnqv4(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tmn7n0n9raou5 a1, void * a2, void * a3) {
    struct gm_fr_coerce_fesnqv4 F;
    void * gm_l0; /* m */
    gm_tmn7n0n9raou5 gm_l1; /* kind */
    void * gm_l2; /* x */
    void * gm_l3; /* e */
    unsigned __int128 gm_l5; /* bits */
    uint32_t gm_l6; /* i */
    void * gm_l7; /* p */
    gm_tkaapcii86udk gm_l8; /* pl */
    gm_t88rcucg1b7uk gm_l9; /* pointee */
    uint64_t gm_l10; /* n */
    void * gm_l11; /* p */
    gm_t88rcucg1b7uk gm_l12; /* inner */
    gm_t88rcucg1b7uk gm_l13; /* from */
    GM_STACK_CHECK(&F, 7239u);
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
        gm_ttk92ufj2f6eh t4;
        gm_f_eval_2vbtp1v((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttk92ufj2f6eh t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_t39uai1ajk3qv t6 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t5) + 16));
            gm_ttk92ufj2f6eh t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t7 + 8, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t7 + 16), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_t6evimt84lndq t8 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t5) + 16));
        F.l4 = t8;
    }
    {
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t20) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma11; case ((uint8_t)2ULL): goto ma12; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma14; case ((uint8_t)5ULL): goto ma17; case ((uint8_t)6ULL): goto ma18; case ((uint8_t)7ULL): goto ma15; case ((uint8_t)8ULL): goto ma18; case ((uint8_t)9ULL): goto ma18; case ((uint8_t)10ULL): goto ma18; case ((uint8_t)11ULL): goto ma16; case ((uint8_t)12ULL): goto ma17; default: goto ma18; }
ma10:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t22 != ((uint8_t)0ULL)) goto mn21;
        {
            uint8_t t25 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t25 != ((uint8_t)0ULL)) goto ie24;
            unsigned __int128 t26 = GM_LD(unsigned __int128, (((uint8_t *)&F.l4) + 16));
            gm_l5 = t26;
            {
                void * t28 = gm_l0;
                void * t29 = gm_l0;
                unsigned __int128 t30 = gm_l5;
                void * t31 = gm_l2;
                gm_t88rcucg1b7uk t32 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t31) + 0));
                __int128 t33 = gm_f_int_value_6hmh8tj(t29, t30, GM_CONV(gm_csguankko4lmhd, t32));
                __int128 t34 = t33;
                unsigned __int128 t35;
                memset(&t35, 0, sizeof t35); memcpy(&t35, ((uint8_t *)&t34), sizeof t35 < 16 ? sizeof t35 : 16);
                void * t36 = gm_l3;
                gm_t88rcucg1b7uk t37 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t36) + 0));
                unsigned __int128 t38 = gm_f_wrap_bt8r149(t28, t35, GM_CONV(gm_csguankko4lmhd, t37));
                gm_t6evimt84lndq t39;
                GM_ST(uint8_t, (uint8_t *)&t39 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t39 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t39 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t39 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t39 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t39 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t39 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t39 + 16), t38);
                gm_ttk92ufj2f6eh t40;
                memset(&t40, 0, sizeof t40);
                ((uint8_t *)&t40)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t40 + 16), t39);
                gm_ttk92ufj2f6eh t41 = t40;
                uint8_t t42 = ((((uint8_t *)&t41))[0] != 0);
                F.ret = t40;
                goto gm_fret;
            }
            goto ix24;
ie24:;
            {
                gm_t6evimt84lndq t44 = F.l4;
                gm_ttk92ufj2f6eh t45;
                memset(&t45, 0, sizeof t45);
                ((uint8_t *)&t45)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t45 + 16), t44);
                gm_ttk92ufj2f6eh t46 = t45;
                uint8_t t47 = ((((uint8_t *)&t46))[0] != 0);
                F.ret = t45;
                goto gm_fret;
            }
ix24:;
        }
        goto me9;
mn21:;
ma11:;
        uint8_t t49 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t49 != ((uint8_t)1ULL)) goto mn48;
        gm_t6evimt84lndq t50 = F.l4;
        gm_ttk92ufj2f6eh t51;
        memset(&t51, 0, sizeof t51);
        ((uint8_t *)&t51)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t51 + 16), t50);
        gm_ttk92ufj2f6eh t52 = t51;
        uint8_t t53 = ((((uint8_t *)&t52))[0] != 0);
        F.ret = t51;
        goto gm_fret;
        goto me9;
mn48:;
ma12:;
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t55 != ((uint8_t)2ULL)) goto mn54;
        gm_tq7s98ihonkju t56 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l4)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kjqc35oao72)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t57 = t56;
        if (((((uint8_t *)&t57))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t57) + 8, gm_srcue4ionbe9lgh, 264, 39);
        void * t58 = GM_LD(void *, (((uint8_t *)&t57) + 8));
        gm_t6evimt84lndq t59;
        memset(&t59, 0, sizeof t59);
        GM_ST(uint8_t, ((uint8_t *)&t59 + 0), ((uint8_t)7ULL));
        GM_ST(void *, ((uint8_t *)&t59 + 16), t58);
        gm_ttk92ufj2f6eh t60;
        memset(&t60, 0, sizeof t60);
        ((uint8_t *)&t60)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t60 + 16), t59);
        gm_ttk92ufj2f6eh t61 = t60;
        uint8_t t62 = ((((uint8_t *)&t61))[0] != 0);
        F.ret = t60;
        goto gm_fret;
        goto me9;
mn54:;
ma13:;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t64 != ((uint8_t)3ULL)) goto mn63;
        gm_tq7s98ihonkju t65 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l4)), &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kct6b70ce3d)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t66 = t65;
        if (((((uint8_t *)&t66))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t66) + 8, gm_srcue4ionbe9lgh, 265, 31);
        void * t67 = GM_LD(void *, (((uint8_t *)&t66) + 8));
        gm_t6evimt84lndq t68;
        memset(&t68, 0, sizeof t68);
        GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)8ULL));
        GM_ST(void *, ((uint8_t *)&t68 + 16), t67);
        gm_ttk92ufj2f6eh t69;
        memset(&t69, 0, sizeof t69);
        ((uint8_t *)&t69)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t69 + 16), t68);
        gm_ttk92ufj2f6eh t70 = t69;
        uint8_t t71 = ((((uint8_t *)&t70))[0] != 0);
        F.ret = t69;
        goto gm_fret;
        goto me9;
mn63:;
ma14:;
        uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t73 != ((uint8_t)4ULL)) goto mn72;
        uint32_t t74 = GM_LD(uint32_t, (((uint8_t *)&gm_l1) + 4));
        gm_l6 = t74;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t77 != ((uint8_t)9ULL)) goto mn76;
        void * t78 = GM_LD(void *, (((uint8_t *)&F.l4) + 16));
        gm_l7 = t78;
        uint32_t t79 = gm_l6;
        void * t80 = gm_l7;
        gm_ttggq7ng4kp3p t81;
        GM_ST(uint32_t, (uint8_t *)&t81 + 4, 0);
        GM_ST(uint32_t, ((uint8_t *)&t81 + 0), t79);
        GM_ST(void *, ((uint8_t *)&t81 + 8), t80);
        gm_t6evimt84lndq t82;
        GM_ST(uint8_t, (uint8_t *)&t82 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t82 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t82 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t82 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t82 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t82 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t82 + 0), ((uint8_t)10ULL));
        GM_ST(gm_ttggq7ng4kp3p, ((uint8_t *)&t82 + 16), t81);
        gm_tq7s98ihonkju t83 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t82, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ktv7kfnnmgp)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t84 = t83;
        if (((((uint8_t *)&t84))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t84) + 8, gm_srcue4ionbe9lgh, 267, 36);
        void * t85 = GM_LD(void *, (((uint8_t *)&t84) + 8));
        gm_t6evimt84lndq t86;
        memset(&t86, 0, sizeof t86);
        GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)9ULL));
        GM_ST(void *, ((uint8_t *)&t86 + 16), t85);
        gm_ttk92ufj2f6eh t87;
        memset(&t87, 0, sizeof t87);
        ((uint8_t *)&t87)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t87 + 16), t86);
        gm_ttk92ufj2f6eh t88 = t87;
        uint8_t t89 = ((((uint8_t *)&t88))[0] != 0);
        F.ret = t87;
        goto gm_fret;
        goto me75;
mn76:;
        gm_t6evimt84lndq t91 = F.l4;
        gm_ttk92ufj2f6eh t92;
        memset(&t92, 0, sizeof t92);
        ((uint8_t *)&t92)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t92 + 16), t91);
        gm_ttk92ufj2f6eh t93 = t92;
        uint8_t t94 = ((((uint8_t *)&t93))[0] != 0);
        F.ret = t92;
        goto gm_fret;
        goto me75;
mn90:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 266, 24);
me75:;
        goto me9;
mn72:;
ma15:;
        uint8_t t96 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t96 != ((uint8_t)7ULL)) goto mn95;
        {
            uint8_t t99 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
            if (t99 != ((uint8_t)4ULL)) goto ie98;
            gm_tkaapcii86udk t100 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l4) + 16));
            gm_l8 = t100;
            {
                void * t103 = gm_l0;
                void * t104 = GM_LD(void *, (((uint8_t *)t103) + 0));
                void * t105 = (void *)(((uint8_t *)t104) + 8);
                void * t106 = gm_l2;
                gm_t88rcucg1b7uk t107 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t106) + 0));
                gm_t53at80pfdus3 t108;
                gm_f_kind_jcnt4hm((void *)&t108, t105, GM_CONV(gm_csguankko4lmhd, t107));
                gm_t53at80pfdus3 t109 = t108;
                uint8_t t110 = GM_LD(uint8_t, (((uint8_t *)&t109) + 0));
                if (t110 != ((uint8_t)8ULL)) goto ie102;
                gm_t88rcucg1b7uk t111 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t109) + 8) + 4));
                gm_l9 = t111;
                {
                    void * t114 = gm_l0;
                    void * t115 = GM_LD(void *, (((uint8_t *)t114) + 0));
                    void * t116 = (void *)(((uint8_t *)t115) + 8);
                    gm_t88rcucg1b7uk t117 = gm_l9;
                    gm_t53at80pfdus3 t118;
                    gm_f_kind_jcnt4hm((void *)&t118, t116, GM_CONV(gm_csguankko4lmhd, t117));
                    gm_t53at80pfdus3 t119 = t118;
                    uint8_t t120 = GM_LD(uint8_t, (((uint8_t *)&t119) + 0));
                    if (t120 != ((uint8_t)11ULL)) goto ie113;
                    uint64_t t121 = GM_LD(uint64_t, ((((uint8_t *)&t119) + 8) + 0));
                    gm_l10 = t121;
                    {
                        gm_to6glrduves6o t123;
                        void * t124 = GM_LD(void *, (((uint8_t *)&gm_l8) + 0));
                        GM_ST(void *, (uint8_t *)&(*(gm_tkaapcii86udk *)((uint8_t *)&t123 + 0)) + 0, t124);
                        gm_b16_8 t125 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l8) + 8));
                        gm_b16_8 t126 = gm_f_append_sajo2rq((void *)NULL, cx, NULL, t125, ((uint64_t)0ULL));
                        if (gm_unw.kind) {
                            goto gm_lad;
                        }
                        GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tkaapcii86udk *)((uint8_t *)&t123 + 0)) + 8, t126);
                        uint64_t t127 = gm_l10;
                        GM_ST(uint64_t, (uint8_t *)&t123 + 24, t127);
                        gm_t6evimt84lndq t128;
                        GM_ST(uint8_t, (uint8_t *)&t128 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t128 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t128 + 8, 0);
                        GM_ST(uint8_t, ((uint8_t *)&t128 + 0), ((uint8_t)5ULL));
                        GM_ST(gm_to6glrduves6o, ((uint8_t *)&t128 + 16), t123);
                        gm_ttk92ufj2f6eh t129;
                        memset(&t129, 0, sizeof t129);
                        ((uint8_t *)&t129)[0] = 0;
                        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t129 + 16), t128);
                        gm_ttk92ufj2f6eh t130 = t129;
                        uint8_t t131 = ((((uint8_t *)&t130))[0] != 0);
                        F.ret = t129;
                        goto gm_fret;
                    }
                    goto ix113;
ie113:;
                    {
                        void * t133 = gm_l0;
                        void * t134 = gm_l3;
                        gm_tl874t2atnlh3 t135 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t134) + 72));
                        gm_b16_8 t136;
                        GM_ST(void *, (uint8_t *)&t136, (void *)(gm_sqh0568fl8oao));
                        GM_ST(uintptr_t, (uint8_t *)&t136 + 8, (uintptr_t)(13));
                        gm_t39uai1ajk3qv t137;
                        gm_f_unsupported_2seq49l((void *)&t137, t133, GM_CONV(gm_cseahvg8v7p0vs, t135), t136);
                        gm_ttk92ufj2f6eh t138;
                        GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t138 + 8, 0);
                        ((uint8_t *)&t138)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t138 + 16), t137);
                        gm_ttk92ufj2f6eh t139 = t138;
                        uint8_t t140 = ((((uint8_t *)&t139))[0] != 0);
                        F.ret = t138;
                        goto gm_fret;
                    }
ix113:;
                }
                goto ix102;
ie102:;
                {
                    void * t142 = gm_l0;
                    void * t143 = gm_l3;
                    gm_tl874t2atnlh3 t144 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t143) + 72));
                    gm_b16_8 t145;
                    GM_ST(void *, (uint8_t *)&t145, (void *)(gm_sqh0568fl8oao));
                    GM_ST(uintptr_t, (uint8_t *)&t145 + 8, (uintptr_t)(13));
                    gm_t39uai1ajk3qv t146;
                    gm_f_unsupported_2seq49l((void *)&t146, t142, GM_CONV(gm_cseahvg8v7p0vs, t144), t145);
                    gm_ttk92ufj2f6eh t147;
                    GM_ST(uint8_t, (uint8_t *)&t147 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t147 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t147 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t147 + 8, 0);
                    ((uint8_t *)&t147)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t147 + 16), t146);
                    gm_ttk92ufj2f6eh t148 = t147;
                    uint8_t t149 = ((((uint8_t *)&t148))[0] != 0);
                    F.ret = t147;
                    goto gm_fret;
                }
ix102:;
            }
            goto ix98;
ie98:;
            {
                void * t151 = gm_l0;
                void * t152 = gm_l3;
                gm_tl874t2atnlh3 t153 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t152) + 72));
                gm_b16_8 t154;
                GM_ST(void *, (uint8_t *)&t154, (void *)(gm_sqh0568fl8oao));
                GM_ST(uintptr_t, (uint8_t *)&t154 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t155;
                gm_f_unsupported_2seq49l((void *)&t155, t151, GM_CONV(gm_cseahvg8v7p0vs, t153), t154);
                gm_ttk92ufj2f6eh t156;
                GM_ST(uint8_t, (uint8_t *)&t156 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t156 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t156 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t156 + 8, 0);
                ((uint8_t *)&t156)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t156 + 16), t155);
                gm_ttk92ufj2f6eh t157 = t156;
                uint8_t t158 = ((((uint8_t *)&t157))[0] != 0);
                F.ret = t156;
                goto gm_fret;
            }
ix98:;
        }
        goto me9;
mn95:;
ma16:;
        uint8_t t160 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t160 != ((uint8_t)11ULL)) goto mn159;
        uint8_t t163 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t163 != ((uint8_t)7ULL)) goto mn162;
        void * t164 = GM_LD(void *, (((uint8_t *)&F.l4) + 16));
        gm_l11 = t164;
        {
            void * t167 = gm_l0;
            void * t168 = GM_LD(void *, (((uint8_t *)t167) + 0));
            void * t169 = (void *)(((uint8_t *)t168) + 8);
            void * t170 = gm_l3;
            gm_t88rcucg1b7uk t171 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t170) + 0));
            gm_t53at80pfdus3 t172;
            gm_f_kind_jcnt4hm((void *)&t172, t169, GM_CONV(gm_csguankko4lmhd, t171));
            gm_t53at80pfdus3 t173 = t172;
            uint8_t t174 = GM_LD(uint8_t, (((uint8_t *)&t173) + 0));
            if (t174 != ((uint8_t)13ULL)) goto ie166;
            gm_t88rcucg1b7uk t175 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t173) + 8));
            gm_l12 = t175;
            {
                void * t178 = gm_l0;
                void * t179 = GM_LD(void *, (((uint8_t *)t178) + 0));
                void * t180 = (void *)(((uint8_t *)t179) + 8);
                void * t181 = gm_l2;
                gm_t88rcucg1b7uk t182 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t181) + 0));
                gm_t53at80pfdus3 t183;
                gm_f_kind_jcnt4hm((void *)&t183, t180, GM_CONV(gm_csguankko4lmhd, t182));
                gm_t53at80pfdus3 t184 = t183;
                uint8_t t185 = GM_LD(uint8_t, (((uint8_t *)&t184) + 0));
                if (t185 != ((uint8_t)13ULL)) goto ie177;
                gm_t88rcucg1b7uk t186 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t184) + 8));
                gm_l13 = t186;
                {
                    void * t188 = gm_l0;
                    void * t189 = gm_l11;
                    gm_t88rcucg1b7uk t190 = gm_l13;
                    gm_t88rcucg1b7uk t191 = gm_l12;
                    gm_ttk92ufj2f6eh t192;
                    gm_f_convert_value_v1i8v3i((void *)&t192, (void *)NULL, cx, NULL, t188, &(*(gm_t6evimt84lndq *)((uint8_t *)t189)), t190, t191);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_ttk92ufj2f6eh t193 = t192;
                    if (((((uint8_t *)&t193))[0] != 0)) {
                        gm_t39uai1ajk3qv t194 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t193) + 16));
                        gm_ttk92ufj2f6eh t195;
                        GM_ST(uint8_t, (uint8_t *)&t195 + 1, 0);
                        GM_ST(uint16_t, (uint8_t *)&t195 + 2, 0);
                        GM_ST(uint32_t, (uint8_t *)&t195 + 4, 0);
                        GM_ST(uint64_t, (uint8_t *)&t195 + 8, 0);
                        ((uint8_t *)&t195)[0] = 1;
                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t195 + 16), t194);
                        F.ret = t195;
                        goto gm_fret;
                    }
                    gm_t6evimt84lndq t196 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t193) + 16));
                    gm_tq7s98ihonkju t197 = gm_f_new_70le0lp((void *)NULL, cx, NULL, &t196, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_k1fl2q13fn0)));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tq7s98ihonkju t198 = t197;
                    if (((((uint8_t *)&t198))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t198) + 8, gm_srcue4ionbe9lgh, 280, 30);
                    void * t199 = GM_LD(void *, (((uint8_t *)&t198) + 8));
                    gm_t6evimt84lndq t200;
                    memset(&t200, 0, sizeof t200);
                    GM_ST(uint8_t, ((uint8_t *)&t200 + 0), ((uint8_t)7ULL));
                    GM_ST(void *, ((uint8_t *)&t200 + 16), t199);
                    gm_ttk92ufj2f6eh t201;
                    memset(&t201, 0, sizeof t201);
                    ((uint8_t *)&t201)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t201 + 16), t200);
                    gm_ttk92ufj2f6eh t202 = t201;
                    uint8_t t203 = ((((uint8_t *)&t202))[0] != 0);
                    F.ret = t201;
                    goto gm_fret;
                }
                goto ix177;
ie177:;
                {
                    gm_t6evimt84lndq t205 = F.l4;
                    gm_ttk92ufj2f6eh t206;
                    memset(&t206, 0, sizeof t206);
                    ((uint8_t *)&t206)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t206 + 16), t205);
                    gm_ttk92ufj2f6eh t207 = t206;
                    uint8_t t208 = ((((uint8_t *)&t207))[0] != 0);
                    F.ret = t206;
                    goto gm_fret;
                }
ix177:;
            }
            goto ix166;
ie166:;
            {
                gm_t6evimt84lndq t210 = F.l4;
                gm_ttk92ufj2f6eh t211;
                memset(&t211, 0, sizeof t211);
                ((uint8_t *)&t211)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t211 + 16), t210);
                gm_ttk92ufj2f6eh t212 = t211;
                uint8_t t213 = ((((uint8_t *)&t212))[0] != 0);
                F.ret = t211;
                goto gm_fret;
            }
ix166:;
        }
        goto me161;
mn162:;
        gm_t6evimt84lndq t215 = F.l4;
        gm_ttk92ufj2f6eh t216;
        memset(&t216, 0, sizeof t216);
        ((uint8_t *)&t216)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t216 + 16), t215);
        gm_ttk92ufj2f6eh t217 = t216;
        uint8_t t218 = ((((uint8_t *)&t217))[0] != 0);
        F.ret = t216;
        goto gm_fret;
        goto me161;
mn214:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 276, 24);
me161:;
        goto me9;
mn159:;
ma17:;
        uint8_t t222 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t222 != ((uint8_t)12ULL)) goto pa221;
        goto po220;
pa221:;
        uint8_t t224 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t224 != ((uint8_t)5ULL)) goto pa223;
        goto po220;
pa223:;
        goto mn219;
po220:;
        void * t225 = gm_l0;
        void * t226 = gm_l3;
        gm_tl874t2atnlh3 t227 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t226) + 72));
        gm_b16_8 t228;
        GM_ST(void *, (uint8_t *)&t228, (void *)(gm_sqh0568fl8oao));
        GM_ST(uintptr_t, (uint8_t *)&t228 + 8, (uintptr_t)(13));
        gm_t39uai1ajk3qv t229;
        gm_f_unsupported_2seq49l((void *)&t229, t225, GM_CONV(gm_cseahvg8v7p0vs, t227), t228);
        gm_ttk92ufj2f6eh t230;
        GM_ST(uint8_t, (uint8_t *)&t230 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t230 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t230 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t230 + 8, 0);
        ((uint8_t *)&t230)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t230 + 16), t229);
        gm_ttk92ufj2f6eh t231 = t230;
        uint8_t t232 = ((((uint8_t *)&t231))[0] != 0);
        F.ret = t230;
        goto gm_fret;
        goto me9;
mn219:;
ma18:;
        gm_t6evimt84lndq t234 = F.l4;
        gm_ttk92ufj2f6eh t235;
        memset(&t235, 0, sizeof t235);
        ((uint8_t *)&t235)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t235 + 16), t234);
        gm_ttk92ufj2f6eh t236 = t235;
        uint8_t t237 = ((((uint8_t *)&t236))[0] != 0);
        F.ret = t235;
        goto gm_fret;
        goto me9;
mn233:;
ma19:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 258, 5);
me9:;
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
__attribute__((aligned(4))) void gm_f_const_cell_p4adg2s(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_tl874t2atnlh3 a1, gm_t88rcucg1b7uk a2, gm_tl874t2atnlh3 a3) {
    struct gm_fr_const_cell_p4adg2s F;
    void * gm_l0; /* m */
    gm_tl874t2atnlh3 gm_l1; /* span */
    gm_t88rcucg1b7uk gm_l2; /* ty */
    gm_tl874t2atnlh3 gm_l3; /* use_span */
    uint32_t gm_l4; /* id */
    GM_STACK_CHECK(&F, 1985u);
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
        gm_tl874t2atnlh3 t4 = gm_l1;
        gm_t88rcucg1b7uk t5 = gm_l2;
        gm_t9805qtonbsag t6 = gm_f_const_id_2mlh0og(t3, GM_CONV(gm_cseahvg8v7p0vs, t4), GM_CONV(gm_csguankko4lmhd, t5));
        gm_t9805qtonbsag t7 = t6;
        uint32_t t8;
        if (((((uint8_t *)&t7))[4] != 0)) {
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&t7) + 0));
            t8 = t9;
        } else {
            void * t10 = gm_l0;
            gm_tl874t2atnlh3 t11 = gm_l3;
            gm_b16_8 t12;
            GM_ST(void *, (uint8_t *)&t12, (void *)(gm_sdrg69bmlm0tm));
            GM_ST(uintptr_t, (uint8_t *)&t12 + 8, (uintptr_t)(29));
            gm_t39uai1ajk3qv t13;
            gm_f_unsupported_2seq49l((void *)&t13, t10, GM_CONV(gm_cseahvg8v7p0vs, t11), t12);
            gm_t7g11k265ojef t14;
            GM_ST(uint8_t, (uint8_t *)&t14 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t14 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t14 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t14 + 8, 0);
            ((uint8_t *)&t14)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t14 + 16), t13);
            gm_t7g11k265ojef t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
        gm_l4 = t8;
    }
    {
        uint32_t t17 = gm_l4;
        uint64_t t18 = (uint64_t)t17;
        uint64_t t19 = (uintptr_t)t18;
        void * t20 = gm_l0;
        void * t21 = GM_LD(void *, (((uint8_t *)t20) + 0));
        uint64_t t22 = GM_LD(uintptr_t, ((((uint8_t *)t21) + 1968) + 0) + 8);
        if (t19 >= (uintptr_t)(t22)) gm_trap_index(t19, t22, gm_srcue4ionbe9lgh, 783, 10);
        gm_tjcptv9sagfq8 t23 = GM_LD(gm_tjcptv9sagfq8, ((uint8_t *)GM_LD(void *, ((((uint8_t *)t21) + 1968) + 0)) + t19 * 88));
        F.l5 = t23;
    }
    {
        uint8_t t30 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 72) + 0));
        switch (t30) { case ((uint8_t)0ULL): goto ma27; case ((uint8_t)1ULL): goto ma26; case ((uint8_t)2ULL): goto ma25; case ((uint8_t)3ULL): goto ma28; case ((uint8_t)4ULL): goto ma28; default: goto ma28; }
ma25:;
        uint8_t t32 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 72) + 0));
        if (t32 != ((uint8_t)2ULL)) goto mn31;
        void * t33 = GM_LD(void *, (((uint8_t *)&F.l5) + 80));
        void * t34 = t33;
        if (!(GM_LD(void *, ((uint8_t *)&t34)) != 0)) gm_trap("unwrap of null", gm_srcue4ionbe9lgh, 785, 25);
        void * t35 = GM_LD(void *, ((uint8_t *)&t34));
        gm_t7g11k265ojef t36;
        memset(&t36, 0, sizeof t36);
        ((uint8_t *)&t36)[0] = 0;
        GM_ST(void *, ((uint8_t *)&t36 + 16), t35);
        gm_t7g11k265ojef t37 = t36;
        uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
        F.ret = t36;
        goto gm_fret;
        goto me24;
mn31:;
ma26:;
        uint8_t t40 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 72) + 0));
        if (t40 != ((uint8_t)1ULL)) goto mn39;
        void * t41 = gm_l0;
        gm_tl874t2atnlh3 t42 = gm_l3;
        gm_b16_8 t43 = GM_LD(gm_b16_8, (((uint8_t *)&F.l5) + 0));
        gm_tnpifv20irnph t44;
        GM_ST(gm_b16_8, ((uint8_t *)&t44 + 0), t43);
        gm_tabb710d9mq2e t45;
        gm_f_format_q8f0n70((void *)&t45, (void *)NULL, cx, NULL, t44);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t46 = t45;
        if (((((uint8_t *)&t46))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t46) + 8, gm_srcue4ionbe9lgh, 786, 49);
        gm_b16_8 t47 = GM_LD(gm_b16_8, (((uint8_t *)&t46) + 8));
        gm_b16_8 t48 = t47;
        gm_b16_8 t49 = GM_LD(gm_b16_8, ((uint8_t *)&t48));
        gm_t39uai1ajk3qv t50;
        gm_f_trap_jftbss6((void *)&t50, (void *)NULL, cx, NULL, t41, t42, t49);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t7g11k265ojef t51;
        GM_ST(uint8_t, (uint8_t *)&t51 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t51 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t51 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t51 + 8, 0);
        ((uint8_t *)&t51)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t51 + 16), t50);
        gm_t7g11k265ojef t52 = t51;
        uint8_t t53 = ((((uint8_t *)&t52))[0] != 0);
        F.ret = t51;
        goto gm_fret;
        goto me24;
mn39:;
ma27:;
        uint8_t t55 = GM_LD(uint8_t, ((((uint8_t *)&F.l5) + 72) + 0));
        if (t55 != ((uint8_t)0ULL)) goto mn54;
        void * t56 = gm_l0;
        gm_tl874t2atnlh3 t57 = gm_l3;
        gm_b16_8 t58;
        GM_ST(void *, (uint8_t *)&t58, (void *)(gm_sdrg69bmlm0tm));
        GM_ST(uintptr_t, (uint8_t *)&t58 + 8, (uintptr_t)(29));
        gm_t39uai1ajk3qv t59;
        gm_f_unsupported_2seq49l((void *)&t59, t56, GM_CONV(gm_cseahvg8v7p0vs, t57), t58);
        gm_t7g11k265ojef t60;
        GM_ST(uint8_t, (uint8_t *)&t60 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t60 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t60 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t60 + 8, 0);
        ((uint8_t *)&t60)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t60 + 16), t59);
        gm_t7g11k265ojef t61 = t60;
        uint8_t t62 = ((((uint8_t *)&t61))[0] != 0);
        F.ret = t60;
        goto gm_fret;
        goto me24;
mn54:;
ma28:;
        gm_t39uai1ajk3qv t64;
        memset(&t64, 0, sizeof t64);
        GM_ST(uint8_t, ((uint8_t *)&t64 + 0), ((uint8_t)6ULL));
        gm_t7g11k265ojef t65;
        GM_ST(uint8_t, (uint8_t *)&t65 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t65 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t65 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t65 + 8, 0);
        ((uint8_t *)&t65)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t65 + 16), t64);
        gm_t7g11k265ojef t66 = t65;
        uint8_t t67 = ((((uint8_t *)&t66))[0] != 0);
        F.ret = t65;
        goto gm_fret;
        goto me24;
mn63:;
ma29:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 784, 5);
me24:;
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
__attribute__((aligned(4))) void gm_f_convert_value_v1i8v3i(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3) {
    struct gm_fr_convert_value_v1i8v3i F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l2; /* from */
    gm_t88rcucg1b7uk gm_l3; /* to */
    unsigned __int128 gm_l4; /* bits */
    gm_tkaapcii86udk gm_l5; /* pl */
    gm_t88rcucg1b7uk gm_l6; /* pointee */
    uint64_t gm_l7; /* n */
    GM_STACK_CHECK(&F, 2385u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    gm_l3 = a3;
    {
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_t88rcucg1b7uk t4 = gm_l3;
        gm_t88rcucg1b7uk t5 = t3;
        gm_t88rcucg1b7uk t6 = t4;
        uint8_t t7 = (gm_eq9m6mvtmgde(((uint8_t *)&t5), ((uint8_t *)&t6)));
        if (!t7) goto ie2;
        {
            gm_t6evimt84lndq t9 = F.l1;
            gm_ttk92ufj2f6eh t10;
            memset(&t10, 0, sizeof t10);
            ((uint8_t *)&t10)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t10 + 16), t9);
            gm_ttk92ufj2f6eh t11 = t10;
            uint8_t t12 = ((((uint8_t *)&t11))[0] != 0);
            F.ret = t10;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        switch (t18) { case ((uint8_t)0ULL): goto ma14; case ((uint8_t)1ULL): goto ma16; case ((uint8_t)2ULL): goto ma16; case ((uint8_t)3ULL): goto ma16; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma16; case ((uint8_t)6ULL): goto ma16; case ((uint8_t)7ULL): goto ma16; case ((uint8_t)8ULL): goto ma16; case ((uint8_t)9ULL): goto ma16; case ((uint8_t)10ULL): goto ma16; case ((uint8_t)11ULL): goto ma16; case ((uint8_t)12ULL): goto ma16; case ((uint8_t)13ULL): goto ma16; default: goto ma16; }
ma14:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t20 != ((uint8_t)0ULL)) goto mn19;
        unsigned __int128 t21 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
        gm_l4 = t21;
        void * t22 = gm_l0;
        void * t23 = gm_l0;
        unsigned __int128 t24 = gm_l4;
        gm_t88rcucg1b7uk t25 = gm_l2;
        __int128 t26 = gm_f_int_value_6hmh8tj(t23, t24, GM_CONV(gm_csguankko4lmhd, t25));
        __int128 t27 = t26;
        unsigned __int128 t28;
        memset(&t28, 0, sizeof t28); memcpy(&t28, ((uint8_t *)&t27), sizeof t28 < 16 ? sizeof t28 : 16);
        gm_t88rcucg1b7uk t29 = gm_l3;
        unsigned __int128 t30 = gm_f_wrap_bt8r149(t22, t28, GM_CONV(gm_csguankko4lmhd, t29));
        gm_t6evimt84lndq t31;
        GM_ST(uint8_t, (uint8_t *)&t31 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t31 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t31 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t31 + 8, 0);
        GM_ST(uint64_t, (uint8_t *)&t31 + 32, 0);
        GM_ST(uint64_t, (uint8_t *)&t31 + 40, 0);
        GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)0ULL));
        GM_ST(unsigned __int128, ((uint8_t *)&t31 + 16), t30);
        gm_ttk92ufj2f6eh t32;
        memset(&t32, 0, sizeof t32);
        ((uint8_t *)&t32)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t32 + 16), t31);
        gm_ttk92ufj2f6eh t33 = t32;
        uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
        F.ret = t32;
        goto gm_fret;
        goto me13;
mn19:;
ma15:;
        uint8_t t36 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t36 != ((uint8_t)4ULL)) goto mn35;
        gm_tkaapcii86udk t37 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l1) + 16));
        gm_l5 = t37;
        {
            void * t40 = gm_l0;
            void * t41 = GM_LD(void *, (((uint8_t *)t40) + 0));
            void * t42 = (void *)(((uint8_t *)t41) + 8);
            gm_t88rcucg1b7uk t43 = gm_l2;
            gm_t53at80pfdus3 t44;
            gm_f_kind_jcnt4hm((void *)&t44, t42, GM_CONV(gm_csguankko4lmhd, t43));
            gm_t53at80pfdus3 t45 = t44;
            uint8_t t46 = GM_LD(uint8_t, (((uint8_t *)&t45) + 0));
            if (t46 != ((uint8_t)8ULL)) goto ie39;
            gm_t88rcucg1b7uk t47 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t45) + 8) + 4));
            gm_l6 = t47;
            {
                void * t50 = gm_l0;
                void * t51 = GM_LD(void *, (((uint8_t *)t50) + 0));
                void * t52 = (void *)(((uint8_t *)t51) + 8);
                gm_t88rcucg1b7uk t53 = gm_l3;
                gm_t53at80pfdus3 t54;
                gm_f_kind_jcnt4hm((void *)&t54, t52, GM_CONV(gm_csguankko4lmhd, t53));
                gm_t53at80pfdus3 t55 = t54;
                uint8_t t56 = GM_LD(uint8_t, (((uint8_t *)&t55) + 0));
                if (t56 != ((uint8_t)10ULL)) goto ie49;
                void * t57 = gm_l0;
                void * t58 = GM_LD(void *, (((uint8_t *)t57) + 0));
                void * t59 = (void *)(((uint8_t *)t58) + 8);
                gm_t88rcucg1b7uk t60 = gm_l6;
                gm_t53at80pfdus3 t61;
                gm_f_kind_jcnt4hm((void *)&t61, t59, GM_CONV(gm_csguankko4lmhd, t60));
                gm_t53at80pfdus3 t62 = t61;
                uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&t62) + 0));
                if (t63 != ((uint8_t)11ULL)) goto ie49;
                uint64_t t64 = GM_LD(uint64_t, ((((uint8_t *)&t62) + 8) + 0));
                gm_l7 = t64;
                {
                    gm_to6glrduves6o t66;
                    void * t67 = GM_LD(void *, (((uint8_t *)&gm_l5) + 0));
                    GM_ST(void *, (uint8_t *)&(*(gm_tkaapcii86udk *)((uint8_t *)&t66 + 0)) + 0, t67);
                    gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l5) + 8));
                    gm_b16_8 t69 = gm_f_append_sajo2rq((void *)NULL, cx, NULL, t68, ((uint64_t)0ULL));
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    GM_ST(gm_b16_8, (uint8_t *)&(*(gm_tkaapcii86udk *)((uint8_t *)&t66 + 0)) + 8, t69);
                    uint64_t t70 = gm_l7;
                    GM_ST(uint64_t, (uint8_t *)&t66 + 24, t70);
                    gm_t6evimt84lndq t71;
                    GM_ST(uint8_t, (uint8_t *)&t71 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t71 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t71 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t71 + 8, 0);
                    GM_ST(uint8_t, ((uint8_t *)&t71 + 0), ((uint8_t)5ULL));
                    GM_ST(gm_to6glrduves6o, ((uint8_t *)&t71 + 16), t66);
                    gm_ttk92ufj2f6eh t72;
                    memset(&t72, 0, sizeof t72);
                    ((uint8_t *)&t72)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t72 + 16), t71);
                    gm_ttk92ufj2f6eh t73 = t72;
                    uint8_t t74 = ((((uint8_t *)&t73))[0] != 0);
                    F.ret = t72;
                    goto gm_fret;
                }
                goto ix49;
ie49:;
ix49:;
            }
            {
                gm_t6evimt84lndq t75 = F.l1;
                gm_ttk92ufj2f6eh t76;
                memset(&t76, 0, sizeof t76);
                ((uint8_t *)&t76)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t76 + 16), t75);
                gm_ttk92ufj2f6eh t77 = t76;
                uint8_t t78 = ((((uint8_t *)&t77))[0] != 0);
                F.ret = t76;
                goto gm_fret;
            }
            goto ix39;
ie39:;
            {
                gm_t6evimt84lndq t80 = F.l1;
                gm_ttk92ufj2f6eh t81;
                memset(&t81, 0, sizeof t81);
                ((uint8_t *)&t81)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t81 + 16), t80);
                gm_ttk92ufj2f6eh t82 = t81;
                uint8_t t83 = ((((uint8_t *)&t82))[0] != 0);
                F.ret = t81;
                goto gm_fret;
            }
ix39:;
        }
        goto me13;
mn35:;
ma16:;
        gm_t6evimt84lndq t85 = F.l1;
        gm_ttk92ufj2f6eh t86;
        memset(&t86, 0, sizeof t86);
        ((uint8_t *)&t86)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t86 + 16), t85);
        gm_ttk92ufj2f6eh t87 = t86;
        uint8_t t88 = ((((uint8_t *)&t87))[0] != 0);
        F.ret = t86;
        goto gm_fret;
        goto me13;
mn84:;
ma17:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 292, 5);
me13:;
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
__attribute__((aligned(4))) void gm_f_display_covn4i2(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_display_covn4i2 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l2; /* t */
    unsigned __int128 gm_l3; /* bits */
    uint8_t gm_l4; /* b */
    uint32_t gm_l5; /* index */
    uint32_t gm_l6; /* id */
    gm_b16_8 gm_l7; /* variants */
    GM_STACK_CHECK(&F, 745u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    gm_l2 = a2;
    {
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma6; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma5; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; default: goto ma6; }
ma3:;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t10 != ((uint8_t)0ULL)) goto mn9;
        unsigned __int128 t11 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
        gm_l3 = t11;
        void * t13 = gm_l0;
        void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
        void * t15 = (void *)(((uint8_t *)t14) + 8);
        gm_t88rcucg1b7uk t16 = gm_l2;
        uint8_t t17 = gm_f_is_integer_8d4f9ek(t15, GM_CONV(gm_csguankko4lmhd, t16));
        if (!t17) goto ie12;
        {
            void * t19 = gm_l0;
            unsigned __int128 t20 = gm_l3;
            gm_t88rcucg1b7uk t21 = gm_l2;
            gm_tbci5ospk5nnn t22;
            gm_f_big_of_1cq0qao((void *)&t22, (void *)NULL, cx, NULL, t19, t20, t21);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_b16_8 t23 = gm_f_to_decimal_8bshn5b((void *)NULL, cx, NULL, &t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_th2lqbo77pvsc t24;
            GM_ST(uint8_t, (uint8_t *)&t24 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t24 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t24 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t24), t23);
            ((uint8_t *)&t24)[16] = 1;
            uint8_t t25 = 0;
            F.ret = t24;
            goto gm_fret;
        }
        goto ix12;
ie12:;
ix12:;
        goto me2;
mn9:;
ma4:;
        uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t27 != ((uint8_t)2ULL)) goto mn26;
        uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
        gm_l4 = t28;
        uint8_t t29 = gm_l4;
        gm_t9qkh0rsooc8d t30;
        GM_ST(uint8_t, ((uint8_t *)&t30 + 0), t29);
        gm_tabb710d9mq2e t31;
        gm_f_format_9u3jvus((void *)&t31, (void *)NULL, cx, NULL, t30);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t32 = t31;
        if (((((uint8_t *)&t32))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t32) + 8, gm_srcue4ionbe9lgh, 764, 28);
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&t32) + 8));
        gm_th2lqbo77pvsc t34;
        GM_ST(uint8_t, (uint8_t *)&t34 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t34 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t34 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t34), t33);
        ((uint8_t *)&t34)[16] = 1;
        uint8_t t35 = 0;
        F.ret = t34;
        goto gm_fret;
        goto me2;
mn26:;
ma5:;
        uint8_t t37 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t37 != ((uint8_t)10ULL)) goto mn36;
        uint32_t t38 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 16) + 0));
        gm_l5 = t38;
        if ((GM_LD(void *, ((((uint8_t *)&F.l1) + 16) + 8)) != 0)) goto mn36;
        {
            void * t41 = gm_l0;
            void * t42 = GM_LD(void *, (((uint8_t *)t41) + 0));
            void * t43 = (void *)(((uint8_t *)t42) + 8);
            gm_t88rcucg1b7uk t44 = gm_l2;
            gm_t53at80pfdus3 t45;
            gm_f_kind_jcnt4hm((void *)&t45, t43, GM_CONV(gm_csguankko4lmhd, t44));
            gm_t53at80pfdus3 t46 = t45;
            uint8_t t47 = GM_LD(uint8_t, (((uint8_t *)&t46) + 0));
            if (t47 != ((uint8_t)17ULL)) goto ie40;
            uint32_t t48 = GM_LD(uint32_t, ((((uint8_t *)&t46) + 8) + 0));
            gm_l6 = t48;
            {
                uint32_t t51 = gm_l6;
                uint64_t t52 = (uint64_t)t51;
                uint64_t t53 = (uintptr_t)t52;
                void * t54 = gm_l0;
                void * t55 = GM_LD(void *, (((uint8_t *)t54) + 0));
                uint64_t t56 = GM_LD(uintptr_t, ((((uint8_t *)t55) + 1088) + 0) + 8);
                if (t53 >= (uintptr_t)(t56)) gm_trap_index(t53, t56, gm_srcue4ionbe9lgh, 767, 36);
                uint8_t t57 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t55) + 1088) + 0)) + t53 * 184) + 24) + 0));
                if (t57 != ((uint8_t)1ULL)) goto ie50;
                gm_b16_8 t58 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t55) + 1088) + 0)) + t53 * 184) + 24) + 8) + 8));
                gm_l7 = t58;
                {
                    uint32_t t60 = gm_l5;
                    uint64_t t61 = (uint64_t)t60;
                    uint64_t t62 = (uintptr_t)t61;
                    uint64_t t63 = GM_LD(uintptr_t, ((uint8_t *)&gm_l7) + 8);
                    if (t62 >= (uintptr_t)(t63)) gm_trap_index(t62, t63, gm_srcue4ionbe9lgh, 768, 38);
                    gm_b16_8 t64 = GM_LD(gm_b16_8, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l7)) + t62 * 72) + 0));
                    gm_tnpifv20irnph t65;
                    GM_ST(gm_b16_8, ((uint8_t *)&t65 + 0), t64);
                    gm_tabb710d9mq2e t66;
                    gm_f_format_g0ibdjv((void *)&t66, (void *)NULL, cx, NULL, t65);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_tabb710d9mq2e t67 = t66;
                    if (((((uint8_t *)&t67))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t67) + 8, gm_srcue4ionbe9lgh, 768, 20);
                    gm_b16_8 t68 = GM_LD(gm_b16_8, (((uint8_t *)&t67) + 8));
                    gm_th2lqbo77pvsc t69;
                    GM_ST(uint8_t, (uint8_t *)&t69 + 17, 0);
                    GM_ST(uint16_t, (uint8_t *)&t69 + 18, 0);
                    GM_ST(uint32_t, (uint8_t *)&t69 + 20, 0);
                    GM_ST(gm_b16_8, ((uint8_t *)&t69), t68);
                    ((uint8_t *)&t69)[16] = 1;
                    uint8_t t70 = 0;
                    F.ret = t69;
                    goto gm_fret;
                }
                goto ix50;
ie50:;
                {
                    gm_th2lqbo77pvsc t72;
                    memset(&t72, 0, sizeof t72);
                    uint8_t t73 = 0;
                    F.ret = t72;
                    goto gm_fret;
                }
ix50:;
            }
            goto ix40;
ie40:;
            {
                gm_th2lqbo77pvsc t75;
                memset(&t75, 0, sizeof t75);
                uint8_t t76 = 0;
                F.ret = t75;
                goto gm_fret;
            }
ix40:;
        }
        goto me2;
mn36:;
ma6:;
        goto me2;
mn77:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 762, 5);
me2:;
    }
    {
        gm_th2lqbo77pvsc t79;
        memset(&t79, 0, sizeof t79);
        uint8_t t80 = 0;
        F.ret = t79;
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
__attribute__((aligned(4))) void gm_f_equal_hinsj77(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, const void * a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_equal_hinsj77 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_tl874t2atnlh3 gm_l4; /* span */
    void * gm_l5; /* tys */
    unsigned __int128 gm_l6; /* x */
    unsigned __int128 gm_l7; /* y */
    double gm_l8; /* x */
    double gm_l9; /* y */
    uint8_t gm_l10; /* x */
    uint8_t gm_l11; /* y */
    uint32_t gm_l12; /* f */
    uint32_t gm_l13; /* g */
    void * gm_l14; /* x */
    void * gm_l15; /* y */
    gm_t88rcucg1b7uk gm_l16; /* inner */
    void * gm_l17; /* x */
    void * gm_l18; /* y */
    gm_t88rcucg1b7uk gm_l19; /* ok */
    void * gm_l20; /* x */
    void * gm_l21; /* y */
    gm_t88rcucg1b7uk gm_l22; /* err */
    uint32_t gm_l23; /* index */
    void * gm_l24; /* pa */
    uint32_t gm_l25; /* index_b */
    void * gm_l26; /* pb */
    void * gm_l27; /* x */
    void * gm_l28; /* y */
    gm_t88rcucg1b7uk gm_l29; /* pt */
    void * gm_l30; /* x */
    void * gm_l31; /* y */
    gm_b16_8 gm_l32; /* elems */
    uint64_t gm_l33; /* <tmp> */
    uint64_t gm_l34; /* <tmp> */
    uint64_t gm_l35; /* i */
    gm_to6glrduves6o gm_l36; /* x */
    gm_to6glrduves6o gm_l37; /* y */
    gm_t88rcucg1b7uk gm_l38; /* et */
    uint64_t gm_l39; /* <tmp> */
    uint64_t gm_l40; /* <tmp> */
    uint64_t gm_l41; /* i */
    gm_tkaapcii86udk gm_l42; /* x */
    gm_tkaapcii86udk gm_l43; /* y */
    GM_STACK_CHECK(&F, 11374u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    memcpy(&F.l1, a1, sizeof F.l1);
    memcpy(&F.l2, a2, sizeof F.l2);
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        void * t4 = (void *)(((uint8_t *)t3) + 8);
        gm_l5 = t4;
    }
    {
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        switch (t20) { case ((uint8_t)0ULL): goto ma6; case ((uint8_t)1ULL): goto ma7; case ((uint8_t)2ULL): goto ma8; case ((uint8_t)3ULL): goto ma15; case ((uint8_t)4ULL): goto ma17; case ((uint8_t)5ULL): goto ma16; case ((uint8_t)6ULL): goto ma9; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma14; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma18; case ((uint8_t)13ULL): goto ma18; default: goto ma18; }
ma6:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t22 != ((uint8_t)0ULL)) goto mn21;
        unsigned __int128 t23 = GM_LD(unsigned __int128, (((uint8_t *)&F.l1) + 16));
        gm_l6 = t23;
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t26 != ((uint8_t)0ULL)) goto ie25;
            unsigned __int128 t27 = GM_LD(unsigned __int128, (((uint8_t *)&F.l2) + 16));
            gm_l7 = t27;
            {
                unsigned __int128 t29 = gm_l6;
                unsigned __int128 t30 = gm_l7;
                uint8_t t31 = ((t29 == t30));
                gm_tsg40glpvq01o t32;
                memset(&t32, 0, sizeof t32);
                ((uint8_t *)&t32)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t32 + 16), t31);
                gm_tsg40glpvq01o t33 = t32;
                uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
                F.ret = t32;
                goto gm_fret;
            }
            goto ix25;
ie25:;
            {
                gm_tsg40glpvq01o t36;
                memset(&t36, 0, sizeof t36);
                ((uint8_t *)&t36)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t36 + 16), ((uint8_t)0));
                gm_tsg40glpvq01o t37 = t36;
                uint8_t t38 = ((((uint8_t *)&t37))[0] != 0);
                F.ret = t36;
                goto gm_fret;
            }
ix25:;
        }
        goto me5;
mn21:;
ma7:;
        uint8_t t40 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t40 != ((uint8_t)1ULL)) goto mn39;
        double t41 = GM_LD(double, (((uint8_t *)&F.l1) + 16));
        gm_l8 = t41;
        {
            uint8_t t44 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t44 != ((uint8_t)1ULL)) goto ie43;
            double t45 = GM_LD(double, (((uint8_t *)&F.l2) + 16));
            gm_l9 = t45;
            {
                double t47 = gm_l8;
                double t48 = gm_l9;
                uint8_t t49 = ((t47 == t48));
                gm_tsg40glpvq01o t50;
                memset(&t50, 0, sizeof t50);
                ((uint8_t *)&t50)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t50 + 16), t49);
                gm_tsg40glpvq01o t51 = t50;
                uint8_t t52 = ((((uint8_t *)&t51))[0] != 0);
                F.ret = t50;
                goto gm_fret;
            }
            goto ix43;
ie43:;
            {
                gm_tsg40glpvq01o t54;
                memset(&t54, 0, sizeof t54);
                ((uint8_t *)&t54)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t54 + 16), ((uint8_t)0));
                gm_tsg40glpvq01o t55 = t54;
                uint8_t t56 = ((((uint8_t *)&t55))[0] != 0);
                F.ret = t54;
                goto gm_fret;
            }
ix43:;
        }
        goto me5;
mn39:;
ma8:;
        uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t58 != ((uint8_t)2ULL)) goto mn57;
        uint8_t t59 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 16));
        gm_l10 = t59;
        {
            uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t62 != ((uint8_t)2ULL)) goto ie61;
            uint8_t t63 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 16));
            gm_l11 = t63;
            {
                uint8_t t65 = gm_l10;
                uint8_t t66 = gm_l11;
                uint8_t t67 = ((t65 == t66));
                gm_tsg40glpvq01o t68;
                memset(&t68, 0, sizeof t68);
                ((uint8_t *)&t68)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t68 + 16), t67);
                gm_tsg40glpvq01o t69 = t68;
                uint8_t t70 = ((((uint8_t *)&t69))[0] != 0);
                F.ret = t68;
                goto gm_fret;
            }
            goto ix61;
ie61:;
            {
                gm_tsg40glpvq01o t72;
                memset(&t72, 0, sizeof t72);
                ((uint8_t *)&t72)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t72 + 16), ((uint8_t)0));
                gm_tsg40glpvq01o t73 = t72;
                uint8_t t74 = ((((uint8_t *)&t73))[0] != 0);
                F.ret = t72;
                goto gm_fret;
            }
ix61:;
        }
        goto me5;
mn57:;
ma9:;
        uint8_t t76 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t76 != ((uint8_t)6ULL)) goto mn75;
        uint8_t t77 = ((GM_LD(uint8_t, ((uint8_t *)&F.l2) + 0) == ((uint8_t)6ULL)));
        gm_tsg40glpvq01o t78;
        memset(&t78, 0, sizeof t78);
        ((uint8_t *)&t78)[0] = 0;
        GM_ST(uint8_t, ((uint8_t *)&t78 + 16), t77);
        gm_tsg40glpvq01o t79 = t78;
        uint8_t t80 = ((((uint8_t *)&t79))[0] != 0);
        F.ret = t78;
        goto gm_fret;
        goto me5;
mn75:;
ma10:;
        uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t82 != ((uint8_t)11ULL)) goto mn81;
        uint32_t t83 = GM_LD(uint32_t, (((uint8_t *)&F.l1) + 16));
        gm_l12 = t83;
        {
            uint8_t t86 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t86 != ((uint8_t)11ULL)) goto ie85;
            uint32_t t87 = GM_LD(uint32_t, (((uint8_t *)&F.l2) + 16));
            gm_l13 = t87;
            {
                uint32_t t89 = gm_l12;
                uint32_t t90 = gm_l13;
                uint8_t t91 = ((t89 == t90));
                gm_tsg40glpvq01o t92;
                memset(&t92, 0, sizeof t92);
                ((uint8_t *)&t92)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t92 + 16), t91);
                gm_tsg40glpvq01o t93 = t92;
                uint8_t t94 = ((((uint8_t *)&t93))[0] != 0);
                F.ret = t92;
                goto gm_fret;
            }
            goto ix85;
ie85:;
            {
                gm_tsg40glpvq01o t96;
                memset(&t96, 0, sizeof t96);
                ((uint8_t *)&t96)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t96 + 16), ((uint8_t)0));
                gm_tsg40glpvq01o t97 = t96;
                uint8_t t98 = ((((uint8_t *)&t97))[0] != 0);
                F.ret = t96;
                goto gm_fret;
            }
ix85:;
        }
        goto me5;
mn81:;
ma11:;
        uint8_t t100 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t100 != ((uint8_t)7ULL)) goto mn99;
        void * t101 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
        gm_l14 = t101;
        {
            uint8_t t104 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t104 != ((uint8_t)7ULL)) goto ie103;
            void * t105 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l15 = t105;
            {
                void * t108 = gm_l5;
                gm_t88rcucg1b7uk t109 = gm_l3;
                gm_t53at80pfdus3 t110;
                gm_f_kind_jcnt4hm((void *)&t110, t108, GM_CONV(gm_csguankko4lmhd, t109));
                gm_t53at80pfdus3 t111 = t110;
                uint8_t t112 = GM_LD(uint8_t, (((uint8_t *)&t111) + 0));
                if (t112 != ((uint8_t)13ULL)) goto ie107;
                gm_t88rcucg1b7uk t113 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t111) + 8));
                gm_l16 = t113;
                {
                    void * t115 = gm_l0;
                    void * t116 = gm_l14;
                    void * t117 = gm_l15;
                    gm_t88rcucg1b7uk t118 = gm_l16;
                    gm_tl874t2atnlh3 t119 = gm_l4;
                    gm_f_equal_hinsj77((void *)&(*(gm_tsg40glpvq01o *)gm_out), (void *)NULL, cx, NULL, t115, &(*(gm_t6evimt84lndq *)((uint8_t *)t116)), &(*(gm_t6evimt84lndq *)((uint8_t *)t117)), t118, t119);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix107;
ie107:;
                {
                    void * t121 = gm_l0;
                    gm_tl874t2atnlh3 t122 = gm_l4;
                    gm_b16_8 t123;
                    GM_ST(void *, (uint8_t *)&t123, (void *)(gm_s9ofbo8c28rrv));
                    GM_ST(uintptr_t, (uint8_t *)&t123 + 8, (uintptr_t)(15));
                    gm_t39uai1ajk3qv t124;
                    gm_f_unsupported_2seq49l((void *)&t124, t121, GM_CONV(gm_cseahvg8v7p0vs, t122), t123);
                    gm_tsg40glpvq01o t125;
                    GM_ST(uint8_t, (uint8_t *)&t125 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t125 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t125 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t125 + 8, 0);
                    ((uint8_t *)&t125)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t125 + 16), t124);
                    gm_tsg40glpvq01o t126 = t125;
                    uint8_t t127 = ((((uint8_t *)&t126))[0] != 0);
                    F.ret = t125;
                    goto gm_fret;
                }
ix107:;
            }
            goto ix103;
ie103:;
            {
                gm_tsg40glpvq01o t129;
                memset(&t129, 0, sizeof t129);
                ((uint8_t *)&t129)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t129 + 16), ((uint8_t)0));
                gm_tsg40glpvq01o t130 = t129;
                uint8_t t131 = ((((uint8_t *)&t130))[0] != 0);
                F.ret = t129;
                goto gm_fret;
            }
ix103:;
        }
        goto me5;
mn99:;
ma12:;
        uint8_t t133 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t133 != ((uint8_t)8ULL)) goto mn132;
        void * t134 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
        gm_l17 = t134;
        {
            uint8_t t137 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t137 != ((uint8_t)8ULL)) goto ie136;
            void * t138 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l18 = t138;
            {
                void * t141 = gm_l5;
                gm_t88rcucg1b7uk t142 = gm_l3;
                gm_t53at80pfdus3 t143;
                gm_f_kind_jcnt4hm((void *)&t143, t141, GM_CONV(gm_csguankko4lmhd, t142));
                gm_t53at80pfdus3 t144 = t143;
                uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&t144) + 0));
                if (t145 != ((uint8_t)14ULL)) goto ie140;
                gm_t88rcucg1b7uk t146 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t144) + 8) + 4));
                gm_l19 = t146;
                {
                    void * t148 = gm_l0;
                    void * t149 = gm_l17;
                    void * t150 = gm_l18;
                    gm_t88rcucg1b7uk t151 = gm_l19;
                    gm_tl874t2atnlh3 t152 = gm_l4;
                    gm_f_equal_hinsj77((void *)&(*(gm_tsg40glpvq01o *)gm_out), (void *)NULL, cx, NULL, t148, &(*(gm_t6evimt84lndq *)((uint8_t *)t149)), &(*(gm_t6evimt84lndq *)((uint8_t *)t150)), t151, t152);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix140;
ie140:;
                {
                    void * t154 = gm_l0;
                    gm_tl874t2atnlh3 t155 = gm_l4;
                    gm_b16_8 t156;
                    GM_ST(void *, (uint8_t *)&t156, (void *)(gm_s9ofbo8c28rrv));
                    GM_ST(uintptr_t, (uint8_t *)&t156 + 8, (uintptr_t)(15));
                    gm_t39uai1ajk3qv t157;
                    gm_f_unsupported_2seq49l((void *)&t157, t154, GM_CONV(gm_cseahvg8v7p0vs, t155), t156);
                    gm_tsg40glpvq01o t158;
                    GM_ST(uint8_t, (uint8_t *)&t158 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t158 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t158 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t158 + 8, 0);
                    ((uint8_t *)&t158)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t158 + 16), t157);
                    gm_tsg40glpvq01o t159 = t158;
                    uint8_t t160 = ((((uint8_t *)&t159))[0] != 0);
                    F.ret = t158;
                    goto gm_fret;
                }
ix140:;
            }
            goto ix136;
ie136:;
            {
                gm_tsg40glpvq01o t162;
                memset(&t162, 0, sizeof t162);
                ((uint8_t *)&t162)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t162 + 16), ((uint8_t)0));
                gm_tsg40glpvq01o t163 = t162;
                uint8_t t164 = ((((uint8_t *)&t163))[0] != 0);
                F.ret = t162;
                goto gm_fret;
            }
ix136:;
        }
        goto me5;
mn132:;
ma13:;
        uint8_t t166 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t166 != ((uint8_t)9ULL)) goto mn165;
        void * t167 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
        gm_l20 = t167;
        {
            uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t170 != ((uint8_t)9ULL)) goto ie169;
            void * t171 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l21 = t171;
            {
                void * t174 = gm_l5;
                gm_t88rcucg1b7uk t175 = gm_l3;
                gm_t53at80pfdus3 t176;
                gm_f_kind_jcnt4hm((void *)&t176, t174, GM_CONV(gm_csguankko4lmhd, t175));
                gm_t53at80pfdus3 t177 = t176;
                uint8_t t178 = GM_LD(uint8_t, (((uint8_t *)&t177) + 0));
                if (t178 != ((uint8_t)14ULL)) goto ie173;
                gm_t88rcucg1b7uk t179 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t177) + 8) + 0));
                gm_l22 = t179;
                {
                    void * t181 = gm_l0;
                    void * t182 = gm_l20;
                    void * t183 = gm_l21;
                    gm_t88rcucg1b7uk t184 = gm_l22;
                    gm_tl874t2atnlh3 t185 = gm_l4;
                    gm_f_equal_hinsj77((void *)&(*(gm_tsg40glpvq01o *)gm_out), (void *)NULL, cx, NULL, t181, &(*(gm_t6evimt84lndq *)((uint8_t *)t182)), &(*(gm_t6evimt84lndq *)((uint8_t *)t183)), t184, t185);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix173;
ie173:;
                {
                    void * t187 = gm_l0;
                    gm_tl874t2atnlh3 t188 = gm_l4;
                    gm_b16_8 t189;
                    GM_ST(void *, (uint8_t *)&t189, (void *)(gm_s9ofbo8c28rrv));
                    GM_ST(uintptr_t, (uint8_t *)&t189 + 8, (uintptr_t)(15));
                    gm_t39uai1ajk3qv t190;
                    gm_f_unsupported_2seq49l((void *)&t190, t187, GM_CONV(gm_cseahvg8v7p0vs, t188), t189);
                    gm_tsg40glpvq01o t191;
                    GM_ST(uint8_t, (uint8_t *)&t191 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t191 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t191 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t191 + 8, 0);
                    ((uint8_t *)&t191)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t191 + 16), t190);
                    gm_tsg40glpvq01o t192 = t191;
                    uint8_t t193 = ((((uint8_t *)&t192))[0] != 0);
                    F.ret = t191;
                    goto gm_fret;
                }
ix173:;
            }
            goto ix169;
ie169:;
            {
                gm_tsg40glpvq01o t195;
                memset(&t195, 0, sizeof t195);
                ((uint8_t *)&t195)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t195 + 16), ((uint8_t)0));
                gm_tsg40glpvq01o t196 = t195;
                uint8_t t197 = ((((uint8_t *)&t196))[0] != 0);
                F.ret = t195;
                goto gm_fret;
            }
ix169:;
        }
        goto me5;
mn165:;
ma14:;
        uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t199 != ((uint8_t)10ULL)) goto mn198;
        uint32_t t200 = GM_LD(uint32_t, ((((uint8_t *)&F.l1) + 16) + 0));
        gm_l23 = t200;
        void * t201 = GM_LD(void *, ((((uint8_t *)&F.l1) + 16) + 8));
        gm_l24 = t201;
        {
            uint8_t t204 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t204 != ((uint8_t)10ULL)) goto ie203;
            uint32_t t205 = GM_LD(uint32_t, ((((uint8_t *)&F.l2) + 16) + 0));
            gm_l25 = t205;
            void * t206 = GM_LD(void *, ((((uint8_t *)&F.l2) + 16) + 8));
            gm_l26 = t206;
            {
                uint32_t t209 = gm_l23;
                uint32_t t210 = gm_l25;
                uint8_t t211 = (!(t209 == t210));
                if (!t211) goto ie208;
                {
                    gm_tsg40glpvq01o t213;
                    memset(&t213, 0, sizeof t213);
                    ((uint8_t *)&t213)[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&t213 + 16), ((uint8_t)0));
                    gm_tsg40glpvq01o t214 = t213;
                    uint8_t t215 = ((((uint8_t *)&t214))[0] != 0);
                    F.ret = t213;
                    goto gm_fret;
                }
                goto ix208;
ie208:;
ix208:;
            }
            {
                void * t216 = gm_l24;
                void * t217 = t216;
                void * t218;
                if ((GM_LD(void *, ((uint8_t *)&t217)) != 0)) {
                    void * t219 = GM_LD(void *, (((uint8_t *)&t217) + 0));
                    t218 = t219;
                } else {
                    gm_tsg40glpvq01o t220;
                    memset(&t220, 0, sizeof t220);
                    ((uint8_t *)&t220)[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&t220 + 16), ((uint8_t)1));
                    gm_tsg40glpvq01o t221 = t220;
                    uint8_t t222 = ((((uint8_t *)&t221))[0] != 0);
                    F.ret = t220;
                    goto gm_fret;
                }
                gm_l27 = t218;
            }
            {
                void * t223 = gm_l26;
                void * t224 = t223;
                void * t225;
                if ((GM_LD(void *, ((uint8_t *)&t224)) != 0)) {
                    void * t226 = GM_LD(void *, (((uint8_t *)&t224) + 0));
                    t225 = t226;
                } else {
                    gm_tsg40glpvq01o t227;
                    memset(&t227, 0, sizeof t227);
                    ((uint8_t *)&t227)[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&t227 + 16), ((uint8_t)1));
                    gm_tsg40glpvq01o t228 = t227;
                    uint8_t t229 = ((((uint8_t *)&t228))[0] != 0);
                    F.ret = t227;
                    goto gm_fret;
                }
                gm_l28 = t225;
            }
            {
                void * t230 = gm_l0;
                gm_t88rcucg1b7uk t231 = gm_l3;
                uint32_t t232 = gm_l23;
                gm_tkn42u7na3t81 t233 = gm_f_payload_ty_mu1ii1k(t230, GM_CONV(gm_csguankko4lmhd, t231), t232);
                gm_tkn42u7na3t81 t234 = t233;
                gm_t88rcucg1b7uk t235;
                if (((((uint8_t *)&t234))[4] != 0)) {
                    gm_t88rcucg1b7uk t236 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t234) + 0));
                    t235 = t236;
                } else {
                    void * t237 = gm_l0;
                    gm_tl874t2atnlh3 t238 = gm_l4;
                    gm_b16_8 t239;
                    GM_ST(void *, (uint8_t *)&t239, (void *)(gm_s9ofbo8c28rrv));
                    GM_ST(uintptr_t, (uint8_t *)&t239 + 8, (uintptr_t)(15));
                    gm_t39uai1ajk3qv t240;
                    gm_f_unsupported_2seq49l((void *)&t240, t237, GM_CONV(gm_cseahvg8v7p0vs, t238), t239);
                    gm_tsg40glpvq01o t241;
                    GM_ST(uint8_t, (uint8_t *)&t241 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t241 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t241 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t241 + 8, 0);
                    ((uint8_t *)&t241)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t241 + 16), t240);
                    gm_tsg40glpvq01o t242 = t241;
                    uint8_t t243 = ((((uint8_t *)&t242))[0] != 0);
                    F.ret = t241;
                    goto gm_fret;
                }
                gm_l29 = t235;
            }
            {
                void * t244 = gm_l0;
                void * t245 = gm_l27;
                void * t246 = gm_l28;
                gm_t88rcucg1b7uk t247 = gm_l29;
                gm_tl874t2atnlh3 t248 = gm_l4;
                gm_f_equal_hinsj77((void *)&(*(gm_tsg40glpvq01o *)gm_out), (void *)NULL, cx, NULL, t244, &(*(gm_t6evimt84lndq *)((uint8_t *)t245)), &(*(gm_t6evimt84lndq *)((uint8_t *)t246)), t247, t248);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix203;
ie203:;
            {
                gm_tsg40glpvq01o t250;
                memset(&t250, 0, sizeof t250);
                ((uint8_t *)&t250)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t250 + 16), ((uint8_t)0));
                gm_tsg40glpvq01o t251 = t250;
                uint8_t t252 = ((((uint8_t *)&t251))[0] != 0);
                F.ret = t250;
                goto gm_fret;
            }
ix203:;
        }
        goto me5;
mn198:;
ma15:;
        uint8_t t254 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t254 != ((uint8_t)3ULL)) goto mn253;
        void * t255 = GM_LD(void *, (((uint8_t *)&F.l1) + 16));
        gm_l30 = t255;
        {
            uint8_t t258 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t258 != ((uint8_t)3ULL)) goto ie257;
            void * t259 = GM_LD(void *, (((uint8_t *)&F.l2) + 16));
            gm_l31 = t259;
            {
                void * t262 = gm_l30;
                uint64_t t263 = GM_LD(uintptr_t, (((uint8_t *)t262) + 0) + 8);
                void * t264 = gm_l31;
                uint64_t t265 = GM_LD(uintptr_t, (((uint8_t *)t264) + 0) + 8);
                uint8_t t266 = (!(t263 == t265));
                if (!t266) goto ie261;
                {
                    gm_tsg40glpvq01o t268;
                    memset(&t268, 0, sizeof t268);
                    ((uint8_t *)&t268)[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&t268 + 16), ((uint8_t)0));
                    gm_tsg40glpvq01o t269 = t268;
                    uint8_t t270 = ((((uint8_t *)&t269))[0] != 0);
                    F.ret = t268;
                    goto gm_fret;
                }
                goto ix261;
ie261:;
ix261:;
            }
            {
                void * t271 = gm_l0;
                gm_t88rcucg1b7uk t272 = gm_l3;
                void * t273 = gm_l30;
                uint64_t t274 = GM_LD(uintptr_t, (((uint8_t *)t273) + 0) + 8);
                gm_th2lqbo77pvsc t275;
                gm_f_field_tys_foan32f((void *)&t275, (void *)NULL, cx, NULL, t271, t272, t274);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_th2lqbo77pvsc t276 = t275;
                gm_b16_8 t277;
                if (((((uint8_t *)&t276))[16] != 0)) {
                    gm_b16_8 t278 = GM_LD(gm_b16_8, (((uint8_t *)&t276) + 0));
                    t277 = t278;
                } else {
                    void * t279 = gm_l0;
                    gm_tl874t2atnlh3 t280 = gm_l4;
                    gm_b16_8 t281;
                    GM_ST(void *, (uint8_t *)&t281, (void *)(gm_s9ofbo8c28rrv));
                    GM_ST(uintptr_t, (uint8_t *)&t281 + 8, (uintptr_t)(15));
                    gm_t39uai1ajk3qv t282;
                    gm_f_unsupported_2seq49l((void *)&t282, t279, GM_CONV(gm_cseahvg8v7p0vs, t280), t281);
                    gm_tsg40glpvq01o t283;
                    GM_ST(uint8_t, (uint8_t *)&t283 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t283 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t283 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t283 + 8, 0);
                    ((uint8_t *)&t283)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t283 + 16), t282);
                    gm_tsg40glpvq01o t284 = t283;
                    uint8_t t285 = ((((uint8_t *)&t284))[0] != 0);
                    F.ret = t283;
                    goto gm_fret;
                }
                gm_l32 = t277;
            }
            {
                {
                    gm_l34 = ((uint64_t)0ULL);
                }
                {
                    void * t287 = gm_l30;
                    uint64_t t288 = GM_LD(uintptr_t, (((uint8_t *)t287) + 0) + 8);
                    gm_l33 = t288;
                }
                {
                    for (;;) {
                        uint64_t t290 = gm_l34;
                        uint64_t t291 = gm_l33;
                        uint8_t t292 = (t290 < t291);
                        if (!t292) goto lb289;
                        {
                            uint64_t t295 = gm_l34;
                            uint64_t t296 = t295;
                            uint64_t t299 = GM_LD(uint64_t, ((uint8_t *)&t296));
                            gm_l35 = t299;
                            {
                                void * t302 = gm_l0;
                                uint64_t t303 = gm_l35;
                                uint64_t t304 = (uintptr_t)t303;
                                void * t305 = gm_l30;
                                uint64_t t306 = GM_LD(uintptr_t, (((uint8_t *)t305) + 0) + 8);
                                if (t304 >= (uintptr_t)(t306)) gm_trap_index(t304, t306, gm_srcue4ionbe9lgh, 703, 36);
                                uint64_t t307 = gm_l35;
                                uint64_t t308 = (uintptr_t)t307;
                                void * t309 = gm_l31;
                                uint64_t t310 = GM_LD(uintptr_t, (((uint8_t *)t309) + 0) + 8);
                                if (t308 >= (uintptr_t)(t310)) gm_trap_index(t308, t310, gm_srcue4ionbe9lgh, 703, 48);
                                uint64_t t311 = gm_l35;
                                uint64_t t312 = (uintptr_t)t311;
                                uint64_t t313 = GM_LD(uintptr_t, ((uint8_t *)&gm_l32) + 8);
                                if (t312 >= (uintptr_t)(t313)) gm_trap_index(t312, t313, gm_srcue4ionbe9lgh, 703, 60);
                                gm_t88rcucg1b7uk t314 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l32)) + t312 * 4));
                                gm_tl874t2atnlh3 t315 = gm_l4;
                                gm_tsg40glpvq01o t316;
                                gm_f_equal_hinsj77((void *)&t316, (void *)NULL, cx, NULL, t302, &(*(gm_t6evimt84lndq *)((uint8_t *)GM_LD(void *, (((uint8_t *)t305) + 0)) + t304 * 48)), &(*(gm_t6evimt84lndq *)((uint8_t *)GM_LD(void *, (((uint8_t *)t309) + 0)) + t308 * 48)), t314, t315);
                                if (gm_unw.kind) {
                                    goto gm_ll293;
                                }
                                gm_tsg40glpvq01o t317 = t316;
                                if (((((uint8_t *)&t317))[0] != 0)) {
                                    gm_t39uai1ajk3qv t318 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t317) + 16));
                                    gm_tsg40glpvq01o t319;
                                    GM_ST(uint8_t, (uint8_t *)&t319 + 1, 0);
                                    GM_ST(uint16_t, (uint8_t *)&t319 + 2, 0);
                                    GM_ST(uint32_t, (uint8_t *)&t319 + 4, 0);
                                    GM_ST(uint64_t, (uint8_t *)&t319 + 8, 0);
                                    ((uint8_t *)&t319)[0] = 1;
                                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t319 + 16), t318);
                                    F.ret = t319;
                                    goto gm_fret;
                                }
                                uint8_t t320 = GM_LD(uint8_t, (((uint8_t *)&t317) + 16));
                                uint8_t t321 = (uint8_t)!t320;
                                if (!t321) goto ie301;
                                {
                                    gm_tsg40glpvq01o t323;
                                    memset(&t323, 0, sizeof t323);
                                    ((uint8_t *)&t323)[0] = 0;
                                    GM_ST(uint8_t, ((uint8_t *)&t323 + 16), ((uint8_t)0));
                                    gm_tsg40glpvq01o t324 = t323;
                                    uint8_t t325 = ((((uint8_t *)&t324))[0] != 0);
                                    F.ret = t323;
                                    goto gm_fret;
                                }
                                goto ix301;
ie301:;
ix301:;
                            }
                            goto me297;
mn298:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 702, 13);
me297:;
                        }
lc289:;
                        {
                            uint64_t t327 = gm_l34;
                            uint64_t r328; bool o329 = __builtin_add_overflow(t327, ((uint64_t)1ULL), &r328);
                            uint64_t t330 = r328;
                            gm_l34 = t330;
                        }
                    }
gm_ll293:;
                    if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                        if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb289; }
                        if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc289; }
                    }
                    goto gm_lad;
lb289:;
                }
            }
            {
                gm_tsg40glpvq01o t331;
                memset(&t331, 0, sizeof t331);
                ((uint8_t *)&t331)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t331 + 16), ((uint8_t)1));
                gm_tsg40glpvq01o t332 = t331;
                uint8_t t333 = ((((uint8_t *)&t332))[0] != 0);
                F.ret = t331;
                goto gm_fret;
            }
            goto ix257;
ie257:;
            {
                gm_tsg40glpvq01o t335;
                memset(&t335, 0, sizeof t335);
                ((uint8_t *)&t335)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t335 + 16), ((uint8_t)0));
                gm_tsg40glpvq01o t336 = t335;
                uint8_t t337 = ((((uint8_t *)&t336))[0] != 0);
                F.ret = t335;
                goto gm_fret;
            }
ix257:;
        }
        goto me5;
mn253:;
ma16:;
        uint8_t t339 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t339 != ((uint8_t)5ULL)) goto mn338;
        gm_to6glrduves6o t340 = GM_LD(gm_to6glrduves6o, (((uint8_t *)&F.l1) + 16));
        gm_l36 = t340;
        {
            uint8_t t343 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t343 != ((uint8_t)5ULL)) goto ie342;
            gm_to6glrduves6o t344 = GM_LD(gm_to6glrduves6o, (((uint8_t *)&F.l2) + 16));
            gm_l37 = t344;
            {
                void * t347 = gm_l5;
                gm_t88rcucg1b7uk t348 = gm_l3;
                gm_t53at80pfdus3 t349;
                gm_f_kind_jcnt4hm((void *)&t349, t347, GM_CONV(gm_csguankko4lmhd, t348));
                gm_t53at80pfdus3 t350 = t349;
                uint8_t t351 = GM_LD(uint8_t, (((uint8_t *)&t350) + 0));
                if (t351 != ((uint8_t)10ULL)) goto ie346;
                gm_t88rcucg1b7uk t352 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t350) + 8) + 4));
                gm_l38 = t352;
                {
                    uint64_t t355 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 24));
                    uint64_t t356 = GM_LD(uint64_t, (((uint8_t *)&gm_l37) + 24));
                    uint8_t t357 = (!(t355 == t356));
                    if (!t357) goto ie354;
                    {
                        gm_tsg40glpvq01o t359;
                        memset(&t359, 0, sizeof t359);
                        ((uint8_t *)&t359)[0] = 0;
                        GM_ST(uint8_t, ((uint8_t *)&t359 + 16), ((uint8_t)0));
                        gm_tsg40glpvq01o t360 = t359;
                        uint8_t t361 = ((((uint8_t *)&t360))[0] != 0);
                        F.ret = t359;
                        goto gm_fret;
                    }
                    goto ix354;
ie354:;
ix354:;
                }
                {
                    {
                        gm_l40 = ((uint64_t)0ULL);
                    }
                    {
                        uint64_t t363 = GM_LD(uint64_t, (((uint8_t *)&gm_l36) + 24));
                        gm_l39 = t363;
                    }
                    {
                        for (;;) {
                            uint64_t t365 = gm_l40;
                            uint64_t t366 = gm_l39;
                            uint8_t t367 = (t365 < t366);
                            if (!t367) goto lb364;
                            {
                                uint64_t t370 = gm_l40;
                                uint64_t t371 = t370;
                                uint64_t t374 = GM_LD(uint64_t, ((uint8_t *)&t371));
                                gm_l41 = t374;
                                {
                                    void * t377 = gm_l0;
                                    void * t378 = gm_l0;
                                    uint64_t t379 = gm_l41;
                                    gm_tkaapcii86udk t380;
                                    gm_f_offset_nkn711s((void *)&t380, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)(((uint8_t *)&gm_l36) + 0)), t379);
                                    if (gm_unw.kind) {
                                        goto gm_ll368;
                                    }
                                    gm_t6evimt84lndq t381;
                                    gm_f_load_nf7o5kl((void *)&t381, t378, &t380);
                                    void * t382 = gm_l0;
                                    uint64_t t383 = gm_l41;
                                    gm_tkaapcii86udk t384;
                                    gm_f_offset_nkn711s((void *)&t384, (void *)NULL, cx, NULL, &(*(gm_tkaapcii86udk *)(((uint8_t *)&gm_l37) + 0)), t383);
                                    if (gm_unw.kind) {
                                        goto gm_ll368;
                                    }
                                    gm_t6evimt84lndq t385;
                                    gm_f_load_nf7o5kl((void *)&t385, t382, &t384);
                                    gm_t88rcucg1b7uk t386 = gm_l38;
                                    gm_tl874t2atnlh3 t387 = gm_l4;
                                    gm_tsg40glpvq01o t388;
                                    gm_f_equal_hinsj77((void *)&t388, (void *)NULL, cx, NULL, t377, &t381, &t385, t386, t387);
                                    if (gm_unw.kind) {
                                        goto gm_ll368;
                                    }
                                    gm_tsg40glpvq01o t389 = t388;
                                    if (((((uint8_t *)&t389))[0] != 0)) {
                                        gm_t39uai1ajk3qv t390 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t389) + 16));
                                        gm_tsg40glpvq01o t391;
                                        GM_ST(uint8_t, (uint8_t *)&t391 + 1, 0);
                                        GM_ST(uint16_t, (uint8_t *)&t391 + 2, 0);
                                        GM_ST(uint32_t, (uint8_t *)&t391 + 4, 0);
                                        GM_ST(uint64_t, (uint8_t *)&t391 + 8, 0);
                                        ((uint8_t *)&t391)[0] = 1;
                                        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t391 + 16), t390);
                                        F.ret = t391;
                                        goto gm_fret;
                                    }
                                    uint8_t t392 = GM_LD(uint8_t, (((uint8_t *)&t389) + 16));
                                    uint8_t t393 = (uint8_t)!t392;
                                    if (!t393) goto ie376;
                                    {
                                        gm_tsg40glpvq01o t395;
                                        memset(&t395, 0, sizeof t395);
                                        ((uint8_t *)&t395)[0] = 0;
                                        GM_ST(uint8_t, ((uint8_t *)&t395 + 16), ((uint8_t)0));
                                        gm_tsg40glpvq01o t396 = t395;
                                        uint8_t t397 = ((((uint8_t *)&t396))[0] != 0);
                                        F.ret = t395;
                                        goto gm_fret;
                                    }
                                    goto ix376;
ie376:;
ix376:;
                                }
                                goto me372;
mn373:;
                                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 711, 13);
me372:;
                            }
lc364:;
                            {
                                uint64_t t399 = gm_l40;
                                uint64_t r400; bool o401 = __builtin_add_overflow(t399, ((uint64_t)1ULL), &r400);
                                uint64_t t402 = r400;
                                gm_l40 = t402;
                            }
                        }
gm_ll368:;
                        if (gm_unw.frame == &F.h && gm_unw.label == 1) {
                            if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb364; }
                            if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc364; }
                        }
                        goto gm_lad;
lb364:;
                    }
                }
                {
                    gm_tsg40glpvq01o t403;
                    memset(&t403, 0, sizeof t403);
                    ((uint8_t *)&t403)[0] = 0;
                    GM_ST(uint8_t, ((uint8_t *)&t403 + 16), ((uint8_t)1));
                    gm_tsg40glpvq01o t404 = t403;
                    uint8_t t405 = ((((uint8_t *)&t404))[0] != 0);
                    F.ret = t403;
                    goto gm_fret;
                }
                goto ix346;
ie346:;
                {
                    void * t407 = gm_l0;
                    gm_tl874t2atnlh3 t408 = gm_l4;
                    gm_b16_8 t409;
                    GM_ST(void *, (uint8_t *)&t409, (void *)(gm_s9ofbo8c28rrv));
                    GM_ST(uintptr_t, (uint8_t *)&t409 + 8, (uintptr_t)(15));
                    gm_t39uai1ajk3qv t410;
                    gm_f_unsupported_2seq49l((void *)&t410, t407, GM_CONV(gm_cseahvg8v7p0vs, t408), t409);
                    gm_tsg40glpvq01o t411;
                    GM_ST(uint8_t, (uint8_t *)&t411 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t411 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t411 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t411 + 8, 0);
                    ((uint8_t *)&t411)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t411 + 16), t410);
                    gm_tsg40glpvq01o t412 = t411;
                    uint8_t t413 = ((((uint8_t *)&t412))[0] != 0);
                    F.ret = t411;
                    goto gm_fret;
                }
ix346:;
            }
            goto ix342;
ie342:;
            {
                gm_tsg40glpvq01o t415;
                memset(&t415, 0, sizeof t415);
                ((uint8_t *)&t415)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t415 + 16), ((uint8_t)0));
                gm_tsg40glpvq01o t416 = t415;
                uint8_t t417 = ((((uint8_t *)&t416))[0] != 0);
                F.ret = t415;
                goto gm_fret;
            }
ix342:;
        }
        goto me5;
mn338:;
ma17:;
        uint8_t t419 = GM_LD(uint8_t, (((uint8_t *)&F.l1) + 0));
        if (t419 != ((uint8_t)4ULL)) goto mn418;
        gm_tkaapcii86udk t420 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l1) + 16));
        gm_l42 = t420;
        {
            uint8_t t423 = GM_LD(uint8_t, (((uint8_t *)&F.l2) + 0));
            if (t423 != ((uint8_t)4ULL)) goto ie422;
            gm_tkaapcii86udk t424 = GM_LD(gm_tkaapcii86udk, (((uint8_t *)&F.l2) + 16));
            gm_l43 = t424;
            {
                uint8_t t426 = gm_f_same_place_1r7qrqk(&(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l42)), &(*(gm_tkaapcii86udk *)((uint8_t *)&gm_l43)));
                gm_tsg40glpvq01o t427;
                memset(&t427, 0, sizeof t427);
                ((uint8_t *)&t427)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t427 + 16), t426);
                gm_tsg40glpvq01o t428 = t427;
                uint8_t t429 = ((((uint8_t *)&t428))[0] != 0);
                F.ret = t427;
                goto gm_fret;
            }
            goto ix422;
ie422:;
            {
                gm_tsg40glpvq01o t431;
                memset(&t431, 0, sizeof t431);
                ((uint8_t *)&t431)[0] = 0;
                GM_ST(uint8_t, ((uint8_t *)&t431 + 16), ((uint8_t)0));
                gm_tsg40glpvq01o t432 = t431;
                uint8_t t433 = ((((uint8_t *)&t432))[0] != 0);
                F.ret = t431;
                goto gm_fret;
            }
ix422:;
        }
        goto me5;
mn418:;
ma18:;
        void * t435 = gm_l0;
        gm_tl874t2atnlh3 t436 = gm_l4;
        gm_b16_8 t437;
        GM_ST(void *, (uint8_t *)&t437, (void *)(gm_s8afn1h3asffl));
        GM_ST(uintptr_t, (uint8_t *)&t437 + 8, (uintptr_t)(31));
        gm_t39uai1ajk3qv t438;
        gm_f_unsupported_2seq49l((void *)&t438, t435, GM_CONV(gm_cseahvg8v7p0vs, t436), t437);
        gm_tsg40glpvq01o t439;
        GM_ST(uint8_t, (uint8_t *)&t439 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t439 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t439 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t439 + 8, 0);
        ((uint8_t *)&t439)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t439 + 16), t438);
        gm_tsg40glpvq01o t440 = t439;
        uint8_t t441 = ((((uint8_t *)&t440))[0] != 0);
        F.ret = t439;
        goto gm_fret;
        goto me5;
mn434:;
ma19:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 657, 5);
me5:;
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
__attribute__((aligned(4))) void gm_f_field_tys_foan32f(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, uint64_t a2) {
    struct gm_fr_field_tys_foan32f F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint64_t gm_l2; /* n */
    void * gm_l3; /* tys */
    gm_b16_8 gm_l4; /* elems */
    gm_t88rcucg1b7uk gm_l5; /* elem */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint32_t gm_l9; /* id */
    gm_b16_8 gm_l12; /* <tmp> */
    uint64_t gm_l13; /* <tmp> */
    GM_STACK_CHECK(&F, 800u);
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
        void * t5 = gm_l3;
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma10; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma13; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma13; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma13; default: goto ma13; }
ma10:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t17 != ((uint8_t)12ULL)) goto mn16;
        gm_b16_8 t18 = GM_LD(gm_b16_8, ((((uint8_t *)&t8) + 8) + 0));
        gm_l4 = t18;
        gm_b16_8 t19 = gm_l4;
        gm_th2lqbo77pvsc t20;
        GM_ST(uint8_t, (uint8_t *)&t20 + 17, 0);
        GM_ST(uint16_t, (uint8_t *)&t20 + 18, 0);
        GM_ST(uint32_t, (uint8_t *)&t20 + 20, 0);
        GM_ST(gm_b16_8, ((uint8_t *)&t20), t19);
        ((uint8_t *)&t20)[16] = 1;
        uint8_t t21 = 0;
        F.ret = t20;
        goto gm_fret;
        goto me9;
mn16:;
ma11:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t23 != ((uint8_t)11ULL)) goto mn22;
        gm_t88rcucg1b7uk t24 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 8));
        gm_l5 = t24;
        {
            gm_f_list_nof7vfj((void *)&F.l6, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                uint64_t t27 = gm_l2;
                gm_l7 = t27;
            }
            {
                for (;;) {
                    uint64_t t29 = gm_l8;
                    uint64_t t30 = gm_l7;
                    uint8_t t31 = (t29 < t30);
                    if (!t31) goto lb28;
                    {
                        uint64_t t34 = gm_l8;
                        uint64_t t35 = t34;
                        {
                            void * t39 = (void *)((uint8_t *)&F.l6);
                            gm_t88rcucg1b7uk t40 = gm_l5;
                            gm_tftb1odc739t5 t41 = gm_f_push_k3l9nic(t39, GM_CONV(gm_csguankko4lmhd, t40));
                            gm_tftb1odc739t5 t42 = t41;
                            if (((((uint8_t *)&t42))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t42) + 1, gm_srcue4ionbe9lgh, 739, 29);
                            gm_b0_1 t43 = GM_LD(gm_b0_1, (((uint8_t *)&t42) + 1));
                        }
                        goto me36;
mn37:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 739, 13);
me36:;
                    }
lc28:;
                    {
                        uint64_t t45 = gm_l8;
                        uint64_t r46; bool o47 = __builtin_add_overflow(t45, ((uint64_t)1ULL), &r46);
                        uint64_t t48 = r46;
                        gm_l8 = t48;
                    }
                }
lb28:;
            }
        }
        {
            gm_b16_8 t49 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t50 = t49;
            gm_b16_8 t51 = GM_LD(gm_b16_8, ((uint8_t *)&t50));
            gm_th2lqbo77pvsc t52;
            GM_ST(uint8_t, (uint8_t *)&t52 + 17, 0);
            GM_ST(uint16_t, (uint8_t *)&t52 + 18, 0);
            GM_ST(uint32_t, (uint8_t *)&t52 + 20, 0);
            GM_ST(gm_b16_8, ((uint8_t *)&t52), t51);
            ((uint8_t *)&t52)[16] = 1;
            uint8_t t53 = 0;
            F.ret = t52;
            goto gm_fret;
        }
        goto me9;
mn22:;
ma12:;
        uint8_t t55 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t55 != ((uint8_t)17ULL)) goto mn54;
        uint32_t t56 = GM_LD(uint32_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l9 = t56;
        {
            uint32_t t59 = gm_l9;
            uint64_t t60 = (uint64_t)t59;
            uint64_t t61 = (uintptr_t)t60;
            void * t62 = gm_l0;
            void * t63 = GM_LD(void *, (((uint8_t *)t62) + 0));
            uint64_t t64 = GM_LD(uintptr_t, ((((uint8_t *)t63) + 1088) + 0) + 8);
            if (t61 >= (uintptr_t)(t64)) gm_trap_index(t61, t64, gm_srcue4ionbe9lgh, 743, 29);
            uint8_t t65 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t63) + 1088) + 0)) + t61 * 184) + 24) + 0));
            if (t65 != ((uint8_t)0ULL)) goto ie58;
            gm_tpautr4br7h3l t66 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t63) + 1088) + 0)) + t61 * 184) + 24) + 8));
            F.l10 = t66;
            {
                gm_f_list_nof7vfj((void *)&F.l11, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t69 = GM_LD(gm_b16_8, (((uint8_t *)&F.l10) + 0));
                    gm_l12 = t69;
                }
                {
                    gm_l13 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t71 = gm_l13;
                        uint64_t t72 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                        uint8_t t73 = (t71 < t72);
                        if (!t73) goto lb70;
                        {
                            uint64_t t76 = gm_l13;
                            uint64_t t77 = (uintptr_t)t76;
                            uint64_t t78 = GM_LD(uintptr_t, ((uint8_t *)&gm_l12) + 8);
                            gm_t0mglksrkfa5e t81 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l12)) + t77 * 40));
                            F.l14 = t81;
                            {
                                void * t83 = (void *)((uint8_t *)&F.l11);
                                gm_t88rcucg1b7uk t84 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l14) + 16));
                                gm_tftb1odc739t5 t85 = gm_f_push_k3l9nic(t83, GM_CONV(gm_csguankko4lmhd, t84));
                                gm_tftb1odc739t5 t86 = t85;
                                if (((((uint8_t *)&t86))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t86) + 1, gm_srcue4ionbe9lgh, 745, 35);
                                gm_b0_1 t87 = GM_LD(gm_b0_1, (((uint8_t *)&t86) + 1));
                            }
                            goto me79;
mn80:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 745, 13);
me79:;
                        }
lc70:;
                        {
                            uint64_t t89 = gm_l13;
                            uint64_t r90; bool o91 = __builtin_add_overflow(t89, ((uint64_t)1ULL), &r90);
                            uint64_t t92 = r90;
                            gm_l13 = t92;
                        }
                    }
lb70:;
                }
            }
            {
                gm_b16_8 t93 = GM_LD(gm_b16_8, (((uint8_t *)&F.l11) + 0));
                gm_b16_8 t94 = t93;
                gm_b16_8 t95 = GM_LD(gm_b16_8, ((uint8_t *)&t94));
                gm_th2lqbo77pvsc t96;
                GM_ST(uint8_t, (uint8_t *)&t96 + 17, 0);
                GM_ST(uint16_t, (uint8_t *)&t96 + 18, 0);
                GM_ST(uint32_t, (uint8_t *)&t96 + 20, 0);
                GM_ST(gm_b16_8, ((uint8_t *)&t96), t95);
                ((uint8_t *)&t96)[16] = 1;
                uint8_t t97 = 0;
                F.ret = t96;
                goto gm_fret;
            }
            goto ix58;
ie58:;
            {
                gm_th2lqbo77pvsc t99;
                memset(&t99, 0, sizeof t99);
                uint8_t t100 = 0;
                F.ret = t99;
                goto gm_fret;
            }
ix58:;
        }
        goto me9;
mn54:;
ma13:;
        gm_th2lqbo77pvsc t102;
        memset(&t102, 0, sizeof t102);
        uint8_t t103 = 0;
        F.ret = t102;
        goto gm_fret;
        goto me9;
mn101:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 735, 5);
me9:;
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
inline __attribute__((aligned(4))) void gm_f_fit_8hvi4bv(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_fit_8hvi4bv F;
    void * gm_l0; /* m */
    gm_t9qkh0rsooc8d gm_l2; /* flavor */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* sym */
    gm_tl874t2atnlh3 gm_l5; /* span */
    GM_STACK_CHECK(&F, 1583u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l3;
        gm_f_int_range_hue3vms((void *)&F.l6, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        int32_t t5 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l6) + 0)));
        uint8_t t6 = (t5 >= ((int32_t)0LL));
        if (!t6) goto ie4;
        int32_t t7 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l6) + 48)));
        uint8_t t8 = (t7 <= ((int32_t)0LL));
        if (!t8) goto ie4;
        {
            void * t10 = gm_l0;
            unsigned __int128 t11 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
            gm_t88rcucg1b7uk t12 = gm_l3;
            unsigned __int128 t13 = gm_f_wrap_bt8r149(t10, t11, GM_CONV(gm_csguankko4lmhd, t12));
            gm_t8carr7mku484 t14;
            memset(&t14, 0, sizeof t14);
            ((uint8_t *)&t14)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t14 + 16), t13);
            gm_t8carr7mku484 t15 = t14;
            uint8_t t16 = ((((uint8_t *)&t15))[0] != 0);
            F.ret = t14;
            goto gm_fret;
        }
        goto ix4;
ie4:;
ix4:;
    }
    {
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t22) { case ((uint8_t)0ULL): goto ma20; case ((uint8_t)1ULL): goto ma18; case ((uint8_t)2ULL): goto ma19; case ((uint8_t)3ULL): goto ma20; default: goto ma20; }
ma18:;
        uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t24 != ((uint8_t)1ULL)) goto mn23;
        void * t25 = gm_l0;
        unsigned __int128 t26 = gm_f_bits128_jigkui4(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        gm_t88rcucg1b7uk t27 = gm_l3;
        unsigned __int128 t28 = gm_f_wrap_bt8r149(t25, t26, GM_CONV(gm_csguankko4lmhd, t27));
        gm_t8carr7mku484 t29;
        memset(&t29, 0, sizeof t29);
        ((uint8_t *)&t29)[0] = 0;
        GM_ST(unsigned __int128, ((uint8_t *)&t29 + 16), t28);
        gm_t8carr7mku484 t30 = t29;
        uint8_t t31 = ((((uint8_t *)&t30))[0] != 0);
        F.ret = t29;
        goto gm_fret;
        goto me17;
mn23:;
ma19:;
        uint8_t t33 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t33 != ((uint8_t)2ULL)) goto mn32;
        void * t34 = gm_l0;
        gm_tbci5ospk5nnn t35;
        uint8_t t37 = gm_f_is_negative_k87pp3n(&(*(gm_tbci5ospk5nnn *)((uint8_t *)a1)));
        if (!t37) goto ie36;
        gm_tbci5ospk5nnn t39 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l6) + 0));
        gm_tbci5ospk5nnn t40 = t39;
        t35 = t40;
        goto ix36;
ie36:;
        gm_tbci5ospk5nnn t42 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&F.l6) + 48));
        gm_tbci5ospk5nnn t43 = t42;
        t35 = t43;
ix36:;
        unsigned __int128 t44 = gm_f_bits128_jigkui4(&t35);
        gm_t88rcucg1b7uk t45 = gm_l3;
        unsigned __int128 t46 = gm_f_wrap_bt8r149(t34, t44, GM_CONV(gm_csguankko4lmhd, t45));
        gm_t8carr7mku484 t47;
        memset(&t47, 0, sizeof t47);
        ((uint8_t *)&t47)[0] = 0;
        GM_ST(unsigned __int128, ((uint8_t *)&t47 + 16), t46);
        gm_t8carr7mku484 t48 = t47;
        uint8_t t49 = ((((uint8_t *)&t48))[0] != 0);
        F.ret = t47;
        goto gm_fret;
        goto me17;
mn32:;
ma20:;
        void * t51 = gm_l0;
        gm_tl874t2atnlh3 t52 = gm_l5;
        gm_b16_8 t53 = gm_l4;
        gm_tnpifv20irnph t54;
        GM_ST(gm_b16_8, ((uint8_t *)&t54 + 0), t53);
        gm_tabb710d9mq2e t55;
        gm_f_format_eruojoe((void *)&t55, (void *)NULL, cx, NULL, t54);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t56 = t55;
        if (((((uint8_t *)&t56))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t56) + 8, gm_srcue4ionbe9lgh, 53, 35);
        gm_b16_8 t57 = GM_LD(gm_b16_8, (((uint8_t *)&t56) + 8));
        gm_b16_8 t58 = t57;
        gm_b16_8 t59 = GM_LD(gm_b16_8, ((uint8_t *)&t58));
        gm_t39uai1ajk3qv t60;
        gm_f_trap_jftbss6((void *)&t60, (void *)NULL, cx, NULL, t51, t52, t59);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t8carr7mku484 t61;
        GM_ST(uint8_t, (uint8_t *)&t61 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t61 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t61 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t61 + 8, 0);
        ((uint8_t *)&t61)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t61 + 16), t60);
        gm_t8carr7mku484 t62 = t61;
        uint8_t t63 = ((((uint8_t *)&t62))[0] != 0);
        F.ret = t61;
        goto gm_fret;
        goto me17;
mn50:;
ma21:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 50, 5);
me17:;
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
__attribute__((aligned(4))) void gm_f_fit_small_7o94i8v(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, __int128 a1, gm_t9qkh0rsooc8d a2, gm_t88rcucg1b7uk a3, gm_b16_8 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_fit_small_7o94i8v F;
    void * gm_l0; /* m */
    __int128 gm_l1; /* exact */
    gm_t9qkh0rsooc8d gm_l2; /* flavor */
    gm_t88rcucg1b7uk gm_l3; /* t */
    gm_b16_8 gm_l4; /* sym */
    gm_tl874t2atnlh3 gm_l5; /* span */
    gm_ttovhfee8mf28 gm_l6; /* info */
    __int128 gm_l7; /* lo */
    __int128 gm_l8; /* hi */
    GM_STACK_CHECK(&F, 1853u);
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
        gm_t88rcucg1b7uk t3 = gm_l3;
        gm_ttovhfee8mf28 t4 = gm_f_int_info_2r9qb5n(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_l6 = t4;
    }
    {
        __int128 t5;
        uint8_t t7 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
        if (!t7) goto ie6;
        uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        uint32_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint32_t)1ULL), &r10);
        if (o11) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 60, 47);
        uint32_t t12 = r10;
        unsigned __int128 n13 = (unsigned __int128)t12;
        if (n13 >= 128) gm_trap_shift((uint64_t)n13, "i128", gm_srcue4ionbe9lgh, 60, 35);
        __int128 r14 = (__int128)((unsigned __int128)((__int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n13);
        bool o15 = (__int128)(r14 >> (int)n13) != ((__int128)(((unsigned __int128)0u << 64) | 1u));
        if (o15) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 60, 35);
        __int128 t16 = r14;
        if (t16 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 60, 33);
        __int128 t17 = (__int128)((unsigned __int128)0 - (unsigned __int128)t16);
        __int128 t18 = t17;
        t5 = t18;
        goto ix6;
ie6:;
        __int128 t20 = ((__int128)(((unsigned __int128)0u << 64) | 0u));
        t5 = t20;
ix6:;
        gm_l7 = t5;
    }
    {
        __int128 t21;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l6) + 0));
        if (!t23) goto ie22;
        uint32_t t25 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        uint32_t r26; bool o27 = __builtin_sub_overflow(t25, ((uint32_t)1ULL), &r26);
        if (o27) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 61, 46);
        uint32_t t28 = r26;
        unsigned __int128 n29 = (unsigned __int128)t28;
        if (n29 >= 128) gm_trap_shift((uint64_t)n29, "i128", gm_srcue4ionbe9lgh, 61, 34);
        __int128 r30 = (__int128)((unsigned __int128)((__int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n29);
        bool o31 = (__int128)(r30 >> (int)n29) != ((__int128)(((unsigned __int128)0u << 64) | 1u));
        if (o31) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 61, 34);
        __int128 t32 = r30;
        __int128 r33; bool o34 = __builtin_sub_overflow(t32, ((__int128)(((unsigned __int128)0u << 64) | 1u)), &r33);
        if (o34) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 61, 33);
        __int128 t35 = r33;
        __int128 t36 = t35;
        t21 = t36;
        goto ix22;
ie22:;
        uint32_t t38 = GM_LD(uint32_t, (((uint8_t *)&gm_l6) + 4));
        unsigned __int128 n39 = (unsigned __int128)t38;
        if (n39 >= 128) gm_trap_shift((uint64_t)n39, "i128", gm_srcue4ionbe9lgh, 61, 76);
        __int128 r40 = (__int128)((unsigned __int128)((__int128)(((unsigned __int128)0u << 64) | 1u)) << (int)n39);
        bool o41 = (__int128)(r40 >> (int)n39) != ((__int128)(((unsigned __int128)0u << 64) | 1u));
        if (o41) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 61, 76);
        __int128 t42 = r40;
        __int128 r43; bool o44 = __builtin_sub_overflow(t42, ((__int128)(((unsigned __int128)0u << 64) | 1u)), &r43);
        if (o44) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 61, 75);
        __int128 t45 = r43;
        __int128 t46 = t45;
        t21 = t46;
ix22:;
        gm_l8 = t21;
    }
    {
        __int128 t48 = gm_l1;
        __int128 t49 = gm_l7;
        uint8_t t50 = (t48 >= t49);
        if (!t50) goto ie47;
        __int128 t51 = gm_l1;
        __int128 t52 = gm_l8;
        uint8_t t53 = (t51 <= t52);
        if (!t53) goto ie47;
        {
            void * t55 = gm_l0;
            __int128 t56 = gm_l1;
            __int128 t57 = t56;
            unsigned __int128 t58;
            memset(&t58, 0, sizeof t58); memcpy(&t58, ((uint8_t *)&t57), sizeof t58 < 16 ? sizeof t58 : 16);
            gm_t88rcucg1b7uk t59 = gm_l3;
            unsigned __int128 t60 = gm_f_wrap_bt8r149(t55, t58, GM_CONV(gm_csguankko4lmhd, t59));
            gm_t8carr7mku484 t61;
            memset(&t61, 0, sizeof t61);
            ((uint8_t *)&t61)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t61 + 16), t60);
            gm_t8carr7mku484 t62 = t61;
            uint8_t t63 = ((((uint8_t *)&t62))[0] != 0);
            F.ret = t61;
            goto gm_fret;
        }
        goto ix47;
ie47:;
ix47:;
    }
    {
        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        switch (t69) { case ((uint8_t)0ULL): goto ma67; case ((uint8_t)1ULL): goto ma65; case ((uint8_t)2ULL): goto ma66; case ((uint8_t)3ULL): goto ma67; default: goto ma67; }
ma65:;
        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t71 != ((uint8_t)1ULL)) goto mn70;
        void * t72 = gm_l0;
        __int128 t73 = gm_l1;
        __int128 t74 = t73;
        unsigned __int128 t75;
        memset(&t75, 0, sizeof t75); memcpy(&t75, ((uint8_t *)&t74), sizeof t75 < 16 ? sizeof t75 : 16);
        gm_t88rcucg1b7uk t76 = gm_l3;
        unsigned __int128 t77 = gm_f_wrap_bt8r149(t72, t75, GM_CONV(gm_csguankko4lmhd, t76));
        gm_t8carr7mku484 t78;
        memset(&t78, 0, sizeof t78);
        ((uint8_t *)&t78)[0] = 0;
        GM_ST(unsigned __int128, ((uint8_t *)&t78 + 16), t77);
        gm_t8carr7mku484 t79 = t78;
        uint8_t t80 = ((((uint8_t *)&t79))[0] != 0);
        F.ret = t78;
        goto gm_fret;
        goto me64;
mn70:;
ma66:;
        uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (t82 != ((uint8_t)2ULL)) goto mn81;
        void * t83 = gm_l0;
        __int128 t84;
        __int128 t86 = gm_l1;
        uint8_t t87 = (t86 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
        if (!t87) goto ie85;
        __int128 t89 = gm_l7;
        __int128 t90 = t89;
        t84 = t90;
        goto ix85;
ie85:;
        __int128 t92 = gm_l8;
        __int128 t93 = t92;
        t84 = t93;
ix85:;
        __int128 t94 = t84;
        unsigned __int128 t95;
        memset(&t95, 0, sizeof t95); memcpy(&t95, ((uint8_t *)&t94), sizeof t95 < 16 ? sizeof t95 : 16);
        gm_t88rcucg1b7uk t96 = gm_l3;
        unsigned __int128 t97 = gm_f_wrap_bt8r149(t83, t95, GM_CONV(gm_csguankko4lmhd, t96));
        gm_t8carr7mku484 t98;
        memset(&t98, 0, sizeof t98);
        ((uint8_t *)&t98)[0] = 0;
        GM_ST(unsigned __int128, ((uint8_t *)&t98 + 16), t97);
        gm_t8carr7mku484 t99 = t98;
        uint8_t t100 = ((((uint8_t *)&t99))[0] != 0);
        F.ret = t98;
        goto gm_fret;
        goto me64;
mn81:;
ma67:;
        void * t102 = gm_l0;
        gm_tl874t2atnlh3 t103 = gm_l5;
        gm_b16_8 t104 = gm_l4;
        gm_tnpifv20irnph t105;
        GM_ST(gm_b16_8, ((uint8_t *)&t105 + 0), t104);
        gm_tabb710d9mq2e t106;
        gm_f_format_eruojoe((void *)&t106, (void *)NULL, cx, NULL, t105);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tabb710d9mq2e t107 = t106;
        if (((((uint8_t *)&t107))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t107) + 8, gm_srcue4ionbe9lgh, 66, 35);
        gm_b16_8 t108 = GM_LD(gm_b16_8, (((uint8_t *)&t107) + 8));
        gm_b16_8 t109 = t108;
        gm_b16_8 t110 = GM_LD(gm_b16_8, ((uint8_t *)&t109));
        gm_t39uai1ajk3qv t111;
        gm_f_trap_jftbss6((void *)&t111, (void *)NULL, cx, NULL, t102, t103, t110);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_t8carr7mku484 t112;
        GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t112 + 8, 0);
        ((uint8_t *)&t112)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t112 + 16), t111);
        gm_t8carr7mku484 t113 = t112;
        uint8_t t114 = ((((uint8_t *)&t113))[0] != 0);
        F.ret = t112;
        goto gm_fret;
        goto me64;
mn101:;
ma68:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 63, 5);
me64:;
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
__attribute__((aligned(4))) void gm_f_float_exact_1g17c6c(void *gm_out, void *env, gm_node *cx, gm_node *inst, double a0) {
    struct gm_fr_float_exact_1g17c6c F;
    double gm_l0; /* x */
    uint64_t gm_l1; /* raw */
    uint8_t gm_l2; /* negative */
    int64_t gm_l3; /* exp */
    uint64_t gm_l4; /* mant */
    int64_t gm_l5; /* e */
    GM_STACK_CHECK(&F, 1033u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    {
        double t3 = gm_l0;
        double t4 = gm_l0;
        double t5 = (t3 - t4);
        uint8_t t6 = (!(t5 == ((double)0e0)));
        if (!t6) goto ie2;
        {
            gm_tvegkb6ouf3m0 t8;
            memset(&t8, 0, sizeof t8);
            uint8_t t9 = 0;
            F.ret = t8;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        double t10 = gm_l0;
        double t11 = t10;
        uint64_t t12;
        memset(&t12, 0, sizeof t12); memcpy(&t12, ((uint8_t *)&t11), sizeof t12 < 8 ? sizeof t12 : 8);
        gm_l1 = t12;
    }
    {
        uint64_t t13 = gm_l1;
        unsigned __int128 n14 = (unsigned __int128)((uint64_t)63ULL);
        if (n14 >= 64) gm_trap_shift((uint64_t)n14, "u64", gm_srcue4ionbe9lgh, 307, 17);
        uint64_t t15 = (uint64_t)(t13 >> (int)n14);
        uint8_t t16 = ((t15 == ((uint64_t)1ULL)));
        gm_l2 = t16;
    }
    {
        uint64_t t17 = gm_l1;
        unsigned __int128 n18 = (unsigned __int128)((uint64_t)52ULL);
        if (n18 >= 64) gm_trap_shift((uint64_t)n18, "u64", gm_srcue4ionbe9lgh, 308, 14);
        uint64_t t19 = (uint64_t)(t17 >> (int)n18);
        uint64_t t20 = (uint64_t)(t19 & ((uint64_t)2047ULL));
        uint64_t t21 = t20;
        int64_t t22;
        memset(&t22, 0, sizeof t22); memcpy(&t22, ((uint8_t *)&t21), sizeof t22 < 8 ? sizeof t22 : 8);
        gm_l3 = t22;
    }
    {
        uint64_t t23 = gm_l1;
        uint64_t t24 = (uint64_t)(t23 & ((uint64_t)4503599627370495ULL));
        gm_l4 = t24;
    }
    {
        int64_t t26 = gm_l3;
        uint8_t t27 = ((t26 == ((int64_t)0LL)));
        if (!t27) goto ie25;
        {
            gm_l3 = ((int64_t)1LL);
        }
        goto ix25;
ie25:;
        {
            uint64_t t30 = gm_l4;
            unsigned __int128 n31 = (unsigned __int128)((uint64_t)52ULL);
            if (n31 >= 64) gm_trap_shift((uint64_t)n31, "u64", gm_srcue4ionbe9lgh, 313, 17);
            uint64_t r32 = (uint64_t)((uint64_t)((uint64_t)1ULL) << (int)n31);
            bool o33 = (uint64_t)(r32 >> (int)n31) != ((uint64_t)1ULL);
            if (o33) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 313, 17);
            uint64_t t34 = r32;
            uint64_t t35 = (uint64_t)(t30 | t34);
            gm_l4 = t35;
        }
ix25:;
    }
    {
        int64_t t36 = gm_l3;
        int64_t r37; bool o38 = __builtin_sub_overflow(t36, ((int64_t)1075LL), &r37);
        if (o38) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 315, 10);
        int64_t t39 = r37;
        gm_l5 = t39;
    }
    {
        uint64_t t40 = gm_l4;
        gm_f_from_u64_ud13q5v((void *)&F.l6, (void *)NULL, cx, NULL, t40);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t42 = gm_l2;
        if (!t42) goto ie41;
        {
            gm_f_neg_agd5c5k((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)), (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l6)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix41;
ie41:;
ix41:;
    }
    {
        int64_t t45 = gm_l5;
        uint8_t t46 = (t45 >= ((int64_t)0LL));
        if (!t46) goto ie44;
        {
            gm_tbci5ospk5nnn t48 = F.l6;
            int64_t t49 = gm_l5;
            int64_t t50 = t49;
            uint64_t t51;
            memset(&t51, 0, sizeof t51); memcpy(&t51, ((uint8_t *)&t50), sizeof t51 < 8 ? sizeof t51 : 8);
            gm_tbci5ospk5nnn t52;
            gm_f_shl_qqecl81((void *)&t52, (void *)NULL, cx, NULL, &t48, t51);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t53;
            gm_f_from_int_ofjuoqh((void *)&t53, (void *)NULL, cx, NULL, &t52);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tvegkb6ouf3m0 t54;
            GM_ST(uint8_t, (uint8_t *)&t54 + 97, 0);
            GM_ST(uint16_t, (uint8_t *)&t54 + 98, 0);
            GM_ST(uint32_t, (uint8_t *)&t54 + 100, 0);
            GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t54), t53);
            ((uint8_t *)&t54)[96] = 1;
            uint8_t t55 = 0;
            F.ret = t54;
            goto gm_fret;
        }
        goto ix44;
ie44:;
ix44:;
    }
    {
        gm_tbci5ospk5nnn t56 = F.l6;
        gm_tbci5ospk5nnn t57;
        gm_f_one_08qg1op((void *)&t57, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        int64_t t58 = gm_l5;
        if (t58 == ((int64_t)(-9223372036854775807LL - 1))) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 319, 45);
        int64_t t59 = (int64_t)((uint64_t)0 - (uint64_t)t58);
        int64_t t60 = t59;
        uint64_t t61;
        memset(&t61, 0, sizeof t61); memcpy(&t61, ((uint8_t *)&t60), sizeof t61 < 8 ? sizeof t61 : 8);
        gm_tbci5ospk5nnn t62;
        gm_f_shl_qqecl81((void *)&t62, (void *)NULL, cx, NULL, &t57, t61);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t63;
        gm_f_new_2jv6hag((void *)&t63, (void *)NULL, cx, NULL, &t56, &t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvegkb6ouf3m0 t64;
        GM_ST(uint8_t, (uint8_t *)&t64 + 97, 0);
        GM_ST(uint16_t, (uint8_t *)&t64 + 98, 0);
        GM_ST(uint32_t, (uint8_t *)&t64 + 100, 0);
        GM_ST(gm_tngtuiuskn938, ((uint8_t *)&t64), t63);
        ((uint8_t *)&t64)[96] = 1;
        uint8_t t65 = 0;
        F.ret = t64;
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
inline __attribute__((aligned(4))) double gm_f_float_rem_24loign(void *env, gm_node *cx, gm_node *inst, double a0, double a1) {
    struct gm_fr_float_rem_24loign F;
    double gm_l0; /* x */
    double gm_l1; /* y */
    GM_STACK_CHECK(&F, 1398u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        uint8_t t3 = 0;
        uint8_t t4 = 0;
        double t5 = gm_l0;
        double t6 = gm_l0;
        double t7 = (t5 - t6);
        uint8_t t8 = (!(t7 == ((double)0e0)));
        if (!t8) {
            double t9 = gm_l1;
            uint8_t t10 = ((t9 == ((double)0e0)));
            t4 = t10;
        } else t4 = 1;
        if (!t4) {
            double t11 = gm_l1;
            double t12 = gm_l1;
            uint8_t t13 = (!(t11 == t12));
            t3 = t13;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            double t15 = (((double)0e0) / ((double)0e0));
            uint8_t t16 = 0;
            F.ret = t15;
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        double t18 = gm_l1;
        double t19 = gm_l1;
        double t20 = (t18 - t19);
        uint8_t t21 = (!(t20 == ((double)0e0)));
        if (!t21) goto ie17;
        {
            double t23 = gm_l0;
            uint8_t t24 = 0;
            F.ret = t23;
            goto gm_fret;
        }
        goto ix17;
ie17:;
ix17:;
    }
    {
        double t25 = gm_l0;
        gm_tvegkb6ouf3m0 t26;
        gm_f_float_exact_1g17c6c((void *)&t26, (void *)NULL, cx, NULL, t25);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvegkb6ouf3m0 t27 = t26;
        gm_tngtuiuskn938 t28;
        if (((((uint8_t *)&t27))[96] != 0)) {
            gm_tngtuiuskn938 t29 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&t27) + 0));
            t28 = t29;
        } else {
            double t30 = (((double)0e0) / ((double)0e0));
            uint8_t t31 = 0;
            F.ret = t30;
            goto gm_fret;
        }
        F.l2 = t28;
    }
    {
        double t32 = gm_l1;
        gm_tvegkb6ouf3m0 t33;
        gm_f_float_exact_1g17c6c((void *)&t33, (void *)NULL, cx, NULL, t32);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvegkb6ouf3m0 t34 = t33;
        gm_tngtuiuskn938 t35;
        if (((((uint8_t *)&t34))[96] != 0)) {
            gm_tngtuiuskn938 t36 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&t34) + 0));
            t35 = t36;
        } else {
            double t37 = (((double)0e0) / ((double)0e0));
            uint8_t t38 = 0;
            F.ret = t37;
            goto gm_fret;
        }
        F.l3 = t35;
    }
    {
        gm_f_div_d8sd4qc((void *)&F.l4, (void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l2)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l3)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_f_div_f0qrfbr((void *)&F.l5, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l4) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l4) + 48)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tngtuiuskn938 t39 = F.l2;
        gm_tngtuiuskn938 t40 = F.l3;
        gm_tngtuiuskn938 t41;
        gm_f_from_int_ofjuoqh((void *)&t41, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tngtuiuskn938 t42;
        gm_f_mul_od1e7nd((void *)&t42, (void *)NULL, cx, NULL, &t40, &t41);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_sub_mler6q8((void *)&F.l6, (void *)NULL, cx, NULL, &t39, &t42);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t44 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)));
        if (!t44) goto ie43;
        {
            double t46;
            double t48 = gm_l0;
            uint8_t t49 = (t48 < ((double)0e0));
            if (!t49) goto ie47;
            double t51 = -((double)0e0);
            double t52 = t51;
            t46 = t52;
            goto ix47;
ie47:;
            double t54 = ((double)0e0);
            t46 = t54;
ix47:;
            uint8_t t55 = 0;
            F.ret = t46;
            goto gm_fret;
        }
        goto ix43;
ie43:;
ix43:;
    }
    {
        double t56 = gm_f_to_f64_4avhrht((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t57 = 0;
        F.ret = t56;
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
__attribute__((aligned(4))) void gm_f_float_to_int_sf4dap7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, double a1, gm_t88rcucg1b7uk a2, gm_tvdg94bk7aa22 a3) {
    struct gm_fr_float_to_int_sf4dap7 F;
    void * gm_l0; /* m */
    double gm_l1; /* x */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_tvdg94bk7aa22 gm_l3; /* mode */
    uint8_t gm_l8; /* exact */
    int32_t gm_l10; /* c */
    GM_STACK_CHECK(&F, 1341u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = a2;
    gm_l3 = a3;
    {
        double t2 = gm_l1;
        gm_tvegkb6ouf3m0 t3;
        gm_f_float_exact_1g17c6c((void *)&t3, (void *)NULL, cx, NULL, t2);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tvegkb6ouf3m0 t4 = t3;
        gm_tngtuiuskn938 t5;
        if (((((uint8_t *)&t4))[96] != 0)) {
            gm_tngtuiuskn938 t6 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&t4) + 0));
            t5 = t6;
        } else {
            gm_thrdpodrs8pgt t7;
            memset(&t7, 0, sizeof t7);
            uint8_t t8 = 0;
            F.ret = t7;
            goto gm_fret;
        }
        F.l4 = t5;
    }
    {
        gm_f_div_floor_u3mfihk((void *)&F.l5, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l4) + 0)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l4) + 48)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tngtuiuskn938 t9 = F.l4;
        gm_tngtuiuskn938 t10;
        gm_f_from_int_ofjuoqh((void *)&t10, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_f_sub_mler6q8((void *)&F.l6, (void *)NULL, cx, NULL, &t9, &t10);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        gm_tbci5ospk5nnn t11 = F.l5;
        F.l7 = t11;
    }
    {
        uint8_t t12 = gm_f_is_zero_5aept6u(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)));
        gm_l8 = t12;
    }
    {
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        switch (t19) { case ((uint8_t)0ULL): goto ma17; case ((uint8_t)1ULL): goto ma14; case ((uint8_t)2ULL): goto ma16; case ((uint8_t)3ULL): goto ma17; case ((uint8_t)4ULL): goto ma15; case ((uint8_t)5ULL): goto ma17; case ((uint8_t)6ULL): goto ma17; case ((uint8_t)7ULL): goto ma17; case ((uint8_t)8ULL): goto ma17; case ((uint8_t)9ULL): goto ma17; case ((uint8_t)10ULL): goto ma17; case ((uint8_t)11ULL): goto ma17; case ((uint8_t)12ULL): goto ma17; case ((uint8_t)13ULL): goto ma17; case ((uint8_t)14ULL): goto ma17; case ((uint8_t)15ULL): goto ma17; case ((uint8_t)16ULL): goto ma17; case ((uint8_t)17ULL): goto ma17; case ((uint8_t)18ULL): goto ma17; case ((uint8_t)19ULL): goto ma17; case ((uint8_t)20ULL): goto ma17; case ((uint8_t)21ULL): goto ma17; case ((uint8_t)22ULL): goto ma17; case ((uint8_t)23ULL): goto ma17; case ((uint8_t)24ULL): goto ma17; case ((uint8_t)25ULL): goto ma17; case ((uint8_t)26ULL): goto ma17; case ((uint8_t)27ULL): goto ma17; case ((uint8_t)28ULL): goto ma17; case ((uint8_t)29ULL): goto ma17; case ((uint8_t)30ULL): goto ma17; case ((uint8_t)31ULL): goto ma17; case ((uint8_t)32ULL): goto ma17; case ((uint8_t)33ULL): goto ma17; case ((uint8_t)34ULL): goto ma17; case ((uint8_t)35ULL): goto ma17; case ((uint8_t)36ULL): goto ma17; case ((uint8_t)37ULL): goto ma17; case ((uint8_t)38ULL): goto ma17; case ((uint8_t)39ULL): goto ma17; case ((uint8_t)40ULL): goto ma17; case ((uint8_t)41ULL): goto ma17; case ((uint8_t)42ULL): goto ma17; case ((uint8_t)43ULL): goto ma17; case ((uint8_t)44ULL): goto ma17; case ((uint8_t)45ULL): goto ma17; case ((uint8_t)46ULL): goto ma17; case ((uint8_t)47ULL): goto ma17; case ((uint8_t)48ULL): goto ma17; case ((uint8_t)49ULL): goto ma17; case ((uint8_t)50ULL): goto ma17; case ((uint8_t)51ULL): goto ma17; case ((uint8_t)52ULL): goto ma17; case ((uint8_t)53ULL): goto ma17; case ((uint8_t)54ULL): goto ma17; case ((uint8_t)55ULL): goto ma17; case ((uint8_t)56ULL): goto ma17; case ((uint8_t)57ULL): goto ma17; case ((uint8_t)58ULL): goto ma17; case ((uint8_t)59ULL): goto ma17; case ((uint8_t)60ULL): goto ma17; case ((uint8_t)61ULL): goto ma17; case ((uint8_t)62ULL): goto ma17; case ((uint8_t)63ULL): goto ma17; case ((uint8_t)64ULL): goto ma17; case ((uint8_t)65ULL): goto ma17; case ((uint8_t)66ULL): goto ma17; case ((uint8_t)67ULL): goto ma17; case ((uint8_t)68ULL): goto ma17; case ((uint8_t)69ULL): goto ma17; case ((uint8_t)70ULL): goto ma17; case ((uint8_t)71ULL): goto ma17; case ((uint8_t)72ULL): goto ma17; case ((uint8_t)73ULL): goto ma17; case ((uint8_t)74ULL): goto ma17; case ((uint8_t)75ULL): goto ma17; case ((uint8_t)76ULL): goto ma17; case ((uint8_t)77ULL): goto ma17; case ((uint8_t)78ULL): goto ma17; case ((uint8_t)79ULL): goto ma17; case ((uint8_t)80ULL): goto ma17; case ((uint8_t)81ULL): goto ma17; default: goto ma17; }
ma14:;
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t21 != ((uint8_t)1ULL)) goto mn20;
        uint8_t t23 = gm_l8;
        uint8_t t24 = (uint8_t)!t23;
        if (!t24) goto ie22;
        uint8_t t25 = gm_f_is_negative_rb46l9j(&(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l4)));
        if (!t25) goto ie22;
        {
            gm_tbci5ospk5nnn t27 = F.l5;
            gm_tbci5ospk5nnn t28;
            gm_f_one_08qg1op((void *)&t28, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_add_420lj6o((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), (void *)NULL, cx, NULL, &t27, &t28);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix22;
ie22:;
ix22:;
        goto me13;
mn20:;
ma15:;
        uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t30 != ((uint8_t)4ULL)) goto mn29;
        uint8_t t32 = gm_l8;
        uint8_t t33 = (uint8_t)!t32;
        if (!t33) goto ie31;
        {
            gm_tbci5ospk5nnn t35 = F.l5;
            gm_tbci5ospk5nnn t36;
            gm_f_one_08qg1op((void *)&t36, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_f_add_420lj6o((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), (void *)NULL, cx, NULL, &t35, &t36);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        goto ix31;
ie31:;
ix31:;
        goto me13;
mn29:;
ma16:;
        uint8_t t38 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (t38 != ((uint8_t)2ULL)) goto mn37;
        {
            uint8_t t41 = gm_l8;
            uint8_t t42 = (uint8_t)!t41;
            if (!t42) goto ie40;
            {
                gm_tbci5ospk5nnn t44;
                gm_f_one_08qg1op((void *)&t44, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbci5ospk5nnn t45;
                gm_f_from_u64_ud13q5v((void *)&t45, (void *)NULL, cx, NULL, ((uint64_t)2ULL));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_f_new_2jv6hag((void *)&F.l9, (void *)NULL, cx, NULL, &t44, &t45);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                int32_t t46 = gm_f_cmp_9aseneh((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l6)), &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l9)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_l10 = t46;
            }
            {
                uint8_t t48 = 0;
                int32_t t49 = gm_l10;
                uint8_t t50 = (t49 > ((int32_t)0LL));
                if (!t50) {
                    uint8_t t51 = 0;
                    int32_t t52 = gm_l10;
                    uint8_t t53 = ((t52 == ((int32_t)0LL)));
                    if (t53) {
                        uint8_t t54 = gm_f_is_even_3kgcpjs(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l5)));
                        uint8_t t55 = (uint8_t)!t54;
                        t51 = t55;
                    } else t51 = 0;
                    t48 = t51;
                } else t48 = 1;
                if (!t48) goto ie47;
                {
                    gm_tbci5ospk5nnn t57 = F.l5;
                    gm_tbci5ospk5nnn t58;
                    gm_f_one_08qg1op((void *)&t58, (void *)NULL, cx, NULL);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    gm_f_add_420lj6o((void *)&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), (void *)NULL, cx, NULL, &t57, &t58);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                }
                goto ix47;
ie47:;
ix47:;
            }
            goto ix40;
ie40:;
ix40:;
        }
        goto me13;
mn37:;
ma17:;
        goto me13;
mn59:;
ma18:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 331, 5);
me13:;
    }
    {
        void * t61 = gm_l0;
        gm_t88rcucg1b7uk t62 = gm_l2;
        gm_f_int_range_hue3vms((void *)&F.l11, (void *)NULL, cx, NULL, t61, t62);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        uint8_t t64 = 0;
        int32_t t65 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l11) + 0)));
        uint8_t t66 = (t65 < ((int32_t)0LL));
        if (!t66) {
            int32_t t67 = gm_f_cmp_i1knrgu(&(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l7)), &(*(gm_tbci5ospk5nnn *)(((uint8_t *)&F.l11) + 48)));
            uint8_t t68 = (t67 > ((int32_t)0LL));
            t64 = t68;
        } else t64 = 1;
        if (!t64) goto ie63;
        {
            gm_thrdpodrs8pgt t70;
            memset(&t70, 0, sizeof t70);
            uint8_t t71 = 0;
            F.ret = t70;
            goto gm_fret;
        }
        goto ix63;
ie63:;
ix63:;
    }
    {
        gm_tbci5ospk5nnn t72 = F.l7;
        gm_thrdpodrs8pgt t73;
        GM_ST(uint8_t, (uint8_t *)&t73 + 49, 0);
        GM_ST(uint16_t, (uint8_t *)&t73 + 50, 0);
        GM_ST(uint32_t, (uint8_t *)&t73 + 52, 0);
        GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t73), t72);
        ((uint8_t *)&t73)[48] = 1;
        uint8_t t74 = 0;
        F.ret = t73;
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
__attribute__((aligned(4))) void gm_f_get_bytes_ql9o4k0(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1, gm_b16_8 a2, uint64_t a3) {
    struct gm_fr_get_bytes_ql9o4k0 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_b16_8 gm_l2; /* b */
    uint64_t gm_l3; /* at */
    void * gm_l4; /* s */
    uint64_t gm_l5; /* size */
    unsigned __int128 gm_l6; /* v */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint16_t gm_l10; /* w */
    uint64_t gm_l11; /* n */
    uint64_t gm_l12; /* raw */
    uint64_t gm_l13; /* <tmp> */
    uint64_t gm_l14; /* <tmp> */
    uint64_t gm_l15; /* i */
    gm_t6j6smj7qoctu gm_l16; /* o */
    gm_b16_8 gm_l18; /* <tmp> */
    uint64_t gm_l19; /* <tmp> */
    uint64_t gm_l20; /* i */
    gm_t88rcucg1b7uk gm_l21; /* et */
    GM_STACK_CHECK(&F, 2289u);
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
        void * t4 = gm_l4;
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        switch (t14) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma11; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; default: goto ma12; }
ma10:;
        uint8_t t18 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t18 != ((uint8_t)0ULL)) goto pa17;
        goto po16;
pa17:;
        uint8_t t20 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t20 != ((uint8_t)1ULL)) goto pa19;
        goto po16;
pa19:;
        uint8_t t22 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t22 != ((uint8_t)2ULL)) goto pa21;
        goto po16;
pa21:;
        goto mn15;
po16:;
        {
            void * t24 = gm_l4;
            gm_t88rcucg1b7uk t25 = gm_l1;
            gm_tbccvikg6b96e t26;
            gm_f_layout_a5ovnrm((void *)&t26, (void *)NULL, cx, NULL, t24, t25);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbccvikg6b96e t27 = t26;
            uint64_t t28 = GM_LD(uint64_t, (((uint8_t *)&t27) + 0));
            gm_l5 = t28;
        }
        {
            gm_l6 = ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u));
        }
        {
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                uint64_t t30 = gm_l5;
                gm_l7 = t30;
            }
            {
                for (;;) {
                    uint64_t t32 = gm_l8;
                    uint64_t t33 = gm_l7;
                    uint8_t t34 = (t32 < t33);
                    if (!t34) goto lb31;
                    {
                        uint64_t t37 = gm_l8;
                        uint64_t t38 = t37;
                        uint64_t t41 = GM_LD(uint64_t, ((uint8_t *)&t38));
                        gm_l9 = t41;
                        {
                            unsigned __int128 t43 = gm_l6;
                            gm_b16_8 t44 = gm_l2;
                            gm_b16_8 t45 = t44;
                            uint64_t t46 = gm_l3;
                            void * t47 = gm_l0;
                            uint64_t t48 = gm_l9;
                            uint64_t t49 = gm_l5;
                            uint64_t t50 = gm_f_byte_at_m94bj3e(t47, t48, t49);
                            uint64_t r51; bool o52 = __builtin_add_overflow(t46, t50, &r51);
                            if (o52) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 883, 45);
                            uint64_t t53 = r51;
                            uint64_t t54 = (uint64_t)t53;
                            uint64_t t55 = (uintptr_t)t54;
                            gm_b16_8 t56 = t45;
                            uint64_t t57 = GM_LD(uintptr_t, ((uint8_t *)&t56) + 8);
                            if (t55 >= (uintptr_t)(t57)) gm_trap_index(t55, t57, gm_srcue4ionbe9lgh, 883, 42);
                            uint8_t t58 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t56)) + t55 * 1));
                            unsigned __int128 t59 = (unsigned __int128)t58;
                            uint64_t t60 = gm_l9;
                            uint64_t r61; bool o62 = __builtin_mul_overflow(t60, ((uint64_t)8ULL), &r61);
                            if (o62) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 883, 93);
                            uint64_t t63 = r61;
                            unsigned __int128 t64 = (unsigned __int128)t63;
                            unsigned __int128 n65 = (unsigned __int128)t64;
                            if (n65 >= 128) gm_trap_shift((uint64_t)n65, "u128", gm_srcue4ionbe9lgh, 883, 37);
                            unsigned __int128 r66 = (unsigned __int128)((unsigned __int128)t59 << (int)n65);
                            bool o67 = (unsigned __int128)(r66 >> (int)n65) != t59;
                            if (o67) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 883, 37);
                            unsigned __int128 t68 = r66;
                            unsigned __int128 t69 = (unsigned __int128)(t43 | t68);
                            gm_l6 = t69;
                        }
                        goto me39;
mn40:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 883, 13);
me39:;
                    }
lc31:;
                    {
                        uint64_t t71 = gm_l8;
                        uint64_t r72; bool o73 = __builtin_add_overflow(t71, ((uint64_t)1ULL), &r72);
                        uint64_t t74 = r72;
                        gm_l8 = t74;
                    }
                }
lb31:;
            }
        }
        {
            void * t75 = gm_l0;
            unsigned __int128 t76 = gm_l6;
            gm_t88rcucg1b7uk t77 = gm_l1;
            unsigned __int128 t78 = gm_f_wrap_bt8r149(t75, t76, GM_CONV(gm_csguankko4lmhd, t77));
            gm_t6evimt84lndq t79;
            GM_ST(uint8_t, (uint8_t *)&t79 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t79 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t79 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t79 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t79 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t79 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t79 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t79 + 16), t78);
            gm_tjoi8o798ne9k t80;
            GM_ST(uint8_t, (uint8_t *)&t80 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t80 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t80 + 52, 0);
            GM_ST(uint64_t, (uint8_t *)&t80 + 56, 0);
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t80), t79);
            ((uint8_t *)&t80)[48] = 1;
            uint8_t t81 = 0;
            F.ret = t80;
            goto gm_fret;
        }
        goto me9;
mn15:;
ma11:;
        uint8_t t83 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t83 != ((uint8_t)3ULL)) goto mn82;
        uint16_t t84 = GM_LD(uint16_t, (((uint8_t *)&t8) + 8));
        gm_l10 = t84;
        {
            uint64_t t86;
            uint16_t t88 = gm_l10;
            uint8_t t89 = ((t88 == ((uint16_t)64ULL)));
            if (!t89) goto ie87;
            uint64_t t91 = ((uint64_t)8ULL);
            t86 = t91;
            goto ix87;
ie87:;
            uint64_t t92;
            uint16_t t94 = gm_l10;
            uint8_t t95 = ((t94 == ((uint16_t)32ULL)));
            if (!t95) goto ie93;
            uint64_t t97 = ((uint64_t)4ULL);
            t92 = t97;
            goto ix93;
ie93:;
            gm_tjoi8o798ne9k t99;
            memset(&t99, 0, sizeof t99);
            uint8_t t100 = 0;
            F.ret = t99;
            goto gm_fret;
            t92 = ((uint64_t)0);
ix93:;
            t86 = t92;
ix87:;
            gm_l11 = t86;
        }
        {
            gm_l12 = ((uint64_t)0ULL);
        }
        {
            {
                gm_l14 = ((uint64_t)0ULL);
            }
            {
                uint64_t t102 = gm_l11;
                gm_l13 = t102;
            }
            {
                for (;;) {
                    uint64_t t104 = gm_l14;
                    uint64_t t105 = gm_l13;
                    uint8_t t106 = (t104 < t105);
                    if (!t106) goto lb103;
                    {
                        uint64_t t109 = gm_l14;
                        uint64_t t110 = t109;
                        uint64_t t113 = GM_LD(uint64_t, ((uint8_t *)&t110));
                        gm_l15 = t113;
                        {
                            uint64_t t115 = gm_l12;
                            gm_b16_8 t116 = gm_l2;
                            gm_b16_8 t117 = t116;
                            uint64_t t118 = gm_l3;
                            void * t119 = gm_l0;
                            uint64_t t120 = gm_l15;
                            uint64_t t121 = gm_l11;
                            uint64_t t122 = gm_f_byte_at_m94bj3e(t119, t120, t121);
                            uint64_t r123; bool o124 = __builtin_add_overflow(t118, t122, &r123);
                            if (o124) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 889, 43);
                            uint64_t t125 = r123;
                            uint64_t t126 = (uint64_t)t125;
                            uint64_t t127 = (uintptr_t)t126;
                            gm_b16_8 t128 = t117;
                            uint64_t t129 = GM_LD(uintptr_t, ((uint8_t *)&t128) + 8);
                            if (t127 >= (uintptr_t)(t129)) gm_trap_index(t127, t129, gm_srcue4ionbe9lgh, 889, 40);
                            uint8_t t130 = GM_LD(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t128)) + t127 * 1));
                            uint64_t t131 = (uint64_t)t130;
                            uint64_t t132 = gm_l15;
                            uint64_t r133; bool o134 = __builtin_mul_overflow(t132, ((uint64_t)8ULL), &r133);
                            if (o134) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 889, 84);
                            uint64_t t135 = r133;
                            unsigned __int128 n136 = (unsigned __int128)t135;
                            if (n136 >= 64) gm_trap_shift((uint64_t)n136, "u64", gm_srcue4ionbe9lgh, 889, 36);
                            uint64_t r137 = (uint64_t)((uint64_t)t131 << (int)n136);
                            bool o138 = (uint64_t)(r137 >> (int)n136) != t131;
                            if (o138) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 889, 36);
                            uint64_t t139 = r137;
                            uint64_t t140 = (uint64_t)(t115 | t139);
                            gm_l12 = t140;
                        }
                        goto me111;
mn112:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 889, 13);
me111:;
                    }
lc103:;
                    {
                        uint64_t t142 = gm_l14;
                        uint64_t r143; bool o144 = __builtin_add_overflow(t142, ((uint64_t)1ULL), &r143);
                        uint64_t t145 = r143;
                        gm_l14 = t145;
                    }
                }
lb103:;
            }
        }
        {
            double t146;
            uint16_t t148 = gm_l10;
            uint8_t t149 = ((t148 == ((uint16_t)64ULL)));
            if (!t149) goto ie147;
            uint64_t t151 = gm_l12;
            uint64_t t152 = t151;
            double t153;
            memset(&t153, 0, sizeof t153); memcpy(&t153, ((uint8_t *)&t152), sizeof t153 < 8 ? sizeof t153 : 8);
            double t154 = t153;
            t146 = t154;
            goto ix147;
ie147:;
            uint64_t t156 = gm_l12;
            uint32_t t157 = (uint32_t)t156;
            uint32_t t158 = t157;
            float t159;
            memset(&t159, 0, sizeof t159); memcpy(&t159, ((uint8_t *)&t158), sizeof t159 < 4 ? sizeof t159 : 4);
            double t160 = (double)t159;
            double t161 = t160;
            t146 = t161;
ix147:;
            gm_t6evimt84lndq t162;
            memset(&t162, 0, sizeof t162);
            GM_ST(uint8_t, ((uint8_t *)&t162 + 0), ((uint8_t)1ULL));
            GM_ST(double, ((uint8_t *)&t162 + 16), t146);
            gm_tjoi8o798ne9k t163;
            GM_ST(uint8_t, (uint8_t *)&t163 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t163 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t163 + 52, 0);
            GM_ST(uint64_t, (uint8_t *)&t163 + 56, 0);
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t163), t162);
            ((uint8_t *)&t163)[48] = 1;
            uint8_t t164 = 0;
            F.ret = t163;
            goto gm_fret;
        }
        goto me9;
mn82:;
ma12:;
        {
            void * t167 = gm_l0;
            gm_t88rcucg1b7uk t168 = gm_l1;
            gm_ttu09lij36f8c t169;
            gm_f_offsets_niooj98((void *)&t169, (void *)NULL, cx, NULL, t167, t168);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttu09lij36f8c t170 = t169;
            gm_t6j6smj7qoctu t171;
            if (((((uint8_t *)&t170))[32] != 0)) {
                gm_t6j6smj7qoctu t172 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t170) + 0));
                t171 = t172;
            } else {
                gm_tjoi8o798ne9k t173;
                memset(&t173, 0, sizeof t173);
                uint8_t t174 = 0;
                F.ret = t173;
                goto gm_fret;
            }
            gm_l16 = t171;
        }
        {
            gm_f_list_0202sdh((void *)&F.l17, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_b16_8 t176 = GM_LD(gm_b16_8, (((uint8_t *)&gm_l16) + 0));
                gm_l18 = t176;
            }
            {
                gm_l19 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t178 = gm_l19;
                    uint64_t t179 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                    uint8_t t180 = (t178 < t179);
                    if (!t180) goto lb177;
                    {
                        uint64_t t183 = gm_l19;
                        uint64_t t184 = gm_l19;
                        uint64_t t185 = (uintptr_t)t184;
                        uint64_t t186 = GM_LD(uintptr_t, ((uint8_t *)&gm_l18) + 8);
                        gm_t88rcucg1b7uk t187 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l18)) + t185 * 4));
                        gm_tlcm4lvu3g1a8 t188;
                        GM_ST(uint32_t, (uint8_t *)&t188 + 12, 0);
                        GM_ST(uint64_t, ((uint8_t *)&t188 + 0), t183);
                        GM_ST(gm_t88rcucg1b7uk, ((uint8_t *)&t188 + 8), t187);
                        gm_tlcm4lvu3g1a8 t189 = t188;
                        uint64_t t192 = GM_LD(uint64_t, (((uint8_t *)&t189) + 0));
                        gm_l20 = t192;
                        gm_t88rcucg1b7uk t193 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t189) + 8));
                        gm_l21 = t193;
                        {
                            void * t195 = (void *)((uint8_t *)&F.l17);
                            void * t196 = gm_l0;
                            gm_t88rcucg1b7uk t197 = gm_l21;
                            gm_b16_8 t198 = gm_l2;
                            uint64_t t199 = gm_l3;
                            uint64_t t200 = gm_l20;
                            uint64_t t201 = (uintptr_t)t200;
                            uint64_t t202 = GM_LD(uintptr_t, (((uint8_t *)&gm_l16) + 16) + 8);
                            if (t201 >= (uintptr_t)(t202)) gm_trap_index(t201, t202, gm_srcue4ionbe9lgh, 895, 80);
                            uint64_t t203 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l16) + 16)) + t201 * 8));
                            uint64_t r204; bool o205 = __builtin_add_overflow(t199, t203, &r204);
                            if (o205) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 895, 75);
                            uint64_t t206 = r204;
                            gm_tjoi8o798ne9k t207;
                            gm_f_get_bytes_ql9o4k0((void *)&t207, (void *)NULL, cx, NULL, t196, t197, t198, t206);
                            if (gm_unw.kind) {
                                goto gm_ll181;
                            }
                            gm_tjoi8o798ne9k t208 = t207;
                            gm_t6evimt84lndq t209;
                            if (((((uint8_t *)&t208))[48] != 0)) {
                                gm_t6evimt84lndq t210 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t208) + 0));
                                t209 = t210;
                            } else {
                                gm_tjoi8o798ne9k t211;
                                memset(&t211, 0, sizeof t211);
                                uint8_t t212 = 0;
                                F.ret = t211;
                                goto gm_fret;
                            }
                            gm_tftb1odc739t5 t213 = gm_f_push_0pgacsj(t195, &t209);
                            gm_tftb1odc739t5 t214 = t213;
                            if (((((uint8_t *)&t214))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t214) + 1, gm_srcue4ionbe9lgh, 895, 45);
                            gm_b0_1 t215 = GM_LD(gm_b0_1, (((uint8_t *)&t214) + 1));
                        }
                        goto me190;
mn191:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 895, 13);
me190:;
                    }
lc177:;
                    {
                        uint64_t t217 = gm_l19;
                        uint64_t r218; bool o219 = __builtin_add_overflow(t217, ((uint64_t)1ULL), &r218);
                        uint64_t t220 = r218;
                        gm_l19 = t220;
                    }
                }
gm_ll181:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb177; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc177; }
                }
                goto gm_lad;
lb177:;
            }
        }
        {
            gm_b16_8 t221 = GM_LD(gm_b16_8, (((uint8_t *)&F.l17) + 0));
            gm_tnpifv20irnph t222;
            GM_ST(gm_b16_8, ((uint8_t *)&t222 + 0), t221);
            gm_tq7s98ihonkju t223 = gm_f_new_lcff781((void *)NULL, cx, NULL, t222, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kdra5fvcfrb)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t224 = t223;
            if (((((uint8_t *)&t224))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t224) + 8, gm_srcue4ionbe9lgh, 896, 25);
            void * t225 = GM_LD(void *, (((uint8_t *)&t224) + 8));
            gm_t6evimt84lndq t226;
            memset(&t226, 0, sizeof t226);
            GM_ST(uint8_t, ((uint8_t *)&t226 + 0), ((uint8_t)3ULL));
            GM_ST(void *, ((uint8_t *)&t226 + 16), t225);
            gm_tjoi8o798ne9k t227;
            GM_ST(uint8_t, (uint8_t *)&t227 + 49, 0);
            GM_ST(uint16_t, (uint8_t *)&t227 + 50, 0);
            GM_ST(uint32_t, (uint8_t *)&t227 + 52, 0);
            GM_ST(uint64_t, (uint8_t *)&t227 + 56, 0);
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t227), t226);
            ((uint8_t *)&t227)[48] = 1;
            uint8_t t228 = 0;
            F.ret = t227;
            goto gm_fret;
        }
        goto me9;
mn165:;
ma13:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 879, 5);
me9:;
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
inline __attribute__((aligned(4))) gm_ttovhfee8mf28 gm_f_int_info_2r9qb5n(void * a0, gm_csguankko4lmhd a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_info_2r9qb5n F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint8_t gm_l2; /* signed */
    uint16_t gm_l3; /* bits */
    GM_STACK_CHECK_SHALLOW(&F, 297u);
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
    gm_ttovhfee8mf28 t7;
    uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    switch (t14) { case ((uint8_t)0ULL): goto ma9; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma11; case ((uint8_t)3ULL): goto ma12; case ((uint8_t)4ULL): goto ma12; case ((uint8_t)5ULL): goto ma12; case ((uint8_t)6ULL): goto ma12; case ((uint8_t)7ULL): goto ma12; case ((uint8_t)8ULL): goto ma12; case ((uint8_t)9ULL): goto ma12; case ((uint8_t)10ULL): goto ma12; case ((uint8_t)11ULL): goto ma12; case ((uint8_t)12ULL): goto ma12; case ((uint8_t)13ULL): goto ma12; case ((uint8_t)14ULL): goto ma12; case ((uint8_t)15ULL): goto ma12; case ((uint8_t)16ULL): goto ma12; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma12; case ((uint8_t)19ULL): goto ma12; case ((uint8_t)20ULL): goto ma12; case ((uint8_t)21ULL): goto ma12; case ((uint8_t)22ULL): goto ma12; case ((uint8_t)23ULL): goto ma12; case ((uint8_t)24ULL): goto ma12; case ((uint8_t)25ULL): goto ma12; case ((uint8_t)26ULL): goto ma12; default: goto ma12; }
ma9:;
    uint8_t t16 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    if (t16 != ((uint8_t)0ULL)) goto mn15;
    uint8_t t17 = GM_LD(uint8_t, ((((uint8_t *)&t6) + 8) + 0));
    gm_l2 = t17;
    uint16_t t18 = GM_LD(uint16_t, ((((uint8_t *)&t6) + 8) + 2));
    gm_l3 = t18;
    uint8_t t19 = gm_l2;
    uint16_t t20 = gm_l3;
    uint32_t t21 = (uint32_t)t20;
    gm_ttovhfee8mf28 t22;
    GM_ST(uint8_t, (uint8_t *)&t22 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t22 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t22 + 0), t19);
    GM_ST(uint32_t, ((uint8_t *)&t22 + 4), t21);
    t7 = t22;
    goto me8;
mn15:;
ma10:;
    uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    if (t24 != ((uint8_t)1ULL)) goto mn23;
    void * t25 = gm_l0;
    void * t26 = GM_LD(void *, (((uint8_t *)t25) + 0));
    void * t27 = GM_LD(void *, (((uint8_t *)t26) + 0));
    void * t28 = (void *)(((uint8_t *)t27) + 88);
    uint32_t t29 = gm_f_ptr_bits_glps5i7(t28);
    gm_ttovhfee8mf28 t30;
    GM_ST(uint8_t, (uint8_t *)&t30 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t30 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t30 + 0), ((uint8_t)0));
    GM_ST(uint32_t, ((uint8_t *)&t30 + 4), t29);
    t7 = t30;
    goto me8;
mn23:;
ma11:;
    uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&t6) + 0));
    if (t32 != ((uint8_t)2ULL)) goto mn31;
    void * t33 = gm_l0;
    void * t34 = GM_LD(void *, (((uint8_t *)t33) + 0));
    void * t35 = GM_LD(void *, (((uint8_t *)t34) + 0));
    void * t36 = (void *)(((uint8_t *)t35) + 88);
    uint32_t t37 = gm_f_ptr_bits_glps5i7(t36);
    gm_ttovhfee8mf28 t38;
    GM_ST(uint8_t, (uint8_t *)&t38 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t38 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t38 + 0), ((uint8_t)1));
    GM_ST(uint32_t, ((uint8_t *)&t38 + 4), t37);
    t7 = t38;
    goto me8;
mn31:;
ma12:;
    gm_ttovhfee8mf28 t40;
    GM_ST(uint8_t, (uint8_t *)&t40 + 1, 0);
    GM_ST(uint16_t, (uint8_t *)&t40 + 2, 0);
    GM_ST(uint8_t, ((uint8_t *)&t40 + 0), ((uint8_t)0));
    GM_ST(uint32_t, ((uint8_t *)&t40 + 4), ((uint32_t)128ULL));
    t7 = t40;
    goto me8;
mn39:;
ma13:;
    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 6, 74);
me8:;
    F.ret = t7;
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) void gm_f_int_op_jnafd2a(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, unsigned __int128 a2, unsigned __int128 a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_int_op_jnafd2a F;
    void * gm_l0; /* m */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    unsigned __int128 gm_l2; /* x */
    unsigned __int128 gm_l3; /* y */
    gm_t88rcucg1b7uk gm_l4; /* lt */
    gm_t88rcucg1b7uk gm_l5; /* rt */
    gm_tl874t2atnlh3 gm_l6; /* span */
    gm_ttovhfee8mf28 gm_l7; /* info */
    gm_b16_8 gm_l8; /* sym */
    __int128 gm_l9; /* amount */
    uint32_t gm_l10; /* n */
    gm_t9qkh0rsooc8d gm_l11; /* flavor */
    uint8_t gm_l12; /* is_div */
    __int128 gm_l13; /* a */
    __int128 gm_l14; /* c */
    __int128 gm_l15; /* exact */
    gm_t9qkh0rsooc8d gm_l19; /* flavor */
    gm_t9qkh0rsooc8d gm_l20; /* flavor */
    gm_t9qkh0rsooc8d gm_l21; /* flavor */
    __int128 gm_l22; /* a */
    __int128 gm_l23; /* c */
    __int128 gm_l24; /* exact */
    GM_STACK_CHECK(&F, 4588u);
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
        gm_t88rcucg1b7uk t3 = gm_l4;
        gm_ttovhfee8mf28 t4 = gm_f_int_info_2r9qb5n(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_l7 = t4;
    }
    {
        gm_ts6oqppjh0ba3 t5 = gm_l1;
        gm_b16_8 t6 = gm_f_binop_symbol_9ijp0eo(t5);
        gm_l8 = t6;
    }
    {
        uint8_t t13 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t13) { case ((uint8_t)0ULL): goto ma10; case ((uint8_t)1ULL): goto ma10; case ((uint8_t)2ULL): goto ma10; case ((uint8_t)3ULL): goto ma8; case ((uint8_t)4ULL): goto ma9; case ((uint8_t)5ULL): goto ma9; case ((uint8_t)6ULL): goto ma8; case ((uint8_t)7ULL): goto ma11; case ((uint8_t)8ULL): goto ma11; case ((uint8_t)9ULL): goto ma11; case ((uint8_t)10ULL): goto ma11; case ((uint8_t)11ULL): goto ma11; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma11; default: goto ma11; }
ma8:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t17 != ((uint8_t)3ULL)) goto pa16;
        goto po15;
pa16:;
        uint8_t t19 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t19 != ((uint8_t)6ULL)) goto pa18;
        goto po15;
pa18:;
        goto mn14;
po15:;
        {
            void * t21 = gm_l0;
            unsigned __int128 t22 = gm_l3;
            gm_t88rcucg1b7uk t23 = gm_l5;
            __int128 t24 = gm_f_int_value_6hmh8tj(t21, t22, GM_CONV(gm_csguankko4lmhd, t23));
            gm_l9 = t24;
        }
        {
            uint8_t t26 = 0;
            __int128 t27 = gm_l9;
            uint8_t t28 = (t27 < ((__int128)(((unsigned __int128)0u << 64) | 0u)));
            if (!t28) {
                __int128 t29 = gm_l9;
                uint32_t t30 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                __int128 t31 = (__int128)t30;
                uint8_t t32 = (t29 >= t31);
                t26 = t32;
            } else t26 = 1;
            if (!t26) goto ie25;
            {
                void * t34 = gm_l0;
                gm_tl874t2atnlh3 t35 = gm_l6;
                gm_t7a4aqr549lb5 t36;
                __int128 t37 = gm_l9;
                GM_ST(__int128, (uint8_t *)&t36 + 0, t37);
                void * t38 = gm_l0;
                void * t39 = GM_LD(void *, (((uint8_t *)t38) + 0));
                gm_t88rcucg1b7uk t40 = gm_l4;
                gm_b16_8 t41 = gm_f_show_8uijkbs((void *)NULL, cx, NULL, t39, t40);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                GM_ST(gm_b16_8, (uint8_t *)&t36 + 16, t41);
                gm_tabb710d9mq2e t42;
                gm_f_format_0fuesvo((void *)&t42, (void *)NULL, cx, NULL, &t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tabb710d9mq2e t43 = t42;
                if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 8, gm_srcue4ionbe9lgh, 204, 79);
                gm_b16_8 t44 = GM_LD(gm_b16_8, (((uint8_t *)&t43) + 8));
                gm_b16_8 t45 = t44;
                gm_b16_8 t46 = GM_LD(gm_b16_8, ((uint8_t *)&t45));
                gm_t39uai1ajk3qv t47;
                gm_f_trap_jftbss6((void *)&t47, (void *)NULL, cx, NULL, t34, t35, t46);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t8carr7mku484 t48;
                GM_ST(uint8_t, (uint8_t *)&t48 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t48 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t48 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t48 + 8, 0);
                ((uint8_t *)&t48)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t48 + 16), t47);
                gm_t8carr7mku484 t49 = t48;
                uint8_t t50 = ((((uint8_t *)&t49))[0] != 0);
                F.ret = t48;
                goto gm_fret;
            }
            goto ix25;
ie25:;
ix25:;
        }
        {
            __int128 t51 = gm_l9;
            uint32_t t52 = (uint32_t)t51;
            gm_l10 = t52;
        }
        {
            uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t54 != ((uint8_t)3ULL)) goto ie53;
            gm_t9qkh0rsooc8d t55 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
            gm_l11 = t55;
            {
                uint32_t t58 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
                uint8_t t59 = (t58 <= ((uint32_t)63ULL));
                if (!t59) goto ie57;
                {
                    void * t61 = gm_l0;
                    void * t62 = gm_l0;
                    unsigned __int128 t63 = gm_l2;
                    gm_t88rcucg1b7uk t64 = gm_l4;
                    __int128 t65 = gm_f_int_value_6hmh8tj(t62, t63, GM_CONV(gm_csguankko4lmhd, t64));
                    uint32_t t66 = gm_l10;
                    unsigned __int128 n67 = (unsigned __int128)t66;
                    if (n67 >= 128) gm_trap_shift((uint64_t)n67, "i128", gm_srcue4ionbe9lgh, 207, 57);
                    __int128 r68 = (__int128)((unsigned __int128)t65 << (int)n67);
                    bool o69 = (__int128)(r68 >> (int)n67) != t65;
                    if (o69) gm_trap("integer overflow in `<<`", gm_srcue4ionbe9lgh, 207, 57);
                    __int128 t70 = r68;
                    gm_t9qkh0rsooc8d t71 = gm_l11;
                    gm_t88rcucg1b7uk t72 = gm_l4;
                    gm_b16_8 t73 = gm_l8;
                    gm_tl874t2atnlh3 t74 = gm_l6;
                    gm_f_fit_small_7o94i8v((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t61, t70, t71, t72, t73, t74);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    goto gm_fdone;
                }
                goto ix57;
ie57:;
ix57:;
            }
            {
                void * t75 = gm_l0;
                void * t76 = gm_l0;
                unsigned __int128 t77 = gm_l2;
                gm_t88rcucg1b7uk t78 = gm_l4;
                gm_tbci5ospk5nnn t79;
                gm_f_big_of_1cq0qao((void *)&t79, (void *)NULL, cx, NULL, t76, t77, t78);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                uint32_t t80 = gm_l10;
                uint64_t t81 = (uint64_t)t80;
                gm_tbci5ospk5nnn t82;
                gm_f_shl_qqecl81((void *)&t82, (void *)NULL, cx, NULL, &t79, t81);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t9qkh0rsooc8d t83 = gm_l11;
                gm_t88rcucg1b7uk t84 = gm_l4;
                gm_b16_8 t85 = gm_l8;
                gm_tl874t2atnlh3 t86 = gm_l6;
                gm_f_fit_8hvi4bv((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t75, &t82, t83, t84, t85, t86);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix53;
ie53:;
ix53:;
        }
        {
            uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            if (!t88) goto ie87;
            {
                void * t90 = gm_l0;
                void * t91 = gm_l0;
                unsigned __int128 t92 = gm_l2;
                gm_t88rcucg1b7uk t93 = gm_l4;
                __int128 t94 = gm_f_int_value_6hmh8tj(t91, t92, GM_CONV(gm_csguankko4lmhd, t93));
                uint32_t t95 = gm_l10;
                unsigned __int128 n96 = (unsigned __int128)t95;
                if (n96 >= 128) gm_trap_shift((uint64_t)n96, "i128", gm_srcue4ionbe9lgh, 210, 45);
                __int128 t97 = (__int128)(t94 >> (int)n96);
                __int128 t98 = t97;
                unsigned __int128 t99;
                memset(&t99, 0, sizeof t99); memcpy(&t99, ((uint8_t *)&t98), sizeof t99 < 16 ? sizeof t99 : 16);
                gm_t88rcucg1b7uk t100 = gm_l4;
                unsigned __int128 t101 = gm_f_wrap_bt8r149(t90, t99, GM_CONV(gm_csguankko4lmhd, t100));
                gm_t8carr7mku484 t102;
                memset(&t102, 0, sizeof t102);
                ((uint8_t *)&t102)[0] = 0;
                GM_ST(unsigned __int128, ((uint8_t *)&t102 + 16), t101);
                gm_t8carr7mku484 t103 = t102;
                uint8_t t104 = ((((uint8_t *)&t103))[0] != 0);
                F.ret = t102;
                goto gm_fret;
            }
            goto ix87;
ie87:;
ix87:;
        }
        {
            unsigned __int128 t105 = gm_l2;
            uint32_t t106 = gm_l10;
            unsigned __int128 t107 = (unsigned __int128)t106;
            unsigned __int128 n108 = (unsigned __int128)t107;
            if (n108 >= 128) gm_trap_shift((uint64_t)n108, "u128", gm_srcue4ionbe9lgh, 211, 20);
            unsigned __int128 t109 = (unsigned __int128)(t105 >> (int)n108);
            gm_t8carr7mku484 t110;
            memset(&t110, 0, sizeof t110);
            ((uint8_t *)&t110)[0] = 0;
            GM_ST(unsigned __int128, ((uint8_t *)&t110 + 16), t109);
            gm_t8carr7mku484 t111 = t110;
            uint8_t t112 = ((((uint8_t *)&t111))[0] != 0);
            F.ret = t110;
            goto gm_fret;
        }
        goto me7;
mn14:;
ma9:;
        uint8_t t116 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t116 != ((uint8_t)4ULL)) goto pa115;
        goto po114;
pa115:;
        uint8_t t118 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t118 != ((uint8_t)5ULL)) goto pa117;
        goto po114;
pa117:;
        goto mn113;
po114:;
        {
            unsigned __int128 t121 = gm_l3;
            uint8_t t122 = ((t121 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 0u))));
            if (!t122) goto ie120;
            {
                void * t124 = gm_l0;
                gm_tl874t2atnlh3 t125 = gm_l6;
                gm_b16_8 t126;
                GM_ST(void *, (uint8_t *)&t126, (void *)(gm_slf2kl43ogurh));
                GM_ST(uintptr_t, (uint8_t *)&t126 + 8, (uintptr_t)(16));
                gm_t39uai1ajk3qv t127;
                gm_f_trap_jftbss6((void *)&t127, (void *)NULL, cx, NULL, t124, t125, t126);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t8carr7mku484 t128;
                GM_ST(uint8_t, (uint8_t *)&t128 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t128 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t128 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t128 + 8, 0);
                ((uint8_t *)&t128)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t128 + 16), t127);
                gm_t8carr7mku484 t129 = t128;
                uint8_t t130 = ((((uint8_t *)&t129))[0] != 0);
                F.ret = t128;
                goto gm_fret;
            }
            goto ix120;
ie120:;
ix120:;
        }
        {
            uint8_t t131 = ((GM_LD(uint8_t, ((uint8_t *)&gm_l1) + 0) == ((uint8_t)4ULL)));
            gm_l12 = t131;
        }
        {
            uint32_t t133 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
            uint8_t t134 = (t133 <= ((uint32_t)63ULL));
            if (!t134) goto ie132;
            {
                void * t136 = gm_l0;
                unsigned __int128 t137 = gm_l2;
                gm_t88rcucg1b7uk t138 = gm_l4;
                __int128 t139 = gm_f_int_value_6hmh8tj(t136, t137, GM_CONV(gm_csguankko4lmhd, t138));
                gm_l13 = t139;
            }
            {
                void * t140 = gm_l0;
                unsigned __int128 t141 = gm_l3;
                gm_t88rcucg1b7uk t142 = gm_l5;
                __int128 t143 = gm_f_int_value_6hmh8tj(t140, t141, GM_CONV(gm_csguankko4lmhd, t142));
                gm_l14 = t143;
            }
            {
                __int128 t144;
                uint8_t t146 = gm_l12;
                if (!t146) goto ie145;
                __int128 t148 = gm_l13;
                __int128 t149 = gm_l14;
                if (t149 == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 219, 38);
                if (t148 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u)) && t149 == (__int128)-1) gm_trap("integer overflow in `/`", gm_srcue4ionbe9lgh, 219, 38);
                __int128 t150 = (__int128)(t148 / t149);
                __int128 t151 = t150;
                t144 = t151;
                goto ix145;
ie145:;
                __int128 t153 = gm_l13;
                __int128 t154 = gm_l14;
                if (t154 == 0) gm_trap("division by zero", gm_srcue4ionbe9lgh, 219, 53);
                if (t153 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u)) && t154 == (__int128)-1) gm_trap("integer overflow in `%`", gm_srcue4ionbe9lgh, 219, 53);
                __int128 t155 = (__int128)(t153 % t154);
                __int128 t156 = t155;
                t144 = t156;
ix145:;
                gm_l15 = t144;
            }
            {
                void * t157 = gm_l0;
                __int128 t158 = gm_l15;
                gm_t9qkh0rsooc8d t159;
                GM_ST(uint8_t, ((uint8_t *)&t159 + 0), ((uint8_t)0ULL));
                gm_t88rcucg1b7uk t160 = gm_l4;
                gm_b16_8 t161 = gm_l8;
                gm_tl874t2atnlh3 t162 = gm_l6;
                gm_f_fit_small_7o94i8v((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t157, t158, t159, t160, t161, t162);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix132;
ie132:;
ix132:;
        }
        {
            void * t163 = gm_l0;
            unsigned __int128 t164 = gm_l2;
            gm_t88rcucg1b7uk t165 = gm_l4;
            gm_f_big_of_1cq0qao((void *)&F.l16, (void *)NULL, cx, NULL, t163, t164, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t166 = gm_l0;
            unsigned __int128 t167 = gm_l3;
            gm_t88rcucg1b7uk t168 = gm_l5;
            gm_f_big_of_1cq0qao((void *)&F.l17, (void *)NULL, cx, NULL, t166, t167, t168);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            uint8_t t170 = GM_LD(uint8_t, (((uint8_t *)&gm_l7) + 0));
            if (!t170) goto ie169;
            uint8_t t171 = gm_l12;
            uint8_t t172 = (uint8_t)!t171;
            if (!t172) goto ie169;
            gm_tbci5ospk5nnn t173 = F.l17;
            gm_tbci5ospk5nnn t174;
            gm_f_one_08qg1op((void *)&t174, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t175;
            gm_f_neg_agd5c5k((void *)&t175, (void *)NULL, cx, NULL, &t174);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint8_t t176 = gm_f_eq_60fjn21(&t173, &t175);
            if (!t176) goto ie169;
            gm_tbci5ospk5nnn t177 = F.l16;
            void * t178 = gm_l0;
            gm_t88rcucg1b7uk t179 = gm_l4;
            gm_tngtuiuskn938 t180;
            gm_f_int_range_hue3vms((void *)&t180, (void *)NULL, cx, NULL, t178, t179);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tngtuiuskn938 t181 = t180;
            gm_tbci5ospk5nnn t182 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)&t181) + 0));
            uint8_t t183 = gm_f_eq_60fjn21(&t177, &t182);
            if (!t183) goto ie169;
            {
                void * t185 = gm_l0;
                gm_tl874t2atnlh3 t186 = gm_l6;
                gm_b16_8 t187;
                GM_ST(void *, (uint8_t *)&t187, (void *)(gm_s3385032lmmqq));
                GM_ST(uintptr_t, (uint8_t *)&t187 + 8, (uintptr_t)(23));
                gm_t39uai1ajk3qv t188;
                gm_f_trap_jftbss6((void *)&t188, (void *)NULL, cx, NULL, t185, t186, t187);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t8carr7mku484 t189;
                GM_ST(uint8_t, (uint8_t *)&t189 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t189 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t189 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t189 + 8, 0);
                ((uint8_t *)&t189)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t189 + 16), t188);
                gm_t8carr7mku484 t190 = t189;
                uint8_t t191 = ((((uint8_t *)&t190))[0] != 0);
                F.ret = t189;
                goto gm_fret;
            }
            goto ix169;
ie169:;
ix169:;
        }
        {
            gm_tbci5ospk5nnn t192;
            uint8_t t194 = gm_l12;
            if (!t194) goto ie193;
            gm_tbci5ospk5nnn t196;
            gm_f_div_f0qrfbr((void *)&t196, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l17)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t197 = t196;
            t192 = t197;
            goto ix193;
ie193:;
            gm_tbci5ospk5nnn t199;
            gm_f_rem_ib4l1vl((void *)&t199, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l16)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l17)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t200 = t199;
            t192 = t200;
ix193:;
            F.l18 = t192;
        }
        {
            void * t201 = gm_l0;
            gm_t9qkh0rsooc8d t202;
            GM_ST(uint8_t, ((uint8_t *)&t202 + 0), ((uint8_t)0ULL));
            gm_t88rcucg1b7uk t203 = gm_l4;
            gm_b16_8 t204 = gm_l8;
            gm_tl874t2atnlh3 t205 = gm_l6;
            gm_f_fit_8hvi4bv((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t201, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l18)), t202, t203, t204, t205);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me7;
mn113:;
ma10:;
        uint8_t t209 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t209 != ((uint8_t)0ULL)) goto pa208;
        gm_t9qkh0rsooc8d t210 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l19 = t210;
        goto po207;
pa208:;
        uint8_t t212 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t212 != ((uint8_t)1ULL)) goto pa211;
        gm_t9qkh0rsooc8d t213 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l19 = t213;
        goto po207;
pa211:;
        uint8_t t215 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t215 != ((uint8_t)2ULL)) goto pa214;
        gm_t9qkh0rsooc8d t216 = GM_LD(gm_t9qkh0rsooc8d, (((uint8_t *)&gm_l1) + 1));
        gm_l19 = t216;
        goto po207;
pa214:;
        goto mn206;
po207:;
        {
            uint32_t t219 = GM_LD(uint32_t, (((uint8_t *)&gm_l7) + 4));
            uint8_t t220 = (t219 <= ((uint32_t)63ULL));
            if (!t220) goto ie218;
            {
                void * t222 = gm_l0;
                unsigned __int128 t223 = gm_l2;
                gm_t88rcucg1b7uk t224 = gm_l4;
                __int128 t225 = gm_f_int_value_6hmh8tj(t222, t223, GM_CONV(gm_csguankko4lmhd, t224));
                gm_l22 = t225;
            }
            {
                void * t226 = gm_l0;
                unsigned __int128 t227 = gm_l3;
                gm_t88rcucg1b7uk t228 = gm_l5;
                __int128 t229 = gm_f_int_value_6hmh8tj(t226, t227, GM_CONV(gm_csguankko4lmhd, t228));
                gm_l23 = t229;
            }
            {
                __int128 t230;
                uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                switch (t236) { case ((uint8_t)0ULL): goto ma232; case ((uint8_t)1ULL): goto ma233; case ((uint8_t)2ULL): goto ma234; case ((uint8_t)3ULL): goto ma234; case ((uint8_t)4ULL): goto ma234; case ((uint8_t)5ULL): goto ma234; case ((uint8_t)6ULL): goto ma234; case ((uint8_t)7ULL): goto ma234; case ((uint8_t)8ULL): goto ma234; case ((uint8_t)9ULL): goto ma234; case ((uint8_t)10ULL): goto ma234; case ((uint8_t)11ULL): goto ma234; case ((uint8_t)12ULL): goto ma234; case ((uint8_t)13ULL): goto ma234; default: goto ma234; }
ma232:;
                uint8_t t238 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t238 != ((uint8_t)0ULL)) goto mn237;
                __int128 t239 = gm_l22;
                __int128 t240 = gm_l23;
                __int128 r241; bool o242 = __builtin_add_overflow(t239, t240, &r241);
                if (o242) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 233, 32);
                __int128 t243 = r241;
                t230 = t243;
                goto me231;
mn237:;
ma233:;
                uint8_t t245 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t245 != ((uint8_t)1ULL)) goto mn244;
                __int128 t246 = gm_l22;
                __int128 t247 = gm_l23;
                __int128 r248; bool o249 = __builtin_sub_overflow(t246, t247, &r248);
                if (o249) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 234, 32);
                __int128 t250 = r248;
                t230 = t250;
                goto me231;
mn244:;
ma234:;
                __int128 t252 = gm_l22;
                __int128 t253 = gm_l23;
                __int128 r254; bool o255 = __builtin_mul_overflow(t252, t253, &r254);
                if (o255) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 235, 26);
                __int128 t256 = r254;
                t230 = t256;
                goto me231;
mn251:;
ma235:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 232, 26);
me231:;
                gm_l24 = t230;
            }
            {
                void * t257 = gm_l0;
                __int128 t258 = gm_l24;
                gm_t9qkh0rsooc8d t259 = gm_l19;
                gm_t88rcucg1b7uk t260 = gm_l4;
                gm_b16_8 t261 = gm_l8;
                gm_tl874t2atnlh3 t262 = gm_l6;
                gm_f_fit_small_7o94i8v((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t257, t258, t259, t260, t261, t262);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                goto gm_fdone;
            }
            goto ix218;
ie218:;
ix218:;
        }
        {
            void * t263 = gm_l0;
            unsigned __int128 t264 = gm_l2;
            gm_t88rcucg1b7uk t265 = gm_l4;
            gm_f_big_of_1cq0qao((void *)&F.l25, (void *)NULL, cx, NULL, t263, t264, t265);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            void * t266 = gm_l0;
            unsigned __int128 t267 = gm_l3;
            gm_t88rcucg1b7uk t268 = gm_l5;
            gm_f_big_of_1cq0qao((void *)&F.l26, (void *)NULL, cx, NULL, t266, t267, t268);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tbci5ospk5nnn t269;
            uint8_t t275 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            switch (t275) { case ((uint8_t)0ULL): goto ma271; case ((uint8_t)1ULL): goto ma272; case ((uint8_t)2ULL): goto ma273; case ((uint8_t)3ULL): goto ma273; case ((uint8_t)4ULL): goto ma273; case ((uint8_t)5ULL): goto ma273; case ((uint8_t)6ULL): goto ma273; case ((uint8_t)7ULL): goto ma273; case ((uint8_t)8ULL): goto ma273; case ((uint8_t)9ULL): goto ma273; case ((uint8_t)10ULL): goto ma273; case ((uint8_t)11ULL): goto ma273; case ((uint8_t)12ULL): goto ma273; case ((uint8_t)13ULL): goto ma273; default: goto ma273; }
ma271:;
            uint8_t t277 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t277 != ((uint8_t)0ULL)) goto mn276;
            gm_tbci5ospk5nnn t278;
            gm_f_add_420lj6o((void *)&t278, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l25)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l26)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t269 = t278;
            goto me270;
mn276:;
ma272:;
            uint8_t t280 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
            if (t280 != ((uint8_t)1ULL)) goto mn279;
            gm_tbci5ospk5nnn t281;
            gm_f_sub_pktm5cr((void *)&t281, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l25)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l26)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t269 = t281;
            goto me270;
mn279:;
ma273:;
            gm_tbci5ospk5nnn t283;
            gm_f_mul_nfj5rjv((void *)&t283, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l25)), &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l26)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            t269 = t283;
            goto me270;
mn282:;
ma274:;
            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 241, 22);
me270:;
            F.l27 = t269;
        }
        {
            void * t284 = gm_l0;
            gm_t9qkh0rsooc8d t285 = gm_l19;
            gm_t88rcucg1b7uk t286 = gm_l4;
            gm_b16_8 t287 = gm_l8;
            gm_tl874t2atnlh3 t288 = gm_l6;
            gm_f_fit_8hvi4bv((void *)&(*(gm_t8carr7mku484 *)gm_out), (void *)NULL, cx, NULL, t284, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l27)), t285, t286, t287, t288);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            goto gm_fdone;
        }
        goto me7;
mn206:;
ma11:;
        void * t290 = gm_l0;
        gm_tl874t2atnlh3 t291 = gm_l6;
        gm_b16_8 t292;
        GM_ST(void *, (uint8_t *)&t292, (void *)(gm_sqe4aodms9tfs));
        GM_ST(uintptr_t, (uint8_t *)&t292 + 8, (uintptr_t)(13));
        gm_t39uai1ajk3qv t293;
        gm_f_unsupported_2seq49l((void *)&t293, t290, GM_CONV(gm_cseahvg8v7p0vs, t291), t292);
        gm_t8carr7mku484 t294;
        GM_ST(uint8_t, (uint8_t *)&t294 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t294 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t294 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t294 + 8, 0);
        ((uint8_t *)&t294)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t294 + 16), t293);
        gm_t8carr7mku484 t295 = t294;
        uint8_t t296 = ((((uint8_t *)&t295))[0] != 0);
        F.ret = t294;
        goto gm_fret;
        goto me7;
mn289:;
ma12:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 201, 5);
me7:;
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
inline __attribute__((aligned(4))) void gm_f_int_range_hue3vms(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_int_range_hue3vms F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    gm_ttovhfee8mf28 gm_l2; /* info */
    GM_STACK_CHECK(&F, 549u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l1;
        gm_ttovhfee8mf28 t4 = gm_f_int_info_2r9qb5n(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_l2 = t4;
    }
    {
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l2) + 0));
        if (!t6) goto ie5;
        {
            gm_tbci5ospk5nnn t8;
            gm_f_one_08qg1op((void *)&t8, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            uint32_t t9 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
            uint32_t r10; bool o11 = __builtin_sub_overflow(t9, ((uint32_t)1ULL), &r10);
            if (o11) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 39, 38);
            uint32_t t12 = r10;
            uint64_t t13 = (uint64_t)t12;
            gm_f_shl_qqecl81((void *)&F.l3, (void *)NULL, cx, NULL, &t8, t13);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_tbci5ospk5nnn t14;
            gm_f_neg_agd5c5k((void *)&t14, (void *)NULL, cx, NULL, &(*(gm_tbci5ospk5nnn *)((uint8_t *)&F.l3)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t15 = F.l3;
            gm_tbci5ospk5nnn t16;
            gm_f_one_08qg1op((void *)&t16, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t17;
            gm_f_sub_pktm5cr((void *)&t17, (void *)NULL, cx, NULL, &t15, &t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0, t14);
            GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48, t17);
            goto gm_fdone;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        gm_tbci5ospk5nnn t18;
        gm_f_zero_s25l990((void *)&t18, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t19;
        gm_f_one_08qg1op((void *)&t19, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint32_t t20 = GM_LD(uint32_t, (((uint8_t *)&gm_l2) + 4));
        uint64_t t21 = (uint64_t)t20;
        gm_tbci5ospk5nnn t22;
        gm_f_shl_qqecl81((void *)&t22, (void *)NULL, cx, NULL, &t19, t21);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t23;
        gm_f_one_08qg1op((void *)&t23, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbci5ospk5nnn t24;
        gm_f_sub_pktm5cr((void *)&t24, (void *)NULL, cx, NULL, &t22, &t23);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 0, t18);
        GM_ST(gm_tbci5ospk5nnn, (uint8_t *)&(*(gm_tngtuiuskn938 *)gm_out) + 48, t24);
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
inline __attribute__((aligned(4))) __int128 gm_f_int_value_6hmh8tj(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_int_value_6hmh8tj F;
    void * gm_l0; /* m */
    unsigned __int128 gm_l1; /* bits */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_ttovhfee8mf28 gm_l3; /* info */
    GM_STACK_CHECK_SHALLOW(&F, 317u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l2 = GM_CONV(gm_t88rcucg1b7uk, a2);
    {
        void * t2 = gm_l0;
        gm_t88rcucg1b7uk t3 = gm_l2;
        gm_ttovhfee8mf28 t4 = gm_f_int_info_2r9qb5n(t2, GM_CONV(gm_csguankko4lmhd, t3));
        gm_l3 = t4;
    }
    {
        uint8_t t6 = GM_LD(uint8_t, (((uint8_t *)&gm_l3) + 0));
        if (!t6) goto ie5;
        uint32_t t7 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        uint8_t t8 = (t7 < ((uint32_t)128ULL));
        if (!t8) goto ie5;
        unsigned __int128 t9 = gm_l1;
        uint32_t t10 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
        uint32_t r11; bool o12 = __builtin_sub_overflow(t10, ((uint32_t)1ULL), &r11);
        if (o12) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 23, 58);
        uint32_t t13 = r11;
        unsigned __int128 t14 = (unsigned __int128)t13;
        unsigned __int128 n15 = (unsigned __int128)t14;
        if (n15 >= 128) gm_trap_shift((uint64_t)n15, "u128", gm_srcue4ionbe9lgh, 23, 45);
        unsigned __int128 t16 = (unsigned __int128)(t9 >> (int)n15);
        unsigned __int128 t17 = (unsigned __int128)(t16 & ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u)));
        uint8_t t18 = ((t17 == ((unsigned __int128)(((unsigned __int128)0u << 64) | 1u))));
        if (!t18) goto ie5;
        {
            unsigned __int128 t20 = gm_l1;
            uint32_t t21 = GM_LD(uint32_t, (((uint8_t *)&gm_l3) + 4));
            unsigned __int128 t22 = gm_f_mask_gbhl4hp(t21);
            unsigned __int128 t23 = (unsigned __int128)~t22;
            unsigned __int128 t24 = (unsigned __int128)(t20 | t23);
            unsigned __int128 t25 = t24;
            __int128 t26;
            memset(&t26, 0, sizeof t26); memcpy(&t26, ((uint8_t *)&t25), sizeof t26 < 16 ? sizeof t26 : 16);
            uint8_t t27 = 0;
            F.ret = t26;
            goto gm_fret;
        }
        goto ix5;
ie5:;
ix5:;
    }
    {
        unsigned __int128 t28 = gm_l1;
        unsigned __int128 t29 = t28;
        __int128 t30;
        memset(&t30, 0, sizeof t30); memcpy(&t30, ((uint8_t *)&t29), sizeof t30 < 16 ? sizeof t30 : 16);
        uint8_t t31 = 0;
        F.ret = t30;
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
extern __attribute__((aligned(4))) unsigned __int128 gm_f_mask_gbhl4hp(uint32_t a0);
__attribute__((aligned(4))) void gm_f_offsets_niooj98(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t88rcucg1b7uk a1) {
    struct gm_fr_offsets_niooj98 F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    void * gm_l2; /* s */
    uint64_t gm_l3; /* len */
    gm_t88rcucg1b7uk gm_l4; /* elem */
    uint64_t gm_l5; /* size */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    uint64_t gm_l10; /* i */
    gm_b16_8 gm_l11; /* elems */
    uint32_t gm_l12; /* id */
    gm_b16_8 gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    GM_STACK_CHECK(&F, 1394u);
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
        void * t4 = gm_l2;
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t15 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        switch (t15) { case ((uint8_t)0ULL): goto ma13; case ((uint8_t)1ULL): goto ma13; case ((uint8_t)2ULL): goto ma13; case ((uint8_t)3ULL): goto ma13; case ((uint8_t)4ULL): goto ma13; case ((uint8_t)5ULL): goto ma13; case ((uint8_t)6ULL): goto ma13; case ((uint8_t)7ULL): goto ma13; case ((uint8_t)8ULL): goto ma13; case ((uint8_t)9ULL): goto ma13; case ((uint8_t)10ULL): goto ma13; case ((uint8_t)11ULL): goto ma10; case ((uint8_t)12ULL): goto ma11; case ((uint8_t)13ULL): goto ma13; case ((uint8_t)14ULL): goto ma13; case ((uint8_t)15ULL): goto ma13; case ((uint8_t)16ULL): goto ma13; case ((uint8_t)17ULL): goto ma12; case ((uint8_t)18ULL): goto ma13; case ((uint8_t)19ULL): goto ma13; case ((uint8_t)20ULL): goto ma13; case ((uint8_t)21ULL): goto ma13; case ((uint8_t)22ULL): goto ma13; case ((uint8_t)23ULL): goto ma13; case ((uint8_t)24ULL): goto ma13; case ((uint8_t)25ULL): goto ma13; case ((uint8_t)26ULL): goto ma13; default: goto ma13; }
ma10:;
        uint8_t t17 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t17 != ((uint8_t)11ULL)) goto mn16;
        uint64_t t18 = GM_LD(uint64_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l3 = t18;
        gm_t88rcucg1b7uk t19 = GM_LD(gm_t88rcucg1b7uk, ((((uint8_t *)&t8) + 8) + 8));
        gm_l4 = t19;
        {
            void * t21 = gm_l2;
            gm_t88rcucg1b7uk t22 = gm_l4;
            gm_tbccvikg6b96e t23;
            gm_f_layout_a5ovnrm((void *)&t23, (void *)NULL, cx, NULL, t21, t22);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbccvikg6b96e t24 = t23;
            uint64_t t25 = GM_LD(uint64_t, (((uint8_t *)&t24) + 0));
            gm_l5 = t25;
        }
        {
            gm_f_list_nof7vfj((void *)&F.l6, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            gm_f_list_kl0t9m0((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                uint64_t t27 = gm_l3;
                gm_l8 = t27;
            }
            {
                for (;;) {
                    uint64_t t29 = gm_l9;
                    uint64_t t30 = gm_l8;
                    uint8_t t31 = (t29 < t30);
                    if (!t31) goto lb28;
                    {
                        uint64_t t34 = gm_l9;
                        uint64_t t35 = t34;
                        uint64_t t38 = GM_LD(uint64_t, ((uint8_t *)&t35));
                        gm_l10 = t38;
                        {
                            void * t40 = (void *)((uint8_t *)&F.l6);
                            gm_t88rcucg1b7uk t41 = gm_l4;
                            gm_tftb1odc739t5 t42 = gm_f_push_k3l9nic(t40, GM_CONV(gm_csguankko4lmhd, t41));
                            gm_tftb1odc739t5 t43 = t42;
                            if (((((uint8_t *)&t43))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t43) + 1, gm_srcue4ionbe9lgh, 816, 17);
                            gm_b0_1 t44 = GM_LD(gm_b0_1, (((uint8_t *)&t43) + 1));
                        }
                        {
                            void * t45 = (void *)((uint8_t *)&F.l7);
                            uint64_t t46 = gm_l10;
                            uint64_t t47 = gm_l5;
                            uint64_t r48; bool o49 = __builtin_mul_overflow(t46, t47, &r48);
                            if (o49) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 817, 27);
                            uint64_t t50 = r48;
                            gm_tftb1odc739t5 t51 = gm_f_push_7o6oubd(t45, t50);
                            gm_tftb1odc739t5 t52 = t51;
                            if (((((uint8_t *)&t52))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t52) + 1, gm_srcue4ionbe9lgh, 817, 17);
                            gm_b0_1 t53 = GM_LD(gm_b0_1, (((uint8_t *)&t52) + 1));
                        }
                        goto me36;
mn37:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 815, 13);
me36:;
                    }
lc28:;
                    {
                        uint64_t t55 = gm_l9;
                        uint64_t r56; bool o57 = __builtin_add_overflow(t55, ((uint64_t)1ULL), &r56);
                        uint64_t t58 = r56;
                        gm_l9 = t58;
                    }
                }
lb28:;
            }
        }
        {
            gm_t6j6smj7qoctu t59;
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
            gm_b16_8 t61 = t60;
            gm_b16_8 t62 = GM_LD(gm_b16_8, ((uint8_t *)&t61));
            GM_ST(gm_b16_8, (uint8_t *)&t59 + 0, t62);
            gm_b16_8 t63 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_b16_8 t64 = t63;
            gm_b16_8 t65 = GM_LD(gm_b16_8, ((uint8_t *)&t64));
            GM_ST(gm_b16_8, (uint8_t *)&t59 + 16, t65);
            gm_ttu09lij36f8c t66;
            GM_ST(uint8_t, (uint8_t *)&t66 + 33, 0);
            GM_ST(uint16_t, (uint8_t *)&t66 + 34, 0);
            GM_ST(uint32_t, (uint8_t *)&t66 + 36, 0);
            GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t66), t59);
            ((uint8_t *)&t66)[32] = 1;
            uint8_t t67 = 0;
            F.ret = t66;
            goto gm_fret;
        }
        goto me9;
mn16:;
ma11:;
        uint8_t t69 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t69 != ((uint8_t)12ULL)) goto mn68;
        gm_b16_8 t70 = GM_LD(gm_b16_8, ((((uint8_t *)&t8) + 8) + 0));
        gm_l11 = t70;
        gm_t6j6smj7qoctu t71;
        gm_b16_8 t72 = gm_l11;
        GM_ST(gm_b16_8, (uint8_t *)&t71 + 0, t72);
        void * t73 = gm_l2;
        gm_t88rcucg1b7uk t74 = gm_l1;
        gm_tbccvikg6b96e t75;
        gm_f_layout_a5ovnrm((void *)&t75, (void *)NULL, cx, NULL, t73, t74);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbccvikg6b96e t76 = t75;
        gm_b16_8 t77 = GM_LD(gm_b16_8, (((uint8_t *)&t76) + 16));
        GM_ST(gm_b16_8, (uint8_t *)&t71 + 16, t77);
        gm_ttu09lij36f8c t78;
        GM_ST(uint8_t, (uint8_t *)&t78 + 33, 0);
        GM_ST(uint16_t, (uint8_t *)&t78 + 34, 0);
        GM_ST(uint32_t, (uint8_t *)&t78 + 36, 0);
        GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t78), t71);
        ((uint8_t *)&t78)[32] = 1;
        uint8_t t79 = 0;
        F.ret = t78;
        goto gm_fret;
        goto me9;
mn68:;
ma12:;
        uint8_t t81 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t81 != ((uint8_t)17ULL)) goto mn80;
        uint32_t t82 = GM_LD(uint32_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l12 = t82;
        {
            uint32_t t85 = gm_l12;
            uint64_t t86 = (uint64_t)t85;
            uint64_t t87 = (uintptr_t)t86;
            void * t88 = gm_l2;
            uint64_t t89 = GM_LD(uintptr_t, ((((uint8_t *)t88) + 1088) + 0) + 8);
            if (t87 >= (uintptr_t)(t89)) gm_trap_index(t87, t89, gm_srcue4ionbe9lgh, 823, 29);
            uint8_t t90 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t88) + 1088) + 0)) + t87 * 184) + 24) + 0));
            if (t90 != ((uint8_t)0ULL)) goto ie84;
            gm_tpautr4br7h3l t91 = GM_LD(gm_tpautr4br7h3l, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t88) + 1088) + 0)) + t87 * 184) + 24) + 8));
            F.l13 = t91;
            {
                uint8_t t94;
                if ((((((uint8_t *)&F.l13) + 40))[4] != 0)) goto mn96;
                t94 = ((uint8_t)0);
                goto me95;
mn96:;
                t94 = ((uint8_t)1);
                goto me95;
mn97:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 824, 16);
me95:;
                if (!t94) goto ie93;
                {
                    gm_ttu09lij36f8c t99;
                    memset(&t99, 0, sizeof t99);
                    uint8_t t100 = 0;
                    F.ret = t99;
                    goto gm_fret;
                }
                goto ix93;
ie93:;
ix93:;
            }
            {
                gm_f_list_nof7vfj((void *)&F.l14, (void *)NULL, cx, NULL);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
            }
            {
                {
                    gm_b16_8 t102 = GM_LD(gm_b16_8, (((uint8_t *)&F.l13) + 0));
                    gm_l15 = t102;
                }
                {
                    gm_l16 = ((uint64_t)0ULL);
                }
                {
                    for (;;) {
                        uint64_t t104 = gm_l16;
                        uint64_t t105 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                        uint8_t t106 = (t104 < t105);
                        if (!t106) goto lb103;
                        {
                            uint64_t t109 = gm_l16;
                            uint64_t t110 = (uintptr_t)t109;
                            uint64_t t111 = GM_LD(uintptr_t, ((uint8_t *)&gm_l15) + 8);
                            gm_t0mglksrkfa5e t114 = GM_LD(gm_t0mglksrkfa5e, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l15)) + t110 * 40));
                            F.l17 = t114;
                            {
                                void * t116 = (void *)((uint8_t *)&F.l14);
                                gm_t88rcucg1b7uk t117 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&F.l17) + 16));
                                gm_tftb1odc739t5 t118 = gm_f_push_k3l9nic(t116, GM_CONV(gm_csguankko4lmhd, t117));
                                gm_tftb1odc739t5 t119 = t118;
                                if (((((uint8_t *)&t119))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t119) + 1, gm_srcue4ionbe9lgh, 826, 35);
                                gm_b0_1 t120 = GM_LD(gm_b0_1, (((uint8_t *)&t119) + 1));
                            }
                            goto me112;
mn113:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 826, 13);
me112:;
                        }
lc103:;
                        {
                            uint64_t t122 = gm_l16;
                            uint64_t r123; bool o124 = __builtin_add_overflow(t122, ((uint64_t)1ULL), &r123);
                            uint64_t t125 = r123;
                            gm_l16 = t125;
                        }
                    }
lb103:;
                }
            }
            {
                gm_t6j6smj7qoctu t126;
                gm_b16_8 t127 = GM_LD(gm_b16_8, (((uint8_t *)&F.l14) + 0));
                gm_b16_8 t128 = t127;
                gm_b16_8 t129 = GM_LD(gm_b16_8, ((uint8_t *)&t128));
                GM_ST(gm_b16_8, (uint8_t *)&t126 + 0, t129);
                void * t130 = gm_l2;
                gm_t88rcucg1b7uk t131 = gm_l1;
                gm_tbccvikg6b96e t132;
                gm_f_layout_a5ovnrm((void *)&t132, (void *)NULL, cx, NULL, t130, t131);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tbccvikg6b96e t133 = t132;
                gm_b16_8 t134 = GM_LD(gm_b16_8, (((uint8_t *)&t133) + 16));
                GM_ST(gm_b16_8, (uint8_t *)&t126 + 16, t134);
                gm_ttu09lij36f8c t135;
                GM_ST(uint8_t, (uint8_t *)&t135 + 33, 0);
                GM_ST(uint16_t, (uint8_t *)&t135 + 34, 0);
                GM_ST(uint32_t, (uint8_t *)&t135 + 36, 0);
                GM_ST(gm_t6j6smj7qoctu, ((uint8_t *)&t135), t126);
                ((uint8_t *)&t135)[32] = 1;
                uint8_t t136 = 0;
                F.ret = t135;
                goto gm_fret;
            }
            goto ix84;
ie84:;
            {
                gm_ttu09lij36f8c t138;
                memset(&t138, 0, sizeof t138);
                uint8_t t139 = 0;
                F.ret = t138;
                goto gm_fret;
            }
ix84:;
        }
        goto me9;
mn80:;
ma13:;
        gm_ttu09lij36f8c t141;
        memset(&t141, 0, sizeof t141);
        uint8_t t142 = 0;
        F.ret = t141;
        goto gm_fret;
        goto me9;
mn140:;
ma14:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 810, 5);
me9:;
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
inline __attribute__((aligned(4))) gm_tkn42u7na3t81 gm_f_payload_ty_mu1ii1k(void * a0, gm_csguankko4lmhd a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_payload_ty_mu1ii1k F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* i */
    uint32_t gm_l3; /* id */
    gm_b16_8 gm_l4; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 343u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t10 = GM_LD(uint32_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l3 = t10;
        {
            uint32_t t13 = gm_l3;
            uint64_t t14 = (uint64_t)t13;
            uint64_t t15 = (uintptr_t)t14;
            void * t16 = gm_l0;
            void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
            uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 1088) + 0) + 8);
            if (t15 >= (uintptr_t)(t18)) gm_trap_index(t15, t18, gm_srcue4ionbe9lgh, 755, 28);
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 0));
            if (t19 != ((uint8_t)1ULL)) goto ie12;
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 8) + 8));
            gm_l4 = t20;
            {
                uint32_t t23 = gm_l2;
                uint64_t t24 = (uint64_t)t23;
                uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t26 = (t24 >= t25);
                if (!t26) goto ie22;
                {
                    gm_tkn42u7na3t81 t28;
                    memset(&t28, 0, sizeof t28);
                    uint8_t t29 = 0;
                    F.ret = t28;
                    goto gm_fret;
                }
                goto ix22;
ie22:;
ix22:;
            }
            {
                uint32_t t30 = gm_l2;
                uint64_t t31 = (uint64_t)t30;
                uint64_t t32 = (uintptr_t)t31;
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcue4ionbe9lgh, 757, 12);
                gm_tkn42u7na3t81 t34 = GM_LD(gm_tkn42u7na3t81, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t32 * 72) + 16));
                uint8_t t35 = 0;
                F.ret = t34;
                goto gm_fret;
            }
            goto ix12;
ie12:;
            {
                gm_tkn42u7na3t81 t37;
                memset(&t37, 0, sizeof t37);
                uint8_t t38 = 0;
                F.ret = t37;
                goto gm_fret;
            }
ix12:;
        }
        goto ix2;
ie2:;
        {
            gm_tkn42u7na3t81 t40;
            memset(&t40, 0, sizeof t40);
            uint8_t t41 = 0;
            F.ret = t40;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_put_bytes_1ltl1lv(void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_b16_8 a3, uint64_t a4) {
    struct gm_fr_put_bytes_1ltl1lv F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l2; /* t */
    gm_b16_8 gm_l3; /* out */
    uint64_t gm_l4; /* at */
    unsigned __int128 gm_l5; /* bits */
    uint64_t gm_l6; /* size */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    uint64_t gm_l9; /* i */
    uint8_t gm_l10; /* b */
    double gm_l11; /* x */
    uint16_t gm_l12; /* w */
    uint64_t gm_l13; /* raw */
    uint64_t gm_l14; /* n */
    uint64_t gm_l15; /* <tmp> */
    uint64_t gm_l16; /* <tmp> */
    uint64_t gm_l17; /* i */
    void * gm_l18; /* a */
    gm_t6j6smj7qoctu gm_l19; /* o */
    gm_b16_8 gm_l20; /* <tmp> */
    uint64_t gm_l21; /* <tmp> */
    uint64_t gm_l22; /* i */
    GM_STACK_CHECK(&F, 1626u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        switch (t9) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma5; case ((uint8_t)2ULL): goto ma4; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma7; case ((uint8_t)5ULL): goto ma7; case ((uint8_t)6ULL): goto ma7; case ((uint8_t)7ULL): goto ma7; case ((uint8_t)8ULL): goto ma7; case ((uint8_t)9ULL): goto ma7; case ((uint8_t)10ULL): goto ma7; case ((uint8_t)11ULL): goto ma7; case ((uint8_t)12ULL): goto ma7; case ((uint8_t)13ULL): goto ma7; default: goto ma7; }
ma3:;
        uint8_t t11 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t11 != ((uint8_t)0ULL)) goto mn10;
        unsigned __int128 t12 = GM_LD(unsigned __int128, (((uint8_t *)a1) + 16));
        gm_l5 = t12;
        {
            void * t14 = gm_l0;
            void * t15 = GM_LD(void *, (((uint8_t *)t14) + 0));
            gm_t88rcucg1b7uk t16 = gm_l2;
            gm_tbccvikg6b96e t17;
            gm_f_layout_a5ovnrm((void *)&t17, (void *)NULL, cx, NULL, t15, t16);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbccvikg6b96e t18 = t17;
            uint64_t t19 = GM_LD(uint64_t, (((uint8_t *)&t18) + 0));
            gm_l6 = t19;
        }
        {
            {
                gm_l8 = ((uint64_t)0ULL);
            }
            {
                uint64_t t21 = gm_l6;
                gm_l7 = t21;
            }
            {
                for (;;) {
                    uint64_t t23 = gm_l8;
                    uint64_t t24 = gm_l7;
                    uint8_t t25 = (t23 < t24);
                    if (!t25) goto lb22;
                    {
                        uint64_t t28 = gm_l8;
                        uint64_t t29 = t28;
                        uint64_t t32 = GM_LD(uint64_t, ((uint8_t *)&t29));
                        gm_l9 = t32;
                        {
                            gm_b16_8 t34 = gm_l3;
                            gm_b16_8 t35 = t34;
                            uint64_t t36 = gm_l4;
                            void * t37 = gm_l0;
                            uint64_t t38 = gm_l9;
                            uint64_t t39 = gm_l6;
                            uint64_t t40 = gm_f_byte_at_m94bj3e(t37, t38, t39);
                            uint64_t r41; bool o42 = __builtin_add_overflow(t36, t40, &r41);
                            if (o42) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 843, 37);
                            uint64_t t43 = r41;
                            uint64_t t44 = (uint64_t)t43;
                            uint64_t t45 = (uintptr_t)t44;
                            gm_b16_8 t46 = t35;
                            uint64_t t47 = GM_LD(uintptr_t, ((uint8_t *)&t46) + 8);
                            if (t45 >= (uintptr_t)(t47)) gm_trap_index(t45, t47, gm_srcue4ionbe9lgh, 843, 32);
                            unsigned __int128 t48 = gm_l5;
                            uint64_t t49 = gm_l9;
                            uint64_t r50; bool o51 = __builtin_mul_overflow(t49, ((uint64_t)8ULL), &r50);
                            if (o51) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 843, 92);
                            uint64_t t52 = r50;
                            unsigned __int128 t53 = (unsigned __int128)t52;
                            unsigned __int128 n54 = (unsigned __int128)t53;
                            if (n54 >= 128) gm_trap_shift((uint64_t)n54, "u128", gm_srcue4ionbe9lgh, 843, 79);
                            unsigned __int128 t55 = (unsigned __int128)(t48 >> (int)n54);
                            uint8_t t56 = (uint8_t)t55;
                            GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t46)) + t45 * 1), t56);
                        }
                        goto me30;
mn31:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 843, 13);
me30:;
                    }
lc22:;
                    {
                        uint64_t t58 = gm_l8;
                        uint64_t r59; bool o60 = __builtin_add_overflow(t58, ((uint64_t)1ULL), &r59);
                        uint64_t t61 = r59;
                        gm_l8 = t61;
                    }
                }
lb22:;
            }
        }
        {
            uint8_t t62 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me2;
mn10:;
ma4:;
        uint8_t t64 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t64 != ((uint8_t)2ULL)) goto mn63;
        uint8_t t65 = GM_LD(uint8_t, (((uint8_t *)a1) + 16));
        gm_l10 = t65;
        {
            gm_b16_8 t67 = gm_l3;
            gm_b16_8 t68 = t67;
            uint64_t t69 = gm_l4;
            uint64_t t70 = (uint64_t)t69;
            uint64_t t71 = (uintptr_t)t70;
            gm_b16_8 t72 = t68;
            uint64_t t73 = GM_LD(uintptr_t, ((uint8_t *)&t72) + 8);
            if (t71 >= (uintptr_t)(t73)) gm_trap_index(t71, t73, gm_srcue4ionbe9lgh, 847, 13);
            uint8_t *t74 = ((uint8_t *)GM_LD(void *, ((uint8_t *)&t72)) + t71 * 1);
            uint8_t t75;
            uint8_t t77 = gm_l10;
            if (!t77) goto ie76;
            uint8_t t79 = ((uint8_t)1ULL);
            t75 = t79;
            goto ix76;
ie76:;
            uint8_t t81 = ((uint8_t)0ULL);
            t75 = t81;
ix76:;
            GM_ST(uint8_t, t74, t75);
        }
        {
            uint8_t t82 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me2;
mn63:;
ma5:;
        uint8_t t84 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t84 != ((uint8_t)1ULL)) goto mn83;
        double t85 = GM_LD(double, (((uint8_t *)a1) + 16));
        gm_l11 = t85;
        {
            void * t88 = gm_l0;
            void * t89 = GM_LD(void *, (((uint8_t *)t88) + 0));
            void * t90 = (void *)(((uint8_t *)t89) + 8);
            gm_t88rcucg1b7uk t91 = gm_l2;
            gm_t53at80pfdus3 t92;
            gm_f_kind_jcnt4hm((void *)&t92, t90, GM_CONV(gm_csguankko4lmhd, t91));
            gm_t53at80pfdus3 t93 = t92;
            uint8_t t94 = GM_LD(uint8_t, (((uint8_t *)&t93) + 0));
            if (t94 != ((uint8_t)3ULL)) goto ie87;
            uint16_t t95 = GM_LD(uint16_t, (((uint8_t *)&t93) + 8));
            gm_l12 = t95;
            {
                gm_l13 = ((uint64_t)0ULL);
            }
            {
                gm_l14 = ((uint64_t)8ULL);
            }
            {
                uint16_t t97 = gm_l12;
                uint16_t t98 = t97;
                uint16_t t101 = GM_LD(uint16_t, ((uint8_t *)&t98));
                if (!(t101 == ((uint16_t)64ULL))) goto mn100;
                {
                    double t103 = gm_l11;
                    double t104 = t103;
                    uint64_t t105;
                    memset(&t105, 0, sizeof t105); memcpy(&t105, ((uint8_t *)&t104), sizeof t105 < 8 ? sizeof t105 : 8);
                    gm_l13 = t105;
                }
                goto me99;
mn100:;
                uint16_t t107 = GM_LD(uint16_t, ((uint8_t *)&t98));
                if (!(t107 == ((uint16_t)32ULL))) goto mn106;
                {
                    double t109 = gm_l11;
                    float t110 = (float)((double)t109);
                    float t111 = t110;
                    uint32_t t112;
                    memset(&t112, 0, sizeof t112); memcpy(&t112, ((uint8_t *)&t111), sizeof t112 < 4 ? sizeof t112 : 4);
                    uint64_t t113 = (uint64_t)t112;
                    gm_l13 = t113;
                }
                {
                    gm_l14 = ((uint64_t)4ULL);
                }
                goto me99;
mn106:;
                uint8_t t115 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
                goto me99;
mn114:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 854, 13);
me99:;
            }
            {
                {
                    gm_l16 = ((uint64_t)0ULL);
                }
                {
                    uint64_t t117 = gm_l14;
                    gm_l15 = t117;
                }
                {
                    for (;;) {
                        uint64_t t119 = gm_l16;
                        uint64_t t120 = gm_l15;
                        uint8_t t121 = (t119 < t120);
                        if (!t121) goto lb118;
                        {
                            uint64_t t124 = gm_l16;
                            uint64_t t125 = t124;
                            uint64_t t128 = GM_LD(uint64_t, ((uint8_t *)&t125));
                            gm_l17 = t128;
                            {
                                gm_b16_8 t130 = gm_l3;
                                gm_b16_8 t131 = t130;
                                uint64_t t132 = gm_l4;
                                void * t133 = gm_l0;
                                uint64_t t134 = gm_l17;
                                uint64_t t135 = gm_l14;
                                uint64_t t136 = gm_f_byte_at_m94bj3e(t133, t134, t135);
                                uint64_t r137; bool o138 = __builtin_add_overflow(t132, t136, &r137);
                                if (o138) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 862, 34);
                                uint64_t t139 = r137;
                                uint64_t t140 = (uint64_t)t139;
                                uint64_t t141 = (uintptr_t)t140;
                                gm_b16_8 t142 = t131;
                                uint64_t t143 = GM_LD(uintptr_t, ((uint8_t *)&t142) + 8);
                                if (t141 >= (uintptr_t)(t143)) gm_trap_index(t141, t143, gm_srcue4ionbe9lgh, 862, 29);
                                uint64_t t144 = gm_l13;
                                uint64_t t145 = gm_l17;
                                uint64_t r146; bool o147 = __builtin_mul_overflow(t145, ((uint64_t)8ULL), &r146);
                                if (o147) gm_trap("integer overflow in `*`", gm_srcue4ionbe9lgh, 862, 81);
                                uint64_t t148 = r146;
                                unsigned __int128 n149 = (unsigned __int128)t148;
                                if (n149 >= 64) gm_trap_shift((uint64_t)n149, "u64", gm_srcue4ionbe9lgh, 862, 73);
                                uint64_t t150 = (uint64_t)(t144 >> (int)n149);
                                uint8_t t151 = (uint8_t)t150;
                                GM_ST(uint8_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&t142)) + t141 * 1), t151);
                            }
                            goto me126;
mn127:;
                            gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 862, 13);
me126:;
                        }
lc118:;
                        {
                            uint64_t t153 = gm_l16;
                            uint64_t r154; bool o155 = __builtin_add_overflow(t153, ((uint64_t)1ULL), &r154);
                            uint64_t t156 = r154;
                            gm_l16 = t156;
                        }
                    }
lb118:;
                }
            }
            {
                uint8_t t157 = 0;
                F.ret = ((uint8_t)1);
                goto gm_fret;
            }
            goto ix87;
ie87:;
            {
                uint8_t t159 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
ix87:;
        }
        goto me2;
mn83:;
ma6:;
        uint8_t t161 = GM_LD(uint8_t, (((uint8_t *)a1) + 0));
        if (t161 != ((uint8_t)3ULL)) goto mn160;
        void * t162 = GM_LD(void *, (((uint8_t *)a1) + 16));
        gm_l18 = t162;
        {
            void * t164 = gm_l0;
            gm_t88rcucg1b7uk t165 = gm_l2;
            gm_ttu09lij36f8c t166;
            gm_f_offsets_niooj98((void *)&t166, (void *)NULL, cx, NULL, t164, t165);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_ttu09lij36f8c t167 = t166;
            gm_t6j6smj7qoctu t168;
            if (((((uint8_t *)&t167))[32] != 0)) {
                gm_t6j6smj7qoctu t169 = GM_LD(gm_t6j6smj7qoctu, (((uint8_t *)&t167) + 0));
                t168 = t169;
            } else {
                uint8_t t170 = 0;
                F.ret = ((uint8_t)0);
                goto gm_fret;
            }
            gm_l19 = t168;
        }
        {
            {
                void * t172 = gm_l18;
                gm_b16_8 t173 = GM_LD(gm_b16_8, (((uint8_t *)t172) + 0));
                gm_b16_8 t174 = t173;
                gm_b16_8 t175 = GM_LD(gm_b16_8, ((uint8_t *)&t174));
                gm_l20 = t175;
            }
            {
                gm_l21 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t177 = gm_l21;
                    uint64_t t178 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                    uint8_t t179 = (t177 < t178);
                    if (!t179) goto lb176;
                    {
                        gm_thf8as07kj3uj t182;
                        memset((uint8_t *)&t182 + 8, 0, 8);
                        uint64_t t183 = gm_l21;
                        GM_ST(uint64_t, (uint8_t *)&t182 + 0, t183);
                        uint64_t t184 = gm_l21;
                        uint64_t t185 = (uintptr_t)t184;
                        uint64_t t186 = GM_LD(uintptr_t, ((uint8_t *)&gm_l20) + 8);
                        memcpy((uint8_t *)&t182 + 16, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l20)) + t185 * 48), 48);
                        gm_thf8as07kj3uj t187 = t182;
                        uint64_t t190 = GM_LD(uint64_t, (((uint8_t *)&t187) + 0));
                        gm_l22 = t190;
                        gm_t6evimt84lndq t191 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t187) + 16));
                        F.l23 = t191;
                        {
                            void * t194 = gm_l0;
                            uint64_t t195 = gm_l22;
                            uint64_t t196 = (uintptr_t)t195;
                            uint64_t t197 = GM_LD(uintptr_t, (((uint8_t *)&gm_l19) + 0) + 8);
                            if (t196 >= (uintptr_t)(t197)) gm_trap_index(t196, t197, gm_srcue4ionbe9lgh, 868, 39);
                            gm_t88rcucg1b7uk t198 = GM_LD(gm_t88rcucg1b7uk, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l19) + 0)) + t196 * 4));
                            gm_b16_8 t199 = gm_l3;
                            uint64_t t200 = gm_l4;
                            uint64_t t201 = gm_l22;
                            uint64_t t202 = (uintptr_t)t201;
                            uint64_t t203 = GM_LD(uintptr_t, (((uint8_t *)&gm_l19) + 16) + 8);
                            if (t202 >= (uintptr_t)(t203)) gm_trap_index(t202, t203, gm_srcue4ionbe9lgh, 868, 59);
                            uint64_t t204 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)&gm_l19) + 16)) + t202 * 8));
                            uint64_t r205; bool o206 = __builtin_add_overflow(t200, t204, &r205);
                            if (o206) gm_trap("integer overflow in `+`", gm_srcue4ionbe9lgh, 868, 54);
                            uint64_t t207 = r205;
                            uint8_t t208 = gm_f_put_bytes_1ltl1lv((void *)NULL, cx, NULL, t194, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l23)), t198, t199, t207);
                            if (gm_unw.kind) {
                                goto gm_ll180;
                            }
                            uint8_t t209 = (uint8_t)!t208;
                            if (!t209) goto ie193;
                            {
                                uint8_t t211 = 0;
                                F.ret = ((uint8_t)0);
                                goto gm_fret;
                            }
                            goto ix193;
ie193:;
ix193:;
                        }
                        goto me188;
mn189:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 867, 13);
me188:;
                    }
lc176:;
                    {
                        uint64_t t213 = gm_l21;
                        uint64_t r214; bool o215 = __builtin_add_overflow(t213, ((uint64_t)1ULL), &r214);
                        uint64_t t216 = r214;
                        gm_l21 = t216;
                    }
                }
gm_ll180:;
                if (gm_unw.frame == &F.h && gm_unw.label == 2) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb176; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc176; }
                }
                goto gm_lad;
lb176:;
            }
        }
        {
            uint8_t t217 = 0;
            F.ret = ((uint8_t)1);
            goto gm_fret;
        }
        goto me2;
mn160:;
ma7:;
        uint8_t t219 = 0;
        F.ret = ((uint8_t)0);
        goto gm_fret;
        goto me2;
mn218:;
ma8:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 840, 5);
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
inline __attribute__((aligned(4))) void gm_f_reinterpret_jmr6jts(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, const void * a1, gm_t88rcucg1b7uk a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4) {
    struct gm_fr_reinterpret_jmr6jts F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l2; /* from */
    gm_t88rcucg1b7uk gm_l3; /* to */
    gm_tl874t2atnlh3 gm_l4; /* span */
    uint64_t gm_l5; /* size */
    uint64_t gm_l7; /* <tmp> */
    uint64_t gm_l8; /* <tmp> */
    GM_STACK_CHECK(&F, 1514u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l2 = a2;
    gm_l3 = a3;
    gm_l4 = a4;
    {
        void * t2 = gm_l0;
        void * t3 = GM_LD(void *, (((uint8_t *)t2) + 0));
        gm_t88rcucg1b7uk t4 = gm_l2;
        gm_tbccvikg6b96e t5;
        gm_f_layout_a5ovnrm((void *)&t5, (void *)NULL, cx, NULL, t3, t4);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tbccvikg6b96e t6 = t5;
        uint64_t t7 = GM_LD(uint64_t, (((uint8_t *)&t6) + 0));
        gm_l5 = t7;
    }
    {
        gm_f_list_uvfu1k1((void *)&F.l6, (void *)NULL, cx, NULL);
        if (gm_unw.kind) {
            goto gm_lad;
        }
    }
    {
        {
            gm_l8 = ((uint64_t)0ULL);
        }
        {
            uint64_t t9 = gm_l5;
            gm_l7 = t9;
        }
        {
            for (;;) {
                uint64_t t11 = gm_l8;
                uint64_t t12 = gm_l7;
                uint8_t t13 = (t11 < t12);
                if (!t13) goto lb10;
                {
                    uint64_t t16 = gm_l8;
                    uint64_t t17 = t16;
                    {
                        void * t21 = (void *)((uint8_t *)&F.l6);
                        gm_tftb1odc739t5 t22 = gm_f_push_t83rg2m(t21, ((uint8_t)0ULL));
                        gm_tftb1odc739t5 t23 = t22;
                        if (((((uint8_t *)&t23))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t23) + 1, gm_srcue4ionbe9lgh, 801, 24);
                        gm_b0_1 t24 = GM_LD(gm_b0_1, (((uint8_t *)&t23) + 1));
                    }
                    goto me18;
mn19:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 801, 5);
me18:;
                }
lc10:;
                {
                    uint64_t t26 = gm_l8;
                    uint64_t r27; bool o28 = __builtin_add_overflow(t26, ((uint64_t)1ULL), &r27);
                    uint64_t t29 = r27;
                    gm_l8 = t29;
                }
            }
lb10:;
        }
    }
    {
        void * t31 = gm_l0;
        gm_t88rcucg1b7uk t32 = gm_l2;
        gm_b16_8 t33 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        uint8_t t34 = gm_f_put_bytes_1ltl1lv((void *)NULL, cx, NULL, t31, &(*(gm_t6evimt84lndq *)((uint8_t *)a1)), t32, t33, ((uint64_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        uint8_t t35 = (uint8_t)!t34;
        if (!t35) goto ie30;
        {
            void * t37 = gm_l0;
            gm_tl874t2atnlh3 t38 = gm_l4;
            gm_b16_8 t39;
            GM_ST(void *, (uint8_t *)&t39, (void *)(gm_s94n39ltlvmhe));
            GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(35));
            gm_t39uai1ajk3qv t40;
            gm_f_unsupported_2seq49l((void *)&t40, t37, GM_CONV(gm_cseahvg8v7p0vs, t38), t39);
            gm_ttk92ufj2f6eh t41;
            GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t41 + 8, 0);
            ((uint8_t *)&t41)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t41 + 16), t40);
            gm_ttk92ufj2f6eh t42 = t41;
            uint8_t t43 = ((((uint8_t *)&t42))[0] != 0);
            F.ret = t41;
            goto gm_fret;
        }
        goto ix30;
ie30:;
ix30:;
    }
    {
        void * t44 = gm_l0;
        gm_t88rcucg1b7uk t45 = gm_l3;
        gm_b16_8 t46 = GM_LD(gm_b16_8, (((uint8_t *)&F.l6) + 0));
        gm_b16_8 t47 = t46;
        gm_b16_8 t48 = GM_LD(gm_b16_8, ((uint8_t *)&t47));
        gm_tjoi8o798ne9k t49;
        gm_f_get_bytes_ql9o4k0((void *)&t49, (void *)NULL, cx, NULL, t44, t45, t48, ((uint64_t)0ULL));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tjoi8o798ne9k t50 = t49;
        gm_t6evimt84lndq t51;
        if (((((uint8_t *)&t50))[48] != 0)) {
            gm_t6evimt84lndq t52 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t50) + 0));
            t51 = t52;
        } else {
            void * t53 = gm_l0;
            gm_tl874t2atnlh3 t54 = gm_l4;
            gm_b16_8 t55;
            GM_ST(void *, (uint8_t *)&t55, (void *)(gm_sbjuojbqo51av));
            GM_ST(uintptr_t, (uint8_t *)&t55 + 8, (uintptr_t)(35));
            gm_t39uai1ajk3qv t56;
            gm_f_unsupported_2seq49l((void *)&t56, t53, GM_CONV(gm_cseahvg8v7p0vs, t54), t55);
            gm_ttk92ufj2f6eh t57;
            GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t57 + 8, 0);
            ((uint8_t *)&t57)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t57 + 16), t56);
            gm_ttk92ufj2f6eh t58 = t57;
            uint8_t t59 = ((((uint8_t *)&t58))[0] != 0);
            F.ret = t57;
            goto gm_fret;
        }
        F.l9 = t51;
    }
    {
        gm_t6evimt84lndq t60 = F.l9;
        gm_ttk92ufj2f6eh t61;
        memset(&t61, 0, sizeof t61);
        ((uint8_t *)&t61)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t61 + 16), t60);
        gm_ttk92ufj2f6eh t62 = t61;
        uint8_t t63 = ((((uint8_t *)&t62))[0] != 0);
        F.ret = t61;
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
inline __attribute__((aligned(4))) void gm_f_round_float_saju9fu(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, double a1, gm_t88rcucg1b7uk a2) {
    struct gm_fr_round_float_saju9fu F;
    void * gm_l0; /* m */
    double gm_l1; /* x */
    gm_t88rcucg1b7uk gm_l2; /* t */
    uint16_t gm_l3; /* bits */
    GM_STACK_CHECK(&F, 2014u);
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
        gm_t88rcucg1b7uk t6 = gm_l2;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)3ULL)) goto ie2;
        uint16_t t10 = GM_LD(uint16_t, (((uint8_t *)&t8) + 8));
        gm_l3 = t10;
        {
            uint16_t t13 = gm_l3;
            uint8_t t14 = ((t13 == ((uint16_t)32ULL)));
            if (!t14) goto ie12;
            {
                double t16 = gm_l1;
                float t17 = (float)((double)t16);
                double t18 = (double)t17;
                gm_t26jhpkpc0sda t19;
                memset(&t19, 0, sizeof t19);
                ((uint8_t *)&t19)[0] = 0;
                GM_ST(double, ((uint8_t *)&t19 + 16), t18);
                gm_t26jhpkpc0sda t20 = t19;
                uint8_t t21 = ((((uint8_t *)&t20))[0] != 0);
                F.ret = t19;
                goto gm_fret;
            }
            goto ix12;
ie12:;
ix12:;
        }
        {
            uint16_t t23 = gm_l3;
            uint8_t t24 = ((t23 == ((uint16_t)16ULL)));
            if (!t24) goto ie22;
            {
                double t27 = gm_l1;
                double t28 = gm_l1;
                double t29 = (t27 - t28);
                uint8_t t30 = (!(t29 == ((double)0e0)));
                if (!t30) goto ie26;
                {
                    double t32 = gm_l1;
                    gm_t26jhpkpc0sda t33;
                    memset(&t33, 0, sizeof t33);
                    ((uint8_t *)&t33)[0] = 0;
                    GM_ST(double, ((uint8_t *)&t33 + 16), t32);
                    gm_t26jhpkpc0sda t34 = t33;
                    uint8_t t35 = ((((uint8_t *)&t34))[0] != 0);
                    F.ret = t33;
                    goto gm_fret;
                }
                goto ix26;
ie26:;
ix26:;
            }
            {
                double t36 = gm_l1;
                gm_tvegkb6ouf3m0 t37;
                gm_f_float_exact_1g17c6c((void *)&t37, (void *)NULL, cx, NULL, t36);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_tvegkb6ouf3m0 t38 = t37;
                gm_tngtuiuskn938 t39;
                if (((((uint8_t *)&t38))[96] != 0)) {
                    gm_tngtuiuskn938 t40 = GM_LD(gm_tngtuiuskn938, (((uint8_t *)&t38) + 0));
                    t39 = t40;
                } else {
                    double t41 = gm_l1;
                    gm_t26jhpkpc0sda t42;
                    memset(&t42, 0, sizeof t42);
                    ((uint8_t *)&t42)[0] = 0;
                    GM_ST(double, ((uint8_t *)&t42 + 16), t41);
                    gm_t26jhpkpc0sda t43 = t42;
                    uint8_t t44 = ((((uint8_t *)&t43))[0] != 0);
                    F.ret = t42;
                    goto gm_fret;
                }
                F.l4 = t39;
            }
            {
                double t45 = gm_f_to_f16_0pcu0pq((void *)NULL, cx, NULL, &(*(gm_tngtuiuskn938 *)((uint8_t *)&F.l4)));
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t26jhpkpc0sda t46;
                memset(&t46, 0, sizeof t46);
                ((uint8_t *)&t46)[0] = 0;
                GM_ST(double, ((uint8_t *)&t46 + 16), t45);
                gm_t26jhpkpc0sda t47 = t46;
                uint8_t t48 = ((((uint8_t *)&t47))[0] != 0);
                F.ret = t46;
                goto gm_fret;
            }
            goto ix22;
ie22:;
ix22:;
        }
        {
            double t49 = gm_l1;
            gm_t26jhpkpc0sda t50;
            memset(&t50, 0, sizeof t50);
            ((uint8_t *)&t50)[0] = 0;
            GM_ST(double, ((uint8_t *)&t50 + 16), t49);
            gm_t26jhpkpc0sda t51 = t50;
            uint8_t t52 = ((((uint8_t *)&t51))[0] != 0);
            F.ret = t50;
            goto gm_fret;
        }
        goto ix2;
ie2:;
        {
            double t54 = gm_l1;
            gm_t26jhpkpc0sda t55;
            memset(&t55, 0, sizeof t55);
            ((uint8_t *)&t55)[0] = 0;
            GM_ST(double, ((uint8_t *)&t55 + 16), t54);
            gm_t26jhpkpc0sda t56 = t55;
            uint8_t t57 = ((((uint8_t *)&t56))[0] != 0);
            F.ret = t55;
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
inline __attribute__((aligned(4))) uint8_t gm_f_same_place_1r7qrqk(const void * a0, const void * a1) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_same_place_1r7qrqk F;
    gm_tkaapcii86udk gm_l0; /* a */
    gm_tkaapcii86udk gm_l1; /* b */
    gm_b16_8 gm_l2; /* <tmp> */
    uint64_t gm_l3; /* <tmp> */
    uint64_t gm_l4; /* i */
    uint64_t gm_l5; /* x */
    GM_STACK_CHECK_SHALLOW(&F, 306u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    {
        uint8_t t3 = 0;
        void * t4 = GM_LD(void *, (((uint8_t *)a0) + 0));
        void * t5 = GM_LD(void *, (((uint8_t *)a1) + 0));
        uint8_t t6 = (!(t4 == t5));
        if (!t6) {
            uint64_t t7 = GM_LD(uintptr_t, (((uint8_t *)a0) + 8) + 8);
            uint64_t t8 = GM_LD(uintptr_t, (((uint8_t *)a1) + 8) + 8);
            uint8_t t9 = (!(t7 == t8));
            t3 = t9;
        } else t3 = 1;
        if (!t3) goto ie2;
        {
            uint8_t t11 = 0;
            F.ret = ((uint8_t)0);
            goto gm_fret;
        }
        goto ix2;
ie2:;
ix2:;
    }
    {
        {
            gm_b16_8 t13 = GM_LD(gm_b16_8, (((uint8_t *)a0) + 8));
            gm_l2 = t13;
        }
        {
            gm_l3 = ((uint64_t)0ULL);
        }
        {
            for (;;) {
                uint64_t t15 = gm_l3;
                uint64_t t16 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                uint8_t t17 = (t15 < t16);
                if (!t17) goto lb14;
                {
                    uint64_t t20 = gm_l3;
                    uint64_t t21 = gm_l3;
                    uint64_t t22 = (uintptr_t)t21;
                    uint64_t t23 = GM_LD(uintptr_t, ((uint8_t *)&gm_l2) + 8);
                    uint64_t t24 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l2)) + t22 * 8));
                    gm_t2lqu8jsj511d t25;
                    GM_ST(uint64_t, ((uint8_t *)&t25 + 0), t20);
                    GM_ST(uint64_t, ((uint8_t *)&t25 + 8), t24);
                    gm_t2lqu8jsj511d t26 = t25;
                    uint64_t t29 = GM_LD(uint64_t, (((uint8_t *)&t26) + 0));
                    gm_l4 = t29;
                    uint64_t t30 = GM_LD(uint64_t, (((uint8_t *)&t26) + 8));
                    gm_l5 = t30;
                    {
                        uint64_t t33 = gm_l5;
                        uint64_t t34 = gm_l4;
                        uint64_t t35 = (uintptr_t)t34;
                        uint64_t t36 = GM_LD(uintptr_t, (((uint8_t *)a1) + 8) + 8);
                        if (t35 >= (uintptr_t)(t36)) gm_trap_index(t35, t36, gm_srcue4ionbe9lgh, 727, 21);
                        uint64_t t37 = GM_LD(uint64_t, ((uint8_t *)GM_LD(void *, (((uint8_t *)a1) + 8)) + t35 * 8));
                        uint8_t t38 = (!(t33 == t37));
                        if (!t38) goto ie32;
                        {
                            uint8_t t40 = 0;
                            F.ret = ((uint8_t)0);
                            goto gm_fret;
                        }
                        goto ix32;
ie32:;
ix32:;
                    }
                    goto me27;
mn28:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 726, 5);
me27:;
                }
lc14:;
                {
                    uint64_t t42 = gm_l3;
                    uint64_t r43; bool o44 = __builtin_add_overflow(t42, ((uint64_t)1ULL), &r43);
                    uint64_t t45 = r43;
                    gm_l3 = t45;
                }
            }
lb14:;
        }
    }
    {
        uint8_t t46 = 0;
        F.ret = ((uint8_t)1);
        goto gm_fret;
    }
gm_fret:
    return F.ret;
gm_fprop:
    return F.ret;
}
__attribute__((aligned(4))) uint8_t gm_f_same_place_1r7qrqk_c(gm_tkaapcii86udk a0, gm_tkaapcii86udk a1) { return gm_f_same_place_1r7qrqk(&a0, &a1); }
__attribute__((aligned(4))) void gm_f_scalar_binary_bvc65kt(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_ts6oqppjh0ba3 a1, const void * a2, const void * a3, gm_t88rcucg1b7uk a4, gm_t88rcucg1b7uk a5, gm_tl874t2atnlh3 a6) {
    struct gm_fr_scalar_binary_bvc65kt F;
    void * gm_l0; /* m */
    gm_ts6oqppjh0ba3 gm_l1; /* op */
    gm_t88rcucg1b7uk gm_l4; /* lt */
    gm_t88rcucg1b7uk gm_l5; /* rt */
    gm_tl874t2atnlh3 gm_l6; /* span */
    unsigned __int128 gm_l7; /* x */
    unsigned __int128 gm_l8; /* y */
    int32_t gm_l9; /* c */
    double gm_l10; /* x */
    double gm_l11; /* y */
    double gm_l12; /* r */
    uint8_t gm_l13; /* x */
    uint8_t gm_l14; /* y */
    GM_STACK_CHECK(&F, 7362u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    memcpy(&F.l3, a3, sizeof F.l3);
    gm_l4 = a4;
    gm_l5 = a5;
    gm_l6 = a6;
    {
        uint8_t t8 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        switch (t8) { case ((uint8_t)0ULL): goto ma3; case ((uint8_t)1ULL): goto ma4; case ((uint8_t)2ULL): goto ma5; case ((uint8_t)3ULL): goto ma6; case ((uint8_t)4ULL): goto ma6; case ((uint8_t)5ULL): goto ma6; case ((uint8_t)6ULL): goto ma6; case ((uint8_t)7ULL): goto ma6; case ((uint8_t)8ULL): goto ma6; case ((uint8_t)9ULL): goto ma6; case ((uint8_t)10ULL): goto ma6; case ((uint8_t)11ULL): goto ma6; case ((uint8_t)12ULL): goto ma6; case ((uint8_t)13ULL): goto ma6; default: goto ma6; }
ma3:;
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t10 != ((uint8_t)0ULL)) goto mn9;
        unsigned __int128 t11 = GM_LD(unsigned __int128, (((uint8_t *)a2) + 16));
        gm_l7 = t11;
        {
            uint8_t t14 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t14 != ((uint8_t)0ULL)) goto ie13;
            unsigned __int128 t15 = GM_LD(unsigned __int128, (((uint8_t *)&F.l3) + 16));
            gm_l8 = t15;
            {
                uint8_t t24 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                switch (t24) { case ((uint8_t)0ULL): goto ma22; case ((uint8_t)1ULL): goto ma22; case ((uint8_t)2ULL): goto ma22; case ((uint8_t)3ULL): goto ma22; case ((uint8_t)4ULL): goto ma22; case ((uint8_t)5ULL): goto ma22; case ((uint8_t)6ULL): goto ma22; case ((uint8_t)7ULL): goto ma19; case ((uint8_t)8ULL): goto ma20; case ((uint8_t)9ULL): goto ma21; case ((uint8_t)10ULL): goto ma18; case ((uint8_t)11ULL): goto ma18; case ((uint8_t)12ULL): goto ma18; case ((uint8_t)13ULL): goto ma18; default: goto ma22; }
ma18:;
                uint8_t t28 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t28 != ((uint8_t)10ULL)) goto pa27;
                goto po26;
pa27:;
                uint8_t t30 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t30 != ((uint8_t)11ULL)) goto pa29;
                goto po26;
pa29:;
                uint8_t t32 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t32 != ((uint8_t)12ULL)) goto pa31;
                goto po26;
pa31:;
                uint8_t t34 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t34 != ((uint8_t)13ULL)) goto pa33;
                goto po26;
pa33:;
                goto mn25;
po26:;
                {
                    void * t36 = gm_l0;
                    unsigned __int128 t37 = gm_l7;
                    gm_t88rcucg1b7uk t38 = gm_l4;
                    gm_tbci5ospk5nnn t39;
                    gm_f_big_of_1cq0qao((void *)&t39, (void *)NULL, cx, NULL, t36, t37, t38);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    void * t40 = gm_l0;
                    unsigned __int128 t41 = gm_l8;
                    gm_t88rcucg1b7uk t42 = gm_l5;
                    gm_tbci5ospk5nnn t43;
                    gm_f_big_of_1cq0qao((void *)&t43, (void *)NULL, cx, NULL, t40, t41, t42);
                    if (gm_unw.kind) {
                        goto gm_lad;
                    }
                    int32_t t44 = gm_f_cmp_i1knrgu(&t39, &t43);
                    gm_l9 = t44;
                }
                {
                    uint8_t t45;
                    uint8_t t52 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    switch (t52) { case ((uint8_t)0ULL): goto ma50; case ((uint8_t)1ULL): goto ma50; case ((uint8_t)2ULL): goto ma50; case ((uint8_t)3ULL): goto ma50; case ((uint8_t)4ULL): goto ma50; case ((uint8_t)5ULL): goto ma50; case ((uint8_t)6ULL): goto ma50; case ((uint8_t)7ULL): goto ma50; case ((uint8_t)8ULL): goto ma50; case ((uint8_t)9ULL): goto ma50; case ((uint8_t)10ULL): goto ma47; case ((uint8_t)11ULL): goto ma48; case ((uint8_t)12ULL): goto ma49; case ((uint8_t)13ULL): goto ma50; default: goto ma50; }
ma47:;
                    uint8_t t54 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    if (t54 != ((uint8_t)10ULL)) goto mn53;
                    int32_t t55 = gm_l9;
                    uint8_t t56 = (t55 < ((int32_t)0LL));
                    t45 = t56;
                    goto me46;
mn53:;
ma48:;
                    uint8_t t58 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    if (t58 != ((uint8_t)11ULL)) goto mn57;
                    int32_t t59 = gm_l9;
                    uint8_t t60 = (t59 <= ((int32_t)0LL));
                    t45 = t60;
                    goto me46;
mn57:;
ma49:;
                    uint8_t t62 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                    if (t62 != ((uint8_t)12ULL)) goto mn61;
                    int32_t t63 = gm_l9;
                    uint8_t t64 = (t63 > ((int32_t)0LL));
                    t45 = t64;
                    goto me46;
mn61:;
ma50:;
                    int32_t t66 = gm_l9;
                    uint8_t t67 = (t66 >= ((int32_t)0LL));
                    t45 = t67;
                    goto me46;
mn65:;
ma51:;
                    gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 129, 34);
me46:;
                    gm_t6evimt84lndq t68;
                    memset(&t68, 0, sizeof t68);
                    GM_ST(uint8_t, ((uint8_t *)&t68 + 0), ((uint8_t)2ULL));
                    GM_ST(uint8_t, ((uint8_t *)&t68 + 16), t45);
                    gm_ttk92ufj2f6eh t69;
                    memset(&t69, 0, sizeof t69);
                    ((uint8_t *)&t69)[0] = 0;
                    GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t69 + 16), t68);
                    gm_ttk92ufj2f6eh t70 = t69;
                    uint8_t t71 = ((((uint8_t *)&t70))[0] != 0);
                    F.ret = t69;
                    goto gm_fret;
                }
                goto me17;
mn25:;
ma19:;
                uint8_t t73 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t73 != ((uint8_t)7ULL)) goto mn72;
                unsigned __int128 t74 = gm_l7;
                unsigned __int128 t75 = gm_l8;
                unsigned __int128 t76 = (unsigned __int128)(t74 & t75);
                gm_t6evimt84lndq t77;
                GM_ST(uint8_t, (uint8_t *)&t77 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t77 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t77 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t77 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t77 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t77 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t77 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t77 + 16), t76);
                gm_ttk92ufj2f6eh t78;
                memset(&t78, 0, sizeof t78);
                ((uint8_t *)&t78)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t78 + 16), t77);
                gm_ttk92ufj2f6eh t79 = t78;
                uint8_t t80 = ((((uint8_t *)&t79))[0] != 0);
                F.ret = t78;
                goto gm_fret;
                goto me17;
mn72:;
ma20:;
                uint8_t t82 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t82 != ((uint8_t)8ULL)) goto mn81;
                unsigned __int128 t83 = gm_l7;
                unsigned __int128 t84 = gm_l8;
                unsigned __int128 t85 = (unsigned __int128)(t83 | t84);
                gm_t6evimt84lndq t86;
                GM_ST(uint8_t, (uint8_t *)&t86 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t86 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t86 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t86 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t86 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t86 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t86 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t86 + 16), t85);
                gm_ttk92ufj2f6eh t87;
                memset(&t87, 0, sizeof t87);
                ((uint8_t *)&t87)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t87 + 16), t86);
                gm_ttk92ufj2f6eh t88 = t87;
                uint8_t t89 = ((((uint8_t *)&t88))[0] != 0);
                F.ret = t87;
                goto gm_fret;
                goto me17;
mn81:;
ma21:;
                uint8_t t91 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t91 != ((uint8_t)9ULL)) goto mn90;
                unsigned __int128 t92 = gm_l7;
                unsigned __int128 t93 = gm_l8;
                unsigned __int128 t94 = (unsigned __int128)(t92 ^ t93);
                gm_t6evimt84lndq t95;
                GM_ST(uint8_t, (uint8_t *)&t95 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t95 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t95 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t95 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t95 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t95 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t95 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t95 + 16), t94);
                gm_ttk92ufj2f6eh t96;
                memset(&t96, 0, sizeof t96);
                ((uint8_t *)&t96)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t96 + 16), t95);
                gm_ttk92ufj2f6eh t97 = t96;
                uint8_t t98 = ((((uint8_t *)&t97))[0] != 0);
                F.ret = t96;
                goto gm_fret;
                goto me17;
mn90:;
ma22:;
                void * t100 = gm_l0;
                gm_ts6oqppjh0ba3 t101 = gm_l1;
                unsigned __int128 t102 = gm_l7;
                unsigned __int128 t103 = gm_l8;
                gm_t88rcucg1b7uk t104 = gm_l4;
                gm_t88rcucg1b7uk t105 = gm_l5;
                gm_tl874t2atnlh3 t106 = gm_l6;
                gm_t8carr7mku484 t107;
                gm_f_int_op_jnafd2a((void *)&t107, (void *)NULL, cx, NULL, t100, t101, t102, t103, t104, t105, t106);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t8carr7mku484 t108 = t107;
                if (((((uint8_t *)&t108))[0] != 0)) {
                    gm_t39uai1ajk3qv t109 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t108) + 16));
                    gm_ttk92ufj2f6eh t110;
                    GM_ST(uint8_t, (uint8_t *)&t110 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t110 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t110 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t110 + 8, 0);
                    ((uint8_t *)&t110)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t110 + 16), t109);
                    F.ret = t110;
                    goto gm_fret;
                }
                unsigned __int128 t111 = GM_LD(unsigned __int128, (((uint8_t *)&t108) + 16));
                gm_t6evimt84lndq t112;
                GM_ST(uint8_t, (uint8_t *)&t112 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t112 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t112 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t112 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t112 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t112 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t112 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t112 + 16), t111);
                gm_ttk92ufj2f6eh t113;
                memset(&t113, 0, sizeof t113);
                ((uint8_t *)&t113)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t113 + 16), t112);
                gm_ttk92ufj2f6eh t114 = t113;
                uint8_t t115 = ((((uint8_t *)&t114))[0] != 0);
                F.ret = t113;
                goto gm_fret;
                goto me17;
mn99:;
ma23:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 126, 13);
me17:;
            }
            goto ix13;
ie13:;
            {
                void * t117 = gm_l0;
                gm_tl874t2atnlh3 t118 = gm_l6;
                gm_b16_8 t119;
                GM_ST(void *, (uint8_t *)&t119, (void *)(gm_sqe4aodms9tfs));
                GM_ST(uintptr_t, (uint8_t *)&t119 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t120;
                gm_f_unsupported_2seq49l((void *)&t120, t117, GM_CONV(gm_cseahvg8v7p0vs, t118), t119);
                gm_ttk92ufj2f6eh t121;
                GM_ST(uint8_t, (uint8_t *)&t121 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t121 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t121 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t121 + 8, 0);
                ((uint8_t *)&t121)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t121 + 16), t120);
                gm_ttk92ufj2f6eh t122 = t121;
                uint8_t t123 = ((((uint8_t *)&t122))[0] != 0);
                F.ret = t121;
                goto gm_fret;
            }
ix13:;
        }
        goto me2;
mn9:;
ma4:;
        uint8_t t125 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t125 != ((uint8_t)1ULL)) goto mn124;
        double t126 = GM_LD(double, (((uint8_t *)a2) + 16));
        gm_l10 = t126;
        {
            uint8_t t129 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t129 != ((uint8_t)1ULL)) goto ie128;
            double t130 = GM_LD(double, (((uint8_t *)&F.l3) + 16));
            gm_l11 = t130;
            {
                double t132;
                uint8_t t145 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                switch (t145) { case ((uint8_t)0ULL): goto ma134; case ((uint8_t)1ULL): goto ma135; case ((uint8_t)2ULL): goto ma136; case ((uint8_t)3ULL): goto ma143; case ((uint8_t)4ULL): goto ma137; case ((uint8_t)5ULL): goto ma138; case ((uint8_t)6ULL): goto ma143; case ((uint8_t)7ULL): goto ma143; case ((uint8_t)8ULL): goto ma143; case ((uint8_t)9ULL): goto ma143; case ((uint8_t)10ULL): goto ma139; case ((uint8_t)11ULL): goto ma140; case ((uint8_t)12ULL): goto ma141; case ((uint8_t)13ULL): goto ma142; default: goto ma143; }
ma134:;
                uint8_t t147 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t147 != ((uint8_t)0ULL)) goto mn146;
                double t148 = gm_l10;
                double t149 = gm_l11;
                double t150 = (t148 + t149);
                t132 = t150;
                goto me133;
mn146:;
ma135:;
                uint8_t t152 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t152 != ((uint8_t)1ULL)) goto mn151;
                double t153 = gm_l10;
                double t154 = gm_l11;
                double t155 = (t153 - t154);
                t132 = t155;
                goto me133;
mn151:;
ma136:;
                uint8_t t157 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t157 != ((uint8_t)2ULL)) goto mn156;
                double t158 = gm_l10;
                double t159 = gm_l11;
                double t160 = (t158 * t159);
                t132 = t160;
                goto me133;
mn156:;
ma137:;
                uint8_t t162 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t162 != ((uint8_t)4ULL)) goto mn161;
                double t163 = gm_l10;
                double t164 = gm_l11;
                double t165 = (t163 / t164);
                t132 = t165;
                goto me133;
mn161:;
ma138:;
                uint8_t t167 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t167 != ((uint8_t)5ULL)) goto mn166;
                double t168 = gm_l10;
                double t169 = gm_l11;
                double t170 = gm_f_float_rem_24loign((void *)NULL, cx, NULL, t168, t169);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                t132 = t170;
                goto me133;
mn166:;
ma139:;
                uint8_t t172 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t172 != ((uint8_t)10ULL)) goto mn171;
                double t173 = gm_l10;
                double t174 = gm_l11;
                uint8_t t175 = (t173 < t174);
                gm_t6evimt84lndq t176;
                memset(&t176, 0, sizeof t176);
                GM_ST(uint8_t, ((uint8_t *)&t176 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t176 + 16), t175);
                gm_ttk92ufj2f6eh t177;
                memset(&t177, 0, sizeof t177);
                ((uint8_t *)&t177)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t177 + 16), t176);
                gm_ttk92ufj2f6eh t178 = t177;
                uint8_t t179 = ((((uint8_t *)&t178))[0] != 0);
                F.ret = t177;
                goto gm_fret;
                goto me133;
mn171:;
ma140:;
                uint8_t t181 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t181 != ((uint8_t)11ULL)) goto mn180;
                double t182 = gm_l10;
                double t183 = gm_l11;
                uint8_t t184 = (t182 <= t183);
                gm_t6evimt84lndq t185;
                memset(&t185, 0, sizeof t185);
                GM_ST(uint8_t, ((uint8_t *)&t185 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t185 + 16), t184);
                gm_ttk92ufj2f6eh t186;
                memset(&t186, 0, sizeof t186);
                ((uint8_t *)&t186)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t186 + 16), t185);
                gm_ttk92ufj2f6eh t187 = t186;
                uint8_t t188 = ((((uint8_t *)&t187))[0] != 0);
                F.ret = t186;
                goto gm_fret;
                goto me133;
mn180:;
ma141:;
                uint8_t t190 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t190 != ((uint8_t)12ULL)) goto mn189;
                double t191 = gm_l10;
                double t192 = gm_l11;
                uint8_t t193 = (t191 > t192);
                gm_t6evimt84lndq t194;
                memset(&t194, 0, sizeof t194);
                GM_ST(uint8_t, ((uint8_t *)&t194 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t194 + 16), t193);
                gm_ttk92ufj2f6eh t195;
                memset(&t195, 0, sizeof t195);
                ((uint8_t *)&t195)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t195 + 16), t194);
                gm_ttk92ufj2f6eh t196 = t195;
                uint8_t t197 = ((((uint8_t *)&t196))[0] != 0);
                F.ret = t195;
                goto gm_fret;
                goto me133;
mn189:;
ma142:;
                uint8_t t199 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t199 != ((uint8_t)13ULL)) goto mn198;
                double t200 = gm_l10;
                double t201 = gm_l11;
                uint8_t t202 = (t200 >= t201);
                gm_t6evimt84lndq t203;
                memset(&t203, 0, sizeof t203);
                GM_ST(uint8_t, ((uint8_t *)&t203 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t203 + 16), t202);
                gm_ttk92ufj2f6eh t204;
                memset(&t204, 0, sizeof t204);
                ((uint8_t *)&t204)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t204 + 16), t203);
                gm_ttk92ufj2f6eh t205 = t204;
                uint8_t t206 = ((((uint8_t *)&t205))[0] != 0);
                F.ret = t204;
                goto gm_fret;
                goto me133;
mn198:;
ma143:;
                void * t208 = gm_l0;
                gm_tl874t2atnlh3 t209 = gm_l6;
                gm_b16_8 t210;
                GM_ST(void *, (uint8_t *)&t210, (void *)(gm_sllgvbfukep8p));
                GM_ST(uintptr_t, (uint8_t *)&t210 + 8, (uintptr_t)(23));
                gm_t39uai1ajk3qv t211;
                gm_f_unsupported_2seq49l((void *)&t211, t208, GM_CONV(gm_cseahvg8v7p0vs, t209), t210);
                gm_ttk92ufj2f6eh t212;
                GM_ST(uint8_t, (uint8_t *)&t212 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t212 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t212 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t212 + 8, 0);
                ((uint8_t *)&t212)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t212 + 16), t211);
                gm_ttk92ufj2f6eh t213 = t212;
                uint8_t t214 = ((((uint8_t *)&t213))[0] != 0);
                F.ret = t212;
                goto gm_fret;
                goto me133;
mn207:;
ma144:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 144, 18);
me133:;
                gm_l12 = t132;
            }
            {
                void * t215 = gm_l0;
                double t216 = gm_l12;
                gm_t88rcucg1b7uk t217 = gm_l4;
                gm_t26jhpkpc0sda t218;
                gm_f_round_float_saju9fu((void *)&t218, (void *)NULL, cx, NULL, t215, t216, t217);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t26jhpkpc0sda t219 = t218;
                if (((((uint8_t *)&t219))[0] != 0)) {
                    gm_t39uai1ajk3qv t220 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t219) + 16));
                    gm_ttk92ufj2f6eh t221;
                    GM_ST(uint8_t, (uint8_t *)&t221 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t221 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t221 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t221 + 8, 0);
                    ((uint8_t *)&t221)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t221 + 16), t220);
                    F.ret = t221;
                    goto gm_fret;
                }
                double t222 = GM_LD(double, (((uint8_t *)&t219) + 16));
                gm_t6evimt84lndq t223;
                memset(&t223, 0, sizeof t223);
                GM_ST(uint8_t, ((uint8_t *)&t223 + 0), ((uint8_t)1ULL));
                GM_ST(double, ((uint8_t *)&t223 + 16), t222);
                gm_ttk92ufj2f6eh t224;
                memset(&t224, 0, sizeof t224);
                ((uint8_t *)&t224)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t224 + 16), t223);
                gm_ttk92ufj2f6eh t225 = t224;
                uint8_t t226 = ((((uint8_t *)&t225))[0] != 0);
                F.ret = t224;
                goto gm_fret;
            }
            goto ix128;
ie128:;
            {
                void * t228 = gm_l0;
                gm_tl874t2atnlh3 t229 = gm_l6;
                gm_b16_8 t230;
                GM_ST(void *, (uint8_t *)&t230, (void *)(gm_sqe4aodms9tfs));
                GM_ST(uintptr_t, (uint8_t *)&t230 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t231;
                gm_f_unsupported_2seq49l((void *)&t231, t228, GM_CONV(gm_cseahvg8v7p0vs, t229), t230);
                gm_ttk92ufj2f6eh t232;
                GM_ST(uint8_t, (uint8_t *)&t232 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t232 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t232 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t232 + 8, 0);
                ((uint8_t *)&t232)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t232 + 16), t231);
                gm_ttk92ufj2f6eh t233 = t232;
                uint8_t t234 = ((((uint8_t *)&t233))[0] != 0);
                F.ret = t232;
                goto gm_fret;
            }
ix128:;
        }
        goto me2;
mn124:;
ma5:;
        uint8_t t236 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t236 != ((uint8_t)2ULL)) goto mn235;
        uint8_t t237 = GM_LD(uint8_t, (((uint8_t *)a2) + 16));
        gm_l13 = t237;
        {
            uint8_t t240 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 0));
            if (t240 != ((uint8_t)2ULL)) goto ie239;
            uint8_t t241 = GM_LD(uint8_t, (((uint8_t *)&F.l3) + 16));
            gm_l14 = t241;
            {
                uint8_t t249 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                switch (t249) { case ((uint8_t)0ULL): goto ma247; case ((uint8_t)1ULL): goto ma247; case ((uint8_t)2ULL): goto ma247; case ((uint8_t)3ULL): goto ma247; case ((uint8_t)4ULL): goto ma247; case ((uint8_t)5ULL): goto ma247; case ((uint8_t)6ULL): goto ma247; case ((uint8_t)7ULL): goto ma244; case ((uint8_t)8ULL): goto ma245; case ((uint8_t)9ULL): goto ma246; case ((uint8_t)10ULL): goto ma247; case ((uint8_t)11ULL): goto ma247; case ((uint8_t)12ULL): goto ma247; case ((uint8_t)13ULL): goto ma247; default: goto ma247; }
ma244:;
                uint8_t t251 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t251 != ((uint8_t)7ULL)) goto mn250;
                uint8_t t252 = 0;
                uint8_t t253 = gm_l13;
                if (t253) {
                    uint8_t t254 = gm_l14;
                    t252 = t254;
                } else t252 = 0;
                gm_t6evimt84lndq t255;
                memset(&t255, 0, sizeof t255);
                GM_ST(uint8_t, ((uint8_t *)&t255 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t255 + 16), t252);
                gm_ttk92ufj2f6eh t256;
                memset(&t256, 0, sizeof t256);
                ((uint8_t *)&t256)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t256 + 16), t255);
                gm_ttk92ufj2f6eh t257 = t256;
                uint8_t t258 = ((((uint8_t *)&t257))[0] != 0);
                F.ret = t256;
                goto gm_fret;
                goto me243;
mn250:;
ma245:;
                uint8_t t260 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t260 != ((uint8_t)8ULL)) goto mn259;
                uint8_t t261 = 0;
                uint8_t t262 = gm_l13;
                if (!t262) {
                    uint8_t t263 = gm_l14;
                    t261 = t263;
                } else t261 = 1;
                gm_t6evimt84lndq t264;
                memset(&t264, 0, sizeof t264);
                GM_ST(uint8_t, ((uint8_t *)&t264 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t264 + 16), t261);
                gm_ttk92ufj2f6eh t265;
                memset(&t265, 0, sizeof t265);
                ((uint8_t *)&t265)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t265 + 16), t264);
                gm_ttk92ufj2f6eh t266 = t265;
                uint8_t t267 = ((((uint8_t *)&t266))[0] != 0);
                F.ret = t265;
                goto gm_fret;
                goto me243;
mn259:;
ma246:;
                uint8_t t269 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
                if (t269 != ((uint8_t)9ULL)) goto mn268;
                uint8_t t270 = gm_l13;
                uint8_t t271 = gm_l14;
                uint8_t t272 = (!(t270 == t271));
                gm_t6evimt84lndq t273;
                memset(&t273, 0, sizeof t273);
                GM_ST(uint8_t, ((uint8_t *)&t273 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t273 + 16), t272);
                gm_ttk92ufj2f6eh t274;
                memset(&t274, 0, sizeof t274);
                ((uint8_t *)&t274)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t274 + 16), t273);
                gm_ttk92ufj2f6eh t275 = t274;
                uint8_t t276 = ((((uint8_t *)&t275))[0] != 0);
                F.ret = t274;
                goto gm_fret;
                goto me243;
mn268:;
ma247:;
                void * t278 = gm_l0;
                gm_tl874t2atnlh3 t279 = gm_l6;
                gm_b16_8 t280;
                GM_ST(void *, (uint8_t *)&t280, (void *)(gm_sbsu8rcurukea));
                GM_ST(uintptr_t, (uint8_t *)&t280 + 8, (uintptr_t)(24));
                gm_t39uai1ajk3qv t281;
                gm_f_unsupported_2seq49l((void *)&t281, t278, GM_CONV(gm_cseahvg8v7p0vs, t279), t280);
                gm_ttk92ufj2f6eh t282;
                GM_ST(uint8_t, (uint8_t *)&t282 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t282 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t282 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t282 + 8, 0);
                ((uint8_t *)&t282)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t282 + 16), t281);
                gm_ttk92ufj2f6eh t283 = t282;
                uint8_t t284 = ((((uint8_t *)&t283))[0] != 0);
                F.ret = t282;
                goto gm_fret;
                goto me243;
mn277:;
ma248:;
                gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 160, 13);
me243:;
            }
            goto ix239;
ie239:;
            {
                void * t286 = gm_l0;
                gm_tl874t2atnlh3 t287 = gm_l6;
                gm_b16_8 t288;
                GM_ST(void *, (uint8_t *)&t288, (void *)(gm_sqe4aodms9tfs));
                GM_ST(uintptr_t, (uint8_t *)&t288 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t289;
                gm_f_unsupported_2seq49l((void *)&t289, t286, GM_CONV(gm_cseahvg8v7p0vs, t287), t288);
                gm_ttk92ufj2f6eh t290;
                GM_ST(uint8_t, (uint8_t *)&t290 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t290 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t290 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t290 + 8, 0);
                ((uint8_t *)&t290)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t290 + 16), t289);
                gm_ttk92ufj2f6eh t291 = t290;
                uint8_t t292 = ((((uint8_t *)&t291))[0] != 0);
                F.ret = t290;
                goto gm_fret;
            }
ix239:;
        }
        goto me2;
mn235:;
ma6:;
        void * t294 = gm_l0;
        gm_tl874t2atnlh3 t295 = gm_l6;
        gm_b16_8 t296;
        GM_ST(void *, (uint8_t *)&t296, (void *)(gm_s0dggtblbl7jq));
        GM_ST(uintptr_t, (uint8_t *)&t296 + 8, (uintptr_t)(29));
        gm_t39uai1ajk3qv t297;
        gm_f_unsupported_2seq49l((void *)&t297, t294, GM_CONV(gm_cseahvg8v7p0vs, t295), t296);
        gm_ttk92ufj2f6eh t298;
        GM_ST(uint8_t, (uint8_t *)&t298 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t298 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t298 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t298 + 8, 0);
        ((uint8_t *)&t298)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t298 + 16), t297);
        gm_ttk92ufj2f6eh t299 = t298;
        uint8_t t300 = ((((uint8_t *)&t299))[0] != 0);
        F.ret = t298;
        goto gm_fret;
        goto me2;
mn293:;
ma7:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 123, 5);
me2:;
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
__attribute__((aligned(4))) void gm_f_unary_19d8qg7(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, void * a2, void * a3) {
    struct gm_fr_unary_19d8qg7 F;
    void * gm_l0; /* m */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    void * gm_l2; /* x */
    void * gm_l3; /* e */
    void * gm_l5; /* a */
    gm_t88rcucg1b7uk gm_l6; /* lane */
    gm_b16_8 gm_l8; /* <tmp> */
    uint64_t gm_l9; /* <tmp> */
    GM_STACK_CHECK(&F, 1766u);
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
        gm_ttk92ufj2f6eh t4;
        gm_f_eval_2vbtp1v((void *)&t4, (void *)NULL, cx, NULL, t2, t3);
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_ttk92ufj2f6eh t5 = t4;
        if (((((uint8_t *)&t5))[0] != 0)) {
            gm_t39uai1ajk3qv t6 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t5) + 16));
            gm_ttk92ufj2f6eh t7;
            GM_ST(uint8_t, (uint8_t *)&t7 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t7 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t7 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t7 + 8, 0);
            ((uint8_t *)&t7)[0] = 1;
            GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t7 + 16), t6);
            F.ret = t7;
            goto gm_fret;
        }
        gm_t6evimt84lndq t8 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t5) + 16));
        F.l4 = t8;
    }
    {
        uint8_t t10 = GM_LD(uint8_t, (((uint8_t *)&F.l4) + 0));
        if (t10 != ((uint8_t)3ULL)) goto ie9;
        void * t11 = GM_LD(void *, (((uint8_t *)&F.l4) + 16));
        gm_l5 = t11;
        {
            void * t13 = gm_l0;
            void * t14 = GM_LD(void *, (((uint8_t *)t13) + 0));
            void * t15 = (void *)(((uint8_t *)t14) + 8);
            void * t16 = gm_l3;
            gm_t88rcucg1b7uk t17 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t16) + 0));
            gm_tb7u19loo3j87 t18;
            gm_f_vector_parts_crrfopm((void *)&t18, t15, GM_CONV(gm_csguankko4lmhd, t17));
            gm_tb7u19loo3j87 t19 = t18;
            if (!((((uint8_t *)&t19))[16] != 0)) gm_trap("unwrap of null", gm_srcue4ionbe9lgh, 74, 17);
            gm_tlcm4lvu3g1a8 t20 = GM_LD(gm_tlcm4lvu3g1a8, ((uint8_t *)&t19));
            gm_tlcm4lvu3g1a8 t21 = t20;
            gm_t88rcucg1b7uk t22 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)&t21) + 8));
            gm_l6 = t22;
        }
        {
            gm_f_list_0202sdh((void *)&F.l7, (void *)NULL, cx, NULL);
            if (gm_unw.kind) {
                goto gm_lad;
            }
        }
        {
            {
                void * t24 = gm_l5;
                gm_b16_8 t25 = GM_LD(gm_b16_8, (((uint8_t *)t24) + 0));
                gm_l8 = t25;
            }
            {
                gm_l9 = ((uint64_t)0ULL);
            }
            {
                for (;;) {
                    uint64_t t27 = gm_l9;
                    uint64_t t28 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                    uint8_t t29 = (t27 < t28);
                    if (!t29) goto lb26;
                    {
                        uint64_t t32 = gm_l9;
                        uint64_t t33 = (uintptr_t)t32;
                        uint64_t t34 = GM_LD(uintptr_t, ((uint8_t *)&gm_l8) + 8);
                        gm_t6evimt84lndq t37 = GM_LD(gm_t6evimt84lndq, ((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l8)) + t33 * 48));
                        F.l10 = t37;
                        {
                            void * t39 = (void *)((uint8_t *)&F.l7);
                            void * t40 = gm_l0;
                            gm_t9qkh0rsooc8d t41 = gm_l1;
                            gm_t88rcucg1b7uk t42 = gm_l6;
                            void * t43 = gm_l2;
                            gm_tl874t2atnlh3 t44 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t43) + 72));
                            void * t45 = gm_l3;
                            gm_tl874t2atnlh3 t46 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t45) + 72));
                            gm_ttk92ufj2f6eh t47;
                            gm_f_unary_value_vi7cs6u((void *)&t47, (void *)NULL, cx, NULL, t40, t41, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l10)), t42, t44, t46);
                            if (gm_unw.kind) {
                                goto gm_ll30;
                            }
                            gm_ttk92ufj2f6eh t48 = t47;
                            if (((((uint8_t *)&t48))[0] != 0)) {
                                gm_t39uai1ajk3qv t49 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t48) + 16));
                                gm_ttk92ufj2f6eh t50;
                                GM_ST(uint8_t, (uint8_t *)&t50 + 1, 0);
                                GM_ST(uint16_t, (uint8_t *)&t50 + 2, 0);
                                GM_ST(uint32_t, (uint8_t *)&t50 + 4, 0);
                                GM_ST(uint64_t, (uint8_t *)&t50 + 8, 0);
                                ((uint8_t *)&t50)[0] = 1;
                                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t50 + 16), t49);
                                F.ret = t50;
                                goto gm_fret;
                            }
                            gm_t6evimt84lndq t51 = GM_LD(gm_t6evimt84lndq, (((uint8_t *)&t48) + 16));
                            gm_tftb1odc739t5 t52 = gm_f_push_0pgacsj(t39, &t51);
                            gm_tftb1odc739t5 t53 = t52;
                            if (((((uint8_t *)&t53))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t53) + 1, gm_srcue4ionbe9lgh, 76, 29);
                            gm_b0_1 t54 = GM_LD(gm_b0_1, (((uint8_t *)&t53) + 1));
                        }
                        goto me35;
mn36:;
                        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 76, 9);
me35:;
                    }
lc26:;
                    {
                        uint64_t t56 = gm_l9;
                        uint64_t r57; bool o58 = __builtin_add_overflow(t56, ((uint64_t)1ULL), &r57);
                        uint64_t t59 = r57;
                        gm_l9 = t59;
                    }
                }
gm_ll30:;
                if (gm_unw.frame == &F.h && gm_unw.label == 0) {
                    if (gm_unw.kind == GM_BREAK) { gm_unw.kind = GM_NONE; goto lb26; }
                    if (gm_unw.kind == GM_CONTINUE) { gm_unw.kind = GM_NONE; goto lc26; }
                }
                goto gm_lad;
lb26:;
            }
        }
        {
            gm_b16_8 t60 = GM_LD(gm_b16_8, (((uint8_t *)&F.l7) + 0));
            gm_tnpifv20irnph t61;
            GM_ST(gm_b16_8, ((uint8_t *)&t61 + 0), t60);
            gm_tq7s98ihonkju t62 = gm_f_new_lcff781((void *)NULL, cx, NULL, t61, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ktbnd3fuoao)));
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tq7s98ihonkju t63 = t62;
            if (((((uint8_t *)&t63))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t63) + 8, gm_srcue4ionbe9lgh, 77, 21);
            void * t64 = GM_LD(void *, (((uint8_t *)&t63) + 8));
            gm_t6evimt84lndq t65;
            memset(&t65, 0, sizeof t65);
            GM_ST(uint8_t, ((uint8_t *)&t65 + 0), ((uint8_t)3ULL));
            GM_ST(void *, ((uint8_t *)&t65 + 16), t64);
            gm_ttk92ufj2f6eh t66;
            memset(&t66, 0, sizeof t66);
            ((uint8_t *)&t66)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t66 + 16), t65);
            gm_ttk92ufj2f6eh t67 = t66;
            uint8_t t68 = ((((uint8_t *)&t67))[0] != 0);
            F.ret = t66;
            goto gm_fret;
        }
        goto ix9;
ie9:;
ix9:;
    }
    {
        void * t69 = gm_l0;
        gm_t9qkh0rsooc8d t70 = gm_l1;
        void * t71 = gm_l2;
        gm_t88rcucg1b7uk t72 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t71) + 0));
        void * t73 = gm_l2;
        gm_tl874t2atnlh3 t74 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t73) + 72));
        void * t75 = gm_l3;
        gm_tl874t2atnlh3 t76 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t75) + 72));
        gm_f_unary_value_vi7cs6u((void *)&(*(gm_ttk92ufj2f6eh *)gm_out), (void *)NULL, cx, NULL, t69, t70, &(*(gm_t6evimt84lndq *)((uint8_t *)&F.l4)), t72, t74, t76);
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
__attribute__((aligned(4))) void gm_f_unary_value_vi7cs6u(void *gm_out, void *env, gm_node *cx, gm_node *inst, void * a0, gm_t9qkh0rsooc8d a1, const void * a2, gm_t88rcucg1b7uk a3, gm_tl874t2atnlh3 a4, gm_tl874t2atnlh3 a5) {
    struct gm_fr_unary_value_vi7cs6u F;
    void * gm_l0; /* m */
    gm_t9qkh0rsooc8d gm_l1; /* op */
    gm_t88rcucg1b7uk gm_l3; /* xt */
    gm_tl874t2atnlh3 gm_l4; /* xspan */
    gm_tl874t2atnlh3 gm_l5; /* span */
    void * gm_l6; /* x */
    void * gm_l7; /* e */
    uint8_t gm_l8; /* b */
    unsigned __int128 gm_l9; /* bits */
    double gm_l10; /* f */
    unsigned __int128 gm_l11; /* bits */
    GM_STACK_CHECK(&F, 4357u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = a1;
    gm_l3 = a3;
    gm_l4 = a4;
    gm_l5 = a5;
    {
        gm_t69m56meqefs8 t2;
        memset((uint8_t *)&t2 + 4, 0, 4);
        memset((uint8_t *)&t2 + 84, 0, 4);
        gm_t88rcucg1b7uk t3 = gm_l3;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t2 + 0, t3);
        gm_teti411eujvf4 t4;
        memset(&t4, 0, sizeof t4);
        GM_ST(uint8_t, ((uint8_t *)&t4 + 0), ((uint8_t)7ULL));
        GM_ST(gm_teti411eujvf4, (uint8_t *)&t2 + 8, t4);
        gm_tl874t2atnlh3 t5 = gm_l4;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t2 + 72, t5);
        gm_tq7s98ihonkju t6 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &t2, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_kinrtga7ov4)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t7 = t6;
        if (((((uint8_t *)&t7))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t7) + 8, gm_srcue4ionbe9lgh, 84, 10);
        void * t8 = GM_LD(void *, (((uint8_t *)&t7) + 8));
        gm_l6 = t8;
    }
    {
        gm_t69m56meqefs8 t9;
        memset((uint8_t *)&t9 + 4, 0, 4);
        memset((uint8_t *)&t9 + 84, 0, 4);
        gm_t88rcucg1b7uk t10 = gm_l3;
        GM_ST(gm_t88rcucg1b7uk, (uint8_t *)&t9 + 0, t10);
        gm_teti411eujvf4 t11;
        memset(&t11, 0, sizeof t11);
        GM_ST(uint8_t, ((uint8_t *)&t11 + 0), ((uint8_t)7ULL));
        GM_ST(gm_teti411eujvf4, (uint8_t *)&t9 + 8, t11);
        gm_tl874t2atnlh3 t12 = gm_l5;
        GM_ST(gm_tl874t2atnlh3, (uint8_t *)&t9 + 72, t12);
        gm_tq7s98ihonkju t13 = gm_f_new_ve8bb01_2((void *)NULL, cx, NULL, &t9, &(*(gm_tb40qn7hk0tl9 *)((uint8_t *)gm_ksrlu8s6v53)));
        if (gm_unw.kind) {
            goto gm_lad;
        }
        gm_tq7s98ihonkju t14 = t13;
        if (((((uint8_t *)&t14))[0] != 0)) gm_trap_unwrap(&gm_tyo86ks9qkvt, ((uint8_t *)&t14) + 8, gm_srcue4ionbe9lgh, 85, 10);
        void * t15 = GM_LD(void *, (((uint8_t *)&t14) + 8));
        gm_l7 = t15;
    }
    {
        uint8_t t21 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        switch (t21) { case ((uint8_t)0ULL): goto ma19; case ((uint8_t)1ULL): goto ma18; case ((uint8_t)2ULL): goto ma17; default: goto ma20; }
ma17:;
        uint8_t t23 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t23 != ((uint8_t)2ULL)) goto mn22;
        {
            uint8_t t26 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t26 != ((uint8_t)2ULL)) goto ie25;
            uint8_t t27 = GM_LD(uint8_t, (((uint8_t *)a2) + 16));
            gm_l8 = t27;
            {
                uint8_t t29 = gm_l8;
                uint8_t t30 = (uint8_t)!t29;
                gm_t6evimt84lndq t31;
                memset(&t31, 0, sizeof t31);
                GM_ST(uint8_t, ((uint8_t *)&t31 + 0), ((uint8_t)2ULL));
                GM_ST(uint8_t, ((uint8_t *)&t31 + 16), t30);
                gm_ttk92ufj2f6eh t32;
                memset(&t32, 0, sizeof t32);
                ((uint8_t *)&t32)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t32 + 16), t31);
                gm_ttk92ufj2f6eh t33 = t32;
                uint8_t t34 = ((((uint8_t *)&t33))[0] != 0);
                F.ret = t32;
                goto gm_fret;
            }
            goto ix25;
ie25:;
            {
                void * t36 = gm_l0;
                void * t37 = gm_l7;
                gm_tl874t2atnlh3 t38 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t37) + 72));
                gm_b16_8 t39;
                GM_ST(void *, (uint8_t *)&t39, (void *)(gm_sqe4aodms9tfs));
                GM_ST(uintptr_t, (uint8_t *)&t39 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t40;
                gm_f_unsupported_2seq49l((void *)&t40, t36, GM_CONV(gm_cseahvg8v7p0vs, t38), t39);
                gm_ttk92ufj2f6eh t41;
                GM_ST(uint8_t, (uint8_t *)&t41 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t41 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t41 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t41 + 8, 0);
                ((uint8_t *)&t41)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t41 + 16), t40);
                gm_ttk92ufj2f6eh t42 = t41;
                uint8_t t43 = ((((uint8_t *)&t42))[0] != 0);
                F.ret = t41;
                goto gm_fret;
            }
ix25:;
        }
        goto me16;
mn22:;
ma18:;
        uint8_t t45 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t45 != ((uint8_t)1ULL)) goto mn44;
        {
            uint8_t t48 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
            if (t48 != ((uint8_t)0ULL)) goto ie47;
            unsigned __int128 t49 = GM_LD(unsigned __int128, (((uint8_t *)a2) + 16));
            gm_l9 = t49;
            {
                void * t51 = gm_l0;
                unsigned __int128 t52 = gm_l9;
                unsigned __int128 t53 = (unsigned __int128)~t52;
                void * t54 = gm_l7;
                gm_t88rcucg1b7uk t55 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t54) + 0));
                unsigned __int128 t56 = gm_f_wrap_bt8r149(t51, t53, GM_CONV(gm_csguankko4lmhd, t55));
                gm_t6evimt84lndq t57;
                GM_ST(uint8_t, (uint8_t *)&t57 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t57 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t57 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t57 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t57 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t57 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t57 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t57 + 16), t56);
                gm_ttk92ufj2f6eh t58;
                memset(&t58, 0, sizeof t58);
                ((uint8_t *)&t58)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t58 + 16), t57);
                gm_ttk92ufj2f6eh t59 = t58;
                uint8_t t60 = ((((uint8_t *)&t59))[0] != 0);
                F.ret = t58;
                goto gm_fret;
            }
            goto ix47;
ie47:;
            {
                void * t62 = gm_l0;
                void * t63 = gm_l7;
                gm_tl874t2atnlh3 t64 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t63) + 72));
                gm_b16_8 t65;
                GM_ST(void *, (uint8_t *)&t65, (void *)(gm_sqe4aodms9tfs));
                GM_ST(uintptr_t, (uint8_t *)&t65 + 8, (uintptr_t)(13));
                gm_t39uai1ajk3qv t66;
                gm_f_unsupported_2seq49l((void *)&t66, t62, GM_CONV(gm_cseahvg8v7p0vs, t64), t65);
                gm_ttk92ufj2f6eh t67;
                GM_ST(uint8_t, (uint8_t *)&t67 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t67 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t67 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t67 + 8, 0);
                ((uint8_t *)&t67)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t67 + 16), t66);
                gm_ttk92ufj2f6eh t68 = t67;
                uint8_t t69 = ((((uint8_t *)&t68))[0] != 0);
                F.ret = t67;
                goto gm_fret;
            }
ix47:;
        }
        goto me16;
mn44:;
ma19:;
        uint8_t t71 = GM_LD(uint8_t, (((uint8_t *)&gm_l1) + 0));
        if (t71 != ((uint8_t)0ULL)) goto mn70;
        uint8_t t77 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        switch (t77) { case ((uint8_t)0ULL): goto ma74; case ((uint8_t)1ULL): goto ma73; case ((uint8_t)2ULL): goto ma75; case ((uint8_t)3ULL): goto ma75; case ((uint8_t)4ULL): goto ma75; case ((uint8_t)5ULL): goto ma75; case ((uint8_t)6ULL): goto ma75; case ((uint8_t)7ULL): goto ma75; case ((uint8_t)8ULL): goto ma75; case ((uint8_t)9ULL): goto ma75; case ((uint8_t)10ULL): goto ma75; case ((uint8_t)11ULL): goto ma75; case ((uint8_t)12ULL): goto ma75; case ((uint8_t)13ULL): goto ma75; default: goto ma75; }
ma73:;
        uint8_t t79 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t79 != ((uint8_t)1ULL)) goto mn78;
        double t80 = GM_LD(double, (((uint8_t *)a2) + 16));
        gm_l10 = t80;
        double t81 = gm_l10;
        double t82 = -t81;
        gm_t6evimt84lndq t83;
        memset(&t83, 0, sizeof t83);
        GM_ST(uint8_t, ((uint8_t *)&t83 + 0), ((uint8_t)1ULL));
        GM_ST(double, ((uint8_t *)&t83 + 16), t82);
        gm_ttk92ufj2f6eh t84;
        memset(&t84, 0, sizeof t84);
        ((uint8_t *)&t84)[0] = 0;
        GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t84 + 16), t83);
        gm_ttk92ufj2f6eh t85 = t84;
        uint8_t t86 = ((((uint8_t *)&t85))[0] != 0);
        F.ret = t84;
        goto gm_fret;
        goto me72;
mn78:;
ma74:;
        uint8_t t88 = GM_LD(uint8_t, (((uint8_t *)a2) + 0));
        if (t88 != ((uint8_t)0ULL)) goto mn87;
        unsigned __int128 t89 = GM_LD(unsigned __int128, (((uint8_t *)a2) + 16));
        gm_l11 = t89;
        {
            void * t92 = gm_l0;
            void * t93 = gm_l7;
            gm_t88rcucg1b7uk t94 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t93) + 0));
            gm_ttovhfee8mf28 t95 = gm_f_int_info_2r9qb5n(t92, GM_CONV(gm_csguankko4lmhd, t94));
            gm_ttovhfee8mf28 t96 = t95;
            uint32_t t97 = GM_LD(uint32_t, (((uint8_t *)&t96) + 4));
            uint8_t t98 = (t97 <= ((uint32_t)63ULL));
            if (!t98) goto ie91;
            {
                void * t100 = gm_l0;
                void * t101 = gm_l0;
                unsigned __int128 t102 = gm_l11;
                void * t103 = gm_l6;
                gm_t88rcucg1b7uk t104 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t103) + 0));
                __int128 t105 = gm_f_int_value_6hmh8tj(t101, t102, GM_CONV(gm_csguankko4lmhd, t104));
                if (t105 == ((__int128)(((unsigned __int128)9223372036854775808u << 64) | 0u))) gm_trap("integer overflow in `-`", gm_srcue4ionbe9lgh, 98, 78);
                __int128 t106 = (__int128)((unsigned __int128)0 - (unsigned __int128)t105);
                gm_t9qkh0rsooc8d t107;
                GM_ST(uint8_t, ((uint8_t *)&t107 + 0), ((uint8_t)0ULL));
                void * t108 = gm_l7;
                gm_t88rcucg1b7uk t109 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t108) + 0));
                gm_b16_8 t110;
                GM_ST(void *, (uint8_t *)&t110, (void *)(gm_skolmnfc0ccbd));
                GM_ST(uintptr_t, (uint8_t *)&t110 + 8, (uintptr_t)(1));
                void * t111 = gm_l7;
                gm_tl874t2atnlh3 t112 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t111) + 72));
                gm_t8carr7mku484 t113;
                gm_f_fit_small_7o94i8v((void *)&t113, (void *)NULL, cx, NULL, t100, t106, t107, t109, t110, t112);
                if (gm_unw.kind) {
                    goto gm_lad;
                }
                gm_t8carr7mku484 t114 = t113;
                if (((((uint8_t *)&t114))[0] != 0)) {
                    gm_t39uai1ajk3qv t115 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t114) + 16));
                    gm_ttk92ufj2f6eh t116;
                    GM_ST(uint8_t, (uint8_t *)&t116 + 1, 0);
                    GM_ST(uint16_t, (uint8_t *)&t116 + 2, 0);
                    GM_ST(uint32_t, (uint8_t *)&t116 + 4, 0);
                    GM_ST(uint64_t, (uint8_t *)&t116 + 8, 0);
                    ((uint8_t *)&t116)[0] = 1;
                    GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t116 + 16), t115);
                    F.ret = t116;
                    goto gm_fret;
                }
                unsigned __int128 t117 = GM_LD(unsigned __int128, (((uint8_t *)&t114) + 16));
                gm_t6evimt84lndq t118;
                GM_ST(uint8_t, (uint8_t *)&t118 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t118 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t118 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t118 + 8, 0);
                GM_ST(uint64_t, (uint8_t *)&t118 + 32, 0);
                GM_ST(uint64_t, (uint8_t *)&t118 + 40, 0);
                GM_ST(uint8_t, ((uint8_t *)&t118 + 0), ((uint8_t)0ULL));
                GM_ST(unsigned __int128, ((uint8_t *)&t118 + 16), t117);
                gm_ttk92ufj2f6eh t119;
                memset(&t119, 0, sizeof t119);
                ((uint8_t *)&t119)[0] = 0;
                GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t119 + 16), t118);
                gm_ttk92ufj2f6eh t120 = t119;
                uint8_t t121 = ((((uint8_t *)&t120))[0] != 0);
                F.ret = t119;
                goto gm_fret;
            }
            goto ix91;
ie91:;
ix91:;
        }
        {
            void * t122 = gm_l0;
            void * t123 = gm_l0;
            unsigned __int128 t124 = gm_l11;
            void * t125 = gm_l6;
            gm_t88rcucg1b7uk t126 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t125) + 0));
            gm_tbci5ospk5nnn t127;
            gm_f_big_of_1cq0qao((void *)&t127, (void *)NULL, cx, NULL, t123, t124, t126);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_tbci5ospk5nnn t128;
            gm_f_neg_agd5c5k((void *)&t128, (void *)NULL, cx, NULL, &t127);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t9qkh0rsooc8d t129;
            GM_ST(uint8_t, ((uint8_t *)&t129 + 0), ((uint8_t)0ULL));
            void * t130 = gm_l7;
            gm_t88rcucg1b7uk t131 = GM_LD(gm_t88rcucg1b7uk, (((uint8_t *)t130) + 0));
            gm_b16_8 t132;
            GM_ST(void *, (uint8_t *)&t132, (void *)(gm_skolmnfc0ccbd));
            GM_ST(uintptr_t, (uint8_t *)&t132 + 8, (uintptr_t)(1));
            void * t133 = gm_l7;
            gm_tl874t2atnlh3 t134 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t133) + 72));
            gm_t8carr7mku484 t135;
            gm_f_fit_8hvi4bv((void *)&t135, (void *)NULL, cx, NULL, t122, &t128, t129, t131, t132, t134);
            if (gm_unw.kind) {
                goto gm_lad;
            }
            gm_t8carr7mku484 t136 = t135;
            if (((((uint8_t *)&t136))[0] != 0)) {
                gm_t39uai1ajk3qv t137 = GM_LD(gm_t39uai1ajk3qv, (((uint8_t *)&t136) + 16));
                gm_ttk92ufj2f6eh t138;
                GM_ST(uint8_t, (uint8_t *)&t138 + 1, 0);
                GM_ST(uint16_t, (uint8_t *)&t138 + 2, 0);
                GM_ST(uint32_t, (uint8_t *)&t138 + 4, 0);
                GM_ST(uint64_t, (uint8_t *)&t138 + 8, 0);
                ((uint8_t *)&t138)[0] = 1;
                GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t138 + 16), t137);
                F.ret = t138;
                goto gm_fret;
            }
            unsigned __int128 t139 = GM_LD(unsigned __int128, (((uint8_t *)&t136) + 16));
            gm_t6evimt84lndq t140;
            GM_ST(uint8_t, (uint8_t *)&t140 + 1, 0);
            GM_ST(uint16_t, (uint8_t *)&t140 + 2, 0);
            GM_ST(uint32_t, (uint8_t *)&t140 + 4, 0);
            GM_ST(uint64_t, (uint8_t *)&t140 + 8, 0);
            GM_ST(uint64_t, (uint8_t *)&t140 + 32, 0);
            GM_ST(uint64_t, (uint8_t *)&t140 + 40, 0);
            GM_ST(uint8_t, ((uint8_t *)&t140 + 0), ((uint8_t)0ULL));
            GM_ST(unsigned __int128, ((uint8_t *)&t140 + 16), t139);
            gm_ttk92ufj2f6eh t141;
            memset(&t141, 0, sizeof t141);
            ((uint8_t *)&t141)[0] = 0;
            GM_ST(gm_t6evimt84lndq, ((uint8_t *)&t141 + 16), t140);
            gm_ttk92ufj2f6eh t142 = t141;
            uint8_t t143 = ((((uint8_t *)&t142))[0] != 0);
            F.ret = t141;
            goto gm_fret;
        }
        goto me72;
mn87:;
ma75:;
        void * t145 = gm_l0;
        void * t146 = gm_l7;
        gm_tl874t2atnlh3 t147 = GM_LD(gm_tl874t2atnlh3, (((uint8_t *)t146) + 72));
        gm_b16_8 t148;
        GM_ST(void *, (uint8_t *)&t148, (void *)(gm_sqe4aodms9tfs));
        GM_ST(uintptr_t, (uint8_t *)&t148 + 8, (uintptr_t)(13));
        gm_t39uai1ajk3qv t149;
        gm_f_unsupported_2seq49l((void *)&t149, t145, GM_CONV(gm_cseahvg8v7p0vs, t147), t148);
        gm_ttk92ufj2f6eh t150;
        GM_ST(uint8_t, (uint8_t *)&t150 + 1, 0);
        GM_ST(uint16_t, (uint8_t *)&t150 + 2, 0);
        GM_ST(uint32_t, (uint8_t *)&t150 + 4, 0);
        GM_ST(uint64_t, (uint8_t *)&t150 + 8, 0);
        ((uint8_t *)&t150)[0] = 1;
        GM_ST(gm_t39uai1ajk3qv, ((uint8_t *)&t150 + 16), t149);
        gm_ttk92ufj2f6eh t151 = t150;
        uint8_t t152 = ((((uint8_t *)&t151))[0] != 0);
        F.ret = t150;
        goto gm_fret;
        goto me72;
mn144:;
ma76:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 95, 17);
me72:;
        goto me16;
mn70:;
ma20:;
        gm_trap("no match arm matched the scrutinee", gm_srcue4ionbe9lgh, 86, 5);
me16:;
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
inline __attribute__((aligned(4))) void gm_f_variant_tag_4pbamin(void *gm_out, void * a0, gm_csguankko4lmhd a1, uint32_t a2) {
    void *env = NULL; gm_node *cx = NULL; gm_node *inst = NULL;
    struct gm_fr_variant_tag_4pbamin F;
    void * gm_l0; /* m */
    gm_t88rcucg1b7uk gm_l1; /* t */
    uint32_t gm_l2; /* i */
    uint32_t gm_l3; /* id */
    gm_b16_8 gm_l4; /* variants */
    GM_STACK_CHECK_SHALLOW(&F, 567u);
    F.h.up = (gm_frame *)env;
    F.h.clause_lit = 0 && env != NULL;
    (void)inst;
    gm_l0 = a0;
    gm_l1 = GM_CONV(gm_t88rcucg1b7uk, a1);
    gm_l2 = a2;
    {
        void * t3 = gm_l0;
        void * t4 = GM_LD(void *, (((uint8_t *)t3) + 0));
        void * t5 = (void *)(((uint8_t *)t4) + 8);
        gm_t88rcucg1b7uk t6 = gm_l1;
        gm_t53at80pfdus3 t7;
        gm_f_kind_jcnt4hm((void *)&t7, t5, GM_CONV(gm_csguankko4lmhd, t6));
        gm_t53at80pfdus3 t8 = t7;
        uint8_t t9 = GM_LD(uint8_t, (((uint8_t *)&t8) + 0));
        if (t9 != ((uint8_t)17ULL)) goto ie2;
        uint32_t t10 = GM_LD(uint32_t, ((((uint8_t *)&t8) + 8) + 0));
        gm_l3 = t10;
        {
            uint32_t t13 = gm_l3;
            uint64_t t14 = (uint64_t)t13;
            uint64_t t15 = (uintptr_t)t14;
            void * t16 = gm_l0;
            void * t17 = GM_LD(void *, (((uint8_t *)t16) + 0));
            uint64_t t18 = GM_LD(uintptr_t, ((((uint8_t *)t17) + 1088) + 0) + 8);
            if (t15 >= (uintptr_t)(t18)) gm_trap_index(t15, t18, gm_srcue4ionbe9lgh, 645, 28);
            uint8_t t19 = GM_LD(uint8_t, ((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 0));
            if (t19 != ((uint8_t)1ULL)) goto ie12;
            gm_b16_8 t20 = GM_LD(gm_b16_8, (((((uint8_t *)GM_LD(void *, ((((uint8_t *)t17) + 1088) + 0)) + t15 * 184) + 24) + 8) + 8));
            gm_l4 = t20;
            {
                uint32_t t23 = gm_l2;
                uint64_t t24 = (uint64_t)t23;
                uint64_t t25 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                uint8_t t26 = (t24 >= t25);
                if (!t26) goto ie22;
                {
                    gm_thrdpodrs8pgt t28;
                    memset(&t28, 0, sizeof t28);
                    uint8_t t29 = 0;
                    F.ret = t28;
                    goto gm_fret;
                }
                goto ix22;
ie22:;
ix22:;
            }
            {
                uint32_t t30 = gm_l2;
                uint64_t t31 = (uint64_t)t30;
                uint64_t t32 = (uintptr_t)t31;
                uint64_t t33 = GM_LD(uintptr_t, ((uint8_t *)&gm_l4) + 8);
                if (t32 >= (uintptr_t)(t33)) gm_trap_index(t32, t33, gm_srcue4ionbe9lgh, 647, 12);
                gm_tbci5ospk5nnn t34 = GM_LD(gm_tbci5ospk5nnn, (((uint8_t *)GM_LD(void *, ((uint8_t *)&gm_l4)) + t32 * 72) + 24));
                gm_thrdpodrs8pgt t35;
                GM_ST(uint8_t, (uint8_t *)&t35 + 49, 0);
                GM_ST(uint16_t, (uint8_t *)&t35 + 50, 0);
                GM_ST(uint32_t, (uint8_t *)&t35 + 52, 0);
                GM_ST(gm_tbci5ospk5nnn, ((uint8_t *)&t35), t34);
                ((uint8_t *)&t35)[48] = 1;
                uint8_t t36 = 0;
                F.ret = t35;
                goto gm_fret;
            }
            goto ix12;
ie12:;
            {
                gm_thrdpodrs8pgt t38;
                memset(&t38, 0, sizeof t38);
                uint8_t t39 = 0;
                F.ret = t38;
                goto gm_fret;
            }
ix12:;
        }
        goto ix2;
ie2:;
        {
            gm_thrdpodrs8pgt t41;
            memset(&t41, 0, sizeof t41);
            uint8_t t42 = 0;
            F.ret = t41;
            goto gm_fret;
        }
ix2:;
    }
gm_fret:
    memcpy(gm_out, &F.ret, sizeof F.ret);
gm_fdone:
    return;
gm_fprop:
    return;
}
__attribute__((aligned(4))) gm_thrdpodrs8pgt gm_f_variant_tag_4pbamin_c(void * a0, gm_csguankko4lmhd a1, uint32_t a2) { gm_thrdpodrs8pgt gm_r; gm_f_variant_tag_4pbamin(&gm_r, a0, a1, a2); return gm_r; }
extern __attribute__((aligned(4))) unsigned __int128 gm_f_wrap_bt8r149(void * a0, unsigned __int128 a1, gm_csguankko4lmhd a2);
